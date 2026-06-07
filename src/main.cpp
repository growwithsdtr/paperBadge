#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>
#include <cstring>

#include "embedded_assets.h"
#include "embedded_papercoach_deck.h"

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSdSpiHz = 25000000;
constexpr const char* kFirmwareVersion = "v1.7";
constexpr const char* kBadgeJsonPath = "/paperbadge/badge.json";
constexpr const char* kCoachDeckPath = "/papercoach/decks/interview_cards.json";
constexpr const char* kLegacyCoachDeckPath = "/papercoach/decks/sample_interview.json";
constexpr const char* kPrefsNamespace = "paperbadge";
constexpr uint32_t kDefaultLanguageIntervalSeconds = 15;
constexpr size_t kMaxCoachItems = 96;
constexpr uint8_t kMaxOptions = 4;

constexpr const char* kBadgeEnCandidates[] = {
    "/paperbadge/badge_en.png",
    "/paperbadge/completeBadge.png",
    "/paperbadge/complete_badge.png",
    "/paperbadge/badge.png",
    "/paperbadge/badge_full.png",
    "/paperbadge/full_badge.png",
};

constexpr const char* kBadgeJaCandidates[] = {
    "/paperbadge/badge_ja.png",
    "/paperbadge/badge_jp.png",
    "/paperbadge/badge_japanese.png",
    "/paperbadge/completeBadge_ja.png",
    "/paperbadge/complete_badge_ja.png",
};

constexpr const char* kProfileCandidates[] = {
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

template <typename T, size_t N>
constexpr size_t countOf(const T (&)[N]) {
  return N;
}

enum class ImageType {
  Unknown,
  Png,
  Jpg,
};

enum class Screen {
  Badge,
  Home,
  Settings,
  Debug,
  QrZoom,
  PhotoZoom,
  InterviewPractice,
  BlitzQuiz,
  WeakAnswerDetector,
  MetricPrecision,
  HostileFollowup,
  Glossary,
  MockInterview,
};

enum class BadgeLanguage {
  English,
  Japanese,
};

enum class OrientationMode : uint8_t {
  Strap = 0,
  Handheld = 1,
};

enum class LanguageMode : uint8_t {
  Auto = 0,
  English = 1,
  Japanese = 2,
};

enum class FontSizeMode : uint8_t {
  Medium = 0,
  Large = 1,
  XL = 2,
  Huge = 3,
};

enum class CoachItemType : uint8_t {
  Qa,
  Mcq,
  WeakAnswer,
  Glossary,
  HostileFollowup,
  MetricPrecision,
};

enum class CoachStage : uint8_t {
  Prompt,
  Answer,
  Rubric,
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

struct ImageAsset {
  bool found = false;
  ImageType type = ImageType::Unknown;
  String path;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct BadgeConfig {
  uint32_t intervalSeconds = kDefaultLanguageIntervalSeconds;
};

struct Settings {
  OrientationMode orientationMode = OrientationMode::Strap;
  LanguageMode languageMode = LanguageMode::Auto;
  FontSizeMode fontSizeMode = FontSizeMode::Large;
};

struct CoachItem {
  CoachItemType type = CoachItemType::Qa;
  String id;
  String sectionId;
  String section;
  String number;
  String title;
  bool mustMaster = false;
  String theme;
  String spoken;
  String anchor;
  String confidence;
  String watch;
  String prompt;
  String answer;
  String rubric;
  String weakAnswer;
  String category;
  String term;
  String definition;
  String explanation;
  String options[kMaxOptions];
  uint8_t optionCount = 0;
  uint8_t correctIndex = 0;
};

struct CoachItemView {
  CoachItemType type = CoachItemType::Qa;
  const char* id = "";
  const char* sectionId = "";
  const char* section = "";
  const char* number = "";
  const char* title = "";
  bool mustMaster = false;
  const char* theme = "";
  const char* spoken = "";
  const char* anchor = "";
  const char* confidence = "";
  const char* watch = "";
  const char* prompt = "";
  const char* answer = "";
  const char* rubric = "";
  const char* weakAnswer = "";
  const char* category = "";
  const char* term = "";
  const char* definition = "";
  const char* explanation = "";
  const char* options[kMaxOptions] = {"", "", "", ""};
  uint8_t optionCount = 0;
  uint8_t correctIndex = 0;
};

BadgeConfig gBadgeConfig;
Settings gSettings;
Preferences gPrefs;
ImageAsset gBadgeEnSd;
ImageAsset gBadgeJaSd;
ImageAsset gProfileSd;
ImageAsset gQrSd;
Screen gScreen = Screen::Badge;
BadgeLanguage gBadgeLanguage = BadgeLanguage::English;
Rect gPhotoRect;
Rect gQrRect;
Rect gBadgeButton;
Rect gInterviewButton;
Rect gBlitzButton;
Rect gWeakAnswerButton;
Rect gMetricPrecisionButton;
Rect gHostileFollowupButton;
Rect gGlossaryButton;
Rect gMockInterviewButton;
Rect gSettingsButton;
Rect gDebugButton;
Rect gOrientationButton;
Rect gLanguageAutoButton;
Rect gLanguageEnglishButton;
Rect gLanguageJapaneseButton;
Rect gFontMediumButton;
Rect gFontLargeButton;
Rect gFontXlButton;
Rect gFontHugeButton;
Rect gHomeButton;
Rect gNextButton;
Rect gTouchDebugButton;
Rect gOptionButtons[kMaxOptions];
bool gSdMounted = false;
bool gBadgeJsonLoaded = false;
bool gCoachDeckLoadedFromSd = false;
size_t gCoachMustMasterCount = 0;
String gCoachDeckSource = "embedded";
uint8_t gNormalPortraitRotation = 0;
uint32_t gLastLanguageSwitchMs = 0;
String gLastBadgeSource = "embedded";
CoachItem gCoachItems[kMaxCoachItems];
size_t gCoachItemCount = 0;
size_t gCoachIndex = 0;
uint8_t gCoachStage = 0;
int8_t gSelectedOption = -1;
uint8_t gMockStep = 0;
uint8_t gBottomLeftTapCount = 0;
uint32_t gLastBottomLeftTapMs = 0;
bool gTouchDebugEnabled = false;
int32_t gLastTouchDownX = -1;
int32_t gLastTouchDownY = -1;
int32_t gLastTouchUpX = -1;
int32_t gLastTouchUpY = -1;

struct EmbeddedCoachItem {
  CoachItemType type;
  const char* prompt;
  const char* answer;
  const char* rubric;
  const char* weakAnswer;
  const char* category;
  const char* term;
  const char* definition;
  const char* explanation;
  const char* options[kMaxOptions];
  uint8_t optionCount;
  uint8_t correctIndex;
};

constexpr EmbeddedCoachItem kEmbeddedCoachItems[] = {
    {CoachItemType::Qa,
     "Tell me about a time you shipped an AI feature from zero to one.",
     "Frame the customer pain, the riskiest assumption, the smallest useful launch, and the measured behavior change.",
     "Watch-outs: vague AI excitement, no adoption metric, no safety or rollback plan.",
     "",
     "",
     "",
     "",
     "",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::Qa,
     "How would you decide whether to build, buy, or partner for an LLM capability?",
     "Compare differentiation, latency, data control, vendor risk, unit economics, and iteration speed.",
     "Strong answers make a reversible first step and name the future trigger to revisit the decision.",
     "",
     "",
     "",
     "",
     "",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::Mcq,
     "Which metric best proves an AI summarization feature is creating value?",
     "Correct: repeat usage tied to saved workflow time.",
     "",
     "",
     "",
     "",
     "",
     "Accuracy alone is not enough; value shows up when users repeatedly choose the feature and finish faster.",
     {"Model benchmark score", "Repeat usage plus time saved", "Prompt token count", "Number of generated summaries"},
     4,
     1},
    {CoachItemType::Mcq,
     "A launch has high trial but low retention. What is the best first PM move?",
     "Correct: inspect the activation path and compare retained vs. churned users.",
     "",
     "",
     "",
     "",
     "",
     "Retention failure usually means the promise, first value moment, or workflow fit is broken.",
     {"Increase ads", "Add more model choices", "Analyze activation and retained cohorts", "Rewrite the roadmap"},
     4,
     2},
    {CoachItemType::WeakAnswer,
     "Interviewer: How did you handle model hallucinations?",
     "",
     "",
     "We improved the prompts and told users the AI might be wrong.",
     "missing operational control",
     "",
     "",
     "A stronger answer names evaluation data, confidence thresholds, UX guardrails, escalation, and monitoring.",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::WeakAnswer,
     "Interviewer: Why did your pilot fail?",
     "",
     "",
     "The customer was not ready and sales picked the wrong account.",
     "low ownership",
     "",
     "",
     "Own the learning: qualification criteria, onboarding gaps, buyer/user mismatch, and what changed next.",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::Glossary,
     "",
     "",
     "",
     "",
     "",
     "Guardrail metric",
     "A metric that must stay within bounds while optimizing the primary goal, such as latency, trust, or cost.",
     "",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::Glossary,
     "",
     "",
     "",
     "",
     "",
     "Activation",
     "The first meaningful moment when a user experiences the product's core value.",
     "",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::HostileFollowup,
     "Your AI feature sounds like a wrapper. Why should we hire you?",
     "Acknowledge the risk, then show where the product differentiated: workflow fit, data loop, distribution, trust, or cost.",
     "Do not get defensive. Convert the challenge into a crisp strategy answer.",
     "",
     "",
     "",
     "",
     "",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::HostileFollowup,
     "Why did it take your team so long to learn the customer did not care?",
     "Explain the early signal you missed, the decision rule you added, and how cycle time improved afterward.",
     "Strong answers show humility plus a concrete operating-system change.",
     "",
     "",
     "",
     "",
     "",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::MetricPrecision,
     "Define a success metric for an AI interview coach.",
     "Primary: weekly completed practice sessions leading to improved rubric score. Guardrails: answer quality, latency, and churn.",
     "Name numerator, denominator, cohort, timeframe, and one countermetric.",
     "",
     "",
     "",
     "",
     "",
     {"", "", "", ""},
     0,
     0},
    {CoachItemType::MetricPrecision,
     "How would you measure PMF for an AI workflow tool in B2B SaaS?",
     "Use retained active teams, workflow completion, expansion intent, and qualitative pull from buyers and users.",
     "Avoid one-off demo excitement. Tie usage to a recurring business workflow.",
     "",
     "",
     "",
     "",
     "",
     {"", "", "", ""},
     0,
     0},
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

void captureNormalPortraitRotation() {
  auto& display = M5.Display;
  const uint8_t currentRotation = display.getRotation() & 3;
  gNormalPortraitRotation = display.width() > display.height() ? ((currentRotation + 1) & 3) : currentRotation;
}

void applyBadgeRotation() {
  const uint8_t rotation =
      gSettings.orientationMode == OrientationMode::Strap ? ((gNormalPortraitRotation + 2) & 3) : gNormalPortraitRotation;
  M5.Display.setRotation(rotation);
}

void applyAppRotation() {
  M5.Display.setRotation(gNormalPortraitRotation);
}

bool mountSdCard() {
  const int8_t sclk = M5.getPin(m5::sd_spi_sclk);
  const int8_t mosi = M5.getPin(m5::sd_spi_mosi);
  const int8_t miso = M5.getPin(m5::sd_spi_miso);
  const int8_t cs = M5.getPin(m5::sd_spi_cs);

  Serial.printf("SD SPI pins: SCLK=%d MOSI=%d MISO=%d CS=%d\n", sclk, mosi, miso, cs);
  if (sclk < 0 || mosi < 0 || miso < 0 || cs < 0) {
    Serial.println("SD mounted no: PaperS3 SD pins unavailable.");
    return false;
  }

  SPI.begin(sclk, miso, mosi, cs);
  return SD.begin(cs, SPI, kSdSpiHz);
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
    if (marker < 0 || marker == 0xD9 || marker == 0xDA) {
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

const char* languageName(BadgeLanguage language) {
  return language == BadgeLanguage::Japanese ? "Japanese" : "English";
}

const char* orientationModeName() {
  return gSettings.orientationMode == OrientationMode::Strap ? "strap" : "handheld";
}

const char* languageModeName() {
  switch (gSettings.languageMode) {
    case LanguageMode::English:
      return "English";
    case LanguageMode::Japanese:
      return "Japanese";
    case LanguageMode::Auto:
    default:
      return "auto";
  }
}

const char* fontSizeModeName() {
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      return "Medium";
    case FontSizeMode::XL:
      return "XL";
    case FontSizeMode::Huge:
      return "Huge";
    case FontSizeMode::Large:
    default:
      return "Large";
  }
}

void loadSettings() {
  gPrefs.begin(kPrefsNamespace, true);
  const uint8_t orientation = gPrefs.getUChar("orient", static_cast<uint8_t>(OrientationMode::Strap));
  const uint8_t language = gPrefs.getUChar("lang", static_cast<uint8_t>(LanguageMode::Auto));
  const uint8_t fontSize = gPrefs.getUChar("font", static_cast<uint8_t>(FontSizeMode::Large));
  gPrefs.end();

  gSettings.orientationMode = orientation == static_cast<uint8_t>(OrientationMode::Handheld) ? OrientationMode::Handheld
                                                                                             : OrientationMode::Strap;
  if (language == static_cast<uint8_t>(LanguageMode::English)) {
    gSettings.languageMode = LanguageMode::English;
  } else if (language == static_cast<uint8_t>(LanguageMode::Japanese)) {
    gSettings.languageMode = LanguageMode::Japanese;
  } else {
    gSettings.languageMode = LanguageMode::Auto;
  }

  if (fontSize == static_cast<uint8_t>(FontSizeMode::Medium)) {
    gSettings.fontSizeMode = FontSizeMode::Medium;
  } else if (fontSize == static_cast<uint8_t>(FontSizeMode::XL)) {
    gSettings.fontSizeMode = FontSizeMode::XL;
  } else if (fontSize == static_cast<uint8_t>(FontSizeMode::Huge)) {
    gSettings.fontSizeMode = FontSizeMode::Huge;
  } else {
    gSettings.fontSizeMode = FontSizeMode::Large;
  }

  Serial.printf("Settings loaded: orientation=%s language=%s font=%s\n", orientationModeName(), languageModeName(),
                fontSizeModeName());
}

void saveSettings() {
  gPrefs.begin(kPrefsNamespace, false);
  gPrefs.putUChar("orient", static_cast<uint8_t>(gSettings.orientationMode));
  gPrefs.putUChar("lang", static_cast<uint8_t>(gSettings.languageMode));
  gPrefs.putUChar("font", static_cast<uint8_t>(gSettings.fontSizeMode));
  gPrefs.end();
  Serial.printf("Settings saved: orientation=%s language=%s font=%s\n", orientationModeName(), languageModeName(),
                fontSizeModeName());
}

void enforceLanguageMode() {
  if (gSettings.languageMode == LanguageMode::English) {
    gBadgeLanguage = BadgeLanguage::English;
  } else if (gSettings.languageMode == LanguageMode::Japanese) {
    gBadgeLanguage = BadgeLanguage::Japanese;
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

bool findSdImage(const char* label, const char* const* candidates, size_t candidateCount, ImageAsset& asset) {
  asset = ImageAsset{};
  if (!gSdMounted) {
    Serial.printf("%s SD image loaded no: SD not mounted.\n", label);
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
    Serial.printf("%s SD image loaded yes: %s (%ux%u)\n", label, asset.path.c_str(), asset.width, asset.height);
    return true;
  }

  Serial.printf("%s SD image loaded no: no accepted path found.\n", label);
  return false;
}

bool loadBadgeJson() {
  if (!gSdMounted || !SD.exists(kBadgeJsonPath)) {
    Serial.printf("badge.json loaded no: %s missing.\n", kBadgeJsonPath);
    return false;
  }

  File file = SD.open(kBadgeJsonPath, FILE_READ);
  if (!file) {
    Serial.println("badge.json loaded no: open failed.");
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.printf("badge.json loaded no: %s\n", error.c_str());
    return false;
  }

  const uint32_t intervalSeconds = doc["interval_seconds"] | kDefaultLanguageIntervalSeconds;
  gBadgeConfig.intervalSeconds = intervalSeconds > 0 ? intervalSeconds : kDefaultLanguageIntervalSeconds;
  Serial.printf("badge.json loaded yes: interval_seconds=%u\n", gBadgeConfig.intervalSeconds);
  return true;
}

const char* coachTypeName(CoachItemType type) {
  switch (type) {
    case CoachItemType::Mcq:
      return "mcq";
    case CoachItemType::WeakAnswer:
      return "weak_answer";
    case CoachItemType::Glossary:
      return "glossary";
    case CoachItemType::HostileFollowup:
      return "hostile_followup";
    case CoachItemType::MetricPrecision:
      return "metric_precision";
    case CoachItemType::Qa:
    default:
      return "qa";
  }
}

CoachItemType parseCoachType(const char* rawType) {
  const String type = rawType ? String(rawType) : String("qa");
  if (type == "mcq") {
    return CoachItemType::Mcq;
  }
  if (type == "weak_answer") {
    return CoachItemType::WeakAnswer;
  }
  if (type == "glossary") {
    return CoachItemType::Glossary;
  }
  if (type == "hostile_followup") {
    return CoachItemType::HostileFollowup;
  }
  if (type == "metric_precision") {
    return CoachItemType::MetricPrecision;
  }
  return CoachItemType::Qa;
}

void clearCoachDeck() {
  for (size_t index = 0; index < kMaxCoachItems; ++index) {
    gCoachItems[index] = CoachItem{};
  }
  gCoachItemCount = 0;
  gCoachMustMasterCount = 0;
  gCoachIndex = 0;
  gCoachStage = 0;
  gSelectedOption = -1;
  gMockStep = 0;
}

bool addCoachItem(const CoachItem& item) {
  if (gCoachItemCount >= kMaxCoachItems) {
    return false;
  }
  gCoachItems[gCoachItemCount++] = item;
  return true;
}

void loadEmbeddedCoachDeck() {
  clearCoachDeck();
  gCoachItemCount = embedded_papercoach::kCardCount;
  gCoachMustMasterCount = embedded_papercoach::kMustMasterCount;
  gCoachDeckLoadedFromSd = false;
  gCoachDeckSource = "embedded";
  Serial.println("PaperCoach deck source: embedded");
  Serial.printf("PaperCoach card count: %u\n", static_cast<unsigned>(gCoachItemCount));
  Serial.printf("PaperCoach must-master count: %u\n", static_cast<unsigned>(gCoachMustMasterCount));
}

bool loadCoachDeckFromSd() {
  const char* path = nullptr;
  if (gSdMounted && SD.exists(kCoachDeckPath)) {
    path = kCoachDeckPath;
  } else if (gSdMounted && SD.exists(kLegacyCoachDeckPath)) {
    path = kLegacyCoachDeckPath;
  }

  if (!path) {
    Serial.printf("PaperCoach SD deck missing: %s\n", kCoachDeckPath);
    return false;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.println("PaperCoach SD deck open failed.");
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.printf("PaperCoach SD deck parse failed: %s\n", error.c_str());
    return false;
  }

  const bool generatedSchema = !doc["cards"].isNull();
  JsonArray items = generatedSchema ? doc["cards"].as<JsonArray>() : doc["items"].as<JsonArray>();
  if (items.isNull() || items.size() == 0) {
    Serial.println("PaperCoach SD deck empty.");
    return false;
  }

  clearCoachDeck();
  for (JsonObject object : items) {
    CoachItem item;
    item.type = parseCoachType(object["type"] | "qa");
    item.id = object["id"] | "";
    item.sectionId = object["section_id"] | "";
    item.section = object["section"] | "";
    item.number = object["number"] | "";
    item.title = object["title"] | "";
    item.mustMaster = object["must_master"] | false;
    item.theme = object["theme"] | "";
    item.spoken = object["spoken"] | "";
    item.anchor = object["anchor"] | "";
    item.confidence = object["confidence"] | "";
    item.watch = object["watch"] | "";
    item.prompt = object["prompt"] | "";
    if (item.prompt.length() == 0) {
      item.prompt = item.title;
    }
    item.answer = object["answer"] | "";
    if (item.answer.length() == 0) {
      item.answer = item.spoken;
    }
    item.rubric = object["rubric"] | "";
    if (item.rubric.length() == 0) {
      item.rubric = item.anchor;
    }
    item.weakAnswer = object["weak_answer"] | "";
    item.category = object["category"] | "";
    item.term = object["term"] | "";
    item.definition = object["definition"] | "";
    item.explanation = object["explanation"] | "";
    item.correctIndex = object["correct_index"] | 0;

    JsonArray options = object["options"].as<JsonArray>();
    item.optionCount = 0;
    if (!options.isNull()) {
      for (JsonVariant option : options) {
        if (item.optionCount >= kMaxOptions) {
          break;
        }
        item.options[item.optionCount++] = option.as<const char*>();
      }
    }
    if (item.mustMaster) {
      ++gCoachMustMasterCount;
    }
    addCoachItem(item);
  }

  if (gCoachItemCount == 0) {
    Serial.println("PaperCoach SD deck had no usable items.");
    return false;
  }

  gCoachDeckLoadedFromSd = true;
  gCoachDeckSource = "SD";
  Serial.printf("PaperCoach deck source: SD (%s)\n", path);
  Serial.printf("PaperCoach card count: %u\n", static_cast<unsigned>(gCoachItemCount));
  Serial.printf("PaperCoach must-master count: %u\n", static_cast<unsigned>(gCoachMustMasterCount));
  return true;
}

void loadCoachDeck() {
  if (!loadCoachDeckFromSd()) {
    loadEmbeddedCoachDeck();
  }
}

CoachItemView coachItemAt(size_t index) {
  CoachItemView view;
  if (gCoachDeckLoadedFromSd && index < gCoachItemCount) {
    const CoachItem& item = gCoachItems[index];
    view.type = item.type;
    view.id = item.id.c_str();
    view.sectionId = item.sectionId.c_str();
    view.section = item.section.c_str();
    view.number = item.number.c_str();
    view.title = item.title.c_str();
    view.mustMaster = item.mustMaster;
    view.theme = item.theme.c_str();
    view.spoken = item.spoken.c_str();
    view.anchor = item.anchor.c_str();
    view.confidence = item.confidence.c_str();
    view.watch = item.watch.c_str();
    view.prompt = item.prompt.c_str();
    view.answer = item.answer.c_str();
    view.rubric = item.rubric.c_str();
    view.weakAnswer = item.weakAnswer.c_str();
    view.category = item.category.c_str();
    view.term = item.term.c_str();
    view.definition = item.definition.c_str();
    view.explanation = item.explanation.c_str();
    view.optionCount = item.optionCount;
    view.correctIndex = item.correctIndex;
    for (uint8_t option = 0; option < kMaxOptions; ++option) {
      view.options[option] = item.options[option].c_str();
    }
    return view;
  }

  if (index < embedded_papercoach::kCardCount) {
    const auto& card = embedded_papercoach::kCards[index];
    view.type = CoachItemType::Qa;
    view.id = card.id;
    view.sectionId = card.sectionId;
    view.section = card.section;
    view.number = card.number;
    view.title = card.title;
    view.mustMaster = card.mustMaster;
    view.theme = card.theme;
    view.spoken = card.spoken;
    view.anchor = card.anchor;
    view.confidence = card.confidence;
    view.watch = card.watch;
    view.prompt = card.title;
    view.answer = card.spoken;
    view.rubric = card.anchor;
  }
  return view;
}

bool itemMatchesScreen(const CoachItemView& item, Screen screen) {
  if (screen == Screen::InterviewPractice) {
    return item.type == CoachItemType::Qa || item.type == CoachItemType::HostileFollowup ||
           item.type == CoachItemType::MetricPrecision;
  }
  if (screen == Screen::BlitzQuiz) {
    return item.type == CoachItemType::Mcq && item.optionCount > 0;
  }
  if (screen == Screen::WeakAnswerDetector) {
    return item.type == CoachItemType::WeakAnswer;
  }
  if (screen == Screen::MetricPrecision) {
    return item.type == CoachItemType::MetricPrecision;
  }
  if (screen == Screen::HostileFollowup) {
    return item.type == CoachItemType::HostileFollowup;
  }
  if (screen == Screen::Glossary) {
    return item.type == CoachItemType::Glossary;
  }
  if (screen == Screen::MockInterview) {
    return item.type == CoachItemType::Qa || item.type == CoachItemType::HostileFollowup ||
           item.type == CoachItemType::MetricPrecision;
  }
  return false;
}

size_t findCoachItem(Screen screen, size_t startIndex) {
  if (gCoachItemCount == 0) {
    return 0;
  }
  for (size_t offset = 0; offset < gCoachItemCount; ++offset) {
    const size_t index = (startIndex + offset) % gCoachItemCount;
    if (itemMatchesScreen(coachItemAt(index), screen)) {
      return index;
    }
  }
  return 0;
}

void startCoachMode(Screen screen) {
  gScreen = screen;
  gCoachIndex = findCoachItem(screen, 0);
  gCoachStage = 0;
  gSelectedOption = -1;
  gMockStep = 0;
}

void nextCoachItem() {
  if (gCoachItemCount == 0) {
    return;
  }
  gCoachIndex = findCoachItem(gScreen, (gCoachIndex + 1) % gCoachItemCount);
  gCoachStage = 0;
  gSelectedOption = -1;
  if (gScreen == Screen::MockInterview) {
    gMockStep = (gMockStep + 1) % 5;
  }
}

float fitScale(uint16_t sourceWidth, uint16_t sourceHeight, int32_t targetWidth, int32_t targetHeight) {
  if (sourceWidth == 0 || sourceHeight == 0 || targetWidth <= 0 || targetHeight <= 0) {
    return 1.0f;
  }

  const float scaleX = static_cast<float>(targetWidth) / sourceWidth;
  const float scaleY = static_cast<float>(targetHeight) / sourceHeight;
  return scaleX < scaleY ? scaleX : scaleY;
}

bool drawSdImageFit(const ImageAsset& asset, const Rect& rect, const char* label) {
  if (!asset.found || asset.type == ImageType::Unknown) {
    Serial.printf("%s draw FAIL: SD image unavailable.\n", label);
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

  Serial.printf("%s draw %s: %s\n", label, drawn ? "OK" : "FAIL", asset.path.c_str());
  return drawn;
}

bool drawEmbeddedPngFit(const uint8_t* data, size_t dataSize, uint16_t sourceWidth, uint16_t sourceHeight,
                        const Rect& rect, const char* label) {
  if (data == nullptr || dataSize == 0 || sourceWidth == 0 || sourceHeight == 0) {
    Serial.printf("%s draw FAIL: embedded image unavailable.\n", label);
    return false;
  }

  const float scale = fitScale(sourceWidth, sourceHeight, rect.w, rect.h);
  const int32_t drawW = static_cast<int32_t>(sourceWidth * scale + 0.5f);
  const int32_t drawH = static_cast<int32_t>(sourceHeight * scale + 0.5f);
  const int32_t x = rect.x + (rect.w - drawW) / 2;
  const int32_t y = rect.y + (rect.h - drawH) / 2;
  const bool drawn = M5.Display.drawPng(data, static_cast<uint32_t>(dataSize), x, y, drawW, drawH, 0, 0, scale, scale);
  Serial.printf("%s draw %s: embedded (%u bytes)\n", label, drawn ? "OK" : "FAIL", static_cast<unsigned>(dataSize));
  return drawn;
}

void prepareFullRefresh(const char* reason = nullptr, bool highQuality = false) {
  auto& display = M5.Display;
  if (reason && reason[0] != '\0') {
    Serial.printf("Full refresh: %s\n", reason);
  }
  display.setEpdMode(highQuality ? m5gfx::epd_quality : m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextWrap(false, false);
}

void cleanWhiteRefresh(const char* reason) {
  auto& display = M5.Display;
  Serial.printf("Full refresh: %s\n", reason);
  display.setEpdMode(m5gfx::epd_quality);
  display.fillScreen(TFT_WHITE);
  display.display();
  delay(250);
  display.setEpdMode(m5gfx::epd_fastest);
}

bool drawBadgeImage(BadgeLanguage language) {
  auto& display = M5.Display;
  const Rect screen = {0, 0, display.width(), display.height()};
  bool drawn = false;

  if (language == BadgeLanguage::Japanese) {
    drawn = drawSdImageFit(gBadgeJaSd, screen, "badge JA SD");
    if (drawn) {
      gLastBadgeSource = "SD badge_ja";
      return true;
    }
    drawn = drawEmbeddedPngFit(embedded_assets::kBadgeJaPng, embedded_assets::kBadgeJaPngSize,
                               embedded_assets::kBadgeJaWidth, embedded_assets::kBadgeJaHeight, screen,
                               "badge JA embedded");
    gLastBadgeSource = "embedded badge_ja";
    return drawn;
  }

  drawn = drawSdImageFit(gBadgeEnSd, screen, "badge EN SD");
  if (drawn) {
    gLastBadgeSource = "SD badge_en";
    return true;
  }
  drawn = drawEmbeddedPngFit(embedded_assets::kBadgeEnPng, embedded_assets::kBadgeEnPngSize,
                             embedded_assets::kBadgeEnWidth, embedded_assets::kBadgeEnHeight, screen,
                             "badge EN embedded");
  gLastBadgeSource = "embedded badge_en";
  return drawn;
}

void drawMinimalFallbackText() {
  auto& display = M5.Display;
  display.setFont(&fonts::Font4);
  display.setTextSize(2);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString("Daniel Jimenez", display.width() / 2, display.height() / 2 - 40);
  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString("Product Manager (AI)", display.width() / 2, display.height() / 2 + 30);
}

void setBadgeTouchZones() {
  gPhotoRect = {94, 32, 352, 350};
  gQrRect = {104, 600, 332, 330};
}

void renderBadge(bool highQualityRefresh = false, const char* refreshReason = nullptr) {
  gScreen = Screen::Badge;
  enforceLanguageMode();
  applyBadgeRotation();
  prepareFullRefresh(refreshReason, highQualityRefresh);

  if (!drawBadgeImage(gBadgeLanguage)) {
    Serial.println("badge draw failed: using minimal text fallback.");
    drawMinimalFallbackText();
    gLastBadgeSource = "minimal text fallback";
  }

  setBadgeTouchZones();
  M5.Display.display();
  gLastLanguageSwitchMs = millis();
  Serial.printf("Badge mode: language=%s source=%s orientation=%s\n", languageName(gBadgeLanguage),
                gLastBadgeSource.c_str(), orientationModeName());
}

void drawButton(const Rect& rect, const char* label) {
  auto& display = M5.Display;
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  display.setTextDatum(textdatum_t::middle_center);
  display.setFont(&fonts::Font4);
  display.setTextSize(1);
  display.drawString(label, rect.x + rect.w / 2, rect.y + rect.h / 2);
}

void drawButton(const Rect& rect, const String& label) {
  drawButton(rect, label.c_str());
}

bool isCoachScreen(Screen screen) {
  return screen == Screen::InterviewPractice || screen == Screen::BlitzQuiz || screen == Screen::WeakAnswerDetector ||
         screen == Screen::MetricPrecision || screen == Screen::HostileFollowup || screen == Screen::Glossary ||
         screen == Screen::MockInterview;
}

const char* coachScreenTitle(Screen screen) {
  switch (screen) {
    case Screen::BlitzQuiz:
      return "Blitz Quiz";
    case Screen::WeakAnswerDetector:
      return "Weak Answer Detector";
    case Screen::MetricPrecision:
      return "Metric Precision";
    case Screen::HostileFollowup:
      return "Hostile Follow-up";
    case Screen::Glossary:
      return "Glossary";
    case Screen::MockInterview:
      return "Mock Interview";
    case Screen::InterviewPractice:
    default:
      return "Interview Practice";
  }
}

const char* screenName(Screen screen) {
  switch (screen) {
    case Screen::Home:
      return "Home";
    case Screen::Settings:
      return "Settings";
    case Screen::Debug:
      return "Debug";
    case Screen::QrZoom:
      return "QR zoom";
    case Screen::PhotoZoom:
      return "Photo zoom";
    case Screen::InterviewPractice:
    case Screen::BlitzQuiz:
    case Screen::WeakAnswerDetector:
    case Screen::MetricPrecision:
    case Screen::HostileFollowup:
    case Screen::Glossary:
    case Screen::MockInterview:
      return coachScreenTitle(screen);
    case Screen::Badge:
    default:
      return "Badge";
  }
}

uint8_t coachContentTextSize() {
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      return 1;
    case FontSizeMode::XL:
      return 1;
    case FontSizeMode::Huge:
      return 2;
    case FontSizeMode::Large:
    default:
      return 2;
  }
}

void applyCoachContentFont() {
  auto& display = M5.Display;
  if (gSettings.fontSizeMode == FontSizeMode::XL || gSettings.fontSizeMode == FontSizeMode::Huge) {
    display.setFont(&fonts::Font4);
  } else {
    display.setFont(&fonts::Font2);
  }
  display.setTextSize(coachContentTextSize());
}

int32_t coachLineHeight() {
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      return 24;
    case FontSizeMode::XL:
      return 42;
    case FontSizeMode::Huge:
      return 66;
    case FontSizeMode::Large:
    default:
      return 34;
  }
}

uint8_t coachPromptLineCount() {
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      return 10;
    case FontSizeMode::XL:
      return 6;
    case FontSizeMode::Huge:
      return 4;
    case FontSizeMode::Large:
    default:
      return 8;
  }
}

uint8_t coachAnswerLineCount() {
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      return 8;
    case FontSizeMode::XL:
      return 4;
    case FontSizeMode::Huge:
      return 3;
    case FontSizeMode::Large:
    default:
      return 7;
  }
}

uint8_t coachRubricLineCount() {
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      return 5;
    case FontSizeMode::XL:
      return 3;
    case FontSizeMode::Huge:
      return 2;
    case FontSizeMode::Large:
    default:
      return 4;
  }
}

bool isBottomLeftTap(int32_t x, int32_t y) {
  return x < M5.Display.width() / 3 && y > (M5.Display.height() * 2) / 3;
}

bool recordBottomLeftTripleTap(int32_t x, int32_t y) {
  if (!isBottomLeftTap(x, y)) {
    return false;
  }

  const uint32_t now = millis();
  if (now - gLastBottomLeftTapMs > 1600) {
    gBottomLeftTapCount = 0;
  }
  gLastBottomLeftTapMs = now;
  ++gBottomLeftTapCount;

  if (gBottomLeftTapCount >= 3) {
    gBottomLeftTapCount = 0;
    Serial.printf("triple tap detected: x=%ld y=%ld\n", static_cast<long>(x), static_cast<long>(y));
    return true;
  }
  return false;
}

void drawWrappedText(const String& text, int32_t x, int32_t y, int32_t width, int32_t lineHeight, uint8_t maxLines) {
  auto& display = M5.Display;
  String line;
  String word;
  uint8_t lines = 0;

  for (size_t index = 0; index <= text.length(); ++index) {
    const char ch = index < text.length() ? text[index] : ' ';
    if (ch != ' ' && ch != '\n') {
      word += ch;
      continue;
    }

    if (word.length() > 0) {
      const String candidate = line.length() == 0 ? word : line + " " + word;
      if (line.length() > 0 && display.textWidth(candidate) > width) {
        display.drawString(line, x, y + lines * lineHeight);
        ++lines;
        if (lines >= maxLines) {
          return;
        }
        line = word;
      } else {
        line = candidate;
      }
      word = "";
    }

    if (ch == '\n' && line.length() > 0) {
      display.drawString(line, x, y + lines * lineHeight);
      ++lines;
      if (lines >= maxLines) {
        return;
      }
      line = "";
    }
  }

  if (line.length() > 0 && lines < maxLines) {
    display.drawString(line, x, y + lines * lineHeight);
  }
}

void drawCoachChrome(const char* title) {
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  display.setFont(&fonts::Font4);
  display.setTextSize(1);
  display.drawString(title, 28, 28);
  display.setFont(&fonts::Font2);
  display.setTextSize(1);
  display.drawString(gCoachDeckLoadedFromSd ? "SD deck" : "Embedded deck", 30, 82);

  gHomeButton = {28, display.height() - 92, 164, 64};
  gNextButton = {display.width() - 192, display.height() - 92, 164, 64};
  drawButton(gHomeButton, "Home");
  drawButton(gNextButton, "Next");
}

String coachPromptFor(const CoachItemView& item) {
  if (item.type == CoachItemType::Glossary) {
    return item.term;
  }
  if (item.type == CoachItemType::WeakAnswer) {
    return String(item.prompt) + "\nWeak answer: " + item.weakAnswer;
  }
  return item.prompt;
}

String coachAnswerFor(const CoachItemView& item) {
  if (item.type == CoachItemType::Glossary) {
    return item.definition;
  }
  if (item.type == CoachItemType::WeakAnswer) {
    return String(item.category) + ": " + item.explanation;
  }
  return item.answer;
}

String coachRubricFor(const CoachItemView& item) {
  if (strlen(item.explanation) > 0 && item.type != CoachItemType::WeakAnswer) {
    return item.explanation;
  }
  String rubric = item.rubric;
  if (strlen(item.watch) > 0) {
    if (rubric.length() > 0) {
      rubric += "\nWatch: ";
    }
    rubric += item.watch;
  }
  return rubric;
}

void renderCoachScreen() {
  applyAppRotation();
  prepareFullRefresh();
  drawCoachChrome(coachScreenTitle(gScreen));

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachContentFont();
  const int32_t lineHeight = coachLineHeight();
  const uint8_t promptLines = coachPromptLineCount();
  const uint8_t answerLines = coachAnswerLineCount();
  const uint8_t rubricLines = coachRubricLineCount();

  if (gCoachItemCount == 0) {
    drawWrappedText("No PaperCoach deck available.", 34, 140, display.width() - 68, lineHeight, promptLines);
    display.display();
    return;
  }

  const CoachItemView item = coachItemAt(gCoachIndex);
  if (gScreen == Screen::MockInterview) {
    display.drawString(String("Prompt ") + (gMockStep + 1) + "/5", 34, 118);
  }

  if (gScreen == Screen::BlitzQuiz) {
    drawWrappedText(item.prompt, 34, 124, display.width() - 68, lineHeight, promptLines < 5 ? promptLines : 5);
    int32_t y = 320;
    for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
      gOptionButtons[option] = {34, y, display.width() - 68, 68};
      String label = String(static_cast<char>('A' + option)) + ". " + item.options[option];
      if (gSelectedOption >= 0) {
        if (option == item.correctIndex) {
          label = String("* ") + label;
        } else if (option == static_cast<uint8_t>(gSelectedOption)) {
          label = String("x ") + label;
        }
      }
      drawButton(gOptionButtons[option], label);
      y += 82;
    }
    if (gSelectedOption >= 0) {
      display.setTextDatum(textdatum_t::top_left);
      applyCoachContentFont();
      drawWrappedText(item.explanation, 34, 682, display.width() - 68, lineHeight, rubricLines);
    }
    display.display();
    return;
  }

  drawWrappedText(coachPromptFor(item), 34, 132, display.width() - 68, lineHeight, promptLines);

  if (gCoachStage >= 1) {
    display.drawLine(34, 430, display.width() - 34, 430, TFT_BLACK);
    drawWrappedText(coachAnswerFor(item), 34, 456, display.width() - 68, lineHeight, answerLines);
  }

  if (gCoachStage >= 2) {
    display.drawLine(34, 674, display.width() - 34, 674, TFT_BLACK);
    drawWrappedText(coachRubricFor(item), 34, 700, display.width() - 68, lineHeight, rubricLines);
  }

  display.display();
  Serial.printf("%s shown: type=%s index=%u stage=%u source=%s\n", coachScreenTitle(gScreen), coachTypeName(item.type),
                static_cast<unsigned>(gCoachIndex), gCoachStage, gCoachDeckLoadedFromSd ? "SD" : "embedded");
}

void renderHome(const char* refreshReason = "mode switch") {
  gScreen = Screen::Home;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  display.setFont(&fonts::Font4);
  display.setTextSize(1);
  display.drawString("PaperBadge", 32, 34);
  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString("Home", 34, 92);

  const int32_t width = display.width();
  const int32_t buttonX = 34;
  const int32_t buttonW = width - 68;
  const int32_t buttonH = 62;
  const int32_t gap = 8;
  int32_t y = 124;
  gBadgeButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gInterviewButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gBlitzButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gWeakAnswerButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gMetricPrecisionButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gHostileFollowupButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gGlossaryButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gMockInterviewButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gSettingsButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDebugButton = {buttonX, y, buttonW, buttonH};

  drawButton(gBadgeButton, "Badge");
  drawButton(gInterviewButton, "Interview Practice");
  drawButton(gBlitzButton, "Blitz Quiz");
  drawButton(gWeakAnswerButton, "Weak Answer Detector");
  drawButton(gMetricPrecisionButton, "Metric Precision");
  drawButton(gHostileFollowupButton, "Hostile Follow-up");
  drawButton(gGlossaryButton, "Glossary");
  drawButton(gMockInterviewButton, "Mock Interview");
  drawButton(gSettingsButton, "Settings");
  drawButton(gDebugButton, "Debug");

  display.display();
  Serial.println("Home/Menu mode: normal orientation.");
}

void renderSettings(const char* refreshReason = "mode switch") {
  gScreen = Screen::Settings;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  display.setFont(&fonts::Font4);
  display.setTextSize(1);
  display.drawString("Settings", 32, 34);
  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString("Badge orientation", 36, 98);

  const int32_t width = display.width();
  const int32_t halfW = (width - 88) / 2;
  gOrientationButton = {36, 136, width - 72, 66};
  gLanguageAutoButton = {36, 284, width - 72, 58};
  gLanguageEnglishButton = {36, 352, width - 72, 58};
  gLanguageJapaneseButton = {36, 420, width - 72, 58};
  gFontMediumButton = {36, 590, halfW, 58};
  gFontLargeButton = {52 + halfW, 590, halfW, 58};
  gFontXlButton = {36, 660, halfW, 58};
  gFontHugeButton = {52 + halfW, 660, halfW, 58};
  gHomeButton = {36, display.height() - 104, 178, 70};

  drawButton(gOrientationButton, gSettings.orientationMode == OrientationMode::Strap ? "Strap 180" : "Handheld 0");
  display.setTextDatum(textdatum_t::top_left);
  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString("Language mode", 36, 238);
  drawButton(gLanguageAutoButton, gSettings.languageMode == LanguageMode::Auto ? "Auto *" : "Auto");
  drawButton(gLanguageEnglishButton, gSettings.languageMode == LanguageMode::English ? "English *" : "English");
  drawButton(gLanguageJapaneseButton, gSettings.languageMode == LanguageMode::Japanese ? "Japanese *" : "Japanese");
  display.setTextDatum(textdatum_t::top_left);
  display.setFont(&fonts::Font2);
  display.setTextSize(2);
  display.drawString("PaperCoach font", 36, 542);
  drawButton(gFontMediumButton, gSettings.fontSizeMode == FontSizeMode::Medium ? "Medium *" : "Medium");
  drawButton(gFontLargeButton, gSettings.fontSizeMode == FontSizeMode::Large ? "Large *" : "Large");
  drawButton(gFontXlButton, gSettings.fontSizeMode == FontSizeMode::XL ? "XL *" : "XL");
  drawButton(gFontHugeButton, gSettings.fontSizeMode == FontSizeMode::Huge ? "Huge *" : "Huge");
  drawButton(gHomeButton, "Home");

  display.display();
  Serial.println("Settings screen shown.");
}

void renderDebug(const char* refreshReason = "mode switch") {
  gScreen = Screen::Debug;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  display.setFont(&fonts::Font2);
  display.setTextSize(1);

  int32_t y = 28;
  display.drawString("PaperBadge Debug", 26, y);
  y += 34;
  display.drawString(String("firmware: ") + kFirmwareVersion, 26, y);
  y += 26;
  display.drawString(String("SD mounted: ") + (gSdMounted ? "yes" : "no"), 26, y);
  y += 26;
  display.drawString(String("badge.json: ") + (gBadgeJsonLoaded ? "loaded" : "missing/fail"), 26, y);
  y += 26;
  display.drawString(String("EN SD: ") + (gBadgeEnSd.found ? gBadgeEnSd.path : "missing"), 26, y);
  y += 26;
  display.drawString(String("JA SD: ") + (gBadgeJaSd.found ? gBadgeJaSd.path : "missing"), 26, y);
  y += 26;
  display.drawString(String("profile: ") + (gProfileSd.found ? gProfileSd.path : "embedded"), 26, y);
  y += 26;
  display.drawString(String("QR: ") + (gQrSd.found ? gQrSd.path : "embedded"), 26, y);
  y += 26;
  display.drawString(String("coach deck: ") + gCoachDeckSource + " " + static_cast<unsigned>(gCoachItemCount), 26, y);
  y += 26;
  display.drawString(String("badge language: ") + languageName(gBadgeLanguage), 26, y);
  y += 26;
  display.drawString(String("language mode: ") + languageModeName(), 26, y);
  y += 26;
  display.drawString(String("font size: ") + fontSizeModeName(), 26, y);
  y += 26;
  display.drawString(String("orientation: ") + orientationModeName(), 26, y);
  y += 26;
  display.drawString(String("source: ") + gLastBadgeSource, 26, y);
  y += 26;
  const int16_t batteryMv = M5.Power.getBatteryVoltage();
  display.drawString(batteryMv > 0 ? String("battery: ") + batteryMv + " mV" : "battery: unknown", 26, y);
  y += 26;
  display.drawString(String("embedded bytes: ") + static_cast<unsigned>(embedded_assets::kEmbeddedPngTotalSize), 26, y);
  y += 30;
  display.drawString(String("touch debug: ") + (gTouchDebugEnabled ? "on" : "off"), 26, y);
  y += 26;
  display.drawString(String("touch down: ") + gLastTouchDownX + "," + gLastTouchDownY, 26, y);
  y += 26;
  display.drawString(String("touch up: ") + gLastTouchUpX + "," + gLastTouchUpY, 26, y);

  gTouchDebugButton = {26, display.height() - 178, display.width() - 52, 58};
  gHomeButton = {26, display.height() - 100, display.width() - 52, 58};
  drawButton(gTouchDebugButton, gTouchDebugEnabled ? "Touch debug off" : "Touch debug on");
  drawButton(gHomeButton, "Home");

  display.display();
  Serial.println("Debug screen shown.");
}

void renderQrZoom() {
  gScreen = Screen::QrZoom;
  applyBadgeRotation();
  prepareFullRefresh("zoom enter", true);

  auto& display = M5.Display;
  const int32_t margin = 46;
  const Rect rect = {margin, (display.height() - (display.width() - margin * 2)) / 2, display.width() - margin * 2,
                     display.width() - margin * 2};

  bool drawn = drawSdImageFit(gQrSd, rect, "QR zoom SD");
  if (!drawn) {
    drawn = drawEmbeddedPngFit(embedded_assets::kQrPng, embedded_assets::kQrPngSize, embedded_assets::kQrWidth,
                               embedded_assets::kQrHeight, rect, "QR zoom embedded");
  }
  if (!drawn) {
    drawMinimalFallbackText();
  }

  display.display();
  Serial.println("QR zoom shown.");
}

void renderPhotoZoom() {
  gScreen = Screen::PhotoZoom;
  applyBadgeRotation();
  prepareFullRefresh("zoom enter", true);

  auto& display = M5.Display;
  const Rect rect = {26, 26, display.width() - 52, display.height() - 52};

  bool drawn = drawSdImageFit(gProfileSd, rect, "photo zoom SD");
  if (!drawn) {
    drawn = drawEmbeddedPngFit(embedded_assets::kProfilePng, embedded_assets::kProfilePngSize,
                               embedded_assets::kProfileWidth, embedded_assets::kProfileHeight, rect,
                               "photo zoom embedded");
  }
  if (!drawn) {
    drawMinimalFallbackText();
  }

  display.display();
  Serial.println("Photo zoom shown.");
}

bool clickedPoint(const m5::touch_detail_t& detail, int32_t& x, int32_t& y) {
  if (!detail.wasClicked()) {
    return false;
  }

  x = constrain(detail.x, 0, M5.Display.width());
  y = constrain(detail.y, 0, M5.Display.height());
  return true;
}

void handleTouch() {
  if (!M5.Touch.isEnabled()) {
    return;
  }

  const auto detail = M5.Touch.getDetail();
  if (detail.wasPressed()) {
    gLastTouchDownX = constrain(detail.x, 0, M5.Display.width());
    gLastTouchDownY = constrain(detail.y, 0, M5.Display.height());
    Serial.printf("touch down coordinates: x=%ld y=%ld screen=%s\n", static_cast<long>(gLastTouchDownX),
                  static_cast<long>(gLastTouchDownY), screenName(gScreen));
  }
  if (detail.wasClicked() || detail.wasReleased()) {
    gLastTouchUpX = constrain(detail.x, 0, M5.Display.width());
    gLastTouchUpY = constrain(detail.y, 0, M5.Display.height());
    Serial.printf("touch up coordinates: x=%ld y=%ld screen=%s\n", static_cast<long>(gLastTouchUpX),
                  static_cast<long>(gLastTouchUpY), screenName(gScreen));
  }
  if (gScreen == Screen::Badge && detail.wasHold() && detail.x > M5.Display.width() / 4 &&
      detail.x < (M5.Display.width() * 3) / 4 && detail.y > M5.Display.height() / 4 &&
      detail.y < (M5.Display.height() * 3) / 4) {
    Serial.printf("long press detected: x=%ld y=%ld\n", static_cast<long>(detail.x), static_cast<long>(detail.y));
    Serial.println("entering Home");
    renderHome();
    return;
  }

  int32_t tapX = 0;
  int32_t tapY = 0;
  if (!clickedPoint(detail, tapX, tapY)) {
    return;
  }

  if (gScreen == Screen::Badge) {
    if (recordBottomLeftTripleTap(tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (gQrRect.contains(tapX, tapY)) {
      renderQrZoom();
    } else if (gPhotoRect.contains(tapX, tapY)) {
      renderPhotoZoom();
    }
    return;
  }

  if (gScreen == Screen::QrZoom || gScreen == Screen::PhotoZoom) {
    cleanWhiteRefresh("zoom exit");
    renderBadge(false);
    return;
  }

  if (gScreen == Screen::Debug) {
    if (gTouchDebugButton.contains(tapX, tapY)) {
      gTouchDebugEnabled = !gTouchDebugEnabled;
      renderDebug();
    } else if (gHomeButton.contains(tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (gTouchDebugEnabled) {
      renderDebug();
    }
    return;
  }

  if (isCoachScreen(gScreen)) {
    if (gHomeButton.contains(tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (gNextButton.contains(tapX, tapY)) {
      nextCoachItem();
      renderCoachScreen();
    } else if (gScreen == Screen::BlitzQuiz && gSelectedOption < 0) {
      const CoachItemView item = coachItemAt(gCoachIndex);
      for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
        if (gOptionButtons[option].contains(tapX, tapY)) {
          gSelectedOption = option;
          gCoachStage = 1;
          renderCoachScreen();
          break;
        }
      }
    } else {
      if (gCoachStage < 2) {
        ++gCoachStage;
      }
      renderCoachScreen();
    }
    return;
  }

  if (gScreen == Screen::Settings) {
    if (gOrientationButton.contains(tapX, tapY)) {
      gSettings.orientationMode =
          gSettings.orientationMode == OrientationMode::Strap ? OrientationMode::Handheld : OrientationMode::Strap;
      saveSettings();
      renderSettings("orientation switch");
    } else if (gLanguageAutoButton.contains(tapX, tapY)) {
      gSettings.languageMode = LanguageMode::Auto;
      saveSettings();
      renderSettings();
    } else if (gLanguageEnglishButton.contains(tapX, tapY)) {
      gSettings.languageMode = LanguageMode::English;
      gBadgeLanguage = BadgeLanguage::English;
      saveSettings();
      renderSettings();
    } else if (gLanguageJapaneseButton.contains(tapX, tapY)) {
      gSettings.languageMode = LanguageMode::Japanese;
      gBadgeLanguage = BadgeLanguage::Japanese;
      saveSettings();
      renderSettings();
    } else if (gFontMediumButton.contains(tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::Medium;
      saveSettings();
      renderSettings();
    } else if (gFontLargeButton.contains(tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::Large;
      saveSettings();
      renderSettings();
    } else if (gFontXlButton.contains(tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::XL;
      saveSettings();
      renderSettings();
    } else if (gFontHugeButton.contains(tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::Huge;
      saveSettings();
      renderSettings();
    } else if (gHomeButton.contains(tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    return;
  }

  if (gScreen == Screen::Home) {
    if (gBadgeButton.contains(tapX, tapY)) {
      Serial.println("returning to Badge");
      renderBadge(true, "mode switch");
    } else if (gInterviewButton.contains(tapX, tapY)) {
      startCoachMode(Screen::InterviewPractice);
      renderCoachScreen();
    } else if (gBlitzButton.contains(tapX, tapY)) {
      startCoachMode(Screen::BlitzQuiz);
      renderCoachScreen();
    } else if (gWeakAnswerButton.contains(tapX, tapY)) {
      startCoachMode(Screen::WeakAnswerDetector);
      renderCoachScreen();
    } else if (gMetricPrecisionButton.contains(tapX, tapY)) {
      startCoachMode(Screen::MetricPrecision);
      renderCoachScreen();
    } else if (gHostileFollowupButton.contains(tapX, tapY)) {
      startCoachMode(Screen::HostileFollowup);
      renderCoachScreen();
    } else if (gGlossaryButton.contains(tapX, tapY)) {
      startCoachMode(Screen::Glossary);
      renderCoachScreen();
    } else if (gMockInterviewButton.contains(tapX, tapY)) {
      startCoachMode(Screen::MockInterview);
      renderCoachScreen();
    } else if (gSettingsButton.contains(tapX, tapY)) {
      renderSettings();
    } else if (gDebugButton.contains(tapX, tapY)) {
      renderDebug();
    }
  }
}

void maybeSwitchBadgeLanguage() {
  if (gScreen != Screen::Badge) {
    return;
  }

  if (gSettings.languageMode != LanguageMode::Auto) {
    return;
  }

  const uint32_t intervalMs = gBadgeConfig.intervalSeconds * 1000UL;
  if (intervalMs == 0 || millis() - gLastLanguageSwitchMs < intervalMs) {
    return;
  }

  gBadgeLanguage = gBadgeLanguage == BadgeLanguage::English ? BadgeLanguage::Japanese : BadgeLanguage::English;
  Serial.printf("Badge language timer: switching to %s\n", languageName(gBadgeLanguage));
  renderBadge(false);
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
  captureNormalPortraitRotation();
  loadSettings();

  Serial.println();
  Serial.printf("PaperBadge+ %s boot\n", kFirmwareVersion);
  Serial.printf("M5 board id: %d\n", static_cast<int>(M5.getBoard()));
  Serial.printf("Display at boot: %dx%d rotation=%u normalPortrait=%u\n", M5.Display.width(), M5.Display.height(),
                M5.Display.getRotation() & 3, gNormalPortraitRotation);
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
  Serial.printf("embedded EN yes: %s %ux%u %u bytes\n", embedded_assets::kBadgeEnGenerated,
                embedded_assets::kBadgeEnWidth, embedded_assets::kBadgeEnHeight,
                static_cast<unsigned>(embedded_assets::kBadgeEnPngSize));
  Serial.printf("embedded JA yes: %s %ux%u %u bytes\n", embedded_assets::kBadgeJaGenerated,
                embedded_assets::kBadgeJaWidth, embedded_assets::kBadgeJaHeight,
                static_cast<unsigned>(embedded_assets::kBadgeJaPngSize));
  Serial.printf("embedded profile yes: %s %ux%u %u bytes\n", embedded_assets::kProfileGenerated,
                embedded_assets::kProfileWidth, embedded_assets::kProfileHeight,
                static_cast<unsigned>(embedded_assets::kProfilePngSize));
  Serial.printf("embedded QR yes: %s %ux%u %u bytes\n", embedded_assets::kQrGenerated, embedded_assets::kQrWidth,
                embedded_assets::kQrHeight, static_cast<unsigned>(embedded_assets::kQrPngSize));

  bootBeep();

  gSdMounted = mountSdCard();
  Serial.printf("SD mounted %s.\n", gSdMounted ? "yes" : "no");
  gBadgeJsonLoaded = loadBadgeJson();
  findSdImage("badge EN", kBadgeEnCandidates, countOf(kBadgeEnCandidates), gBadgeEnSd);
  findSdImage("badge JA", kBadgeJaCandidates, countOf(kBadgeJaCandidates), gBadgeJaSd);
  findSdImage("profile", kProfileCandidates, countOf(kProfileCandidates), gProfileSd);
  findSdImage("QR", kQrCandidates, countOf(kQrCandidates), gQrSd);
  loadCoachDeck();
  Serial.println("Badge mode defaults to strap 180 orientation unless Settings override is handheld.");

  renderBadge();
}

void loop() {
  M5.update();
  handleTouch();
  maybeSwitchBadgeLanguage();
  delay(100);
}
