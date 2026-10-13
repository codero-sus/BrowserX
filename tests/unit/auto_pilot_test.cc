#include "core/auto_pilot.h"

#include "test_harness.h"

using namespace mybrowser::core;

TEST(AutoPilot, ParsePolicyStrings) {
  AutoPilotMode mode = AutoPilotMode::kAsk;
  EXPECT_TRUE(ParseAutoPilotMode("off", &mode));
  EXPECT_TRUE(mode == AutoPilotMode::kOff);
  EXPECT_TRUE(ParseAutoPilotMode("AUTO", &mode));
  EXPECT_TRUE(mode == AutoPilotMode::kAuto);
  EXPECT_TRUE(ParseAutoPilotMode("suggest", &mode));
  EXPECT_TRUE(mode == AutoPilotMode::kAsk);
  EXPECT_FALSE(ParseAutoPilotMode("maybe", &mode));
  EXPECT_EQ(std::string(AutoPilotModeId(AutoPilotMode::kAuto)), std::string("auto"));
}

TEST(AutoPilot, OffDoesNothing) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kOff);
  AutoPilotInput input;
  input.on_battery = true;
  input.battery_percent = 5.0;
  input.memory_pressure = 0.99;
  input.pointer_locked = true;
  EXPECT_FALSE(pilot.Evaluate(input).change);
}

TEST(AutoPilot, AskModeOnlyProposes) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAsk);
  AutoPilotInput input;
  input.pointer_locked = true;
  input.fullscreen_page = true;
  const AutoPilotDecision decision = pilot.Evaluate(input);
  EXPECT_TRUE(decision.change);
  EXPECT_TRUE(decision.ask_only) << "ask mode must never switch on its own";
  EXPECT_TRUE(decision.target == Mode::kGaming);
  EXPECT_FALSE(decision.reason.empty());
}

TEST(AutoPilot, DetectsGames) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput input;
  input.pointer_locked = true;
  const AutoPilotDecision decision = pilot.Evaluate(input);
  EXPECT_TRUE(decision.change);
  EXPECT_TRUE(decision.target == Mode::kGaming);
  EXPECT_EQ(decision.rule, std::string("gaming"));
  EXPECT_FALSE(decision.ask_only);

  // Already in GAME mode: nothing to do.
  AutoPilot already(Mode::kGaming, AutoPilotMode::kAuto);
  EXPECT_FALSE(already.Evaluate(input).change);

  // A gamepad alone is enough.
  AutoPilot pad_pilot(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput pad_input;
  pad_input.fullscreen_page = true;
  pad_input.gamepad_active = true;
  EXPECT_TRUE(pad_pilot.Evaluate(pad_input).change);
}

TEST(AutoPilot, ReactsToMemoryPressure) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput input;
  input.memory_pressure = 0.90;
  const AutoPilotDecision decision = pilot.Evaluate(input);
  EXPECT_TRUE(decision.change);
  EXPECT_TRUE(decision.target == Mode::kLowRam);
  EXPECT_EQ(decision.rule, std::string("memory"));

  // Below the threshold nothing happens.
  AutoPilot calm(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput gentle;
  gentle.memory_pressure = 0.75;
  EXPECT_FALSE(calm.Evaluate(gentle).change);
}

TEST(AutoPilot, ReactsToBattery) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput input;
  input.on_battery = true;
  input.battery_percent = 30.0;
  const AutoPilotDecision eco = pilot.Evaluate(input);
  EXPECT_TRUE(eco.change);
  EXPECT_TRUE(eco.target == Mode::kEco);

  AutoPilot low(Mode::kUltra, AutoPilotMode::kAuto);
  input.battery_percent = 15.0;
  const AutoPilotDecision low_ram = low.Evaluate(input);
  EXPECT_TRUE(low_ram.target == Mode::kLowRam)
      << "very low battery should prefer the lightest mode";

  // ECO already active: no churn.
  AutoPilot already(Mode::kEco, AutoPilotMode::kAuto);
  input.battery_percent = 30.0;
  EXPECT_FALSE(already.Evaluate(input).change);
}

TEST(AutoPilot, RestoresPerformanceWhenPluggedIn) {
  AutoPilot pilot(Mode::kEco, AutoPilotMode::kAuto);
  AutoPilotInput input;
  input.on_battery = false;
  input.memory_pressure = 0.4;
  input.open_tabs = 8;
  const AutoPilotDecision decision = pilot.Evaluate(input);
  EXPECT_TRUE(decision.change);
  EXPECT_TRUE(decision.target == Mode::kHighPerformance);
}

TEST(AutoPilot, GoesQuietWhenIdleOnBattery) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput input;
  input.on_battery = true;
  input.battery_percent = 80.0;
  input.seconds_since_user_input = 600;
  const AutoPilotDecision decision = pilot.Evaluate(input);
  EXPECT_TRUE(decision.change);
  EXPECT_TRUE(decision.target == Mode::kEco);
  EXPECT_EQ(decision.rule, std::string("idle"));
}

TEST(AutoPilot, ManualOverridePausesAutomation) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAuto);
  pilot.NoteManualOverride();
  EXPECT_GT(pilot.override_lockout_remaining(), 0);

  AutoPilotInput input;
  input.memory_pressure = 0.95;
  input.pointer_locked = true;
  EXPECT_FALSE(pilot.Evaluate(input).change) << "must not fight the user's choice";

  // After the lockout expires, automation resumes.
  for (int i = 0; i < AutoPilot::kManualOverrideTicks + 1; ++i) pilot.Evaluate(AutoPilotInput{});
  EXPECT_EQ(pilot.override_lockout_remaining(), 0);
  EXPECT_TRUE(pilot.Evaluate(input).change);
}

TEST(AutoPilot, RuleCooldownPreventsRepeatSwitching) {
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput input;
  input.memory_pressure = 0.95;

  const AutoPilotDecision first = pilot.Evaluate(input);
  EXPECT_TRUE(first.change);
  pilot.NoteAutomaticSwitch(first.rule);
  pilot.SetCurrentMode(first.target);

  // Same condition immediately afterwards: the rule is cooling down.
  AutoPilotInput still_pressured;
  still_pressured.memory_pressure = 0.95;
  EXPECT_FALSE(pilot.Evaluate(still_pressured).change);
  EXPECT_GT(pilot.recent_actions().size(), 0u);
}

TEST(AutoPilot, GamingBeatsMemoryPressure) {
  // Both signals fire on the same tick: the game must win, because switching
  // to LOW RAM mid-game would be the worst possible move.
  AutoPilot pilot(Mode::kUltra, AutoPilotMode::kAuto);
  AutoPilotInput input;
  input.pointer_locked = true;
  input.fullscreen_page = true;
  input.memory_pressure = 0.93;
  const AutoPilotDecision decision = pilot.Evaluate(input);
  EXPECT_TRUE(decision.target == Mode::kGaming);
}
