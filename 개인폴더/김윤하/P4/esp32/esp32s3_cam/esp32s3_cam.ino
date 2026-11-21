/*
  AiHand 손 카메라 (ESP32-S3 + 카메라) -> 와이파이로 영상 보내기
  -----------------------------------------------------------------
  [준비 (Arduino IDE)]
   1. 파일 > 기본 설정 > 추가 보드 관리자 URL 에 아래 주소 추가
        https://espressif.github.io/arduino-esp32/package_esp32_index.json
   2. 보드 관리자에서 "esp32 by Espressif Systems" 설치
   3. 도구 메뉴 설정
        보드: XIAO 보드면 "XIAO_ESP32S3", 그 밖에는 "ESP32S3 Dev Module"
        PSRAM: "OPI PSRAM"   (안 되면 "QSPI PSRAM")
        Flash Size: 보드에 맞게 (보통 8MB 또는 16MB)
        USB CDC On Boot: "Enabled"   (시리얼 모니터로 주소 보기)
   4. 아래 [설정]에서 와이파이와 보드 종류를 고르고 업로드
   5. 시리얼 모니터(115200)에 나오는 주소를 web_server.py 에 넣기
        python web_server.py --camera http://192.168.x.x:81/stream

  ※ 와이파이 비밀번호를 넣은 채로 깃(GitHub)에 올리지 마세요.
  ※ 영상은 PC의 web_server.py 하나만 받아 가요 (ESP32는 한 번에 한 곳에만 보냄).
     웹 브라우저는 PC 웹 서버(http://localhost:8000)로 보세요.
*/
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ================= 설정 =================
const char* WIFI_SSID = "와이파이이름";
const char* WIFI_PASS = "비밀번호";

// 보드 종류: 하나만 남기고 나머지는 // 로 막기
//#define BOARD_S3_EYE_TYPE        // Freenove ESP32-S3 CAM, ESP32-S3-CAM(N16R8 등), ESP32-S3-EYE
#define BOARD_XIAO_S3_SENSE    // Seeed XIAO ESP32S3 Sense

#define FRAME_SIZE   FRAMESIZE_VGA   // 640x480. 끊기면 FRAMESIZE_QVGA(320x240)
#define JPEG_QUALITY 12              // 10~63, 낮을수록 화질 좋고 무거움
#define FLIP_V       false           // 영상이 위아래로 뒤집혀 보이면 true
#define MIRROR_H     false           // 좌우가 바뀌어 보이면 true

// ================= 보드별 카메라 핀 =================
#if defined(BOARD_S3_EYE_TYPE)
  #define PWDN_GPIO_NUM  -1
  #define RESET_GPIO_NUM -1
  #define XCLK_GPIO_NUM  15
  #define SIOD_GPIO_NUM  4
  #define SIOC_GPIO_NUM  5
  #define Y9_GPIO_NUM    16
  #define Y8_GPIO_NUM    17
  #define Y7_GPIO_NUM    18
  #define Y6_GPIO_NUM    12
  #define Y5_GPIO_NUM    10
  #define Y4_GPIO_NUM    8
  #define Y3_GPIO_NUM    9
  #define Y2_GPIO_NUM    11
  #define VSYNC_GPIO_NUM 6
  #define HREF_GPIO_NUM  7
  #define PCLK_GPIO_NUM  13
#elif defined(BOARD_XIAO_S3_SENSE)
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
#else
  #error "위 [설정]에서 보드 종류를 하나 고르세요"
#endif

// ================= 영상 전송 =================
#define BOUNDARY "frame"
static const char* STREAM_TYPE = "multipart/x-mixed-replace;boundary=" BOUNDARY;
static const char* STREAM_SEP = "\r\n--" BOUNDARY "\r\n";
static const char* PART_HDR = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// http://<IP>:81/stream  ->  MJPEG 영상
static esp_err_t stream_handler(httpd_req_t* req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char hdr[80];
  Serial.println("[영상] 보내기 시작");
  while (true) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[영상] 사진 찍기 실패");
      res = ESP_FAIL;
      break;
    }
    size_t n = snprintf(hdr, sizeof(hdr), PART_HDR, (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, STREAM_SEP, strlen(STREAM_SEP));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, hdr, n);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;      // 받는 쪽이 끊음
  }
  Serial.println("[영상] 보내기 끝");
  return res;
}

// http://<IP>/  ->  확인용 페이지
static esp_err_t index_handler(httpd_req_t* req) {
  static const char page[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
    "<title>AiHand 손 카메라</title><body style='margin:0;background:#111;color:#ddd;font-family:sans-serif'>"
    "<p style='padding:8px'>AiHand 손 카메라 · 영상 주소: <b id=u></b><br>"
    "(확인용이에요. 평소에는 PC 웹 서버로 보세요. 여기서 보는 동안에는 PC가 영상을 못 받아요)</p>"
    "<img id=v style='width:100%'>"
    "<script>var s='http://'+location.hostname+':81/stream';u.textContent=s;v.src=s</script>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, page, sizeof(page) - 1);
}

static void startServers() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t web = NULL, stream = NULL;

  cfg.server_port = 80;
  if (httpd_start(&web, &cfg) == ESP_OK) {
    httpd_uri_t u = {};
    u.uri = "/";
    u.method = HTTP_GET;
    u.handler = index_handler;
    httpd_register_uri_handler(web, &u);
  }

  cfg.server_port = 81;            // 영상은 따로 (영상 보내는 동안에도 80번이 응답하게)
  cfg.ctrl_port += 1;
  if (httpd_start(&stream, &cfg) == ESP_OK) {
    httpd_uri_t u = {};
    u.uri = "/stream";
    u.method = HTTP_GET;
    u.handler = stream_handler;
    httpd_register_uri_handler(stream, &u);
  }
}

// ================= 시작 =================
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nAiHand 손 카메라 시작");

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
  c.frame_size = FRAME_SIZE;
  c.jpeg_quality = JPEG_QUALITY;
  if (psramFound()) {
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.fb_count = 2;
    c.grab_mode = CAMERA_GRAB_LATEST;      // 항상 가장 최근 장면
  } else {
    Serial.println("PSRAM 없음 -> 작은 화면(320x240)으로. 도구 > PSRAM 설정을 확인하세요");
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.fb_count = 1;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    c.frame_size = FRAMESIZE_QVGA;
  }

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("카메라 시작 실패 (0x%x). 보드 종류와 카메라 케이블을 확인하세요. 5초 뒤 다시 시작\n", err);
    delay(5000);
    ESP.restart();
  }
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, FLIP_V);
  s->set_hmirror(s, MIRROR_H);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // 절전 끄기 (영상 끊김 방지)
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("와이파이 연결 중");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println("\n와이파이 연결 실패. 이름/비밀번호를 확인하세요 (2.4GHz만 됨). 다시 시작");
      ESP.restart();
    }
  }
  startServers();
  Serial.println("\n연결됨!");
  Serial.print("영상 주소:  http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");
  Serial.print("PC에서 실행: python web_server.py --camera http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");
}

void loop() {
  // 와이파이가 끊기면 다시 연결
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("와이파이 끊김 -> 다시 연결");
    WiFi.reconnect();
    delay(5000);
  }
  delay(1000);
}
