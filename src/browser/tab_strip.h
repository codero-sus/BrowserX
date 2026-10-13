// BrowserX — the tab strip.
//
// A native Views strip: one chip per tab (title button + close button) plus a
// "+" button. The strip never decides anything itself - it reads the Session,
// turns clicks into TabActions and lets tab_commands.cc mutate the session - so
// what you see in the strip is always exactly the session state.
//
// Sleeping tabs are real: their browser view is destroyed, which is where the
// memory saving comes from; the chip stays so the tab can be woken again.

#ifndef BROWSERX_BROWSER_TAB_STRIP_H_
#define BROWSERX_BROWSER_TAB_STRIP_H_

#include <string>
#include <vector>

#include "include/views/cef_browser_view.h"
#include "include/views/cef_button.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"

#include "browser/session.h"
#include "browser/tab_commands.h"

namespace browserx {
namespace browser {

/// View ids. Tab ids are encoded so one delegate can tell "activate tab 7" from
/// "close tab 7" without a lookup table.
enum TabStripId {
  kTabTitleIdBase = 10000,   // + tab id
  kTabCloseIdBase = 20000,   // + tab id
  kTabStripId = 30000,
  kNewTabButtonId = 30001,
  kTabTitleMaxWidth = 190,
  kTabStripHeight = 32,
};

class TabStripHost {
 public:
  virtual ~TabStripHost() = default;
  /// The single source of truth for which tabs exist.
  virtual Session& session() = 0;
  /// Creates (but does not show) the browser view for `tab_id`.
  virtual void CreateTabView(int tab_id, const std::string& url) = 0;
  /// Destroys the browser view of `tab_id`, keeping the tab itself.
  virtual void DestroyTabView(int tab_id) = 0;
  /// Makes `tab_id` the visible tab.
  virtual void ShowTabView(int tab_id) = 0;
  /// Called after any change: the window refreshes its title and status bar.
  virtual void OnTabsChanged(const std::string& summary) = 0;
  /// Short-lived message for the status bar.
  virtual void FlashTabStatus(const std::string& text) = 0;
  /// URL used for a brand-new tab (settings-driven).
  virtual std::string NewTabUrl() const = 0;
};

class TabStrip : public CefPanelDelegate, public CefButtonDelegate {
 public:
  /// Builds the strip panel; the caller adds it to its own view tree.
  CefRefPtr<CefPanel> Create(TabStripHost* host);

  /// Re-renders the chips from the session.
  void Refresh();

  /// Applies a tab action and performs the resulting view work.
  void Apply(const TabAction& action);

  /// Handles a view id produced by a chip; false when the id is not ours.
  bool HandleCommand(int command_id);

  /// Puts `tab_ids` to sleep in one pass (one refresh, one status update).
  /// Returns how many actually slept.
  int SleepTabs(const std::vector<int>& tab_ids);

  /// Reopened tabs come from the strip's own stack, filled by every close.
  void PushClosed(const std::string& url, const std::string& title);
  bool ReopenLast();
  size_t closed_count() const { return closed_.size(); }

  /// Drops every raw pointer held by the delegates (window teardown).
  void Invalidate();

  /// Label helper used by tests and by the status bar.
  std::string ActiveTabSummary() const;

  // --- CefPanelDelegate / CefButtonDelegate --------------------------------
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
  CefSize GetMaximumSize(CefRefPtr<CefView> view) override;
  void OnButtonPressed(CefRefPtr<CefButton> button) override;

 private:
  void Rebuild();

  TabStripHost* host_ = nullptr;
  CefRefPtr<CefPanel> strip_;
  std::vector<std::pair<std::string, std::string>> closed_;  // url, title

  IMPLEMENT_REFCOUNTING(TabStrip);
};

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_TAB_STRIP_H_
