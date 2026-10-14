// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the session model: which tabs exist, in which order, and how they
// are doing.
//
// This is deliberately CEF-free: the shell tells the session what happened and
// the session tells the views what to draw, so tab logic (ordering, activation
// rules, sleeping, restore) is unit tested without a running browser. It is also
// what makes crash-safe session restore possible - the whole state is one small
// JSON document written on a timer and on exit.

#ifndef BROWSERX_BROWSER_SESSION_H_
#define BROWSERX_BROWSER_SESSION_H_

#include <cstdint>
#include <string>
#include <vector>

namespace browserx {
namespace browser {

struct TabState {
  int id = 0;
  std::string url;
  std::string title;
  std::string group;          // "" = ungrouped; otherwise a group name
  bool pinned = false;
  bool muted = false;
  bool sleeping = false;      // renderer discarded; the tab keeps url/title
  bool audible = false;
  int zoom_percent = 100;
  int64_t last_active_ms = 0;
  int64_t created_ms = 0;
};

/// How a tab should be treated by the memory policy.
enum class TabLifecycle { kActive, kHidden, kSleeping };

class Session {
 public:
  /// Restores from JSON when it parses and is not newer than this build's
  /// format; otherwise returns an empty session with one blank tab.
  static Session Restore(const std::string& json, std::string* error);

  int AddTab(const std::string& url, bool activate);
  /// Adds a tab next to `after_id` (like Ctrl+click on a link).
  int AddTabAfter(int after_id, const std::string& url, bool activate);
  bool CloseTab(int id);
  bool MoveTab(int id, int new_index);
  bool Activate(int id);
  bool SetTitle(int id, const std::string& title);
  bool SetUrl(int id, const std::string& url);
  bool SetPinned(int id, bool pinned);
  bool SetMuted(int id, bool muted);
  bool SetAudible(int id, bool audible);
  bool SetGroup(int id, const std::string& group);
  bool SetZoom(int id, int zoom_percent);
  bool SetSleeping(int id, bool sleeping);
  /// Removes the sleeping flag and updates the activity timestamp.
  bool Wake(int id);

  const TabState* Find(int id) const;
  TabState* MutableFind(int id);
  int active_id() const { return active_id_; }
  const TabState* active_tab() const { return Find(active_id_); }
  const std::vector<TabState>& tabs() const { return tabs_; }
  size_t count() const { return tabs_.size(); }
  bool empty() const { return tabs_.empty(); }

  int IndexOf(int id) const;
  /// Id of the neighbour `delta` positions away in the visual order (wraps).
  int NeighbourId(int id, int delta) const;
  /// The next tab to close when the memory policy needs one: hidden first, then
  /// oldest, never pinned and never audible.
  int LruCandidateId() const;
  /// Ids of tabs that have been hidden (not active, not pinned, not audible) for
  /// at least `idle_after_ms`, oldest first.
  std::vector<int> IdleTabIds(int64_t now_ms, int64_t idle_after_ms) const;

  int CountKind(TabLifecycle kind) const;

  /// Monotonic milliseconds, process-relative. Public so the memory policy and
  /// the UI can reason about tab age in the same units the session uses.
  static int64_t NowMs();
  /// Puts up to `max_count` idle hidden tabs to sleep (oldest first) and returns
  /// their ids. This is what the memory policy calls: never the visible tab,
  /// never a pinned or audible tab.
  std::vector<int> SleepIdleTabs(int max_count, int64_t idle_after_ms);
  /// Only the URL of the active tab (used by the window title and the tests).
  std::string ActiveUrl() const;

  std::string ToJson() const;
  bool FromJson(const std::string& json, std::string* error);

  /// "3 tabs (1 sleeping)" for the status bar.
  std::string DescribeCounts() const;

 private:
  std::vector<TabState> tabs_;
  int active_id_ = 0;
  int next_id_ = 1;
};

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_SESSION_H_
