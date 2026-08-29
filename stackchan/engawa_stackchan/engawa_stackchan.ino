// えんがわポスト — スタックチャン受信端末
// 必要ライブラリ: M5Unified のみ(ライブラリマネージャでインストール済みならOK)
// Wi-Fi経由でMacのサーバをポーリングし、お便りが届いたら口パクしながら表示する

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ==== 会場で書き換える設定 ====
const char* WIFI_SSID  = "YOUR_WIFI_SSID";
const char* WIFI_PASS  = "YOUR_WIFI_PASS";
const char* SERVER_URL = "http://192.168.0.10:8787";  // Macで `ipconfig getifaddr en0` したIP
// ================================

String   lastTs = "0";
String   letterText = "";
bool     talking = false;
bool     mouthOpen = false;
bool     blinkNow = false;
unsigned long talkEnd = 0, lastPoll = 0, lastAnim = 0, nextBlink = 3000, blinkEnd = 0;

uint32_t BG, INK, CARD;

void drawFace() {
  auto& d = M5.Display;
  d.startWrite();
  d.fillRect(0, 0, d.width(), 138, BG);
  int ey = 68, lx = d.width() / 2 - 55, rx = d.width() / 2 + 55;
  if (blinkNow) {
    d.fillRect(lx - 14, ey - 2, 28, 5, INK);
    d.fillRect(rx - 14, ey - 2, 28, 5, INK);
  } else {
    d.fillCircle(lx, ey, 12, INK);
    d.fillCircle(rx, ey, 12, INK);
  }
  int my = 108;
  if (talking && mouthOpen) d.fillEllipse(d.width() / 2, my, 16, 12, INK);
  else                      d.fillRoundRect(d.width() / 2 - 18, my - 3, 36, 6, 3, INK);
  d.endWrite();
}

void drawText(const String& t) {
  auto& d = M5.Display;
  d.fillRect(0, 138, d.width(), d.height() - 138, CARD);
  d.setFont(&fonts::efontJA_16);
  d.setTextColor(INK, CARD);
  d.setCursor(8, 148);
  d.setTextWrap(true);
  d.print(t);
}

void statusLine(const String& t) {
  auto& d = M5.Display;
  d.fillRect(0, 138, d.width(), d.height() - 138, CARD);
  d.setFont(&fonts::efontJA_16);
  d.setTextColor(INK, CARD);
  d.setCursor(8, 148);
  d.print(t);
}

void startTalking() {
  talking = true;
  unsigned long dur = letterText.length() * 40UL;  // 文字量に応じて口パク
  if (dur < 2000) dur = 2000;
  if (dur > 9000) dur = 9000;
  talkEnd = millis() + dur;
  if (M5.Speaker.isEnabled()) { M5.Speaker.tone(1047, 120); delay(140); M5.Speaker.tone(1319, 160); }
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
  statusLine("Wi-Fi接続中: " + String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); }
  if (WiFi.status() == WL_CONNECTED) statusLine("お便りを待っています…\nIP: " + WiFi.localIP().toString());
  else statusLine("Wi-Fi接続失敗。設定を確認してリセットしてください");
}

void loop() {
  M5.update();
  unsigned long now = millis();

  // まばたき
  if (!blinkNow && now > nextBlink) { blinkNow = true; blinkEnd = now + 150; drawFace(); }
  if (blinkNow && now > blinkEnd)   { blinkNow = false; nextBlink = now + 2200 + (esp_random() % 2600); drawFace(); }

  // 口パク
  if (talking) {
    if (now > talkEnd) { talking = false; mouthOpen = false; drawFace(); }
    else if (now - lastAnim > 160) { lastAnim = now; mouthOpen = !mouthOpen; drawFace(); }
  }

  // ボタンAでもう一度
  if (M5.BtnA.wasPressed() && letterText.length() > 0) startTalking();

  // ポーリング
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
