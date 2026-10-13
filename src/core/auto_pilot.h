// MyBrowser — Auto-Pilot: automatic mode switching.
//
// Auto-Pilot watches a few signals (battery, memory pressure, fullscreen/game
// activity) and proposes or performs a mode change. It is deliberately
// conservative:
//   * it never overrides a manual choice for a while (override lockout),
//   * it has per-rule cooldowns, and
//   * in "ask" mode it only proposes, leaving the decision to the user.
//
// The whole thing is a pure function of its inputs, so behaviour is tested
// deterministically (see tests/unit/auto_pilot_test.cc).

#ifndef MYBROWSER_CORE_AUTO_PILOT_H_
#define MYBROWSER_CORE_AUTO_PILOT_H_

#include <string>
#include <vector>

#include "core/mode_engine.h"
#include "core/settings_schema.h"

namespace mybrowser {
namespace core {

struct AutoPilotInput {
  bool on_battery = false;
  double battery_percent = -1.0;
  double memory_pressure = 0.0;   // 0..1 from the memory guard
  bool fullscreen_page = false;   // a page requested fullscreen (likely a game/video)
  bool pointer_locked = false;    // pointer lock strongly implies a 3D game
  bool gamepad_active = false;
  int seconds_since_user_input = 0;
  int open_tabs = 0;
  bool heavy_page_active = false;  // the active tab is CPU/GPU heavy
};

struct AutoPilotDecision {
  bool change = false;
  bool ask_only = false;      // "ask" mode: show a suggestion, do not switch
  Mode target = Mode::kUltra;
  std::string reason;         // shown in the UI and written to the log
  std::string rule;           // stable rule id for tests/logs
};

class AutoPilot {
 public:
  AutoPilot(Mode current_mode, AutoPilotMode policy);

  void SetPolicy(AutoPilotMode policy) { policy_ = policy; }
  AutoPilotMode policy() const { return policy_; }
  void SetCurrentMode(Mode mode) { current_mode_ = mode; }
  Mode current_mode() const { return current_mode_; }

  /// Evaluates the current signals. Call once per telemetry tick.
  AutoPilotDecision Evaluate(const AutoPilotInput& input);

  /// Called when the user changes the mode by hand: suppresses automatic
  /// switching for `kManualOverrideTicks` ticks so we never fight the user.
  void NoteManualOverride();
  /// Called when an automatic switch happened (starts the per-rule cooldown).
  void NoteAutomaticSwitch(std::string rule);

  /// Seconds until automatic switching resumes (0 = active now).
  int override_lockout_remaining() const { return override_lockout_; }
  const std::vector<std::string>& recent_actions() const { return recent_actions_; }

  static constexpr int kManualOverrideTicks = 300;  // ~5 minutes at 1 tick/s

 private:
  bool CoolingDown(const std::string& rule) const;
  AutoPilotDecision Propose(Mode target, const std::string& rule, std::string reason,
                            bool ask_only);

  Mode current_mode_;
  AutoPilotMode policy_;
  int override_lockout_ = 0;
  std::vector<std::pair<std::string, int>> cooldowns_;  // rule -> ticks remaining
  std::vector<std::string> recent_actions_;
};

}  // namespace core
}  // namespace mybrowser

#endif  // MYBROWSER_CORE_AUTO_PILOT_H_
