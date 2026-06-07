#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSdSpiHz = 25000000;
constexpr const char* kFirmwareVersion = "v0.6";
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
  bool initialLandscape = false;
  bool upsideDown = false;
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
bool gLandscape = false;
uint8_t gPortraitRotation = 0;
uint8_t gLandscapeRotation = 1;
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
  const uint8_t orientation = doc["orientation"] | 0;
  const uint8_t strapOrientation = doc["strap_orientation"] | 0;
  config.initialLandscape = orientation == 1 || orientation == 3;
  config.upsideDown = orientation == 2 || strapOrientation == 2;

  config.englishFromJson = readBadgeText(doc["english"], config.english);
  if (!config.englishFromJson) {
    Serial.println("JSON parse FAIL: missing english object.");
    return false;
  }

  config.japaneseFromJson = readBadgeText(doc["japanese"], config.japanese);

  Serial.println("JSON parse OK.");
  Serial.printf("Language interval: %u seconds\n", config.intervalSeconds);
  Serial.printf("Initial layout: %s\n", config.initialLandscape ? "landscape" : "portrait");
  Serial.printf("Upside-down rotation: %s\n", config.upsideDown ? "enabled" : "disabled");
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

void captureBaseRotations() {
  auto& display = M5.Display;
  const uint8_t currentRotation = display.getRotation() & 3;

  if (display.width() > display.height()) {
    gLandscapeRotation = currentRotation;
    gPortraitRotation = (currentRotation + 1) & 3;
  } else {
    gPortraitRotation = currentRotation;
    gLandscapeRotation = (currentRotation + 1) & 3;
  }
}

void applyLayoutRotation() {
  uint8_t targetRotation = gLandscape ? gLandscapeRotation : gPortraitRotation;
  if (gBadge.upsideDown) {
    targetRotation = (targetRotation + 2) & 3;
  }
  M5.Display.setRotation(targetRotation);
}

void setBadgeFont() {
  if (gShowJapanese && kTryJapaneseGlyphs && gBadge.japaneseFromJson) {
    M5.Display.setFont(&fonts::efontJA_16);
    M5.Display.setTextSize(1);
  } else {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(2);
  }
}

void drawStatusLine() {
  auto& display = M5.Display;
  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString(gSdOk ? "SD OK" : "SD FAIL", 40, display.height() - 50);
  display.drawString(gShowJapanese ? "JP" : "EN", display.width() - 150, display.height() - 50);
  display.drawString(gLandscape ? "LAND" : "PORT", display.width() - 90, display.height() - 50);
}

void drawBadgeTextBlock(int32_t x, int32_t y, int32_t lineHeight) {
  const BadgeText& badge = currentBadgeText();
  auto& display = M5.Display;

  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString("PaperBadge+", x, y);

  setBadgeFont();
  display.drawString(badge.name, x, y + lineHeight);
  display.drawString(badge.title, x, y + lineHeight * 2);
  display.drawString(badge.subtitle, x, y + lineHeight * 3);
  display.drawString(badge.location, x, y + lineHeight * 4);
  display.drawString(badge.footer, x, y + lineHeight * 5);
  display.setFont(&fonts::Font2);
}

void renderPortrait() {
  auto& display = M5.Display;
  drawPngAsset(kProfilePhotoPath, (display.width() - 220) / 2, 50, 220, "PHOTO", gAssets.profilePhoto);
  drawBadgeTextBlock(40, 300, 60);
  drawPngAsset(kQrPath, (display.width() - 320) / 2, 650, 320, "QR", gAssets.qr);
}

void renderLandscape() {
  auto& display = M5.Display;
  drawPngAsset(kProfilePhotoPath, 40, 120, 220, "PHOTO", gAssets.profilePhoto);
  drawBadgeTextBlock(310, 90, 48);
  drawPngAsset(kQrPath, display.width() - 360, 110, 320, "QR", gAssets.qr);
}

void renderBadge() {
  applyLayoutRotation();
  auto& display = M5.Display;

  display.setEpdMode(m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextWrap(false, false);

  if (gLandscape) {
    renderLandscape();
  } else {
    renderPortrait();
  }

  drawStatusLine();
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

bool touchWasClickedInZone(int32_t xMin, int32_t xMax, int32_t yMin, int32_t yMax) {
  if (!M5.Touch.isEnabled()) {
    return false;
  }

  auto detail = M5.Touch.getDetail();
  if (!detail.wasClicked()) {
    return false;
  }

  const int32_t width = M5.Display.width();
  const int32_t height = M5.Display.height();
  const int32_t x = constrain(detail.x, 0, width);
  const int32_t y = constrain(detail.y, 0, height);
  return x >= xMin && x <= xMax && y >= yMin && y <= yMax;
}

bool centerTapWasClicked() {
  const int32_t width = M5.Display.width();
  const int32_t height = M5.Display.height();
  return touchWasClickedInZone(width / 4, (width * 3) / 4, height / 4, (height * 3) / 4);
}

bool topRightTapWasClicked() {
  const int32_t width = M5.Display.width();
  const int32_t height = M5.Display.height();
  return touchWasClickedInZone((width * 3) / 4, width, 0, height / 4);
}

void toggleLayout(const char* reason) {
  gLandscape = !gLandscape;
  Serial.printf("Layout: %s (%s)\n", gLandscape ? "landscape" : "portrait", reason);
  renderBadge();
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
  captureBaseRotations();

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

  gLandscape = gBadge.initialLandscape;
  gSdOk = sdMounted && badgeJsonExists;
  gLastLanguageSwitchMs = millis();
  renderBadge();
}

void loop() {
  M5.update();
  if (topRightTapWasClicked()) {
    toggleLayout("top-right tap");
  } else if (centerTapWasClicked()) {
    toggleLanguage("center tap");
  }

  const uint32_t intervalMs = gBadge.intervalSeconds * 1000UL;
  if (intervalMs > 0 && millis() - gLastLanguageSwitchMs >= intervalMs) {
    toggleLanguage("timer");
  }

  delay(100);
}
