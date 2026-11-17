"""
웹캠으로 손을 인식해서 uHand 로봇 손에 손가락 각도를 보내는 스크립트.

동작 흐름:
  웹캠 -> MediaPipe Hands (손 랜드마크 21개) -> 손가락별 굽힘 정도 계산
  -> 0~180 각도로 변환 -> USB 시리얼로 micro:bit에 "a,b,c,d,e\n" 전송

사용법:
  python hand_tracker.py --port COM3          (Windows)
  python hand_tracker.py --port /dev/ttyACM0  (Linux)
  python hand_tracker.py --port /dev/cu.usbmodem1102  (macOS)
  python hand_tracker.py --list               (연결된 포트 목록 보기)
  python hand_tracker.py --no-serial          (로봇 없이 인식만 테스트)
"""
import argparse
import time

import cv2
import mediapipe as mp
import serial
import serial.tools.list_ports

# MediaPipe 랜드마크 번호: (손끝, 손가락 뿌리 관절)
# 엄지, 검지, 중지, 약지, 소지 순서
FINGERS = [(4, 2), (8, 5), (12, 9), (16, 13), (20, 17)]
FINGER_NAMES = ["Thumb", "Index", "Middle", "Ring", "Pinky"]

# 손끝-손목 거리 / 뿌리관절-손목 거리 비율의 보정값
# (주먹 쥐었을 때 비율, 완전히 폈을 때 비율) - 본인 손에 맞게 조정하세요
RATIO_RANGE = [
    (0.9, 1.5),  # 엄지
    (0.9, 2.0),  # 검지
    (0.9, 2.1),  # 중지
    (0.9, 2.0),  # 약지
    (0.9, 1.9),  # 소지
]

SEND_INTERVAL = 0.1   # 초. 너무 자주 보내면 서보가 떨려요
SMOOTHING = 0.5       # 0~1, 클수록 이전 값을 더 유지 (떨림 감소)


def dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5


def finger_angles(landmarks):
    """손가락별 각도(0=완전히 굽힘, 180=완전히 폄)를 반환."""
    wrist = landmarks[0]
    angles = []
    for (tip, base), (closed, opened) in zip(FINGERS, RATIO_RANGE):
        base_d = dist(landmarks[base], wrist)
        if base_d == 0:
            angles.append(90)
            continue
        ratio = dist(landmarks[tip], wrist) / base_d
        t = (ratio - closed) / (opened - closed)
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

    smoothed = [90] * 5
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
                smoothed = [
                    int(SMOOTHING * s + (1 - SMOOTHING) * r)
                    for s, r in zip(smoothed, raw)
                ]

                now = time.time()
                if ser and now - last_send >= SEND_INTERVAL:
                    msg = ",".join(str(a) for a in smoothed) + "\n"
                    ser.write(msg.encode())
                    last_send = now

                for i, (name, a) in enumerate(zip(FINGER_NAMES, smoothed)):
                    cv2.putText(frame, f"{name}: {a}", (10, 30 + i * 28),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

            cv2.imshow("uHand Tracker (ESC to quit)", frame)
            if cv2.waitKey(1) & 0xFF == 27:
                break
    finally:
        cap.release()
        cv2.destroyAllWindows()
        if ser:
            ser.close()


if __name__ == "__main__":
    main()
