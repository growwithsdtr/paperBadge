#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

namespace paperbadge {
namespace hw {

struct BookEntry {
  String path;
  String title;
  String extension;
  uint32_t size = 0;
};

class SDManager {
 public:
  bool mount(uint32_t spiHz);
  bool isMounted() const { return mounted_; }
  const std::vector<BookEntry>& listBooks(bool forceRefresh = false);
  bool buildIndex();
  bool loadIndexCache();
  bool saveIndexCache() const;
  uint32_t lastIndexMs() const { return lastIndexMs_; }
  const char* lastStatus() const { return lastStatus_; }
  size_t bookCount() const { return books_.size(); }

  static bool isSupportedBookPath(const String& path);

 private:
  void scanDir(const String& dirPath, uint8_t depth);
  void addBook(const String& path, uint32_t size);
  bool ensureAppDir() const;
  bool hasBook(const String& path) const;

  bool mounted_ = false;
  bool indexLoaded_ = false;
  uint32_t lastIndexMs_ = 0;
  const char* lastStatus_ = "not mounted";
  std::vector<BookEntry> books_;
};

}  // namespace hw
}  // namespace paperbadge
