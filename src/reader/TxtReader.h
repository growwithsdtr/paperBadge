#pragma once

#include <Arduino.h>
#include <vector>

namespace paperbadge {
namespace reader {

struct TxtPageView {
  uint32_t page = 0;
  uint32_t pageCount = 0;
  uint32_t byteOffset = 0;
  uint16_t firstLine = 0;
  uint16_t lineCount = 0;
};

class TxtReader {
 public:
  bool open(const String& path, uint16_t charsPerLine, uint8_t linesPerPage);
  void close();
  bool isOpen() const { return open_; }
  bool truncated() const { return truncated_; }
  const String& path() const { return path_; }
  const String& title() const { return title_; }
  uint32_t pageCount() const;
  uint32_t currentPage() const { return currentPage_; }
  bool goToPage(uint32_t page);
  bool nextPage();
  bool previousPage();
  TxtPageView pageView() const;
  const String& lineAt(uint16_t absoluteLine) const;
  uint32_t currentByteOffset() const;

 private:
  void paginate(uint16_t charsPerLine, uint8_t linesPerPage);
  static String titleFromPath(const String& path);

  bool open_ = false;
  bool truncated_ = false;
  String path_;
  String title_;
  String text_;
  std::vector<String> lines_;
  std::vector<uint32_t> lineOffsets_;
  uint8_t linesPerPage_ = 12;
  uint32_t currentPage_ = 0;
};

}  // namespace reader
}  // namespace paperbadge
