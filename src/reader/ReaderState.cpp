#include "ReaderState.h"

#include <ArduinoJson.h>
#include <SD.h>

namespace paperbadge {
namespace reader {

namespace {
constexpr const char* kReaderStatePath = "/paperBadge/reader_state.json";
}

bool ReaderState::load() {
  if (!SD.exists(kReaderStatePath)) {
    return false;
  }

  File file = SD.open(kReaderStatePath, FILE_READ);
  if (!file) {
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  data_.lastPath = doc["last_path"] | "";
  data_.page = doc["page"] | 0;
  data_.byteOffset = doc["byte_offset"] | 0;
  data_.fontSize = doc["font_size"] | 2;
  return true;
}

bool ReaderState::save() const {
  if (!SD.exists("/paperBadge")) {
    SD.mkdir("/paperBadge");
  }
  if (SD.exists(kReaderStatePath)) {
    SD.remove(kReaderStatePath);
  }

  File file = SD.open(kReaderStatePath, FILE_WRITE);
  if (!file) {
    return false;
  }

  JsonDocument doc;
  doc["last_path"] = data_.lastPath;
  doc["page"] = data_.page;
  doc["byte_offset"] = data_.byteOffset;
  doc["font_size"] = data_.fontSize;
  serializeJson(doc, file);
  file.close();
  return true;
}

void ReaderState::set(const String& path, uint32_t page, uint32_t byteOffset, uint8_t fontSize) {
  data_.lastPath = path;
  data_.page = page;
  data_.byteOffset = byteOffset;
  data_.fontSize = fontSize;
}

}  // namespace reader
}  // namespace paperbadge
