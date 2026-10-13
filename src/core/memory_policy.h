// BrowserX — memory pressure policy.
//
// The policy is a pure state machine: you feed it a snapshot of the machine and
// the tab list, and it tells you which action (if any) to take. All hysteresis,
// cooldowns and thresholds live here so they are testable and so the UI can
// explain exactly why the browser did something.

#ifndef BROWSERX_CORE_MEMORY_POLICY_H_
#define BROWSERX_CORE_MEMORY_POLICY_H_

#include <cstdint>
#include <string>

#include "core/mode_engine.h"

namespace browserx {
namespace core {

struct MemorySnapshot {
  uint64_t total_mb = 0;
  uint64_t available_mb = 0;
  uint64_t browser_rss_mb = 0;   // all Chromium processes together
  uint64_t largest_tab_mb = 0;
  int tab_count = 0;
  int hidden_tabs = 0;           // hidden and still alive (renderer resident)
  int sleeping_tabs = 0;         // already discarded
  int audible_tabs = 0;
  int pinned_tabs = 0;
  int seconds_since_last_action = 1000000;
};

enum class PressureLevel { kRelaxed, kModerate, kHigh, kCritical };

const char* PressureLevelLabel(PressureLevel level);

enum class MemoryAction {
  kNone,
  kWarnUser,        // show a notice; never destructive
  kTrimCaches,      // drop HTTP cache buffers, ask Chromium to free memory
  kSleepHiddenTabs, // put idle hidden tabs to sleep (restorable)
  kDiscardLruTab,   // discard the least recently used restorable tab
  kBlockNewHeavyTab,
};

const char* MemoryActionLabel(MemoryAction action);

struct MemoryDecision {
  bool act = false;
  MemoryAction action = MemoryAction::kNone;
  PressureLevel level = PressureLevel::kRelaxed;
  double pressure = 0.0;
  std::string reason;  // human text for the action log / UI
};

class MemoryPolicy {
 public:
  explicit MemoryPolicy(const RuntimePolicy& policy);

  /// Re-reads thresholds from a (possibly changed) runtime policy.
  void UpdatePolicy(const RuntimePolicy& policy);

  /// Computes the pressure ratio (0..1) from a snapshot. Exposed for the UI.
  static double ComputePressure(const MemorySnapshot& snapshot);
  static PressureLevel LevelFor(double pressure, double warn, double discard);

  /// The decision function. Cooldowns are handled internally, so calling this
  /// every second is safe: it will not repeat the same action immediately.
  MemoryDecision Evaluate(const MemorySnapshot& snapshot);

  /// Records that the action was carried out (keeps cooldown bookkeeping
  /// honest, and lets the UI report "last action: ...").
  void NoteActionPerformed(MemoryAction action, const std::string& detail);
  MemoryAction last_action() const { return last_action_; }
  std::string last_action_detail() const { return last_action_detail_; }
  PressureLevel current_level() const { return current_level_; }

  /// Number of automatic actions taken since start (shown in the UI so the
  /// user can see that the mode is doing something).
  int actions_taken() const { return actions_taken_; }

 private:
  RuntimePolicy policy_;
  PressureLevel current_level_ = PressureLevel::kRelaxed;
  MemoryAction last_action_ = MemoryAction::kNone;
  std::string last_action_detail_;
  int actions_taken_ = 0;
  int elevated_ticks_ = 0;
  int sleep_cooldown_ = 0;
  int discard_cooldown_ = 0;
  int trim_cooldown_ = 0;
  int warn_cooldown_ = 0;
};

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_MEMORY_POLICY_H_
