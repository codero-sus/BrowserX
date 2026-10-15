// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Unit tests for the tab command table (src/browser/tab_commands.cc).
//
// This is the layer every shortcut, menu entry and chip click goes through, so
// the tests below are effectively the specification of the tab strip.

#include <string>
#include <vector>

#include "browser/session.h"
#include "browser/tab_commands.h"
#include "test_harness.h"

namespace {

using browserx::browser::ApplyTabAction;
using browserx::browser::NextGroupNameIn;
using browserx::browser::Session;
using browserx::browser::TabState;
using browserx::browser::TabAction;
using browserx::browser::TabActionOutcome;
using browserx::browser::TabCommand;
using browserx::browser::TabLabelFor;

Session MakeSession() {
  Session session;
  session.AddTab("https://a.example", true);
  session.AddTab("https://b.example", false);
  session.AddTab("https://c.example", false);
  return session;
}

TabAction Action(TabCommand command, int tab_id = 0, const std::string& argument = "") {
  TabAction action;
  action.command = command;
  action.tab_id = tab_id;
  action.argument = argument;
  return action;
}

TEST(TabCommands, NewTabActivatesAndAsksForAView) {
  Session session = MakeSession();
  const TabActionOutcome outcome = ApplyTabAction(Action(TabCommand::kNewTab), &session);
  EXPECT_TRUE(outcome.handled);
  EXPECT_EQ(session.count(), size_t{4});
  EXPECT_EQ(session.ActiveUrl(), std::string("browserx://newtab"));
  ASSERT_TRUE_OR_RETURN(outcome.create_ids.size() == 1);
  EXPECT_EQ(outcome.activate_id, outcome.create_ids[0]);
  EXPECT_TRUE(outcome.structure_changed);
}

TEST(TabCommands, CloseTabActivatesTheNeighbour) {
  Session session = MakeSession();
  const int middle = session.tabs()[1].id;
  session.Activate(middle);
  const TabActionOutcome outcome = ApplyTabAction(Action(TabCommand::kCloseTab, middle), &session);
  EXPECT_TRUE(outcome.handled);
  EXPECT_EQ(session.count(), size_t{2});
  EXPECT_EQ(session.ActiveUrl(), std::string("https://c.example"));
  ASSERT_TRUE_OR_RETURN(outcome.destroy_ids.size() == 1);
  EXPECT_EQ(outcome.destroy_ids[0], middle);
  EXPECT_EQ(outcome.closed_url, std::string("https://b.example"));
  EXPECT_TRUE(outcome.message.find("Closed") != std::string::npos);
}

TEST(TabCommands, ClosingTheLastTabLeavesANewTab) {
  Session session;
  session.AddTab("https://only.example", true);
  const TabActionOutcome outcome = ApplyTabAction(Action(TabCommand::kCloseTab), &session);
  EXPECT_TRUE(outcome.handled);
  // Never an empty window: exactly one fresh tab replaces it.
  EXPECT_EQ(session.count(), size_t{1});
  EXPECT_EQ(session.ActiveUrl(), std::string("browserx://newtab"));
  EXPECT_EQ(outcome.create_ids.size(), size_t{1});
}

TEST(TabCommands, ReopenUsesTheGivenUrl) {
  Session session = MakeSession();
  const TabActionOutcome outcome =
      ApplyTabAction(Action(TabCommand::kReopenTab, 0, "https://closed.example"), &session);
  EXPECT_TRUE(outcome.handled);
  EXPECT_EQ(session.ActiveUrl(), std::string("https://closed.example"));
  // Nothing to reopen -> nothing happens, and no view is created.
  const TabActionOutcome empty = ApplyTabAction(Action(TabCommand::kReopenTab), &session);
  EXPECT_FALSE(empty.handled);
  EXPECT_TRUE(empty.create_ids.empty());
}

TEST(TabCommands, CyclingTabsSkipsNothingAndWraps) {
  Session session = MakeSession();
  const int a = session.tabs()[0].id;
  const int b = session.tabs()[1].id;
  const int c = session.tabs()[2].id;
  EXPECT_EQ(ApplyTabAction(Action(TabCommand::kNextTab), &session).activate_id, b);
  EXPECT_EQ(session.active_id(), b);
  EXPECT_EQ(ApplyTabAction(Action(TabCommand::kNextTab), &session).activate_id, c);
  EXPECT_EQ(ApplyTabAction(Action(TabCommand::kNextTab), &session).activate_id, a);
  EXPECT_EQ(ApplyTabAction(Action(TabCommand::kPreviousTab), &session).activate_id, c);
  // A single tab cannot cycle: the command is a no-op, not an error.
  Session single;
  single.AddTab("https://a.example", true);
  EXPECT_FALSE(ApplyTabAction(Action(TabCommand::kNextTab), &single).handled);
}

TEST(TabCommands, MoveTabStopsAtTheEnds) {
  Session session = MakeSession();
  const int first = session.tabs()[0].id;
  const int last = session.tabs()[2].id;
  EXPECT_EQ(ApplyTabAction(Action(TabCommand::kMoveRight), &session).activate_id, first);
  EXPECT_EQ(session.IndexOf(first), 1);
  EXPECT_EQ(session.tabs()[1].id, first);
  // At the right edge nothing happens.
  session.Activate(last);
  const TabActionOutcome stuck = ApplyTabAction(Action(TabCommand::kMoveRight), &session);
  EXPECT_FALSE(stuck.handled);
  EXPECT_FALSE(stuck.structure_changed);
}

TEST(TabCommands, PinReordersAndSleepRespectsEligibility) {
  Session session = MakeSession();
  const int target = session.tabs()[2].id;
  const TabActionOutcome pinned = ApplyTabAction(Action(TabCommand::kTogglePin, target), &session);
  EXPECT_TRUE(pinned.handled);
  EXPECT_EQ(session.IndexOf(target), 0);

  const TabActionOutcome muted = ApplyTabAction(Action(TabCommand::kToggleMute, target), &session);
  EXPECT_TRUE(muted.handled);
  EXPECT_TRUE(session.Find(target)->muted);

  // Sleeping a background tab destroys its view but keeps the tab.
  const TabActionOutcome slept = ApplyTabAction(Action(TabCommand::kSleep, target), &session);
  EXPECT_TRUE(slept.handled);
  EXPECT_TRUE(session.Find(target)->sleeping);
  ASSERT_TRUE_OR_RETURN(slept.destroy_ids.size() == 1);
  EXPECT_EQ(slept.destroy_ids[0], target);
  EXPECT_EQ(session.count(), size_t{3});

  // The visible tab refuses to sleep (that would be a visible freeze).
  const TabActionOutcome refused =
      ApplyTabAction(Action(TabCommand::kSleep, session.active_id()), &session);
  EXPECT_TRUE(refused.handled);
  EXPECT_TRUE(refused.message.find("cannot sleep") != std::string::npos);
  EXPECT_TRUE(refused.destroy_ids.empty());
  EXPECT_FALSE(session.Find(session.active_id())->sleeping);

  // Waking re-creates the view and activates it.
  const TabActionOutcome woke = ApplyTabAction(Action(TabCommand::kWake, target), &session);
  EXPECT_TRUE(woke.handled);
  EXPECT_FALSE(session.Find(target)->sleeping);
  EXPECT_EQ(session.active_id(), target);
  ASSERT_TRUE_OR_RETURN(woke.create_ids.size() == 1);
  EXPECT_EQ(woke.create_ids[0], target);
}

TEST(TabCommands, DuplicateInsertsNextToTheSource) {
  Session session = MakeSession();
  const int source = session.tabs()[1].id;
  session.Activate(source);
  const TabActionOutcome outcome = ApplyTabAction(Action(TabCommand::kDuplicate, source), &session);
  EXPECT_TRUE(outcome.handled);
  EXPECT_EQ(session.count(), size_t{4});
  EXPECT_EQ(session.IndexOf(outcome.activate_id), 2);
  EXPECT_EQ(session.Find(outcome.activate_id)->url, std::string("https://b.example"));
}

TEST(TabCommands, CloseOthersKeepsPinnedTabsAndTheActiveOne) {
  Session session = MakeSession();
  const int pinned = session.tabs()[2].id;
  session.SetPinned(pinned, true);
  const int active = session.tabs()[1].id;  // the pinned tab is now at index 0
  session.Activate(active);

  const TabActionOutcome outcome = ApplyTabAction(Action(TabCommand::kCloseOthers), &session);
  EXPECT_TRUE(outcome.handled);
  EXPECT_EQ(session.count(), size_t{2});
  EXPECT_TRUE(session.Find(pinned) != nullptr);
  EXPECT_TRUE(session.Find(active) != nullptr);
  EXPECT_EQ(outcome.destroy_ids.size(), size_t{1});
  EXPECT_EQ(session.active_id(), active);
}

TEST(TabCommands, CloseToRightSkipsPinnedTabs) {
  Session session;
  const int left = session.AddTab("https://left.example", true);
  const int right = session.AddTab("https://right.example", false);
  const int pinned = session.AddTab("https://pinned.example", false);
  session.SetPinned(pinned, true);
  // Order is now: pinned, left, right.
  session.Activate(left);
  const TabActionOutcome outcome = ApplyTabAction(Action(TabCommand::kCloseToRight), &session);
  EXPECT_TRUE(outcome.handled);
  EXPECT_EQ(session.count(), size_t{2});
  EXPECT_TRUE(session.Find(pinned) != nullptr);
  EXPECT_TRUE(session.Find(right) == nullptr);
}

TEST(TabCommands, GroupingCyclesThroughExistingGroups) {
  Session session = MakeSession();
  const int a = session.tabs()[0].id;
  const int b = session.tabs()[1].id;
  const int c = session.tabs()[2].id;

  // With no groups yet, the first cycle invents one.
  const TabActionOutcome first = ApplyTabAction(Action(TabCommand::kCycleGroup, a), &session);
  EXPECT_TRUE(first.handled);
  EXPECT_EQ(session.Find(a)->group, std::string("work"));

  // A second tab joins another group. Cycling walks the groups in sorted order
  // and then leaves the tab ungrouped, which is the only way to escape a group.
  session.SetGroup(b, "media");
  EXPECT_EQ(NextGroupNameIn(session, *session.Find(a)), std::string(""));  // "work" is last
  ApplyTabAction(Action(TabCommand::kCycleGroup, a), &session);
  EXPECT_TRUE(session.Find(a)->group.empty());
  // Ungrouped starts at the first group again ("media" < "work").
  ApplyTabAction(Action(TabCommand::kCycleGroup, a), &session);
  EXPECT_EQ(session.Find(a)->group, std::string("media"));
  // "work" only ever existed on tab a, so it vanished with it; "media" is now the
  // last group, and cycling past the last group is how a tab leaves it.
  ApplyTabAction(Action(TabCommand::kCycleGroup, a), &session);
  EXPECT_TRUE(session.Find(a)->group.empty());
  EXPECT_FALSE(session.Find(b)->group.empty());  // b keeps its own group

  const TabActionOutcome named = ApplyTabAction(Action(TabCommand::kSetGroup, c, "code"), &session);
  EXPECT_TRUE(named.handled);
  EXPECT_EQ(session.Find(c)->group, std::string("code"));
  ApplyTabAction(Action(TabCommand::kSetGroup, c), &session);
  EXPECT_TRUE(session.Find(c)->group.empty());
}

TEST(TabCommands, LabelsCarryState) {
  Session session = MakeSession();
  const int id = session.tabs()[0].id;
  session.SetTitle(id, "Example domain");
  EXPECT_EQ(TabLabelFor(*session.Find(id)), std::string("Example domain"));

  session.SetMuted(id, true);
  EXPECT_TRUE(TabLabelFor(*session.Find(id)).find("Example domain") != std::string::npos);
  EXPECT_TRUE(TabLabelFor(*session.Find(id)).size() > std::string("Example domain").size());

  session.SetSleeping(id, true);
  session.SetPinned(id, true);
  const std::string label = TabLabelFor(*session.Find(id));
  EXPECT_TRUE(label.find("Example domain") != std::string::npos);

  // A long title is truncated but never loses its state glyph.
  session.SetTitle(id, std::string(200, 'x'));
  const std::string long_label = TabLabelFor(*session.Find(id), 12);
  EXPECT_TRUE(long_label.size() <= 12 + 12);
  EXPECT_TRUE(long_label.find('x') != std::string::npos);

  // A tab with no title falls back to its URL, then to "New tab".
  Session bare;
  const int bare_id = bare.AddTab("https://fallback.example", true);
  EXPECT_EQ(TabLabelFor(*bare.Find(bare_id)), std::string("https://fallback.example"));
  bare.SetUrl(bare_id, "");
  EXPECT_EQ(TabLabelFor(*bare.Find(bare_id)), std::string("New tab"));
}

TEST(TabCommands, UnhandledActionsAreInert) {
  Session session = MakeSession();
  // Acting on a tab that does not exist falls back to the active tab.
  const TabActionOutcome outcome = ApplyTabAction(Action(TabCommand::kTogglePin, 9999), &session);
  EXPECT_TRUE(outcome.handled);
  EXPECT_TRUE(session.active_tab()->pinned);
  // A null session never crashes.
  const TabActionOutcome null_outcome = ApplyTabAction(Action(TabCommand::kNewTab), nullptr);
  EXPECT_FALSE(null_outcome.handled);
}

TEST(TabCommands, ClosingRemembersTheTabSoReopeningRestoresIt) {
  Session session;
  const int a = session.AddTab("https://a.example/", true);
  const int b = session.AddTab("https://b.example/reading", false);
  session.SetTitle(b, "A long read");
  session.SetGroup(b, "reading");
  session.SetZoom(b, 130);
  session.SetPinned(b, true);
  session.Activate(b);
  session.AddTab("https://c.example/", false);

  // Closing through a tab command is what the strip's close button does.
  const TabActionOutcome closed = ApplyTabAction(Action(TabCommand::kCloseTab, b), &session);
  EXPECT_TRUE(closed.handled);
  EXPECT_TRUE(session.Find(b) == nullptr);
  EXPECT_EQ(session.recently_closed_count(), size_t{1});

  // Ctrl+Shift+T goes through the session's stack, not a URL passed in by the UI.
  const TabActionOutcome reopened = ApplyTabAction(Action(TabCommand::kReopenTab), &session);
  EXPECT_TRUE(reopened.handled);
  ASSERT_TRUE(reopened.create_ids.size() == 1);
  const TabState* back = session.Find(reopened.create_ids[0]);
  ASSERT_TRUE(back != nullptr);
  EXPECT_EQ(back->url, std::string("https://b.example/reading"));
  EXPECT_EQ(back->title, std::string("A long read")) << "the title comes back, not just the URL";
  EXPECT_EQ(back->group, std::string("reading"));
  EXPECT_EQ(back->zoom_percent, 130);
  EXPECT_TRUE(back->pinned);
  EXPECT_EQ(session.recently_closed_count(), size_t{0}) << "the stack is consumed";

  // With nothing to undo, an explicit URL still works, and nothing at all is a no-op.
  const TabActionOutcome from_url =
      ApplyTabAction(Action(TabCommand::kReopenTab, 0, "https://shell.example/"), &session);
  EXPECT_TRUE(from_url.handled);
  EXPECT_EQ(session.Find(from_url.activate_id)->url, std::string("https://shell.example/"));
  const TabActionOutcome nothing = ApplyTabAction(Action(TabCommand::kReopenTab), &session);
  EXPECT_FALSE(nothing.handled) << "an empty stack and no URL means there is nothing to do";

  (void)a;
}

TEST(TabCommands, BulkClosesProtectPinnedTabsAndAreUndoable) {
  Session session;
  const int a = session.AddTab("https://a.example/", true);
  const int pinned = session.AddTab("https://pinned.example/", false);
  session.SetPinned(pinned, true);
  session.AddTab("https://b.example/", false);
  session.AddTab("https://c.example/", false);
  session.Activate(a);

  const TabActionOutcome others = ApplyTabAction(Action(TabCommand::kCloseOthers), &session);
  EXPECT_TRUE(others.handled);
  EXPECT_EQ(session.count(), size_t{2}) << "the kept tab and the pinned one survive";
  EXPECT_TRUE(session.Find(pinned) != nullptr) << "pinning means 'do not touch this'";
  EXPECT_EQ(session.active_id(), a);
  EXPECT_TRUE(others.message.find("2 other tabs") != std::string::npos)
      << "the status bar says how many went";

  // Both closed tabs can be walked back, one Ctrl+Shift+T at a time.
  EXPECT_EQ(session.recently_closed_count(), size_t{2});
  EXPECT_TRUE(ApplyTabAction(Action(TabCommand::kReopenTab), &session).handled);
  EXPECT_TRUE(ApplyTabAction(Action(TabCommand::kReopenTab), &session).handled);
  EXPECT_TRUE(ApplyTabAction(Action(TabCommand::kReopenTab), &session).handled == false)
      << "and then the stack is empty";
  EXPECT_EQ(session.count(), size_t{4});

  session.Activate(a);
  const TabActionOutcome right = ApplyTabAction(Action(TabCommand::kCloseToRight), &session);
  EXPECT_TRUE(right.handled);
  EXPECT_TRUE(session.Find(pinned) != nullptr) << "close-to-right never sweeps away pinned tabs";
  EXPECT_EQ(session.active_id(), a);
}

}  // namespace
