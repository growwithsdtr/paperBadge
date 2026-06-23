#include "TouchManager.h"

namespace paperbadge {
namespace hw {

namespace {
constexpr int kPaperS3TouchIntPin = 48;
}

void TouchManager::begin() {}

TouchSnapshot TouchManager::sample(uint32_t nowMs) const {
  TouchSnapshot snapshot;
  snapshot.enabled = M5.Touch.isEnabled();
  snapshot.sampledAtMs = nowMs;
  if (!snapshot.enabled) {
    return snapshot;
  }

  snapshot.count = static_cast<uint8_t>(M5.Touch.getCount());
  const auto detail = M5.Touch.getDetail();
  snapshot.pressed = detail.isPressed();
  if (snapshot.count > 0 || snapshot.pressed) {
    snapshot.x = detail.x;
    snapshot.y = detail.y;
  }
  return snapshot;
}

bool TouchManager::lightSleepWakeSupported() const {
  return M5.getBoard() == m5::board_t::board_M5PaperS3;
}

bool TouchManager::deepSleepWakeSupported() const {
  return false;
}

int TouchManager::touchInterruptPin() const {
  return M5.getBoard() == m5::board_t::board_M5PaperS3 ? kPaperS3TouchIntPin : -1;
}

const char* TouchManager::wakeCaveat() const {
  if (M5.getBoard() == m5::board_t::board_M5PaperS3) {
    return "GT911 INT GPIO48: light sleep GPIO wake only; not RTC deep wake";
  }
  return "board touch wake not characterized";
}

}  // namespace hw
}  // namespace paperbadge
