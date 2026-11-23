/*
  AiHand 로봇 쪽 micro:bit + 손목 카메라 따라 하기 (김다혜 P3)
  -------------------------------------------------------------------
  김윤하 P5 robot_hub.ts에 "손목 카메라(WonderCam) 따라 하기" 모드(C)를 더한 버전이에요.
  P5의 기능(리모컨, 웹 조종, 웹캠 따라 하기, 알림음, 무선)은 그대로예요.

  [카메라 모드 C]  손목 카메라를 아래로 향하게 두고, 그 아래에서 손 모양을 보여 주면 로봇 손이 따라 해요.
   - 켜기/끄기: micro:bit A+B 동시에 누르기 (또는 PC/무선 명령 "M,C")
   - 카메라에 "특징 학습"으로 손 모양을 학습시켜 두세요 (ID 1~7, 아래 CAM_POSE 표)
   - 움직이기 전 0.5초 알림음 규칙도 그대로 적용돼요
   - 상태 보고에는 R(수동)로 보내요 (P5 웹·pc_bridge와 호환)

  ---- 아래는 P5 원래 설명 ----
  AiHand 로봇 쪽 micro:bit (micro:bit V2 / MakeCode JavaScript)
  -------------------------------------------------------------------
  로봇 손 확장보드에 꽂는 micro:bit예요. PC 쪽 micro:bit(pc_bridge.ts)와 무선으로 연결돼요.
  (USB로 PC에 바로 꽂아도 똑같이 동작해요)

  [넣는 방법]
   1. makecode.microbit.org 에서 새 프로젝트
   2. 확장(Extensions)에서 아래 주소를 붙여넣고 "MakerBit-IR-Receiver" 추가
        https://github.com/1010Technologies/pxt-makerbit-ir-receiver
      ("radio"는 기본으로 들어 있어요. Bluetooth 확장은 넣지 마세요. 무선(radio)과 같이 못 써요)
   3. 위쪽 [JavaScript] 탭을 누르고 이 파일 내용을 전부 붙여넣기 -> 다운로드
   (StartbitV2 확장은 넣지 마세요. 서보 명령은 이 코드가 직접 보냅니다)

  [배선]  IR 수신 센서  OUT -> P1,  VCC -> 3V,  GND -> GND
          확장보드 서보: 1엄지 2검지 3중지 4약지 5새끼 6회전대

  [모드]  R = 수동 조종 (리모컨 + 웹)
          P = 따라 하기 (PC 웹캠이 손을 인식 -> 로봇 손이 따라 함)
          C = 손목 카메라 따라 하기 (WonderCam이 손 모양을 인식 -> 로봇 손이 따라 함)  [김다혜 추가]

  [리모컨 버튼]  (21키 "Car MP3" 리모컨 기준)
   EQ 따라 하기 켜기/끄기  (따라 하는 중에 다른 버튼을 누르면 바로 수동 조종으로)
   1~5 손가락 선택   6 회전대 선택   CH 손가락 전체 선택
   + 펴기(회전대 왼쪽)   - 접기(회전대 오른쪽)   (누르고 있으면 계속)
   |<< 회전대 왼쪽   >>| 회전대 오른쪽   CH-/CH+ 움직임 간격 5/10/20도
   >|| 펴기<->주먹   0 중립   7 가위  8 바위  9 보  200+ 좋아요  100+ 숫자 세기

  [알림음]  움직이기 전에 0.5초 소리 -> 소리가 끝나면 움직임 (micro:bit V2 내장 스피커)
            로고(금색 부분) 터치: 소리 켜기/끄기

  [무선 (pc_bridge.ts와 같은 규칙)]  그룹 RADIO_GROUP, 패킷 첫 바이트 MAGIC
   PC -> 로봇 :  [MAGIC, 'W', a1..a6, 시간L, 시간H]   웹 조종 (언제든 적용, 따라 하기 중이면 수동으로)
                 [MAGIC, 'S', a1..a6, 시간L, 시간H]   따라 하기 (따라 하기 모드일 때만 적용)
                 [MAGIC, 'M', 'R' 또는 'P']           모드 바꾸기
   로봇 -> PC :  [MAGIC, 'A', 모드, a1..a6]           상태 (0.1초마다)
                 [MAGIC, 'I', 버튼코드]               눌린 리모컨 버튼

  [USB로 바로 연결할 때 (115200bps)]  pc_bridge.ts가 PC에 보내는 것과 같은 글자 규칙
   PC -> micro:bit :  "W,a1,a2,a3,a4,a5,a6,dur,sum"  sum = a1+...+a6+dur
                      "S,a1,a2,a3,a4,a5,a6,dur"     "M,R" / "M,P"     "PING" -> "READY"
   micro:bit -> PC :  "A,<모드>,a1,a2,a3,a4,a5,a6,sum"   "R" (명령 보내도 됨)   "# IR 0x.."
*/

// ---------------- 설정 ----------------
const RADIO_GROUP = 73                 // pc_bridge.ts와 같은 번호. 다른 팀과 겹치면 바꾸세요 (0~255)
const MAGIC = 0xA5                     // 우리 팀 패킷 표시
const IR_PIN = DigitalPin.P1
const MOVE_TIME = 150                  // 서보 이동 시간(ms)
const REPEAT_MS = 150                  // 누르고 있을 때 반복 간격(ms)
const BEEP_FREQ = 880                  // 알림음 높이(Hz)
const BEEP_MS = 500                    // 알림음 길이(ms). 소리가 끝난 뒤에 움직임
const BEEP_GAP = 800                   // 이 시간(ms) 넘게 멈춰 있다가 움직일 때만 소리 (계속 움직이는 중에는 안 남)
//                엄지 검지 중지 약지 새끼 회전대
const OPEN_A = [50, 130, 130, 130, 130, 90]
const CLOSE_A = [130, 50, 50, 50, 50, 90]

// ---------------- 손목 카메라 (WonderCam) 설정 [김다혜 추가] ----------------
const CAM_ADDR = 0x32                  // WonderCam I2C 주소
const CAM_FUNC_FEATURE = 4             // 특징 학습(Feature Learning) 기능 번호
const CAM_MIN_CONF = 6000              // 확신도 기준 (10000 = 100%). 이보다 낮으면 무시
const CAM_STABLE = 3                   // 같은 결과가 연속 몇 번 나와야 바꿀지 (떨림 방지)
const CAM_PERIOD = 100                 // 카메라 읽는 간격(ms)
// 카메라 ID별 손 모양 (비트0=엄지 ... 비트4=새끼, 1=폄)  -> 카메라에 이 순서로 학습시키세요
//  ID:          -  1주먹 2보  3가위 4엄지척 5검지 6인사(보+손목 흔들기) 7(엄지·검지·새끼)
const CAM_POSE = [0, 0,    31,  6,    1,     2,    31,                  19]
const CAM_WAVE_ID = 6                  // 이 ID는 손을 펴고 회전대를 좌우로 흔들어요
const WAVE_ANGLES = [60, 120, 60, 120, 60, 120, 90]

// 리모컨 버튼 코드 (다르면 웹 화면 위쪽 "리모컨: IR 0x.." 값으로 바꾸세요)
const BTN_CH_MINUS = 0xA2
const BTN_CH = 0x62
const BTN_CH_PLUS = 0xE2
const BTN_PREV = 0x22
const BTN_NEXT = 0x02
const BTN_PLAY = 0xC2
const BTN_MINUS = 0xE0
const BTN_PLUS = 0xA8
const BTN_EQ = 0x90
const BTN_100 = 0x98
const BTN_200 = 0xB0
//               0     1     2     3     4     5     6     7     8     9
const BTN_NUM = [0x68, 0x30, 0x18, 0x7A, 0x10, 0x38, 0x5A, 0x42, 0x4A, 0x52]

// ---------------- 상태 ----------------
let angles = [90, 90, 90, 90, 90, 90]
let moveTime = 800
let selected = -1          // -1 = 손가락 전체, 0~4 = 손가락, 5 = 회전대
let stepDeg = 10
let mode = "R"             // R = 수동, P = 따라 하기
let modeChanged = true
let handOpen = false
let dirty = true           // 확장보드로 보낼 새 각도가 있음
let holdCmd = -1
let nextRepeat = 0
let lastCode = -1
let soundOn = true
let lastMoveAt = -100000   // 마지막으로 움직인 시각
let radioNext = 0

// 손목 카메라 상태 [김다혜 추가]
let camReady = false
let camNext = 0
let camCandidate = 0
let camCount = 0
let camCurrent = 0
let waveIdx = -1
let waveNext = 0

// 숫자 세기 동작 (비트0=엄지 ... 비트4=새끼, 1=폄)
const SEQ_BITS = [0, 2, 6, 14, 30, 31, 0]
const SEQ_MS = [700, 700, 700, 700, 700, 1000, 500]
let seqIdx = -1
let seqNext = 0

// ---------------- 서보 동작 ----------------
function setFingers(bits: number) {
    for (let i = 0; i < 5; i++) {
        angles[i] = ((bits >> i) & 1) == 1 ? OPEN_A[i] : CLOSE_A[i]
    }
}

function clampServo(i: number, a: number): number {
    if (i == 5) return Math.constrain(a, 0, 180)
    return Math.constrain(a, Math.min(OPEN_A[i], CLOSE_A[i]), Math.max(OPEN_A[i], CLOSE_A[i]))
}

// dir = 1 펴기(회전대는 왼쪽), -1 접기(회전대는 오른쪽)
function moveServo(i: number, dir: number) {
    let sign = 1
    if (i < 5 && OPEN_A[i] < CLOSE_A[i]) sign = -1
    angles[i] = clampServo(i, angles[i] + sign * dir * stepDeg)
}

function sendToBoard() {
    // StartbitV2 확장과 같은 서보 명령 6개 (각 10바이트)
    let buf = pins.createBuffer(60)
    for (let i = 0; i < 6; i++) {
        let pos = 500 + Math.idiv(Math.constrain(angles[i], 0, 180) * 2000, 180)
        let o = i * 10
        buf[o] = 0x55
        buf[o + 1] = 0x55
        buf[o + 2] = 0x08
        buf[o + 3] = 0x03
        buf[o + 4] = 0x01
        buf[o + 5] = moveTime & 0xff
        buf[o + 6] = (moveTime >> 8) & 0xff
        buf[o + 7] = i + 1
        buf[o + 8] = pos & 0xff
        buf[o + 9] = (pos >> 8) & 0xff
    }
    serial.redirect(SerialPin.P12, SerialPin.P8, BaudRate.BaudRate115200)
    serial.writeBuffer(buf)
    basic.pause(10)
    serial.redirectToUSB()
    serial.setBaudRate(BaudRate.BaudRate115200)
}

function setMode(m: string) {
    if (m == mode) return
    if (m == "C" && !camStart()) {     // 카메라를 못 찾으면 카메라 모드로 안 바꿈
        basic.showString("E")
        modeChanged = true
        return
    }
    mode = m
    modeChanged = true
    seqIdx = -1
    waveIdx = -1
    camCurrent = 0
    camCandidate = 0
    camCount = 0
}

// ---------------- 손목 카메라 (WonderCam, I2C) [김다혜 추가] ----------------
function camRead(reg: number, n: number): Buffer {
    let a = pins.createBuffer(2)
    a[0] = reg & 0xff
    a[1] = (reg >> 8) & 0xff
    pins.i2cWriteBuffer(CAM_ADDR, a, false)
    return pins.i2cReadBuffer(CAM_ADDR, n, false)
}

function camWrite(reg: number, v: number) {
    let a = pins.createBuffer(3)
    a[0] = reg & 0xff
    a[1] = (reg >> 8) & 0xff
    a[2] = v & 0xff
    pins.i2cWriteBuffer(CAM_ADDR, a, false)
}

// 카메라 연결 확인 + 특징 학습 기능으로 바꾸기. 성공하면 true
function camStart(): boolean {
    if (camRead(0x0000, 1)[0] != 0x76) return false        // 'v' 가 오면 WonderCam
    if (camRead(0x0035, 1)[0] != CAM_FUNC_FEATURE) {
        camWrite(0x0035, CAM_FUNC_FEATURE)
        for (let i = 0; i < 40; i++) {
            basic.pause(100)
            if (camRead(0x0035, 1)[0] == CAM_FUNC_FEATURE) break
        }
    }
    camReady = true
    return true
}

// 카메라 결과를 읽어서 손 모양이 바뀌면 로봇 손에 적용
function camStep() {
    if (!camReady || input.runningTime() < camNext) return
    camNext = input.runningTime() + CAM_PERIOD
    if (waveIdx >= 0) return                                  // 인사 동작 중에는 기다림
    let b = camRead(0x0E00, 4)
    let id = b[1]
    let conf = b[2] | (b[3] << 8)
    if (id >= 1 && id < CAM_POSE.length && conf >= CAM_MIN_CONF) {
        if (id == camCandidate) camCount++
        else {
            camCandidate = id
            camCount = 1
        }
        if (camCount >= CAM_STABLE && id != camCurrent) {
            camCurrent = id
            setFingers(CAM_POSE[id])
            angles[5] = 90
            moveTime = 300
            dirty = true
            basic.showNumber(id, 0)
            if (id == CAM_WAVE_ID) {
                waveIdx = 0
                waveNext = input.runningTime() + 900          // 알림음 + 손 펴기가 끝난 뒤 흔들기
            }
        }
    } else {
        camCandidate = 0
        camCount = 0
    }
}

// 인사: 회전대를 좌우로 흔들기
function runWave() {
    if (waveIdx < 0 || input.runningTime() < waveNext) return
    if (waveIdx >= WAVE_ANGLES.length) {
        waveIdx = -1
        return
    }
    angles[5] = WAVE_ANGLES[waveIdx]
    moveTime = 350
    dirty = true
    lastMoveAt = input.runningTime()                          // 흔드는 중에는 알림음 안 나게
    waveNext = input.runningTime() + 400
    waveIdx++
}

// ---------------- PC 명령 (무선, USB 공통) ----------------
// v = 각도 6개 + 이동시간. isWeb = 웹 조종(W), 아니면 따라 하기(S)
function applyCmd(isWeb: boolean, v: number[]): boolean {
    if (isWeb) {
        setMode("R")               // 웹 조종: 언제든 적용. 따라 하는 중이었다면 수동으로
        seqIdx = -1
    } else if (mode != "P") {
        return false               // 따라 하기 명령은 따라 하기 모드일 때만
    }
    let changed = isWeb
    for (let i = 0; i < 6; i++) {
        let a = Math.constrain(v[i], 0, 180)
        if (Math.abs(a - angles[i]) >= 2) changed = true
        angles[i] = a
    }
    if (changed) {                 // 같은 자세가 다시 오면 (연결 확인용) 움직이지 않음 -> 소리도 안 남
        moveTime = Math.constrain(v[6], 20, 2000)
        dirty = true
    }
    return true
}

// ---------------- 무선 ----------------
radio.setGroup(RADIO_GROUP)
radio.setTransmitPower(7)

radio.onReceivedBuffer(function (b) {
    if (b.length < 3 || b[0] != MAGIC) return
    let t = b[1]
    if (t == 0x4D) {                                   // 'M' 모드
        if (b[2] == 0x52) setMode("R")
        else if (b[2] == 0x50) setMode("P")
        else if (b[2] == 0x43) setMode("C")            // 'C' 손목 카메라 [김다혜 추가]
    } else if ((t == 0x57 || t == 0x53) && b.length >= 10) {   // 'W' / 'S'
        let v: number[] = []
        for (let i = 0; i < 6; i++) v.push(b[2 + i])
        v.push(b[8] | (b[9] << 8))
        applyCmd(t == 0x57, v)
    }
})

function radioStatus() {
    let b = pins.createBuffer(9)
    b[0] = MAGIC
    b[1] = 0x41                                        // 'A'
    b[2] = mode == "P" ? 0x50 : 0x52                   // C(카메라)는 R로 보고 (P5 웹 호환)
    for (let i = 0; i < 6; i++) b[3 + i] = angles[i]
    radio.sendBuffer(b)
}

function radioIR(code: number) {
    let b = pins.createBuffer(3)
    b[0] = MAGIC
    b[1] = 0x49                                        // 'I'
    b[2] = code
    radio.sendBuffer(b)
}

// ---------------- 리모컨 ----------------
function isRepeatable(c: number): boolean {
    return c == BTN_PLUS || c == BTN_MINUS || c == BTN_PREV || c == BTN_NEXT
}

function handleButton(c: number, repeat: boolean) {
    if (repeat && !isRepeatable(c)) return

    if (c == BTN_EQ) {             // 따라 하기 켜기/끄기
        setMode(mode == "R" ? "P" : "R")
        return
    }
    setMode("R")                   // 따라 하는 중에 다른 버튼 -> 수동 조종으로
    seqIdx = -1                    // 진행 중인 동작 멈춤

    if (c == BTN_PLUS || c == BTN_MINUS) {
        let dir = (c == BTN_PLUS) ? 1 : -1
        if (selected < 0) {
            for (let i = 0; i < 5; i++) moveServo(i, dir)
        } else {
            moveServo(selected, dir)
        }
    } else if (c == BTN_PREV) moveServo(5, 1)
    else if (c == BTN_NEXT) moveServo(5, -1)
    else if (c == BTN_CH) selected = -1
    else if (c == BTN_CH_MINUS) stepDeg = (stepDeg == 20) ? 10 : 5
    else if (c == BTN_CH_PLUS) stepDeg = (stepDeg == 5) ? 10 : 20
    else if (c == BTN_PLAY) {
        handOpen = !handOpen
        setFingers(handOpen ? 31 : 0)
    }
    else if (c == BTN_NUM[0]) angles = [90, 90, 90, 90, 90, 90]
    else if (c == BTN_NUM[7]) setFingers(6)     // 가위
    else if (c == BTN_NUM[8]) setFingers(0)     // 바위
    else if (c == BTN_NUM[9]) setFingers(31)    // 보
    else if (c == BTN_200) setFingers(1)        // 좋아요
    else if (c == BTN_100) {                    // 숫자 세기
        seqIdx = 0
        seqNext = 0
    } else {
        for (let n = 1; n <= 6; n++) {
            if (c == BTN_NUM[n]) selected = n - 1
        }
    }
    moveTime = MOVE_TIME
    dirty = true
}

function runSeq() {
    if (seqIdx < 0 || input.runningTime() < seqNext) return
    if (seqIdx >= SEQ_BITS.length) {
        seqIdx = -1
        return
    }
    setFingers(SEQ_BITS[seqIdx])
    moveTime = MOVE_TIME
    dirty = true
    seqNext = input.runningTime() + SEQ_MS[seqIdx]
    seqIdx++
}

makerbit.connectIrReceiver(IR_PIN, IrProtocol.NEC)
makerbit.onIrButton(IrButton.Any, IrButtonAction.Pressed, function () {
    let c = makerbit.irButton()
    lastCode = c
    holdCmd = c
    nextRepeat = input.runningTime() + 400
    handleButton(c, false)
})
makerbit.onIrButton(IrButton.Any, IrButtonAction.Released, function () {
    holdCmd = -1
})

// ---------------- USB (PC에 바로 꽂았을 때) ----------------
let pcBuf = ""

function hex2(n: number): string {
    const d = "0123456789ABCDEF"
    return d.charAt((n >> 4) & 15) + d.charAt(n & 15)
}

function handleUSB(line: string): boolean {
    if (line.length > 0 && line.charAt(line.length - 1) == "\r") line = line.substr(0, line.length - 1)
    if (line == "PING") {
        serial.writeLine("READY")
        return false
    }
    let p = line.split(",")
    if (p[0] == "M" && p.length == 2) {
        if (p[1] == "R" || p[1] == "P" || p[1] == "C") {
            setMode(p[1])
            return true
        }
        return false
    }
    let isWeb = p[0] == "W"
    // W: 각도6 + 시간 + 합계(검증용) = 9칸,  S: 각도6 + 시간 = 8칸
    if (!(isWeb && p.length == 9) && !(p[0] == "S" && p.length == 8)) return false
    let v: number[] = []
    let sum = 0
    for (let i = 1; i <= 7; i++) {
        let n = parseInt(p[i])
        if (!(n >= 0 && n <= 2000)) return false      // 숫자가 아니거나 범위 밖이면 버림
        v.push(n)
        sum += n
    }
    if (isWeb && parseInt(p[8]) != sum) return false  // 글자가 빠져 깨진 명령은 버림
    return applyCmd(isWeb, v)
}

function listenUSB(ms: number) {
    let t = input.runningTime()
    while (input.runningTime() - t < ms) {
        pcBuf = pcBuf + serial.readString()
        if (pcBuf.length > 200) pcBuf = ""
        let i = pcBuf.indexOf("\n")
        while (i >= 0) {
            let line = pcBuf.substr(0, i)
            pcBuf = pcBuf.substr(i + 1)
            if (handleUSB(line)) return
            i = pcBuf.indexOf("\n")
        }
        basic.pause(2)
    }
}

// 테스트 버튼: A 펴기, B 주먹
input.onButtonPressed(Button.A, function () {
    setMode("R")
    setFingers(31)
    moveTime = 500
    dirty = true
})
input.onButtonPressed(Button.B, function () {
    setMode("R")
    setFingers(0)
    moveTime = 500
    dirty = true
})

// A+B 동시에: 손목 카메라 모드 켜기/끄기 [김다혜 추가]
input.onButtonPressed(Button.AB, function () {
    setMode(mode == "C" ? "R" : "C")
})

// 로고 터치: 알림음 켜기/끄기 (켜면 짧게 두 번, 끄면 낮게 한 번)
input.onLogoEvent(TouchButtonEvent.Pressed, function () {
    soundOn = !soundOn
    if (soundOn) {
        music.playTone(988, 80)
        music.playTone(1319, 80)
    } else {
        music.playTone(262, 150)
    }
})

// ---------------- 메인 루프 ----------------
music.setBuiltInSpeakerEnabled(true)
serial.setBaudRate(BaudRate.BaudRate115200)
// 기본 버퍼(약 20바이트)는 명령 한 줄보다 작아서 글자가 빠짐 -> 넉넉하게
serial.setTxBufferSize(128)
serial.setRxBufferSize(128)
basic.forever(function () {
    // 누르고 있는 버튼 반복
    if (holdCmd >= 0 && isRepeatable(holdCmd) && input.runningTime() >= nextRepeat) {
        handleButton(holdCmd, true)
        nextRepeat = input.runningTime() + REPEAT_MS
    }
    if (mode == "R") runSeq()
    if (mode == "C") {                 // [김다혜 추가]
        camStep()
        runWave()
    }

    if (dirty) {
        dirty = false
        // 한동안 멈춰 있다가 움직일 때: 먼저 0.5초 소리 -> 소리가 끝나면 움직임
        if (soundOn && input.runningTime() - lastMoveAt > BEEP_GAP) {
            music.playTone(BEEP_FREQ, BEEP_MS)   // 소리가 끝날 때까지 기다림
        }
        sendToBoard()                            // 소리 나는 동안 바뀐 값이 있으면 최신 값으로 움직임
        lastMoveAt = input.runningTime()
    }
    if (modeChanged) {
        modeChanged = false
        if (mode == "P") basic.showIcon(IconNames.Heart, 0)   // 따라 하기 중
        else if (mode == "C") basic.showString("C")           // 손목 카메라 따라 하기 중
        else basic.showString("R")
    }

    // 리모컨 버튼 코드 알리기
    if (lastCode >= 0) {
        radioIR(lastCode)
        serial.writeLine("# IR 0x" + hex2(lastCode))
        lastCode = -1
    }
    // 상태 보내기: 무선은 0.1초마다, USB는 매번
    if (input.runningTime() >= radioNext) {
        radioNext = input.runningTime() + 100
        radioStatus()
    }
    let total = 0
    for (let i = 0; i < 6; i++) total += angles[i]
    serial.writeLine("A," + (mode == "P" ? "P" : "R") + "," + angles.join(",") + "," + total)   // 마지막 값 = 각도 합계(검증용)
    serial.writeLine("R")
    listenUSB(40)
})
