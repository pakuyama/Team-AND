"""AiHand 최종 - 웹 제어 + 웹캠 손 따라 하기 서버

PC 웹캠 하나로
  1) 웹 화면에 영상을 보여 주고 (CCTV처럼 보기)
  2) 따라 하기 모드에서는 웹 화면(브라우저)이 그 영상으로 손을 인식해서 각도를 보내면
     로봇 손이 따라 해요 (손 인식: 브라우저용 MediaPipe, 인터넷 연결 필요)
로봇 손은 웹 화면, 리모컨 어느 쪽으로든 조종할 수 있어요.

연결:  PC ─USB─ micro:bit(pc_bridge.ts) ~~무선~~ micro:bit(robot_hub.ts) ─ 확장보드 ─ 서보
       (robot_hub.ts micro:bit를 USB로 PC에 바로 꽂아도 돼요)

실행:  python web_server.py                 (카메라 0번, micro:bit 자동 검색)
       python web_server.py --camera 1      (다른 카메라)
       python web_server.py --port COM5     (micro:bit 포트 직접 지정)
       python web_server.py --http 8080     (웹 포트 변경, 기본 8000)
접속:  이 PC에서 http://localhost:8000
       같은 와이파이의 휴대폰에서 http://<이 PC의 IP>:8000  (실행하면 주소가 표시됨)
"""
import argparse
import json
import os
import socket
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2
import numpy as np
import serial
import serial.tools.list_ports

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "hand_landmarker.task")
MODEL_URL = ("https://storage.googleapis.com/mediapipe-models/hand_landmarker/"
             "hand_landmarker/float16/1/hand_landmarker.task")


def clamp(v, a, b):
    return max(a, min(b, v))


# ---------------------------------------------------------------- 카메라
class Camera:
    """PC 웹캠. 영상을 JPEG로 만들어 둠 (거울 모드)."""

    def __init__(self, source):
        self.source = source
        self.jpeg = None
        self.ok = False
        self.enabled = True                    # 웹에서 켜고 끔
        self.fps = 0.0
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    @staticmethod
    def _placeholder(text):
        img = np.full((480, 640, 3), 40, np.uint8)
        cv2.putText(img, text, (40, 240), cv2.FONT_HERSHEY_SIMPLEX, 1, (200, 200, 200), 2)
        return cv2.imencode(".jpg", img)[1].tobytes()

    def _set(self, jpg):
        with self.lock:
            self.jpeg = jpg

    def _run(self):
        off_img = self._placeholder("Camera OFF")
        fail_img = self._placeholder(f"No camera {self.source}")
        while True:
            if not self.enabled:               # 꺼져 있으면 장치를 열지 않음
                self.ok = False
                self._set(off_img)
                time.sleep(0.3)
                continue
            if not self._stream():
                self._set(fail_img)            # 열지 못함 -> 2초 뒤 다시
                for _ in range(20):
                    if not self.enabled:
                        break
                    time.sleep(0.1)
            elif self.enabled:
                print("[카메라] 연결이 끊겼어요. 다시 시도합니다.")
                time.sleep(1)
            self.ok = False

    def _stream(self):
        cap = cv2.VideoCapture(self.source, cv2.CAP_DSHOW)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        if not cap.isOpened():
            cap.release()
            return False
        print(f"[카메라] {self.source}번 카메라 연결됨")
        t0, n = time.time(), 0
        while self.enabled:
            good, frame = cap.read()
            if not good:
                break
            frame = cv2.flip(frame, 1)         # 거울 모드 (내 손과 같은 방향)
            self.ok = True
            self._set(cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 70])[1].tobytes())
            n += 1
            if time.time() - t0 >= 1:
                self.fps, t0, n = n / (time.time() - t0), time.time(), 0
        cap.release()
        return True

    def get(self):
        with self.lock:
            return self.jpeg


# ---------------------------------------------------------------- micro:bit
class Hub:
    """robot_hub.ts (또는 pc_bridge.ts를 거쳐서) 와 통신. 'R'을 받으면 대기 중인 명령을 보냄."""

    def __init__(self, port=None):
        self.want_port = port
        self.port = None
        self.ser = None
        self.mode = "?"                    # R = 수동, P = 따라 하기
        self.angles = [90] * 6
        self.last_seen = 0.0
        self.pending = None                # 다음 'R'에 보낼 웹 조종 (angles, dur)
        self.target = None                 # 적용 확인 중인 웹 조종
        self.tries = 0
        self.sent_at = self.cmd_at = 0.0
        self.pending_mode = None           # 다음 'R'에 보낼 모드 바꾸기
        self.mode_target = None            # 적용 확인 중인 모드
        self.mode_at = self.mode_sent = 0.0
        self.follow = None                 # 다음 'R'에 보낼 따라 하기 각도
        self.follow_last = None
        self.follow_at = 0.0
        self.last_ir = ""
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    @staticmethod
    def find_port():
        for p in serial.tools.list_ports.comports():
            if p.vid == 0x0D28:
                return p.device
        return None

    def _run(self):
        while True:
            port = self.want_port or self.find_port()
            if not port:
                time.sleep(2)
                continue
            try:
                self.ser = serial.Serial(port, 115200, timeout=0.2)
                self.port = port
                print(f"[micro:bit] {port} 연결됨")
                self._read_loop()
            except serial.SerialException as e:
                print(f"[micro:bit] {port} 사용 불가: {e}")
            self.ser = None
            self.port = None
            time.sleep(2)

    def _read_loop(self):
        buf = b""
        while True:
            buf += self.ser.read(256)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self._handle(line.strip().decode(errors="ignore"))

    def _handle(self, line):
        if line.startswith("A,"):
            # A,모드,각도6개,합계  -> 글자가 빠져 깨진 줄은 합계가 안 맞아서 버림
            parts = line.split(",")
            if len(parts) != 9 or parts[1] not in ("R", "P"):
                return
            try:
                nums = [int(v) for v in parts[2:]]
            except ValueError:
                return
            angles, total = nums[:6], nums[6]
            if sum(angles) != total or not all(0 <= a <= 180 for a in angles):
                return
            with self.lock:
                self.mode = parts[1]
                self.angles = angles
                self.last_seen = now = time.time()
                # 모드 바꾸기가 안 먹혔으면 다시 보내기 (2.5초까지)
                if self.mode_target:
                    if parts[1] == self.mode_target or now - self.mode_at > 2.5:
                        self.mode_target = None
                    elif now - self.mode_sent > 0.5 and not self.pending_mode:
                        self.pending_mode = self.mode_target
                # 웹 조종이 아직 적용 안 됐으면 다시 보내기 (최대 5번, 명령 후 1.5초까지만)
                if self.target and angles != self.target[0] and now - self.cmd_at < 1.5:
                    if now - self.sent_at > 0.4 and self.tries < 5 and not self.pending:
                        self.pending = self.target
                        self.tries += 1
                else:
                    self.target = None
        elif line == "R":
            msg = None
            with self.lock:
                if self.pending_mode:                      # 1순위: 모드 바꾸기
                    msg = f"M,{self.pending_mode}"
                    self.pending_mode = None
                    self.mode_sent = time.time()
                elif self.pending:                         # 2순위: 웹 조종
                    a, dur = self.pending
                    self.pending = None
                    self.sent_at = time.time()
                    nums = a + [dur]
                    msg = "W," + ",".join(map(str, nums)) + f",{sum(nums)}"
                elif self.follow and self.mode == "P":     # 3순위: 따라 하기
                    msg = "S," + ",".join(map(str, self.follow)) + ",120"
                    self.follow = None
            if msg:
                self.ser.write((msg + "\n").encode())
        elif line.startswith("# IR"):
            self.last_ir = line[2:]

    def command(self, angles, dur):
        """웹 조종 (슬라이더, 동작 버튼)"""
        a = [max(0, min(180, int(round(v)))) for v in angles[:6]]
        with self.lock:
            self.pending = (a, max(20, min(2000, int(dur))))
            self.target = self.pending
            self.tries = 0
            self.sent_at = self.cmd_at = time.time()
            self.follow = None

    def set_mode(self, m):
        with self.lock:
            self.pending_mode = self.mode_target = m
            self.mode_at = time.time()
            self.pending = self.target = None   # 다시 보내기가 모드를 수동으로 되돌리지 않게
            self.follow = self.follow_last = None

    def follow_hand(self, fingers):
        """따라 하기: 손가락 5개 각도. 회전대는 지금 위치 그대로.
        조금이라도 바뀌면 바로, 그대로면 1초마다 한 번 (무선으로 빠진 명령 메우기)."""
        with self.lock:
            if self.mode != "P" or self.mode_target:
                return
            a = [int(round(clamp(v, 0, 180))) for v in fingers] + [self.angles[5]]
            now = time.time()
            changed = self.follow_last is None or any(abs(x - y) >= 2 for x, y in zip(a, self.follow_last))
            if changed or now - self.follow_at > 1.0:
                self.follow = self.follow_last = a
                self.follow_at = now

    def status(self):
        with self.lock:
            alive = self.ser is not None and time.time() - self.last_seen < 2
            return {
                "connected": alive,
                "port": self.port,
                "mode": self.mode if alive else "?",
                "angles": self.angles,
                "lastIR": self.last_ir,
            }


# ---------------------------------------------------------------- 웹 서버
def make_handler(cam, hub):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def _json(self, obj, code=200):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _body(self):
            n = int(self.headers.get("Content-Length", 0))
            return json.loads(self.rfile.read(n))

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/hand_landmarker.task":     # 브라우저 손 인식 모델
                if not os.path.exists(MODEL):
                    print("[손 인식] 모델 다운로드 중... (처음 한 번만)")
                    urllib.request.urlretrieve(MODEL_URL, MODEL)
                with open(MODEL, "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path.startswith("/stream.mjpg"):
                self.send_response(200)
                self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                try:
                    while True:
                        jpg = cam.get()
                        if jpg:
                            self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n"
                                             + f"Content-Length: {len(jpg)}\r\n\r\n".encode()
                                             + jpg + b"\r\n")
                        time.sleep(0.05)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
            elif self.path == "/api/status":
                st = hub.status()
                st["camera"] = cam.ok
                st["cameraOn"] = cam.enabled
                st["fps"] = round(cam.fps)
                self._json(st)
            else:
                self._json({"error": "not found"}, 404)

        def do_POST(self):
            try:
                data = self._body()
                if self.path == "/api/camera":
                    cam.enabled = bool(data["on"])
                    print("[카메라] 켜기" if cam.enabled else "[카메라] 끄기")
                    self._json({"ok": True, "cameraOn": cam.enabled})
                elif self.path == "/api/mode":
                    m = data["mode"]
                    if m not in ("R", "P"):
                        raise ValueError
                    hub.set_mode(m)
                    if m == "P" and not cam.enabled:
                        cam.enabled = True     # 따라 하려면 카메라가 켜져 있어야 함
                    print("[모드] 따라 하기" if m == "P" else "[모드] 수동 조종")
                    self._json({"ok": True})
                elif self.path == "/api/follow":
                    # 브라우저가 인식한 손가락 각도 5개 (따라 하기 모드일 때만 보냄)
                    fingers = [float(v) for v in data["fingers"]]
                    if len(fingers) != 5:
                        raise ValueError
                    hub.follow_hand(fingers)
                    self._json({"ok": True})
                elif self.path == "/api/servo":
                    angles = data["angles"]
                    if len(angles) != 6:
                        raise ValueError
                    hub.command(angles, data.get("dur", 150))
                    self._json({"ok": True})
                else:
                    self._json({"error": "not found"}, 404)
            except (ValueError, KeyError, TypeError):
                self._json({"error": "잘못된 요청이에요"}, 400)

    return Handler


def lan_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return None


def main():
    ap = argparse.ArgumentParser(description="AiHand 최종 - 웹 제어 + 웹캠 손 따라 하기")
    ap.add_argument("--camera", type=int, default=0, help="웹캠 번호 (기본 0)")
    ap.add_argument("--port", help="micro:bit COM 포트 (생략하면 자동 검색)")
    ap.add_argument("--http", type=int, default=8000, help="웹 서버 포트 (기본 8000)")
    args = ap.parse_args()

    hub = Hub(args.port)
    cam = Camera(args.camera)
    server = ThreadingHTTPServer(("0.0.0.0", args.http), make_handler(cam, hub))
    server.daemon_threads = True
    print(f"\n웹 주소:  http://localhost:{args.http}")
    ip = lan_ip()
    if ip:
        print(f"휴대폰(같은 와이파이): http://{ip}:{args.http}")
    print("종료: 이 창에서 Ctrl+C\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
