#pragma once

#include <Arduino.h>

namespace paperbadge {
namespace reader {

struct ReaderStateData {
  String lastPath;
  uint32_t page = 0;
  uint32_t byteOffset = 0;
  uint8_t fontSize = 2;
};

class ReaderState {
 public:
  bool load();
  bool save() const;
  void set(const String& path, uint32_t page, uint32_t byteOffset, uint8_t fontSize);
  const ReaderStateData& data() const { return data_; }

 private:
  ReaderStateData data_;
};

}  // namespace reader
}  // namespace paperbadge
