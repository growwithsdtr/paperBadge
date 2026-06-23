#include "WakeManager.h"

#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif

namespace paperbadge {
namespace hw {

void WakeManager::begin() {
  capture();
}

WakeSnapshot WakeManager::capture() {
  snapshot_.wakeCause = esp_sleep_get_wakeup_cause();
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  snapshot_.wakeCauses = esp_sleep_get_wakeup_causes();
#else
  snapshot_.wakeCauses = snapshot_.wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED ? 0 : (1UL << snapshot_.wakeCause);
#endif
  snapshot_.resetReason = esp_reset_reason();
  snapshot_.usbConnected = static_cast<bool>(Serial);
  snapshot_.kind = classify(snapshot_);
  return snapshot_;
}

const char* WakeManager::wakeCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
      return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ulp";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "gpio";
    case ESP_SLEEP_WAKEUP_UART:
      return "uart";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "reset";
    default:
      return "unknown";
  }
}

const char* WakeManager::resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int-wdt";
    case ESP_RST_TASK_WDT:
      return "task-wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
#if defined(ESP_RST_USB)
    case ESP_RST_USB:
      return "usb";
#endif
#if defined(ESP_RST_JTAG)
    case ESP_RST_JTAG:
      return "jtag";
#endif
#if defined(ESP_RST_EFUSE)
    case ESP_RST_EFUSE:
      return "efuse";
#endif
#if defined(ESP_RST_PWR_GLITCH)
    case ESP_RST_PWR_GLITCH:
      return "power-glitch";
#endif
#if defined(ESP_RST_CPU_LOCKUP)
    case ESP_RST_CPU_LOCKUP:
      return "cpu-lockup";
#endif
    case ESP_RST_UNKNOWN:
    default:
      return "unknown";
  }
}

const char* WakeManager::wakeKindName(WakeKind kind) {
  switch (kind) {
    case WakeKind::Reset:
      return "reset";
    case WakeKind::Timer:
      return "timer";
    case WakeKind::Touch:
      return "touch";
    case WakeKind::Gpio:
      return "gpio";
    case WakeKind::Uart:
      return "uart";
    case WakeKind::Usb:
      return "usb";
    case WakeKind::Unknown:
    default:
      return "unknown";
  }
}

String WakeManager::summaryLine() const {
  return String("wake: ") + wakeKindName(snapshot_.kind) + " cause=" + wakeCauseName(snapshot_.wakeCause) +
         " reset=" + resetReasonName(snapshot_.resetReason);
}

WakeKind WakeManager::classify(const WakeSnapshot& snapshot) {
  switch (snapshot.wakeCause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WakeKind::Timer;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return WakeKind::Touch;
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
      return WakeKind::Gpio;
    case ESP_SLEEP_WAKEUP_UART:
      return WakeKind::Uart;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      if (snapshot.usbConnected && (snapshot.resetReason == ESP_RST_POWERON ||
#if defined(ESP_RST_USB)
                                    snapshot.resetReason == ESP_RST_USB ||
#endif
                                    snapshot.resetReason == ESP_RST_EXT)) {
        return WakeKind::Usb;
      }
      return WakeKind::Reset;
    default:
      return WakeKind::Unknown;
  }
}

}  // namespace hw
}  // namespace paperbadge
