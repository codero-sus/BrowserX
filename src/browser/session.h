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

  // --- bulk tab operations ---------------------------------------------------
  // These are the ones a person reaches for after a while: close everything else, close
  // the tabs to the right, get rid of the duplicates, put the tabs back in a sane order.
  // They live here (CEF-free, tested) rather than in a callback, because getting the
  // active tab wrong after a close is exactly the bug that makes a browser feel broken.

  /// Closes every tab except `keep_id` (which becomes active). Pinned tabs survive, as
  /// they do in Chromium. Returns how many closed; never leaves the session empty.
  size_t CloseOthers(int keep_id);
  /// Closes the unpinned tabs to the right of `id`, leaving `id` active. Returns how many
  /// closed. Pinned tabs are never swept away.
  size_t CloseToTheRight(int id);
  /// Closes tabs whose canonical URL is already open, keeping the oldest of each pair.
  /// Returns how many closed; pinned tabs are never treated as duplicates of a plain tab
  /// with the same address, and are never removed.
  size_t CloseDuplicates();
  /// Reorders tabs by the host of their URL, keeping pinned tabs in front and the active
  /// tab active. Stable within a host, so tabs that belong together stay in order.
  void SortByHost();
  /// Moves a tab to the front of the unpinned group (pin without pinning).
  bool MoveToFront(int id);

  /// Remembers a closed tab so it can be reopened (Ctrl+Shift+T), and returns its id.
  /// The stack is bounded; the oldest entry falls off the end.
  static constexpr size_t kMaxRecentlyClosed = 25;
  void RememberClosed(const TabState& tab);
  /// Reopens the most recently closed tab, as a new tab next to the active one. Returns
  /// the new id, or 0 when there is nothing to reopen.
  int ReopenLastClosed(bool activate = true);
  size_t recently_closed_count() const { return recently_closed_.size(); }
  const std::vector<TabState>& recently_closed() const { return recently_closed_; }

  /// The N most recently used tab ids (most recent first), for a "recent tabs" menu.
  std::vector<int> RecentlyActive(int max_count) const;
  /// Total zoom factor applied to a tab, as a multiplier - what the UI shows as "110%".
  static double ZoomFactor(const TabState& tab);
  /// Sleeps tabs so that at most `max_live` renderers stay awake, choosing the least
  /// recently used first and never touching the active, pinned or audible tabs. Returns
  /// the ids that were put to sleep.
  std::vector<int> SleepBeyondLiveLimit(int max_live);
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
  std::vector<TabState> recently_closed_;  // newest first, bounded by kMaxRecentlyClosed
  int active_id_ = 0;
  int next_id_ = 1;
};

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_SESSION_H_
