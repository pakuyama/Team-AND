/*
  AiHand PC 쪽 micro:bit - 무선 중계 (micro:bit V2 / MakeCode JavaScript)
  -------------------------------------------------------------------
  PC에 USB로 꽂아 두는 micro:bit예요. PC(web_server.py)와 로봇 쪽 micro:bit(robot_hub.ts) 사이를
  무선(micro:bit radio)으로 이어 줘요. 그래서 로봇 손과 PC 사이에 USB 선이 필요 없어요.

    PC (web_server.py) ─USB─ [이 micro:bit] ~~무선~~ [robot_hub.ts micro:bit] ─ 확장보드 ─ 서보

  [넣는 방법]
   1. makecode.microbit.org 에서 새 프로젝트 (확장 필요 없음, Bluetooth 확장은 넣지 마세요)
   2. 위쪽 [JavaScript] 탭을 누르고 이 파일 내용을 전부 붙여넣기 -> 다운로드
   3. RADIO_GROUP은 robot_hub.ts와 같은 번호여야 해요

  [화면]  ✓ = 로봇과 연결됨,  X = 로봇 신호 없음 (로봇 전원, 거리, 그룹 번호 확인)
          A 버튼: 지금 상태를 화면에 표시 (R = 수동, ♥ = 따라 하기)

  PC 쪽 글자 규칙은 로봇 micro:bit를 USB로 바로 꽂았을 때와 같아요 (web_server.py 수정 필요 없음).
   PC -> 이 micro:bit :  "W,a1..a6,dur,sum"   "S,a1..a6,dur"   "M,R" / "M,P"   "PING"
   이 micro:bit -> PC :  "A,<모드>,a1..a6,sum"   "R"   "# IR 0x.."   "READY"
*/

const RADIO_GROUP = 73                 // robot_hub.ts와 같은 번호
const MAGIC = 0xA5
const LOST_MS = 1000                   // 이 시간 동안 로봇 신호가 없으면 X 표시

let lastSeen = -100000
let linked = false
let robotMode = 0x52                   // 'R'

radio.setGroup(RADIO_GROUP)
radio.setTransmitPower(7)
serial.setBaudRate(BaudRate.BaudRate115200)
serial.setTxBufferSize(128)
serial.setRxBufferSize(128)

function hex2(n: number): string {
    const d = "0123456789ABCDEF"
    return d.charAt((n >> 4) & 15) + d.charAt(n & 15)
}

// ---------------- 로봇 -> PC ----------------
radio.onReceivedBuffer(function (b) {
    if (b.length < 3 || b[0] != MAGIC) return
    if (b[1] == 0x41 && b.length >= 9) {               // 'A' 상태
        robotMode = b[2]
        let s = "A," + (b[2] == 0x50 ? "P" : "R")
        let total = 0
        for (let i = 0; i < 6; i++) {
            s = s + "," + b[3 + i]
            total += b[3 + i]
        }
        serial.writeLine(s + "," + total)
        serial.writeLine("R")                           // PC가 다음 명령을 보내도 됨
        lastSeen = input.runningTime()
    } else if (b[1] == 0x49) {                          // 'I' 리모컨 버튼
        serial.writeLine("# IR 0x" + hex2(b[2]))
    }
})

// ---------------- PC -> 로봇 ----------------
function sendAngles(kind: number, p: string[]): boolean {
    let b = pins.createBuffer(10)
    b[0] = MAGIC
    b[1] = kind
    let sum = 0
    for (let i = 0; i < 7; i++) {
        let n = parseInt(p[i + 1])
        if (!(n >= 0 && n <= 2000)) return false        // 숫자가 아니거나 범위 밖이면 버림
        if (i < 6 && n > 180) return false
        sum += n
        if (i < 6) b[2 + i] = n
        else {
            b[8] = n & 0xff
            b[9] = (n >> 8) & 0xff
        }
    }
    if (kind == 0x57 && parseInt(p[8]) != sum) return false   // 깨진 웹 명령은 버림
    radio.sendBuffer(b)
    return true
}

function handlePC(line: string) {
    if (line.length > 0 && line.charAt(line.length - 1) == "\r") line = line.substr(0, line.length - 1)
    if (line == "PING") {
        serial.writeLine("READY")
        return
    }
    let p = line.split(",")
    if (p[0] == "W" && p.length == 9) sendAngles(0x57, p)
    else if (p[0] == "S" && p.length == 8) sendAngles(0x53, p)
    else if (p[0] == "M" && p.length == 2 && (p[1] == "R" || p[1] == "P")) {
        let b = pins.createBuffer(3)
        b[0] = MAGIC
        b[1] = 0x4D                                     // 'M'
        b[2] = p[1] == "P" ? 0x50 : 0x52
        radio.sendBuffer(b)
    }
}

serial.onDataReceived(serial.delimiters(Delimiters.NewLine), function () {
    handlePC(serial.readUntil(serial.delimiters(Delimiters.NewLine)))
})

input.onButtonPressed(Button.A, function () {
    if (robotMode == 0x50) basic.showIcon(IconNames.Heart, 0)
    else basic.showString("R")
    basic.pause(800)
    linked = !linked                                    // 아래에서 연결 표시를 다시 그리게
})

// ---------------- 연결 표시 ----------------
basic.showIcon(IconNames.No, 0)
basic.forever(function () {
    let now = input.runningTime() - lastSeen < LOST_MS
    if (now != linked) {
        linked = now
        if (linked) basic.showIcon(IconNames.Yes, 0)
        else basic.showIcon(IconNames.No, 0)
    }
    basic.pause(100)
})
