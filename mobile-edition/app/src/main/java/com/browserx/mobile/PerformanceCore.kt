// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

package com.browserx.mobile

import android.util.Log
import org.json.JSONObject

/**
 * The JNI facade in front of the shared performance core (`src/core/` — the same
 * C++ the desktop edition compiles into its binary).
 *
 * Two rules this class exists to enforce:
 *  1. **No policy in Kotlin.** Mode tables, thresholds, switch lists and the
 *     auto-pilot rules live in the core; this file only marshals JSON.
 *  2. **Always usable.** If the native library is missing (a debug build without
 *     the NDK, a broken ABI), every call falls back to [Fallback], which applies
 *     documented approximations and reports `nativeAvailable = false` in the UI.
 *     The browser still works; it just stops being clever.
 */
object PerformanceCore {

    private const val TAG = "BrowserXCore"

    private val nativeAvailable: Boolean = try {
        System.loadLibrary("browserxcore")
        true
    } catch (error: UnsatisfiedLinkError) {
        Log.w(TAG, "native core unavailable (${error.message}); using Kotlin fallbacks")
        false
    }

    /** True when the shared C++ core is loaded. Shown in the mode panel. */
    val isNative: Boolean get() = nativeAvailable

    /** Version string of the loaded core ("browserx-core/1"). */
    val version: String get() = if (nativeAvailable) nativeCoreVersion() else "kotlin-fallback"

    fun plan(mode: Mode, profile: DeviceProfile, overrides: Overrides): ModePlan =
        if (nativeAvailable) parsePlan(nativeBuildPlanJson(mode.id, profileJson(profile), overridesJson(overrides), ""))
        else Fallback.plan(mode, profile)

    fun memoryDecision(
        mode: Mode,
        profile: DeviceProfile,
        overrides: Overrides,
        usedRamMb: Int,
        hiddenTabs: Int,
        sleepingTabs: Int,
        audibleTabs: Int,
        pinnedTabs: Int,
        openTabs: Int,
    ): MemoryDecision {
        if (!nativeAvailable) {
            return Fallback.memoryDecision(mode, profile, usedRamMb, openTabs)
        }
        val snapshot = JSONObject()
            .put("totalRamMb", profile.totalRamMb)
            .put("availableRamMb", profile.availableRamMb)
            .put("browserRssMb", usedRamMb)
            .put("openTabs", openTabs)
            .put("hiddenTabs", hiddenTabs)
            .put("sleepingTabs", sleepingTabs)
            .put("audibleTabs", audibleTabs)
            .put("pinnedTabs", pinnedTabs)
        val json = JSONObject(
            nativeMemoryEvaluateJson(mode.id, profileJson(profile), overridesJson(overrides), snapshot.toString())
        )
        return MemoryDecision(
            act = json.optBoolean("act", false),
            action = json.optString("action", "None"),
            level = json.optInt("level", 0),
            pressure = json.optDouble("pressure", 0.0),
            reason = json.optString("reason", ""),
        )
    }

    fun autoPilotTick(
        policy: AutoPilotMode,
        currentMode: Mode,
        onBattery: Boolean,
        batteryPercent: Double,
        memoryPressure: Double,
        fullscreen: Boolean,
        secondsSinceUserInput: Int,
        openTabs: Int,
    ): AutoPilotDecision {
        if (!nativeAvailable) return Fallback.autoPilotDecision(policy, currentMode, onBattery, batteryPercent)
        val state = JSONObject()
            .put("policy", policy.id)
            .put("currentMode", currentMode.id)
            .put("onBattery", onBattery)
            .put("batteryPercent", batteryPercent)
            .put("memoryPressure", memoryPressure)
            .put("fullscreen", fullscreen)
            .put("pointerLocked", false)
            .put("gamepadActive", false)
            .put("secondsSinceUserInput", secondsSinceUserInput)
            .put("openTabs", openTabs)
            .put("heavyPageActive", false)
        val json = JSONObject(nativeAutoPilotTickJson(state.toString()))
        return AutoPilotDecision(
            change = json.optBoolean("change", false),
            askOnly = json.optBoolean("askOnly", false),
            target = Mode.fromId(json.optString("targetMode", currentMode.id)),
            reason = json.optString("reason", ""),
            rule = json.optString("rule", ""),
        )
    }

    /** Omnibox parsing: URL vs search, dangerous schemes become searches. */
    fun resolveUrl(input: String, searchTemplate: String): String =
        if (nativeAvailable) nativeResolveUrl(input, searchTemplate) else Fallback.resolveUrl(input, searchTemplate)

    fun shieldDecision(documentHost: String, requestHost: String, path: String, type: String): ShieldDecision {
        if (!nativeAvailable) return Fallback.shieldDecision(documentHost, requestHost, path, type)
        val json = JSONObject(nativeShieldDecideJson(documentHost, requestHost, path, type))
        val verdict = when (json.optString("decision", "allow")) {
            "block" -> ShieldVerdict.BLOCK
            "allowlisted" -> ShieldVerdict.ALLOWLISTED
            "disabled" -> ShieldVerdict.DISABLED
            else -> ShieldVerdict.ALLOW
        }
        return ShieldDecision(verdict, json.optString("category", "ads"))
    }

    // -----------------------------------------------------------------------
    // JSON marshalling
    // -----------------------------------------------------------------------
    private fun profileJson(profile: DeviceProfile): String = JSONObject()
        .put("osName", profile.osName)
        .put("arch", profile.arch)
        .put("cpuLogical", profile.cpuLogical)
        .put("cpuPhysical", profile.cpuLogical)
        .put("totalRamMb", profile.totalRamMb)
        .put("availableRamMb", profile.availableRamMb)
        .put("isLowRamDevice", if (profile.isLowRamDevice) 1 else 0)
        .put("gpuTier", profile.gpuTier)
        .put("hasAcceleratedVideo", profile.hasAcceleratedVideo)
        .put("batteryState", profile.batteryState)
        .put("batteryPercent", profile.batteryPercent)
        .put("batterySecondsLeft", profile.batterySecondsLeft)
        .put("refreshRateHz", profile.refreshRateHz)
        .put("densityPercent", profile.densityPercent)
        .toString()

    private fun overridesJson(overrides: Overrides): String {
        val json = JSONObject()
            .put("allowAdvanced", overrides.allowAdvanced)
            .put("allowLab", overrides.allowLab)
        overrides.cacheMb?.let { json.put("cacheMb", it) }
        overrides.rendererLimit?.let { json.put("rendererLimit", it) }
        overrides.targetFps?.let { json.put("targetFps", it) }
        return json.toString()
    }

    private fun parsePlan(json: JSONObject): ModePlan {
        val switches = mutableListOf<PlannedSwitch>()
        val boot = json.optJSONArray("bootSwitches")
        if (boot != null) {
            for (index in 0 until boot.length()) {
                val entry = boot.optJSONObject(index) ?: continue
                switches += PlannedSwitch(
                    id = entry.optString("id"),
                    value = entry.optString("value"),
                    reason = entry.optString("reason"),
                )
            }
        }
        val runtime = json.optJSONObject("runtime") ?: JSONObject()
        return ModePlan(
            mode = Mode.fromId(json.optString("mode", "ultra")),
            label = json.optString("label", "ULTRA"),
            bootSwitches = switches,
            enableFeatures = json.optStringList("enableFeatures"),
            disableFeatures = json.optStringList("disableFeatures"),
            runtime = RuntimePolicy(
                backgroundThrottling = runtime.optBoolean("backgroundThrottling", true),
                targetFps = runtime.optDouble("targetFps", 0.0),
                blockImages = runtime.optBoolean("blockImages", false),
                blockMedia = runtime.optBoolean("blockMedia", false),
                blockFonts = runtime.optBoolean("blockFonts", false),
                blockThirdParty = runtime.optBoolean("blockThirdParty", false),
                blockTrackers = runtime.optBoolean("blockTrackers", true),
                sleepAfterSeconds = runtime.optInt("sleepAfterSeconds", 1800),
                discardAfterSeconds = runtime.optInt("discardAfterSeconds", 3600),
                warnPressure = runtime.optDouble("warnPressure", 0.80),
                discardPressure = runtime.optDouble("discardPressure", 0.88),
                keepDisplayAwake = runtime.optBoolean("keepDisplayAwake", false),
                metricsIntervalMs = runtime.optInt("metricsIntervalMs", 1000),
                cacheSizeMb = runtime.optInt("cacheSizeMb", 256),
                lazyNewTabs = runtime.optBoolean("lazyNewTabs", true),
                reduceMotion = runtime.optBoolean("reduceMotion", false),
            ),
            notes = json.optStringList("notes"),
            warnings = json.optStringList("warnings"),
            memoryBudgetMb = json.optInt("memoryBudgetMb", 0),
        )
    }

    private fun JSONObject.optStringList(key: String): List<String> {
        val array = optJSONArray(key) ?: return emptyList()
        val values = mutableListOf<String>()
        for (index in 0 until array.length()) {
            values += array.optString(index)
        }
        return values
    }

    // -----------------------------------------------------------------------
    // Native declarations — keep in sync with jni/core_bridge.cc
    // -----------------------------------------------------------------------
    private external fun nativeCoreVersion(): String
    private external fun nativeModeIds(): Array<String>
    private external fun nativeModeLabels(): Array<String>
    private external fun nativeBuildPlanJson(
        modeId: String,
        profileJson: String,
        overridesJson: String,
        glBackend: String,
    ): String

    private external fun nativeMemoryEvaluateJson(
        modeId: String,
        profileJson: String,
        overridesJson: String,
        snapshotJson: String,
    ): String

    private external fun nativeMemoryPolicyJson(modeId: String, profileJson: String, overridesJson: String): String
    private external fun nativeAutoPilotTickJson(stateJson: String): String
    private external fun nativeResolveUrl(input: String, searchTemplate: String): String
    private external fun nativeShieldDecideJson(
        documentHost: String,
        requestHost: String,
        requestPath: String,
        typeName: String,
    ): String
}

/**
 * Kotlin stand-ins used only when `libbrowserxcore.so` is not loaded.
 *
 * They are intentionally simple and documented as approximations: the whole
 * point of the shared core is that these paths are never taken in a real build.
 * They exist so a missing .so degrades into "a normal browser" instead of a
 * crash, and so `nativeAvailable` in the UI can honestly say which one is live.
 */
private object Fallback {

    fun plan(mode: Mode, profile: DeviceProfile): ModePlan {
        val lowRam = profile.isLowRamDevice || profile.totalRamMb <= 4096
        val runtime = when (mode) {
            Mode.GAME -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = false, blockMedia = false,
                blockFonts = false, blockThirdParty = false, blockTrackers = true, sleepAfterSeconds = 3600,
                discardAfterSeconds = 7200, warnPressure = 0.85, discardPressure = 0.92,
                keepDisplayAwake = true, metricsIntervalMs = 500, cacheSizeMb = 1024,
                lazyNewTabs = false, reduceMotion = false,
            )
            Mode.HIGH_PERFORMANCE -> runtimeFor(mode, 1536, 3600, true, false)
            Mode.LOW_RAM -> RuntimePolicy(
                backgroundThrottling = true, targetFps = 60.0, blockImages = false, blockMedia = false,
                blockFonts = false, blockThirdParty = false, blockTrackers = true, sleepAfterSeconds = 120,
                discardAfterSeconds = 300, warnPressure = 0.70, discardPressure = 0.78,
                keepDisplayAwake = false, metricsIntervalMs = 1000, cacheSizeMb = 48,
                lazyNewTabs = true, reduceMotion = true,
            )
            Mode.ECO -> RuntimePolicy(
                backgroundThrottling = true, targetFps = 30.0, blockImages = false, blockMedia = true,
                blockFonts = true, blockThirdParty = true, blockTrackers = true, sleepAfterSeconds = 600,
                discardAfterSeconds = 1800, warnPressure = 0.72, discardPressure = 0.80,
                keepDisplayAwake = false, metricsIntervalMs = 3000, cacheSizeMb = 128,
                lazyNewTabs = true, reduceMotion = true,
            )
            Mode.ULTRA -> RuntimePolicy(
                backgroundThrottling = true, targetFps = 0.0, blockImages = false, blockMedia = false,
                blockFonts = false, blockThirdParty = false, blockTrackers = true, sleepAfterSeconds = 1800,
                discardAfterSeconds = 3600, warnPressure = if (lowRam) 0.72 else 0.80,
                discardPressure = if (lowRam) 0.80 else 0.88, keepDisplayAwake = false,
                metricsIntervalMs = 1000, cacheSizeMb = if (lowRam) 64 else 256, lazyNewTabs = true,
                reduceMotion = false,
            )
            // Specialist modes (approximate fallback only — the C++ core has the real
            // policy). Same shape as the desktop: nothing throttled, nothing unloaded,
            // thresholds raised, tight telemetry.
            Mode.SUPER_INTERNET -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = false, blockMedia = false,
                blockFonts = false, blockThirdParty = false, blockTrackers = true, sleepAfterSeconds = 0,
                discardAfterSeconds = 0, warnPressure = 0.90, discardPressure = 0.95,
                keepDisplayAwake = false, metricsIntervalMs = 500,
                cacheSizeMb = if (lowRam) 128 else 1024, lazyNewTabs = false, reduceMotion = false,
            )
            Mode.DOWNLOAD -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = false, blockMedia = false,
                blockFonts = false, blockThirdParty = false, blockTrackers = true, sleepAfterSeconds = 0,
                discardAfterSeconds = 0, warnPressure = 0.92, discardPressure = 0.97,
                keepDisplayAwake = true, metricsIntervalMs = 1000,
                cacheSizeMb = if (lowRam) 128 else 2048, lazyNewTabs = false, reduceMotion = false,
            )
            Mode.STREAMING -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = false, blockMedia = false,
                blockFonts = false, blockThirdParty = false, blockTrackers = true, sleepAfterSeconds = 0,
                discardAfterSeconds = 0, warnPressure = 0.88, discardPressure = 0.95,
                keepDisplayAwake = true, metricsIntervalMs = 1000,
                cacheSizeMb = if (lowRam) 128 else 1024, lazyNewTabs = false, reduceMotion = false,
            )
            Mode.PRIVACY -> RuntimePolicy(
                backgroundThrottling = true, targetFps = 0.0, blockImages = false,
                blockMedia = false, blockFonts = false, blockThirdParty = true,
                blockTrackers = true, sleepAfterSeconds = 300, discardAfterSeconds = 1800,
                warnPressure = 0.72, discardPressure = 0.80, keepDisplayAwake = false,
                metricsIntervalMs = 1000, cacheSizeMb = 8, lazyNewTabs = true, reduceMotion = false,
            )
            Mode.DATA_SAVER -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = true,
                blockMedia = true, blockFonts = true, blockThirdParty = true,
                blockTrackers = true, sleepAfterSeconds = 1200, discardAfterSeconds = 2700,
                warnPressure = 0.85, discardPressure = 0.92, keepDisplayAwake = false,
                metricsIntervalMs = 1000, cacheSizeMb = if (lowRam) 128 else 1024,
                lazyNewTabs = true, reduceMotion = false,
            )
            Mode.WEAK_NETWORK -> RuntimePolicy(
                backgroundThrottling = true, targetFps = 0.0, blockImages = true,
                blockMedia = false, blockFonts = false, blockThirdParty = true,
                blockTrackers = true, sleepAfterSeconds = 600, discardAfterSeconds = 1800,
                warnPressure = 0.82, discardPressure = 0.90, keepDisplayAwake = false,
                metricsIntervalMs = 1000, cacheSizeMb = if (lowRam) 128 else 1024,
                lazyNewTabs = true, reduceMotion = false,
            )
            Mode.LOW_LATENCY -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = false,
                blockMedia = false, blockFonts = false, blockThirdParty = false,
                blockTrackers = true, sleepAfterSeconds = 0, discardAfterSeconds = 0,
                warnPressure = 0.88, discardPressure = 0.94, keepDisplayAwake = false,
                metricsIntervalMs = 250, cacheSizeMb = if (lowRam) 128 else 512,
                lazyNewTabs = false, reduceMotion = false,
            )
            Mode.READER -> RuntimePolicy(
                backgroundThrottling = true, targetFps = 0.0, blockImages = false,
                blockMedia = true, blockFonts = false, blockThirdParty = false,
                blockTrackers = true, sleepAfterSeconds = 3600, discardAfterSeconds = 10800,
                warnPressure = 0.82, discardPressure = 0.90, keepDisplayAwake = false,
                metricsIntervalMs = 2000, cacheSizeMb = if (lowRam) 128 else 512,
                lazyNewTabs = true, reduceMotion = true,
            )
            Mode.BENCHMARK -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = false,
                blockMedia = false, blockFonts = false, blockThirdParty = false,
                blockTrackers = true, sleepAfterSeconds = 0, discardAfterSeconds = 0,
                warnPressure = 0.95, discardPressure = 0.98, keepDisplayAwake = false,
                metricsIntervalMs = 250, cacheSizeMb = if (lowRam) 128 else 512,
                lazyNewTabs = false, reduceMotion = false,
            )
            Mode.KIOSK -> RuntimePolicy(
                backgroundThrottling = false, targetFps = 0.0, blockImages = false,
                blockMedia = false, blockFonts = false, blockThirdParty = false,
                blockTrackers = true, sleepAfterSeconds = 0, discardAfterSeconds = 0,
                warnPressure = 0.90, discardPressure = 0.96, keepDisplayAwake = true,
                metricsIntervalMs = 5000, cacheSizeMb = if (lowRam) 128 else 512,
                lazyNewTabs = true, reduceMotion = false,
            )
        }
        return ModePlan(
            mode = mode,
            label = mode.label,
            bootSwitches = emptyList(),
            enableFeatures = emptyList(),
            disableFeatures = emptyList(),
            runtime = runtime,
            notes = listOf(
                "Native core not loaded — Kotlin fallback policy in use.",
                if (mode in Mode.specialists) {
                    "Specialist modes need the native core for their boot switches and Chromium " +
                        "features; this fallback only applies the runtime policy."
                } else {
                    "Boot switches come from the core, so the fallback applies runtime policy only."
                },
            ),
            warnings = listOf("Install a build with the NDK-compiled core for the real mode engine."),
            memoryBudgetMb = 0,
        )
    }

    private fun runtimeFor(
        mode: Mode,
        cacheMb: Int,
        sleepAfter: Int,
        prefetch: Boolean,
        reduceMotion: Boolean,
    ): RuntimePolicy = RuntimePolicy(
        backgroundThrottling = false, targetFps = 0.0, blockImages = false, blockMedia = false,
        blockFonts = false, blockThirdParty = false, blockTrackers = true, sleepAfterSeconds = sleepAfter,
        discardAfterSeconds = sleepAfter * 2, warnPressure = 0.85, discardPressure = 0.92,
        keepDisplayAwake = false, metricsIntervalMs = 1000, cacheSizeMb = cacheMb,
        lazyNewTabs = !prefetch, reduceMotion = reduceMotion,
    )

    fun memoryDecision(mode: Mode, profile: DeviceProfile, usedRamMb: Int, openTabs: Int): MemoryDecision {
        val total = profile.totalRamMb.coerceAtLeast(1)
        val pressure = (usedRamMb.toDouble() / total).coerceIn(0.0, 1.0)
        val (warn, discard) = when (mode) {
            Mode.LOW_RAM -> 0.70 to 0.78
            Mode.ECO -> 0.72 to 0.80
            Mode.GAME -> 0.85 to 0.92
            // A specialist mode accepts more pressure before it interferes, because
            // interfering means killing the transfer or the stream it was chosen for.
            Mode.SUPER_INTERNET -> 0.90 to 0.95
            Mode.DOWNLOAD -> 0.92 to 0.97
            Mode.STREAMING -> 0.88 to 0.95
            Mode.PRIVACY -> 0.72 to 0.80
            Mode.DATA_SAVER -> 0.85 to 0.92
            Mode.WEAK_NETWORK -> 0.82 to 0.90
            Mode.LOW_LATENCY -> 0.88 to 0.94
            Mode.READER -> 0.82 to 0.90
            Mode.BENCHMARK -> 0.95 to 0.98
            Mode.KIOSK -> 0.90 to 0.96
            else -> 0.80 to 0.88
        }
        val (action, level) = when {
            pressure >= discard -> "DiscardLruTab" to 3
            pressure >= warn -> "SleepHiddenTabs" to 2
            pressure >= warn - 0.05 -> "TrimCaches" to 1
            else -> "None" to 0
        }
        return MemoryDecision(
            act = action != "None",
            action = action,
            level = level,
            pressure = pressure,
            reason = if (action == "None") "" else "Android reports ${(pressure * 100).toInt()}% of RAM in use",
        )
    }

    fun autoPilotDecision(
        policy: AutoPilotMode,
        currentMode: Mode,
        onBattery: Boolean,
        batteryPercent: Double,
    ): AutoPilotDecision {
        if (policy == AutoPilotMode.OFF) return AutoPilotDecision(false, false, currentMode, "", "")
        fun propose(target: Mode, reason: String, rule: String) =
            AutoPilotDecision(true, policy == AutoPilotMode.ASK, target, reason, rule)
        // Same rule as the core: never move the user off a specialist mode to save
        // battery — that is exactly when a long transfer runs.
        if (Mode.specialists.contains(currentMode)) {
            return AutoPilotDecision(false, false, currentMode, "", "")
        }
        return when {
            onBattery && batteryPercent in 0.0..25.0 && currentMode != Mode.ECO ->
                propose(Mode.ECO, "Battery at ${batteryPercent.toInt()}%", "battery-low")
            !onBattery && currentMode == Mode.ECO ->
                propose(Mode.ULTRA, "Charger connected", "plugged-in")
            else -> AutoPilotDecision(false, false, currentMode, "", "")
        }
    }

    fun resolveUrl(input: String, searchTemplate: String): String {
        val text = input.trim()
        if (text.isEmpty()) return ""
        val looksLikeUrl = !text.contains(' ') &&
            (text.contains('.') || text.startsWith("http://") || text.startsWith("https://"))
        val safe = looksLikeUrl && !text.startsWith("javascript:") && !text.startsWith("data:")
        return if (safe && text.contains("://")) text
        else if (safe) "https://$text"
        else searchTemplate.replace("%s", android.net.Uri.encode(text))
    }

    fun shieldDecision(documentHost: String, requestHost: String, path: String, type: String): ShieldDecision {
        val thirdParty = documentHost.isNotEmpty() && requestHost.isNotEmpty() &&
            !requestHost.endsWith(documentHost)
        val trackers = listOf("doubleclick.net", "google-analytics.com", "scorecardresearch.com", "adservice")
        val blocked = thirdParty && trackers.any { requestHost.contains(it) }
        return ShieldDecision(if (blocked) ShieldVerdict.BLOCK else ShieldVerdict.ALLOW, "ads")
    }
}
