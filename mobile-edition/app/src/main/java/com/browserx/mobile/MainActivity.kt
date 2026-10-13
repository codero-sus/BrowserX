package com.browserx.mobile

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast

/**
 * The mobile edition's UI: a toolbar, a native tab strip, a web container and a
 * status bar — built from framework widgets only, so the app has no third-party
 * runtime dependency and starts without inflating a Material theme.
 *
 * Everything that decides *behaviour* lives in [ModeController], [TabController]
 * and [ShieldEngine]; this file only wires them to views.
 */
class MainActivity : Activity(), ModeController.Listener, TabController.Listener, ShieldEngine.Listener {

    private lateinit var container: FrameLayout
    private lateinit var stripRow: LinearLayout
    private lateinit var omnibox: EditText
    private lateinit var statusLeft: TextView
    private lateinit var statusRight: TextView
    private lateinit var progress: ProgressBar
    private lateinit var backButton: Button
    private lateinit var forwardButton: Button
    private lateinit var shieldButton: Button
    private lateinit var modeButton: Button

    private lateinit var tabs: TabController
    private lateinit var modes: ModeController
    private lateinit var shield: ShieldEngine

    private var fullscreenView: View? = null

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()

        shield = ShieldEngine(this)
        shield.listener = this
        tabs = TabController(this, container, this)
        modes = ModeController(this, tabs, this)
        modes.autoPilot = AutoPilotMode.fromId(prefs().getString(KEY_AUTOPILOT, null))
        modes.setMode(Mode.fromId(prefs().getString(KEY_MODE, null)), byUser = false)

        restoreOrOpenStartPage()
        modes.start()
        Log.i(TAG, "started with ${modes.statusLine()}")
    }

    override fun onResume() {
        super.onResume()
        modes.start()
        tabs.active()?.webView?.onResume()
    }

    override fun onPause() {
        tabs.saveSession()
        modes.stop()
        tabs.active()?.webView?.let {
            it.onPause()
            it.pauseTimers() // per-tab: the visible tab is the only one alive anyway
        }
        super.onPause()
    }

    override fun onDestroy() {
        tabs.saveSession()
        for (tab in tabs.all()) {
            tab.webView?.destroy()
        }
        super.onDestroy()
    }

    /** Android's own memory-pressure signal feeds the shared memory policy. */
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (level >= TRIM_MEMORY_RUNNING_LOW) {
            modes.refreshPlan()
            val slept = tabs.sleepHiddenTabs(limit = 3, idleSeconds = 0)
            if (slept > 0) {
                notice(getString(R.string.trim_memory_notice, slept))
            }
        }
    }

    override fun onUserInteraction() {
        modes.noteUserInput()
        super.onUserInteraction()
    }

    // -----------------------------------------------------------------------
    // UI construction
    // -----------------------------------------------------------------------
    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(BACKGROUND)
        }

        root += buildToolbar()
        root += buildStrip()
        root += ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = false
            max = 100
            progress = 0
            visibility = View.GONE
        }.also { progress = it }
        container = FrameLayout(this).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        }
        root += container
        root += buildStatusBar()
        setContentView(root)
        installEdgeToEdgeWorkaround()
    }

    private fun buildToolbar(): View {
        val toolbar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(SURFACE)
            setPadding(8, 8, 8, 8)
            gravity = Gravity.CENTER_VERTICAL
        }
        fun iconButton(text: String, description: String, action: () -> Unit): Button =
            Button(this).apply {
                this.text = text
                contentDescription = description
                minWidth = 0
                minimumWidth = 0
                setPadding(18, 0, 18, 0)
                setOnClickListener { action() }
                setBackgroundColor(Color.TRANSPARENT)
                setTextColor(TEXT)
            }

        backButton = iconButton("◀", "Back") { tabs.active()?.webView?.goBack()?.also { refreshNavButtons() } }
        forwardButton = iconButton("▶", "Forward") { tabs.active()?.webView?.goForward()?.also { refreshNavButtons() } }
        val reload = iconButton("⟳", "Reload") {
            tabs.active()?.webView?.reload()
        }
        toolbar += backButton
        toolbar += forwardButton
        toolbar += reload

        omnibox = EditText(this).apply {
            hint = getString(R.string.omnibox_hint)
            singleLine = true
            imeOptions = EditorInfo.IME_ACTION_GO
            setTextColor(TEXT)
            setHintTextColor(TEXT_DIM)
            setBackgroundColor(FIELD)
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            setOnEditorActionListener { _, actionId, event ->
                val submit = actionId == EditorInfo.IME_ACTION_GO ||
                    (event != null && event.keyCode == KeyEvent.KEYCODE_ENTER)
                if (submit) {
                    navigateFromOmnibox()
                    true
                } else {
                    false
                }
            }
        }
        toolbar += omnibox

        shieldButton = iconButton("🛡", "Tracker shield") {
            shield.enabled = !shield.enabled
            updateShieldButton()
            notice(if (shield.enabled) getString(R.string.shield_on) else getString(R.string.shield_off))
        }
        modeButton = iconButton("ULTRA", "Performance mode") { showModeMenu() }
        toolbar += shieldButton
        toolbar += modeButton
        toolbar += iconButton("☰", "Menu") { showMainMenu() }
        return toolbar
    }

    private fun buildStrip(): View {
        val scroll = HorizontalScrollView(this).apply {
            isHorizontalScrollBarEnabled = false
            setBackgroundColor(SURFACE)
        }
        stripRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        scroll.addView(stripRow)
        return scroll
    }

    private fun buildStatusBar(): View {
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(SURFACE_ALT)
            setPadding(12, 4, 12, 4)
        }
        statusLeft = TextView(this).apply {
            textSize = 11f
            setTextColor(TEXT_DIM)
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        statusRight = TextView(this).apply {
            textSize = 11f
            setTextColor(TEXT_DIM)
        }
        bar += statusLeft
        bar += statusRight
        return bar
    }

    private fun installEdgeToEdgeWorkaround() {
        // Keep the toolbar under the status bar: a full immersive setup would need
        // insets handling, and the strip is more useful visible.
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
    }

    private operator fun LinearLayout.plusAssign(view: View) {
        addView(view)
    }

    // -----------------------------------------------------------------------
    // Tab strip
    // -----------------------------------------------------------------------
    override fun onTabsChanged() {
        stripRow.removeAllViews()
        for (tab in tabs.all()) {
            val chip = Button(this).apply {
                text = chipLabel(tab)
                textSize = 12f
                maxLines = 1
                isAllCaps = false
                setTextColor(if (tab.id == tabs.active()?.id) TEXT else TEXT_DIM)
                setBackgroundColor(if (tab.id == tabs.active()?.id) SURFACE_ALT else SURFACE)
                setOnClickListener {
                    tabs.noteTouched(tab.id)
                    tabs.activateTab(tab.id) { createWebView() }
                    syncOmnibox()
                }
                setOnLongClickListener {
                    val menu = PopupMenu(this@MainActivity, this)
                    menu.menu.add(getString(R.string.close_tab)).setOnMenuItemClickListener {
                        tabs.closeTab(tab.id)
                        true
                    }
                    menu.menu.add(getString(R.string.sleep_tab)).setOnMenuItemClickListener {
                        if (!tabs.sleepTab(tab.id)) notice(getString(R.string.cannot_sleep_active))
                        true
                    }
                    menu.menu.add(getString(R.string.duplicate_tab)).setOnMenuItemClickListener {
                        tabs.addTab(tab.url) { createWebView() }
                        true
                    }
                    menu.show()
                    true
                }
            }
            stripRow.addView(chip)
        }
        val add = Button(this).apply {
            text = "+"
            textSize = 16f
            setTextColor(ACCENT)
            setBackgroundColor(Color.TRANSPARENT)
            contentDescription = getString(R.string.new_tab)
            setOnClickListener { newTab() }
        }
        stripRow.addView(add)
        onActiveTabChanged(tabs.active() ?: return)
    }

    private fun chipLabel(tab: TabController.Tab): String {
        val prefix = if (tab.sleeping) "💤 " else ""
        val label = tab.title.ifEmpty { tab.url }
        return prefix + if (label.length > 18) label.take(17) + "…" else label
    }

    override fun onActiveTabChanged(tab: TabController.Tab) {
        if (tab.sleeping) return
        syncOmnibox()
        refreshNavButtons()
    }

    override fun onNotice(text: String) = notice(text)

    private fun syncOmnibox() {
        val tab = tabs.active() ?: return
        if (!omnibox.hasFocus()) {
            omnibox.setText(tab.url)
        }
    }

    private fun refreshNavButtons() {
        val webView = tabs.active()?.webView
        backButton.isEnabled = webView?.canGoBack() == true
        forwardButton.isEnabled = webView?.canGoForward() == true
        backButton.alpha = if (backButton.isEnabled) 1f else 0.4f
        forwardButton.alpha = if (forwardButton.isEnabled) 1f else 0.4f
    }

    private fun updateShieldButton() {
        shieldButton.text = if (shield.enabled) "🛡" else "🛡̶"
        shieldButton.alpha = if (shield.enabled) 1f else 0.4f
    }

    // -----------------------------------------------------------------------
    // Navigation
    // -----------------------------------------------------------------------
    private fun navigateFromOmnibox() {
        val text = omnibox.text.toString()
        val tab = tabs.active() ?: return
        val resolved = PerformanceCore.resolveUrl(text, tabs.searchTemplate)
        (omnibox.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
            .hideSoftInputFromWindow(omnibox.windowToken, 0)
        omnibox.clearFocus()
        if (resolved.isEmpty()) return
        if (tab.sleeping || tab.webView == null) {
            tab.url = resolved
            tabs.activateTab(tab.id) { createWebView() }
        } else {
            tab.webView?.loadUrl(resolved)
        }
    }

    private fun newTab() {
        tabs.addTab(getString(R.string.new_tab_url)) { createWebView() }
    }

    private fun restoreOrOpenStartPage() {
        val restored = tabs.restoredSession()
        if (restored != null) {
            tabs.applyRestoredSession(restored)
            tabs.activateTab(tabs.active()?.id ?: 0) { createWebView() }
            val sleeping = tabs.sleepingCount()
            if (sleeping > 0) notice(getString(R.string.restored_sleeping, sleeping))
        } else {
            tabs.addTab(getString(R.string.new_tab_url)) { createWebView() }
        }
    }

    // -----------------------------------------------------------------------
    // WebView plumbing
    // -----------------------------------------------------------------------
    @SuppressLint("SetJavaScriptEnabled")
    private fun createWebView(): WebView {
        val webView = WebView(this)
        modes.applyRuntimeToWebView(webView)
        // A dedicated profile per mode keeps the HTTP cache budget honest when the
        // user switches modes: LOW RAM must not inherit GAME's warm cache.
        webView.settings.javaScriptCanOpenWindowsAutomatically = false
        webView.settings.setSupportMultipleWindows(false)
        webView.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(
                view: WebView?,
                request: WebResourceRequest?,
            ): WebResourceResponse? {
                if (request == null) return null
                val type = ShieldEngine.typeFor(requestTypeOf(request))
                return shield.intercept(view?.url, request, type)
            }

            override fun onPageStarted(view: WebView?, url: String?, favicon: android.graphics.Bitmap?) {
                progress.visibility = View.VISIBLE
            }

            override fun onPageFinished(view: WebView?, url: String?) {
                progress.visibility = View.GONE
                tabs.active()?.let { tab ->
                    if (url != null) tab.url = url
                    tabs.saveSession()
                }
                refreshNavButtons()
                syncOmnibox()
            }

            override fun onReceivedTitle(view: WebView?, title: String?) {
                val tab = tabs.active() ?: return
                if (title != null) tab.title = title
                onTabsChanged()
            }

            override fun doUpdateVisitedHistory(view: WebView?, url: String?, isReload: Boolean) {
                val tab = tabs.active() ?: return
                if (url != null) tab.url = url
            }

            private fun requestTypeOf(request: WebResourceRequest): String = when {
                request.isForMainFrame -> "document"
                else -> "other"
            }
        }
        webView.webChromeClient = object : WebChromeClient() {
            override fun onProgressChanged(view: WebView?, newProgress: Int) {
                progress.progress = newProgress
                progress.visibility = if (newProgress in 1..99) View.VISIBLE else View.GONE
            }

            override fun onShowCustomView(view: View?, callback: CustomViewCallback?) {
                fullscreenView = view
                view?.let {
                    container.addView(it)
                    window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
                }
            }

            override fun onHideCustomView() {
                fullscreenView?.let { container.removeView(it) }
                fullscreenView = null
                window.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
            }
        }
        webView.setOnScrollChangeListener { _, _, scrollY, _, _ ->
            tabs.active()?.let { it.scrollY = scrollY }
        }
        return webView
    }

    override fun onBackPressed() {
        val webView = tabs.active()?.webView
        if (fullscreenView != null) {
            (webView?.webChromeClient as? WebChromeClient)?.onHideCustomView()
            return
        }
        if (webView != null && webView.canGoBack()) {
            webView.goBack()
            return
        }
        if (tabs.count() > 1) {
            tabs.closeTab(tabs.active()?.id ?: 0)
            return
        }
        super.onBackPressed()
    }

    // -----------------------------------------------------------------------
    // Menus
    // -----------------------------------------------------------------------
    private fun showModeMenu() {
        val anchor = modeButton
        val menu = PopupMenu(this, anchor)
        for (mode in Mode.ordered) {
            val item = menu.menu.add("${mode.label} — ${mode.tagline}")
            item.isCheckable = true
            item.isChecked = mode == modes.mode
            item.setOnMenuItemClickListener {
                modes.setMode(mode)
                anchor.text = mode.label
                true
            }
        }
        menu.menu.addSeparator()
        val pilot = menu.menu.add(getString(R.string.autopilot_prefix, modes.autoPilot.label))
        pilot.setOnMenuItemClickListener {
            val next = when (modes.autoPilot) {
                AutoPilotMode.OFF -> AutoPilotMode.ASK
                AutoPilotMode.ASK -> AutoPilotMode.AUTO
                AutoPilotMode.AUTO -> AutoPilotMode.OFF
            }
            modes.setAutoPilot(next)
            prefs().edit().putString(KEY_AUTOPILOT, next.id).apply()
            notice(getString(R.string.autopilot_now, next.label))
            true
        }
        menu.menu.add(getString(R.string.show_plan)).setOnMenuItemClickListener {
            showPlanDialog()
            true
        }
        menu.show()
    }

    private fun showMainMenu() {
        val menu = PopupMenu(this, modeButton)
        menu.menu.add(getString(R.string.new_tab)).setOnMenuItemClickListener { newTab(); true }
        menu.menu.add(getString(R.string.close_tab)).setOnMenuItemClickListener {
            tabs.active()?.id?.let { tabs.closeTab(it) }
            true
        }
        menu.menu.add(getString(R.string.sleep_tab)).setOnMenuItemClickListener {
            tabs.active()?.id?.let {
                if (!tabs.sleepTab(it)) notice(getString(R.string.cannot_sleep_active))
            }
            true
        }
        menu.menu.addSeparator()
        menu.menu.add(getString(R.string.shield_report)).setOnMenuItemClickListener {
            val top = shield.topBlockedHosts()
            val lines = if (top.isEmpty()) getString(R.string.shield_nothing_blocked)
            else top.joinToString("\n") { (host, count) -> "$count × $host" }
            showText(getString(R.string.shield_report), lines)
            true
        }
        menu.menu.add(getString(R.string.clear_browsing_data)).setOnMenuItemClickListener {
            for (tab in tabs.all()) tab.webView?.clearCache(true)
            shield.reset()
            notice(getString(R.string.cache_cleared))
            true
        }
        menu.show()
    }

    private fun showPlanDialog() {
        val plan = modes.plan
        val builder = StringBuilder()
        builder.appendLine(getString(R.string.plan_mode_line, plan.label, modes.statusLine()))
        if (plan.memoryBudgetMb > 0) builder.appendLine("Budget: ${plan.memoryBudgetMb} MB")
        builder.appendLine()
        builder.appendLine("Boot switches the desktop edition uses for this mode:")
        for (entry in plan.bootSwitches) {
            builder.appendLine("  ${entry.flag}")
            builder.appendLine("      ${entry.reason}")
        }
        if (plan.enableFeatures.isNotEmpty()) {
            builder.appendLine("enable-features=${plan.enableFeatures.joinToString(",")}")
        }
        for (note in plan.notes) builder.appendLine("• $note")
        for (warning in plan.warnings) builder.appendLine("⚠ $warning")
        showText(getString(R.string.plan_title), builder.toString())
    }

    // -----------------------------------------------------------------------
    // Listener implementations
    // -----------------------------------------------------------------------
    override fun onModeChanged(mode: Mode, reason: String) {
        modeButton.text = mode.label
        prefs().edit().putString(KEY_MODE, mode.id).apply()
        statusRight.text = mode.label
        updateShieldRuntime()
        if (reason.isNotEmpty() && reason != "chosen by the user") notice("${mode.label}: $reason")
    }

    private fun updateShieldRuntime() {
        shield.blockImages = modes.plan.runtime.blockImages
        shield.blockMedia = modes.plan.runtime.blockMedia
        shield.blockFonts = modes.plan.runtime.blockFonts
        shield.enabled = modes.plan.runtime.blockTrackers
        updateShieldButton()
    }

    override fun onAutoPilotSuggestion(mode: Mode, reason: String) {
        // A suggestion is a chip in the status bar, never a dialog: the user is
        // probably typing or watching something.
        statusLeft.text = getString(R.string.autopilot_suggestion, mode.label, reason)
        statusLeft.setOnClickListener {
            modes.setMode(mode, "accepted the Auto-Pilot suggestion")
            statusLeft.setOnClickListener(null)
            statusLeft.text = ""
        }
    }

    override fun onTelemetry(text: String, pressure: Double) {
        statusLeft.text = getString(R.string.telemetry_line, text, (pressure * 100).toInt())
    }

    override fun onFullscreenChanged(fullscreen: Boolean) {
        // Auto-Pilot treats a fullscreen page as a likely game; the core decides.
        modes.refreshPlan()
    }

    override fun onBlocked(host: String, category: String) {
        ShieldEngine.log(host, category)
    }

    override fun onBlockedCountChanged(count: Int, savedKb: Int) {
        shieldButton.text = if (count > 0) "🛡 $count" else if (shield.enabled) "🛡" else "🛡̶"
    }

    // -----------------------------------------------------------------------
    // Small helpers
    // -----------------------------------------------------------------------
    private fun prefs() = getSharedPreferences("browserx", Context.MODE_PRIVATE)

    private fun notice(text: String) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show()
    }

    private fun showText(title: String, body: String) {
        android.app.AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(body)
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    /** Swallow fling gestures on the strip so long chip press still works. */
    override fun dispatchTouchEvent(ev: MotionEvent?): Boolean = super.dispatchTouchEvent(ev)

    private companion object {
        const val TAG = "BrowserX"
        const val KEY_MODE = "mode"
        const val KEY_AUTOPILOT = "autopilot"
        const val BACKGROUND = 0xFF101318.toInt()
        const val SURFACE = 0xFF1B1D22.toInt()
        const val SURFACE_ALT = 0xFF23262E.toInt()
        const val FIELD = 0xFF2C3039.toInt()
        const val TEXT = 0xFFE6E8EE.toInt()
        const val TEXT_DIM = 0xFF9AA1AE.toInt()
        const val ACCENT = 0xFF5AC8FA.toInt()
    }
}
