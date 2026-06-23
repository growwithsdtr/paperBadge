#include "PowerManager.h"

#include <esp_err.h>

#if defined(ENABLE_EXPERIMENTAL_DFS) && ENABLE_EXPERIMENTAL_DFS
#include <esp_pm.h>
#endif

namespace paperbadge {
namespace hw {

void PowerManager::begin() {
  bootCpuMhz_ = static_cast<uint32_t>(getCpuFrequencyMhz());
  lastChangedMs_ = millis();

#if defined(ENABLE_EXPERIMENTAL_DFS) && ENABLE_EXPERIMENTAL_DFS
  esp_pm_config_t config = {};
  config.max_freq_mhz = bootCpuMhz_ > 0 ? static_cast<int>(bootCpuMhz_) : 240;
  config.min_freq_mhz = 80;
  config.light_sleep_enable = false;
  const esp_err_t err = esp_pm_configure(&config);
  dfsConfigured_ = err == ESP_OK;
  Serial.printf("PowerManager: experimental DFS %s err=%d\n", dfsConfigured_ ? "configured" : "not configured",
                static_cast<int>(err));
#else
  dfsConfigured_ = false;
  Serial.println("PowerManager: DFS disabled at compile time");
#endif
}

bool PowerManager::applyMode(PowerRuntimeMode mode, const char* reason) {
  mode_ = mode;
  lastChangedMs_ = millis();
  Serial.printf("PowerManager: mode=%s reason=%s dfs=%s\n", modeName(mode_),
                reason && reason[0] ? reason : "unspecified", dfsConfigured_ ? "on" : "off");
  return true;
}

const char* PowerManager::modeName(PowerRuntimeMode mode) {
  switch (mode) {
    case PowerRuntimeMode::PerformanceMode:
      return "Performance";
    case PowerRuntimeMode::InteractiveMode:
      return "Interactive";
    case PowerRuntimeMode::ReaderMode:
      return "Reader";
    case PowerRuntimeMode::IdleMode:
      return "Idle";
    default:
      return "Unknown";
  }
}

}  // namespace hw
}  // namespace paperbadge
