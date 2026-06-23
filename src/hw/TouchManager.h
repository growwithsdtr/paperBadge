#pragma once

#include <Arduino.h>
#include <M5Unified.h>

namespace paperbadge {
namespace hw {

struct TouchSnapshot {
  bool enabled = false;
  uint8_t count = 0;
  int32_t x = -1;
  int32_t y = -1;
  bool pressed = false;
  uint32_t sampledAtMs = 0;
};

class TouchManager {
 public:
  void begin();
  TouchSnapshot sample(uint32_t nowMs = millis()) const;
  bool lightSleepWakeSupported() const;
  bool deepSleepWakeSupported() const;
  int touchInterruptPin() const;
  const char* wakeCaveat() const;
};

}  // namespace hw
}  // namespace paperbadge
