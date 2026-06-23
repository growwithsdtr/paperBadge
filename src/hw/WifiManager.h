#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace paperbadge {
namespace hw {

class WifiManager {
 public:
  void begin();
  bool beginOnDemand();
  void disconnectAndPowerOff(const char* reason = nullptr);
  bool isEnabled() const;
  wifi_mode_t mode() const;
  const char* modeName() const;
  uint32_t lastChangedMs() const { return lastChangedMs_; }

  template <typename Fn>
  void withWifi(Fn&& fn) {
    beginOnDemand();
    fn();
    disconnectAndPowerOff("withWifi complete");
  }

 private:
  uint32_t lastChangedMs_ = 0;
};

}  // namespace hw
}  // namespace paperbadge
