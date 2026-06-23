#pragma once

#include <Arduino.h>
#include <M5Unified.h>

namespace paperbadge {
namespace hw {

enum class DisplayRefreshMode : uint8_t {
  Fast,
  Balanced,
  Clean,
};

enum class PaletteColor : uint8_t {
  Bg,
  Text,
  MutedText,
  Panel,
  Border,
  Highlight,
  Disabled,
};

struct DisplaySnapshot {
  DisplayRefreshMode lastMode = DisplayRefreshMode::Balanced;
  uint32_t refreshCount = 0;
  uint32_t cleanCount = 0;
  uint32_t fastCount = 0;
  uint32_t partialSinceClean = 0;
  uint32_t lastRefreshMs = 0;
  bool panelSleeping = false;
  bool grayscaleEnabled = false;
  const char* lastReason = "boot";
};

class DisplayManager {
 public:
  void begin();
  void refreshFast(const char* reason = nullptr);
  void refreshBalanced(const char* reason = nullptr);
  void refreshClean(const char* reason = nullptr);
  void noteRefresh(DisplayRefreshMode mode, const char* reason = nullptr);
  void powerDownPanelIfSupported();
  void prepareForSleep();
  void recoverAfterWake();

  uint32_t color(PaletteColor color) const;
  const DisplaySnapshot& snapshot() const { return snapshot_; }
  static const char* refreshModeName(DisplayRefreshMode mode);

 private:
  DisplaySnapshot snapshot_;
};

}  // namespace hw
}  // namespace paperbadge
