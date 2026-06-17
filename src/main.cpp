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
constexpr const char* kFirmwareVersion = "v5.9-dev3";
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
constexpr uint32_t kStaticScreenIdleMs = 90000;
constexpr uint32_t kBadgeLightSleepIdleMs = 30000;
constexpr uint32_t kBadgeLightSleepDurationUs = 2000000;
constexpr bool kEnableIdleCpuScaling = true;
constexpr uint32_t kActiveCpuMhz = 240;
constexpr uint32_t kIdleCpuMhz = 80;
constexpr uint32_t kPowerPollIntervalMs = 45000;
constexpr uint32_t kPostTouchIdleGuardMs = 2000;
constexpr uint32_t kHardCleanTransitionLimit = 16;      // Fast: clean every ~16 non-clean transitions
constexpr uint32_t kBalancedCleanTransitionLimit = 10;  // Balanced: clean every ~10 transitions
constexpr int32_t kHitboxPadding = 10;
constexpr size_t kMaxCoachItems = 180;
constexpr size_t kMaxSdGlossaryTerms = 56;
constexpr size_t kMaxSessionResults = 160;
constexpr uint8_t kMaxExamQuestions = 10;
constexpr size_t kMaxExamPool = 180;
constexpr uint8_t kMaxOptions = 4;
constexpr uint8_t kMaxWrappedLines = 18;
constexpr uint8_t kMaxReaderPageCount = 32;
constexpr size_t kMaxJapaneseResults = 64;
constexpr int32_t kCoachMargin = 20;
constexpr int32_t kCoachHeaderBottom = 132;
constexpr const char* kHeaderSep = " | ";

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
  Advanced,
  Debug,
  PowerAudit,
  PowerLab,
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
  InterviewMenu,
  JapaneseMenu,
  JapaneseSourceSelect,
  JapaneseWeekSelect,
  JapaneseDaySelect,
  JapaneseDaily,
  JapaneseReference,
  JapaneseResults,
  JapaneseMockTest,
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

enum class PowerProfile : uint8_t {
  Balanced = 0,
  Aggressive = 1,
  BadgeMax = 2,
};

enum class PowerStage : uint8_t {
  Active = 0,
  WarmIdle = 1,
  LightNap = 2,
  Hibernate = 3,
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
  int32_t questionLineHeight = 48;
  uint8_t linesPerPage = 12;
  uint8_t questionLinesPerPage = 11;
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

// Embedded Japanese Daily Questions item — Week 1 Day 1 sample only. const char* fields point at
// flash string literals (no heap allocation), independent of CoachItem/CoachItemView.
struct JapaneseItem {
  const char* sourceId;
  const char* bookId;
  const char* jlptLevel;
  uint8_t week;
  uint8_t day;
  const char* lessonId;
  const char* itemId;
  uint16_t sourceQuestionNumber;
  const char* categoryJapanese;   // もじ / ごい / ぶんぽう
  const char* macroArea;          // kanji / vocabulary / grammar
  const char* promptJapanese;
  const char* choiceJapanese[4];
  uint8_t correctChoice;          // 0-3
  const char* answerSentenceJapanese;
  const char* explanationJapanese;
  const char* explanationEnglish;
  const char* grammarPattern;
  const char* vocabularyItems;
  const char* kanjiItems;
  const char* conceptIds;
};

// RAM-only result record for Japanese Daily Questions — intentionally separate from
// SessionResult/gSessionResults (Interview Practice/Drills/Exam), per the requirement that
// Japanese results never appear inside Interview Results.
struct JapaneseSessionResult {
  uint32_t millisAt = 0;
  char itemId[24] = {};
  char sourceId[24] = {};
  char macroArea[16] = {};
  char categoryJapanese[16] = {};
  uint8_t week = 0;
  uint8_t day = 0;
  uint8_t selectedChoice = 0;
  uint8_t correctChoice = 0;
  bool correct = false;
};

// Embedded Japanese Daily Questions dataset — Week 1 Day 1 sample only (originally written,
// N3-style; not extracted from any copyrighted book). Covers もじ (kanji)/ごい (vocabulary)/
// ぶんぽう (grammar). Do not import the full 500-question book here.
static const JapaneseItem kJapaneseDayItems[] = {
    {"n3sample_w1d1_001", "n3_sample_w1d1", "N3", 1, 1, "w1d1_kanji", "w1d1_q001", 1, "もじ", "kanji",
     "「郵便局」の読み方として正しいものはどれですか。",
     {"ゆうびんきょく", "ゆうべんきょく", "ゆびんきょく", "ゆうびんきょうく"}, 0,
     "彼は郵便局へ荷物を取りに行きました。",
     "「郵便局」は「ゆうびんきょく」と読みます。郵便局は荷物や手紙を送る場所です。",
     "Post office is read 'yuubinkyoku'. It is a place to send packages and letters.", "",
     "郵便局,荷物", "郵,便,局", "kanji_yuubinkyoku"},
    {"n3sample_w1d1_002", "n3_sample_w1d1", "N3", 1, 1, "w1d1_kanji", "w1d1_q002", 2, "もじ", "kanji",
     "「荷物」の読み方として正しいものはどれですか。",
     {"にもつ", "かもつ", "にぶつ", "かぶつ"}, 0,
     "引っ越したので、荷物がたくさんあります。",
     "「荷物」は「にもつ」と読みます。引っ越しの時は荷物が多くなります。",
     "Luggage/baggage is read 'nimotsu'. There is a lot of luggage when moving.", "",
     "荷物,引っ越す", "荷,物", "kanji_nimotsu"},
    {"n3sample_w1d1_003", "n3_sample_w1d1", "N3", 1, 1, "w1d1_kanji", "w1d1_q003", 3, "もじ", "kanji",
     "「違っていました」の「違う」の読み方として正しいものはどれですか。",
     {"ちがう", "いちがう", "たがう", "ちかう"}, 0,
     "子供のころに聞いた話と、実際の話は違っていました。",
     "「違う」は「ちがう」と読みます。「違っていました」は過去形です。",
     "'Chigau' means to differ/be wrong. '違っていました' is its past-tense form.", "",
     "違う,子供のころ", "違", "kanji_chigau"},
    {"n3sample_w1d1_004", "n3_sample_w1d1", "N3", 1, 1, "w1d1_vocab", "w1d1_q004", 4, "ごい", "vocabulary",
     "「引っ越した」の意味として正しいものはどれですか。",
     {"住む場所を変えた", "仕事を変えた", "学校を変えた", "名前を変えた"}, 0,
     "先月、新しいアパートに引っ越したばかりです。",
     "「引っ越す」は住む場所を変えることです。「引っ越した」はその過去形です。",
     "'Hikkosu' means to move (change residence). '引っ越した' is its past form.", "",
     "引っ越す", "引,越", "vocab_hikkosu"},
    {"n3sample_w1d1_005", "n3_sample_w1d1", "N3", 1, 1, "w1d1_vocab", "w1d1_q005", 5, "ごい", "vocabulary",
     "「子供のころ」の意味として正しいものはどれですか。",
     {"子供だった時", "子供を産む時", "子供を育てる時", "子供に会う時"}, 0,
     "子供のころ、毎日公園で遊んでいました。",
     "「子供のころ」は「子供だった時」という意味です。",
     "'Kodomo no koro' means 'when one was a child.'", "",
     "子供のころ", "子,供", "vocab_kodomonokoro"},
    {"n3sample_w1d1_006", "n3_sample_w1d1", "N3", 1, 1, "w1d1_vocab", "w1d1_q006", 6, "ごい", "vocabulary",
     "郵便局でできることとして正しいものはどれですか。",
     {"荷物を送る", "映画を見る", "野菜を買う", "お風呂に入る"}, 0,
     "郵便局で荷物を送ってから、買い物に行きました。",
     "郵便局では荷物や手紙を送ることができます。",
     "At the post office, you can send packages and letters.", "",
     "郵便局,荷物", "郵,便,局,荷,物", "vocab_yuubinkyoku_use"},
    {"n3sample_w1d1_007", "n3_sample_w1d1", "N3", 1, 1, "w1d1_grammar", "w1d1_q007", 7, "ぶんぽう", "grammar",
     "（　）に入る正しい言葉を選びなさい。「子供のころ、よくこの公園で遊んだ（　）。」",
     {"ものだ", "ことだ", "はずだ", "べきだ"}, 0,
     "子供のころ、よくこの公園で遊んだものだ。",
     "「～ものだ」は昔の習慣や思い出を懐かしんで言う時に使います。",
     "'~monoda' is used to reminisce about past habits, like 'used to ~.'", "～ものだ", "", "",
     "grammar_monoda"},
    {"n3sample_w1d1_008", "n3_sample_w1d1", "N3", 1, 1, "w1d1_grammar", "w1d1_q008", 8, "ぶんぽう", "grammar",
     "（　）に入る正しい言葉を選びなさい。「弟はゲーム（　）て、勉強しません。」",
     {"をしてばかりい", "をしたところ", "をしてから", "をするはず"}, 0,
     "弟はゲームをしてばかりいて、勉強しません。",
     "「～てばかりいる」は同じことばかりするという意味です。",
     "'~tebakari iru' means doing nothing but ~ / always ~ing.", "～てばかりいる", "", "",
     "grammar_tebakariiru"},
    {"n3sample_w1d1_009", "n3_sample_w1d1", "N3", 1, 1, "w1d1_grammar", "w1d1_q009", 9, "ぶんぽう", "grammar",
     "（　）に入る正しい言葉を選びなさい。「これは大事な物だから、（　）よ。」",
     {"とっちゃいけない", "とってもいい", "とったらしい", "とるそうだ"}, 0,
     "これは大事な物だから、とっちゃいけないよ。",
     "「とっちゃいけない」は「取ってはいけない」のくだけた言い方です。",
     "'Tocchaikenai' is a casual contraction of 'totte wa ikenai' (must not take).",
     "～ちゃいけない（～てはいけない）", "", "", "grammar_tcha_ikenai"},
    {"n3sample_w1d1_010", "n3_sample_w1d1", "N3", 1, 1, "w1d1_grammar", "w1d1_q010", 10, "ぶんぽう", "grammar",
     "（　）に入る正しい言葉を選びなさい。「もうご飯を食べ（　）たよ。」",
     {"ちゃっ", "てい", "ながら", "そうに"}, 0,
     "もうご飯を食べちゃったよ。",
     "「ちゃう」は「てしまう」のくだけた言い方です。「食べちゃった」は「食べてしまった」のことです。",
     "'Chau' is a casual contraction of 'teshimau'. '食べちゃった' = '食べてしまった' (already ate).",
     "～ちゃう（～てしまう）", "", "", "grammar_chau"},
    {"n3sample_w1d1_011", "n3_sample_w1d1", "N3", 1, 1, "w1d1_grammar", "w1d1_q011", 11, "ぶんぽう", "grammar",
     "（　）に入る正しい言葉を選びなさい。「出かける前に、窓を閉め（　）。」",
     {"とこう", "るところ", "たばかり", "るはず"}, 0,
     "出かける前に、窓を閉めとこう。",
     "「とく」は「ておく」のくだけた言い方です。「閉めとこう」は「閉めておこう」のことです。",
     "'Toku' is a casual contraction of 'te oku'. '閉めとこう' = '閉めておこう' (let's close it in advance).",
     "～とく（～ておく）", "", "", "grammar_toku"},
};
constexpr size_t kJapaneseDayItemCount = sizeof(kJapaneseDayItems) / sizeof(kJapaneseDayItems[0]);

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
String formatFeedbackBody(const String& raw);
uint8_t glossaryPageCountFor(const CoachItemView& item, const PracticeLayout& layout);
uint8_t paginateGlossaryLines(const std::vector<GlossaryRenderLine>& lines, const PracticeLayout& layout,
                              uint16_t* pageStarts, uint8_t maxPages);
void buildPracticeLines(const CoachItemView& item, const PracticeLayout& layout,
                        std::vector<GlossaryRenderLine>& lines);
uint8_t practiceStructuredPageCount(const CoachItemView& item, const PracticeLayout& layout);
const char* powerProfileName();
const char* powerStageName(PowerStage stage);
bool isVerboseLogOk();
bool profileAllowsLightSleep();
uint32_t profileIdleScaleThresholdMs();
uint32_t profileLightSleepIdleMs();
uint32_t profileLightSleepDurationUs();
uint32_t profileListenWindowMs();
uint32_t profileBatteryPollMs();
void enterPowerStage(PowerStage newStage);
void renderAdvanced(const char* refreshReason);
CoachItemView coachItemAt(size_t index);
bool isOptionDrillScreen(Screen screen, const CoachItemView& item);

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
Rect gJapaneseButton;
Rect gJapaneseDailyButton;
Rect gJapaneseMockTestButton;
Rect gJapaneseReferenceButton;
Rect gJapaneseResultsButton;
Rect gJapaneseOptionButtons[kMaxOptions];
Rect gJapaneseNextButton;
Rect gJapanesePrevButton;
Rect gJapaneseBackButton;
Rect gJapaneseSourceN3Button;
Rect gJapaneseWeek1Button;
Rect gJapaneseDay1Button;
Rect gSettingsButton;
Rect gDebugButton;
Rect gAdvancedButton;
Rect gOrientationButton;
Rect gOrientationStrapButton;
Rect gRefreshFastButton;
Rect gRefreshBalancedButton;
Rect gRefreshCleanButton;
Rect gPowerResponsiveButton;
Rect gPowerBalancedButton;
Rect gPowerMaxButton;
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
Rect gPowerLabButton;
Rect gPowerProfileButton;
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
bool gDrillShowFeedback = false;    // true = show feedback page; false = show result/options state
uint8_t gDrillLastResultPage = 0;  // result page to restore when returning from feedback
size_t gJapaneseQuestionIndex = 0;
uint8_t gJapaneseNavSource = 0;
uint8_t gJapaneseNavWeek = 1;
uint8_t gJapaneseNavDay = 1;
int8_t gJapaneseSelectedOption = -1;
bool gJapaneseShowFeedback = false;
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
JapaneseSessionResult gJapaneseResults[kMaxJapaneseResults];
size_t gJapaneseResultCount = 0;
uint8_t gResultsPage = 0;
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
bool gIdleCpuScaled = false;
uint32_t gLastIdleScaledAtMs = 0;
uint32_t gLastRestoreAtMs = 0;
String gLastRestoreReason = "none";
uint32_t gPreRenderCpuMhz = kActiveCpuMhz;
bool gPreRenderWasIdleScaled = false;
PowerProfile gPowerProfile = PowerProfile::Balanced;
uint8_t gPowerAuditPage = 0;
// Power Lab event counters — survive across renders
uint32_t gCpuScaleCount = 0;
uint32_t gCpuRestoreCount = 0;
uint32_t gLast80MhzDurationMs = 0;
uint32_t gCumulative80MhzMs = 0;
uint32_t gLongest80MhzMs = 0;
uint8_t gPowerLabPage = 0;
uint32_t gRedrawWhileIdleCount = 0;
uint32_t gLightSleepEnteredCount = 0;
uint32_t gLightSleepWakeCount = 0;
// Power stage ladder — tracks Active/WarmIdle/LightNap/Hibernate transitions
PowerStage gPowerStage = PowerStage::Active;
PowerStage gLastPowerStage = PowerStage::Active;
uint32_t gStageTransitionCount = 0;
uint32_t gStageEnteredAtMs = 0;
uint32_t gLightSleepTotalMs = 0;
uint32_t gLightSleepAttemptCount = 0;
uint32_t gLightSleepFailedCount = 0;
uint32_t gLastLightSleepDurationMs = 0;
uint32_t gLongestLightSleepMs = 0;
bool gHibernateArmed = false;
bool gInputDetectedAfterWake = false;
uint32_t gLastWakeTimestampMs = 0;
bool gInWakeListenWindow = false;
uint32_t gWakeListenWindowEndMs = 0;
uint32_t gWakeListenWindowDurationMs = 0;
uint32_t gWakeListenWindowEnteredCount = 0;
bool gWakeListenWindowTouchDetected = false;
uint32_t gInputLockedAtMs = 0;
uint32_t gInputLockWatchdogCount = 0;
bool gSleepDisabledByLongPress = false;
uint32_t gLongPressEscapeCount = 0;
uint32_t gLastTouchDownMs = 0;
uint32_t gLastTouchUpMs = 0;
uint32_t gInvalidAnswerKeyCount = 0;
String gLastAnswerKeyWarning = "none";
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
void validateCoachAnswerKeys(const char* source);
void applyCoachButtonFont();
void applyCoachQuestionFont();
CoachTypography coachTypography();
const char* coachDisplayId(const CoachItemView& item);
bool itemUsesAnswerKey(const CoachItemView& item);
bool hasValidAnswerKey(const CoachItemView& item);
void logInvalidAnswerKey(const CoachItemView& item, const char* context);
TextLayoutResult wrapTextToLines(const String& text, int32_t width, int32_t lineHeight, uint8_t maxLines,
                                 String* lines);
void logLayoutBox(const char* field, const Rect& box, int32_t usedHeight, uint8_t pageCount, bool overflow);
int32_t coachFooterTop();
void logCurrentLayoutDiagnostics(const char* reason);
void dumpCurrentRenderTraceToSd();
void restoreActiveCpu(const char* reason);

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
         refreshReasonContains(reason, "language") || refreshReasonContains(reason, "orientation") ||
         refreshReasonContains(reason, "japanese entry");
}

bool shouldUseCleanRefresh(const char* reason, bool /*highQuality*/, bool hardCleanTriggered) {
  if (gSettings.refreshMode == RefreshMode::Clean) return true;
  if (hardCleanTriggered) return true;
  if (isImageOrZoomRefresh(reason)) return true;
  // Fast and Balanced: cadence only — highQuality alone does not trigger a clean refresh
  return false;
}

bool chooseRefreshClean(const char* reason, bool highQuality, bool& hardCleanTriggered) {
  const uint32_t nextTransition = gRefreshTransitionCount + 1;
  const uint32_t cleanLimit = (gSettings.refreshMode == RefreshMode::Balanced)
                               ? kBalancedCleanTransitionLimit
                               : kHardCleanTransitionLimit;
  hardCleanTriggered = nextTransition >= cleanLimit;
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

const char* powerModeButtonLabel() {
  switch (gSettings.powerMode) {
    case PowerMode::BatterySaver:
      return "Battery Saver";
    case PowerMode::ConferenceBadge:
      return "Conf. Badge";
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

bool isStaticIdleScreen(Screen screen) {
  switch (screen) {
    case Screen::Home:
    case Screen::PracticeMenu:
    case Screen::GlossaryMenu:
    case Screen::DrillsMenu:
    case Screen::InterviewPractice:
    case Screen::Glossary:
    case Screen::Results:
    case Screen::Settings:
    case Screen::Advanced:
    case Screen::Debug:
    case Screen::PowerAudit:
    case Screen::PowerLab:
    case Screen::VisualQa:
    case Screen::HelpLegend:
    case Screen::FontLab:
    case Screen::InterviewMenu:
    case Screen::JapaneseMenu:
    case Screen::JapaneseSourceSelect:
    case Screen::JapaneseWeekSelect:
    case Screen::JapaneseDaySelect:
    case Screen::JapaneseReference:
    case Screen::JapaneseResults:
    case Screen::JapaneseMockTest:
      return true;
    default:
      return false;
  }
}

// LightNap eligibility: only normal content/display screens — never control or diagnostic screens.
// JapaneseDaily mirrors Drills/Exam: eligible here, but guarded out during pre-answer by
// isAnswerSelectionActive() below.
bool isLightNapEligibleScreen(Screen screen) {
  switch (screen) {
    case Screen::Badge:
    case Screen::Home:
    case Screen::PracticeMenu:
    case Screen::GlossaryMenu:
    case Screen::DrillsMenu:
    case Screen::InterviewPractice:
    case Screen::Glossary:
    case Screen::Results:
    case Screen::Drills:
    case Screen::Exam:
    case Screen::InterviewMenu:
    case Screen::JapaneseMenu:
    case Screen::JapaneseSourceSelect:
    case Screen::JapaneseWeekSelect:
    case Screen::JapaneseDaySelect:
    case Screen::JapaneseDaily:
    case Screen::JapaneseReference:
    case Screen::JapaneseResults:
      return true;
    default:
      return false;
  }
}

// True when the user's next tap could record an exam, drill, or Japanese Daily Question answer —
// never nap here.
bool isAnswerSelectionActive() {
  if (gScreen == Screen::Exam && gExamActive && !gExamSummary) return true;
  if (gScreen == Screen::Drills && gSelectedOption < 0 && gCoachIndex < gCoachItemCount) {
    const CoachItemView item = coachItemAt(gCoachIndex);
    if (isOptionDrillScreen(gScreen, item)) return true;
  }
  if (gScreen == Screen::JapaneseDaily && !gJapaneseShowFeedback) return true;
  return false;
}

const char* lightNapBlockedReason() {
  if (gSettings.badgeSleepMode == BadgeSleepMode::Off) return "sleep off";
  if (!profileAllowsLightSleep()) return "profile Responsive";
  if (!isLightNapEligibleScreen(gScreen)) return "control screen";
  if (isAnswerSelectionActive()) return "answer selection active";
  if (gInputLocked) return "input locked";
  if (gTouchActive) return "touch active";
  return "";
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

String sleepAuditStatusLine() {
  switch (gSettings.badgeSleepMode) {
    case BadgeSleepMode::Light:
      return "enabled: light sleep on idle eligible screens";
    case BadgeSleepMode::DeepExperiment:
      return "blocked: deep wake not verified";
    case BadgeSleepMode::Off:
    default:
      return "off: disabled by setting";
  }
}

int32_t batteryLevelPercent() {
  const uint32_t now = millis();
  if (gLastPowerPollMs == 0 || now - gLastPowerPollMs >= profileBatteryPollMs()) {
    gCachedBatteryMv = M5.Power.getBatteryVoltage();
    gCachedBatteryLevel = M5.Power.getBatteryLevel();
    gCachedBatteryCurrentMa = M5.Power.getBatteryCurrent();
    gCachedVbusMv = M5.Power.getVBUSVoltage();
    gCachedChargingState = M5.Power.isCharging();
    gLastPowerPollMs = now;
    if (isVerboseLogOk()) Serial.printf("Power poll: batteryMv=%d level=%ld currentMa=%ld vbusMv=%d charge=%s\n",
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

bool isVerboseLogOk() {
  if (!gIdleModeActive) return true;
  return gPowerProfile == PowerProfile::Balanced; // Responsive profile: always verbose
}

String idleAuditStatusLine() {
  String status = gIdleModeActive ? "active" : "awake";
  status += " ";
  status += screenName(gScreen);
  status += " loop ";
  status += static_cast<unsigned>(loopDelayMs());
  status += "ms";
  if (isStaticIdleScreen(gScreen)) {
    status += " static-ok";
  }
  return status;
}

String idleScaleBlockedReason() {
  if (!kEnableIdleCpuScaling) return "scaling disabled in firmware";
  if (gIdleCpuScaled) return "already idle-scaled";
  if (gInputLocked) return "input locked";
  if (gTouchActive) return "touch active";
  if (!isStaticIdleScreen(gScreen)) return "screen not static";
  return "";
}

String msSinceIdleScaled() {
  if (gLastIdleScaledAtMs == 0) return "never";
  const uint32_t elapsed = millis() - gLastIdleScaledAtMs;
  if (elapsed >= 1000) {
    return String(elapsed / 1000) + "s ago";
  }
  return String(elapsed) + "ms ago";
}

void restoreActiveCpu(const char* reason) {
  if (!gIdleCpuScaled) {
    return;
  }
  const uint32_t now = millis();
  gLastRestoreAtMs = now;
  gLastRestoreReason = reason && reason[0] != '\0' ? reason : "active";
  if (gLastIdleScaledAtMs > 0) {
    const uint32_t dur = now - gLastIdleScaledAtMs;
    gLast80MhzDurationMs = dur;
    gCumulative80MhzMs += dur;
    if (dur > gLongest80MhzMs) gLongest80MhzMs = dur;
  }
  ++gCpuRestoreCount;
  setCpuFrequencyMhz(kActiveCpuMhz);
  gIdleCpuScaled = false;
  enterPowerStage(PowerStage::Active);
  Serial.printf("Power CPU scale: active %uMHz reason=%s last80dur=%ums cumulative=%ums\n",
                static_cast<unsigned>(ESP.getCpuFreqMHz()), gLastRestoreReason.c_str(),
                static_cast<unsigned>(gLast80MhzDurationMs), static_cast<unsigned>(gCumulative80MhzMs));
}

void maybeScaleIdleCpu(const char* reason) {
  if (!kEnableIdleCpuScaling || gIdleCpuScaled || !isStaticIdleScreen(gScreen)) {
    return;
  }
  setCpuFrequencyMhz(kIdleCpuMhz);
  gIdleCpuScaled = ESP.getCpuFreqMHz() <= kIdleCpuMhz;
  if (gIdleCpuScaled) {
    gLastIdleScaledAtMs = millis();
    ++gCpuScaleCount;
    enterPowerStage(PowerStage::WarmIdle);
  }
  Serial.printf("Power CPU scale: idle requested=%uMHz actual=%uMHz profile=%s reason=%s active=%s scaleCount=%u\n",
                static_cast<unsigned>(kIdleCpuMhz), static_cast<unsigned>(ESP.getCpuFreqMHz()),
                powerProfileName(), reason && reason[0] != '\0' ? reason : "idle", gIdleCpuScaled ? "yes" : "no",
                static_cast<unsigned>(gCpuScaleCount));
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
  const String sleepStatus = sleepAuditStatusLine();
  const bool wifiOff = WiFi.getMode() == WIFI_OFF;
  const bool bluetoothStarted = btStarted();
  const bool badgeStatic = gSettings.powerMode == PowerMode::ConferenceBadge ||
                           gSettings.languageMode != LanguageMode::Auto || autoRotateIntervalSeconds() == 0;
  const uint32_t activeLoopDelayMs = loopDelayMs();

  Serial.printf(
      "Power audit: reason=%s mode=%s profile=%s idle=%s batteryMv=%d level=%ld charge=%s currentMa=%ld vbusMv=%d usb=%s "
      "wifi=%s bluetooth=%s imu=disabled speaker=stopped sd=%s refreshMode=%s idleStatus=\"%s\" badgeMode=%s "
      "badgeLang=%s autoInterval=%s staticBadge=%s cpuMhz=%u idleCpuScaled=%s badgeSleep=%s sleepStatus=\"%s\" lastSleep=\"%s\" "
      "lastWake=%s millis=%u redraws=%u refreshes=%u badgeRedraws=%u lastRefreshReason=\"%s\" lastInputMs=%u "
      "loopDelayMs=%u\n",
      reason && reason[0] != '\0' ? reason : "audit", powerModeName(), powerProfileName(), gIdleModeActive ? "yes" : "no",
      static_cast<int>(batteryMv), static_cast<long>(level), chargingStateName(gCachedChargingState),
      static_cast<long>(currentMa), static_cast<int>(vbusMv), usbPowerName(vbusMv), wifiOff ? "off" : "on",
      bluetoothStarted ? "on" : "off", gSdMounted ? "mounted" : "missing", refreshModeName(),
      idleAuditStatusLine().c_str(), languageModeName(), languageName(gBadgeLanguage), autoRotateIntervalName(),
      badgeStatic ? "yes" : "no", static_cast<unsigned>(ESP.getCpuFreqMHz()), gIdleCpuScaled ? "yes" : "no",
      badgeSleepModeName(),
      sleepStatus.c_str(), gLastSleepAttempt.c_str(), gLastWakeReason.c_str(), static_cast<unsigned>(millis()),
      static_cast<unsigned>(gDisplayRefreshCount),
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
  restoreActiveCpu(reason);
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
  restoreActiveCpu(reason);
  gLastUserActivityMs = millis();
  gInputDetectedAfterWake = false;
  if (gInWakeListenWindow) {
    gInWakeListenWindow = false;
    gWakeListenWindowTouchDetected = true;
    Serial.printf("Wake listen window: closed by activity reason=%s\n",
                  reason && reason[0] != '\0' ? reason : "touch");
  }
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
  maybeScaleIdleCpu(reason);
  logPowerAudit(reason);
}

void maybeEnterPowerIdle() {
  if (gInputLocked || gTouchActive || gLastUserActivityMs == 0) {
    return;
  }

  const uint32_t elapsedMs = millis() - gLastUserActivityMs;
  if (elapsedMs < kPostTouchIdleGuardMs) {
    return;
  }

  const uint32_t profileThresholdMs = profileIdleScaleThresholdMs();
  if (isStaticIdleScreen(gScreen) && elapsedMs >= profileThresholdMs) {
    enterIdleMode("static screen light idle");
  } else if (gSettings.powerMode == PowerMode::BatterySaver && elapsedMs >= kBatterySaverIdleMs) {
    enterIdleMode("battery saver inactivity");
  } else if (gSettings.powerMode == PowerMode::ConferenceBadge && gScreen == Screen::Badge &&
             elapsedMs >= kConferenceBadgeIdleMs) {
    enterIdleMode("conference badge static display");
  }
}

void maybeEnterBadgeSleep() {
  if (gSettings.badgeSleepMode == BadgeSleepMode::Off || gInputLocked || gTouchActive ||
      gLastUserActivityMs == 0) {
    return;
  }
  // LightNap only on eligible content screens — never on control/diagnostic screens
  if (!isLightNapEligibleScreen(gScreen)) {
    return;
  }
  // Never nap while an exam question or drill option is awaiting a tap
  if (isAnswerSelectionActive()) {
    return;
  }
  // Responsive profile disables light sleep entirely
  if (!profileAllowsLightSleep()) {
    return;
  }
  const uint32_t now = millis();
  // Post-wake listen window: stay awake for a fixed period after each timer wake
  if (gInWakeListenWindow) {
    if (now < gWakeListenWindowEndMs) {
      return;
    }
    gInWakeListenWindow = false;
    Serial.println("Wake listen window: expired");
  }
  const uint32_t lightSleepThresholdMs = profileLightSleepIdleMs();
  if (now - gLastUserActivityMs < lightSleepThresholdMs) {
    return;
  }

  if (gSettings.badgeSleepMode == BadgeSleepMode::DeepExperiment) {
    if (now - gLastSleepAttemptMs >= lightSleepThresholdMs) {
      gLastSleepAttemptMs = now;
      gLastSleepAttempt = "deep blocked: touch wake unverified";
      Serial.println("Hibernate blocked: safe PaperS3 touch wake source is not verified.");
    }
    return;
  }

  // Light sleep timer nap cycle: 2s nap, then wake and poll touch.
  // Normal taps during the nap are missed; tap-and-hold ~2s will be detected.
  if (now - gLastLightSleepMs < 1000) {
    return;  // minimum 1s gap between nap attempts
  }

  ++gLightSleepAttemptCount;
  gLastSleepAttemptMs = now;
  const uint32_t napDurationUs = profileLightSleepDurationUs();
  const uint32_t napDurationS = napDurationUs / 1000000;
  gLastSleepAttempt = String("light nap ") + napDurationS + "s timer";
  disableUnusedRadiosAndPeripherals("light nap entry");
  Serial.printf("Light nap entry: attempt=%u screen=%s idle=%ums napDuration=%us\n",
                static_cast<unsigned>(gLightSleepAttemptCount), screenName(gScreen),
                static_cast<unsigned>(now - gLastUserActivityMs),
                static_cast<unsigned>(napDurationS));
  enterPowerStage(PowerStage::LightNap);
  ++gLightSleepEnteredCount;
  esp_sleep_enable_timer_wakeup(napDurationUs);
  esp_light_sleep_start();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  const uint32_t wakeNow = millis();
  const uint32_t napDur = wakeNow > now ? wakeNow - now : 0;
  gLastLightSleepDurationMs = napDur;
  gLightSleepTotalMs += napDur;
  if (napDur > gLongestLightSleepMs) gLongestLightSleepMs = napDur;
  ++gLightSleepWakeCount;
  gLastLightSleepMs = wakeNow;
  gLastWakeTimestampMs = wakeNow;
  gLastWakeReason = wakeReasonName(esp_sleep_get_wakeup_cause());
  enterPowerStage(PowerStage::WarmIdle);
  Serial.printf("Light nap wake: reason=%s dur=%ums total=%ums wakes=%u\n",
                gLastWakeReason.c_str(), static_cast<unsigned>(napDur),
                static_cast<unsigned>(gLightSleepTotalMs),
                static_cast<unsigned>(gLightSleepWakeCount));
  // Post-wake input guard: block touches for 400ms so a wake tap cannot record an answer
  gInputLocked = true;
  gInputLockedAtMs = wakeNow;
  gInputUnlockAtMs = wakeNow + 400;
  Serial.println("Post-wake input guard: locked 400ms");
  // Poll touch post-wake to detect any held input
  M5.update();
  if (M5.Touch.getCount() > 0) {
    gInputDetectedAfterWake = true;
  }
  // Start post-wake listen window — any touch in this window wakes fully
  const uint32_t listenMs = profileListenWindowMs();
  gWakeListenWindowDurationMs = listenMs;
  gWakeListenWindowEndMs = wakeNow + listenMs;
  gInWakeListenWindow = true;
  gWakeListenWindowTouchDetected = false;
  ++gWakeListenWindowEnteredCount;
  Serial.printf("Wake listen window: %ums started profile=%s\n",
                static_cast<unsigned>(listenMs), powerProfileName());
  if (gInputDetectedAfterWake) {
    gWakeListenWindowTouchDetected = true;
    recordUserActivity("post-wake touch held");
  }
}

uint32_t loopDelayMs() {
  if (gIdleModeActive && gSettings.powerMode == PowerMode::ConferenceBadge && gScreen == Screen::Badge) {
    return 600;
  }
  if (gIdleModeActive && isStaticIdleScreen(gScreen)) {
    if (gPowerProfile == PowerProfile::BadgeMax) return 800;   // Max Battery
    if (gPowerProfile == PowerProfile::Aggressive) return 500; // Balanced
    return 300;                                                 // Responsive
  }
  if (gSettings.powerMode == PowerMode::BatterySaver || gSettings.powerMode == PowerMode::ConferenceBadge) {
    return 120;
  }
  if (gPowerProfile == PowerProfile::Aggressive && isStaticIdleScreen(gScreen)) {
    return 100;  // Balanced profile: slightly slower poll on static screens
  }
  if (gPowerProfile == PowerProfile::BadgeMax && gScreen == Screen::Badge) {
    return 250;  // Max Battery: slower badge loop
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

const char* powerProfileName() {
  switch (gPowerProfile) {
    case PowerProfile::Aggressive:
      return "Balanced";
    case PowerProfile::BadgeMax:
      return "Max Battery";
    case PowerProfile::Balanced:
    default:
      return "Responsive";
  }
}

const char* powerStageName(PowerStage stage) {
  switch (stage) {
    case PowerStage::WarmIdle:  return "WarmIdle";
    case PowerStage::LightNap:  return "LightNap";
    case PowerStage::Hibernate: return "Hibernate";
    case PowerStage::Active:
    default:                    return "Active";
  }
}

void enterPowerStage(PowerStage newStage) {
  if (gPowerStage == newStage) return;
  gLastPowerStage = gPowerStage;
  gPowerStage = newStage;
  ++gStageTransitionCount;
  gStageEnteredAtMs = millis();
}

void cyclePowerProfile() {
  switch (gPowerProfile) {
    case PowerProfile::Balanced:
      gPowerProfile = PowerProfile::Aggressive;
      break;
    case PowerProfile::Aggressive:
      gPowerProfile = PowerProfile::BadgeMax;
      break;
    case PowerProfile::BadgeMax:
    default:
      gPowerProfile = PowerProfile::Balanced;
      break;
  }
}

uint32_t profileIdleScaleThresholdMs() {
  // WarmIdle CPU scaling thresholds per profile
  switch (gPowerProfile) {
    case PowerProfile::Aggressive: return 15000;  // Balanced: WarmIdle@15s
    case PowerProfile::BadgeMax:   return 5000;   // Max Battery: WarmIdle@5s
    case PowerProfile::Balanced:
    default:                        return 30000;  // Responsive: WarmIdle@30s
  }
}

bool profileAllowsLightSleep() {
  // Responsive profile disables light sleep — prioritizes tap responsiveness
  return gPowerProfile != PowerProfile::Balanced;
}

uint32_t profileLightSleepIdleMs() {
  // Time idle before entering LightNap (only used when profileAllowsLightSleep())
  switch (gPowerProfile) {
    case PowerProfile::Aggressive: return 600000;  // Balanced: 10 min
    case PowerProfile::BadgeMax:   return 300000;  // Max Battery: 5 min
    case PowerProfile::Balanced:
    default:                        return 999999999; // Responsive: disabled
  }
}

uint32_t profileLightSleepDurationUs() {
  switch (gPowerProfile) {
    case PowerProfile::Aggressive: return 12000000;  // Balanced: 12s nap
    case PowerProfile::BadgeMax:   return 15000000;  // Max Battery: 15s nap
    case PowerProfile::Balanced:
    default:                        return 12000000;  // Responsive: irrelevant (blocked)
  }
}

// Post-wake listen window: stay awake this long after each timer nap to catch user input.
uint32_t profileListenWindowMs() {
  switch (gPowerProfile) {
    case PowerProfile::BadgeMax:   return 15000;  // Max Battery: 15s
    case PowerProfile::Aggressive: return 12000;  // Balanced: 12s
    case PowerProfile::Balanced:
    default:                        return 10000;  // Responsive: irrelevant (no nap)
  }
}

// Per-profile battery poll interval: aggressive/badge-max poll less often while idle.
uint32_t profileBatteryPollMs() {
  if (gIdleModeActive) {
    switch (gPowerProfile) {
      case PowerProfile::Aggressive:
        return 120000;
      case PowerProfile::BadgeMax:
        return 180000;
      default:
        break;
    }
  }
  return kPowerPollIntervalMs;
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
  const uint8_t powerProfile = gPrefs.getUChar("powerProfile", static_cast<uint8_t>(PowerProfile::Balanced));
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

  // Sleep mode always resets to Off on boot — experimental sleep is not sticky.
  // NVS value is ignored; user must re-enable from Settings or Power Lab each session.
  gSettings.badgeSleepMode = BadgeSleepMode::Off;
  Serial.println("Sleep mode: reset to Off on boot (not sticky)");

  if (powerProfile == static_cast<uint8_t>(PowerProfile::Aggressive)) {
    gPowerProfile = PowerProfile::Aggressive;
  } else if (powerProfile == static_cast<uint8_t>(PowerProfile::BadgeMax)) {
    gPowerProfile = PowerProfile::BadgeMax;
  } else {
    gPowerProfile = PowerProfile::Balanced;
  }

  Serial.printf("Settings loaded: orientation=%s badgeLanguage=%s languageMode=%s autoInterval=%s font=%s style=%s "
                "contrast=%s lineSpacing=%s refresh=%s power=%s badgeSleep=%s profile=%s\n",
                orientationModeName(), languageName(gBadgeLanguage), languageModeName(), autoRotateIntervalName(),
                fontSizeModeName(), fontStyleModeName(), contrastModeName(), lineSpacingModeName(), refreshModeName(),
                powerModeName(), badgeSleepModeName(), powerProfileName());
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
  gPrefs.putUChar("powerProfile", static_cast<uint8_t>(gPowerProfile));
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
  gDrillShowFeedback = false;
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
  validateCoachAnswerKeys(gCoachDeckLoadedFromSd ? "SD" : "embedded");
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
    if (itemUsesAnswerKey(item) && !hasValidAnswerKey(item)) {
      return false;
    }
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
    return item.type == CoachItemType::Mcq && hasValidAnswerKey(item);
  }
  if (screen == Screen::WeakAnswerDetector) {
    return item.type == CoachItemType::WeakAnswer && hasValidAnswerKey(item);
  }
  if (screen == Screen::MetricPrecision) {
    return item.type == CoachItemType::MetricPrecision && hasValidAnswerKey(item);
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

void validateCoachAnswerKeys(const char* source) {
  uint32_t invalid = 0;
  for (size_t index = 0; index < gCoachItemCount; ++index) {
    const CoachItemView item = coachItemAt(index);
    if (!itemUsesAnswerKey(item)) {
      continue;
    }
    if (!hasValidAnswerKey(item)) {
      ++invalid;
      logInvalidAnswerKey(item, source);
    }
  }
  Serial.printf("Answer key validation: source=%s invalid=%u totalWarnings=%u\n",
                source && source[0] != '\0' ? source : "deck", static_cast<unsigned>(invalid),
                static_cast<unsigned>(gInvalidAnswerKeyCount));
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
  gDrillShowFeedback = false;
  gDrillLastResultPage = 0;
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
  gDrillShowFeedback = false;
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
  gDrillShowFeedback = false;
  gDrillLastResultPage = 0;
  // Always clean refresh on card change in Practice to prevent ghosting (v5.8-dev2)
  if (leavingFeedback || gScreen == Screen::InterviewPractice) {
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
  gDrillShowFeedback = false;
  gDrillLastResultPage = 0;
  // Always clean refresh on card change in Practice to prevent ghosting (v5.8-dev2)
  if (leavingFeedback || gScreen == Screen::InterviewPractice) {
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
  gInputLockedAtMs = millis();
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
  if (!gInputLocked) {
    return;
  }
  const uint32_t now = millis();
  // Watchdog: if locked > 8s something went wrong, clear it
  if (gInputLockedAtMs > 0 && now - gInputLockedAtMs > 8000) {
    gInputLocked = false;
    ++gInputLockWatchdogCount;
    Serial.printf("Input lock watchdog: cleared after %ums screen=%s count=%u\n",
                  static_cast<unsigned>(now - gInputLockedAtMs), screenName(gScreen),
                  static_cast<unsigned>(gInputLockWatchdogCount));
    return;
  }
  if (gInputUnlockAtMs == 0 || now < gInputUnlockAtMs) {
    return;
  }
  gInputLocked = false;
  Serial.printf("UI input unlocked: screen=%s font=%s refresh=%s\n", screenName(gScreen), fontSizeModeName(),
                refreshModeName());
}

void prepareFullRefresh(const char* reason = nullptr, bool highQuality = false) {
  restoreActiveCpu(reason && reason[0] != '\0' ? reason : "display refresh");
  auto& display = M5.Display;
  if (reason && reason[0] != '\0') {
    Serial.printf("Refresh requested: %s\n", reason);
  }
  const String renderReason = reason && reason[0] != '\0' ? String(reason) : String("render");
  if (gIdleModeActive) {
    ++gRedrawWhileIdleCount;
  }
  if (gScreen == gLastRenderedScreen && renderReason == gLastRenderedReason) {
    ++gRepeatedRenderCount;
    if (gRepeatedRenderCount >= 3) {
      Serial.printf("Power redraw warning: repeated render screen=%s reason=%s count=%u refreshes=%u lastInputMs=%u "
                    "no state change detected\n",
                    screenName(gScreen), renderReason.c_str(), static_cast<unsigned>(gRepeatedRenderCount),
                    static_cast<unsigned>(gDisplayRefreshCount), static_cast<unsigned>(gLastUserActivityMs));
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
    case Screen::Advanced:
      return "Advanced";
    case Screen::Debug:
      return "Debug";
    case Screen::PowerAudit:
      return "Power Audit";
    case Screen::PowerLab:
      return "Power Lab";
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
    case Screen::InterviewMenu:
      return "Interview";
    case Screen::JapaneseMenu:
      return "Japanese";
    case Screen::JapaneseSourceSelect:
      return "Japanese Source";
    case Screen::JapaneseWeekSelect:
      return "Japanese Week";
    case Screen::JapaneseDaySelect:
      return "Japanese Day";
    case Screen::JapaneseDaily:
      return "Japanese Daily";
    case Screen::JapaneseReference:
      return "Japanese Reference";
    case Screen::JapaneseResults:
      return "Japanese Results";
    case Screen::JapaneseMockTest:
      return "Japanese Mock Test";
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

void applySansFont(uint8_t px) {
  auto& display = M5.Display;
  if (px >= 40) {
    display.setFont(&fonts::FreeSans24pt7b);
  } else if (px >= 31) {
    display.setFont(&fonts::FreeSans18pt7b);
  } else if (px >= 24) {
    display.setFont(&fonts::FreeSans12pt7b);
  } else {
    display.setFont(&fonts::FreeSans9pt7b);
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

void applyBodyFont(uint8_t px) {
  if (gSettings.fontStyleMode == FontStyleMode::DebugMono) {
    applyMonoBoldFont(px);
    return;
  }
  if (gSettings.fontStyleMode == FontStyleMode::LargeReader ||
      gSettings.fontStyleMode == FontStyleMode::SansBoldLike ||
      gSettings.fontStyleMode == FontStyleMode::HighContrast) {
    applySansFont(px);
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

void applyCoachBodyFont() {
  applyBodyFont(coachTypography().bodyPx);
}

void applyCoachQuestionFont() {
  const CoachTypography type = coachTypography();
  const uint8_t questionPx = type.bodyPx >= 34 ? type.bodyPx : static_cast<uint8_t>(type.bodyPx + 4);
  applyTypographyFont(questionPx);
}

void applyCoachButtonFont() {
  applyTypographyFont(coachTypography().buttonPx);
}

void applyCoachFooterFont() {
  applyTypographyFont(coachTypography().footerPx);
}

// Fixed font size for segmented control buttons in Settings chrome.
// 24px (Gothic_24) — larger than before; "Responsive" / "Balanced" fit in a 3-way ~150px button.
static constexpr uint8_t kSegmentedPx = 24;

// Draw a segmented control button. Selected state: 3-rect thick border + fake-bold text.
void drawSegmentedButton(const Rect& rect, const char* label, bool selected) {
  auto& display = M5.Display;
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
  if (selected) {
    display.drawRoundRect(rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4, 6, TFT_BLACK);
  }
  applyTypographyFont(kSegmentedPx);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::middle_center);
  const int32_t cx = rect.x + rect.w / 2;
  const int32_t cy = rect.y + rect.h / 2;
  display.drawString(label, cx, cy);
  if (selected) {
    display.drawString(label, cx + 1, cy);
  }
  display.setTextDatum(textdatum_t::top_left);
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
  static constexpr uint8_t kKatakanaMiddleDot[] = {0xE3, 0x83, 0xBB};

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
               matchesBytes(text, index, kMiddleDot, sizeof(kMiddleDot)) ||
               matchesBytes(text, index, kKatakanaMiddleDot, sizeof(kKatakanaMiddleDot))) {
      replacement = kHeaderSep;
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
      replacement = kHeaderSep;
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

// Japanese-safe text path — fully separate from sanitizeCoachText()/wrapTextToLines(), which
// replace any non-allowlisted multi-byte UTF-8 (including all kanji/kana) with "?". This path
// preserves valid UTF-8 verbatim and never substitutes "?" for Japanese text.
String sanitizeJapaneseText(const String& text) {
  String sanitized;
  sanitized.reserve(text.length());
  for (size_t index = 0; index < text.length();) {
    const uint8_t ch = static_cast<uint8_t>(text[index]);
    if (ch == '\n' || ch == '\t') {
      sanitized += static_cast<char>(ch);
      ++index;
      continue;
    }
    if (ch < 0x20) {
      // Drop other control characters; never touch bytes >= 0x20 (ASCII or UTF-8 continuation).
      ++index;
      continue;
    }
    const uint8_t seqLen = utf8SequenceLength(ch);
    for (uint8_t offset = 0; offset < seqLen && index + offset < text.length(); ++offset) {
      sanitized += text[index + offset];
    }
    index += seqLen;
  }
  return sanitized;
}

// Wraps by UTF-8 code point / measured display width instead of English space-splitting, so
// Japanese text (no spaces between words) wraps correctly. Respects explicit '\n' and never
// splits a multi-byte UTF-8 sequence mid-character.
TextLayoutResult wrapJapaneseTextToLines(const String& text, int32_t width, int32_t lineHeight, uint8_t maxLines,
                                         String* lines) {
  auto& display = M5.Display;
  TextLayoutResult result;
  const String safeText = sanitizeJapaneseText(text);
  String line;

  for (size_t index = 0; index < safeText.length();) {
    const uint8_t ch = static_cast<uint8_t>(safeText[index]);
    if (ch == '\n') {
      result.overflow = appendWrappedLine(lines, result.lineCount, maxLines, line) || result.overflow;
      line = "";
      ++index;
      continue;
    }

    const uint8_t seqLen = utf8SequenceLength(ch);
    String character;
    for (uint8_t offset = 0; offset < seqLen && index + offset < safeText.length(); ++offset) {
      character += safeText[index + offset];
    }

    const String candidate = line + character;
    if (line.length() > 0 && display.textWidth(candidate) > width) {
      result.overflow = appendWrappedLine(lines, result.lineCount, maxLines, line) || result.overflow;
      line = character;
    } else {
      line = candidate;
    }
    index += seqLen;
  }

  result.overflow = appendWrappedLine(lines, result.lineCount, maxLines, line) || result.overflow;
  result.height = static_cast<int32_t>(result.lineCount) * lineHeight;
  return result;
}

int32_t japaneseLineHeight(uint8_t px) {
  return static_cast<int32_t>(px) + 16;
}

// Maps the current Reader S/M/L setting to a Japanese Gothic body size.
// Never routes through FontStyleMode — always uses lgfxJapanGothic_* regardless of the English
// typography setting.
uint8_t japaneseBodyPxForReader() {
  switch (canonicalFontSizeMode(gSettings.fontSizeMode)) {
    case FontSizeMode::Medium: return 24;  // Reader S
    case FontSizeMode::XL:     return 32;  // Reader L
    case FontSizeMode::Large:
    default:                   return 28;  // Reader M
  }
}

// Japanese title: one step above the body size for the current reader setting.
void applyJapaneseTitleFont() {
  const uint8_t bodyPx = japaneseBodyPxForReader();
  applyGothicFont(bodyPx <= 24 ? 28 : (bodyPx <= 28 ? 32 : 36));
}

// Japanese body: defaults to the reader-mapped size; pass an explicit px to override.
void applyJapaneseBodyFont(uint8_t px = 0) {
  applyGothicFont(px > 0 ? px : japaneseBodyPxForReader());
}

// English labels inside Japanese screens — always Sans Bold, independent of FontStyleMode.
void applyJapaneseEnglishLabelFont(uint8_t px) {
  applySansBoldFont(px);
}

// Role-specific Japanese font sizes for headers, prompts, choices, and explanations.
// Separated from japaneseBodyPxForReader() so each role can scale independently.
uint8_t japaneseMetaPxForReader() {
  switch (canonicalFontSizeMode(gSettings.fontSizeMode)) {
    case FontSizeMode::Medium: return 24;
    case FontSizeMode::XL:     return 28;
    case FontSizeMode::Large:
    default:                   return 24;
  }
}
uint8_t japanesePromptPxForReader() {
  switch (canonicalFontSizeMode(gSettings.fontSizeMode)) {
    case FontSizeMode::Medium: return 28;
    case FontSizeMode::XL:     return 36;
    case FontSizeMode::Large:
    default:                   return 32;
  }
}
uint8_t japaneseChoicePxForReader() {
  switch (canonicalFontSizeMode(gSettings.fontSizeMode)) {
    case FontSizeMode::Medium: return 28;
    case FontSizeMode::XL:     return 32;
    case FontSizeMode::Large:
    default:                   return 32;
  }
}
uint8_t japaneseExplanationPxForReader() {
  switch (canonicalFontSizeMode(gSettings.fontSizeMode)) {
    case FontSizeMode::Medium: return 24;
    case FontSizeMode::XL:     return 32;
    case FontSizeMode::Large:
    default:                   return 28;
  }
}
void applyJapaneseMetaFont()        { applyGothicFont(japaneseMetaPxForReader()); }
void applyJapanesePromptFont()      { applyGothicFont(japanesePromptPxForReader()); }
void applyJapaneseChoiceFont()      { applyGothicFont(japaneseChoicePxForReader()); }
void applyJapaneseExplanationFont() { applyGothicFont(japaneseExplanationPxForReader()); }

TextLayoutResult drawJapaneseWrappedText(const String& text, int32_t x, int32_t y, int32_t width, int32_t lineHeight,
                                         uint8_t maxLines, const char* field = nullptr) {
  auto& display = M5.Display;
  String lines[kMaxWrappedLines];
  const uint8_t lineLimit = maxLines > kMaxWrappedLines ? kMaxWrappedLines : maxLines;
  TextLayoutResult result = wrapJapaneseTextToLines(text, width, lineHeight, lineLimit, lines);
  for (uint8_t line = 0; line < result.lineCount; ++line) {
    display.drawString(lines[line], x, y + line * lineHeight);
  }
  if (field != nullptr && field[0] != '\0') {
    Serial.printf("Japanese layout: field=%s lines=%u overflow=%s\n", field,
                  static_cast<unsigned>(result.lineCount), result.overflow ? "yes" : "no");
  }
  return result;
}

void drawJapaneseOptionButton(const Rect& rect, const String& label) {
  auto& display = M5.Display;
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
  const uint8_t px = japaneseChoicePxForReader();
  applyJapaneseChoiceFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);

  String lines[3];
  const int32_t lineH = japaneseLineHeight(px);
  TextLayoutResult layout = wrapJapaneseTextToLines(label, rect.w - 32, lineH, 3, lines);
  const int32_t textBlockH = static_cast<int32_t>(layout.lineCount) * lineH;
  const int32_t textY = rect.y + (rect.h - textBlockH) / 2;
  for (uint8_t line = 0; line < layout.lineCount; ++line) {
    display.drawString(lines[line], rect.x + 18, textY + static_cast<int32_t>(line) * lineH);
  }
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

void appendReaderLine(std::vector<String>& lines, const String& line, bool allowEmpty = false) {
  if (line.length() > 0 || allowEmpty) {
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
      appendReaderLine(lines, line, true);
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
  restoreActiveCpu("render trace append");
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
  restoreActiveCpu("render trace dump");
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
  restoreActiveCpu("deck export");
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
  int32_t currentY = y;
  for (size_t line = startLine; line < endLine; ++line) {
    if (pages.lines[line].length() > 0) {
      display.drawString(pages.lines[line], x, currentY);
    }
    currentY += pages.lines[line].length() == 0 ? lineHeight / 2 : lineHeight;
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

bool itemUsesAnswerKey(const CoachItemView& item) {
  return item.type == CoachItemType::Mcq || item.type == CoachItemType::WeakAnswer ||
         item.type == CoachItemType::MetricPrecision;
}

bool hasValidAnswerKey(const CoachItemView& item) {
  return itemUsesAnswerKey(item) && item.optionCount > 0 && item.optionCount <= kMaxOptions &&
         item.correctIndex < item.optionCount;
}

String optionLetterOrDash(uint8_t option, uint8_t optionCount) {
  if (option < optionCount && option < kMaxOptions) {
    return String(static_cast<char>('A' + option));
  }
  return "-";
}

String optionLabelWithSafeLetter(const CoachItemView& item, uint8_t option) {
  if (option >= item.optionCount || option >= kMaxOptions) {
    return String("- ");
  }
  return String(static_cast<char>('A' + option)) + ". " + coachOptionLabelFor(item, option);
}

void logInvalidAnswerKey(const CoachItemView& item, const char* context) {
  ++gInvalidAnswerKeyCount;
  gLastAnswerKeyWarning = String(coachDisplayId(item)) + " options=" + static_cast<unsigned>(item.optionCount) +
                          " correct=" + static_cast<unsigned>(item.correctIndex);
  Serial.printf("Answer key invalid: context=%s item=%s type=%s optionCount=%u correctIndex=%u\n",
                context && context[0] != '\0' ? context : "validation", coachDisplayId(item), coachTypeName(item.type),
                static_cast<unsigned>(item.optionCount), static_cast<unsigned>(item.correctIndex));
}

bool isOptionDrillScreen(Screen screen, const CoachItemView& item) {
  return (screen == Screen::Drills || screen == Screen::BlitzQuiz || screen == Screen::WeakAnswerDetector ||
          screen == Screen::MetricPrecision) &&
         hasValidAnswerKey(item);
}

bool isChoiceQuestionScreen(Screen screen, const CoachItemView& item) {
  return isOptionDrillScreen(screen, item) || (screen == Screen::Exam && item.optionCount > 0);
}

bool isExamEligibleItem(const CoachItemView& item) {
  return hasValidAnswerKey(item);
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
  // Reader S/M/L applies to content screens — no auto-downgrade for drill/exam.
  // Option buttons cap their own font at 36px via optionTextPxFor.
  const FontSizeMode size = canonicalFontSizeMode(gSettings.fontSizeMode);
  Serial.printf("coachReader: item=%s screen=%s size=%s bodyPx=%u\n",
                coachDisplayId(item), screenName(gScreen), fontSizeModeName(),
                coachTypography().bodyPx);
  return size;
}

String optionLabelWithLetter(const CoachItemView& item, uint8_t option) {
  return optionLabelWithSafeLetter(item, option);
}

int32_t optionLineHeight(uint8_t optionPx) {
  return static_cast<int32_t>(optionPx) + (optionPx >= 31 ? 10 : 8);
}

uint8_t optionTextPxFor(const String& label, int32_t buttonWidth) {
  const CoachTypography type = coachTypography();
  const uint8_t preferredPx = type.bodyPx;  // 40 for Reader L, 31 for M, 24 for S — no cap
  const uint8_t compactPx = type.buttonPx;
  if (preferredPx <= compactPx) {
    return compactPx;
  }

  applyTypographyFont(preferredPx);
  std::vector<String> largeLines;
  wrapReaderTextToLines(label, buttonWidth - 32, largeLines, "option-large-probe");
  if (largeLines.size() <= 2) {
    return preferredPx;
  }

  // Reader L (bodyPx=40): step to 36px; if still >2 lines, stay at 36px and allow 3 lines.
  // Never fall back to 32px — it is too close to Reader M's 31px (gothic_28 vs gothic_32
  // are visually indistinguishable at reading distance).
  if (type.bodyPx > 36) {
    const uint8_t midPx = 36;
    applyTypographyFont(midPx);
    std::vector<String> midLines;
    wrapReaderTextToLines(label, buttonWidth - 32, midLines, "option-mid-probe");
    const bool midFits = midLines.size() <= 2;
    Serial.printf("optionText: L 40->36px label=%.24s midLines=%u fits=%s\n",
                  label.c_str(), static_cast<unsigned>(midLines.size()), midFits ? "yes" : "allow3");
    return midPx;  // 36px always for Reader L; allow 3 lines when needed
  }

  return compactPx;
}

int32_t optionButtonHeightFor(const String& label, int32_t buttonWidth) {
  const uint8_t optionPx = optionTextPxFor(label, buttonWidth);
  applyTypographyFont(optionPx);  // same font as drawing — must match drawOptionButton
  std::vector<String> lines;
  wrapReaderTextToLines(label, buttonWidth - 32, lines, "option-measure");
  const int32_t lineH = optionLineHeight(optionPx);
  const size_t lineCount = lines.empty() ? 1 : lines.size();
  // Snap to tier (1-line/2-line/3-line) so all boxes sharing a screen stay visually uniform.
  const size_t tier = lineCount <= 1 ? 1 : (lineCount <= 2 ? 2 : 3);
  const int32_t vPad = optionPx >= 31 ? 24 : 20;  // balanced top+bottom padding
  const int32_t minH = optionPx >= 31 ? 70 : 60;
  const int32_t tieredH = static_cast<int32_t>(tier) * lineH + vPad;
  return tieredH > minH ? tieredH : minH;
}

void drawOptionButton(const Rect& rect, const String& label) {
  auto& display = M5.Display;
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
  const uint8_t optionPx = optionTextPxFor(label, rect.w);
  // Use regular (non-bold) body font for option text per v5.8-dev2 spec
  applyBodyFont(optionPx);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);

  std::vector<String> lines;
  wrapReaderTextToLines(label, rect.w - 32, lines, "option-button");
  const int32_t lineHeight = optionLineHeight(optionPx);
  // Center text block vertically so top and bottom padding look balanced.
  const int32_t textBlockH = static_cast<int32_t>(lines.size()) * lineHeight;
  const int32_t textY = rect.y + (rect.h - textBlockH) / 2;
  for (size_t line = 0; line < lines.size(); ++line) {
    display.drawString(lines[line], rect.x + 18, textY + static_cast<int32_t>(line) * lineHeight);
  }
  logLayoutBox("option-button", rect, textBlockH + 24, 1, textBlockH + 24 > rect.h);
}

// Compute a shared (uniform) option box height for all options on a screen.
// All boxes are drawn at the same height (max across options) so the layout is visually consistent.
int32_t sharedOptionButtonHeight(const CoachItemView& item, int32_t buttonWidth) {
  const uint8_t optionCount = item.optionCount < kMaxOptions ? item.optionCount : kMaxOptions;
  int32_t maxH = 0;
  for (uint8_t i = 0; i < optionCount; ++i) {
    String label = optionLabelWithLetter(item, i);
    label.trim();
    const int32_t h = optionButtonHeightFor(label, buttonWidth);
    if (h > maxH) maxH = h;
  }
  return maxH > 0 ? maxH : optionButtonHeightFor(String("A. "), buttonWidth);
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

String formatDrillPrompt(const char* rawPrompt) {
  if (!rawPrompt || rawPrompt[0] == '\0') {
    return "";
  }
  const String text = rawPrompt;
  const int colonPos = text.indexOf(": ");
  if (colonPos < 0) {
    return text;
  }
  String after = text.substring(colonPos + 2);
  after.trim();
  if (after.length() < 16 || after.indexOf(' ') < 0) {
    return text;
  }
  const String before = text.substring(0, colonPos + 1);
  return before + "\n" + after;
}

DrillPagePlan buildDrillPagePlan(const CoachItemView& item, const PracticeLayout& layout) {
  DrillPagePlan plan;
  const String formattedPrompt = formatDrillPrompt(item.prompt);
  applyCoachQuestionFont();
  plan.questionPages = buildReaderPages(formattedPrompt, layout.contentW, layout.questionLinesPerPage, "Question");
  plan.optionPageCount = 0;
  plan.reminder = compactQuestionReminder(item.prompt);

  const uint8_t optionCount = item.optionCount < kMaxOptions ? item.optionCount : kMaxOptions;
  if (optionCount == 0) {
    plan.totalPages = plan.questionPages.pageCount;
    return plan;
  }

  int32_t optionHeights[kMaxOptions] = {};
  const int32_t optionGap = 8;
  // Compute per-option heights then normalize to max for visual consistency.
  for (uint8_t option = 0; option < optionCount; ++option) {
    const String label = optionLabelWithLetter(item, option);
    optionHeights[option] = optionButtonHeightFor(label, layout.contentW);
  }
  int32_t maxOptionH = 0;
  for (uint8_t option = 0; option < optionCount; ++option) {
    if (optionHeights[option] > maxOptionH) maxOptionH = optionHeights[option];
  }
  for (uint8_t option = 0; option < optionCount; ++option) {
    optionHeights[option] = maxOptionH;
  }
  int32_t allOptionsHeight = 0;
  for (uint8_t option = 0; option < optionCount; ++option) {
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
  plan.questionBlockHeight = static_cast<int32_t>(plan.questionLineCount) * layout.questionLineHeight;
  const int32_t combinedGap = 14;
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
  const int32_t reminderHeight = coachTypography().metadataLineHeight * 2 + 16;
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
  String result = "Selected\n";
  result += optionLabelWithSafeLetter(item, static_cast<uint8_t>(gSelectedOption));
  result += "\n\nBest\n";
  if (hasValidAnswerKey(item)) {
    result += optionLabelWithSafeLetter(item, item.correctIndex);
  } else {
    result += "-";
  }
  result += "\n\nWhy this is best\n";
  if (strlen(item.explanation) > 0) {
    result += item.explanation;
  } else if (strlen(item.answer) > 0) {
    result += item.answer;
  } else {
    result += "Explanation not available.";
  }
  Serial.printf("Render trace warning: item=%s missing per-option explanations selected=%c best=%s\n",
                coachDisplayId(item), static_cast<char>('A' + gSelectedOption),
                optionLetterOrDash(item.correctIndex, item.optionCount).c_str());
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
  restoreActiveCpu("results persist");
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
    String bestOptionText = result.bestOption < kMaxOptions ? String(static_cast<char>('A' + result.bestOption)) : "-";
    printJsonEscaped(file, bestOptionText.c_str());
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
  const bool validKey = hasValidAnswerKey(item);
  result.bestOption = validKey ? item.correctIndex : 255;
  result.correct = validKey && selectedOption == item.correctIndex;
  result.firstAttempt = firstAttempt;
  copyToBuffer(result.reader, sizeof(result.reader), fontSizeModeName());

  const String bestLetter = optionLetterOrDash(result.bestOption, item.optionCount);
  Serial.printf("Result recorded: session=%u item=%s mode=%s category=%s selected=%c best=%s correct=%s first=%s count=%u\n",
                static_cast<unsigned>(gSessionId), result.itemId, result.mode, result.category,
                static_cast<char>('A' + result.selectedOption), bestLetter.c_str(),
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
    addStage("Feedback", selectedOptionExplanationText(item));
    return count == 0 ? 1 : count;
  }

  if (item.type == CoachItemType::HostileFollowup) {
    addStage("Follow-up", coachPromptFor(item));
    // "Suggested response" separates the interviewer prompt from the candidate answer.
    addStage("Suggested response", formatFeedbackBody(sanitizeCoachText(coachAnswerFor(item), "hostile-answer")));
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
  applyCoachBodyFont();
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

// Conservative formatter for feedback/explanation body text on e-ink.
// Inserts hard line breaks only for clear structural patterns; leaves prose unchanged.
String formatFeedbackBody(const String& raw) {
  String text = raw;
  text.trim();
  if (text.length() < 10) return text;

  // Numbered list items: "N. " or "N) " preceded by whitespace or start of string.
  // Example: "1. First 2. Second" → "1. First\n2. Second"
  // Skips decimal numbers: "3.14" is not at a word break.
  {
    String result;
    const int len = (int)text.length();
    for (int i = 0; i < len; ++i) {
      const char c = text[i];
      if (isDigit(c) && i + 2 < len) {
        const char n1 = text[i + 1];
        const char n2 = text[i + 2];
        if ((n1 == '.' || n1 == ')') && n2 == ' ') {
          const bool atBreak = (i == 0) || text[i - 1] == ' ' || text[i - 1] == '\n';
          if (atBreak && i > 0 && result.length() > 0 && result[result.length() - 1] != '\n') {
            result += '\n';
          }
        }
      }
      result += c;
    }
    text = result;
  }

  // Semicolon clause splitting: if 2+ "; " occurrences, render each clause on its own line.
  // Example: "a; b; c" → "a\nb\nc"
  {
    int count = 0;
    int pos = 0;
    while ((pos = text.indexOf("; ", pos)) >= 0) { ++count; pos += 2; }
    if (count >= 2) {
      String result;
      int start = 0;
      int found;
      while ((found = text.indexOf("; ", start)) >= 0) {
        result += text.substring(start, found);
        result += '\n';
        start = found + 2;
      }
      result += text.substring(start);
      text = result;
    }
  }

  // Colon-label splitting: restricted to a known allowlist of interview/feedback labels.
  // Only fires when 2+ recognized labels appear in sequence; skips everything else.
  // URLs are safe: "http://..." uses "://" not ": " so they never match the outer condition.
  // Example: "Question: foo Answer: bar" → "Question: foo\nAnswer: bar"
  // Non-example: "outcome: low output: high" → unchanged (not in allowlist)
  {
    static const char* const kAllowedLabels[] = {
      "Q", "A", "Question", "Answer", "Problem", "Fix", "Result",
      "Selected", "Best", "Why", "Risk", "Action", "Example", "Note", nullptr
    };
    struct ColonPos { int labelStart = 0; };
    static constexpr int kMaxColonLabels = 14;
    ColonPos labels[kMaxColonLabels];
    int labelCount = 0;
    const int tlen = (int)text.length();
    for (int i = 1; i < tlen - 1 && labelCount < kMaxColonLabels; ++i) {
      if (text[i] == ':' && text[i + 1] == ' ') {
        // Find label word start (non-space, non-newline chars immediately before colon).
        int ls = i - 1;
        while (ls > 0 && text[ls - 1] != ' ' && text[ls - 1] != '\n') --ls;
        const int labelLen = i - ls;
        // Check against allowlist only.
        bool inList = false;
        for (int k = 0; kAllowedLabels[k]; ++k) {
          if ((int)strlen(kAllowedLabels[k]) == labelLen &&
              strncmp(text.c_str() + ls, kAllowedLabels[k], labelLen) == 0) {
            inList = true;
            break;
          }
        }
        if (inList) {
          labels[labelCount].labelStart = ls;
          ++labelCount;
        }
      }
    }
    if (labelCount >= 2) {
      String result;
      int prev = 0;
      for (int j = 0; j < labelCount; ++j) {
        const int ls = labels[j].labelStart;
        if (ls <= 0) continue;            // label is at very start of text — no newline before it
        if (text[ls - 1] == '\n') continue;  // already on its own line
        result += text.substring(prev, ls);
        result += '\n';
        prev = ls;
      }
      result += text.substring(prev);
      text = result;
    }
  }

  // Hyphen list splitting: only fires on "\n- item" patterns already in the text.
  // Does NOT split "A - B - C" prose or em-dash-converted "risk - signal - stop" sentences.
  // If the text already has newline-prefixed hyphens they are already on separate lines.
  // Example: "\n- point1\n- point2" stays unchanged.
  // Non-example: "risk - strength - signal" → unchanged.
  // (No code action needed — the rule is a guard against re-splitting already-formatted text.)

  return text;
}

// Append a glossary section with formatFeedbackBody applied to the body text.
void appendGlossarySectionFormatted(std::vector<GlossaryRenderLine>& lines, const char* label,
                                     const char* body, int32_t width) {
  String formatted = formatFeedbackBody(sanitizeCoachText(body ? body : "", "glossary-fmt"));
  appendGlossarySection(lines, label, formatted.c_str(), width);
}

void buildPracticeLines(const CoachItemView& item, const PracticeLayout& layout,
                        std::vector<GlossaryRenderLine>& lines) {
  lines.clear();
  if (item.type == CoachItemType::HostileFollowup) {
    appendGlossarySection(lines, "Follow-up", coachPromptFor(item).c_str(), layout.contentW);
    appendGlossarySectionFormatted(lines, "Suggested response", item.answer, layout.contentW);
    appendGlossarySectionFormatted(lines, "Anchor", item.rubric, layout.contentW);
  } else if (item.type == CoachItemType::WeakAnswer) {
    appendGlossarySection(lines, "Question", coachPromptFor(item).c_str(), layout.contentW);
    appendGlossarySectionFormatted(lines, "Explanation", coachAnswerFor(item).c_str(), layout.contentW);
  } else {
    String questionText = sanitizeCoachText(item.title, "practice-title");
    if (questionText.length() == 0) {
      questionText = sanitizeCoachText(item.prompt, "practice-prompt");
    }
    appendGlossarySection(lines, "Question", questionText.c_str(), layout.contentW);
    appendGlossarySection(lines, "Confidence", item.confidence, layout.contentW);
    appendGlossarySectionFormatted(lines, "Answer", item.spoken, layout.contentW);
    appendGlossarySectionFormatted(lines, "Anchor", item.anchor, layout.contentW);
    appendGlossarySection(lines, "Watch-out", item.watch, layout.contentW);
    appendGlossarySectionFormatted(lines, "Follow-up", item.explanation, layout.contentW);
  }
  if (lines.empty()) {
    appendGlossarySection(lines, "Card", "No content available.", layout.contentW);
  }
}

uint8_t practiceStructuredPageCount(const CoachItemView& item, const PracticeLayout& layout) {
  std::vector<GlossaryRenderLine> lines;
  buildPracticeLines(item, layout, lines);
  uint16_t pageStarts[kMaxReaderPageCount] = {};
  return paginateGlossaryLines(lines, layout, pageStarts, countOf(pageStarts));
}

void drawPracticeStructuredPage(const CoachItemView& item, const PracticeLayout& layout,
                                const std::vector<GlossaryRenderLine>& lines, uint8_t pageIndex,
                                uint8_t pageCount) {
  auto& display = M5.Display;
  uint16_t pageStarts[kMaxReaderPageCount] = {};
  const uint8_t computedPageCount = paginateGlossaryLines(lines, layout, pageStarts, countOf(pageStarts));
  const uint8_t safePage = pageIndex < computedPageCount ? pageIndex : (computedPageCount > 0 ? computedPageCount - 1 : 0);
  const size_t start = pageStarts[safePage];
  const size_t end = safePage + 1 < computedPageCount ? pageStarts[safePage + 1] : lines.size();
  display.setTextDatum(textdatum_t::top_left);
  int32_t y = layout.contentY;
  for (size_t index = start; index < end && index < lines.size(); ++index) {
    const GlossaryRenderLine& gl = lines[index];
    if (gl.kind == GlossaryLineKind::Space) {
      y += gl.height;
      continue;
    }
    if (gl.kind == GlossaryLineKind::Label) {
      applyCoachMetadataFont();
      display.setTextColor(metadataTextColor(), TFT_WHITE);
      display.drawString(gl.text, layout.contentX, y);
      y += gl.height;
      continue;
    }
    applyCoachBodyFont();
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    if (gl.text.length() > 0) {
      display.drawString(gl.text, layout.contentX, y);
    }
    y += gl.height;
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  (void)pageCount;
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

void appendFeedbackSection(std::vector<GlossaryRenderLine>& lines, const char* label, const String& body,
                           int32_t width) {
  String sectionBody = sanitizeCoachText(body, "feedback-section");
  sectionBody.trim();
  sectionBody = formatFeedbackBody(sectionBody);
  if (sectionBody.length() == 0) {
    return;
  }
  if (!lines.empty()) {
    appendGlossarySpace(lines, 12);
  }
  GlossaryRenderLine labelLine;
  labelLine.text = label;
  labelLine.kind = GlossaryLineKind::Label;
  labelLine.height = coachTypography().metadataLineHeight + 2;
  lines.push_back(labelLine);
  appendGlossaryWrappedBody(lines, sectionBody, width);
}

String feedbackExplanationFor(const CoachItemView& item) {
  if (strlen(item.explanation) > 0) {
    return item.explanation;
  }
  if (strlen(item.answer) > 0) {
    return item.answer;
  }
  return "No explanation available.";
}

void buildFeedbackLines(const CoachItemView& item, const PracticeLayout& layout, std::vector<GlossaryRenderLine>& lines) {
  lines.clear();
  const uint8_t selected = gSelectedOption >= 0 ? static_cast<uint8_t>(gSelectedOption) : 255;
  appendFeedbackSection(lines, "Selected", optionLabelWithSafeLetter(item, selected), layout.contentW);
  appendFeedbackSection(lines, "Best", hasValidAnswerKey(item) ? optionLabelWithSafeLetter(item, item.correctIndex) : "-",
                        layout.contentW);
  appendFeedbackSection(lines, "Why this is best", feedbackExplanationFor(item), layout.contentW);
}

uint8_t feedbackPageCountFor(const CoachItemView& item, const PracticeLayout& layout) {
  std::vector<GlossaryRenderLine> lines;
  buildFeedbackLines(item, layout, lines);
  uint16_t pageStarts[kMaxReaderPageCount] = {};
  return paginateGlossaryLines(lines, layout, pageStarts, countOf(pageStarts));
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
  if (isOptionDrillScreen(gScreen, item) && gSelectedOption >= 0) {
    if (!gDrillShowFeedback) {
      // Result view uses the same fit-aware plan as the pre-answer screen.
      applyCoachButtonFont();
      const DrillPagePlan plan = buildDrillPagePlan(item, layout);
      gSettings.fontSizeMode = savedSize;
      return plan.totalPages;
    }
    const uint8_t pageCount = feedbackPageCountFor(item, layout);
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
  // Known compact section mappings (checked before generic replacements)
  if (category == "Background, Motivation & Fit") return "Background / Fit";
  if (category == "Product Strategy & Discovery") return "Product Strategy";
  if (category == "Stakeholder Management & Influence") return "Stakeholder Mgmt";
  if (category == "Execution / Delivery / Tradeoffs") return "Execution / Tradeoffs";
  if (category == "Execution, Delivery & Tradeoffs") return "Execution / Tradeoffs";
  if (category == "Data, Metrics & Analytics") return "Data & Metrics";
  if (category == "Cross-functional Leadership") return "Cross-func Leadership";
  if (category == "Technical Depth & AI/ML") return "Technical / AI/ML";
  // Generic fallbacks
  category.replace("PaperCoach ", "");
  category.replace("Motivation & Fit", "Motivation/Fit");
  category.replace("Background, ", "Background / ");
  category.replace(" and ", " & ");
  category.replace(", ", " / ");
  return category;
}

String compactPracticeTitle(const char* rawTitle) {
  if (!rawTitle || rawTitle[0] == '\0') return "";
  String title = sanitizeCoachText(rawTitle, "practice-title-compact");
  title.trim();
  if (title.length() == 0) return "";
  // Known mappings for verbose titles
  struct CompactMap { const char* raw; const char* compact; };
  static const CompactMap kMap[] = {
    {"How do you QA/test a non-deterministic chatbot or AI output", "QA/testing non-deterministic AI output"},
    {"Success / 90-day impact & successful traits",                "90-day impact & successful traits"},
    {"Self-introduction / career & recent work",                   "Self-introduction, career & recent work"},
    {"Why are you interested in this product area, and why payments","Product area / payments motivation"},
    {"Tell me about a time you used metrics to make a product decision","Metrics-based product decision"},
    {"Guardrails against hallucinations / PII leaks",              "Hallucination & PII guardrails"},
  };
  for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); ++i) {
    if (title.startsWith(kMap[i].raw)) return kMap[i].compact;
  }
  // Strip common verbose question starters
  static const char* kPrefixes[] = {
    "How would you ", "How do you ", "How do ", "How did ",
    "Tell me about a time ", "Tell me about ",
    "Why are you ", "Why did ", "Why do ", "Why have ", "Why is ",
    "What is the ", "What is ", "What are ", "What would ", "What was ",
    "Walk me through ", "Describe ", "Can you ", "Have you ",
  };
  for (size_t i = 0; i < sizeof(kPrefixes) / sizeof(kPrefixes[0]); ++i) {
    const size_t plen = strlen(kPrefixes[i]);
    if (title.length() > plen + 4 && title.startsWith(kPrefixes[i])) {
      String rest = title.substring(static_cast<unsigned>(plen));
      rest.trim();
      if (rest.length() > 0) rest[0] = static_cast<char>(toupper(static_cast<unsigned char>(rest[0])));
      title = rest;
      break;
    }
  }
  // Remove trailing "?"
  while (title.length() > 0 && title[title.length() - 1] == '?') {
    title.remove(title.length() - 1);
    title.trim();
  }
  return title;
}

String headerJoin2(const String& first, const String& second) {
  String line = first;
  line += kHeaderSep;
  line += second;
  return line;
}

String headerJoin3(const String& first, const String& second, const String& third) {
  String line = headerJoin2(first, second);
  line += kHeaderSep;
  line += third;
  return line;
}

String headerPageText(uint8_t pageNumber, uint8_t pageCount) {
  return String(static_cast<unsigned>(pageNumber)) + "/" + static_cast<unsigned>(pageCount);
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
  if (item.type == CoachItemType::Glossary) {
    line = headerJoin2(line, "Glossary");
  } else if (gScreen == Screen::InterviewPractice || item.type == CoachItemType::Qa) {
    line = headerJoin2(line, item.mustMaster ? "Must" : "Card");
  } else {
    line = headerJoin2(line, drillHeaderLabel(item));
  }
  line = headerJoin3(line, stageName, headerPageText(pageNumber, pageCount));
  String fallback = sanitizeCoachText(coachDisplayId(item), "reader-id-fallback");
  fallback += kHeaderSep;
  fallback += (gScreen == Screen::InterviewPractice || item.type == CoachItemType::Qa) ? (item.mustMaster ? "Must" : "Card")
                                                                                       : stageName;
  fallback += " ";
  fallback += static_cast<unsigned>(pageNumber);
  fallback += "/";
  fallback += static_cast<unsigned>(pageCount);
  drawReadableHeaderLine(line, fallback, kCoachMargin, 18, headerW);
  if (item.section && item.section[0] != '\0') {
    const String section = compactHeaderCategory(item.section);
    if (section.length() > 0 && section != "Card" && section != stageName && section != coachDisplayId(item)) {
      drawReadableHeaderLine(section, "", kCoachMargin, 54, headerW);
    }
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void drawCompactDrillHeader(const CoachItemView& item, uint8_t pageNumber, uint8_t pageCount) {
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  const int32_t headerW = display.width() - kCoachMargin * 2;
  String line = sanitizeCoachText(coachDisplayId(item), "drill-id");
  line = headerJoin2(line, drillHeaderLabel(item));
  if (pageCount > 1) {
    line = headerJoin2(line, headerPageText(pageNumber, pageCount));
  }
  String fallback = sanitizeCoachText(coachDisplayId(item), "drill-id-fallback");
  fallback += kHeaderSep;
  fallback += drillHeaderLabel(item);
  if (pageCount > 1) {
    fallback += " ";
    fallback += headerPageText(pageNumber, pageCount);
  }
  drawReadableHeaderLine(line, fallback, kCoachMargin, 18, headerW);
  String categoryLine;
  if (item.section && item.section[0] != '\0' && strcmp(item.section, "PaperCoach Drills") != 0) {
    categoryLine = item.section;
  }
  if (categoryLine.length() > 0 && categoryLine != drillHeaderLabel(item)) {
    drawReadableHeaderLine(compactHeaderCategory(categoryLine.c_str()), "", kCoachMargin, 54, headerW);
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void drawExamHeader(const CoachItemView& item, uint8_t pageNumber, uint8_t pageCount) {
  (void)item;
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  const int32_t headerW = display.width() - kCoachMargin * 2;
  String line = "Exam";
  String question = "Q";
  question += static_cast<unsigned>(gExamPosition + 1);
  question += "/";
  question += static_cast<unsigned>(gExamCount);
  line = headerJoin2(line, question);
  if (pageCount > 1) {
    line = headerJoin2(line, headerPageText(pageNumber, pageCount));
  }
  drawReadableHeaderLine(line, "", kCoachMargin, 18, headerW);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void drawFeedbackHeader(const CoachItemView& item, uint8_t pageNumber, uint8_t pageCount) {
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  const int32_t headerW = display.width() - kCoachMargin * 2;
  String line = sanitizeCoachText(coachDisplayId(item), "feedback-id");
  line = headerJoin3(line, drillHeaderLabel(item), "Feedback");
  if (pageCount > 1) {
    line = headerJoin2(line, headerPageText(pageNumber, pageCount));
  }
  String fallback = sanitizeCoachText(coachDisplayId(item), "feedback-id-fallback");
  fallback = headerJoin2(fallback, "Feedback");
  if (pageCount > 1) {
    fallback += " ";
    fallback += headerPageText(pageNumber, pageCount);
  }
  drawReadableHeaderLine(line, fallback, kCoachMargin, 18, headerW);
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
  String line = headerJoin2("Glossary", glossaryCategoryName(gGlossaryCategory));
  if (total > 0) {
    line = headerJoin2(line, headerPageText(position, total));
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
        applyCoachBodyFont();
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

void drawFeedbackPage(const CoachItemView& item, const PracticeLayout& layout) {
  auto& display = M5.Display;
  std::vector<GlossaryRenderLine> lines;
  buildFeedbackLines(item, layout, lines);
  uint16_t pageStarts[kMaxReaderPageCount] = {};
  const uint8_t pageCount = paginateGlossaryLines(lines, layout, pageStarts, countOf(pageStarts));
  if (gCoachStage >= pageCount) {
    gCoachStage = pageCount - 1;
  }

  drawFeedbackHeader(item, gCoachStage + 1, pageCount);
  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};

  const size_t start = pageStarts[gCoachStage];
  const size_t end = gCoachStage + 1 < pageCount ? pageStarts[gCoachStage + 1] : lines.size();
  int32_t y = layout.contentY;
  String traceBody;
  for (size_t index = start; index < end && index < lines.size(); ++index) {
    const GlossaryRenderLine& line = lines[index];
    if (line.kind != GlossaryLineKind::Space && line.text.length() > 0) {
      if (traceBody.length() > 0) {
        traceBody += "\n";
      }
      traceBody += line.text;
    }
    switch (line.kind) {
      case GlossaryLineKind::Label:
        applyCoachMetadataFont();
        display.setTextColor(metadataTextColor(), TFT_WHITE);
        display.drawString(line.text, layout.contentX, y);
        break;
      case GlossaryLineKind::Body:
        applyCoachBodyFont();
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.drawString(line.text, layout.contentX, y);
        break;
      case GlossaryLineKind::Term:
      case GlossaryLineKind::Space:
        break;
    }
    y += line.height;
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  ReaderPageSet tracePages = buildReaderPages(traceBody, layout.contentW, layout.linesPerPage, "Feedback");
  const char* warning = hasValidAnswerKey(item) ? "per-option explanations unavailable; omitted from live feedback"
                                                : "invalid answer key reached feedback";
  recordRenderTrace(item, "Feedback", traceBody, tracePages, 0, pageCount > 1, warning);
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
  layout.contentX = kCoachMargin;
  layout.contentY = 92;
  layout.contentW = display.width() - kCoachMargin * 2;
  layout.buttonH = 58;
  layout.footerY = display.height() - layout.buttonH - 18;
  layout.contentH = layout.footerY - layout.contentY - 16;
  layout.lineHeight = type.bodyLineHeight;
  layout.questionLineHeight = type.bodyLineHeight + 4;
  layout.linesPerPage = linesThatFit(layout.contentH, layout.lineHeight, 3, kMaxWrappedLines);
  layout.questionLinesPerPage = linesThatFit(layout.contentH, layout.questionLineHeight, 3, kMaxWrappedLines);

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
  applyCoachBodyFont();
  const uint8_t totalPages = practiceStructuredPageCount(item, layout);
  const bool tooFewLines = layout.linesPerPage < 8;
  gSettings.fontSizeMode = savedSize;

  const bool tooManyPages = totalPages > 8;
  if (tooManyPages || tooFewLines) {
    if (logDecision) {
      Serial.printf("Reader auto-fit: card=%s from=Reader L to=Reader M reason=%s pages=%u linesPerPage=%u\n",
                    item.id, tooManyPages ? "too-many-pages" : "too-few-lines", totalPages, layout.linesPerPage);
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
  applyCoachBodyFont();
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
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  const FontSizeMode renderSize = choosePracticeReaderSize(item, false);
  gSettings.fontSizeMode = renderSize;
  const PracticeLayout layout = practiceLayoutFor(renderSize);
  const uint8_t count = practiceStructuredPageCount(item, layout);
  gSettings.fontSizeMode = savedSize;
  return count;
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

  std::vector<GlossaryRenderLine> practiceLines;
  buildPracticeLines(item, layout, practiceLines);
  uint16_t pageStarts[kMaxReaderPageCount] = {};
  const uint8_t pageCount = paginateGlossaryLines(practiceLines, layout, pageStarts, countOf(pageStarts));

  if (gCoachStage >= pageCount) {
    gCoachStage = pageCount > 0 ? pageCount - 1 : 0;
  }

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);

  const int32_t headerW = display.width() - kCoachMargin * 2;
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  // Header line 1: id | Must/Card | compact section (no page count per v5.8-dev2)
  String headerLine = sanitizeCoachText(coachDisplayId(item), "practice-id");
  headerLine = headerJoin2(headerLine, item.mustMaster ? "Must" : "Card");
  if (item.section && item.section[0] != '\0') {
    const String section = compactHeaderCategory(item.section);
    if (section.length() > 0 && section != "Card") {
      headerLine = headerJoin2(headerLine, section);
    }
  }
  drawReadableHeaderLine(headerLine, sanitizeCoachText(coachDisplayId(item), "practice-id-fallback"),
                         kCoachMargin, 18, headerW);
  // Header line 2: compact synthesized title
  const String compactTitle = compactPracticeTitle(item.title);
  if (compactTitle.length() > 0) {
    drawReadableHeaderLine(compactTitle, "", kCoachMargin, 54, headerW);
  }
  // Thin divider below header
  display.drawLine(kCoachMargin, 84, kCoachMargin + headerW, 84, metadataTextColor());
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};
  drawPracticeStructuredPage(item, layout, practiceLines, gCoachStage, pageCount);

  drawCoachFooterNav(hasPreviousCoachItem(), hasNextCoachItem());
  gSettings.fontSizeMode = savedSize;
  finishDisplayRefresh();
  Serial.printf("Practice shown: card=%s page=%u/%u reader=%s\n", item.id, gCoachStage + 1,
                pageCount, shortFontSizeModeName(renderSize));
}

// Show post-answer result view: question + options with selected/correct borders.
// Paginates using gCoachStage with the same fit-aware plan as the pre-answer screen.
void drawDrillResultView(const CoachItemView& item, const PracticeLayout& layout) {
  auto& display = M5.Display;
  const uint8_t selected = static_cast<uint8_t>(gSelectedOption);
  const bool validKey = hasValidAnswerKey(item);
  const DrillPagePlan plan = buildDrillPagePlan(item, layout);
  const int32_t sharedH = sharedOptionButtonHeight(item, layout.contentW);
  const uint8_t resultPages = plan.totalPages;
  if (gCoachStage >= resultPages) gCoachStage = resultPages - 1;

  drawCompactDrillHeader(item, gCoachStage + 1, resultPages);
  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};

  // Helper: draw one option box with selected/correct highlighting and vertically centered text.
  auto drawResultOption = [&](uint8_t option, int32_t optY) {
    const String label = optionLabelWithLetter(item, option);
    const Rect rect = {layout.contentX, optY, layout.contentW, sharedH};
    const bool highlighted = (option == selected) || (validKey && option == item.correctIndex);
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
    display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
    if (highlighted) {
      display.drawRoundRect(rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4, 6, TFT_BLACK);
    }
    const uint8_t optionPx = optionTextPxFor(label, rect.w);
    applyBodyFont(optionPx);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(textdatum_t::top_left);
    std::vector<String> wlines;
    wrapReaderTextToLines(label, rect.w - 32, wlines, "result-option");
    const int32_t lineH = optionLineHeight(optionPx);
    const int32_t textBlockH = static_cast<int32_t>(wlines.size()) * lineH;
    const int32_t textY = rect.y + (rect.h - textBlockH) / 2;
    for (size_t li = 0; li < wlines.size(); ++li) {
      display.drawString(wlines[li], rect.x + 18, textY + static_cast<int32_t>(li) * lineH);
      if (highlighted) {
        display.drawString(wlines[li], rect.x + 19, textY + static_cast<int32_t>(li) * lineH);
      }
    }
  };

  const int32_t optionGap = 8;
  if (plan.combinedQuestionOptions) {
    // All fits on one page: question followed by all options.
    applyCoachQuestionFont();
    drawReaderPage(plan.questionPages, 0, layout.contentX, layout.contentY, layout.questionLineHeight);
    int32_t y = layout.contentY + plan.questionBlockHeight + 14;
    for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
      drawResultOption(option, y);
      y += sharedH + optionGap;
    }
  } else if (gCoachStage < plan.questionPages.pageCount) {
    // Question page(s) — same as pre-answer.
    applyCoachQuestionFont();
    drawReaderPage(plan.questionPages, gCoachStage, layout.contentX, layout.contentY, layout.questionLineHeight);
  } else {
    // Options page(s) with compact question reminder + selected/correct state.
    const uint8_t opPageIdx = gCoachStage - plan.questionPages.pageCount;
    const DrillOptionPage& opPage =
        plan.optionPages[opPageIdx < plan.optionPageCount ? opPageIdx : plan.optionPageCount - 1];
    int32_t y = layout.contentY;
    applyCoachMetadataFont();
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    TextLayoutResult reminderLayout =
        drawWrappedText(plan.reminder, layout.contentX, y, layout.contentW,
                        coachTypography().metadataLineHeight, 2, "result-reminder", 1);
    y += reminderLayout.height + 12;
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    for (uint8_t offset = 0; offset < opPage.optionCount; ++offset) {
      const uint8_t option = opPage.firstOption + offset;
      if (option >= item.optionCount || option >= kMaxOptions) continue;
      drawResultOption(option, y);
      y += sharedH + optionGap;
    }
  }
  Serial.printf("Drill result view: item=%s page=%u/%u combined=%s\n", coachDisplayId(item),
                static_cast<unsigned>(gCoachStage + 1), static_cast<unsigned>(resultPages),
                plan.combinedQuestionOptions ? "yes" : "no");
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
    {
      const CoachTypography ct = coachTypography();
      const uint8_t qPx = ct.bodyPx >= 34 ? ct.bodyPx : static_cast<uint8_t>(ct.bodyPx + 4);
      const uint8_t feedbackPx = ct.bodyPx;
      const uint8_t opt0Px = item.optionCount > 0
          ? optionTextPxFor(optionLabelWithLetter(item, 0), layout.contentW) : 0;
      const int32_t opt0H = item.optionCount > 0
          ? optionButtonHeightFor(optionLabelWithLetter(item, 0), layout.contentW) : 0;
      Serial.printf("Drill fonts: screen=%s reader=%s qPx=%u opt0Px=%u opt0H=%ld feedbackPx=%u bodyPx=%u\n",
                    screenName(gScreen), shortFontSizeModeName(renderSize), qPx, opt0Px,
                    static_cast<long>(opt0H), feedbackPx, ct.bodyPx);
    }
    drawCompactDrillHeader(item, gCoachStage + 1, plan.totalPages);
    gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};

    if (plan.combinedQuestionOptions) {
      applyCoachQuestionFont();
      drawReaderPage(plan.questionPages, 0, layout.contentX, layout.contentY, layout.questionLineHeight);
      logReaderPagination("Question", plan.questionPages, layout);
      int32_t y = layout.contentY + plan.questionBlockHeight + 14;
      const int32_t optionGap = 8;
      const int32_t sharedH = sharedOptionButtonHeight(item, layout.contentW);
      String traceBody = String(item.prompt) + "\n";
      for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
        const String label = optionLabelWithLetter(item, option);
        gOptionButtons[option] = {layout.contentX, y, layout.contentW, sharedH};
        drawOptionButton(gOptionButtons[option], label);
        traceBody += "\n";
        traceBody += label;
        y += sharedH + optionGap;
      }
      ReaderPageSet combinedTrace = buildReaderPages(traceBody, layout.contentW, layout.linesPerPage, "Question+Options");
      recordRenderTrace(item, "Question+Options", traceBody, combinedTrace, 0, false);
      Serial.printf("Drill combined shown: item=%s options=%u sharedH=%ld totalPages=%u\n", coachDisplayId(item),
                    item.optionCount, static_cast<long>(sharedH), plan.totalPages);
    } else if (gCoachStage < plan.questionPages.pageCount) {
      applyCoachQuestionFont();
      drawReaderPage(plan.questionPages, gCoachStage, layout.contentX, layout.contentY, layout.questionLineHeight);
      if (gCoachStage + 1 == plan.questionPages.pageCount) {
        applyCoachMetadataFont();
        display.setTextColor(metadataTextColor(), TFT_WHITE);
        display.drawString("Choices", layout.contentX, layout.footerY - coachTypography().metadataLineHeight - 8);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
      }
      logReaderPagination("Question", plan.questionPages, layout);
      recordRenderTrace(item, "Question", item.prompt, plan.questionPages, gCoachStage, true);
    } else {
      const uint8_t optionPageIndex = gCoachStage - plan.questionPages.pageCount;
      const DrillOptionPage& optionPage =
          plan.optionPages[optionPageIndex < plan.optionPageCount ? optionPageIndex : plan.optionPageCount - 1];
      int32_t y = layout.contentY;
      const int32_t optionGap = 8;
      applyCoachMetadataFont();
      display.setTextColor(metadataTextColor(), TFT_WHITE);
      TextLayoutResult reminderLayout =
          drawWrappedText(plan.reminder, layout.contentX, y, layout.contentW, coachTypography().metadataLineHeight, 2,
                          "question-reminder", 1);
      y += reminderLayout.height + 12;
      display.setTextColor(TFT_BLACK, TFT_WHITE);
      const int32_t sharedH = sharedOptionButtonHeight(item, layout.contentW);
      String traceBody = plan.reminder;
      for (uint8_t offset = 0; offset < optionPage.optionCount; ++offset) {
        const uint8_t option = optionPage.firstOption + offset;
        if (option >= item.optionCount || option >= kMaxOptions) {
          continue;
        }
        const String label = optionLabelWithLetter(item, option);
        gOptionButtons[option] = {layout.contentX, y, layout.contentW, sharedH};
        drawOptionButton(gOptionButtons[option], label);
        if (traceBody.length() > 0) {
          traceBody += "\n";
        }
        traceBody += label;
        y += sharedH + optionGap;
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

  if (optionDrill && gSelectedOption >= 0) {
    if (!gDrillShowFeedback) {
      drawDrillResultView(item, layout);
    } else {
      drawFeedbackPage(item, layout);
    }
    drawCoachFooterNav(hasPreviousCoachItem(), hasNextCoachItem());
    gSettings.fontSizeMode = savedSize;
    finishDisplayRefresh();
    Serial.printf("Drill post-answer: item=%s mode=%s page=%u/%u validKey=%s reader=%s\n",
                  coachDisplayId(item), gDrillShowFeedback ? "feedback" : "result",
                  static_cast<unsigned>(gCoachStage + 1), static_cast<unsigned>(currentCoachReaderPageCount()),
                  hasValidAnswerKey(item) ? "yes" : "no", shortFontSizeModeName(renderSize));
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
    drawFeedbackHeader(item, gCoachStage + 1, totalPages);
  } else {
    drawCompactReaderHeader(item, stage.name, localPage + 1, stage.pages.pageCount);
  }
  applyCoachContentFont();
  gReaderContentRect = {layout.contentX, layout.contentY, layout.contentW, layout.contentH};
  drawReaderPage(stage.pages, localPage, layout.contentX, layout.contentY, layout.lineHeight);
  logReaderPagination(stage.name, stage.pages, layout);
  const char* traceWarning =
      optionDrill && gSelectedOption >= 0 ? "per-option explanations unavailable; omitted from live feedback" : nullptr;
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
  const int32_t buttonH = 112;
  const int32_t gap = 20;
  int32_t y = 120;
  gBadgeButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gInterviewButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gJapaneseButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gSettingsButton = {buttonX, y, buttonW, buttonH};
  // Clear rects that are no longer on Home (now in InterviewMenu)
  gPracticeButton = {};
  gDrillsButton = {};
  gExamButton = {};
  gGlossaryButton = {};
  gResultsButton = {};
  gDebugButton = {};

  drawButton(gBadgeButton, "Badge", IconType::Badge);
  drawButton(gInterviewButton, "Interview", IconType::Practice);
  drawButton(gJapaneseButton, "Japanese");
  drawButton(gSettingsButton, "Settings", IconType::Settings);

  finishDisplayRefresh();
  Serial.println("Home: Badge / Interview / Japanese / Settings");
}

void renderInterviewMenu(const char* refreshReason = "mode switch") {
  gScreen = Screen::InterviewMenu;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Interview", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("Choose mode", 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 82;
  const int32_t gap = 14;
  int32_t y = 148;
  gPracticeButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gDrillsButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gExamButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gGlossaryButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gResultsButton = {buttonX, y, buttonW, buttonH};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gPracticeButton, "Practice", IconType::Practice);
  drawButton(gDrillsButton, "Drills", IconType::Drills);
  drawButton(gExamButton, "Exam", IconType::Exam);
  drawButton(gGlossaryButton, "Glossary", IconType::Glossary);
  drawButton(gResultsButton, "Results", IconType::Results);
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.println("Interview menu: Practice / Drills / Exam / Glossary / Results");
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

void renderJapaneseMenu(const char* refreshReason = "mode switch") {
  gScreen = Screen::JapaneseMenu;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Japanese", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("N3 sample - Week 1 Day 1", 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 82;
  const int32_t gap = 14;
  int32_t y = 148;
  gJapaneseDailyButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gJapaneseMockTestButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gJapaneseReferenceButton = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  gJapaneseResultsButton = {buttonX, y, buttonW, buttonH};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gJapaneseDailyButton, "Daily Questions");
  drawButton(gJapaneseMockTestButton, "Mock Test");
  drawButton(gJapaneseReferenceButton, "Reference");
  drawButton(gJapaneseResultsButton, "Results");
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.println("Japanese menu shown.");
}

// Japanese Source Select — first step of Daily Questions navigation.
// Only "500問 N3" (n3sample W1D1) is enabled in v5.9-dev3; additional sources stub/placeholder.
void renderJapaneseSourceSelect(const char* refreshReason = "japanese entry") {
  gScreen = Screen::JapaneseSourceSelect;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Daily Questions", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("Select source", 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 100;
  const int32_t gap = 16;
  int32_t y = 148;

  // Enabled source
  gJapaneseSourceN3Button = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;

  // Disabled placeholder
  Rect placeholderBtn = {buttonX, y, buttonW, 76};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gJapaneseSourceN3Button, "500\xe5\x95\x8f N3  \xe2\x80\x94  Week/day practice");
  // Dim placeholder
  display.drawRoundRect(placeholderBtn.x, placeholderBtn.y, placeholderBtn.w, placeholderBtn.h, 8, metadataTextColor());
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.setTextDatum(textdatum_t::middle_left);
  display.drawString("More sources coming soon", placeholderBtn.x + 20, placeholderBtn.y + placeholderBtn.h / 2);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.println("Japanese source select shown.");
}

// Japanese Week Select — second step.
void renderJapaneseWeekSelect(const char* refreshReason = "japanese entry") {
  gScreen = Screen::JapaneseWeekSelect;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Daily Questions", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("500\xe5\x95\x8f N3  \xe2\x80\x94  Select week", 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 100;
  const int32_t gap = 16;
  int32_t y = 148;

  gJapaneseWeek1Button = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;
  Rect placeholderBtn = {buttonX, y, buttonW, 76};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gJapaneseWeek1Button, "Week 1");
  display.drawRoundRect(placeholderBtn.x, placeholderBtn.y, placeholderBtn.w, placeholderBtn.h, 8, metadataTextColor());
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.setTextDatum(textdatum_t::middle_left);
  display.drawString("Week 2+  \xe2\x80\x94  Coming soon", placeholderBtn.x + 20, placeholderBtn.y + placeholderBtn.h / 2);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.println("Japanese week select shown.");
}

// Japanese Day Select — third step.
void renderJapaneseDaySelect(const char* refreshReason = "japanese entry") {
  gScreen = Screen::JapaneseDaySelect;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Daily Questions", 32, 34);
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("500\xe5\x95\x8f N3  \xc2\xb7  Week 1  \xe2\x80\x94  Select day", 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  const int32_t buttonX = 34;
  const int32_t buttonW = display.width() - 68;
  const int32_t buttonH = 100;
  const int32_t gap = 14;
  int32_t y = 148;

  gJapaneseDay1Button = {buttonX, y, buttonW, buttonH};
  y += buttonH + gap;

  // Days 2–6 placeholder row (single wide button, dim)
  Rect placeholderBtn = {buttonX, y, buttonW, 76};
  gHomeButton = {buttonX, display.height() - 110, buttonW, 76};

  drawButton(gJapaneseDay1Button, "Day 1");
  display.drawRoundRect(placeholderBtn.x, placeholderBtn.y, placeholderBtn.w, placeholderBtn.h, 8, metadataTextColor());
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.setTextDatum(textdatum_t::middle_left);
  display.drawString("Day 2\xe2\x80\x93""6  \xe2\x80\x94  Coming soon", placeholderBtn.x + 20, placeholderBtn.y + placeholderBtn.h / 2);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.println("Japanese day select shown.");
}

// RAM-only — intentionally not gSessionResults/SessionResult (Interview Practice/Drills/Exam).
void recordJapaneseAnswer(const JapaneseItem& item, uint8_t selectedChoice) {
  if (gJapaneseResultCount >= kMaxJapaneseResults) {
    for (size_t i = 1; i < kMaxJapaneseResults; ++i) {
      gJapaneseResults[i - 1] = gJapaneseResults[i];
    }
    gJapaneseResultCount = kMaxJapaneseResults - 1;
  }
  JapaneseSessionResult& result = gJapaneseResults[gJapaneseResultCount++];
  result.millisAt = millis();
  copyToBuffer(result.itemId, sizeof(result.itemId), item.itemId);
  copyToBuffer(result.sourceId, sizeof(result.sourceId), item.sourceId);
  copyToBuffer(result.macroArea, sizeof(result.macroArea), item.macroArea);
  copyToBuffer(result.categoryJapanese, sizeof(result.categoryJapanese), item.categoryJapanese);
  result.week = item.week;
  result.day = item.day;
  result.selectedChoice = selectedChoice;
  result.correctChoice = item.correctChoice;
  result.correct = selectedChoice == item.correctChoice;
  Serial.printf("Japanese answer recorded: item=%s selected=%u correct=%u total=%u\n", item.itemId,
                static_cast<unsigned>(selectedChoice), result.correct ? 1 : 0,
                static_cast<unsigned>(gJapaneseResultCount));
}

// Draws Japanese text twice (x, x+1) for a slightly heavier look on e-ink.
// Use only for headers and prompts — not for dense body paragraphs.
TextLayoutResult drawJapaneseTextBoldish(const String& text, int32_t x, int32_t y, int32_t width,
                                         int32_t lineHeight, uint8_t maxLines, const char* field = nullptr) {
  TextLayoutResult result = drawJapaneseWrappedText(text, x, y, width, lineHeight, maxLines, field);
  drawJapaneseWrappedText(text, x + 1, y, width - 1, lineHeight, maxLines);
  return result;
}

void drawJapaneseOptionButtonChoice(const Rect& rect, const String& label, bool selected, bool correct) {
  auto& display = M5.Display;
  // Outline: double-line for normal, filled border for selected/correct states
  if (selected && correct) {
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
    display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
    display.drawRoundRect(rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4, 6, TFT_BLACK);
  } else if (selected) {
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
    display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 7, TFT_BLACK);
  } else {
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, TFT_BLACK);
  }

  const uint8_t px = japaneseChoicePxForReader();
  applyJapaneseChoiceFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextDatum(textdatum_t::top_left);

  String lines[3];
  const int32_t lineH = japaneseLineHeight(px);
  TextLayoutResult layout = wrapJapaneseTextToLines(label, rect.w - 32, lineH, 3, lines);
  const int32_t textBlockH = static_cast<int32_t>(layout.lineCount) * lineH;
  const int32_t textY = rect.y + (rect.h - textBlockH) / 2;
  for (uint8_t line = 0; line < layout.lineCount; ++line) {
    display.drawString(lines[line], rect.x + 16, textY + line * lineH);
  }
}

void renderJapaneseDaily(const char* refreshReason = "japanese entry") {
  gScreen = Screen::JapaneseDaily;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  if (gJapaneseQuestionIndex >= kJapaneseDayItemCount) {
    gJapaneseQuestionIndex = 0;
  }
  const JapaneseItem& item = kJapaneseDayItems[gJapaneseQuestionIndex];
  const int32_t contentX = 32;
  const int32_t contentW = display.width() - 64;

  // Header — compact but heavier, uses meta font size
  display.setTextDatum(textdatum_t::top_left);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  const uint8_t metaPx = japaneseMetaPxForReader();
  applyJapaneseMetaFont();
  char qNumber[8];
  snprintf(qNumber, sizeof(qNumber), "Q%03u", static_cast<unsigned>(item.sourceQuestionNumber));
  String header = String(item.jlptLevel) + " \xc2\xb7 W" + String(item.week) + "D" + String(item.day) +
                  " \xc2\xb7 " + qNumber + " \xc2\xb7 " + item.categoryJapanese;
  drawJapaneseTextBoldish(header, contentX, 28, contentW, japaneseLineHeight(metaPx), 1, "japanese-header");

  const uint8_t promptPx = japanesePromptPxForReader();
  const uint8_t choicePx = japaneseChoicePxForReader();
  const int32_t promptLineH = japaneseLineHeight(promptPx);

  // Reader-size-scaled button geometry
  const int32_t buttonH = (choicePx <= 28) ? 82 : 94;
  const int32_t gap = 12;
  const uint8_t maxPromptLines = (promptPx <= 28) ? 5 : (promptPx <= 32 ? 4 : 3);
  const int32_t promptY = 28 + japaneseLineHeight(metaPx) + 14;

  // Footer layout constants shared by both states
  const int32_t footerY = display.height() - 110;
  const int32_t footerH = 76;
  const bool hasPrev = gJapaneseQuestionIndex > 0;
  const bool hasNext = gJapaneseQuestionIndex + 1 < kJapaneseDayItemCount;
  const int32_t navW = (contentW - 28) / 3;

  if (!gJapaneseShowFeedback) {
    applyJapanesePromptFont();
    TextLayoutResult promptLayout =
        drawJapaneseWrappedText(item.promptJapanese, contentX, promptY, contentW, promptLineH, maxPromptLines,
                                "japanese-prompt");
    int32_t y = promptY + promptLayout.height + 20;
    for (uint8_t i = 0; i < 4; ++i) {
      gJapaneseOptionButtons[i] = {contentX, y, contentW, buttonH};
      String label = String(static_cast<char>('A' + i)) + ". " + item.choiceJapanese[i];
      // Use choice font via drawJapaneseOptionButtonChoice (draws outlined button)
      drawJapaneseOptionButton(gJapaneseOptionButtons[i], label);
      y += buttonH + gap;
    }
    // Pre-answer 3-part footer: [Prev] [Home] [Next]
    gJapanesePrevButton = {contentX, footerY, navW, footerH};
    gHomeButton         = {contentX + navW + 14, footerY, navW, footerH};
    gJapaneseNextButton = {contentX + (navW + 14) * 2, footerY, navW, footerH};

    if (hasPrev) {
      drawButton(gJapanesePrevButton, "Prev");
    } else {
      // Draw faded prev at boundary — omit (clear rect stays empty, no button drawn)
      gJapanesePrevButton = {};
    }
    drawButton(gHomeButton, "", IconType::Home);
    // No Next pre-answer — user selects an answer to advance
    gJapaneseNextButton = {};
  } else {
    // Feedback section
    const bool correct = gJapaneseSelectedOption == static_cast<int8_t>(item.correctChoice);
    const uint8_t titlePx = (choicePx <= 28) ? 28 : (choicePx <= 32 ? 32 : 36);
    applyJapaneseEnglishLabelFont(titlePx);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(correct ? "Correct" : "Wrong", contentX, promptY);

    int32_t y = promptY + static_cast<int32_t>(titlePx) + 12;

    // Correct answer line — Japanese Gothic
    const uint8_t explPx = japaneseExplanationPxForReader();
    const int32_t explLineH = japaneseLineHeight(explPx);
    applyJapaneseChoiceFont();
    String correctLine = String(static_cast<char>('A' + item.correctChoice)) + ". " +
                         item.choiceJapanese[item.correctChoice];
    TextLayoutResult l1 =
        drawJapaneseWrappedText(correctLine, contentX, y, contentW, japaneseLineHeight(choicePx), 2, "japanese-correct");
    y += l1.height + 12;

    // Answer sentence — Japanese Gothic at explanation size
    applyJapaneseExplanationFont();
    TextLayoutResult l2 = drawJapaneseWrappedText(item.answerSentenceJapanese, contentX, y, contentW,
                                                   explLineH, 3, "japanese-answer-sentence");
    y += l2.height + 10;

    // Japanese explanation — Gothic at explanation size
    TextLayoutResult l3 = drawJapaneseWrappedText(item.explanationJapanese, contentX, y, contentW,
                                                   explLineH, 3, "japanese-explanation");
    y += l3.height + 10;

    // English meaning — English only, Sans Bold smaller
    const uint8_t enPx = 20;
    applyJapaneseEnglishLabelFont(enPx);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    const int32_t enLineH = static_cast<int32_t>(enPx) + 10;
    String enLines[kMaxWrappedLines];
    String englishLine = String("EN: ") + item.explanationEnglish;
    TextLayoutResult l4 = wrapTextToLines(englishLine, contentW, enLineH, 3, enLines);
    for (uint8_t line = 0; line < l4.lineCount; ++line) {
      display.drawString(enLines[line], contentX, y + line * enLineH);
    }
    y += l4.height + 8;

    // Grammar tag — grammarPattern can contain Japanese (e.g. "～ものだ"), use Gothic path
    if (item.grammarPattern[0] != '\0') {
      applyGothicFont(20);
      display.setTextColor(metadataTextColor(), TFT_WHITE);
      String tagLine = String("Grammar: ") + item.grammarPattern;
      String tagLines[2];
      TextLayoutResult lt = wrapJapaneseTextToLines(tagLine, contentW, 28, 2, tagLines);
      for (uint8_t line = 0; line < lt.lineCount; ++line) {
        display.drawString(tagLines[line], contentX, y + line * 28);
      }
      display.setTextColor(TFT_BLACK, TFT_WHITE);
    }

    // Post-feedback 3-part footer: [Prev] [Home] [Next]
    gJapanesePrevButton = {contentX, footerY, navW, footerH};
    gHomeButton         = {contentX + navW + 14, footerY, navW, footerH};
    gJapaneseNextButton = {contentX + (navW + 14) * 2, footerY, navW, footerH};

    if (hasPrev) {
      drawButton(gJapanesePrevButton, "Prev");
    } else {
      gJapanesePrevButton = {};
    }
    drawButton(gHomeButton, "", IconType::Home);
    if (hasNext) {
      drawButton(gJapaneseNextButton, "Next", IconType::Next);
    } else {
      gJapaneseNextButton = {};
    }
  }

  finishDisplayRefresh();
  Serial.printf("Japanese daily question shown: item=%s q=%u/%u feedback=%s reader=%u/%u px\n",
                item.itemId, static_cast<unsigned>(gJapaneseQuestionIndex + 1),
                static_cast<unsigned>(kJapaneseDayItemCount), gJapaneseShowFeedback ? "yes" : "no",
                static_cast<unsigned>(promptPx), static_cast<unsigned>(choicePx));
}

// Simple, single-page summary built from gJapaneseResults (RAM-only) — never reads or writes
// gSessionResults/SessionResult (Interview Practice/Drills/Exam).
// Uses Sans Bold directly (not via applyCoachTitleFont) to avoid huge HighContrast sizes.
void renderJapaneseResults(const char* refreshReason = "japanese entry") {
  gScreen = Screen::JapaneseResults;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const int32_t contentX = kCoachMargin;
  const int32_t contentW = display.width() - kCoachMargin * 2;
  display.setTextDatum(textdatum_t::top_left);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  // Title — compact Sans Bold, fixed size independent of Reader setting
  applyJapaneseEnglishLabelFont(32);
  display.drawString("Japanese Results", contentX, 34);

  applyJapaneseEnglishLabelFont(20);
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("RAM-only  \xc2\xb7  resets on reboot", contentX, 80);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  int32_t y = 136;

  if (gJapaneseResultCount == 0) {
    applyJapaneseEnglishLabelFont(24);
    String emptyLines[3];
    TextLayoutResult el = wrapTextToLines("No answers yet. Try Daily Questions first.",
                                          contentW, 34, 3, emptyLines);
    for (uint8_t line = 0; line < el.lineCount; ++line) {
      display.drawString(emptyLines[line], contentX, y + line * 34);
    }
  } else {
    uint16_t correctCount = 0;
    CategoryStat macroStats[3];
    copyToBuffer(macroStats[0].name, sizeof(macroStats[0].name), "kanji");
    copyToBuffer(macroStats[1].name, sizeof(macroStats[1].name), "vocabulary");
    copyToBuffer(macroStats[2].name, sizeof(macroStats[2].name), "grammar");
    macroStats[0].total = macroStats[1].total = macroStats[2].total = 0;
    macroStats[0].correct = macroStats[1].correct = macroStats[2].correct = 0;
    for (size_t i = 0; i < gJapaneseResultCount; ++i) {
      const JapaneseSessionResult& r = gJapaneseResults[i];
      if (r.correct) ++correctCount;
      for (uint8_t m = 0; m < 3; ++m) {
        if (strcmp(r.macroArea, macroStats[m].name) == 0) {
          ++macroStats[m].total;
          if (r.correct) ++macroStats[m].correct;
        }
      }
    }
    const uint8_t accuracy = resultAccuracyPercent(correctCount, static_cast<uint16_t>(gJapaneseResultCount));

    applyJapaneseEnglishLabelFont(26);
    display.drawString(String("Answered: ") + static_cast<unsigned>(gJapaneseResultCount), contentX, y);
    y += 40;
    display.drawString(String("Correct:  ") + static_cast<unsigned>(correctCount) + " / " +
                           static_cast<unsigned>(gJapaneseResultCount) + "   " +
                           static_cast<unsigned>(accuracy) + "%",
                       contentX, y);
    y += 52;

    applyJapaneseEnglishLabelFont(22);
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    display.drawString("By area", contentX, y);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    y += 36;

    static const char* macroLabels[3] = {"Kanji", "Vocabulary", "Grammar"};
    applyJapaneseEnglishLabelFont(24);
    for (uint8_t m = 0; m < 3; ++m) {
      const uint8_t statAccuracy = resultAccuracyPercent(macroStats[m].correct, macroStats[m].total);
      String row = String(macroLabels[m]);
      // Pad label to 12 chars for column alignment
      while (static_cast<int>(row.length()) < 12) row += ' ';
      if (macroStats[m].total == 0) {
        row += "\xe2\x80\x94";  // em-dash
      } else {
        row += String(static_cast<unsigned>(macroStats[m].correct)) + " / " +
               String(static_cast<unsigned>(macroStats[m].total)) + "   " +
               String(static_cast<unsigned>(statAccuracy)) + "%";
      }
      display.drawString(row, contentX, y);
      y += 38;
    }
  }

  gHomeButton = {34, display.height() - 110, display.width() - 68, 76};
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.printf("Japanese results shown: answered=%u\n", static_cast<unsigned>(gJapaneseResultCount));
}

// Collects comma-separated tokens from `src` into `out` vector, deduped.
static void collectDeduped(const char* src, std::vector<String>& out) {
  if (!src || src[0] == '\0') return;
  String token;
  for (size_t i = 0; ; ++i) {
    const char ch = src[i];
    if (ch == ',' || ch == '\0') {
      token.trim();
      if (token.length() > 0) {
        bool dup = false;
        for (const auto& existing : out) {
          if (existing == token) { dup = true; break; }
        }
        if (!dup) out.push_back(token);
      }
      token = "";
      if (ch == '\0') break;
    } else {
      token += ch;
    }
  }
}

// Structured, deduped reference built from Week1Day1 embedded dataset concept fields.
// Grouped into Kanji / Grammar / Vocabulary sections — no raw per-item rows.
void renderJapaneseReference(const char* refreshReason = "japanese entry") {
  gScreen = Screen::JapaneseReference;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const int32_t contentX = kCoachMargin;
  const int32_t contentW = display.width() - kCoachMargin * 2;
  const int32_t bottomLimit = display.height() - 130;
  display.setTextDatum(textdatum_t::top_left);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  // Title — compact Sans Bold
  applyJapaneseEnglishLabelFont(32);
  display.drawString("Reference", contentX, 34);
  applyJapaneseEnglishLabelFont(20);
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString("N3 sample  \xc2\xb7  W1D1  \xc2\xb7  Kanji / Grammar / Vocabulary", contentX, 80);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  // Collect deduped lists from the embedded dataset
  std::vector<String> kanjiList;
  std::vector<String> grammarList;
  std::vector<String> vocabList;
  for (size_t i = 0; i < kJapaneseDayItemCount; ++i) {
    const JapaneseItem& item = kJapaneseDayItems[i];
    collectDeduped(item.kanjiItems, kanjiList);
    collectDeduped(item.grammarPattern, grammarList);
    collectDeduped(item.vocabularyItems, vocabList);
  }

  const uint8_t refPx = 24;  // reference content always at 24px for readability on small sections
  const int32_t refLineH = japaneseLineHeight(refPx);
  const int32_t sectionGap = 20;
  const int32_t headerH = 32;
  int32_t y = 128;

  auto drawSection = [&](const char* title, const std::vector<String>& items, bool japaneseFont) {
    if (items.empty() || y >= bottomLimit) return;
    // Section header — English Sans Bold
    applyJapaneseEnglishLabelFont(22);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.drawString(title, contentX, y);
    y += headerH;

    if (japaneseFont) {
      // All items on one wrapped line, space-separated
      String joined;
      for (size_t k = 0; k < items.size(); ++k) {
        if (k > 0) joined += "  ";
        joined += items[k];
      }
      applyJapaneseBodyFont(refPx);
      display.setTextColor(TFT_BLACK, TFT_WHITE);
      TextLayoutResult lay = drawJapaneseWrappedText(joined, contentX + 8, y, contentW - 8,
                                                      refLineH, 4, "ref-section");
      y += (lay.height > 0 ? lay.height : refLineH) + sectionGap;
    } else {
      // Each item on its own line (grammar patterns can be long)
      applyJapaneseBodyFont(refPx);
      display.setTextColor(TFT_BLACK, TFT_WHITE);
      for (const auto& term : items) {
        if (y >= bottomLimit) break;
        TextLayoutResult lay = drawJapaneseWrappedText(term, contentX + 8, y, contentW - 8,
                                                        refLineH, 2, "ref-item");
        y += (lay.height > 0 ? lay.height : refLineH) + 6;
      }
      y += sectionGap - 6;
    }
  };

  drawSection("Kanji", kanjiList, true);
  drawSection("Grammar", grammarList, false);
  drawSection("Vocabulary", vocabList, true);

  gHomeButton = {34, display.height() - 110, display.width() - 68, 76};
  drawButton(gHomeButton, "", IconType::Home);

  finishDisplayRefresh();
  Serial.printf("Japanese reference shown: kanji=%u grammar=%u vocab=%u\n",
                static_cast<unsigned>(kanjiList.size()), static_cast<unsigned>(grammarList.size()),
                static_cast<unsigned>(vocabList.size()));
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
    applyCoachQuestionFont();
    drawReaderPage(plan.questionPages, 0, layout.contentX, layout.contentY, layout.questionLineHeight);
    int32_t y = layout.contentY + plan.questionBlockHeight + 14;
    const int32_t optionGap = 8;
    const int32_t sharedH = sharedOptionButtonHeight(item, layout.contentW);
    String traceBody = String(item.prompt) + "\n";
    for (uint8_t option = 0; option < item.optionCount && option < kMaxOptions; ++option) {
      const String label = optionLabelWithLetter(item, option);
      gOptionButtons[option] = {layout.contentX, y, layout.contentW, sharedH};
      drawOptionButton(gOptionButtons[option], label);
      traceBody += "\n";
      traceBody += label;
      y += sharedH + optionGap;
    }
    ReaderPageSet combinedTrace = buildReaderPages(traceBody, layout.contentW, layout.linesPerPage, "ExamQuestion");
    recordRenderTrace(item, "ExamQuestion", traceBody, combinedTrace, 0, false);
  } else if (gCoachStage < plan.questionPages.pageCount) {
    applyCoachQuestionFont();
    drawReaderPage(plan.questionPages, gCoachStage, layout.contentX, layout.contentY, layout.questionLineHeight);
    if (gCoachStage + 1 == plan.questionPages.pageCount) {
      applyCoachMetadataFont();
      display.setTextColor(metadataTextColor(), TFT_WHITE);
      display.drawString("Choices", layout.contentX, layout.footerY - coachTypography().metadataLineHeight - 8);
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
    const int32_t sharedH = sharedOptionButtonHeight(item, layout.contentW);
    String traceBody = plan.reminder;
    const int32_t optionGap = 8;
    for (uint8_t offset = 0; offset < optionPage.optionCount; ++offset) {
      const uint8_t option = optionPage.firstOption + offset;
      if (option >= item.optionCount || option >= kMaxOptions) {
        continue;
      }
      const String label = optionLabelWithLetter(item, option);
      gOptionButtons[option] = {layout.contentX, y, layout.contentW, sharedH};
      drawOptionButton(gOptionButtons[option], label);
      traceBody += "\n";
      traceBody += label;
      y += sharedH + optionGap;
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

uint8_t resultsCategoryPageCount(uint8_t statCount) {
  static constexpr uint8_t kResultsStatsPerPage = 3;
  if (statCount == 0) {
    return 1;
  }
  return static_cast<uint8_t>((statCount + kResultsStatsPerPage - 1) / kResultsStatsPerPage);
}

// Combine summary + categories on page 0 only when the content actually fits.
// Measures worst-case height: condensed summary block + per-category bars (2-line label cap).
bool resultsCombinedFirstPage(uint8_t statCount) {
  if (gSessionResultCount == 0 || statCount > 3) return false;
  const int32_t metaLH = coachTypography().metadataLineHeight;
  // Condensed summary: status+44, headers+44, numbers+76, detail+36, bar+38, divider+14+18 = 270
  const int32_t summaryH = 44 + 44 + 76 + 36 + 38 + 14 + 18;
  // Per category: up to 2-line label (drawWrappedText maxLines=2) + gap + bar increment
  const int32_t perCatMaxH = metaLH * 2 + 12 + 62;
  const int32_t needed = summaryH + static_cast<int32_t>(statCount) * perCatMaxH;
  // Available: display(960) - resultHeader(132) - footer(86) = 742px
  const int32_t available = 742;
  return needed <= available;
}

uint8_t resultsPageCountFor(uint8_t statCount) {
  if (gSessionResultCount == 0) {
    return 1;
  }
  const uint8_t categoryPages = resultsCategoryPageCount(statCount);
  if (resultsCombinedFirstPage(statCount)) {
    // Combined page absorbs the first category page; remaining cats + weakest + recent
    const uint8_t extraCatPages = categoryPages > 1 ? static_cast<uint8_t>(categoryPages - 1) : 0;
    return static_cast<uint8_t>(1 + extraCatPages + 2);
  }
  return static_cast<uint8_t>(1 + categoryPages + 2);
}

uint8_t currentResultsPageCount() {
  CategoryStat stats[8];
  const uint8_t statCount = buildCategoryStats(stats, countOf(stats));
  return resultsPageCountFor(statCount);
}

void drawResultsFooter(uint8_t pageCount) {
  if (pageCount > 1) {
    drawCoachFooterNav(true, true);
  } else {
    drawCenteredHomeFooter();
  }
}

void drawResultsPageLabel(const String& label, uint8_t pageNumber, uint8_t pageCount) {
  auto& display = M5.Display;
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  String line = label;
  if (pageCount > 1) {
    line = headerJoin2(line, headerPageText(pageNumber, pageCount));
  }
  display.drawString(line, 34, 92);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void renderResultsSummaryPage(uint16_t total, uint16_t correct, uint8_t accuracy) {
  auto& display = M5.Display;
  int32_t y = 144;
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString(gResultsStorageStatus, 36, y);
  y += 50;

  applyCoachContentFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Answered", 36, y);
  display.drawString("Correct", 306, y);
  y += 48;

  applyCoachTitleFont();
  display.drawString(String(total), 36, y);
  display.drawString(String(accuracy) + "%", 306, y);
  y += 86;

  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString(String(static_cast<unsigned>(correct)) + " correct of " + static_cast<unsigned>(total), 36, y);
  y += 42;
  drawResultBar(36, y, display.width() - 72, 28, accuracy);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

// Combined page 0: summary block (numbers + bar) followed by category stats (if ≤3 categories).
void renderResultsSummaryAndCategoriesPage(uint16_t total, uint16_t correct, uint8_t accuracy,
                                            const CategoryStat* stats, uint8_t statCount) {
  auto& display = M5.Display;
  int32_t y = 144;

  // Summary block (condensed)
  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString(gResultsStorageStatus, 36, y);
  y += 44;

  applyCoachContentFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Answered", 36, y);
  display.drawString("Correct", 306, y);
  y += 44;

  applyCoachTitleFont();
  display.drawString(String(total), 36, y);
  display.drawString(String(accuracy) + "%", 306, y);
  y += 76;

  applyCoachMetadataFont();
  display.setTextColor(metadataTextColor(), TFT_WHITE);
  display.drawString(String(static_cast<unsigned>(correct)) + " correct of " + static_cast<unsigned>(total), 36, y);
  y += 36;
  drawResultBar(36, y, display.width() - 72, 22, accuracy);
  y += 38;
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  if (statCount == 0) {
    return;
  }

  // Divider
  y += 14;
  display.drawLine(36, y, display.width() - 36, y, metadataTextColor());
  y += 18;

  // Category stats (up to 3 on this combined page)
  const uint8_t shown = statCount < 3 ? statCount : 3;
  for (uint8_t index = 0; index < shown; ++index) {
    const uint8_t statAccuracy = resultAccuracyPercent(stats[index].correct, stats[index].total);
    applyCoachMetadataFont();
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    TextLayoutResult labelLayout =
        drawWrappedText(stats[index].name, 36, y, display.width() - 140, coachTypography().metadataLineHeight, 2,
                        "results-category-name", 1);
    applyCoachContentFont();
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(textdatum_t::top_right);
    display.drawString(String(statAccuracy) + "%", display.width() - 36, y - 4);
    display.setTextDatum(textdatum_t::top_left);
    y += labelLayout.height + 12;
    drawResultBar(36, y, display.width() - 72, 20, statAccuracy);
    y += 62;
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void renderResultsCategoriesPage(const CategoryStat* stats, uint8_t statCount, uint8_t categoryPage,
                                 uint8_t categoryPages) {
  auto& display = M5.Display;
  static constexpr uint8_t kResultsStatsPerPage = 3;
  const uint8_t start = categoryPage * kResultsStatsPerPage;
  const uint8_t end =
      statCount < static_cast<uint8_t>(start + kResultsStatsPerPage) ? statCount : static_cast<uint8_t>(start + kResultsStatsPerPage);
  int32_t y = 144;
  if (statCount == 0) {
    applyCoachContentFont();
    drawWrappedText("No category data yet.", 36, y, display.width() - 72, coachTypography().bodyLineHeight, 3,
                    "results-no-categories", 1);
    return;
  }

  for (uint8_t index = start; index < end; ++index) {
    const uint8_t statAccuracy = resultAccuracyPercent(stats[index].correct, stats[index].total);
    applyCoachMetadataFont();
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    TextLayoutResult labelLayout =
        drawWrappedText(stats[index].name, 36, y, display.width() - 140, coachTypography().metadataLineHeight, 2,
                        "results-category-name", 1);
    applyCoachContentFont();
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(textdatum_t::top_right);
    display.drawString(String(statAccuracy) + "%", display.width() - 36, y - 4);
    display.setTextDatum(textdatum_t::top_left);
    y += labelLayout.height + 14;
    drawResultBar(36, y, display.width() - 72, 24, statAccuracy);
    y += 72;
  }

  if (categoryPages > 1) {
    applyCoachMetadataFont();
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    display.drawString(String("Category page ") + static_cast<unsigned>(categoryPage + 1) + "/" +
                           static_cast<unsigned>(categoryPages),
                       36, display.height() - 148);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
  }
}

void renderResultsWeakestPage(const CategoryStat* stats, uint8_t statCount) {
  auto& display = M5.Display;
  int32_t y = 148;
  bool pickedStats[8] = {};
  uint8_t shown = 0;
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

    applyCoachContentFont();
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    TextLayoutResult labelLayout =
        drawWrappedText(String(rank + 1) + ". " + stats[weakestIndex].name, 36, y, display.width() - 72,
                        coachTypography().bodyLineHeight, 2, "results-weakest-label", 1);
    y += labelLayout.height + 8;
    applyCoachMetadataFont();
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    display.drawString(String(weakestAccuracy) + "% accuracy, " + static_cast<unsigned>(stats[weakestIndex].total) +
                           " answered",
                       58, y);
    y += 58;
    pickedStats[weakestIndex] = true;
    ++shown;
  }
  if (shown == 0) {
    applyCoachContentFont();
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    drawWrappedText("No weak areas yet. Answer more questions to build a useful pattern.", 36, y, display.width() - 72,
                    coachTypography().bodyLineHeight, 4, "results-no-weak", 1);
  }
}

void renderResultsRecentPage() {
  auto& display = M5.Display;
  int32_t y = 144;
  applyCoachContentFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Recent misses", 36, y);
  y += 54;

  uint8_t shownMisses = 0;
  for (size_t offset = 0; offset < gSessionResultCount && shownMisses < 3; ++offset) {
    const size_t index = gSessionResultCount - 1 - offset;
    const SessionResult& result = gSessionResults[index];
    if (result.correct) {
      continue;
    }
    String itemLine = result.cardId[0] != '\0' ? String(result.cardId) : String(result.itemId);
    String resultId = result.itemId;
    if (resultId.indexOf("-watch") >= 0) {
      itemLine += " - Watch";
    } else if (resultId.indexOf("-confidence") >= 0) {
      itemLine += " - Confidence";
    } else if (resultId.indexOf("-metric") >= 0) {
      itemLine += " - Metric";
    }
    applyCoachMetadataFont();
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    TextLayoutResult itemLayout =
        drawWrappedText(itemLine, 42, y, display.width() - 84, coachTypography().metadataLineHeight, 2,
                        "recent-miss-item", 1);
    y += itemLayout.height + 4;
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    TextLayoutResult categoryLayout =
        drawWrappedText(result.category, 42, y, display.width() - 84, coachTypography().metadataLineHeight, 1,
                        "recent-miss-category", 1);
    y += categoryLayout.height + 4;
    String best = "Best: ";
    best += result.bestOption < kMaxOptions ? String(static_cast<char>('A' + result.bestOption)) : "-";
    drawWrappedText(best, 42, y, display.width() - 84, coachTypography().metadataLineHeight, 1, "recent-miss-best", 1);
    y += coachTypography().metadataLineHeight + 18;
    ++shownMisses;
  }
  if (shownMisses == 0) {
    applyCoachMetadataFont();
    display.setTextColor(metadataTextColor(), TFT_WHITE);
    display.drawString("No misses in this session.", 42, y);
    y += 58;
  }

  applyCoachContentFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Next", 36, y);
  y += 52;
  applyCoachMetadataFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  drawWrappedText(recommendedNextPractice(), 42, y, display.width() - 84, coachTypography().metadataLineHeight, 4,
                  "recommendation", 1);
}

void renderResultsScreen(const char* refreshReason = "mode switch") {
  const bool enteringResults = gScreen != Screen::Results;
  gScreen = Screen::Results;
  if (enteringResults) {
    gResultsPage = 0;
  }
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Results", 32, 34);

  if (gSessionResultCount == 0) {
    drawResultsPageLabel("Session summary", 1, 1);
    applyCoachContentFont();
    drawWrappedText("No results yet. Answer Drills or Exam questions to build a session summary.", kCoachMargin, 154,
                    display.width() - kCoachMargin * 2, coachLineHeight(), 5, "results-empty", 1);
    drawResultsFooter(1);
    finishDisplayRefresh();
    Serial.println("Results screen shown: empty state.");
    return;
  }

  const uint16_t total = static_cast<uint16_t>(gSessionResultCount);
  const uint16_t correct = resultCorrectCount();
  const uint8_t accuracy = resultAccuracyPercent(correct, total);
  CategoryStat stats[8];
  const uint8_t statCount = buildCategoryStats(stats, countOf(stats));
  const uint8_t categoryPages = resultsCategoryPageCount(statCount);
  const uint8_t pageCount = resultsPageCountFor(statCount);
  if (gResultsPage >= pageCount) {
    gResultsPage = pageCount - 1;
  }
  gReaderContentRect = {34, 132, display.width() - 68, coachFooterTop() - 132 - 10};

  const bool combined = resultsCombinedFirstPage(statCount);
  // Extra category pages beyond those absorbed by the combined first page.
  const uint8_t extraCatPages = combined && categoryPages > 1 ? static_cast<uint8_t>(categoryPages - 1) : 0;

  if (gResultsPage == 0) {
    if (combined) {
      drawResultsPageLabel("Summary", gResultsPage + 1, pageCount);
      renderResultsSummaryAndCategoriesPage(total, correct, accuracy, stats, statCount);
    } else {
      drawResultsPageLabel("Summary", gResultsPage + 1, pageCount);
      renderResultsSummaryPage(total, correct, accuracy);
    }
  } else if (!combined && gResultsPage <= categoryPages) {
    drawResultsPageLabel("Categories", gResultsPage + 1, pageCount);
    renderResultsCategoriesPage(stats, statCount, gResultsPage - 1, categoryPages);
  } else if (combined && gResultsPage <= extraCatPages) {
    // Extra category pages beyond the combined first page (show starting from cat page 1)
    drawResultsPageLabel("Categories", gResultsPage + 1, pageCount);
    renderResultsCategoriesPage(stats, statCount, gResultsPage, categoryPages);
  } else {
    const uint8_t weakestPage = combined ? extraCatPages + 1 : categoryPages + 1;
    if (gResultsPage == weakestPage) {
      drawResultsPageLabel("Weakest areas", gResultsPage + 1, pageCount);
      renderResultsWeakestPage(stats, statCount);
    } else {
      drawResultsPageLabel("Recent / next", gResultsPage + 1, pageCount);
      renderResultsRecentPage();
    }
  }

  drawResultsFooter(pageCount);
  finishDisplayRefresh();
  Serial.printf("Results screen shown: total=%u correct=%u accuracy=%u page=%u/%u storage=%s\n", total, correct,
                accuracy, static_cast<unsigned>(gResultsPage + 1), static_cast<unsigned>(pageCount),
                gResultsStorageStatus.c_str());
}

void renderSettings(const char* refreshReason = "mode switch") {
  gScreen = Screen::Settings;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  const int32_t width = display.width();

  // Snapshot content state before pinning font for UI chrome stability.
  // Settings layout is fixed regardless of Reader S/M/L.
  const FontSizeMode activeReaderSize = canonicalFontSizeMode(gSettings.fontSizeMode);
  const RefreshMode activeRefresh = gSettings.refreshMode;
  const PowerProfile activePower = gPowerProfile;
  const OrientationMode activeOrientation = gSettings.orientationMode;
  const FontSizeMode savedSize = gSettings.fontSizeMode;
  gSettings.fontSizeMode = FontSizeMode::Large;

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Settings", 32, 26);

  const int32_t bh = 52;   // button height — was 48, now 52 for better touch targets
  const int32_t bx = 36;
  const int32_t bw = width - 72;
  const int32_t sg = 8;
  const int32_t bw3 = (bw - sg * 2) / 3;
  const int32_t bx2_3 = bx + bw3 + sg;
  const int32_t bx3_3 = bx + 2 * (bw3 + sg);
  const int32_t bw2 = (bw - sg) / 2;
  const int32_t bx2_2 = bx + bw2 + sg;
  // 28px for section labels — one step above metadata (24px), clearly readable
  static constexpr uint8_t kSettingsLabelPx = 28;

  // Battery block: % left + bar right, vertically co-aligned
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  const int32_t battLevel = batteryLevelPercent();
  const int16_t battMv = gCachedBatteryMv;
  const int32_t battBlockY = 72;
  const int32_t battBlockH = 52;
  const int32_t barH = 38;
  const int32_t barW = 210;
  const int32_t barY = battBlockY + (battBlockH - barH) / 2;
  applyCoachTitleFont();
  const String battPctText = (battLevel >= 0) ? String(static_cast<unsigned>(battLevel)) + "%" : "--%";
  const int32_t battCenterY = battBlockY + battBlockH / 2;
  display.setTextDatum(textdatum_t::middle_left);
  display.drawString(battPctText, bx, battCenterY);
  display.setTextDatum(textdatum_t::top_left);
  drawBatteryBar(width - bx - barW, barY, barW, barH, battLevel);
  applyTypographyFont(kSettingsLabelPx);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  const String mvText = (battMv > 0) ? String(battMv) + "mV" : "---mV";
  const int32_t detailY = battBlockY + battBlockH + 10;
  display.drawString(mvText, bx, detailY);
  const String usbText = String("USB: ") + usbPowerName(gCachedVbusMv)
                       + "  " + chargingStateName(gCachedChargingState);
  display.drawString(usbText, bx, detailY + 32);

  // Reader size — S / M / L
  applyTypographyFont(kSettingsLabelPx);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Reader size", bx, 206);
  gFontMediumButton = {bx,    242, bw3, bh};
  gFontLargeButton  = {bx2_3, 242, bw3, bh};
  gFontXlButton     = {bx3_3, 242, bw3, bh};
  gFontXxlButton = {};
  gFontHugeButton = {};
  drawSegmentedButton(gFontMediumButton, "S", activeReaderSize == FontSizeMode::Medium);
  drawSegmentedButton(gFontLargeButton,  "M", activeReaderSize == FontSizeMode::Large);
  drawSegmentedButton(gFontXlButton,     "L", activeReaderSize == FontSizeMode::XL);

  // Refresh — Fast / Balanced / Clean
  applyTypographyFont(kSettingsLabelPx);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Refresh", bx, 310);
  gRefreshFastButton     = {bx,    346, bw3, bh};
  gRefreshBalancedButton = {bx2_3, 346, bw3, bh};
  gRefreshCleanButton    = {bx3_3, 346, bw3, bh};
  gRefreshModeButton = {};
  drawSegmentedButton(gRefreshFastButton,     "Fast",     activeRefresh == RefreshMode::Fast);
  drawSegmentedButton(gRefreshBalancedButton, "Balanced", activeRefresh == RefreshMode::Balanced);
  drawSegmentedButton(gRefreshCleanButton,    "Clean",    activeRefresh == RefreshMode::Clean);

  // Power — Responsive / Balanced / Max
  applyTypographyFont(kSettingsLabelPx);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Power", bx, 414);
  gPowerResponsiveButton = {bx,    450, bw3, bh};
  gPowerBalancedButton   = {bx2_3, 450, bw3, bh};
  gPowerMaxButton        = {bx3_3, 450, bw3, bh};
  gPowerProfileButton = {};
  gBadgeSleepButton = {};
  gPowerModeButton = {};
  drawSegmentedButton(gPowerResponsiveButton, "Responsive", activePower == PowerProfile::Balanced);
  drawSegmentedButton(gPowerBalancedButton,   "Balanced",   activePower == PowerProfile::Aggressive);
  drawSegmentedButton(gPowerMaxButton,        "Max",        activePower == PowerProfile::BadgeMax);

  // Orientation — Normal / Strap
  applyTypographyFont(kSettingsLabelPx);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("Orientation", bx, 518);
  gOrientationButton      = {bx,    554, bw2, bh};
  gOrientationStrapButton = {bx2_2, 554, bw2, bh};
  gLanguageAutoButton = {};
  gLanguageEnglishButton = {};
  gLanguageJapaneseButton = {};
  gFontStyleButton = {};
  drawSegmentedButton(gOrientationButton,      "Normal", activeOrientation == OrientationMode::Handheld);
  drawSegmentedButton(gOrientationStrapButton, "Strap",  activeOrientation == OrientationMode::Strap);

  // Advanced button — uses same 24px font as segmented controls for visual consistency.
  gAdvancedButton = {bx, 632, bw, 58};
  drawSegmentedButton(gAdvancedButton, "Advanced", false);

  // Home button
  gHomeButton = {bx, display.height() - 82, bw, 62};
  drawButton(gHomeButton, "", IconType::Home);

  gSettings.fontSizeMode = savedSize;
  finishDisplayRefresh();
  logTypographySettings("settings screen");
  logPowerAudit("settings screen");
  Serial.printf("Settings screen shown: font=%s refresh=%s profile=%s orientation=%s\n",
                fontSizeModeName(), refreshModeName(), powerProfileName(),
                gSettings.orientationMode == OrientationMode::Strap ? "strap" : "handheld");
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
  display.drawString(String("touch: dn ") + gLastTouchDownX + "," + gLastTouchDownY +
                         "  up " + gLastTouchUpX + "," + gLastTouchUpY + "  hit: " + gLastHitTarget,
                     26, y);
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
  int32_t actionY = display.height() - 420;
  gHelpButton = {actionX, actionY, actionW, actionH};
  gPowerLabButton = {rightX, actionY, actionW, actionH};
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
  gPowerProfileButton = {};   // profile is in Settings and Power Lab; remove from Debug
  gHomeButton = {26, display.height() - 94, display.width() - 52, 58};
  drawButton(gHelpButton, "Help");
  drawButton(gPowerLabButton, "Power Lab");
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

void renderAdvanced(const char* refreshReason = "mode switch") {
  gScreen = Screen::Advanced;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  const FontSizeMode savedSize = gSettings.fontSizeMode;
  gSettings.fontSizeMode = FontSizeMode::Large;

  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  applyCoachMetadataFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  int32_t y = 28;
  display.drawString("Advanced", 26, y);
  y += 34;
  display.drawString(String("firmware: ") + kFirmwareVersion, 26, y);
  y += 26;
  display.drawString(String("deck: ") + gCoachDeckSource + " " + static_cast<unsigned>(gCoachItemCount) +
                         "  SD: " + (gSdMounted ? "yes" : "no"),
                     26, y);
  y += 26;
  display.drawString(String("profile: ") + powerProfileName() + "  sleep: " + badgeSleepModeName(), 26, y);
  y += 26;
  display.drawString(batteryStatusLine(), 26, y);
  y += 24;
  drawBatteryBar(26, y, 220, 16, batteryLevelPercent());
  y += 26;
  display.drawString(String("font: ") + fontSizeModeName() + "  refresh: " + refreshModeName(), 26, y);
  y += 24;
  display.drawString(String("trace: ") + gLastRenderTraceStatus, 26, y);
  y += 24;
  display.drawString(String("export: ") + gLastDeckExportStatus, 26, y);
  y += 24;
  if (gSettings.badgeSleepMode != BadgeSleepMode::Off) {
    display.drawString("Sleep: tap after wake  long press = disable", 26, y);
    y += 24;
  }

  const int32_t actionX = 26;
  const int32_t actionGap = 10;
  const int32_t actionW = (display.width() - 52 - actionGap) / 2;
  const int32_t actionH = 42;
  const int32_t rightX = actionX + actionW + actionGap;
  // Start button grid just below the info text (minimum gap of 20px, clamp above home button)
  const int32_t minActionY = display.height() - 490;
  int32_t actionY = (y + 20 > minActionY) ? y + 20 : minActionY;
  gHelpButton = {actionX, actionY, actionW, actionH};
  gPowerLabButton = {rightX, actionY, actionW, actionH};
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
  gBadgeSleepButton = {rightX, actionY, actionW, actionH};
  gPowerProfileButton = {};
  gDebugButton = {};
  gHomeButton = {26, display.height() - 94, display.width() - 52, 58};
  drawButton(gHelpButton, "Help");
  drawButton(gPowerLabButton, "Power Lab");
  drawButton(gVisualQaButton, "Visual QA", IconType::Exam);
  drawButton(gFontLabButton, "Font Lab");
  drawButton(gTypographyResetButton, "Reset Type");
  drawButton(gLayoutDebugButton, "Layout Log");
  drawButton(gRenderTraceButton, "Trace Dump");
  drawButton(gTouchDebugButton, gTouchDebugEnabled ? "Touch Dbg Off" : "Touch Dbg On");
  drawButton(gExportDeckButton, "Export deck");
  drawButton(gBadgeSleepButton, String("Sleep: ") + badgeSleepModeName());
  drawButton(gHomeButton, "", IconType::Home);

  gSettings.fontSizeMode = savedSize;
  finishDisplayRefresh();
  logTypographySettings("advanced screen");
  logPowerAudit("advanced screen");
  Serial.println("Advanced screen shown.");
}

// ---- Power Lab: power discovery and event history screen ----

static String fmtDurationMs(uint32_t ms) {
  if (ms == 0) return "0ms";
  if (ms < 1000) return String(ms) + "ms";
  if (ms < 60000) return String(ms / 1000) + "." + String((ms % 1000) / 100) + "s";
  return String(ms / 60000) + "m" + String((ms % 60000) / 1000) + "s";
}

static String fmtMsSince(uint32_t atMs) {
  if (atMs == 0) return "never";
  return fmtDurationMs(millis() - atMs) + " ago";
}

void renderPowerLab(const char* refreshReason = "mode switch") {
  gPreRenderCpuMhz = static_cast<uint32_t>(ESP.getCpuFreqMHz());
  gPreRenderWasIdleScaled = gIdleCpuScaled;

  gScreen = Screen::PowerLab;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  const FontSizeMode savedSize = gSettings.fontSizeMode;
  gSettings.fontSizeMode = FontSizeMode::Large;

  auto& display = M5.Display;
  constexpr uint8_t kPowerLabPageCount = 4;
  if (gPowerLabPage >= kPowerLabPageCount) gPowerLabPage = 0;

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  const String pageLabel = String("Power Lab ") + (gPowerLabPage + 1) + "/" + kPowerLabPageCount;
  display.drawString(pageLabel, 26, 26);

  applyCoachMetadataFont();
  int32_t y = 78;
  const int32_t lineH = coachTypography().metadataLineHeight;
  const int32_t maxY = display.height() - 130;
  auto row = [&](const String& text) {
    if (y >= maxY) return;
    TextLayoutResult result = drawWrappedText(text, 26, y, display.width() - 52, lineH, 2, "power-lab-row", 1);
    y += result.height + 5;
  };

  if (gPowerLabPage == 0) {
    // Page 1: CPU / stage / idle counters
    row(String("Stage: ") + powerStageName(gPowerStage) +
        "  last: " + powerStageName(gLastPowerStage) +
        "  transitions: " + static_cast<unsigned>(gStageTransitionCount));
    row(String("Profile: ") + powerProfileName() +
        "  WarmIdle@" + static_cast<unsigned>(profileIdleScaleThresholdMs() / 1000) + "s" +
        "  LightNap@" + static_cast<unsigned>(profileLightSleepIdleMs() / 1000) + "s");
    row(String("CPU now: ") + static_cast<unsigned>(ESP.getCpuFreqMHz()) + "MHz" +
        "  pre-render: " + gPreRenderCpuMhz + "MHz" +
        (gPreRenderWasIdleScaled ? " [idle]" : " [active]"));
    row(String("Scale: ") + static_cast<unsigned>(gCpuScaleCount) +
        "  Restore: " + static_cast<unsigned>(gCpuRestoreCount));
    row(String("Last scaled: ") + fmtMsSince(gLastIdleScaledAtMs) +
        "  last restore: " + fmtMsSince(gLastRestoreAtMs));
    row(String("Last restore reason: ") + (gLastRestoreAtMs > 0 ? gLastRestoreReason : String("none")));
    row(String("80MHz last: ") + fmtDurationMs(gLast80MhzDurationMs) +
        "  total: " + fmtDurationMs(gCumulative80MhzMs) +
        "  longest: " + fmtDurationMs(gLongest80MhzMs));
    row(String("Light nap total: ") + fmtDurationMs(gLightSleepTotalMs) +
        "  longest: " + fmtDurationMs(gLongestLightSleepMs));
    {
      const uint32_t sinceInput = gLastUserActivityMs > 0 ? millis() - gLastUserActivityMs : 0;
      row(String("Last input: ") + fmtDurationMs(sinceInput) + " ago" +
          "  idle entries: " + static_cast<unsigned>(gIdleEntryCount));
    }
    {
      const String blocked = idleScaleBlockedReason();
      const uint32_t sinceInput = gLastUserActivityMs > 0 ? millis() - gLastUserActivityMs : 0;
      const uint32_t warmThreshold = profileIdleScaleThresholdMs();
      String eta;
      if (gIdleCpuScaled) {
        eta = "active";
      } else if (blocked.length() > 0) {
        eta = "blocked";
      } else if (sinceInput >= warmThreshold) {
        eta = "due now";
      } else {
        eta = fmtDurationMs(warmThreshold - sinceInput) + " left";
      }
      row(String("WarmIdle: ") + (gIdleCpuScaled ? "ACTIVE" : "inactive") + "  in: " + eta +
          (blocked.length() > 0 ? (String("  (") + blocked + ")") : String("")));
    }
    row(String("Loop delay: ") + static_cast<unsigned>(loopDelayMs()) + "ms" +
        "  redraws while idle: " + static_cast<unsigned>(gRedrawWhileIdleCount));
    row(String("Screen: ") + screenName(gScreen) +
        "  refreshes: " + static_cast<unsigned>(gDisplayRefreshCount) +
        "  last: " + fmtMsSince(gLastRefreshEndMs));
    {
      const String napBlocked = lightNapBlockedReason();
      String napEta;
      if (napBlocked.length() > 0) {
        napEta = "blocked";
      } else {
        const uint32_t sinceInput = gLastUserActivityMs > 0 ? millis() - gLastUserActivityMs : 0;
        const uint32_t napThreshold = profileLightSleepIdleMs();
        napEta = sinceInput >= napThreshold ? "due now" : fmtDurationMs(napThreshold - sinceInput) + " left";
      }
      row(String("LightNap (this screen): ") + (napBlocked.length() == 0 ? "eligible" : String("no — ") + napBlocked) +
          "  in: " + napEta);
    }
  } else if (gPowerLabPage == 1) {
    // Page 2: battery / peripherals
    const bool wifiOff = WiFi.getMode() == WIFI_OFF;
    const bool btOn = btStarted();
    const int32_t level = batteryLevelPercent();
    row(String("Battery: ") + gCachedBatteryMv + "mV  " + level + "%");
    row(String("Charge: ") + chargingStateName(gCachedChargingState) +
        "  " + gCachedBatteryCurrentMa + "mA" +
        "  USB: " + usbPowerName(gCachedVbusMv) + " " + gCachedVbusMv + "mV");
    row(String("Batt poll: ") + fmtMsSince(gLastPowerPollMs) +
        "  interval: " + static_cast<unsigned>(profileBatteryPollMs() / 1000) + "s" +
        (gIdleModeActive ? " (idle)" : " (active)"));
    row(String("Wi-Fi: ") + (wifiOff ? "off" : "ON") +
        "  BT: " + (btOn ? "ON" : "off") +
        "  Speaker: stopped");
    row("Mic: not used  IMU: not started");
    row(String("SD: ") + (gSdMounted ? "mounted/idle" : "not mounted"));
    row(String("Redraws while idle: ") + static_cast<unsigned>(gRedrawWhileIdleCount));
  } else if (gPowerLabPage == 2) {
    // Page 3: Sleep Lab — nap/wake/listen diagnostics and sleep mode button
    row(String("Sleep mode: ") + badgeSleepModeName() +
        "  stage: " + powerStageName(gPowerStage) +
        "  last: " + powerStageName(gLastPowerStage));
    row(String("Nap: ") + static_cast<unsigned>(profileLightSleepDurationUs() / 1000000) + "s  " +
        "attempts: " + static_cast<unsigned>(gLightSleepAttemptCount) +
        "  entered: " + static_cast<unsigned>(gLightSleepEnteredCount) +
        "  woke: " + static_cast<unsigned>(gLightSleepWakeCount));
    row(String("Light total: ") + fmtDurationMs(gLightSleepTotalMs) +
        "  last: " + fmtDurationMs(gLastLightSleepDurationMs) +
        "  longest: " + fmtDurationMs(gLongestLightSleepMs));
    row(String("Wake reason: ") + gLastWakeReason +
        "  last wake: " + (gLastWakeTimestampMs > 0 ? fmtMsSince(gLastWakeTimestampMs) : String("none")));
    row(String("Last sleep attempt: ") + gLastSleepAttempt);
    {
      const uint32_t now = millis();
      const bool inWindow = gInWakeListenWindow && now < gWakeListenWindowEndMs;
      row(String("Listen window: ") + (inWindow ? "ACTIVE" : "idle") +
          "  dur: " + static_cast<unsigned>(gWakeListenWindowDurationMs / 1000) + "s" +
          "  entered: " + static_cast<unsigned>(gWakeListenWindowEnteredCount));
      if (inWindow) {
        row(String("  expires in: ") + fmtDurationMs(gWakeListenWindowEndMs - now));
      }
    }
    row(String("Input after wake: ") + (gInputDetectedAfterWake ? "yes (held)" : "no") +
        "  listen touch: " + (gWakeListenWindowTouchDetected ? "yes" : "no"));
    row(String("Input locked: ") + (gInputLocked ? "YES" : "no") +
        "  watchdog clears: " + static_cast<unsigned>(gInputLockWatchdogCount));
    row(String("Long press escapes: ") + static_cast<unsigned>(gLongPressEscapeCount) +
        (gSleepDisabledByLongPress ? "  (last: long press)" : ""));
    // Sleep mode toggle — placed 20px above footer to avoid overlap
    const int32_t sleepBtnH = 52;
    const int32_t sleepBtnW = display.width() - 52;
    const int32_t sleepBtnY = maxY + 10;
    gBadgeSleepButton = {26, sleepBtnY, sleepBtnW, sleepBtnH};
    drawButton(gBadgeSleepButton, String("Sleep mode: ") + badgeSleepModeName());
  } else {
    // Page 4: Wake source / deep sleep audit
    row(String("--- Wake source audit (") + kFirmwareVersion + ") ---");
    row("GT911 touch INT = GPIO48.");
    row("ESP32-S3 RTC GPIO range = 0-21 only.");
    row("GPIO48 is NOT RTC-wake capable — cannot wake from deep sleep.");
    row("Power button = GPIO44, also NOT RTC-wake capable.");
    row("Deep sleep: BLOCKED — no verified RTC wake source.");
    row("Light sleep: timer wake ONLY. Short taps during nap are missed.");
    row("Sleep Off resets on reboot (experimental flag).");
    row("LightNap eligible screens: Badge, Home, Practice, Glossary,");
    row("  Drills, Exam, Results, DrillsMenu, GlossaryMenu, PracticeMenu.");
    row("NOT eligible: Settings, Advanced, Debug, PowerLab, FontLab,");
    row("  VisualQA, TouchDebug, HelpLegend, PowerAudit.");
  }

  // Footer: Profile | Page | Home  (body tap refreshes current page)
  const int32_t paFooterH = 58;
  const int32_t paFooterY = display.height() - paFooterH - 10;
  const int32_t paGap = 10;
  const int32_t paW = (display.width() - 52 - 2 * paGap) / 3;
  gPowerProfileButton = {26, paFooterY, paW, paFooterH};
  gFilterButton = {26 + paW + paGap, paFooterY, paW, paFooterH};
  gHomeButton = {26 + 2 * (paW + paGap), paFooterY, paW, paFooterH};
  gPowerModeButton = {};
  gPowerAuditButton = {};
  if (gPowerLabPage != 2) gBadgeSleepButton = {};
  gNextButton = {};
  // Use short profile name to avoid footer overflow
  const char* profileShort = (gPowerProfile == PowerProfile::Balanced)   ? "Resp"
                           : (gPowerProfile == PowerProfile::Aggressive)  ? "Bal"
                                                                          : "Max";
  drawButton(gPowerProfileButton, profileShort);
  drawButton(gFilterButton, "Page");
  drawButton(gHomeButton, "", IconType::Home);

  gSettings.fontSizeMode = savedSize;
  finishDisplayRefresh();
  logPowerAudit("power lab screen");
  Serial.printf("Power Lab shown: page=%u/%u profile=%s stage=%s scaleCount=%u lightNaps=%u\n",
                gPowerLabPage + 1, kPowerLabPageCount, powerProfileName(),
                powerStageName(gPowerStage),
                static_cast<unsigned>(gCpuScaleCount),
                static_cast<unsigned>(gLightSleepEnteredCount));
}

void renderPowerAudit(const char* refreshReason = "mode switch") {
  // Capture CPU state BEFORE prepareFullRefresh() restores it to 240MHz
  gPreRenderCpuMhz = static_cast<uint32_t>(ESP.getCpuFreqMHz());
  gPreRenderWasIdleScaled = gIdleCpuScaled;

  gScreen = Screen::PowerAudit;
  applyAppRotation();
  prepareFullRefresh(refreshReason, true);

  auto& display = M5.Display;
  constexpr uint8_t kPowerAuditPageCount = 4;
  if (gPowerAuditPage >= kPowerAuditPageCount) {
    gPowerAuditPage = 0;
  }

  const int32_t level = batteryLevelPercent();
  const bool wifiOff = WiFi.getMode() == WIFI_OFF;
  const bool bluetoothStarted = btStarted();

  display.setTextDatum(textdatum_t::top_left);
  applyCoachTitleFont();
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  const String pageLabel = String("Power Audit ") + (gPowerAuditPage + 1) + "/" + kPowerAuditPageCount;
  display.drawString(pageLabel, 26, 26);

  applyCoachMetadataFont();
  int32_t y = 78;
  const int32_t lineH = coachTypography().metadataLineHeight;
  const int32_t maxY = display.height() - 200;
  auto row = [&](const String& text) {
    if (y >= maxY) return;
    TextLayoutResult result = drawWrappedText(text, 26, y, display.width() - 52, lineH, 2, "power-audit-row", 1);
    y += result.height + 5;
  };

  if (gPowerAuditPage == 0) {
    row(String("Battery: ") + gCachedBatteryMv + "mV  " + level + "%");
    row(String("USB/VBUS: ") + usbPowerName(gCachedVbusMv) + "  " + gCachedVbusMv + "mV");
    row(String("Charge: ") + chargingStateName(gCachedChargingState) + "  " + gCachedBatteryCurrentMa + "mA");
    row(String("Wi-Fi: ") + (wifiOff ? "off" : "on") + "  BT: " + (bluetoothStarted ? "on" : "off"));
    row("IMU: disabled  Speaker: stopped");
    row(String("SD: ") + (gSdMounted ? "mounted" : "missing"));
    row(String("Power mode: ") + powerModeName());
    row(String("Profile: ") + powerProfileName());
  } else if (gPowerAuditPage == 1) {
    // CPU now always shows 240MHz (restored by prepareFullRefresh before this draw).
    // gPreRenderCpuMhz / gPreRenderWasIdleScaled capture the state that existed at render time.
    row(String("CPU now: ") + static_cast<unsigned>(ESP.getCpuFreqMHz()) + " MHz [active]");
    if (gPreRenderWasIdleScaled) {
      row(String("Pre-render: ") + gPreRenderCpuMhz + " MHz [was idle-scaled]");
    } else {
      row(String("Pre-render: ") + gPreRenderCpuMhz + " MHz [was active]");
    }
    row(String("Profile: ") + powerProfileName() + "  threshold: " +
        static_cast<unsigned>(profileIdleScaleThresholdMs() / 1000) + "s");
    row(String("Last scaled: ") + msSinceIdleScaled());
    row(String("Last restore: ") + (gLastRestoreAtMs > 0 ? gLastRestoreReason : String("none")));
    {
      const uint32_t sinceInput = gLastUserActivityMs > 0 ? millis() - gLastUserActivityMs : 0;
      const uint32_t threshold = profileIdleScaleThresholdMs();
      row(String("Since input: ") + sinceInput + "ms / " + threshold + "ms threshold");
    }
    {
      const String blocked = idleScaleBlockedReason();
      if (blocked.length() > 0) {
        row(String("Idle blocked: ") + blocked);
      } else {
        row(String("Eligible: yes  idle mode: ") + (gIdleModeActive ? "active" : "awake"));
      }
    }
    row(String("Loop delay: ") + static_cast<unsigned>(loopDelayMs()) + "ms  refreshes: " +
        static_cast<unsigned>(gDisplayRefreshCount));
  } else if (gPowerAuditPage == 2) {
    row(String("Sleep mode: ") + badgeSleepModeName());
    row(String("Sleep status: ") + sleepAuditStatusLine());
    row(String("Last sleep: ") + gLastSleepAttempt);
    row(String("Wake reason: ") + gLastWakeReason);
    row(String("Millis since boot: ") + static_cast<unsigned>(millis()));
    {
      const uint32_t sinceInput = gLastUserActivityMs > 0 ? millis() - gLastUserActivityMs : 0;
      row(String("Since input: ") + sinceInput + "ms");
    }
    row("Deep sleep: BLOCKED — GT911 INT=GPIO48, not RTC-wake capable on ESP32-S3.");
    row("Light sleep: allowed in Balanced/Max Battery. Disabled in Responsive profile.");
  } else {
    row(String("Answer keys invalid: ") + static_cast<unsigned>(gInvalidAnswerKeyCount));
    row(String("Last key warning: ") + gLastAnswerKeyWarning);
    row(String("Sanitize total: ") + static_cast<unsigned>(gSanitizerReplacementTotal));
    row(String("Last touch: ") + gLastTouchDownX + "," + gLastTouchDownY);
    row(String("Last hit: ") + gLastHitTarget);
    row(String("Deck source: ") + gCoachDeckSource);
    row(String("Items: ") + static_cast<unsigned>(gCoachItemCount) + " drills: " +
        static_cast<unsigned>(gCoachDrillCount));
    row(String("Firmware: ") + kFirmwareVersion);
  }

  // Standard 3-button footer: prev / home / next
  const int32_t paFooterH = 58;
  const int32_t paFooterY = display.height() - paFooterH - 10;
  const int32_t paGap = 10;
  const int32_t paW = (display.width() - 52 - 2 * paGap) / 3;
  gFilterButton = {26, paFooterY, paW, paFooterH};
  gHomeButton = {26 + paW + paGap, paFooterY, paW, paFooterH};
  gNextButton = {26 + 2 * (paW + paGap), paFooterY, paW, paFooterH};
  gPowerModeButton = {};
  gPowerProfileButton = {};
  gBadgeSleepButton = {};
  drawButton(gFilterButton, "< Prev");
  drawButton(gHomeButton, "", IconType::Home);
  drawButton(gNextButton, "Next >");

  finishDisplayRefresh();
  logPowerAudit("power audit screen");
  Serial.printf("Power audit shown: page=%u/%u profile=%s\n", gPowerAuditPage + 1, kPowerAuditPageCount,
                powerProfileName());
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
  const int32_t lineH = coachTypography().metadataLineHeight;
  const int32_t contentW = display.width() - 68;
  for (size_t index = 0; index < countOf(lines); ++index) {
    TextLayoutResult result = drawWrappedText(lines[index], 34, y, contentW, lineH, 2, "help-line", 1);
    y += result.height + 6;
    if (y > display.height() - 130) {
      Serial.printf("Help legend fit warning: stopped at line=%u\n", static_cast<unsigned>(index));
      break;
    }
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
    // Top half: previous page, or previous item if already on first page
    if (gCoachStage > 0) {
      markHitTarget("practice previous page", tapX, tapY);
      --gCoachStage;
      renderCoachScreen();
    } else if (hasPreviousCoachItem()) {
      markHitTarget("practice previous item", tapX, tapY);
      previousCoachItem();
      renderCoachScreen();
    } else {
      Serial.println("practice previous: at first item/page");
    }
    return;
  }

  // Bottom half: next page, or next item if on last page
  if (gCoachStage + 1 < stageCount) {
    markHitTarget("practice next page", tapX, tapY);
    ++gCoachStage;
    renderCoachScreen();
  } else if (hasNextCoachItem()) {
    markHitTarget("practice next item", tapX, tapY);
    nextCoachItem();
    renderCoachScreen();
  } else {
    Serial.println("practice next: at last item/page");
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
    gLastTouchDownMs = millis();
    if (gInWakeListenWindow) {
      Serial.printf("Listen window: touch press during window, window will close. screen=%s\n",
                    screenName(gScreen));
    }
    recordUserActivity("touch press");
    gLastTouchDownX = constrain(detail.x, 0, M5.Display.width());
    gLastTouchDownY = constrain(detail.y, 0, M5.Display.height());
    Serial.printf("touch down coordinates: x=%ld y=%ld screen=%s\n", static_cast<long>(gLastTouchDownX),
                  static_cast<long>(gLastTouchDownY), screenName(gScreen));
  }
  if (detail.wasClicked() || detail.wasReleased()) {
    gTouchActive = false;
    gLastTouchUpMs = millis();
    recordUserActivity("touch release");
    gLastTouchUpX = constrain(detail.x, 0, M5.Display.width());
    gLastTouchUpY = constrain(detail.y, 0, M5.Display.height());
    Serial.printf("touch up coordinates: x=%ld y=%ld screen=%s\n", static_cast<long>(gLastTouchUpX),
                  static_cast<long>(gLastTouchUpY), screenName(gScreen));
  }
  // Emergency escape: long press anywhere disables sleep and returns to Power Lab.
  // Only active when Sleep mode is not Off — dev escape, not production behavior.
  if (gSettings.badgeSleepMode != BadgeSleepMode::Off && detail.wasHold()) {
    gSettings.badgeSleepMode = BadgeSleepMode::Off;
    gInWakeListenWindow = false;
    gSleepDisabledByLongPress = true;
    ++gLongPressEscapeCount;
    saveSettings();
    Serial.printf("Emergency long press: sleep disabled, to Power Lab. screen=%s escapeCount=%u\n",
                  screenName(gScreen), static_cast<unsigned>(gLongPressEscapeCount));
    renderPowerLab("emergency long press");
    return;
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

  if (gScreen == Screen::Advanced) {
    if (hitTarget(gHelpButton, "help legend", tapX, tapY)) {
      renderHelpLegend();
    } else if (hitTarget(gPowerLabButton, "power lab", tapX, tapY)) {
      gPowerLabPage = 0;
      renderPowerLab();
    } else if (hitTarget(gVisualQaButton, "visual qa", tapX, tapY)) {
      renderVisualQa();
    } else if (hitTarget(gFontLabButton, "font lab", tapX, tapY)) {
      renderFontLab();
    } else if (hitTarget(gTypographyResetButton, "reset typography", tapX, tapY)) {
      resetTypographyDefaults();
      saveSettings();
      renderAdvanced("typography reset");
    } else if (hitTarget(gLayoutDebugButton, "layout log", tapX, tapY)) {
      logCurrentLayoutDiagnostics("advanced button");
      renderAdvanced();
    } else if (hitTarget(gRenderTraceButton, "render trace dump", tapX, tapY)) {
      dumpCurrentRenderTraceToSd();
      renderAdvanced("render trace dump");
    } else if (hitTarget(gTouchDebugButton, "touch debug", tapX, tapY)) {
      gTouchDebugEnabled = !gTouchDebugEnabled;
      renderAdvanced();
    } else if (hitTarget(gExportDeckButton, "export deck", tapX, tapY)) {
      exportDeckTextToSd();
      renderAdvanced("deck export");
    } else if (hitTarget(gBadgeSleepButton, "sleep mode", tapX, tapY)) {
      cycleBadgeSleepMode();
      saveSettings();
      renderAdvanced("sleep mode switch");
    } else if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (gTouchDebugEnabled) {
      markHitTarget("touch debug canvas", tapX, tapY);
      renderAdvanced();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::Debug) {
    if (hitTarget(gHelpButton, "help legend", tapX, tapY)) {
      renderHelpLegend();
    } else if (hitTarget(gPowerLabButton, "power lab", tapX, tapY)) {
      gPowerLabPage = 0;
      renderPowerLab();
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

  if (gScreen == Screen::PowerLab) {
    constexpr uint8_t kPlPageCount = 4;
    if (hitTarget(gPowerProfileButton, "power lab profile cycle", tapX, tapY)) {
      cyclePowerProfile();
      saveSettings();
      renderPowerLab("profile switch");
    } else if (hitTarget(gFilterButton, "power lab page", tapX, tapY)) {
      gPowerLabPage = static_cast<uint8_t>((gPowerLabPage + 1) % kPlPageCount);
      renderPowerLab("page");
    } else if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (gPowerLabPage == 2 && hitTarget(gBadgeSleepButton, "sleep mode cycle", tapX, tapY)) {
      cycleBadgeSleepMode();
      saveSettings();
      renderPowerLab("sleep mode switch");
    } else {
      // Body tap: re-render current page to capture fresh state
      renderPowerLab("tap refresh");
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::PowerAudit) {
    constexpr uint8_t kPaPageCount = 4;
    if (hitTarget(gPowerModeButton, "power mode", tapX, tapY)) {
      cyclePowerMode();
      saveSettings();
      applyPowerPolicy("power mode switch");
      renderPowerAudit("power mode switch");
    } else if (hitTarget(gBadgeSleepButton, "badge sleep", tapX, tapY)) {
      cycleBadgeSleepMode();
      saveSettings();
      applyPowerPolicy("badge sleep switch");
      renderPowerAudit("badge sleep switch");
    } else if (hitTarget(gPowerProfileButton, "power profile", tapX, tapY)) {
      cyclePowerProfile();
      saveSettings();
      renderPowerAudit("profile switch");
    } else if (hitTarget(gFilterButton, "power audit prev page", tapX, tapY)) {
      gPowerAuditPage = static_cast<uint8_t>((gPowerAuditPage + kPaPageCount - 1) % kPaPageCount);
      renderPowerAudit("prev page");
    } else if (hitTarget(gNextButton, "power audit next page", tapX, tapY)) {
      gPowerAuditPage = static_cast<uint8_t>((gPowerAuditPage + 1) % kPaPageCount);
      renderPowerAudit("next page");
    } else if (hitTarget(gHomeButton, "home", tapX, tapY)) {
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

  if (gScreen == Screen::JapaneseMenu) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gJapaneseDailyButton, "japanese daily questions", tapX, tapY)) {
      // Daily Questions now enters source/week/day navigation scaffold
      renderJapaneseSourceSelect("japanese entry");
    } else if (hitTarget(gJapaneseMockTestButton, "japanese mock test", tapX, tapY)) {
      renderPlaceholderScreen(Screen::JapaneseMockTest, "Mock Test",
                              "Mock Test is not available yet in this build. Use Daily Questions "
                              "for now.", "japanese entry");
    } else if (hitTarget(gJapaneseReferenceButton, "japanese reference", tapX, tapY)) {
      renderJapaneseReference("japanese entry");
    } else if (hitTarget(gJapaneseResultsButton, "japanese results", tapX, tapY)) {
      renderJapaneseResults("japanese entry");
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::JapaneseSourceSelect) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gJapaneseSourceN3Button, "japanese source n3", tapX, tapY)) {
      gJapaneseNavSource = 0;
      renderJapaneseWeekSelect("japanese week select");
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::JapaneseWeekSelect) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gJapaneseWeek1Button, "japanese week 1", tapX, tapY)) {
      gJapaneseNavWeek = 1;
      renderJapaneseDaySelect("japanese day select");
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::JapaneseDaySelect) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    } else if (hitTarget(gJapaneseDay1Button, "japanese day 1", tapX, tapY)) {
      gJapaneseNavDay = 1;
      gJapaneseQuestionIndex = 0;
      gJapaneseSelectedOption = -1;
      gJapaneseShowFeedback = false;
      renderJapaneseDaily("japanese daily entry");
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::JapaneseDaily) {
    if (!gJapaneseShowFeedback) {
      const JapaneseItem& item = kJapaneseDayItems[gJapaneseQuestionIndex];
      // Prev: navigate without recording answer
      if (hitTarget(gJapanesePrevButton, "japanese prev question", tapX, tapY)) {
        if (gJapaneseQuestionIndex > 0) {
          --gJapaneseQuestionIndex;
          gJapaneseSelectedOption = -1;
          gJapaneseShowFeedback = false;
          renderJapaneseDaily("prev question");
        }
        noteIgnoredIfNoHit(tapX, tapY);
        return;
      }
      if (hitTarget(gHomeButton, "home", tapX, tapY)) {
        Serial.println("entering Home");
        renderHome();
        return;
      }
      // Answer options
      for (uint8_t i = 0; i < 4; ++i) {
        char target[24];
        snprintf(target, sizeof(target), "japanese option %u", static_cast<unsigned>(i));
        if (hitTarget(gJapaneseOptionButtons[i], target, tapX, tapY)) {
          gJapaneseSelectedOption = static_cast<int8_t>(i);
          gJapaneseShowFeedback = true;
          recordJapaneseAnswer(item, i);
          renderJapaneseDaily("japanese feedback");
          return;
        }
      }
    } else {
      if (hitTarget(gJapanesePrevButton, "japanese prev question", tapX, tapY)) {
        if (gJapaneseQuestionIndex > 0) {
          --gJapaneseQuestionIndex;
          gJapaneseSelectedOption = -1;
          gJapaneseShowFeedback = false;
          renderJapaneseDaily("prev question");
        }
      } else if (hitTarget(gJapaneseNextButton, "japanese next question", tapX, tapY)) {
        if (gJapaneseQuestionIndex + 1 < kJapaneseDayItemCount) {
          ++gJapaneseQuestionIndex;
          gJapaneseSelectedOption = -1;
          gJapaneseShowFeedback = false;
          renderJapaneseDaily("next question");
        }
      } else if (hitTarget(gHomeButton, "home", tapX, tapY)) {
        Serial.println("entering Home");
        renderHome();
      }
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::JapaneseResults) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::JapaneseReference) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::JapaneseMockTest) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
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
      return;
    }
    const uint8_t pageCount = currentResultsPageCount();
    if (pageCount > 1 && hitTarget(gFilterButton, "results previous page", tapX, tapY)) {
      gResultsPage = static_cast<uint8_t>((gResultsPage + pageCount - 1) % pageCount);
      renderResultsScreen("results previous page");
      return;
    }
    if (pageCount > 1 && hitTarget(gNextButton, "results next page", tapX, tapY)) {
      gResultsPage = static_cast<uint8_t>((gResultsPage + 1) % pageCount);
      renderResultsScreen("results next page");
      return;
    }
    if (pageCount > 1 && gReaderContentRect.contains(tapX, tapY)) {
      if (tapY < gReaderContentRect.y + gReaderContentRect.h / 2) {
        markHitTarget("results previous page", tapX, tapY);
        gResultsPage = static_cast<uint8_t>((gResultsPage + pageCount - 1) % pageCount);
        renderResultsScreen("results previous page");
      } else {
        markHitTarget("results next page", tapX, tapY);
        gResultsPage = static_cast<uint8_t>((gResultsPage + 1) % pageCount);
        renderResultsScreen("results next page");
      }
      return;
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
          gDrillShowFeedback = true;  // immediately show feedback page on first tap
          gCoachStage = 0;
          gDrillLastResultPage = 0;   // no result page viewed yet
          gCoachNeedsCleanEntryRefresh = true;
          Serial.printf("Drill answer selected: item=%s selected=%c best=%c -> feedback cleanRefresh=queued\n",
                        coachDisplayId(item), static_cast<char>('A' + option),
                        static_cast<char>('A' + item.correctIndex));
          renderCoachScreen();
          break;
        }
      }
      if (gHitMatchedThisTap) {
        return;
      }
    }

    if (gReaderContentRect.contains(tapX, tapY)) {
      // Drill post-answer state machine (gSelectedOption >= 0):
      //
      // Two views toggle via gDrillShowFeedback:
      //   Result view  (!gDrillShowFeedback): question + options with selected/correct borders.
      //   Feedback view (gDrillShowFeedback): explanation/feedback pages.
      //
      // Result view taps:
      //   Top-half    → prev result page if gCoachStage > 0; otherwise no-op.
      //   Bottom-half → next result page if one exists; otherwise enter feedback at page 0.
      //
      // Feedback view taps:
      //   Top-half    → prev feedback page if gCoachStage > 0; otherwise return to result view
      //                 at the last result page the user reached (gDrillLastResultPage).
      //   Bottom-half → next feedback page, or next drill item on last page.
      //
      // Footer ← → always navigate between drill items from any state.
      if (isOptionDrillScreen(gScreen, item) && gSelectedOption >= 0) {
        if (!gDrillShowFeedback) {
          // Result view: compute page count using the same plan as the draw path.
          const PracticeLayout rl = practiceLayoutFor(canonicalFontSizeMode(gSettings.fontSizeMode));
          applyCoachButtonFont();
          const DrillPagePlan rplan = buildDrillPagePlan(item, rl);
          const uint8_t resultPages = rplan.totalPages;
          if (tapY >= gReaderContentRect.y + gReaderContentRect.h / 2) {
            if (gCoachStage + 1 < resultPages) {
              // Bottom-half: advance to next result page.
              markHitTarget("drill result next page", tapX, tapY);
              ++gCoachStage;
              renderCoachScreen();
            } else {
              // Bottom-half on last result page: enter feedback at page 0.
              markHitTarget("drill result -> feedback", tapX, tapY);
              gDrillLastResultPage = gCoachStage;
              gDrillShowFeedback = true;
              gCoachStage = 0;
              gCoachNeedsCleanEntryRefresh = true;
              renderCoachScreen();
            }
          } else if (gCoachStage > 0) {
            // Top-half on a later result page: go to previous result page.
            markHitTarget("drill result prev page", tapX, tapY);
            --gCoachStage;
            renderCoachScreen();
          } else {
            // Top-half on first result page: no-op (footer ← navigates to previous item).
            Serial.println("drill result: top-half at page 0, no action");
          }
        } else {
          // Feedback view.
          if (tapY < gReaderContentRect.y + gReaderContentRect.h / 2) {
            // Top-half: go to previous feedback page, or return to result view.
            markHitTarget("drill feedback nav up", tapX, tapY);
            if (gCoachStage > 0) {
              --gCoachStage;
              renderCoachScreen();
            } else {
              // Return to result view at the last page the user had viewed.
              gDrillShowFeedback = false;
              gCoachStage = gDrillLastResultPage;
              gCoachNeedsCleanEntryRefresh = true;
              renderCoachScreen();
            }
          } else {
            // Bottom-half: advance feedback page, or next item on last feedback page.
            const uint8_t feedbackPages = currentCoachReaderPageCount();
            if (gCoachStage + 1 < feedbackPages) {
              markHitTarget("drill feedback next page", tapX, tapY);
              ++gCoachStage;
              renderCoachScreen();
            } else if (hasNextCoachItem()) {
              markHitTarget("drill feedback -> next item", tapX, tapY);
              nextCoachItem();
              renderCoachScreen();
            } else {
              Serial.println("drill feedback: no next item");
            }
          }
        }
        return;
      }

      const uint8_t pageCount = currentCoachReaderPageCount();
      // Only allow item-advance navigation on Glossary (not Drills/Exam option screens)
      const bool allowItemNav = (gScreen == Screen::Glossary);
      if (tapY < gReaderContentRect.y + gReaderContentRect.h / 2) {
        if (gCoachStage > 0) {
          markHitTarget("coach previous page", tapX, tapY);
          --gCoachStage;
          renderCoachScreen();
        } else if (allowItemNav && hasPreviousCoachItem()) {
          markHitTarget("coach previous item", tapX, tapY);
          previousCoachItem();
          renderCoachScreen();
        } else {
          Serial.println("coach previous page/item disabled");
        }
      } else {
        if (gCoachStage + 1 < pageCount) {
          markHitTarget("coach next page", tapX, tapY);
          ++gCoachStage;
          renderCoachScreen();
        } else if (allowItemNav && hasNextCoachItem()) {
          markHitTarget("coach next item", tapX, tapY);
          nextCoachItem();
          renderCoachScreen();
        } else {
          Serial.println("coach next page/item disabled");
        }
      }
      return;
    }

    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::Settings) {
    // Reader size — S / M / L
    if (hitTarget(gFontMediumButton, "reader S", tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::Medium;
      saveSettings();
      renderSettings();
    } else if (hitTarget(gFontLargeButton, "reader M", tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::Large;
      saveSettings();
      renderSettings();
    } else if (hitTarget(gFontXlButton, "reader L", tapX, tapY)) {
      gSettings.fontSizeMode = FontSizeMode::XL;
      saveSettings();
      renderSettings();
    // Refresh — Fast / Balanced / Clean
    } else if (hitTarget(gRefreshFastButton, "refresh fast", tapX, tapY)) {
      gSettings.refreshMode = RefreshMode::Fast;
      saveSettings();
      renderSettings("refresh fast");
    } else if (hitTarget(gRefreshBalancedButton, "refresh balanced", tapX, tapY)) {
      gSettings.refreshMode = RefreshMode::Balanced;
      saveSettings();
      renderSettings("refresh balanced");
    } else if (hitTarget(gRefreshCleanButton, "refresh clean", tapX, tapY)) {
      gSettings.refreshMode = RefreshMode::Clean;
      saveSettings();
      renderSettings("refresh clean");
    // Power — Responsive / Balanced / Max Battery
    } else if (hitTarget(gPowerResponsiveButton, "power responsive", tapX, tapY)) {
      gPowerProfile = PowerProfile::Balanced;
      saveSettings();
      renderSettings("power responsive");
    } else if (hitTarget(gPowerBalancedButton, "power balanced", tapX, tapY)) {
      gPowerProfile = PowerProfile::Aggressive;
      saveSettings();
      renderSettings("power balanced");
    } else if (hitTarget(gPowerMaxButton, "power max battery", tapX, tapY)) {
      gPowerProfile = PowerProfile::BadgeMax;
      saveSettings();
      renderSettings("power max battery");
    // Badge orientation — Normal / Strap
    } else if (hitTarget(gOrientationButton, "orientation normal", tapX, tapY)) {
      gSettings.orientationMode = OrientationMode::Handheld;
      saveSettings();
      renderSettings("orientation normal");
    } else if (hitTarget(gOrientationStrapButton, "orientation strap", tapX, tapY)) {
      gSettings.orientationMode = OrientationMode::Strap;
      saveSettings();
      renderSettings("orientation strap");
    // Advanced / Home
    } else if (hitTarget(gAdvancedButton, "advanced", tapX, tapY)) {
      renderAdvanced();
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
    } else if (hitTarget(gInterviewButton, "interview", tapX, tapY)) {
      renderInterviewMenu();
    } else if (hitTarget(gJapaneseButton, "japanese", tapX, tapY)) {
      renderJapaneseMenu();
    } else if (hitTarget(gSettingsButton, "settings", tapX, tapY)) {
      renderSettings();
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
  }

  if (gScreen == Screen::InterviewMenu) {
    if (hitTarget(gHomeButton, "home", tapX, tapY)) {
      Serial.println("entering Home");
      renderHome();
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
    }
    noteIgnoredIfNoHit(tapX, tapY);
    return;
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
  Serial.println("Wake sources: timer=supported uart=supported(auto) touch-INT=unverified gpio-btn=not-configured deep=blocked");
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
