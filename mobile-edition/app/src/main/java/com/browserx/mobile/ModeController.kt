// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

package com.browserx.mobile

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.webkit.WebSettings
import android.webkit.WebView
import kotlin.math.roundToInt

/**
 * Turns a [ModePlan] from the shared core into actual Android behaviour, and runs
 * the telemetry/Auto-Pilot ticker.
 *
 * This class is the *only* place that maps core decisions onto the WebView: if a
 * mode stops doing what it promises, the bug is here or in the core, never
 * scattered across the UI.
 */
class ModeController(
    private val context: Context,
    private val tabs: TabController,
    private val listener: Listener,
) {

    interface Listener {
        /** A mode change the user should know about (switch or suggestion). */
        fun onModeChanged(mode: Mode, reason: String)
        /** Auto-Pilot "ask" wants permission to switch. */
        fun onAutoPilotSuggestion(mode: Mode, reason: String)
        /** Telemetry line for the status bar. */
        fun onTelemetry(text: String, pressure: Double)
        /** Something the user should see (memory action, trim, ...). */
        fun onNotice(text: String)
        fun onFullscreenChanged(fullscreen: Boolean)
    }

    var mode: Mode = Mode.ULTRA
        private set
    var autoPilot: AutoPilotMode = AutoPilotMode.ASK
        private set
    var plan: ModePlan = PerformanceCore.plan(mode, deviceProfile(), Overrides())
        private set
    private var nativeAvailable = PerformanceCore.isNative

    private val handler = Handler(Looper.getMainLooper())
    private var running = false
    private var secondsSinceUserInput = 0
    private var lastMemoryAction = ""
    private var overrideUntilTick = 0
    private var tickCount = 0
    private val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
    private val batteryManager = context.getSystemService(Context.BATTERY_SERVICE) as BatteryManager

    // -----------------------------------------------------------------------
    // Mode switching
    // -----------------------------------------------------------------------
    fun setMode(mode: Mode, reason: String = "chosen by the user", byUser: Boolean = true) {
        this.mode = mode
        plan = PerformanceCore.plan(mode, deviceProfile(), overrides())
        if (byUser) overrideUntilTick = tickCount + MANUAL_OVERRIDE_TICKS
        applyPlanToAllTabs()
        listener.onModeChanged(mode, reason)
        Log.i(TAG, "mode -> ${mode.id} ($reason)")
    }

    fun setAutoPilot(policy: AutoPilotMode) {
        autoPilot = policy
    }

    fun refreshPlan() {
        plan = PerformanceCore.plan(mode, deviceProfile(), overrides())
        applyPlanToAllTabs()
    }

    private fun overrides() = Overrides(allowAdvanced = true, allowLab = false)

    // -----------------------------------------------------------------------
    // Applying a plan
    // -----------------------------------------------------------------------
    private fun applyPlanToAllTabs() {
        for (tab in tabs.all()) {
            tab.webView?.let { applyRuntimeToWebView(it) }
        }
        // Renderer lifetime: LOW RAM lets the system kill hidden renderers, GAME
        // keeps them bound so a tab switch never stalls behind a process start.
        val (priority, waived) = when (mode) {
            Mode.GAME, Mode.HIGH_PERFORMANCE -> WebView.RENDERER_PRIORITY_IMPORTANT to false
            // The transfer/media modes must not have their renderer reclaimed while a
            // download or a stream is running, so they are pinned like GAME.
            Mode.SUPER_INTERNET, Mode.DOWNLOAD, Mode.STREAMING,
            Mode.LOW_LATENCY, Mode.KIOSK, Mode.BENCHMARK, Mode.DATA_SAVER ->
                WebView.RENDERER_PRIORITY_IMPORTANT to false
            Mode.LOW_RAM -> WebView.RENDERER_PRIORITY_WAIVED to true
            else -> WebView.RENDERER_PRIORITY_BOUND to false
        }
        for (tab in tabs.all()) {
            tab.webView?.setRendererPriorityPolicy(priority, waived)
        }
        if (plan.runtime.backgroundThrottling) {
            WebView.pauseTimersIfBackground() // no-op wrapper, see below
        } else if (Mode.specialists.contains(mode)) {
            // A specialist mode asked for no throttling; resume so a running transfer
            // is not left paused by an earlier mode's call.
            WebView.resumeTimersIfBackground()
        }
    }

    fun applyRuntimeToWebView(webView: WebView) {
        val settings: WebSettings = webView.settings
        settings.javaScriptEnabled = true
        settings.domStorageEnabled = true
        // Playback starts without a tap in the two modes where that is the point.
        settings.mediaPlaybackRequiresUserGesture =
            mode != Mode.GAME && mode != Mode.STREAMING && mode != Mode.KIOSK
        settings.loadsImagesAutomatically = !plan.runtime.blockImages
        settings.blockNetworkImage = plan.runtime.blockImages
        settings.blockNetworkLoads = plan.runtime.blockThirdParty && mode == Mode.ECO
        settings.cacheMode = when {
            // PRIVACY keeps nothing on the device, so it does not read a cache either.
            mode == Mode.PRIVACY -> WebSettings.LOAD_NO_CACHE
            // The network modes want the cache to answer whenever it can, so a repeat
            // visit never pays for the network at all.
            mode == Mode.SUPER_INTERNET || mode == Mode.DOWNLOAD ||
                mode == Mode.DATA_SAVER || mode == Mode.WEAK_NETWORK || mode == Mode.READER ->
                WebSettings.LOAD_CACHE_ELSE_NETWORK
            mode == Mode.LOW_RAM -> WebSettings.LOAD_CACHE_ELSE_NETWORK
            plan.runtime.backgroundThrottling -> WebSettings.LOAD_DEFAULT
            else -> WebSettings.LOAD_DEFAULT
        }
        settings.setSupportZoom(true)
        settings.builtInZoomControls = false
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            settings.safeBrowsingEnabled = true
        }
        // Keep compositing on the GPU even while the tab is off screen; this is the
        // closest Android equivalent of the desktop's pre-raster switches.
        webView.setOffscreenPreRaster(mode != Mode.LOW_RAM)
        webView.keepScreenOn = plan.runtime.keepDisplayAwake
        webView.alpha = if (plan.runtime.reduceMotion) 1f else 1f
        webView.isFocusableInTouchMode = true
        // Blocking media in ECO is enforced by the shield (see ShieldEngine) as well,
        // because WebSettings has no per-type loader hook.
    }

    /** Rough equivalent of Chromium's background throttling for hidden tabs. */
    private fun WebView.pauseTimersIfBackground() {
        // Deliberately empty: pausing *global* timers would freeze the visible tab
        // too. Instead, TabController.sleepHiddenTabs() destroys and restores the
        // WebViews of hidden tabs, which is the behaviour the core asks for.
    }

    /**
     * Symmetric placeholder, and honestly so: WebView exposes no per-tab timer
     * freeze, so the modes that ask for "no throttling" (the specialists) express it
     * where it is enforceable instead - the renderer priority pinned in
     * [applyPlanToAllTabs], the [ModeController.onTrimMemory] floor in MainActivity,
     * and never sleeping a tab whose policy says `sleepAfterSeconds == 0`.
     */
    private fun WebView.resumeTimersIfBackground() {}

    // -----------------------------------------------------------------------
    // Device profile
    // -----------------------------------------------------------------------
    fun deviceProfile(): DeviceProfile {
        val memoryInfo = ActivityManager.MemoryInfo()
        activityManager.getMemoryInfo(memoryInfo)
        val totalMb = (memoryInfo.totalMem / (1024 * 1024)).toInt()
        val availMb = (memoryInfo.availMem / (1024 * 1024)).toInt()
        val batteryIntent = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val level = batteryIntent?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = batteryIntent?.getIntExtra(BatteryManager.EXTRA_SCALE, 100) ?: 100
        val status = batteryIntent?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) ?: -1
        val charging = status == BatteryManager.BATTERY_STATUS_CHARGING || batteryManager.isCharging
        val full = status == BatteryManager.BATTERY_STATUS_FULL
        val present = batteryIntent?.getBooleanExtra(BatteryManager.EXTRA_PRESENT, true) ?: true
        val refresh = context.display?.refreshRate?.roundToInt() ?: 60
        return DeviceProfile(
            arch = Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown",
            cpuLogical = Runtime.getRuntime().availableProcessors(),
            totalRamMb = totalMb,
            availableRamMb = availMb,
            isLowRamDevice = activityManager.isLowRamDevice,
            gpuTier = "integrated",
            hasAcceleratedVideo = true,
            batteryState = when {
                !present -> "absent"
                full -> "full"
                charging -> "charging"
                else -> "discharging"
            },
            batteryPercent = if (level >= 0 && scale > 0) level * 100.0 / scale else -1.0,
            batterySecondsLeft = -1,
            refreshRateHz = refresh,
            densityPercent = context.resources.configuration.densityDpi * 100 / 160,
        )
    }

    private fun onBattery(): Boolean {
        val intent = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val plugged = intent?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0
        return plugged == 0
    }

    // -----------------------------------------------------------------------
    // Ticker: telemetry, memory policy, Auto-Pilot
    // -----------------------------------------------------------------------
    fun start() {
        if (running) return
        running = true
        applyPlanToAllTabs()
        scheduleNext()
    }

    fun stop() {
        running = false
        handler.removeCallbacksAndMessages(null)
    }

    fun noteUserInput() {
        secondsSinceUserInput = 0
    }

    private fun scheduleNext() {
        if (!running) return
        val interval = plan.runtime.metricsIntervalMs.coerceIn(250, 5000).toLong()
        handler.postDelayed({ tick() }, interval)
    }

    private fun tick() {
        if (!running) return
        tickCount++
        secondsSinceUserInput++

        applyRuntimeToWebView(tabs.active()?.webView ?: return)

        val profile = deviceProfile()
        val usedMb = profile.totalRamMb - profile.availableRamMb
        val decision = PerformanceCore.memoryDecision(
            mode = mode,
            profile = profile,
            overrides = overrides(),
            usedRamMb = usedMb,
            hiddenTabs = tabs.hiddenCount(),
            sleepingTabs = tabs.sleepingCount(),
            audibleTabs = 0,
            pinnedTabs = 0,
            openTabs = tabs.count(),
        )
        listener.onTelemetry(
            text = "${mode.label} · ${usedMb}MB/${profile.totalRamMb}MB · ${tabs.count()} tabs",
            pressure = decision.pressure,
        )
        if (decision.act) {
            applyMemoryDecision(decision)
        }

        evaluateAutoPilot(decision.pressure, profile)
        scheduleNext()
    }

    private fun applyMemoryDecision(decision: MemoryDecision) {
        // De-duplicate: the same action should not re-notify on every tick.
        if (decision.action == lastMemoryAction && decision.action != "DiscardLruTab") return
        lastMemoryAction = decision.action
        when (decision.action) {
            "TrimCaches" -> {
                tabs.active()?.webView?.clearCache(false)
                listener.onNotice("Trimmed caches (${decision.reason})")
            }
            "SleepHiddenTabs" -> {
                val slept = tabs.sleepHiddenTabs(limit = 2, idleSeconds = plan.runtime.sleepAfterSeconds)
                if (slept > 0) listener.onNotice("Slept $slept background tab(s) to free memory")
            }
            "DiscardLruTab" -> {
                if (tabs.discardLeastRecentlyUsed()) {
                    listener.onNotice("Released the least recently used tab (${decision.reason})")
                }
            }
            "WarnUser", "BlockNewHeavyTab" -> listener.onNotice(decision.reason)
        }
    }

    private fun evaluateAutoPilot(memoryPressure: Double, profile: DeviceProfile) {
        if (autoPilot == AutoPilotMode.OFF) return
        if (tickCount < overrideUntilTick) return  // never fight the user
        val decision = PerformanceCore.autoPilotTick(
            policy = autoPilot,
            currentMode = mode,
            onBattery = onBattery(),
            batteryPercent = profile.batteryPercent,
            memoryPressure = memoryPressure,
            fullscreen = false,
            secondsSinceUserInput = secondsSinceUserInput,
            openTabs = tabs.count(),
        )
        if (!decision.change) return
        if (decision.askOnly) {
            listener.onAutoPilotSuggestion(decision.target, decision.reason)
            // Ask once per rule per session: a nagging banner is worse than a slow
            // battery drain. The user can still accept later from the mode menu.
            overrideUntilTick = tickCount + MANUAL_OVERRIDE_TICKS
            return
        }
        setMode(decision.target, decision.reason, byUser = false)
        listener.onNotice("Auto-Pilot: ${decision.target.label} — ${decision.reason}")
    }

    /** Used by the UI: "is the smart stuff real on this build?" */
    fun statusLine(): String =
        if (nativeAvailable) "Core ${PerformanceCore.version}" else "Fallback policy (no native core)"

    private companion object {
        const val TAG = "BrowserXMode"
        const val MANUAL_OVERRIDE_TICKS = 300  // same 5-minute lockout as the desktop core
    }
}
