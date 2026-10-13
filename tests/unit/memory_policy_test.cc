#include "core/memory_policy.h"

#include "test_harness.h"

using namespace mybrowser::core;

namespace {

RuntimePolicy LowRamPolicy() {
  RuntimePolicy policy;
  policy.warn_pressure = 0.70;
  policy.discard_pressure = 0.78;
  return policy;
}

MemorySnapshot Snapshot(uint64_t total_mb, uint64_t available_mb, int hidden_tabs) {
  MemorySnapshot snapshot;
  snapshot.total_mb = total_mb;
  snapshot.available_mb = available_mb;
  snapshot.browser_rss_mb = total_mb - available_mb;
  snapshot.tab_count = 6;
  snapshot.hidden_tabs = hidden_tabs;
  return snapshot;
}

}  // namespace

TEST(MemoryPolicy, ComputesPressure) {
  EXPECT_NEAR(MemoryPolicy::ComputePressure(Snapshot(1000, 500, 2)), 0.5, 1e-9);
  EXPECT_NEAR(MemoryPolicy::ComputePressure(Snapshot(1000, 1000, 0)), 0.0, 1e-9);
  EXPECT_NEAR(MemoryPolicy::ComputePressure(Snapshot(0, 0, 0)), 0.0, 1e-9);
  // Bogus readings must not produce negative or >1 pressure.
  EXPECT_GE(MemoryPolicy::ComputePressure(Snapshot(1000, 5000, 0)), 0.0);
  EXPECT_LE(MemoryPolicy::ComputePressure(Snapshot(1000, 5000, 0)), 1.0);
}

TEST(MemoryPolicy, PressureLevels) {
  EXPECT_TRUE(MemoryPolicy::LevelFor(0.30, 0.70, 0.80) == PressureLevel::kRelaxed);
  EXPECT_TRUE(MemoryPolicy::LevelFor(0.72, 0.70, 0.80) == PressureLevel::kModerate);
  EXPECT_TRUE(MemoryPolicy::LevelFor(0.80, 0.70, 0.80) == PressureLevel::kHigh);
  EXPECT_TRUE(MemoryPolicy::LevelFor(0.95, 0.70, 0.80) == PressureLevel::kCritical);
  EXPECT_EQ(std::string(PressureLevelLabel(PressureLevel::kHigh)), std::string("high"));
}

TEST(MemoryPolicy, DoesNothingWhenMemoryIsFine) {
  MemoryPolicy policy(LowRamPolicy());
  const MemoryDecision decision = policy.Evaluate(Snapshot(8000, 5000, 3));
  EXPECT_FALSE(decision.act);
  EXPECT_TRUE(decision.level == PressureLevel::kRelaxed);
  EXPECT_EQ(policy.actions_taken(), 0);
}

TEST(MemoryPolicy, TrimsFirstThenWarnsIfPressurePersists) {
  MemoryPolicy policy(LowRamPolicy());
  const MemorySnapshot snapshot = Snapshot(8000, 2200, 3);  // 72.5% used
  const MemoryDecision first = policy.Evaluate(snapshot);
  EXPECT_TRUE(first.act);
  EXPECT_TRUE(first.action == MemoryAction::kTrimCaches)
      << "moderate pressure should free cache memory before nagging the user";
  EXPECT_FALSE(first.reason.empty());

  // A short spike must not produce a user-visible warning ...
  bool warned_early = false;
  for (int i = 0; i < 5; ++i) {
    const MemoryDecision decision = policy.Evaluate(snapshot);
    if (decision.act && decision.action == MemoryAction::kWarnUser) warned_early = true;
  }
  EXPECT_FALSE(warned_early);

  // ... but sustained pressure eventually does.
  bool warned = false;
  for (int i = 0; i < 80; ++i) {
    const MemoryDecision decision = policy.Evaluate(snapshot);
    if (decision.act && decision.action == MemoryAction::kWarnUser) warned = true;
  }
  EXPECT_TRUE(warned) << "sustained pressure must surface a warning";
}

TEST(MemoryPolicy, SleepsOrDiscardsUnderHighPressure) {
  RuntimePolicy runtime = LowRamPolicy();
  runtime.warn_pressure = 0.5;
  runtime.discard_pressure = 0.70;  // 77.5% used => "high"
  MemoryPolicy policy(runtime);
  policy.Evaluate(Snapshot(8000, 6000, 3));  // calm tick first
  const MemoryDecision decision = policy.Evaluate(Snapshot(8000, 1800, 3));
  EXPECT_TRUE(decision.act);
  EXPECT_TRUE(decision.action == MemoryAction::kDiscardLruTab ||
              decision.action == MemoryAction::kSleepHiddenTabs)
      << MemoryActionLabel(decision.action);
}

TEST(MemoryPolicy, DiscardsTabsAtCriticalPressure) {
  MemoryPolicy policy(LowRamPolicy());
  policy.Evaluate(Snapshot(8000, 6000, 3));
  const MemoryDecision decision = policy.Evaluate(Snapshot(8000, 1200, 3));  // 85% used
  EXPECT_TRUE(decision.act);
  EXPECT_TRUE(decision.action == MemoryAction::kDiscardLruTab ||
              decision.action == MemoryAction::kBlockNewHeavyTab);
}

TEST(MemoryPolicy, BlocksNewTabsWhenAlmostOutOfMemory) {
  MemoryPolicy policy(LowRamPolicy());
  policy.Evaluate(Snapshot(8000, 7000, 1));
  const MemoryDecision decision = policy.Evaluate(Snapshot(8000, 200, 1));  // 97.5% used
  EXPECT_TRUE(decision.act);
  EXPECT_TRUE(decision.action == MemoryAction::kBlockNewHeavyTab) << MemoryActionLabel(decision.action);
  EXPECT_TRUE(decision.reason.find("blocked") != std::string::npos);
}

TEST(MemoryPolicy, RecordsActionsForTheUi) {
  MemoryPolicy policy(LowRamPolicy());
  policy.Evaluate(Snapshot(8000, 6000, 3));
  const MemoryDecision decision = policy.Evaluate(Snapshot(8000, 2000, 3));
  if (decision.act) {
    policy.NoteActionPerformed(decision.action, decision.reason);
    EXPECT_EQ(policy.actions_taken(), 1);
    EXPECT_TRUE(policy.last_action() == decision.action);
    EXPECT_FALSE(policy.last_action_detail().empty());
  }
}

TEST(MemoryPolicy, HysteresisPreventsFlapping) {
  RuntimePolicy runtime = LowRamPolicy();
  MemoryPolicy policy(runtime);
  policy.Evaluate(Snapshot(8000, 1500, 3));  // high pressure
  EXPECT_TRUE(policy.current_level() != PressureLevel::kRelaxed);
  // Pressure dips just below the threshold: the level must not drop instantly.
  const MemoryDecision decision = policy.Evaluate(Snapshot(8000, 2600, 3));  // 67.5% used
  EXPECT_TRUE(decision.level != PressureLevel::kRelaxed);
  // A clearly relaxed reading does drop it.
  policy.Evaluate(Snapshot(8000, 6000, 3));
  EXPECT_TRUE(policy.current_level() == PressureLevel::kRelaxed);
}

TEST(MemoryPolicy, PolicyUpdatesAreHonoured) {
  MemoryPolicy policy(LowRamPolicy());
  const MemorySnapshot snapshot = Snapshot(8000, 3000, 3);  // 62.5%
  EXPECT_FALSE(policy.Evaluate(snapshot).act);

  RuntimePolicy strict;
  strict.warn_pressure = 0.5;
  strict.discard_pressure = 0.6;
  policy.UpdatePolicy(strict);
  const MemoryDecision decision = policy.Evaluate(snapshot);
  EXPECT_TRUE(decision.act) << "lowering thresholds must make the guard more active";
}
