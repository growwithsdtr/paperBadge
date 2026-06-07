#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSdSpiHz = 25000000;
constexpr const char* kBadgeJsonPath = "/paperbadge/badge.json";

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

void renderBadge(bool sdOk) {
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
  display.setTextSize(3);
  display.drawString("PaperBadge+", 40, 80);
  display.drawString("Daniel Jimenez", 40, 150);
  display.drawString("Product Manager (AI)", 40, 220);

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
  Serial.println("PaperBadge+ v0.1 boot");
  Serial.printf("M5 board id: %d\n", static_cast<int>(M5.getBoard()));
  Serial.printf("Display: %dx%d\n", M5.Display.width(), M5.Display.height());
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());

  bootBeep();

  const bool sdMounted = mountSdCard();
  Serial.println(sdMounted ? "SD mounted successfully." : "SD mount failed.");

  const bool badgeJsonExists = sdMounted && SD.exists(kBadgeJsonPath);
  Serial.printf("%s %s\n", badgeJsonExists ? "Found" : "Missing", kBadgeJsonPath);

  renderBadge(sdMounted && badgeJsonExists);
}

void loop() {
  M5.update();
  delay(100);
}
