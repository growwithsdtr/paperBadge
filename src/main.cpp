#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <cstring>
#include <esp32-hal-bt.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <vector>

#include "embedded_assets.h"
#include "embedded_papercoach_deck.h"

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSdSpiHz = 25000000;
constexpr const char* kFirmwareVersion = "v4.9";
constexpr const char* kBadgeJsonPath = "/paperbadge/badge.json";
constexpr const char* kCoachDeckPath = "/papercoach/decks/interview_cards.json";
constexpr const char* kLegacyCoachDeckPath = "/papercoach/decks/sample_interview.json";
constexpr const char* kCoachGlossaryPath = "/papercoach/glossary.json";
constexpr const char* kPrefsNamespace = "paperbadge";
constexpr const char* kReaderVlwPath = "/paperbadge/fonts/reader.vlw";
constexpr const char* kReaderMidVlwPath = "/paperbadge/fonts/reader_mid.vlw";
constexpr const char* kRenderTracePath = "/papercoach/debug/render_trace.txt";
constexpr const char* kEmbeddedDeckDumpPath = "/papercoach/debug/embedded_deck_dump.md";
constexpr const char* kResultsPath = "/papercoach/progress/session_results.json";
constexpr const char* kResultsTempPath = "/papercoach/progress/session_results.tmp";
constexpr uint32_t kDefaultLanguageIntervalSeconds = 15;
constexpr uint32_t kInputDebounceMs = 250;
constexpr uint32_t kInputCleanRefreshDebounceMs = 600;
constexpr uint32_t kBatterySaverIdleMs = 180000;
constexpr uint32_t kConferenceBadgeIdleMs = 30000;
constexpr uint32_t kBadgeLightSleepIdleMs = 30000;
constexpr uint32_t kBadgeLightSleepDurationUs = 2000000;
constexpr uint32_t kPowerPollIntervalMs = 45000;
constexpr uint32_t kPostTouchIdleGuardMs = 5000;
constexpr uint32_t kHardCleanTransitionLimit = 14;
constexpr int32_t kHitboxPadding = 10;
constexpr size_t kMaxCoachItems = 180;
constexpr size_t kMaxSdGlossaryTerms = 56;
constexpr size_t kMaxSessionResults = 160;
constexpr uint8_t kMaxExamQuestions = 10;
constexpr size_t kMaxExamPool = 180;
constexpr uint8_t kMaxOptions = 4;
constexpr uint8_t kMaxWrappedLines = 18;
constexpr uint8_t kMaxReaderPageCount = 32;
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
  PowerAudit,
  FontLab,
  VisualQa,
  HelpLegend,
  QrZoom,
  PhotoZoom,
  PracticeMenu,
  InterviewPractice,
  DrillsMenu,
  Drills,
  Exam,
  Results,
  BlitzQuiz,
  WeakAnswerDetector,
  MetricPrecision,
  HostileFollowup,
  GlossaryMenu,
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
  Manual = 3,
};

enum class FontSizeMode : uint8_t {
  Medium = 0,
  Large = 1,
  XL = 2,
  XXL = 3,
  Huge = 4,
};

enum class RefreshMode : uint8_t {
  Fast = 0,
  Balanced = 1,
  Clean = 2,
};

enum class PowerMode : uint8_t {
  Normal = 0,
  BatterySaver = 1,
  ConferenceBadge = 2,
};

enum class BadgeSleepMode : uint8_t {
  Off = 0,
  Light = 1,
  DeepExperiment = 2,
};

enum class FontStyleMode : uint8_t {
  SansCurrent = 0,
  LargeReader = 1,
  SansBoldLike = 2,
  HighContrast = 3,
  DebugMono = 4,
};

enum class ContrastMode : uint8_t {
  Standard = 0,
  Dark = 1,
  Max = 2,
};

enum class LineSpacingMode : uint8_t {
  Tight = 0,
  Normal = 1,
  Loose = 2,
};

enum class AutoRotateIntervalMode : uint8_t {
  Off = 0,
  Sec15 = 1,
  Sec30 = 2,
  Sec60 = 3,
};

enum class IconType : uint8_t {
  None,
  Home,
  Back,
  Next,
  Badge,
  Practice,
  Drills,
  Exam,
  Glossary,
  Results,
  Settings,
  Debug,
};

enum class ButtonTextAlign : uint8_t {
  Center,
  Left,
};

enum class DrillCategory : uint8_t {
  All = 0,
  WeakAnswer = 1,
  MetricPrecision = 2,
  FollowupDefense = 3,
  FrameworkChoice = 4,
  MaturityClaim = 5,
};

enum class GlossaryCategory : uint8_t {
  AiRag = 0,
  Evals = 1,
  Metrics = 2,
  Product = 3,
  Interview = 4,
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
  LanguageMode languageMode = LanguageMode::Manual;
  AutoRotateIntervalMode autoRotateIntervalMode = AutoRotateIntervalMode::Off;
  FontSizeMode fontSizeMode = FontSizeMode::Large;
  FontStyleMode fontStyleMode = FontStyleMode::HighContrast;
  ContrastMode contrastMode = ContrastMode::Max;
  LineSpacingMode lineSpacingMode = LineSpacingMode::Tight;
  RefreshMode refreshMode = RefreshMode::Balanced;
  PowerMode powerMode = PowerMode::Normal;
  BadgeSleepMode badgeSleepMode = BadgeSleepMode::Off;
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

struct ReaderPageSet {
  std::vector<String> lines;
  uint8_t linesPerPage = 1;
  uint8_t pageCount = 1;
  uint32_t sourceLength = 0;
  uint32_t sanitizedLength = 0;
};

struct PracticeLayout {
  int32_t contentX = 34;
  int32_t contentY = 96;
  int32_t contentW = 472;
  int32_t contentH = 760;
  int32_t footerY = 878;
  int32_t buttonH = 58;
  int32_t lineHeight = 44;
  uint8_t linesPerPage = 12;
  FontSizeMode renderSize = FontSizeMode::Large;
  bool autoFit = false;
};

struct PracticePageCounts {
  uint8_t promptPages = 1;
  uint8_t spokenPages = 1;
  uint8_t anchorPages = 1;
  uint8_t watchPages = 1;
  uint8_t totalPages = 4;
};

struct CoachReaderStage {
  const char* name = "Text";
  String body;
  ReaderPageSet pages;
};

struct DrillOptionPage {
  uint8_t firstOption = 0;
  uint8_t optionCount = 0;
};

struct DrillPagePlan {
  ReaderPageSet questionPages;
  DrillOptionPage optionPages[kMaxOptions];
  uint8_t optionPageCount = 1;
  uint8_t totalPages = 2;
  bool combinedQuestionOptions = false;
  uint8_t questionLineCount = 0;
  int32_t questionBlockHeight = 0;
  String reminder;
};

struct CoachItem {
  CoachItemType type = CoachItemType::Qa;
  String id;
  String cardId;
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
  const char* cardId = "";
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

struct SessionResult {
  uint32_t millisAt = 0;
  uint32_t sessionId = 0;
  char itemId[32] = {};
  char cardId[16] = {};
  char mode[18] = {};
  char type[22] = {};
  char category[28] = {};
  uint8_t selectedOption = 0;
  uint8_t bestOption = 0;
  bool correct = false;
  bool firstAttempt = true;
  char reader[16] = {};
};

struct CategoryStat {
  char name[28] = {};
  uint16_t total = 0;
  uint16_t correct = 0;
};

enum class GlossaryLineKind : uint8_t {
  Term,
  Label,
  Body,
  Space,
};

struct GlossaryRenderLine {
  String text;
  GlossaryLineKind kind = GlossaryLineKind::Body;
  int32_t height = 0;
};

uint8_t interviewStageCount(const CoachItemView& item);
PracticeLayout practiceLayoutFor(FontSizeMode renderSize);
uint8_t glossaryPageCountFor(const CoachItemView& item, const PracticeLayout& layout);

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
Rect gPracticeButton;
Rect gDrillsButton;
Rect gExamButton;
Rect gResultsButton;
Rect gExamStart10Button;
Rect gExamStart5Button;
Rect gExamReviewButton;
Rect gGlossaryAiButton;
Rect gGlossaryEvalsButton;
Rect gGlossaryMetricsButton;
Rect gGlossaryProductButton;
Rect gGlossaryInterviewButton;
Rect gPracticeMustButton;
Rect gPracticeAllButton;
Rect gPracticeContinueButton;
Rect gPracticeHelpButton;
Rect gDrillAllButton;
Rect gDrillWeakButton;
Rect gDrillMetricButton;
Rect gDrillFollowupButton;
Rect gDrillFrameworkButton;
Rect gDrillMaturityButton;
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
Rect gPowerModeButton;
Rect gBadgeSleepButton;
Rect gFontStyleButton;
Rect gContrastButton;
Rect gFontMediumButton;
Rect gFontLargeButton;
Rect gFontXlButton;
Rect gFontXxlButton;
Rect gFontHugeButton;
Rect gHomeButton;
Rect gNextButton;
Rect gFilterButton;
Rect gTouchDebugButton;
Rect gLayoutDebugButton;
Rect gRenderTraceButton;
Rect gExportDeckButton;
Rect gPowerAuditButton;
Rect gHelpButton;
Rect gFontLabButton;
Rect gVisualQaButton;
Rect gTypographyResetButton;
Rect gFontLabStyleButton;
Rect gFontLabSizeButton;
Rect gFontLabContrastButton;
Rect gFontLabLineSpacingButton;
Rect gOptionButtons[kMaxOptions];
Rect gReaderContentRect;
bool gSdMounted = false;
bool gBadgeJsonLoaded = false;
bool gCoachDeckLoadedFromSd = false;
bool gGlossaryLoadedFromSd = false;
bool gReaderMidVlwAvailable = false;
size_t gCoachMustMasterCount = 0;
size_t gCoachCardCount = 0;
size_t gCoachDrillCount = 0;
size_t gCoachGlossaryCount = 0;
String gCoachDeckSource = "embedded";
String gGlossarySource = "embedded";
uint8_t gNormalPortraitRotation = 0;
uint32_t gLastLanguageSwitchMs = 0;
String gLastBadgeSource = "embedded";
uint32_t gBadgeRedrawCount = 0;
CoachItem gCoachItems[kMaxCoachItems];
CoachItem gSdGlossaryItems[kMaxSdGlossaryTerms];
size_t gCoachItemCount = 0;
size_t gCoachIndex = 0;
size_t gLastPracticeIndex = 0;
uint8_t gCoachStage = 0;
int8_t gSelectedOption = -1;
uint8_t gMockStep = 0;
uint8_t gBottomLeftTapCount = 0;
uint32_t gLastBottomLeftTapMs = 0;
bool gTouchDebugEnabled = false;
bool gInterviewMustMasterOnly = false;
bool gHasPracticeLastIndex = false;
DrillCategory gDrillCategory = DrillCategory::All;
GlossaryCategory gGlossaryCategory = GlossaryCategory::AiRag;
bool gInputLocked = false;
bool gCurrentRefreshClean = false;
uint32_t gInputUnlockAtMs = 0;
uint32_t gLastRefreshEndMs = 0;
uint32_t gLastDebounceMs = 0;
uint32_t gRefreshTransitionCount = 0;
uint32_t gDisplayRefreshCount = 0;
uint32_t gLastUserActivityMs = 0;
bool gIdleModeActive = false;
uint32_t gIdleEntryCount = 0;
bool gTouchActive = false;
int32_t gLastTouchDownX = -1;
int32_t gLastTouchDownY = -1;
int32_t gLastTouchUpX = -1;
int32_t gLastTouchUpY = -1;
String gLastHitTarget = "none";
String gLastIgnoredTouchReason = "none";
bool gHitMatchedThisTap = false;
uint32_t gSanitizerReplacementTotal = 0;
uint32_t gSanitizerReplacementLast = 0;
bool gCoachNeedsCleanEntryRefresh = false;
String gLastRenderTrace;
String gLastRenderTraceStatus = "not dumped";
String gLastDeckExportStatus = "not exported";
String gLastRefreshReason = "boot";
SessionResult gSessionResults[kMaxSessionResults];
size_t gSessionResultCount = 0;
uint32_t gSessionId = 0;
String gResultsStorageStatus = "RAM session only";
size_t gExamItemIndices[kMaxExamQuestions];
uint8_t gExamCount = 0;
uint8_t gExamPosition = 0;
uint8_t gExamCorrect = 0;
bool gExamActive = false;
bool gExamSummary = false;
bool gExamHadShortage = false;
uint8_t gLastExamCount = 0;
uint8_t gLastExamCorrect = 0;
bool gLastExamHadShortage = false;
char gLastExamMissIds[kMaxExamQuestions][32] = {};
char gLastExamMissCategories[kMaxExamQuestions][28] = {};
uint8_t gLastExamMissCount = 0;
uint32_t gLastPowerPollMs = 0;
int16_t gCachedBatteryMv = -1;
int32_t gCachedBatteryLevel = -1;
int32_t gCachedBatteryCurrentMa = 0;
int16_t gCachedVbusMv = -1;
m5::Power_Class::is_charging_t gCachedChargingState = m5::Power_Class::charge_unknown;
String gLastSleepAttempt = "none";
String gLastWakeReason = "not sleep";
uint32_t gLastSleepAttemptMs = 0;
uint32_t gLastLightSleepMs = 0;
Screen gLastRenderedScreen = Screen::Badge;
String gLastRenderedReason = "";
uint8_t gRepeatedRenderCount = 0;

const char* screenName(Screen screen);
const char* fontSizeModeName();
const char* refreshModeName();
const char* powerModeName();
const char* fontStyleModeName();
const char* contrastModeName();
const char* lineSpacingModeName();
void applyPowerPolicy(const char* reason);
void recordUserActivity(const char* reason);
void maybeEnterPowerIdle();
uint32_t loopDelayMs();
void applyCoachButtonFont();
CoachTypography coachTypography();
const char* coachDisplayId(const CoachItemView& item);
TextLayoutResult wrapTextToLines(const String& text, int32_t width, int32_t lineHeight, uint8_t maxLines,
                                 String* lines);
void logLayoutBox(const char* field, const Rect& box, int32_t usedHeight, uint8_t pageCount, bool overflow);
int32_t coachFooterTop();
void logCurrentLayoutDiagnostics(const char* reason);
void dumpCurrentRenderTraceToSd();

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
  Serial.println("Boot beep skipped: buzzer silent unless an explicit feedback setting is added.");
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
    case LanguageMode::Manual:
      return "Manual toggle";
    case LanguageMode::Auto:
    default:
      return "Auto rotate";
  }
}

const char* autoRotateIntervalName() {
  switch (gSettings.autoRotateIntervalMode) {
    case AutoRotateIntervalMode::Sec15:
      return "15s";
    case AutoRotateIntervalMode::Sec30:
      return "30s";
    case AutoRotateIntervalMode::Sec60:
      return "60s";
    case AutoRotateIntervalMode::Off:
    default:
      return "Off";
  }
}

uint32_t autoRotateIntervalSeconds() {
  switch (gSettings.autoRotateIntervalMode) {
    case AutoRotateIntervalMode::Sec15:
      return 15;
    case AutoRotateIntervalMode::Sec30:
      return 30;
    case AutoRotateIntervalMode::Sec60:
      return 60;
    case AutoRotateIntervalMode::Off:
    default:
      return 0;
  }
}

const char* fontSizeModeName() {
  switch (gSettings.fontSizeMode) {
    case FontSizeMode::Medium:
      return "Reader S";
    case FontSizeMode::XL:
      return "Reader L";
    case FontSizeMode::XXL:
      return "Reader L (legacy XXL)";
    case FontSizeMode::Huge:
      return "Reader L (legacy Huge)";
    case FontSizeMode::Large:
    default:
      return "Reader M";
  }
}

const char* shortFontSizeModeName(FontSizeMode size) {
  switch (size) {
    case FontSizeMode::Medium:
      return "Reader S";
    case FontSizeMode::Large:
      return "Reader M";
    case FontSizeMode::XL:
      return "Reader L";
    case FontSizeMode::XXL:
      return "Legacy XXL";
    case FontSizeMode::Huge:
    default:
      return "Legacy Huge";
  }
}

FontSizeMode canonicalFontSizeMode(FontSizeMode size) {
  switch (size) {
    case FontSizeMode::Medium:
    case FontSizeMode::Large:
    case FontSizeMode::XL:
      return size;
    case FontSizeMode::XXL:
    case FontSizeMode::Huge:
    default:
      return FontSizeMode::XL;
  }
}

const char* refreshModeName() {
  switch (gSettings.refreshMode) {
    case RefreshMode::Fast:
      return "Fast";
    case RefreshMode::Clean:
      return "Clean";
    case RefreshMode::Balanced:
    default:
      return "Balanced";
  }
}

const char* lineSpacingModeName() {
  switch (gSettings.lineSpacingMode) {
    case LineSpacingMode::Normal:
      return "Normal";
    case LineSpacingMode::Loose:
      return "Loose";
    case LineSpacingMode::Tight:
    default:
      return "Tight";
  }
}

void cycleLineSpacingMode() {
  switch (gSettings.lineSpacingMode) {
    case LineSpacingMode::Tight:
      gSettings.lineSpacingMode = LineSpacingMode::Normal;
      break;
    case LineSpacingMode::Normal:
      gSettings.lineSpacingMode = LineSpacingMode::Loose;
      break;
    case LineSpacingMode::Loose:
    default:
      gSettings.lineSpacingMode = LineSpacingMode::Tight;
      break;
  }
}

void cycleRefreshMode() {
  switch (gSettings.refreshMode) {
    case RefreshMode::Fast:
      gSettings.refreshMode = RefreshMode::Balanced;
      break;
    case RefreshMode::Balanced:
      gSettings.refreshMode = RefreshMode::Clean;
      break;
    case RefreshMode::Clean:
    default:
      gSettings.refreshMode = RefreshMode::Fast;
      break;
  }
}

bool refreshReasonContains(const char* reason, const char* token) {
  return reason != nullptr && token != nullptr && strstr(reason, token) != nullptr;
}

bool isImageOrZoomRefresh(const char* reason) {
  return gScreen == Screen::Badge || gScreen == Screen::QrZoom || gScreen == Screen::PhotoZoom ||
         refreshReasonContains(reason, "zoom") || refreshReasonContains(reason, "badge") ||
         refreshReasonContains(reason, "language") || refreshReasonContains(reason, "orientation");
}

bool shouldUseCleanRefresh(const char* reason, bool highQuality, bool hardCleanTriggered) {
  if (hardCleanTriggered || gSettings.refreshMode == RefreshMode::Clean) {
    return true;
  }
  if (gSettings.refreshMode == RefreshMode::Balanced) {
    return highQuality || isImageOrZoomRefresh(reason);
  }
  return isImageOrZoomRefresh(reason);
}

bool chooseRefreshClean(const char* reason, bool highQuality, bool& hardCleanTriggered) {
  const uint32_t nextTransition = gRefreshTransitionCount + 1;
  hardCleanTriggered = nextTransition >= kHardCleanTransitionLimit;
  const bool cleanRefresh = shouldUseCleanRefresh(reason, highQuality, hardCleanTriggered);
  gRefreshTransitionCount = cleanRefresh ? 0 : nextTransition;
  Serial.printf("Refresh policy: mode=%s reason=%s highQuality=%s actual=%s transitionCount=%u hardClean=%s\n",
                refreshModeName(), reason && reason[0] != '\0' ? reason : "render", highQuality ? "yes" : "no",
                cleanRefresh ? "Clean" : "Fast", static_cast<unsigned>(gRefreshTransitionCount),
                hardCleanTriggered ? "yes" : "no");
  return cleanRefresh;
}

void noteForcedCleanRefresh(const char* reason) {
  gRefreshTransitionCount = 0;
  Serial.printf("Refresh policy: mode=%s reason=%s highQuality=yes actual=Clean transitionCount=0 hardClean=no\n",
                refreshModeName(), reason && reason[0] != '\0' ? reason : "forced clean");
}

const char* powerModeName() {
  switch (gSettings.powerMode) {
    case PowerMode::BatterySaver:
      return "Battery Saver";
    case PowerMode::ConferenceBadge:
      return "Conference Badge";
    case PowerMode::Normal:
    default:
      return "Normal";
  }
}

const char* badgeSleepModeName() {
  switch (gSettings.badgeSleepMode) {
    case BadgeSleepMode::Light:
      return "Light";
    case BadgeSleepMode::DeepExperiment:
      return "Deep experiment";
    case BadgeSleepMode::Off:
    default:
      return "Off";
  }
}

void cycleBadgeSleepMode() {
  switch (gSettings.badgeSleepMode) {
    case BadgeSleepMode::Off:
      gSettings.badgeSleepMode = BadgeSleepMode::Light;
      break;
    case BadgeSleepMode::Light:
      gSettings.badgeSleepMode = BadgeSleepMode::DeepExperiment;
      break;
    case BadgeSleepMode::DeepExperiment:
    default:
      gSettings.badgeSleepMode = BadgeSleepMode::Off;
      break;
  }
}

const char* wakeReasonName(esp_sleep_wakeup_cause_t reason) {
  switch (reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ulp";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "gpio";
    case ESP_SLEEP_WAKEUP_UART:
      return "uart";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
      return "not sleep";
  }
}

const char* chargingStateName(m5::Power_Class::is_charging_t state) {
  switch (state) {
    case m5::Power_Class::is_charging:
      return "charging";
    case m5::Power_Class::is_discharging:
      return "discharging";
    case m5::Power_Class::charge_unknown:
    default:
      return "unknown";
  }
}

int32_t batteryLevelPercent() {
  const uint32_t now = millis();
  if (gLastPowerPollMs == 0 || now - gLastPowerPollMs >= kPowerPollIntervalMs) {
    gCachedBatteryMv = M5.Power.getBatteryVoltage();
    gCachedBatteryLevel = M5.Power.getBatteryLevel();
    gCachedBatteryCurrentMa = M5.Power.getBatteryCurrent();
    gCachedVbusMv = M5.Power.getVBUSVoltage();
    gCachedChargingState = M5.Power.isCharging();
    gLastPowerPollMs = now;
    Serial.printf("Power poll: batteryMv=%d level=%ld currentMa=%ld vbusMv=%d charge=%s\n",
                  static_cast<int>(gCachedBatteryMv), static_cast<long>(gCachedBatteryLevel),
                  static_cast<long>(gCachedBatteryCurrentMa), static_cast<int>(gCachedVbusMv),
                  chargingStateName(gCachedChargingState));
  }

  if (gCachedBatteryLevel >= 0 && gCachedBatteryLevel <= 100) {
    return gCachedBatteryLevel;
  }

  const int16_t mv = gCachedBatteryMv;
  if (mv <= 0) {
    return -1;
  }
  const long estimated = map(static_cast<long>(mv), 3300L, 4200L, 0L, 100L);
  return static_cast<int32_t>(constrain(estimated, 0L, 100L));
}

const char* usbPowerName(int16_t vbusMv) {
  if (vbusMv < 0) {
    return "unknown";
  }
  return vbusMv >= 4300 ? "yes" : "no";
}

String batteryStatusLine() {
  const int32_t level = batteryLevelPercent();
  const int16_t batteryMv = gCachedBatteryMv;
  String status = "battery: ";
  if (batteryMv > 0) {
    status += batteryMv;
    status += "mV ";
  } else {
    status += "unknown ";
  }
  if (level >= 0) {
    status += level;
    status += "%";
  } else {
    status += "--%";
  }
  return status;
}

String chargeStatusLine() {
  batteryLevelPercent();
  String status = "charge: ";
  status += chargingStateName(gCachedChargingState);
  status += " ";
  status += gCachedBatteryCurrentMa;
  status += "mA";
  return status;
}

String usbStatusLine() {
  batteryLevelPercent();
  const int16_t vbusMv = gCachedVbusMv;
  String status = "usb: ";
  status += usbPowerName(vbusMv);
  if (vbusMv >= 0) {
    status += " ";
    status += vbusMv;
    status += "mV";
  }
  return status;
}

String compactPowerStatusLine() {
  const int32_t level = batteryLevelPercent();
  const int16_t batteryMv = gCachedBatteryMv;
  const int16_t vbusMv = gCachedVbusMv;

  String status = "Batt ";
  if (batteryMv > 0) {
    status += batteryMv;
    status += "mV";
  } else {
    status += "unknown";
  }
  status += " ";
  if (level >= 0) {
    status += level;
    status += "%";
  } else {
    status += "--%";
  }
  status += "  USB ";
  status += usbPowerName(vbusMv);
  return status;
}

void drawBatteryBar(int32_t x, int32_t y, int32_t w, int32_t h, int32_t level) {
  auto& display = M5.Display;
  const int32_t terminalW = 5;
  const int32_t innerX = x + 2;
  const int32_t innerY = y + 2;
  const int32_t innerW = w - 4;
  const int32_t innerH = h - 4;
  const int32_t clampedLevel = level >= 0 ? constrain(level, 0, 100) : 0;
  const int32_t fillW = innerW * clampedLevel / 100;

  display.drawRect(x, y, w, h, TFT_BLACK);
  display.fillRect(x + w, y + h / 4, terminalW, h / 2, TFT_BLACK);
  display.fillRect(innerX, innerY, innerW, innerH, TFT_WHITE);
  if (level >= 0 && fillW > 0) {
    display.fillRect(innerX, innerY, fillW, innerH, TFT_BLACK);
  }
  if (level < 0) {
    for (int32_t hatchX = innerX; hatchX < innerX + innerW; hatchX += 10) {
      display.drawLine(hatchX, innerY + innerH, hatchX + 8, innerY, TFT_DARKGREY);
    }
  }
}

void logPowerAudit(const char* reason) {
  const int32_t level = batteryLevelPercent();
  const int16_t batteryMv = gCachedBatteryMv;
  const int32_t currentMa = gCachedBatteryCurrentMa;
  const int16_t vbusMv = gCachedVbusMv;
  const bool wifiOff = WiFi.getMode() == WIFI_OFF;
  const bool bluetoothStarted = btStarted();
  const bool badgeStatic = gSettings.powerMode == PowerMode::ConferenceBadge ||
                           gSettings.languageMode != LanguageMode::Auto || autoRotateIntervalSeconds() == 0;
  const uint32_t activeLoopDelayMs =
      gIdleModeActive && gSettings.powerMode == PowerMode::ConferenceBadge && gScreen == Screen::Badge
          ? 500
          : (gSettings.powerMode == PowerMode::BatterySaver || gSettings.powerMode == PowerMode::ConferenceBadge ? 120
                                                                                                                   : 50);

  Serial.printf(
      "Power audit: reason=%s mode=%s idle=%s batteryMv=%d level=%ld charge=%s currentMa=%ld vbusMv=%d usb=%s "
      "wifi=%s bluetooth=%s imu=disabled speaker=stopped badgeMode=%s badgeLang=%s autoInterval=%s staticBadge=%s "
      "cpuMhz=%u badgeSleep=%s sleepEnabled=%s lastSleep=\"%s\" lastWake=%s millis=%u redraws=%u refreshes=%u "
      "badgeRedraws=%u lastRefreshReason=\"%s\" lastInputMs=%u loopDelayMs=%u\n",
      reason && reason[0] != '\0' ? reason : "audit", powerModeName(), gIdleModeActive ? "yes" : "no",
      static_cast<int>(batteryMv), static_cast<long>(level), chargingStateName(gCachedChargingState),
      static_cast<long>(currentMa), static_cast<int>(vbusMv), usbPowerName(vbusMv), wifiOff ? "off" : "on",
      bluetoothStarted ? "on" : "off", languageModeName(), languageName(gBadgeLanguage), autoRotateIntervalName(),
      badgeStatic ? "yes" : "no", static_cast<unsigned>(ESP.getCpuFreqMHz()), badgeSleepModeName(),
      gSettings.badgeSleepMode == BadgeSleepMode::Off ? "no" : "yes", gLastSleepAttempt.c_str(),
      gLastWakeReason.c_str(), static_cast<unsigned>(millis()), static_cast<unsigned>(gDisplayRefreshCount),
      static_cast<unsigned>(gDisplayRefreshCount), static_cast<unsigned>(gBadgeRedrawCount),
      gLastRefreshReason.c_str(), static_cast<unsigned>(gLastUserActivityMs), static_cast<unsigned>(activeLoopDelayMs));
}

void cyclePowerMode() {
  switch (gSettings.powerMode) {
    case PowerMode::Normal:
      gSettings.powerMode = PowerMode::BatterySaver;
      break;
    case PowerMode::BatterySaver:
      gSettings.powerMode = PowerMode::ConferenceBadge;
      break;
    case PowerMode::ConferenceBadge:
    default:
      gSettings.powerMode = PowerMode::Normal;
      break;
  }
}

void disableUnusedRadiosAndPeripherals(const char* reason) {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  btStop();
  M5.Speaker.stop();
  Serial.printf("Power policy: unused radios/peripherals off reason=%s wifi=off bluetooth=off speaker=stopped\n",
                reason && reason[0] != '\0' ? reason : "policy");
}

void applyPowerPolicy(const char* reason) {
  disableUnusedRadiosAndPeripherals(reason);
  if (gSettings.powerMode == PowerMode::Normal && gIdleModeActive) {
    gIdleModeActive = false;
    Serial.printf("Power idle exit: mode=%s reason=%s\n", powerModeName(),
                  reason && reason[0] != '\0' ? reason : "normal mode");
  }
  Serial.printf("Power mode active: mode=%s idle=%s reason=%s\n", powerModeName(), gIdleModeActive ? "yes" : "no",
                reason && reason[0] != '\0' ? reason : "policy");
  logPowerAudit(reason);
}

void recordUserActivity(const char* reason) {
  gLastUserActivityMs = millis();
  if (gIdleModeActive) {
    gIdleModeActive = false;
    Serial.printf("Power idle exit: mode=%s reason=%s\n", powerModeName(),
                  reason && reason[0] != '\0' ? reason : "touch");
  }
}

void enterIdleMode(const char* reason) {
  if (gIdleModeActive) {
    return;
  }
  gIdleModeActive = true;
  ++gIdleEntryCount;
  disableUnusedRadiosAndPeripherals(reason);
  Serial.printf("Power idle entry: mode=%s reason=%s count=%u sleep=deferred\n", powerModeName(),
                reason && reason[0] != '\0' ? reason : "inactivity", static_cast<unsigned>(gIdleEntryCount));
  logPowerAudit(reason);
}

void maybeEnterPowerIdle() {
  if (gSettings.powerMode == PowerMode::Normal || gInputLocked || gTouchActive || gLastUserActivityMs == 0) {
    return;
  }

  const uint32_t elapsedMs = millis() - gLastUserActivityMs;
  if (elapsedMs < kPostTouchIdleGuardMs) {
    return;
  }
  if (gSettings.powerMode == PowerMode::BatterySaver && elapsedMs >= kBatterySaverIdleMs) {
    enterIdleMode("battery saver inactivity");
  } else if (gSettings.powerMode == PowerMode::ConferenceBadge && gScreen == Screen::Badge &&
             elapsedMs >= kConferenceBadgeIdleMs) {
    enterIdleMode("conference badge static display");
  }
}

void maybeEnterBadgeSleep() {
  if (gScreen != Screen::Badge || gSettings.badgeSleepMode == BadgeSleepMode::Off || gInputLocked || gTouchActive ||
      gLastUserActivityMs == 0) {
    return;
  }
  const uint32_t now = millis();
  if (now - gLastUserActivityMs < kBadgeLightSleepIdleMs) {
    return;
  }

  if (gSettings.badgeSleepMode == BadgeSleepMode::DeepExperiment) {
    if (now - gLastSleepAttemptMs >= kBadgeLightSleepIdleMs) {
      gLastSleepAttemptMs = now;
      gLastSleepAttempt = "deep blocked: touch wake unverified";
      Serial.println("Badge deep sleep experiment blocked: safe PaperS3 touch wake source is not verified.");
    }
    return;
  }

  if (gSettings.badgeSleepMode == BadgeSleepMode::Light && now - gLastLightSleepMs >= 5000) {
    gLastSleepAttemptMs = now;
    gLastSleepAttempt = "light sleep timer 2s";
    disableUnusedRadiosAndPeripherals("badge light sleep");
    Serial.println("Badge light sleep entry: timer=2s touchWake=not-used display=static");
    esp_sleep_enable_timer_wakeup(kBadgeLightSleepDurationUs);
    esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gLastLightSleepMs = millis();
    gLastWakeReason = wakeReasonName(esp_sleep_get_wakeup_cause());
    Serial.printf("Badge light sleep wake: reason=%s millis=%u\n", gLastWakeReason.c_str(),
                  static_cast<unsigned>(gLastLightSleepMs));
  }
}

uint32_t loopDelayMs() {
  if (gIdleModeActive && gSettings.powerMode == PowerMode::ConferenceBadge && gScreen == Screen::Badge) {
    return 500;
  }
  if (gSettings.powerMode == PowerMode::BatterySaver || gSettings.powerMode == PowerMode::ConferenceBadge) {
    return 120;
  }
  return 50;
}

const char* fontStyleModeName() {
  switch (gSettings.fontStyleMode) {
    case FontStyleMode::SansCurrent:
      return "Sans Thin/current";
    case FontStyleMode::SansBoldLike:
      return "Sans Bold-like";
    case FontStyleMode::HighContrast:
      return "High Contrast";
    case FontStyleMode::DebugMono:
      return "Debug Mono";
    case FontStyleMode::LargeReader:
    default:
      return "Large Reader";
  }
}

const char* contrastModeName() {
  switch (gSettings.contrastMode) {
    case ContrastMode::Standard:
      return "Standard";
    case ContrastMode::Max:
      return "Max";
    case ContrastMode::Dark:
    default:
      return "Dark";
  }
}

const char* drillCategoryName(DrillCategory category) {
  switch (category) {
    case DrillCategory::WeakAnswer:
      return "Weak Answer";
    case DrillCategory::MetricPrecision:
      return "Metric Precision";
    case DrillCategory::FollowupDefense:
      return "Follow-up Defense";
    case DrillCategory::FrameworkChoice:
      return "Framework Choice";
    case DrillCategory::MaturityClaim:
      return "Maturity Claim";
    case DrillCategory::All:
    default:
      return "All Drills";
  }
}

const char* glossaryCategoryName(GlossaryCategory category) {
  switch (category) {
    case GlossaryCategory::Evals:
      return "Evals";
    case GlossaryCategory::Metrics:
      return "Metrics";
    case GlossaryCategory::Product:
      return "Product";
    case GlossaryCategory::Interview:
      return "Interview";
    case GlossaryCategory::AiRag:
    default:
      return "AI/RAG";
  }
}

bool glossaryCategoryMatches(const char* category) {
  if (category == nullptr || category[0] == '\0') {
    return false;
  }
  return String(category) == glossaryCategoryName(gGlossaryCategory);
}

void cycleFontSizeMode() {
  switch (canonicalFontSizeMode(gSettings.fontSizeMode)) {
    case FontSizeMode::Medium:
      gSettings.fontSizeMode = FontSizeMode::Large;
      break;
    case FontSizeMode::Large:
      gSettings.fontSizeMode = FontSizeMode::XL;
      break;
    case FontSizeMode::XL:
    default:
      gSettings.fontSizeMode = FontSizeMode::Medium;
      break;
  }
}

void resetTypographyDefaults() {
  gSettings.fontStyleMode = FontStyleMode::HighContrast;
  gSettings.fontSizeMode = FontSizeMode::Large;
  gSettings.contrastMode = ContrastMode::Max;
  gSettings.lineSpacingMode = LineSpacingMode::Tight;
  Serial.println("Typography reset: style=High Contrast size=Reader M contrast=Max lineSpacing=Tight");
}

void cycleFontStyleMode() {
  switch (gSettings.fontStyleMode) {
    case FontStyleMode::SansCurrent:
      gSettings.fontStyleMode = FontStyleMode::LargeReader;
      break;
    case FontStyleMode::LargeReader:
      gSettings.fontStyleMode = FontStyleMode::SansBoldLike;
      break;
    case FontStyleMode::SansBoldLike:
      gSettings.fontStyleMode = FontStyleMode::HighContrast;
      break;
    case FontStyleMode::HighContrast:
      gSettings.fontStyleMode = FontStyleMode::DebugMono;
      break;
    case FontStyleMode::DebugMono:
    default:
      gSettings.fontStyleMode = FontStyleMode::SansCurrent;
      break;
  }
}

void cycleContrastMode() {
  switch (gSettings.contrastMode) {
    case ContrastMode::Standard:
      gSettings.contrastMode = ContrastMode::Dark;
      break;
    case ContrastMode::Dark:
      gSettings.contrastMode = ContrastMode::Max;
      break;
    case ContrastMode::Max:
    default:
      gSettings.contrastMode = ContrastMode::Standard;
      break;
  }
}

void cycleLanguageMode() {
  switch (gSettings.languageMode) {
    case LanguageMode::Manual:
      gSettings.languageMode = LanguageMode::English;
      gBadgeLanguage = BadgeLanguage::English;
      break;
    case LanguageMode::English:
      gSettings.languageMode = LanguageMode::Japanese;
      gBadgeLanguage = BadgeLanguage::Japanese;
      break;
    case LanguageMode::Japanese:
      gSettings.languageMode = LanguageMode::Auto;
      break;
    case LanguageMode::Auto:
    default:
      gSettings.languageMode = LanguageMode::Manual;
      break;
  }
}

void cycleAutoRotateInterval() {
  switch (gSettings.autoRotateIntervalMode) {
    case AutoRotateIntervalMode::Off:
      gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Sec15;
      break;
    case AutoRotateIntervalMode::Sec15:
      gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Sec30;
      break;
    case AutoRotateIntervalMode::Sec30:
      gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Sec60;
      break;
    case AutoRotateIntervalMode::Sec60:
    default:
      gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Off;
      break;
  }
}

void loadSettings() {
  gPrefs.begin(kPrefsNamespace, true);
  const uint8_t orientation = gPrefs.getUChar("orient", static_cast<uint8_t>(OrientationMode::Strap));
  const uint8_t language = gPrefs.getUChar("lang", static_cast<uint8_t>(LanguageMode::Manual));
  const uint8_t languageModeVersion = gPrefs.getUChar("langModeV", 0);
  const uint8_t badgeLanguage = gPrefs.getUChar("badgeLang", static_cast<uint8_t>(BadgeLanguage::English));
  const uint8_t autoRotateInterval = gPrefs.getUChar("autoInt", static_cast<uint8_t>(AutoRotateIntervalMode::Off));
  const bool hasFontSetting = gPrefs.isKey("font");
  const uint8_t fontVersion = gPrefs.getUChar("fontV", 0);
  const uint8_t fontSize = gPrefs.getUChar("font", static_cast<uint8_t>(FontSizeMode::XXL));
  const uint8_t fontStyle = gPrefs.getUChar("fontStyle", static_cast<uint8_t>(FontStyleMode::HighContrast));
  const uint8_t contrast = gPrefs.getUChar("contrast", static_cast<uint8_t>(ContrastMode::Max));
  const uint8_t lineSpacing = gPrefs.getUChar("lineSpace", static_cast<uint8_t>(LineSpacingMode::Tight));
  const bool hasRefreshSetting = gPrefs.isKey("refresh");
  const uint8_t refreshVersion = gPrefs.getUChar("refreshV", 0);
  const uint8_t refreshMode = gPrefs.getUChar("refresh", static_cast<uint8_t>(RefreshMode::Balanced));
  const uint8_t powerMode = gPrefs.getUChar("power", static_cast<uint8_t>(PowerMode::Normal));
  const uint8_t badgeSleepMode = gPrefs.getUChar("badgeSleep", static_cast<uint8_t>(BadgeSleepMode::Off));
  gPrefs.end();

  gSettings.orientationMode = orientation == static_cast<uint8_t>(OrientationMode::Handheld) ? OrientationMode::Handheld
                                                                                             : OrientationMode::Strap;
  gBadgeLanguage = badgeLanguage == static_cast<uint8_t>(BadgeLanguage::Japanese) ? BadgeLanguage::Japanese
                                                                                  : BadgeLanguage::English;

  if (language == static_cast<uint8_t>(LanguageMode::Auto) && languageModeVersion == 0) {
    gSettings.languageMode = LanguageMode::Manual;
  } else if (language == static_cast<uint8_t>(LanguageMode::English)) {
    gSettings.languageMode = LanguageMode::English;
  } else if (language == static_cast<uint8_t>(LanguageMode::Japanese)) {
    gSettings.languageMode = LanguageMode::Japanese;
  } else if (language == static_cast<uint8_t>(LanguageMode::Auto)) {
    gSettings.languageMode = LanguageMode::Auto;
  } else {
    gSettings.languageMode = LanguageMode::Manual;
  }

  if (autoRotateInterval == static_cast<uint8_t>(AutoRotateIntervalMode::Sec15)) {
    gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Sec15;
  } else if (autoRotateInterval == static_cast<uint8_t>(AutoRotateIntervalMode::Sec30)) {
    gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Sec30;
  } else if (autoRotateInterval == static_cast<uint8_t>(AutoRotateIntervalMode::Sec60)) {
    gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Sec60;
  } else {
    gSettings.autoRotateIntervalMode = AutoRotateIntervalMode::Off;
  }

  if (fontVersion == 0 && hasFontSetting && fontSize == 3) {
    gSettings.fontSizeMode = FontSizeMode::XL;
  } else if (fontSize == static_cast<uint8_t>(FontSizeMode::Medium)) {
    gSettings.fontSizeMode = FontSizeMode::Medium;
  } else if (fontSize == static_cast<uint8_t>(FontSizeMode::XL)) {
    gSettings.fontSizeMode = FontSizeMode::XL;
  } else if (fontVersion > 0 && fontSize == static_cast<uint8_t>(FontSizeMode::XXL)) {
    gSettings.fontSizeMode = FontSizeMode::XL;
  } else if (fontVersion > 0 && fontSize == static_cast<uint8_t>(FontSizeMode::Huge)) {
    gSettings.fontSizeMode = FontSizeMode::XL;
  } else if (!hasFontSetting) {
    gSettings.fontSizeMode = FontSizeMode::Large;
  } else {
    gSettings.fontSizeMode = FontSizeMode::Large;
  }

  if (fontStyle == static_cast<uint8_t>(FontStyleMode::SansCurrent)) {
    gSettings.fontStyleMode = FontStyleMode::SansCurrent;
  } else if (fontStyle == static_cast<uint8_t>(FontStyleMode::SansBoldLike)) {
    gSettings.fontStyleMode = FontStyleMode::SansBoldLike;
  } else if (fontStyle == static_cast<uint8_t>(FontStyleMode::HighContrast)) {
    gSettings.fontStyleMode = FontStyleMode::HighContrast;
  } else if (fontStyle == static_cast<uint8_t>(FontStyleMode::DebugMono)) {
    gSettings.fontStyleMode = FontStyleMode::DebugMono;
  } else {
    gSettings.fontStyleMode = FontStyleMode::LargeReader;
  }

  if (contrast == static_cast<uint8_t>(ContrastMode::Standard)) {
    gSettings.contrastMode = ContrastMode::Standard;
  } else if (contrast == static_cast<uint8_t>(ContrastMode::Max)) {
    gSettings.contrastMode = ContrastMode::Max;
  } else {
    gSettings.contrastMode = ContrastMode::Dark;
  }

  if (lineSpacing == static_cast<uint8_t>(LineSpacingMode::Normal)) {
    gSettings.lineSpacingMode = LineSpacingMode::Normal;
  } else if (lineSpacing == static_cast<uint8_t>(LineSpacingMode::Loose)) {
    gSettings.lineSpacingMode = LineSpacingMode::Loose;
  } else {
    gSettings.lineSpacingMode = LineSpacingMode::Tight;
  }

  if (refreshVersion == 0) {
    gSettings.refreshMode =
        hasRefreshSetting && refreshMode == 1 ? RefreshMode::Clean : RefreshMode::Balanced;
  } else if (refreshMode == static_cast<uint8_t>(RefreshMode::Fast)) {
    gSettings.refreshMode = RefreshMode::Fast;
  } else if (refreshMode == static_cast<uint8_t>(RefreshMode::Clean)) {
    gSettings.refreshMode = RefreshMode::Clean;
  } else {
    gSettings.refreshMode = RefreshMode::Balanced;
  }

  if (powerMode == static_cast<uint8_t>(PowerMode::BatterySaver)) {
    gSettings.powerMode = PowerMode::BatterySaver;
  } else if (powerMode == static_cast<uint8_t>(PowerMode::ConferenceBadge)) {
    gSettings.powerMode = PowerMode::ConferenceBadge;
  } else {
    gSettings.powerMode = PowerMode::Normal;
  }

  if (badgeSleepMode == static_cast<uint8_t>(BadgeSleepMode::Light)) {
    gSettings.badgeSleepMode = BadgeSleepMode::Light;
  } else if (badgeSleepMode == static_cast<uint8_t>(BadgeSleepMode::DeepExperiment)) {
    gSettings.badgeSleepMode = BadgeSleepMode::DeepExperiment;
  } else {
    gSettings.badgeSleepMode = BadgeSleepMode::Off;
  }

  Serial.printf("Settings loaded: orientation=%s badgeLanguage=%s languageMode=%s autoInterval=%s font=%s style=%s "
                "contrast=%s lineSpacing=%s refresh=%s power=%s badgeSleep=%s\n",
                orientationModeName(), languageName(gBadgeLanguage), languageModeName(), autoRotateIntervalName(),
                fontSizeModeName(), fontStyleModeName(), contrastModeName(), lineSpacingModeName(), refreshModeName(),
                powerModeName(), badgeSleepModeName());
}

void saveSettings() {
  gPrefs.begin(kPrefsNamespace, false);
  gPrefs.putUChar("orient", static_cast<uint8_t>(gSettings.orientationMode));
  gPrefs.putUChar("lang", static_cast<uint8_t>(gSettings.languageMode));
  gPrefs.putUChar("langModeV", 1);
  gPrefs.putUChar("badgeLang", static_cast<uint8_t>(gBadgeLanguage));
  gPrefs.putUChar("autoInt", static_cast<uint8_t>(gSettings.autoRotateIntervalMode));
  gPrefs.putUChar("font", static_cast<uint8_t>(gSettings.fontSizeMode));
  gPrefs.putUChar("fontV", 1);
  gPrefs.putUChar("fontStyle", static_cast<uint8_t>(gSettings.fontStyleMode));
  gPrefs.putUChar("contrast", static_cast<uint8_t>(gSettings.contrastMode));
  gPrefs.putUChar("lineSpace", static_cast<uint8_t>(gSettings.lineSpacingMode));
  gPrefs.putUChar("refresh", static_cast<uint8_t>(gSettings.refreshMode));
  gPrefs.putUChar("refreshV", 1);
  gPrefs.putUChar("power", static_cast<uint8_t>(gSettings.powerMode));
  gPrefs.putUChar("badgeSleep", static_cast<uint8_t>(gSettings.badgeSleepMode));
  gPrefs.end();
  Serial.printf("Settings saved: orientation=%s badgeLanguage=%s languageMode=%s autoInterval=%s font=%s style=%s "
                "contrast=%s lineSpacing=%s refresh=%s power=%s badgeSleep=%s\n",
                orientationModeName(), languageName(gBadgeLanguage), languageModeName(), autoRotateIntervalName(),
                fontSizeModeName(), fontStyleModeName(), contrastModeName(), lineSpacingModeName(), refreshModeName(),
                powerModeName(), badgeSleepModeName());
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
  gCoachGlossaryCount = 0;
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
  gCoachGlossaryCount = embedded_papercoach::kGlossaryCount;
  gCoachItemCount = gCoachCardCount + gCoachDrillCount + gCoachGlossaryCount;
  gCoachMustMasterCount = embedded_papercoach::kMustMasterCount;
  gCoachDeckLoadedFromSd = false;
  gCoachDeckSource = "embedded";
  gGlossaryLoadedFromSd = false;
  gGlossarySource = "embedded";
  Serial.println("PaperCoach deck source: embedded");
  Serial.printf("PaperCoach card count: %u\n", static_cast<unsigned>(gCoachCardCount));
  Serial.printf("PaperCoach drill count: %u\n", static_cast<unsigned>(gCoachDrillCount));
  Serial.printf("PaperCoach glossary count: %u\n", static_cast<unsigned>(gCoachGlossaryCount));
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
    item.cardId = object["card_id"] | "";
    if (item.cardId.length() == 0) {
      item.cardId = item.id;
    }
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
  gCoachGlossaryCount = 0;
  Serial.printf("PaperCoach deck source: SD (%s)\n", path);
  Serial.printf("PaperCoach card count: %u\n", static_cast<unsigned>(gCoachItemCount));
  Serial.printf("PaperCoach drill count: %u\n", static_cast<unsigned>(gCoachDrillCount));
  Serial.printf("PaperCoach must-master count: %u\n", static_cast<unsigned>(gCoachMustMasterCount));
  return true;
}

bool loadGlossaryFromSd() {
  for (size_t index = 0; index < kMaxSdGlossaryTerms; ++index) {
    gSdGlossaryItems[index] = CoachItem{};
  }
  gGlossaryLoadedFromSd = false;
  if (!gSdMounted || !SD.exists(kCoachGlossaryPath)) {
    gGlossarySource = "embedded";
    gCoachGlossaryCount = embedded_papercoach::kGlossaryCount;
    gCoachItemCount = gCoachCardCount + gCoachDrillCount + gCoachGlossaryCount;
    Serial.printf("PaperCoach glossary SD missing: %s; using embedded count=%u\n", kCoachGlossaryPath,
                  static_cast<unsigned>(gCoachGlossaryCount));
    return false;
  }

  File file = SD.open(kCoachGlossaryPath, FILE_READ);
  if (!file) {
    gGlossarySource = "embedded";
    gCoachGlossaryCount = embedded_papercoach::kGlossaryCount;
    gCoachItemCount = gCoachCardCount + gCoachDrillCount + gCoachGlossaryCount;
    Serial.println("PaperCoach glossary SD open failed; using embedded.");
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    gGlossarySource = "embedded";
    gCoachGlossaryCount = embedded_papercoach::kGlossaryCount;
    gCoachItemCount = gCoachCardCount + gCoachDrillCount + gCoachGlossaryCount;
    Serial.printf("PaperCoach glossary SD parse failed: %s; using embedded.\n", error.c_str());
    return false;
  }

  JsonArray terms = doc["terms"].as<JsonArray>();
  if (terms.isNull() || terms.size() == 0) {
    gGlossarySource = "embedded";
    gCoachGlossaryCount = embedded_papercoach::kGlossaryCount;
    gCoachItemCount = gCoachCardCount + gCoachDrillCount + gCoachGlossaryCount;
    Serial.println("PaperCoach glossary SD empty; using embedded.");
    return false;
  }

  size_t count = 0;
  for (JsonObject object : terms) {
    if (count >= kMaxSdGlossaryTerms) {
      Serial.printf("PaperCoach glossary SD cap reached: max=%u\n", static_cast<unsigned>(kMaxSdGlossaryTerms));
      break;
    }
    CoachItem item;
    item.type = CoachItemType::Glossary;
    item.category = object["category"] | "";
    item.term = object["term"] | "";
    item.definition = object["definition"] | "";
    const char* why = object["interview_importance"] | "";
    if (why[0] == '\0') {
      why = object["why_it_matters"] | "";
    }
    if (why[0] == '\0') {
      why = object["why"] | "";
    }
    item.explanation = why;
    item.answer = object["example"] | "";
    item.id = item.term;
    item.section = item.category;
    item.title = item.term;
    item.prompt = item.term;
    if (item.term.length() == 0 || item.definition.length() == 0 || item.category.length() == 0) {
      Serial.println("PaperCoach glossary SD term skipped: missing term/category/definition.");
      continue;
    }
    gSdGlossaryItems[count++] = item;
  }

  if (count == 0) {
    gGlossarySource = "embedded";
    gCoachGlossaryCount = embedded_papercoach::kGlossaryCount;
    gCoachItemCount = gCoachCardCount + gCoachDrillCount + gCoachGlossaryCount;
    Serial.println("PaperCoach glossary SD had no usable terms; using embedded.");
    return false;
  }

  gGlossaryLoadedFromSd = true;
  gGlossarySource = "SD";
  gCoachGlossaryCount = count;
  gCoachItemCount = gCoachCardCount + gCoachDrillCount + gCoachGlossaryCount;
  Serial.printf("PaperCoach glossary source: SD (%s) count=%u\n", kCoachGlossaryPath,
                static_cast<unsigned>(gCoachGlossaryCount));
  return true;
}

void loadCoachDeck() {
  if (!loadCoachDeckFromSd()) {
    loadEmbeddedCoachDeck();
  }
  loadGlossaryFromSd();
}

CoachItemView coachItemAt(size_t index) {
  CoachItemView view;
  if (gCoachDeckLoadedFromSd && index < gCoachCardCount) {
    const CoachItem& item = gCoachItems[index];
    view.type = item.type;
    view.id = item.id.c_str();
    view.cardId = item.cardId.length() > 0 ? item.cardId.c_str() : item.id.c_str();
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

  const size_t glossaryStart = gCoachCardCount + gCoachDrillCount;
  if (index >= glossaryStart && index < glossaryStart + gCoachGlossaryCount) {
    const size_t glossaryIndex = index - glossaryStart;
    if (gGlossaryLoadedFromSd && glossaryIndex < gCoachGlossaryCount && glossaryIndex < kMaxSdGlossaryTerms) {
      const CoachItem& item = gSdGlossaryItems[glossaryIndex];
      view.type = CoachItemType::Glossary;
      view.id = item.id.c_str();
      view.cardId = item.id.c_str();
      view.sectionId = "G";
      view.section = item.category.c_str();
      view.title = item.term.c_str();
      view.prompt = item.term.c_str();
      view.answer = item.answer.c_str();
      view.category = item.category.c_str();
      view.term = item.term.c_str();
      view.definition = item.definition.c_str();
      view.explanation = item.explanation.c_str();
      return view;
    }

    if (glossaryIndex < embedded_papercoach::kGlossaryCount) {
      const auto& term = embedded_papercoach::kGlossaryTerms[glossaryIndex];
      view.type = CoachItemType::Glossary;
      view.id = term.term;
      view.cardId = term.term;
      view.sectionId = "G";
      view.section = term.category;
      view.title = term.term;
      view.prompt = term.term;
      view.answer = term.example;
      view.category = term.category;
      view.term = term.term;
      view.definition = term.definition;
      view.explanation = term.interviewImportance;
      return view;
    }
  }

  if (index < embedded_papercoach::kCardCount) {
    const auto& card = embedded_papercoach::kCards[index];
    view.type = CoachItemType::Qa;
    view.id = card.id;
    view.cardId = card.id;
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
    view.cardId = drill.cardId;
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
    return item.type == CoachItemType::Qa;
  }
  if (screen == Screen::Drills) {
    if (gDrillCategory == DrillCategory::All) {
      return item.type == CoachItemType::Mcq || item.type == CoachItemType::WeakAnswer ||
             item.type == CoachItemType::MetricPrecision || item.type == CoachItemType::HostileFollowup;
    }
    if (gDrillCategory == DrillCategory::WeakAnswer) {
      return item.type == CoachItemType::WeakAnswer;
    }
    if (gDrillCategory == DrillCategory::MetricPrecision) {
      return item.type == CoachItemType::MetricPrecision;
    }
    if (gDrillCategory == DrillCategory::FollowupDefense) {
      return item.type == CoachItemType::HostileFollowup;
    }
    if (gDrillCategory == DrillCategory::FrameworkChoice) {
      return item.type == CoachItemType::Mcq;
    }
    if (gDrillCategory == DrillCategory::MaturityClaim) {
      return item.type == CoachItemType::WeakAnswer &&
             (String(item.category).indexOf("maturity") >= 0 || String(item.explanation).indexOf("maturity") >= 0 ||
              String(item.prompt).indexOf("maturity") >= 0);
    }
    return false;
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
    return item.type == CoachItemType::Glossary && glossaryCategoryMatches(item.category);
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

bool findNextCoachItem(Screen screen, size_t currentIndex, size_t& nextIndex) {
  if (gCoachItemCount == 0) {
    return false;
  }
  for (size_t offset = 1; offset <= gCoachItemCount; ++offset) {
    const size_t index = (currentIndex + offset) % gCoachItemCount;
    if (itemMatchesScreen(coachItemAt(index), screen)) {
      nextIndex = index;
      return true;
    }
  }
  return false;
}

bool findPreviousCoachItem(Screen screen, size_t currentIndex, size_t& previousIndex) {
  if (gCoachItemCount == 0) {
    return false;
  }
  for (size_t offset = 1; offset <= gCoachItemCount; ++offset) {
    const size_t index = (currentIndex + gCoachItemCount - offset) % gCoachItemCount;
    if (itemMatchesScreen(coachItemAt(index), screen)) {
      previousIndex = index;
      return true;
    }
  }
  return false;
}

uint16_t matchingCoachItemCount(Screen screen) {
  uint16_t total = 0;
  for (size_t index = 0; index < gCoachItemCount; ++index) {
    if (itemMatchesScreen(coachItemAt(index), screen)) {
      ++total;
    }
  }
  return total;
}

void filteredItemPosition(Screen screen, size_t currentIndex, uint16_t& position, uint16_t& total) {
  position = 0;
  total = 0;
  for (size_t index = 0; index < gCoachItemCount; ++index) {
    if (!itemMatchesScreen(coachItemAt(index), screen)) {
      continue;
    }
    ++total;
    if (index == currentIndex) {
      position = total;
    }
  }
  if (position == 0 && total > 0) {
    position = 1;
  }
}

bool hasNextCoachItem() {
  return matchingCoachItemCount(gScreen) > 0;
}

bool hasPreviousCoachItem() {
  return matchingCoachItemCount(gScreen) > 0;
}

void startCoachMode(Screen screen) {
  gScreen = screen;
  gCoachIndex = findCoachItem(screen, 0);
  gCoachStage = 0;
  gSelectedOption = -1;
  gMockStep = 0;
  gCoachNeedsCleanEntryRefresh = true;
  Serial.printf("Coach mode entry: screen=%s cleanRefresh=queued\n", screenName(screen));
}

void startPracticeMode(bool mustOnly, bool continueLast) {
  gInterviewMustMasterOnly = mustOnly;
  gScreen = Screen::InterviewPractice;
  size_t startIndex = 0;
  if (continueLast && gHasPracticeLastIndex && gLastPracticeIndex < gCoachItemCount &&
      itemMatchesScreen(coachItemAt(gLastPracticeIndex), Screen::InterviewPractice)) {
    startIndex = gLastPracticeIndex;
  }
  gCoachIndex = findCoachItem(Screen::InterviewPractice, startIndex);
  gCoachStage = 0;
  gSelectedOption = -1;
  gMockStep = 0;
  gCoachNeedsCleanEntryRefresh = true;
  Serial.printf("Practice entry: filter=%s continue=%s index=%u cleanRefresh=queued\n", mustOnly ? "Must" : "All",
                continueLast ? "yes" : "no", static_cast<unsigned>(gCoachIndex));
}

void nextCoachItem() {
  if (gCoachItemCount == 0) {
    return;
  }
  size_t nextIndex = 0;
  if (!findNextCoachItem(gScreen, gCoachIndex, nextIndex)) {
    Serial.printf("Coach next item unavailable: screen=%s index=%u\n", screenName(gScreen),
                  static_cast<unsigned>(gCoachIndex));
    return;
  }
  const bool leavingFeedback = gSelectedOption >= 0;
  gCoachIndex = nextIndex;
  gCoachStage = 0;
  gSelectedOption = -1;
  if (leavingFeedback) {
    gCoachNeedsCleanEntryRefresh = true;
  }
  if (gScreen == Screen::MockInterview) {
    gMockStep = (gMockStep + 1) % 5;
  }
}

void previousCoachItem() {
  if (gCoachItemCount == 0) {
    return;
  }
  size_t previousIndex = 0;
  if (!findPreviousCoachItem(gScreen, gCoachIndex, previousIndex)) {
    Serial.printf("Coach previous item unavailable: screen=%s index=%u\n", screenName(gScreen),
                  static_cast<unsigned>(gCoachIndex));
    return;
  }
  const bool leavingFeedback = gSelectedOption >= 0;
  gCoachIndex = previousIndex;
  gCoachStage = 0;
  gSelectedOption = -1;
  if (leavingFeedback) {
    gCoachNeedsCleanEntryRefresh = true;
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

void lockInputForRefresh(const char* reason, bool cleanRefresh) {
  gInputLocked = true;
  gCurrentRefreshClean = cleanRefresh;
  gLastRefreshReason = reason && reason[0] != '\0' ? reason : "render";
  gInputUnlockAtMs = 0;
  Serial.printf("UI refresh start: screen=%s font=%s style=%s contrast=%s settingRefresh=%s actualRefresh=%s "
                "input=locked reason=%s\n",
                screenName(gScreen), fontSizeModeName(), fontStyleModeName(), contrastModeName(), refreshModeName(),
                cleanRefresh ? "Clean" : "Fast", gLastRefreshReason.c_str());
}

void finishDisplayRefresh() {
  M5.Display.display();
  ++gDisplayRefreshCount;
  const uint32_t debounceMs = gCurrentRefreshClean ? kInputCleanRefreshDebounceMs : kInputDebounceMs;
  gLastRefreshEndMs = millis();
  gLastDebounceMs = debounceMs;
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
    Serial.printf("Refresh requested: %s\n", reason);
  }
  const String renderReason = reason && reason[0] != '\0' ? String(reason) : String("render");
  if (gScreen == gLastRenderedScreen && renderReason == gLastRenderedReason) {
    ++gRepeatedRenderCount;
    if (gRepeatedRenderCount >= 3) {
      Serial.printf("Power redraw warning: repeated render screen=%s reason=%s count=%u no state change detected\n",
                    screenName(gScreen), renderReason.c_str(), static_cast<unsigned>(gRepeatedRenderCount));
    }
  } else {
    gLastRenderedScreen = gScreen;
    gLastRenderedReason = renderReason;
    gRepeatedRenderCount = 0;
  }
  bool hardCleanTriggered = false;
  const bool cleanRefresh = chooseRefreshClean(reason, highQuality, hardCleanTriggered);
  lockInputForRefresh(reason, cleanRefresh);
  display.setEpdMode(cleanRefresh ? m5gfx::epd_quality : m5gfx::epd_fastest);
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextWrap(false, false);
}

void prepareCoachContentRefresh(const char* reason = "content page") {
  const bool cleanEntry = gCoachNeedsCleanEntryRefresh;
  prepareFullRefresh(cleanEntry ? "content entry clean" : reason, cleanEntry);
  if (cleanEntry) {
    Serial.printf("Content entry clean refresh consumed: screen=%s\n", screenName(gScreen));
  }
  gCoachNeedsCleanEntryRefresh = false;
}

void cleanWhiteRefresh(const char* reason) {
  auto& display = M5.Display;
  Serial.printf("Clean white refresh: %s\n", reason);
  noteForcedCleanRefresh(reason);
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
  ++gBadgeRedrawCount;
  Serial.printf("Badge mode: language=%s languageMode=%s autoInterval=%s source=%s orientation=%s refreshReason=%s "
                "redrawCount=%u\n",
                languageName(gBadgeLanguage), languageModeName(), autoRotateIntervalName(), gLastBadgeSource.c_str(),
                orientationModeName(), refreshReason && refreshReason[0] != '\0' ? refreshReason : "initial",
                static_cast<unsigned>(gBadgeRedrawCount));
}

void drawCheckMark(int32_t x, int32_t y, int32_t size) {
  auto& display = M5.Display;
  const int32_t t = size >= 28 ? 3 : 2;
  for (int32_t offset = 0; offset < t; ++offset) {
    display.drawLine(x, y + size / 2 + offset, x + size / 3, y + size - 2 + offset, TFT_BLACK);
    display.drawLine(x + size / 3, y + size - 2 + offset, x + size, y + offset, TFT_BLACK);
  }
}

void drawThickLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t thickness) {
  auto& display = M5.Display;
  const int32_t half = thickness / 2;
  for (int32_t offset = -half; offset <= half; ++offset) {
    display.drawLine(x0 + offset, y0, x1 + offset, y1, TFT_BLACK);
    display.drawLine(x0, y0 + offset, x1, y1 + offset, TFT_BLACK);
  }
}

void drawIcon(IconType icon, int32_t x, int32_t y, int32_t size) {
  auto& display = M5.Display;
  const int32_t s = size;
  const int32_t t = s >= 36 ? 4 : 3;
  switch (icon) {
    case IconType::Home:
      display.fillTriangle(x + s / 2, y + 1, x + 2, y + s / 2, x + s - 2, y + s / 2, TFT_BLACK);
      display.fillRect(x + s / 5, y + s / 2, (s * 3) / 5, s / 2 - 2, TFT_BLACK);
      display.fillRect(x + s / 2 - s / 10, y + (s * 2) / 3, s / 5, s / 3 - 2, TFT_WHITE);
      break;
    case IconType::Back:
      display.fillTriangle(x + 3, y + s / 2, x + s / 2, y + 4, x + s / 2, y + s - 4, TFT_BLACK);
      display.fillRect(x + s / 2 - 1, y + s / 3, s / 2 - 3, s / 3, TFT_BLACK);
      break;
    case IconType::Next:
      display.fillTriangle(x + s - 3, y + s / 2, x + s / 2, y + 4, x + s / 2, y + s - 4, TFT_BLACK);
      display.fillRect(x + 3, y + s / 3, s / 2, s / 3, TFT_BLACK);
      break;
    case IconType::Badge:
      display.drawRoundRect(x + 2, y + 5, s - 4, s - 10, 4, TFT_BLACK);
      display.drawRoundRect(x + 3, y + 6, s - 6, s - 12, 4, TFT_BLACK);
      display.fillCircle(x + s / 3, y + s / 2, s / 8, TFT_BLACK);
      display.fillRect(x + s / 2, y + s / 3, s / 3, t, TFT_BLACK);
      display.fillRect(x + s / 2, y + s / 2, s / 3, t, TFT_BLACK);
      break;
    case IconType::Practice:
      display.drawRect(x + 3, y + 6, s / 2 - 3, s - 12, TFT_BLACK);
      display.drawRect(x + s / 2, y + 6, s / 2 - 4, s - 12, TFT_BLACK);
      display.fillRect(x + s / 2 - 1, y + 6, 3, s - 12, TFT_BLACK);
      display.fillRect(x + 8, y + 15, s / 2 - 13, t, TFT_BLACK);
      display.fillRect(x + s / 2 + 7, y + 15, s / 2 - 14, t, TFT_BLACK);
      display.fillRect(x + 8, y + 24, s / 2 - 13, t, TFT_BLACK);
      break;
    case IconType::Drills:
      display.fillTriangle(x + s / 2, y + 2, x + s / 4, y + s / 2, x + s / 2, y + s / 2, TFT_BLACK);
      display.fillTriangle(x + s / 2, y + s / 2, x + s / 3, y + s - 2, x + (s * 3) / 4, y + s / 3, TFT_BLACK);
      drawCheckMark(x + s / 2, y + s / 2, s / 2);
      break;
    case IconType::Exam:
      display.drawRoundRect(x + 5, y + 5, s - 10, s - 8, 4, TFT_BLACK);
      display.drawRoundRect(x + 6, y + 6, s - 12, s - 10, 4, TFT_BLACK);
      display.fillRect(x + s / 3, y + 2, s / 3, s / 7, TFT_BLACK);
      display.fillRect(x + s / 4, y + s / 3, s / 2, t, TFT_BLACK);
      drawCheckMark(x + s / 4, y + s / 2, s / 2);
      break;
    case IconType::Glossary:
      display.drawRect(x + 3, y + 6, s - 14, s - 12, TFT_BLACK);
      display.fillRect(x + s / 2 - 1, y + 6, 3, s - 12, TFT_BLACK);
      display.fillRect(x + 8, y + 15, s / 3, t, TFT_BLACK);
      display.drawCircle(x + s - 10, y + s - 10, s / 5, TFT_BLACK);
      display.drawCircle(x + s - 10, y + s - 10, s / 5 - 1, TFT_BLACK);
      drawThickLine(x + s - 5, y + s - 5, x + s, y + s, t);
      break;
    case IconType::Results:
      display.drawLine(x + 3, y + s - 4, x + s - 3, y + s - 4, TFT_BLACK);
      display.fillRect(x + 8, y + s / 2, s / 5, s / 2 - 4, TFT_BLACK);
      display.fillRect(x + s / 2 - s / 10, y + s / 3, s / 5, (s * 2) / 3 - 4, TFT_BLACK);
      display.fillRect(x + s - s / 4, y + s / 5, s / 5, (s * 4) / 5 - 4, TFT_BLACK);
      break;
    case IconType::Settings:
      display.fillCircle(x + s / 2, y + s / 2, s / 4, TFT_BLACK);
      display.fillCircle(x + s / 2, y + s / 2, s / 10, TFT_WHITE);
      display.fillRect(x + s / 2 - t / 2, y + 2, t, s / 5, TFT_BLACK);
      display.fillRect(x + s / 2 - t / 2, y + (s * 4) / 5, t, s / 5 - 2, TFT_BLACK);
      display.fillRect(x + 2, y + s / 2 - t / 2, s / 5, t, TFT_BLACK);
      display.fillRect(x + (s * 4) / 5, y + s / 2 - t / 2, s / 5 - 2, t, TFT_BLACK);
      break;
    case IconType::Debug:
      drawThickLine(x + s / 5, y + (s * 4) / 5, x + (s * 4) / 5, y + s / 5, t);
      display.fillCircle(x + (s * 4) / 5, y + s / 5, s / 7, TFT_BLACK);
      display.fillCircle(x + (s * 4) / 5, y + s / 5, s / 14, TFT_WHITE);
      drawThickLine(x + s / 5, y + (s * 4) / 5, x + s / 10, y + s - 2, t);
      drawThickLine(x + s / 5, y + (s * 4) / 5, x + s / 3, y + s - 2, t);
      break;
    case IconType::None:
    default:
      break;
  }
}

void drawButton(const Rect& rect, const char* label, IconType icon = IconType::None,
                ButtonTextAlign align = ButtonTextAlign::Center) {
  auto& display = M5.Display;
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
  applyCoachButtonFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);

  const String labelText = label ? String(label) : String("");
  if (icon == IconType::None && labelText == "Home") {
    icon = IconType::Home;
  }
  const bool hasIcon = icon != IconType::None;
  const bool iconOnly = hasIcon && labelText.length() == 0;
  const int32_t iconSize = rect.w < 160 ? 30 : (rect.h > 74 ? 42 : 34);
  if (iconOnly) {
    drawIcon(icon, rect.x + (rect.w - iconSize) / 2, rect.y + (rect.h - iconSize) / 2, iconSize);
    logLayoutBox("button", Rect(rect.x, rect.y, rect.w, rect.h), iconSize, 1, false);
    return;
  }

  const int32_t iconGap = hasIcon ? (rect.w < 180 ? 12 : 16) : 0;
  const int32_t maxTextW = rect.w - 28 - (hasIcon ? iconSize + iconGap : 0);
  const int32_t innerW = maxTextW > 24 ? maxTextW : rect.w - 24;
  const int32_t lineHeight = static_cast<int32_t>(coachTypography().buttonPx) + 8;
  String lines[2];
  TextLayoutResult result = wrapTextToLines(labelText, innerW, lineHeight, 2, lines);
  const int32_t centeredOffset = (rect.h - result.height) / 2;
  const int32_t textY = rect.y + (centeredOffset > 6 ? centeredOffset : 6);
  int32_t maxLineW = 0;
  for (uint8_t line = 0; line < result.lineCount; ++line) {
    const int32_t textW = display.textWidth(lines[line]);
    if (textW > maxLineW) {
      maxLineW = textW;
    }
  }

  const int32_t groupW = maxLineW + (hasIcon ? iconSize + iconGap : 0);
  const int32_t groupX = align == ButtonTextAlign::Left ? rect.x + 22 : rect.x + (rect.w - groupW) / 2;
  const int32_t innerX = hasIcon ? groupX + iconSize + iconGap
                                 : (align == ButtonTextAlign::Left ? rect.x + 24 : rect.x + (rect.w - maxLineW) / 2);
  if (hasIcon) {
    drawIcon(icon, groupX, rect.y + (rect.h - iconSize) / 2, iconSize);
  }
  for (uint8_t line = 0; line < result.lineCount; ++line) {
    const int32_t textW = display.textWidth(lines[line]);
    const int32_t lineX = align == ButtonTextAlign::Left ? innerX : innerX + (maxLineW - textW) / 2;
    display.drawString(lines[line], lineX, textY + line * lineHeight);
  }
  logLayoutBox("button", Rect(groupX, rect.y + 4, groupW, rect.h - 8), result.height, 1, result.overflow);
}

void drawButton(const Rect& rect, const String& label, IconType icon = IconType::None) {
  drawButton(rect, label.c_str(), icon);
}

void drawButton(const Rect& rect, const String& label, IconType icon, ButtonTextAlign align) {
  drawButton(rect, label.c_str(), icon, align);
}

void drawCoachFooterNav(bool previousEnabled, bool nextEnabled) {
  auto& display = M5.Display;
  (void)previousEnabled;
  (void)nextEnabled;
  const int32_t footerY = display.height() - 76;
  const int32_t buttonH = 58;
  const int32_t sideW = 132;
  const int32_t homeW = 108;
  gFilterButton = {24, footerY, sideW, buttonH};
  gHomeButton = {(display.width() - homeW) / 2, footerY, homeW, buttonH};
  gNextButton = {display.width() - sideW - 24, footerY, sideW, buttonH};

  drawButton(gFilterButton, "", IconType::Back);
  drawButton(gHomeButton, "", IconType::Home);
  drawButton(gNextButton, "", IconType::Next);
}

void drawCenteredHomeFooter() {
  auto& display = M5.Display;
  const int32_t footerY = display.height() - 76;
  const int32_t buttonH = 58;
  const int32_t homeW = 132;
  gFilterButton = {};
  gHomeButton = {(display.width() - homeW) / 2, footerY, homeW, buttonH};
  gNextButton = {};
  drawButton(gHomeButton, "", IconType::Home);
}

bool isCoachScreen(Screen screen) {
  return screen == Screen::InterviewPractice || screen == Screen::Drills || screen == Screen::BlitzQuiz ||
         screen == Screen::WeakAnswerDetector || screen == Screen::MetricPrecision || screen == Screen::HostileFollowup ||
         screen == Screen::Glossary || screen == Screen::MockInterview;
}

const char* coachScreenTitle(Screen screen) {
  switch (screen) {
    case Screen::Drills:
      return drillCategoryName(gDrillCategory);
    case Screen::BlitzQuiz:
      return "Blitz Quiz";
    case Screen::WeakAnswerDetector:
      return "Weak Answer Detector";
    case Screen::MetricPrecision:
      return "Metric Precision";
    case Screen::HostileFollowup:
      return "Hostile Follow-up";
    case Screen::GlossaryMenu:
    case Screen::Glossary:
      return "Glossary";
    case Screen::MockInterview:
      return "Mock Interview";
    case Screen::InterviewPractice:
    default:
      return "Practice";
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
    case Screen::PowerAudit:
      return "Power Audit";
    case Screen::FontLab:
      return "Font Lab";
    case Screen::VisualQa:
      return "Visual QA";
    case Screen::HelpLegend:
      return "Help";
    case Screen::QrZoom:
      return "QR zoom";
    case Screen::PhotoZoom:
      return "Photo zoom";
    case Screen::PracticeMenu:
      return "Practice";
    case Screen::DrillsMenu:
      return "Drills";
    case Screen::GlossaryMenu:
      return "Glossary";
    case Screen::Exam:
      return "Exam";
    case Screen::Results:
      return "Results";
    case Screen::InterviewPractice:
    case Screen::Drills:
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
  switch (canonicalFontSizeMode(gSettings.fontSizeMode)) {
    case FontSizeMode::Medium:
      type.titlePx = 31;
      type.metadataPx = 24;
      type.footerPx = 20;
      type.bodyPx = 24;
      type.buttonPx = 24;
      type.bodyLineHeight = 34;
      type.metadataLineHeight = 30;
      type.footerLineHeight = 26;
      type.buttonHeight = 64;
      type.promptLines = 9;
      type.answerLines = 7;
      type.rubricLines = 5;
      type.charsPerPage = 560;
      break;
    case FontSizeMode::XL:
      type.titlePx = 40;
      type.metadataPx = 24;
      type.footerPx = 24;
      type.bodyPx = 40;
      type.buttonPx = 31;
      type.bodyLineHeight = 54;
      type.metadataLineHeight = 34;
      type.footerLineHeight = 32;
      type.buttonHeight = 82;
      type.promptLines = 5;
      type.answerLines = 4;
      type.rubricLines = 3;
      type.charsPerPage = 260;
      break;
    case FontSizeMode::Large:
    default:
      type.titlePx = 40;
      type.metadataPx = 24;
      type.footerPx = 20;
      type.bodyPx = 31;
      type.buttonPx = 24;
      type.bodyLineHeight = 44;
      type.metadataLineHeight = 32;
      type.footerLineHeight = 28;
      type.buttonHeight = 72;
      type.promptLines = 8;
      type.answerLines = 6;
      type.rubricLines = 4;
      type.charsPerPage = 380;
      break;
  }

  if (gSettings.fontStyleMode == FontStyleMode::LargeReader) {
    type.buttonHeight += 4;
    type.charsPerPage = (type.charsPerPage * 82) / 100;
  } else if (gSettings.fontStyleMode == FontStyleMode::SansBoldLike) {
    type.buttonHeight += 3;
    type.charsPerPage = (type.charsPerPage * 88) / 100;
  } else if (gSettings.fontStyleMode == FontStyleMode::HighContrast) {
    type.buttonHeight += 6;
    type.charsPerPage = (type.charsPerPage * 78) / 100;
  } else if (gSettings.fontStyleMode == FontStyleMode::DebugMono) {
    type.buttonPx = canonicalFontSizeMode(gSettings.fontSizeMode) == FontSizeMode::XL ? 24 : type.buttonPx;
    type.bodyLineHeight += 4;
    type.buttonHeight += 4;
    type.charsPerPage = (type.charsPerPage * 72) / 100;
  }

  switch (gSettings.lineSpacingMode) {
    case LineSpacingMode::Normal:
      type.bodyLineHeight += 5;
      type.metadataLineHeight += 3;
      type.footerLineHeight += 3;
      break;
    case LineSpacingMode::Loose:
      type.bodyLineHeight += 10;
      type.metadataLineHeight += 6;
      type.footerLineHeight += 6;
      break;
    case LineSpacingMode::Tight:
    default:
      break;
  }

  return type;
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

void applySansBoldFont(uint8_t px) {
  auto& display = M5.Display;
  if (px >= 40) {
    display.setFont(&fonts::FreeSansBold24pt7b);
  } else if (px >= 31) {
    display.setFont(&fonts::FreeSansBold18pt7b);
  } else if (px >= 24) {
    display.setFont(&fonts::FreeSansBold12pt7b);
  } else {
    display.setFont(&fonts::FreeSansBold9pt7b);
  }
  display.setTextSize(1);
}

void applyMonoBoldFont(uint8_t px) {
  auto& display = M5.Display;
  if (px >= 34) {
    display.setFont(&fonts::FreeMonoBold18pt7b);
  } else if (px >= 24) {
    display.setFont(&fonts::FreeMonoBold12pt7b);
  } else {
    display.setFont(&fonts::FreeMonoBold9pt7b);
  }
  display.setTextSize(1);
}

void applyTypographyFont(uint8_t px) {
  if (gSettings.fontStyleMode == FontStyleMode::DebugMono) {
    applyMonoBoldFont(px);
    return;
  }
  if (gSettings.fontStyleMode == FontStyleMode::LargeReader ||
      gSettings.fontStyleMode == FontStyleMode::SansBoldLike ||
      gSettings.fontStyleMode == FontStyleMode::HighContrast) {
    applySansBoldFont(px);
    return;
  }
  applyGothicFont(px);
}

uint16_t metadataTextColor() {
  auto& display = M5.Display;
  if (gSettings.contrastMode == ContrastMode::Max || gSettings.fontStyleMode == FontStyleMode::HighContrast) {
    return TFT_BLACK;
  }
  if (gSettings.contrastMode == ContrastMode::Dark) {
    return display.color565(28, 28, 28);
  }
  return display.color565(55, 55, 55);
}

void applyCoachTitleFont() {
  applyTypographyFont(coachTypography().titlePx);
}

void applyCoachMetadataFont() {
  applyTypographyFont(coachTypography().metadataPx);
}

void applyCoachContentFont() {
  applyTypographyFont(coachTypography().bodyPx);
}

void applyCoachButtonFont() {
  applyTypographyFont(coachTypography().buttonPx);
}

void applyCoachFooterFont() {
  applyTypographyFont(coachTypography().footerPx);
}

const char* fontTypeName(lgfx::IFont::font_type_t type) {
  switch (type) {
    case lgfx::IFont::ft_glcd:
      return "GLCD bitmap";
    case lgfx::IFont::ft_bmp:
      return "BMP bitmap";
    case lgfx::IFont::ft_rle:
      return "RLE bitmap";
    case lgfx::IFont::ft_gfx:
      return "GFX bitmap/discrete";
    case lgfx::IFont::ft_bdf:
      return "BDF bitmap/discrete";
    case lgfx::IFont::ft_vlw:
      return "VLW runtime font";
    case lgfx::IFont::ft_u8g2:
      return "U8G2 bitmap";
    case lgfx::IFont::ft_lvgl:
      return "LVGL font";
    case lgfx::IFont::ft_ttf:
      return "TTF scalable";
    case lgfx::IFont::ft_unknown:
    default:
      return "unknown";
  }
}

const char* selectedTypographyFontName(FontStyleMode style, uint8_t px) {
  if (style == FontStyleMode::DebugMono) {
    if (px >= 34) {
      return "FreeMonoBold18pt7b";
    }
    if (px >= 24) {
      return "FreeMonoBold12pt7b";
    }
    return "FreeMonoBold9pt7b";
  }
  if (style == FontStyleMode::LargeReader || style == FontStyleMode::SansBoldLike ||
      style == FontStyleMode::HighContrast) {
    if (px >= 40) {
      return "FreeSansBold24pt7b";
    }
    if (px >= 31) {
      return "FreeSansBold18pt7b";
    }
    if (px >= 24) {
      return "FreeSansBold12pt7b";
    }
    return "FreeSansBold9pt7b";
  }
  if (px >= 36) {
    return "lgfxJapanGothic_36";
  }
  if (px >= 32) {
    return "lgfxJapanGothic_32";
  }
  if (px >= 28) {
    return "lgfxJapanGothic_28";
  }
  if (px >= 24) {
    return "lgfxJapanGothic_24";
  }
  return "lgfxJapanGothic_20";
}

struct FontProbe {
  FontSizeMode requestedSize = FontSizeMode::Large;
  FontSizeMode effectiveSize = FontSizeMode::Large;
  int32_t width = 0;
  int32_t height = 0;
  int32_t lineHeight = 0;
  uint8_t bodyPx = 0;
  const char* fontName = "";
  const char* fontType = "";
};

FontProbe measureTypography(FontStyleMode style, FontSizeMode size, const char* sample) {
  auto& display = M5.Display;
  const FontStyleMode savedStyle = gSettings.fontStyleMode;
  const FontSizeMode savedSize = gSettings.fontSizeMode;

  gSettings.fontStyleMode = style;
  gSettings.fontSizeMode = size;
  const CoachTypography type = coachTypography();
  applyCoachContentFont();

  FontProbe probe;
  probe.requestedSize = size;
  probe.effectiveSize = canonicalFontSizeMode(size);
  probe.width = display.textWidth(sample);
  probe.height = display.fontHeight();
  probe.lineHeight = type.bodyLineHeight;
  probe.bodyPx = type.bodyPx;
  probe.fontName = selectedTypographyFontName(style, type.bodyPx);
  probe.fontType = display.getFont() != nullptr ? fontTypeName(display.getFont()->getType()) : "none";

  gSettings.fontStyleMode = savedStyle;
  gSettings.fontSizeMode = savedSize;
  return probe;
}

bool fontProbeCollides(const FontProbe& lhs, const FontProbe& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && strcmp(lhs.fontName, rhs.fontName) == 0;
}

void logFontEngineDiagnostics(const char* reason) {
  static constexpr FontSizeMode kProbeSizes[] = {
      FontSizeMode::Medium, FontSizeMode::Large, FontSizeMode::XL, FontSizeMode::XXL, FontSizeMode::Huge};
  static constexpr const char* kSample = "Define impact without overclaiming causality";
  FontProbe probes[countOf(kProbeSizes)];
  Serial.printf("Font engine: reason=%s activeStyle=%s activeSize=%s lineSpacing=%s sample=\"%s\"\n",
                reason && reason[0] != '\0' ? reason : "diagnostic", fontStyleModeName(), fontSizeModeName(),
                lineSpacingModeName(), kSample);
  for (size_t i = 0; i < countOf(kProbeSizes); ++i) {
    probes[i] = measureTypography(gSettings.fontStyleMode, kProbeSizes[i], kSample);
    const char* collision = "none";
    for (size_t previous = 0; previous < i; ++previous) {
      if (fontProbeCollides(probes[i], probes[previous])) {
        collision = shortFontSizeModeName(kProbeSizes[previous]);
        break;
      }
    }
    Serial.printf("Font probe: request=%s effective=%s font=%s type=%s technology=bitmap/discrete bodyPx=%u "
                  "height=%ld width=%ld lineHeight=%ld collision=%s\n",
                  shortFontSizeModeName(kProbeSizes[i]), shortFontSizeModeName(probes[i].effectiveSize),
                  probes[i].fontName, probes[i].fontType, static_cast<unsigned>(probes[i].bodyPx),
                  static_cast<long>(probes[i].height), static_cast<long>(probes[i].width),
                  static_cast<long>(probes[i].lineHeight), collision);
  }
  Serial.printf("VLW probe: feasible=yes path=%s status=%s flashImpact=0 unless an asset is embedded\n", kReaderVlwPath,
                gSdMounted ? "SD available; Font Lab can try loadFont(SD,path)" : "SD not mounted");
  Serial.printf("Reader Mid probe: path=%s found=%s fallback=%s source=SD-only\n", kReaderMidVlwPath,
                gReaderMidVlwAvailable ? "yes" : "no", gReaderMidVlwAvailable ? "no" : "yes");
}

int32_t coachLineHeight() {
  return coachTypography().bodyLineHeight;
}

void logTypographySettings(const char* reason) {
  const CoachTypography type = coachTypography();
  FontProbe probe = measureTypography(gSettings.fontStyleMode, gSettings.fontSizeMode,
                                      "Define impact without overclaiming causality");
  Serial.printf("Typography: reason=%s style=%s size=%s contrast=%s lineSpacing=%s titlePx=%u bodyPx=%u buttonPx=%u "
                "font=%s type=%s actualHeight=%ld sampleWidth=%ld lineHeight=%ld buttonHeight=%ld\n",
                reason && reason[0] != '\0' ? reason : "render", fontStyleModeName(), fontSizeModeName(),
                contrastModeName(), lineSpacingModeName(), static_cast<unsigned>(type.titlePx),
                static_cast<unsigned>(type.bodyPx), static_cast<unsigned>(type.buttonPx), probe.fontName,
                probe.fontType, static_cast<long>(probe.height), static_cast<long>(probe.width),
                static_cast<long>(type.bodyLineHeight), static_cast<long>(type.buttonHeight));
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

bool isBadgeCenterTap(int32_t x, int32_t y) {
  return x > M5.Display.width() / 4 && x < (M5.Display.width() * 3) / 4 && y > M5.Display.height() / 3 &&
         y < (M5.Display.height() * 2) / 3;
}

bool rectContainsExpanded(const Rect& rect, int32_t px, int32_t py, int32_t padding = kHitboxPadding) {
  if (rect.w <= 0 || rect.h <= 0) {
    return false;
  }
  return px >= rect.x - padding && px <= rect.x + rect.w + padding && py >= rect.y - padding &&
         py <= rect.y + rect.h + padding;
}

bool hitTarget(const Rect& rect, const char* target, int32_t px, int32_t py, int32_t padding = kHitboxPadding) {
  if (!rectContainsExpanded(rect, px, py, padding)) {
    return false;
  }
  gHitMatchedThisTap = true;
  gLastHitTarget = target && target[0] != '\0' ? target : "unnamed";
  Serial.printf("hitbox matched: target=%s x=%ld y=%ld visible=(%ld,%ld,%ld,%ld) pad=%ld screen=%s\n",
                gLastHitTarget.c_str(), static_cast<long>(px), static_cast<long>(py), static_cast<long>(rect.x),
                static_cast<long>(rect.y), static_cast<long>(rect.w), static_cast<long>(rect.h),
                static_cast<long>(padding), screenName(gScreen));
  return true;
}

void markHitTarget(const char* target, int32_t px, int32_t py) {
  gHitMatchedThisTap = true;
  gLastHitTarget = target && target[0] != '\0' ? target : "unnamed";
  Serial.printf("hitbox matched: target=%s x=%ld y=%ld screen=%s\n", gLastHitTarget.c_str(), static_cast<long>(px),
                static_cast<long>(py), screenName(gScreen));
}

void noteIgnoredIfNoHit(int32_t px, int32_t py) {
  if (gHitMatchedThisTap) {
    return;
  }
  gLastIgnoredTouchReason = "no hit target";
  Serial.printf("touch ignored: reason=no hit target x=%ld y=%ld screen=%s\n", static_cast<long>(px),
                static_cast<long>(py), screenName(gScreen));
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

void copyToBuffer(char* dest, size_t destSize, const char* source) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  const char* safeSource = source != nullptr ? source : "";
  strncpy(dest, safeSource, destSize - 1);
  dest[destSize - 1] = '\0';
}

void printJsonEscaped(File& file, const char* text) {
  file.print('"');
  const char* safeText = text != nullptr ? text : "";
  for (const char* cursor = safeText; *cursor != '\0'; ++cursor) {
    const char ch = *cursor;
    if (ch == '"' || ch == '\\') {
      file.print('\\');
      file.print(ch);
    } else if (ch == '\n') {
      file.print("\\n");
    } else if (ch == '\r') {
      file.print("\\r");
    } else if (ch == '\t') {
      file.print("\\t");
    } else {
      file.print(ch);
    }
  }
  file.print('"');
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

bool matchesBytes(const String& text, size_t index, const uint8_t* bytes, size_t length) {
  if (index + length > text.length()) {
    return false;
  }
  for (size_t offset = 0; offset < length; ++offset) {
    if (static_cast<uint8_t>(text[index + offset]) != bytes[offset]) {
      return false;
    }
  }
  return true;
}

uint8_t utf8SequenceLength(uint8_t firstByte) {
  if ((firstByte & 0x80) == 0) {
    return 1;
  }
  if ((firstByte & 0xE0) == 0xC0) {
    return 2;
  }
  if ((firstByte & 0xF0) == 0xE0) {
    return 3;
  }
  if ((firstByte & 0xF8) == 0xF0) {
    return 4;
  }
  return 1;
}

String sanitizeCoachText(const String& text, const char* field = nullptr) {
  static constexpr uint8_t kEmDash[] = {0xE2, 0x80, 0x94};
  static constexpr uint8_t kEnDash[] = {0xE2, 0x80, 0x93};
  static constexpr uint8_t kLeftDoubleQuote[] = {0xE2, 0x80, 0x9C};
  static constexpr uint8_t kRightDoubleQuote[] = {0xE2, 0x80, 0x9D};
  static constexpr uint8_t kLeftSingleQuote[] = {0xE2, 0x80, 0x98};
  static constexpr uint8_t kRightSingleQuote[] = {0xE2, 0x80, 0x99};
  static constexpr uint8_t kBullet[] = {0xE2, 0x80, 0xA2};
  static constexpr uint8_t kEllipsis[] = {0xE2, 0x80, 0xA6};
  static constexpr uint8_t kNbsp[] = {0xC2, 0xA0};
  static constexpr uint8_t kNarrowNbsp[] = {0xE2, 0x80, 0xAF};
  static constexpr uint8_t kThinSpace[] = {0xE2, 0x80, 0x89};
  static constexpr uint8_t kSoftHyphen[] = {0xC2, 0xAD};
  static constexpr uint8_t kMinusSign[] = {0xE2, 0x88, 0x92};
  static constexpr uint8_t kRightArrow[] = {0xE2, 0x86, 0x92};
  static constexpr uint8_t kMiddleDot[] = {0xC2, 0xB7};

  String sanitized;
  sanitized.reserve(text.length());
  uint32_t replacements = 0;

  for (size_t index = 0; index < text.length();) {
    const uint8_t ch = static_cast<uint8_t>(text[index]);
    const char* replacement = nullptr;
    size_t consumed = 0;

    if (matchesBytes(text, index, kEmDash, sizeof(kEmDash)) ||
        matchesBytes(text, index, kEnDash, sizeof(kEnDash)) ||
        matchesBytes(text, index, kSoftHyphen, sizeof(kSoftHyphen)) ||
        matchesBytes(text, index, kMinusSign, sizeof(kMinusSign))) {
      replacement = "-";
      consumed = matchesBytes(text, index, kSoftHyphen, sizeof(kSoftHyphen)) ? sizeof(kSoftHyphen) : 3;
    } else if (matchesBytes(text, index, kLeftDoubleQuote, sizeof(kLeftDoubleQuote)) ||
               matchesBytes(text, index, kRightDoubleQuote, sizeof(kRightDoubleQuote))) {
      replacement = "\"";
      consumed = 3;
    } else if (matchesBytes(text, index, kLeftSingleQuote, sizeof(kLeftSingleQuote)) ||
               matchesBytes(text, index, kRightSingleQuote, sizeof(kRightSingleQuote))) {
      replacement = "'";
      consumed = 3;
    } else if (matchesBytes(text, index, kBullet, sizeof(kBullet)) ||
               matchesBytes(text, index, kMiddleDot, sizeof(kMiddleDot))) {
      replacement = "-";
      consumed = matchesBytes(text, index, kMiddleDot, sizeof(kMiddleDot)) ? sizeof(kMiddleDot) : 3;
    } else if (matchesBytes(text, index, kEllipsis, sizeof(kEllipsis))) {
      replacement = "...";
      consumed = 3;
    } else if (matchesBytes(text, index, kNbsp, sizeof(kNbsp)) ||
               matchesBytes(text, index, kNarrowNbsp, sizeof(kNarrowNbsp)) ||
               matchesBytes(text, index, kThinSpace, sizeof(kThinSpace))) {
      replacement = " ";
      consumed = matchesBytes(text, index, kNbsp, sizeof(kNbsp)) ? sizeof(kNbsp) : 3;
    } else if (matchesBytes(text, index, kRightArrow, sizeof(kRightArrow))) {
      replacement = "->";
      consumed = 3;
    }

    if (replacement != nullptr) {
      sanitized += replacement;
      index += consumed;
      ++replacements;
      continue;
    }

    if (ch < 0x80) {
      sanitized += static_cast<char>(ch);
      ++index;
      continue;
    }

    sanitized += "?";
    index += utf8SequenceLength(ch);
    ++replacements;
  }

  gSanitizerReplacementLast = replacements;
  gSanitizerReplacementTotal += replacements;
  if (replacements > 0) {
    Serial.printf("Sanitizer: field=%s replacements=%u total=%u beforeBytes=%u afterBytes=%u\n",
                  field && field[0] != '\0' ? field : "text", static_cast<unsigned>(replacements),
                  static_cast<unsigned>(gSanitizerReplacementTotal), static_cast<unsigned>(text.length()),
                  static_cast<unsigned>(sanitized.length()));
  }
  return sanitized;
}

TextLayoutResult wrapTextToLines(const String& text, int32_t width, int32_t lineHeight, uint8_t maxLines,
                                 String* lines) {
  auto& display = M5.Display;
  TextLayoutResult result;
  const String safeText = sanitizeCoachText(text, "wrap");
  String line;
  String word;

  for (size_t index = 0; index <= safeText.length(); ++index) {
    const char ch = index < safeText.length() ? safeText[index] : ' ';
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
  Serial.printf("Layout diagnostics: reason=%s screen=%s font=%s style=%s contrast=%s bodyPx=%u buttonPx=%u "
                "refresh=%s size=%dx%d\n",
                reason && reason[0] != '\0' ? reason : "manual", screenName(gScreen), fontSizeModeName(),
                fontStyleModeName(), contrastModeName(), type.bodyPx, type.buttonPx, refreshModeName(), display.width(),
                display.height());
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

void appendReaderLine(std::vector<String>& lines, const String& line) {
  if (line.length() > 0) {
    lines.push_back(line);
  }
}

void appendReaderWrappedWord(std::vector<String>& lines, const String& word, int32_t width) {
  auto& display = M5.Display;
  if (display.textWidth(word) <= width) {
    appendReaderLine(lines, word);
    return;
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
    appendReaderLine(lines, word.substring(start, end));
    start = end;
  }
}

String practicePromptText(const CoachItemView& item) {
  return item.title;
}

void wrapSanitizedReaderTextToLines(const String& text, int32_t width, std::vector<String>& lines) {
  auto& display = M5.Display;
  lines.clear();
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
        appendReaderLine(lines, line);
        line = "";
        if (display.textWidth(word) > width) {
          appendReaderWrappedWord(lines, word, width);
        } else {
          line = word;
        }
      } else if (line.length() == 0 && display.textWidth(word) > width) {
        appendReaderWrappedWord(lines, word, width);
      } else {
        line = candidate;
      }
      word = "";
    }

    if (ch == '\n') {
      appendReaderLine(lines, line);
      line = "";
    }
  }

  appendReaderLine(lines, line);
  if (lines.empty()) {
    lines.push_back("");
  }
}

void wrapReaderTextToLines(const String& rawText, int32_t width, std::vector<String>& lines, const char* field) {
  const String text = sanitizeCoachText(rawText, field);
  wrapSanitizedReaderTextToLines(text, width, lines);
}

uint8_t boundedPageCount(size_t lineCount, uint8_t linesPerPage) {
  const size_t safeLinesPerPage = linesPerPage == 0 ? 1 : linesPerPage;
  size_t pages = (lineCount + safeLinesPerPage - 1) / safeLinesPerPage;
  if (pages == 0) {
    pages = 1;
  }
  if (pages > kMaxReaderPageCount) {
    pages = kMaxReaderPageCount;
  }
  return static_cast<uint8_t>(pages);
}

ReaderPageSet buildReaderPages(const String& rawText, int32_t width, uint8_t linesPerPage, const char* field) {
  ReaderPageSet pages;
  pages.sourceLength = rawText.length();
  pages.linesPerPage = linesPerPage == 0 ? 1 : linesPerPage;
  const String sanitized = sanitizeCoachText(rawText, field);
  pages.sanitizedLength = sanitized.length();
  wrapSanitizedReaderTextToLines(sanitized, width, pages.lines);
  pages.pageCount = boundedPageCount(pages.lines.size(), pages.linesPerPage);
  if (pages.lines.size() > static_cast<size_t>(pages.linesPerPage) * kMaxReaderPageCount) {
    Serial.printf("Reader page cap warning: field=%s wrappedLines=%u visibleCapacity=%u pageCap=%u\n",
                  field && field[0] != '\0' ? field : "text", static_cast<unsigned>(pages.lines.size()),
                  static_cast<unsigned>(pages.linesPerPage * kMaxReaderPageCount),
                  static_cast<unsigned>(kMaxReaderPageCount));
  }
  return pages;
}

String firstCharsForPage(const ReaderPageSet& pages, uint8_t pageIndex, size_t limit = 30) {
  String preview;
  const size_t startLine = static_cast<size_t>(pageIndex) * pages.linesPerPage;
  const size_t endLine = min(startLine + pages.linesPerPage, pages.lines.size());
  for (size_t line = startLine; line < endLine && preview.length() < limit; ++line) {
    if (preview.length() > 0) {
      preview += " ";
    }
    preview += pages.lines[line];
  }
  if (preview.length() > limit) {
    preview = preview.substring(0, limit);
  }
  return preview;
}

String visibleTextForPage(const ReaderPageSet& pages, uint8_t pageIndex, size_t limit = 220) {
  String visible;
  const size_t startLine = static_cast<size_t>(pageIndex) * pages.linesPerPage;
  const size_t endLine = min(startLine + pages.linesPerPage, pages.lines.size());
  for (size_t line = startLine; line < endLine; ++line) {
    if (visible.length() > 0) {
      visible += " ";
    }
    visible += pages.lines[line];
    if (visible.length() >= limit) {
      visible = visible.substring(0, limit);
      visible += "...";
      break;
    }
  }
  return visible;
}

void logReaderPagination(const char* field, const ReaderPageSet& pages, const PracticeLayout& layout) {
  Serial.printf("Reader pagination: field=%s sourceLen=%u sanitizedLen=%u wrappedLines=%u pages=%u reader=%s "
                "contentH=%ld linesPerPage=%u\n",
                field && field[0] != '\0' ? field : "text", static_cast<unsigned>(pages.sourceLength),
                static_cast<unsigned>(pages.sanitizedLength), static_cast<unsigned>(pages.lines.size()), pages.pageCount,
                shortFontSizeModeName(layout.renderSize), static_cast<long>(layout.contentH), layout.linesPerPage);
  for (uint8_t page = 0; page < pages.pageCount; ++page) {
    Serial.printf("Reader page first30: field=%s page=%u/%u text=\"%s\"\n",
                  field && field[0] != '\0' ? field : "text", page + 1, pages.pageCount,
                  firstCharsForPage(pages, page).c_str());
  }
}

void appendTraceLine(String& trace, const String& line) {
  trace += line;
  trace += "\n";
}

String renderTraceFor(const CoachItemView& item, const char* stageName, const String& rawText,
                      const ReaderPageSet& pages, uint8_t pageIndex, bool splitLayout, const char* warning) {
  String trace;
  trace.reserve(1200 + pages.lines.size() * 72);
  const size_t startLine = static_cast<size_t>(pageIndex) * pages.linesPerPage;
  const size_t endLine = min(startLine + pages.linesPerPage, pages.lines.size());
  const bool capped = pages.lines.size() > static_cast<size_t>(pages.linesPerPage) * kMaxReaderPageCount;
  appendTraceLine(trace, "Render trace");
  appendTraceLine(trace, String("screen/mode: ") + screenName(gScreen));
  appendTraceLine(trace, String("card id: ") + (item.id && item.id[0] != '\0' ? item.id : "(none)"));
  appendTraceLine(trace, String("card/item display id: ") + coachDisplayId(item));
  appendTraceLine(trace, String("section/category: ") +
                             (item.section && item.section[0] != '\0'
                                  ? item.section
                                  : (item.category && item.category[0] != '\0' ? item.category : coachScreenTitle(gScreen))));
  appendTraceLine(trace, String("stage: ") + (stageName && stageName[0] != '\0' ? stageName : "text"));
  appendTraceLine(trace, String("page index: ") + static_cast<unsigned>(pageIndex + 1) + "/" +
                             static_cast<unsigned>(pages.pageCount));
  appendTraceLine(trace, String("actual visible text excerpt: ") + visibleTextForPage(pages, pageIndex));
  appendTraceLine(trace, String("visible line count: ") + static_cast<unsigned>(endLine > startLine ? endLine - startLine : 0));
  appendTraceLine(trace, String("source content line range: wrapped ") + static_cast<unsigned>(startLine + 1) + "-" +
                             static_cast<unsigned>(endLine));
  appendTraceLine(trace, String("font used: ") + selectedTypographyFontName(gSettings.fontStyleMode, coachTypography().bodyPx));
  appendTraceLine(trace, String("reader size: ") + fontSizeModeName());
  appendTraceLine(trace, String("split layout: ") + (splitLayout ? "yes" : "no"));
  appendTraceLine(trace, String("clean refresh forced: ") + (gCurrentRefreshClean ? "yes" : "no"));
  appendTraceLine(trace, String("refresh reason: ") + gLastRefreshReason);
  appendTraceLine(trace, String("content truncated: ") + (capped ? "yes" : "no"));
  if (warning != nullptr && warning[0] != '\0') {
    appendTraceLine(trace, String("warning: ") + warning);
  } else if (capped) {
    appendTraceLine(trace, "warning: wrapped content exceeded maximum page capacity");
  } else {
    appendTraceLine(trace, "warning: none");
  }
  appendTraceLine(trace, String("raw text length: ") + static_cast<unsigned>(rawText.length()));
  appendTraceLine(trace, String("sanitized text length: ") + static_cast<unsigned>(pages.sanitizedLength));
  appendTraceLine(trace, String("wrapped line count: ") + static_cast<unsigned>(pages.lines.size()));
  appendTraceLine(trace, String("page count: ") + static_cast<unsigned>(pages.pageCount));
  appendTraceLine(trace, String("page number: ") + static_cast<unsigned>(pageIndex + 1));
  for (uint8_t page = 0; page < pages.pageCount; ++page) {
    appendTraceLine(trace, String("page ") + static_cast<unsigned>(page + 1) + "/" +
                               static_cast<unsigned>(pages.pageCount));
    const size_t startLine = static_cast<size_t>(page) * pages.linesPerPage;
    const size_t endLine = min(startLine + pages.linesPerPage, pages.lines.size());
    for (size_t line = startLine; line < endLine; ++line) {
      appendTraceLine(trace, String("  line ") + static_cast<unsigned>(line - startLine + 1) + ": " + pages.lines[line]);
    }
  }
  return trace;
}

void recordRenderTrace(const CoachItemView& item, const char* stageName, const String& rawText,
                       const ReaderPageSet& pages, uint8_t pageIndex, bool splitLayout = false,
                       const char* warning = nullptr) {
  gLastRenderTrace = renderTraceFor(item, stageName, rawText, pages, pageIndex, splitLayout, warning);
  Serial.println("Render trace begin");
  Serial.print(gLastRenderTrace);
  Serial.println("Render trace end");
  if (!gSdMounted) {
    return;
  }
  SD.mkdir("/papercoach");
  SD.mkdir("/papercoach/debug");
  File file = SD.open(kRenderTracePath, FILE_APPEND);
  if (!file) {
    gLastRenderTraceStatus = "append failed; Serial only";
    Serial.printf("Render trace append failed path=%s\n", kRenderTracePath);
    return;
  }
  file.print(gLastRenderTrace);
  file.println();
  file.close();
  gLastRenderTraceStatus = String("appended ") + kRenderTracePath;
}

void dumpCurrentRenderTraceToSd() {
  if (gLastRenderTrace.length() == 0) {
    gLastRenderTrace = "Render trace\nNo rendered card/stage trace available yet.\n";
  }

  if (!gSdMounted) {
    gLastRenderTraceStatus = "SD missing; Serial only";
    Serial.println("Render trace dump: SD missing; Serial only.");
    Serial.print(gLastRenderTrace);
    return;
  }

  SD.mkdir("/papercoach");
  SD.mkdir("/papercoach/debug");
  if (SD.exists(kRenderTracePath)) {
    SD.remove(kRenderTracePath);
  }
  File file = SD.open(kRenderTracePath, FILE_WRITE);
  if (!file) {
    gLastRenderTraceStatus = "open failed; Serial only";
    Serial.printf("Render trace dump: open failed path=%s; Serial only.\n", kRenderTracePath);
    Serial.print(gLastRenderTrace);
    return;
  }

  file.print(gLastRenderTrace);
  file.close();
  gLastRenderTraceStatus = String("wrote ") + kRenderTracePath;
  Serial.printf("Render trace dump: wrote %s bytes=%u\n", kRenderTracePath,
                static_cast<unsigned>(gLastRenderTrace.length()));
}

void writeDeckDumpItem(File& file, size_t index) {
  const CoachItemView item = coachItemAt(index);
  file.print("## ");
  file.print(coachDisplayId(item));
  file.print(" - ");
  file.println(coachTypeName(item.type));
  if (item.section && item.section[0] != '\0') {
    file.print("Section: ");
    file.println(sanitizeCoachText(item.section, "deck-dump-section"));
  }
  if (item.cardId && item.cardId[0] != '\0') {
    file.print("Card: ");
    file.println(item.cardId);
  }
  if (item.mustMaster) {
    file.println("Priority: Must");
  }
  if (item.title && item.title[0] != '\0') {
    file.print("Title: ");
    file.println(sanitizeCoachText(item.title, "deck-dump-title"));
  }
  if (item.prompt && item.prompt[0] != '\0') {
    file.println();
    file.println("Prompt:");
    file.println(sanitizeCoachText(item.prompt, "deck-dump-prompt"));
  }
  if (item.spoken && item.spoken[0] != '\0') {
    file.println();
    file.println("Answer:");
    file.println(sanitizeCoachText(item.spoken, "deck-dump-answer"));
  }
  if (item.anchor && item.anchor[0] != '\0') {
    file.println();
    file.println("Anchor:");
    file.println(sanitizeCoachText(item.anchor, "deck-dump-anchor"));
  }
  if (item.watch && item.watch[0] != '\0') {
    file.println();
    file.println("Watch-out:");
    file.println(sanitizeCoachText(item.watch, "deck-dump-watch"));
  }
  if (item.optionCount > 0) {
    file.println();
    file.println("Options:");
    for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
      file.print(static_cast<char>('A' + option));
      file.print(". ");
      file.println(sanitizeCoachText(item.options[option], "deck-dump-option"));
    }
    file.print("Best: ");
    file.println(static_cast<char>('A' + item.correctIndex));
  }
  if (item.explanation && item.explanation[0] != '\0') {
    file.println();
    file.println("Explanation:");
    file.println(sanitizeCoachText(item.explanation, "deck-dump-explanation"));
  }
  if (item.term && item.term[0] != '\0') {
    file.println();
    file.print("Term: ");
    file.println(sanitizeCoachText(item.term, "deck-dump-term"));
  }
  if (item.definition && item.definition[0] != '\0') {
    file.println("Definition:");
    file.println(sanitizeCoachText(item.definition, "deck-dump-definition"));
  }
  file.println();
}

void exportDeckTextToSd() {
  if (!gSdMounted) {
    gLastDeckExportStatus = "SD missing";
    Serial.printf("Deck export: SD missing path=%s\n", kEmbeddedDeckDumpPath);
    return;
  }

  SD.mkdir("/papercoach");
  SD.mkdir("/papercoach/debug");
  if (SD.exists(kEmbeddedDeckDumpPath)) {
    SD.remove(kEmbeddedDeckDumpPath);
  }
  File file = SD.open(kEmbeddedDeckDumpPath, FILE_WRITE);
  if (!file) {
    gLastDeckExportStatus = "open failed";
    Serial.printf("Deck export: open failed path=%s\n", kEmbeddedDeckDumpPath);
    return;
  }

  file.println("# PaperCoach Deck Dump");
  file.println();
  file.print("Firmware: ");
  file.println(kFirmwareVersion);
  file.print("Source: ");
  file.println(gCoachDeckSource);
  file.print("Items: ");
  file.println(static_cast<unsigned>(gCoachItemCount));
  file.println();
  for (size_t index = 0; index < gCoachItemCount; ++index) {
    writeDeckDumpItem(file, index);
  }
  file.close();
  gLastDeckExportStatus = String("wrote ") + kEmbeddedDeckDumpPath;
  Serial.printf("Deck export: wrote %s items=%u\n", kEmbeddedDeckDumpPath, static_cast<unsigned>(gCoachItemCount));
}

void drawReaderPage(const ReaderPageSet& pages, uint8_t pageIndex, int32_t x, int32_t y, int32_t lineHeight) {
  auto& display = M5.Display;
  const size_t startLine = static_cast<size_t>(pageIndex) * pages.linesPerPage;
  const size_t endLine = min(startLine + pages.linesPerPage, pages.lines.size());
  for (size_t line = startLine; line < endLine; ++line) {
    display.drawString(pages.lines[line], x, y + static_cast<int32_t>(line - startLine) * lineHeight);
  }
}

void drawCoachChrome(const char* title) {
  auto& display = M5.Display;
  const CoachTypography type = coachTypography();
  logTypographySettings("practice screen");
  const uint16_t darkGray = metadataTextColor();
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString(title, 28, 28);
  applyCoachMetadataFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_right);
  display.drawString(screenName(gScreen), display.width() - 28, 34);
  display.setTextDatum(textdatum_t::top_left);
  display.drawString(gCoachDeckLoadedFromSd ? "SD deck" : "Embedded deck", 30, 82);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  gHomeButton = {28, display.height() - type.buttonHeight - 28, 164, type.buttonHeight};
  gNextButton = {display.width() - 192, display.height() - type.buttonHeight - 28, 164, type.buttonHeight};
  drawButton(gHomeButton, "", IconType::Home);
  drawButton(gNextButton, "Next");
}

int32_t coachFooterTop() {
  return M5.Display.height() - coachTypography().buttonHeight - 56;
}

void drawCoachPageNumber(uint8_t currentPage, uint8_t pageCount) {
  auto& display = M5.Display;
  const uint16_t darkGray = metadataTextColor();
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
    String body = String("Definition: ") + item.definition;
    if (item.explanation && item.explanation[0] != '\0') {
      body += "\nWhy it matters: ";
      body += item.explanation;
    }
    if (item.answer && item.answer[0] != '\0') {
      body += "\nExample: ";
      body += item.answer;
    }
    return body;
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

bool isOptionDrillScreen(Screen screen, const CoachItemView& item) {
  return (screen == Screen::Drills || screen == Screen::BlitzQuiz || screen == Screen::WeakAnswerDetector ||
          screen == Screen::MetricPrecision) &&
         item.optionCount > 0;
}

bool isExamEligibleItem(const CoachItemView& item) {
  return item.optionCount > 0 &&
         (item.type == CoachItemType::Mcq || item.type == CoachItemType::WeakAnswer ||
          item.type == CoachItemType::MetricPrecision);
}

const char* coachDisplayId(const CoachItemView& item) {
  if (item.cardId && item.cardId[0] != '\0') {
    return item.cardId;
  }
  if (item.id && item.id[0] != '\0') {
    return item.id;
  }
  return coachScreenTitle(gScreen);
}

const char* drillHeaderLabel(const CoachItemView& item) {
  switch (item.type) {
    case CoachItemType::WeakAnswer:
      return "Weak Answer";
    case CoachItemType::MetricPrecision:
      return "Metric Precision";
    case CoachItemType::HostileFollowup:
      return "Hostile Follow-up";
    case CoachItemType::Mcq:
      return gDrillCategory == DrillCategory::FrameworkChoice ? "Framework Choice" : "MCQ";
    default:
      return coachScreenTitle(gScreen);
  }
}

FontSizeMode coachReaderSizeFor(const CoachItemView& item) {
  const FontSizeMode requested = canonicalFontSizeMode(gSettings.fontSizeMode);
  if (isOptionDrillScreen(gScreen, item) && requested == FontSizeMode::XL) {
    Serial.printf("Drill auto-fit: item=%s from=Reader L to=Reader M reason=question-options-fit\n",
                  coachDisplayId(item));
    return FontSizeMode::Large;
  }
  return requested;
}

String optionLabelWithLetter(const CoachItemView& item, uint8_t option) {
  return String(static_cast<char>('A' + option)) + ". " + coachOptionLabelFor(item, option);
}

int32_t optionButtonHeightFor(const String& label, int32_t buttonWidth) {
  applyCoachButtonFont();
  std::vector<String> lines;
  wrapReaderTextToLines(label, buttonWidth - 32, lines, "option-measure");
  const int32_t lineHeight = static_cast<int32_t>(coachTypography().buttonPx) + 8;
  const int32_t desiredHeight = static_cast<int32_t>(lines.size()) * lineHeight + 24;
  return desiredHeight > 56 ? desiredHeight : 56;
}

void drawOptionButton(const Rect& rect, const String& label) {
  auto& display = M5.Display;
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
  applyCoachButtonFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);

  std::vector<String> lines;
  wrapReaderTextToLines(label, rect.w - 32, lines, "option-button");
  const int32_t lineHeight = static_cast<int32_t>(coachTypography().buttonPx) + 8;
  const int32_t textY = rect.y + 12;
  for (size_t line = 0; line < lines.size(); ++line) {
    display.drawString(lines[line], rect.x + 18, textY + static_cast<int32_t>(line) * lineHeight);
  }
  logLayoutBox("option-button", rect, static_cast<int32_t>(lines.size()) * lineHeight + 24, 1,
               static_cast<int32_t>(lines.size()) * lineHeight + 24 > rect.h);
}

String compactQuestionReminder(const String& prompt) {
  String safe = sanitizeCoachText(prompt, "question-reminder");
  safe.replace("\n", " ");
  safe.trim();
  const int colon = safe.indexOf(": ");
  if (colon >= 0 && colon + 2 < static_cast<int>(safe.length())) {
    safe = safe.substring(colon + 2);
  }
  if (safe.length() > 112) {
    safe = safe.substring(0, 109);
    safe += "...";
  }
  return String("Q: ") + safe;
}

DrillPagePlan buildDrillPagePlan(const CoachItemView& item, const PracticeLayout& layout) {
  DrillPagePlan plan;
  plan.questionPages = buildReaderPages(item.prompt, layout.contentW, layout.linesPerPage, "Question");
  plan.optionPageCount = 0;
  plan.reminder = compactQuestionReminder(item.prompt);

  const uint8_t optionCount = item.optionCount < kMaxOptions ? item.optionCount : kMaxOptions;
  if (optionCount == 0) {
    plan.totalPages = plan.questionPages.pageCount;
    return plan;
  }

  int32_t optionHeights[kMaxOptions] = {};
  const int32_t optionGap = 10;
  int32_t allOptionsHeight = 0;
  for (uint8_t option = 0; option < optionCount; ++option) {
    const String label = optionLabelWithLetter(item, option);
    optionHeights[option] = optionButtonHeightFor(label, layout.contentW);
    if (optionHeights[option] > layout.contentH) {
      Serial.printf("Drill option fit warning: item=%s option=%c height=%ld contentH=%ld\n", coachDisplayId(item),
                    static_cast<char>('A' + option), static_cast<long>(optionHeights[option]),
                    static_cast<long>(layout.contentH));
    }
    allOptionsHeight += optionHeights[option];
    if (option > 0) {
      allOptionsHeight += optionGap;
    }
  }

  plan.questionLineCount = static_cast<uint8_t>(min(plan.questionPages.lines.size(), static_cast<size_t>(layout.linesPerPage)));
  plan.questionBlockHeight = static_cast<int32_t>(plan.questionLineCount) * layout.lineHeight;
  const int32_t combinedGap = 18;
  if (plan.questionPages.pageCount == 1 &&
      plan.questionBlockHeight + combinedGap + allOptionsHeight <= layout.contentH) {
    plan.combinedQuestionOptions = true;
    plan.optionPageCount = 1;
    plan.optionPages[0].firstOption = 0;
    plan.optionPages[0].optionCount = optionCount;
    plan.totalPages = 1;
    Serial.printf("Drill fit: item=%s layout=combined questionLines=%u optionsHeight=%ld contentH=%ld\n",
                  coachDisplayId(item), static_cast<unsigned>(plan.questionLineCount),
                  static_cast<long>(allOptionsHeight), static_cast<long>(layout.contentH));
    return plan;
  }

  int32_t usedHeight = 0;
  const int32_t reminderHeight = coachTypography().metadataLineHeight * 2 + 12;
  const int32_t optionAreaH = layout.contentH - reminderHeight;
  for (uint8_t option = 0; option < optionCount; ++option) {
    const int32_t optionHeight = optionHeights[option];
    const int32_t nextHeight = usedHeight == 0 ? optionHeight : usedHeight + optionGap + optionHeight;
    if (plan.optionPageCount > 0 && plan.optionPages[plan.optionPageCount - 1].optionCount > 0 &&
        nextHeight > optionAreaH) {
      ++plan.optionPageCount;
      usedHeight = 0;
    }
    if (plan.optionPageCount >= kMaxOptions) {
      plan.optionPageCount = kMaxOptions;
      break;
    }
    if (plan.optionPages[plan.optionPageCount].optionCount == 0) {
      plan.optionPages[plan.optionPageCount].firstOption = option;
    }
    ++plan.optionPages[plan.optionPageCount].optionCount;
    usedHeight = usedHeight == 0 ? optionHeight : usedHeight + optionGap + optionHeight;
  }

  if (plan.optionPageCount == 0 || plan.optionPages[plan.optionPageCount].optionCount > 0) {
    ++plan.optionPageCount;
  }
  if (plan.optionPageCount == 0) {
    plan.optionPageCount = 1;
  }
  plan.totalPages = plan.questionPages.pageCount + plan.optionPageCount;
  if (plan.totalPages == 0) {
    plan.totalPages = 1;
  }
  Serial.printf("Drill fit: item=%s layout=split questionPages=%u optionPages=%u reminder=yes contentH=%ld\n",
                coachDisplayId(item), plan.questionPages.pageCount, plan.optionPageCount,
                static_cast<long>(layout.contentH));
  return plan;
}

String selectedOptionExplanationText(const CoachItemView& item) {
  String result = String("Selected: ") + static_cast<char>('A' + gSelectedOption) + ". " +
                  coachOptionLabelFor(item, static_cast<uint8_t>(gSelectedOption));
  result += "\nBest: ";
  result += static_cast<char>('A' + item.correctIndex);
  result += ". ";
  result += coachOptionLabelFor(item, item.correctIndex);
  result += "\nWhy this is best: ";
  if (strlen(item.explanation) > 0) {
    result += item.explanation;
  } else if (strlen(item.answer) > 0) {
    result += item.answer;
  } else {
    result += "No explanation available";
  }
  Serial.printf("Render trace warning: item=%s missing per-option explanations selected=%c best=%c\n",
                coachDisplayId(item), static_cast<char>('A' + gSelectedOption), static_cast<char>('A' + item.correctIndex));
  result += "\nWhy other options are weaker: No per-option explanation available yet.";
  return result;
}

bool resultHasPriorAttempt(const char* itemId, const char* mode) {
  for (size_t index = 0; index < gSessionResultCount; ++index) {
    if (strcmp(gSessionResults[index].itemId, itemId) == 0 && strcmp(gSessionResults[index].mode, mode) == 0) {
      return true;
    }
  }
  return false;
}

void persistSessionResults() {
  if (!gSdMounted) {
    gResultsStorageStatus = "RAM session only";
    return;
  }

  SD.mkdir("/papercoach");
  SD.mkdir("/papercoach/progress");
  if (SD.exists(kResultsTempPath)) {
    SD.remove(kResultsTempPath);
  }
  File file = SD.open(kResultsTempPath, FILE_WRITE);
  if (!file) {
    gResultsStorageStatus = "SD write open failed";
    Serial.printf("Results persist failed: open temp path=%s\n", kResultsTempPath);
    return;
  }

  file.print("{\"schema_version\":1,\"session_id\":");
  file.print(gSessionId);
  file.print(",\"result_count\":");
  file.print(static_cast<unsigned>(gSessionResultCount));
  file.print(",\"results\":[");
  for (size_t index = 0; index < gSessionResultCount; ++index) {
    const SessionResult& result = gSessionResults[index];
    if (index > 0) {
      file.print(',');
    }
    file.print("{\"millis\":");
    file.print(result.millisAt);
    file.print(",\"session_id\":");
    file.print(result.sessionId);
    file.print(",\"item_id\":");
    printJsonEscaped(file, result.itemId);
    file.print(",\"card_id\":");
    printJsonEscaped(file, result.cardId);
    file.print(",\"mode\":");
    printJsonEscaped(file, result.mode);
    file.print(",\"type\":");
    printJsonEscaped(file, result.type);
    file.print(",\"category\":");
    printJsonEscaped(file, result.category);
    file.print(",\"selected_option\":");
    printJsonEscaped(file, String(static_cast<char>('A' + result.selectedOption)).c_str());
    file.print(",\"best_option\":");
    printJsonEscaped(file, String(static_cast<char>('A' + result.bestOption)).c_str());
    file.print(",\"correct\":");
    file.print(result.correct ? "true" : "false");
    file.print(",\"first_attempt\":");
    file.print(result.firstAttempt ? "true" : "false");
    file.print(",\"reader\":");
    printJsonEscaped(file, result.reader);
    file.print('}');
  }
  file.print("]}");
  file.close();

  if (SD.exists(kResultsPath)) {
    SD.remove(kResultsPath);
  }
  if (!SD.rename(kResultsTempPath, kResultsPath)) {
    gResultsStorageStatus = "SD rename failed";
    Serial.printf("Results persist failed: rename %s -> %s\n", kResultsTempPath, kResultsPath);
    return;
  }
  gResultsStorageStatus = "SD-backed session";
  Serial.printf("Results persisted: path=%s count=%u session=%u\n", kResultsPath,
                static_cast<unsigned>(gSessionResultCount), static_cast<unsigned>(gSessionId));
}

void recordDrillAnswer(const CoachItemView& item, uint8_t selectedOption) {
  const char* mode = screenName(gScreen);
  const bool firstAttempt = !resultHasPriorAttempt(item.id, mode);
  if (gSessionResultCount >= kMaxSessionResults) {
    for (size_t index = 1; index < gSessionResultCount; ++index) {
      gSessionResults[index - 1] = gSessionResults[index];
    }
    gSessionResultCount = kMaxSessionResults - 1;
    Serial.printf("Results buffer full: dropped oldest max=%u\n", static_cast<unsigned>(kMaxSessionResults));
  }

  SessionResult& result = gSessionResults[gSessionResultCount++];
  result.millisAt = millis();
  result.sessionId = gSessionId;
  copyToBuffer(result.itemId, sizeof(result.itemId), item.id);
  copyToBuffer(result.cardId, sizeof(result.cardId), item.cardId);
  copyToBuffer(result.mode, sizeof(result.mode), mode);
  copyToBuffer(result.type, sizeof(result.type), coachTypeName(item.type));
  copyToBuffer(result.category, sizeof(result.category), drillHeaderLabel(item));
  result.selectedOption = selectedOption;
  result.bestOption = item.correctIndex;
  result.correct = selectedOption == item.correctIndex;
  result.firstAttempt = firstAttempt;
  copyToBuffer(result.reader, sizeof(result.reader), fontSizeModeName());

  Serial.printf("Result recorded: session=%u item=%s mode=%s category=%s selected=%c best=%c correct=%s first=%s count=%u\n",
                static_cast<unsigned>(gSessionId), result.itemId, result.mode, result.category,
                static_cast<char>('A' + result.selectedOption), static_cast<char>('A' + result.bestOption),
                result.correct ? "true" : "false", result.firstAttempt ? "true" : "false",
                static_cast<unsigned>(gSessionResultCount));
  persistSessionResults();
}

uint16_t resultCorrectCount() {
  uint16_t correct = 0;
  for (size_t index = 0; index < gSessionResultCount; ++index) {
    if (gSessionResults[index].correct) {
      ++correct;
    }
  }
  return correct;
}

uint8_t resultAccuracyPercent(uint16_t correct, uint16_t total) {
  if (total == 0) {
    return 0;
  }
  return static_cast<uint8_t>((static_cast<uint32_t>(correct) * 100U + total / 2U) / total);
}

uint8_t buildCategoryStats(CategoryStat* stats, uint8_t maxStats) {
  uint8_t statCount = 0;
  for (size_t resultIndex = 0; resultIndex < gSessionResultCount; ++resultIndex) {
    const SessionResult& result = gSessionResults[resultIndex];
    uint8_t statIndex = 0;
    for (; statIndex < statCount; ++statIndex) {
      if (strcmp(stats[statIndex].name, result.category) == 0) {
        break;
      }
    }
    if (statIndex == statCount) {
      if (statCount >= maxStats) {
        continue;
      }
      copyToBuffer(stats[statCount].name, sizeof(stats[statCount].name), result.category);
      ++statCount;
    }
    ++stats[statIndex].total;
    if (result.correct) {
      ++stats[statIndex].correct;
    }
  }
  return statCount;
}

bool isMustCardId(const char* cardId) {
  if (cardId == nullptr || cardId[0] == '\0') {
    return false;
  }
  for (size_t index = 0; index < gCoachCardCount; ++index) {
    const CoachItemView item = coachItemAt(index);
    if (item.type == CoachItemType::Qa && item.mustMaster && strcmp(item.cardId, cardId) == 0) {
      return true;
    }
  }
  return false;
}

String recommendedNextPractice() {
  if (gSessionResultCount == 0) {
    return "Complete a drill or exam to generate results.";
  }

  CategoryStat stats[8];
  const uint8_t statCount = buildCategoryStats(stats, countOf(stats));
  for (uint8_t index = 0; index < statCount; ++index) {
    const uint8_t accuracy = resultAccuracyPercent(stats[index].correct, stats[index].total);
    if (stats[index].total > 0 && accuracy < 70) {
      return String("Review ") + stats[index].name + " drills next.";
    }
  }

  for (size_t index = 0; index < gSessionResultCount; ++index) {
    const SessionResult& result = gSessionResults[index];
    if (!result.correct && isMustCardId(result.cardId)) {
      return "Review Must practice cards tied to recent misses.";
    }
  }
  return "Run a mixed drill set to keep coverage broad.";
}

uint8_t buildCoachReaderStages(const CoachItemView& item, const PracticeLayout& layout, CoachReaderStage* stages,
                               uint8_t maxStages) {
  uint8_t count = 0;
  auto addStage = [&](const char* name, const String& body) {
    if (count >= maxStages || body.length() == 0) {
      return;
    }
    stages[count].name = name;
    stages[count].body = body;
    stages[count].pages = buildReaderPages(body, layout.contentW, layout.linesPerPage, name);
    ++count;
  };

  if (isOptionDrillScreen(gScreen, item) && gSelectedOption >= 0) {
    addStage("Explanation", selectedOptionExplanationText(item));
    return count == 0 ? 1 : count;
  }

  if (item.type == CoachItemType::HostileFollowup) {
    addStage("Follow-up", coachPromptFor(item));
    addStage("Defense", coachAnswerFor(item));
    addStage("Anchor", coachRubricFor(item));
  } else if (item.type == CoachItemType::Glossary) {
    addStage("Term", String(item.term) + "\n" + coachAnswerFor(item));
  } else if (item.type == CoachItemType::WeakAnswer) {
    addStage("Question", coachPromptFor(item));
    addStage("Explanation", coachAnswerFor(item));
  } else {
    addStage("Question", coachPromptFor(item));
    addStage("Answer", coachAnswerFor(item));
    addStage("Anchor", coachRubricFor(item));
  }

  if (count == 0) {
    stages[0].name = "Text";
    stages[0].body = "No content available.";
    stages[0].pages = buildReaderPages(stages[0].body, layout.contentW, layout.linesPerPage, stages[0].name);
    count = 1;
  }
  return count;
}

uint8_t totalReaderPages(const CoachReaderStage* stages, uint8_t stageCount) {
  uint16_t total = 0;
  for (uint8_t stage = 0; stage < stageCount; ++stage) {
    total += stages[stage].pages.pageCount;
  }
  if (total == 0) {
    total = 1;
  }
  if (total > kMaxReaderPageCount) {
    total = kMaxReaderPageCount;
  }
  return static_cast<uint8_t>(total);
}

int32_t glossaryTermLineHeight() {
  return coachTypography().titlePx + 18;
}

int32_t glossarySectionGap() {
  return 14;
}

void appendGlossarySpace(std::vector<GlossaryRenderLine>& lines, int32_t height) {
  GlossaryRenderLine line;
  line.kind = GlossaryLineKind::Space;
  line.height = height;
  lines.push_back(line);
}

void appendGlossaryWrappedBody(std::vector<GlossaryRenderLine>& lines, const String& text, int32_t width) {
  if (text.length() == 0) {
    return;
  }
  applyCoachContentFont();
  std::vector<String> wrapped;
  wrapReaderTextToLines(text, width, wrapped, "glossary-body");
  const int32_t lineH = coachTypography().bodyLineHeight;
  for (const String& wrappedLine : wrapped) {
    GlossaryRenderLine line;
    line.text = wrappedLine;
    line.kind = GlossaryLineKind::Body;
    line.height = lineH;
    lines.push_back(line);
  }
}

void appendGlossarySection(std::vector<GlossaryRenderLine>& lines, const char* label, const char* body,
                           int32_t width) {
  String sectionBody = sanitizeCoachText(body ? body : "", "glossary-section");
  sectionBody.trim();
  if (sectionBody.length() == 0) {
    return;
  }
  if (!lines.empty()) {
    appendGlossarySpace(lines, glossarySectionGap());
  }
  GlossaryRenderLine labelLine;
  labelLine.text = label;
  labelLine.kind = GlossaryLineKind::Label;
  labelLine.height = coachTypography().metadataLineHeight + 2;
  lines.push_back(labelLine);
  appendGlossaryWrappedBody(lines, sectionBody, width);
}

void buildGlossaryLines(const CoachItemView& item, const PracticeLayout& layout, std::vector<GlossaryRenderLine>& lines) {
  lines.clear();
  GlossaryRenderLine term;
  term.text = sanitizeCoachText(item.term, "glossary-term");
  term.kind = GlossaryLineKind::Term;
  term.height = glossaryTermLineHeight();
  lines.push_back(term);
  appendGlossarySpace(lines, 10);
  appendGlossarySection(lines, "Definition", item.definition, layout.contentW);
  appendGlossarySection(lines, "Why it matters", item.explanation, layout.contentW);
  appendGlossarySection(lines, "Example", item.answer, layout.contentW);
  if (lines.size() <= 2) {
    appendGlossarySection(lines, "Definition", "No glossary definition available.", layout.contentW);
  }
}

uint8_t paginateGlossaryLines(const std::vector<GlossaryRenderLine>& lines, const PracticeLayout& layout,
                              uint16_t* pageStarts, uint8_t maxPages) {
  if (maxPages == 0) {
    return 0;
  }
  pageStarts[0] = 0;
  uint8_t pageCount = 1;
  int32_t used = 0;
  for (size_t index = 0; index < lines.size(); ++index) {
    const GlossaryRenderLine& line = lines[index];
    int32_t required = line.height;
    if (line.kind == GlossaryLineKind::Label && index + 1 < lines.size() &&
        lines[index + 1].kind == GlossaryLineKind::Body) {
      required += lines[index + 1].height;
    }
    if (used > 0 && used + required > layout.contentH && pageCount < maxPages) {
      pageStarts[pageCount++] = static_cast<uint16_t>(index);
      used = 0;
    } else if (used > 0 && used + line.height > layout.contentH && pageCount < maxPages) {
      pageStarts[pageCount++] = static_cast<uint16_t>(index);
      used = 0;
    }
    used += line.height;
  }
  return pageCount == 0 ? 1 : pageCount;
}

uint8_t glossaryPageCountFor(const CoachItemView& item, const PracticeLayout& layout) {
  std::vector<GlossaryRenderLine> lines;
  buildGlossaryLines(item, layout, lines);
  uint16_t pageStarts[kMaxReaderPageCount] = {};
  return paginateGlossaryLines(lines, layout, pageStarts, countOf(pageStarts));
}

uint8_t currentCoachReaderPageCount() {
  if (gCoachItemCount == 0) {
    return 1;
  }
  const CoachItemView item = coachItemAt(gCoachIndex);
  if (gScreen == Screen::InterviewPractice) {
    return interviewStageCount(item);
  }

  const FontSizeMode savedSize = gSettings.fontSizeMode;
  const FontSizeMode renderSize = coachReaderSizeFor(item);
  gSettings.fontSizeMode = renderSize;
  const PracticeLayout layout = practiceLayoutFor(renderSize);
  applyCoachContentFont();
  if (item.type == CoachItemType::Glossary) {
    const uint8_t pageCount = glossaryPageCountFor(item, layout);
    gSettings.fontSizeMode = savedSize;
    return pageCount;
  }
  if (isOptionDrillScreen(gScreen, item) && gSelectedOption < 0) {
    applyCoachButtonFont();
    const DrillPagePlan plan = buildDrillPagePlan(item, layout);
    gSettings.fontSizeMode = savedSize;
    return plan.totalPages;
  }
  CoachReaderStage stages[3];
  const uint8_t stageCount = buildCoachReaderStages(item, layout, stages, countOf(stages));
  gSettings.fontSizeMode = savedSize;
  return totalReaderPages(stages, stageCount);
}

bool containsNonAscii(const String& text) {
  for (size_t index = 0; index < text.length(); ++index) {
    if (static_cast<uint8_t>(text[index]) >= 0x80) {
      return true;
    }
  }
  return false;
}

String compactHeaderCategory(const char* raw) {
  String category = sanitizeCoachText(raw ? raw : "", "header-category");
  category.trim();
  category.replace("PaperCoach ", "");
  category.replace("Motivation & Fit", "Motivation/Fit");
  category.replace("Background, ", "Background / ");
  category.replace(" and ", " & ");
  category.replace(", ", " / ");
  return category;
}

String fitHeaderText(String primary, const String& fallback, int32_t maxWidth) {
  auto& display = M5.Display;
  primary.trim();
  if (primary.length() == 0) {
    return "";
  }
  if (display.textWidth(primary) <= maxWidth) {
    return primary;
  }
  String candidate = fallback;
  candidate.trim();
  if (candidate.length() > 0 && display.textWidth(candidate) <= maxWidth) {
    return candidate;
  }
  if (containsNonAscii(primary)) {
    return "";
  }
  while (primary.length() > 8 && display.textWidth(primary + "...") > maxWidth) {
    primary.remove(primary.length() - 1);
  }
  primary.trim();
  if (primary.length() <= 8) {
    return "";
  }
  return primary + "...";
}

bool drawReadableHeaderLine(const String& primary, const String& fallback, int32_t x, int32_t y, int32_t width) {
  const String line = fitHeaderText(primary, fallback, width);
  if (line.length() == 0) {
    return false;
  }
  M5.Display.drawString(line, x, y);
  return true;
}

void drawCompactReaderHeader(const CoachItemView& item, const char* stageName, uint8_t pageNumber, uint8_t pageCount) {
  auto& display = M5.Display;
  const uint16_t darkGray = metadataTextColor();
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(darkGray, TFT_WHITE);
  const int32_t headerW = display.width() - kCoachMargin * 2;
  String line = sanitizeCoachText(coachDisplayId(item), "reader-id");
  line += " · ";
  if (item.type == CoachItemType::Glossary) {
    line += "Glossary";
  } else if (gScreen == Screen::InterviewPractice || item.type == CoachItemType::Qa) {
    line += (item.mustMaster ? "Must" : "Card");
  } else {
    line += drillHeaderLabel(item);
  }
  line += " · ";
  line += stageName;
  line += " · ";
  line += static_cast<unsigned>(pageNumber);
  line += "/";
  line += static_cast<unsigned>(pageCount);
  String fallback = sanitizeCoachText(coachDisplayId(item), "reader-id-fallback");
  fallback += " · ";
  fallback += (gScreen == Screen::InterviewPractice || item.type == CoachItemType::Qa) ? (item.mustMaster ? "Must" : "Card")
                                                                                       : stageName;
  fallback += " ";
  fallback += static_cast<unsigned>(pageNumber);
  fallback += "/";
  fallback += static_cast<unsigned>(pageCount);
  drawReadableHeaderLine(line, fallback, kCoachMargin, 18, headerW);
  if (item.section && item.section[0] != '\0') {
    const String section = compactHeaderCategory(item.section);
    drawReadableHeaderLine(section, "", kCoachMargin, 54, headerW);
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void drawCompactDrillHeader(const CoachItemView& item, uint8_t pageNumber, uint8_t pageCount) {
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  const int32_t headerW = display.width() - kCoachMargin * 2;
  uint16_t position = 0;
  uint16_t total = 0;
  filteredItemPosition(gScreen, gCoachIndex, position, total);
  String line = sanitizeCoachText(coachDisplayId(item), "drill-id");
  line += " · ";
  line += drillHeaderLabel(item);
  if (total > 0) {
    line += " · ";
    line += static_cast<unsigned>(position);
    line += "/";
    line += static_cast<unsigned>(total);
  }
  String fallback = sanitizeCoachText(coachDisplayId(item), "drill-id-fallback");
  fallback += " · ";
  fallback += drillHeaderLabel(item);
  if (total > 0) {
    fallback += " ";
    fallback += static_cast<unsigned>(position);
    fallback += "/";
    fallback += static_cast<unsigned>(total);
  }
  drawReadableHeaderLine(line, fallback, kCoachMargin, 18, headerW);
  String categoryLine;
  if (item.cardId && item.cardId[0] != '\0' && strcmp(coachDisplayId(item), item.cardId) != 0) {
    categoryLine = String("Card ") + item.cardId;
  } else if (item.category && item.category[0] != '\0' && strcmp(item.category, drillHeaderLabel(item)) != 0) {
    categoryLine = item.category;
  } else if (item.section && item.section[0] != '\0' && strcmp(item.section, "PaperCoach Drills") != 0) {
    categoryLine = item.section;
  }
  if (categoryLine.length() > 0 && categoryLine != drillHeaderLabel(item)) {
    drawReadableHeaderLine(compactHeaderCategory(categoryLine.c_str()), "", kCoachMargin, 54, headerW);
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void drawExamHeader(const CoachItemView& item, uint8_t pageNumber, uint8_t pageCount) {
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  const int32_t headerW = display.width() - kCoachMargin * 2;
  String line = "Exam · Q";
  line += static_cast<unsigned>(gExamPosition + 1);
  line += "/";
  line += static_cast<unsigned>(gExamCount);
  if (pageCount > 1) {
    line += " · ";
    line += static_cast<unsigned>(pageNumber);
    line += "/";
    line += static_cast<unsigned>(pageCount);
  }
  drawReadableHeaderLine(line, "", kCoachMargin, 18, headerW);
  String categoryLine = drillHeaderLabel(item);
  if (item.category && item.category[0] != '\0' && strcmp(item.category, categoryLine.c_str()) != 0) {
    categoryLine = item.category;
  }
  if (categoryLine.length() > 0 && categoryLine != "MCQ") {
    drawReadableHeaderLine(compactHeaderCategory(categoryLine.c_str()), "", kCoachMargin, 54, headerW);
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void drawCompactGlossaryHeader(const CoachItemView& item, uint8_t pageNumber, uint8_t pageCount) {
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  const int32_t headerW = display.width() - kCoachMargin * 2;
  uint16_t position = 0;
  uint16_t total = 0;
  filteredItemPosition(Screen::Glossary, gCoachIndex, position, total);
  String line = "Glossary · ";
  line += glossaryCategoryName(gGlossaryCategory);
  if (total > 0) {
    line += " · ";
    line += static_cast<unsigned>(position);
    line += "/";
    line += static_cast<unsigned>(total);
  }
  String fallback = "Glossary";
  if (total > 0) {
    fallback += " ";
    fallback += static_cast<unsigned>(position);
    fallback += "/";
    fallback += static_cast<unsigned>(total);
  }
  drawReadableHeaderLine(line, fallback, kCoachMargin, 18, headerW);
  if (pageCount > 1) {
    String pageLine = "Page ";
    pageLine += static_cast<unsigned>(pageNumber);
    pageLine += "/";
    pageLine += static_cast<unsigned>(pageCount);
    drawReadableHeaderLine(pageLine, "", kCoachMargin, 54, headerW);
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

ReaderPageSet glossaryTracePages(const std::vector<GlossaryRenderLine>& lines, const uint16_t* pageStarts,
                                 uint8_t pageCount) {
  ReaderPageSet trace;
  trace.linesPerPage = 1;
  trace.pageCount = pageCount == 0 ? 1 : pageCount;
  trace.sourceLength = 0;
  for (uint8_t page = 0; page < trace.pageCount; ++page) {
    const size_t start = pageStarts[page];
    const size_t end = page + 1 < trace.pageCount ? pageStarts[page + 1] : lines.size();
    String visible;
    for (size_t index = start; index < end && index < lines.size(); ++index) {
      if (lines[index].kind == GlossaryLineKind::Space || lines[index].text.length() == 0) {
        continue;
      }
      if (visible.length() > 0) {
        visible += "\n";
      }
      visible += lines[index].text;
      trace.sourceLength += lines[index].text.length();
    }
    trace.lines.push_back(visible);
  }
  trace.sanitizedLength = trace.sourceLength;
  return trace;
}

void drawGlossaryTermPage(const CoachItemView& item, const PracticeLayout& layout) {
  auto& display = M5.Display;
  std::vector<GlossaryRenderLine> lines;
  buildGlossaryLines(item, layout, lines);
  uint16_t pageStarts[kMaxReaderPageCount] = {};
  const uint8_t pageCount = paginateGlossaryLines(lines, layout, pageStarts, countOf(pageStarts));
  if (gCoachStage >= pageCount) {
    gCoachStage = pageCount - 1;
  }

  drawCompactGlossaryHeader(item, gCoachStage + 1, pageCount);
  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};

  const size_t start = pageStarts[gCoachStage];
  const size_t end = gCoachStage + 1 < pageCount ? pageStarts[gCoachStage + 1] : lines.size();
  int32_t y = layout.contentY;
  for (size_t index = start; index < end && index < lines.size(); ++index) {
    const GlossaryRenderLine& line = lines[index];
    switch (line.kind) {
      case GlossaryLineKind::Term:
        applyCoachTitleFont();
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.drawString(line.text, layout.contentX, y);
        break;
      case GlossaryLineKind::Label:
        applyCoachMetadataFont();
        display.setTextColor(metadataTextColor(), TFT_WHITE);
        display.drawString(line.text, layout.contentX, y);
        break;
      case GlossaryLineKind::Body:
        applyCoachContentFont();
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.drawString(line.text, layout.contentX, y);
        break;
      case GlossaryLineKind::Space:
        break;
    }
    y += line.height;
  }

  display.setTextColor(TFT_BLACK, TFT_WHITE);
  const ReaderPageSet tracePages = glossaryTracePages(lines, pageStarts, pageCount);
  recordRenderTrace(item, "Glossary", String(item.term), tracePages, gCoachStage, pageCount > 1);
}

int32_t wrappedButtonHeightFor(const String& label, int32_t buttonWidth) {
  applyCoachButtonFont();
  String lines[2];
  const int32_t lineHeight = static_cast<int32_t>(coachTypography().buttonPx) + 8;
  TextLayoutResult result = wrapTextToLines(label, buttonWidth - 24, lineHeight, 2, lines);
  const int32_t desiredHeight = result.height + 24;
  return desiredHeight > coachTypography().buttonHeight ? desiredHeight : coachTypography().buttonHeight;
}

PracticeLayout practiceLayoutFor(FontSizeMode renderSize) {
  auto& display = M5.Display;
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  gSettings.fontSizeMode = renderSize;
  const CoachTypography type = coachTypography();

  PracticeLayout layout;
  layout.renderSize = renderSize;
  layout.contentX = 38;
  layout.contentY = 92;
  layout.contentW = display.width() - 76;
  layout.buttonH = 58;
  layout.footerY = display.height() - layout.buttonH - 18;
  layout.contentH = layout.footerY - layout.contentY - 16;
  layout.lineHeight = type.bodyLineHeight;
  layout.linesPerPage = linesThatFit(layout.contentH, layout.lineHeight, 3, kMaxWrappedLines);

  gSettings.fontSizeMode = savedSize;
  return layout;
}

FontSizeMode choosePracticeReaderSize(const CoachItemView& item, bool logDecision) {
  const FontSizeMode requested = canonicalFontSizeMode(gSettings.fontSizeMode);
  if (requested != FontSizeMode::XL) {
    return requested;
  }

  const FontSizeMode savedSize = gSettings.fontSizeMode;
  gSettings.fontSizeMode = FontSizeMode::XL;
  PracticeLayout layout = practiceLayoutFor(FontSizeMode::XL);
  applyCoachContentFont();
  struct AutoFitProbe {
    const char* name;
    String body;
  };
  AutoFitProbe probes[] = {
      {"Question", practicePromptText(item)},
      {"Answer", item.spoken},
      {"Anchor", item.anchor},
      {"Watch-out", item.watch},
  };
  const char* crowdedStage = "Answer";
  uint8_t crowdedPages = 0;
  for (size_t index = 0; index < countOf(probes); ++index) {
    ReaderPageSet pages = buildReaderPages(probes[index].body, layout.contentW, layout.linesPerPage, probes[index].name);
    if (pages.pageCount > crowdedPages) {
      crowdedPages = pages.pageCount;
      crowdedStage = probes[index].name;
    }
  }
  gSettings.fontSizeMode = savedSize;

  const bool tooManyPages = crowdedPages > 4;
  const bool tooFewLines = layout.linesPerPage < 8;
  if (tooManyPages || tooFewLines) {
    if (logDecision) {
      Serial.printf("Reader auto-fit: card=%s from=Reader L to=Reader M reason=%s stage=%s pages=%u linesPerPage=%u\n",
                    item.id, tooManyPages ? "too-many-pages" : "too-few-lines", crowdedStage, crowdedPages,
                    layout.linesPerPage);
    }
    return FontSizeMode::Large;
  }
  return FontSizeMode::XL;
}

PracticePageCounts practicePageCountsFor(const CoachItemView& item, const PracticeLayout& layout) {
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  gSettings.fontSizeMode = layout.renderSize;
  applyCoachContentFont();
  const ReaderPageSet prompt = buildReaderPages(practicePromptText(item), layout.contentW, layout.linesPerPage, "count-prompt");
  const ReaderPageSet spoken = buildReaderPages(item.spoken, layout.contentW, layout.linesPerPage, "count-spoken");
  const ReaderPageSet anchor = buildReaderPages(item.anchor, layout.contentW, layout.linesPerPage, "count-anchor");
  const ReaderPageSet watch = buildReaderPages(item.watch, layout.contentW, layout.linesPerPage, "count-watch");
  gSettings.fontSizeMode = savedSize;

  PracticePageCounts counts;
  counts.promptPages = prompt.pageCount;
  counts.spokenPages = spoken.pageCount;
  counts.anchorPages = anchor.pageCount;
  counts.watchPages = watch.pageCount;
  counts.totalPages = counts.promptPages + counts.spokenPages + counts.anchorPages + counts.watchPages;
  if (counts.totalPages == 0) {
    counts.totalPages = 1;
  }
  return counts;
}

uint8_t interviewStageCount(const CoachItemView& item) {
  const FontSizeMode renderSize = choosePracticeReaderSize(item, false);
  const PracticeLayout layout = practiceLayoutFor(renderSize);
  return practicePageCountsFor(item, layout).totalPages;
}

const char* practiceStageName(uint8_t stage, const PracticePageCounts& counts, uint8_t& localPage, uint8_t& localCount) {
  if (stage < counts.promptPages) {
    localPage = stage;
    localCount = counts.promptPages;
    return "Question";
  }
  stage -= counts.promptPages;
  if (stage < counts.spokenPages) {
    localPage = stage;
    localCount = counts.spokenPages;
    return "Answer";
  }
  stage -= counts.spokenPages;
  if (stage < counts.anchorPages) {
    localPage = stage;
    localCount = counts.anchorPages;
    return "Anchor";
  }
  stage -= counts.anchorPages;
  localPage = stage < counts.watchPages ? stage : counts.watchPages - 1;
  localCount = counts.watchPages;
  return "Watch-out";
}

void renderInterviewPracticeScreen() {
  applyAppRotation();
  prepareCoachContentRefresh();

  const CoachItemView item = coachItemAt(gCoachIndex);
  gLastPracticeIndex = gCoachIndex;
  gHasPracticeLastIndex = true;
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  const FontSizeMode renderSize = choosePracticeReaderSize(item, true);
  gSettings.fontSizeMode = renderSize;
  const PracticeLayout layout = practiceLayoutFor(renderSize);
  const PracticePageCounts pageCounts = practicePageCountsFor(item, layout);
  if (gCoachStage >= pageCounts.totalPages) {
    gCoachStage = pageCounts.totalPages - 1;
  }

  uint8_t localPage = 0;
  uint8_t localCount = 1;
  const char* stageName = practiceStageName(gCoachStage, pageCounts, localPage, localCount);
  String body;
  if (strcmp(stageName, "Question") == 0) {
    body = practicePromptText(item);
  } else if (strcmp(stageName, "Answer") == 0) {
    body = item.spoken;
  } else if (strcmp(stageName, "Anchor") == 0) {
    body = item.anchor;
  } else {
    body = item.watch;
  }
  applyCoachContentFont();
  ReaderPageSet pages = buildReaderPages(body, layout.contentW, layout.linesPerPage, stageName);
  if (localPage >= pages.pageCount) {
    localPage = pages.pageCount - 1;
  }
  logReaderPagination(stageName, pages, layout);
  recordRenderTrace(item, stageName, body, pages, localPage);

  auto& display = M5.Display;
  drawCompactReaderHeader(item, stageName, localPage + 1, localCount);

  applyCoachContentFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};
  drawReaderPage(pages, localPage, layout.contentX, layout.contentY, layout.lineHeight);
  if (strcmp(stageName, "Question") == 0 && localPage == 0 && strlen(item.confidence) > 0) {
    const size_t startLine = static_cast<size_t>(localPage) * pages.linesPerPage;
    const size_t endLine = min(startLine + pages.linesPerPage, pages.lines.size());
    const int32_t tagY = layout.contentY + static_cast<int32_t>(endLine - startLine) * layout.lineHeight + 14;
    if (tagY + coachTypography().metadataLineHeight < layout.footerY - 8) {
      applyCoachMetadataFont();
      display.setTextColor(metadataTextColor(), TFT_WHITE);
      display.drawString(sanitizeCoachText(String("Confidence: ") + item.confidence, "practice-confidence"),
                         layout.contentX, tagY);
      applyCoachContentFont();
      display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
  }

  display.setTextColor(TFT_BLACK, TFT_WHITE);
  drawCoachFooterNav(hasPreviousCoachItem(), hasNextCoachItem());

  gSettings.fontSizeMode = savedSize;
  finishDisplayRefresh();
  Serial.printf("Practice shown: card=%s stage=%u/%u local=%s %u/%u reader=%s autoFit=%s\n", item.id, gCoachStage + 1,
                pageCounts.totalPages, stageName, localPage + 1, localCount, shortFontSizeModeName(renderSize),
                renderSize != canonicalFontSizeMode(savedSize) ? "yes" : "no");
}

void renderCoachScreen() {
  if (gScreen == Screen::InterviewPractice) {
    renderInterviewPracticeScreen();
    return;
  }

  applyAppRotation();
  prepareCoachContentRefresh();

  auto& display = M5.Display;
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  FontSizeMode renderSize = canonicalFontSizeMode(gSettings.fontSizeMode);
  gSettings.fontSizeMode = renderSize;
  PracticeLayout layout = practiceLayoutFor(renderSize);
  logTypographySettings("coach screen");
  for (uint8_t option = 0; option < kMaxOptions; ++option) {
    gOptionButtons[option] = {};
  }
  display.setTextDatum(textdatum_t::top_left);
  applyCoachContentFont();

  if (gCoachItemCount == 0) {
    drawWrappedText("No PaperCoach deck available.", layout.contentX, layout.contentY, layout.contentW,
                    layout.lineHeight, layout.linesPerPage, "empty", 1);
    drawCenteredHomeFooter();
    gSettings.fontSizeMode = savedSize;
    finishDisplayRefresh();
    return;
  }

  const CoachItemView item = coachItemAt(gCoachIndex);
  if (!itemMatchesScreen(item, gScreen)) {
    drawWrappedText(String("No ") + coachScreenTitle(gScreen) + " items yet.", layout.contentX, layout.contentY,
                    layout.contentW, layout.lineHeight, layout.linesPerPage, "empty", 1);
    drawCenteredHomeFooter();
    gSettings.fontSizeMode = savedSize;
    finishDisplayRefresh();
    return;
  }
  renderSize = coachReaderSizeFor(item);
  gSettings.fontSizeMode = renderSize;
  layout = practiceLayoutFor(renderSize);
  logTypographySettings("coach item screen");
  if (item.type == CoachItemType::Glossary) {
    drawGlossaryTermPage(item, layout);
    drawCoachFooterNav(hasPreviousCoachItem(), hasNextCoachItem());
    gSettings.fontSizeMode = savedSize;
    finishDisplayRefresh();
    Serial.printf("Glossary shown: term=%s index=%u page=%u/%u source=%s\n", item.term,
                  static_cast<unsigned>(gCoachIndex), static_cast<unsigned>(gCoachStage + 1),
                  static_cast<unsigned>(currentCoachReaderPageCount()), gGlossarySource.c_str());
    return;
  }
  if (gScreen == Screen::MockInterview) {
    applyCoachMetadataFont();
    display.drawString(String("Prompt ") + (gMockStep + 1) + "/5", kCoachMargin, 76);
    applyCoachContentFont();
  }

  const bool optionDrill = isOptionDrillScreen(gScreen, item);
  if (optionDrill && gSelectedOption < 0) {
    const DrillPagePlan plan = buildDrillPagePlan(item, layout);
    if (gCoachStage >= plan.totalPages) {
      gCoachStage = plan.totalPages - 1;
    }
    drawCompactDrillHeader(item, gCoachStage + 1, plan.totalPages);
    gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};

    if (plan.combinedQuestionOptions) {
      applyCoachContentFont();
      drawReaderPage(plan.questionPages, 0, layout.contentX, layout.contentY, layout.lineHeight);
      logReaderPagination("Question", plan.questionPages, layout);
      int32_t y = layout.contentY + plan.questionBlockHeight + 18;
      const int32_t optionGap = 10;
      String traceBody = String(item.prompt) + "\n";
      for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
        const String label = optionLabelWithLetter(item, option);
        const int32_t buttonH = optionButtonHeightFor(label, layout.contentW);
        gOptionButtons[option] = {layout.contentX, y, layout.contentW, buttonH};
        drawOptionButton(gOptionButtons[option], label);
        traceBody += "\n";
        traceBody += label;
        y += buttonH + optionGap;
      }
      ReaderPageSet combinedTrace = buildReaderPages(traceBody, layout.contentW, layout.linesPerPage, "Question+Options");
      recordRenderTrace(item, "Question+Options", traceBody, combinedTrace, 0, false);
      Serial.printf("Drill combined shown: item=%s options=%u totalPages=%u\n", coachDisplayId(item), item.optionCount,
                    plan.totalPages);
    } else if (gCoachStage < plan.questionPages.pageCount) {
      applyCoachContentFont();
      drawReaderPage(plan.questionPages, gCoachStage, layout.contentX, layout.contentY, layout.lineHeight);
      if (gCoachStage + 1 == plan.questionPages.pageCount) {
        applyCoachMetadataFont();
        display.setTextColor(metadataTextColor(), TFT_WHITE);
        display.drawString("Choices ->", layout.contentX, layout.footerY - coachTypography().metadataLineHeight - 8);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
      }
      logReaderPagination("Question", plan.questionPages, layout);
      recordRenderTrace(item, "Question", item.prompt, plan.questionPages, gCoachStage, true);
    } else {
      const uint8_t optionPageIndex = gCoachStage - plan.questionPages.pageCount;
      const DrillOptionPage& optionPage =
          plan.optionPages[optionPageIndex < plan.optionPageCount ? optionPageIndex : plan.optionPageCount - 1];
      int32_t y = layout.contentY;
      const int32_t optionGap = 10;
      applyCoachMetadataFont();
      display.setTextColor(metadataTextColor(), TFT_WHITE);
      TextLayoutResult reminderLayout =
          drawWrappedText(plan.reminder, layout.contentX, y, layout.contentW, coachTypography().metadataLineHeight, 2,
                          "question-reminder", 1);
      y += reminderLayout.height + 12;
      display.setTextColor(TFT_BLACK, TFT_WHITE);
      String traceBody = plan.reminder;
      for (uint8_t offset = 0; offset < optionPage.optionCount; ++offset) {
        const uint8_t option = optionPage.firstOption + offset;
        if (option >= item.optionCount || option >= kMaxOptions) {
          continue;
        }
        const String label = optionLabelWithLetter(item, option);
        const int32_t buttonH = optionButtonHeightFor(label, layout.contentW);
        gOptionButtons[option] = {layout.contentX, y, layout.contentW, buttonH};
        drawOptionButton(gOptionButtons[option], label);
        if (traceBody.length() > 0) {
          traceBody += "\n";
        }
        traceBody += label;
        y += buttonH + optionGap;
      }
      ReaderPageSet optionTrace = buildReaderPages(traceBody, layout.contentW, layout.linesPerPage, "Options");
      recordRenderTrace(item, "Options", traceBody, optionTrace, 0, true);
      Serial.printf("Drill options shown: item=%s optionPage=%u/%u first=%u count=%u totalPages=%u\n",
                    coachDisplayId(item), optionPageIndex + 1, plan.optionPageCount, optionPage.firstOption,
                    optionPage.optionCount, plan.totalPages);
    }
    drawCoachFooterNav(hasPreviousCoachItem(), hasNextCoachItem());
    gSettings.fontSizeMode = savedSize;
    finishDisplayRefresh();
    return;
  }

  CoachReaderStage stages[3];
  const uint8_t stageCount = buildCoachReaderStages(item, layout, stages, countOf(stages));
  const uint8_t totalPages = totalReaderPages(stages, stageCount);
  if (gCoachStage >= totalPages) {
    gCoachStage = totalPages - 1;
  }

  uint8_t remainingPage = gCoachStage;
  uint8_t currentStage = 0;
  uint8_t localPage = 0;
  for (uint8_t stage = 0; stage < stageCount; ++stage) {
    if (remainingPage < stages[stage].pages.pageCount) {
      currentStage = stage;
      localPage = remainingPage;
      break;
    }
    remainingPage -= stages[stage].pages.pageCount;
  }

  const CoachReaderStage& stage = stages[currentStage];
  if (optionDrill) {
    drawCompactDrillHeader(item, gCoachStage + 1, totalPages);
  } else {
    drawCompactReaderHeader(item, stage.name, localPage + 1, stage.pages.pageCount);
  }
  applyCoachContentFont();
  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};
  drawReaderPage(stage.pages, localPage, layout.contentX, layout.contentY, layout.lineHeight);
  logReaderPagination(stage.name, stage.pages, layout);
  const char* traceWarning =
      optionDrill && gSelectedOption >= 0 ? "per-option explanations unavailable; placeholder shown" : nullptr;
  recordRenderTrace(item, stage.name, stage.body, stage.pages, localPage, false, traceWarning);
  drawCoachFooterNav(hasPreviousCoachItem(), hasNextCoachItem());

  gSettings.fontSizeMode = savedSize;
  finishDisplayRefresh();
  Serial.printf("%s shown: type=%s index=%u page=%u/%u source=%s\n", coachScreenTitle(gScreen), coachTypeName(item.type),
                static_cast<unsigned>(gCoachIndex), gCoachStage + 1, totalPages,
                gCoachDeckLoadedFromSd ? "SD" : "embedded");
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

  const int32_t width = display.width();
  const int32_t buttonX = 34;
  const int32_t buttonW = width - 68;
  const int32_t buttonH = 82;
  const int32_t gap = 12;
  int32_t y = 104;
  gBadgeButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gPracticeButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDrillsButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gExamButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gGlossaryButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gResultsButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gSettingsButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDebugButton = {buttonX, y, buttonW, buttonH};

  drawButton(gBadgeButton, "Badge", IconType::Badge);
  drawButton(gPracticeButton, "Practice", IconType::Practice);
  drawButton(gDrillsButton, "Drills", IconType::Drills);
  drawButton(gExamButton, "Exam", IconType::Exam);
  drawButton(gGlossaryButton, "Glossary", IconType::Glossary);
  drawButton(gResultsButton, "Results", IconType::Results);
  drawButton(gSettingsButton, "Settings", IconType::Settings);
  drawButton(gDebugButton, "Debug", IconType::Debug);

  finishDisplayRefresh();
  Serial.println("Home/Menu mode: normal orientation.");
}

void renderPracticeMenu(const char* refreshReason = "mode switch") {
  gScreen = Screen::PracticeMenu;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Practice", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("Choose cards", 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 86;
  const int32_t gap = 18;
  int32_t y = 148;
  gPracticeMustButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gPracticeAllButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gPracticeContinueButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gPracticeHelpButton = {buttonX, y, buttonW, buttonH};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gPracticeMustButton, "Must cards", IconType::Practice);
  drawButton(gPracticeAllButton, "All cards", IconType::Practice);
  drawButton(gPracticeContinueButton, "Continue last card", IconType::Next);
  drawButton(gPracticeHelpButton, "Help / Legend");
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.printf("Practice menu shown: last=%s index=%u mustCount=%u\n", gHasPracticeLastIndex ? "yes" : "no",
                static_cast<unsigned>(gLastPracticeIndex), static_cast<unsigned>(gCoachMustMasterCount));
}

uint16_t glossaryTermCountFor(GlossaryCategory category) {
  uint16_t count = 0;
  const GlossaryCategory saved = gGlossaryCategory;
  gGlossaryCategory = category;
  for (size_t index = 0; index < gCoachItemCount; ++index) {
    const CoachItemView item = coachItemAt(index);
    if (item.type == CoachItemType::Glossary && glossaryCategoryMatches(item.category)) {
      ++count;
    }
  }
  gGlossaryCategory = saved;
  return count;
}

String glossaryButtonLabel(GlossaryCategory category) {
  return String(glossaryCategoryName(category)) + " (" + static_cast<unsigned>(glossaryTermCountFor(category)) + ")";
}

void renderGlossaryMenu(const char* refreshReason = "mode switch") {
  gScreen = Screen::GlossaryMenu;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Glossary", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString(String("Source: ") + gGlossarySource + "  Terms: " + static_cast<unsigned>(gCoachGlossaryCount), 34,
                     92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t margin = 34;
  const int32_t gap = 14;
  const int32_t colW = (display.width() - margin * 2 - gap) / 2;
  const int32_t rowH = 112;
  int32_t y = 150;
  gGlossaryAiButton = {margin, y, colW, rowH};
  gGlossaryEvalsButton = {margin + colW + gap, y, colW, rowH};
  y += rowH + gap;
  gGlossaryMetricsButton = {margin, y, colW, rowH};
  gGlossaryProductButton = {margin + colW + gap, y, colW, rowH};
  y += rowH + gap;
  gGlossaryInterviewButton = {margin, y, display.width() - margin * 2, rowH};
  gHomeButton = {margin, display.height() - 110, display.width() - margin * 2, 76};

  drawButton(gGlossaryAiButton, glossaryButtonLabel(GlossaryCategory::AiRag), IconType::Glossary);
  drawButton(gGlossaryEvalsButton, glossaryButtonLabel(GlossaryCategory::Evals), IconType::Glossary);
  drawButton(gGlossaryMetricsButton, glossaryButtonLabel(GlossaryCategory::Metrics), IconType::Results);
  drawButton(gGlossaryProductButton, glossaryButtonLabel(GlossaryCategory::Product), IconType::Practice);
  drawButton(gGlossaryInterviewButton, glossaryButtonLabel(GlossaryCategory::Interview), IconType::Exam);
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.printf("Glossary menu shown: source=%s terms=%u\n", gGlossarySource.c_str(),
                static_cast<unsigned>(gCoachGlossaryCount));
}

void renderDrillsMenu(const char* refreshReason = "mode switch") {
  gScreen = Screen::DrillsMenu;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Drills", 32, 34);
  applyCoachMetadataFont();
  const uint16_t darkGray = metadataTextColor();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString("Choose category", 34, 92);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 82;
  const int32_t gap = 12;
  int32_t y = 132;
  gDrillAllButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDrillWeakButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDrillMetricButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDrillFollowupButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDrillFrameworkButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDrillMaturityButton = {buttonX, y, buttonW, buttonH};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gDrillAllButton, "All Drills");
  drawButton(gDrillWeakButton, "Weak Answer");
  drawButton(gDrillMetricButton, "Metric Precision");
  drawButton(gDrillFollowupButton, "Follow-up Defense");
  drawButton(gDrillFrameworkButton, "Framework Choice");
  drawButton(gDrillMaturityButton, "Maturity Claim");
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  Serial.println("Drills menu shown.");
}

void renderPlaceholderScreen(Screen screen, const char* title, const char* body, const char* refreshReason = "mode switch") {
  gScreen = screen;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const uint16_t darkGray = metadataTextColor();
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString(title, 32, 34);

  applyCoachContentFont();
  const int32_t contentX = kCoachMargin;
  const int32_t contentY = kCoachHeaderBottom + 18;
  const int32_t contentW = display.width() - kCoachMargin * 2;
  const int32_t contentH = coachFooterTop() - contentY - 42;
  drawWrappedText(body, contentX, contentY, contentW, coachLineHeight(), linesThatFit(contentH, coachLineHeight(), 2),
                  "placeholder", 1);

  gHomeButton = {34, display.height() - coachTypography().buttonHeight - 28, display.width() - 68,
                 coachTypography().buttonHeight};
  display.setTextColor(darkGray, TFT_WHITE);
  drawCoachPageNumber(1, 1);
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.printf("%s placeholder shown.\n", title);
}

void renderExamMenu(const char* refreshReason = "mode switch") {
  gScreen = Screen::Exam;
  gExamActive = false;
  gExamSummary = false;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Exam", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("Mixed question mode", 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 86;
  const int32_t gap = 18;
  int32_t y = 148;
  gExamStart10Button = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gExamStart5Button = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gExamReviewButton = {buttonX, y, buttonW, buttonH};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gExamStart10Button, "Start 10-question mixed exam", IconType::Exam);
  drawButton(gExamStart5Button, "Start 5-question quick exam", IconType::Exam);
  drawButton(gExamReviewButton, "Review last exam results", IconType::Results);
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.printf("Exam menu shown: lastCount=%u\n", static_cast<unsigned>(gLastExamCount));
}

void finishExam() {
  gExamActive = false;
  gExamSummary = true;
  gLastExamCount = gExamCount;
  gLastExamCorrect = gExamCorrect;
  gLastExamHadShortage = gExamHadShortage;
  Serial.printf("Exam finished: score=%u/%u shortage=%s misses=%u\n", static_cast<unsigned>(gLastExamCorrect),
                static_cast<unsigned>(gLastExamCount), gLastExamHadShortage ? "yes" : "no",
                static_cast<unsigned>(gLastExamMissCount));
}

void renderExamSummary(const char* refreshReason = "exam summary") {
  gScreen = Screen::Exam;
  gExamActive = false;
  gExamSummary = true;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Exam Results", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString(gResultsStorageStatus, 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  int32_t y = 136;
  if (gLastExamCount == 0) {
    applyCoachContentFont();
    drawWrappedText("No completed exam yet. Start a quick or mixed exam first.", kCoachMargin, y,
                    display.width() - kCoachMargin * 2, coachLineHeight(), 4, "exam-empty", 1);
  } else {
    applyCoachContentFont();
    const uint8_t score = resultAccuracyPercent(gLastExamCorrect, gLastExamCount);
    display.drawString(String("Score: ") + static_cast<unsigned>(gLastExamCorrect) + "/" +
                           static_cast<unsigned>(gLastExamCount) + " (" + static_cast<unsigned>(score) + "%)",
                       34, y);
    y += 56;
    if (gLastExamHadShortage) {
      applyCoachMetadataFont();
      display.drawString("Not enough items; used all available.", 34, y);
      y += 34;
    }

    applyCoachMetadataFont();
    display.drawString("Missed items", 34, y);
    y += 30;
    if (gLastExamMissCount == 0) {
      display.drawString("No misses.", 48, y);
      y += 30;
    } else {
      for (uint8_t index = 0; index < gLastExamMissCount && index < 5; ++index) {
        display.drawString(String(gLastExamMissIds[index]) + " " + gLastExamMissCategories[index], 48, y);
        y += 28;
      }
    }

    y += 8;
    display.drawString("Weak categories", 34, y);
    y += 30;
    CategoryStat missStats[6];
    uint8_t missStatCount = 0;
    for (uint8_t miss = 0; miss < gLastExamMissCount; ++miss) {
      uint8_t stat = 0;
      for (; stat < missStatCount; ++stat) {
        if (strcmp(missStats[stat].name, gLastExamMissCategories[miss]) == 0) {
          break;
        }
      }
      if (stat == missStatCount && missStatCount < countOf(missStats)) {
        copyToBuffer(missStats[missStatCount].name, sizeof(missStats[missStatCount].name),
                     gLastExamMissCategories[miss]);
        ++missStatCount;
      }
      if (stat < missStatCount) {
        ++missStats[stat].total;
      }
    }
    if (missStatCount == 0) {
      display.drawString("None.", 48, y);
      y += 28;
    } else {
      for (uint8_t index = 0; index < missStatCount && index < 3; ++index) {
        display.drawString(String(missStats[index].name) + " misses: " + static_cast<unsigned>(missStats[index].total),
                           48, y);
        y += 28;
      }
    }

    y += 8;
    display.drawString("Next", 34, y);
    y += 30;
    drawWrappedText(recommendedNextPractice(), 48, y, display.width() - 96, coachTypography().metadataLineHeight, 2,
                    "exam-recommendation", 1);
  }

  gHomeButton = {34, display.height() - 110, display.width() - 68, 76};
  drawButton(gHomeButton, "", IconType::Home);
  finishDisplayRefresh();
  Serial.printf("Exam summary shown: score=%u/%u\n", static_cast<unsigned>(gLastExamCorrect),
                static_cast<unsigned>(gLastExamCount));
}

void renderExamQuestion(const char* refreshReason = "exam question") {
  gScreen = Screen::Exam;
  gExamActive = true;
  applyAppRotation();
  prepareCoachContentRefresh(refreshReason);

  auto& display = M5.Display;
  for (uint8_t option = 0; option < kMaxOptions; ++option) {
    gOptionButtons[option] = {};
  }

  if (gExamCount == 0 || gExamPosition >= gExamCount) {
    finishExam();
    renderExamSummary();
    return;
  }

  gCoachIndex = gExamItemIndices[gExamPosition];
  const CoachItemView item = coachItemAt(gCoachIndex);
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  const FontSizeMode renderSize = coachReaderSizeFor(item);
  gSettings.fontSizeMode = renderSize;
  const PracticeLayout layout = practiceLayoutFor(renderSize);
  applyCoachContentFont();
  const DrillPagePlan plan = buildDrillPagePlan(item, layout);
  if (gCoachStage >= plan.totalPages) {
    gCoachStage = plan.totalPages - 1;
  }
  drawExamHeader(item, gCoachStage + 1, plan.totalPages);
  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};

  if (plan.combinedQuestionOptions) {
    drawReaderPage(plan.questionPages, 0, layout.contentX, layout.contentY, layout.lineHeight);
    int32_t y = layout.contentY + plan.questionBlockHeight + 18;
    const int32_t optionGap = 10;
    String traceBody = String(item.prompt) + "\n";
    for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
      const String label = optionLabelWithLetter(item, option);
      const int32_t buttonH = optionButtonHeightFor(label, layout.contentW);
      gOptionButtons[option] = {layout.contentX, y, layout.contentW, buttonH};
      drawOptionButton(gOptionButtons[option], label);
      traceBody += "\n";
      traceBody += label;
      y += buttonH + optionGap;
    }
    ReaderPageSet combinedTrace = buildReaderPages(traceBody, layout.contentW, layout.linesPerPage, "ExamQuestion");
    recordRenderTrace(item, "ExamQuestion", traceBody, combinedTrace, 0, false);
  } else if (gCoachStage < plan.questionPages.pageCount) {
    drawReaderPage(plan.questionPages, gCoachStage, layout.contentX, layout.contentY, layout.lineHeight);
    if (gCoachStage + 1 == plan.questionPages.pageCount) {
      applyCoachMetadataFont();
      display.setTextColor(metadataTextColor(), TFT_WHITE);
      display.drawString("Choices ->", layout.contentX, layout.footerY - coachTypography().metadataLineHeight - 8);
      display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    recordRenderTrace(item, "ExamQuestion", item.prompt, plan.questionPages, gCoachStage, true);
  } else {
    const uint8_t optionPageIndex = gCoachStage - plan.questionPages.pageCount;
    const DrillOptionPage& optionPage =
        plan.optionPages[optionPageIndex < plan.optionPageCount ? optionPageIndex : plan.optionPageCount - 1];
    int32_t y = layout.contentY;
    applyCoachMetadataFont();
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    TextLayoutResult reminderLayout =
        drawWrappedText(plan.reminder, layout.contentX, y, layout.contentW, coachTypography().metadataLineHeight, 2,
                        "exam-question-reminder", 1);
    y += reminderLayout.height + 12;
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    String traceBody = plan.reminder;
    const int32_t optionGap = 10;
    for (uint8_t offset = 0; offset < optionPage.optionCount; ++offset) {
      const uint8_t option = optionPage.firstOption + offset;
      if (option >= item.optionCount || option >= kMaxOptions) {
        continue;
      }
      const String label = optionLabelWithLetter(item, option);
      const int32_t buttonH = optionButtonHeightFor(label, layout.contentW);
      gOptionButtons[option] = {layout.contentX, y, layout.contentW, buttonH};
      drawOptionButton(gOptionButtons[option], label);
      traceBody += "\n";
      traceBody += label;
      y += buttonH + optionGap;
    }
    ReaderPageSet optionTrace = buildReaderPages(traceBody, layout.contentW, layout.linesPerPage, "ExamOptions");
    recordRenderTrace(item, "ExamOptions", traceBody, optionTrace, 0, true);
  }

  drawCenteredHomeFooter();
  gSettings.fontSizeMode = savedSize;
  finishDisplayRefresh();
  Serial.printf("Exam question shown: position=%u/%u item=%s\n", static_cast<unsigned>(gExamPosition + 1),
                static_cast<unsigned>(gExamCount), coachDisplayId(item));
}

void startExam(uint8_t desiredCount) {
  size_t pool[kMaxExamPool];
  size_t poolCount = 0;
  for (size_t index = 0; index < gCoachItemCount && poolCount < kMaxExamPool; ++index) {
    const CoachItemView item = coachItemAt(index);
    if (isExamEligibleItem(item)) {
      pool[poolCount++] = index;
    }
  }

  for (size_t index = 0; index < poolCount; ++index) {
    const size_t swapIndex = index + (esp_random() % (poolCount - index));
    const size_t temp = pool[index];
    pool[index] = pool[swapIndex];
    pool[swapIndex] = temp;
  }

  gExamCount = static_cast<uint8_t>(poolCount < desiredCount ? poolCount : desiredCount);
  gExamHadShortage = poolCount < desiredCount;
  for (uint8_t index = 0; index < gExamCount; ++index) {
    gExamItemIndices[index] = pool[index];
  }
  gExamPosition = 0;
  gExamCorrect = 0;
  gLastExamMissCount = 0;
  gCoachStage = 0;
  gSelectedOption = -1;
  gExamActive = gExamCount > 0;
  gExamSummary = gExamCount == 0;
  gCoachNeedsCleanEntryRefresh = true;
  Serial.printf("Exam start: requested=%u actual=%u pool=%u shortage=%s\n", static_cast<unsigned>(desiredCount),
                static_cast<unsigned>(gExamCount), static_cast<unsigned>(poolCount), gExamHadShortage ? "yes" : "no");
  if (gExamCount == 0) {
    gLastExamCount = 0;
    renderExamSummary();
    return;
  }
  renderExamQuestion("exam start");
}

void drawResultBar(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t percent) {
  auto& display = M5.Display;
  const int32_t fillW = (w - 4) * constrain(static_cast<int32_t>(percent), 0, 100) / 100;
  display.drawRect(x, y, w, h, TFT_BLACK);
  if (fillW > 0) {
    display.fillRect(x + 2, y + 2, fillW, h - 4, TFT_BLACK);
  }
}

void renderResultsScreen(const char* refreshReason = "mode switch") {
  gScreen = Screen::Results;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Results", 32, 34);

  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString(gResultsStorageStatus, 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  int32_t y = 134;
  if (gSessionResultCount == 0) {
    applyCoachContentFont();
    drawWrappedText("No results yet. Answer Drills or Exam questions to build a session summary.", kCoachMargin, y,
                    display.width() - kCoachMargin * 2, coachLineHeight(), 5, "results-empty", 1);
    gHomeButton = {34, display.height() - 110, display.width() - 68, 76};
    drawButton(gHomeButton, "", IconType::Home);
    finishDisplayRefresh();
    Serial.println("Results screen shown: empty state.");
    return;
  }

  const uint16_t total = static_cast<uint16_t>(gSessionResultCount);
  const uint16_t correct = resultCorrectCount();
  const uint8_t accuracy = resultAccuracyPercent(correct, total);
  applyCoachContentFont();
  display.drawString(String("Answered: ") + total, 34, y);
  display.drawString(String("Correct: ") + accuracy + "%", 282, y);
  y += 54;

  CategoryStat stats[8];
  const uint8_t statCount = buildCategoryStats(stats, countOf(stats));
  applyCoachMetadataFont();
  display.drawString("By category", 34, y);
  y += 32;
  const uint8_t visibleStats = statCount < 4 ? statCount : 4;
  for (uint8_t index = 0; index < visibleStats; ++index) {
    const uint8_t statAccuracy = resultAccuracyPercent(stats[index].correct, stats[index].total);
    display.drawString(String(stats[index].name) + " " + statAccuracy + "%", 34, y);
    drawResultBar(276, y + 4, 220, 18, statAccuracy);
    y += 34;
  }

  y += 8;
  display.drawString("Weakest areas", 34, y);
  y += 30;
  bool pickedStats[8] = {};
  for (uint8_t rank = 0; rank < 3; ++rank) {
    int8_t weakestIndex = -1;
    uint8_t weakestAccuracy = 101;
    for (uint8_t index = 0; index < statCount; ++index) {
      if (pickedStats[index] || stats[index].total == 0) {
        continue;
      }
      const uint8_t statAccuracy = resultAccuracyPercent(stats[index].correct, stats[index].total);
      if (statAccuracy < weakestAccuracy) {
        weakestAccuracy = statAccuracy;
        weakestIndex = static_cast<int8_t>(index);
      }
    }
    if (weakestIndex < 0 || weakestIndex >= static_cast<int8_t>(statCount)) {
      break;
    }
    display.drawString(String(rank + 1) + ". " + stats[weakestIndex].name + " " + weakestAccuracy + "%", 48, y);
    pickedStats[weakestIndex] = true;
    y += 28;
  }

  y += 8;
  display.drawString("Recent misses", 34, y);
  y += 30;
  uint8_t shownMisses = 0;
  for (size_t offset = 0; offset < gSessionResultCount && shownMisses < 3; ++offset) {
    const size_t index = gSessionResultCount - 1 - offset;
    const SessionResult& result = gSessionResults[index];
    if (result.correct) {
      continue;
    }
    String line = String(result.itemId) + " " + result.category + " -> " + static_cast<char>('A' + result.bestOption);
    drawWrappedText(line, 48, y, display.width() - 96, coachTypography().metadataLineHeight, 1, "recent-miss", 1);
    y += 28;
    ++shownMisses;
  }
  if (shownMisses == 0) {
    display.drawString("No misses in this session.", 48, y);
    y += 28;
  }

  y += 8;
  display.drawString("Next", 34, y);
  y += 30;
  drawWrappedText(recommendedNextPractice(), 48, y, display.width() - 96, coachTypography().metadataLineHeight, 2,
                  "recommendation", 1);

  gHomeButton = {34, display.height() - 110, display.width() - 68, 76};
  drawButton(gHomeButton, "", IconType::Home);
  finishDisplayRefresh();
  Serial.printf("Results screen shown: total=%u correct=%u accuracy=%u storage=%s\n", total, correct, accuracy,
                gResultsStorageStatus.c_str());
}

void renderSettings(const char* refreshReason = "mode switch") {
  gScreen = Screen::Settings;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const int32_t width = display.width();
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Settings", 32, 34);
  applyCoachMetadataFont();
  display.drawString(compactPowerStatusLine(), 36, 76);
  drawBatteryBar(36, 102, width - 84, 16, batteryLevelPercent());
  display.drawString("Badge orientation", 36, 132);

  const int32_t halfW = (width - 88) / 2;
  gOrientationButton = {36, 160, width - 72, 54};
  gLanguageAutoButton = {36, 244, width - 72, 52};
  gLanguageEnglishButton = {36, 302, width - 72, 52};
  gLanguageJapaneseButton = {};
  gFontMediumButton = {36, 400, halfW, 52};
  gFontLargeButton = {52 + halfW, 400, halfW, 52};
  gFontXlButton = {36, 460, width - 72, 52};
  gFontXxlButton = {};
  gFontHugeButton = {};
  gFontStyleButton = {36, 614, width - 72, 54};
  gRefreshModeButton = {36, 718, width - 72, 54};
  gPowerModeButton = {36, 822, halfW, 54};
  gBadgeSleepButton = {52 + halfW, 822, halfW, 54};
  gHomeButton = {36, display.height() - 60, 178, 50};

  drawButton(gOrientationButton, gSettings.orientationMode == OrientationMode::Strap ? "Strap 180" : "Handheld 0");
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("Badge language", 36, 218);
  drawButton(gLanguageAutoButton, String("Mode: ") + languageModeName());
  drawButton(gLanguageEnglishButton, String("Auto interval: ") + autoRotateIntervalName());
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("Reader size", 36, 370);
  const FontSizeMode activeReaderSize = canonicalFontSizeMode(gSettings.fontSizeMode);
  drawButton(gFontMediumButton, activeReaderSize == FontSizeMode::Medium ? "Reader S *" : "Reader S");
  drawButton(gFontLargeButton, activeReaderSize == FontSizeMode::Large ? "Reader M *" : "Reader M");
  drawButton(gFontXlButton, activeReaderSize == FontSizeMode::XL ? "Reader L *" : "Reader L");
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("Font style", 36, 584);
  drawButton(gFontStyleButton, fontStyleModeName());
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("Refresh mode", 36, 688);
  drawButton(gRefreshModeButton, String(refreshModeName()) + " *");
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.drawString("Power / Badge sleep", 36, 792);
  drawButton(gPowerModeButton, powerModeName());
  drawButton(gBadgeSleepButton, badgeSleepModeName());
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  logTypographySettings("settings screen");
  logPowerAudit("settings screen");
  Serial.printf("Settings screen shown: font=%s style=%s contrast=%s refresh=%s power=%s badgeSleep=%s\n",
                fontSizeModeName(), fontStyleModeName(), contrastModeName(), refreshModeName(), powerModeName(),
                badgeSleepModeName());
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
  display.drawString(String("SD: ") + (gSdMounted ? "yes" : "no") + "  deck: " + gCoachDeckSource + " " +
                         static_cast<unsigned>(gCoachItemCount),
                     26, y);
  y += 26;
  display.drawString(String("badge: ") + languageName(gBadgeLanguage) + " / " + languageModeName() + " / " +
                         autoRotateIntervalName(),
                     26, y);
  y += 24;
  display.drawString(String("font: ") + fontSizeModeName() + " / " + fontStyleModeName() + " / " +
                         lineSpacingModeName(),
                     26, y);
  y += 24;
  display.drawString(String("contrast: ") + contrastModeName() + "  refresh: " + refreshModeName(), 26, y);
  y += 24;
  display.drawString(String("refresh count: ") + static_cast<unsigned>(gRefreshTransitionCount) + "/" +
                         static_cast<unsigned>(kHardCleanTransitionLimit),
                     26, y);
  y += 24;
  display.drawString(String("power: ") + powerModeName() + " sleep=" + badgeSleepModeName() +
                         " idle=" + (gIdleModeActive ? "yes" : "no"),
                     26, y);
  y += 24;
  display.drawString(batteryStatusLine(), 26, y);
  y += 24;
  drawBatteryBar(26, y, 220, 16, batteryLevelPercent());
  y += 26;
  display.drawString(chargeStatusLine(), 26, y);
  y += 24;
  display.drawString(usbStatusLine(), 26, y);
  y += 24;
  display.drawString(String("radios: wifi ") + (WiFi.getMode() == WIFI_OFF ? "off" : "on") + " bt " +
                         (btStarted() ? "on" : "off") + " imu off spk stopped",
                     26, y);
  y += 24;
  display.drawString(String("input locked: ") + (gInputLocked ? "yes" : "no") + "  touch active: " +
                         (gTouchActive ? "yes" : "no"),
                     26, y);
  y += 24;
  display.drawString(String("touch down: ") + gLastTouchDownX + "," + gLastTouchDownY, 26, y);
  y += 24;
  display.drawString(String("touch up: ") + gLastTouchUpX + "," + gLastTouchUpY, 26, y);
  y += 24;
  display.drawString(String("last hit: ") + gLastHitTarget, 26, y);
  y += 24;
  display.drawString(String("ignored: ") + gLastIgnoredTouchReason, 26, y);
  y += 24;
  display.drawString(String("refresh end: ") + static_cast<unsigned>(gLastRefreshEndMs) + " ms", 26, y);
  y += 24;
  display.drawString(String("debounce: ") + static_cast<unsigned>(gLastDebounceMs) + " ms", 26, y);
  y += 24;
  display.drawString(String("sanitize: last ") + static_cast<unsigned>(gSanitizerReplacementLast) + " total " +
                         static_cast<unsigned>(gSanitizerReplacementTotal),
                     26, y);
  y += 24;
  display.drawString(String("touch debug: ") + (gTouchDebugEnabled ? "on" : "off"), 26, y);
  y += 24;
  display.drawString(String("trace: ") + gLastRenderTraceStatus, 26, y);
  y += 24;
  display.drawString(String("deck export: ") + gLastDeckExportStatus, 26, y);

  const int32_t actionX = 26;
  const int32_t actionGap = 10;
  const int32_t actionW = (display.width() - 52 - actionGap) / 2;
  const int32_t actionH = 42;
  const int32_t rightX = actionX + actionW + actionGap;
  int32_t actionY = display.height() - 362;
  gHelpButton = {actionX, actionY, actionW, actionH};
  gPowerAuditButton = {rightX, actionY, actionW, actionH};
  actionY += actionH + 8;
  gVisualQaButton = {actionX, actionY, actionW, actionH};
  gFontLabButton = {rightX, actionY, actionW, actionH};
  actionY += actionH + 8;
  gTypographyResetButton = {actionX, actionY, actionW, actionH};
  gLayoutDebugButton = {rightX, actionY, actionW, actionH};
  actionY += actionH + 8;
  gRenderTraceButton = {actionX, actionY, actionW, actionH};
  gExportDeckButton = {rightX, actionY, actionW, actionH};
  actionY += actionH + 8;
  gTouchDebugButton = {actionX, actionY, actionW, actionH};
  gHomeButton = {26, display.height() - 94, display.width() - 52, 58};
  drawButton(gHelpButton, "Help");
  drawButton(gPowerAuditButton, "Power Audit");
  drawButton(gVisualQaButton, "Visual QA", IconType::Exam);
  drawButton(gFontLabButton, "Font Lab");
  drawButton(gTypographyResetButton, "Reset typography");
  drawButton(gLayoutDebugButton, "Layout log");
  drawButton(gRenderTraceButton, "Dump render trace");
  drawButton(gTouchDebugButton, gTouchDebugEnabled ? "Touch debug off" : "Touch debug on");
  drawButton(gExportDeckButton, "Export deck text");
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  logTypographySettings("debug screen");
  logPowerAudit("debug screen");
  Serial.println("Debug screen shown.");
}

void renderPowerAudit(const char* refreshReason = "mode switch") {
  gScreen = Screen::PowerAudit;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const int32_t level = batteryLevelPercent();
  const bool wifiOff = WiFi.getMode() == WIFI_OFF;
  const bool bluetoothStarted = btStarted();

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Power Audit", 28, 26);

  applyCoachMetadataFont();
  int32_t y = 82;
  const int32_t lineH = coachTypography().metadataLineHeight + 2;
  auto row = [&](const String& text) {
    display.drawString(text, 28, y);
    y += lineH;
  };

  row(String("Battery: ") + gCachedBatteryMv + "mV  " + level + "%");
  row(String("USB/VBUS: ") + usbPowerName(gCachedVbusMv) + "  " + gCachedVbusMv + "mV");
  row(String("Charge: ") + chargingStateName(gCachedChargingState) + "  " + gCachedBatteryCurrentMa + "mA");
  row(String("Wi-Fi: ") + (wifiOff ? "off" : "on") + "  Bluetooth: " + (bluetoothStarted ? "on" : "off"));
  row("IMU: disabled  Speaker: stopped");
  row(String("CPU: ") + static_cast<unsigned>(ESP.getCpuFreqMHz()) + " MHz");
  row(String("Power mode: ") + powerModeName());
  row(String("Badge sleep: ") + badgeSleepModeName());
  row(String("Sleep enabled: ") + (gSettings.badgeSleepMode == BadgeSleepMode::Off ? "no" : "yes"));
  row(String("Last sleep: ") + gLastSleepAttempt);
  row(String("Wake reason: ") + gLastWakeReason);
  row(String("Millis since boot: ") + static_cast<unsigned>(millis()));
  row(String("Refresh count: ") + static_cast<unsigned>(gDisplayRefreshCount));
  row(String("Badge redraws: ") + static_cast<unsigned>(gBadgeRedrawCount));
  row(String("Last refresh: ") + gLastRefreshReason);
  row(String("Last input ms: ") + static_cast<unsigned>(gLastUserActivityMs));
  row(String("Power poll age ms: ") + static_cast<unsigned>(millis() - gLastPowerPollMs));
  if (gSettings.badgeSleepMode == BadgeSleepMode::DeepExperiment) {
    row("Deep sleep: blocked until touch wake verified");
  }

  gHomeButton = {26, display.height() - 94, display.width() - 52, 58};
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  logPowerAudit("power audit screen");
  Serial.println("Power audit screen shown.");
}

void renderVisualQa(const char* refreshReason = "mode switch") {
  gScreen = Screen::VisualQa;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const uint16_t darkGray = metadataTextColor();
  static constexpr const char* checklist[] = {
      "Practice first page",
      "Practice long answer page",
      "Practice last page",
      "Drills MCQ screen",
      "Settings battery area",
      "Font Lab comparison",
      "Badge English",
      "Badge Japanese",
  };

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Visual QA", 28, 26);

  applyCoachMetadataFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString(String("Reader: ") + fontSizeModeName() + " / " + fontStyleModeName(), 30, 84);
  display.drawString(String("Refresh: ") + refreshModeName() + "  Power: " + powerModeName(), 30, 112);
  display.drawString(String("Custom font: ") + (gReaderMidVlwAvailable ? "yes" : "no"), 30, 140);
  display.drawString(kReaderMidVlwPath, 30, 166);

  applySansBoldFont(24);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  int32_t y = 210;
  display.drawString("Photograph these screens:", 30, y);
  y += 52;
  for (size_t index = 0; index < countOf(checklist); ++index) {
    display.drawRect(34, y + 8, 24, 24, TFT_BLACK);
    display.drawRect(35, y + 9, 22, 22, TFT_BLACK);
    display.drawString(checklist[index], 74, y);
    y += 54;
  }

  applyCoachMetadataFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString("Use this as the screenshot/photo checklist after flashing.", 30, y + 12);

  gHomeButton = {26, display.height() - 94, display.width() - 52, 58};
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  Serial.printf("Visual QA shown: reader=%s style=%s refresh=%s power=%s customFont=%s checklist=%u\n",
                fontSizeModeName(), fontStyleModeName(), refreshModeName(), powerModeName(),
                gReaderMidVlwAvailable ? "yes" : "no", static_cast<unsigned>(countOf(checklist)));
}

void renderHelpLegend(const char* refreshReason = "mode switch") {
  gScreen = Screen::HelpLegend;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  static constexpr const char* lines[] = {
      "Must = important card",
      "Card = standard card",
      "Question = interview prompt",
      "Answer = spoken answer draft",
      "Anchor = memory cue / key points",
      "Watch-out = mistake to avoid",
      "Follow-up = likely interviewer push",
      "Defense = strong response principle",
      "Tap upper half = previous page",
      "Tap lower half = next page",
      "Arrows = previous/next card",
      "Evidence-backed = concrete evidence, metric, or example",
      "Directional = usable, but caveat causality",
      "Needs proof = risky unless supported by data",
      "Avoid / Reframe = weak, defensive, or overclaimed",
  };

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Help / Legend", 28, 26);

  applyCoachMetadataFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  int32_t y = 92;
  for (size_t index = 0; index < countOf(lines); ++index) {
    display.drawString(lines[index], 34, y);
    y += coachTypography().metadataLineHeight + 6;
  }

  gHomeButton = {26, display.height() - 94, display.width() - 52, 58};
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  Serial.println("Help legend shown.");
}

void drawFontLabProbeRow(FontStyleMode style, FontSizeMode size, const char* label, const char* sample, int32_t y,
                         bool disabled = false) {
  auto& display = M5.Display;
  const FontStyleMode savedStyle = gSettings.fontStyleMode;
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  const uint16_t darkGray = metadataTextColor();
  const FontProbe probe = measureTypography(style, size, sample);

  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);
  String meta = String(label) + "  h" + probe.height + " w" + probe.width + " lh" + probe.lineHeight;
  if (disabled) {
    meta += "  COLLIDES";
  }
  display.drawString(meta, kCoachMargin, y);
  display.drawString(String(probe.fontName) + " / " + probe.fontType, kCoachMargin, y + 22);
  if (disabled) {
    gSettings.fontStyleMode = savedStyle;
    gSettings.fontSizeMode = savedSize;
    return;
  }

  gSettings.fontStyleMode = style;
  gSettings.fontSizeMode = size;
  const CoachTypography sampleType = coachTypography();
  applyCoachContentFont();
  display.setTextColor(disabled ? darkGray : TFT_BLACK, TFT_WHITE);
  drawWrappedText(sample, kCoachMargin, y + 42, display.width() - kCoachMargin * 2, sampleType.bodyLineHeight, 1,
                  "font-lab-probe", 1);

  gSettings.fontStyleMode = savedStyle;
  gSettings.fontSizeMode = savedSize;
}

void drawFontLabVlwProbe(const char* path, const char* label, int32_t y) {
  auto& display = M5.Display;
  static constexpr const char* kSample = "highest-volume, highest-conversion intent";
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);

  if (!gSdMounted) {
    display.drawString(String(label) + ": SD not mounted. Path " + path, kCoachMargin, y);
    Serial.printf("Font Lab VLW: label=%s status=sd-not-mounted path=%s\n", label, path);
    return;
  }
  if (!SD.exists(path)) {
    display.drawString(String(label) + ": missing " + path, kCoachMargin, y);
    Serial.printf("Font Lab VLW: label=%s status=missing path=%s\n", label, path);
    return;
  }

  if (!display.loadFont(SD, path)) {
    display.drawString(String(label) + ": load failed " + path, kCoachMargin, y);
    Serial.printf("Font Lab VLW: label=%s status=load-failed path=%s\n", label, path);
    return;
  }

  display.setTextSize(1);
  const int32_t height = display.fontHeight();
  const int32_t width = display.textWidth(kSample);
  const char* type = display.getFont() != nullptr ? fontTypeName(display.getFont()->getType()) : "none";
  display.drawString(String(label) + "  h" + height + " w" + width + "  " + type, kCoachMargin, y);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString(kSample, kCoachMargin, y + 26);
  Serial.printf("Font Lab VLW: label=%s status=loaded path=%s type=%s height=%ld width=%ld fallback=no\n", label, path, type,
                static_cast<long>(height), static_cast<long>(width));
  display.unloadFont();
}

void renderFontLab(const char* refreshReason = "mode switch") {
  gScreen = Screen::FontLab;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const CoachTypography type = coachTypography();
  const uint16_t darkGray = metadataTextColor();
  const char* english = "How would you explain the product impact without overclaiming causality?";
  const char* shortSample = "Define impact without overclaiming causality";
  const char* intentSample = "highest-volume, highest-conversion intent";
  const char* optionSample = "A. Define cohort + period";
  const String paragraph =
      String(english) + " " + intentSample + ". " + optionSample + ".";

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Font Lab", 28, 24);

  applyCoachMetadataFont();
  display.setTextColor(darkGray, TFT_WHITE);
  display.drawString(String("Style: ") + fontStyleModeName(), 30, 76);
  display.drawString(String("Size: ") + fontSizeModeName() + "  Spacing: " + lineSpacingModeName(), 30, 104);
  display.drawString(String("Contrast: ") + contrastModeName(), 30, 128);

  int32_t y = 154;
  drawFontLabProbeRow(gSettings.fontStyleMode, canonicalFontSizeMode(gSettings.fontSizeMode), "Current app",
                      shortSample, y);
  y += 86;
  drawFontLabProbeRow(FontStyleMode::HighContrast, FontSizeMode::Medium, "Best bitmap Reader S", optionSample, y);
  y += 76;
  drawFontLabProbeRow(FontStyleMode::HighContrast, FontSizeMode::Large, "Best bitmap Reader M", intentSample, y);
  y += 86;
  drawFontLabVlwProbe(kReaderMidVlwPath, "Reader Mid SD VLW", y);
  y += 58;
  drawFontLabProbeRow(FontStyleMode::HighContrast, FontSizeMode::XL, "Best bitmap Reader L", shortSample, y);
  y += 96;
  drawFontLabProbeRow(FontStyleMode::HighContrast, FontSizeMode::XXL, "Legacy XXL disabled", shortSample, y, true);
  y += 44;
  drawFontLabVlwProbe(kReaderVlwPath, "Generic reader SD VLW", y);
  y += 34;

  applyCoachContentFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  drawWrappedText(paragraph, kCoachMargin, y, display.width() - kCoachMargin * 2, type.bodyLineHeight,
                  linesThatFit(120, type.bodyLineHeight, 2), "font-lab-paragraph", 1);

  gFontLabStyleButton = {26, display.height() - 280, display.width() - 52, 44};
  gFontLabSizeButton = {26, display.height() - 230, display.width() - 52, 44};
  gFontLabContrastButton = {26, display.height() - 180, display.width() - 52, 44};
  gFontLabLineSpacingButton = {26, display.height() - 130, display.width() - 52, 44};
  gHomeButton = {26, display.height() - 82, display.width() - 52, 58};

  drawButton(gFontLabStyleButton, String("Style: ") + fontStyleModeName());
  drawButton(gFontLabSizeButton, String("Size: ") + fontSizeModeName());
  drawButton(gFontLabContrastButton, String("Contrast: ") + contrastModeName());
  drawButton(gFontLabLineSpacingButton, String("Spacing: ") + lineSpacingModeName());
  drawButton(gHomeButton, "Home");

  finishDisplayRefresh();
  logTypographySettings("font lab");
  logFontEngineDiagnostics("font lab");
  Serial.printf("Font Lab shown: style=%s size=%s contrast=%s lineSpacing=%s fonts=FreeSansBold/FreeMonoBold/JapanGothic\n",
                fontStyleModeName(), fontSizeModeName(), contrastModeName(), lineSpacingModeName());
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
  if (hitTarget(gHomeButton, "home", tapX, tapY)) {
    Serial.println("entering Home");
    renderHome();
    return;
  }
  if (hitTarget(gFilterButton, "previous card", tapX, tapY)) {
    if (hasPreviousCoachItem()) {
      previousCoachItem();
      renderCoachScreen();
    } else {
      Serial.println("practice previous card disabled");
    }
    return;
  }
  if (hitTarget(gNextButton, "next card", tapX, tapY)) {
    if (hasNextCoachItem()) {
      nextCoachItem();
      renderCoachScreen();
    } else {
      Serial.println("practice next card disabled");
    }
    return;
  }

  const CoachItemView item = coachItemAt(gCoachIndex);
  const uint8_t stageCount = interviewStageCount(item);
  if (!gReaderContentRect.contains(tapX, tapY)) {
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (tapY < gReaderContentRect.y + gReaderContentRect.h / 2) {
    markHitTarget("practice previous page", tapX, tapY);
    if (gCoachStage > 0) {
      --gCoachStage;
      renderCoachScreen();
    } else {
      Serial.println("practice previous page disabled");
    }
    return;
  }

  markHitTarget("practice next page", tapX, tapY);
  if (gCoachStage + 1 < stageCount) {
    ++gCoachStage;
    renderCoachScreen();
  } else {
    Serial.println("practice next page disabled");
  }
}

void handleTouch() {
  if (!M5.Touch.isEnabled()) {
    return;
  }

  updateInputLock();
  const auto detail = M5.Touch.getDetail();
  if (gInputLocked) {
    if (detail.wasPressed() || detail.wasReleased() || detail.wasClicked() || detail.wasHold()) {
      gLastIgnoredTouchReason = "input locked";
      Serial.printf("touch ignored: reason=input locked screen=%s font=%s refresh=%s unlockIn=%ld\n",
                    screenName(gScreen), fontSizeModeName(), refreshModeName(),
                    static_cast<long>(gInputUnlockAtMs > millis() ? gInputUnlockAtMs - millis() : 0));
    }
    return;
  }

  if (detail.wasPressed()) {
    gTouchActive = true;
    recordUserActivity("touch press");
    gLastTouchDownX = constrain(detail.x, 0, M5.Display.width());
    gLastTouchDownY = constrain(detail.y, 0, M5.Display.height());
    Serial.printf("touch down coordinates: x=%ld y=%ld screen=%s\n", static_cast<long>(gLastTouchDownX),
                  static_cast<long>(gLastTouchDownY), screenName(gScreen));
  }
  if (detail.wasClicked() || detail.wasReleased()) {
    gTouchActive = false;
    recordUserActivity("touch release");
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
  gHitMatchedThisTap = false;

  if (gScreen == Screen::Badge) {
    if (recordBottomLeftTripleTap(tapX, tapY)) {
      markHitTarget("emergency home", tapX, tapY);
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gQrRect, "qr zoom", tapX, tapY)) {
      renderQrZoom();
    } else if (hitTarget(gPhotoRect, "photo zoom", tapX, tapY)) {
      renderPhotoZoom();
    } else if (gSettings.languageMode == LanguageMode::Manual && isBadgeCenterTap(tapX, tapY)) {
      markHitTarget("badge language toggle", tapX, tapY);
      gBadgeLanguage = gBadgeLanguage == BadgeLanguage::English ? BadgeLanguage::Japanese : BadgeLanguage::English;
      Serial.printf("Badge manual language toggle: %s\n", languageName(gBadgeLanguage));
      saveSettings();
      renderBadge(true, "manual language toggle");
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::QrZoom || gScreen == Screen::PhotoZoom) {
    markHitTarget("zoom exit", tapX, tapY);
    cleanWhiteRefresh("zoom exit");
    renderBadge(false);
    return;
  }

  if (gScreen == Screen::Debug) {
    if (hitTarget(gHelpButton, "help legend", tapX, tapY)) {
      renderHelpLegend();
    } else if (hitTarget(gPowerAuditButton, "power audit", tapX, tapY)) {
      renderPowerAudit();
    } else if (hitTarget(gVisualQaButton, "visual qa", tapX, tapY)) {
      renderVisualQa();
    } else if (hitTarget(gFontLabButton, "font lab", tapX, tapY)) {
      renderFontLab();
    } else if (hitTarget(gTypographyResetButton, "reset typography", tapX, tapY)) {
      resetTypographyDefaults();
      saveSettings();
      renderDebug("typography reset");
    } else if (hitTarget(gLayoutDebugButton, "layout log", tapX, tapY)) {
      logCurrentLayoutDiagnostics("debug button");
      renderDebug();
    } else if (hitTarget(gRenderTraceButton, "render trace dump", tapX, tapY)) {
      dumpCurrentRenderTraceToSd();
      renderDebug("render trace dump");
    } else if (hitTarget(gTouchDebugButton, "touch debug", tapX, tapY)) {
      gTouchDebugEnabled = !gTouchDebugEnabled;
      renderDebug();
    } else if (hitTarget(gExportDeckButton, "export deck text", tapX, tapY)) {
      exportDeckTextToSd();
      renderDebug("deck export");
    } else if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (gTouchDebugEnabled) {
      markHitTarget("touch debug canvas", tapX, tapY);
      renderDebug();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::PowerAudit) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::HelpLegend) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::VisualQa) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::FontLab) {
    if (hitTarget(gFontLabStyleButton, "font lab style", tapX, tapY)) {
      cycleFontStyleMode();
      saveSettings();
      renderFontLab("font style switch");
    } else if (hitTarget(gFontLabSizeButton, "font lab size", tapX, tapY)) {
      cycleFontSizeMode();
      saveSettings();
      renderFontLab("font size switch");
    } else if (hitTarget(gFontLabContrastButton, "font lab contrast", tapX, tapY)) {
      cycleContrastMode();
      saveSettings();
      renderFontLab("contrast switch");
    } else if (hitTarget(gFontLabLineSpacingButton, "font lab line spacing", tapX, tapY)) {
      cycleLineSpacingMode();
      saveSettings();
      renderFontLab("line spacing switch");
    } else if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::PracticeMenu) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gPracticeMustButton, "practice must cards", tapX, tapY)) {
      startPracticeMode(true, false);
      renderCoachScreen();
    } else if (hitTarget(gPracticeAllButton, "practice all cards", tapX, tapY)) {
      startPracticeMode(false, false);
      renderCoachScreen();
    } else if (hitTarget(gPracticeContinueButton, "practice continue", tapX, tapY)) {
      startPracticeMode(gInterviewMustMasterOnly, true);
      renderCoachScreen();
    } else if (hitTarget(gPracticeHelpButton, "practice help legend", tapX, tapY)) {
      renderHelpLegend();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::GlossaryMenu) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gGlossaryAiButton, "glossary ai rag", tapX, tapY)) {
      gGlossaryCategory = GlossaryCategory::AiRag;
      startCoachMode(Screen::Glossary);
      renderCoachScreen();
    } else if (hitTarget(gGlossaryEvalsButton, "glossary evals", tapX, tapY)) {
      gGlossaryCategory = GlossaryCategory::Evals;
      startCoachMode(Screen::Glossary);
      renderCoachScreen();
    } else if (hitTarget(gGlossaryMetricsButton, "glossary metrics", tapX, tapY)) {
      gGlossaryCategory = GlossaryCategory::Metrics;
      startCoachMode(Screen::Glossary);
      renderCoachScreen();
    } else if (hitTarget(gGlossaryProductButton, "glossary product", tapX, tapY)) {
      gGlossaryCategory = GlossaryCategory::Product;
      startCoachMode(Screen::Glossary);
      renderCoachScreen();
    } else if (hitTarget(gGlossaryInterviewButton, "glossary interview", tapX, tapY)) {
      gGlossaryCategory = GlossaryCategory::Interview;
      startCoachMode(Screen::Glossary);
      renderCoachScreen();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::DrillsMenu) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gDrillAllButton, "all drills", tapX, tapY)) {
      gDrillCategory = DrillCategory::All;
      startCoachMode(Screen::Drills);
      renderCoachScreen();
    } else if (hitTarget(gDrillWeakButton, "weak answer drill", tapX, tapY)) {
      gDrillCategory = DrillCategory::WeakAnswer;
      startCoachMode(Screen::Drills);
      renderCoachScreen();
    } else if (hitTarget(gDrillMetricButton, "metric precision drill", tapX, tapY)) {
      gDrillCategory = DrillCategory::MetricPrecision;
      startCoachMode(Screen::Drills);
      renderCoachScreen();
    } else if (hitTarget(gDrillFollowupButton, "follow-up defense drill", tapX, tapY)) {
      gDrillCategory = DrillCategory::FollowupDefense;
      startCoachMode(Screen::Drills);
      renderCoachScreen();
    } else if (hitTarget(gDrillFrameworkButton, "framework choice drill", tapX, tapY)) {
      gDrillCategory = DrillCategory::FrameworkChoice;
      startCoachMode(Screen::Drills);
      renderCoachScreen();
    } else if (hitTarget(gDrillMaturityButton, "maturity claim drill", tapX, tapY)) {
      gDrillCategory = DrillCategory::MaturityClaim;
      startCoachMode(Screen::Drills);
      renderCoachScreen();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::Exam) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
      noteIgnoredIfNoHit(tapX, tapY);
      return;
    }
    if (!gExamActive && !gExamSummary) {
      if (hitTarget(gExamStart10Button, "exam 10", tapX, tapY)) {
        startExam(10);
      } else if (hitTarget(gExamStart5Button, "exam 5", tapX, tapY)) {
        startExam(5);
      } else if (hitTarget(gExamReviewButton, "exam review", tapX, tapY)) {
        renderExamSummary("exam review");
      }
      noteIgnoredIfNoHit(tapX, tapY);
      return;
    }
    if (gExamSummary) {
      noteIgnoredIfNoHit(tapX, tapY);
      return;
    }
    if (gExamActive && gExamCount > 0 && gExamPosition < gExamCount) {
      const CoachItemView item = coachItemAt(gExamItemIndices[gExamPosition]);
      for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
        const String optionTarget = String("exam option ") + static_cast<char>('A' + option);
        if (hitTarget(gOptionButtons[option], optionTarget.c_str(), tapX, tapY)) {
          recordDrillAnswer(item, option);
          if (option == item.correctIndex) {
            ++gExamCorrect;
          } else if (gLastExamMissCount < kMaxExamQuestions) {
            copyToBuffer(gLastExamMissIds[gLastExamMissCount], sizeof(gLastExamMissIds[gLastExamMissCount]), item.id);
            copyToBuffer(gLastExamMissCategories[gLastExamMissCount],
                         sizeof(gLastExamMissCategories[gLastExamMissCount]), drillHeaderLabel(item));
            ++gLastExamMissCount;
          }
          ++gExamPosition;
          gCoachStage = 0;
          if (gExamPosition >= gExamCount) {
            finishExam();
            renderExamSummary();
          } else {
            renderExamQuestion("exam next question");
          }
          return;
        }
      }
      if (gReaderContentRect.contains(tapX, tapY)) {
        const FontSizeMode savedSize = gSettings.fontSizeMode;
        const FontSizeMode renderSize = coachReaderSizeFor(item);
        gSettings.fontSizeMode = renderSize;
        const PracticeLayout layout = practiceLayoutFor(renderSize);
        applyCoachContentFont();
        applyCoachButtonFont();
        const DrillPagePlan plan = buildDrillPagePlan(item, layout);
        gSettings.fontSizeMode = savedSize;
        if (tapY < gReaderContentRect.y + gReaderContentRect.h / 2) {
          markHitTarget("exam previous page", tapX, tapY);
          if (gCoachStage > 0) {
            --gCoachStage;
            renderExamQuestion("exam previous page");
          }
        } else {
          markHitTarget("exam next page", tapX, tapY);
          if (gCoachStage + 1 < plan.totalPages) {
            ++gCoachStage;
            renderExamQuestion("exam next page");
          }
        }
        return;
      }
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::Results) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (isCoachScreen(gScreen)) {
    if (gScreen == Screen::InterviewPractice) {
      handleInterviewPracticeTouch(tapX, tapY);
      return;
    }
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
      return;
    }
    if (hitTarget(gFilterButton, "previous item", tapX, tapY)) {
      if (hasPreviousCoachItem()) {
        previousCoachItem();
        renderCoachScreen();
      } else {
        Serial.println("coach previous item disabled");
      }
      return;
    }
    if (hitTarget(gNextButton, "next item", tapX, tapY)) {
      if (hasNextCoachItem()) {
        nextCoachItem();
        renderCoachScreen();
      } else {
        Serial.println("coach next item disabled");
      }
      return;
    }

    const CoachItemView item = coachItemAt(gCoachIndex);
    if (isOptionDrillScreen(gScreen, item) && gSelectedOption < 0) {
      const CoachItemView item = coachItemAt(gCoachIndex);
      for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
        const String optionTarget = String("option ") + static_cast<char>('A' + option);
        if (hitTarget(gOptionButtons[option], optionTarget.c_str(), tapX, tapY)) {
          recordDrillAnswer(item, option);
          gSelectedOption = option;
          gCoachStage = 0;
          gCoachNeedsCleanEntryRefresh = true;
          Serial.printf("Drill answer selected: item=%s selected=%c best=%c cleanRefresh=queued\n", coachDisplayId(item),
                        static_cast<char>('A' + option), static_cast<char>('A' + item.correctIndex));
          renderCoachScreen();
          break;
        }
      }
      if (gHitMatchedThisTap) {
        return;
      }
    }

    if (gReaderContentRect.contains(tapX, tapY)) {
      const uint8_t pageCount = currentCoachReaderPageCount();
      if (tapY < gReaderContentRect.y + gReaderContentRect.h / 2) {
        markHitTarget("coach previous page", tapX, tapY);
        if (gCoachStage > 0) {
          --gCoachStage;
          renderCoachScreen();
        } else {
          Serial.println("coach previous page disabled");
        }
      } else {
        markHitTarget("coach next page", tapX, tapY);
        if (gCoachStage + 1 < pageCount) {
          ++gCoachStage;
          renderCoachScreen();
        } else {
          Serial.println("coach next page disabled");
        }
      }
      return;
    }

    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::Settings) {
    if (hitTarget(gOrientationButton, "orientation", tapX, tapY)) {
      gSettings.orientationMode =
          gSettings.orientationMode == OrientationMode::Strap ? OrientationMode::Handheld : OrientationMode::Strap;
      saveSettings();
      renderSettings("orientation switch");
    } else if (hitTarget(gLanguageAutoButton, "language mode", tapX, tapY)) {
      cycleLanguageMode();
      saveSettings();
      renderSettings("badge language mode switch");
    } else if (hitTarget(gLanguageEnglishButton, "auto interval", tapX, tapY)) {
      cycleAutoRotateInterval();
      saveSettings();
      renderSettings("badge interval switch");
    } else if (hitTarget(gFontMediumButton, "font medium", tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::Medium;
      saveSettings();
      renderSettings();
    } else if (hitTarget(gFontLargeButton, "font large", tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::Large;
      saveSettings();
      renderSettings();
    } else if (hitTarget(gFontXlButton, "font xl", tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::XL;
      saveSettings();
      renderSettings();
    } else if (hitTarget(gFontStyleButton, "font style", tapX, tapY)) {
      cycleFontStyleMode();
      saveSettings();
      renderSettings("font style switch");
    } else if (hitTarget(gRefreshModeButton, "refresh mode", tapX, tapY)) {
      cycleRefreshMode();
      saveSettings();
      renderSettings("refresh mode switch");
    } else if (hitTarget(gPowerModeButton, "power mode", tapX, tapY)) {
      cyclePowerMode();
      saveSettings();
      applyPowerPolicy("power mode switch");
      renderSettings("power mode switch");
    } else if (hitTarget(gBadgeSleepButton, "badge sleep", tapX, tapY)) {
      cycleBadgeSleepMode();
      saveSettings();
      applyPowerPolicy("badge sleep switch");
      renderSettings("badge sleep switch");
    } else if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::Home) {
    if (hitTarget(gBadgeButton, "badge", tapX, tapY)) {
      Serial.println("returning to Badge");
      renderBadge(true, "mode switch");
    } else if (hitTarget(gPracticeButton, "practice", tapX, tapY)) {
      renderPracticeMenu();
    } else if (hitTarget(gDrillsButton, "drills", tapX, tapY)) {
      renderDrillsMenu();
    } else if (hitTarget(gExamButton, "exam", tapX, tapY)) {
      renderExamMenu();
    } else if (hitTarget(gGlossaryButton, "glossary", tapX, tapY)) {
      renderGlossaryMenu();
    } else if (hitTarget(gResultsButton, "results", tapX, tapY)) {
      renderResultsScreen();
    } else if (hitTarget(gSettingsButton, "settings", tapX, tapY)) {
      renderSettings();
    } else if (hitTarget(gDebugButton, "debug", tapX, tapY)) {
      renderDebug();
    }
    noteIgnoredIfNoHit(tapX, tapY);
  }
}

void maybeSwitchBadgeLanguage() {
  if (gScreen != Screen::Badge) {
    return;
  }

  if (gSettings.languageMode != LanguageMode::Auto) {
    return;
  }
  if (gSettings.powerMode == PowerMode::ConferenceBadge) {
    return;
  }

  const uint32_t intervalSeconds = autoRotateIntervalSeconds();
  const uint32_t intervalMs = intervalSeconds * 1000UL;
  if (intervalSeconds == 0 || millis() - gLastLanguageSwitchMs < intervalMs) {
    return;
  }

  gBadgeLanguage = gBadgeLanguage == BadgeLanguage::English ? BadgeLanguage::Japanese : BadgeLanguage::English;
  Serial.printf("Badge language timer: switching to %s interval=%s\n", languageName(gBadgeLanguage),
                autoRotateIntervalName());
  saveSettings();
  renderBadge(false, "auto language rotate");
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
  gSessionId = static_cast<uint32_t>(ESP.getEfuseMac()) ^ esp_random() ^ millis();
  loadSettings();
  gLastUserActivityMs = millis();
  applyPowerPolicy("boot");

  Serial.println();
  Serial.printf("PaperBadge+ %s boot\n", kFirmwareVersion);
  Serial.printf("PaperCoach session id: %u\n", static_cast<unsigned>(gSessionId));
  const esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  gLastWakeReason = wakeReasonName(wakeReason);
  Serial.printf("Wake reason: %s (%d)\n", wakeReasonName(wakeReason), static_cast<int>(wakeReason));
  Serial.println("Power sleep policy: deep/light sleep deferred until PaperS3 touch wake is physically verified.");
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
  gReaderMidVlwAvailable = gSdMounted && SD.exists(kReaderMidVlwPath);
  Serial.printf("Reader Mid font: path=%s found=%s fallback=%s\n", kReaderMidVlwPath,
                gReaderMidVlwAvailable ? "yes" : "no", gReaderMidVlwAvailable ? "no" : "yes");
  logFontEngineDiagnostics("boot");
  Serial.println("Badge mode defaults to strap 180 orientation unless Settings override is handheld.");

  renderBadge();
}

void loop() {
  M5.update();
  handleTouch();
  maybeSwitchBadgeLanguage();
  maybeEnterPowerIdle();
  maybeEnterBadgeSleep();
  delay(loopDelayMs());
}
