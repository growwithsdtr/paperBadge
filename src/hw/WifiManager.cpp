#include "WifiManager.h"

#include <esp_wifi.h>

namespace paperbadge {
namespace hw {

void WifiManager::begin() {
  disconnectAndPowerOff("boot");
}

bool WifiManager::beginOnDemand() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  lastChangedMs_ = millis();
  Serial.println("WiFi manager: STA enabled on demand");
  return WiFi.getMode() != WIFI_OFF;
}

void WifiManager::disconnectAndPowerOff(const char* reason) {
  WiFi.disconnect(true, true);
  WiFi.scanDelete();
  esp_wifi_stop();
#if defined(WIFI_MANAGER_DEINIT_ON_OFF) && WIFI_MANAGER_DEINIT_ON_OFF
  esp_wifi_deinit();
#endif
  WiFi.mode(WIFI_OFF);
  lastChangedMs_ = millis();
  Serial.printf("WiFi manager: off reason=%s\n", reason && reason[0] ? reason : "policy");
}

bool WifiManager::isEnabled() const {
  return WiFi.getMode() != WIFI_OFF;
}

wifi_mode_t WifiManager::mode() const {
  return WiFi.getMode();
}

const char* WifiManager::modeName() const {
  switch (WiFi.getMode()) {
    case WIFI_OFF:
      return "off";
    case WIFI_STA:
      return "sta";
    case WIFI_AP:
      return "ap";
    case WIFI_AP_STA:
      return "ap+sta";
    default:
      return "unknown";
  }
}

}  // namespace hw
}  // namespace paperbadge
