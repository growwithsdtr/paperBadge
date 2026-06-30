#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
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
#include "font/text_render.hpp"
#include "font/utf8.hpp"
#include "font/xteink_font.hpp"
#include "hal/battery.hpp"
#include "hal/display.hpp"
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
    InterviewPractice,
    InterviewDrillQ,
    InterviewDrillFB,
    InterviewGlossary,
    InterviewResults,
    Japanese,
    JapaneseFeedback,
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
ps3::font::XTEinkFont* g_font = &g_biz_font;
Screen g_screen = Screen::Home;
JapaneseFontFace g_jp_font = JapaneseFontFace::BizUdGothic;
int64_t g_last_activity_us = 0;
int g_sleep_minutes = 5;
int g_power_off_minutes = 20;
int g_pages_since_full = 0;

// ── Hit-test rects ────────────────────────────────────────────────────
Rect g_home_buttons[6];
Rect g_footer_left;
Rect g_footer_mid;
Rect g_footer_right;
Rect g_list_rows[10];
Rect g_jp_choices[4];
Rect g_settings_buttons[5];

// Interview menu / sub-screen rects
Rect g_iv_menu_buttons[5];   // Practice, Drills, Exam, Glossary, Results
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

// ── Reader globals ────────────────────────────────────────────────────
std::vector<BookFile> g_reader_books;
std::vector<std::string> g_reader_lines;
std::string g_reader_path;
std::string g_reader_title;
int g_reader_page = 0;
int g_reader_lines_per_page = 24;
std::string g_reader_error_msg;

// ── Japanese globals ──────────────────────────────────────────────────
int g_jp_index = 0;
int g_jp_selected = -1;
int g_jp_feedback_page = 0;
bool g_jp_feedback_single = true;

// ── Interview state ───────────────────────────────────────────────────
int g_iv_card_idx = 0;
bool g_iv_card_spoken = false;   // false=title view, true=spoken answer view

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
    g_footer_left = {12, y + 8, bw, kFooterH - 16};
    g_footer_mid = {24 + bw, y + 8, bw, kFooterH - 16};
    g_footer_right = {36 + bw * 2, y + 8, bw, kFooterH - 16};
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
            if (std::strncmp(data + i, "&nbsp;", 6) == 0) {
                out.push_back(' ');
                i += 5;
            } else if (std::strncmp(data + i, "&amp;", 5) == 0) {
                out.push_back('&');
                i += 4;
            } else if (std::strncmp(data + i, "&lt;", 4) == 0) {
                out.push_back('<');
                i += 3;
            } else if (std::strncmp(data + i, "&gt;", 4) == 0) {
                out.push_back('>');
                i += 3;
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

std::string read_epub_text(const char* path) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path, 0)) return {};
    std::string out;
    const int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    for (int i = 0; i < n && out.size() < 512 * 1024; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        if (!(has_suffix_icase(name, ".xhtml") || has_suffix_icase(name, ".html") || has_suffix_icase(name, ".htm"))) {
            continue;
        }
        // Skip oversized entries to avoid heap OOM
        if (st.m_uncomp_size > 2 * 1024 * 1024) continue;
        size_t sz = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!data) continue;
        out += strip_html(static_cast<const char*>(data), sz);
        out += "\n\n";
        std::free(data);
    }
    mz_zip_reader_end(&zip);
    return out;
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

void select_japanese_font(JapaneseFontFace face) {
    g_jp_font = face;
    g_font = (face == JapaneseFontFace::BizUdGothic) ? &g_biz_font : &g_ipa_font;
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

// ── Forward declarations ───────────────────────────────────────────────
void render_home(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_badge();
void render_interview(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_practice(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_drill_q(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_drill_fb(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_glossary(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_interview_results(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_manga_library(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_manga_page(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GL16);
void render_manga_error(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_reader_library(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_reader_page(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GL16);
void render_reader_error(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_feedback(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_japanese_font(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);
void render_settings(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16);

void render_current(ps3::display::RefreshMode mode = ps3::display::RefreshMode::GC16) {
    switch (g_screen) {
        case Screen::Home:               render_home(mode); break;
        case Screen::Badge:              render_badge(); break;
        case Screen::Interview:          render_interview(mode); break;
        case Screen::InterviewPractice:  render_interview_practice(mode); break;
        case Screen::InterviewDrillQ:    render_interview_drill_q(mode); break;
        case Screen::InterviewDrillFB:   render_interview_drill_fb(mode); break;
        case Screen::InterviewGlossary:  render_interview_glossary(mode); break;
        case Screen::InterviewResults:   render_interview_results(mode); break;
        case Screen::MangaLibrary:       render_manga_library(mode); break;
        case Screen::MangaReading:       render_manga_page(mode); break;
        case Screen::MangaError:         render_manga_error(mode); break;
        case Screen::ReaderLibrary:      render_reader_library(mode); break;
        case Screen::ReaderReading:      render_reader_page(mode); break;
        case Screen::ReaderError:        render_reader_error(mode); break;
        case Screen::Japanese:           render_japanese(mode); break;
        case Screen::JapaneseFeedback:   render_japanese_feedback(mode); break;
        case Screen::Settings:           render_settings(mode); break;
        default:                         render_home(mode); break;
    }
}

// ── Badge final-frame (persists on e-ink during sleep / power-off) ────
void draw_english_badge_final_frame() {
    ps3::display::clear();
    const bool shown = ps3::comic::display_png(
        embedded_badge::kBadgeEnPng, embedded_badge::kBadgeEnSize);
    if (!shown) {
        // Minimal fallback text if PNG decode fails
        draw_wrapped(40, 80, ps3::display::width() - 80,
                     "Daniel Jimenez\nSenior Technical PM | AI Products");
    }
    ps3::display::flush(ps3::display::RefreshMode::GC16Full);
}

// ── Home ───────────────────────────────────────────────────────────────
void render_home(ps3::display::RefreshMode mode) {
    g_screen = Screen::Home;
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
void render_badge() {
    g_screen = Screen::Badge;
    ps3::display::clear();
    // Try embedded PNG first; fall back to SD asset file
    bool shown = ps3::comic::display_png(
        embedded_badge::kBadgeEnPng, embedded_badge::kBadgeEnSize);
    if (!shown) {
        const std::string png = std::string(kAssetsRoot) + "/badge_en.png";
        const auto bytes = read_file_bytes(png.c_str(), 2 * 1024 * 1024);
        shown = !bytes.empty() &&
                ps3::comic::display_png(bytes.data(), bytes.size());
    }
    if (!shown) {
        draw_header("Badge");
        draw_wrapped(34, 140, ps3::display::width() - 68,
                     "Daniel Jimenez\nSenior Technical PM | AI Products\n\n"
                     "Embedded badge image unavailable.");
    }
    draw_footer("Home", nullptr, nullptr);
    ps3::display::flush(ps3::display::RefreshMode::GC16Full);
}

// ── Interview — menu ──────────────────────────────────────────────────
void render_interview(ps3::display::RefreshMode mode) {
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

// ── Interview — Practice ──────────────────────────────────────────────
void render_interview_practice(ps3::display::RefreshMode mode) {
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
        // Show the spoken answer
        draw_wrapped(30, y, ps3::display::width() - 60, c.spoken, 14);
    } else {
        // Show prompt to reveal
        draw_wrapped(30, y, ps3::display::width() - 60,
                     "Tap REVEAL to see the answer.\n\n"
                     "Theme: " + std::string(c.theme) + "\n"
                     "Confidence: " + c.confidence, 6);
    }
    draw_footer(g_iv_card_idx > 0 ? "Prev" : "Menu",
                g_iv_card_spoken ? "Hide" : "Reveal",
                g_iv_card_idx + 1 < static_cast<int>(embedded_papercoach::kCardCount) ? "Next" : nullptr);
    ps3::display::flush(mode);
}

// ── Interview — Drill Q ───────────────────────────────────────────────
void render_interview_drill_q(ps3::display::RefreshMode mode) {
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
    draw_header("Drills", std::to_string(mcq_pos));
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
    draw_footer("Menu", "Home", right_label);
    ps3::display::flush(mode);
}

// ── Interview — Glossary ──────────────────────────────────────────────
void render_interview_glossary(ps3::display::RefreshMode mode) {
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
    g_screen = Screen::InterviewResults;
    ps3::display::clear();
    draw_header("Interview Results");
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
    if (g_iv_exam_count > 0 && g_iv_in_exam) {
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
    draw_footer("Menu", "Reset", "Home");
    ps3::display::flush(mode);
}

// ── Manga ─────────────────────────────────────────────────────────────
bool open_manga_library() {
    if (g_manga_library.open(kMangaRoot)) return true;
    mkdir_if_missing(kMangaRoot);
    return g_manga_library.open(kMangaRoot);
}

void render_manga_library(ps3::display::RefreshMode mode) {
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
    g_screen = Screen::MangaReading;
    if (!g_manga_open) {
        render_manga_library(ps3::display::RefreshMode::GC16);
        return;
    }
    const size_t fb_size = static_cast<size_t>(epd_width()) * epd_height() / 2;
    bool ok = ps3::comic::page_loader::try_consume(g_manga_page, ps3::display::framebuffer(), fb_size);
    if (!ok) {
        ok = ps3::comic::page_loader::fetch_and_consume(g_manga_page, ps3::display::framebuffer(), fb_size);
    }
    if (!ok) {
        ps3::display::clear();
        draw_header("Manga error");
        draw_wrapped(34, 130, ps3::display::width() - 68, "Failed to decode this page.");
    }
    fill_rect({0, 0, ps3::display::width(), kToolbarH}, 15);
    draw_text(14, 18, "Manga");
    const std::string page = std::to_string(g_manga_page + 1) + "/" + std::to_string(g_manga_book.page_count());
    draw_text(ps3::display::width() - text_width(page) - 14, 18, page);
    draw_hline(0, kToolbarH - 1, ps3::display::width());
    ps3::display::flush(mode);
    ps3::comic::page_loader::request(g_manga_page);
}

void render_manga_error(ps3::display::RefreshMode mode) {
    g_screen = Screen::MangaError;
    ps3::display::clear();
    draw_header("Manga - Cannot Open");
    draw_wrapped(34, 90, ps3::display::width() - 68, g_manga_error_msg);
    draw_footer("Back", nullptr, nullptr);
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

void load_reader_state_for(const std::string& path) {
    FILE* fp = std::fopen(kReaderStatePath, "r");
    if (!fp) return;
    char line[1200] = {};
    if (std::fgets(line, sizeof(line), fp)) {
        char* tab = std::strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            if (path == line) g_reader_page = std::atoi(tab + 1);
        }
    }
    std::fclose(fp);
}

bool open_reader_book(const std::string& path) {
    std::string text = has_suffix_icase(path, ".epub") ? read_epub_text(path.c_str()) : read_text_file(path.c_str());
    if (text.empty()) return false;
    g_reader_path = path;
    g_reader_title = basename_of(path);
    g_reader_lines.clear();
    for (const auto& para : wrap_text(text, ps3::display::width() - 56)) {
        g_reader_lines.push_back(para);
    }
    if (g_reader_lines.empty()) g_reader_lines.push_back("(empty)");
    g_reader_lines_per_page = std::max(1, (ps3::display::height() - kToolbarH - kFooterH - 28) / (active_font().height() + 8));
    g_reader_page = 0;
    load_reader_state_for(path);
    const int max_page = std::max(0, static_cast<int>((g_reader_lines.size() + g_reader_lines_per_page - 1) /
                                                     g_reader_lines_per_page) - 1);
    g_reader_page = std::max(0, std::min(g_reader_page, max_page));
    return true;
}

void render_reader_library(ps3::display::RefreshMode mode) {
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
    g_screen = Screen::ReaderReading;
    ps3::display::clear();
    const int total_pages = std::max(1, static_cast<int>((g_reader_lines.size() + g_reader_lines_per_page - 1) /
                                                        g_reader_lines_per_page));
    draw_header(g_reader_title, std::to_string(g_reader_page + 1) + "/" + std::to_string(total_pages));
    int y = kToolbarH + 18;
    const int first = g_reader_page * g_reader_lines_per_page;
    for (int i = 0; i < g_reader_lines_per_page && first + i < static_cast<int>(g_reader_lines.size()); ++i) {
        draw_text(28, y, g_reader_lines[first + i]);
        y += active_font().height() + 8;
    }
    draw_footer("Library", "Prev", "Next");
    ps3::display::flush(mode);
}

void render_reader_error(ps3::display::RefreshMode mode) {
    g_screen = Screen::ReaderError;
    ps3::display::clear();
    draw_header("Reader - Cannot Open");
    draw_wrapped(34, 90, ps3::display::width() - 68, g_reader_error_msg);
    draw_footer("Back", nullptr, nullptr);
    ps3::display::flush(mode);
}

// ── Japanese ──────────────────────────────────────────────────────────
void render_japanese(ps3::display::RefreshMode mode) {
    g_screen = Screen::Japanese;
    const auto& item = kJapaneseItems[g_jp_index];
    ps3::display::clear();
    draw_header("日本語", std::string("Q") + std::to_string(g_jp_index + 1) + "/" + std::to_string(sizeof(kJapaneseItems) / sizeof(kJapaneseItems[0])));
    int y = 92;
    y = draw_wrapped(30, y, ps3::display::width() - 60, item.prompt, 5) + 10;
    for (int i = 0; i < 4; ++i) {
        g_jp_choices[i] = {30, y, ps3::display::width() - 60, 78};
        std::string label;
        label.push_back(static_cast<char>('A' + i));
        label += ". ";
        label += item.choices[i];
        draw_button(g_jp_choices[i], label);
        y += 90;
    }
    draw_footer(g_jp_index > 0 ? "Prev" : "Home", "Font", nullptr);
    ps3::display::flush(mode);
}

bool japanese_feedback_single_page(const JapaneseItem& item) {
    const int available = ps3::display::height() - kToolbarH - kFooterH - 40;
    int h = active_font().height() + 18;
    h += static_cast<int>(wrap_text(item.answer, ps3::display::width() - 60).size()) * (active_font().height() + 8) + 14;
    h += static_cast<int>(wrap_text(item.explanation, ps3::display::width() - 60).size()) * (active_font().height() + 8) + 14;
    h += static_cast<int>(wrap_text(item.english, ps3::display::width() - 60).size()) * (active_font().height() + 8) + 14;
    return h <= available;
}

void render_japanese_feedback(ps3::display::RefreshMode mode) {
    g_screen = Screen::JapaneseFeedback;
    const auto& item = kJapaneseItems[g_jp_index];
    g_jp_feedback_single = japanese_feedback_single_page(item);
    if (g_jp_feedback_single && g_jp_feedback_page > 0) g_jp_feedback_page = 0;
    ps3::display::clear();
    draw_header("日本語 feedback", font_face_name());
    int y = 92;
    const bool correct = g_jp_selected == item.correct;
    draw_text(30, y, correct ? "Correct" : "Wrong");
    y += active_font().height() + 18;
    std::string answer = "Answer: ";
    answer.push_back(static_cast<char>('A' + item.correct));
    answer += ". ";
    answer += item.choices[item.correct];
    y = draw_wrapped(30, y, ps3::display::width() - 60, answer, 3) + 12;
    if (g_jp_feedback_page == 0) {
        y = draw_wrapped(30, y, ps3::display::width() - 60, item.answer, 4) + 12;
        y = draw_wrapped(30, y, ps3::display::width() - 60, item.explanation, g_jp_feedback_single ? 8 : 4) + 12;
        if (g_jp_feedback_single) {
            draw_wrapped(30, y, ps3::display::width() - 60, item.english, 4);
        }
    } else {
        y = draw_wrapped(30, y, ps3::display::width() - 60, item.explanation, 8) + 12;
        draw_wrapped(30, y, ps3::display::width() - 60, item.english, 8);
    }
    draw_footer(g_jp_index > 0 ? "Prev" : "Home", "Home",
                g_jp_feedback_single ? (g_jp_index + 1 < static_cast<int>(sizeof(kJapaneseItems) / sizeof(kJapaneseItems[0])) ? "Next" : nullptr)
                                     : (g_jp_feedback_page == 0 ? "More" : "Next"));
    ps3::display::flush(mode);
}

void render_japanese_font(ps3::display::RefreshMode mode) {
    g_screen = Screen::JapaneseFont;
    ps3::display::clear();
    draw_header("JP Font", font_face_name());
    draw_wrapped(34, 96, ps3::display::width() - 68,
                 "Tap Switch to choose the Japanese face used by the practice app and reader text.");
    draw_text(48, 188, std::string("Selected: ") + font_face_name());
    draw_text(48, 258, "BIZ UDGothic Regular:");
    draw_text_font(48, 304, "郵便局で荷物を送ります。", g_biz_font);
    draw_text_font(48, 350, "日本語の読みやすさ ABC123", g_biz_font);
    draw_text(48, 428, "IPAex Gothic:");
    draw_text_font(48, 474, "郵便局で荷物を送ります。", g_ipa_font);
    draw_text_font(48, 520, "日本語の読みやすさ ABC123", g_ipa_font);
    draw_footer("Back", "Switch", nullptr);
    ps3::display::flush(mode);
}

// ── Settings ──────────────────────────────────────────────────────────
void render_settings(ps3::display::RefreshMode mode) {
    g_screen = Screen::Settings;
    ps3::display::clear();
    draw_header("Settings", "Power");
    const int mv = ps3::battery::voltage_mv();
    const int pct = ps3::battery::level_pct();
    draw_wrapped(30, 92, ps3::display::width() - 60,
                 "Battery: " + std::to_string(mv) + " mV  " + std::to_string(pct) + "%  " +
                     (ps3::battery::is_charging() ? "charging" : "battery"));
    const char* labels[] = {"JP font", "Sleep now", "Power off", "Sleep timeout", "Clean refresh"};
    for (int i = 0; i < 5; ++i) {
        g_settings_buttons[i] = {34, 180 + i * 92, ps3::display::width() - 68, 74};
        std::string label = labels[i];
        if (i == 0) label += std::string(": ") + font_face_name();
        if (i == 3) label += ": " + std::to_string(g_sleep_minutes) + "m";
        draw_button(g_settings_buttons[i], label);
    }
    draw_footer("Home", nullptr, nullptr);
    ps3::display::flush(mode);
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
    if (g_home_buttons[0].contains(x, y)) render_badge();
    else if (g_home_buttons[1].contains(x, y)) render_interview();
    else if (g_home_buttons[2].contains(x, y)) {
        g_jp_index = 0;
        g_jp_selected = -1;
        render_japanese();
    } else if (g_home_buttons[3].contains(x, y)) render_manga_library();
    else if (g_home_buttons[4].contains(x, y)) render_reader_library();
    else if (g_home_buttons[5].contains(x, y)) render_settings();
}

void handle_interview_menu(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        render_home();
        return;
    }
    if (g_iv_menu_buttons[0].contains(x, y)) {
        g_iv_card_idx = 0;
        g_iv_card_spoken = false;
        render_interview_practice();
    } else if (g_iv_menu_buttons[1].contains(x, y)) {
        g_iv_drill_idx = 0;
        g_iv_drill_answer = -1;
        g_iv_in_exam = false;
        render_interview_drill_q();
    } else if (g_iv_menu_buttons[2].contains(x, y)) {
        // Exam mode
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
        g_iv_gloss_idx = 0;
        render_interview_glossary();
    } else if (g_iv_menu_buttons[4].contains(x, y)) {
        render_interview_results();
    }
}

void handle_interview_practice(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_iv_card_idx > 0) {
            --g_iv_card_idx;
            g_iv_card_spoken = false;
            render_interview_practice(ps3::display::RefreshMode::GL16);
        } else {
            render_interview();
        }
    } else if (g_footer_mid.contains(x, y)) {
        g_iv_card_spoken = !g_iv_card_spoken;
        if (g_iv_card_spoken) ++g_iv_session_practice;
        render_interview_practice(ps3::display::RefreshMode::GL16);
    } else if (g_footer_right.contains(x, y)) {
        if (g_iv_card_idx + 1 < static_cast<int>(embedded_papercoach::kCardCount)) {
            ++g_iv_card_idx;
            g_iv_card_spoken = false;
            render_interview_practice(ps3::display::RefreshMode::GL16);
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

void handle_interview_glossary(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_iv_gloss_idx > 0) {
            --g_iv_gloss_idx;
            render_interview_glossary(ps3::display::RefreshMode::GL16);
        } else {
            render_interview();
        }
    } else if (g_footer_mid.contains(x, y)) {
        render_interview();
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
            render_manga_error();
            return;
        }

        // File size guard — miniz cannot parse ZIP64 central directories.
        // Files >50 MB are very likely ZIP64 and will fail.
        struct stat st{};
        long long file_size = 0;
        if (stat(path, &st) == 0) file_size = static_cast<long long>(st.st_size);
        if (file_size > 50LL * 1024 * 1024) {
            char buf[512];
            const long long mb = file_size / (1024LL * 1024LL);
            std::snprintf(buf, sizeof(buf),
                "Archive too large (%lld MB).\n\n"
                "Miniz cannot parse ZIP64 central directories. Archives\n"
                "over ~50 MB likely use ZIP64 and will fail.\n\n"
                "Diagnostics:\n"
                "  File size:   %lld MB\n"
                "  ZIP64 risk:  high (>50 MB)\n\n"
                "Split into volumes under 50 MB or re-archive\n"
                "without ZIP64 (zip -0 or use a non-ZIP64 tool).",
                mb, mb);
            g_manga_error_msg = buf;
            render_manga_error();
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
            diag +=
                "\nPossible causes:\n"
                "  - Archive contains only PNG/WebP (JPEG required)\n"
                "  - Corrupted or truncated archive\n"
                "  - Unsupported compression method (requires Deflate)\n"
                "  - Insufficient PSRAM for page index\n"
                "\nCheck serial log (cbz: tag) for details.";
            g_manga_error_msg = diag;
            render_manga_error();
        } else {
            render_manga_page(ps3::display::RefreshMode::GC16Full);
        }
        return;
    }
}

void handle_manga_reading(int x, int y) {
    if (y < kToolbarH) {
        if (g_manga_open) {
            ps3::comic::page_loader::stop();
            g_manga_book.close();
            g_manga_open = false;
        }
        render_manga_library(ps3::display::RefreshMode::GC16Full);
        return;
    }
    const bool right_binding = ps3::settings::state().right_binding;
    const bool advance = right_binding ? x < ps3::display::width() / 2 : x >= ps3::display::width() / 2;
    if (advance && g_manga_page + 1 < g_manga_book.page_count()) {
        ++g_manga_page;
    } else if (!advance && g_manga_page > 0) {
        --g_manga_page;
    } else {
        return;
    }
    update_manga_progress();
    ++g_pages_since_full;
    const int cadence = std::max(1, ps3::settings::state().full_refresh_pages);
    const bool clean = g_pages_since_full >= cadence;
    if (clean) g_pages_since_full = 0;
    render_manga_page(clean ? ps3::display::RefreshMode::GC16Full : ps3::display::RefreshMode::GL16);
}

void handle_manga_error(int /*x*/, int /*y*/) {
    // Any tap dismisses the error and returns to library
    render_manga_library();
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

        // File size guard — large files may OOM during text extraction
        struct stat st{};
        long long fsize = 0;
        if (stat(book.path.c_str(), &st) == 0) fsize = static_cast<long long>(st.st_size);
        if (fsize > 4LL * 1024 * 1024) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "File too large to open (%lld KB).\n\n"
                "Maximum supported size: 4 MB.\n\n"
                "For EPUB files, convert to TXT and trim to under 4 MB.",
                fsize / 1024LL);
            g_reader_error_msg = buf;
            render_reader_error();
            return;
        }

        if (!open_reader_book(book.path)) {
            if (has_suffix_icase(book.path, ".epub")) {
                g_reader_error_msg =
                    "Could not open EPUB: " + book.name + "\n\n"
                    "Possible causes:\n"
                    "  - Corrupted or encrypted archive\n"
                    "  - No readable HTML/XHTML content found\n"
                    "  - All HTML entries exceed 2 MB uncompressed\n"
                    "  - Insufficient heap memory\n\n"
                    "Try converting to TXT format.";
            } else {
                g_reader_error_msg =
                    "Could not open: " + book.name + "\n\n"
                    "The file may be empty or unreadable.";
            }
            render_reader_error();
        } else {
            render_reader_page(ps3::display::RefreshMode::GC16);
        }
        return;
    }
}

void handle_reader_reading(int x, int y) {
    const int total_pages = std::max(1, static_cast<int>((g_reader_lines.size() + g_reader_lines_per_page - 1) /
                                                        g_reader_lines_per_page));
    if (g_footer_left.contains(x, y)) {
        save_reader_state();
        render_reader_library();
    } else if ((g_footer_mid.contains(x, y) || x < ps3::display::width() / 3) && g_reader_page > 0) {
        --g_reader_page;
        save_reader_state();
        render_reader_page(ps3::display::RefreshMode::GL16);
    } else if ((g_footer_right.contains(x, y) || x > ps3::display::width() * 2 / 3) && g_reader_page + 1 < total_pages) {
        ++g_reader_page;
        save_reader_state();
        render_reader_page(ps3::display::RefreshMode::GL16);
    }
}

void handle_reader_error(int /*x*/, int /*y*/) {
    render_reader_library();
}

void handle_japanese(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        if (g_jp_index > 0) {
            --g_jp_index;
            g_jp_selected = -1;
            render_japanese();
        } else {
            render_home();
        }
        return;
    }
    if (g_footer_mid.contains(x, y)) {
        render_japanese_font();
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (g_jp_choices[i].contains(x, y)) {
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
            render_home();
        }
    } else if (g_footer_mid.contains(x, y)) {
        render_home();
    } else if (g_footer_right.contains(x, y)) {
        if (!g_jp_feedback_single && g_jp_feedback_page == 0) {
            g_jp_feedback_page = 1;
            render_japanese_feedback(ps3::display::RefreshMode::GL16);
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
        render_japanese();
    } else if (g_footer_mid.contains(x, y)) {
        select_japanese_font(g_jp_font == JapaneseFontFace::BizUdGothic ? JapaneseFontFace::IpaCurrent : JapaneseFontFace::BizUdGothic);
        render_japanese_font(ps3::display::RefreshMode::GL16);
    }
}

void handle_settings(int x, int y) {
    if (g_footer_left.contains(x, y)) {
        render_home();
        return;
    }
    if (g_settings_buttons[0].contains(x, y)) {
        render_japanese_font();
    } else if (g_settings_buttons[1].contains(x, y)) {
        enter_light_sleep("settings");
    } else if (g_settings_buttons[2].contains(x, y)) {
        enter_deep_sleep();
    } else if (g_settings_buttons[3].contains(x, y)) {
        g_sleep_minutes = g_sleep_minutes == 0 ? 5 : (g_sleep_minutes == 5 ? 10 : (g_sleep_minutes == 10 ? 15 : 0));
        render_settings(ps3::display::RefreshMode::GL16);
    } else if (g_settings_buttons[4].contains(x, y)) {
        ps3::display::flush(ps3::display::RefreshMode::GC16Full);
    }
}

void handle_tap(int x, int y) {
    mark_activity();
    switch (g_screen) {
        case Screen::Home:              handle_home(x, y); break;
        case Screen::Badge:             render_home(); break;
        case Screen::Interview:         handle_interview_menu(x, y); break;
        case Screen::InterviewPractice: handle_interview_practice(x, y); break;
        case Screen::InterviewDrillQ:   handle_interview_drill_q(x, y); break;
        case Screen::InterviewDrillFB:  handle_interview_drill_fb(x, y); break;
        case Screen::InterviewGlossary: handle_interview_glossary(x, y); break;
        case Screen::InterviewResults:  handle_interview_results(x, y); break;
        case Screen::MangaLibrary:      handle_manga_library(x, y); break;
        case Screen::MangaReading:      handle_manga_reading(x, y); break;
        case Screen::MangaError:        handle_manga_error(x, y); break;
        case Screen::ReaderLibrary:     handle_reader_library(x, y); break;
        case Screen::ReaderReading:     handle_reader_reading(x, y); break;
        case Screen::ReaderError:       handle_reader_error(x, y); break;
        case Screen::Japanese:          handle_japanese(x, y); break;
        case Screen::JapaneseFeedback:  handle_japanese_feedback(x, y); break;
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
    ps3::battery::init();
    ps3::touch::init();
    ps3::sd::mount();
    ensure_dirs();
    ps3::settings::load();
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
