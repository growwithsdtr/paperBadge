#include "TxtReader.h"

#include <SD.h>

namespace paperbadge {
namespace reader {

namespace {
constexpr size_t kMaxTxtBytes = 220 * 1024;
const String kEmptyLine;
}

bool TxtReader::open(const String& path, uint16_t charsPerLine, uint8_t linesPerPage) {
  close();

  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    return false;
  }

  path_ = path;
  title_ = titleFromPath(path);
  text_.reserve(min(static_cast<size_t>(file.size()), kMaxTxtBytes));
  size_t readBytes = 0;
  while (file.available() && readBytes < kMaxTxtBytes) {
    text_ += static_cast<char>(file.read());
    ++readBytes;
  }
  truncated_ = file.available();
  file.close();

  paginate(charsPerLine, linesPerPage);
  open_ = true;
  return !lines_.empty();
}

bool TxtReader::openFromString(const String& title, const String& content,
                                uint16_t charsPerLine, uint8_t linesPerPage) {
  close();
  path_ = String("@embedded:") + title;
  title_ = title;
  text_ = content;
  truncated_ = false;
  paginate(charsPerLine, linesPerPage);
  open_ = true;
  return !lines_.empty();
}

void TxtReader::close() {
  open_ = false;
  truncated_ = false;
  path_ = "";
  title_ = "";
  text_ = "";
  lines_.clear();
  lineOffsets_.clear();
  currentPage_ = 0;
}

uint32_t TxtReader::pageCount() const {
  if (lines_.empty()) return 0;
  return (lines_.size() + linesPerPage_ - 1) / linesPerPage_;
}

bool TxtReader::goToPage(uint32_t page) {
  const uint32_t count = pageCount();
  if (count == 0) {
    currentPage_ = 0;
    return false;
  }
  currentPage_ = min(page, count - 1);
  return true;
}

bool TxtReader::nextPage() {
  if (currentPage_ + 1 >= pageCount()) {
    return false;
  }
  ++currentPage_;
  return true;
}

bool TxtReader::previousPage() {
  if (currentPage_ == 0) {
    return false;
  }
  --currentPage_;
  return true;
}

TxtPageView TxtReader::pageView() const {
  TxtPageView view;
  view.page = currentPage_;
  view.pageCount = pageCount();
  view.firstLine = static_cast<uint16_t>(currentPage_ * linesPerPage_);
  const uint32_t remaining = lines_.size() > view.firstLine ? lines_.size() - view.firstLine : 0;
  view.lineCount = static_cast<uint16_t>(remaining < linesPerPage_ ? remaining : linesPerPage_);
  view.byteOffset = currentByteOffset();
  return view;
}

const String& TxtReader::lineAt(uint16_t absoluteLine) const {
  if (absoluteLine >= lines_.size()) {
    return kEmptyLine;
  }
  return lines_[absoluteLine];
}

uint32_t TxtReader::currentByteOffset() const {
  if (lineOffsets_.empty()) {
    return 0;
  }
  const uint32_t firstLine = currentPage_ * linesPerPage_;
  if (firstLine >= lineOffsets_.size()) {
    return lineOffsets_.back();
  }
  return lineOffsets_[firstLine];
}

void TxtReader::paginate(uint16_t charsPerLine, uint8_t linesPerPage) {
  linesPerPage_ = linesPerPage > 0 ? linesPerPage : 1;
  const uint16_t maxChars = charsPerLine > 12 ? charsPerLine : 12;
  String line;
  line.reserve(maxChars + 2);
  uint32_t lineOffset = 0;

  auto pushLine = [&]() {
    lines_.push_back(line);
    lineOffsets_.push_back(lineOffset);
    line = "";
  };

  for (uint32_t i = 0; i < text_.length(); ++i) {
    const char c = text_[i];
    if (line.length() == 0) {
      lineOffset = i;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      pushLine();
      continue;
    }
    if (line.length() >= maxChars) {
      pushLine();
      lineOffset = i;
    }
    line += c;
  }

  if (line.length() > 0 || lines_.empty()) {
    pushLine();
  }
}

String TxtReader::titleFromPath(const String& path) {
  const int slash = path.lastIndexOf('/');
  String title = slash >= 0 ? path.substring(slash + 1) : path;
  const int dot = title.lastIndexOf('.');
  if (dot > 0) {
    title = title.substring(0, dot);
  }
  title.replace('_', ' ');
  return title;
}

}  // namespace reader
}  // namespace paperbadge
