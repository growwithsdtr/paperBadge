#pragma once

#include <Arduino.h>

namespace paperbadge {
namespace hw {

enum class PowerRuntimeMode : uint8_t {
  PerformanceMode,
  InteractiveMode,
  ReaderMode,
  IdleMode,
};

class PowerManager {
 public:
  void begin();
  bool applyMode(PowerRuntimeMode mode, const char* reason = nullptr);
  PowerRuntimeMode mode() const { return mode_; }
  uint32_t bootCpuMhz() const { return bootCpuMhz_; }
  uint32_t lastChangedMs() const { return lastChangedMs_; }
  bool dfsConfigured() const { return dfsConfigured_; }
  static const char* modeName(PowerRuntimeMode mode);

 private:
  PowerRuntimeMode mode_ = PowerRuntimeMode::InteractiveMode;
  uint32_t bootCpuMhz_ = 0;
  uint32_t lastChangedMs_ = 0;
  bool dfsConfigured_ = false;
};

}  // namespace hw
}  // namespace paperbadge
