# micro:bit (MicroPython) 수신 코드
# PC에서 "a,b,c,d,e\n" (엄지,검지,중지,약지,소지 각도 0~180)를 받아 서보를 움직여요.
#
# !! 주의: 아래 SERVO_PINS 는 예시 값이에요.
#    uHand 확장보드에서 각 손가락 서보가 micro:bit 어느 핀에 연결되는지
#    Hiwonder 자료(보드 회로도/예제)를 확인해서 맞게 바꿔주세요.
#    보드가 서보를 직접 PWM 핀으로 구동하지 않고 별도 컨트롤러를 쓰는 경우,
#    MakeCode의 Hiwonder 확장 블록을 쓰는 방식(README 참고)으로 하세요.
from microbit import *

SERVO_PINS = [pin0, pin1, pin2, pin8, pin12]   # 엄지, 검지, 중지, 약지, 소지
REVERSE = [False, False, False, False, False]  # 방향이 반대인 손가락은 True

for p in SERVO_PINS:
    p.set_analog_period(20)  # 서보용 50Hz


def write_servo(pin, angle):
    angle = max(0, min(180, angle))
    # 0.5ms ~ 2.5ms 펄스 -> 1023 기준 duty 값
    duty = int((0.5 + angle / 180 * 2.0) / 20 * 1023)
    pin.write_analog(duty)


uart.init(baudrate=115200)
buf = ""
display.show(Image.HAPPY)

while True:
    if uart.any():
        data = uart.read()
        if data:
            buf += str(data, "utf-8")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                parts = line.strip().split(",")
                if len(parts) != 5:
                    continue
                try:
                    angles = [int(x) for x in parts]
                except ValueError:
                    continue
                for pin, a, rev in zip(SERVO_PINS, angles, REVERSE):
                    write_servo(pin, 180 - a if rev else a)
    sleep(5)
