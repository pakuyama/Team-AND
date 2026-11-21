"""
웹캠으로 손을 인식해서 uHand 로봇 손에 손가락 각도를 보내는 스크립트.

동작 흐름:
  웹캠 -> MediaPipe Hands (손 랜드마크 21개) -> 손가락별 굽힘 정도 계산
  -> 0~180 각도로 변환 -> USB 시리얼로 micro:bit에 "엄지,검지,중지,약지,소지,손목\n" 전송

사용법:
  python hand_tracker.py --port COM3          (Windows)
  python hand_tracker.py --port /dev/ttyACM0  (Linux)
  python hand_tracker.py --port /dev/cu.usbmodem1102  (macOS)
  python hand_tracker.py --list               (연결된 포트 목록 보기)
  python hand_tracker.py --no-serial          (로봇 없이 인식만 테스트)
"""
import argparse
import math
import time

import cv2
import mediapipe as mp
import serial
import serial.tools.list_ports

# MediaPipe 랜드마크 번호: (손끝, 손가락 뿌리 관절)
# 엄지, 검지, 중지, 약지, 소지 순서
FINGERS = [(4, 2), (8, 5), (12, 9), (16, 13), (20, 17)]
FINGER_NAMES = ["Thumb", "Index", "Middle", "Ring", "Pinky"]

# 손가락별 보정 시작값 (주먹 쥐었을 때 비율, 완전히 폈을 때 비율)
# 실행 중 자동 보정으로 범위가 넓어지니 대략적인 값이면 돼요
RATIO_RANGE = [
    (0.6, 0.9),  # 엄지 (엄지 끝~소지 뿌리 거리 / 손바닥 크기)
    (0.9, 2.0),  # 검지
    (0.9, 2.1),  # 중지
    (0.9, 2.0),  # 약지
    (0.9, 1.9),  # 소지
]

SEND_INTERVAL = 0.1   # 초. 너무 자주 보내면 서보가 떨려요
SMOOTHING = 0.5       # 0~1, 클수록 이전 값을 더 유지 (떨림 감소)


def dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5


# 자동 보정: 실행 중 손을 몇 번 쥐었다 폈다 하면 손가락별 최소/최대값을 스스로 학습해요.
# (R 키를 누르면 보정을 처음부터 다시 해요)
calib = [list(r) for r in RATIO_RANGE]


def reset_calib():
    global calib
    calib = [list(r) for r in RATIO_RANGE]


def finger_ratios(landmarks):
    """손가락별 펴짐 정도(비율)를 반환."""
    wrist = landmarks[0]
    palm = dist(landmarks[9], wrist)  # 손바닥 크기 (손목~중지 뿌리)
    ratios = []
    for i, (tip, base) in enumerate(FINGERS):
        if i == 0:
            # 엄지: 엄지 끝이 소지 뿌리(17)에 얼마나 가까운지로 판단 (접으면 가까워짐)
            r = dist(landmarks[4], landmarks[17]) / palm if palm else 1.0
        else:
            base_d = dist(landmarks[base], wrist)
            r = dist(landmarks[tip], wrist) / base_d if base_d else 1.0
        ratios.append(r)
    return ratios


# 손목 회전: 손바닥이 향하는 방향(3D)으로 비틀림 각도를 계산해요.
# 처음 손을 인식한 자세를 기준(가운데)으로 잡고, 문고리 돌리듯 손을 돌리면 각도가 바뀌어요.
WRIST_RANGE = (-20.0, 20.0)   # 보정 시작값(도). 자동 보정으로 넓어져요
wrist_calib = list(WRIST_RANGE)
wrist_ref = None


def reset_wrist():
    global wrist_calib, wrist_ref
    wrist_calib = list(WRIST_RANGE)
    wrist_ref = None


def wrist_angle(world):
    """손목 비틀림을 0~180 각도로 반환 (90 = 처음 자세)."""
    global wrist_ref
    w, a, b = world[0], world[5], world[17]
    v1 = (a.x - w.x, a.y - w.y, a.z - w.z)
    v2 = (b.x - w.x, b.y - w.y, b.z - w.z)
    nx = v1[1] * v2[2] - v1[2] * v2[1]
    nz = v1[0] * v2[1] - v1[1] * v2[0]
    ang = math.degrees(math.atan2(nx, nz))
    if wrist_ref is None:
        wrist_ref = ang
    d = (ang - wrist_ref + 180) % 360 - 180   # -180~180, 기준 대비 비틀림
    lo = min(wrist_calib[0], d)
    hi = max(wrist_calib[1], d)
    wrist_calib[0], wrist_calib[1] = lo, hi
    # 가운데(0도)는 항상 90으로, 양쪽은 각자 범위로 나눠서 매핑
    if d >= 0:
        t = 0.5 + 0.5 * (d / hi if hi > 0 else 0)
    else:
        t = 0.5 - 0.5 * (d / lo if lo < 0 else 0)
    return int(max(0.0, min(1.0, t)) * 180)


def finger_angles(landmarks):
    """손가락별 각도(0=완전히 굽힘, 180=완전히 폄)를 반환."""
    angles = []
    for i, r in enumerate(finger_ratios(landmarks)):
        lo, hi = calib[i]
        lo = min(lo, r)
        hi = max(hi, r)
        calib[i] = [lo, hi]
        t = (r - lo) / (hi - lo) if hi > lo else 0.5
        # 양 끝 10%는 완전히 접힘/펴짐으로 처리해서 끝까지 움직이게
        t = (t - 0.1) / 0.8
        t = max(0.0, min(1.0, t))
        angles.append(int(t * 180))
    return angles


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("연결된 시리얼 포트가 없어요.")
    for p in ports:
        print(f"{p.device}  -  {p.description}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="micro:bit 시리얼 포트")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--camera", type=int, default=0, help="웹캠 번호")
    parser.add_argument("--list", action="store_true", help="포트 목록 출력")
    parser.add_argument("--no-serial", action="store_true", help="시리얼 없이 테스트")
    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    ser = None
    if not args.no_serial:
        if not args.port:
            print("--port 를 지정하세요. (--list 로 포트 확인)")
            return
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
        time.sleep(2)  # 연결 직후 micro:bit 리셋 대기

    hands = mp.solutions.hands.Hands(
        max_num_hands=1,
        min_detection_confidence=0.7,
        min_tracking_confidence=0.5,
    )
    drawer = mp.solutions.drawing_utils
    cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        print("웹캠을 열 수 없어요. --camera 번호를 바꿔보세요.")
        return

    smoothed = [90] * 6   # 엄지~소지 + 손목
    last_send = 0.0

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            frame = cv2.flip(frame, 1)  # 거울 모드
            res = hands.process(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))

            if res.multi_hand_landmarks:
                hand = res.multi_hand_landmarks[0]
                drawer.draw_landmarks(frame, hand, mp.solutions.hands.HAND_CONNECTIONS)
                raw = finger_angles(hand.landmark)
                if res.multi_hand_world_landmarks:
                    raw.append(wrist_angle(res.multi_hand_world_landmarks[0].landmark))
                else:
                    raw.append(smoothed[5])
                smoothed = [
                    int(SMOOTHING * s + (1 - SMOOTHING) * r)
                    for s, r in zip(smoothed, raw)
                ]

                now = time.time()
                if ser and now - last_send >= SEND_INTERVAL:
                    msg = ",".join(str(a) for a in smoothed) + "\n"
                    ser.write(msg.encode())
                    last_send = now

                for i, (name, a) in enumerate(zip(FINGER_NAMES + ["Wrist"], smoothed)):
                    cv2.putText(frame, f"{name}: {a}", (10, 30 + i * 28),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

            cv2.putText(frame, "Open/close & twist hand to calibrate (R: reset)",
                        (10, frame.shape[0] - 15), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)
            cv2.imshow("uHand Tracker (ESC to quit)", frame)
            key = cv2.waitKey(1) & 0xFF
            if key == 27:
                break
            if key in (ord("r"), ord("R")):
                reset_calib()
                reset_wrist()
    finally:
        cap.release()
        cv2.destroyAllWindows()
        if ser:
            ser.close()


if __name__ == "__main__":
    main()
