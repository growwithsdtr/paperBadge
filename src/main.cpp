#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSdSpiHz = 25000000;
constexpr const char* kFirmwareVersion = "v0.5";
constexpr uint32_t kDefaultIntervalSeconds = 15;
constexpr bool kTryJapaneseGlyphs = true;
constexpr const char* kBadgeJsonPath = "/paperbadge/badge.json";
constexpr const char* kProfilePhotoPath = "/paperbadge/profile_photo.png";
constexpr const char* kQrPath = "/paperbadge/qr.png";

struct BadgeText {
  BadgeText() = default;
  BadgeText(const char* nameValue, const char* titleValue, const char* subtitleValue, const char* locationValue,
            const char* footerValue)
      : name(nameValue), title(titleValue), subtitle(subtitleValue), location(locationValue), footer(footerValue) {}

  String name = "Daniel Jimenez";
  String title = "Product Manager (AI)";
  String subtitle = "0->1 AI, SaaS & FinTech";
  String location = "Tokyo, Japan";
  String footer = "Scan for LinkedIn";
};

struct BadgeConfig {
  uint32_t intervalSeconds = kDefaultIntervalSeconds;
  BadgeText english;
  BadgeText japanese;
  BadgeText japaneseRomanized = {
      "Daniel Jimenez",
      "AI Product Manager",
      "Zero-to-one AI, SaaS, FinTech",
      "Tokyo base",
      "Scan for LinkedIn",
  };
  bool englishFromJson = false;
  bool japaneseFromJson = false;
};

struct AssetStatus {
  bool profilePhoto = false;
  bool qr = false;
};

BadgeConfig gBadge;
AssetStatus gAssets;
bool gSdOk = false;
bool gShowJapanese = false;
uint32_t gLastLanguageSwitchMs = 0;

void waitForSerial() {
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }
}

void bootBeep() {
  if (!M5.Speaker.isEnabled()) {
    return;
  }

  M5.Speaker.setVolume(64);
  M5.Speaker.tone(2000, 80);
  delay(100);
  M5.Speaker.stop();
}

bool mountSdCard() {
  const int8_t sclk = M5.getPin(m5::sd_spi_sclk);
  const int8_t mosi = M5.getPin(m5::sd_spi_mosi);
  const int8_t miso = M5.getPin(m5::sd_spi_miso);
  const int8_t cs = M5.getPin(m5::sd_spi_cs);

  Serial.printf("SD SPI pins: SCLK=%d MOSI=%d MISO=%d CS=%d\n", sclk, mosi, miso, cs);

  if (sclk < 0 || mosi < 0 || miso < 0 || cs < 0) {
    Serial.println("SD mount failed: PaperS3 SD pins are unavailable.");
    return false;
  }

  SPI.begin(sclk, miso, mosi, cs);
  return SD.begin(cs, SPI, kSdSpiHz);
}

String readJsonString(JsonObject object, const char* key, const String& fallback) {
  const char* value = object[key] | nullptr;
  return value && value[0] ? String(value) : fallback;
}

bool readBadgeText(JsonObject object, BadgeText& badge) {
  if (object.isNull()) {
    return false;
  }

  badge.name = readJsonString(object, "name", badge.name);
  badge.title = readJsonString(object, "title", badge.title);
  badge.subtitle = readJsonString(object, "subtitle", badge.subtitle);
  badge.location = readJsonString(object, "location", badge.location);
  badge.footer = readJsonString(object, "footer", badge.footer);
  return true;
}

bool loadBadgeConfigFromJson(BadgeConfig& config) {
  File file = SD.open(kBadgeJsonPath, FILE_READ);
  if (!file) {
    Serial.println("JSON parse FAIL: could not open badge.json.");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("JSON parse FAIL: %s\n", error.c_str());
    return false;
  }

  uint32_t intervalSeconds = doc["interval_seconds"] | config.intervalSeconds;
  config.intervalSeconds = intervalSeconds > 0 ? intervalSeconds : kDefaultIntervalSeconds;

  config.englishFromJson = readBadgeText(doc["english"], config.english);
  if (!config.englishFromJson) {
    Serial.println("JSON parse FAIL: missing english object.");
    return false;
  }

  config.japaneseFromJson = readBadgeText(doc["japanese"], config.japanese);

  Serial.println("JSON parse OK.");
  Serial.printf("Language interval: %u seconds\n", config.intervalSeconds);
  Serial.printf("Japanese text source: %s\n", config.japaneseFromJson ? "JSON" : "romanized fallback");
  return true;
}

void drawAssetBox(int32_t x, int32_t y, int32_t size, const char* label, bool exists) {
  auto& display = M5.Display;
  display.drawRect(x, y, size, size, TFT_BLACK);
  display.drawLine(x, y, x + size - 1, y + size - 1, TFT_BLACK);
  display.drawLine(x + size - 1, y, x, y + size - 1, TFT_BLACK);
  display.setTextDatum(textdatum_t::middle_center);
  display.setTextSize(1);
  display.drawString(label, x + size / 2, y + size / 2 - 14);
  display.drawString(exists ? "FOUND" : "MISSING", x + size / 2, y + size / 2 + 14);
  display.setTextDatum(textdatum_t::top_left);
}

bool drawPngAsset(const char* path, int32_t x, int32_t y, int32_t size, const char* label, bool exists) {
  if (!exists) {
    drawAssetBox(x, y, size, label, false);
    Serial.printf("%s draw skipped: file missing.\n", label);
    return false;
  }

  bool drawn = M5.Display.drawPngFile(SD, path, x, y, size, size);
  Serial.printf("%s draw %s: %s\n", label, drawn ? "OK" : "FAIL", path);
  if (!drawn) {
    drawAssetBox(x, y, size, label, true);
  }
  return drawn;
}

const BadgeText& currentBadgeText() {
  if (!gShowJapanese) {
    return gBadge.english;
  }

  if (kTryJapaneseGlyphs && gBadge.japaneseFromJson) {
    return gBadge.japanese;
  }

  return gBadge.japaneseRomanized;
}

void renderBadge() {
  const BadgeText& badge = currentBadgeText();
  auto& display = M5.Display;

  if (display.width() > display.height()) {
    display.setRotation(display.getRotation() ^ 1);
  }

  display.setEpdMode(m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextWrap(false, false);

  display.setFont(&fonts::Font2);
  drawPngAsset(kProfilePhotoPath, (display.width() - 220) / 2, 50, 220, "PHOTO", gAssets.profilePhoto);

  display.setTextDatum(textdatum_t::top_left);
  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString("PaperBadge+", 40, 300);

  if (gShowJapanese && kTryJapaneseGlyphs && gBadge.japaneseFromJson) {
    display.setFont(&fonts::efontJA_16);
    display.setTextSize(1);
  } else {
    display.setFont(&fonts::Font2);
    display.setTextSize(2);
  }

  display.drawString(badge.name, 40, 360);

  display.drawString(badge.title, 40, 420);
  display.drawString(badge.subtitle, 40, 480);
  display.drawString(badge.location, 40, 540);
  display.drawString(badge.footer, 40, 600);

  display.setFont(&fonts::Font2);

  drawPngAsset(kQrPath, (display.width() - 320) / 2, 650, 320, "QR", gAssets.qr);

  display.setTextSize(2);
  display.drawString(gSdOk ? "SD OK" : "SD FAIL", 40, display.height() - 50);
  display.drawString(gShowJapanese ? "JP" : "EN", display.width() - 80, display.height() - 50);
  display.display();
}

void switchLanguage(bool showJapanese, const char* reason) {
  gShowJapanese = showJapanese;
  gLastLanguageSwitchMs = millis();
  Serial.printf("Language: %s (%s)\n", gShowJapanese ? "Japanese" : "English", reason);
  renderBadge();
}

void toggleLanguage(const char* reason) {
  switchLanguage(!gShowJapanese, reason);
}

bool centerTapWasClicked() {
  if (!M5.Touch.isEnabled()) {
    return false;
  }

  auto detail = M5.Touch.getDetail();
  if (!detail.wasClicked()) {
    return false;
  }

  const int32_t width = M5.Display.width();
  const int32_t height = M5.Display.height();
  const bool inCenterX = detail.x >= width / 4 && detail.x <= (width * 3) / 4;
  const bool inCenterY = detail.y >= height / 4 && detail.y <= (height * 3) / 4;
  return inCenterX && inCenterY;
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = kSerialBaud;
  cfg.fallback_board = m5::board_t::board_M5PaperS3;
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.external_display_value = 0;
  cfg.external_speaker_value = 0;

  M5.begin(cfg);
  waitForSerial();

  Serial.println();
  Serial.printf("PaperBadge+ %s boot\n", kFirmwareVersion);
  Serial.printf("M5 board id: %d\n", static_cast<int>(M5.getBoard()));
  Serial.printf("Display: %dx%d\n", M5.Display.width(), M5.Display.height());
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());

  bootBeep();

  const bool sdMounted = mountSdCard();
  Serial.println(sdMounted ? "SD mount OK." : "SD mount FAIL.");

  const bool badgeJsonExists = sdMounted && SD.exists(kBadgeJsonPath);
  Serial.printf("badge.json %s: %s\n", badgeJsonExists ? "found" : "missing", kBadgeJsonPath);

  gAssets.profilePhoto = sdMounted && SD.exists(kProfilePhotoPath);
  gAssets.qr = sdMounted && SD.exists(kQrPath);
  Serial.printf("profile_photo.png %s: %s\n", gAssets.profilePhoto ? "found" : "missing", kProfilePhotoPath);
  Serial.printf("qr.png %s: %s\n", gAssets.qr ? "found" : "missing", kQrPath);

  const bool jsonOk = badgeJsonExists && loadBadgeConfigFromJson(gBadge);
  Serial.printf("Text source: %s\n", jsonOk ? "JSON" : "fallback");

  gSdOk = sdMounted && badgeJsonExists;
  gLastLanguageSwitchMs = millis();
  renderBadge();
}

void loop() {
  M5.update();
  if (centerTapWasClicked()) {
    toggleLanguage("center tap");
  }

  const uint32_t intervalMs = gBadge.intervalSeconds * 1000UL;
  if (intervalMs > 0 && millis() - gLastLanguageSwitchMs >= intervalMs) {
    toggleLanguage("timer");
  }

  delay(100);
}
