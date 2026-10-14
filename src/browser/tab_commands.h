// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — tab commands as pure logic.
//
// The window, the menus and the keyboard all funnel into `ApplyTabAction`, which
// mutates a Session and reports exactly which tab views must be created or
// destroyed. Keeping this layer CEF-free means the behaviour of every tab
// shortcut (including the awkward ones: close-others, sleep, reopen) is covered
// by unit tests instead of by clicking around.

#ifndef BROWSERX_BROWSER_TAB_COMMANDS_H_
#define BROWSERX_BROWSER_TAB_COMMANDS_H_

#include <string>
#include <vector>

#include "browser/session.h"

namespace browserx {
namespace browser {

enum class TabCommand {
  kNewTab,        // open `argument` (may be empty = the new-tab page)
  kCloseTab,      // close the target tab (default: active)
  kReopenTab,     // re-open `argument`, the last closed URL
  kNextTab,       // activate the tab to the right (wraps)
  kPreviousTab,   // activate the tab to the left (wraps)
  kMoveLeft,      // move the active tab one position left
  kMoveRight,     // move the active tab one position right
  kTogglePin,     // pin/unpin (pinned tabs sort to the front)
  kToggleMute,    // mute/unmute the tab
  kSleep,         // discard the renderer of the target tab
  kWake,          // restore a sleeping tab and activate it
  kDuplicate,     // copy of the target tab, activated
  kCloseOthers,   // close every tab except the active one and pinned tabs
  kCloseToRight,  // close every unpinned tab after the active one
  kSetGroup,      // put the target tab in group `argument` ("" = ungroup)
  kCycleGroup,    // move the target tab through the existing groups
};

struct TabAction {
  TabCommand command = TabCommand::kNewTab;
  /// Which tab to act on; 0 means "the active tab".
  int tab_id = 0;
  /// Command-specific payload (URL for kNewTab, group name for kSetGroup, ...).
  std::string argument;
};

struct TabActionOutcome {
  bool handled = false;
  /// Tab that must become visible after the action (0 = keep the current one).
  int activate_id = 0;
  /// Tabs whose browser views must be created (new, woken, duplicated).
  std::vector<int> create_ids;
  /// Tabs whose browser views must be destroyed (closed, slept, closed-others).
  std::vector<int> destroy_ids;
  /// Tab order or tab set changed: the strip must be re-rendered.
  bool structure_changed = false;
  /// URL of the tab that was closed, for the reopen stack (empty when none).
  std::string closed_url;
  std::string closed_title;
  /// Short user-facing message for the status bar (empty = say nothing).
  std::string message;
};

/// Applies `action` to `session`. Never throws, never touches views.
TabActionOutcome ApplyTabAction(const TabAction& action, Session* session);

/// Formats the tab-strip label for a tab: title, with decorations for pinned,
/// muted and sleeping state ("A" / "⌛" / "🔇"), truncated for the strip.
std::string TabLabelFor(const TabState& tab, int max_chars = 28);

/// The closest group name to `tab` in `session`: used by kCycleGroup.
std::string NextGroupNameIn(const Session& session, const TabState& tab);

/// Human summary of a sleep decision, used by the memory policy and the UI.
std::string DescribeTabLifecycle(const Session& session);

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_TAB_COMMANDS_H_
