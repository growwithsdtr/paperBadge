#pragma once

#include <Arduino.h>

namespace paperbadge {
namespace reader {

struct PageCacheStats {
  uint32_t pageCount = 0;
  uint32_t sourceBytes = 0;
  bool truncated = false;
};

class PageCache {
 public:
  void setStats(PageCacheStats stats) { stats_ = stats; }
  const PageCacheStats& stats() const { return stats_; }

 private:
  PageCacheStats stats_;
};

}  // namespace reader
}  // namespace paperbadge
