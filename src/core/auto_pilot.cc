#include "core/auto_pilot.h"

#include <algorithm>

#include "core/format.h"

namespace mybrowser {
namespace core {
namespace {

constexpr int kRuleCooldownTicks = 120;   // do not repeat the same rule sooner
constexpr double kBatteryLowPercent = 35.0;
constexpr double kBatteryVeryLowPercent = 20.0;
constexpr double kPressureHigh = 0.85;
constexpr double kPressureModerate = 0.72;
constexpr int kIdleForEco = 120;

}  // namespace

AutoPilot::AutoPilot(Mode current_mode, AutoPilotMode policy)
    : current_mode_(current_mode), policy_(policy) {}

bool AutoPilot::CoolingDown(const std::string& rule) const {
  for (const auto& entry : cooldowns_) {
    if (entry.first == rule && entry.second > 0) return true;
  }
  return false;
}

AutoPilotDecision AutoPilot::Propose(Mode target, const std::string& rule, std::string reason,
                                     bool ask_only) {
  AutoPilotDecision decision;
  decision.change = true;
  decision.target = target;
  decision.rule = rule;
  decision.reason = std::move(reason);
  decision.ask_only = ask_only || policy_ == AutoPilotMode::kAsk;
  return decision;
}

void AutoPilot::NoteManualOverride() {
  override_lockout_ = kManualOverrideTicks;
  recent_actions_.push_back("manual override: auto-pilot paused for " +
                            std::to_string(kManualOverrideTicks) + "s");
}

void AutoPilot::NoteAutomaticSwitch(std::string rule) {
  current_mode_ = current_mode_;  // caller sets the mode explicitly too
  for (auto& entry : cooldowns_) {
    if (entry.first == rule) {
      entry.second = kRuleCooldownTicks;
      return;
    }
  }
  cooldowns_.emplace_back(std::move(rule), kRuleCooldownTicks);
  recent_actions_.push_back("auto-pilot switched mode");
  if (recent_actions_.size() > 32) recent_actions_.erase(recent_actions_.begin());
}

AutoPilotDecision AutoPilot::Evaluate(const AutoPilotInput& input) {
  // Tick the timers first.
  if (override_lockout_ > 0) --override_lockout_;
  for (auto& entry : cooldowns_) {
    if (entry.second > 0) --entry.second;
  }
  if (policy_ == AutoPilotMode::kOff || override_lockout_ > 0) return AutoPilotDecision{};

  const bool ask_only = policy_ == AutoPilotMode::kAsk;

  // 1) A game running in fullscreen / pointer lock: the strongest signal there
  //    is. Even in "ask" mode we want to surface this quickly.
  const bool gaming = (input.fullscreen_page && (input.pointer_locked || input.gamepad_active)) ||
                      input.pointer_locked;
  if (gaming && current_mode_ != Mode::kGaming && !CoolingDown("gaming")) {
    std::string reason = "A game appears to be running";
    if (input.pointer_locked) reason += " (pointer lock is active)";
    else if (input.gamepad_active) reason += " (gamepad activity detected)";
    reason += " — GAME mode removes vsync and background throttling";
    return Propose(Mode::kGaming, "gaming", reason, ask_only);
  }

  // 2) Memory pressure: escape to LOW RAM before the machine starts swapping.
  if (input.memory_pressure >= kPressureHigh && current_mode_ != Mode::kLowRam &&
      !CoolingDown("memory")) {
    return Propose(Mode::kLowRam, "memory",
                   "Memory pressure is " + fmt::FormatPercent(input.memory_pressure * 100.0) +
                       " — LOW RAM mode caps renderers and sleeps idle tabs",
                   ask_only);
  }

  // 3) Battery: keep the machine alive.
  if (input.on_battery && input.battery_percent >= 0 &&
      input.battery_percent <= kBatteryLowPercent && current_mode_ != Mode::kEco &&
      current_mode_ != Mode::kLowRam && !CoolingDown("battery")) {
    const bool very_low = input.battery_percent <= kBatteryVeryLowPercent;
    return Propose(very_low ? Mode::kLowRam : Mode::kEco, "battery",
                   "Running on battery at " +
                       fmt::FormatPercent(input.battery_percent, 0) +
                       (very_low ? " — switching to LOW RAM to stretch the remaining charge"
                                 : " — ECO mode saves power until you plug in"),
                   ask_only);
  }

  // 4) Plugged in, plenty of memory, doing real work: offer HIGH PERFORMANCE.
  if (!input.on_battery && input.memory_pressure < kPressureModerate &&
      input.open_tabs >= 4 && current_mode_ == Mode::kEco && !CoolingDown("plugged-in")) {
    return Propose(Mode::kHighPerformance, "plugged-in",
                   "Back on AC power with memory to spare — HIGH PERFORMANCE mode restores "
                   "full speed",
                   ask_only);
  }

  // 5) Long idle on battery: ECO to stop background work entirely.
  if (input.on_battery && input.seconds_since_user_input >= kIdleForEco &&
      current_mode_ != Mode::kEco && !CoolingDown("idle")) {
    return Propose(Mode::kEco, "idle",
                   "No input for " + fmt::FormatDuration(input.seconds_since_user_input) +
                       " while on battery — ECO mode stops background wake-ups",
                   ask_only);
  }

  return AutoPilotDecision{};
}

}  // namespace core
}  // namespace mybrowser
