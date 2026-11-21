# micro:bit (MicroPython) — uHand 로봇 손 제어 (Hiwonder 보드 시리얼 방식)
# PC에서 USB로 "엄지,검지,중지,약지,소지[,손목]\n"(각도 0~180)를 받고,
# 보드 칩에 P12(TX)/P8(RX) 시리얼로 서보 명령을 보내요.
# micro:bit 화면: 체크(✓)=실행 중, 왼쪽 위 점=PC 신호 수신, 가운데 점=로봇에 명령 전송
from microbit import *

SERVO_IDS = [1, 2, 3, 4, 5]          # 엄지, 검지, 중지, 약지, 소지 (보드 서보 번호)
REVERSE = [True, False, False, False, False]  # 반대로 움직이는 손가락은 True
PULSE_MIN = [800, 900, 900, 900, 900]     # 각도 0일 때 펄스(us), 손가락별
PULSE_MAX = [2200, 2100, 2100, 2100, 2100] # 각도 180일 때 펄스(us), 손가락별 (엄지: 800=펴짐, 2200=접힘)
MOVE_TIME = 100    # ms

# 손목 (6번 모터) — PC가 6번째 값을 보내면 움직여요. 처음엔 안전한 범위로 설정
WRIST_ID = 6
WRIST_MIN = 1000   # 각도 0일 때
WRIST_MAX = 2000   # 각도 180일 때
WRIST_REVERSE = False   # 손목이 반대로 돌면 True


def send_to_board(angles):
    pairs = []
    for sid, a, rev, lo, hi in zip(SERVO_IDS, angles, REVERSE, PULSE_MIN, PULSE_MAX):
        a = max(0, min(180, a))
        if rev:
            a = 180 - a
        pairs.append((sid, lo + (hi - lo) * a // 180))
    if len(angles) >= 6:
        w = max(0, min(180, angles[5]))
        if WRIST_REVERSE:
            w = 180 - w
        pairs.append((WRIST_ID, WRIST_MIN + (WRIST_MAX - WRIST_MIN) * w // 180))
    n = len(pairs)
    frame = bytearray([0x55, 0x55, n * 3 + 5, 0x03, n,
                       MOVE_TIME & 0xff, MOVE_TIME >> 8])
    for sid, p in pairs:
        frame += bytes([sid, p & 0xff, p >> 8])
    # 보드로 잠깐 전환해서 보내고 다시 USB로 복귀
    uart.init(baudrate=115200, tx=pin12, rx=pin8)
    uart.write(frame)
    sleep(5)
    uart.init(baudrate=115200)


def parse(line):
    if not line:
        return None
    parts = line.strip().split(",")
    if len(parts) not in (5, 6):   # 5개 = 손가락만, 6개 = 손가락 + 손목
        return None
    try:
        return [int(x) for x in parts]
    except ValueError:
        return None


uart.init(baudrate=115200)
display.show(Image.YES)
buf = ""
skip_partial = False
blink = False
rx_blink = False

while True:
    if uart.any():
        data = uart.read()
        if data:
            # 전환 중 깨진 문자는 버리고 출력 가능한 글자와 줄바꿈만 남김 (에러 방지)
            buf += "".join(chr(b) for b in data if b == 10 or 32 <= b < 127)
            if len(buf) > 200:
                buf = buf[-100:]
            rx_blink = not rx_blink
            display.set_pixel(0, 0, 9 if rx_blink else 0)  # 데이터 수신 표시
    if skip_partial and "\n" in buf:
        buf = buf.split("\n", 1)[1]   # 전환 중 잘렸을 수 있는 첫 줄 버림
        skip_partial = False
    if not skip_partial and "\n" in buf:
        lines = buf.split("\n")
        buf = lines[-1]
        angles = None
        for ln in reversed(lines[:-1]):
            angles = parse(ln)
            if angles:
                break
        if angles:
            try:
                send_to_board(angles)
            except Exception:
                uart.init(baudrate=115200)  # 문제가 생겨도 USB로 복귀하고 계속
            buf = ""
            skip_partial = True
            blink = not blink
            display.set_pixel(2, 2, 9 if blink else 0)
    sleep(5)
