#include "ReaderApp.h"

#include <M5Unified.h>
#include <SD.h>

namespace paperbadge {
namespace apps {

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
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(displayManager_ ? displayManager_->color(hw::PaletteColor::Text) : TFT_BLACK, TFT_WHITE);

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
  const uint32_t textColor = displayManager_ ? displayManager_->color(hw::PaletteColor::Text) : TFT_BLACK;
  const uint32_t muted = displayManager_ ? displayManager_->color(hw::PaletteColor::MutedText) : TFT_DARKGREY;

  display.setTextFont(4);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString("Reader", 28, 24);

  display.setTextFont(2);
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
  renderStatusLine(status);

  const int32_t rowX = 26;
  const int32_t rowW = width - 52;
  const int32_t rowH = 70;
  const int32_t gap = 10;
  int32_t y = 114;
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
    display.setTextColor(textColor, TFT_WHITE);
    display.drawString(book->title, rowX + 14, y + 10);
    display.setTextColor(muted, TFT_WHITE);
    display.drawString(book->extension + "  " + String(book->size / 1024) + " KB  " + shortPath(book->path, 36),
                       rowX + 14, y + 40);
    y += rowH + gap;
    ++visibleRows_;
  }

  if (library_.count() == 0) {
    display.setTextColor(textColor, TFT_WHITE);
    display.drawString("No TXT or MD books found.", 34, 180);
    display.setTextColor(muted, TFT_WHITE);
    display.drawString("Place files in /paperBadge/books or /books on the SD card.", 34, 218);
    display.drawString("EPUB entries are listed as placeholders for a later parser.", 34, 248);
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
  const uint32_t textColor = displayManager_ ? displayManager_->color(hw::PaletteColor::Text) : TFT_BLACK;
  const uint32_t muted = displayManager_ ? displayManager_->color(hw::PaletteColor::MutedText) : TFT_DARKGREY;

  const reader::TxtPageView page = txt_.pageView();
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString(txt_.title(), 26, 18);
  display.setTextColor(muted, TFT_WHITE);
  const uint32_t safePageCount = page.pageCount > 0 ? page.pageCount : 1;
  String status = String("Page ") + (page.page + 1) + "/" + safePageCount +
                  "  offset " + page.byteOffset;
  if (txt_.truncated()) status += "  truncated";
  if (battery_) {
    status += "  ";
    status += battery_->summaryLine();
  }
  display.drawString(status, 26, 48);
  display.drawLine(24, 76, width - 24, 76, muted);

  display.setTextFont(2);
  display.setTextSize(textScale());
  display.setTextColor(textColor, TFT_WHITE);
  int32_t y = 94;
  const int32_t lineH = lineHeight();
  for (uint16_t i = 0; i < page.lineCount; ++i) {
    if (y > height - 108) break;
    display.drawString(txt_.lineAt(page.firstLine + i), 28, y);
    y += lineH;
  }

  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(muted, TFT_WHITE);
  display.drawString("Tap left/right page zones", 28, height - 100);

  const int32_t footerY = height - 76;
  const int32_t buttonW = (width - 52 - 20) / 3;
  libraryRect_ = {26, footerY, buttonW, 54};
  fontRect_ = {26 + buttonW + 10, footerY, buttonW, 54};
  homeRect_ = {26 + 2 * (buttonW + 10), footerY, buttonW, 54};
  rescanRect_ = {};
  prevListRect_ = {};
  nextListRect_ = {};
  renderFooterButton(libraryRect_, "Library");
  renderFooterButton(fontRect_, String("Font ") + static_cast<unsigned>(fontSize_));
  renderFooterButton(homeRect_, "Home");
}

void ReaderApp::renderMessage() {
  auto& display = M5.Display;
  const int32_t width = display.width();
  const int32_t height = display.height();
  const uint32_t textColor = displayManager_ ? displayManager_->color(hw::PaletteColor::Text) : TFT_BLACK;
  const uint32_t muted = displayManager_ ? displayManager_->color(hw::PaletteColor::MutedText) : TFT_DARKGREY;

  display.setTextFont(4);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString("Reader", 28, 24);
  display.setTextFont(2);
  display.setTextColor(textColor, TFT_WHITE);
  display.drawString(message_, 34, 150);
  display.setTextColor(muted, TFT_WHITE);
  display.drawString("Tap anywhere to return to the library.", 34, 190);

  homeRect_ = {26, height - 76, width - 52, 54};
  renderFooterButton(homeRect_, "Home");
}

void ReaderApp::renderFooterButton(const Rect& rect, const String& label) {
  auto& display = M5.Display;
  const uint32_t textColor = displayManager_ ? displayManager_->color(hw::PaletteColor::Text) : TFT_BLACK;
  display.drawRect(rect.x, rect.y, rect.w, rect.h, textColor);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(textColor, TFT_WHITE);
  const int32_t textW = display.textWidth(label);
  display.drawString(label, rect.x + (rect.w - textW) / 2, rect.y + 18);
}

void ReaderApp::renderStatusLine(const String& text) {
  auto& display = M5.Display;
  display.setTextFont(2);
  display.setTextSize(1);
  display.drawString(text, 30, 70);
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
  const uint8_t scale = textScale();
  const uint8_t approxCharWidth = scale == 1 ? 6 : (scale == 2 ? 12 : 18);
  int32_t chars = usableWidth / approxCharWidth;
  if (chars < 18) chars = 18;
  return static_cast<uint16_t>(chars);
}

uint8_t ReaderApp::linesPerPage() const {
  int32_t usableHeight = M5.Display.height() - 204;
  if (usableHeight < 120) usableHeight = 120;
  int32_t lines = usableHeight / lineHeight();
  if (lines < 3) lines = 3;
  return static_cast<uint8_t>(lines);
}

uint8_t ReaderApp::textScale() const {
  return static_cast<uint8_t>(constrain(fontSize_, 1, 3));
}

int32_t ReaderApp::lineHeight() const {
  return 18 * textScale() + 8;
}

bool ReaderApp::isTxtLike(const hw::BookEntry& book) {
  const String ext = extensionLower(book.path);
  return ext == ".txt" || ext == ".md";
}

}  // namespace apps
}  // namespace paperbadge
