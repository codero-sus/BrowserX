// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the window: native CEF Views chrome + one browser view.
//
// Stage 3 ships a complete single-page window (toolbar, omnibox, mode button,
// status bar, shortcuts, live telemetry, memory policy). Stage 4 swaps the single
// CefBrowserView for a TabManager-owned strip; the handlers and chrome below stay
// as they are.
//
// Two deliberate design choices:
//   * The chrome is CEF Views, not an HTML toolbar: one process less, no page can
//     block it, and it paints at native speed during GAME mode.
//   * Every widget gets its *own* small delegate class, because CEF views take a
//     single delegate interface each and sizing (GetMinimumSize) must not be
//     ambiguous. All clicks funnel into one command table, so a button, an
//     accelerator and a menu entry can never drift apart.

#ifndef BROWSERX_BROWSER_BROWSER_WINDOW_H_
#define BROWSERX_BROWSER_BROWSER_WINDOW_H_

#include <map>
#include <cstdint>
#include <string>
#include <vector>

#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_button.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_menu_button_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"

#include "app/cef_host.h"
#include "browser/tab_strip.h"
#include "core/captcha_assist.h"
#include "core/security_state.h"

namespace browserx {
namespace browser {

class BrowserWindow;
class BrowserShellImpl;

/// Chrome view ids: also the accelerator command ids, so the window delegate, the
/// toolbar and the menu agree by construction.
enum ChromeId {
  kIdNone = 0,
  kIdBack = 100,
  kIdForward,
  kIdReload,
  kIdHome,
  kIdOmnibox,
  kIdShield,
  kIdSecurity,   // the transport indicator at the left of the omnibox
  kIdCaptchaAssist,
  kIdOpenTor,
  kIdOpenExtensions,
  kIdOpenShield,
  kIdOpenAi,
  kIdMode,
  kIdMenu,
  kIdStatusLeft,
  kIdStatusRight,
  kIdNewWindow,
  kIdCloseWindow,
  kIdFocusOmnibox,
  kIdOpenSettings,
  kIdOpenTelemetry,
  kIdOpenHistory,
  kIdOpenBookmarks,
  kIdOpenDownloads,
  kIdOpenModes,
  kIdOpenBench,
  kIdOpenShortcuts,
  kIdJumpTo = 240,
  // The jump list (Ctrl+K) needs ids for pages that have no menu item of their own.
  // Kept below the tab strip's ids (10000+) so a view can never be confused for one.
  kIdJumpPagesMenu = 250,   // the submenu headings themselves (never actioned)
  kIdJumpModesMenu = 251,
  kIdJumpPageBase = 1000,
  kIdDevTools,
  kIdFindInPage,
  kIdPrint,
  kIdZoomIn,
  kIdZoomOut,
  kIdZoomReset,
  kIdToggleShield,
  kIdQuit,
  kIdModeUltra,
  kIdModeGaming,
  kIdModeHighPerformance,
  kIdModeLowRam,
  kIdModeEco,
  kIdModeSuperInternet,
  kIdModeDownload,
  kIdModeStreaming,
  kIdModePrivacy,
  kIdModeDataSaver,
  kIdModeWeakNetwork,
  kIdModeLowLatency,
  kIdModeReader,
  kIdModeBenchmark,
  kIdModeKiosk,
  kIdAutoPilotCycle,
  // --- tab commands (handled by the TabStrip) ------------------------------
  kIdNewTab,
  kIdCloseTab,
  kIdReopenTab,
  kIdNextTab,
  kIdPrevTab,
  kIdMoveTabLeft,
  kIdMoveTabRight,
  kIdPinTab,
  kIdMuteTab,
  kIdSleepTab,
  kIdDuplicateTab,
  kIdGroupTab,
  kIdCloseOtherTabs,
  kIdCloseTabsToRight,
};

/// Delegate for the toolbar buttons (back/forward/reload/home/shield).
class ToolbarButtonDelegate : public CefButtonDelegate {
 public:
  explicit ToolbarButtonDelegate(BrowserWindow* window) : window_(window) {}
  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
  CefSize GetMaximumSize(CefRefPtr<CefView> view) override;
  void Invalidate() { window_ = nullptr; }

 private:
  BrowserWindow* window_ = nullptr;
  IMPLEMENT_REFCOUNTING(ToolbarButtonDelegate);
};

/// Delegate for the omnibox.
class OmniboxDelegate : public CefTextfieldDelegate {
 public:
  explicit OmniboxDelegate(BrowserWindow* window) : window_(window) {}
  bool OnKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event) override;
  void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override;
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  void Invalidate() { window_ = nullptr; }

 private:
  BrowserWindow* window_ = nullptr;
  IMPLEMENT_REFCOUNTING(OmniboxDelegate);
};

/// Delegate for the mode button and the main menu button (+ their menus).
class ChromeMenuDelegate : public CefMenuButtonDelegate, public CefMenuModelDelegate {
 public:
  explicit ChromeMenuDelegate(BrowserWindow* window) : window_(window) {}
  void OnMenuButtonPressed(CefRefPtr<CefMenuButton> menu_button, const CefPoint& screen_point,
                           CefRefPtr<CefMenuButtonPressedLock> button_pressed_lock) override;
  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  void ExecuteCommand(CefRefPtr<CefMenuModel> menu_model, int command_id,
                      cef_event_flags_t event_flags) override;
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
  CefSize GetMaximumSize(CefRefPtr<CefView> view) override;
  void Invalidate() { window_ = nullptr; }

 private:
  BrowserWindow* window_ = nullptr;
  IMPLEMENT_REFCOUNTING(ChromeMenuDelegate);
};

/// Delegate for the toolbar and status-bar panels (fixed heights).
class ChromePanelDelegate : public CefPanelDelegate {
 public:
  explicit ChromePanelDelegate(BrowserWindow* window) : window_(window) {}
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  void Invalidate() { window_ = nullptr; }

 private:
  BrowserWindow* window_ = nullptr;
  IMPLEMENT_REFCOUNTING(ChromePanelDelegate);
};

class BrowserWindow : public CefWindowDelegate,
                      public CefBrowserViewDelegate,
                      public CefClient,
                      public CefDisplayHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefKeyboardHandler,
                      public CefRequestHandler,
                      public CefContextMenuHandler,
                      public TabStripHost {
 public:
  explicit BrowserWindow(BrowserShellImpl* shell);

  /// Creates the window. `url` empty = the configured home page; when
  /// `restored_session_json` is a saved session, its tabs replace the single
  /// start page (and the restored active tab is the one shown).
  void Create(const std::string& url, const std::string& restored_session_json = std::string());
  void Close();
  /// Injects the read-only text collector the AI panel asked for. Public because the
  /// shell, not the window, decides when a summary is wanted.
  void InjectAiPageTextCollector();
  /// The address and title the pending collector was injected into. The answer arrives
  /// asynchronously, and by then the address bar may show a different page.
  std::string ai_text_url_;
  std::string ai_text_title_;

  CefRefPtr<CefBrowser> browser() const { return browser_; }
  CefRefPtr<CefWindow> window() const { return window_; }
  bool is_closing() const { return closing_; }
  int tab_count() const { return static_cast<int>(session_.count()); }
  /// Tabs whose renderer is currently alive (sleeping tabs are not).
  int live_tab_count() const { return static_cast<int>(tab_views_.size()); }

  // --- memory policy hooks (called from the shell's telemetry tick) --------
  /// Sleeps up to `max_count` idle hidden tabs; returns how many slept.
  int SleepIdleTabs(int max_count, int64_t idle_after_ms);
  /// Sleeps the least recently used tab. False when nothing may be evicted.
  bool DiscardLruTab();

  // --- TabStripHost --------------------------------------------------------
  Session& session() override;
  void CreateTabView(int tab_id, const std::string& url) override;
  void DestroyTabView(int tab_id) override;
  void ShowTabView(int tab_id) override;
  void OnTabsChanged(const std::string& summary) override;
  void FlashTabStatus(const std::string& text) override;
  std::string NewTabUrl() const override;

  // --- called by the shell's telemetry tick --------------------------------
  void UpdateStatus(const std::string& left, const std::string& right);
  void UpdateModeIndicators(const std::string& mode_label, const std::string& detail);
  void Flash(const std::string& text);

  // --- hooks used by the widget delegates ---------------------------------
  void OnToolbarCommand(int command_id);           // buttons + accelerators + menu
  void OnOmniboxSubmit();
  bool OnOmniboxKey(const CefKeyEvent& event);
  void OnOmniboxChanged();
  void OnChromeMenuPressed(CefRefPtr<CefMenuButton> button, const CefPoint& screen_point);
  void OnChromeMenuCommand(int command_id);

  void SetZoom(double level);

  /// Clears raw pointers held by delegates (called from OnWindowDestroyed).
  void Invalidate();

 private:
  void BuildChrome(CefRefPtr<CefWindow> window, CefRefPtr<CefPanel> root);

  /// Re-reads the transport of the visible tab and repaints the indicator.
  void UpdateSecurityIndicator();
  /// Puts text in the status bar without disturbing the load state.
  void SetStatusText(const std::string& text);
  /// Injects the read-only CAPTCHA detector into the visible page.
  void InjectCaptchaDetector();
  /// Injects the read-only text collector the AI panel asked for. Public because the
  /// shell, not the window, decides when a summary is wanted.
  /// Opens `url` in a new tab (session + view) and optionally activates it.
  int OpenTab(const std::string& url, bool activate);
  /// Replaces the session with the tabs of a saved session (see Create()).
  void RestoreTabs(const std::string& json);
  /// Which tab owns `browser` (0 when it belongs to a popup / DevTools window).
  int TabIdForBrowser(CefRefPtr<CefBrowser> browser) const;
  /// The session-driven resync after any tab change.
  void SyncActiveTab();
  /// Maps a window/accelerator command id onto the tab command table.
  void HandleTabCommand(int command_id);
  void UpdateNavigationButtons();
  void RefreshShieldIndicator();
  void Navigate(const std::string& input);
  void OpenInternalPage(const std::string& path);
  void ShowMainMenu(const CefPoint& screen_point);
  void ShowModeMenu(const CefPoint& screen_point);
  /// Adds every mode item to a menu model. Shared by the mode menu and Ctrl+K, so a
  /// new mode cannot leave one of them behind.
  void AddModeMenuItems(CefRefPtr<CefMenuModel> model);
  /// The Ctrl+K jump list: every page and every mode, from anywhere - including
  /// ordinary web pages, where an internal page's own palette cannot run.
  void ShowJumpMenu();

  // --- CefWindowDelegate ---------------------------------------------------
  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowClosing(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  CefRect GetInitialBounds(CefRefPtr<CefWindow> window) override;
  cef_show_state_t GetInitialShowState(CefRefPtr<CefWindow> window) override;
  bool IsFrameless(CefRefPtr<CefWindow> window) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;
  bool OnKeyEvent(CefRefPtr<CefWindow> window, const CefKeyEvent& event) override;
  void OnThemeColorsChanged(CefRefPtr<CefWindow> window, bool chrome_theme) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;

  // --- CefBrowserViewDelegate ---------------------------------------------
  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override;
  CefRefPtr<CefBrowserViewDelegate> GetDelegateForPopupBrowserView(
      CefRefPtr<CefBrowserView> browser_view, const CefBrowserSettings& settings,
      CefRefPtr<CefClient> client, bool is_devtools) override;
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override;
  ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView> browser_view) override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override;

  // --- CefClient -----------------------------------------------------------
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }

  // --- CefDisplayHandler --------------------------------------------------
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
  void OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser, bool fullscreen) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                        const CefString& message, const CefString& source, int line) override;

  // --- CefLifeSpanHandler -------------------------------------------------
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
                     const CefString& target_url, const CefString& target_frame_name,
                     CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture,
                     const CefPopupFeatures& popup_features, CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info, bool* no_javascript_access) override;

  // --- CefLoadHandler -----------------------------------------------------
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
                            bool can_go_forward) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode error_code,
                   const CefString& error_text, const CefString& failed_url) override;

  // --- CefKeyboardHandler -------------------------------------------------
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                     CefEventHandle os_event, bool* is_keyboard_shortcut) override;

  // --- CefRequestHandler --------------------------------------------------
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) override;
  /// Hands every subresource to the request guard: the shield and the mode's
  /// resource policy (DATA SAVER, PRIVACY, ...) are enforced here, before the
  /// request reaches the network.
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
      bool is_navigation, bool is_download, const CefString& request_initiator,
      bool& disable_default_handling) override;

  // --- CefContextMenuHandler ----------------------------------------------
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params, int command_id,
                            EventFlags event_flags) override;

  BrowserShellImpl* shell_ = nullptr;
  CefRefPtr<CefWindow> window_;
  /// One browser view per live tab; sleeping and closed tabs are absent here.
  std::map<int, CefRefPtr<CefBrowserView>> tab_views_;
  CefRefPtr<CefBrowser> browser_;  // the browser of the *visible* tab

  Session session_;
  /// Refcounted because CEF holds a reference to the strip's delegates.
  CefRefPtr<TabStrip> strip_;

  CefRefPtr<CefPanel> root_;
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<CefPanel> status_bar_;
  CefRefPtr<CefLabelButton> back_button_;
  CefRefPtr<CefLabelButton> forward_button_;
  CefRefPtr<CefLabelButton> reload_button_;
  CefRefPtr<CefLabelButton> home_button_;
  CefRefPtr<CefTextfield> omnibox_;
  CefRefPtr<CefLabelButton> shield_button_;
  /// The padlock/warning at the left of the omnibox. Its glyph is a pure function of the
  /// URL (core::ClassifyTransport), so it cannot disagree with the site panel.
  CefRefPtr<CefLabelButton> security_button_;
  core::Transport transport_ = core::Transport::kUnknown;
  /// The challenge seen on the current page, if any. Reset on every navigation, because a
  /// notice that survived to the next page would be a false claim about that page.
  core::CaptchaKind captcha_ = core::CaptchaKind::kUnknown;
  bool captcha_seen_ = false;
  CefRefPtr<CefMenuButton> mode_button_;
  CefRefPtr<CefMenuButton> menu_button_;
  CefRefPtr<CefLabelButton> status_left_;
  CefRefPtr<CefLabelButton> status_right_;

  CefRefPtr<ToolbarButtonDelegate> button_delegate_;
  CefRefPtr<OmniboxDelegate> omnibox_delegate_;
  CefRefPtr<ChromeMenuDelegate> menu_delegate_;
  CefRefPtr<ChromePanelDelegate> panel_delegate_;

  std::string initial_url_;
  std::string restored_session_json_;
  std::string current_url_;
  std::string current_title_;  // last title the page reported, for history
  std::string status_message_;
  std::string flash_;
  std::string mode_label_ = "ULTRA";
  std::string mode_detail_;
  bool loading_ = false;
  bool closing_ = false;
  bool fullscreen_ = false;
  double zoom_level_ = 0.0;
  IMPLEMENT_REFCOUNTING(BrowserWindow);
};

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_BROWSER_WINDOW_H_
