#pragma once

#include <Arduino.h>
#include <vector>

#include "../hw/SDManager.h"

namespace paperbadge {
namespace reader {

class BookLibrary {
 public:
  void begin(hw::SDManager* sdManager);
  bool refresh(bool force);
  size_t count() const { return books_.size(); }
  const hw::BookEntry* at(size_t index) const;
  const std::vector<hw::BookEntry>& books() const { return books_; }
  const char* status() const;

 private:
  hw::SDManager* sd_ = nullptr;
  std::vector<hw::BookEntry> books_;
};

}  // namespace reader
}  // namespace paperbadge
