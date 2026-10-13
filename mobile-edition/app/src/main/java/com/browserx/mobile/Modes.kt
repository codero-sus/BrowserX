package com.browserx.mobile

/**
 * The five performance modes plus Auto-Pilot.
 *
 * The [id] strings are the contract with the shared C++ core
 * (`src/core/mode_engine.cc`): they are what gets passed to JNI, what is written
 * to settings, and what appears in logs. Never translate them for display — use
 * [label] for that.
 */
enum class Mode(val id: String, val label: String, val tagline: String) {
    ULTRA("ultra", "ULTRA", "balanced speed and memory"),
    GAME("gaming", "GAME", "maximum frame rate"),
    HIGH_PERFORMANCE("high-performance", "HIGH PERFORMANCE", "maximum throughput"),
    LOW_RAM("low-ram", "LOW RAM", "smallest footprint"),
    ECO("eco", "ECO", "longest battery life");

    companion object {
        fun fromId(id: String?): Mode = entries.firstOrNull { it.id == id } ?: ULTRA

        /** Same order as the core's AllModes(), used by the mode menu. */
        val ordered: List<Mode> = listOf(ULTRA, GAME, HIGH_PERFORMANCE, LOW_RAM, ECO)
    }
}

/** Auto-Pilot policy: mirrors `core::AutoPilotMode`. */
enum class AutoPilotMode(val id: String, val label: String) {
    OFF("off", "Off"),
    ASK("ask", "Ask first"),
    AUTO("auto", "Switch automatically");

    companion object {
        fun fromId(id: String?): AutoPilotMode = entries.firstOrNull { it.id == id } ?: ASK
    }
}

/**
 * Everything the shared core needs to know about this device. Values come from
 * ActivityManager / BatteryManager / DisplayMetrics — Android has no /proc-based
 * detector, so the desktop detector is not used; only the struct is shared.
 */
data class DeviceProfile(
    val osName: String = "Android",
    val arch: String,
    val cpuLogical: Int,
    val totalRamMb: Int,
    val availableRamMb: Int,
    val isLowRamDevice: Boolean,
    val gpuTier: String = "integrated",
    val hasAcceleratedVideo: Boolean = true,
    val batteryState: String = "unknown",
    val batteryPercent: Double = -1.0,
    val batterySecondsLeft: Int = -1,
    val refreshRateHz: Int = 60,
    val densityPercent: Int = 100,
)

/** User overrides from the settings screen (mirrors `core::RuntimeOverrides`). */
data class Overrides(
    val cacheMb: Int? = null,
    val rendererLimit: Int? = null,
    val targetFps: Double? = null,
    val allowAdvanced: Boolean = true,
    val allowLab: Boolean = false,
)

/** One resolved boot switch, with the reason the core chose it. */
data class PlannedSwitch(val id: String, val value: String, val reason: String) {
    val flag: String get() = if (value.isEmpty()) "--$id" else "--$id=$value"
}

/** A complete, resolved plan for one mode (parsed from the core's JSON). */
data class ModePlan(
    val mode: Mode,
    val label: String,
    val bootSwitches: List<PlannedSwitch>,
    val enableFeatures: List<String>,
    val disableFeatures: List<String>,
    val runtime: RuntimePolicy,
    val notes: List<String>,
    val warnings: List<String>,
    val memoryBudgetMb: Int,
)

/** The live (no-restart) part of a plan; on Android these drive WebSettings. */
data class RuntimePolicy(
    val backgroundThrottling: Boolean,
    val targetFps: Double,
    val blockImages: Boolean,
    val blockMedia: Boolean,
    val blockFonts: Boolean,
    val blockThirdParty: Boolean,
    val blockTrackers: Boolean,
    val sleepAfterSeconds: Int,
    val discardAfterSeconds: Int,
    val warnPressure: Double,
    val discardPressure: Double,
    val keepDisplayAwake: Boolean,
    val metricsIntervalMs: Int,
    val cacheSizeMb: Int,
    val lazyNewTabs: Boolean,
    val reduceMotion: Boolean,
)

data class MemoryDecision(val act: Boolean, val action: String, val level: Int, val pressure: Double, val reason: String)

data class AutoPilotDecision(val change: Boolean, val askOnly: Boolean, val target: Mode, val reason: String, val rule: String)

enum class ShieldVerdict { ALLOW, BLOCK, ALLOWLISTED, DISABLED }

data class ShieldDecision(val verdict: ShieldVerdict, val category: String)
