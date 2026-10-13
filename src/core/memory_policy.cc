#include "core/memory_policy.h"

#include <algorithm>

#include "core/format.h"

namespace browserx {
namespace core {
namespace {

// Cooldowns are expressed in Evaluate() calls (one per telemetry tick).
constexpr int kSleepCooldownTicks = 20;
constexpr int kDiscardCooldownTicks = 10;
constexpr int kTrimCooldownTicks = 60;
constexpr int kWarnCooldownTicks = 45;
// Warn only after sustained moderate pressure (~15 s at 1 tick/s), so a short
// spike (a big image, a video seek) never nags the user.
constexpr int kWarnAfterElevatedTicks = 15;

}  // namespace

const char* PressureLevelLabel(PressureLevel level) {
  switch (level) {
    case PressureLevel::kRelaxed: return "relaxed";
    case PressureLevel::kModerate: return "moderate";
    case PressureLevel::kHigh: return "high";
    case PressureLevel::kCritical: return "critical";
  }
  return "?";
}

const char* MemoryActionLabel(MemoryAction action) {
  switch (action) {
    case MemoryAction::kNone: return "none";
    case MemoryAction::kWarnUser: return "warn user";
    case MemoryAction::kTrimCaches: return "trim caches";
    case MemoryAction::kSleepHiddenTabs: return "sleep hidden tabs";
    case MemoryAction::kDiscardLruTab: return "discard least-recently-used tab";
    case MemoryAction::kBlockNewHeavyTab: return "refuse new heavy tab";
  }
  return "?";
}

MemoryPolicy::MemoryPolicy(const RuntimePolicy& policy) : policy_(policy) {}

void MemoryPolicy::UpdatePolicy(const RuntimePolicy& policy) { policy_ = policy; }

double MemoryPolicy::ComputePressure(const MemorySnapshot& snapshot) {
  if (snapshot.total_mb == 0) return 0.0;
  // Pressure is "how much of the machine is in use", but with the kernel's own
  // available figure preferred when it looks sane (it accounts for reclaimable
  // page cache, which is what actually matters for allocations).
  if (snapshot.available_mb > 0 && snapshot.available_mb <= snapshot.total_mb) {
    const double used = static_cast<double>(snapshot.total_mb - snapshot.available_mb);
    return std::clamp(used / static_cast<double>(snapshot.total_mb), 0.0, 1.0);
  }
  return 0.0;
}

PressureLevel MemoryPolicy::LevelFor(double pressure, double warn, double discard) {
  if (pressure >= std::min(0.97, discard + 0.07)) return PressureLevel::kCritical;
  if (pressure >= discard) return PressureLevel::kHigh;
  if (pressure >= warn) return PressureLevel::kModerate;
  return PressureLevel::kRelaxed;
}

MemoryDecision MemoryPolicy::Evaluate(const MemorySnapshot& snapshot) {
  MemoryDecision decision;
  decision.pressure = ComputePressure(snapshot);
  decision.level = LevelFor(decision.pressure, policy_.warn_pressure, policy_.discard_pressure);

  // Hysteresis: the reported level only drops when pressure falls clearly
  // below the threshold, otherwise the UI would flap between states.
  const double release_threshold = std::max(0.05, policy_.warn_pressure - 0.07);
  if (decision.level == PressureLevel::kRelaxed && current_level_ != PressureLevel::kRelaxed &&
      decision.pressure > release_threshold) {
    decision.level = current_level_;
  }
  current_level_ = decision.level;

  if (sleep_cooldown_ > 0) --sleep_cooldown_;
  if (discard_cooldown_ > 0) --discard_cooldown_;
  if (trim_cooldown_ > 0) --trim_cooldown_;
  if (warn_cooldown_ > 0) --warn_cooldown_;

  if (decision.level == PressureLevel::kRelaxed) {
    elevated_ticks_ = 0;
    return decision;
  }
  ++elevated_ticks_;

  const bool has_sleepable = snapshot.hidden_tabs > 0;
  const bool has_discardable = snapshot.tab_count > 0 &&
                               (snapshot.hidden_tabs > 1 || snapshot.sleeping_tabs == 0);

  // Critical: refuse new heavy tabs and discard aggressively.
  if (decision.level == PressureLevel::kCritical) {
    if (decision.pressure >= 0.97) {
      decision.act = true;
      decision.action = MemoryAction::kBlockNewHeavyTab;
      decision.reason = "Memory pressure is at " + fmt::FormatPercent(decision.pressure * 100.0) +
                        " — opening new heavy tabs is blocked until pressure drops";
      return decision;
    }
    if (has_discardable && discard_cooldown_ == 0) {
      decision.act = true;
      decision.action = MemoryAction::kDiscardLruTab;
      decision.reason = "Critical memory pressure (" + fmt::FormatPercent(decision.pressure * 100.0) +
                        "): discarding the least recently used tab";
      discard_cooldown_ = kDiscardCooldownTicks;
      return decision;
    }
  }

  if (decision.level == PressureLevel::kHigh) {
    if (has_discardable && discard_cooldown_ == 0) {
      decision.act = true;
      decision.action = MemoryAction::kDiscardLruTab;
      decision.reason = "High memory pressure (" + fmt::FormatPercent(decision.pressure * 100.0) +
                        ", threshold " + fmt::FormatPercent(policy_.discard_pressure * 100.0) +
                        "): discarding the least recently used tab";
      discard_cooldown_ = kDiscardCooldownTicks;
      return decision;
    }
    if (has_sleepable && sleep_cooldown_ == 0) {
      decision.act = true;
      decision.action = MemoryAction::kSleepHiddenTabs;
      decision.reason = "Sleeping " + std::to_string(snapshot.hidden_tabs) +
                        " hidden tab(s) to relieve memory pressure";
      sleep_cooldown_ = kSleepCooldownTicks;
      return decision;
    }
    if (trim_cooldown_ == 0) {
      decision.act = true;
      decision.action = MemoryAction::kTrimCaches;
      decision.reason = "Trimming caches (HTTP cache + image caches) to hand memory back";
      trim_cooldown_ = kTrimCooldownTicks;
      return decision;
    }
  }

  if (decision.level == PressureLevel::kModerate) {
    if (trim_cooldown_ == 0 && snapshot.browser_rss_mb > 0) {
      decision.act = true;
      decision.action = MemoryAction::kTrimCaches;
      decision.reason = "Moderate memory pressure (" + fmt::FormatPercent(decision.pressure * 100.0) +
                        "): trimming caches";
      trim_cooldown_ = kTrimCooldownTicks;
      return decision;
    }
    if (warn_cooldown_ == 0 && elevated_ticks_ >= kWarnAfterElevatedTicks) {
      decision.act = true;
      decision.action = MemoryAction::kWarnUser;
      decision.reason = "Memory pressure is " + fmt::FormatPercent(decision.pressure * 100.0) +
                        "% — consider closing unused tabs or switching to LOW RAM mode";
      warn_cooldown_ = kWarnCooldownTicks;
      return decision;
    }
  }

  return decision;
}

void MemoryPolicy::NoteActionPerformed(MemoryAction action, const std::string& detail) {
  last_action_ = action;
  last_action_detail_ = detail;
  ++actions_taken_;
}

}  // namespace core
}  // namespace browserx
