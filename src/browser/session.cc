// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "browser/session.h"

#include "core/url_utils.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "core/format.h"
#include "core/json.h"
#include "core/log.h"

namespace browserx {
namespace browser {
namespace {

constexpr int kSessionFormatVersion = 1;

std::string StringOf(const core::json::Value& value, const char* key, const std::string& fallback) {
  const core::json::Value& found = value.Get(key);
  return found.IsString() ? found.AsString() : fallback;
}

int64_t IntOf(const core::json::Value& value, const char* key, int64_t fallback) {
  const core::json::Value& found = value.Get(key);
  return found.IsNumber() ? found.AsInt64(fallback) : fallback;
}

bool BoolOf(const core::json::Value& value, const char* key, bool fallback) {
  const core::json::Value& found = value.Get(key);
  return found.IsBool() ? found.AsBool(fallback) : fallback;
}

}  // namespace

/// Monotonic milliseconds. Age is the only thing that matters here, so a
/// process-relative clock is both sufficient and immune to wall-clock jumps.
int64_t Session::NowMs() {
  using clock = std::chrono::steady_clock;
  static const clock::time_point start = clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
}

Session Session::Restore(const std::string& json_text, std::string* error) {
  Session session;
  if (json_text.empty()) {
    session.AddTab("browserx://newtab", true);
    return session;
  }
  if (!session.FromJson(json_text, error)) {
    if (error != nullptr) {
      core::log::WarnF("session", "restore failed: %s", error->c_str());
    }
    session = Session();
    session.AddTab("browserx://newtab", true);
  }
  return session;
}

int Session::AddTab(const std::string& url, bool activate) {
  TabState tab;
  tab.id = next_id_++;
  tab.url = url;
  tab.created_ms = Session::NowMs();
  tab.last_active_ms = tab.created_ms;
  tabs_.push_back(tab);
  if (activate || tabs_.size() == 1) active_id_ = tab.id;
  return tab.id;
}

int Session::AddTabAfter(int after_id, const std::string& url, bool activate) {
  const int id = AddTab(url, false);
  const int index = IndexOf(after_id);
  if (index >= 0) MoveTab(id, index + 1);
  if (activate) Activate(id);
  return id;
}

bool Session::CloseTab(int id) {
  const int index = IndexOf(id);
  if (index < 0) return false;
  const bool was_active = active_id_ == id;
  tabs_.erase(tabs_.begin() + index);
  if (was_active) {
    if (tabs_.empty()) {
      active_id_ = 0;
    } else {
      // Activate the neighbour on the right (Chromium behaviour), else the left.
      const size_t next = static_cast<size_t>(index) < tabs_.size() ? static_cast<size_t>(index)
                                                                   : tabs_.size() - 1;
      active_id_ = tabs_[next].id;
      tabs_[next].last_active_ms = Session::NowMs();
    }
  }
  return true;
}

bool Session::MoveTab(int id, int new_index) {
  const int index = IndexOf(id);
  if (index < 0) return false;
  if (new_index < 0) new_index = 0;
  if (new_index > static_cast<int>(tabs_.size()) - 1) {
    new_index = static_cast<int>(tabs_.size()) - 1;
  }
  if (new_index == index) return true;
  TabState moved = tabs_[static_cast<size_t>(index)];
  tabs_.erase(tabs_.begin() + index);
  tabs_.insert(tabs_.begin() + new_index, moved);
  return true;
}

bool Session::Activate(int id) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  active_id_ = id;
  tab->last_active_ms = Session::NowMs();
  if (tab->sleeping) tab->sleeping = false;  // activating a sleeping tab wakes it
  return true;
}

bool Session::SetTitle(int id, const std::string& title) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  if (tab->title == title) return false;
  tab->title = title;
  return true;
}

bool Session::SetUrl(int id, const std::string& url) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  if (tab->url == url) return false;
  tab->url = url;
  return true;
}

bool Session::SetPinned(int id, bool pinned) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  tab->pinned = pinned;
  // Pinned tabs sort to the front, like every other browser.
  if (pinned) {
    int pinned_count = 0;
    for (const TabState& candidate : tabs_) {
      if (candidate.pinned && candidate.id != id) ++pinned_count;
    }
    MoveTab(id, pinned_count);
  }
  return true;
}

bool Session::SetMuted(int id, bool muted) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  tab->muted = muted;
  return true;
}

bool Session::SetAudible(int id, bool audible) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  tab->audible = audible;
  return true;
}

bool Session::SetGroup(int id, const std::string& group) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  tab->group = group;
  return true;
}

bool Session::SetZoom(int id, int zoom_percent) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  tab->zoom_percent = std::max(25, std::min(500, zoom_percent));
  return true;
}

bool Session::SetSleeping(int id, bool sleeping) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  // The active tab is never allowed to sleep: that would be a visible freeze.
  if (sleeping && id == active_id_) return false;
  tab->sleeping = sleeping;
  return true;
}

bool Session::Wake(int id) {
  TabState* tab = MutableFind(id);
  if (tab == nullptr) return false;
  tab->sleeping = false;
  tab->last_active_ms = Session::NowMs();
  return true;
}

const TabState* Session::Find(int id) const {
  for (const TabState& tab : tabs_) {
    if (tab.id == id) return &tab;
  }
  return nullptr;
}

TabState* Session::MutableFind(int id) {
  for (TabState& tab : tabs_) {
    if (tab.id == id) return &tab;
  }
  return nullptr;
}

int Session::IndexOf(int id) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

int Session::NeighbourId(int id, int delta) const {
  if (tabs_.empty()) return 0;
  const int index = IndexOf(id);
  if (index < 0) return tabs_.front().id;
  const int size = static_cast<int>(tabs_.size());
  int next = (index + delta) % size;
  if (next < 0) next += size;
  return tabs_[static_cast<size_t>(next)].id;
}

int Session::LruCandidateId() const {
  int candidate = 0;
  int64_t oldest = 0;
  for (const TabState& tab : tabs_) {
    if (tab.id == active_id_ || tab.pinned || tab.audible) continue;
    if (candidate == 0 || tab.last_active_ms < oldest) {
      candidate = tab.id;
      oldest = tab.last_active_ms;
    }
  }
  return candidate;
}

std::vector<int> Session::IdleTabIds(int64_t now_ms, int64_t idle_after_ms) const {
  std::vector<const TabState*> idle;
  for (const TabState& tab : tabs_) {
    if (tab.id == active_id_ || tab.pinned || tab.audible) continue;
    if (tab.sleeping && tab.url.empty()) continue;
    if (now_ms - tab.last_active_ms >= idle_after_ms) idle.push_back(&tab);
  }
  std::sort(idle.begin(), idle.end(), [](const TabState* a, const TabState* b) {
    return a->last_active_ms < b->last_active_ms;
  });
  std::vector<int> ids;
  ids.reserve(idle.size());
  for (const TabState* tab : idle) ids.push_back(tab->id);
  return ids;
}

std::vector<int> Session::SleepIdleTabs(int max_count, int64_t idle_after_ms) {
  std::vector<int> slept;
  if (max_count <= 0) return slept;
  const std::vector<int> candidates = IdleTabIds(NowMs(), idle_after_ms);
  for (const int id : candidates) {
    if (static_cast<int>(slept.size()) >= max_count) break;
    if (SetSleeping(id, true)) slept.push_back(id);
  }
  return slept;
}

int Session::CountKind(TabLifecycle kind) const {
  int count = 0;
  for (const TabState& tab : tabs_) {
    switch (kind) {
      case TabLifecycle::kActive:
        if (tab.id == active_id_) ++count;
        break;
      case TabLifecycle::kHidden:
        if (tab.id != active_id_ && !tab.sleeping) ++count;
        break;
      case TabLifecycle::kSleeping:
        if (tab.sleeping) ++count;
        break;
    }
  }
  return count;
}

// ---------------------------------------------------------------------------
// Bulk tab operations
// ---------------------------------------------------------------------------
size_t Session::CloseOthers(int keep_id) {
  if (Find(keep_id) == nullptr) return 0;
  // Collect the victims first, then close them one by one, so CloseTab's activation
  // rules keep running (and never leave the session without an active tab). Pinned tabs
  // survive: pinning is how a person says "do not touch this one" (Chromium's semantics).
  std::vector<int> victims;
  for (const TabState& tab : tabs_) {
    if (tab.id == keep_id || tab.pinned) continue;
    victims.push_back(tab.id);
  }
  // Remembering every one of them means Ctrl+Shift+T can walk back through the whole
  // group in reverse, which is what people expect after closing 20 tabs by accident.
  for (const int id : victims) {
    const TabState* tab = Find(id);
    if (tab != nullptr) RememberClosed(*tab);
  }
  size_t closed = 0;
  for (const int id : victims) {
    if (CloseTab(id)) ++closed;
  }
  Activate(keep_id);
  return closed;
}

size_t Session::CloseToTheRight(int id) {
  const int index = IndexOf(id);
  if (index < 0) return 0;
  std::vector<int> victims;
  for (size_t i = static_cast<size_t>(index) + 1; i < tabs_.size(); ++i) {
    if (tabs_[i].pinned) continue;  // a pinned tab is never swept away
    victims.push_back(tabs_[i].id);
  }
  size_t closed = 0;
  for (const int victim : victims) {
    const TabState* tab = Find(victim);
    if (tab != nullptr) RememberClosed(*tab);
    if (CloseTab(victim)) ++closed;
  }
  return closed;
}

size_t Session::CloseDuplicates() {
  std::vector<int> victims;
  std::vector<std::string> seen;
  for (const TabState& tab : tabs_) {
    if (tab.pinned) continue;  // a pinned tab is a deliberate second copy
    // The same rule history uses: tracking parameters and the fragment are not part of
    // a page's identity, so "?utm_source=newsletter" is not a second copy of an article.
    const std::string key = core::url::NormalizeForCompare(core::url::StripTrackingParams(tab.url));
    if (key.empty()) continue;
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
      victims.push_back(tab.id);
    } else {
      seen.push_back(key);
    }
  }
  size_t closed = 0;
  for (const int id : victims) {
    const TabState* tab = Find(id);
    if (tab != nullptr) RememberClosed(*tab);
    if (CloseTab(id)) ++closed;
  }
  return closed;
}

void Session::SortByHost() {
  const int active = active_id_;
  std::stable_sort(tabs_.begin(), tabs_.end(), [](const TabState& a, const TabState& b) {
    if (a.pinned != b.pinned) return a.pinned;  // pinned tabs stay in front
    const std::string host_a = core::url::HostOf(a.url);
    const std::string host_b = core::url::HostOf(b.url);
    if (host_a != host_b) return host_a < host_b;
    return false;  // stable: tabs of one host keep their relative order
  });
  active_id_ = active;
}

bool Session::MoveToFront(int id) {
  const int index = IndexOf(id);
  if (index < 0) return false;
  TabState tab = tabs_[static_cast<size_t>(index)];
  tabs_.erase(tabs_.begin() + index);
  // After the pinned block, which must stay where it is.
  size_t insert_at = 0;
  while (insert_at < tabs_.size() && tabs_[insert_at].pinned) ++insert_at;
  tabs_.insert(tabs_.begin() + static_cast<long>(insert_at), std::move(tab));
  return true;
}

void Session::RememberClosed(const TabState& tab) {
  // A sleeping tab that gets reopened should come back awake: the user asked for it.
  TabState remembered = tab;
  remembered.sleeping = false;
  recently_closed_.insert(recently_closed_.begin(), remembered);
  if (recently_closed_.size() > kMaxRecentlyClosed) recently_closed_.resize(kMaxRecentlyClosed);
}

int Session::ReopenLastClosed(bool activate) {
  if (recently_closed_.empty()) return 0;
  const TabState tab = recently_closed_.front();
  recently_closed_.erase(recently_closed_.begin());
  const int id = AddTabAfter(active_id_, tab.url, activate);
  // The reopened tab keeps what made it worth reopening: its title, group, zoom, mute and
  // pinned state. Its old id is gone (ids are never reused), so everything else is copied
  // by hand.
  if (TabState* added = MutableFind(id)) {
    added->title = tab.title;
    added->group = tab.group;
    added->zoom_percent = tab.zoom_percent;
    added->muted = tab.muted;
    added->pinned = tab.pinned;
  }
  return id;
}

std::vector<int> Session::RecentlyActive(int max_count) const {
  if (max_count <= 0) return {};
  const size_t wanted = static_cast<size_t>(max_count);
  std::vector<const TabState*> sorted;
  sorted.reserve(tabs_.size());
  for (const TabState& tab : tabs_) sorted.push_back(&tab);
  std::stable_sort(sorted.begin(), sorted.end(), [](const TabState* a, const TabState* b) {
    return a->last_active_ms > b->last_active_ms;
  });
  std::vector<int> out;
  for (const TabState* tab : sorted) {
    if (out.size() >= wanted) break;
    out.push_back(tab->id);
  }
  return out;
}

double Session::ZoomFactor(const TabState& tab) {
  // Zoom is stored as a percentage and shown as one; a corrupt value (0, negative, or a
  // number no browser would apply) is clamped instead of scaled into nonsense.
  const int zoom = tab.zoom_percent <= 0 ? 100 : std::min(tab.zoom_percent, 500);
  return static_cast<double>(zoom) / 100.0;
}

std::vector<int> Session::SleepBeyondLiveLimit(int max_live) {
  std::vector<int> slept;
  if (max_live <= 0) return slept;
  // Candidates: awake, unpinned, silent, not active. Least recently used first, so the tab
  // the user touched 5 minutes ago survives one they touched an hour ago.
  std::vector<const TabState*> candidates;
  for (const TabState& tab : tabs_) {
    if (tab.sleeping || tab.pinned || tab.audible || tab.id == active_id_) continue;
    candidates.push_back(&tab);
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const TabState* a, const TabState* b) {
                     return a->last_active_ms < b->last_active_ms;
                   });
  // How many renderers are awake right now, counting the ones that are not candidates
  // (active, pinned, audible, already awake and protected).
  size_t awake = 0;
  for (const TabState& tab : tabs_) {
    const bool protected_tab = tab.pinned || tab.audible || tab.id == active_id_;
    if (!tab.sleeping && (protected_tab || std::find_if(candidates.begin(), candidates.end(),
                                                        [&](const TabState* c) {
                                                          return c->id == tab.id;
                                                        }) == candidates.end())) {
      ++awake;
    }
  }
  const size_t budget =
      max_live > static_cast<int>(awake) ? static_cast<size_t>(max_live) - awake : 0;
  // `candidates` is least-recently-used first, so the ones to sleep are at the *start*.
  const size_t to_sleep = candidates.size() > budget ? candidates.size() - budget : 0;
  for (size_t i = 0; i < to_sleep; ++i) {
    if (SetSleeping(candidates[i]->id, true)) slept.push_back(candidates[i]->id);
  }
  return slept;
}

std::string Session::ActiveUrl() const {
  const TabState* tab = active_tab();
  return tab != nullptr ? tab->url : std::string();
}

std::string Session::DescribeCounts() const {
  std::string text = std::to_string(tabs_.size()) + (tabs_.size() == 1 ? " tab" : " tabs");
  const int sleeping = CountKind(TabLifecycle::kSleeping);
  if (sleeping > 0) text += " (" + std::to_string(sleeping) + " sleeping)";
  const int pinned = static_cast<int>(std::count_if(tabs_.begin(), tabs_.end(),
                                                   [](const TabState& tab) { return tab.pinned; }));
  if (pinned > 0) text += " (" + std::to_string(pinned) + " pinned)";
  return text;
}

namespace {

core::json::Value TabToJson(const TabState& tab) {
  core::json::Value::Object object;
  object["id"] = core::json::Value(static_cast<int64_t>(tab.id));
  object["url"] = core::json::Value(tab.url);
  object["title"] = core::json::Value(tab.title);
  object["group"] = core::json::Value(tab.group);
  object["pinned"] = core::json::Value(tab.pinned);
  object["muted"] = core::json::Value(tab.muted);
  object["sleeping"] = core::json::Value(tab.sleeping);
  object["zoom"] = core::json::Value(static_cast<int64_t>(tab.zoom_percent));
  object["last_active_ms"] = core::json::Value(static_cast<int64_t>(tab.last_active_ms));
  return core::json::Value(object);
}

}  // namespace

std::string Session::ToJson() const {
  core::json::Value::Object root;
  root["version"] = core::json::Value(static_cast<int64_t>(kSessionFormatVersion));
  root["active_id"] = core::json::Value(static_cast<int64_t>(active_id_));
  root["next_id"] = core::json::Value(static_cast<int64_t>(next_id_));
  core::json::Value::Array array;
  for (const TabState& tab : tabs_) array.push_back(TabToJson(tab));
  root["tabs"] = core::json::Value(array);
  return core::json::Serialize(core::json::Value(root), 2);
}

bool Session::FromJson(const std::string& json_text, std::string* error) {
  std::string parse_error;
  auto parsed = core::json::Parse(json_text, &parse_error);
  if (!parsed.has_value()) {
    if (error != nullptr) *error = "invalid JSON: " + parse_error;
    return false;
  }
  if (!parsed->IsObject()) {
    if (error != nullptr) *error = "session root must be an object";
    return false;
  }
  const int64_t version = IntOf(*parsed, "version", kSessionFormatVersion);
  if (version > kSessionFormatVersion) {
    if (error != nullptr) {
      *error = "session was written by a newer BrowserX (version " + std::to_string(version) + ")";
    }
    return false;
  }

  const core::json::Value& tabs_value = parsed->Get("tabs");
  if (!tabs_value.IsArray()) {
    if (error != nullptr) *error = "session has no tab array";
    return false;
  }

  tabs_.clear();
  next_id_ = 1;
  for (const core::json::Value& entry : tabs_value.AsArray()) {
    TabState tab;
    tab.id = static_cast<int>(IntOf(entry, "id", 0));
    tab.url = StringOf(entry, "url", "");
    tab.title = StringOf(entry, "title", "");
    tab.group = StringOf(entry, "group", "");
    tab.pinned = BoolOf(entry, "pinned", false);
    tab.muted = BoolOf(entry, "muted", false);
    tab.sleeping = BoolOf(entry, "sleeping", false);
    tab.zoom_percent = static_cast<int>(std::max<int64_t>(25, std::min<int64_t>(500, IntOf(entry, "zoom", 100))));
    tab.last_active_ms = IntOf(entry, "last_active_ms", Session::NowMs());
    tab.created_ms = tab.last_active_ms;
    if (tab.id <= 0) tab.id = next_id_;
    if (tab.url.empty()) tab.url = "browserx://newtab";
    next_id_ = std::max(next_id_, tab.id + 1);
    tabs_.push_back(tab);
  }

  active_id_ = static_cast<int>(IntOf(*parsed, "active_id", tabs_.empty() ? 0 : tabs_.front().id));
  if (Find(active_id_) == nullptr && !tabs_.empty()) active_id_ = tabs_.front().id;
  return true;
}

}  // namespace browser
}  // namespace browserx
