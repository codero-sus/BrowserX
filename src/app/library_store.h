// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the library store: history and bookmarks, on disk.
//
// The data structures live in src/core/library.h and know nothing about files. This is
// the thin app-layer part that owns the two paths in the profile, loads both at start-up
// and writes them back atomically — the same shape as SettingsStore, for the same reason:
// one place decides when BrowserX touches the disk, and a crash mid-save can never leave a
// half-written history behind.
//
// Deliberately CEF-free, so `make test` covers it: the interesting part is not the file
// calls but the truthfulness rules around them (a corrupt file must not be silently
// replaced, an unreadable one must be reported).

#ifndef BROWSERX_APP_LIBRARY_STORE_H_
#define BROWSERX_APP_LIBRARY_STORE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "app/app_paths.h"
#include "core/library.h"

namespace browserx {
namespace app {

class LibraryStore {
 public:
  /// Reads history.json and bookmarks.json from `paths`. Missing files are normal (a new
  /// profile) and produce a note, not an error. A corrupt file is preserved as
  /// `<name>.corrupt` and reported, so the user never loses data to a parse bug.
  void Load(const Paths& paths);

  /// Writes both files atomically. Returns false (and sets last_error()) when either
  /// write fails; the in-memory data is untouched either way.
  bool SaveNow();
  void MarkDirty() { dirty_ = true; }
  bool SaveIfDirty();
  bool dirty() const { return dirty_; }

  core::History& history() { return history_; }
  const core::History& history() const { return history_; }
  core::Bookmarks& bookmarks() { return bookmarks_; }
  const core::Bookmarks& bookmarks() const { return bookmarks_; }

  /// Records a visit and marks the store dirty. Returns false when the URL is not
  /// something history should hold (javascript:, the browser's own pages, empty).
  bool RecordVisit(const std::string& url, const std::string& title, int64_t now_ms);

  /// What loading did, in one line, for the log and the telemetry page. Empty when the
  /// store has not been loaded yet.
  const std::string& last_note() const { return last_note_; }
  const std::string& last_error() const { return last_error_; }
  const std::string& history_path() const { return history_path_; }
  const std::string& bookmarks_path() const { return bookmarks_path_; }

  /// Caps, in one place, so a settings change can move them later without hunting for
  /// constants. Both are honest limits: history is a convenience, not an archive.
  void ApplyLimits(size_t max_history, size_t max_bookmarks);

 private:
  core::History history_;
  core::Bookmarks bookmarks_;
  std::string history_path_;
  std::string bookmarks_path_;
  std::string last_note_;
  std::string last_error_;
  bool dirty_ = false;
};

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_LIBRARY_STORE_H_
