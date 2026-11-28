# Team-AND · 웹 제어 + 리모컨 통합 (김윤하 P2)

웹 브라우저에서 카메라 영상을 보면서 AiHand 로봇 손을 조종합니다. IR 리모컨과 웹 조종을 **동시에** 쓸 수 있습니다.
[P1](../P1)의 IR 리모컨 허브를 개선한 버전입니다.

```
IR 리모컨 ─▶ micro:bit ◀─USB─▶ PC (web_server.py) ◀─▶ 웹 브라우저 (영상 + 조종)
                │                    ▲
                ▼                    └── USB 웹캠
           확장보드 ─▶ 서보 6개
```

## 폴더 구조

```
microbit/
  microbit_ir_hub.ts   # micro:bit 코드 (MakeCode JavaScript). P1 버전을 대체합니다
web/
  web_server.py        # 웹 서버: 영상 전송 + 조종 + 상태 + 카메라 켜기/끄기
  index.html           # 웹 화면
  run_web.bat          # 더블클릭 실행 (Windows)
```

## 실행 방법

### 1. micro:bit
1. [makecode.microbit.org](https://makecode.microbit.org) 에서 새 프로젝트를 만듭니다.
2. **확장 프로그램**에 아래 주소를 넣고 `MakerBit-IR-Receiver`를 추가합니다.
   ```
   https://github.com/1010Technologies/pxt-makerbit-ir-receiver
   ```
3. **JavaScript** 탭에 `microbit/microbit_ir_hub.ts` 내용을 붙여넣고 다운로드합니다.
4. 확장보드에 꽂고, micro:bit를 USB로 PC에도 연결합니다.

배선(IR 센서)은 [P1 README](../P1/README.md#배선)와 같습니다.

### 2. 웹 서버 (PC)
Python 3과 `opencv-python`, `pyserial`, `numpy`가 필요합니다.

```bash
pip install opencv-python pyserial numpy
cd web
python web_server.py                 # 카메라 0번, micro:bit 자동 검색
python web_server.py --camera 1      # 다른 카메라
python web_server.py --port COM5     # micro:bit 포트 직접 지정
```

Windows에서는 `run_web.bat`을 더블클릭해도 됩니다. 브라우저에서 **http://localhost:8000** 으로 접속합니다.

> ⚠️ 서버는 다른 기기에서도 접속할 수 있게 열려 있습니다. 같은 네트워크의 누구나 로봇을 조종하고 카메라를 볼 수 있으므로, Windows 방화벽 창이 뜨면 **공용 네트워크는 허용하지 마십시오.**

## 웹 화면 기능
- **카메라 영상** (USB 웹캠)과 **📷 카메라 켜기/끄기** 버튼. 끄면 웹캠을 놓아 주어 다른 프로그램이 쓸 수 있습니다.
- **손가락 5개(접힘 ↔ 펴짐)와 회전대(오른쪽 ↔ 왼쪽) 슬라이더.** 회색 막대는 로봇 손의 실제 위치입니다.
- **동작 버튼**: 보 · 바위 · 가위 · 좋아요 · 하나 · 중간 · 회전 왼쪽/가운데/오른쪽
- **상태 표시**: micro:bit 연결 상태, 현재 모드, 마지막으로 누른 리모컨 버튼

## 모드
| LED | 모드 | 조종 |
|---|---|---|
| `R` | 수동 | **리모컨과 웹 모두 조종 가능.** 마지막 명령이 적용됩니다. 리모컨으로 움직이면 웹 슬라이더도 따라갑니다 |
| `P` | 비전 | 웹캠 따라 하기(`S` 명령)만 적용. 리모컨이나 웹을 조작하면 바로 수동 모드로 돌아옵니다 |

리모컨 **EQ**로 수동 ↔ 비전 모드를 바꿉니다. 리모컨 버튼표는 [P1 README](../P1/README.md#리모컨-버튼-21키-리모컨-기준)와 같습니다.

## 통신 규칙 (USB 시리얼 115200bps) — P1에서 바뀐 점 ★

| 방향 | 메시지 | 의미 |
|---|---|---|
| PC → micro:bit | `W,a1,…,a6,dur,sum` ★ | **웹 조종.** 언제든 적용됩니다. `sum` = a1+…+a6+dur (깨진 명령 걸러내기) |
| PC → micro:bit | `S,a1,…,a6,dur` | 비전(웹캠 따라 하기). 비전 모드일 때만 적용 |
| PC → micro:bit | `PING` | `READY` 응답 |
| micro:bit → PC | `A,<모드>,a1,…,a6,sum` ★ | 상태 보고. `sum` = 각도 합계 (P1에는 없었음) |
| micro:bit → PC | `R` / `K` | 지금 명령을 보내도 됨 / PC 명령 적용됨 |

서보 번호: 1 엄지, 2 검지, 3 중지, 4 약지, 5 새끼, 6 회전대 (각도 0~180).
USB 포트는 한 프로그램만 열 수 있습니다. 웹 서버가 포트를 열고 있으면 다른 프로그램은 같은 micro:bit에 연결할 수 없습니다.

## P1에서 고친 문제
- **웹 명령이 적용되지 않던 문제**: MakeCode 시리얼 버퍼 기본값(약 20바이트)이 명령 한 줄보다 작아서 글자가 빠졌습니다. 그래서 버퍼를 128바이트로 늘리고, 명령과 상태 줄에 합계를 붙여 깨진 줄은 버리게 했습니다.
- **명령이 사라지는 경우**: 웹 서버가 적용 여부를 확인하고, 적용되지 않았으면 최대 5번 다시 보냅니다. 다시 보내기는 명령 후 1.5초 안에서만 하므로, 그 뒤 리모컨으로 움직인 것은 덮어쓰지 않습니다.

## 확인 상태
- ✅ 웹 서버, 영상 전송, 카메라 켜기/끄기 (실제 웹캠으로 확인)
- ✅ 깨진 줄 버리기와 재전송 로직 (가짜 장치로 확인)
- ⏳ 새 micro:bit 코드로 웹 조종이 실제로 되는지 (하드웨어 확인 필요)

## 참고: WonderCam
- AiHand의 WonderCam은 **영상을 밖으로 보내지 않고 인식 결과만** I2C로 보냅니다 (Hiwonder 공식 문서 기준). 그래서 웹 영상은 USB 웹캠을 사용합니다.
- WonderCam으로 손가락 하나하나를 따라 하는 것은 안 되지만, **손 모양 학습(✊✋✌️👍), 가위바위보, 색깔·얼굴 따라 회전**은 가능합니다. 다음 작업 후보입니다.

## 다음 할 일
1. 새 micro:bit 코드로 웹 조종 하드웨어 테스트
2. 웹 접속 범위 정하기 (이 PC 전용 / 비밀번호)
3. WonderCam 손 모양 따라 하기 + 가위바위보
