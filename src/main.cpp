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
constexpr const char* kFirmwareVersion = "v2.1";
constexpr const char* kBadgeJsonPath = "/paperbadge/badge.json";
constexpr const char* kCoachDeckPath = "/papercoach/decks/interview_cards.json";
constexpr const char* kLegacyCoachDeckPath = "/papercoach/decks/sample_interview.json";
constexpr const char* kPrefsNamespace = "paperbadge";
constexpr uint32_t kDefaultLanguageIntervalSeconds = 15;
constexpr uint32_t kInputDebounceMs = 450;
constexpr uint32_t kInputCleanRefreshDebounceMs = 850;
constexpr size_t kMaxCoachItems = 96;
constexpr uint8_t kMaxOptions = 4;
constexpr uint8_t kMaxWrappedLines = 18;
constexpr int32_t kCoachMargin = 34;
constexpr int32_t kCoachHeaderBottom = 132;

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

enum class RefreshMode : uint8_t {
  Normal = 0,
  Clean = 1,
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
  FontSizeMode fontSizeMode = FontSizeMode::XL;
  RefreshMode refreshMode = RefreshMode::Normal;
};

struct CoachTypography {
  uint8_t titlePx = 32;
  uint8_t metadataPx = 20;
  uint8_t bodyPx = 32;
  uint8_t buttonPx = 28;
  uint8_t footerPx = 20;
  int32_t bodyLineHeight = 46;
  int32_t metadataLineHeight = 26;
  int32_t footerLineHeight = 26;
  int32_t buttonHeight = 88;
  uint8_t promptLines = 7;
  uint8_t answerLines = 5;
  uint8_t rubricLines = 3;
  size_t charsPerPage = 320;
};

struct TextLayoutResult {
  uint8_t lineCount = 0;
  int32_t height = 0;
  bool overflow = false;
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
Rect gRefreshModeButton;
Rect gFontMediumButton;
Rect gFontLargeButton;
Rect gFontXlButton;
Rect gFontHugeButton;
Rect gHomeButton;
Rect gNextButton;
Rect gFilterButton;
Rect gTouchDebugButton;
Rect gLayoutDebugButton;
Rect gOptionButtons[kMaxOptions];
bool gSdMounted = false;
bool gBadgeJsonLoaded = false;
bool gCoachDeckLoadedFromSd = false;
size_t gCoachMustMasterCount = 0;
size_t gCoachCardCount = 0;
size_t gCoachDrillCount = 0;
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
bool gInterviewMustMasterOnly = false;
bool gInputLocked = false;
bool gCurrentRefreshClean = false;
uint32_t gInputUnlockAtMs = 0;
int32_t gLastTouchDownX = -1;
int32_t gLastTouchDownY = -1;
int32_t gLastTouchUpX = -1;
int32_t gLastTouchUpY = -1;

const char* screenName(Screen screen);
const char* fontSizeModeName();
const char* refreshModeName();
void applyCoachButtonFont();
CoachTypography coachTypography();
TextLayoutResult wrapTextToLines(const String& text, int32_t width, int32_t lineHeight, uint8_t maxLines,
                                 String* lines);
void logLayoutBox(const char* field, const Rect& box, int32_t usedHeight, uint8_t pageCount, bool overflow);
int32_t coachFooterTop();
void logCurrentLayoutDiagnostics(const char* reason);

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

const char* refreshModeName() {
  return gSettings.refreshMode == RefreshMode::Clean ? "Clean" : "Normal";
}

void loadSettings() {
  gPrefs.begin(kPrefsNamespace, true);
  const uint8_t orientation = gPrefs.getUChar("orient", static_cast<uint8_t>(OrientationMode::Strap));
  const uint8_t language = gPrefs.getUChar("lang", static_cast<uint8_t>(LanguageMode::Auto));
  const uint8_t fontSize = gPrefs.getUChar("font", static_cast<uint8_t>(FontSizeMode::XL));
  const uint8_t refreshMode = gPrefs.getUChar("refresh", static_cast<uint8_t>(RefreshMode::Normal));
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

  gSettings.refreshMode =
      refreshMode == static_cast<uint8_t>(RefreshMode::Clean) ? RefreshMode::Clean : RefreshMode::Normal;

  Serial.printf("Settings loaded: orientation=%s language=%s font=%s refresh=%s\n", orientationModeName(),
                languageModeName(), fontSizeModeName(), refreshModeName());
}

void saveSettings() {
  gPrefs.begin(kPrefsNamespace, false);
  gPrefs.putUChar("orient", static_cast<uint8_t>(gSettings.orientationMode));
  gPrefs.putUChar("lang", static_cast<uint8_t>(gSettings.languageMode));
  gPrefs.putUChar("font", static_cast<uint8_t>(gSettings.fontSizeMode));
  gPrefs.putUChar("refresh", static_cast<uint8_t>(gSettings.refreshMode));
  gPrefs.end();
  Serial.printf("Settings saved: orientation=%s language=%s font=%s refresh=%s\n", orientationModeName(),
                languageModeName(), fontSizeModeName(), refreshModeName());
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
  gCoachCardCount = 0;
  gCoachDrillCount = 0;
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
  gCoachCardCount = embedded_papercoach::kCardCount;
  gCoachDrillCount = embedded_papercoach::kDrillCount;
  gCoachItemCount = gCoachCardCount + gCoachDrillCount;
  gCoachMustMasterCount = embedded_papercoach::kMustMasterCount;
  gCoachDeckLoadedFromSd = false;
  gCoachDeckSource = "embedded";
  Serial.println("PaperCoach deck source: embedded");
  Serial.printf("PaperCoach card count: %u\n", static_cast<unsigned>(gCoachCardCount));
  Serial.printf("PaperCoach drill count: %u\n", static_cast<unsigned>(gCoachDrillCount));
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
  gCoachCardCount = gCoachItemCount;
  gCoachDrillCount = 0;
  Serial.printf("PaperCoach deck source: SD (%s)\n", path);
  Serial.printf("PaperCoach card count: %u\n", static_cast<unsigned>(gCoachItemCount));
  Serial.printf("PaperCoach drill count: %u\n", static_cast<unsigned>(gCoachDrillCount));
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
    return view;
  }

  const size_t drillIndex = index - embedded_papercoach::kCardCount;
  if (drillIndex < embedded_papercoach::kDrillCount) {
    const auto& drill = embedded_papercoach::kDrills[drillIndex];
    view.type = parseCoachType(drill.type);
    view.id = drill.id;
    view.sectionId = "D";
    view.section = "PaperCoach Drills";
    view.number = "";
    view.title = drill.prompt;
    view.theme = drill.type;
    view.prompt = drill.prompt;
    view.answer = drill.explanation;
    view.rubric = drill.explanation;
    view.explanation = drill.explanation;
    view.category = drill.optionCount > 0 && drill.correctIndex < drill.optionCount ? drill.options[drill.correctIndex] : "";
    view.optionCount = drill.optionCount;
    view.correctIndex = drill.correctIndex;
    for (uint8_t option = 0; option < kMaxOptions; ++option) {
      view.options[option] = drill.options[option];
    }
  }
  return view;
}

bool itemMatchesScreen(const CoachItemView& item, Screen screen) {
  if (screen == Screen::InterviewPractice) {
    if (gInterviewMustMasterOnly && !item.mustMaster) {
      return false;
    }
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

void previousCoachItem() {
  if (gCoachItemCount == 0) {
    return;
  }
  for (size_t offset = 1; offset <= gCoachItemCount; ++offset) {
    const size_t index = (gCoachIndex + gCoachItemCount - offset) % gCoachItemCount;
    if (itemMatchesScreen(coachItemAt(index), gScreen)) {
      gCoachIndex = index;
      break;
    }
  }
  gCoachStage = 0;
  gSelectedOption = -1;
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

void lockInputForRefresh(const char* reason, bool cleanRefresh) {
  gInputLocked = true;
  gCurrentRefreshClean = cleanRefresh;
  gInputUnlockAtMs = 0;
  Serial.printf("UI refresh start: screen=%s font=%s settingRefresh=%s actualRefresh=%s input=locked reason=%s\n",
                screenName(gScreen), fontSizeModeName(), refreshModeName(), cleanRefresh ? "Clean" : "Normal",
                reason && reason[0] != '\0' ? reason : "render");
}

void finishDisplayRefresh() {
  M5.Display.display();
  const uint32_t debounceMs = gCurrentRefreshClean ? kInputCleanRefreshDebounceMs : kInputDebounceMs;
  gInputUnlockAtMs = millis() + debounceMs;
  Serial.printf("UI refresh displayed: screen=%s input=locked debounce=%u ms\n", screenName(gScreen),
                static_cast<unsigned>(debounceMs));
}

void updateInputLock() {
  if (!gInputLocked || gInputUnlockAtMs == 0 || millis() < gInputUnlockAtMs) {
    return;
  }
  gInputLocked = false;
  Serial.printf("UI input unlocked: screen=%s font=%s refresh=%s\n", screenName(gScreen), fontSizeModeName(),
                refreshModeName());
}

void prepareFullRefresh(const char* reason = nullptr, bool highQuality = false) {
  auto& display = M5.Display;
  if (reason && reason[0] != '\0') {
    Serial.printf("Full refresh: %s\n", reason);
  }
  const bool cleanRefresh = highQuality || gSettings.refreshMode == RefreshMode::Clean;
  lockInputForRefresh(reason, cleanRefresh);
  display.setEpdMode(cleanRefresh ? m5gfx::epd_quality : m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextWrap(false, false);
}

void cleanWhiteRefresh(const char* reason) {
  auto& display = M5.Display;
  Serial.printf("Full refresh: %s\n", reason);
  lockInputForRefresh(reason, true);
  display.setEpdMode(m5gfx::epd_quality);
  display.fillScreen(TFT_WHITE);
  finishDisplayRefresh();
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
  finishDisplayRefresh();
  gLastLanguageSwitchMs = millis();
  Serial.printf("Badge mode: language=%s source=%s orientation=%s\n", languageName(gBadgeLanguage),
                gLastBadgeSource.c_str(), orientationModeName());
}

void drawButton(const Rect& rect, const char* label) {
  auto& display = M5.Display;
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  applyCoachButtonFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);

  const int32_t innerX = rect.x + 12;
  const int32_t innerW = rect.w - 24;
  const int32_t lineHeight = static_cast<int32_t>(coachTypography().buttonPx) + 8;
  String lines[2];
  TextLayoutResult result = wrapTextToLines(label ? String(label) : String(""), innerW, lineHeight, 2, lines);
  const int32_t centeredOffset = (rect.h - result.height) / 2;
  const int32_t textY = rect.y + (centeredOffset > 6 ? centeredOffset : 6);
  for (uint8_t line = 0; line < result.lineCount; ++line) {
    const int32_t textW = display.textWidth(lines[line]);
    display.drawString(lines[line], rect.x + (rect.w - textW) / 2, textY + line * lineHeight);
  }
  logLayoutBox("button", Rect(innerX, rect.y + 4, innerW, rect.h - 8), result.height, 1, result.overflow);
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

CoachTypography coachTypography() {
  CoachTypography type;
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      type.bodyPx = 24;
      type.buttonPx = 24;
      type.bodyLineHeight = 34;
      type.buttonHeight = 76;
      type.promptLines = 9;
      type.answerLines = 7;
      type.rubricLines = 5;
      type.charsPerPage = 560;
      return type;
    case FontSizeMode::Huge:
      type.bodyPx = 36;
      type.buttonPx = 32;
      type.bodyLineHeight = 54;
      type.buttonHeight = 100;
      type.promptLines = 5;
      type.answerLines = 4;
      type.rubricLines = 3;
      type.charsPerPage = 230;
      return type;
    case FontSizeMode::XL:
      type.bodyPx = 32;
      type.buttonPx = 28;
      type.bodyLineHeight = 46;
      type.buttonHeight = 90;
      type.promptLines = 6;
      type.answerLines = 5;
      type.rubricLines = 3;
      type.charsPerPage = 320;
      return type;
    case FontSizeMode::Large:
    default:
      type.bodyPx = 28;
      type.buttonPx = 24;
      type.bodyLineHeight = 40;
      type.buttonHeight = 82;
      type.promptLines = 8;
      type.answerLines = 6;
      type.rubricLines = 4;
      type.charsPerPage = 430;
      return type;
  }
}

void applyGothicFont(uint8_t px) {
  auto& display = M5.Display;
  if (px >= 36) {
    display.setFont(&fonts::lgfxJapanGothic_36);
  } else if (px >= 32) {
    display.setFont(&fonts::lgfxJapanGothic_32);
  } else if (px >= 28) {
    display.setFont(&fonts::lgfxJapanGothic_28);
  } else if (px >= 24) {
    display.setFont(&fonts::lgfxJapanGothic_24);
  } else {
    display.setFont(&fonts::lgfxJapanGothic_20);
  }
  display.setTextSize(1);
}

void applyCoachTitleFont() {
  applyGothicFont(coachTypography().titlePx);
}

void applyCoachMetadataFont() {
  applyGothicFont(coachTypography().metadataPx);
}

void applyCoachContentFont() {
  applyGothicFont(coachTypography().bodyPx);
}

void applyCoachButtonFont() {
  applyGothicFont(coachTypography().buttonPx);
}

void applyCoachFooterFont() {
  applyGothicFont(coachTypography().footerPx);
}

int32_t coachLineHeight() {
  return coachTypography().bodyLineHeight;
}

uint8_t coachPromptLineCount() {
  return coachTypography().promptLines;
}

uint8_t coachAnswerLineCount() {
  return coachTypography().answerLines;
}

uint8_t coachRubricLineCount() {
  return coachTypography().rubricLines;
}

uint8_t linesThatFit(int32_t availableHeight, int32_t lineHeight, uint8_t minLines = 1,
                     uint8_t maxLines = kMaxWrappedLines) {
  if (lineHeight <= 0) {
    return minLines;
  }
  int32_t lines = availableHeight / lineHeight;
  if (lines < minLines) {
    lines = minLines;
  }
  if (lines > maxLines) {
    lines = maxLines;
  }
  return static_cast<uint8_t>(lines);
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

bool appendWrappedLine(String* lines, uint8_t& lineCount, uint8_t maxLines, const String& line) {
  if (line.length() == 0) {
    return false;
  }
  if (lineCount >= maxLines) {
    return true;
  }
  lines[lineCount++] = line;
  return false;
}

bool appendWrappedWord(String* lines, uint8_t& lineCount, uint8_t maxLines, const String& word, int32_t width) {
  auto& display = M5.Display;
  if (display.textWidth(word) <= width) {
    return appendWrappedLine(lines, lineCount, maxLines, word);
  }

  size_t start = 0;
  while (start < word.length()) {
    size_t end = start + 1;
    while (end <= word.length() && display.textWidth(word.substring(start, end)) <= width) {
      ++end;
    }
    if (end > start + 1) {
      --end;
    }
    if (appendWrappedLine(lines, lineCount, maxLines, word.substring(start, end))) {
      return true;
    }
    start = end;
  }
  return false;
}

TextLayoutResult wrapTextToLines(const String& text, int32_t width, int32_t lineHeight, uint8_t maxLines,
                                 String* lines) {
  auto& display = M5.Display;
  TextLayoutResult result;
  String line;
  String word;

  for (size_t index = 0; index <= text.length(); ++index) {
    const char ch = index < text.length() ? text[index] : ' ';
    const bool isBreak = ch == ' ' || ch == '\n' || ch == '\t';
    if (!isBreak) {
      word += ch;
      continue;
    }

    if (word.length() > 0) {
      const String candidate = line.length() == 0 ? word : line + " " + word;
      if (line.length() > 0 && display.textWidth(candidate) > width) {
        result.overflow = appendWrappedLine(lines, result.lineCount, maxLines, line) || result.overflow;
        line = "";
        if (display.textWidth(word) > width) {
          result.overflow = appendWrappedWord(lines, result.lineCount, maxLines, word, width) || result.overflow;
        } else {
          line = word;
        }
      } else if (line.length() == 0 && display.textWidth(word) > width) {
        result.overflow = appendWrappedWord(lines, result.lineCount, maxLines, word, width) || result.overflow;
      } else {
        line = candidate;
      }
      word = "";
    }

    if (ch == '\n') {
      result.overflow = appendWrappedLine(lines, result.lineCount, maxLines, line) || result.overflow;
      line = "";
    }
  }

  result.overflow = appendWrappedLine(lines, result.lineCount, maxLines, line) || result.overflow;
  result.height = static_cast<int32_t>(result.lineCount) * lineHeight;
  return result;
}

void logLayoutBox(const char* field, const Rect& box, int32_t usedHeight, uint8_t pageCount, bool overflow) {
  Serial.printf("Layout box: screen=%s field=%s font=%s bodyPx=%u box=%ld,%ld,%ld,%ld used=%ld available=%ld "
                "pageCount=%u overflow=%s\n",
                screenName(gScreen), field, fontSizeModeName(), coachTypography().bodyPx, static_cast<long>(box.x),
                static_cast<long>(box.y), static_cast<long>(box.w), static_cast<long>(box.h),
                static_cast<long>(usedHeight), static_cast<long>(box.h), pageCount, overflow ? "yes" : "no");
  if (overflow || usedHeight > box.h) {
    Serial.printf("Overflow warning: screen=%s field=%s computedHeight=%ld availableHeight=%ld\n", screenName(gScreen),
                  field, static_cast<long>(usedHeight), static_cast<long>(box.h));
  }
}

void logCurrentLayoutDiagnostics(const char* reason) {
  auto& display = M5.Display;
  const CoachTypography type = coachTypography();
  const int32_t footerTop = coachFooterTop();
  Serial.printf("Layout diagnostics: reason=%s screen=%s font=%s bodyPx=%u buttonPx=%u refresh=%s size=%dx%d\n",
                reason && reason[0] != '\0' ? reason : "manual", screenName(gScreen), fontSizeModeName(), type.bodyPx,
                type.buttonPx, refreshModeName(), display.width(), display.height());
  logLayoutBox("header", Rect(0, 0, display.width(), kCoachHeaderBottom), kCoachHeaderBottom, 1, false);
  logLayoutBox("content", Rect(kCoachMargin, kCoachHeaderBottom + 18, display.width() - kCoachMargin * 2,
                               footerTop - (kCoachHeaderBottom + 18) - 42),
               0, 1, false);
  logLayoutBox("footer", Rect(0, footerTop, display.width(), display.height() - footerTop), display.height() - footerTop,
               1, false);
  Serial.println("Screenshot-to-SD unavailable: using serial layout diagnostics for this build.");
}

TextLayoutResult drawWrappedText(const String& text, int32_t x, int32_t y, int32_t width, int32_t lineHeight,
                                 uint8_t maxLines, const char* field = nullptr, uint8_t pageCount = 1) {
  auto& display = M5.Display;
  String lines[kMaxWrappedLines];
  const uint8_t lineLimit = maxLines > kMaxWrappedLines ? kMaxWrappedLines : maxLines;
  TextLayoutResult result = wrapTextToLines(text, width, lineHeight, lineLimit, lines);
  for (uint8_t line = 0; line < result.lineCount; ++line) {
    display.drawString(lines[line], x, y + line * lineHeight);
  }
  if (field != nullptr && field[0] != '\0') {
    logLayoutBox(field, Rect(x, y, width, lineHeight * lineLimit), result.height, pageCount, result.overflow);
  }
  return result;
}

void drawCoachChrome(const char* title) {
  auto& display = M5.Display;
  const CoachTypography type = coachTypography();
  const uint16_t darkGray = display.color565(55, 55, 55);
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString(title, 28, 28);
  applyCoachMetadataFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString(gCoachDeckLoadedFromSd ? "SD deck" : "Embedded deck", 30, 82);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  gHomeButton = {28, display.height() - type.buttonHeight - 28, 164, type.buttonHeight};
  gNextButton = {display.width() - 192, display.height() - type.buttonHeight - 28, 164, type.buttonHeight};
  drawButton(gHomeButton, "Home");
  drawButton(gNextButton, "Next");
}

int32_t coachFooterTop() {
  return M5.Display.height() - coachTypography().buttonHeight - 56;
}

void drawCoachPageNumber(uint8_t currentPage, uint8_t pageCount) {
  auto& display = M5.Display;
  const uint16_t darkGray = display.color565(55, 55, 55);
  display.setTextDatum(textdatum_t::top_left);
  applyCoachFooterFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString(String("Page ") + currentPage + "/" + pageCount, kCoachMargin, coachFooterTop() - 34);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

String coachPromptFor(const CoachItemView& item) {
  if (item.type == CoachItemType::Glossary) {
    return item.term;
  }
  if (item.type == CoachItemType::WeakAnswer) {
    if (strlen(item.weakAnswer) == 0) {
      return item.prompt;
    }
    return String(item.prompt) + "\nWeak answer: " + item.weakAnswer;
  }
  return item.prompt;
}

String coachAnswerFor(const CoachItemView& item) {
  if (item.type == CoachItemType::Glossary) {
    return item.definition;
  }
  if (item.type == CoachItemType::WeakAnswer) {
    if (strlen(item.category) == 0) {
      return item.explanation;
    }
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

String coachOptionLabelFor(const CoachItemView& item, uint8_t option) {
  if (item.type == CoachItemType::MetricPrecision) {
    switch (option) {
      case 0:
        return "Define cohort + period";
      case 1:
        return "Claim clean causality";
      case 2:
        return "Use biggest number";
      case 3:
        return "Skip baseline";
      default:
        break;
    }
  }
  return option < item.optionCount ? String(item.options[option]) : String("");
}

int32_t wrappedButtonHeightFor(const String& label, int32_t buttonWidth) {
  applyCoachButtonFont();
  String lines[2];
  const int32_t lineHeight = static_cast<int32_t>(coachTypography().buttonPx) + 8;
  TextLayoutResult result = wrapTextToLines(label, buttonWidth - 24, lineHeight, 2, lines);
  const int32_t desiredHeight = result.height + 24;
  return desiredHeight > coachTypography().buttonHeight ? desiredHeight : coachTypography().buttonHeight;
}

size_t interviewCharsPerPage() {
  return coachTypography().charsPerPage;
}

uint8_t textPageCount(const char* rawText) {
  const String text = rawText ? String(rawText) : String("");
  if (text.length() == 0) {
    return 1;
  }
  const size_t charsPerPage = interviewCharsPerPage();
  return static_cast<uint8_t>((text.length() + charsPerPage - 1) / charsPerPage);
}

String textPageSlice(const char* rawText, uint8_t pageIndex) {
  const String text = rawText ? String(rawText) : String("");
  const size_t charsPerPage = interviewCharsPerPage();
  size_t start = static_cast<size_t>(pageIndex) * charsPerPage;
  if (start >= text.length()) {
    return "";
  }
  size_t end = start + charsPerPage;
  if (end >= text.length()) {
    return text.substring(start);
  }
  size_t breakAt = end;
  while (breakAt > start + charsPerPage / 2 && text[breakAt] != ' ' && text[breakAt] != '\n') {
    --breakAt;
  }
  if (breakAt <= start + charsPerPage / 2) {
    breakAt = end;
  }
  return text.substring(start, breakAt);
}

uint8_t interviewStageCount(const CoachItemView& item) {
  return textPageCount(item.spoken) + 3;
}

void renderInterviewPracticeScreen() {
  applyAppRotation();
  prepareFullRefresh();

  const CoachItemView item = coachItemAt(gCoachIndex);
  const uint8_t spokenPages = textPageCount(item.spoken);
  const uint8_t stageCount = interviewStageCount(item);
  if (gCoachStage >= stageCount) {
    gCoachStage = stageCount - 1;
  }

  auto& display = M5.Display;
  const CoachTypography type = coachTypography();
  const uint16_t darkGray = display.color565(55, 55, 55);
  const uint16_t lightGray = display.color565(170, 170, 170);

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Practice", 28, 26);
  applyCoachMetadataFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString(String(item.id) + "  " + item.section, 30, 76);
  display.drawString(String(gInterviewMustMasterOnly ? "Must-master" : "All cards") + "  " +
                         (gCoachDeckLoadedFromSd ? "SD" : "Embedded"),
                     30, 102);
  display.drawLine(28, 132, display.width() - 28, 132, lightGray);

  applyCoachContentFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  const int32_t lineHeight = coachLineHeight();
  const int32_t bodyY = 162;
  const int32_t bodyW = display.width() - 68;
  const int32_t footerTop = display.height() - type.buttonHeight - 56;
  const int32_t contentBottom = footerTop - 46;

  if (gCoachStage == 0) {
    display.setTextColor(darkGray, TFT_WHITE);
    display.drawString(String(item.theme) + (item.mustMaster ? "  *" : ""), 34, bodyY);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    const int32_t questionY = bodyY + lineHeight + 12;
    const uint8_t questionLines = linesThatFit(contentBottom - questionY - 90, lineHeight, 2);
    const TextLayoutResult questionLayout =
        drawWrappedText(item.title, 34, questionY, bodyW, lineHeight, questionLines, "question", stageCount);
    display.setTextColor(darkGray, TFT_WHITE);
    applyCoachMetadataFont();
    drawWrappedText(String("Confidence: ") + item.confidence, 34, questionY + questionLayout.height + 24, bodyW,
                    type.metadataLineHeight, 3, "confidence", stageCount);
  } else if (gCoachStage <= spokenPages) {
    const uint8_t spokenPage = gCoachStage - 1;
    display.setTextColor(darkGray, TFT_WHITE);
    display.drawString(String("Spoken answer ") + (spokenPage + 1) + "/" + spokenPages, 34, bodyY);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    applyCoachContentFont();
    drawWrappedText(textPageSlice(item.spoken, spokenPage), 34, bodyY + lineHeight + 10, bodyW, lineHeight,
                    linesThatFit(contentBottom - bodyY - lineHeight - 10, lineHeight, 2), "spoken", stageCount);
  } else if (gCoachStage == spokenPages + 1) {
    display.setTextColor(darkGray, TFT_WHITE);
    display.drawString("Anchor", 34, bodyY);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    applyCoachContentFont();
    drawWrappedText(item.anchor, 34, bodyY + lineHeight + 10, bodyW, lineHeight,
                    linesThatFit(contentBottom - bodyY - lineHeight - 10, lineHeight, 2), "anchor", stageCount);
  } else {
    display.setTextColor(darkGray, TFT_WHITE);
    display.drawString("Watch", 34, bodyY);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    applyCoachContentFont();
    drawWrappedText(item.watch, 34, bodyY + lineHeight + 10, bodyW, lineHeight,
                    linesThatFit(contentBottom - bodyY - lineHeight - 10, lineHeight, 2), "watch", stageCount);
  }

  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawLine(28, display.height() - type.buttonHeight - 56, display.width() - 28,
                   display.height() - type.buttonHeight - 56, lightGray);
  gHomeButton = {26, display.height() - type.buttonHeight - 28, 126, type.buttonHeight};
  gFilterButton = {168, display.height() - type.buttonHeight - 28, 158, type.buttonHeight};
  gNextButton = {display.width() - 196, display.height() - type.buttonHeight - 28, 170, type.buttonHeight};
  drawButton(gHomeButton, "Home");
  drawButton(gFilterButton, gInterviewMustMasterOnly ? "Must *" : "All");
  drawButton(gNextButton, "Next");

  display.setTextDatum(textdatum_t::top_left);
  applyCoachFooterFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString(String("Page ") + (gCoachStage + 1) + "/" + stageCount, 34,
                     display.height() - type.buttonHeight - 80);

  finishDisplayRefresh();
  Serial.printf("Interview Practice shown: card=%s stage=%u/%u filter=%s source=%s\n", item.id, gCoachStage + 1,
                stageCount, gInterviewMustMasterOnly ? "must" : "all", gCoachDeckSource.c_str());
}

void renderCoachScreen() {
  if (gScreen == Screen::InterviewPractice) {
    renderInterviewPracticeScreen();
    return;
  }

  applyAppRotation();
  prepareFullRefresh();
  drawCoachChrome(coachScreenTitle(gScreen));

  auto& display = M5.Display;
  const CoachTypography type = coachTypography();
  display.setTextDatum(textdatum_t::top_left);
  applyCoachContentFont();
  const int32_t lineHeight = coachLineHeight();
  const uint8_t promptLines = coachPromptLineCount();
  const uint8_t answerLines = coachAnswerLineCount();
  const uint8_t rubricLines = coachRubricLineCount();

  if (gCoachItemCount == 0) {
    drawWrappedText("No PaperCoach deck available.", 34, 140, display.width() - 68, lineHeight, promptLines);
    finishDisplayRefresh();
    return;
  }

  const CoachItemView item = coachItemAt(gCoachIndex);
  if (gScreen == Screen::MockInterview) {
    display.drawString(String("Prompt ") + (gMockStep + 1) + "/5", 34, 118);
  }

  const bool optionDrill = (gScreen == Screen::BlitzQuiz || gScreen == Screen::WeakAnswerDetector ||
                            gScreen == Screen::MetricPrecision) &&
                           item.optionCount > 0;
  if (optionDrill) {
    const uint8_t optionCount = item.optionCount < kMaxOptions ? item.optionCount : kMaxOptions;
    const int32_t contentX = kCoachMargin;
    const int32_t contentW = display.width() - kCoachMargin * 2;
    const int32_t contentY = kCoachHeaderBottom + 18;
    const int32_t footerTop = coachFooterTop();
    const int32_t contentH = footerTop - contentY - 42;

    if (gSelectedOption >= 0) {
      String result = String("Correct: ") + static_cast<char>('A' + item.correctIndex) + ". " +
                      coachOptionLabelFor(item, item.correctIndex);
      if (gSelectedOption != static_cast<int8_t>(item.correctIndex)) {
        result += "\nYou chose: ";
        result += static_cast<char>('A' + gSelectedOption);
        result += ". ";
        result += coachOptionLabelFor(item, static_cast<uint8_t>(gSelectedOption));
      }
      result += "\n";
      result += item.explanation;
      applyCoachContentFont();
      drawWrappedText(result, contentX, contentY, contentW, lineHeight, linesThatFit(contentH, lineHeight, 2),
                      "explanation", 2);
      drawCoachPageNumber(2, 2);
      finishDisplayRefresh();
      return;
    }

    String optionLabels[kMaxOptions];
    int32_t optionHeights[kMaxOptions] = {};
    int32_t optionTotal = 0;
    const int32_t optionGap = 12;
    for (uint8_t option = 0; option < optionCount; ++option) {
      optionLabels[option] = String(static_cast<char>('A' + option)) + ". " + coachOptionLabelFor(item, option);
      optionHeights[option] = wrappedButtonHeightFor(optionLabels[option], contentW);
      optionTotal += optionHeights[option];
    }
    if (optionCount > 1) {
      optionTotal += optionGap * (optionCount - 1);
    }

    const int32_t promptAvailable = contentH - optionTotal - 24;
    const uint8_t promptMaxLines = linesThatFit(promptAvailable, lineHeight, 2, promptLines < 5 ? promptLines : 5);
    applyCoachContentFont();
    const TextLayoutResult promptLayout =
        drawWrappedText(item.prompt, contentX, contentY, contentW, lineHeight, promptMaxLines, "prompt", 2);

    int32_t y = contentY + promptLayout.height + 22;
    const int32_t usedHeight = promptLayout.height + 22 + optionTotal;
    Serial.printf("Layout vertical budget: screen=%s header=%ld prompt=%ld options=%ld footer=%ld total=%ld "
                  "available=%ld pageCount=2\n",
                  screenName(gScreen), static_cast<long>(kCoachHeaderBottom), static_cast<long>(promptLayout.height),
                  static_cast<long>(optionTotal), static_cast<long>(display.height() - footerTop),
                  static_cast<long>(kCoachHeaderBottom + promptLayout.height + 22 + optionTotal +
                                    (display.height() - footerTop)),
                  static_cast<long>(display.height()));
    logLayoutBox("drill-vertical-budget", Rect(contentX, contentY, contentW, contentH), usedHeight, 2,
                 usedHeight > contentH);
    for (uint8_t option = 0; option < optionCount; ++option) {
      int32_t buttonH = optionHeights[option];
      if (y + buttonH > footerTop - 8) {
        buttonH = footerTop - 8 - y;
      }
      if (buttonH < 56) {
        logLayoutBox("option-clipped", Rect(contentX, y, contentW, buttonH), optionHeights[option], 2, true);
        break;
      }
      gOptionButtons[option] = {contentX, y, contentW, buttonH};
      drawButton(gOptionButtons[option], optionLabels[option]);
      y += buttonH + optionGap;
    }
    drawCoachPageNumber(1, 2);
    finishDisplayRefresh();
    return;
  }

  const bool hasRubric = coachRubricFor(item).length() > 0;
  const uint8_t pageCount = hasRubric ? 3 : 2;
  if (gCoachStage >= pageCount) {
    gCoachStage = pageCount - 1;
  }
  String body;
  const char* field = "prompt";
  if (gCoachStage == 0) {
    body = coachPromptFor(item);
    field = "prompt";
  } else if (gCoachStage == 1) {
    body = coachAnswerFor(item);
    field = item.type == CoachItemType::Glossary ? "definition" : "answer";
  } else {
    body = coachRubricFor(item);
    field = "rubric";
  }

  const int32_t contentX = kCoachMargin;
  const int32_t contentY = kCoachHeaderBottom + 18;
  const int32_t contentW = display.width() - kCoachMargin * 2;
  const int32_t contentH = coachFooterTop() - contentY - 42;
  drawWrappedText(body, contentX, contentY, contentW, lineHeight, linesThatFit(contentH, lineHeight, 2), field,
                  pageCount);
  drawCoachPageNumber(gCoachStage + 1, pageCount);

  finishDisplayRefresh();
  Serial.printf("%s shown: type=%s index=%u stage=%u source=%s\n", coachScreenTitle(gScreen), coachTypeName(item.type),
                static_cast<unsigned>(gCoachIndex), gCoachStage, gCoachDeckLoadedFromSd ? "SD" : "embedded");
}

void renderHome(const char* refreshReason = "mode switch") {
  gScreen = Screen::Home;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("PaperBadge", 32, 34);
  applyCoachMetadataFont();
  display.drawString("Home", 34, 92);

  const int32_t width = display.width();
  const int32_t buttonX = 34;
  const int32_t buttonW = width - 68;
  const int32_t buttonH = 70;
  const int32_t gap = 7;
  int32_t y = 126;
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

  finishDisplayRefresh();
  Serial.println("Home/Menu mode: normal orientation.");
}

void renderSettings(const char* refreshReason = "mode switch") {
  gScreen = Screen::Settings;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Settings", 32, 34);
  applyCoachMetadataFont();
  display.drawString("Badge orientation", 36, 98);

  const int32_t width = display.width();
  const int32_t halfW = (width - 88) / 2;
  gOrientationButton = {36, 136, width - 72, 66};
  gLanguageAutoButton = {36, 268, width - 72, 58};
  gLanguageEnglishButton = {36, 334, width - 72, 58};
  gLanguageJapaneseButton = {36, 400, width - 72, 58};
  gFontMediumButton = {36, 534, halfW, 58};
  gFontLargeButton = {52 + halfW, 534, halfW, 58};
  gFontXlButton = {36, 604, halfW, 58};
  gFontHugeButton = {52 + halfW, 604, halfW, 58};
  gRefreshModeButton = {36, 742, width - 72, 66};
  gHomeButton = {36, display.height() - 104, 178, 70};

  drawButton(gOrientationButton, gSettings.orientationMode == OrientationMode::Strap ? "Strap 180" : "Handheld 0");
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("Language mode", 36, 226);
  drawButton(gLanguageAutoButton, gSettings.languageMode == LanguageMode::Auto ? "Auto *" : "Auto");
  drawButton(gLanguageEnglishButton, gSettings.languageMode == LanguageMode::English ? "English *" : "English");
  drawButton(gLanguageJapaneseButton, gSettings.languageMode == LanguageMode::Japanese ? "Japanese *" : "Japanese");
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("PaperCoach font", 36, 494);
  drawButton(gFontMediumButton, gSettings.fontSizeMode == FontSizeMode::Medium ? "Medium *" : "Medium");
  drawButton(gFontLargeButton, gSettings.fontSizeMode == FontSizeMode::Large ? "Large *" : "Large");
  drawButton(gFontXlButton, gSettings.fontSizeMode == FontSizeMode::XL ? "XL *" : "XL");
  drawButton(gFontHugeButton, gSettings.fontSizeMode == FontSizeMode::Huge ? "Huge *" : "Huge");
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("Refresh mode", 36, 702);
  drawButton(gRefreshModeButton, gSettings.refreshMode == RefreshMode::Clean ? "Clean *" : "Normal *");
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  Serial.printf("Settings screen shown: font=%s refresh=%s\n", fontSizeModeName(), refreshModeName());
}

void renderDebug(const char* refreshReason = "mode switch") {
  gScreen = Screen::Debug;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);

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
  display.drawString(String("refresh mode: ") + refreshModeName(), 26, y);
  y += 26;
  display.drawString(String("input locked: ") + (gInputLocked ? "yes" : "no"), 26, y);
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

  gLayoutDebugButton = {26, display.height() - 246, display.width() - 52, 58};
  gTouchDebugButton = {26, display.height() - 178, display.width() - 52, 58};
  gHomeButton = {26, display.height() - 100, display.width() - 52, 58};
  drawButton(gLayoutDebugButton, "Layout log");
  drawButton(gTouchDebugButton, gTouchDebugEnabled ? "Touch debug off" : "Touch debug on");
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
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

  finishDisplayRefresh();
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

  finishDisplayRefresh();
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

void handleInterviewPracticeTouch(int32_t tapX, int32_t tapY) {
  if (gHomeButton.contains(tapX, tapY)) {
    Serial.println("entering Home");
    renderHome();
    return;
  }
  if (gFilterButton.contains(tapX, tapY)) {
    gInterviewMustMasterOnly = !gInterviewMustMasterOnly;
    startCoachMode(Screen::InterviewPractice);
    renderCoachScreen();
    return;
  }
  if (gNextButton.contains(tapX, tapY)) {
    nextCoachItem();
    renderCoachScreen();
    return;
  }

  const CoachItemView item = coachItemAt(gCoachIndex);
  const uint8_t stageCount = interviewStageCount(item);
  if (tapX < M5.Display.width() / 3) {
    if (gCoachStage > 0) {
      --gCoachStage;
    } else {
      previousCoachItem();
    }
    renderCoachScreen();
    return;
  }

  if (gCoachStage + 1 < stageCount) {
    ++gCoachStage;
  } else {
    nextCoachItem();
  }
  renderCoachScreen();
}

void handleTouch() {
  if (!M5.Touch.isEnabled()) {
    return;
  }

  updateInputLock();
  const auto detail = M5.Touch.getDetail();
  if (gInputLocked) {
    if (detail.wasPressed() || detail.wasReleased() || detail.wasClicked() || detail.wasHold()) {
      Serial.printf("touch ignored: input locked screen=%s font=%s refresh=%s\n", screenName(gScreen),
                    fontSizeModeName(), refreshModeName());
    }
    return;
  }

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
    if (gLayoutDebugButton.contains(tapX, tapY)) {
      logCurrentLayoutDiagnostics("debug button");
      renderDebug();
    } else if (gTouchDebugButton.contains(tapX, tapY)) {
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
    if (gScreen == Screen::InterviewPractice) {
      handleInterviewPracticeTouch(tapX, tapY);
      return;
    }
    if (gHomeButton.contains(tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (gNextButton.contains(tapX, tapY)) {
      nextCoachItem();
      renderCoachScreen();
    } else if ((gScreen == Screen::BlitzQuiz || gScreen == Screen::WeakAnswerDetector ||
                gScreen == Screen::MetricPrecision) &&
               gSelectedOption < 0) {
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
    } else if (gRefreshModeButton.contains(tapX, tapY)) {
      gSettings.refreshMode = gSettings.refreshMode == RefreshMode::Clean ? RefreshMode::Normal : RefreshMode::Clean;
      saveSettings();
      renderSettings("refresh mode switch");
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
