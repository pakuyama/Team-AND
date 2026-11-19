# P2 · 손목 카메라(WonderCam)로 손 모양 따라 하기

uHand 손목에 달린 AI 카메라(WonderCam)가 손 모양(✊ ✋ ✌️)을 인식하면, 로봇 손이 같은 모양을 따라 해요. **PC 없이 micro:bit 혼자** 동작해요.

```
WonderCam ─I2C(0x32)─▶ micro:bit ─시리얼(P12/P8)─▶ uHand 보드 ─▶ 서보 5개
```

## 파일
| 파일 | 설명 |
|---|---|
| `main_wondercam.py` | micro:bit MicroPython 코드. 카메라의 특징 학습 결과(ID)를 읽어 로봇 손 모양으로 변환 |

## 사용 방법

### 1. 카메라에 손 모양 학습시키기 (특징 학습)
- 오른쪽 버튼을 왼쪽으로 밀면 학습 모드, 왼쪽 버튼으로 ID 선택
- 화면 가운데 + 에 손을 맞추고 오른쪽 버튼을 왼쪽으로 밀어 학습 → 아래로 눌러 확인
- **ID 1 = 주먹 ✊, ID 2 = 보 ✋, ID 3 = 가위 ✌️**

### 2. micro:bit에 코드 넣기
[python.microbit.org](https://python.microbit.org)에 `main_wondercam.py`를 붙여넣고 플래시해요.

### 3. 실행
- 보드 전원 ON → 로봇 손이 펼쳐지고 micro:bit에 체크(✓)
- 손 모양을 카메라에 1초씩 보여주면 로봇이 따라 해요
- micro:bit 화면: ✊=작은 네모, ✋=큰 네모, ✌️=가위, `E`=카메라 연결 안 됨

## 설정 바꾸기
- `POSES`: ID별 손가락 각도. ID 4~7을 더 학습시키고 여기에 추가하면 손 모양을 늘릴 수 있어요 (예: 👍 `[180, 0, 0, 0, 0]`)
- `MIN_CONFIDENCE`: 인식 확신도 기준 (낮추면 더 잘 반응, 대신 오인식 증가)
- `STABLE_COUNT`: 같은 결과가 연속 몇 번 나와야 바꿀지 (떨림 방지)
- 서보 보정값(`REVERSE`, `PULSE_MIN/MAX`)은 P1의 `main_hiwonder.py`와 같아요.
