// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "browser/tab_strip.h"

#include <algorithm>
#include <map>
#include <string>

#include "include/views/cef_box_layout.h"
#include "include/wrapper/cef_helpers.h"

#include "core/format.h"
#include "core/log.h"

namespace browserx {
namespace browser {
namespace {

// Same tokens as the internal pages and the rest of the chrome: one palette for the
// whole product, so the UI cannot look like two different browsers.
constexpr cef_color_t kColorChrome = 0xFF0B0F16;
constexpr cef_color_t kColorChip = 0xFF141B26;
constexpr cef_color_t kColorChipActive = 0xFF1E2735;
constexpr cef_color_t kColorText = 0xFFE9EFF7;
constexpr cef_color_t kColorTextDim = 0xFF94A3B8;
constexpr cef_color_t kColorAccent = 0xFF3DDC84;
constexpr cef_color_t kColorAmber = 0xFFE3B341;

constexpr int kChipHeight = 30;
constexpr int kCloseWidth = 24;
constexpr int kNewTabWidth = 32;
constexpr int kStateDotWidth = 18;

/// The state dot every chip leads with: the place a favicon will go once the browser
/// has one, and until then the honest answer to "what is this tab doing?" -
/// hollow for a sleeping tab, a dot for a live one, amber while muted.
struct TabStateGlyph {
  const char* text;
  cef_color_t color;
};

TabStateGlyph GlyphFor(const TabState& tab, bool active) {
  if (tab.sleeping) return {"\xE2\x97\x8B", kColorTextDim};   // ○ renderer gone
  if (tab.muted) return {"\xE2\x97\x8F", kColorAmber};        // ● muted
  if (tab.pinned) return {"\xE2\x96\xAA", kColorTextDim};     // ▪ pinned
  return {"\xE2\x97\x8F", active ? kColorAccent : kColorTextDim};
}

}  // namespace

// ---------------------------------------------------------------------------
// Delegate plumbing
// ---------------------------------------------------------------------------
void TabStrip::Invalidate() { host_ = nullptr; }

CefRefPtr<CefPanel> TabStrip::Create(TabStripHost* host) {
  host_ = host;
  strip_ = CefPanel::CreatePanel(this);
  strip_->SetID(kTabStripId);
  strip_->SetBackgroundColor(kColorChrome);
  {
    CefBoxLayoutSettings settings;
    settings.horizontal = 1;
    settings.inside_border_horizontal_spacing = 4;
    settings.inside_border_vertical_spacing = 2;
    settings.between_child_spacing = 2;
    strip_->SetToBoxLayout(settings);
  }
  Rebuild();
  return strip_;
}

// CefPanelDelegate: the strip is a fixed-height row that takes the full width.
CefSize TabStrip::GetPreferredSize(CefRefPtr<CefView> view) {
  return CefSize(0, kTabStripHeight);
}

CefSize TabStrip::GetMinimumSize(CefRefPtr<CefView> view) {
  return CefSize(0, kTabStripHeight);
}

// CefButtonDelegate: all chips and both close/new buttons land here.
void TabStrip::OnButtonPressed(CefRefPtr<CefButton> button) {
  if (button.get() == nullptr) return;
  HandleCommand(button->GetID());
}

CefSize TabStrip::GetMaximumSize(CefRefPtr<CefView> view) {
  if (view.get() == nullptr) return CefSize(kTabTitleMaxWidth, kChipHeight);
  const int id = view->GetID();
  if (id >= kTabCloseIdBase) return CefSize(kCloseWidth, kChipHeight);
  if (id == kNewTabButtonId) return CefSize(kNewTabWidth, kChipHeight);
  if (id == kTabStateDotId) return CefSize(kStateDotWidth, kChipHeight);
  return CefSize(kTabTitleMaxWidth, kChipHeight);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void TabStrip::Rebuild() {
  if (strip_.get() == nullptr || host_ == nullptr) return;
  const size_t existing = strip_->GetChildViewCount();
  for (size_t i = 0; i < existing; ++i) {
    strip_->RemoveChildView(strip_->GetChildViewAt(0));
  }

  Session& session = host_->session();
  for (const TabState& tab : session.tabs()) {
    const bool active = tab.id == session.active_id();

    CefRefPtr<CefPanel> chip = CefPanel::CreatePanel(this);
    chip->SetBackgroundColor(active ? kColorChipActive : kColorChip);

    // A non-interactive label: it is a status light, not a button, so it must not
    // swallow the click that activates the tab.
    const TabStateGlyph glyph = GlyphFor(tab, active);
    CefRefPtr<CefLabelButton> state =
        CefLabelButton::CreateLabelButton(nullptr, glyph.text);
    state->SetID(kTabStateDotId);
    state->SetEnabled(false);
    state->SetFocusable(false);
    state->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    state->SetEnabledTextColors(glyph.color);
    state->SetInkDropEnabled(false);
    chip->AddChildView(state);

    CefRefPtr<CefLabelButton> title =
        CefLabelButton::CreateLabelButton(this, TabLabelFor(tab));
    title->SetID(kTabTitleIdBase + tab.id);
    title->SetTooltipText(tab.url);
    title->SetAccessibleName(TabLabelFor(tab, 64));
    title->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    title->SetEnabledTextColors(active ? kColorText : kColorTextDim);
    if (tab.sleeping) title->SetEnabledTextColors(kColorTextDim);
    title->SetInkDropEnabled(true);
    title->SetFocusable(true);
    chip->AddChildView(title);

    CefRefPtr<CefLabelButton> close =
        CefLabelButton::CreateLabelButton(this, "\xC3\x97");  // ×
    close->SetID(kTabCloseIdBase + tab.id);
    close->SetTooltipText("Close tab (Ctrl+W)");
    close->SetAccessibleName("Close tab");
    close->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    close->SetEnabledTextColors(kColorTextDim);
    close->SetInkDropEnabled(true);
    chip->AddChildView(close);

    {
      CefBoxLayoutSettings settings;
      settings.horizontal = 1;
      settings.inside_border_horizontal_spacing = 7;
      settings.inside_border_vertical_spacing = 1;
      settings.between_child_spacing = 2;
      CefRefPtr<CefBoxLayout> layout = chip->SetToBoxLayout(settings);
      if (layout.get() != nullptr) layout->SetFlexForView(title, 1);
    }
    strip_->AddChildView(chip);
  }

  CefRefPtr<CefLabelButton> add = CefLabelButton::CreateLabelButton(this, "+");
  add->SetID(kNewTabButtonId);
  add->SetTooltipText("New tab (Ctrl+T)");
  add->SetAccessibleName("New tab");
  add->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  add->SetTextColor(CEF_BUTTON_STATE_NORMAL, kColorTextDim);
  add->SetTextColor(CEF_BUTTON_STATE_HOVERED, kColorAccent);
  add->SetInkDropEnabled(true);
  add->SetInkDropEnabled(true);
  strip_->AddChildView(add);
}

void TabStrip::Refresh() {
  CEF_REQUIRE_UI_THREAD();
  Rebuild();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
bool TabStrip::HandleCommand(int command_id) {
  if (host_ == nullptr) return false;
  if (command_id == kNewTabButtonId) {
    TabAction action;
    action.command = TabCommand::kNewTab;
    action.argument = host_->NewTabUrl();
    Apply(action);
    return true;
  }
  if (command_id >= kTabCloseIdBase) {
    TabAction action;
    action.command = TabCommand::kCloseTab;
    action.tab_id = command_id - kTabCloseIdBase;
    Apply(action);
    return true;
  }
  if (command_id >= kTabTitleIdBase) {
    const int tab_id = command_id - kTabTitleIdBase;
    Session& session = host_->session();
    if (session.Find(tab_id) == nullptr) return true;
    const TabState* tab = session.Find(tab_id);
    if (tab != nullptr && tab->sleeping) {
      TabAction action;
      action.command = TabCommand::kWake;
      action.tab_id = tab_id;
      Apply(action);
      return true;
    }
    session.Activate(tab_id);
    host_->ShowTabView(tab_id);
    Refresh();
    host_->OnTabsChanged(session.DescribeCounts());
    return true;
  }
  return false;
}

void TabStrip::Apply(const TabAction& action) {
  CEF_REQUIRE_UI_THREAD();
  if (host_ == nullptr) return;
  Session& session = host_->session();

  const TabActionOutcome outcome = ApplyTabAction(action, &session);

  // Destroy first: a closed tab's renderer is the expensive part, and dropping
  // it before creating the next one keeps the peak memory lower.
  for (const int id : outcome.destroy_ids) {
    host_->DestroyTabView(id);
  }
  for (const int id : outcome.create_ids) {
    const TabState* tab = session.Find(id);
    host_->CreateTabView(id, tab != nullptr ? tab->url : std::string());
  }
  if (outcome.activate_id != 0) {
    host_->ShowTabView(outcome.activate_id);
  }
  if (!outcome.closed_url.empty()) {
    PushClosed(outcome.closed_url, outcome.closed_title);
  }
  if (outcome.structure_changed || !outcome.create_ids.empty() ||
      !outcome.destroy_ids.empty()) {
    Refresh();
  }
  if (!outcome.message.empty()) {
    host_->FlashTabStatus(outcome.message);
  }
  if (outcome.handled) {
    host_->OnTabsChanged(session.DescribeCounts());
  }
}

int TabStrip::SleepTabs(const std::vector<int>& tab_ids) {
  CEF_REQUIRE_UI_THREAD();
  if (host_ == nullptr || tab_ids.empty()) return 0;
  Session& session = host_->session();
  int slept = 0;
  for (const int id : tab_ids) {
    const TabState* tab = session.Find(id);
    if (tab == nullptr || tab->sleeping) continue;
    const int active = session.active_id();
    if (id == active) continue;
    if (session.SetSleeping(id, true)) {
      host_->DestroyTabView(id);
      ++slept;
    }
  }
  if (slept > 0) {
    if (slept == 1) {
      host_->FlashTabStatus("Slept 1 background tab");
    } else {
      host_->FlashTabStatus("Slept " + std::to_string(slept) + " background tabs");
    }
    Refresh();
    host_->OnTabsChanged(session.DescribeCounts());
  }
  return slept;
}

// ---------------------------------------------------------------------------
// Reopen stack
// ---------------------------------------------------------------------------
void TabStrip::PushClosed(const std::string& url, const std::string& title) {
  if (url.empty() || url == "browserx://newtab") return;  // nothing worth restoring
  closed_.emplace_back(url, title);
  // Ten is enough to undo an accident without holding on to stale pages forever.
  static constexpr size_t kMaxClosed = 10;
  if (closed_.size() > kMaxClosed) closed_.erase(closed_.begin());
}

bool TabStrip::ReopenLast() {
  if (closed_.empty()) return false;
  const std::pair<std::string, std::string> entry = closed_.back();
  closed_.pop_back();
  TabAction action;
  action.command = TabCommand::kReopenTab;
  action.argument = entry.first;
  Apply(action);
  return true;
}

std::string TabStrip::ActiveTabSummary() const {
  if (host_ == nullptr) return std::string();
  const Session& session = host_->session();
  const TabState* tab = session.active_tab();
  if (tab == nullptr) return std::string();
  return core::fmt::Truncate(tab->url, 96);
}

}  // namespace browser
}  // namespace browserx
