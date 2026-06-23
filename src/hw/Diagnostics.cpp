#include "Diagnostics.h"

#include <Esp.h>

namespace paperbadge {
namespace hw {

DiagnosticsSnapshot Diagnostics::capture() {
  DiagnosticsSnapshot snapshot;
  snapshot.freeHeap = ESP.getFreeHeap();
  snapshot.minFreeHeap = ESP.getMinFreeHeap();
  snapshot.freePsram = ESP.getFreePsram();
  snapshot.psramSize = ESP.getPsramSize();
  snapshot.sketchSize = ESP.getSketchSize();
  snapshot.freeSketchSpace = ESP.getFreeSketchSpace();
  snapshot.cpuMhz = static_cast<uint32_t>(ESP.getCpuFreqMHz());
  snapshot.uptimeMs = millis();
  return snapshot;
}

String Diagnostics::heapLine(const DiagnosticsSnapshot& snapshot) {
  return String("heap: ") + snapshot.freeHeap + " free min " + snapshot.minFreeHeap + " psram " +
         snapshot.freePsram + "/" + snapshot.psramSize;
}

String Diagnostics::sketchLine(const DiagnosticsSnapshot& snapshot) {
  return String("firmware: ") + snapshot.sketchSize + " bytes free OTA " + snapshot.freeSketchSpace;
}

}  // namespace hw
}  // namespace paperbadge
