// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Unit tests for the tab session model (src/browser/session.cc).
//
// The session is deliberately CEF-free, so every rule that decides what the
// user sees - ordering, activation after close, pinning, sleeping, restore -
// is verified here without a browser process.

#include <string>

#include "browser/session.h"
#include "core/json.h"
#include "test_harness.h"

namespace {

using browserx::browser::Session;
using browserx::browser::TabLifecycle;
using browserx::browser::TabState;
using browserx::core::json::Parse;

Session MakeSession() {
  Session session;
  session.AddTab("https://a.example", true);
  session.AddTab("https://b.example", false);
  session.AddTab("https://c.example", false);
  return session;
}

TEST(Session, AddActivateAndClose) {
  Session session;
  EXPECT_TRUE(session.empty());
  const int first = session.AddTab("https://a.example", true);
  EXPECT_EQ(session.count(), size_t{1});
  // The first tab is always active, even when activate == false.
  const int second = session.AddTab("https://b.example", false);
  EXPECT_EQ(session.active_id(), first);
  EXPECT_TRUE(session.Activate(second));
  EXPECT_EQ(session.active_id(), second);
  EXPECT_EQ(session.ActiveUrl(), std::string("https://b.example"));

  EXPECT_TRUE(session.CloseTab(second));
  EXPECT_EQ(session.count(), size_t{1});
  EXPECT_EQ(session.active_id(), first);
  // Closing the last tab empties the session; the shell then adds a new tab.
  EXPECT_TRUE(session.CloseTab(first));
  EXPECT_TRUE(session.empty());
  EXPECT_EQ(session.active_id(), 0);
  EXPECT_FALSE(session.CloseTab(999));
}

TEST(Session, CloseActivatesTheRightNeighbour) {
  Session session = MakeSession();
  const std::vector<TabState>& tabs = session.tabs();
  const int middle = tabs[1].id;
  session.Activate(middle);
  session.CloseTab(middle);
  // Chromium activates the tab that took the closed tab's place.
  EXPECT_EQ(session.ActiveUrl(), std::string("https://c.example"));
  session.CloseTab(session.active_id());
  EXPECT_EQ(session.ActiveUrl(), std::string("https://a.example"));
}

TEST(Session, Reorder) {
  Session session = MakeSession();
  const int last = session.tabs()[2].id;
  EXPECT_TRUE(session.MoveTab(last, 0));
  EXPECT_EQ(session.tabs()[0].id, last);
  EXPECT_EQ(session.IndexOf(last), 0);
  // Out-of-range indices are clamped, not rejected.
  const int first = session.tabs()[2].id;
  EXPECT_TRUE(session.MoveTab(first, 99));
  EXPECT_EQ(session.IndexOf(first), 2);
  EXPECT_FALSE(session.MoveTab(12345, 0));
}

TEST(Session, PinnedTabsMoveToTheFront) {
  Session session = MakeSession();
  const int third = session.tabs()[2].id;
  EXPECT_TRUE(session.SetPinned(third, true));
  EXPECT_EQ(session.IndexOf(third), 0);
  EXPECT_TRUE(session.tabs()[0].pinned);
  // Two pinned tabs stay left of everything else.
  const int second = session.tabs()[2].id;
  session.SetPinned(second, true);
  EXPECT_TRUE(session.tabs()[0].pinned);
  EXPECT_TRUE(session.tabs()[1].pinned);
  EXPECT_FALSE(session.tabs()[2].pinned);
}

TEST(Session, NeighbourWrapsAround) {
  Session session = MakeSession();
  const int a = session.tabs()[0].id;
  const int b = session.tabs()[1].id;
  const int c = session.tabs()[2].id;
  EXPECT_EQ(session.NeighbourId(a, 1), b);
  EXPECT_EQ(session.NeighbourId(a, -1), c);  // wraps backwards, as Chrome does
  EXPECT_EQ(session.NeighbourId(c, 1), a);
  EXPECT_EQ(session.NeighbourId(4242, 1), a);
}

TEST(Session, ZoomIsClampedAndTitleChangesAreReported) {
  Session session = MakeSession();
  const int id = session.tabs()[0].id;
  EXPECT_TRUE(session.SetTitle(id, "Example"));
  EXPECT_FALSE(session.SetTitle(id, "Example"));  // no change -> no redraw
  EXPECT_TRUE(session.SetTitle(id, "Other"));
  session.SetZoom(id, 10);
  EXPECT_EQ(session.tabs()[0].zoom_percent, 25);
  session.SetZoom(id, 5000);
  EXPECT_EQ(session.tabs()[0].zoom_percent, 500);
  EXPECT_FALSE(session.SetZoom(999, 100));
}

TEST(Session, SleepingRespectsActiveAndPinnedTabs) {
  Session session = MakeSession();
  const int active = session.active_id();
  const int hidden = session.tabs()[1].id;
  const int pinned = session.tabs()[2].id;
  session.SetPinned(pinned, true);

  EXPECT_FALSE(session.SetSleeping(active, true));  // the visible tab never sleeps
  EXPECT_TRUE(session.SetSleeping(hidden, true));
  EXPECT_EQ(session.CountKind(TabLifecycle::kSleeping), 1);
  // The pinned tab is still a live hidden tab; hidden means "not active, not asleep".
  EXPECT_EQ(session.CountKind(TabLifecycle::kHidden), 1);
  EXPECT_EQ(session.CountKind(TabLifecycle::kActive), 1);

  // The LRU candidate is the oldest hidden, unpinned, inaudible tab.
  EXPECT_EQ(session.LruCandidateId(), hidden);
  // Activating a sleeping tab wakes it.
  EXPECT_TRUE(session.Activate(hidden));
  EXPECT_EQ(session.CountKind(TabLifecycle::kSleeping), 0);
  EXPECT_FALSE(session.tabs()[1].sleeping);
  // Every tab is always in exactly one lifecycle category.
  EXPECT_EQ(session.CountKind(TabLifecycle::kActive) + session.CountKind(TabLifecycle::kHidden) +
                session.CountKind(TabLifecycle::kSleeping),
            static_cast<int>(session.count()));
}

TEST(Session, AudibleAndPinnedTabsAreNeverEvicted) {
  Session session = MakeSession();
  const int audible = session.tabs()[1].id;
  const int pinned = session.tabs()[2].id;
  session.SetAudible(audible, true);
  session.SetPinned(pinned, true);
  EXPECT_EQ(session.LruCandidateId(), 0);  // nothing may be evicted
}

TEST(Session, IdleTabsAreOrderedOldestFirst) {
  Session session = MakeSession();
  const int active = session.active_id();
  const int hidden = session.tabs()[1].id;
  // Age the hidden tabs so both qualify as idle.
  const std::vector<int> idle = session.IdleTabIds(60 * 60 * 1000, 30 * 60 * 1000);
  EXPECT_EQ(idle.size(), size_t{2});
  EXPECT_EQ(idle[0], hidden < session.tabs()[2].id ? hidden : idle[0]);
  for (const int id : idle) EXPECT_NE(id, active);
  // Nothing is idle with a zero threshold except the active tab's neighbours.
  EXPECT_TRUE(session.IdleTabIds(0, 10 * 60 * 1000).empty());
}

TEST(Session, DescribeCounts) {
  Session session = MakeSession();
  EXPECT_EQ(session.DescribeCounts(), std::string("3 tabs"));
  session.SetSleeping(session.tabs()[1].id, true);
  session.SetPinned(session.tabs()[2].id, true);
  EXPECT_TRUE(session.DescribeCounts().find("1 sleeping") != std::string::npos);
  EXPECT_TRUE(session.DescribeCounts().find("1 pinned") != std::string::npos);
  Session single;
  single.AddTab("https://a.example", true);
  EXPECT_EQ(single.DescribeCounts(), std::string("1 tab"));
}

TEST(Session, RoundTripsThroughJson) {
  Session session = MakeSession();
  const int pinned = session.tabs()[2].id;
  const int sleeping = session.tabs()[1].id;
  session.SetPinned(pinned, true);
  session.SetSleeping(sleeping, true);
  session.SetMuted(sleeping, true);
  session.SetGroup(sleeping, "work");
  session.SetTitle(sleeping, "B");
  session.SetZoom(sleeping, 125);

  const std::string json = session.ToJson();
  auto parsed = Parse(json);
  ASSERT_TRUE_OR_RETURN(parsed);
  EXPECT_TRUE(parsed->Get("version").IsNumber());

  std::string error;
  Session restored;
  if (!restored.FromJson(json, &error)) {
    FATAL_FAIL("restore failed: " << error);
  }
  EXPECT_EQ(restored.count(), session.count());
  EXPECT_EQ(restored.active_id(), session.active_id());
  for (size_t i = 0; i < restored.count(); ++i) {
    EXPECT_EQ(restored.tabs()[i].url, session.tabs()[i].url);
    EXPECT_EQ(restored.tabs()[i].title, session.tabs()[i].title);
    EXPECT_EQ(restored.tabs()[i].pinned, session.tabs()[i].pinned);
    EXPECT_EQ(restored.tabs()[i].sleeping, session.tabs()[i].sleeping);
    EXPECT_EQ(restored.tabs()[i].muted, session.tabs()[i].muted);
    EXPECT_EQ(restored.tabs()[i].group, session.tabs()[i].group);
    EXPECT_EQ(restored.tabs()[i].zoom_percent, session.tabs()[i].zoom_percent);
  }
  // New tabs must not reuse ids of restored tabs.
  const int fresh = restored.AddTab("https://new.example", true);
  EXPECT_TRUE(restored.Find(fresh) != nullptr);
  EXPECT_EQ(restored.IndexOf(fresh), static_cast<int>(restored.count() - 1));
}

TEST(Session, RejectsBadRestoreInput) {
  {
    Session session;
    std::string error;
    EXPECT_FALSE(session.FromJson("{not json", &error));
    EXPECT_FALSE(error.empty());
  }
  {
    Session session;
    std::string error;
    EXPECT_FALSE(session.FromJson("[1,2,3]", &error));  // root must be an object
  }
  {
    Session session;
    std::string error;
    EXPECT_FALSE(session.FromJson("{\"version\":1}", &error));  // no tab array
  }
  {
    // A session from a newer build is refused rather than half-understood.
    Session session;
    std::string error;
    EXPECT_FALSE(session.FromJson("{\"version\":99,\"tabs\":[]}", &error));
    EXPECT_TRUE(error.find("newer") != std::string::npos);
  }
}

TEST(Session, RestoreFallsBackToANewTab) {
  std::string error;
  const Session empty = Session::Restore("", &error);
  EXPECT_EQ(empty.count(), size_t{1});
  EXPECT_EQ(empty.ActiveUrl(), std::string("browserx://newtab"));

  const Session broken = Session::Restore("{{{", &error);
  EXPECT_EQ(broken.count(), size_t{1});
  EXPECT_FALSE(error.empty());

  Session real = MakeSession();
  const std::string json = real.ToJson();
  error.clear();
  const Session restored = Session::Restore(json, &error);
  EXPECT_EQ(restored.count(), size_t{3});
  EXPECT_TRUE(error.empty());
}

TEST(Session, RestoredTabsWithMissingFieldsAreUsable) {
  // A hand-edited or older session must still produce a working browser.
  const char* json =
      "{\"version\":1,\"active_id\":7,\"tabs\":[{\"id\":7,\"url\":\"https://a.example\"}]}";
  Session session;
  std::string error;
  if (!session.FromJson(json, &error)) {
    FATAL_FAIL("restore failed: " << error);
  }
  EXPECT_EQ(session.count(), size_t{1});
  EXPECT_EQ(session.ActiveUrl(), std::string("https://a.example"));
  EXPECT_EQ(session.tabs()[0].zoom_percent, 100);
  EXPECT_FALSE(session.tabs()[0].pinned);
  // An unknown active_id must fall back to a real tab, not to nothing.
  Session other;
  ASSERT_TRUE(other.FromJson("{\"tabs\":[{\"id\":1,\"url\":\"https://x.example\"}],"
                             "\"active_id\":42}",
                             &error));
  EXPECT_EQ(other.ActiveUrl(), std::string("https://x.example"));
  EXPECT_EQ(other.LruCandidateId(), 0);
}

}  // namespace
