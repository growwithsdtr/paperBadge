#include "ReaderApp.h"

#include <M5Unified.h>
#include <SD.h>

namespace paperbadge {
namespace apps {

// ---- Embedded sample documents ----
// Shown when SD has no books. Original content only.

const char* ReaderApp::kEmbeddedTitles[] = {
  "Sample EN — Metro Study",
  "Sample JP — N3 Vocab",
};

const char* ReaderApp::kEmbeddedContent[] = {
  // English sample: micro-study on a train
  "Metro Study Tips\n"
  "\n"
  "This is an embedded sample document. It appears when no books are found on the SD card.\n"
  "\n"
  "The PaperBadge Reader is designed for micro-study on a commute. The 4.7-inch e-paper screen "
  "is easy on the eyes even in bright sunlight or a moving train.\n"
  "\n"
  "How to use this device well:\n"
  "\n"
  "Keep sessions short. Five to ten minutes of focused reading or practice is more effective "
  "than an hour of passive scrolling. The e-paper display saves battery and reduces glare, "
  "making it ideal for repeated short sessions.\n"
  "\n"
  "Tap the right side of the screen to advance a page. Tap the left side to go back. "
  "The footer buttons also work for navigation.\n"
  "\n"
  "Font size can be changed with the Font button below. Reader S fits more text per page. "
  "Reader L is easier to read while standing.\n"
  "\n"
  "To load your own content, place .txt or .md files in /paperBadge/books or /books on the "
  "SD card. Files up to 220 KB are supported. Longer files are truncated at the limit.\n"
  "\n"
  "This sample document is original test content and is not copyrighted material.\n"
  "\n"
  "— End of embedded English sample —\n",

  // Japanese sample: N3 vocabulary
  "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x82\xb5\xe3\x83\xb3\xe3\x83\x97\xe3\x83\xab\n"
  "\n"
  "\xe3\x81\x93\xe3\x82\x8c\xe3\x81\xaf\xe5\x86\x85\xe8\x94\xb5\xe3\x82\xb5\xe3\x83\xb3\xe3\x83\x97\xe3\x83\xab\xe3\x81\xa7\xe3\x81\x99\xe3\x80\x82\n"
  "\n"
  "\xe9\x83\xb5\xe4\xbe\xbf\xe5\xb1\x80\xe3\x81\xa7\xe8\x8d\xb7\xe7\x89\xa9\xe3\x82\x92\xe5\x8f\x97\xe3\x81\x91\xe5\x8f\x96\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\xe3\x80\x82\n"
  "\n"
  "\xe5\x85\x88\xe9\x80\xb1\xe3\x80\x81\xe6\x96\xb0\xe3\x81\x97\xe3\x81\x84\xe5\xa0\xb4\xe6\x89\x80\xe3\x81\xab\xe5\xbc\x95\xe3\x81\xa3\xe8\xb6\x8a\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\xe3\x80\x82\n"
  "\n"
  "\xe5\xad\x90\xe4\xbe\x9b\xe3\x81\xae\xe3\x81\x93\xe3\x82\x8d\xe3\x80\x81\xe3\x81\x93\xe3\x81\xae\xe8\xb1\x8a\xe3\x81\x8b\xe3\x81\xaa\xe5\x9c\xb0\xe5\x8c\xba\xe3\x81\xab\xe4\xbd\x8f\xe3\x82\x93\xe3\x81\xa7\xe3\x81\x84\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\xe3\x80\x82\n"
  "\n"
  "\xe9\x81\x95\xe3\x81\x86\xe3\x81\xa8\xe3\x81\x84\xe3\x81\x86\xe8\xa8\x80\xe8\x91\x89\xe3\x81\xaf\xe3\x80\x81\xe9\x96\x93\xe9\x81\x95\xe3\x81\x88\xe3\x82\x8b\xe3\x81\xbe\xe3\x81\x9f\xe3\x81\xaf\xe7\x95\xb0\xe3\x81\xaa\xe3\x82\x8b\xe3\x81\x93\xe3\x81\xa8\xe3\x82\x92\xe6\x84\x8f\xe5\x91\xb3\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x99\xe3\x80\x82\n"
  "\n"
  "\xe6\x89\x8b\xe7\xb4\x99\xe3\x82\x92\xe9\x80\x81\xe3\x82\x8b\xe3\x81\xab\xe3\x81\xaf\xe3\x80\x81\xe9\x83\xb5\xe4\xbe\xbf\xe5\xb1\x80\xe3\x81\xab\xe8\xa1\x8c\xe3\x81\x8d\xe3\x81\xbe\xe3\x81\x99\xe3\x80\x82\n"
  "\n"
  "\xe6\xad\xa3\xe3\x81\x97\xe3\x81\x84\xe8\xaa\xad\xe3\x81\xbf\xe6\x96\xb9\xe3\x82\x92\xe7\xb7\xb4\xe7\xbf\x92\xe3\x81\x97\xe3\x81\xaa\xe3\x81\x8c\xe3\x82\x89\xe8\xaa\xad\xe3\x82\x93\xe3\x81\xa7\xe3\x81\x8f\xe3\x81\xa0\xe3\x81\x95\xe3\x81\x84\xe3\x80\x82\n"
  "\n"
  "--- \xe3\x82\xb5\xe3\x83\xb3\xe3\x83\x97\xe3\x83\xab\xe7\xb5\x82\xe3\x82\x8f\xe3\x82\x8a ---\n",
};

namespace {
String shortPath(const String& path, size_t maxLen = 46) {
  if (path.length() <= maxLen) return path;
  return String("...") + path.substring(path.length() - maxLen + 3);
}

String extensionLower(const String& path) {
  const int dot = path.lastIndexOf('.');
  if (dot < 0) return "";
  String ext = path.substring(dot);
  ext.toLowerCase();
  return ext;
}
}  // namespace

void ReaderApp::begin(hw::SDManager* sdManager, hw::BatteryManager* batteryManager, hw::RTCManager* rtcManager,
                      hw::DisplayManager* displayManager) {
  sd_ = sdManager;
  battery_ = batteryManager;
  rtc_ = rtcManager;
  displayManager_ = displayManager;
  library_.begin(sd_);
}

void ReaderApp::onEnter() {
  if (rtc_) {
    rtc_->setLastSelectedApp(hw::PersistedApp::Reader);
  }
  state_.load();
  fontSize_ = static_cast<uint8_t>(constrain(state_.data().fontSize, 1, 3));
  refreshLibrary(false);
  view_ = View::Library;
}

void ReaderApp::onExit() {
  saveProgress();
  txt_.close();
}

void ReaderApp::onBeforeSleep() {
  saveProgress();
}

void ReaderApp::onAfterWake() {
  if (battery_) {
    battery_->sample();
  }
}

void ReaderApp::onLowBattery() {
  message_ = "Low battery. Reader progress has been saved.";
  view_ = View::Message;
  saveProgress();
}

void ReaderApp::render() {
  auto& display = M5.Display;
  display.setTextDatum(textdatum_t::top_left);
  display.setTextWrap(false, false);
  display.setTextColor(TFT_BLACK, TFT_WHITE);

  switch (view_) {
    case View::Reading:
      renderReading();
      break;
    case View::Message:
      renderMessage();
      break;
    case View::Library:
    default:
      renderLibrary();
      break;
  }
}

ReaderAction ReaderApp::handleTap(int32_t x, int32_t y) {
  if (view_ == View::Library) {
    if (homeRect_.contains(x, y)) {
      return ReaderAction::ExitRequested;
    }
    if (rescanRect_.contains(x, y)) {
      refreshLibrary(true);
      return ReaderAction::RenderRequested;
    }
    if (prevListRect_.contains(x, y)) {
      if (listOffset_ >= visibleRows_) {
        listOffset_ -= visibleRows_;
      } else {
        listOffset_ = 0;
      }
      return ReaderAction::RenderRequested;
    }
    if (nextListRect_.contains(x, y)) {
      if (listOffset_ + visibleRows_ < library_.count()) {
        listOffset_ += visibleRows_;
      }
      return ReaderAction::RenderRequested;
    }
    for (uint8_t i = 0; i < visibleRows_ && i < 6; ++i) {
      if (bookRows_[i].contains(x, y)) {
        openBook(listOffset_ + i);
        return ReaderAction::RenderRequested;
      }
    }
    return ReaderAction::None;
  }

  if (view_ == View::Message) {
    if (homeRect_.contains(x, y)) {
      return ReaderAction::ExitRequested;
    }
    view_ = View::Library;
    return ReaderAction::RenderRequested;
  }

  if (libraryRect_.contains(x, y)) {
    saveProgress();
    view_ = View::Library;
    return ReaderAction::RenderRequested;
  }
  if (homeRect_.contains(x, y)) {
    saveProgress();
    return ReaderAction::ExitRequested;
  }
  if (fontRect_.contains(x, y)) {
    const uint32_t page = txt_.currentPage();
    fontSize_ = fontSize_ >= 3 ? 1 : fontSize_ + 1;
    reopenCurrentAtPage(page);
    return ReaderAction::RenderRequested;
  }

  const int32_t width = M5.Display.width();
  bool changed = false;
  if (x < width / 3) {
    changed = txt_.previousPage();
  } else if (x > (width * 2) / 3) {
    changed = txt_.nextPage();
  }
  if (changed) {
    saveProgress();
    return ReaderAction::RenderRequested;
  }
  return ReaderAction::None;
}

const char* ReaderApp::viewName() const {
  switch (view_) {
    case View::Reading:
      return "Reading";
    case View::Message:
      return "Message";
    case View::Library:
    default:
      return "Library";
  }
}

void ReaderApp::renderLibrary() {
  auto& display = M5.Display;
  const int32_t width = display.width();
  const int32_t height = display.height();
  const uint32_t textColor = TFT_BLACK;
  const uint32_t muted = TFT_DARKGREY;

  // Title
  display.setFont(&fonts::FreeSansBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString("Reader", 28, 16);

  // Status line (SD + battery)
  display.setFont(&fonts::FreeSansBold9pt7b);
  display.setTextColor(muted, TFT_WHITE);
  String status = String(library_.count()) + " books";
  if (sd_) {
    status += "  SD: ";
    status += sd_->lastStatus();
  }
  if (battery_) {
    status += "  ";
    status += battery_->summaryLine();
  }
  display.drawString(status, 28, 64);

  const int32_t rowX = 26;
  const int32_t rowW = width - 52;
  const int32_t rowH = 80;
  const int32_t gap = 8;
  int32_t y = 96;
  visibleRows_ = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    bookRows_[i] = {};
    const size_t bookIndex = listOffset_ + i;
    if (bookIndex >= library_.count() || y + rowH > height - 96) {
      continue;
    }
    const hw::BookEntry* book = library_.at(bookIndex);
    if (!book) continue;
    bookRows_[i] = {rowX, y, rowW, rowH};
    display.drawRect(rowX, y, rowW, rowH, textColor);
    // Book title in readable body font
    display.setFont(&fonts::FreeSansBold12pt7b);
    display.setTextColor(textColor, TFT_WHITE);
    display.drawString(book->title, rowX + 14, y + 10);
    // Metadata in small muted font
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextColor(muted, TFT_WHITE);
    display.drawString(book->extension + "  " + String(book->size / 1024) + " KB  " + shortPath(book->path, 36),
                       rowX + 14, y + 50);
    y += rowH + gap;
    ++visibleRows_;
  }

  if (library_.count() == 0) {
    // Show embedded sample documents when SD has no books
    display.setFont(&fonts::FreeSansBold12pt7b);
    display.setTextColor(textColor, TFT_WHITE);
    display.drawString("Embedded sample documents", 34, y > 96 ? y : 96);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextColor(muted, TFT_WHITE);
    display.drawString("SD not mounted or no books found.", 34, (y > 96 ? y : 96) + 32);
    display.setTextColor(textColor, TFT_WHITE);

    int32_t embedY = (y > 96 ? y : 96) + 66;
    for (uint8_t i = 0; i < kEmbeddedCount; ++i) {
      if (i < 6) {
        bookRows_[i] = {rowX, embedY, rowW, rowH};
        display.setFont(&fonts::FreeSansBold12pt7b);
        display.setTextColor(textColor, TFT_WHITE);
        display.drawRect(rowX, embedY, rowW, rowH, textColor);
        display.drawString(kEmbeddedTitles[i], rowX + 14, embedY + 10);
        display.setFont(&fonts::FreeSansBold9pt7b);
        display.setTextColor(muted, TFT_WHITE);
        display.drawString("Embedded sample  \xe2\x80\xa2  tap to open", rowX + 14, embedY + 50);
        embedY += rowH + gap;
        ++visibleRows_;
      }
    }
  }

  const int32_t footerY = height - 76;
  const int32_t buttonW = (width - 52 - 30) / 4;
  prevListRect_ = {26, footerY, buttonW, 54};
  nextListRect_ = {26 + buttonW + 10, footerY, buttonW, 54};
  rescanRect_ = {26 + 2 * (buttonW + 10), footerY, buttonW, 54};
  homeRect_ = {26 + 3 * (buttonW + 10), footerY, buttonW, 54};
  libraryRect_ = {};
  fontRect_ = {};
  renderFooterButton(prevListRect_, "Prev");
  renderFooterButton(nextListRect_, "Next");
  renderFooterButton(rescanRect_, "Scan");
  renderFooterButton(homeRect_, "Home");
}

void ReaderApp::renderReading() {
  auto& display = M5.Display;
  const int32_t width = display.width();
  const int32_t height = display.height();
  const uint32_t textColor = TFT_BLACK;
  const uint32_t muted = TFT_DARKGREY;

  const reader::TxtPageView page = txt_.pageView();

  // Header: book title + page info in small readable font
  display.setFont(&fonts::FreeSansBold12pt7b);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString(txt_.title(), 26, 14);
  display.setTextColor(muted, TFT_WHITE);
  const uint32_t safePageCount = page.pageCount > 0 ? page.pageCount : 1;
  String status = String("Page ") + (page.page + 1) + "/" + safePageCount;
  if (txt_.truncated()) status += "  truncated";
  if (battery_) {
    status += "  ";
    status += battery_->summaryLine();
  }
  display.drawString(status, 26, 42);
  display.drawLine(24, 70, width - 24, 70, muted);

  // Body text — proportional FreeSansBold font
  applyBodyFont();
  display.setTextColor(textColor, TFT_WHITE);
  int32_t y = 86;
  const int32_t lineH = lineHeight();
  for (uint16_t i = 0; i < page.lineCount; ++i) {
    if (y + lineH > height - 100) break;
    display.drawString(txt_.lineAt(page.firstLine + i), 28, y);
    y += lineH;
  }

  // Hint line in small muted font
  display.setFont(&fonts::FreeSansBold9pt7b);
  display.setTextColor(muted, TFT_WHITE);
  display.drawString("Tap left / right thirds to turn pages", 28, height - 98);

  const int32_t footerY = height - 76;
  const int32_t buttonW = (width - 52 - 20) / 3;
  libraryRect_ = {26, footerY, buttonW, 54};
  fontRect_ = {26 + buttonW + 10, footerY, buttonW, 54};
  homeRect_ = {26 + 2 * (buttonW + 10), footerY, buttonW, 54};
  rescanRect_ = {};
  prevListRect_ = {};
  nextListRect_ = {};
  const char* sizeLabel = (fontSize_ == 1) ? "Font S" : (fontSize_ == 2 ? "Font M" : "Font L");
  renderFooterButton(libraryRect_, "Library");
  renderFooterButton(fontRect_, sizeLabel);
  renderFooterButton(homeRect_, "Home");
}

void ReaderApp::renderMessage() {
  auto& display = M5.Display;
  const int32_t width = display.width();
  const int32_t height = display.height();
  const uint32_t textColor = TFT_BLACK;
  const uint32_t muted = TFT_DARKGREY;

  display.setFont(&fonts::FreeSansBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString("Reader", 28, 20);

  display.setFont(&fonts::FreeSansBold12pt7b);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString(message_, 34, 130);

  display.setFont(&fonts::FreeSansBold9pt7b);
  display.setTextColor(muted, TFT_WHITE);
  display.drawString("Tap anywhere to return to the library.", 34, 180);

  homeRect_ = {26, height - 76, width - 52, 54};
  renderFooterButton(homeRect_, "Home");
}

void ReaderApp::renderFooterButton(const Rect& rect, const String& label) {
  auto& display = M5.Display;
  const uint32_t textColor = TFT_BLACK;
  display.drawRect(rect.x, rect.y, rect.w, rect.h, textColor);
  display.setFont(&fonts::FreeSansBold12pt7b);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  const int32_t textW = display.textWidth(label);
  display.drawString(label, rect.x + (rect.w - textW) / 2, rect.y + 14);
}

void ReaderApp::renderStatusLine(const String& text) {
  auto& display = M5.Display;
  display.setFont(&fonts::FreeSansBold9pt7b);
  display.setTextSize(1);
  display.drawString(text, 30, 66);
}

void ReaderApp::refreshLibrary(bool force) {
  if (battery_) {
    battery_->sampleIfStale(30000);
  }
  library_.refresh(force);
  if (listOffset_ >= library_.count()) {
    listOffset_ = 0;
  }
}

bool ReaderApp::openBook(size_t index) {
  // If SD library is empty, the index refers to an embedded sample
  if (library_.count() == 0) {
    return openEmbedded(static_cast<uint8_t>(index));
  }
  const hw::BookEntry* book = library_.at(index);
  if (!book) {
    return false;
  }

  if (!isTxtLike(*book)) {
    message_ = epub_.status();
    view_ = View::Message;
    return false;
  }

  if (!txt_.open(book->path, charsPerLine(), linesPerPage())) {
    message_ = String("Could not open ") + book->title;
    view_ = View::Message;
    return false;
  }

  if (state_.data().lastPath == book->path) {
    txt_.goToPage(state_.data().page);
  }
  view_ = View::Reading;
  saveProgress();
  return true;
}

bool ReaderApp::openEmbedded(uint8_t index) {
  if (index >= kEmbeddedCount) {
    message_ = "Embedded sample not found.";
    view_ = View::Message;
    return false;
  }
  if (!txt_.openFromString(String(kEmbeddedTitles[index]), String(kEmbeddedContent[index]),
                            charsPerLine(), linesPerPage())) {
    message_ = String("Could not open embedded sample: ") + kEmbeddedTitles[index];
    view_ = View::Message;
    return false;
  }
  view_ = View::Reading;
  return true;
}

bool ReaderApp::reopenCurrentAtPage(uint32_t page) {
  if (!txt_.isOpen()) {
    return false;
  }
  const String path = txt_.path();
  if (!txt_.open(path, charsPerLine(), linesPerPage())) {
    message_ = "Reader font change failed to reload the current file.";
    view_ = View::Message;
    return false;
  }
  txt_.goToPage(page);
  saveProgress();
  return true;
}

void ReaderApp::saveProgress() {
  if (!txt_.isOpen()) {
    return;
  }
  const uint32_t page = txt_.currentPage();
  const uint32_t offset = txt_.currentByteOffset();
  state_.set(txt_.path(), page, offset, fontSize_);
  state_.save();
  if (rtc_) {
    rtc_->setLastReaderPosition(txt_.path(), offset);
  }
}

uint16_t ReaderApp::charsPerLine() const {
  int32_t usableWidth = M5.Display.width() - 56;
  if (usableWidth < 120) usableWidth = 120;
  // Average character width for proportional FreeSansBold fonts
  const uint8_t approxCharWidth = textScale() == 1 ? 11 : (textScale() == 2 ? 16 : 22);
  int32_t chars = usableWidth / approxCharWidth;
  if (chars < 10) chars = 10;
  return static_cast<uint16_t>(chars);
}

uint8_t ReaderApp::linesPerPage() const {
  // Content area: from y=86 to height-100 (leaves room for footer)
  int32_t usableHeight = M5.Display.height() - 186;
  if (usableHeight < 80) usableHeight = 80;
  int32_t lines = usableHeight / lineHeight();
  if (lines < 2) lines = 2;
  return static_cast<uint8_t>(lines);
}

uint8_t ReaderApp::textScale() const {
  return static_cast<uint8_t>(constrain(fontSize_, 1, 3));
}

void ReaderApp::applyBodyFont() const {
  auto& display = M5.Display;
  switch (textScale()) {
    case 3:  display.setFont(&fonts::FreeSansBold24pt7b); break;  // Reader L: ~44px
    case 2:  display.setFont(&fonts::FreeSansBold18pt7b); break;  // Reader M: ~34px
    default: display.setFont(&fonts::FreeSansBold12pt7b); break;  // Reader S: ~24px
  }
  display.setTextSize(1);
}

int32_t ReaderApp::lineHeight() const {
  switch (textScale()) {
    case 3:  return 54;  // FreeSansBold24pt7b ~44px + leading
    case 2:  return 44;  // FreeSansBold18pt7b ~34px + leading
    default: return 34;  // FreeSansBold12pt7b ~24px + leading
  }
}

bool ReaderApp::isTxtLike(const hw::BookEntry& book) {
  const String ext = extensionLower(book.path);
  return ext == ".txt" || ext == ".md";
}

}  // namespace apps
}  // namespace paperbadge
