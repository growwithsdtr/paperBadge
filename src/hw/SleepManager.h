#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>

namespace paperbadge {
namespace hw {

enum class SleepMode : uint8_t {
  None = 0,
  Light = 1,
  Deep = 2,
};

struct SleepResult {
  SleepMode mode = SleepMode::None;
  esp_sleep_wakeup_cause_t wakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  esp_err_t error = ESP_OK;
  bool entered = false;
  bool touchWakeConfigured = false;
  uint32_t requestedMs = 0;
  uint32_t sleptMs = 0;
};

class SleepManager {
 public:
  void begin();
  SleepResult sleepForMs(uint32_t durationMs, bool allowTouchWake, const char* reason);
  SleepResult sleepForMinutes(uint32_t minutes, bool allowTouchWake, const char* reason);
  SleepResult enterLightSleep(uint64_t durationUs, bool allowTouchWake, const char* reason);
  SleepResult enterDeepSleep(uint64_t durationUs, bool allowTouchWake, const char* reason);

  esp_sleep_wakeup_cause_t getWakeCause() const;
  esp_reset_reason_t getResetReason() const;
  const SleepResult& lastResult() const { return lastResult_; }

  static const char* sleepModeName(SleepMode mode);
  static const char* wakeCauseName(esp_sleep_wakeup_cause_t cause);
  static const char* resetReasonName(esp_reset_reason_t reason);
  static bool paperS3TouchDeepWakeSupported();

 private:
  bool configurePaperS3TouchLightWake();
  void clearPaperS3TouchLightWake(bool configured);

  SleepResult lastResult_;
};

}  // namespace hw
}  // namespace paperbadge
