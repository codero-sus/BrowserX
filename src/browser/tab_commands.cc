#include "browser/tab_commands.h"

#include <algorithm>
#include <string>

#include "core/format.h"

namespace mybrowser {
namespace browser {
namespace {

int ResolveTarget(const Session& session, int tab_id) {
  if (tab_id != 0 && session.Find(tab_id) != nullptr) return tab_id;
  return session.active_id();
}

std::string Quoted(const std::string& text) {
  return text.empty() ? std::string("(untitled)") : text;
}

}  // namespace

std::string TabLabelFor(const TabState& tab, int max_chars) {
  static constexpr int kDefaultLabelChars = 28;
  if (max_chars <= 0) max_chars = kDefaultLabelChars;
  std::string label = tab.title.empty() ? tab.url : tab.title;
  if (label.empty()) label = "New tab";
  label = core::fmt::Truncate(label, static_cast<size_t>(std::max(1, max_chars)));
  // Decorations are prefixes so they survive truncation.
  std::string prefix;
  if (tab.pinned) prefix += "\xF0\x9F\x93\x8C";   // 📌
  if (tab.sleeping) prefix += "\xF0\x9F\x92\xA4";  // 💤
  if (tab.muted) prefix += "\xF0\x9F\x94\x87";     // 🔇
  else if (tab.audible)
    prefix += "\xF0\x9F\x94\x8A";  // 🔊
  if (!tab.group.empty()) prefix += "[" + tab.group + "] ";
  return prefix + label;
}

std::string NextGroupNameIn(const Session& session, const TabState& tab) {
  std::vector<std::string> groups;
  for (const TabState& candidate : session.tabs()) {
    if (candidate.group.empty()) continue;
    if (std::find(groups.begin(), groups.end(), candidate.group) == groups.end()) {
      groups.push_back(candidate.group);
    }
  }
  std::sort(groups.begin(), groups.end());
  if (groups.empty()) return tab.group.empty() ? std::string("work") : std::string();
  if (tab.group.empty()) return groups.front();
  const auto found = std::find(groups.begin(), groups.end(), tab.group);
  if (found == groups.end() || found + 1 == groups.end()) return std::string();  // back to ungrouped
  return *(found + 1);
}

std::string DescribeTabLifecycle(const Session& session) {
  const int live = session.CountKind(TabLifecycle::kActive) + session.CountKind(TabLifecycle::kHidden);
  const int sleeping = session.CountKind(TabLifecycle::kSleeping);
  std::string text = std::to_string(live) + " live";
  if (sleeping > 0) text += ", " + std::to_string(sleeping) + " sleeping";
  return text;
}

TabActionOutcome ApplyTabAction(const TabAction& action, Session* session) {
  TabActionOutcome outcome;
  if (session == nullptr) return outcome;
  const int target = ResolveTarget(*session, action.tab_id);
  const TabState* tab = session->Find(target);

  switch (action.command) {
    case TabCommand::kNewTab: {
      const std::string url = action.argument.empty() ? "mybrowser://newtab" : action.argument;
      const int id = session->AddTab(url, /*activate=*/true);
      outcome.handled = true;
      outcome.activate_id = id;
      outcome.create_ids.push_back(id);
      outcome.structure_changed = true;
      break;
    }

    case TabCommand::kCloseTab: {
      if (tab == nullptr) break;
      outcome.closed_url = tab->url;
      outcome.closed_title = tab->title.empty() ? tab->url : tab->title;
      const bool was_last = session->count() == 1;
      outcome.destroy_ids.push_back(target);
      session->CloseTab(target);
      if (was_last) {
        // The last tab never leaves an empty window: replace it, like Chromium.
        const int id = session->AddTab("mybrowser://newtab", /*activate=*/true);
        outcome.create_ids.push_back(id);
        outcome.activate_id = id;
        outcome.message = "New tab";
      } else {
        outcome.activate_id = session->active_id();
        outcome.message = "Closed " + Quoted(outcome.closed_title);
      }
      outcome.handled = true;
      outcome.structure_changed = true;
      break;
    }

    case TabCommand::kReopenTab: {
      if (action.argument.empty()) break;
      const int id = session->AddTab(action.argument, /*activate=*/true);
      outcome.handled = true;
      outcome.activate_id = id;
      outcome.create_ids.push_back(id);
      outcome.structure_changed = true;
      outcome.message = "Reopened tab";
      break;
    }

    case TabCommand::kNextTab:
    case TabCommand::kPreviousTab: {
      if (session->count() < 2) break;
      const int delta = action.command == TabCommand::kNextTab ? 1 : -1;
      const int id = session->NeighbourId(session->active_id(), delta);
      if (id == 0) break;
      session->Activate(id);
      outcome.handled = true;
      outcome.activate_id = id;
      outcome.structure_changed = true;  // the active marker moved
      break;
    }

    case TabCommand::kMoveLeft:
    case TabCommand::kMoveRight: {
      const int index = session->IndexOf(target);
      if (index < 0) break;
      const int delta = action.command == TabCommand::kMoveLeft ? -1 : 1;
      const int size = static_cast<int>(session->count());
      if (action.command == TabCommand::kMoveLeft && index == 0) break;
      if (action.command == TabCommand::kMoveRight && index == size - 1) break;
      session->MoveTab(target, index + delta);
      outcome.handled = true;
      outcome.activate_id = target;
      outcome.structure_changed = true;
      break;
    }

    case TabCommand::kTogglePin: {
      if (tab == nullptr) break;
      session->SetPinned(target, !tab->pinned);
      outcome.handled = true;
      outcome.activate_id = session->active_id() == target ? target : session->active_id();
      outcome.structure_changed = true;
      outcome.message = tab->pinned ? "Tab unpinned" : "Tab pinned";
      break;
    }

    case TabCommand::kToggleMute: {
      if (tab == nullptr) break;
      session->SetMuted(target, !tab->muted);
      outcome.handled = true;
      outcome.structure_changed = true;  // the label carries the mute glyph
      outcome.message = tab->muted ? "Tab unmuted" : "Tab muted";
      break;
    }

    case TabCommand::kSleep: {
      if (tab == nullptr) break;
      if (target == session->active_id()) {
        outcome.handled = true;
        outcome.message = "The visible tab cannot sleep";
        break;
      }
      if (tab->sleeping) break;
      session->SetSleeping(target, true);
      outcome.handled = true;
      outcome.destroy_ids.push_back(target);
      outcome.structure_changed = true;
      outcome.message = "Slept " + Quoted(tab->title.empty() ? tab->url : tab->title);
      break;
    }

    case TabCommand::kWake: {
      if (tab == nullptr) break;
      session->Wake(target);
      session->Activate(target);
      outcome.handled = true;
      outcome.activate_id = target;
      outcome.create_ids.push_back(target);
      outcome.structure_changed = true;
      break;
    }

    case TabCommand::kDuplicate: {
      if (tab == nullptr) break;
      const int id = session->AddTabAfter(target, tab->url, /*activate=*/true);
      outcome.handled = true;
      outcome.activate_id = id;
      outcome.create_ids.push_back(id);
      outcome.structure_changed = true;
      outcome.message = "Duplicated tab";
      break;
    }

    case TabCommand::kCloseOthers: {
      const int keep = session->active_id();
      std::vector<int> victims;
      for (const TabState& candidate : session->tabs()) {
        if (candidate.id == keep || candidate.pinned) continue;
        victims.push_back(candidate.id);
        outcome.destroy_ids.push_back(candidate.id);
      }
      if (victims.empty()) break;
      for (const int id : victims) session->CloseTab(id);
      outcome.handled = true;
      outcome.activate_id = session->active_id();
      outcome.structure_changed = true;
      outcome.message = "Closed " + std::to_string(victims.size()) + " other tabs";
      break;
    }

    case TabCommand::kCloseToRight: {
      const int index = session->IndexOf(session->active_id());
      if (index < 0) break;
      std::vector<int> victims;
      const std::vector<TabState> snapshot = session->tabs();
      for (size_t i = static_cast<size_t>(index) + 1; i < snapshot.size(); ++i) {
        if (snapshot[i].pinned) continue;  // pinned tabs are never swept away
        victims.push_back(snapshot[i].id);
        outcome.destroy_ids.push_back(snapshot[i].id);
      }
      if (victims.empty()) break;
      for (const int id : victims) session->CloseTab(id);
      outcome.handled = true;
      outcome.activate_id = session->active_id();
      outcome.structure_changed = true;
      outcome.message = "Closed " + std::to_string(victims.size()) + " tabs to the right";
      break;
    }

    case TabCommand::kSetGroup: {
      if (tab == nullptr) break;
      session->SetGroup(target, action.argument);
      outcome.handled = true;
      outcome.structure_changed = true;
      outcome.message = action.argument.empty() ? "Removed from group"
                                                : "Grouped under \"" + action.argument + "\"";
      break;
    }

    case TabCommand::kCycleGroup: {
      if (tab == nullptr) break;
      const std::string next = NextGroupNameIn(*session, *tab);
      session->SetGroup(target, next);
      outcome.handled = true;
      outcome.structure_changed = true;
      outcome.message = next.empty() ? "Removed from group" : "Group \"" + next + "\"";
      break;
    }
  }

  return outcome;
}

}  // namespace browser
}  // namespace mybrowser
