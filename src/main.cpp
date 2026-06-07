#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSdSpiHz = 25000000;
constexpr const char* kFirmwareVersion = "v0.3";
constexpr const char* kBadgeJsonPath = "/paperbadge/badge.json";
constexpr const char* kProfilePhotoPath = "/paperbadge/profile_photo.png";
constexpr const char* kQrPath = "/paperbadge/qr.png";

struct BadgeText {
  String name = "Daniel Jimenez";
  String title = "Product Manager (AI)";
  String subtitle = "0->1 AI, SaaS & FinTech";
  String location = "Tokyo, Japan";
  String footer = "Scan for LinkedIn";
};

struct AssetStatus {
  bool profilePhoto = false;
  bool qr = false;
};

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

bool loadBadgeTextFromJson(BadgeText& badge) {
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

  JsonObject english = doc["english"];
  if (english.isNull()) {
    Serial.println("JSON parse FAIL: missing english object.");
    return false;
  }

  badge.name = english["name"] | badge.name;
  badge.title = english["title"] | badge.title;
  badge.subtitle = english["subtitle"] | badge.subtitle;
  badge.location = english["location"] | badge.location;
  badge.footer = english["footer"] | badge.footer;

  Serial.println("JSON parse OK.");
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

void renderBadge(const BadgeText& badge, bool sdOk, const AssetStatus& assets) {
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
  drawAssetBox(40, 70, 220, "PHOTO", assets.profilePhoto);

  display.setTextDatum(textdatum_t::top_left);
  display.setTextSize(2);
  display.drawString("PaperBadge+", 300, 90);
  display.drawString(badge.name, 300, 150);

  display.setTextSize(2);
  display.drawString(badge.title, 40, 330);
  display.drawString(badge.subtitle, 40, 390);
  display.drawString(badge.location, 40, 450);
  display.drawString(badge.footer, 40, 510);

  drawAssetBox((display.width() - 320) / 2, 570, 320, "QR", assets.qr);

  display.setTextSize(2);
  display.drawString(sdOk ? "SD OK" : "SD FAIL", 40, display.height() - 100);
  display.display();
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

  AssetStatus assets;
  assets.profilePhoto = sdMounted && SD.exists(kProfilePhotoPath);
  assets.qr = sdMounted && SD.exists(kQrPath);
  Serial.printf("profile_photo.png %s: %s\n", assets.profilePhoto ? "found" : "missing", kProfilePhotoPath);
  Serial.printf("qr.png %s: %s\n", assets.qr ? "found" : "missing", kQrPath);

  BadgeText badge;
  const bool jsonOk = badgeJsonExists && loadBadgeTextFromJson(badge);
  Serial.printf("Text source: %s\n", jsonOk ? "JSON" : "fallback");

  renderBadge(badge, sdMounted && badgeJsonExists, assets);
}

void loop() {
  M5.update();
  delay(100);
}
