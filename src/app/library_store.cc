// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/library_store.h"

#include "core/log.h"

namespace browserx {
namespace app {
namespace {

/// Loads one JSON-backed store. A missing file is a new profile (quiet); a corrupt one is
/// preserved next to the original, because a parse bug must never silently delete what a
/// person browsed.
template <typename Store>
void LoadOne(const std::string& path, const char* label, Store* store, std::string* note,
             std::string* error) {
  if (!FileExists(path)) {
    *note += *note == "" ? "" : "; ";
    *note += std::string(label) + ": none yet";
    return;
  }
  std::string content;
  if (!ReadFileToString(path, &content)) {
    *error = std::string("could not read ") + path;
    core::log::WarnF("library", "%s", error->c_str());
    return;
  }
  std::string parse_error;
  if (!store->FromJson(content, &parse_error)) {
    const std::string preserved = path + ".corrupt";
    WriteStringToFileAtomically(preserved, content);
    *error = std::string(label) + ": " + parse_error + " (" + preserved + " kept)";
    core::log::WarnF("library", "%s", error->c_str());
    return;
  }
  *note += *note == "" ? "" : "; ";
  *note += std::string(label) + ": " + std::to_string(store->size());
}

}  // namespace

void LibraryStore::Load(const Paths& paths) {
  history_path_ = paths.history_file();
  bookmarks_path_ = paths.bookmarks_file();
  last_note_.clear();
  last_error_.clear();
  LoadOne(history_path_, "history", &history_, &last_note_, &last_error_);
  LoadOne(bookmarks_path_, "bookmarks", &bookmarks_, &last_note_, &last_error_);
}

bool LibraryStore::SaveNow() {
  last_error_.clear();
  if (!WriteStringToFileAtomically(history_path_, history_.ToJson() + "\n")) {
    last_error_ = "could not write " + history_path_;
    core::log::WarnF("library", "%s", last_error_.c_str());
    return false;
  }
  if (!WriteStringToFileAtomically(bookmarks_path_, bookmarks_.ToJson() + "\n")) {
    last_error_ = "could not write " + bookmarks_path_;
    core::log::WarnF("library", "%s", last_error_.c_str());
    return false;
  }
  dirty_ = false;
  return true;
}

bool LibraryStore::SaveIfDirty() {
  if (!dirty_) return true;
  return SaveNow();
}

bool LibraryStore::RecordVisit(const std::string& url, const std::string& title, int64_t now_ms) {
  if (!history_.Add(url, title, now_ms)) return false;
  MarkDirty();
  return true;
}

void LibraryStore::ApplyLimits(size_t max_history, size_t max_bookmarks) {
  history_.SetLimit(max_history);
  bookmarks_.SetLimit(max_bookmarks);
  MarkDirty();
}

}  // namespace app
}  // namespace browserx
