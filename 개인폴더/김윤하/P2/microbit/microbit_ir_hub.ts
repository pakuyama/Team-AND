/*
  AiHand IR 리모컨 + 비전 통합 허브 (micro:bit V2 / MakeCode JavaScript)
  -------------------------------------------------------------------
  [넣는 방법]
   1. makecode.microbit.org 에서 새 프로젝트
   2. 확장(Extensions)에서 아래 주소를 붙여넣고 "MakerBit-IR-Receiver" 추가
        https://github.com/1010Technologies/pxt-makerbit-ir-receiver
   3. 위쪽 [JavaScript] 탭을 누르고 이 파일 내용을 전부 붙여넣기 -> 다운로드
   (StartbitV2 확장은 넣지 마세요. 서보 명령은 이 코드가 직접 보냅니다)

  [배선]  IR 수신 센서  OUT -> P1,  VCC -> 3V,  GND -> GND

  [통신 규칙 (팀원 공유용, USB 115200bps)]
   PC -> micro:bit :  "W,a1,a2,a3,a4,a5,a6,dur,sum" 웹 조종: 리모컨과 함께 언제든 적용 (비전 모드면 수동으로 복귀)
                                                    sum = a1+...+a6+dur (깨진 명령 걸러내기용)
                      "S,a1,a2,a3,a4,a5,a6,dur"   비전(자동 따라하기): 비전 모드일 때만 적용
                      "PING"                      -> "READY"
   micro:bit -> PC :  "A,<모드>,a1,a2,a3,a4,a5,a6,sum"  상태 (R=수동: 리모컨+웹, P=비전), sum = 각도 합계
                      "R"                         지금 명령을 보내도 됨
                      "K"                         PC 명령 적용됨
                      "# IR 0x.."                 눌린 리모컨 버튼 코드 (코드표 확인용)
   서보: 1엄지 2검지 3중지 4약지 5새끼 6회전대

  [리모컨 버튼]  (21키 "Car MP3" 리모컨 기준)
   1~5 손가락 선택   6 회전대 선택   CH 손가락 전체 선택
   + 펴기(회전대 왼쪽)   - 접기(회전대 오른쪽)   (누르고 있으면 계속)
   |<< 회전대 왼쪽   >>| 회전대 오른쪽   CH-/CH+ 움직임 간격 5/10/20도
   >|| 펴기<->주먹   0 중립   7 가위  8 바위  9 보  200+ 좋아요  100+ 숫자 세기
   EQ 수동 모드(리모컨+웹) <-> 비전 모드
*/

// ---------------- 설정 ----------------
const IR_PIN = DigitalPin.P1
const MOVE_TIME = 150                  // 서보 이동 시간(ms)
const REPEAT_MS = 150                  // 누르고 있을 때 반복 간격(ms)
//                엄지 검지 중지 약지 새끼 회전대
const OPEN_A = [50, 130, 130, 130, 130, 90]
const CLOSE_A = [130, 50, 50, 50, 50, 90]

// 리모컨 버튼 코드 (이 확장 기준. 다르면 USB 콘솔에 나오는 "# IR 0x.." 값으로 바꾸세요)
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
let mode = "R"             // R = 리모컨, P = 비전(PC)
let modeChanged = true
let handOpen = false
let dirty = true           // 확장보드로 보낼 새 각도가 있음
let holdCmd = -1
let nextRepeat = 0
let lastCode = -1
let pcApplied = false

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

// ---------------- 리모컨 ----------------
function isRepeatable(c: number): boolean {
    return c == BTN_PLUS || c == BTN_MINUS || c == BTN_PREV || c == BTN_NEXT
}

function handleButton(c: number, repeat: boolean) {
    if (repeat && !isRepeatable(c)) return

    if (c == BTN_EQ) {
        mode = (mode == "R") ? "P" : "R"
        modeChanged = true
        return
    }
    if (mode != "R") {             // 비전 모드에서 다른 버튼 -> 리모컨 모드로 복귀
        mode = "R"
        modeChanged = true
    }
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

// ---------------- PC (USB) ----------------
let pcBuf = ""

function hex2(n: number): string {
    const d = "0123456789ABCDEF"
    return d.charAt((n >> 4) & 15) + d.charAt(n & 15)
}

function handlePC(line: string): boolean {
    if (line.length > 0 && line.charAt(line.length - 1) == "\r") line = line.substr(0, line.length - 1)
    if (line == "PING") {
        serial.writeLine("READY")
        return false
    }
    let p = line.split(",")
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
    if (isWeb) {
        if (parseInt(p[8]) != sum) return false       // 글자가 빠져 깨진 명령은 버림
        // 웹 조종: 리모컨과 똑같이 언제든 적용. 비전 모드였다면 수동 모드로 돌아옴
        if (mode != "R") {
            mode = "R"
            modeChanged = true
        }
        seqIdx = -1
    } else if (mode != "P") {
        return false               // 비전 명령(S)은 비전 모드일 때만
    }
    for (let i = 0; i < 6; i++) angles[i] = Math.constrain(v[i], 0, 180)
    moveTime = Math.constrain(v[6], 20, 2000)
    dirty = true
    pcApplied = true
    return true
}

function listenPC(ms: number) {
    let t = input.runningTime()
    while (input.runningTime() - t < ms) {
        pcBuf = pcBuf + serial.readString()
        if (pcBuf.length > 200) pcBuf = ""
        let i = pcBuf.indexOf("\n")
        while (i >= 0) {
            let line = pcBuf.substr(0, i)
            pcBuf = pcBuf.substr(i + 1)
            if (handlePC(line)) return
            i = pcBuf.indexOf("\n")
        }
        basic.pause(2)
    }
}

// 테스트 버튼: A 펴기, B 주먹
input.onButtonPressed(Button.A, function () {
    setFingers(31)
    moveTime = 500
    dirty = true
})
input.onButtonPressed(Button.B, function () {
    setFingers(0)
    moveTime = 500
    dirty = true
})

// ---------------- 메인 루프 ----------------
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

    if (dirty) {
        dirty = false
        sendToBoard()
    }
    if (modeChanged) {
        modeChanged = false
        basic.showString(mode)
    }

    // PC로 상태 보내고 명령 받기
    if (lastCode >= 0) {
        serial.writeLine("# IR 0x" + hex2(lastCode))
        lastCode = -1
    }
    if (pcApplied) {
        serial.writeLine("K")
        pcApplied = false
    }
    let total = 0
    for (let i = 0; i < 6; i++) total += angles[i]
    serial.writeLine("A," + mode + "," + angles.join(",") + "," + total)   // 마지막 값 = 각도 합계(검증용)
    serial.writeLine("R")
    listenPC(40)
})
