// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "browser/browser_window.h"

#include "browser/request_guard.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_request.h"
#include "include/views/cef_box_layout.h"
#include "include/wrapper/cef_helpers.h"

#include "browser/browser_shell.h"
#include "core/format.h"
#include "core/log.h"
#include "core/url_utils.h"

namespace browserx {
namespace browser {
namespace {

// --- palette (fixed on purpose: a performance browser must not re-skin itself
//     mid-game, and a fixed palette costs no theme lookups) ------------------
constexpr cef_color_t kColorChrome = 0xFF1B1D22;
constexpr cef_color_t kColorChromeAlt = 0xFF23262E;
constexpr cef_color_t kColorField = 0xFF2C3039;
constexpr cef_color_t kColorText = 0xFFE6E8EE;
constexpr cef_color_t kColorTextDim = 0xFF9AA1AE;
constexpr cef_color_t kColorAccent = 0xFF5AC8FA;

constexpr int kToolbarHeight = 46;
constexpr int kStatusHeight = 24;
constexpr int kButtonWidth = 36;
constexpr int kButtonHeight = 32;
constexpr int kOmniboxWidth = 480;

// Virtual key codes CEF uses for accelerators (Windows convention).
constexpr int kKeyTab = 0x09;
constexpr int kKeyPageUp = 0x21;
constexpr int kKeyPageDown = 0x22;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyEscape = 0x1B;
constexpr int kKeyLeft = 0x25;
constexpr int kKeyRight = 0x27;
constexpr int kKeyHome = 0x24;
constexpr int kKeyF11 = 0x7A;
constexpr int kKeyF12 = 0x7B;
constexpr int kKeyPlus = 0xBB;
constexpr int kKeyMinus = 0xBD;

std::string ZoomText(double level) {
  return core::fmt::FormatPercent(100.0 * std::pow(1.2, level), 0);
}

CefRefPtr<CefLabelButton> MakeToolbarButton(CefRefPtr<CefButtonDelegate> delegate, int id,
                                           const std::string& text, const std::string& tooltip) {
  CefRefPtr<CefLabelButton> button = CefLabelButton::CreateLabelButton(delegate, text);
  button->SetID(id);
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetTextColor(CEF_BUTTON_STATE_NORMAL, kColorText);
  button->SetTextColor(CEF_BUTTON_STATE_HOVERED, kColorAccent);
  button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  button->SetInkDropEnabled(true);
  return button;
}

/// Static text: a label button that never reacts to input.
CefRefPtr<CefLabelButton> MakeStatusLabel(int id, const std::string& text,
                                         cef_horizontal_alignment_t alignment, cef_color_t color) {
  CefRefPtr<CefLabelButton> label = CefLabelButton::CreateLabelButton(nullptr, text);
  label->SetID(id);
  label->SetEnabled(false);
  label->SetFocusable(false);
  label->SetHorizontalAlignment(alignment);
  label->SetEnabledTextColors(color);
  label->SetInkDropEnabled(false);
  return label;
}

}  // namespace

// ---------------------------------------------------------------------------
// Widget delegates
// ---------------------------------------------------------------------------
void ToolbarButtonDelegate::OnButtonPressed(CefRefPtr<CefButton> button) {
  if (window_ != nullptr && button.get() != nullptr) window_->OnToolbarCommand(button->GetID());
}

CefSize ToolbarButtonDelegate::GetMinimumSize(CefRefPtr<CefView> view) {
  return CefSize(kButtonWidth, kButtonHeight);
}

CefSize ToolbarButtonDelegate::GetMaximumSize(CefRefPtr<CefView> view) {
  // The shield button carries a wider glyph and a blocked count.
  if (view.get() != nullptr && view->GetID() == kIdShield) return CefSize(64, kButtonHeight);
  return CefSize(kButtonWidth * 2, kButtonHeight);
}

bool OmniboxDelegate::OnKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event) {
  return window_ != nullptr && window_->OnOmniboxKey(event);
}

void OmniboxDelegate::OnAfterUserAction(CefRefPtr<CefTextfield> textfield) {
  if (window_ != nullptr) window_->OnOmniboxChanged();
}

CefSize OmniboxDelegate::GetMinimumSize(CefRefPtr<CefView> view) {
  return CefSize(240, kButtonHeight);
}

CefSize OmniboxDelegate::GetPreferredSize(CefRefPtr<CefView> view) {
  return CefSize(kOmniboxWidth, kButtonHeight);
}

void ChromeMenuDelegate::OnMenuButtonPressed(CefRefPtr<CefMenuButton> menu_button,
                                             const CefPoint& screen_point,
                                             CefRefPtr<CefMenuButtonPressedLock> lock) {
  if (window_ != nullptr) window_->OnChromeMenuPressed(menu_button, screen_point);
}

void ChromeMenuDelegate::OnButtonPressed(CefRefPtr<CefButton> button) {
  // Menu buttons act through their menu, but the interface requires this: treat a
  // click without a menu as the equivalent toolbar command.
  if (window_ != nullptr && button.get() != nullptr) window_->OnToolbarCommand(button->GetID());
}

void ChromeMenuDelegate::ExecuteCommand(CefRefPtr<CefMenuModel> menu_model, int command_id,
                                        cef_event_flags_t event_flags) {
  if (window_ != nullptr) window_->OnChromeMenuCommand(command_id);
}

CefSize ChromeMenuDelegate::GetMinimumSize(CefRefPtr<CefView> view) {
  return CefSize(kButtonWidth, kButtonHeight);
}

CefSize ChromeMenuDelegate::GetMaximumSize(CefRefPtr<CefView> view) {
  if (view.get() != nullptr && view->GetID() == kIdMode) return CefSize(120, kButtonHeight);
  return CefSize(kButtonWidth * 2, kButtonHeight);
}

CefSize ChromePanelDelegate::GetMinimumSize(CefRefPtr<CefView> view) {
  if (view.get() != nullptr && view->GetID() == kIdStatusLeft + 1000) return CefSize(0, kStatusHeight);
  return CefSize(0, kToolbarHeight);
}

CefSize ChromePanelDelegate::GetPreferredSize(CefRefPtr<CefView> view) {
  return GetMinimumSize(view);
}

// ---------------------------------------------------------------------------
// BrowserWindow
// ---------------------------------------------------------------------------
BrowserWindow::BrowserWindow(BrowserShellImpl* shell) : shell_(shell) {
  button_delegate_ = new ToolbarButtonDelegate(this);
  omnibox_delegate_ = new OmniboxDelegate(this);
  menu_delegate_ = new ChromeMenuDelegate(this);
  panel_delegate_ = new ChromePanelDelegate(this);
  strip_ = new TabStrip();
}

void BrowserWindow::Invalidate() {
  if (button_delegate_.get() != nullptr) button_delegate_->Invalidate();
  if (omnibox_delegate_.get() != nullptr) omnibox_delegate_->Invalidate();
  if (menu_delegate_.get() != nullptr) menu_delegate_->Invalidate();
  if (panel_delegate_.get() != nullptr) panel_delegate_->Invalidate();
  if (strip_.get() != nullptr) strip_->Invalidate();
  shell_ = nullptr;
}

void BrowserWindow::Create(const std::string& url, const std::string& restored_session_json) {
  CEF_REQUIRE_UI_THREAD();
  initial_url_ = url.empty() ? NewTabUrl() : url;
  restored_session_json_ = restored_session_json;
  CefWindow::CreateTopLevelWindow(this);
}

void BrowserWindow::BuildChrome(CefRefPtr<CefWindow> window, CefRefPtr<CefPanel> root) {
  // --- toolbar -------------------------------------------------------------
  toolbar_ = CefPanel::CreatePanel(panel_delegate_);
  toolbar_->SetID(kIdNone);
  toolbar_->SetBackgroundColor(kColorChrome);

  back_button_ = MakeToolbarButton(button_delegate_, kIdBack, "\xE2\x86\x90", "Back (Alt+Left)");
  forward_button_ = MakeToolbarButton(button_delegate_, kIdForward, "\xE2\x86\x92", "Forward (Alt+Right)");
  reload_button_ = MakeToolbarButton(button_delegate_, kIdReload, "\xE2\x9F\xB3", "Reload (Ctrl+R)");
  home_button_ = MakeToolbarButton(button_delegate_, kIdHome, "\xE2\x8C\x82", "Home (Alt+Home)");
  back_button_->SetEnabled(false);
  forward_button_->SetEnabled(false);

  omnibox_ = CefTextfield::CreateTextfield(omnibox_delegate_);
  omnibox_->SetID(kIdOmnibox);
  omnibox_->SetPlaceholderText("Search the web or type a URL");
  omnibox_->SetAccessibleName("Address and search bar");
  omnibox_->SetBackgroundColor(kColorField);

  shield_button_ = MakeToolbarButton(button_delegate_, kIdShield, "\xF0\x9F\x9B\xA1", "Tracker shield");
  mode_button_ = CefMenuButton::CreateMenuButton(menu_delegate_, "ULTRA");
  mode_button_->SetID(kIdMode);
  mode_button_->SetTooltipText("Performance mode - click to switch (Ctrl+1..8)");
  mode_button_->SetTextColor(CEF_BUTTON_STATE_NORMAL, kColorAccent);
  mode_button_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  menu_button_ = CefMenuButton::CreateMenuButton(menu_delegate_, "\xE2\x98\xB0");
  menu_button_->SetID(kIdMenu);
  menu_button_->SetTooltipText("Menu");
  menu_button_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);

  toolbar_->AddChildView(back_button_);
  toolbar_->AddChildView(forward_button_);
  toolbar_->AddChildView(reload_button_);
  toolbar_->AddChildView(home_button_);
  toolbar_->AddChildView(omnibox_);
  toolbar_->AddChildView(shield_button_);
  toolbar_->AddChildView(mode_button_);
  toolbar_->AddChildView(menu_button_);
  {
    CefBoxLayoutSettings settings;
    settings.horizontal = 1;
    settings.inside_border_horizontal_spacing = 6;
    settings.inside_border_vertical_spacing = 6;
    settings.between_child_spacing = 4;
    CefRefPtr<CefBoxLayout> layout = toolbar_->SetToBoxLayout(settings);
    if (layout.get() != nullptr) layout->SetFlexForView(omnibox_, 1);
  }

  // --- tab strip -----------------------------------------------------------
  CefRefPtr<CefPanel> strip_panel = strip_->Create(this);

  // --- content -------------------------------------------------------------
  // One panel holds every tab's browser view; only the active one is visible, so
  // switching tabs costs a visibility flip instead of a new renderer.
  content_ = CefPanel::CreatePanel(panel_delegate_);
  content_->SetBackgroundColor(0xFF101318);

  // --- status bar ----------------------------------------------------------
  status_bar_ = CefPanel::CreatePanel(panel_delegate_);
  status_bar_->SetBackgroundColor(kColorChromeAlt);
  status_left_ = MakeStatusLabel(kIdStatusLeft, "Ready", CEF_HORIZONTAL_ALIGNMENT_LEFT, kColorTextDim);
  status_right_ = MakeStatusLabel(kIdStatusRight, "", CEF_HORIZONTAL_ALIGNMENT_RIGHT, kColorTextDim);
  status_bar_->AddChildView(status_left_);
  status_bar_->AddChildView(status_right_);
  {
    CefBoxLayoutSettings settings;
    settings.horizontal = 1;
    settings.inside_border_horizontal_spacing = 8;
    settings.inside_border_vertical_spacing = 1;
    settings.between_child_spacing = 8;
    CefRefPtr<CefBoxLayout> layout = status_bar_->SetToBoxLayout(settings);
    if (layout.get() != nullptr) layout->SetFlexForView(status_left_, 1);
  }

  // --- root ----------------------------------------------------------------
  root_ = root;
  root_->SetBackgroundColor(kColorChrome);
  root_->AddChildView(strip_panel);
  root_->AddChildView(toolbar_);
  root_->AddChildView(content_);
  root_->AddChildView(status_bar_);
  {
    CefBoxLayoutSettings settings;
    settings.horizontal = 0;
    CefRefPtr<CefBoxLayout> layout = root_->SetToBoxLayout(settings);
    if (layout.get() != nullptr) layout->SetFlexForView(content_, 1);
  }
  // Tabs are created last: they need content_ and the accelerators in place.
  if (!restored_session_json_.empty()) {
    RestoreTabs(restored_session_json_);
  } else {
    OpenTab(initial_url_, true);
  }

  // --- accelerators --------------------------------------------------------
  auto bind = [&](int id, int key, bool shift, bool ctrl, bool alt) {
    window->SetAccelerator(id, key, shift, ctrl, alt, /*high_priority=*/false);
  };
  bind(kIdFocusOmnibox, 'L', false, true, false);
  bind(kIdReload, 'R', false, true, false);
  bind(kIdNewWindow, 'N', false, true, false);
  bind(kIdCloseWindow, 'W', true, true, false);
  // Tabs.
  bind(kIdNewTab, 'T', false, true, false);
  bind(kIdCloseTab, 'W', false, true, false);
  bind(kIdReopenTab, 'T', true, true, false);
  bind(kIdNextTab, kKeyTab, false, true, false);
  bind(kIdPrevTab, kKeyTab, true, true, false);
  bind(kIdMoveTabLeft, kKeyPageUp, true, true, false);
  bind(kIdMoveTabRight, kKeyPageDown, true, true, false);
  bind(kIdDuplicateTab, 'D', true, true, false);
  bind(kIdPinTab, 'P', true, true, false);
  bind(kIdMuteTab, 'M', true, true, false);
  bind(kIdGroupTab, 'G', true, true, false);
  bind(kIdFindInPage, 'F', false, true, false);
  bind(kIdPrint, 'P', false, true, false);
  bind(kIdZoomIn, kKeyPlus, false, true, false);
  bind(kIdZoomOut, kKeyMinus, false, true, false);
  bind(kIdZoomReset, '0', false, true, false);
  bind(kIdDevTools, kKeyF12, false, false, false);
  bind(kIdBack, kKeyLeft, false, false, true);
  bind(kIdForward, kKeyRight, false, false, true);
  bind(kIdHome, kKeyHome, false, false, true);
  bind(kIdQuit, 'Q', false, true, false);
  bind(kIdModeUltra, '1', false, true, false);
  bind(kIdModeGaming, '2', false, true, false);
  bind(kIdModeHighPerformance, '3', false, true, false);
  bind(kIdModeLowRam, '4', false, true, false);
  bind(kIdModeEco, '5', false, true, false);
  bind(kIdModeSuperInternet, '6', false, true, false);
  bind(kIdModeDownload, '7', false, true, false);
  bind(kIdModeStreaming, '8', false, true, false);
  // The benchmark page is the on-machine measuring stick, so it gets a key of its own -
  // running the same file in another browser is the whole point of it.
  bind(kIdOpenBench, 'B', true, true, false);
  // Only one more is worth a key: PRIVACY is the other mode people reach for in a
  // hurry. The remaining six live in the mode menu, browserx://modes and --mode=,
  // which is the honest place for fifteen options on a keyboard.
  bind(kIdModePrivacy, '9', false, true, false);

  RefreshShieldIndicator();
  UpdateNavigationButtons();
}

void BrowserWindow::UpdateNavigationButtons() {
  if (browser_.get() == nullptr) return;
  if (back_button_.get() != nullptr && back_button_->IsValid()) {
    back_button_->SetEnabled(browser_->CanGoBack());
  }
  if (forward_button_.get() != nullptr && forward_button_->IsValid()) {
    forward_button_->SetEnabled(browser_->CanGoForward());
  }
}

void BrowserWindow::RefreshShieldIndicator() {
  if (shield_button_.get() == nullptr || shell_ == nullptr) return;
  const bool enabled = shell_->shield_enabled();
  const int blocked = shell_->blocked_request_count();
  shield_button_->SetText(enabled ? "\xF0\x9F\x9B\xA1" : "\xE2\x9A\xAA");
  shield_button_->SetTooltipText(enabled ? "Shield ON - " + std::to_string(blocked) +
                                               " requests blocked so far"
                                         : "Shield OFF - click to enable");
}

// ---------------------------------------------------------------------------
// Commands (one table for buttons, accelerators and menus)
// ---------------------------------------------------------------------------
void BrowserWindow::Navigate(const std::string& input) {
  if (browser_.get() == nullptr || shell_ == nullptr) return;
  const std::string resolved =
      core::url::ResolveInput(core::url::SanitizeInput(input), shell_->search_template());
  if (resolved.empty()) return;
  browser_->GetMainFrame()->LoadURL(resolved);
}

void BrowserWindow::OpenInternalPage(const std::string& path) { Navigate("browserx://" + path); }

void BrowserWindow::OnToolbarCommand(int command_id) {
  if (shell_ == nullptr) return;
  switch (command_id) {
    case kIdBack:
      if (browser_.get() != nullptr && browser_->CanGoBack()) browser_->GoBack();
      return;
    case kIdForward:
      if (browser_.get() != nullptr && browser_->CanGoForward()) browser_->GoForward();
      return;
    case kIdReload:
      if (browser_.get() == nullptr) return;
      if (loading_) {
        browser_->StopLoad();
      } else {
        browser_->Reload();
      }
      return;
    case kIdHome:
      Navigate(shell_->home_page());
      return;
    case kIdFocusOmnibox:
      if (omnibox_.get() != nullptr && omnibox_->IsValid()) {
        omnibox_->RequestFocus();
        omnibox_->SelectAll(true);
      }
      return;
    case kIdNewWindow:
      shell_->OpenWindow("");
      return;
    case kIdCloseWindow:
      Close();
      return;
    case kIdNewTab:
    case kIdCloseTab:
    case kIdReopenTab:
    case kIdNextTab:
    case kIdPrevTab:
    case kIdMoveTabLeft:
    case kIdMoveTabRight:
    case kIdPinTab:
    case kIdMuteTab:
    case kIdDuplicateTab:
    case kIdGroupTab:
    case kIdCloseOtherTabs:
    case kIdCloseTabsToRight:
      HandleTabCommand(command_id);
      return;
    case kIdQuit:
      shell_->Quit();
      return;
    case kIdToggleShield:
      shell_->ToggleShield();
      RefreshShieldIndicator();
      Flash(shell_->shield_enabled() ? "Tracker shield enabled" : "Tracker shield disabled");
      return;
    case kIdZoomIn:
      SetZoom(zoom_level_ + 0.5);
      return;
    case kIdZoomOut:
      SetZoom(zoom_level_ - 0.5);
      return;
    case kIdZoomReset:
      SetZoom(0.0);
      return;
    case kIdDevTools:
      if (browser_.get() != nullptr) {
        browser_->GetHost()->ShowDevTools(CefWindowInfo(), nullptr, CefBrowserSettings(), CefPoint());
      }
      return;
    case kIdPrint:
      if (browser_.get() != nullptr) browser_->GetHost()->Print();
      return;
    case kIdFindInPage:
      // CEF has no native find bar; browserx://find drives the same call through
      // the page bridge, so the shortcut opens that page for the active tab.
      OpenInternalPage("find");
      return;
    case kIdSleepTab: {
      TabAction action;
      action.command = TabCommand::kSleep;
      strip_->Apply(action);
      return;
    }
    case kIdOpenSettings: OpenInternalPage("settings"); return;
    case kIdOpenTelemetry: OpenInternalPage("telemetry"); return;
    case kIdOpenModes: OpenInternalPage("modes"); return;
    case kIdOpenBench: OpenInternalPage("bench"); return;
    case kIdOpenHistory: OpenInternalPage("history"); return;
    case kIdOpenBookmarks: OpenInternalPage("bookmarks"); return;
    case kIdOpenDownloads: OpenInternalPage("downloads"); return;
    case kIdAutoPilotCycle: shell_->CycleAutoPilot(); return;
    case kIdModeUltra:
      shell_->SetMode(core::Mode::kUltra);
      return;
    case kIdModeGaming:
      shell_->SetMode(core::Mode::kGaming);
      return;
    case kIdModeHighPerformance:
      shell_->SetMode(core::Mode::kHighPerformance);
      return;
    case kIdModeLowRam:
      shell_->SetMode(core::Mode::kLowRam);
      return;
    case kIdModeEco:
      shell_->SetMode(core::Mode::kEco);
      return;
    case kIdModeSuperInternet:
      shell_->SetMode(core::Mode::kSuperInternet);
      return;
    case kIdModeDownload:
      shell_->SetMode(core::Mode::kDownload);
      return;
    case kIdModeStreaming:
      shell_->SetMode(core::Mode::kStreaming);
      return;
    case kIdModePrivacy:
      shell_->SetMode(core::Mode::kPrivacy);
      return;
    case kIdModeDataSaver:
      shell_->SetMode(core::Mode::kDataSaver);
      return;
    case kIdModeWeakNetwork:
      shell_->SetMode(core::Mode::kWeakNetwork);
      return;
    case kIdModeLowLatency:
      shell_->SetMode(core::Mode::kLowLatency);
      return;
    case kIdModeReader:
      shell_->SetMode(core::Mode::kReader);
      return;
    case kIdModeBenchmark:
      shell_->SetMode(core::Mode::kBenchmark);
      return;
    case kIdModeKiosk:
      shell_->SetMode(core::Mode::kKiosk);
      return;
    default:
      return;
  }
}

void BrowserWindow::HandleTabCommand(int command_id) {
  if (strip_.get() == nullptr) return;
  Session& session = session_;
  TabAction action;
  switch (command_id) {
    case kIdNewTab: action.command = TabCommand::kNewTab; break;
    case kIdCloseTab: action.command = TabCommand::kCloseTab; break;
    case kIdReopenTab: {
      // The strip owns the closed-tab stack; a sleeping tab is woken instead.
      if (strip_->closed_count() > 0) {
        strip_->ReopenLast();
        return;
      }
      if (session.active_tab() != nullptr && session.active_tab()->sleeping) {
        action.command = TabCommand::kWake;
        break;
      }
      action.command = TabCommand::kReopenTab;
      action.argument = initial_url_;
      break;
    }
    case kIdNextTab: action.command = TabCommand::kNextTab; break;
    case kIdPrevTab: action.command = TabCommand::kPreviousTab; break;
    case kIdMoveTabLeft: action.command = TabCommand::kMoveLeft; break;
    case kIdMoveTabRight: action.command = TabCommand::kMoveRight; break;
    case kIdPinTab: action.command = TabCommand::kTogglePin; break;
    case kIdMuteTab: action.command = TabCommand::kToggleMute; break;
    case kIdDuplicateTab: action.command = TabCommand::kDuplicate; break;
    case kIdGroupTab: action.command = TabCommand::kSetGroup; break;
    case kIdCloseOtherTabs: action.command = TabCommand::kCloseOthers; break;
    case kIdCloseTabsToRight: action.command = TabCommand::kCloseToRight; break;
    default: return;
  }
  strip_->Apply(action);
}

void BrowserWindow::SetZoom(double level) {
  zoom_level_ = std::max(-5.0, std::min(5.0, level));
  if (browser_.get() != nullptr) {
    browser_->GetHost()->SetZoomLevel(zoom_level_);
    Flash("Zoom " + ZoomText(zoom_level_));
  }
}

void BrowserWindow::Close() {
  if (closing_) return;
  closing_ = true;
  // Close every tab's browser first: one window, many browsers.
  const std::vector<CefRefPtr<CefBrowserView>> views = [&] {
    std::vector<CefRefPtr<CefBrowserView>> all;
    for (const auto& entry : tab_views_) {
      if (entry.second.get() != nullptr) all.push_back(entry.second);
    }
    return all;
  }();
  for (const CefRefPtr<CefBrowserView>& view : views) {
    CefRefPtr<CefBrowser> browser = view->GetBrowser();
    if (browser.get() != nullptr) browser->GetHost()->CloseBrowser(/*force_close=*/false);
  }
  if (views.empty() && window_.get() != nullptr && window_->IsValid()) window_->Close();
}

void BrowserWindow::UpdateStatus(const std::string& left, const std::string& right) {
  const std::string text = !flash_.empty() ? flash_ : (!status_message_.empty() ? status_message_ : left);
  if (status_left_.get() != nullptr && status_left_->IsValid()) status_left_->SetText(text);
  if (status_right_.get() != nullptr && status_right_->IsValid()) status_right_->SetText(right);
  flash_.clear();  // transient: shown for exactly one tick
}

void BrowserWindow::UpdateModeIndicators(const std::string& mode_label,
                                         const std::string& detail) {
  mode_label_ = mode_label;
  mode_detail_ = detail;
  if (mode_button_.get() != nullptr && mode_button_->IsValid()) {
    mode_button_->SetText(mode_label);
    mode_button_->SetTooltipText(detail.empty() ? "Performance mode" : detail);
  }
}

void BrowserWindow::Flash(const std::string& text) {
  flash_ = text;
  if (status_left_.get() != nullptr && status_left_->IsValid() && !text.empty()) {
    status_left_->SetText(text);
  }
}

// ---------------------------------------------------------------------------
// Omnibox
// ---------------------------------------------------------------------------
void BrowserWindow::OnOmniboxSubmit() {
  if (omnibox_.get() == nullptr) return;
  const std::string text = omnibox_->GetText().ToString();
  Navigate(text);
  if (browser_.get() != nullptr) browser_->GetHost()->SetFocus(true);
}

bool BrowserWindow::OnOmniboxKey(const CefKeyEvent& event) {
  if (event.type != KEYEVENT_RAWKEYDOWN && event.type != KEYEVENT_KEYDOWN) return false;
  if (event.windows_key_code == kKeyEnter) {
    OnOmniboxSubmit();
    return true;
  }
  if (event.windows_key_code == kKeyEscape) {
    if (omnibox_.get() != nullptr) {
      omnibox_->SetText(core::url::PrettyForDisplay(current_url_, 200));
    }
    if (browser_.get() != nullptr) browser_->GetHost()->SetFocus(true);
    return true;
  }
  return false;
}

void BrowserWindow::OnOmniboxChanged() {
  // Nothing to do yet: suggestion ranking arrives with the history store. The
  // hook exists so the history/omnibox stage has a single place to plug in.
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------
void BrowserWindow::OnChromeMenuPressed(CefRefPtr<CefMenuButton> button,
                                        const CefPoint& screen_point) {
  if (button.get() != nullptr && button->GetID() == kIdMode) {
    ShowModeMenu(screen_point);
    return;
  }
  ShowMainMenu(screen_point);
}

void BrowserWindow::OnChromeMenuCommand(int command_id) { OnToolbarCommand(command_id); }

void BrowserWindow::ShowModeMenu(const CefPoint& screen_point) {
  if (menu_button_.get() == nullptr || !menu_button_->IsValid()) return;
  CefRefPtr<CefMenuModel> model = CefMenuModel::CreateMenuModel(menu_delegate_);
  model->AddItem(kIdModeUltra, "ULTRA - balanced (Ctrl+1)");
  model->AddItem(kIdModeGaming, "GAME - maximum frame rate (Ctrl+2)");
  model->AddItem(kIdModeHighPerformance, "HIGH PERFORMANCE - maximum throughput (Ctrl+3)");
  model->AddItem(kIdModeLowRam, "LOW RAM - smallest footprint (Ctrl+4)");
  model->AddItem(kIdModeEco, "ECO - longest battery life (Ctrl+5)");
  model->AddItem(kIdModeSuperInternet, "SUPER INTERNET - max network speed (Ctrl+6)");
  model->AddItem(kIdModeDownload, "DOWNLOAD - built for big transfers (Ctrl+7)");
  model->AddItem(kIdModeStreaming, "STREAMING - smooth video and audio (Ctrl+8)");
  model->AddSeparator();
  model->AddItem(kIdModePrivacy, "PRIVACY - nothing left on disk (Ctrl+9)");
  model->AddItem(kIdModeDataSaver, "DATA SAVER - spend the least data");
  model->AddItem(kIdModeWeakNetwork, "WEAK NETWORK - for slow or crowded links");
  model->AddItem(kIdModeLowLatency, "LOW LATENCY - first byte and input response");
  model->AddItem(kIdModeReader, "READER - long-form reading");
  model->AddItem(kIdModeBenchmark, "BENCHMARK - measure, do not browse");
  model->AddItem(kIdModeKiosk, "KIOSK - unattended screen");
  model->AddSeparator();
  model->AddItem(kIdAutoPilotCycle, "Auto-Pilot: off / ask / auto");
  mode_button_->ShowMenu(model, screen_point, CEF_MENU_ANCHOR_TOPLEFT);
}

void BrowserWindow::ShowMainMenu(const CefPoint& screen_point) {
  if (menu_button_.get() == nullptr || !menu_button_->IsValid()) return;
  CefRefPtr<CefMenuModel> model = CefMenuModel::CreateMenuModel(menu_delegate_);
  model->AddItem(kIdNewTab, "New tab\tCtrl+T");
  model->AddItem(kIdCloseTab, "Close tab\tCtrl+W");
  model->AddItem(kIdReopenTab, "Reopen closed tab\tCtrl+Shift+T");
  model->AddItem(kIdDuplicateTab, "Duplicate tab\tCtrl+Shift+D");
  model->AddItem(kIdSleepTab, "Sleep this tab (free memory)");
  model->AddSeparator();
  model->AddItem(kIdNextTab, "Next tab\tCtrl+Tab");
  model->AddItem(kIdPinTab, "Pin / unpin tab\tCtrl+Shift+P");
  model->AddItem(kIdMuteTab, "Mute / unmute tab\tCtrl+Shift+M");
  model->AddItem(kIdGroupTab, "Add to group\tCtrl+Shift+G");
  model->AddItem(kIdCloseOtherTabs, "Close other tabs");
  model->AddItem(kIdCloseTabsToRight, "Close tabs to the right");
  model->AddSeparator();
  model->AddItem(kIdNewWindow, "New window\tCtrl+N");
  model->AddItem(kIdCloseWindow, "Close window\tCtrl+Shift+W");
  model->AddSeparator();
  model->AddItem(kIdOpenModes, "Performance mode...");
  model->AddItem(kIdOpenTelemetry, "Live telemetry");
  model->AddItem(kIdOpenBench, "Benchmark page (offline)\tCtrl+Shift+B");
  model->AddItem(kIdOpenSettings, "Settings");
  model->AddSeparator();
  model->AddItem(kIdOpenHistory, "History");
  model->AddItem(kIdOpenBookmarks, "Bookmarks");
  model->AddItem(kIdOpenDownloads, "Downloads");
  model->AddSeparator();
  model->AddItem(kIdToggleShield, "Toggle tracker shield");
  model->AddItem(kIdFindInPage, "Find in page\tCtrl+F");
  model->AddItem(kIdPrint, "Print\tCtrl+P");
  model->AddItem(kIdDevTools, "Developer tools\tF12");
  model->AddSeparator();
  model->AddItem(kIdQuit, "Quit BrowserX\tCtrl+Q");
  menu_button_->ShowMenu(model, screen_point, CEF_MENU_ANCHOR_TOPLEFT);
}

// ---------------------------------------------------------------------------
// TabStripHost
// ---------------------------------------------------------------------------
Session& BrowserWindow::session() { return session_; }

int BrowserWindow::TabIdForBrowser(CefRefPtr<CefBrowser> browser) const {
  if (browser.get() == nullptr) return 0;
  for (const auto& entry : tab_views_) {
    if (entry.second.get() != nullptr && entry.second->GetBrowser() == browser) return entry.first;
  }
  return 0;
}

int BrowserWindow::OpenTab(const std::string& url, bool activate) {
  const std::string target = url.empty() ? NewTabUrl() : url;
  const int id = session_.AddTab(target, activate);
  CreateTabView(id, target);
  if (activate) ShowTabView(id);
  if (strip_.get() != nullptr) strip_->Refresh();
  return id;
}

void BrowserWindow::CreateTabView(int tab_id, const std::string& url) {
  if (content_.get() == nullptr || tab_views_.find(tab_id) != tab_views_.end()) return;
  CefBrowserSettings browser_settings;
  browser_settings.background_color = 0xFF101318;
  const std::string target = url.empty() ? NewTabUrl() : url;
  CefRefPtr<CefBrowserView> view =
      CefBrowserView::CreateBrowserView(this, target, browser_settings, nullptr, nullptr, this);
  view->SetPreferAccelerators(true);
  view->SetVisible(false);  // ShowTabView() decides who is on screen
  content_->AddChildView(view);
  tab_views_[tab_id] = view;
}

void BrowserWindow::DestroyTabView(int tab_id) {
  const auto found = tab_views_.find(tab_id);
  if (found == tab_views_.end()) return;
  CefRefPtr<CefBrowserView> view = found->second;
  tab_views_.erase(found);
  if (view.get() == nullptr) return;
  if (browser_ != nullptr && view->GetBrowser() == browser_) browser_ = nullptr;
  // Closing the browser view destroys its renderer process; that is exactly the
  // memory a sleeping or closed tab must give back.
  if (content_.get() != nullptr) content_->RemoveChildView(view);
}

void BrowserWindow::ShowTabView(int tab_id) {
  if (tab_views_.find(tab_id) == tab_views_.end()) return;
  for (auto& entry : tab_views_) {
    if (entry.second.get() == nullptr) continue;
    entry.second->SetVisible(entry.first == tab_id);
  }
  session_.Activate(tab_id);
  SyncActiveTab();
}

void BrowserWindow::SyncActiveTab() {
  const TabState* tab = session_.active_tab();
  if (tab == nullptr) {
    browser_ = nullptr;
    return;
  }
  const auto found = tab_views_.find(tab->id);
  browser_ = (found != tab_views_.end() && found->second.get() != nullptr)
                 ? found->second->GetBrowser()
                 : nullptr;
  if (browser_ != nullptr && omnibox_.get() != nullptr && omnibox_->IsValid()) {
    omnibox_->SetText(core::url::PrettyForDisplay(tab->url, 200));
  }
  UpdateNavigationButtons();
  if (window_.get() != nullptr && window_->IsValid()) {
    const std::string label = tab->title.empty() ? tab->url : tab->title;
    window_->SetTitle(label.empty() ? "BrowserX" : label + " - BrowserX");
  }
}

void BrowserWindow::RestoreTabs(const std::string& json) {
  Session restored = Session::Restore(json, nullptr);
  if (restored.empty()) {
    OpenTab(initial_url_, true);
    return;
  }
  // Two passes so the saved order survives: adding a pinned tab would otherwise
  // move it to the front before its neighbours exist.
  std::vector<int> ids;
  for (const TabState& tab : restored.tabs()) {
    ids.push_back(session_.AddTab(tab.url, /*activate=*/false));
  }
  size_t index = 0;
  for (const TabState& tab : restored.tabs()) {
    const int id = ids[index++];
    session_.SetTitle(id, tab.title);
    session_.SetGroup(id, tab.group);
    session_.SetZoom(id, tab.zoom_percent);
    session_.SetMuted(id, tab.muted);
    if (tab.sleeping) session_.SetSleeping(id, true);
    if (tab.pinned) session_.SetPinned(id, true);
  }
  // Sleeping tabs come back as chips only: no renderer until they are clicked.
  for (const TabState& tab : session_.tabs()) {
    if (!tab.sleeping) CreateTabView(tab.id, tab.url);
  }
  const TabState* restored_active = restored.active_tab();
  int target = ids.empty() ? 0 : ids.front();
  if (restored_active != nullptr) {
    for (size_t i = 0; i < restored.tabs().size(); ++i) {
      if (restored.tabs()[i].id == restored_active->id && i < ids.size()) target = ids[i];
    }
  }
  if (target != 0) {
    ShowTabView(target);
  }
  if (strip_.get() != nullptr) strip_->Refresh();
  core::log::InfoF("tabs", "restored %d tab(s)", static_cast<int>(session_.count()));
}

int BrowserWindow::SleepIdleTabs(int max_count, int64_t idle_after_ms) {
  if (strip_.get() == nullptr) return 0;
  return strip_->SleepTabs(session_.SleepIdleTabs(max_count, idle_after_ms));
}

bool BrowserWindow::DiscardLruTab() {
  if (strip_.get() == nullptr) return false;
  const int id = session_.LruCandidateId();
  if (id == 0) return false;  // nothing eligible (single tab, or all pinned/audible)
  if (session_.SetSleeping(id, true)) {
    strip_->SleepTabs({id});
    return true;
  }
  return false;
}

void BrowserWindow::OnTabsChanged(const std::string& summary) {
  if (status_right_.get() != nullptr && status_right_->IsValid()) status_right_->SetText(summary);
}

void BrowserWindow::FlashTabStatus(const std::string& text) { Flash(text); }

std::string BrowserWindow::NewTabUrl() const {
  if (shell_ != nullptr && !shell_->home_page().empty()) return shell_->home_page();
  return "browserx://newtab";
}

// ---------------------------------------------------------------------------
// CefWindowDelegate
// ---------------------------------------------------------------------------
void BrowserWindow::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window_ = window;
  window->SetTitle("BrowserX");
  CefRefPtr<CefPanel> root = CefPanel::CreatePanel(panel_delegate_);
  window->AddChildView(root);
  BuildChrome(window, root);
  window->Show();
  core::log::Info("window", "window created");
}

void BrowserWindow::OnWindowClosing(CefRefPtr<CefWindow> window) {
  core::log::Debug("window", "window closing");
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  BrowserShellImpl* shell = shell_;
  // Drop every view reference before the window goes away.
  if (strip_.get() != nullptr) strip_->Invalidate();
  tab_views_.clear();
  window_ = nullptr;
  content_ = nullptr;
  root_ = nullptr;
  toolbar_ = nullptr;
  status_bar_ = nullptr;
  back_button_ = nullptr;
  forward_button_ = nullptr;
  reload_button_ = nullptr;
  home_button_ = nullptr;
  omnibox_ = nullptr;
  shield_button_ = nullptr;
  mode_button_ = nullptr;
  menu_button_ = nullptr;
  status_left_ = nullptr;
  status_right_ = nullptr;
  if (shell != nullptr) shell->OnWindowDestroyed(this);
}

CefRect BrowserWindow::GetInitialBounds(CefRefPtr<CefWindow> window) {
  const CefPoint origin = shell_ != nullptr ? shell_->NextWindowOrigin() : CefPoint(60, 60);
  const CefSize size = shell_ != nullptr ? shell_->initial_size() : CefSize(1280, 800);
  return CefRect(origin.x, origin.y, size.width, size.height);
}

cef_show_state_t BrowserWindow::GetInitialShowState(CefRefPtr<CefWindow> window) {
  return CEF_SHOW_STATE_NORMAL;
}

bool BrowserWindow::IsFrameless(CefRefPtr<CefWindow> window) {
  // OS decorations by default: they behave correctly with tiling WM, snapping
  // and screen readers. --frameless switches to our own title bar later.
  return shell_ != nullptr && shell_->frameless();
}

bool BrowserWindow::CanClose(CefRefPtr<CefWindow> window) { return true; }

bool BrowserWindow::OnAccelerator(CefRefPtr<CefWindow> window, int command_id) {
  // One table for buttons, menus and keys: a shortcut can never drift from the
  // menu entry that advertises it.
  OnToolbarCommand(command_id);
  return true;
}

bool BrowserWindow::OnKeyEvent(CefRefPtr<CefWindow> window, const CefKeyEvent& event) {
  // F11 fullscreen is handled here so it works even when the page never sees the
  // key (a game capturing the keyboard, for instance).
  if (event.type == KEYEVENT_RAWKEYDOWN && event.windows_key_code == kKeyF11) {
    window->SetFullscreen(!window->IsFullscreen());
    return true;
  }
  return false;
}

void BrowserWindow::OnThemeColorsChanged(CefRefPtr<CefWindow> window, bool chrome_theme) {}

cef_runtime_style_t BrowserWindow::GetWindowRuntimeStyle() { return CEF_RUNTIME_STYLE_DEFAULT; }

// ---------------------------------------------------------------------------
// CefBrowserViewDelegate
// ---------------------------------------------------------------------------
void BrowserWindow::OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                                     CefRefPtr<CefBrowser> browser) {
  UpdateNavigationButtons();
}

CefRefPtr<CefBrowserViewDelegate> BrowserWindow::GetDelegateForPopupBrowserView(
    CefRefPtr<CefBrowserView> browser_view, const CefBrowserSettings& settings,
    CefRefPtr<CefClient> client, bool is_devtools) {
  return this;
}

bool BrowserWindow::OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                              CefRefPtr<CefBrowserView> popup_browser_view,
                                              bool is_devtools) {
  if (shell_ != nullptr) shell_->AdoptPopupBrowserView(popup_browser_view);
  return true;
}

BrowserWindow::ChromeToolbarType BrowserWindow::GetChromeToolbarType(
    CefRefPtr<CefBrowserView> browser_view) {
  return CEF_CTT_NONE;  // our toolbar is native Views chrome, not CEF's
}

cef_runtime_style_t BrowserWindow::GetBrowserRuntimeStyle() { return CEF_RUNTIME_STYLE_DEFAULT; }

// ---------------------------------------------------------------------------
// CefLifeSpanHandler
// ---------------------------------------------------------------------------
void BrowserWindow::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  core::log::InfoF("tabs", "browser created (id %d)", browser->GetIdentifier());
  // Only the visible tab's browser becomes the window's current browser; the
  // others stay in tab_views_ and are switched in by ShowTabView().
  SyncActiveTab();
}

bool BrowserWindow::DoClose(CefRefPtr<CefBrowser> browser) {
  closing_ = true;
  return false;  // let the close continue; we destroy the window in OnBeforeClose
}

void BrowserWindow::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  const int tab_id = TabIdForBrowser(browser);
  if (tab_id != 0) {
    if (browser_ == browser) browser_ = nullptr;
    const auto found = tab_views_.find(tab_id);
    if (found != tab_views_.end() && content_.get() != nullptr) {
      content_->RemoveChildView(found->second);
    }
    tab_views_.erase(tab_id);
  }
  if (!closing_) {
    // The page asked to close itself (window.close()): close just that tab.
    Session& session = session_;
    if (tab_id != 0 && session.Find(tab_id) != nullptr) {
      TabAction action;
      action.command = TabCommand::kCloseTab;
      action.tab_id = tab_id;
      strip_->Apply(action);
    }
    SyncActiveTab();
    return;
  }
  if (tab_views_.empty()) {
    if (window_.get() != nullptr && window_->IsValid()) {
      window_->Close();
    } else if (shell_ != nullptr) {
      shell_->OnWindowDestroyed(this);
    }
  }
}

bool BrowserWindow::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  int popup_id, const CefString& target_url,
                                  const CefString& target_frame_name,
                                  CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                                  bool user_gesture, const CefPopupFeatures& popup_features,
                                  CefWindowInfo& window_info, CefRefPtr<CefClient>& client,
                                  CefBrowserSettings& settings,
                                  CefRefPtr<CefDictionaryValue>& extra_info,
                                  bool* no_javascript_access) {
  // Keep target=_blank / window.open() inside BrowserX: a new window we control
  // inherits the mode policy (a raw OS popup would not).
  if (shell_ == nullptr) return false;
  const std::string url = target_url.ToString();
  if (url.empty()) return false;  // scripts with no URL: let CEF decide
  shell_->OpenWindow(url);
  return true;
}

// ---------------------------------------------------------------------------
// CefDisplayHandler
// ---------------------------------------------------------------------------
void BrowserWindow::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  if (!frame->IsMain()) return;
  const std::string address = url.ToString();
  const int tab_id = TabIdForBrowser(browser);
  if (tab_id != 0) session_.SetUrl(tab_id, address);
  if (browser_ == nullptr || browser != browser_) return;  // background tab
  current_url_ = address;
  if (omnibox_.get() != nullptr && omnibox_->IsValid() && !omnibox_->HasFocus()) {
    // Idle omnibox shows the readable form; the raw URL appears on focus.
    omnibox_->SetText(core::url::PrettyForDisplay(current_url_, 200));
  }
}

void BrowserWindow::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
  const std::string text = title.ToString();
  const int tab_id = TabIdForBrowser(browser);
  if (tab_id != 0 && session_.SetTitle(tab_id, text)) {
    // Only the label of that one chip changes, so update it in place.
    strip_->Refresh();
  }
  if (window_.get() == nullptr || !window_->IsValid()) return;
  if (browser_ != nullptr && browser != browser_) return;
  window_->SetTitle(text.empty() ? "BrowserX" : text + " - BrowserX");
}

void BrowserWindow::OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) {
  status_message_ = value.ToString();
  if (status_left_.get() != nullptr && status_left_->IsValid()) {
    status_left_->SetText(status_message_.empty() ? "Ready" : status_message_);
  }
}

void BrowserWindow::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser, bool fullscreen) {
  fullscreen_ = fullscreen;
  if (window_.get() != nullptr && window_->IsValid()) {
    window_->SetFullscreen(fullscreen);
  }
  if (shell_ != nullptr) shell_->NoteFullscreenChanged(fullscreen);
}

bool BrowserWindow::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                                     const CefString& message, const CefString& source, int line) {
  const core::LogLevel mapped = level <= LOGSEVERITY_ERROR ? core::LogLevel::kError
                                                          : core::LogLevel::kDebug;
  core::Logger::Get().Log(mapped, "page",
                          source.ToString() + ":" + std::to_string(line) + ": " + message.ToString());
  return false;  // let CEF do its default handling as well
}

// ---------------------------------------------------------------------------
// CefLoadHandler
// ---------------------------------------------------------------------------
void BrowserWindow::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                                        bool can_go_back, bool can_go_forward) {
  if (browser_ != nullptr && browser != browser_) {
    if (shell_ != nullptr) shell_->NoteLoadState(this, is_loading);
    return;  // background tab: no toolbar changes
  }
  loading_ = is_loading;
  if (back_button_.get() != nullptr && back_button_->IsValid()) back_button_->SetEnabled(can_go_back);
  if (forward_button_.get() != nullptr && forward_button_->IsValid()) {
    forward_button_->SetEnabled(can_go_forward);
  }
  if (reload_button_.get() != nullptr && reload_button_->IsValid()) {
    reload_button_->SetText(is_loading ? "\xE2\x9C\x95" : "\xE2\x9F\xB3");
    reload_button_->SetTooltipText(is_loading ? "Stop (Esc)" : "Reload (Ctrl+R)");
  }
  if (shell_ != nullptr) shell_->NoteLoadState(this, is_loading);
}

void BrowserWindow::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                ErrorCode error_code, const CefString& error_text,
                                const CefString& failed_url) {
  if (!frame->IsMain()) return;
  core::log::WarnF("page", "load error %d: %s (%s)", static_cast<int>(error_code),
                   error_text.ToString().c_str(), failed_url.ToString().c_str());
  Flash("Failed to load: " + error_text.ToString());
}

// ---------------------------------------------------------------------------
// CefKeyboardHandler
// ---------------------------------------------------------------------------
bool BrowserWindow::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                                  CefEventHandle os_event, bool* is_keyboard_shortcut) {
  if (event.type != KEYEVENT_RAWKEYDOWN) return false;
  const bool ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
  if (!ctrl && event.windows_key_code == kKeyEscape && loading_ && browser.get() != nullptr) {
    browser->StopLoad();
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// CefRequestHandler
// ---------------------------------------------------------------------------
bool BrowserWindow::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefRequest> request, bool user_gesture,
                                  bool is_redirect) {
  if (shell_ != nullptr) shell_->NoteNavigationStarted(this, request->GetURL().ToString());
  return false;
}

CefRefPtr<CefResourceRequestHandler> BrowserWindow::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
    bool is_navigation, bool is_download, const CefString& request_initiator,
    bool& disable_default_handling) {
  // Downloads are left alone: cancelling one here would silently break the DOWNLOAD
  // mode's whole purpose, and a download is never a tracker subresource.
  if (is_download) return nullptr;
  return new RequestGuard(shell_);
}

// ---------------------------------------------------------------------------
// CefContextMenuHandler
// ---------------------------------------------------------------------------
void BrowserWindow::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefContextMenuParams> params,
                                        CefRefPtr<CefMenuModel> model) {
  model->AddSeparator();
  model->AddItem(kIdOpenTelemetry, "BrowserX: telemetry");
  model->AddItem(kIdOpenModes, "BrowserX: performance mode");
  model->AddItem(kIdOpenBench, "BrowserX: benchmark page");
  model->AddItem(kIdToggleShield, "BrowserX: toggle tracker shield");
}

bool BrowserWindow::OnContextMenuCommand(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefContextMenuParams> params, int command_id,
                                        EventFlags event_flags) {
  switch (command_id) {
    case kIdOpenTelemetry:
    case kIdOpenModes:
    case kIdOpenBench:
    case kIdToggleShield:
      OnToolbarCommand(command_id);
      return true;
    default:
      return false;
  }
}

}  // namespace browser
}  // namespace browserx
