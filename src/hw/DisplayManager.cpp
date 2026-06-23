#include "DisplayManager.h"

namespace paperbadge {
namespace hw {

void DisplayManager::begin() {
  snapshot_ = {};
#if defined(ENABLE_GRAYSCALE_UI) && !ENABLE_GRAYSCALE_UI
  snapshot_.grayscaleEnabled = false;
#else
  snapshot_.grayscaleEnabled = true;
#endif
}

void DisplayManager::refreshFast(const char* reason) {
  M5.Display.setEpdMode(m5gfx::epd_fastest);
  noteRefresh(DisplayRefreshMode::Fast, reason);
}

void DisplayManager::refreshBalanced(const char* reason) {
  M5.Display.setEpdMode(m5gfx::epd_fastest);
  noteRefresh(DisplayRefreshMode::Balanced, reason);
}

void DisplayManager::refreshClean(const char* reason) {
  M5.Display.setEpdMode(m5gfx::epd_quality);
  noteRefresh(DisplayRefreshMode::Clean, reason);
}

void DisplayManager::noteRefresh(DisplayRefreshMode mode, const char* reason) {
  snapshot_.lastMode = mode;
  snapshot_.lastReason = reason && reason[0] ? reason : "render";
  snapshot_.lastRefreshMs = millis();
  ++snapshot_.refreshCount;
  if (mode == DisplayRefreshMode::Clean) {
    ++snapshot_.cleanCount;
    snapshot_.partialSinceClean = 0;
  } else {
    ++snapshot_.fastCount;
    ++snapshot_.partialSinceClean;
  }
  snapshot_.panelSleeping = false;
}

void DisplayManager::powerDownPanelIfSupported() {
  M5.Display.waitDisplay();
  M5.Display.sleep();
  snapshot_.panelSleeping = true;
  Serial.println("DisplayManager: panel sleep requested");
}

void DisplayManager::prepareForSleep() {
  M5.Display.waitDisplay();
}

void DisplayManager::recoverAfterWake() {
  M5.Display.wakeup();
  snapshot_.panelSleeping = false;
}

uint32_t DisplayManager::color(PaletteColor colorName) const {
  if (!snapshot_.grayscaleEnabled) {
    switch (colorName) {
      case PaletteColor::Bg:
      case PaletteColor::Panel:
        return TFT_WHITE;
      case PaletteColor::Disabled:
      case PaletteColor::MutedText:
      case PaletteColor::Border:
      case PaletteColor::Highlight:
      case PaletteColor::Text:
      default:
        return TFT_BLACK;
    }
  }

  switch (colorName) {
    case PaletteColor::Bg:
      return TFT_WHITE;
    case PaletteColor::Text:
      return TFT_BLACK;
    case PaletteColor::MutedText:
      return TFT_DARKGREY;
    case PaletteColor::Panel:
      return 0xE71C;
    case PaletteColor::Border:
      return 0x9CF3;
    case PaletteColor::Highlight:
      return 0xCE79;
    case PaletteColor::Disabled:
      return TFT_LIGHTGREY;
    default:
      return TFT_BLACK;
  }
}

const char* DisplayManager::refreshModeName(DisplayRefreshMode mode) {
  switch (mode) {
    case DisplayRefreshMode::Fast:
      return "Fast";
    case DisplayRefreshMode::Balanced:
      return "Balanced";
    case DisplayRefreshMode::Clean:
      return "Clean";
    default:
      return "Unknown";
  }
}

}  // namespace hw
}  // namespace paperbadge
