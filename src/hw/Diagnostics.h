#pragma once

#include <Arduino.h>

namespace paperbadge {
namespace hw {

struct DiagnosticsSnapshot {
  uint32_t freeHeap = 0;
  uint32_t minFreeHeap = 0;
  uint32_t freePsram = 0;
  uint32_t psramSize = 0;
  uint32_t sketchSize = 0;
  uint32_t freeSketchSpace = 0;
  uint32_t cpuMhz = 0;
  uint32_t uptimeMs = 0;
};

class Diagnostics {
 public:
  static DiagnosticsSnapshot capture();
  static String heapLine(const DiagnosticsSnapshot& snapshot);
  static String sketchLine(const DiagnosticsSnapshot& snapshot);
};

}  // namespace hw
}  // namespace paperbadge
