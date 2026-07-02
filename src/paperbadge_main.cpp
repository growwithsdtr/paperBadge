#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <cerrno>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {
#include <epdiy.h>
#include "local/miniz.h"
}

#include "comic/cbz_book.hpp"
#include "comic/image_display.hpp"
#include "comic/page_loader.hpp"
#include "font/builtin_ui_font.h"
#include "font/font_lab_assets.h"
#include "font/text_render.hpp"
#include "font/utf8.hpp"
#include "font/xteink_font.hpp"
#include "hal/battery.hpp"
#include "hal/display.hpp"
#include "hal/imu.hpp"
#include "hal/sd.hpp"
#include "hal/touch.hpp"
#include "library/book_db.hpp"
#include "library/library.hpp"
#include "system/settings.hpp"
#include "embedded_badge.h"
#include "embedded_interview_deck.h"

namespace {

constexpr const char* TAG = "paperbadge";
constexpr const char* kMangaRoot = "/sdcard/paperBadge/content/manga";
constexpr const char* kBooksRoot = "/sdcard/paperBadge/content/books";
constexpr const char* kJapaneseRoot = "/sdcard/paperBadge/content/japanese";
constexpr const char* kStateRoot = "/sdcard/paperBadge/state";
constexpr const char* kLogsRoot = "/sdcard/paperBadge/logs";
constexpr const char* kAssetsRoot = "/sdcard/paperBadge/assets";
constexpr const char* kMangaDbPath = "/sdcard/paperBadge/state/manga.tsv";
constexpr const char* kReaderStatePath = "/sdcard/paperBadge/state/reader.tsv";
constexpr const char* kSessionPath = "/sdcard/paperBadge/state/session.kv";
constexpr int kToolbarH = 60;
constexpr int kFooterH = 64;

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

enum class Screen {
    Home,
    Badge,
    Interview,
    InterviewPracticeMenu,
    InterviewCardList,
    InterviewPractice,
    InterviewDrillQ,
    InterviewDrillFB,
    InterviewGlossaryList,
    InterviewGlossary,
    InterviewResults,
    JapaneseMenu,
    JapaneseSource,
    JapaneseUnit,
    JapaneseLesson,
    Japanese,
    JapaneseFeedback,
    JapaneseMock,
    JapaneseMockResults,
    JapaneseReference,
    JapaneseResults,
    JapaneseFont,
    MangaLibrary,
    MangaReading,
    MangaError,
    ReaderLibrary,
    ReaderReading,
    ReaderError,
    Settings,
};

enum class JapaneseFontFace : uint8_t {
    IpaCurrent = 0,
    BizUdGothic = 1,
};

enum class InterviewPracticeMode : uint8_t {
    All,
    MustMaster,
    Category,
};

enum class MangaFitMode : uint8_t {
    FitPage,
    FitWidth,
    FitHeight,
};

struct BookFile {
    std::string path;
    std::string name;
};

struct JapaneseItem {
    const char* prompt;
    const char* choices[4];
    int correct;
    const char* answer;
    const char* explanation;
    const char* english;
};

constexpr JapaneseItem kJapaneseItems[] = {
    {
        "「郵便局」の読み方として正しいものはどれですか。",
        {"ゆうびんきょく", "ゆうべんきょく", "ゆびんきょく", "ゆうびんきょうく"},
        0,
        "郵便局へ荷物を取りに行きました。",
        "「郵便局」は「ゆうびんきょく」と読みます。手紙や荷物を送る場所です。",
        "Post office is read yuubinkyoku.",
    },
    {
        "（　）に入る正しい言葉を選びなさい。「子供のころ、よくこの公園で遊んだ（　）。」",
        {"ものだ", "ことだ", "はずだ", "べきだ"},
        0,
        "子供のころ、よくこの公園で遊んだものだ。",
        "「〜ものだ」は昔の習慣や思い出を懐かしんで言う時に使います。",
        "~monoda is used to reminisce about past habits.",
    },
    {
        "「荷物」の読み方として正しいものはどれですか。",
        {"にもつ", "かもつ", "にぶつ", "かぶつ"},
        0,
        "引っ越したので、荷物がたくさんあります。",
        "「荷物」は「にもつ」と読みます。旅行や引っ越しで持つ物のことです。",
        "Baggage/luggage is read nimotsu.",
    },
};

// ── Font / display globals ────────────────────────────────────────────
ps3::font::XTEinkFont g_biz_font(24, 24);
ps3::font::XTEinkFont g_ipa_font(24, 24);
ps3::font::XTEinkFont g_font_lab_font(24, 24);
ps3::font::XTEinkFont* g_font = &g_biz_font;
Screen g_screen = Screen::Home;
JapaneseFontFace g_jp_font = JapaneseFontFace::BizUdGothic;
int64_t g_last_activity_us = 0;
int g_sleep_minutes = 5;
int g_power_off_minutes = 20;
int g_pages_since_full = 0;
Screen g_nav_stack[8];
int g_nav_depth = 0;

// ── Hit-test rects ────────────────────────────────────────────────────
Rect g_home_buttons[6];
Rect g_footer_left;
Rect g_footer_mid;
Rect g_footer_right;
Rect g_list_rows[10];
Rect g_jp_choices[4];
Rect g_jp_menu_buttons[4];
Rect g_settings_buttons[6];
Rect g_badge_qr_rect;
Rect g_manga_overlay_buttons[6];

// Interview menu / sub-screen rects
Rect g_iv_menu_buttons[5];   // Practice, Drills, Exam, Glossary, Results
Rect g_iv_practice_buttons[4];
Rect g_iv_choices[4];        // drill MCQ options

// ── Manga globals ─────────────────────────────────────────────────────
ps3::library::Library g_manga_library;
ps3::library::BookDb g_manga_db;
ps3::library::BookRecord* g_manga_record = nullptr;
ps3::comic::CbzBook g_manga_book;
bool g_manga_open = false;
char g_manga_path[ps3::library::MAX_PATH_LEN] = {};
int g_manga_page = 0;
std::string g_manga_error_msg;
Screen g_manga_error_return = Screen::MangaLibrary;
MangaFitMode g_manga_fit_mode = MangaFitMode::FitPage;
bool g_manga_landscape = false;
int g_manga_slice = 0;
int g_manga_slice_count = 1;
bool g_manga_pending_last_slice = false;
bool g_manga_overlay_visible = false;
int g_manga_hint_pages_remaining = 0;

// ── Badge globals ─────────────────────────────────────────────────────
bool g_badge_japanese = false;
bool g_badge_qr_zoom = false;

// ── Reader globals ────────────────────────────────────────────────────
std::vector<BookFile> g_reader_books;
std::vector<std::string> g_reader_lines;
std::string g_reader_path;
std::string g_reader_title;
int g_reader_page = 0;
int g_reader_lines_per_page = 24;
std::string g_reader_error_msg;
Screen g_reader_error_return = Screen::ReaderLibrary;
int g_reader_font_level = 1;  // 0=S, 1=M, 2=L, 3=XL line density
int g_interview_font_level = 1;
int g_japanese_font_level = 1;
int g_font_lab_page = 0;
int g_settings_page = 0;  // 0 main, 1 fonts, 2 reader, 3 manga, 4 power, 5 refresh

// ── Japanese globals ──────────────────────────────────────────────────
int g_jp_index = 0;
int g_jp_selected = -1;
int g_jp_feedback_page = 0;
bool g_jp_feedback_single = true;
int g_jp_source_idx = 0;
int g_jp_unit_idx = 0;
int g_jp_lesson_idx = 0;
int g_jp_mock_index = 0;
int g_jp_mock_selected = -1;
int g_jp_mock_answers[sizeof(kJapaneseItems) / sizeof(kJapaneseItems[0])] = {};

// ── Interview state ───────────────────────────────────────────────────
int g_iv_card_idx = 0;
bool g_iv_card_spoken = false;   // false=title view, true=spoken answer view
int g_iv_answer_page = 0;
InterviewPracticeMode g_iv_practice_mode = InterviewPracticeMode::All;
int g_iv_practice_section_idx = -1;
int g_iv_card_list_page = 0;

int g_iv_drill_idx = 0;          // index into kDrills[] (MCQ only)
int g_iv_drill_answer = -1;
int g_iv_session_correct = 0;
int g_iv_session_total = 0;
int g_iv_session_practice = 0;

constexpr int kExamSize = 10;
int g_iv_exam_pool[kExamSize];
int g_iv_exam_count = 0;
int g_iv_exam_current = 0;
bool g_iv_exam_answers[kExamSize];
int g_iv_exam_score = 0;
bool g_iv_in_exam = false;

int g_iv_gloss_idx = 0;
int g_iv_gloss_category_idx = -1;
int g_iv_gloss_list_page = 0;

// ── Helpers ───────────────────────────────────────────────────────────

std::string basename_of(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool has_suffix_icase(const std::string& s, const char* suffix) {
    const size_t ns = s.size();
    const size_t nx = std::strlen(suffix);
    if (nx > ns) return false;
    for (size_t i = 0; i < nx; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[ns - nx + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

bool archive_has_zip64_markers(const char* path) {
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return false;
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return false;
    }
    const long size = std::ftell(fp);
    if (size <= 0) {
        std::fclose(fp);
        return false;
    }
    const long window = std::min<long>(size, 128 * 1024);
    if (std::fseek(fp, size - window, SEEK_SET) != 0) {
        std::fclose(fp);
        return false;
    }
    std::vector<uint8_t> tail(static_cast<size_t>(window));
    const size_t n = std::fread(tail.data(), 1, tail.size(), fp);
    std::fclose(fp);
    for (size_t i = 0; i + 3 < n; ++i) {
        const uint32_t sig = static_cast<uint32_t>(tail[i]) |
                             (static_cast<uint32_t>(tail[i + 1]) << 8) |
                             (static_cast<uint32_t>(tail[i + 2]) << 16) |
                             (static_cast<uint32_t>(tail[i + 3]) << 24);
        if (sig == 0x06064B50u || sig == 0x07064B50u) return true;
    }
    return false;
}

void mkdir_if_missing(const char* path) {
    mkdir(path, 0755);
}

void ensure_dirs() {
    mkdir_if_missing("/sdcard/paperBadge");
    mkdir_if_missing("/sdcard/paperBadge/content");
    mkdir_if_missing(kMangaRoot);
    mkdir_if_missing(kBooksRoot);
    mkdir_if_missing(kJapaneseRoot);
    mkdir_if_missing(kStateRoot);
    mkdir_if_missing(kLogsRoot);
    mkdir_if_missing(kAssetsRoot);
}

uint8_t gray_byte(uint8_t gray4) {
    return static_cast<uint8_t>((gray4 << 4) | (gray4 & 0x0F));
}

void fill_rect(const Rect& r, uint8_t gray4) {
    if (r.w <= 0 || r.h <= 0) return;
    EpdRect er{r.x, r.y, r.w, r.h};
    epd_fill_rect(er, gray_byte(gray4), ps3::display::framebuffer());
}

void draw_rect(const Rect& r, uint8_t gray4 = 0) {
    if (r.w <= 0 || r.h <= 0) return;
    EpdRect er{r.x, r.y, r.w, r.h};
    epd_draw_rect(er, gray_byte(gray4), ps3::display::framebuffer());
}

void draw_hline(int x, int y, int w, uint8_t gray4 = 0) {
    epd_draw_hline(x, y, w, gray_byte(gray4), ps3::display::framebuffer());
}

ps3::font::XTEinkFont& active_font() {
    return *g_font;
}

int text_width(const std::string& s) {
    return ps3::font::text_width(s.c_str(), active_font());
}

void draw_text(int x, int y, const std::string& s, uint8_t fg = 0, uint8_t bg = 15) {
    ps3::font::draw_text(x, y, s.c_str(), active_font(), fg, bg);
}

void draw_text_font(int x, int y, const std::string& s, ps3::font::XTEinkFont& font,
                    uint8_t fg = 0, uint8_t bg = 15) {
    ps3::font::draw_text(x, y, s.c_str(), font, fg, bg);
}

int draw_wrapped(int x, int y, int max_w, const std::string& text, int max_lines);

bool bind_font_lab_face(int idx) {
    if (idx < 0 || idx >= ps3::font::kFontLabFaceCount) return false;
    return g_font_lab_font.bind_sparse(ps3::font::kFontLabCodepoints,
                                       ps3::font::kFontLabGlyphCount,
                                       ps3::font::kFontLabFaces[idx].glyphs);
}

int font_lab_page_count() {
    return 3 + ps3::font::kFontLabFaceCount;
}

bool font_lab_face_supports_japanese(const ps3::font::FontLabFace& face) {
    return std::strstr(face.key, "Inter-") == nullptr &&
           std::strstr(face.key, "SourceSerif") == nullptr;
}

std::string font_lab_face_status(const ps3::font::FontLabFace& face) {
    std::string status = std::string(face.display_name) + " " + face.weight;
    status += font_lab_face_supports_japanese(face) ? " - JP subset" : " - Latin only";
    return status;
}

int draw_font_lab_sample_line(int x, int y, const std::string& s,
                              ps3::font::XTEinkFont& font,
                              int scale,
                              int tracking,
                              uint8_t fg = 0,
                              uint8_t bg = 15) {
    if (scale < 1) scale = 1;
    if (tracking < 0) tracking = 0;
    const int start_x = x;
    const int full_w = font.width();
    uint32_t cp = 0;
    const char* p = s.c_str();
    while ((p = ps3::font::utf8_next(p, cp)) != nullptr) {
        if (cp == 0) break;
        if (cp == '\n') {
            x = start_x;
            y += (font.height() + tracking) * scale;
            continue;
        }
        for (int gy = 0; gy < font.height(); ++gy) {
            for (int gx = 0; gx < full_w; ++gx) {
                const uint8_t color = font.pixel(cp, gx, gy) ? fg : bg;
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        ps3::display::put_pixel(x + gx * scale + sx,
                                                y + gy * scale + sy,
                                                color);
                    }
                }
            }
        }
        x += full_w * scale + tracking;
    }
    return y + font.height() * scale;
}

int draw_font_lab_sample_box(int x, int y, int w, const std::string& label,
                             const std::string& sample,
                             int scale,
                             int tracking) {
    const int pad = scale == 1 ? 10 : 14;
    const int sample_h = g_font_lab_font.height() * std::max(1, scale);
    const int box_h = sample_h + pad * 2;
    draw_text(x, y, label);
    y += active_font().height() + 8;
    fill_rect({x, y, w, box_h}, 15);
    draw_rect({x, y, w, box_h}, 4);
    draw_font_lab_sample_line(x + pad, y + pad, sample, g_font_lab_font,
                              scale, tracking);
    return y + box_h + 20;
}

int draw_text_font_scaled(int x, int y, const std::string& s,
                          ps3::font::XTEinkFont& font,
                          int scale,
                          uint8_t fg = 0, uint8_t bg = 15) {
    if (scale < 1) scale = 1;
    const int start_x = x;
    const int full_w = font.width();
    uint32_t cp = 0;
    const char* p = s.c_str();
    while ((p = ps3::font::utf8_next(p, cp)) != nullptr) {
        if (cp == 0) break;
        if (cp == '\n') {
            x = start_x;
            y += font.height() * scale;
            continue;
        }
        const int adv = ps3::font::char_advance(cp, full_w);
        for (int gy = 0; gy < font.height(); ++gy) {
            for (int gx = 0; gx < adv; ++gx) {
                const uint8_t color = font.pixel(cp, gx, gy) ? fg : bg;
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        ps3::display::put_pixel(x + gx * scale + sx,
                                                y + gy * scale + sy,
                                                color);
                    }
                }
            }
        }
        x += adv * scale;
    }
    return y + font.height() * scale;
}

int draw_font_lab_candidate_row(int face_idx, int x, int y, int w,
                                const char* sample) {
    if (!bind_font_lab_face(face_idx)) return y;
    const auto& face = ps3::font::kFontLabFaces[face_idx];
    y = draw_wrapped(x, y, w, font_lab_face_status(face), 2) + 2;
    return draw_font_lab_sample_box(x + 10, y, w - 10, "24 px native sparse preview",
                                    sample, 1, 4);
}

std::vector<std::string> wrap_text(const std::string& text, int max_w, int max_lines = 0) {
    std::vector<std::string> lines;
    std::string line;
    const char* p = text.c_str();
    while (p && *p) {
        uint32_t cp = 0;
        const char* next = ps3::font::utf8_next(p, cp);
        if (!next) break;
        if (cp == '\r') {
            p = next;
            continue;
        }
        if (cp == '\n') {
            lines.push_back(line);
            line.clear();
            p = next;
            if (max_lines > 0 && static_cast<int>(lines.size()) >= max_lines) return lines;
            continue;
        }
        const std::string chunk(p, static_cast<size_t>(next - p));
        const std::string candidate = line + chunk;
        if (!line.empty() && text_width(candidate) > max_w) {
            lines.push_back(line);
            line = chunk;
            if (max_lines > 0 && static_cast<int>(lines.size()) >= max_lines) return lines;
        } else {
            line = candidate;
        }
        p = next;
    }
    if (!line.empty() && (max_lines <= 0 || static_cast<int>(lines.size()) < max_lines)) {
        lines.push_back(line);
    }
    return lines;
}

int draw_wrapped(int x, int y, int max_w, const std::string& text, int max_lines = 0) {
    const auto lines = wrap_text(text, max_w, max_lines);
    for (const auto& line : lines) {
        draw_text(x, y, line);
        y += active_font().height() + 8;
    }
    return y;
}

int draw_wrapped_gap(int x, int y, int max_w, const std::string& text,
                     int max_lines, int gap) {
    const auto lines = wrap_text(text, max_w, max_lines);
    for (const auto& line : lines) {
        draw_text(x, y, line);
        y += active_font().height() + gap;
    }
    return y;
}

void draw_button(const Rect& r, const std::string& label, bool selected = false) {
    fill_rect(r, 15);
    if (selected) {
        fill_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, 13);
    }
    draw_rect(r, 0);
    const int tw = text_width(label);
    const int tx = r.x + std::max(8, (r.w - tw) / 2);
    const int ty = r.y + (r.h - active_font().height()) / 2;
    draw_text(tx, ty, label);
}

void draw_header(const std::string& title, const std::string& right = "") {
    fill_rect({0, 0, ps3::display::width(), kToolbarH}, 15);
    draw_text(18, 18, title);
    if (!right.empty()) {
        draw_text(ps3::display::width() - text_width(right) - 18, 18, right);
    }
    draw_hline(0, kToolbarH - 1, ps3::display::width());
}

void draw_footer(const char* left, const char* mid, const char* right) {
    const int y = ps3::display::height() - kFooterH;
    const int w = ps3::display::width();
    fill_rect({0, y, w, kFooterH}, 15);
    draw_hline(0, y, w);
    const int bw = (w - 48) / 3;
    const Rect left_rect{12, y + 8, bw, kFooterH - 16};
    const Rect mid_rect{24 + bw, y + 8, bw, kFooterH - 16};
    const Rect right_rect{36 + bw * 2, y + 8, bw, kFooterH - 16};
    g_footer_left = left ? left_rect : Rect{};
    g_footer_mid = mid ? mid_rect : Rect{};
    g_footer_right = right ? right_rect : Rect{};
    if (left) draw_button(g_footer_left, left);
    if (mid) draw_button(g_footer_mid, mid);
    if (right) draw_button(g_footer_right, right);
}

std::vector<uint8_t> read_file_bytes(const char* path, size_t max_bytes = 3 * 1024 * 1024) {
    std::vector<uint8_t> out;
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return out;
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0 || static_cast<size_t>(size) > max_bytes) {
        std::fclose(fp);
        return out;
    }
    out.resize(static_cast<size_t>(size));
    const size_t n = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    if (n != out.size()) out.clear();
    return out;
}

std::string read_text_file(const char* path, size_t max_bytes = 512 * 1024) {
    const auto bytes = read_file_bytes(path, max_bytes);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp == 0) return;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool starts_with_at(const char* data, size_t size, size_t pos, const char* needle) {
    const size_t n = std::strlen(needle);
    return pos + n <= size && std::memcmp(data + pos, needle, n) == 0;
}

bool decode_numeric_entity(const char* data, size_t size, size_t pos,
                           size_t* consumed, uint32_t* cp_out) {
    if (!starts_with_at(data, size, pos, "&#")) return false;
    size_t p = pos + 2;
    int base = 10;
    if (p < size && (data[p] == 'x' || data[p] == 'X')) {
        base = 16;
        ++p;
    }
    if (p >= size) return false;
    uint32_t value = 0;
    int digits = 0;
    for (; p < size && digits < 8; ++p) {
        const unsigned char ch = static_cast<unsigned char>(data[p]);
        int digit = -1;
        if (base == 10 && std::isdigit(ch)) {
            digit = ch - '0';
        } else if (base == 16 && std::isxdigit(ch)) {
            digit = std::isdigit(ch) ? ch - '0' : (std::tolower(ch) - 'a' + 10);
        } else {
            break;
        }
        value = value * static_cast<uint32_t>(base) + static_cast<uint32_t>(digit);
        ++digits;
    }
    if (digits == 0 || p >= size || data[p] != ';') return false;
    if (value > 0x10FFFF) return false;
    *consumed = p - pos + 1;
    *cp_out = value;
    return true;
}

std::string strip_html(const char* data, size_t size) {
    std::string out;
    out.reserve(size / 2);
    bool in_tag = false;
    bool last_space = false;
    for (size_t i = 0; i < size; ++i) {
        const char c = data[i];
        if (c == '<') {
            in_tag = true;
            if (!last_space) {
                out.push_back(' ');
                last_space = true;
            }
            continue;
        }
        if (c == '>') {
            in_tag = false;
            continue;
        }
        if (in_tag) continue;
        if (c == '&') {
            size_t consumed = 0;
            uint32_t cp = 0;
            if (starts_with_at(data, size, i, "&nbsp;")) {
                out.push_back(' ');
                i += 5;
            } else if (starts_with_at(data, size, i, "&amp;")) {
                out.push_back('&');
                i += 4;
            } else if (starts_with_at(data, size, i, "&lt;")) {
                out.push_back('<');
                i += 3;
            } else if (starts_with_at(data, size, i, "&gt;")) {
                out.push_back('>');
                i += 3;
            } else if (starts_with_at(data, size, i, "&quot;")) {
                out.push_back('"');
                i += 5;
            } else if (decode_numeric_entity(data, size, i, &consumed, &cp)) {
                append_utf8(out, cp);
                i += consumed - 1;
            } else {
                out.push_back('&');
            }
            last_space = false;
            continue;
        }
        const bool ws = c == '\t' || c == '\n' || c == '\r' || c == ' ';
        if (ws) {
            if (!last_space) out.push_back(' ');
            last_space = true;
        } else {
            out.push_back(c);
            last_space = false;
        }
    }
    return out;
}

struct EpubReadResult {
    bool ok = false;
    std::string text;
    std::string error;
    bool truncated = false;
};

std::string xml_attr_unescape(const std::string& value) {
    return strip_html(value.c_str(), value.size());
}

bool xml_attr_value(const std::string& xml, size_t tag_pos,
                    const char* attr, std::string* out) {
    const size_t tag_end = xml.find('>', tag_pos);
    if (tag_end == std::string::npos) return false;
    const size_t attr_len = std::strlen(attr);
    size_t p = tag_pos;
    while ((p = xml.find(attr, p)) != std::string::npos && p < tag_end) {
        const bool left_ok = p == 0 || std::isspace(static_cast<unsigned char>(xml[p - 1])) || xml[p - 1] == '<';
        if (!left_ok) {
            p += attr_len;
            continue;
        }
        size_t q = p + attr_len;
        while (q < tag_end && std::isspace(static_cast<unsigned char>(xml[q]))) ++q;
        if (q >= tag_end || xml[q] != '=') {
            p += attr_len;
            continue;
        }
        ++q;
        while (q < tag_end && std::isspace(static_cast<unsigned char>(xml[q]))) ++q;
        if (q >= tag_end || (xml[q] != '"' && xml[q] != '\'')) return false;
        const char quote = xml[q++];
        const size_t v0 = q;
        while (q < tag_end && xml[q] != quote) ++q;
        if (q >= tag_end) return false;
        *out = xml_attr_unescape(xml.substr(v0, q - v0));
        return true;
    }
    return false;
}

bool zip_entry_text(mz_zip_archive* zip, const std::string& name,
                    size_t max_bytes, std::string* out, std::string* err) {
    int idx = mz_zip_reader_locate_file(zip, name.c_str(), nullptr,
                                        MZ_ZIP_FLAG_CASE_SENSITIVE);
    if (idx < 0) idx = mz_zip_reader_locate_file(zip, name.c_str(), nullptr, 0);
    if (idx < 0) {
        if (err) *err = "Missing EPUB entry: " + name;
        return false;
    }
    mz_zip_archive_file_stat st{};
    if (!mz_zip_reader_file_stat(zip, idx, &st)) {
        if (err) *err = "Could not stat EPUB entry: " + name;
        return false;
    }
    if (st.m_uncomp_size > max_bytes) {
        if (err) {
            *err = "EPUB entry too large: " + name + " (" +
                   std::to_string(static_cast<unsigned long long>(st.m_uncomp_size / 1024)) + " KB)";
        }
        return false;
    }
    size_t sz = 0;
    void* data = mz_zip_reader_extract_to_heap(zip, idx, &sz, 0);
    if (!data) {
        if (err) *err = "Could not extract EPUB entry: " + name;
        return false;
    }
    out->assign(static_cast<const char*>(data), sz);
    std::free(data);
    return true;
}

std::string dirname_of_zip_path(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string normalize_zip_path(const std::string& raw) {
    std::vector<std::string> parts;
    size_t p = 0;
    while (p <= raw.size()) {
        const size_t slash = raw.find('/', p);
        std::string part = raw.substr(p, slash == std::string::npos ? std::string::npos : slash - p);
        if (part.empty() || part == ".") {
            // skip
        } else if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(part);
        }
        if (slash == std::string::npos) break;
        p = slash + 1;
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back('/');
        out += parts[i];
    }
    return out;
}

std::string resolve_zip_href(const std::string& base, const std::string& href) {
    std::string clean = href;
    const size_t frag = clean.find('#');
    if (frag != std::string::npos) clean.resize(frag);
    for (size_t i = 0; i + 2 < clean.size(); ++i) {
        if (clean[i] == '%' && clean[i + 1] == '2' &&
            clean[i + 2] == '0') {
            clean.replace(i, 3, " ");
        }
    }
    if (!clean.empty() && clean[0] == '/') clean.erase(0, 1);
    return normalize_zip_path(base + clean);
}

struct OpfManifestItem {
    std::string id;
    std::string href;
    std::string media_type;
};

const OpfManifestItem* find_manifest_item(const std::vector<OpfManifestItem>& items,
                                          const std::string& id) {
    for (const auto& item : items) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

bool media_type_is_html(const std::string& media_type, const std::string& href) {
    return media_type == "application/xhtml+xml" ||
           media_type == "text/html" ||
           has_suffix_icase(href, ".xhtml") ||
           has_suffix_icase(href, ".html") ||
           has_suffix_icase(href, ".htm");
}

EpubReadResult read_epub_text(const char* path) {
    constexpr size_t kMaxXmlBytes = 512 * 1024;
    constexpr size_t kMaxHtmlEntryBytes = 2 * 1024 * 1024;
    constexpr size_t kMaxExtractedTextBytes = 2 * 1024 * 1024;

    EpubReadResult result;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path, 0)) {
        result.error = "Could not open EPUB ZIP container.";
        return result;
    }

    std::string err;
    std::string encrypted;
    if (zip_entry_text(&zip, "META-INF/encryption.xml", 64 * 1024, &encrypted, nullptr)) {
        result.error = "Encrypted EPUB files are not supported.";
        mz_zip_reader_end(&zip);
        return result;
    }

    std::string container;
    if (!zip_entry_text(&zip, "META-INF/container.xml", kMaxXmlBytes, &container, &err)) {
        result.error = err.empty() ? "EPUB is missing META-INF/container.xml." : err;
        mz_zip_reader_end(&zip);
        return result;
    }

    size_t root_pos = container.find("<rootfile");
    std::string opf_path;
    while (root_pos != std::string::npos) {
        if (xml_attr_value(container, root_pos, "full-path", &opf_path) && !opf_path.empty()) break;
        root_pos = container.find("<rootfile", root_pos + 9);
    }
    if (opf_path.empty()) {
        result.error = "EPUB container.xml has no OPF rootfile.";
        mz_zip_reader_end(&zip);
        return result;
    }
    opf_path = normalize_zip_path(opf_path);

    std::string opf;
    if (!zip_entry_text(&zip, opf_path, kMaxXmlBytes, &opf, &err)) {
        result.error = err.empty() ? "Could not read EPUB OPF package." : err;
        mz_zip_reader_end(&zip);
        return result;
    }

    std::vector<OpfManifestItem> manifest;
    size_t item_pos = 0;
    while ((item_pos = opf.find("<item", item_pos)) != std::string::npos) {
        OpfManifestItem item;
        xml_attr_value(opf, item_pos, "id", &item.id);
        xml_attr_value(opf, item_pos, "href", &item.href);
        xml_attr_value(opf, item_pos, "media-type", &item.media_type);
        if (!item.id.empty() && !item.href.empty()) manifest.push_back(item);
        item_pos += 5;
    }
    if (manifest.empty()) {
        result.error = "EPUB OPF manifest is empty or unsupported.";
        mz_zip_reader_end(&zip);
        return result;
    }

    const std::string opf_base = dirname_of_zip_path(opf_path);
    std::vector<std::string> spine_entries;
    size_t spine_pos = opf.find("<spine");
    if (spine_pos != std::string::npos) {
        const size_t spine_end = opf.find("</spine>", spine_pos);
        size_t itemref_pos = spine_pos;
        while ((itemref_pos = opf.find("<itemref", itemref_pos)) != std::string::npos &&
               (spine_end == std::string::npos || itemref_pos < spine_end)) {
            std::string idref;
            if (xml_attr_value(opf, itemref_pos, "idref", &idref)) {
                if (const OpfManifestItem* item = find_manifest_item(manifest, idref)) {
                    if (media_type_is_html(item->media_type, item->href)) {
                        spine_entries.push_back(resolve_zip_href(opf_base, item->href));
                    }
                }
            }
            itemref_pos += 8;
        }
    }

    if (spine_entries.empty()) {
        for (const auto& item : manifest) {
            if (media_type_is_html(item.media_type, item.href)) {
                spine_entries.push_back(resolve_zip_href(opf_base, item.href));
            }
        }
        std::sort(spine_entries.begin(), spine_entries.end());
    }

    if (spine_entries.empty()) {
        result.error = "EPUB has no readable HTML/XHTML spine entries.";
        mz_zip_reader_end(&zip);
        return result;
    }

    for (const auto& entry : spine_entries) {
        if (result.text.size() >= kMaxExtractedTextBytes) break;
        std::string html;
        if (!zip_entry_text(&zip, entry, kMaxHtmlEntryBytes, &html, &err)) {
            ESP_LOGW(TAG, "EPUB skip entry %s: %s", entry.c_str(), err.c_str());
            continue;
        }
        std::string stripped = strip_html(html.c_str(), html.size());
        if (stripped.empty()) continue;
        const size_t room = kMaxExtractedTextBytes - result.text.size();
        if (stripped.size() > room) {
            stripped.resize(room);
            result.truncated = true;
        }
        result.text += stripped;
        result.text += "\n\n";
    }

    mz_zip_reader_end(&zip);
    if (result.text.empty()) {
        result.error = "EPUB parsed, but no readable text was extracted.";
        return result;
    }
    if (result.truncated) {
        result.text += "\n[Partial EPUB: firmware reached the safe text extraction cap.]\n";
    }
    result.ok = true;
    return result;
}

void save_session() {
    FILE* fp = std::fopen(kSessionPath, "w");
    if (!fp) return;
    std::fprintf(fp, "screen=%d\n", static_cast<int>(g_screen));
    std::fprintf(fp, "manga_path=%s\n", g_manga_path);
    std::fprintf(fp, "manga_page=%d\n", g_manga_page);
    std::fprintf(fp, "reader_path=%s\n", g_reader_path.c_str());
    std::fprintf(fp, "reader_page=%d\n", g_reader_page);
    std::fclose(fp);
}

void mark_activity() {
    g_last_activity_us = esp_timer_get_time();
}

const char* font_face_name() {
    return g_jp_font == JapaneseFontFace::BizUdGothic ? "BIZ UDGothic" : "IPAex Gothic";
}

const char* manga_fit_name() {
    switch (g_manga_fit_mode) {
        case MangaFitMode::FitPage: return "fit page";
        case MangaFitMode::FitWidth: return "fit width";
        case MangaFitMode::FitHeight: return "fit height";
    }
    return "fit page";
}

const char* manga_fit_label() {
    switch (g_manga_fit_mode) {
        case MangaFitMode::FitPage: return "Page";
        case MangaFitMode::FitWidth: return "Width";
        case MangaFitMode::FitHeight: return "Height";
    }
    return "Page";
}

ps3::comic::ImageFit manga_image_fit() {
    switch (g_manga_fit_mode) {
        case MangaFitMode::FitWidth: return ps3::comic::ImageFit::Width;
        case MangaFitMode::FitHeight: return ps3::comic::ImageFit::Height;
        case MangaFitMode::FitPage:
        default: return ps3::comic::ImageFit::Page;
    }
}

const char* reader_size_name() {
    switch (g_reader_font_level) {
        case 0: return "S";
        case 1: return "M";
        case 2: return "L";
        case 3: return "XL";
    }
    return "M";
}

const char* size_level_name(int level) {
    switch (level) {
        case 0: return "S";
        case 1: return "M";
        case 2: return "L";
        case 3: return "XL";
    }
    return "M";
}

const char* western_profile_name() {
    return ps3::settings::state().western_font_profile == 1 ? "Inter only" : "Off";
}

int reader_line_gap() {
    switch (g_reader_font_level) {
        case 0: return 4;
        case 1: return 8;
        case 2: return 14;
        case 3: return 20;
    }
    return 8;
}

int japanese_line_gap() {
    switch (g_japanese_font_level) {
        case 0: return 4;
        case 1: return 8;
        case 2: return 14;
        case 3: return 20;
    }
    return 8;
}

int japanese_choice_height() {
    switch (g_japanese_font_level) {
        case 0: return 68;
        case 1: return 78;
        case 2: return 90;
        case 3: return 104;
    }
    return 78;
}

int reader_lines_per_page() {
    return std::max(1, (ps3::display::height() - kToolbarH - kFooterH - 28) /
                           (active_font().height() + reader_line_gap()));
}

void reflow_reader_pages_preserving_line() {
    const int first_line = std::max(0, g_reader_page * std::max(1, g_reader_lines_per_page));
    g_reader_lines_per_page = reader_lines_per_page();
    g_reader_page = first_line / std::max(1, g_reader_lines_per_page);
    const int max_page = std::max(0, static_cast<int>((g_reader_lines.size() + g_reader_lines_per_page - 1) /
                                                     g_reader_lines_per_page) - 1);
    g_reader_page = std::max(0, std::min(g_reader_page, max_page));
}

void persist_font_settings() {
    auto& s = ps3::settings::state();
    s.reader_font_level = std::max(0, std::min(g_reader_font_level, 3));
    s.interview_font_level = std::max(0, std::min(g_interview_font_level, 3));
    s.japanese_font_level = std::max(0, std::min(g_japanese_font_level, 3));
    s.japanese_font_face = (g_jp_font == JapaneseFontFace::BizUdGothic) ? 1 : 0;
    ps3::settings::save();
}

void select_japanese_font(JapaneseFontFace face);

void apply_persisted_font_settings() {
    auto& s = ps3::settings::state();
    g_reader_font_level = std::max(0, std::min(s.reader_font_level, 3));
    g_interview_font_level = std::max(0, std::min(s.interview_font_level, 3));
    g_japanese_font_level = std::max(0, std::min(s.japanese_font_level, 3));
    select_japanese_font(s.japanese_font_face == 0 ? JapaneseFontFace::IpaCurrent
                                                   : JapaneseFontFace::BizUdGothic);
}

const char* refresh_profile_name() {
    switch (ps3::settings::state().refresh_profile) {
        case 0: return "Fast";
        case 2: return "Clean";
        case 1:
        default: return "Balanced";
    }
}

ps3::display::RefreshMode refresh_page_turn_mode() {
    switch (ps3::settings::state().refresh_profile) {
        case 0: return ps3::display::RefreshMode::GL16;
        case 2: return ps3::display::RefreshMode::GC16Full;
        case 1:
        default: return ps3::display::RefreshMode::GL16;
    }
}

ps3::display::RefreshMode refresh_reveal_mode() {
    switch (ps3::settings::state().refresh_profile) {
        case 0: return ps3::display::RefreshMode::GL16;
        case 2: return ps3::display::RefreshMode::GC16Full;
        case 1:
        default: return ps3::display::RefreshMode::GC16;
    }
}

void draw_manga_label(int x, int y, const std::string& text) {
    const int pad_x = 8;
    const int pad_y = 5;
    const int w = text_width(text) + pad_x * 2;
    const int h = active_font().height() + pad_y * 2;
    x = std::max(2, std::min(x, ps3::display::width() - w - 2));
    y = std::max(2, std::min(y, ps3::display::height() - h - 2));
    fill_rect({x, y, w, h}, 15);
    draw_rect({x, y, w, h}, 0);
    draw_text(x + pad_x, y + pad_y, text);
}

void draw_manga_edge_markers() {
    const int y = std::max(70, ps3::display::height() / 2 - active_font().height());
    draw_manga_label(8, y, "<");
    draw_manga_label(ps3::display::width() - 42, y, ">");
}

void draw_manga_hints() {
    const int w = std::min(ps3::display::width() - 36, 430);
    const int line_h = active_font().height() + 7;
    const int h = line_h * 3 + 18;
    const int x = (ps3::display::width() - w) / 2;
    const int y = std::max(78, ps3::display::height() - h - 26);
    fill_rect({x, y, w, h}, 15);
    draw_rect({x, y, w, h}, 0);
    draw_text(x + 12, y + 10, "Left/right: page");
    draw_text(x + 12, y + 10 + line_h, "Center: menu");
    draw_text(x + 12, y + 10 + line_h * 2, "Top: library");
}

void draw_manga_overlay_menu() {
    for (auto& r : g_manga_overlay_buttons) r = {};
    const int menu_w = std::min(ps3::display::width() - 40, 470);
    const int button_h = 48;
    const int gap = 8;
    const int menu_h = 22 + 6 * button_h + 5 * gap;
    const int x = (ps3::display::width() - menu_w) / 2;
    const int y = std::max(66, (ps3::display::height() - menu_h) / 2);
    fill_rect({x, y, menu_w, menu_h}, 15);
    draw_rect({x, y, menu_w, menu_h}, 0);
    const std::string labels[] = {
        std::string("Fit: ") + manga_fit_label(),
        std::string("Orientation: ") + (g_manga_landscape ? "Landscape" : "Portrait"),
        std::string("Refresh: ") + refresh_profile_name(),
        "Clean now",
        "Library",
        "Close",
    };
    int by = y + 11;
    for (int i = 0; i < 6; ++i) {
        g_manga_overlay_buttons[i] = {x + 12, by, menu_w - 24, button_h};
        draw_button(g_manga_overlay_buttons[i], labels[i]);
        by += button_h + gap;
    }
}

void draw_manga_chrome() {
    for (auto& r : g_manga_overlay_buttons) r = {};
    draw_manga_label(8, 8,
                     std::string("P ") + std::to_string(g_manga_page + 1) + "/" +
                     std::to_string(std::max(1, g_manga_book.page_count())));
    if (g_manga_slice_count > 1) {
        draw_manga_label(ps3::display::width() - 122, 8,
                         std::string("S ") + std::to_string(g_manga_slice + 1) + "/" +
                         std::to_string(g_manga_slice_count));
    }
    draw_manga_label(8, ps3::display::height() - active_font().height() - 18,
                     std::string(manga_fit_label()) + " " +
                     (g_manga_landscape ? "Land" : "Port") + " " +
                     refresh_profile_name());
    draw_manga_edge_markers();
    if (g_manga_overlay_visible) {
        draw_manga_overlay_menu();
    } else if (g_manga_hint_pages_remaining > 0) {
        draw_manga_hints();
    }
}

int manga_top_strip_h() {
    return std::max(44, std::min(104, ps3::display::height() * 11 / 100));
}

void cycle_refresh_profile() {
    auto& s = ps3::settings::state();
    s.refresh_profile = (s.refresh_profile + 1) % 3;
    ps3::settings::save();
    ESP_LOGI(TAG, "refresh profile: %s", refresh_profile_name());
}

void cycle_full_refresh_pages() {
    auto& s = ps3::settings::state();
    switch (s.full_refresh_pages) {
        case 1: s.full_refresh_pages = 5; break;
        case 5: s.full_refresh_pages = 10; break;
        case 10: s.full_refresh_pages = 15; break;
        case 15: s.full_refresh_pages = 20; break;
        default: s.full_refresh_pages = 1; break;
    }
    ps3::settings::save();
}

void cycle_manga_fit_mode() {
    switch (g_manga_fit_mode) {
        case MangaFitMode::FitPage: g_manga_fit_mode = MangaFitMode::FitWidth; break;
        case MangaFitMode::FitWidth: g_manga_fit_mode = MangaFitMode::FitHeight; break;
        case MangaFitMode::FitHeight: g_manga_fit_mode = MangaFitMode::FitPage; break;
    }
    g_manga_pending_last_slice = false;
    ps3::comic::page_loader::invalidate();
}

void toggle_manga_orientation() {
    g_manga_landscape = !g_manga_landscape;
    g_manga_slice = 0;
    g_manga_slice_count = 1;
    g_manga_pending_last_slice = false;
    g_manga_overlay_visible = false;
    g_manga_hint_pages_remaining = 4;
    if (g_manga_landscape && g_manga_fit_mode == MangaFitMode::FitPage) {
        g_manga_fit_mode = MangaFitMode::FitWidth;
    } else if (!g_manga_landscape && g_manga_fit_mode == MangaFitMode::FitWidth) {
        g_manga_fit_mode = MangaFitMode::FitPage;
    }
    ps3::comic::page_loader::invalidate();
}

void select_japanese_font(JapaneseFontFace face) {
    g_jp_font = face;
    g_font = (face == JapaneseFontFace::BizUdGothic) ? &g_biz_font : &g_ipa_font;
}

void set_screen_orientation(bool inverted) {
    ps3::display::set_inverted(inverted);
    ps3::touch::set_inverted(inverted);
}

void set_screen_rotation(bool landscape, bool inverted) {
    if (landscape) {
        ps3::display::set_rotation(inverted ? ps3::display::Rotation::InvertedLandscape
                                            : ps3::display::Rotation::Landscape);
    } else {
        ps3::display::set_rotation(inverted ? ps3::display::Rotation::Portrait
                                            : ps3::display::Rotation::InvertedPortrait);
    }
    ps3::touch::set_rotation(landscape, inverted);
}

void restore_app_orientation() {
    set_screen_rotation(false, ps3::settings::state().rotation_inverted);
}

void use_manual_badge_orientation() {
    set_screen_rotation(false, false);
}

void use_manga_orientation() {
    set_screen_rotation(g_manga_landscape, ps3::settings::state().rotation_inverted);
}

// ── Interview helpers ─────────────────────────────────────────────────

// Returns true for drills that have selectable MCQ options.
bool iv_drill_is_mcq(int idx) {
    if (idx < 0 || idx >= static_cast<int>(embedded_papercoach::kDrillCount)) return false;
    return embedded_papercoach::kDrills[idx].optionCount > 0;
}

// Build a pool of kExamSize MCQ drill indices, shuffled via LCG.
void iv_build_exam() {
    // Collect all MCQ drill indices
    std::vector<int> pool;
    pool.reserve(embedded_papercoach::kDrillCount);
    for (int i = 0; i < static_cast<int>(embedded_papercoach::kDrillCount); ++i) {
        if (iv_drill_is_mcq(i)) pool.push_back(i);
    }
    if (pool.empty()) {
        g_iv_exam_count = 0;
        g_iv_exam_current = 0;
        g_iv_exam_score = 0;
        return;
    }
    // Shuffle with a simple LCG
    uint32_t seed = static_cast<uint32_t>(esp_timer_get_time() & 0xFFFFFFFF);
    for (size_t i = pool.size() - 1; i > 0; --i) {
        seed = seed * 1664525u + 1013904223u;
        const size_t j = seed % (i + 1);
        std::swap(pool[i], pool[j]);
    }
    g_iv_exam_count = std::min(kExamSize, static_cast<int>(pool.size()));
    for (int i = 0; i < g_iv_exam_count; ++i) g_iv_exam_pool[i] = pool[i];
    g_iv_exam_current = 0;
    g_iv_exam_score = 0;
    std::memset(g_iv_exam_answers, 0, sizeof(g_iv_exam_answers));
}

std::vector<int> iv_section_first_indices() {
    std::vector<int> firsts;
    for (int i = 0; i < static_cast<int>(embedded_papercoach::kCardCount); ++i) {
        const char* id = embedded_papercoach::kCards[i].sectionId;
        bool seen = false;
        for (int first : firsts) {
            if (std::strcmp(embedded_papercoach::kCards[first].sectionId, id) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) firsts.push_back(i);
    }
    return firsts;
}

bool iv_card_matches_filter(int idx) {
    if (idx < 0 || idx >= static_cast<int>(embedded_papercoach::kCardCount)) return false;
    const auto& c = embedded_papercoach::kCards[idx];
    if (g_iv_practice_mode == InterviewPracticeMode::MustMaster && !c.mustMaster) return false;
    if (g_iv_practice_mode == InterviewPracticeMode::Category) {
        const auto sections = iv_section_first_indices();
        if (g_iv_practice_section_idx < 0 ||
            g_iv_practice_section_idx >= static_cast<int>(sections.size())) {
            return false;
        }
        const char* wanted = embedded_papercoach::kCards[sections[g_iv_practice_section_idx]].sectionId;
        return std::strcmp(c.sectionId, wanted) == 0;
    }
    return true;
}

std::vector<int> iv_filtered_cards() {
    std::vector<int> cards;
    for (int i = 0; i < static_cast<int>(embedded_papercoach::kCardCount); ++i) {
        if (iv_card_matches_filter(i)) cards.push_back(i);
    }
    return cards;
}

int iv_next_filtered_card(int from, int step) {
    int i = from + step;
    while (i >= 0 && i < static_cast<int>(embedded_papercoach::kCardCount)) {
        if (iv_card_matches_filter(i)) return i;
        i += step;
    }
    return -1;
}

int iv_answer_page_count(const embedded_papercoach::Card& c) {
    const int body_w = ps3::display::width() - 76;
    const int spoken_lines = static_cast<int>(wrap_text(c.spoken ? c.spoken : "", body_w).size());
    int pages = std::max(1, (spoken_lines + 11) / 12);
    if ((c.anchor && c.anchor[0]) || (c.watch && c.watch[0])) ++pages;
    if ((c.theme && c.theme[0]) || (c.confidence && c.confidence[0])) ++pages;
    return pages;
}

int iv_spoken_page_count(const embedded_papercoach::Card& c) {
    const int body_w = ps3::display::width() - 76;
    const int spoken_lines = static_cast<int>(wrap_text(c.spoken ? c.spoken : "", body_w).size());
    return std::max(1, (spoken_lines + 11) / 12);
}

int draw_labeled_block(int x, int y, int max_w, const std::string& label,
                       const char* body, int max_lines = 0) {
    if (!body || !body[0]) return y;
    draw_text(x, y, label);
    y += active_font().height() + 6;
    y = draw_wrapped(x + 16, y, max_w - 16, body, max_lines) + 12;
    return y;
}

std::vector<const char*> iv_glossary_categories() {
    std::vector<const char*> cats;
    for (int i = 0; i < static_cast<int>(embedded_papercoach::kGlossaryCount); ++i) {
        const char* cat = embedded_papercoach::kGlossaryTerms[i].category;
        if (!cat || !cat[0]) cat = "General";
        bool seen = false;
        for (const char* existing : cats) {
            if (std::strcmp(existing, cat) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) cats.push_back(cat);
    }
    return cats;
}

std::vector<int> iv_glossary_terms_for_category(int category_idx) {
    std::vector<int> terms;
    const auto cats = iv_glossary_categories();
    if (category_idx < 0 || category_idx >= static_cast<int>(cats.size())) return terms;
    const char* wanted = cats[category_idx];
    for (int i = 0; i < static_cast<int>(embedded_papercoach::kGlossaryCount); ++i) {
        const char* cat = embedded_papercoach::kGlossaryTerms[i].category;
        if (!cat || !cat[0]) cat = "General";
        if (std::strcmp(cat, wanted) == 0) terms.push_back(i);
    }
    return terms;
}

// ── Forward declarations ───────────────────────────────────────────────
void render_home(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_badge();
void render_interview(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_practice_menu(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_card_list(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_practice(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_drill_q(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_drill_fb(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_glossary_list(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_glossary(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_results(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_manga_library(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_manga_page(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GL16);
void render_manga_error(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_reader_library(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_reader_page(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GL16);
void render_reader_error(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_menu(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_source(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_unit(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_lesson(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_feedback(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_mock(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_mock_results(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_reference(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_results(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_font(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_settings(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void save_reader_state();
void update_manga_progress();
void enter_light_sleep(const char* trigger);
void enter_deep_sleep();

void close_manga_book_if_open() {
    if (!g_manga_open) return;
    ps3::comic::page_loader::stop();
    g_manga_book.close();
    g_manga_open = false;
    g_manga_overlay_visible = false;
    g_manga_hint_pages_remaining = 0;
    g_manga_pending_last_slice = false;
}

void nav_clear() {
    g_nav_depth = 0;
}

void nav_push(Screen screen) {
    if (screen == Screen::Home) {
        nav_clear();
    }
    if (g_nav_depth > 0 && g_nav_stack[g_nav_depth - 1] == screen) return;
    if (g_nav_depth >= static_cast<int>(sizeof(g_nav_stack) / sizeof(g_nav_stack[0]))) {
        for (int i = 1; i < g_nav_depth; ++i) g_nav_stack[i - 1] = g_nav_stack[i];
        --g_nav_depth;
    }
    g_nav_stack[g_nav_depth++] = screen;
}

Screen fallback_back_target(Screen screen) {
    switch (screen) {
        case Screen::Badge:
        case Screen::Interview:
        case Screen::Japanese:
        case Screen::JapaneseMenu:
        case Screen::MangaLibrary:
        case Screen::ReaderLibrary:
        case Screen::Settings:
            return Screen::Home;
        case Screen::InterviewPracticeMenu:
        case Screen::InterviewCardList:
        case Screen::InterviewPractice:
        case Screen::InterviewDrillQ:
        case Screen::InterviewDrillFB:
        case Screen::InterviewGlossaryList:
        case Screen::InterviewGlossary:
        case Screen::InterviewResults:
            return Screen::Interview;
        case Screen::JapaneseFeedback:
            return Screen::Japanese;
        case Screen::JapaneseSource:
        case Screen::JapaneseReference:
        case Screen::JapaneseResults:
            return Screen::JapaneseMenu;
        case Screen::JapaneseUnit:
            return Screen::JapaneseSource;
        case Screen::JapaneseLesson:
            return Screen::JapaneseUnit;
        case Screen::JapaneseMock:
        case Screen::JapaneseMockResults:
            return Screen::JapaneseMenu;
        case Screen::JapaneseFont:
            return g_nav_depth > 0 ? g_nav_stack[g_nav_depth - 1] : Screen::Japanese;
        case Screen::MangaReading:
        case Screen::MangaError:
            return Screen::MangaLibrary;
        case Screen::ReaderReading:
        case Screen::ReaderError:
            return Screen::ReaderLibrary;
        case Screen::Home:
        default:
            return Screen::Home;
    }
}

void before_leave_screen(Screen from, Screen to) {
    if (from == Screen::ReaderReading && to != Screen::ReaderReading) {
        save_reader_state();
    }
    if (from == Screen::MangaReading && to != Screen::MangaReading) {
        update_manga_progress();
        close_manga_book_if_open();
    }
}

void render_screen(Screen screen, ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16) {
    switch (screen) {
        case Screen::Home:               render_home(mode); break;
        case Screen::Badge:              render_badge(); break;
        case Screen::Interview:          render_interview(mode); break;
        case Screen::InterviewPracticeMenu: render_interview_practice_menu(mode); break;
        case Screen::InterviewCardList:  render_interview_card_list(mode); break;
        case Screen::InterviewPractice:  render_interview_practice(mode); break;
        case Screen::InterviewDrillQ:    render_interview_drill_q(mode); break;
        case Screen::InterviewDrillFB:   render_interview_drill_fb(mode); break;
        case Screen::InterviewGlossaryList: render_interview_glossary_list(mode); break;
        case Screen::InterviewGlossary:  render_interview_glossary(mode); break;
        case Screen::InterviewResults:   render_interview_results(mode); break;
        case Screen::MangaLibrary:       render_manga_library(mode); break;
        case Screen::MangaReading:       render_manga_page(mode); break;
        case Screen::MangaError:         render_manga_error(mode); break;
        case Screen::ReaderLibrary:      render_reader_library(mode); break;
        case Screen::ReaderReading:      render_reader_page(mode); break;
        case Screen::ReaderError:        render_reader_error(mode); break;
        case Screen::JapaneseMenu:       render_japanese_menu(mode); break;
        case Screen::JapaneseSource:     render_japanese_source(mode); break;
        case Screen::JapaneseUnit:       render_japanese_unit(mode); break;
        case Screen::JapaneseLesson:     render_japanese_lesson(mode); break;
        case Screen::Japanese:           render_japanese(mode); break;
        case Screen::JapaneseFeedback:   render_japanese_feedback(mode); break;
        case Screen::JapaneseMock:       render_japanese_mock(mode); break;
        case Screen::JapaneseMockResults: render_japanese_mock_results(mode); break;
        case Screen::JapaneseReference:  render_japanese_reference(mode); break;
        case Screen::JapaneseResults:    render_japanese_results(mode); break;
        case Screen::JapaneseFont:       render_japanese_font(mode); break;
        case Screen::Settings:           render_settings(mode); break;
        default:                         render_home(mode); break;
    }
}

void navigate_to(Screen screen, ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16Full) {
    const Screen from = g_screen;
    before_leave_screen(from, screen);
    if (screen == Screen::Home) nav_clear();
    render_screen(screen, mode);
}

void navigate_back() {
    Screen target = Screen::Home;
    if (g_nav_depth > 0) {
        target = g_nav_stack[--g_nav_depth];
    } else {
        target = fallback_back_target(g_screen);
    }
    navigate_to(target);
}

void navigate_home() {
    navigate_to(Screen::Home);
}

bool screen_uses_header_nav(Screen screen) {
    switch (screen) {
        case Screen::Home:
        case Screen::Badge:
        case Screen::MangaReading:
            return false;
        default:
            return true;
    }
}

bool handle_header_nav(int x, int y) {
    if (!screen_uses_header_nav(g_screen) || y >= kToolbarH) return false;
    if (x < ps3::display::width() / 2) {
        navigate_back();
    } else {
        navigate_home();
    }
    ps3::touch::drain();
    return true;
}

void render_current(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16) {
    render_screen(g_screen, mode);
}

// ── Badge final-frame (persists on e-ink during sleep / power-off) ────
void draw_english_badge_final_frame() {
    const bool previous_orientation = ps3::display::is_inverted();
    set_screen_orientation(true);  // strap/lanyard final frame: 180°
    ps3::display::clear();
    ESP_LOGI(TAG, "badge final frame: PNG %u bytes", (unsigned)embedded_badge::kBadgeEnSize);
    const bool shown = ps3::comic::display_png(
        embedded_badge::kBadgeEnPng, embedded_badge::kBadgeEnSize);
    ESP_LOGI(TAG, "badge final frame: %s", shown ? "OK" : "text fallback");
    if (!shown) {
        draw_wrapped(40, 80, ps3::display::width() - 80,
                     "Daniel Jimenez\nSenior Technical PM | AI Products");
    }
    ps3::display::flush(ps3::display::RefreshMode::GC16Full);
    set_screen_orientation(previous_orientation);
}

// ── Home ───────────────────────────────────────────────────────────────
void render_home(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::Home;
    nav_clear();
    ps3::display::clear();
    draw_header("PaperBadge", "ESP-IDF");
    const char* labels[] = {"Badge", "Interview", "日本語", "Manga", "Reader", "Settings"};
    const int x = 34;
    const int w = ps3::display::width() - 68;
    const int h = 86;
    const int gap = 14;
    int y = 98;
    for (int i = 0; i < 6; ++i) {
        g_home_buttons[i] = {x, y, w, h};
        draw_button(g_home_buttons[i], labels[i]);
        y += h + gap;
    }
    draw_wrapped(34, y + 12, ps3::display::width() - 68,
                 "epdiy display  •  GT911 touch  •  ADC battery  •  SD manga/books");
    ps3::display::flush(mode);
}

// ── Badge ─────────────────────────────────────────────────────────────
void render_badge_qr_zoom() {
    use_manual_badge_orientation();
    g_screen = Screen::Badge;
    ps3::display::clear();
    const bool shown = ps3::comic::display_png_fit(
        embedded_badge::kQrPng, embedded_badge::kQrSize, 500, 500);
    if (!shown) {
        draw_header("QR");
        draw_wrapped(34, 130, ps3::display::width() - 68,
                     "QR image could not be decoded.\nTap to return to badge.");
    }
    ps3::display::flush(ps3::display::RefreshMode::GC16Full);
}

void render_badge() {
    use_manual_badge_orientation();
    g_screen = Screen::Badge;
    g_badge_qr_rect = {111, 600, 318, 318};
    if (g_badge_qr_zoom) {
        render_badge_qr_zoom();
        return;
    }
    ps3::display::clear();
    const uint8_t* badge_png = g_badge_japanese ? embedded_badge::kBadgeJaPng : embedded_badge::kBadgeEnPng;
    const size_t badge_size = g_badge_japanese ? embedded_badge::kBadgeJaSize : embedded_badge::kBadgeEnSize;
    const char* badge_file = g_badge_japanese ? "badge_ja.png" : "badge_en.png";
    ESP_LOGI(TAG, "badge: embedded %s PNG %u bytes", g_badge_japanese ? "JA" : "EN",
             (unsigned)badge_size);
    bool shown = ps3::comic::display_png(
        badge_png, badge_size);
    ESP_LOGI(TAG, "badge: embedded decode %s", shown ? "OK" : "FAILED");
    // Fallback: SD asset file
    if (!shown) {
        const std::string png = std::string(kAssetsRoot) + "/" + badge_file;
        const auto bytes = read_file_bytes(png.c_str(), 2 * 1024 * 1024);
        if (!bytes.empty()) {
            ESP_LOGI(TAG, "badge: trying SD fallback %u bytes", (unsigned)bytes.size());
            shown = ps3::comic::display_png(bytes.data(), bytes.size());
            ESP_LOGI(TAG, "badge: SD decode %s", shown ? "OK" : "FAILED");
        }
    }
    // Fallback: rendered text + QR placeholder
    if (!shown) {
        draw_header("Daniel Jimenez");
        int y = kToolbarH + 24;
        y = draw_wrapped(34, y, ps3::display::width() - 68,
                         g_badge_japanese
                             ? "プロダクトマネージャー (AI)"
                             : "Senior Technical PM | AI Products",
                         2) + 16;
        draw_hline(20, y, ps3::display::width() - 40);
        y += 16;
        draw_wrapped(34, y, ps3::display::width() - 68,
                     "Platform Teams  |  Embedded & AI\n"
                     "M5PaperS3 PaperBadge", 4);
        ps3::comic::display_png_at(embedded_badge::kQrPng, embedded_badge::kQrSize,
                                   (ps3::display::width() - 320) / 2, 600);
        ESP_LOGW(TAG, "badge: all decode paths failed — showing text fallback");
    }
    ps3::display::flush(ps3::display::RefreshMode::GC16Full);
}

// ── Interview — menu ──────────────────────────────────────────────────
void render_interview(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::Interview;
    ps3::display::clear();
    draw_header("Interview", std::to_string(embedded_papercoach::kCardCount) + " cards");
    const char* labels[] = {"Practice", "Drills", "Exam", "Glossary", "Results"};
    const int x = 34;
    const int w = ps3::display::width() - 68;
    const int h = 90;
    const int gap = 10;
    int y = 90;
    for (int i = 0; i < 5; ++i) {
        g_iv_menu_buttons[i] = {x, y, w, h};
        draw_button(g_iv_menu_buttons[i], labels[i]);
        y += h + gap;
    }
    draw_wrapped(34, y + 10, w,
                 std::to_string(embedded_papercoach::kDrillCount) + " drills  |  " +
                 std::to_string(embedded_papercoach::kGlossaryCount) + " glossary terms  |  embedded");
    draw_footer("Home", nullptr, nullptr);
    ps3::display::flush(mode);
}

// ── Interview — Practice menu / list ─────────────────────────────────
void render_interview_practice_menu(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewPracticeMenu;
    ps3::display::clear();
    draw_header("Practice", "PaperCoach");
    const char* labels[] = {"All Cards", "Must Master", "By Category", "Resume"};
    for (int i = 0; i < 4; ++i) {
        g_iv_practice_buttons[i] = {34, 104 + i * 96, ps3::display::width() - 68, 76};
        draw_button(g_iv_practice_buttons[i], labels[i]);
    }
    draw_wrapped(34, 510, ps3::display::width() - 68,
                 "Choose a practice lane, then pick a specific card from the list.");
    draw_footer("Menu", nullptr, nullptr);
    ps3::display::flush(mode);
}

void render_interview_card_list(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewCardList;
    ps3::display::clear();
    for (int i = 0; i < 10; ++i) g_list_rows[i] = {};

    if (g_iv_practice_mode == InterviewPracticeMode::Category &&
        g_iv_practice_section_idx < 0) {
        const auto sections = iv_section_first_indices();
        draw_header("Practice Categories", std::to_string(sections.size()));
        int y = 86;
        for (int i = 0; i < static_cast<int>(sections.size()) && i < 9; ++i) {
            const auto& c = embedded_papercoach::kCards[sections[i]];
            g_list_rows[i] = {24, y, ps3::display::width() - 48, 72};
            draw_rect(g_list_rows[i]);
            draw_wrapped(g_list_rows[i].x + 14, g_list_rows[i].y + 16,
                         g_list_rows[i].w - 28,
                         std::string(c.sectionId) + ". " + c.section, 1);
            y += 78;
        }
        draw_footer("Back", nullptr, nullptr);
        ps3::display::flush(mode);
        return;
    }

    const auto cards = iv_filtered_cards();
    const int page_size = 8;
    const int page_count = std::max(1, static_cast<int>((cards.size() + page_size - 1) / page_size));
    g_iv_card_list_page = std::max(0, std::min(g_iv_card_list_page, page_count - 1));
    const char* title = "All Cards";
    if (g_iv_practice_mode == InterviewPracticeMode::MustMaster) title = "Must Master";
    if (g_iv_practice_mode == InterviewPracticeMode::Category) title = "Category Cards";
    draw_header(title, std::to_string(cards.size()) + " cards");
    int y = 82;
    const int start = g_iv_card_list_page * page_size;
    for (int row = 0; row < page_size && start + row < static_cast<int>(cards.size()); ++row) {
        const int card_idx = cards[start + row];
        const auto& c = embedded_papercoach::kCards[card_idx];
        g_list_rows[row] = {22, y, ps3::display::width() - 44, 80};
        draw_rect(g_list_rows[row]);
        const std::string label = std::string(c.sectionId) + c.number +
                                  (c.mustMaster ? " * " : "   ") + c.title;
        draw_wrapped(g_list_rows[row].x + 12, g_list_rows[row].y + 14,
                     g_list_rows[row].w - 24, label, 2);
        y += 86;
    }
    if (cards.empty()) {
        draw_wrapped(34, 140, ps3::display::width() - 68,
                     "No cards match this practice filter.");
    }
    draw_footer("Back",
                g_iv_card_list_page > 0 ? "Prev" : nullptr,
                g_iv_card_list_page + 1 < page_count ? "Next" : nullptr);
    ps3::display::flush(mode);
}

// ── Interview — Practice ──────────────────────────────────────────────
void render_interview_practice(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewPractice;
    const auto& c = embedded_papercoach::kCards[g_iv_card_idx];
    ps3::display::clear();
    const std::string idx_str = std::to_string(g_iv_card_idx + 1) + "/" +
                                 std::to_string(embedded_papercoach::kCardCount);
    draw_header(std::string("Practice ") + idx_str,
                c.mustMaster ? "MUST" : "");
    int y = 80;
    // Section + number + title
    y = draw_wrapped(30, y, ps3::display::width() - 60,
                     std::string(c.section) + " " + c.number, 1) + 4;
    y = draw_wrapped(30, y, ps3::display::width() - 60, c.title, 3) + 10;
    draw_hline(20, y, ps3::display::width() - 40);
    y += 12;
    if (g_iv_card_spoken) {
        const int page_count = iv_answer_page_count(c);
        g_iv_answer_page = std::max(0, std::min(g_iv_answer_page, page_count - 1));
        const int spoken_pages = iv_spoken_page_count(c);
        if (g_iv_answer_page < spoken_pages) {
            draw_text(30, y, "Spoken answer");
            y += active_font().height() + 6;
            const auto lines = wrap_text(c.spoken ? c.spoken : "", ps3::display::width() - 76);
            const int first = g_iv_answer_page * 12;
            for (int i = 0; i < 12 && first + i < static_cast<int>(lines.size()); ++i) {
                draw_text(46, y, lines[first + i]);
                y += active_font().height() + 8;
            }
        } else if (g_iv_answer_page == spoken_pages) {
            y = draw_labeled_block(30, y, ps3::display::width() - 60,
                                   "Anchor", c.anchor, 5);
            draw_labeled_block(30, y, ps3::display::width() - 60,
                               "Watch-out", c.watch, 6);
        } else {
            y = draw_labeled_block(30, y, ps3::display::width() - 60,
                                   "Theme", c.theme, 3);
            draw_labeled_block(30, y, ps3::display::width() - 60,
                               "Confidence", c.confidence, 5);
        }
    } else {
        // Show prompt to reveal
        draw_wrapped(30, y, ps3::display::width() - 60,
                     "Tap REVEAL to see the answer.\n\n"
                     "Theme: " + std::string(c.theme) + "\n"
                     "Confidence: " + c.confidence, 6);
    }
    const int prev = iv_next_filtered_card(g_iv_card_idx, -1);
    const int next = iv_next_filtered_card(g_iv_card_idx, +1);
    const int page_count = g_iv_card_spoken ? iv_answer_page_count(c) : 1;
    draw_footer(prev >= 0 ? "Prev" : "List",
                g_iv_card_spoken
                    ? (g_iv_answer_page + 1 < page_count ? "More" : "Question")
                    : "Reveal",
                next >= 0 ? "Next" : nullptr);
    ps3::display::flush(mode);
}

// ── Interview — Drill Q ───────────────────────────────────────────────
void render_interview_drill_q(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewDrillQ;
    if (g_iv_in_exam) {
        // In exam mode use the shuffled exam pool
        if (g_iv_exam_current >= g_iv_exam_count) {
            render_interview(mode);
            return;
        }
        g_iv_drill_idx = g_iv_exam_pool[g_iv_exam_current];
    } else {
        // Free-drill mode: skip non-MCQ entries
        while (g_iv_drill_idx < static_cast<int>(embedded_papercoach::kDrillCount) &&
               !iv_drill_is_mcq(g_iv_drill_idx)) {
            ++g_iv_drill_idx;
        }
        if (g_iv_drill_idx >= static_cast<int>(embedded_papercoach::kDrillCount)) {
            render_interview(mode);
            return;
        }
    }
    const auto& d = embedded_papercoach::kDrills[g_iv_drill_idx];
    ps3::display::clear();
    // Count only MCQ drills for the label
    int mcq_pos = 0;
    for (int i = 0; i <= g_iv_drill_idx; ++i) {
        if (iv_drill_is_mcq(i)) ++mcq_pos;
    }
    draw_header(g_iv_in_exam ? "Exam" : "Drills",
                g_iv_in_exam
                    ? (std::to_string(g_iv_exam_current + 1) + "/" + std::to_string(g_iv_exam_count))
                    : std::to_string(mcq_pos));
    int y = 80;
    y = draw_wrapped(30, y, ps3::display::width() - 60, d.prompt, 5) + 10;
    for (int i = 0; i < d.optionCount && i < 4; ++i) {
        g_iv_choices[i] = {30, y, ps3::display::width() - 60, 78};
        std::string label;
        label.push_back(static_cast<char>('A' + i));
        label += ". ";
        label += d.options[i];
        draw_button(g_iv_choices[i], label, g_iv_drill_answer == i);
        y += 90;
    }
    draw_footer("Menu", nullptr, g_iv_drill_answer >= 0 ? "Submit" : nullptr);
    ps3::display::flush(mode);
}

// ── Interview — Drill Feedback ────────────────────────────────────────
void render_interview_drill_fb(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewDrillFB;
    const int drill_idx = g_iv_in_exam
        ? g_iv_exam_pool[g_iv_exam_current]
        : g_iv_drill_idx;
    const auto& d = embedded_papercoach::kDrills[drill_idx];
    const bool correct = g_iv_drill_answer == d.correctIndex;
    ps3::display::clear();
    draw_header(correct ? "Correct!" : "Wrong", g_iv_in_exam ? "Exam" : "Drills");
    int y = 80;
    // Show correct answer
    std::string ans = "Answer: ";
    ans.push_back(static_cast<char>('A' + d.correctIndex));
    if (d.correctIndex < d.optionCount) {
        ans += ". ";
        ans += d.options[d.correctIndex];
    }
    y = draw_wrapped(30, y, ps3::display::width() - 60, ans, 3) + 10;
    draw_hline(20, y, ps3::display::width() - 40);
    y += 12;
    draw_wrapped(30, y, ps3::display::width() - 60, d.explanation, 10);
    const char* right_label = nullptr;
    if (g_iv_in_exam) {
        right_label = (g_iv_exam_current + 1 < g_iv_exam_count) ? "Next" : "Results";
    } else {
        // Check if there's another MCQ drill ahead
        bool has_next = false;
        for (int i = g_iv_drill_idx + 1; i < static_cast<int>(embedded_papercoach::kDrillCount); ++i) {
            if (iv_drill_is_mcq(i)) { has_next = true; break; }
        }
        right_label = has_next ? "Next" : nullptr;
    }
    draw_footer("Menu", nullptr, right_label);
    ps3::display::flush(mode);
}

// ── Interview — Glossary list ────────────────────────────────────────
void render_interview_glossary_list(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewGlossaryList;
    ps3::display::clear();
    for (int i = 0; i < 10; ++i) g_list_rows[i] = {};

    if (g_iv_gloss_category_idx < 0) {
        const auto cats = iv_glossary_categories();
        draw_header("Glossary Categories", std::to_string(cats.size()));
        int y = 90;
        for (int i = 0; i < static_cast<int>(cats.size()) && i < 9; ++i) {
            g_list_rows[i] = {24, y, ps3::display::width() - 48, 72};
            draw_rect(g_list_rows[i]);
            draw_wrapped(g_list_rows[i].x + 14, g_list_rows[i].y + 18,
                         g_list_rows[i].w - 28, cats[i], 1);
            y += 78;
        }
        draw_footer("Menu", nullptr, nullptr);
        ps3::display::flush(mode);
        return;
    }

    const auto cats = iv_glossary_categories();
    const auto terms = iv_glossary_terms_for_category(g_iv_gloss_category_idx);
    const int page_size = 8;
    const int page_count = std::max(1, static_cast<int>((terms.size() + page_size - 1) / page_size));
    g_iv_gloss_list_page = std::max(0, std::min(g_iv_gloss_list_page, page_count - 1));
    draw_header(g_iv_gloss_category_idx < static_cast<int>(cats.size()) ? cats[g_iv_gloss_category_idx] : "Glossary",
                std::to_string(terms.size()));
    int y = 82;
    const int start = g_iv_gloss_list_page * page_size;
    for (int row = 0; row < page_size && start + row < static_cast<int>(terms.size()); ++row) {
        const int term_idx = terms[start + row];
        const auto& term = embedded_papercoach::kGlossaryTerms[term_idx];
        g_list_rows[row] = {22, y, ps3::display::width() - 44, 80};
        draw_rect(g_list_rows[row]);
        draw_wrapped(g_list_rows[row].x + 12, g_list_rows[row].y + 16,
                     g_list_rows[row].w - 24, term.term, 2);
        y += 86;
    }
    draw_footer("Back",
                g_iv_gloss_list_page > 0 ? "Prev" : nullptr,
                g_iv_gloss_list_page + 1 < page_count ? "Next" : nullptr);
    ps3::display::flush(mode);
}

// ── Interview — Glossary ──────────────────────────────────────────────
void render_interview_glossary(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewGlossary;
    const auto& g = embedded_papercoach::kGlossaryTerms[g_iv_gloss_idx];
    ps3::display::clear();
    const std::string idx_str = std::to_string(g_iv_gloss_idx + 1) + "/" +
                                 std::to_string(embedded_papercoach::kGlossaryCount);
    draw_header("Glossary " + idx_str, g.category ? g.category : "");
    int y = 80;
    y = draw_wrapped(30, y, ps3::display::width() - 60, g.term, 2) + 6;
    draw_hline(20, y, ps3::display::width() - 40);
    y += 10;
    y = draw_wrapped(30, y, ps3::display::width() - 60, g.definition, 8) + 10;
    if (g.example && g.example[0]) {
        draw_wrapped(30, y, ps3::display::width() - 60,
                     std::string("Example: ") + g.example, 5);
    }
    draw_footer(g_iv_gloss_idx > 0 ? "Prev" : "Menu",
                "Menu",
                g_iv_gloss_idx + 1 < static_cast<int>(embedded_papercoach::kGlossaryCount) ? "Next" : nullptr);
    ps3::display::flush(mode);
}

// ── Interview — Results ───────────────────────────────────────────────
void render_interview_results(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::InterviewResults;
    ps3::display::clear();
    draw_header(g_iv_exam_count > 0 ? "Exam Results" : "Interview Results");
    int y = 90;
    y = draw_wrapped(30, y, ps3::display::width() - 60,
                     "Session summary", 1) + 10;
    if (g_iv_session_total > 0) {
        const int pct = g_iv_session_correct * 100 / g_iv_session_total;
        y = draw_wrapped(30, y, ps3::display::width() - 60,
                         "Drills:   " + std::to_string(g_iv_session_correct) + "/" +
                         std::to_string(g_iv_session_total) + "  (" + std::to_string(pct) + "%)", 2) + 8;
    } else {
        y = draw_wrapped(30, y, ps3::display::width() - 60,
                         "No drills completed this session.", 2) + 8;
    }
    if (g_iv_exam_count > 0) {
        y = draw_wrapped(30, y, ps3::display::width() - 60,
                         "Last Exam: " + std::to_string(g_iv_exam_score) + "/" +
                         std::to_string(g_iv_exam_count), 2) + 8;
    }
    y = draw_wrapped(30, y, ps3::display::width() - 60,
                     "Practice cards viewed: " + std::to_string(g_iv_session_practice), 2) + 10;
    draw_hline(20, y, ps3::display::width() - 40);
    y += 12;
    draw_wrapped(30, y, ps3::display::width() - 60,
                 "Reset clears session stats for this run.\nSD progress persistence not yet implemented.", 4);
    draw_footer("Menu", "Reset", nullptr);
    ps3::display::flush(mode);
}

// ── Manga ─────────────────────────────────────────────────────────────
bool open_manga_library() {
    if (g_manga_library.open(kMangaRoot)) return true;
    mkdir_if_missing(kMangaRoot);
    return g_manga_library.open(kMangaRoot);
}

void render_manga_library(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::MangaLibrary;
    const bool sd_ok = open_manga_library();
    ps3::display::clear();
    if (!sd_ok) {
        draw_header("Manga", "No SD");
        draw_wrapped(34, 130, ps3::display::width() - 68,
                     "No SD card detected.\n\nInsert an SD card with CBZ files under:\n/paperBadge/content/manga");
        draw_footer("Home", nullptr, nullptr);
        ps3::display::flush(mode);
        return;
    }
    draw_header("Manga", std::to_string(g_manga_library.total_count()) + " items");
    int y = 84;
    const int row_h = 72;
    for (int i = 0; i < 9; ++i) g_list_rows[i] = {};
    for (int i = 0; i < g_manga_library.count(); ++i) {
        const auto& e = g_manga_library.at(i);
        Rect r{24, y, ps3::display::width() - 48, row_h - 8};
        g_list_rows[i] = r;
        draw_rect(r);
        const std::string prefix = e.kind == ps3::library::EntryKind::Folder ? "[DIR] " : "[CBZ] ";
        draw_wrapped(r.x + 14, r.y + 18, r.w - 28, prefix + e.name, 1);
        y += row_h;
    }
    if (g_manga_library.count() == 0) {
        draw_wrapped(34, 130, ps3::display::width() - 68,
                     "No manga found.\n\nPut CBZ or ZIP files under:\n/paperBadge/content/manga");
    }
    draw_footer(g_manga_library.can_go_up() ? "Up" : "Home",
                g_manga_library.can_page_prev() ? "Prev" : nullptr,
                g_manga_library.can_page_next() ? "Next" : nullptr);
    ps3::display::flush(mode);
}

bool open_manga_book(const char* path) {
    if (g_manga_open) {
        ps3::comic::page_loader::stop();
        g_manga_book.close();
        g_manga_open = false;
    }
    if (!g_manga_book.open(path)) return false;
    if (!ps3::comic::page_loader::start(&g_manga_book)) {
        g_manga_book.close();
        return false;
    }
    std::strncpy(g_manga_path, path, sizeof(g_manga_path) - 1);
    g_manga_path[sizeof(g_manga_path) - 1] = '\0';
    g_manga_db.open(kMangaDbPath);
    g_manga_record = g_manga_db.find_or_add(path);
    g_manga_page = 0;
    if (g_manga_record) {
        g_manga_page = std::max(0, std::min(g_manga_record->last_page, g_manga_book.page_count() - 1));
        g_manga_record->last_read_seq = g_manga_db.max_read_seq() + 1;
        g_manga_db.save();
    }
    g_pages_since_full = 0;
    g_manga_slice = 0;
    g_manga_pending_last_slice = false;
    g_manga_overlay_visible = false;
    g_manga_hint_pages_remaining = 4;
    ps3::comic::page_loader::request(g_manga_page);
    g_manga_open = true;
    return true;
}

void update_manga_progress() {
    if (!g_manga_record) return;
    g_manga_record->last_page = g_manga_page;
    std::snprintf(g_manga_record->last_read_at, sizeof(g_manga_record->last_read_at), "%lld",
                  static_cast<long long>(std::time(nullptr)));
    g_manga_db.save();
}

void render_manga_page(ps3::display::RefreshMode mode) {
    use_manga_orientation();
    g_screen = Screen::MangaReading;
    if (!g_manga_open) {
        render_manga_library(ps3::display::RefreshMode::GC16);
        return;
    }
    const size_t fb_size = static_cast<size_t>(epd_width()) * epd_height() / 2;
    bool cache_hit = false;
    auto consume_current_slice = [&]() {
        ps3::comic::page_loader::set_view(manga_image_fit(), g_manga_slice);
        cache_hit = ps3::comic::page_loader::try_consume(g_manga_page,
                                                         ps3::display::framebuffer(),
                                                         fb_size);
        bool consumed = cache_hit;
        if (!consumed) {
            consumed = ps3::comic::page_loader::fetch_and_consume(g_manga_page,
                                                                  ps3::display::framebuffer(),
                                                                  fb_size);
        }
        g_manga_slice_count = ps3::comic::page_loader::current_slice_count();
        if (g_manga_slice_count < 1) g_manga_slice_count = 1;
        return consumed;
    };

    bool ok = consume_current_slice();
    if (g_manga_pending_last_slice) {
        g_manga_pending_last_slice = false;
        const int last_slice = std::max(0, g_manga_slice_count - 1);
        if (g_manga_slice != last_slice) {
            g_manga_slice = last_slice;
            ok = consume_current_slice();
        }
    } else if (g_manga_slice >= g_manga_slice_count) {
        g_manga_slice = g_manga_slice_count - 1;
    }
    if (!ok) {
        ESP_LOGE(TAG, "manga render failed page=%d/%d fit=%s orientation=%s slice=%d/%d cache=%s",
                 g_manga_page + 1, g_manga_book.page_count(), manga_fit_name(),
                 g_manga_landscape ? "landscape" : "portrait",
                 g_manga_slice + 1, g_manga_slice_count,
                 cache_hit ? "hit" : "fetch");
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "Could not decode this manga page.\n\n"
                      "Diagnostics:\n"
                      "  Page: %d/%d\n"
                      "  Slice: %d/%d\n"
                      "  Fit: %s\n"
                      "  Orientation: %s\n\n"
                      "Try another fit mode, re-export this page, or check serial log tag paperbadge.",
                      g_manga_page + 1, g_manga_book.page_count(),
                      g_manga_slice + 1, g_manga_slice_count,
                      manga_fit_name(),
                      g_manga_landscape ? "landscape" : "portrait");
        g_manga_error_msg = buf;
        g_manga_error_return = Screen::MangaLibrary;
        close_manga_book_if_open();
        render_manga_error(ps3::display::RefreshMode::GC16Full);
        return;
    } else {
        ESP_LOGI(TAG, "manga page %d/%d mode=%s orientation=%s slice=%d/%d cache=%s",
                 g_manga_page + 1, g_manga_book.page_count(), manga_fit_name(),
                 g_manga_landscape ? "landscape" : "portrait",
                 g_manga_slice + 1, g_manga_slice_count,
                 cache_hit ? "hit" : "fetch");
        draw_manga_chrome();
    }
    ps3::display::flush(mode);
    ps3::comic::page_loader::request(g_manga_page);
}

void render_manga_error(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::MangaError;
    ps3::display::clear();
    draw_header("Manga - Cannot Open");
    draw_wrapped(34, 90, ps3::display::width() - 68, g_manga_error_msg);
    draw_footer("Back", nullptr, "Home");
    ps3::display::flush(mode);
}

// ── Reader ────────────────────────────────────────────────────────────
void scan_reader_dir(const std::string& dir, int depth) {
    if (depth > 4) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* de = readdir(d)) {
        if (!de->d_name[0] || de->d_name[0] == '.' || de->d_name[0] == '_') continue;
        std::string path = dir + "/" + de->d_name;
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_reader_dir(path, depth + 1);
        } else if (S_ISREG(st.st_mode) &&
                   (has_suffix_icase(path, ".txt") || has_suffix_icase(path, ".md") || has_suffix_icase(path, ".epub"))) {
            g_reader_books.push_back({path, basename_of(path)});
        }
    }
    closedir(d);
}

void scan_reader_books() {
    g_reader_books.clear();
    scan_reader_dir(kBooksRoot, 0);
    std::sort(g_reader_books.begin(), g_reader_books.end(),
              [](const BookFile& a, const BookFile& b) {
                  return ps3::library::natural_compare(a.name.c_str(), b.name.c_str()) < 0;
              });
}

void save_reader_state() {
    FILE* fp = std::fopen(kReaderStatePath, "w");
    if (!fp) return;
    std::fprintf(fp, "%s\t%d\n", g_reader_path.c_str(), g_reader_page);
    std::fclose(fp);
}

int load_reader_page_for(const std::string& path) {
    FILE* fp = std::fopen(kReaderStatePath, "r");
    if (!fp) return 0;
    int page = 0;
    char line[1200] = {};
    if (std::fgets(line, sizeof(line), fp)) {
        char* tab = std::strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            if (path == line) page = std::atoi(tab + 1);
        }
    }
    std::fclose(fp);
    return page;
}

bool open_reader_book(const BookFile& book, std::string* error_out) {
    const std::string& path = book.path;
    if (path.empty()) {
        if (error_out) *error_out = "Reader selection is empty.";
        return false;
    }
    if (!(has_suffix_icase(path, ".txt") || has_suffix_icase(path, ".md") || has_suffix_icase(path, ".epub"))) {
        if (error_out) {
            *error_out = "Unsupported reader format: " + book.name + "\n\n"
                         "Supported formats: TXT, MD, EPUB (basic text extraction).";
        }
        return false;
    }

    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        if (error_out) {
            *error_out = "Could not stat reader file: " + book.name + "\n\n"
                         "The SD card entry may have changed. Rescan the library.";
        }
        return false;
    }
    if (st.st_size <= 0) {
        if (error_out) *error_out = "File is empty: " + book.name;
        return false;
    }
    const bool is_epub = has_suffix_icase(path, ".epub");
    const long long max_reader_bytes = is_epub ? 16LL * 1024 * 1024
                                               : 4LL * 1024 * 1024;
    if (st.st_size > max_reader_bytes) {
        if (error_out) {
            *error_out = "File too large to open (" +
                         std::to_string(static_cast<long long>(st.st_size / 1024LL)) + " KB).\n\n"
                         "Maximum supported size: " +
                         std::to_string(static_cast<long long>(max_reader_bytes / (1024LL * 1024LL))) +
                         " MB.\n\n"
                         "For oversized EPUB files, remove image-heavy sections or convert selected chapters to TXT.";
        }
        return false;
    }

    std::string text;
    if (is_epub) {
        EpubReadResult epub = read_epub_text(path.c_str());
        if (!epub.ok) {
            if (error_out) {
                *error_out = "Could not open EPUB: " + book.name + "\n\n" +
                             (epub.error.empty()
                                  ? "Unsupported, encrypted, corrupt, or too complex for the basic EPUB parser."
                                  : epub.error) +
                             "\n\nTry converting to TXT format.";
            }
            return false;
        }
        text = std::move(epub.text);
    } else {
        text = read_text_file(path.c_str());
        if (text.empty()) {
            if (error_out) {
                *error_out = "Could not open: " + book.name + "\n\n"
                             "The file may be empty, unreadable, or over the text extraction limit.";
            }
            return false;
        }
    }

    std::vector<std::string> next_lines;
    for (const auto& para : wrap_text(text, ps3::display::width() - 56)) {
        next_lines.push_back(para);
    }
    if (next_lines.empty()) next_lines.push_back("(empty)");
    const int next_lines_per_page = reader_lines_per_page();
    int next_page = load_reader_page_for(path);
    const int next_max_page = std::max(
        0, static_cast<int>((next_lines.size() + next_lines_per_page - 1) /
                            next_lines_per_page) - 1);
    next_page = std::max(0, std::min(next_page, next_max_page));

    g_reader_path = path;
    g_reader_title = basename_of(path);
    g_reader_lines = std::move(next_lines);
    g_reader_lines_per_page = next_lines_per_page;
    g_reader_page = next_page;
    return true;
}

void render_reader_library(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::ReaderLibrary;
    scan_reader_books();
    ps3::display::clear();
    if (g_reader_books.empty()) {
        // Check if SD is likely missing
        struct stat st{};
        const bool sd_ok = stat("/sdcard", &st) == 0;
        if (!sd_ok) {
            draw_header("Reader", "No SD");
            draw_wrapped(34, 130, ps3::display::width() - 68,
                         "No SD card detected.\n\nInsert an SD card with TXT, MD, or EPUB files under:\n/paperBadge/content/books");
            draw_footer("Home", nullptr, nullptr);
            ps3::display::flush(mode);
            return;
        }
    }
    draw_header("Reader", std::to_string(g_reader_books.size()) + " books");
    int y = 90;
    for (int i = 0; i < 10; ++i) g_list_rows[i] = {};
    for (size_t i = 0; i < g_reader_books.size() && i < 9; ++i) {
        Rect r{24, y, ps3::display::width() - 48, 64};
        g_list_rows[i] = r;
        draw_rect(r);
        draw_wrapped(r.x + 14, r.y + 18, r.w - 28, g_reader_books[i].name, 1);
        y += 72;
    }
    if (g_reader_books.empty()) {
        draw_wrapped(34, 130, ps3::display::width() - 68,
                     "No books found.\n\nPut TXT, MD, or EPUB files under:\n/paperBadge/content/books");
    }
    draw_footer("Home", "Scan", nullptr);
    ps3::display::flush(mode);
}

void render_reader_page(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::ReaderReading;
    ps3::display::clear();
    const int total_pages = std::max(1, static_cast<int>((g_reader_lines.size() + g_reader_lines_per_page - 1) /
                                                        g_reader_lines_per_page));
    draw_header(g_reader_title, std::to_string(g_reader_page + 1) + "/" +
                                std::to_string(total_pages) + " " + reader_size_name());
    int y = kToolbarH + 18;
    const int first = g_reader_page * g_reader_lines_per_page;
    for (int i = 0; i < g_reader_lines_per_page && first + i < static_cast<int>(g_reader_lines.size()); ++i) {
        draw_text(28, y, g_reader_lines[first + i]);
        y += active_font().height() + reader_line_gap();
    }
    draw_footer("Library", "Size", "Next");
    ps3::display::flush(mode);
}

void render_reader_error(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::ReaderError;
    ps3::display::clear();
    draw_header("Reader - Cannot Open");
    draw_wrapped(34, 90, ps3::display::width() - 68, g_reader_error_msg);
    draw_footer("Back", nullptr, "Home");
    ps3::display::flush(mode);
}

void show_reader_error(const std::string& message, Screen return_target = Screen::ReaderLibrary) {
    g_reader_error_msg = message;
    g_reader_error_return = return_target;
    ps3::touch::drain();
    render_reader_error();
    ps3::touch::drain();
}

// ── Japanese ──────────────────────────────────────────────────────────
int japanese_item_count() {
    return static_cast<int>(sizeof(kJapaneseItems) / sizeof(kJapaneseItems[0]));
}

bool japanese_sd_registry_available() {
    struct stat st{};
    return stat((std::string(kJapaneseRoot) + "/source_registry.json").c_str(), &st) == 0 &&
           S_ISREG(st.st_mode);
}

void reset_japanese_mock() {
    for (int i = 0; i < japanese_item_count(); ++i) g_jp_mock_answers[i] = -1;
    g_jp_mock_index = 0;
    g_jp_mock_selected = -1;
}

void render_japanese_menu(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseMenu;
    ps3::display::clear();
    draw_header("日本語", "Study");
    const char* labels[] = {"Practice", "Mock Test", "Reference", "Results"};
    for (int i = 0; i < 4; ++i) {
        g_jp_menu_buttons[i] = {34, 112 + i * 100, ps3::display::width() - 68, 78};
        draw_button(g_jp_menu_buttons[i], labels[i]);
    }
    draw_wrapped(34, 540, ps3::display::width() - 68,
                 "Embedded sample source is active. SD JSON-LD sources can be added later without new proprietary content.");
    draw_footer("Home", nullptr, nullptr);
    ps3::display::flush(mode);
}

void render_japanese_source(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseSource;
    ps3::display::clear();
    draw_header("Source Select", "1 source");
    g_jp_menu_buttons[0] = {34, 130, ps3::display::width() - 68, 88};
    draw_button(g_jp_menu_buttons[0], "Embedded N3 Samples");
    draw_wrapped(34, 250, ps3::display::width() - 68,
                 japanese_sd_registry_available()
                     ? "SD source_registry.json detected. Embedded samples remain available as fallback."
                     : "No SD source_registry.json yet. Embedded samples are active; external sources can use JSON-LD sidecars later.");
    draw_footer("Back", nullptr, nullptr);
    ps3::display::flush(mode);
}

void render_japanese_unit(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseUnit;
    ps3::display::clear();
    draw_header("Week / Unit", "Embedded");
    g_jp_menu_buttons[0] = {34, 130, ps3::display::width() - 68, 88};
    draw_button(g_jp_menu_buttons[0], "N3 Sample - Week 1");
    draw_footer("Back", nullptr, nullptr);
    ps3::display::flush(mode);
}

void render_japanese_lesson(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseLesson;
    ps3::display::clear();
    draw_header("Day / Lesson", "W1");
    g_jp_menu_buttons[0] = {34, 130, ps3::display::width() - 68, 88};
    draw_button(g_jp_menu_buttons[0], "Day 1 - Mixed Practice");
    draw_footer("Back", nullptr, nullptr);
    ps3::display::flush(mode);
}

void render_japanese(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::Japanese;
    const auto& item = kJapaneseItems[g_jp_index];
    ps3::display::clear();
    draw_header("日本語", std::string("Q") + std::to_string(g_jp_index + 1) + "/" +
                         std::to_string(sizeof(kJapaneseItems) / sizeof(kJapaneseItems[0])) +
                         " " + size_level_name(g_japanese_font_level));
    int y = 92;
    y = draw_wrapped_gap(30, y, ps3::display::width() - 60, item.prompt, 5, japanese_line_gap()) + 10;
    const int choice_h = japanese_choice_height();
    for (int i = 0; i < 4; ++i) {
        g_jp_choices[i] = {30, y, ps3::display::width() - 60, choice_h};
        std::string label;
        label.push_back(static_cast<char>('A' + i));
        label += ". ";
        label += item.choices[i];
        draw_button(g_jp_choices[i], label);
        y += choice_h + 12;
    }
    draw_footer(g_jp_index > 0 ? "Prev" : "Back", "Font", nullptr);
    ps3::display::flush(mode);
}

bool japanese_feedback_single_page(const JapaneseItem& item) {
    const int available = ps3::display::height() - kToolbarH - kFooterH - 40;
    int h = active_font().height() + 18;
    const int gap = japanese_line_gap();
    h += static_cast<int>(wrap_text(item.answer, ps3::display::width() - 60).size()) * (active_font().height() + gap) + 14;
    h += static_cast<int>(wrap_text(item.explanation, ps3::display::width() - 60).size()) * (active_font().height() + gap) + 14;
    h += static_cast<int>(wrap_text(item.english, ps3::display::width() - 60).size()) * (active_font().height() + gap) + 14;
    return h <= available;
}

void render_japanese_feedback(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseFeedback;
    const auto& item = kJapaneseItems[g_jp_index];
    g_jp_feedback_single = japanese_feedback_single_page(item);
    if (g_jp_feedback_single && g_jp_feedback_page > 0) g_jp_feedback_page = 0;
    ps3::display::clear();
    draw_header("日本語 feedback", std::string(font_face_name()) + " " + size_level_name(g_japanese_font_level));
    int y = 92;
    const bool correct = g_jp_selected == item.correct;
    draw_text(30, y, correct ? "Correct" : "Wrong");
    y += active_font().height() + japanese_line_gap() + 10;
    std::string answer = "Answer: ";
    answer.push_back(static_cast<char>('A' + item.correct));
    answer += ". ";
    answer += item.choices[item.correct];
    y = draw_wrapped_gap(30, y, ps3::display::width() - 60, answer, 3, japanese_line_gap()) + 12;
    if (g_jp_feedback_page == 0) {
        y = draw_wrapped_gap(30, y, ps3::display::width() - 60, item.answer, 4, japanese_line_gap()) + 12;
        y = draw_wrapped_gap(30, y, ps3::display::width() - 60, item.explanation,
                             g_jp_feedback_single ? 8 : 4, japanese_line_gap()) + 12;
        if (g_jp_feedback_single) {
            draw_wrapped_gap(30, y, ps3::display::width() - 60, item.english, 4, japanese_line_gap());
        }
    } else {
        y = draw_wrapped_gap(30, y, ps3::display::width() - 60, item.explanation, 8, japanese_line_gap()) + 12;
        draw_wrapped_gap(30, y, ps3::display::width() - 60, item.english, 8, japanese_line_gap());
    }
    draw_footer(g_jp_index > 0 ? "Prev" : "Back", nullptr,
                g_jp_feedback_single ? (g_jp_index + 1 < static_cast<int>(sizeof(kJapaneseItems) / sizeof(kJapaneseItems[0])) ? "Next" : nullptr)
                                     : (g_jp_feedback_page == 0 ? "More" : "Next"));
    ps3::display::flush(mode);
}

void render_japanese_mock(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseMock;
    const auto& item = kJapaneseItems[g_jp_mock_index];
    ps3::display::clear();
    draw_header("Mock Test",
                std::to_string(g_jp_mock_index + 1) + "/" +
                std::to_string(japanese_item_count()) + " " +
                size_level_name(g_japanese_font_level));
    int y = 92;
    y = draw_wrapped_gap(30, y, ps3::display::width() - 60, item.prompt, 5, japanese_line_gap()) + 10;
    const int choice_h = japanese_choice_height();
    for (int i = 0; i < 4; ++i) {
        g_jp_choices[i] = {30, y, ps3::display::width() - 60, choice_h};
        std::string label;
        label.push_back(static_cast<char>('A' + i));
        label += ". ";
        label += item.choices[i];
        draw_button(g_jp_choices[i], label, g_jp_mock_selected == i);
        y += choice_h + 12;
    }
    draw_footer(g_jp_mock_index > 0 ? "Prev" : "Back", nullptr,
                g_jp_mock_selected >= 0 ? (g_jp_mock_index + 1 < japanese_item_count() ? "Next" : "Score") : nullptr);
    ps3::display::flush(mode);
}

void render_japanese_mock_results(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseMockResults;
    int score = 0;
    for (int i = 0; i < japanese_item_count(); ++i) {
        if (g_jp_mock_answers[i] == kJapaneseItems[i].correct) ++score;
    }
    ps3::display::clear();
    draw_header("Mock Results", std::to_string(score) + "/" + std::to_string(japanese_item_count()));
    int y = 100;
    y = draw_wrapped(34, y, ps3::display::width() - 68,
                     "Score: " + std::to_string(score) + "/" + std::to_string(japanese_item_count()), 2) + 16;
    for (int i = 0; i < japanese_item_count(); ++i) {
        std::string row = std::string("Q") + std::to_string(i + 1) + ": ";
        row += (g_jp_mock_answers[i] == kJapaneseItems[i].correct) ? "Correct" : "Review";
        y = draw_wrapped(34, y, ps3::display::width() - 68, row, 1) + 4;
    }
    draw_footer("Menu", "Review", nullptr);
    ps3::display::flush(mode);
}

void render_japanese_reference(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseReference;
    ps3::display::clear();
    draw_header("Reference", "Embedded");
    const char* rows[] = {
        "Grammar: ものだ - nostalgic past habit / memory",
        "Vocabulary: 郵便局, 荷物",
        "Kanji: 郵, 便, 局, 荷, 物",
        "Reading: sample questions link concepts to practice items",
    };
    int y = 96;
    for (const char* row : rows) {
        y = draw_wrapped(34, y, ps3::display::width() - 68, row, 3) + 16;
    }
    draw_footer("Back", nullptr, nullptr);
    ps3::display::flush(mode);
}

void render_japanese_results(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseResults;
    ps3::display::clear();
    draw_header("日本語 Results", "Session");
    int answered = 0;
    int correct = 0;
    for (int i = 0; i < japanese_item_count(); ++i) {
        if (g_jp_mock_answers[i] >= 0) {
            ++answered;
            if (g_jp_mock_answers[i] == kJapaneseItems[i].correct) ++correct;
        }
    }
    draw_wrapped(34, 104, ps3::display::width() - 68,
                 std::string("Mock answers: ") + std::to_string(answered) + "\n" +
                 "Mock correct: " + std::to_string(correct) + "\n\n" +
                 "Progress model: source/item/concept review events are scaffolded for SD NDJSON in the schema docs.");
    draw_footer("Back", nullptr, nullptr);
    ps3::display::flush(mode);
}

void render_japanese_font(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::JapaneseFont;
    ps3::display::clear();
    const int page_count = font_lab_page_count();
    g_font_lab_page = std::max(0, std::min(g_font_lab_page, page_count - 1));
    draw_header("Font Lab", std::string("Page ") + std::to_string(g_font_lab_page + 1) + "/" +
                            std::to_string(page_count));
    int y = 86;
    if (g_font_lab_page == 0) {
        y = draw_wrapped(34, y, ps3::display::width() - 68,
                         std::string("Selected runtime JP: ") + font_face_name() + "\n\n" +
                         "Production runtime fonts: BIZ UDGothic and IPAex Gothic.\n\n"
                         "Font Lab candidates are QA-only fixed bitmap previews. They are sparse subsets generated on Mac from outline fonts; runtime variable TTF rendering is not used on ESP32.\n\n"
                         "Candidate pages show 24 px native bitmap samples and 48 px 2x scaled samples. Other fixed sizes need regenerated firmware assets.", 0) + 12;
        draw_wrapped(34, y, ps3::display::width() - 68,
                     "Inter and Source Serif 4 are Latin-only in this sparse subset, so their pages do not show fake Japanese box rows.");
    } else if (g_font_lab_page == 1) {
        y = draw_wrapped(34, y, ps3::display::width() - 68,
                         "Production runtime fonts only\n\n"
                         "These are the faces used by Settings -> Fonts today. Font Lab candidates do not change Reader, Interview, Japanese, Badge, or Manga text.", 0) + 16;
        draw_text(34, y, "BIZ UDGothic");
        y += active_font().height() + 10;
        draw_text_font(50, y, "郵便局で荷物を送ります。", g_biz_font);
        y += g_biz_font.height() + 18;
        draw_text_font(50, y, "EN: Post office / JP: 郵便局", g_biz_font);
        y += g_biz_font.height() + 34;
        draw_text(34, y, "IPAex Gothic");
        y += active_font().height() + 10;
        draw_text_font(50, y, "郵便局で荷物を送ります。", g_ipa_font);
        y += g_ipa_font.height() + 18;
        draw_text_font(50, y, "日本語の読みやすさ ABC123", g_ipa_font);
    } else if (g_font_lab_page == 2) {
        y = draw_wrapped(34, y, ps3::display::width() - 68,
                         "Candidate index\nTap Next for one spacious page per face.", 0) + 8;
        for (int i = 0; i < ps3::font::kFontLabFaceCount; ++i) {
            const auto& face = ps3::font::kFontLabFaces[i];
            y = draw_wrapped(46, y, ps3::display::width() - 92,
                             std::to_string(i + 1) + ". " + font_lab_face_status(face), 2) + 4;
        }
    } else {
        const int face_idx = g_font_lab_page - 3;
        if (!bind_font_lab_face(face_idx)) {
            draw_wrapped(34, y, ps3::display::width() - 68, "Font Lab face failed to bind.");
        } else {
            const auto& face = ps3::font::kFontLabFaces[face_idx];
            const bool jp = font_lab_face_supports_japanese(face);
            y = draw_wrapped(34, y, ps3::display::width() - 68,
                             font_lab_face_status(face) + "\n" +
                             std::string(face.source) + " / " + face.license + "\n" +
                             (jp ? "Coverage: Latin + sparse Japanese QA glyphs"
                                 : "Coverage: Latin only; Japanese not included in this subset"),
                             0) + 14;
            y = draw_font_lab_sample_box(34, y, ps3::display::width() - 68,
                                         "24 px native Latin",
                                         "Daniel 12345", 1, 4);
            if (jp) {
                y = draw_font_lab_sample_box(34, y, ps3::display::width() - 68,
                                             "24 px native Japanese",
                                             "郵便局 荷物 500問", 1, 5);
                y = draw_font_lab_sample_box(34, y, ps3::display::width() - 68,
                                             "48 px scaled Japanese",
                                             "郵便局", 2, 8);
            } else {
                y = draw_font_lab_sample_box(34, y, ps3::display::width() - 68,
                                             "24 px native numbers",
                                             "0123456789", 1, 4);
                y = draw_font_lab_sample_box(34, y, ps3::display::width() - 68,
                                             "48 px scaled Latin",
                                             "Inter 48", 2, 8);
                draw_wrapped(34, y, ps3::display::width() - 68,
                             "Japanese: not included in this sparse subset.", 0);
            }
        }
    }
    draw_footer("Back", "Switch", "Next");
    ps3::display::flush(mode);
}

// ── Settings ──────────────────────────────────────────────────────────
void render_settings(ps3::display::RefreshMode mode) {
    restore_app_orientation();
    g_screen = Screen::Settings;
    ps3::display::clear();
    const char* page_title = "Settings";
    if (g_settings_page == 1) page_title = "Fonts";
    else if (g_settings_page == 2) page_title = "Reader";
    else if (g_settings_page == 3) page_title = "Manga";
    else if (g_settings_page == 4) page_title = "Power";
    else if (g_settings_page == 5) page_title = "Refresh";
    draw_header(page_title, g_settings_page == 0 ? "Menu" : "Settings");
    const int mv = ps3::battery::voltage_mv();
    const int pct = ps3::battery::level_pct();
    for (int i = 0; i < 6; ++i) g_settings_buttons[i] = {};
    int y = 88;
    if (g_settings_page == 0) {
        draw_wrapped(30, y, ps3::display::width() - 60,
                     "Battery: " + std::to_string(mv) + " mV  " + std::to_string(pct) + "%  " +
                         (ps3::battery::is_charging() ? "charging" : "battery"), 2);
        y = 160;
        const char* labels[] = {"Fonts", "Reader", "Manga", "Power", "Refresh"};
        for (int i = 0; i < 5; ++i) {
            g_settings_buttons[i] = {34, y + i * 86, ps3::display::width() - 68, 68};
            draw_button(g_settings_buttons[i], labels[i]);
        }
    } else {
        std::string labels[6];
        int count = 0;
        if (g_settings_page == 1) {
            labels[count++] = "Font Lab";
            labels[count++] = std::string("Lab preview: ") + western_profile_name();
            labels[count++] = std::string("JP font: ") + font_face_name();
            labels[count++] = std::string("Interview size: ") + size_level_name(g_interview_font_level);
            labels[count++] = std::string("Japanese size: ") + size_level_name(g_japanese_font_level);
        } else if (g_settings_page == 2) {
            labels[count++] = std::string("Reader size: ") + reader_size_name();
        } else if (g_settings_page == 3) {
            labels[count++] = std::string("Fit: ") + manga_fit_name();
            labels[count++] = std::string("Orientation: ") + (g_manga_landscape ? "Landscape" : "Portrait");
        } else if (g_settings_page == 4) {
            labels[count++] = "Sleep now";
            labels[count++] = "Power off";
            labels[count++] = "Sleep timeout: " + std::to_string(g_sleep_minutes) + "m";
        } else if (g_settings_page == 5) {
            labels[count++] = std::string("Profile: ") + refresh_profile_name();
            labels[count++] = "Clean every: " + std::to_string(ps3::settings::state().full_refresh_pages);
            labels[count++] = "Clean refresh";
        }
        labels[count++] = "Back";
        for (int i = 0; i < count && i < 6; ++i) {
            g_settings_buttons[i] = {34, y + i * 84, ps3::display::width() - 68, 66};
            draw_button(g_settings_buttons[i], labels[i]);
        }
    }
    draw_footer("Home", nullptr, nullptr);
    ps3::display::flush(mode);
}

bool settings_back_button(int idx) {
    if (g_settings_page == 0) return false;
    int back_idx = 0;
    if (g_settings_page == 1) back_idx = 5;
    else if (g_settings_page == 2) back_idx = 1;
    else if (g_settings_page == 3) back_idx = 2;
    else if (g_settings_page == 4) back_idx = 3;
    else if (g_settings_page == 5) back_idx = 3;
    return idx == back_idx;
}

void handle_settings(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        g_settings_page = 0;
        navigate_home();
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (!g_settings_buttons[i].contains(x, y)) continue;
        if (settings_back_button(i)) {
            g_settings_page = 0;
            render_settings(ps3::display::RefreshMode::GC16Full);
            return;
        }
        if (g_settings_page == 0) {
            g_settings_page = i + 1;
            render_settings(ps3::display::RefreshMode::GC16Full);
            return;
        }
        if (g_settings_page == 1) {
            if (i == 0) {
                nav_push(Screen::Settings);
                render_japanese_font();
            } else if (i == 1) {
                auto& s = ps3::settings::state();
                s.western_font_profile = (s.western_font_profile + 1) % 2;
                ps3::settings::save();
                render_settings(ps3::display::RefreshMode::GL16);
            } else if (i == 2) {
                select_japanese_font(g_jp_font == JapaneseFontFace::BizUdGothic ? JapaneseFontFace::IpaCurrent : JapaneseFontFace::BizUdGothic);
                persist_font_settings();
                render_settings(ps3::display::RefreshMode::GC16Full);
            } else if (i == 3) {
                g_interview_font_level = (g_interview_font_level + 1) % 4;
                persist_font_settings();
                render_settings(ps3::display::RefreshMode::GL16);
            } else if (i == 4) {
                g_japanese_font_level = (g_japanese_font_level + 1) % 4;
                persist_font_settings();
                render_settings(ps3::display::RefreshMode::GL16);
            }
        } else if (g_settings_page == 2) {
            if (i == 0) {
                g_reader_font_level = (g_reader_font_level + 1) % 4;
                reflow_reader_pages_preserving_line();
                persist_font_settings();
                render_settings(ps3::display::RefreshMode::GL16);
            }
        } else if (g_settings_page == 3) {
            if (i == 0) {
                cycle_manga_fit_mode();
                render_settings(ps3::display::RefreshMode::GL16);
            } else if (i == 1) {
                toggle_manga_orientation();
                render_settings(ps3::display::RefreshMode::GL16);
            }
        } else if (g_settings_page == 4) {
            if (i == 0) {
                enter_light_sleep("settings");
            } else if (i == 1) {
                enter_deep_sleep();
            } else if (i == 2) {
                g_sleep_minutes = g_sleep_minutes == 0 ? 5 : (g_sleep_minutes == 5 ? 10 : (g_sleep_minutes == 10 ? 15 : 0));
                render_settings(ps3::display::RefreshMode::GL16);
            }
        } else if (g_settings_page == 5) {
            if (i == 0) {
                cycle_refresh_profile();
                render_settings(ps3::display::RefreshMode::GC16Full);
            } else if (i == 1) {
                cycle_full_refresh_pages();
                render_settings(ps3::display::RefreshMode::GL16);
            } else if (i == 2) {
                ps3::display::flush(ps3::display::RefreshMode::GC16Full);
            }
        }
        return;
    }
}
// ── Sleep / power ─────────────────────────────────────────────────────
void enter_deep_sleep() {
    save_session();
    // Draw badge as the persistent e-ink final frame
    draw_english_badge_final_frame();
    const int mv = ps3::battery::voltage_mv();
    ps3::battery::save_deep_entry(std::time(nullptr), mv);
    ps3::battery::log_event("deep");
    if (g_manga_open) {
        ps3::comic::page_loader::stop();
        g_manga_book.close();
    }
    epd_poweroff();
    ps3::sd::unmount();
    esp_deep_sleep_start();
    while (true) vTaskDelay(portMAX_DELAY);
}

void enter_light_sleep(const char* trigger) {
    ESP_LOGI(TAG, "light sleep: %s", trigger);
    save_session();
    // Draw badge as the persistent e-ink sleep frame
    draw_english_badge_final_frame();
    ps3::battery::log_event("light");
    const std::time_t t0 = std::time(nullptr);
    uint64_t timeout_us = 0;
    if (g_power_off_minutes > 0) timeout_us = static_cast<uint64_t>(g_power_off_minutes) * 60ULL * 1000000ULL;
    const auto reason = ps3::touch::light_sleep_until_touch_or_timeout(timeout_us);
    vTaskDelay(pdMS_TO_TICKS(120));
    ps3::touch::drain();
    const int64_t dur = static_cast<int64_t>(std::time(nullptr) - t0);
    ps3::battery::log_event("wake", dur > 0 ? dur : 0);
    mark_activity();
    if (reason == ps3::touch::WakeReason::Timer) {
        enter_deep_sleep();
    }
    render_current(ps3::display::RefreshMode::GC16Full);
}

// ── Touch handlers ────────────────────────────────────────────────────
void handle_home(int x, int y) {
    if (g_home_buttons[0].contains(x, y)) {
        nav_push(Screen::Home);
        render_badge();
    } else if (g_home_buttons[1].contains(x, y)) {
        nav_push(Screen::Home);
        render_interview();
    }
    else if (g_home_buttons[2].contains(x, y)) {
        nav_push(Screen::Home);
        g_jp_index = 0;
        g_jp_selected = -1;
        render_japanese_menu();
    } else if (g_home_buttons[3].contains(x, y)) {
        nav_push(Screen::Home);
        render_manga_library();
    }
    else if (g_home_buttons[4].contains(x, y)) {
        nav_push(Screen::Home);
        render_reader_library();
    }
    else if (g_home_buttons[5].contains(x, y)) {
        nav_push(Screen::Home);
        render_settings();
    }
}

void handle_badge(int x, int y) {
    if (g_badge_qr_zoom) {
        g_badge_qr_zoom = false;
        render_badge();
        ps3::touch::drain();
        return;
    }
    if (y < kToolbarH + 20) {
        render_home();
        ps3::touch::drain();
        return;
    }
    if (g_badge_qr_rect.contains(x, y)) {
        g_badge_qr_zoom = true;
        render_badge();
        ps3::touch::drain();
        return;
    }
    g_badge_japanese = !g_badge_japanese;
    render_badge();
    ps3::touch::drain();
}

void handle_interview_menu(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_home();
        return;
    }
    if (g_iv_menu_buttons[0].contains(x, y)) {
        nav_push(Screen::Interview);
        render_interview_practice_menu();
    } else if (g_iv_menu_buttons[1].contains(x, y)) {
        nav_push(Screen::Interview);
        g_iv_drill_idx = 0;
        g_iv_drill_answer = -1;
        g_iv_in_exam = false;
        render_interview_drill_q();
    } else if (g_iv_menu_buttons[2].contains(x, y)) {
        // Exam mode
        nav_push(Screen::Interview);
        iv_build_exam();
        g_iv_in_exam = true;
        if (g_iv_exam_count > 0) {
            g_iv_drill_answer = -1;
            // Show first exam question
            g_iv_drill_idx = g_iv_exam_pool[0]; // not used directly by drill_q in exam mode
            render_interview_drill_q();
        } else {
            render_interview();
        }
    } else if (g_iv_menu_buttons[3].contains(x, y)) {
        nav_push(Screen::Interview);
        g_iv_gloss_category_idx = -1;
        g_iv_gloss_list_page = 0;
        render_interview_glossary_list();
    } else if (g_iv_menu_buttons[4].contains(x, y)) {
        nav_push(Screen::Interview);
        render_interview_results();
    }
}

void handle_interview_practice_menu(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_back();
        return;
    }
    if (g_iv_practice_buttons[0].contains(x, y)) {
        g_iv_practice_mode = InterviewPracticeMode::All;
        g_iv_practice_section_idx = -1;
        g_iv_card_list_page = 0;
        nav_push(Screen::InterviewPracticeMenu);
        render_interview_card_list();
    } else if (g_iv_practice_buttons[1].contains(x, y)) {
        g_iv_practice_mode = InterviewPracticeMode::MustMaster;
        g_iv_practice_section_idx = -1;
        g_iv_card_list_page = 0;
        nav_push(Screen::InterviewPracticeMenu);
        render_interview_card_list();
    } else if (g_iv_practice_buttons[2].contains(x, y)) {
        g_iv_practice_mode = InterviewPracticeMode::Category;
        g_iv_practice_section_idx = -1;
        g_iv_card_list_page = 0;
        nav_push(Screen::InterviewPracticeMenu);
        render_interview_card_list();
    } else if (g_iv_practice_buttons[3].contains(x, y)) {
        g_iv_card_spoken = false;
        g_iv_answer_page = 0;
        nav_push(Screen::InterviewPracticeMenu);
        render_interview_practice();
    }
}

void handle_interview_card_list(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_iv_practice_mode == InterviewPracticeMode::Category &&
            g_iv_practice_section_idx >= 0) {
            g_iv_practice_section_idx = -1;
            g_iv_card_list_page = 0;
            render_interview_card_list();
        } else {
            navigate_back();
        }
        return;
    }
    if (g_footer_mid.contains(x, y) && g_iv_card_list_page > 0) {
        --g_iv_card_list_page;
        render_interview_card_list(ps3::display::RefreshMode::GL16);
        return;
    }
    const auto cards = iv_filtered_cards();
    const int page_size = 8;
    const int page_count = std::max(1, static_cast<int>((cards.size() + page_size - 1) / page_size));
    if (g_footer_right.contains(x, y) && g_iv_card_list_page + 1 < page_count) {
        ++g_iv_card_list_page;
        render_interview_card_list(ps3::display::RefreshMode::GL16);
        return;
    }

    if (g_iv_practice_mode == InterviewPracticeMode::Category &&
        g_iv_practice_section_idx < 0) {
        const auto sections = iv_section_first_indices();
        for (int i = 0; i < static_cast<int>(sections.size()) && i < 9; ++i) {
            if (!g_list_rows[i].contains(x, y)) continue;
            g_iv_practice_section_idx = i;
            g_iv_card_list_page = 0;
            render_interview_card_list();
            return;
        }
        return;
    }

    const int start = g_iv_card_list_page * page_size;
    for (int row = 0; row < page_size && start + row < static_cast<int>(cards.size()); ++row) {
        if (!g_list_rows[row].contains(x, y)) continue;
        g_iv_card_idx = cards[start + row];
        g_iv_card_spoken = false;
        g_iv_answer_page = 0;
        nav_push(Screen::InterviewCardList);
        render_interview_practice();
        return;
    }
}

void handle_interview_practice(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        const int prev = iv_next_filtered_card(g_iv_card_idx, -1);
        if (prev >= 0) {
            g_iv_card_idx = prev;
            g_iv_card_spoken = false;
            g_iv_answer_page = 0;
            render_interview_practice(refresh_reveal_mode());
        } else {
            navigate_back();
        }
    } else if (g_footer_mid.contains(x, y)) {
        if (!g_iv_card_spoken) {
            g_iv_card_spoken = true;
            g_iv_answer_page = 0;
            ++g_iv_session_practice;
        } else {
            const int page_count = iv_answer_page_count(embedded_papercoach::kCards[g_iv_card_idx]);
            if (g_iv_answer_page + 1 < page_count) {
                ++g_iv_answer_page;
            } else {
                g_iv_card_spoken = false;
                g_iv_answer_page = 0;
            }
        }
        render_interview_practice(refresh_reveal_mode());
    } else if (g_footer_right.contains(x, y)) {
        const int next = iv_next_filtered_card(g_iv_card_idx, +1);
        if (next >= 0) {
            g_iv_card_idx = next;
            g_iv_card_spoken = false;
            g_iv_answer_page = 0;
            render_interview_practice(refresh_reveal_mode());
        }
    }
}

void handle_interview_drill_q(int x, int y) {
    // Determine which drill we're showing
    const int drill_idx = g_iv_in_exam ? g_iv_exam_pool[g_iv_exam_current] : g_iv_drill_idx;
    const auto& d = embedded_papercoach::kDrills[drill_idx];

    if (g_footer_left.contains(x, y)) {
        g_iv_in_exam = false;
        g_iv_drill_answer = -1;
        render_interview();
        return;
    }
    if (g_footer_right.contains(x, y) && g_iv_drill_answer >= 0) {
        // Submit
        const bool correct = g_iv_drill_answer == d.correctIndex;
        if (g_iv_in_exam) {
            g_iv_exam_answers[g_iv_exam_current] = correct;
            if (correct) ++g_iv_exam_score;
            ++g_iv_session_total;
            if (correct) ++g_iv_session_correct;
            ++g_iv_exam_current;
            g_iv_drill_answer = -1;
            if (g_iv_exam_current >= g_iv_exam_count) {
                g_iv_in_exam = false;
                render_interview_results();
            } else {
                g_iv_drill_idx = g_iv_exam_pool[g_iv_exam_current];
                render_interview_drill_q();
            }
            return;
        }
        ++g_iv_session_total;
        if (correct) ++g_iv_session_correct;
        render_interview_drill_fb();
        return;
    }
    // Option tap
    for (int i = 0; i < d.optionCount && i < 4; ++i) {
        if (g_iv_choices[i].contains(x, y)) {
            g_iv_drill_answer = i;
            render_interview_drill_q(ps3::display::RefreshMode::GL16);
            return;
        }
    }
}

void handle_interview_drill_fb(int x, int y) {
    if (g_footer_left.contains(x, y) || g_footer_mid.contains(x, y)) {
        g_iv_in_exam = false;
        g_iv_drill_answer = -1;
        render_interview();
        return;
    }
    if (g_footer_right.contains(x, y)) {
        if (g_iv_in_exam) {
            ++g_iv_exam_current;
            if (g_iv_exam_current >= g_iv_exam_count) {
                g_iv_in_exam = false;
                render_interview_results();
            } else {
                // Advance drill index to next exam question
                g_iv_drill_idx = g_iv_exam_pool[g_iv_exam_current];
                g_iv_drill_answer = -1;
                render_interview_drill_q();
            }
        } else {
            // Next MCQ drill
            int next = g_iv_drill_idx + 1;
            while (next < static_cast<int>(embedded_papercoach::kDrillCount) && !iv_drill_is_mcq(next)) {
                ++next;
            }
            if (next < static_cast<int>(embedded_papercoach::kDrillCount)) {
                g_iv_drill_idx = next;
                g_iv_drill_answer = -1;
                render_interview_drill_q();
            } else {
                render_interview();
            }
        }
    }
}

void handle_interview_glossary_list(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_iv_gloss_category_idx >= 0) {
            g_iv_gloss_category_idx = -1;
            g_iv_gloss_list_page = 0;
            render_interview_glossary_list();
        } else {
            navigate_back();
        }
        return;
    }
    if (g_iv_gloss_category_idx < 0) {
        const auto cats = iv_glossary_categories();
        for (int i = 0; i < static_cast<int>(cats.size()) && i < 9; ++i) {
            if (!g_list_rows[i].contains(x, y)) continue;
            g_iv_gloss_category_idx = i;
            g_iv_gloss_list_page = 0;
            render_interview_glossary_list();
            return;
        }
        return;
    }

    const auto terms = iv_glossary_terms_for_category(g_iv_gloss_category_idx);
    const int page_size = 8;
    const int page_count = std::max(1, static_cast<int>((terms.size() + page_size - 1) / page_size));
    if (g_footer_mid.contains(x, y) && g_iv_gloss_list_page > 0) {
        --g_iv_gloss_list_page;
        render_interview_glossary_list(ps3::display::RefreshMode::GL16);
        return;
    }
    if (g_footer_right.contains(x, y) && g_iv_gloss_list_page + 1 < page_count) {
        ++g_iv_gloss_list_page;
        render_interview_glossary_list(ps3::display::RefreshMode::GL16);
        return;
    }
    const int start = g_iv_gloss_list_page * page_size;
    for (int row = 0; row < page_size && start + row < static_cast<int>(terms.size()); ++row) {
        if (!g_list_rows[row].contains(x, y)) continue;
        g_iv_gloss_idx = terms[start + row];
        nav_push(Screen::InterviewGlossaryList);
        render_interview_glossary();
        return;
    }
}

void handle_interview_glossary(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_iv_gloss_idx > 0) {
            --g_iv_gloss_idx;
            render_interview_glossary(ps3::display::RefreshMode::GL16);
        } else {
            navigate_back();
        }
    } else if (g_footer_mid.contains(x, y)) {
        navigate_back();
    } else if (g_footer_right.contains(x, y)) {
        if (g_iv_gloss_idx + 1 < static_cast<int>(embedded_papercoach::kGlossaryCount)) {
            ++g_iv_gloss_idx;
            render_interview_glossary(ps3::display::RefreshMode::GL16);
        }
    }
}

void handle_interview_results(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        render_interview();
    } else if (g_footer_mid.contains(x, y)) {
        // Reset session stats
        g_iv_session_correct = 0;
        g_iv_session_total = 0;
        g_iv_session_practice = 0;
        g_iv_exam_count = 0;
        render_interview_results(ps3::display::RefreshMode::GL16);
    } else if (g_footer_right.contains(x, y)) {
        render_home();
    }
}

void handle_manga_library(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_manga_library.can_go_up()) {
            g_manga_library.go_up();
            render_manga_library();
        } else {
            render_home();
        }
        return;
    }
    if (g_footer_mid.contains(x, y) && g_manga_library.page_prev()) {
        render_manga_library(ps3::display::RefreshMode::GL16);
        return;
    }
    if (g_footer_right.contains(x, y) && g_manga_library.page_next()) {
        render_manga_library(ps3::display::RefreshMode::GL16);
        return;
    }
    for (int i = 0; i < g_manga_library.count(); ++i) {
        if (!g_list_rows[i].contains(x, y)) continue;
        const auto& e = g_manga_library.at(i);
        if (e.kind == ps3::library::EntryKind::Folder) {
            g_manga_library.enter(i);
            render_manga_library();
            return;
        }
        char path[ps3::library::MAX_PATH_LEN] = {};
        if (!g_manga_library.full_path_of(i, path, sizeof(path))) return;

        // CBR/RAR: not supported
        if (has_suffix_icase(e.name, ".cbr") || has_suffix_icase(e.name, ".rar")) {
            g_manga_error_msg =
                "CBR/RAR archives are not supported.\n\n"
                "Convert to CBZ format and copy the CBZ file to:\n/paperBadge/content/manga";
            g_manga_error_return = Screen::MangaLibrary;
            ps3::touch::drain();
            render_manga_error();
            ps3::touch::drain();
            return;
        }

        struct stat st{};
        long long file_size = 0;
        if (stat(path, &st) == 0) file_size = static_cast<long long>(st.st_size);
        const bool zip64_markers = archive_has_zip64_markers(path);
        if (zip64_markers) {
            char buf[512];
            const long long mb = file_size / (1024LL * 1024LL);
            std::snprintf(buf, sizeof(buf),
                "ZIP64 archive not supported (%lld MB).\n\n"
                "This file contains ZIP64 central-directory markers.\n"
                "Firmware miniz cannot parse ZIP64 safely.\n\n"
                "Diagnostics:\n"
                "  File size:   %lld MB\n"
                "  ZIP64:       yes\n\n"
                "Re-archive without ZIP64 or split with tools/manga_preprocess.py.",
                mb, mb);
            g_manga_error_msg = buf;
            g_manga_error_return = Screen::MangaLibrary;
            ps3::touch::drain();
            render_manga_error();
            ps3::touch::drain();
            return;
        }

        // Try to open
        if (!open_manga_book(path)) {
            // Best-effort diagnosis
            std::string diag =
                "Could not open this archive.\n\n"
                "Diagnostics:\n"
                "  File: " + std::string(e.name) + "\n";
            if (file_size > 0) {
                char sz[64];
                std::snprintf(sz, sizeof(sz), "  Size: %lld KB\n", file_size / 1024LL);
                diag += sz;
            }
            diag += std::string("  ZIP64 markers: ") + (zip64_markers ? "yes\n" : "not found\n");
            diag +=
                "\nPossible causes:\n"
                "  - Archive contains only WebP or unsupported images\n"
                "  - Corrupted or truncated archive\n"
                "  - Unsupported compression method (requires Deflate)\n"
                "  - Insufficient PSRAM for page index\n"
                "\nCheck serial log (cbz: tag) for details.";
            g_manga_error_msg = diag;
            g_manga_error_return = Screen::MangaLibrary;
            ps3::touch::drain();
            render_manga_error();
            ps3::touch::drain();
        } else {
            nav_push(Screen::MangaLibrary);
            render_manga_page(ps3::display::RefreshMode::GC16Full);
        }
        return;
    }
}

void handle_manga_reading(int x, int y) {
    if (g_manga_overlay_visible) {
        for (int i = 0; i < 6; ++i) {
            if (!g_manga_overlay_buttons[i].contains(x, y)) continue;
            const char* action = i == 0 ? "fit" :
                                 i == 1 ? "orientation" :
                                 i == 2 ? "refresh" :
                                 i == 3 ? "clean" :
                                 i == 4 ? "library" : "close";
            ESP_LOGI(TAG, "manga touch overlay=%s x=%d y=%d page=%d slice=%d/%d",
                     action, x, y, g_manga_page + 1,
                     g_manga_slice + 1, g_manga_slice_count);
            if (i == 0) {
                cycle_manga_fit_mode();
                render_manga_page(ps3::display::RefreshMode::GC16Full);
            } else if (i == 1) {
                toggle_manga_orientation();
                render_manga_page(ps3::display::RefreshMode::GC16Full);
            } else if (i == 2) {
                cycle_refresh_profile();
                render_manga_page(ps3::display::RefreshMode::GL16);
            } else if (i == 3) {
                g_pages_since_full = 0;
                render_manga_page(ps3::display::RefreshMode::GC16Full);
            } else if (i == 4) {
                close_manga_book_if_open();
                render_manga_library(ps3::display::RefreshMode::GC16Full);
                ps3::touch::drain();
            } else {
                g_manga_overlay_visible = false;
                render_manga_page(ps3::display::RefreshMode::GL16);
            }
            return;
        }
        ESP_LOGI(TAG, "manga touch overlay=outside x=%d y=%d", x, y);
        g_manga_overlay_visible = false;
        render_manga_page(ps3::display::RefreshMode::GL16);
        return;
    }
    const int top_strip_h = manga_top_strip_h();
    if (y < top_strip_h) {
        ESP_LOGI(TAG, "manga touch zone=library x=%d y=%d top=%d", x, y, top_strip_h);
        close_manga_book_if_open();
        render_manga_library(ps3::display::RefreshMode::GC16Full);
        ps3::touch::drain();  // drop taps buffered during the GC16Full library render
        return;
    }
    const bool right_binding = ps3::settings::state().right_binding;
    const int w = ps3::display::width();
    const int h = ps3::display::height();
    const int body_h = std::max(1, h - top_strip_h);
    const Rect center_zone{
        g_manga_landscape ? (w * 3 / 8) : (w / 3),
        top_strip_h,
        g_manga_landscape ? (w / 4) : (w / 3),
        body_h,
    };
    if (center_zone.contains(x, y)) {
        ESP_LOGI(TAG, "manga touch zone=center-menu x=%d y=%d", x, y);
        g_manga_overlay_visible = true;
        g_manga_hint_pages_remaining = 0;
        render_manga_page(ps3::display::RefreshMode::GL16);
        return;
    }
    const bool on_forward_side = g_manga_landscape
        ? x >= w / 2
        : x >= (w * 2 / 3);
    const bool advance = right_binding ? !on_forward_side : on_forward_side;
    if (advance) {
        if (g_manga_slice + 1 < g_manga_slice_count) {
            ++g_manga_slice;
        } else if (g_manga_page + 1 < g_manga_book.page_count()) {
            ++g_manga_page;
            g_manga_slice = 0;
            g_manga_slice_count = 1;
            g_manga_pending_last_slice = false;
        } else {
            return;
        }
    } else if (g_manga_slice > 0) {
        --g_manga_slice;
    } else if (g_manga_page > 0) {
        --g_manga_page;
        g_manga_slice = 0;
        g_manga_slice_count = 1;
        g_manga_pending_last_slice = true;
    } else {
        return;
    }
    ESP_LOGI(TAG, "manga touch zone=%s page=%d/%d slice=%d/%d",
             advance ? "next" : "prev",
             g_manga_page + 1, g_manga_book.page_count(),
             g_manga_slice + 1, g_manga_slice_count);
    update_manga_progress();
    ++g_pages_since_full;
    const int cadence = std::max(1, ps3::settings::state().full_refresh_pages);
    const int profile = ps3::settings::state().refresh_profile;
    const bool clean = profile == 2 || (profile == 1 && g_pages_since_full >= cadence);
    if (clean) g_pages_since_full = 0;
    if (g_manga_hint_pages_remaining > 0) --g_manga_hint_pages_remaining;
    render_manga_page(clean ? ps3::display::RefreshMode::GC16Full : refresh_page_turn_mode());
}

void handle_manga_error(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_to(g_manga_error_return);
        ps3::touch::drain();
    } else if (g_footer_right.contains(x, y)) {
        navigate_home();
        ps3::touch::drain();
    }
}

void handle_reader_library(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        render_home();
        return;
    }
    if (g_footer_mid.contains(x, y)) {
        render_reader_library();
        return;
    }
    for (size_t i = 0; i < g_reader_books.size() && i < 9; ++i) {
        if (!g_list_rows[i].contains(x, y)) continue;
        const auto& book = g_reader_books[i];

        std::string error;
        ps3::touch::drain();
        if (!open_reader_book(book, &error)) {
            show_reader_error(error.empty() ? "Could not open reader file." : error,
                              Screen::ReaderLibrary);
        } else {
            nav_push(Screen::ReaderLibrary);
            render_reader_page(ps3::display::RefreshMode::GC16);
            ps3::touch::drain();
        }
        return;
    }
}

void handle_reader_reading(int x, int y) {
    const int total_pages = std::max(1, static_cast<int>((g_reader_lines.size() + g_reader_lines_per_page - 1) /
                                                        g_reader_lines_per_page));
    if (g_footer_left.contains(x, y)) {
        save_reader_state();
        navigate_back();
    } else if (g_footer_mid.contains(x, y)) {
        g_reader_font_level = (g_reader_font_level + 1) % 4;
        reflow_reader_pages_preserving_line();
        persist_font_settings();
        save_reader_state();
        render_reader_page(ps3::display::RefreshMode::GC16Full);
    } else if (x < ps3::display::width() / 3 && g_reader_page > 0) {
        --g_reader_page;
        save_reader_state();
        render_reader_page(refresh_page_turn_mode());
    } else if ((g_footer_right.contains(x, y) || x > ps3::display::width() * 2 / 3) && g_reader_page + 1 < total_pages) {
        ++g_reader_page;
        save_reader_state();
        render_reader_page(refresh_page_turn_mode());
    }
}

void handle_reader_error(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_reader_error_return == Screen::Home) {
            render_home();
        } else {
            render_reader_library();
        }
        ps3::touch::drain();
    } else if (g_footer_right.contains(x, y)) {
        render_home();
        ps3::touch::drain();
    }
}

void handle_japanese_menu(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_home();
        return;
    }
    if (g_jp_menu_buttons[0].contains(x, y)) {
        nav_push(Screen::JapaneseMenu);
        render_japanese_source();
    } else if (g_jp_menu_buttons[1].contains(x, y)) {
        reset_japanese_mock();
        nav_push(Screen::JapaneseMenu);
        render_japanese_mock();
    } else if (g_jp_menu_buttons[2].contains(x, y)) {
        nav_push(Screen::JapaneseMenu);
        render_japanese_reference();
    } else if (g_jp_menu_buttons[3].contains(x, y)) {
        nav_push(Screen::JapaneseMenu);
        render_japanese_results();
    }
}

void handle_japanese_source(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_back();
    } else if (g_jp_menu_buttons[0].contains(x, y)) {
        g_jp_source_idx = 0;
        nav_push(Screen::JapaneseSource);
        render_japanese_unit();
    }
}

void handle_japanese_unit(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_back();
    } else if (g_jp_menu_buttons[0].contains(x, y)) {
        g_jp_unit_idx = 0;
        nav_push(Screen::JapaneseUnit);
        render_japanese_lesson();
    }
}

void handle_japanese_lesson(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_back();
    } else if (g_jp_menu_buttons[0].contains(x, y)) {
        g_jp_lesson_idx = 0;
        g_jp_index = 0;
        g_jp_selected = -1;
        g_jp_feedback_page = 0;
        nav_push(Screen::JapaneseLesson);
        render_japanese();
    }
}

void handle_japanese_mock(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_jp_mock_index > 0) {
            g_jp_mock_answers[g_jp_mock_index] = g_jp_mock_selected;
            --g_jp_mock_index;
            g_jp_mock_selected = g_jp_mock_answers[g_jp_mock_index];
            render_japanese_mock(ps3::display::RefreshMode::GL16);
        } else {
            navigate_back();
        }
        return;
    }
    if (g_footer_right.contains(x, y) && g_jp_mock_selected >= 0) {
        g_jp_mock_answers[g_jp_mock_index] = g_jp_mock_selected;
        if (g_jp_mock_index + 1 < japanese_item_count()) {
            ++g_jp_mock_index;
            g_jp_mock_selected = g_jp_mock_answers[g_jp_mock_index];
            render_japanese_mock(ps3::display::RefreshMode::GL16);
        } else {
            render_japanese_mock_results();
        }
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (g_jp_choices[i].contains(x, y)) {
            g_jp_mock_selected = i;
            render_japanese_mock(ps3::display::RefreshMode::GL16);
            return;
        }
    }
}

void handle_japanese_mock_results(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        render_japanese_menu();
    } else if (g_footer_mid.contains(x, y)) {
        g_jp_index = 0;
        g_jp_selected = g_jp_mock_answers[0];
        g_jp_feedback_page = 0;
        nav_push(Screen::JapaneseMockResults);
        render_japanese_feedback();
    }
}

void handle_japanese_reference_or_results(int x, int y) {
    if (g_footer_left.contains(x, y)) navigate_back();
}

void handle_japanese(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_jp_index > 0) {
            --g_jp_index;
            g_jp_selected = -1;
            render_japanese();
        } else {
            navigate_back();
        }
        return;
    }
    if (g_footer_mid.contains(x, y)) {
        nav_push(Screen::Japanese);
        render_japanese_font();
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (g_jp_choices[i].contains(x, y)) {
            nav_push(Screen::Japanese);
            g_jp_selected = i;
            g_jp_feedback_page = 0;
            render_japanese_feedback();
            return;
        }
    }
}

void handle_japanese_feedback(int x, int y) {
    const int count = static_cast<int>(sizeof(kJapaneseItems) / sizeof(kJapaneseItems[0]));
    if (g_footer_left.contains(x, y)) {
        if (g_jp_index > 0) {
            --g_jp_index;
            g_jp_selected = -1;
            g_jp_feedback_page = 0;
            render_japanese();
        } else {
            navigate_back();
        }
    } else if (g_footer_mid.contains(x, y)) {
        render_home();
    } else if (g_footer_right.contains(x, y)) {
        if (!g_jp_feedback_single && g_jp_feedback_page == 0) {
            g_jp_feedback_page = 1;
            render_japanese_feedback(refresh_reveal_mode());
        } else if (g_jp_index + 1 < count) {
            ++g_jp_index;
            g_jp_selected = -1;
            g_jp_feedback_page = 0;
            render_japanese();
        }
    }
}

void handle_japanese_font(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        navigate_back();
    } else if (g_footer_mid.contains(x, y)) {
        select_japanese_font(g_jp_font == JapaneseFontFace::BizUdGothic ? JapaneseFontFace::IpaCurrent : JapaneseFontFace::BizUdGothic);
        persist_font_settings();
        render_japanese_font(ps3::display::RefreshMode::GL16);
    } else if (g_footer_right.contains(x, y)) {
        g_font_lab_page = (g_font_lab_page + 1) % font_lab_page_count();
        render_japanese_font(ps3::display::RefreshMode::GL16);
    }
}

void handle_tap(int x, int y) {
    mark_activity();
    if (handle_header_nav(x, y)) return;
    switch (g_screen) {
        case Screen::Home:              handle_home(x, y); break;
        case Screen::Badge:             handle_badge(x, y); break;
        case Screen::Interview:         handle_interview_menu(x, y); break;
        case Screen::InterviewPracticeMenu: handle_interview_practice_menu(x, y); break;
        case Screen::InterviewCardList: handle_interview_card_list(x, y); break;
        case Screen::InterviewPractice: handle_interview_practice(x, y); break;
        case Screen::InterviewDrillQ:   handle_interview_drill_q(x, y); break;
        case Screen::InterviewDrillFB:  handle_interview_drill_fb(x, y); break;
        case Screen::InterviewGlossaryList: handle_interview_glossary_list(x, y); break;
        case Screen::InterviewGlossary: handle_interview_glossary(x, y); break;
        case Screen::InterviewResults:  handle_interview_results(x, y); break;
        case Screen::MangaLibrary:      handle_manga_library(x, y); break;
        case Screen::MangaReading:      handle_manga_reading(x, y); break;
        case Screen::MangaError:        handle_manga_error(x, y); break;
        case Screen::ReaderLibrary:     handle_reader_library(x, y); break;
        case Screen::ReaderReading:     handle_reader_reading(x, y); break;
        case Screen::ReaderError:       handle_reader_error(x, y); break;
        case Screen::JapaneseMenu:      handle_japanese_menu(x, y); break;
        case Screen::JapaneseSource:    handle_japanese_source(x, y); break;
        case Screen::JapaneseUnit:      handle_japanese_unit(x, y); break;
        case Screen::JapaneseLesson:    handle_japanese_lesson(x, y); break;
        case Screen::Japanese:          handle_japanese(x, y); break;
        case Screen::JapaneseFeedback:  handle_japanese_feedback(x, y); break;
        case Screen::JapaneseMock:      handle_japanese_mock(x, y); break;
        case Screen::JapaneseMockResults: handle_japanese_mock_results(x, y); break;
        case Screen::JapaneseReference: handle_japanese_reference_or_results(x, y); break;
        case Screen::JapaneseResults:   handle_japanese_reference_or_results(x, y); break;
        case Screen::JapaneseFont:      handle_japanese_font(x, y); break;
        case Screen::Settings:          handle_settings(x, y); break;
        default:                        render_home(); break;
    }
}

void maybe_auto_sleep() {
    if (g_sleep_minutes <= 0 || g_last_activity_us <= 0) return;
    const int64_t idle_us = esp_timer_get_time() - g_last_activity_us;
    const int64_t threshold = static_cast<int64_t>(g_sleep_minutes) * 60 * 1000000;
    if (idle_us >= threshold) {
        enter_light_sleep("auto");
    }
}

void log_boot_battery() {
    int64_t dur = -1;
    int prev_mv = -1;
    std::time_t t = 0;
    int mv = 0;
    if (ps3::battery::load_and_clear_deep_entry(&t, &mv)) {
        prev_mv = mv;
        const int64_t delta = static_cast<int64_t>(std::time(nullptr) - t);
        if (delta > 0) dur = delta;
    }
    ps3::battery::log_event("boot", dur, prev_mv);
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "PaperBadge boot: Badge+Interview restored, Manga/Reader guarded");
    ps3::battery::init_nvs();
    if (!ps3::display::init()) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    const bool biz_ok = g_biz_font.bind_sparse(ps3::font::kBuiltinUiFontCodepoints,
                                               ps3::font::kBuiltinUiFontGlyphCount,
                                               ps3::font::kBuiltinUiFontGlyphs);
    const bool ipa_ok = g_ipa_font.bind_sparse(ps3::font::kBuiltinUiFontCodepoints,
                                               ps3::font::kBuiltinUiFontGlyphCount,
                                               ps3::font::kBuiltinIpaGothicGlyphs);
    if (!biz_ok || !ipa_ok) {
        ESP_LOGE(TAG, "built-in font bind failed");
        return;
    }
    select_japanese_font(JapaneseFontFace::BizUdGothic);
    reset_japanese_mock();
    ps3::battery::init();
    ps3::touch::init();
    ps3::imu::probe();
    ps3::sd::mount();
    ensure_dirs();
    ps3::settings::load();
    g_sleep_minutes = ps3::settings::state().sleep_minutes;
    g_power_off_minutes = ps3::settings::state().power_off_minutes;
    apply_persisted_font_settings();
    ps3::display::set_inverted(ps3::settings::state().rotation_inverted);
    ps3::touch::set_inverted(ps3::settings::state().rotation_inverted);
    log_boot_battery();
    g_manga_db.open(kMangaDbPath);
    mark_activity();
    render_home(ps3::display::RefreshMode::GC16Full);

    while (true) {
        int x = 0;
        int y = 0;
        if (ps3::touch::poll_tap(&x, &y)) {
            handle_tap(x, y);
            continue;
        }
        maybe_auto_sleep();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
