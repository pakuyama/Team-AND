"""AiHand 웹 제어 서버

웹 브라우저에서 카메라 영상을 보면서 로봇손을 조종합니다.
micro:bit에는 microbit_ir_hub.ts 가 들어 있어야 합니다.

실행:  python web_server.py                 (카메라 0번, micro:bit 자동 검색)
       python web_server.py --camera 1      (다른 카메라)
       python web_server.py --camera http://192.168.0.20:81/stream   (ESP32 손 카메라)
       python web_server.py --port COM5     (micro:bit 포트 직접 지정)
       python web_server.py --http 8080     (웹 포트 변경, 기본 8000)
접속:  이 PC에서 http://localhost:8000
       같은 와이파이의 휴대폰에서 http://<이 PC의 IP>:8000  (실행하면 주소가 표시됨)

※ 웹 조종과 리모컨은 함께 쓸 수 있습니다 (마지막 명령이 적용됨).
   비전(웹캠 따라하기) 모드일 때 웹으로 조종하면 수동 모드로 돌아옵니다.
"""
import argparse
import http.client
import json
import os
import socket
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2
import numpy as np
import serial
import serial.tools.list_ports

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- 카메라
class Camera:
    """source: 카메라 번호(0, 1, ...) 또는 MJPEG 영상 주소(ESP32 손 카메라 등)"""

    def __init__(self, source):
        self.source = source
        self.is_url = isinstance(source, str)
        self.jpeg = None
        self.ok = False
        self.enabled = True                # 웹에서 켜고 끔
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def _placeholder(self, text):
        img = np.full((480, 640, 3), 40, np.uint8)
        cv2.putText(img, text, (40, 240), cv2.FONT_HERSHEY_SIMPLEX, 1, (200, 200, 200), 2)
        return cv2.imencode(".jpg", img)[1].tobytes()

    def _set(self, jpg):
        with self.lock:
            self.jpeg = jpg

    def _run(self):
        off_img = self._placeholder("Camera OFF")
        fail_img = self._placeholder("Hand camera not reachable" if self.is_url
                                     else f"No camera {self.source}")
        while True:
            if not self.enabled:           # 꺼져 있으면 장치를 열지 않음
                self.ok = False
                self._set(off_img)
                time.sleep(0.3)
                continue
            got = self._stream_url() if self.is_url else self._stream_device()
            self.ok = False
            if not self.enabled:
                print("[카메라] 꺼짐")
            elif not got:                  # 열지 못함 -> 2초 뒤 다시 (그사이 꺼지면 바로 반영)
                self._set(fail_img)
                for _ in range(20):
                    if not self.enabled:
                        break
                    time.sleep(0.1)
            else:
                print("[카메라] 연결이 끊겼어요. 다시 시도합니다.")
                time.sleep(1)

    def _stream_device(self):
        """USB 카메라. 열지 못하면 False"""
        cap = cv2.VideoCapture(self.source, cv2.CAP_DSHOW)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        if not cap.isOpened():
            cap.release()
            return False
        print(f"[카메라] {self.source}번 카메라 연결됨")
        while self.enabled:
            good, frame = cap.read()
            if not good:
                break
            self.ok = True
            self._set(cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 70])[1].tobytes())
        cap.release()
        return True

    def _stream_url(self):
        """MJPEG 주소 (ESP32 손 카메라). JPEG를 다시 압축하지 않고 그대로 넘김. 연결 못 하면 False"""
        try:
            r = urllib.request.urlopen(self.source, timeout=5)
        except (urllib.error.URLError, OSError, ValueError) as e:
            print(f"[카메라] {self.source} 연결 실패: {e}")
            return False
        print(f"[카메라] {self.source} 연결됨")
        buf = b""
        try:
            with r:
                while self.enabled:
                    chunk = r.read1(65536)
                    if not chunk:
                        break
                    buf += chunk
                    while True:                        # 완성된 JPEG(FFD8 ~ FFD9) 꺼내기
                        s = buf.find(b"\xff\xd8")
                        if s < 0:
                            buf = buf[-1:]
                            break
                        e = buf.find(b"\xff\xd9", s + 2)
                        if e < 0:
                            buf = buf[s:]
                            break
                        self.ok = True
                        self._set(buf[s:e + 2])
                        buf = buf[e + 2:]
                    if len(buf) > 2_000_000:           # 이상한 데이터가 쌓이면 비움
                        buf = b""
        except (OSError, http.client.HTTPException) as e:
            print(f"[카메라] 영상 끊김: {e}")
        return True

    def get(self):
        with self.lock:
            return self.jpeg


# ---------------------------------------------------------------- micro:bit
class Hub:
    """microbit_ir_hub.ts 와 통신. 'R'을 받으면 대기 중인 명령을 보냄."""

    def __init__(self, port=None):
        self.want_port = port
        self.port = None
        self.ser = None
        self.mode = "?"                    # R = 리모컨, P = 웹/비전
        self.angles = [90] * 6
        self.last_seen = 0.0
        self.pending = None                # 다음 'R'에 보낼 (angles, dur)
        self.target = None                 # 적용 확인 중인 명령
        self.tries = 0
        self.sent_at = self.cmd_at = 0.0
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
                self.last_seen = time.time()
                # 보낸 명령이 아직 적용 안 됐으면 다시 보내기 (최대 5번)
                # (명령 후 1.5초까지만: 그 뒤 리모컨으로 움직인 건 덮어쓰지 않음)
                now = time.time()
                if self.target and angles != self.target[0] and now - self.cmd_at < 1.5:
                    if now - self.sent_at > 0.4 and self.tries < 5 and not self.pending:
                        self.pending = self.target
                        self.tries += 1
                else:
                    self.target = None
        elif line == "R":
            with self.lock:
                cmd = self.pending
                self.pending = None
                if cmd:
                    self.sent_at = time.time()
            if cmd:
                a, dur = cmd
                # W = 웹 조종 (리모컨과 함께 언제든 적용), 마지막 값 = 합계(검증용)
                nums = a + [dur]
                self.ser.write(("W," + ",".join(map(str, nums)) + f",{sum(nums)}\n").encode())
        elif line.startswith("# IR"):
            self.last_ir = line[2:]

    def command(self, angles, dur):
        a = [max(0, min(180, int(round(v)))) for v in angles[:6]]
        with self.lock:
            self.pending = (a, max(20, min(2000, int(dur))))
            self.target = self.pending      # 적용될 때까지 확인할 목표
            self.tries = 0
            self.sent_at = self.cmd_at = time.time()

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

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
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
                self._json(st)
            else:
                self._json({"error": "not found"}, 404)

        def do_POST(self):
            if self.path == "/api/camera":
                try:
                    n = int(self.headers.get("Content-Length", 0))
                    cam.enabled = bool(json.loads(self.rfile.read(n))["on"])
                except (ValueError, KeyError, TypeError):
                    self._json({"error": "on 값(true/false)이 필요해요"}, 400)
                    return
                print("[카메라] 켜기" if cam.enabled else "[카메라] 끄기 요청")
                self._json({"ok": True, "cameraOn": cam.enabled})
                return
            if self.path != "/api/servo":
                self._json({"error": "not found"}, 404)
                return
            try:
                n = int(self.headers.get("Content-Length", 0))
                data = json.loads(self.rfile.read(n))
                angles = data["angles"]
                if len(angles) != 6:
                    raise ValueError
                hub.command(angles, data.get("dur", 150))
            except (ValueError, KeyError, TypeError):
                self._json({"error": "angles 6개가 필요해요"}, 400)
                return
            self._json({"ok": True})

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
    ap = argparse.ArgumentParser(description="AiHand 웹 제어 서버")
    ap.add_argument("--camera", default="0",
                    help="카메라 번호(기본 0) 또는 손 카메라 영상 주소 (예: http://192.168.0.20:81/stream)")
    ap.add_argument("--port", help="micro:bit COM 포트 (생략하면 자동 검색)")
    ap.add_argument("--http", type=int, default=8000, help="웹 서버 포트 (기본 8000)")
    args = ap.parse_args()

    cam = Camera(int(args.camera) if args.camera.isdigit() else args.camera)
    hub = Hub(args.port)
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
