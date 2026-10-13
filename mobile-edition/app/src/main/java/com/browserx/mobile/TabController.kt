package com.browserx.mobile

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.webkit.WebView
import android.widget.FrameLayout
import org.json.JSONArray
import org.json.JSONObject

/**
 * Tabs on Android: one [WebView] per live tab, and *no* WebView at all for a
 * sleeping tab — that is where the memory comes back. A sleeping tab keeps its
 * URL, title and scroll position, so waking it feels like a reload, not a reset.
 *
 * The bookkeeping rules (never sleep the active tab, LRU order, session restore)
 * mirror the desktop `src/browser/session.cc` on purpose: both editions should
 * *feel* the same, and the desktop rules are unit tested.
 */
class TabController(
    private val context: Context,
    private val container: FrameLayout,
    private val listener: Listener,
) {

    interface Listener {
        fun onTabsChanged()
        fun onActiveTabChanged(tab: Tab)
        fun onNotice(text: String)
    }

    class Tab(
        val id: Int,
        val url: String,
        var title: String = "",
        var webView: WebView? = null,
        var sleeping: Boolean = false,
        var scrollY: Int = 0,
        var visitedOrder: Long = 0,
    )

    private val tabs = mutableListOf<Tab>()
    private val handler = Handler(Looper.getMainLooper())
    private var nextId = 1
    private var activeId = 0
    private var clock = 0L

    val searchTemplate: String get() = prefs().getString(KEY_SEARCH, DEFAULT_SEARCH) ?: DEFAULT_SEARCH

    fun all(): List<Tab> = tabs
    fun count(): Int = tabs.size
    fun hiddenCount(): Int = tabs.count { it.id != activeId && !it.sleeping }
    fun sleepingCount(): Int = tabs.count { it.sleeping }
    fun active(): Tab? = tabs.firstOrNull { it.id == activeId }
    fun find(id: Int): Tab? = tabs.firstOrNull { it.id == id }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    fun addTab(url: String, activate: Boolean = true, webViewFactory: () -> WebView): Tab? {
        val resolved = PerformanceCore.resolveUrl(url, searchTemplate).ifEmpty { url }
        val tab = Tab(id = nextId++, url = resolved, visitedOrder = ++clock)
        tabs += tab
        if (activate) activateTab(tab.id, webViewFactory)
        listener.onTabsChanged()
        saveSession()
        return tab
    }

    fun activateTab(id: Int, webViewFactory: () -> WebView) {
        val tab = find(id) ?: return
        if (tab.sleeping || tab.webView == null) {
            wake(tab, webViewFactory)
        }
        for (candidate in tabs) {
            candidate.webView?.visibility = if (candidate.id == id) WebView.VISIBLE else WebView.GONE
        }
        activeId = id
        tab.visitedOrder = ++clock
        listener.onActiveTabChanged(tab)
        listener.onTabsChanged()
    }

    fun closeTab(id: Int) {
        val index = tabs.indexOfFirst { it.id == id }
        if (index < 0) return
        val tab = tabs[index]
        tab.webView?.let { view ->
            container.removeView(view)
            view.destroy()
        }
        tabs.removeAt(index)
        if (activeId == id) {
            val next = tabs.getOrNull(index) ?: tabs.lastOrNull()
            if (next != null) {
                next.webView?.visibility = WebView.VISIBLE
                activeId = next.id
                if (next.sleeping) listener.onNotice("Tab is asleep — restoring it")
            } else {
                activeId = 0
            }
        }
        listener.onTabsChanged()
        saveSession()
    }

    fun sleepTab(id: Int): Boolean {
        val tab = find(id) ?: return false
        if (tab.id == activeId) {
            listener.onNotice("The visible tab cannot sleep")
            return false
        }
        if (tab.sleeping) return false
        tab.webView?.let { view ->
            tab.scrollY = view.scrollY
            container.removeView(view)
            view.destroy()
        }
        tab.webView = null
        tab.sleeping = true
        listener.onTabsChanged()
        saveSession()
        return true
    }

    fun wake(tab: Tab, webViewFactory: () -> WebView) {
        if (tab.webView != null) {
            tab.sleeping = false
            return
        }
        val view = webViewFactory()
        view.visibility = WebView.GONE
        container.addView(
            view,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT,
            ),
        )
        tab.webView = view
        tab.sleeping = false
        view.loadUrl(tab.url)
        // Restore the reading position once the first layout pass is done.
        view.post {
            if (tab.scrollY > 0) view.scrollTo(0, tab.scrollY)
        }
        Log.i(TAG, "woke tab ${tab.id} (${tab.url})")
    }

    // -----------------------------------------------------------------------
    // Memory policy hooks (called by ModeController)
    // -----------------------------------------------------------------------
    /** Sleeps up to [limit] non-active tabs untouched for [idleSeconds]. */
    fun sleepHiddenTabs(limit: Int, idleSeconds: Int): Int {
        val now = System.currentTimeMillis()
        val candidates = tabs
            .filter { it.id != activeId && !it.sleeping && it.webView != null }
            .filter { now - lastTouched(it.id) >= idleSeconds * 1000L }
            .sortedBy { it.visitedOrder }
            .take(limit)
        var slept = 0
        for (tab in candidates) {
            lastTouched.remove(tab.id) // re-armed when the tab is used again
            if (sleepTab(tab.id)) slept++
        }
        return slept
    }

    fun discardLeastRecentlyUsed(): Boolean {
        val victim = tabs.filter { it.id != activeId && !it.sleeping }.minByOrNull { it.visitedOrder }
            ?: return false
        return sleepTab(victim.id)
    }

    fun noteTouched(id: Int) {
        lastTouched[id] = System.currentTimeMillis()
    }

    private val lastTouched = mutableMapOf<Int, Long>()

    // -----------------------------------------------------------------------
    // Session persistence
    // -----------------------------------------------------------------------
    fun saveSession() {
        val array = JSONArray()
        for (tab in tabs) {
            array.put(
                JSONObject()
                    .put("id", tab.id)
                    .put("url", tab.url)
                    .put("title", tab.title)
                    .put("sleeping", tab.sleeping)
                    .put("scrollY", tab.scrollY)
                    .put("order", tab.visitedOrder),
            )
        }
        val document = JSONObject()
            .put("version", 1)
            .put("activeId", activeId)
            .put("nextId", nextId)
            .put("tabs", array)
        prefs().edit().putString(KEY_SESSION, document.toString()).apply()
    }

    data class RestoredSession(val urls: List<String>, val activeIndex: Int, val sleeping: List<Boolean>)

    /** Returns the tabs to open on launch, or null when there is nothing saved. */
    fun restoredSession(): RestoredSession? {
        val raw = prefs().getString(KEY_SESSION, null) ?: return null
        return try {
            val document = JSONObject(raw)
            if (document.optInt("version", 1) > 1) return null  // written by a newer app
            val array = document.optJSONArray("tabs") ?: return null
            val urls = mutableListOf<String>()
            val sleeping = mutableListOf<Boolean>()
            val ids = mutableListOf<Int>()
            var activeIndex = 0
            val activeId = document.optInt("activeId", 0)
            for (index in 0 until array.length()) {
                val entry = array.optJSONObject(index) ?: continue
                val url = entry.optString("url")
                if (url.isEmpty()) continue
                urls += url
                sleeping += entry.optBoolean("sleeping", false)
                ids += entry.optInt("id", 0)
                if (ids.last() == activeId) activeIndex = urls.size - 1
            }
            if (urls.isEmpty()) null else RestoredSession(urls, activeIndex, sleeping)
        } catch (error: Exception) {
            Log.w(TAG, "ignoring unreadable session: ${error.message}")
            null
        }
    }

    /** Rebuilds the in-memory tab list from a restored session. */
    fun applyRestoredSession(session: RestoredSession) {
        tabs.clear()
        nextId = 1
        for ((index, url) in session.urls.withIndex()) {
            val tab = Tab(id = nextId++, url = url, visitedOrder = ++clock)
            tab.sleeping = session.sleeping.getOrElse(index) { false }
            tabs += tab
        }
        activeId = tabs.getOrNull(session.activeIndex)?.id ?: 0
        listener.onTabsChanged()
    }

    fun clearSession() {
        prefs().edit().remove(KEY_SESSION).apply()
    }

    private fun prefs() = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** The handler used by callers that need to post onto the UI thread. */
    fun uiHandler(): Handler = handler

    private companion object {
        const val TAG = "BrowserXTabs"
        const val PREFS = "browserx"
        const val KEY_SESSION = "session"
        const val KEY_SEARCH = "search_template"
        const val DEFAULT_SEARCH = "https://duckduckgo.com/?q=%s"
    }
}
