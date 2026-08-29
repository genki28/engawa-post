// えんがわポスト — スタックチャン受信端末 + カメラ返信 (M5Stack CoreS3 専用)
// ボード: M5CoreS3 / 必要ライブラリ: M5Unified
// 機能: お便り受信で口パク表示。画面下中央タッチ(BtnB相当)で内蔵カメラで顔を撮って「顔だけのお返事」をPOST

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

// ==== 会場で書き換える設定 ====
const char* WIFI_SSID  = "Deeptech-CORE";
const char* WIFI_PASS  = "deeptechcore";
const char* SERVER_URL = "http://192.168.0.10:8787";  // Macで `ipconfig getifaddr en0` したIP
// ================================

// CoreS3 内蔵カメラ(GC0308)のピン
#define CAM_PWDN -1
#define CAM_RESET -1
#define CAM_XCLK 2
#define CAM_SIOD 12
#define CAM_SIOC 11
#define CAM_Y9 47
#define CAM_Y8 48
#define CAM_Y7 16
#define CAM_Y6 15
#define CAM_Y5 42
#define CAM_Y4 41
#define CAM_Y3 40
#define CAM_Y2 39
#define CAM_VSYNC 46
#define CAM_HREF 38
#define CAM_PCLK 45

String   lastTs = "0";
String   letterText = "";
bool     talking = false, mouthOpen = false, blinkNow = false, camOk = false;
unsigned long talkEnd = 0, lastPoll = 0, lastAnim = 0, nextBlink = 3000, blinkEnd = 0;
uint32_t BG, INK, CARD;

void drawFace() {
  auto& d = M5.Display;
  d.startWrite();
  d.fillRect(0, 0, d.width(), 138, BG);
  int ey = 68, lx = d.width() / 2 - 55, rx = d.width() / 2 + 55;
  if (blinkNow) { d.fillRect(lx - 14, ey - 2, 28, 5, INK); d.fillRect(rx - 14, ey - 2, 28, 5, INK); }
  else { d.fillCircle(lx, ey, 12, INK); d.fillCircle(rx, ey, 12, INK); }
  int my = 108;
  if (talking && mouthOpen) d.fillEllipse(d.width() / 2, my, 16, 12, INK);
  else d.fillRoundRect(d.width() / 2 - 18, my - 3, 36, 6, 3, INK);
  d.endWrite();
}

void drawText(const String& t) {
  auto& d = M5.Display;
  d.fillRect(0, 138, d.width(), d.height() - 138, CARD);
  d.setFont(&fonts::efontJA_16);
  d.setTextColor(INK, CARD);
  d.setCursor(8, 146);
  d.setTextWrap(true);
  d.print(t);
}

void startTalking() {
  talking = true;
  unsigned long dur = letterText.length() * 40UL;
  if (dur < 2000) dur = 2000;
  if (dur > 9000) dur = 9000;
  talkEnd = millis() + dur;
  if (M5.Speaker.isEnabled()) { M5.Speaker.tone(1047, 120); delay(140); M5.Speaker.tone(1319, 160); }
}

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = CAM_Y2;  c.pin_d1 = CAM_Y3;  c.pin_d2 = CAM_Y4;  c.pin_d3 = CAM_Y5;
  c.pin_d4 = CAM_Y6;  c.pin_d5 = CAM_Y7;  c.pin_d6 = CAM_Y8;  c.pin_d7 = CAM_Y9;
  c.pin_xclk = CAM_XCLK; c.pin_pclk = CAM_PCLK; c.pin_vsync = CAM_VSYNC; c.pin_href = CAM_HREF;
  c.pin_sccb_sda = CAM_SIOD; c.pin_sccb_scl = CAM_SIOC;
  c.pin_pwdn = CAM_PWDN; c.pin_reset = CAM_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;   // GC0308はJPEG出力不可 → 撮影後にソフトJPEG化
  c.frame_size   = FRAMESIZE_QVGA;
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  return esp_camera_init(&c) == ESP_OK;
}

void sendFaceReply() {
  if (!camOk) { drawText("カメラが使えません"); return; }
  // カウントダウン
  for (int i = 3; i > 0; i--) {
    drawText("お顔を撮ります… " + String(i));
    if (M5.Speaker.isEnabled()) M5.Speaker.tone(880, 80);
    delay(800);
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { drawText("撮影に失敗しました"); return; }
  uint8_t* jpg = nullptr; size_t jlen = 0;
  bool ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80, &jpg, &jlen);
  esp_camera_fb_return(fb);
  if (!ok) { drawText("変換に失敗しました"); return; }
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(String(SERVER_URL) + "/api/reply/raw");
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(jpg, jlen);
  free(jpg);
  http.end();
  if (code == 200) { drawText("お返事、届けました"); if (M5.Speaker.isEnabled()) { M5.Speaker.tone(1319, 120); delay(140); M5.Speaker.tone(1568, 200); } }
  else drawText("送信に失敗しました(" + String(code) + ")");
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  auto& d = M5.Display;
  BG   = d.color888(250, 246, 239);
  INK  = d.color888(58, 50, 38);
  CARD = d.color888(255, 253, 246);
  d.fillScreen(BG);
  drawFace();
  drawText("Wi-Fi接続中: " + String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); }
  camOk = initCamera();
  if (WiFi.status() == WL_CONNECTED)
    drawText("お便りを待っています…\n(画面下の真ん中を触るとお返事撮影)\nIP: " + WiFi.localIP().toString() + (camOk ? "" : "\n※カメラ初期化失敗"));
  else
    drawText("Wi-Fi接続失敗。設定を確認してリセットしてください");
}

void loop() {
  M5.update();
  unsigned long now = millis();

  if (!blinkNow && now > nextBlink) { blinkNow = true; blinkEnd = now + 150; drawFace(); }
  if (blinkNow && now > blinkEnd)   { blinkNow = false; nextBlink = now + 2200 + (esp_random() % 2600); drawFace(); }

  if (talking) {
    if (now > talkEnd) { talking = false; mouthOpen = false; drawFace(); }
    else if (now - lastAnim > 160) { lastAnim = now; mouthOpen = !mouthOpen; drawFace(); }
  }

  if (M5.BtnA.wasPressed() && letterText.length() > 0) startTalking();  // 左: もう一度
  if (M5.BtnB.wasPressed()) sendFaceReply();                            // 中央: 顔だけのお返事

  if (WiFi.status() == WL_CONNECTED && now - lastPoll > 3000) {
    lastPoll = now;
    HTTPClient http;
    http.setTimeout(2500);
    http.begin(String(SERVER_URL) + "/api/letter/plain?since=" + lastTs);
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      if (body.length() > 0) {
        int nl = body.indexOf('\n');
        if (nl > 0) {
          lastTs = body.substring(0, nl);
          letterText = body.substring(nl + 1);
          drawText(letterText);
          startTalking();
        }
      }
    }
    http.end();
  }
}
