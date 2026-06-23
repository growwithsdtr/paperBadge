#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>

namespace paperbadge {
namespace hw {

enum class WakeKind : uint8_t {
  Reset,
  Timer,
  Touch,
  Gpio,
  Uart,
  Usb,
  Unknown,
};

struct WakeSnapshot {
  esp_sleep_wakeup_cause_t wakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  uint32_t wakeCauses = 0;
  esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
  WakeKind kind = WakeKind::Unknown;
  bool usbConnected = false;
};

class WakeManager {
 public:
  void begin();
  WakeSnapshot capture();
  const WakeSnapshot& latest() const { return snapshot_; }

  static const char* wakeCauseName(esp_sleep_wakeup_cause_t cause);
  static const char* resetReasonName(esp_reset_reason_t reason);
  static const char* wakeKindName(WakeKind kind);
  String summaryLine() const;

 private:
  static WakeKind classify(const WakeSnapshot& snapshot);

  WakeSnapshot snapshot_;
};

}  // namespace hw
}  // namespace paperbadge
