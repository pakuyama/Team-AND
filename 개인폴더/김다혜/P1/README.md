# Team-AND · 웹캠 손 추적 → uHand 로봇 손 제어

웹캠으로 사람 손을 인식해서, Hiwonder uHand(micro:bit 확장보드) 로봇 손이 같은 모양을 따라 하도록 만드는 프로젝트예요.

```
웹캠 ─▶ PC (MediaPipe Hands) ─USB 시리얼─▶ micro:bit ─▶ 확장보드 ─▶ 서보 5개
```

## 폴더 구조

```
pc/
  hand_tracker.py   # 웹캠 손 인식 + 각도 전송
  requirements.txt
microbit/
  main.py           # 각도 수신 + 서보 제어 (MicroPython)
```

## 실행 방법

### 1. micro:bit
1. [python.microbit.org](https://python.microbit.org) 에서 `microbit/main.py` 내용을 붙여넣고 micro:bit에 플래시해요.
2. **`SERVO_PINS`를 보드에 맞게 꼭 수정하세요.** (현재 값은 예시)
3. 플래시 후 웃는 얼굴이 뜨면 대기 상태예요.

> MakeCode를 쓴다면: Hiwonder 확장을 추가하고, `시리얼 줄바꿈까지 읽기` → `,`로 나누기 → 각 값을 해당 손가락 서보 각도로 설정하면 같은 동작이 돼요.

### 2. PC
Python 3.9~3.12 권장 (mediapipe 호환 버전 때문)

```bash
cd pc
pip install -r requirements.txt
python hand_tracker.py --list            # 포트 확인
python hand_tracker.py --no-serial       # 로봇 없이 인식만 테스트
python hand_tracker.py --port COM3       # 실제 실행 (포트는 본인 환경에 맞게)
```

ESC를 누르면 종료돼요.

## 프로토콜

PC → micro:bit, 115200bps, 한 줄에 하나씩:

```
엄지,검지,중지,약지,소지\n     예) 180,0,0,180,180\n
```
각 값은 0(완전히 굽힘) ~ 180(완전히 폄).

## 보정 팁
- 손가락이 덜 펴지거나 덜 굽혀지면 `hand_tracker.py`의 `RATIO_RANGE` 값을 조정하세요. 화면에 실시간 각도가 표시돼요.
- 특정 손가락이 반대로 움직이면 `microbit/main.py`의 `REVERSE`를 `True`로 바꾸세요.
- 서보가 떨리면 `SMOOTHING`을 올리거나 `SEND_INTERVAL`을 늘리세요.

## Hiwonder uHand 보드용 버전 (실제 로봇에서 동작 확인)

uHand 확장보드는 micro:bit 핀으로 서보를 직접 구동하지 않고, 보드 칩에 시리얼(P12=TX, P8=RX, 115200bps) 명령을 보내는 방식이에요. 이 보드에서는 아래 파일을 써 주세요.

| 파일 | 설명 |
|---|---|
| `microbit/main_hiwonder.py` | PC에서 받은 각도를 Hiwonder 서보 명령(0x55 0x55 프레임)으로 변환해 보드에 전송 |
| `pc/hand_tracker_v2.py` | 엄지 인식 개선 + 자동 보정 (실행 후 주먹↔펼치기를 3~4번, R 키로 리셋) |

```bash
python hand_tracker_v2.py --port COM5
```

micro:bit 화면: 체크(✓) = 실행 중, 왼쪽 위 점 = PC 신호 수신, 가운데 점 = 로봇에 명령 전송.

보정값(우리 로봇 기준): 엄지는 방향 반대(`REVERSE`), 펄스 범위 800(펼침)~2200(접힘). 다른 손가락은 900~2100.

> 기존 `microbit/main.py`는 서보가 micro:bit 핀에 직접 연결된 경우용이에요.
