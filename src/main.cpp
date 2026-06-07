#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>
#include <cstring>

#include "embedded_assets.h"

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSdSpiHz = 25000000;
constexpr const char* kFirmwareVersion = "v0.8";
constexpr const char* kBadgeJsonPath = "/paperbadge/badge.json";
constexpr uint32_t kDefaultIntervalSeconds = 15;

constexpr const char* kProfilePhotoCandidates[] = {
    "/paperbadge/profile_photo.png",
    "/paperbadge/profilePhoto.png",
    "/paperbadge/photo.png",
    "/paperbadge/portrait.png",
    "/paperbadge/profile.png",
    "/paperbadge/profile_photo.jpg",
    "/paperbadge/profile_photo.jpeg",
    "/paperbadge/profilePhoto.jpg",
    "/paperbadge/profilePhoto.jpeg",
};

constexpr const char* kQrCandidates[] = {
    "/paperbadge/qr.png",
    "/paperbadge/qr.JPG",
    "/paperbadge/qr.jpg",
    "/paperbadge/qr.jpeg",
    "/paperbadge/linkedin_qr.png",
    "/paperbadge/linkedinQR.png",
};

constexpr const char* kFullBadgeCandidates[] = {
    "/paperbadge/badge_full.png",
    "/paperbadge/badge.png",
    "/paperbadge/full_badge.png",
    "/paperbadge/badge_en.png",
    "/paperbadge/complete_badge.png",
    "/paperbadge/completeBadge.png",
};

template <size_t N>
constexpr size_t countOf(const char* const (&)[N]) {
  return N;
}

struct BadgeText {
  String name = "Daniel Jimenez";
  String title = "Product Manager (AI)";
  String subtitle = "0->1 AI, SaaS & FinTech";
  String location = "Tokyo, Japan";
  String footer = "Scan for LinkedIn";
};

struct BadgeConfig {
  uint32_t intervalSeconds = kDefaultIntervalSeconds;
  bool upsideDown = false;
  bool englishFromJson = false;
  BadgeText english;
};

enum class ImageType {
  Unknown,
  Png,
  Jpg,
};

struct ImageAsset {
  bool found = false;
  bool drawn = false;
  ImageType type = ImageType::Unknown;
  String path;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct Rect {
  Rect() = default;
  Rect(int32_t xValue, int32_t yValue, int32_t wValue, int32_t hValue)
      : x(xValue), y(yValue), w(wValue), h(hValue) {}

  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;

  bool contains(int32_t px, int32_t py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
  }
};

enum class RenderMode {
  EmbeddedFallback,
  SdDynamic,
};

enum class ViewMode {
  Badge,
  QrZoom,
  PhotoZoom,
  Debug,
};

BadgeConfig gBadge;
ImageAsset gProfilePhoto;
ImageAsset gQr;
ImageAsset gSdFullBadge;
Rect gPhotoRect;
Rect gQrRect;
RenderMode gRenderMode = RenderMode::EmbeddedFallback;
ViewMode gViewMode = ViewMode::Badge;
bool gSdMounted = false;
bool gBadgeJsonExists = false;
bool gJsonLoaded = false;
uint8_t gPortraitRotation = 0;

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
    Serial.println("SD mounted no: PaperS3 SD pins are unavailable.");
    return false;
  }

  SPI.begin(sclk, miso, mosi, cs);
  return SD.begin(cs, SPI, kSdSpiHz);
}

void capturePortraitRotation() {
  auto& display = M5.Display;
  const uint8_t currentRotation = display.getRotation() & 3;
  gPortraitRotation = display.width() > display.height() ? ((currentRotation + 1) & 3) : currentRotation;
}

void applyPortraitRotation() {
  uint8_t targetRotation = gPortraitRotation;
  if (gBadge.upsideDown) {
    targetRotation = (targetRotation + 2) & 3;
  }
  M5.Display.setRotation(targetRotation);
}

String displaySafeText(const String& input) {
  String output;
  output.reserve(input.length());

  for (size_t index = 0; index < input.length(); ++index) {
    const uint8_t ch = static_cast<uint8_t>(input[index]);
    if (ch < 0x80) {
      output += static_cast<char>(ch);
      continue;
    }

    if (index + 2 < input.length() && ch == 0xE2 && static_cast<uint8_t>(input[index + 1]) == 0x86 &&
        static_cast<uint8_t>(input[index + 2]) == 0x92) {
      output += "->";
      index += 2;
      continue;
    }

    output += " ";
    while (index + 1 < input.length() && (static_cast<uint8_t>(input[index + 1]) & 0xC0) == 0x80) {
      ++index;
    }
  }

  while (output.indexOf("  ") >= 0) {
    output.replace("  ", " ");
  }
  output.trim();
  return output;
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
    Serial.println("badge.json loaded no: open failed.");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("badge.json loaded no: JSON parse failed: %s\n", error.c_str());
    return false;
  }

  const uint32_t intervalSeconds = doc["interval_seconds"] | config.intervalSeconds;
  config.intervalSeconds = intervalSeconds > 0 ? intervalSeconds : kDefaultIntervalSeconds;

  const uint8_t orientation = doc["orientation"] | 0;
  const uint8_t strapOrientation = doc["strap_orientation"] | 0;
  config.upsideDown = orientation == 2 || strapOrientation == 2;
  config.englishFromJson = readBadgeText(doc["english"], config.english);

  Serial.printf("badge.json loaded %s: %s\n", config.englishFromJson ? "yes" : "no", kBadgeJsonPath);
  Serial.printf("interval_seconds: %u\n", config.intervalSeconds);
  Serial.printf("upside-down rotation: %s\n", config.upsideDown ? "yes" : "no");
  return config.englishFromJson;
}

uint16_t readBe16(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

uint32_t readBe32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

bool readPngDimensions(File& file, uint16_t& width, uint16_t& height) {
  uint8_t header[24] = {};
  file.seek(0);
  if (file.read(header, sizeof(header)) != sizeof(header)) {
    return false;
  }

  const uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (memcmp(header, signature, sizeof(signature)) != 0 || memcmp(header + 12, "IHDR", 4) != 0) {
    return false;
  }

  width = static_cast<uint16_t>(readBe32(header + 16));
  height = static_cast<uint16_t>(readBe32(header + 20));
  return width > 0 && height > 0;
}

bool readJpgDimensions(File& file, uint16_t& width, uint16_t& height) {
  file.seek(0);
  if (file.read() != 0xFF || file.read() != 0xD8) {
    return false;
  }

  while (file.available()) {
    int markerPrefix = file.read();
    if (markerPrefix != 0xFF) {
      continue;
    }

    int marker = file.read();
    while (marker == 0xFF) {
      marker = file.read();
    }

    if (marker < 0) {
      return false;
    }

    if (marker == 0xD9 || marker == 0xDA) {
      return false;
    }

    uint8_t lengthBytes[2] = {};
    if (file.read(lengthBytes, sizeof(lengthBytes)) != sizeof(lengthBytes)) {
      return false;
    }
    const uint16_t segmentLength = readBe16(lengthBytes);
    if (segmentLength < 2) {
      return false;
    }

    const bool isStartOfFrame = marker == 0xC0 || marker == 0xC1 || marker == 0xC2 || marker == 0xC3 ||
                                marker == 0xC5 || marker == 0xC6 || marker == 0xC7 || marker == 0xC9 ||
                                marker == 0xCA || marker == 0xCB || marker == 0xCD || marker == 0xCE ||
                                marker == 0xCF;
    if (isStartOfFrame) {
      uint8_t frame[5] = {};
      if (file.read(frame, sizeof(frame)) != sizeof(frame)) {
        return false;
      }
      height = readBe16(frame + 1);
      width = readBe16(frame + 3);
      return width > 0 && height > 0;
    }

    file.seek(file.position() + segmentLength - 2);
  }

  return false;
}

ImageType imageTypeFromPath(const char* path) {
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".png")) {
    return ImageType::Png;
  }
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
    return ImageType::Jpg;
  }
  return ImageType::Unknown;
}

const char* imageTypeName(ImageType type) {
  switch (type) {
    case ImageType::Png:
      return "png";
    case ImageType::Jpg:
      return "jpg";
    default:
      return "unknown";
  }
}

bool inspectSdImage(const char* path, ImageAsset& asset) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }

  asset.type = imageTypeFromPath(path);
  bool dimensionsOk = false;
  if (asset.type == ImageType::Png) {
    dimensionsOk = readPngDimensions(file, asset.width, asset.height);
  } else if (asset.type == ImageType::Jpg) {
    dimensionsOk = readJpgDimensions(file, asset.width, asset.height);
  }
  file.close();
  return dimensionsOk || asset.type != ImageType::Unknown;
}

bool findImageAsset(const char* label, const char* const* candidates, size_t candidateCount, ImageAsset& asset) {
  asset = ImageAsset{};
  if (!gSdMounted) {
    Serial.printf("%s image loaded no: SD not mounted.\n", label);
    return false;
  }

  for (size_t index = 0; index < candidateCount; ++index) {
    const char* path = candidates[index];
    if (!SD.exists(path)) {
      continue;
    }

    asset.found = true;
    asset.path = path;
    inspectSdImage(path, asset);
    Serial.printf("%s image loaded yes: %s (%s %ux%u)\n", label, asset.path.c_str(), imageTypeName(asset.type),
                  asset.width, asset.height);
    return true;
  }

  Serial.printf("%s image loaded no: no accepted path found.\n", label);
  return false;
}

float fitScale(uint16_t sourceWidth, uint16_t sourceHeight, int32_t targetWidth, int32_t targetHeight) {
  if (sourceWidth == 0 || sourceHeight == 0 || targetWidth <= 0 || targetHeight <= 0) {
    return 1.0f;
  }

  const float scaleX = static_cast<float>(targetWidth) / sourceWidth;
  const float scaleY = static_cast<float>(targetHeight) / sourceHeight;
  return scaleX < scaleY ? scaleX : scaleY;
}

bool drawSdImageFit(ImageAsset& asset, const Rect& rect, const char* label) {
  if (!asset.found || asset.type == ImageType::Unknown) {
    Serial.printf("%s draw FAIL: image unavailable.\n", label);
    asset.drawn = false;
    return false;
  }

  const uint16_t sourceWidth = asset.width > 0 ? asset.width : static_cast<uint16_t>(rect.w);
  const uint16_t sourceHeight = asset.height > 0 ? asset.height : static_cast<uint16_t>(rect.h);
  const float scale = fitScale(sourceWidth, sourceHeight, rect.w, rect.h);
  const int32_t drawW = static_cast<int32_t>(sourceWidth * scale + 0.5f);
  const int32_t drawH = static_cast<int32_t>(sourceHeight * scale + 0.5f);
  const int32_t x = rect.x + (rect.w - drawW) / 2;
  const int32_t y = rect.y + (rect.h - drawH) / 2;

  bool drawn = false;
  if (asset.type == ImageType::Png) {
    drawn = M5.Display.drawPngFile(SD, asset.path.c_str(), x, y, drawW, drawH, 0, 0, scale, scale);
  } else if (asset.type == ImageType::Jpg) {
    drawn = M5.Display.drawJpgFile(SD, asset.path.c_str(), x, y, drawW, drawH, 0, 0, scale, scale);
  }

  asset.drawn = drawn;
  Serial.printf("%s draw %s: %s\n", label, drawn ? "OK" : "FAIL", asset.path.c_str());
  return drawn;
}

bool drawEmbeddedPngFit(const uint8_t* data, size_t dataSize, uint16_t sourceWidth, uint16_t sourceHeight,
                        const Rect& rect, const char* label) {
  if (data == nullptr || dataSize == 0 || sourceWidth == 0 || sourceHeight == 0) {
    Serial.printf("%s draw FAIL: embedded asset unavailable.\n", label);
    return false;
  }

  const float scale = fitScale(sourceWidth, sourceHeight, rect.w, rect.h);
  const int32_t drawW = static_cast<int32_t>(sourceWidth * scale + 0.5f);
  const int32_t drawH = static_cast<int32_t>(sourceHeight * scale + 0.5f);
  const int32_t x = rect.x + (rect.w - drawW) / 2;
  const int32_t y = rect.y + (rect.h - drawH) / 2;
  const bool drawn = M5.Display.drawPng(data, static_cast<uint32_t>(dataSize), x, y, drawW, drawH, 0, 0, scale, scale);
  Serial.printf("%s draw %s: embedded PNG (%u bytes)\n", label, drawn ? "OK" : "FAIL",
                static_cast<unsigned>(dataSize));
  return drawn;
}

void setFontById(uint8_t fontId) {
  switch (fontId) {
    case 4:
      M5.Display.setFont(&fonts::Font4);
      break;
    case 2:
    default:
      M5.Display.setFont(&fonts::Font2);
      break;
  }
}

void drawCenteredFitted(const String& rawText, int32_t centerY, uint8_t fontId, uint8_t maxSize, uint8_t minSize,
                        int32_t maxWidth) {
  auto& display = M5.Display;
  const String text = displaySafeText(rawText);
  setFontById(fontId);
  display.setTextDatum(textdatum_t::middle_center);

  uint8_t chosenSize = minSize;
  for (int size = maxSize; size >= minSize; --size) {
    display.setTextSize(size);
    if (display.textWidth(text) <= maxWidth) {
      chosenSize = static_cast<uint8_t>(size);
      break;
    }
  }

  display.setTextSize(chosenSize);
  display.drawString(text, display.width() / 2, centerY);
}

void drawMinimalFallbackText() {
  auto& display = M5.Display;
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  drawCenteredFitted(gBadge.english.name, display.height() / 2 - 50, 4, 2, 1, display.width() - 80);
  drawCenteredFitted(gBadge.english.title, display.height() / 2 + 20, 2, 2, 1, display.width() - 80);
  drawCenteredFitted(gBadge.english.footer, display.height() / 2 + 85, 2, 2, 1, display.width() - 80);
}

bool renderSdDynamicBadge() {
  gRenderMode = RenderMode::SdDynamic;
  gViewMode = ViewMode::Badge;
  applyPortraitRotation();

  auto& display = M5.Display;
  const int32_t width = display.width();
  const int32_t height = display.height();

  display.setEpdMode(m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextWrap(false, false);

  gPhotoRect = {(width - 300) / 2, 36, 300, 330};
  gQrRect = {(width - 310) / 2, height - 365, 310, 310};

  const bool photoOk = drawSdImageFit(gProfilePhoto, gPhotoRect, "profile image");
  if (!photoOk) {
    return false;
  }

  display.drawLine(70, 388, width - 70, 388, TFT_BLACK);
  drawCenteredFitted(gBadge.english.name, 442, 4, 2, 1, width - 80);
  drawCenteredFitted(gBadge.english.title, 505, 2, 2, 1, width - 80);
  drawCenteredFitted(gBadge.english.subtitle, 558, 2, 2, 1, width - 80);
  drawCenteredFitted(gBadge.english.location, 595, 2, 2, 1, width - 80);
  display.drawLine(70, 625, width - 70, 625, TFT_BLACK);

  const bool qrOk = drawSdImageFit(gQr, gQrRect, "QR image");
  if (!qrOk) {
    return false;
  }

  drawCenteredFitted(gBadge.english.footer, height - 32, 2, 2, 1, width - 80);
  display.display();
  return true;
}

void setFallbackTouchZones() {
  const int32_t width = M5.Display.width();
  const int32_t height = M5.Display.height();
  gPhotoRect = {(width - 430) / 2, 32, 430, 360};
  gQrRect = {(width - 360) / 2, height - 390, 360, 330};
}

void renderEmbeddedFallbackBadge(const char* reason) {
  gRenderMode = RenderMode::EmbeddedFallback;
  gViewMode = ViewMode::Badge;
  applyPortraitRotation();

  auto& display = M5.Display;
  display.setEpdMode(m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextWrap(false, false);

  const Rect screen = {0, 0, display.width(), display.height()};
  const bool drawn = drawEmbeddedPngFit(embedded_assets::kFullBadgePng, embedded_assets::kFullBadgePngSize,
                                        embedded_assets::kFullBadgeWidth, embedded_assets::kFullBadgeHeight, screen,
                                        "embedded fallback badge");
  if (!drawn) {
    drawMinimalFallbackText();
  }
  setFallbackTouchZones();
  display.display();

  Serial.printf("using embedded fallback badge: %s\n", reason);
}

void renderBadge() {
  const bool canTryDynamic = gSdMounted && gJsonLoaded && gProfilePhoto.found && gQr.found;

  if (canTryDynamic) {
    Serial.println("using SD dynamic badge: attempting render.");
    if (renderSdDynamicBadge()) {
      Serial.println("using SD dynamic badge: render OK.");
      return;
    }
    Serial.println("using SD dynamic badge: render failed; switching to embedded fallback badge.");
  } else {
    Serial.println("using SD dynamic badge: not available.");
  }

  renderEmbeddedFallbackBadge(canTryDynamic ? "SD component draw failed" : "SD/json/assets incomplete");
}

void renderQrZoom() {
  applyPortraitRotation();
  gViewMode = ViewMode::QrZoom;

  auto& display = M5.Display;
  const int32_t margin = 46;
  const Rect rect = {margin, (display.height() - (display.width() - margin * 2)) / 2, display.width() - margin * 2,
                     display.width() - margin * 2};

  display.setEpdMode(m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);

  bool drawn = false;
  if (gRenderMode == RenderMode::SdDynamic && gQr.found) {
    drawn = drawSdImageFit(gQr, rect, "QR zoom");
  }
  if (!drawn) {
    drawn = drawEmbeddedPngFit(embedded_assets::kQrPng, embedded_assets::kQrPngSize, embedded_assets::kQrWidth,
                               embedded_assets::kQrHeight, rect, "embedded QR zoom");
  }
  if (!drawn) {
    drawMinimalFallbackText();
  }

  display.display();
}

void renderPhotoZoom() {
  applyPortraitRotation();
  gViewMode = ViewMode::PhotoZoom;

  auto& display = M5.Display;
  const Rect rect = {24, 24, display.width() - 48, display.height() - 48};

  display.setEpdMode(m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);

  bool drawn = false;
  if (gRenderMode == RenderMode::SdDynamic && gProfilePhoto.found) {
    drawn = drawSdImageFit(gProfilePhoto, rect, "photo zoom");
  }
  if (!drawn) {
    drawn = drawEmbeddedPngFit(embedded_assets::kProfilePng, embedded_assets::kProfilePngSize,
                               embedded_assets::kProfileWidth, embedded_assets::kProfileHeight, rect,
                               "embedded photo zoom");
  }
  if (!drawn) {
    drawMinimalFallbackText();
  }

  display.display();
}

void renderDebugScreen() {
  applyPortraitRotation();
  gViewMode = ViewMode::Debug;

  auto& display = M5.Display;
  display.setEpdMode(m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setFont(&fonts::Font2);
  display.setTextSize(1);
  display.setTextDatum(textdatum_t::top_left);

  int32_t y = 24;
  display.drawString("PaperBadge debug", 24, y);
  y += 34;
  display.drawString(String("firmware: ") + kFirmwareVersion, 24, y);
  y += 28;
  display.drawString(String("mode: ") + (gRenderMode == RenderMode::SdDynamic ? "SD dynamic" : "embedded fallback"), 24,
                     y);
  y += 28;
  display.drawString(String("SD mounted: ") + (gSdMounted ? "yes" : "no"), 24, y);
  y += 28;
  display.drawString(String("badge.json: ") + (gJsonLoaded ? "loaded" : (gBadgeJsonExists ? "parse fail" : "missing")),
                     24, y);
  y += 28;
  display.drawString(String("profile: ") + (gProfilePhoto.found ? gProfilePhoto.path : "missing"), 24, y);
  y += 28;
  display.drawString(String("QR: ") + (gQr.found ? gQr.path : "missing"), 24, y);
  y += 28;
  display.drawString(String("SD full badge: ") + (gSdFullBadge.found ? gSdFullBadge.path : "missing"), 24, y);
  y += 28;
  display.drawString(String("embedded bytes: ") + static_cast<unsigned>(embedded_assets::kEmbeddedPngTotalSize), 24, y);
  y += 40;
  display.drawString("Tap to return", 24, y);

  display.display();
  Serial.println("debug screen shown.");
}

bool clickedPoint(const m5::touch_detail_t& detail, int32_t& x, int32_t& y) {
  if (!detail.wasClicked()) {
    return false;
  }

  x = constrain(detail.x, 0, M5.Display.width());
  y = constrain(detail.y, 0, M5.Display.height());
  return true;
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
  capturePortraitRotation();

  Serial.println();
  Serial.printf("PaperBadge+ %s boot\n", kFirmwareVersion);
  Serial.printf("M5 board id: %d\n", static_cast<int>(M5.getBoard()));
  Serial.printf("Display at boot: %dx%d rotation=%u\n", M5.Display.width(), M5.Display.height(),
                M5.Display.getRotation() & 3);
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
  Serial.printf("full badge fallback embedded yes: %s %ux%u, %u bytes\n", embedded_assets::kFullBadgeSource,
                embedded_assets::kFullBadgeWidth, embedded_assets::kFullBadgeHeight,
                static_cast<unsigned>(embedded_assets::kFullBadgePngSize));
  Serial.printf("embedded QR fallback %s: %s %ux%u, %u bytes\n", embedded_assets::kQrPngSize ? "yes" : "no",
                embedded_assets::kQrSource, embedded_assets::kQrWidth, embedded_assets::kQrHeight,
                static_cast<unsigned>(embedded_assets::kQrPngSize));
  Serial.printf("embedded photo fallback %s: %s %ux%u, %u bytes\n",
                embedded_assets::kProfilePngSize ? "yes" : "no", embedded_assets::kProfileSource,
                embedded_assets::kProfileWidth, embedded_assets::kProfileHeight,
                static_cast<unsigned>(embedded_assets::kProfilePngSize));

  bootBeep();

  gSdMounted = mountSdCard();
  Serial.printf("SD mounted %s.\n", gSdMounted ? "yes" : "no");

  gBadgeJsonExists = gSdMounted && SD.exists(kBadgeJsonPath);
  Serial.printf("badge.json found %s: %s\n", gBadgeJsonExists ? "yes" : "no", kBadgeJsonPath);
  gJsonLoaded = gBadgeJsonExists && loadBadgeConfigFromJson(gBadge);

  findImageAsset("profile", kProfilePhotoCandidates, countOf(kProfilePhotoCandidates), gProfilePhoto);
  findImageAsset("QR", kQrCandidates, countOf(kQrCandidates), gQr);
  findImageAsset("SD full badge", kFullBadgeCandidates, countOf(kFullBadgeCandidates), gSdFullBadge);

  renderBadge();
}

void loop() {
  M5.update();

  if (M5.Touch.isEnabled()) {
    const auto detail = M5.Touch.getDetail();

    if (detail.wasHold()) {
      renderDebugScreen();
      delay(100);
      return;
    }

    int32_t tapX = 0;
    int32_t tapY = 0;
    if (clickedPoint(detail, tapX, tapY)) {
      if (gViewMode == ViewMode::Debug || gViewMode == ViewMode::QrZoom || gViewMode == ViewMode::PhotoZoom) {
        Serial.println("View: badge");
        renderBadge();
      } else if (gQrRect.contains(tapX, tapY)) {
        Serial.println("View: QR zoom");
        renderQrZoom();
      } else if (gPhotoRect.contains(tapX, tapY)) {
        Serial.println("View: photo zoom");
        renderPhotoZoom();
      }
    }
  }

  delay(100);
}
