// えんがわポスト — スタックチャン受信端末 v2 (M5Stack CoreS3 専用)
// 感情つき: happy=頷いて目が笑う / sad=うつむいて目を伏せる / warm=ひとつ頷く
// 顔のあたりをタップ(または画面下中央)で、内蔵カメラで「顔だけのお返事」
// ボード: M5CoreS3 / ツール→PSRAM→OPI PSRAM / ライブラリ: M5Unified

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

// ==== 会場で書き換える設定 ====
const char* WIFI_SSID  = "Deeptech-CORE";
const char* WIFI_PASS  = "deeptechcore";
const char* SERVER_URL = "http://192.168.96.15:8787";
// ================================

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

String   lastTs = "0", letterText = "", highlightText = "", mood = "warm";
bool     talking = false, mouthOpen = false, blinkNow = false, camOk = false;
int      camErr = 0, faceY = 0;
unsigned long talkEnd = 0, lastPoll = 0, lastAnim = 0, nextBlink = 3000, blinkEnd = 0;
uint32_t BG, INK, CARD;

void drawFace() {
  auto& d = M5.Display;
  d.startWrite();
  d.fillRect(0, 0, d.width(), 138, BG);
  int cx = d.width() / 2;
  int ey = 68 + faceY + (mood == "sad" ? 6 : 0);
  int lx = cx - 55, rx = cx + 55;
  if (blinkNow) {
    d.fillRect(lx - 14, ey - 2, 28, 5, INK);
    d.fillRect(rx - 14, ey - 2, 28, 5, INK);
  } else if (mood == "happy") {           // 目が笑う(∩)
    d.fillCircle(lx, ey, 13, INK); d.fillRect(lx - 14, ey, 29, 15, BG);
    d.fillCircle(rx, ey, 13, INK); d.fillRect(rx - 14, ey, 29, 15, BG);
  } else if (mood == "sad") {             // 目を伏せる(∪)
    d.fillCircle(lx, ey, 12, INK); d.fillRect(lx - 13, ey - 14, 27, 14, BG);
    d.fillCircle(rx, ey, 12, INK); d.fillRect(rx - 13, ey - 14, 27, 14, BG);
  } else {
    d.fillCircle(lx, ey, 12, INK);
    d.fillCircle(rx, ey, 12, INK);
  }
  int my = 108 + faceY + (mood == "sad" ? 6 : 0);
  if (talking && mouthOpen) {
    d.fillEllipse(cx, my, 16, 12, INK);
  } else if (mood == "happy") {           // にっこり(∪)
    d.fillEllipse(cx, my - 4, 20, 12, INK); d.fillRect(cx - 21, my - 17, 42, 14, BG);
    d.fillEllipse(cx, my - 6, 16, 8, BG);
  } else if (mood == "sad") {             // への字(∩)
    d.fillEllipse(cx, my + 4, 18, 10, INK); d.fillRect(cx - 19, my + 4, 38, 12, BG);
    d.fillEllipse(cx, my + 6, 14, 7, BG);
  } else {
    d.fillRoundRect(cx - 18, my - 3, 36, 6, 3, INK);
  }
  d.endWrite();
}

void nod(int times, int amp, int stepDelay) {   // 首を縦に振る
  for (int t = 0; t < times; t++) {
    for (int y = 0; y <= amp; y += 2) { faceY = y; drawFace(); delay(stepDelay); }
    for (int y = amp; y >= 0; y -= 2) { faceY = y; drawFace(); delay(stepDelay); }
  }
  faceY = 0; drawFace();
}

void drawText(const String& t) {
  auto& d = M5.Display;
  d.fillRect(0, 138, d.width(), d.height() - 138, CARD);
  int y = 144;
  if (highlightText.length() > 0) {
    d.setFont(&fonts::efontJA_24);
    d.setTextColor(d.color888(181, 98, 58), CARD);   // 強調は少し赤茶
    d.setCursor(8, y);
    d.print(highlightText);
    y += 30;
  }
  d.setFont(&fonts::efontJA_16);
  d.setTextColor(INK, CARD);
  d.setCursor(8, y);
  d.setTextWrap(true);
  d.print(t);
}

void statusText(const String& t) { highlightText = ""; drawText(t); }

void startTalking() {
  talking = true;
  unsigned long dur = letterText.length() * 40UL;
  if (dur < 2500) dur = 2500;
  if (dur > 10000) dur = 10000;
  talkEnd = millis() + dur;
  if (M5.Speaker.isEnabled()) { M5.Speaker.tone(1047, 120); delay(140); M5.Speaker.tone(1319, 160); }
}

void onLetter() {
  drawText(letterText);
  if (mood == "happy")      nod(3, 10, 12);       // 嬉しい: 3回大きく頷く
  else if (mood == "sad") { faceY = 8; drawFace(); }  // 悲しい: うつむいたまま読む
  else                      nod(1, 6, 14);        // warm: ひとつ頷く
  startTalking();
}

void showPhoto() {   // 孫の顔写真を表示(あれば)
  HTTPClient hp;
  hp.setTimeout(4000);
  hp.begin(String(SERVER_URL) + "/api/letter/photo.jpg");
  int code = hp.GET();
  if (code == 200) {
    int len = hp.getSize();
    if (len > 0 && len < 200000) {
      uint8_t* buf = (uint8_t*)malloc(len);
      if (buf) {
        WiFiClient* s = hp.getStreamPtr();
        int got = 0; unsigned long t0 = millis();
        while (got < len && millis() - t0 < 5000) {
          int r = s->read(buf + got, len - got);
          if (r > 0) got += r; else delay(5);
        }
        if (got == len) {
          M5.Display.fillScreen(INK);
          M5.Display.drawJpg(buf, len, 40, 20, 240, 200, 0, 0, 0.5f);
          delay(3500);
          M5.Display.fillScreen(BG);
        }
        free(buf);
      }
    }
  }
  hp.end();
}

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = CAM_Y2;  c.pin_d1 = CAM_Y3;  c.pin_d2 = CAM_Y4;  c.pin_d3 = CAM_Y5;
  c.pin_d4 = CAM_Y6;  c.pin_d5 = CAM_Y7;  c.pin_d6 = CAM_Y8;  c.pin_d7 = CAM_Y9;
  c.pin_xclk = CAM_XCLK; c.pin_pclk = CAM_PCLK; c.pin_vsync = CAM_VSYNC; c.pin_href = CAM_HREF;
  c.pin_sccb_sda = CAM_SIOD; c.pin_sccb_scl = CAM_SIOC;
  c.pin_pwdn = -1; c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size   = FRAMESIZE_QVGA;
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    esp_camera_deinit();
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.fb_count = 1;
    err = esp_camera_init(&c);
  }
  camErr = err;
  return err == ESP_OK;
}

void sendFaceReply() {
  if (!camOk) { statusText("カメラが使えません err=0x" + String(camErr, HEX)); return; }
  for (int i = 3; i > 0; i--) {
    statusText("お顔を撮ります… " + String(i));
    if (M5.Speaker.isEnabled()) M5.Speaker.tone(880, 80);
    delay(800);
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { statusText("撮影に失敗しました"); return; }
  uint8_t* jpg = nullptr; size_t jlen = 0;
  bool ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80, &jpg, &jlen);
  esp_camera_fb_return(fb);
  if (!ok) { statusText("変換に失敗しました"); return; }
  HTTPClient http;
  http.setTimeout(6000);
  http.begin(String(SERVER_URL) + "/api/reply/raw");
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(jpg, jlen);
  free(jpg);
  http.end();
  if (code == 200) {
    mood = "happy"; statusText("お返事、届けました");
    if (M5.Speaker.isEnabled()) { M5.Speaker.tone(1319, 120); delay(140); M5.Speaker.tone(1568, 200); }
    nod(2, 8, 12);
    if (letterText.length()) drawText(letterText);
  } else statusText("送信に失敗しました(" + String(code) + ")");
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
  statusText("Wi-Fi接続中: " + String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); }
  camOk = initCamera();
  if (WiFi.status() == WL_CONNECTED)
    statusText("お便りを待っています…\n(顔を触るとお返事撮影)\nIP: " + WiFi.localIP().toString() + (camOk ? "" : "\n※カメラ初期化失敗 err=0x" + String(camErr, HEX)));
  else
    statusText("Wi-Fi接続失敗。設定を確認してリセットしてください");
}

void loop() {
  M5.update();
  unsigned long now = millis();

  if (!blinkNow && now > nextBlink) { blinkNow = true; blinkEnd = now + 150; drawFace(); }
  if (blinkNow && now > blinkEnd)   { blinkNow = false; nextBlink = now + 2200 + (esp_random() % 2600); drawFace(); }

  if (talking) {
    if (now > talkEnd) { talking = false; mouthOpen = false; if (mood == "sad") faceY = 0; drawFace(); }
    else if (now - lastAnim > 160) { lastAnim = now; mouthOpen = !mouthOpen; drawFace(); }
  }

  // 顔エリアのタップ or 中央ボタンでお返事撮影 / 左ボタンでもう一度
  auto t = M5.Touch.getDetail();
  if (t.wasPressed() && t.y < 138 && !talking) sendFaceReply();
  if (M5.BtnB.wasPressed()) sendFaceReply();
  if (M5.BtnA.wasPressed() && letterText.length() > 0) onLetter();

  if (WiFi.status() == WL_CONNECTED && now - lastPoll > 3000) {
    lastPoll = now;
    HTTPClient http;
    http.setTimeout(2500);
    http.begin(String(SERVER_URL) + "/api/letter/plain?since=" + lastTs);
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      if (body.length() > 0) {
        int n1 = body.indexOf('\n');
        int n2 = body.indexOf('\n', n1 + 1);
        int n3 = body.indexOf('\n', n2 + 1);
        if (n1 > 0 && n2 > n1 && n3 > n2) {
          lastTs        = body.substring(0, n1);
          mood          = body.substring(n1 + 1, n2);
          highlightText = body.substring(n2 + 1, n3);
          letterText    = body.substring(n3 + 1);
          showPhoto();
          onLetter();
        }
      }
    }
    http.end();
  }
}
