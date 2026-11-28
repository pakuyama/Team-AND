/*
  AiHand CCTV (XIAO ESP32S3 Sense)  -  김다혜 P4
  ------------------------------------------------------------------
  김윤하 P4(손 카메라 영상 보내기)에서 한 걸음 더 나가서, CCTV처럼 쓰는 코드예요.

  [할 수 있는 것]
   - 실시간 영상: 여러 기기에서 동시에 보기 (최대 3대)       http://<IP>:81/stream
   - 움직임 감지: 화면이 바뀌면 "움직임" 표시
   - 자동 녹화:   움직임이 있으면 microSD에 사진을 0.5초마다 저장 (사건별 폴더)
   - 녹화 보기:   웹에서 사건 목록 → 사진 넘겨 보기 / 재생 / 내려받기 / 지우기
   - 스냅샷:      지금 장면을 JPG로 내려받기                     http://<IP>/capture
   - 시계:        인터넷 시간(NTP), 인터넷이 없으면 웹을 연 기기의 시간으로 맞춤
   - 와이파이 설정 화면: 코드에 비밀번호를 안 넣어도 됨 (아래 [와이파이] 참고)
   - 영상 주소가 김윤하 P4와 같아서 (…:81/stream) 팀 웹 화면에서도 그대로 볼 수 있음

  [Arduino IDE 설정]  (김윤하 P4와 같음)
   보드: XIAO_ESP32S3 / PSRAM: OPI PSRAM / USB CDC On Boot: Enabled
   Partition Scheme: 기본값(8M with spiffs 등) 그대로

  [와이파이]
   1. 처음 켜면 와이파이 "AiHand-CCTV-xxxx" (비밀번호 aihand1234)가 생겨요.
   2. 폰으로 거기에 연결 → 브라우저에서 http://192.168.4.1 → 맨 아래 [와이파이 설정]에 공유기 이름/비밀번호 저장
   3. 다시 시작하면 공유기에 연결되고, 시리얼 모니터와 http://aihand-cctv.local 로 주소를 알 수 있어요.
   - 공유기에 연결이 안 되면 다시 "AiHand-CCTV-xxxx" 모드로 켜져요. (이 모드에서도 CCTV는 그대로 동작)
   - 저장한 와이파이 지우기: BOOT 버튼을 누른 채로 RESET → BOOT를 3초 더 누르고 있기
   - 코드에 직접 넣고 싶으면 secrets_example.h를 secrets.h로 복사해서 쓰세요. (secrets.h는 깃에 안 올라감)
*/
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <atomic>
#include <vector>
#include <algorithm>
#include <time.h>
#include <sys/time.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "page.h"

#if __has_include("secrets.h")
  #include "secrets.h"            // WIFI_SSID, WIFI_PASS 를 넣어 두는 파일 (깃에 안 올림)
#endif
#ifndef WIFI_SSID
  #define WIFI_SSID ""
  #define WIFI_PASS ""
#endif

// ================= 설정 =================
#define HOSTNAME      "aihand-cctv"     // http://aihand-cctv.local
#define AP_PASS       "aihand1234"      // 설정 모드 와이파이 비밀번호 (8글자 이상)
#define MAX_VIEWERS   3                 // 동시에 볼 수 있는 기기 수
#define FRAME_CAP     (320 * 1024)      // 사진 한 장 최대 크기 (PSRAM)
#define SD_CS_PIN     21                // XIAO ESP32S3 Sense microSD
#define BOOT_PIN      0                 // BOOT 버튼
#define MOTION_EVERY  250               // 움직임 검사 간격 (ms)
#define REC_EVERY     500               // 녹화 간격 (ms) = 1초에 2장
#define REC_TAIL      5000              // 움직임이 멈춘 뒤 이만큼 더 녹화 (ms)
#define REC_MAX       240               // 사건 하나당 최대 사진 수 (2분)
#define SD_MIN_FREE   0.10              // 남은 공간이 10%보다 적으면 가장 오래된 사건부터 지움

// XIAO ESP32S3 Sense 카메라 핀 (김윤하 P4와 같음)
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

// ================= 상태 =================
struct Settings {
  int size = FRAMESIZE_VGA;    // 해상도
  int quality = 12;            // JPEG 화질 (10~40, 낮을수록 좋음)
  bool vflip = false, hmirror = false;
  bool motion = true;          // 움직임 감지
  bool record = true;          // 움직임 녹화 (SD 필요)
  int sens = 25;               // 점 하나가 "바뀌었다"고 볼 밝기 차이 (5~80)
  int area = 3;                // 화면의 몇 %가 바뀌면 움직임으로 볼지 (1~30)
} cfg;

Preferences prefs;
WebServer web(80);
WiFiServer streamServer(81);

uint8_t* latestBuf = nullptr;            // 가장 최근 사진 (모든 시청자가 같이 씀)
size_t latestLen = 0;
volatile uint32_t frameNo = 0;
SemaphoreHandle_t frameMux;

std::atomic<int> viewers{0};
volatile float fps = 0;
volatile bool motionNow = false;
volatile int motionLevel = 0;            // 바뀐 비율 (0.1% 단위)
volatile uint32_t lastMotionMs = 0;
volatile uint32_t motionResetAt = 0;     // 설정 바꾼 직후에는 잠깐 감지 쉬기

bool apMode = false;
bool sdOk = false;
bool recording = false;
String recDir;
int recCount = 0;
uint32_t recLastMs = 0;
String lastEvent;

// ================= 시간 =================
bool timeValid() { return time(nullptr) > 1700000000; }   // 2023년 이후면 맞춰진 것

String nowStr(const char* fmt) {
  time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  char b[40];
  strftime(b, sizeof(b), fmt, &tm);
  return String(b);
}

// ================= 설정 저장 =================
void loadSettings() {
  prefs.begin("cctv", true);
  cfg.size = prefs.getInt("size", cfg.size);
  cfg.quality = prefs.getInt("quality", cfg.quality);
  cfg.vflip = prefs.getBool("vflip", cfg.vflip);
  cfg.hmirror = prefs.getBool("hmirror", cfg.hmirror);
  cfg.motion = prefs.getBool("motion", cfg.motion);
  cfg.record = prefs.getBool("record", cfg.record);
  cfg.sens = prefs.getInt("sens", cfg.sens);
  cfg.area = prefs.getInt("area", cfg.area);
  prefs.end();
}

void saveSettings() {
  prefs.begin("cctv", false);
  prefs.putInt("size", cfg.size);
  prefs.putInt("quality", cfg.quality);
  prefs.putBool("vflip", cfg.vflip);
  prefs.putBool("hmirror", cfg.hmirror);
  prefs.putBool("motion", cfg.motion);
  prefs.putBool("record", cfg.record);
  prefs.putInt("sens", cfg.sens);
  prefs.putInt("area", cfg.area);
  prefs.end();
}

void applyCamera() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_framesize(s, (framesize_t)cfg.size);
  s->set_quality(s, cfg.quality);
  s->set_vflip(s, cfg.vflip);
  s->set_hmirror(s, cfg.hmirror);
  motionResetAt = millis();
}

// ================= 카메라 + 움직임 감지 (따로 도는 작업) =================
// 사진을 계속 찍어서 latestBuf에 넣고, 0.25초마다 작게 줄인 흑백 화면을 이전 화면과 비교해요.
void cameraTask(void*) {
  uint8_t* small = (uint8_t*)ps_malloc(200 * 150 * 2);   // UXGA/8 크기까지
  uint8_t* prev = (uint8_t*)ps_malloc(200 * 150);
  int prevW = 0, prevH = 0, hits = 0;
  uint32_t lastCheck = 0, fpsT0 = millis(), fpsN = 0;

  for (;;) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

    if (fb->len <= FRAME_CAP) {
      xSemaphoreTake(frameMux, portMAX_DELAY);
      memcpy(latestBuf, fb->buf, fb->len);
      latestLen = fb->len;
      frameNo++;
      xSemaphoreGive(frameMux);
    }

    uint32_t now = millis();
    if (cfg.motion && small && prev && now - lastCheck >= MOTION_EVERY) {
      lastCheck = now;
      int w = fb->width / 8, h = fb->height / 8;
      if (w * h <= 200 * 150 && jpg2rgb565(fb->buf, fb->len, small, JPG_SCALE_8X)) {
        int changed = 0;
        bool fresh = (w != prevW || h != prevH || now - motionResetAt < 3000);
        for (int i = 0; i < w * h; i++) {
          uint8_t b0 = small[2 * i], b1 = small[2 * i + 1];
          int r = b0 >> 3, g = ((b0 & 7) << 3) | (b1 >> 5), b = b1 & 31;
          uint8_t y = (uint8_t)((r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8);   // 밝기 0~255
          if (!fresh && abs((int)y - (int)prev[i]) > cfg.sens) changed++;
          prev[i] = y;
        }
        prevW = w; prevH = h;
        int level = fresh ? 0 : changed * 1000 / (w * h);
        motionLevel = level;
        hits = (level >= cfg.area * 10) ? hits + 1 : 0;
        if (hits >= 2) {                  // 두 번 연속이면 진짜 움직임 (깜빡임 무시)
          motionNow = true;
          lastMotionMs = now;
        } else if (now - lastMotionMs > 1000) {
          motionNow = false;
        }
      }
    } else if (!cfg.motion) {
      motionNow = false;
      motionLevel = 0;
    }
    esp_camera_fb_return(fb);

    fpsN++;
    if (now - fpsT0 >= 2000) {
      fps = fpsN * 1000.0f / (now - fpsT0);
      fpsN = 0;
      fpsT0 = now;
    }
    vTaskDelay(1);
  }
}

// 최근 사진을 out에 복사 (out은 FRAME_CAP 크기)
size_t copyLatest(uint8_t* out, uint32_t* no = nullptr) {
  xSemaphoreTake(frameMux, portMAX_DELAY);
  size_t n = latestLen;
  if (n) memcpy(out, latestBuf, n);
  if (no) *no = frameNo;
  xSemaphoreGive(frameMux);
  return n;
}

// ================= 실시간 영상 (81번, 여러 명 동시에) =================
void viewerTask(void* arg) {
  WiFiClient* c = (WiFiClient*)arg;
  uint8_t* buf = (uint8_t*)ps_malloc(FRAME_CAP);

  // 요청 읽기 (첫 줄만 필요)
  String first;
  uint32_t t0 = millis();
  String line;
  while (c->connected() && millis() - t0 < 2000) {
    if (!c->available()) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    char ch = c->read();
    if (ch == '\n') {
      if (first.length() == 0) first = line;
      if (line.length() <= 1) break;     // 빈 줄 = 요청 끝
      line = "";
    } else line += ch;
  }

  if (buf && first.startsWith("GET /stream")) {
    c->print("HTTP/1.1 200 OK\r\n"
             "Content-Type: multipart/x-mixed-replace;boundary=frame\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Cache-Control: no-store\r\n"
             "Connection: close\r\n\r\n");
    Serial.printf("[영상] 시청 시작 (%d명)\n", viewers.load());
    uint32_t last = 0;
    char hdr[96];
    while (c->connected()) {
      if (frameNo == last) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
      uint32_t no;
      size_t n = copyLatest(buf, &no);
      last = no;
      if (!n) continue;
      int hn = snprintf(hdr, sizeof(hdr),
                        "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)n);
      if (c->write((const uint8_t*)hdr, hn) != (size_t)hn) break;
      if (c->write(buf, n) != n) break;
    }
    Serial.println("[영상] 시청 끝");
  } else {
    c->print("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
             "use /stream (or port 80 for the web page)\n");
  }
  c->stop();
  delete c;
  if (buf) free(buf);
  viewers--;
  vTaskDelete(NULL);
}

void streamAcceptTask(void*) {
  streamServer.begin();
  for (;;) {
    WiFiClient client = streamServer.accept();
    if (client) {
      if (viewers.load() >= MAX_VIEWERS) {
        client.print("HTTP/1.1 503 Busy\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
                     "too many viewers\n");
        client.stop();
      } else {
        viewers++;
        WiFiClient* c = new WiFiClient(client);
        if (xTaskCreatePinnedToCore(viewerTask, "viewer", 6144, c, 2, NULL, 0) != pdPASS) {
          c->stop();
          delete c;
          viewers--;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ================= microSD 녹화 =================
uint64_t sdFree() { return SD.totalBytes() - SD.usedBytes(); }

std::vector<String> listEvents() {        // 새것부터
  std::vector<String> v;
  File root = SD.open("/cctv");
  if (!root) return v;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) {
      String n = f.name();
      int s = n.lastIndexOf('/');
      v.push_back(s >= 0 ? n.substring(s + 1) : n);
    }
    f.close();
  }
  root.close();
  std::sort(v.begin(), v.end(), [](const String& a, const String& b) { return a > b; });
  return v;
}

bool validId(const String& id) {
  if (id.length() == 0 || id.length() > 32) return false;
  for (char ch : id) if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') return false;
  return true;
}

void deleteEvent(const String& id) {
  String dir = "/cctv/" + id;
  File d = SD.open(dir);
  if (!d) return;
  std::vector<String> files;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String n = f.name();
    int s = n.lastIndexOf('/');
    files.push_back(dir + "/" + (s >= 0 ? n.substring(s + 1) : n));
    f.close();
  }
  d.close();
  for (auto& p : files) SD.remove(p);
  SD.rmdir(dir);
}

void makeRoom() {
  if (!sdOk) return;
  for (int i = 0; i < 20 && sdFree() < SD.totalBytes() * SD_MIN_FREE; i++) {
    auto ev = listEvents();
    if (ev.empty()) break;
    if (ev.back() == recDir) break;         // 지금 녹화 중인 건 안 지움
    Serial.println("[SD] 공간 부족 -> 오래된 사건 지움: " + ev.back());
    deleteEvent(ev.back());
  }
}

void recordStep(uint8_t* buf) {
  uint32_t now = millis();
  bool want = cfg.record && sdOk && cfg.motion && (motionNow || (recording && now - lastMotionMs < REC_TAIL));

  if (want && !recording) {
    makeRoom();
    recDir = timeValid() ? nowStr("%Y%m%d_%H%M%S") : ("boot_" + String(now / 1000));
    SD.mkdir("/cctv");
    SD.mkdir("/cctv/" + recDir);
    recording = true;
    recCount = 0;
    recLastMs = 0;
    Serial.println("[녹화] 시작 " + recDir);
  }
  if (recording && (!want || recCount >= REC_MAX)) {
    Serial.printf("[녹화] 끝 %s (%d장)\n", recDir.c_str(), recCount);
    lastEvent = recDir;
    recording = false;
    return;
  }
  if (recording && now - recLastMs >= REC_EVERY) {
    recLastMs = now;
    size_t n = copyLatest(buf);
    if (!n) return;
    char name[64];
    snprintf(name, sizeof(name), "/cctv/%s/%04d.jpg", recDir.c_str(), recCount);
    File f = SD.open(name, FILE_WRITE);
    if (!f) { Serial.println("[녹화] 저장 실패 - SD 카드 확인"); sdOk = false; recording = false; return; }
    f.write(buf, n);
    f.close();
    recCount++;
  }
}

// ================= 웹 (80번) =================
uint8_t* webBuf = nullptr;

void sendJson(const String& s) {
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", s);
}

void handleCapture() {
  size_t n = copyLatest(webBuf);
  if (!n) { web.send(503, "text/plain", "no frame yet"); return; }
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("Access-Control-Allow-Origin", "*");
  if (web.hasArg("dl"))
    web.sendHeader("Content-Disposition", "attachment; filename=\"cctv_" + nowStr("%Y%m%d_%H%M%S") + ".jpg\"");
  web.setContentLength(n);
  web.send(200, "image/jpeg", "");
  web.client().write(webBuf, n);
}

void handleStatus() {
  String s = "{";
  s += "\"time\":\"" + (timeValid() ? nowStr("%Y-%m-%d %H:%M:%S") : String("")) + "\"";
  s += ",\"uptime\":" + String(millis() / 1000);
  s += ",\"ap\":" + String(apMode ? "true" : "false");
  s += ",\"ip\":\"" + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
  s += ",\"ssid\":\"" + (apMode ? WiFi.softAPSSID() : WiFi.SSID()) + "\"";
  s += ",\"rssi\":" + String(apMode ? 0 : WiFi.RSSI());
  s += ",\"fps\":" + String(fps, 1);
  s += ",\"viewers\":" + String(viewers.load());
  s += ",\"maxViewers\":" + String(MAX_VIEWERS);
  s += ",\"motion\":" + String(motionNow ? "true" : "false");
  s += ",\"level\":" + String(motionLevel / 10.0f, 1);
  s += ",\"recording\":" + String(recording ? "true" : "false");
  s += ",\"recDir\":\"" + recDir + "\",\"recCount\":" + String(recCount);
  s += ",\"sd\":" + String(sdOk ? "true" : "false");
  if (sdOk) {
    s += ",\"sdTotalMB\":" + String((uint32_t)(SD.totalBytes() / 1048576ULL));
    s += ",\"sdUsedMB\":" + String((uint32_t)(SD.usedBytes() / 1048576ULL));
  }
  s += ",\"heap\":" + String(ESP.getFreeHeap() / 1024);
  s += ",\"cfg\":{\"size\":" + String(cfg.size) + ",\"quality\":" + String(cfg.quality) +
       ",\"vflip\":" + String(cfg.vflip) + ",\"hmirror\":" + String(cfg.hmirror) +
       ",\"motion\":" + String(cfg.motion) + ",\"record\":" + String(cfg.record) +
       ",\"sens\":" + String(cfg.sens) + ",\"area\":" + String(cfg.area) + "}";
  s += "}";
  sendJson(s);
}

void handleSet() {
  auto has = [](const char* k) { return web.hasArg(k); };
  auto num = [](const char* k) { return web.arg(k).toInt(); };
  if (has("size")) {
    int v = num("size");
    if (v == FRAMESIZE_QVGA || v == FRAMESIZE_VGA || v == FRAMESIZE_SVGA || v == FRAMESIZE_HD || v == FRAMESIZE_SXGA)
      cfg.size = v;
  }
  if (has("quality")) cfg.quality = constrain(num("quality"), 10, 40);
  if (has("vflip")) cfg.vflip = num("vflip") != 0;
  if (has("hmirror")) cfg.hmirror = num("hmirror") != 0;
  if (has("motion")) cfg.motion = num("motion") != 0;
  if (has("record")) cfg.record = num("record") != 0;
  if (has("sens")) cfg.sens = constrain(num("sens"), 5, 80);
  if (has("area")) cfg.area = constrain(num("area"), 1, 30);
  applyCamera();
  saveSettings();
  handleStatus();
}

void handleTime() {                       // 인터넷 시간이 없을 때 웹을 연 기기의 시간으로 맞춤
  long long t = atoll(web.arg("t").c_str());
  if (!timeValid() && t > 1700000000LL) {
    struct timeval tv = { (time_t)t, 0 };
    settimeofday(&tv, nullptr);
    Serial.println("[시간] 브라우저 시간으로 맞춤: " + nowStr("%Y-%m-%d %H:%M:%S"));
  }
  sendJson("{\"ok\":true}");
}

void handleEvents() {
  String s = "{\"sd\":" + String(sdOk ? "true" : "false") + ",\"events\":[";
  if (sdOk) {
    auto ev = listEvents();
    int n = 0;
    for (auto& id : ev) {
      if (n >= 50) break;
      if (n++) s += ",";
      s += "\"" + id + "\"";
    }
  }
  s += "],\"recording\":\"" + String(recording ? recDir : "") + "\"}";
  sendJson(s);
}

void handleEvent() {                      // 사건 하나의 사진 이름들
  String id = web.arg("id");
  if (!sdOk || !validId(id)) { web.send(400, "text/plain", "bad id"); return; }
  if (web.hasArg("delete")) {
    if (recording && id == recDir) { web.send(409, "text/plain", "recording"); return; }
    deleteEvent(id);
    sendJson("{\"ok\":true}");
    return;
  }
  std::vector<String> files;
  File d = SD.open("/cctv/" + id);
  if (d) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      String n = f.name();
      int sl = n.lastIndexOf('/');
      n = sl >= 0 ? n.substring(sl + 1) : n;
      if (n.endsWith(".jpg")) files.push_back(n);
      f.close();
    }
    d.close();
  }
  std::sort(files.begin(), files.end());
  String s = "{\"id\":\"" + id + "\",\"files\":[";
  for (size_t i = 0; i < files.size(); i++) s += (i ? ",\"" : "\"") + files[i] + "\"";
  s += "]}";
  sendJson(s);
}

void handleSdFile() {                     // /sd?id=…&f=0000.jpg
  String id = web.arg("id"), f = web.arg("f");
  if (!sdOk || !validId(id) || !f.endsWith(".jpg") || !validId(f.substring(0, f.length() - 4))) {
    web.send(400, "text/plain", "bad path");
    return;
  }
  File file = SD.open("/cctv/" + id + "/" + f);
  if (!file) { web.send(404, "text/plain", "not found"); return; }
  web.sendHeader("Cache-Control", "max-age=86400");
  if (web.hasArg("dl")) web.sendHeader("Content-Disposition", "attachment; filename=\"" + id + "_" + f + "\"");
  web.streamFile(file, "image/jpeg");
  file.close();
}

void handleWifiSave() {
  String ssid = web.arg("ssid"), pass = web.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    web.send(400, "text/plain; charset=utf-8", "와이파이 이름을 확인하세요");
    return;
  }
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  web.send(200, "text/html; charset=utf-8",
           "<meta charset=utf-8><meta name=viewport content='width=device-width'>"
           "<p style='font-family:sans-serif;padding:16px'>저장했어요. 3초 뒤 다시 시작해서 <b>" + ssid +
           "</b>에 연결해요.<br>연결되면 같은 와이파이에서 <b>http://" HOSTNAME ".local</b> 로 들어오세요.<br>"
           "연결에 실패하면 다시 설정 모드 와이파이가 켜져요.</p>");
  delay(3000);
  ESP.restart();
}

void startWeb() {
  web.on("/", [] { web.send_P(200, "text/html; charset=utf-8", PAGE_HTML); });
  web.on("/capture", handleCapture);
  web.on("/api/status", handleStatus);
  web.on("/api/set", handleSet);
  web.on("/api/time", handleTime);
  web.on("/api/events", handleEvents);
  web.on("/api/event", handleEvent);
  web.on("/sd", handleSdFile);
  web.on("/api/wifi", HTTP_POST, handleWifiSave);
  web.on("/api/reboot", HTTP_POST, [] { sendJson("{\"ok\":true}"); delay(500); ESP.restart(); });
  web.onNotFound([] { web.send(404, "text/plain", "not found"); });
  web.begin();
}

// ================= 와이파이 =================
void clearWifiIfBootHeld() {
  pinMode(BOOT_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_PIN) != LOW) return;
  Serial.println("BOOT 버튼 눌림 - 3초 더 누르면 저장한 와이파이를 지워요");
  uint32_t t0 = millis();
  while (digitalRead(BOOT_PIN) == LOW) {
    if (millis() - t0 > 3000) {
      prefs.begin("wifi", false);
      prefs.clear();
      prefs.end();
      Serial.println("저장한 와이파이를 지웠어요");
      while (digitalRead(BOOT_PIN) == LOW) delay(10);
      return;
    }
    delay(10);
  }
}

bool connectWifi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", WIFI_SSID);
  String pass = prefs.getString("pass", WIFI_PASS);
  prefs.end();
  if (ssid.length() == 0) {
    Serial.println("저장한 와이파이가 없어요 -> 설정 모드");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);                  // 절전 끄기 (영상 끊김 방지)
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("와이파이 연결 중: " + ssid);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println("\n연결 실패 (이름/비밀번호, 2.4GHz인지 확인) -> 설정 모드");
      WiFi.disconnect(true);
      return false;
    }
  }
  Serial.println("\n연결됨!");
  return true;
}

void startAP() {
  char name[32];
  uint64_t mac = ESP.getEfuseMac();
  snprintf(name, sizeof(name), "AiHand-CCTV-%04X", (unsigned)(mac >> 32) & 0xFFFF);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(name, AP_PASS);
  WiFi.setSleep(false);
  apMode = true;
  Serial.printf("설정 모드 와이파이: %s (비밀번호 %s)\n", name, AP_PASS);
  Serial.println("폰으로 연결한 뒤 http://192.168.4.1");
}

// ================= 시작 =================
bool startCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_UXGA;          // 버퍼를 크게 잡아 두고 아래에서 원하는 해상도로 줄임
  c.jpeg_quality = 10;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.fb_count = 2;
  c.grab_mode = CAMERA_GRAB_LATEST;       // 항상 가장 최근 장면
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("카메라 시작 실패 (0x%x). 카메라 보드(Sense)가 잘 끼워졌는지 확인하세요\n", err);
    return false;
  }
  applyCamera();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nAiHand CCTV 시작");

  if (!psramFound()) {
    Serial.println("PSRAM이 없어요. 도구 > PSRAM: OPI PSRAM 으로 바꾸고 다시 업로드하세요");
    delay(5000);
    ESP.restart();
  }
  frameMux = xSemaphoreCreateMutex();
  latestBuf = (uint8_t*)ps_malloc(FRAME_CAP);
  webBuf = (uint8_t*)ps_malloc(FRAME_CAP);
  loadSettings();

  if (!startCamera()) { delay(5000); ESP.restart(); }

  sdOk = SD.begin(SD_CS_PIN);
  if (sdOk && SD.cardType() != CARD_NONE) {
    SD.mkdir("/cctv");
    Serial.printf("microSD: %u MB 중 %u MB 사용\n",
                  (unsigned)(SD.totalBytes() / 1048576ULL), (unsigned)(SD.usedBytes() / 1048576ULL));
  } else {
    sdOk = false;
    Serial.println("microSD 없음 -> 녹화 없이 실시간 영상만 (FAT32로 포맷한 카드를 꽂으면 녹화돼요)");
  }

  clearWifiIfBootHeld();
  if (!connectWifi()) startAP();

  configTzTime("KST-9", "pool.ntp.org", "time.google.com");   // 한국 시간
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  xTaskCreatePinnedToCore(cameraTask, "camera", 8192, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(streamAcceptTask, "stream", 4096, NULL, 2, NULL, 0);
  startWeb();

  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  Serial.println("--------------------------------------------");
  Serial.println("CCTV 화면:  http://" + ip + "   (또는 http://" HOSTNAME ".local)");
  Serial.println("영상 주소:  http://" + ip + ":81/stream   (팀 웹 화면에 넣는 주소)");
  Serial.println("--------------------------------------------");
}

void loop() {
  web.handleClient();
  recordStep(webBuf);

  static uint32_t lastCheck = 0;
  if (!apMode && millis() - lastCheck > 5000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("와이파이 끊김 -> 다시 연결");
      WiFi.reconnect();
    }
  }
  delay(2);
}
