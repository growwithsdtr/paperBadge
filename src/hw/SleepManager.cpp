#include "SleepManager.h"

#include <M5Unified.h>
#include <driver/gpio.h>

namespace paperbadge {
namespace hw {

namespace {
constexpr gpio_num_t kPaperS3TouchIntPin = GPIO_NUM_48;
}

void SleepManager::begin() {
  lastResult_ = {};
}

SleepResult SleepManager::sleepForMs(uint32_t durationMs, bool allowTouchWake, const char* reason) {
  return enterLightSleep(static_cast<uint64_t>(durationMs) * 1000ULL, allowTouchWake, reason);
}

SleepResult SleepManager::sleepForMinutes(uint32_t minutes, bool allowTouchWake, const char* reason) {
  return sleepForMs(minutes * 60UL * 1000UL, allowTouchWake, reason);
}

SleepResult SleepManager::enterLightSleep(uint64_t durationUs, bool allowTouchWake, const char* reason) {
  SleepResult result;
  result.mode = SleepMode::Light;
  result.requestedMs = static_cast<uint32_t>(durationUs / 1000ULL);
  const uint32_t beforeMs = millis();

  Serial.printf("SleepManager: light sleep entry reason=%s durationMs=%u touch=%s\n",
                reason && reason[0] ? reason : "unspecified", static_cast<unsigned>(result.requestedMs),
                allowTouchWake ? "requested" : "off");

  if (durationUs > 0) {
    result.error = esp_sleep_enable_timer_wakeup(durationUs);
    if (result.error != ESP_OK) {
      lastResult_ = result;
      return result;
    }
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

  result.touchWakeConfigured = allowTouchWake && configurePaperS3TouchLightWake();
  result.error = esp_light_sleep_start();
  result.entered = result.error == ESP_OK;
  clearPaperS3TouchLightWake(result.touchWakeConfigured);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  const uint32_t afterMs = millis();
  result.sleptMs = afterMs >= beforeMs ? afterMs - beforeMs : 0;
  result.wakeCause = esp_sleep_get_wakeup_cause();
  Serial.printf("SleepManager: light sleep wake cause=%s sleptMs=%u touchWake=%s err=%d\n",
                wakeCauseName(result.wakeCause), static_cast<unsigned>(result.sleptMs),
                result.touchWakeConfigured ? "yes" : "no", static_cast<int>(result.error));

  lastResult_ = result;
  return result;
}

SleepResult SleepManager::enterDeepSleep(uint64_t durationUs, bool allowTouchWake, const char* reason) {
  SleepResult result;
  result.mode = SleepMode::Deep;
  result.requestedMs = static_cast<uint32_t>(durationUs / 1000ULL);

  if (allowTouchWake && M5.getBoard() == m5::board_t::board_M5PaperS3 && !paperS3TouchDeepWakeSupported()) {
    result.error = ESP_ERR_NOT_SUPPORTED;
    Serial.printf("SleepManager: deep sleep blocked reason=%s detail=PaperS3 GPIO48 is not RTC wake capable\n",
                  reason && reason[0] ? reason : "unspecified");
    lastResult_ = result;
    return result;
  }

  if (durationUs > 0) {
    result.error = esp_sleep_enable_timer_wakeup(durationUs);
    if (result.error != ESP_OK) {
      lastResult_ = result;
      return result;
    }
  }

  Serial.printf("SleepManager: deep sleep entry reason=%s durationMs=%u\n",
                reason && reason[0] ? reason : "unspecified", static_cast<unsigned>(result.requestedMs));
  esp_deep_sleep_start();

  result.error = ESP_ERR_INVALID_STATE;
  lastResult_ = result;
  return result;
}

esp_sleep_wakeup_cause_t SleepManager::getWakeCause() const {
  return esp_sleep_get_wakeup_cause();
}

esp_reset_reason_t SleepManager::getResetReason() const {
  return esp_reset_reason();
}

const char* SleepManager::sleepModeName(SleepMode mode) {
  switch (mode) {
    case SleepMode::Light:
      return "light";
    case SleepMode::Deep:
      return "deep";
    case SleepMode::None:
    default:
      return "none";
  }
}

const char* SleepManager::wakeCauseName(esp_sleep_wakeup_cause_t cause) {
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
      return "not sleep";
    default:
      return "unknown";
  }
}

const char* SleepManager::resetReasonName(esp_reset_reason_t reason) {
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
#if defined(ESP_RST_USB)
    case ESP_RST_USB:
      return "usb";
#endif
    case ESP_RST_UNKNOWN:
    default:
      return "unknown";
  }
}

bool SleepManager::paperS3TouchDeepWakeSupported() {
  return false;
}

bool SleepManager::configurePaperS3TouchLightWake() {
#if defined(ENABLE_PAPERS3_LIGHT_SLEEP_TOUCH_WAKE) && !ENABLE_PAPERS3_LIGHT_SLEEP_TOUCH_WAKE
  return false;
#else
  if (M5.getBoard() != m5::board_t::board_M5PaperS3) {
    return false;
  }

  pinMode(static_cast<uint8_t>(kPaperS3TouchIntPin), INPUT_PULLUP);
  const uint32_t deadline = millis() + 250;
  while (digitalRead(static_cast<uint8_t>(kPaperS3TouchIntPin)) == LOW && millis() < deadline) {
    M5.update();
    delay(10);
  }
  if (digitalRead(static_cast<uint8_t>(kPaperS3TouchIntPin)) == LOW) {
    Serial.println("SleepManager: skip touch wake; GT911 INT still active");
    return false;
  }

  if (gpio_wakeup_enable(kPaperS3TouchIntPin, GPIO_INTR_LOW_LEVEL) != ESP_OK) {
    return false;
  }
  if (esp_sleep_enable_gpio_wakeup() != ESP_OK) {
    gpio_wakeup_disable(kPaperS3TouchIntPin);
    return false;
  }
  return true;
#endif
}

void SleepManager::clearPaperS3TouchLightWake(bool configured) {
  if (configured) {
    gpio_wakeup_disable(kPaperS3TouchIntPin);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  }
}

}  // namespace hw
}  // namespace paperbadge
