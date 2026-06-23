#pragma once

#include <Arduino.h>

namespace paperbadge {
namespace reader {

class EpubReader {
 public:
  bool open(const String&) { return false; }
  const char* status() const {
    return "EPUB is indexed but not parsed yet. TXT/MD are supported now.";
  }
};

}  // namespace reader
}  // namespace paperbadge
