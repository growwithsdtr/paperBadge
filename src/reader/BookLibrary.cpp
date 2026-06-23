#include "BookLibrary.h"

namespace paperbadge {
namespace reader {

void BookLibrary::begin(hw::SDManager* sdManager) {
  sd_ = sdManager;
}

bool BookLibrary::refresh(bool force) {
  books_.clear();
  if (!sd_ || !sd_->isMounted()) {
    return false;
  }

  const auto& source = sd_->listBooks(force);
  books_.assign(source.begin(), source.end());
  return true;
}

const hw::BookEntry* BookLibrary::at(size_t index) const {
  if (index >= books_.size()) {
    return nullptr;
  }
  return &books_[index];
}

const char* BookLibrary::status() const {
  if (!sd_) return "sd unavailable";
  return sd_->lastStatus();
}

}  // namespace reader
}  // namespace paperbadge
