#pragma once

#include <Arduino.h>

#include "../hw/BatteryManager.h"
#include "../hw/DisplayManager.h"
#include "../hw/RTCManager.h"
#include "../hw/SDManager.h"
#include "../reader/BookLibrary.h"
#include "../reader/EpubReader.h"
#include "../reader/ReaderState.h"
#include "../reader/TxtReader.h"

namespace paperbadge {
namespace apps {

enum class ReaderAction : uint8_t {
  None,
  RenderRequested,
  ExitRequested,
};

class ReaderApp {
 public:
  void begin(hw::SDManager* sdManager, hw::BatteryManager* batteryManager, hw::RTCManager* rtcManager,
             hw::DisplayManager* displayManager);
  void onEnter();
  void onExit();
  void onBeforeSleep();
  void onAfterWake();
  void onLowBattery();

  void render();
  ReaderAction handleTap(int32_t x, int32_t y);
  const char* viewName() const;

 private:
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

  enum class View : uint8_t {
    Library,
    Reading,
    Message,
  };

  void renderLibrary();
  void renderReading();
  void renderMessage();
  void renderFooterButton(const Rect& rect, const String& label);
  void renderStatusLine(const String& text);
  void refreshLibrary(bool force);
  bool openBook(size_t index);
  bool openEmbedded(uint8_t index);
  bool reopenCurrentAtPage(uint32_t page);
  void saveProgress();
  void applyBodyFont() const;
  uint16_t charsPerLine() const;
  uint8_t linesPerPage() const;
  uint8_t textScale() const;
  int32_t lineHeight() const;
  static bool isTxtLike(const hw::BookEntry& book);

  static const char* kEmbeddedTitles[];
  static const char* kEmbeddedContent[];
  static constexpr uint8_t kEmbeddedCount = 2;

  hw::SDManager* sd_ = nullptr;
  hw::BatteryManager* battery_ = nullptr;
  hw::RTCManager* rtc_ = nullptr;
  hw::DisplayManager* displayManager_ = nullptr;

  reader::BookLibrary library_;
  reader::ReaderState state_;
  reader::TxtReader txt_;
  reader::EpubReader epub_;
  View view_ = View::Library;
  String message_;
  uint8_t fontSize_ = 2;
  size_t listOffset_ = 0;
  uint8_t visibleRows_ = 0;
  Rect homeRect_;
  Rect rescanRect_;
  Rect prevListRect_;
  Rect nextListRect_;
  Rect libraryRect_;
  Rect fontRect_;
  Rect bookRows_[6];
};

}  // namespace apps
}  // namespace paperbadge
