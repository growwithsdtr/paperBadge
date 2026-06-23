#include "SDManager.h"

#include <ArduinoJson.h>
#include <M5Unified.h>
#include <algorithm>

namespace paperbadge {
namespace hw {

namespace {
constexpr const char* kIndexPath = "/paperBadge/library_index.json";
constexpr size_t kMaxIndexedBooks = 96;

String lowerCopy(String value) {
  value.toLowerCase();
  return value;
}

String extensionOf(const String& path) {
  const int dot = path.lastIndexOf('.');
  if (dot < 0) return "";
  return lowerCopy(path.substring(dot));
}

String titleFromPath(const String& path) {
  int slash = path.lastIndexOf('/');
  String title = slash >= 0 ? path.substring(slash + 1) : path;
  const int dot = title.lastIndexOf('.');
  if (dot > 0) title = title.substring(0, dot);
  title.replace('_', ' ');
  return title;
}

bool skipDirName(const String& path) {
  String lower = lowerCopy(path);
  return lower.endsWith("/system volume information") || lower.endsWith("/.spotlight-v100") ||
         lower.endsWith("/.trashes") || lower.endsWith("/papercoach") || lower.endsWith("/paperbadge/fonts");
}
}  // namespace

bool SDManager::mount(uint32_t spiHz) {
  const int8_t sclk = M5.getPin(m5::sd_spi_sclk);
  const int8_t mosi = M5.getPin(m5::sd_spi_mosi);
  const int8_t miso = M5.getPin(m5::sd_spi_miso);
  const int8_t cs = M5.getPin(m5::sd_spi_cs);

  Serial.printf("SDManager: SPI pins SCLK=%d MOSI=%d MISO=%d CS=%d\n", sclk, mosi, miso, cs);
  if (sclk < 0 || mosi < 0 || miso < 0 || cs < 0) {
    mounted_ = false;
    lastStatus_ = "PaperS3 SD pins unavailable";
    return false;
  }

  SPI.begin(sclk, miso, mosi, cs);
  mounted_ = SD.begin(cs, SPI, spiHz);
  lastStatus_ = mounted_ ? "mounted" : "mount failed";
  if (mounted_) {
    ensureAppDir();
    loadIndexCache();
  }
  return mounted_;
}

const std::vector<BookEntry>& SDManager::listBooks(bool forceRefresh) {
  if (!mounted_) {
    books_.clear();
    return books_;
  }

  if (forceRefresh || (!indexLoaded_ && books_.empty())) {
    buildIndex();
  }
  return books_;
}

bool SDManager::buildIndex() {
  if (!mounted_) {
    lastStatus_ = "not mounted";
    return false;
  }

  books_.clear();
  scanDir("/paperBadge/books", 0);
  scanDir("/books", 0);
  scanDir("/", 0);

  std::sort(books_.begin(), books_.end(), [](const BookEntry& lhs, const BookEntry& rhs) {
    return lowerCopy(lhs.title) < lowerCopy(rhs.title);
  });

  lastIndexMs_ = millis();
  indexLoaded_ = true;
  lastStatus_ = "index built";
  saveIndexCache();
  return true;
}

bool SDManager::loadIndexCache() {
  if (!mounted_ || !SD.exists(kIndexPath)) {
    indexLoaded_ = false;
    return false;
  }

  File file = SD.open(kIndexPath, FILE_READ);
  if (!file) {
    indexLoaded_ = false;
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    indexLoaded_ = false;
    lastStatus_ = "index parse failed";
    return false;
  }

  books_.clear();
  JsonArray books = doc["books"].as<JsonArray>();
  for (JsonObject book : books) {
    if (books_.size() >= kMaxIndexedBooks) break;
    BookEntry entry;
    entry.path = book["path"] | "";
    entry.title = book["title"] | titleFromPath(entry.path);
    entry.extension = book["extension"] | extensionOf(entry.path);
    entry.size = book["size"] | 0;
    if (entry.path.length() > 0 && isSupportedBookPath(entry.path)) {
      books_.push_back(entry);
    }
  }

  indexLoaded_ = true;
  lastIndexMs_ = millis();
  lastStatus_ = "index cache loaded";
  return true;
}

bool SDManager::saveIndexCache() const {
  if (!mounted_) return false;
  ensureAppDir();
  if (SD.exists(kIndexPath)) {
    SD.remove(kIndexPath);
  }

  File file = SD.open(kIndexPath, FILE_WRITE);
  if (!file) return false;

  JsonDocument doc;
  doc["generated_ms"] = millis();
  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& entry : books_) {
    JsonObject book = books.add<JsonObject>();
    book["path"] = entry.path;
    book["title"] = entry.title;
    book["extension"] = entry.extension;
    book["size"] = entry.size;
  }
  serializeJson(doc, file);
  file.close();
  return true;
}

bool SDManager::isSupportedBookPath(const String& path) {
  const String ext = extensionOf(path);
  return ext == ".txt" || ext == ".md" || ext == ".epub";
}

void SDManager::scanDir(const String& dirPath, uint8_t depth) {
  if (books_.size() >= kMaxIndexedBooks || depth > 3 || skipDirName(dirPath)) {
    return;
  }
  if (!SD.exists(dirPath)) {
    return;
  }

  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  for (File file = dir.openNextFile(); file && books_.size() < kMaxIndexedBooks; file = dir.openNextFile()) {
    String name = file.name();
    if (name.length() == 0) {
      file.close();
      continue;
    }
    if (name[0] == '.') {
      file.close();
      continue;
    }

    String path = name.startsWith("/") ? name : (dirPath == "/" ? "/" + name : dirPath + "/" + name);
    if (file.isDirectory()) {
      scanDir(path, depth + 1);
    } else if (isSupportedBookPath(path)) {
      addBook(path, static_cast<uint32_t>(file.size()));
    }
    file.close();
  }
  dir.close();
}

void SDManager::addBook(const String& path, uint32_t size) {
  if (hasBook(path) || books_.size() >= kMaxIndexedBooks) {
    return;
  }

  BookEntry entry;
  entry.path = path;
  entry.title = titleFromPath(path);
  entry.extension = extensionOf(path);
  entry.size = size;
  books_.push_back(entry);
}

bool SDManager::ensureAppDir() const {
  if (SD.exists("/paperBadge")) {
    return true;
  }
  return SD.mkdir("/paperBadge");
}

bool SDManager::hasBook(const String& path) const {
  for (const auto& book : books_) {
    if (book.path == path) return true;
  }
  return false;
}

}  // namespace hw
}  // namespace paperbadge
