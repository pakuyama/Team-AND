/*
  IR 센서 포트 찾기 테스트 (MakeCode JavaScript, 확장 필요 없음)
  1. IR 센서를 확장보드 포트(1~6) 중 하나에 꽂기 (핀 순서 확인 후!)
  2. 이 코드를 MakeCode JavaScript 탭에 붙여넣고 다운로드
  3. 리모컨을 센서에 대고 아무 버튼이나 누르기
  -> LED에 센서가 연결된 micro:bit 핀 번호가 표시됩니다 (예: 1 이면 P1)
  처음에 X 가 나오면 그 핀은 원래 LOW 라서 검사에서 제외된 것입니다.
*/
const PINS = [DigitalPin.P0, DigitalPin.P1, DigitalPin.P2, DigitalPin.P13,
    DigitalPin.P14, DigitalPin.P15, DigitalPin.P16, DigitalPin.P19, DigitalPin.P20]
const NAMES = ["0", "1", "2", "13", "14", "15", "16", "19", "20"]
let skip = [false, false, false, false, false, false, false, false, false]

for (let i = 0; i < PINS.length; i++) {
    pins.setPull(PINS[i], PinPullMode.PullUp)
}
basic.pause(300)
for (let i = 0; i < PINS.length; i++) {
    if (pins.digitalReadPin(PINS[i]) == 0) {
        skip[i] = true          // 원래 LOW인 핀은 제외
        basic.showString("X" + NAMES[i])
    }
}
basic.showIcon(IconNames.Yes)

basic.forever(function () {
    for (let i = 0; i < PINS.length; i++) {
        if (!skip[i] && pins.digitalReadPin(PINS[i]) == 0) {
            basic.showString(NAMES[i])
            basic.pause(500)
            basic.showIcon(IconNames.Yes)
        }
    }
})
