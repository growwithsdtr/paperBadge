#pragma once

#include <Arduino.h>
#include <esp_sleep.h>

namespace paperbadge {
namespace hw {

enum class PersistedApp : uint8_t {
  Badge = 0,
  Home = 1,
  Interview = 2,
  Japanese = 3,
  Reader = 4,
  Settings = 5,
  Unknown = 255,
};

struct RTCStateSnapshot {
  uint32_t bootCount = 0;
  PersistedApp lastSelectedApp = PersistedApp::Unknown;
  char lastReaderBook[96] = {};
  uint32_t lastReaderOffset = 0;
  esp_sleep_wakeup_cause_t lastWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  uint8_t lastSleepMode = 0;
  uint32_t lastSleepDurationMs = 0;
};

class RTCManager {
 public:
  void begin();
  RTCStateSnapshot snapshot() const;
  void setLastSelectedApp(PersistedApp app);
  void setLastReaderPosition(const String& path, uint32_t byteOffset);
  void markSleep(uint8_t mode, uint32_t durationMs);
  void markWake(esp_sleep_wakeup_cause_t cause);
  static const char* appName(PersistedApp app);

 private:
  void ensureInitialized();
};

}  // namespace hw
}  // namespace paperbadge
