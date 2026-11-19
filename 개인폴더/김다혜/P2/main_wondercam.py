# micro:bit (MicroPython) — 손목 카메라(WonderCam)로 손 모양을 인식해서 uHand가 따라 하기
# WonderCam: I2C(주소 0x32), "특징 학습(Feature Learning)" 기능 사용
# 로봇 보드: P12(TX)/P8(RX) 시리얼로 서보 명령 (main_hiwonder.py와 같은 방식)
# PC 없이 micro:bit 혼자 동작해요.
from microbit import *

CAM = 0x32
FUNC_FEATURE_LEARNING = 4

# 카메라에 학습시킨 ID별 손 모양 (각도: 0=접힘, 180=펴짐 / 엄지, 검지, 중지, 약지, 소지)
POSES = {
    1: [0, 0, 0, 0, 0],            # ID 1 = 주먹 ✊
    2: [180, 180, 180, 180, 180],  # ID 2 = 보 ✋
    3: [0, 180, 180, 0, 0],        # ID 3 = 가위 ✌️
}
ICONS = {1: Image.SQUARE_SMALL, 2: Image.SQUARE, 3: Image.SCISSORS}

MIN_CONFIDENCE = 0.6   # 이 값보다 확신이 낮으면 무시
STABLE_COUNT = 3       # 같은 결과가 연속 몇 번 나와야 손 모양을 바꿀지 (떨림 방지)

# 로봇 손 설정 (main_hiwonder.py와 동일)
SERVO_IDS = [1, 2, 3, 4, 5]
REVERSE = [True, False, False, False, False]
PULSE_MIN = [800, 900, 900, 900, 900]
PULSE_MAX = [2200, 2100, 2100, 2100, 2100]
MOVE_TIME = 300   # ms, 손 모양 바꿀 때 걸리는 시간


def cam_read(reg, n):
    i2c.write(CAM, bytes([reg & 0xff, reg >> 8]))
    return i2c.read(CAM, n)


def cam_write(reg, value):
    i2c.write(CAM, bytes([reg & 0xff, reg >> 8, value & 0xff]))


def cam_init():
    while True:
        try:
            if cam_read(0x0000, 1)[0] == ord("v"):
                break
        except OSError:
            pass
        display.show("E")   # 카메라를 못 찾으면 E 표시
        sleep(500)
    display.clear()
    if cam_read(0x0035, 1)[0] != FUNC_FEATURE_LEARNING:
        cam_write(0x0035, FUNC_FEATURE_LEARNING)
        for _ in range(40):
            sleep(100)
            if cam_read(0x0035, 1)[0] == FUNC_FEATURE_LEARNING:
                break


def cam_result():
    """(가장 확실한 ID, 확신도 0~1) 반환. 실패하면 (0, 0)."""
    try:
        if cam_read(0x0035, 1)[0] != FUNC_FEATURE_LEARNING:
            return 0, 0
        b = cam_read(0x0E00, 4)
        return b[1], (b[2] | (b[3] << 8)) / 10000
    except OSError:
        return 0, 0


def move_hand(angles):
    n = len(SERVO_IDS)
    frame = bytearray([0x55, 0x55, n * 3 + 5, 0x03, n,
                       MOVE_TIME & 0xff, MOVE_TIME >> 8])
    for sid, a, rev, lo, hi in zip(SERVO_IDS, angles, REVERSE, PULSE_MIN, PULSE_MAX):
        a = max(0, min(180, a))
        if rev:
            a = 180 - a
        p = lo + (hi - lo) * a // 180
        frame += bytes([sid, p & 0xff, p >> 8])
    uart.write(frame)


uart.init(baudrate=115200, tx=pin12, rx=pin8)
cam_init()
move_hand(POSES[2])        # 시작은 손을 편 상태
display.show(Image.YES)

current = 0
candidate = 0
count = 0

while True:
    cid, conf = cam_result()
    if cid in POSES and conf >= MIN_CONFIDENCE:
        if cid == candidate:
            count += 1
        else:
            candidate = cid
            count = 1
        if count >= STABLE_COUNT and cid != current:
            current = cid
            move_hand(POSES[cid])
            display.show(ICONS.get(cid, str(cid)))
    else:
        candidate = 0
        count = 0
    sleep(100)
