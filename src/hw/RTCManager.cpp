#include "RTCManager.h"

#include <cstring>

namespace paperbadge {
namespace hw {

namespace {
constexpr uint32_t kRtcMagic = 0x50425254;  // PBRT

struct PersistedRTCState {
  uint32_t magic = 0;
  uint32_t bootCount = 0;
  uint8_t lastSelectedApp = static_cast<uint8_t>(PersistedApp::Unknown);
  char lastReaderBook[96] = {};
  uint32_t lastReaderOffset = 0;
  uint32_t lastWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  uint8_t lastSleepMode = 0;
  uint32_t lastSleepDurationMs = 0;
};

RTC_DATA_ATTR PersistedRTCState gRtcState;
}  // namespace

void RTCManager::begin() {
  ensureInitialized();
  ++gRtcState.bootCount;
  gRtcState.lastWakeCause = static_cast<uint32_t>(esp_sleep_get_wakeup_cause());
}

RTCStateSnapshot RTCManager::snapshot() const {
  RTCStateSnapshot copy;
  copy.bootCount = gRtcState.bootCount;
  copy.lastSelectedApp = static_cast<PersistedApp>(gRtcState.lastSelectedApp);
  strlcpy(copy.lastReaderBook, gRtcState.lastReaderBook, sizeof(copy.lastReaderBook));
  copy.lastReaderOffset = gRtcState.lastReaderOffset;
  copy.lastWakeCause = static_cast<esp_sleep_wakeup_cause_t>(gRtcState.lastWakeCause);
  copy.lastSleepMode = gRtcState.lastSleepMode;
  copy.lastSleepDurationMs = gRtcState.lastSleepDurationMs;
  return copy;
}

void RTCManager::setLastSelectedApp(PersistedApp app) {
  ensureInitialized();
  gRtcState.lastSelectedApp = static_cast<uint8_t>(app);
}

void RTCManager::setLastReaderPosition(const String& path, uint32_t byteOffset) {
  ensureInitialized();
  strlcpy(gRtcState.lastReaderBook, path.c_str(), sizeof(gRtcState.lastReaderBook));
  gRtcState.lastReaderOffset = byteOffset;
  gRtcState.lastSelectedApp = static_cast<uint8_t>(PersistedApp::Reader);
}

void RTCManager::markSleep(uint8_t mode, uint32_t durationMs) {
  ensureInitialized();
  gRtcState.lastSleepMode = mode;
  gRtcState.lastSleepDurationMs = durationMs;
}

void RTCManager::markWake(esp_sleep_wakeup_cause_t cause) {
  ensureInitialized();
  gRtcState.lastWakeCause = static_cast<uint32_t>(cause);
}

const char* RTCManager::appName(PersistedApp app) {
  switch (app) {
    case PersistedApp::Badge:
      return "Badge";
    case PersistedApp::Home:
      return "Home";
    case PersistedApp::Interview:
      return "Interview";
    case PersistedApp::Japanese:
      return "Japanese";
    case PersistedApp::Reader:
      return "Reader";
    case PersistedApp::Settings:
      return "Settings";
    case PersistedApp::Unknown:
    default:
      return "Unknown";
  }
}

void RTCManager::ensureInitialized() {
  if (gRtcState.magic == kRtcMagic) {
    return;
  }
  gRtcState = {};
  gRtcState.magic = kRtcMagic;
  gRtcState.lastSelectedApp = static_cast<uint8_t>(PersistedApp::Unknown);
}

}  // namespace hw
}  // namespace paperbadge
