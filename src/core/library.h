// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the library: history and bookmarks.
//
// CEF-free on purpose, like everything else in src/core: two plain data structures
// with JSON persistence, so the rules that matter (what counts as the same page, how
// a list is capped, what "clear the last hour" means) are unit tested instead of being
// discovered by using the browser.
//
// The store does not own a clock and does not touch the disk behind the caller's back:
// timestamps are passed in, and saving is an explicit call. That is what makes history
// testable and what keeps a browser that is closing from writing half a file.

#ifndef BROWSERX_CORE_LIBRARY_H_
#define BROWSERX_CORE_LIBRARY_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace core {

/// One page in the history: the canonical URL, the best title we have seen for it, how
/// many times it was opened, and when it was opened last.
struct Visit {
  std::string url;
  std::string title;
  int64_t visited_ms = 0;
  int64_t visit_count = 0;
};

/// One bookmark. A folder is a label, not a tree: enough to group ("work", "reading")
/// without a database, and the file format can grow a real tree later if it needs one.
struct Bookmark {
  std::string url;
  std::string title;
  std::string folder;
  int64_t added_ms = 0;
};

/// Browsing history, most recent first.
class History {
 public:
  /// How many distinct pages are kept. Older entries fall off the end; the default is
  /// deliberately modest because history is a convenience, not an archive.
  static constexpr size_t kDefaultLimit = 10000;

  void SetLimit(size_t max_entries);
  size_t limit() const { return limit_; }

  /// Records a visit. De-duplicates by the canonical form of the URL (see
  /// url::NormalizeForCompare), so "example.com/" and "example.com#top" are one entry
  /// with two visits rather than two entries. Returns false when the URL is not
  /// something worth remembering (empty, or a scheme we do not record).
  bool Add(std::string_view url, std::string_view title, int64_t now_ms);

  /// The title is only kept when it is better than what we had: a real title beats an
  /// empty one, but a page that later reports an empty title must not erase it.
  void SetTitle(std::string_view url, std::string_view title);

  bool Remove(std::string_view url);
  /// Forgets everything at or after `since_ms` (0 clears all). Returns how many went.
  size_t ClearSince(int64_t since_ms);

  const std::vector<Visit>& entries() const { return entries_; }
  size_t size() const { return entries_.size(); }
  /// Total visits including repeats - the number to show on the history page.
  int64_t total_visits() const;

  /// Case-insensitive search over URL and title, most recent first.
  std::vector<Visit> Search(std::string_view text, size_t max_results = 100) const;
  /// The pages opened most often, for the start page's quick links.
  std::vector<Visit> MostVisited(size_t max_results = 8) const;
  /// Everything opened after `since_ms`, used by "reopen what I just closed" flows.
  std::vector<Visit> Since(int64_t since_ms, size_t max_results = 25) const;

  std::string ToJson() const;
  bool FromJson(std::string_view json_text, std::string* error);
  /// True when the URL is worth recording at all (http/https/browserx, no javascript:).
  static bool IsRecordable(std::string_view url);

 private:
  std::vector<Visit> entries_;
  size_t limit_ = kDefaultLimit;
};

/// Bookmarks, in the order they were added.
class Bookmarks {
 public:
  static constexpr size_t kDefaultLimit = 5000;

  void SetLimit(size_t max_entries) { limit_ = max_entries; }
  size_t limit() const { return limit_; }

  bool Add(std::string_view url, std::string_view title, std::string_view folder,
           int64_t now_ms);
  bool Remove(std::string_view url);
  bool Contains(std::string_view url) const;
  /// Adds when absent, removes when present. Returns the state afterwards, so the UI can
  /// say "Bookmarked" or "Bookmark removed" without a second lookup.
  bool Toggle(std::string_view url, std::string_view title, int64_t now_ms);
  /// Moves a bookmark into a folder (empty folder means "no folder").
  bool SetFolder(std::string_view url, std::string_view folder);

  const std::vector<Bookmark>& all() const { return entries_; }
  size_t size() const { return entries_.size(); }
  std::vector<Bookmark> Search(std::string_view text) const;
  std::vector<Bookmark> InFolder(std::string_view folder) const;
  /// Folder names, in the order they first appear, without duplicates or empties.
  std::vector<std::string> Folders() const;

  std::string ToJson() const;
  bool FromJson(std::string_view json_text, std::string* error);
  /// True when a URL belongs in the bookmark list at all.
  static bool IsBookmarkUrl(std::string_view url);

  /// Netscape bookmark format - the one every browser still reads and writes. Exporting
  /// it means leaving BrowserX does not mean losing the list, and importing means the
  /// list a person already has comes across. Returns how many were added.
  std::string ToNetscapeHtml() const;
  size_t ImportNetscapeHtml(std::string_view html, int64_t now_ms);

 private:
  std::vector<Bookmark> entries_;
  size_t limit_ = kDefaultLimit;
};

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_LIBRARY_H_
