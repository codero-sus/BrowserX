// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/library.h"

#include "test_harness.h"

using namespace browserx::core;

namespace {

constexpr int64_t kMinute = 60 * 1000;
constexpr int64_t kHour = 60 * kMinute;
constexpr int64_t kDay = 24 * kHour;
constexpr int64_t kNow = 1'700'000'000'000LL;

}  // namespace

TEST(Library, HistoryRecordsAndDeduplicatesPages) {
  History history;
  EXPECT_TRUE(history.Add("https://example.com/", "Example", kNow));
  EXPECT_TRUE(history.Add("https://example.com/#top", "", kNow + kMinute))
      << "the same page with a fragment is the same page";
  EXPECT_EQ(history.size(), size_t{1}) << "one page, two visits";
  EXPECT_EQ(history.entries()[0].visit_count, int64_t{2});
  EXPECT_EQ(history.entries()[0].title, std::string("Example"))
      << "an empty title from a later visit must not erase a real one";
  EXPECT_EQ(history.total_visits(), int64_t{2});

  // Tracking parameters are stripped, so the same article is one entry however it
  // was reached.
  EXPECT_TRUE(history.Add("https://news.example/a?utm_source=x&id=7", "Story A", kNow + 2 * kMinute));
  EXPECT_TRUE(history.Add("https://news.example/a?id=7&utm_medium=y", "", kNow + 3 * kMinute));
  EXPECT_EQ(history.size(), size_t{2});
  EXPECT_EQ(history.entries()[0].url, std::string("https://news.example/a?id=7"))
      << "the query we keep is the one that identifies the page";

  // Newest first, always.
  EXPECT_EQ(history.entries()[1].url, std::string("https://example.com"));
}

TEST(Library, HistoryIgnoresUnrecordableUrlsAndCapsItself) {
  History history;
  EXPECT_FALSE(history.Add("", "nothing", kNow));
  EXPECT_FALSE(history.Add("javascript:alert(1)", "trap", kNow));
  EXPECT_FALSE(history.Add("about:blank", "blank", kNow))
      << "the browser's own pages are not places you visited";
  EXPECT_FALSE(history.Add("data:text/html,hi", "data", kNow));
  EXPECT_TRUE(history.Add("http://localhost:8080/app", "Local", kNow));
  EXPECT_EQ(history.size(), size_t{1});

  history.SetLimit(3);
  for (int i = 0; i < 10; ++i) {
    history.Add("https://page.example/" + std::to_string(i), "Page " + std::to_string(i),
                kNow + i * kMinute);
  }
  EXPECT_EQ(history.size(), size_t{3});
  EXPECT_EQ(history.entries()[0].url, std::string("https://page.example/9")) << "newest kept";
  EXPECT_EQ(history.entries()[2].url, std::string("https://page.example/7")) << "oldest dropped";
  EXPECT_EQ(history.limit(), size_t{3});
  history.SetLimit(0);
  EXPECT_EQ(history.limit(), History::kDefaultLimit) << "0 means 'the default', not 'keep nothing'";
}

TEST(Library, HistorySearchOrderAndClearing) {
  History history;
  history.Add("https://docs.example/guide", "The Guide", kNow - 5 * kHour);
  history.Add("https://docs.example/guide", "The Guide", kNow - 4 * kHour);
  history.Add("https://shop.example/cart", "Your Cart", kNow - 30 * kMinute);
  history.Add("https://news.example/guide-to-tea", "Tea", kNow - 10 * kMinute);

  // Search covers URL and title, case-insensitively, newest first.
  const auto guide = history.Search("guide");
  EXPECT_EQ(guide.size(), size_t{2});
  EXPECT_EQ(guide[0].title, std::string("Tea")) << "title matched, and it is the newest";
  EXPECT_EQ(guide[1].url, std::string("https://docs.example/guide"));
  EXPECT_EQ(history.Search("CART").size(), size_t{1}) << "matching ignores case";
  EXPECT_EQ(history.Search("nothing here").size(), size_t{0});
  // Four visits, three pages: the guide was opened twice.
  EXPECT_EQ(history.Search("").size(), size_t{3}) << "an empty search lists everything";

  // Most visited: the page opened twice, then the newest one as the tie-breaker.
  const auto top = history.MostVisited(2);
  EXPECT_EQ(top.size(), size_t{2});
  EXPECT_EQ(top[0].url, std::string("https://docs.example/guide"));
  EXPECT_EQ(top[0].visit_count, int64_t{2});
  EXPECT_EQ(top[1].url, std::string("https://news.example/guide-to-tea"));

  // "Clear the last hour" removes exactly what is inside it.
  EXPECT_EQ(history.ClearSince(kNow - kHour), size_t{2});
  EXPECT_EQ(history.size(), size_t{1}) << "only the page visited twice is left";
  EXPECT_EQ(history.Since(kNow - 6 * kHour).size(), size_t{1});
  EXPECT_EQ(history.ClearSince(0), size_t{1}) << "0 clears everything";
  EXPECT_TRUE(history.entries().empty());
}

TEST(Library, HistoryTitlesCanBeCorrectedAndEntriesRemoved) {
  History history;
  history.Add("https://example.com/", "Loading...", kNow);
  history.SetTitle("https://example.com/", "Real Title");
  EXPECT_EQ(history.entries()[0].title, std::string("Real Title"));
  history.SetTitle("https://example.com/", "   ");
  EXPECT_EQ(history.entries()[0].title, std::string("Real Title"))
      << "a blank title is never a correction";
  EXPECT_TRUE(history.Remove("https://example.com/#fragment")) << "removal uses the same key";
  EXPECT_FALSE(history.Remove("https://example.com/"));
  EXPECT_TRUE(history.entries().empty());
}

TEST(Library, HistorySurvivesAJsonRoundTrip) {
  History history;
  history.Add("https://example.com/a", "A \"quoted\" title", kNow - kDay);
  history.Add("https://example.com/b", "B", kNow);

  History restored;
  std::string error;
  EXPECT_TRUE(restored.FromJson(history.ToJson(), &error)) << error;
  ASSERT_TRUE(restored.size() == size_t{2});
  EXPECT_EQ(restored.entries()[0].url, std::string("https://example.com/b"));
  EXPECT_EQ(restored.entries()[1].title, std::string("A \"quoted\" title"))
      << "quotes and unicode survive the file, because the JSON writer escapes them";

  // Bad input is reported, not swallowed, and never crashes.
  EXPECT_FALSE(restored.FromJson("{not json", &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(restored.FromJson("{\"entries\":42}", &error)) << "wrong type is an error";
  History empty_ok;
  EXPECT_TRUE(empty_ok.FromJson("", &error)) << "an empty file is an empty history";
  EXPECT_TRUE(empty_ok.FromJson("{\"entries\":[]}", &error));

  // Junk inside the array is skipped, and the file's order is normalised.
  History filtered;
  const char* messy =
      R"json({"entries":[{"url":"https://late.example/","visited_ms":2000,"visits":1},)json"
      R"json({"url":"javascript:alert(1)"},)json"
      R"json({"url":"https://early.example/","visited_ms":1000,"visits":3}]})json";
  EXPECT_TRUE(filtered.FromJson(messy, &error));
  ASSERT_TRUE(filtered.size() == 2);
  EXPECT_EQ(filtered.entries()[0].url, std::string("https://late.example/"))
      << "sorted newest first regardless of file order";
  EXPECT_EQ(filtered.entries()[1].visit_count, int64_t{3});
}

TEST(Library, BookmarksAddRemoveAndToggle) {
  Bookmarks bookmarks;
  EXPECT_TRUE(bookmarks.Add("https://example.com/", "Example", "reading", kNow));
  EXPECT_FALSE(bookmarks.Add("https://example.com/#x", "Example again", "", kNow))
      << "bookmarking the same page twice is a no-op, not a duplicate";
  EXPECT_TRUE(bookmarks.Contains("https://example.com"));
  EXPECT_EQ(bookmarks.size(), size_t{1});
  EXPECT_EQ(bookmarks.all()[0].folder, std::string("reading"));

  EXPECT_FALSE(bookmarks.Toggle("https://example.com/", "Example", kNow)) << "toggled off";
  EXPECT_EQ(bookmarks.size(), size_t{0});
  EXPECT_TRUE(bookmarks.Toggle("https://example.com/", "Example", kNow)) << "toggled on";
  EXPECT_TRUE(bookmarks.Contains("https://example.com/"));

  EXPECT_TRUE(bookmarks.SetFolder("https://example.com", "work"));
  EXPECT_TRUE(bookmarks.SetFolder("https://example.com", ""));
  EXPECT_EQ(bookmarks.all()[0].folder, std::string(""));
  EXPECT_FALSE(bookmarks.SetFolder("https://absent.example/", "work"));

  EXPECT_FALSE(bookmarks.Add("javascript:alert(1)", "trap", "", kNow))
      << "a bookmark must be something openable";
  EXPECT_TRUE(bookmarks.Add("browserx://modes", "Modes", "", kNow))
      << "the licence page and the mode list are worth bookmarking";
  EXPECT_FALSE(bookmarks.Remove("https://absent.example/"));
}

TEST(Library, BookmarkFoldersSearchAndJson) {
  Bookmarks bookmarks;
  bookmarks.Add("https://a.example/", "Alpha", "work", kNow);
  bookmarks.Add("https://b.example/", "Beta", "work", kNow);
  bookmarks.Add("https://c.example/", "Gamma", "reading", kNow);
  bookmarks.Add("https://d.example/", "Delta", "", kNow);

  const auto folders = bookmarks.Folders();
  ASSERT_TRUE(folders.size() == size_t{2});
  EXPECT_EQ(folders[0], std::string("reading"));
  EXPECT_EQ(folders[1], std::string("work")) << "sorted, deduplicated, no empty folder";
  EXPECT_EQ(bookmarks.InFolder("work").size(), size_t{2});
  EXPECT_EQ(bookmarks.Search("beta").size(), size_t{1}) << "titles are searchable";
  EXPECT_EQ(bookmarks.Search("work").size(), size_t{2}) << "folders are searchable too";

  Bookmarks restored;
  std::string error;
  EXPECT_TRUE(restored.FromJson(bookmarks.ToJson(), &error)) << error;
  EXPECT_EQ(restored.size(), size_t{4});
  EXPECT_TRUE(restored.Contains("https://d.example/"));
  EXPECT_EQ(restored.Folders().size(), size_t{2});

  // A duplicate in the file collapses instead of showing twice.
  Bookmarks deduped;
  EXPECT_TRUE(deduped.FromJson(
      R"({"entries":[{"url":"https://x.example/"},{"url":"https://x.example/"}]})", &error));
  EXPECT_EQ(deduped.size(), size_t{1});
}

TEST(Library, NetscapeBookmarkExportAndImportRoundTrip) {
  Bookmarks original;
  original.Add("https://a.example/", "A & B", "work", kNow);
  original.Add("https://b.example/", "Quote \" and <tags>", "", kNow);
  original.Add("https://c.example/path?x=1&y=2", "", "reading", kNow);

  const std::string html = original.ToNetscapeHtml();
  EXPECT_TRUE(html.find("<!DOCTYPE NETSCAPE-Bookmark-file-1>") != std::string::npos)
      << "the header other browsers look for";
  EXPECT_TRUE(html.find("HREF=\"https://c.example/path?x=1&amp;y=2\"") != std::string::npos)
      << "an ampersand in a query is written as an entity, which is what HTML requires and "
         "what every other browser writes";

  // A second browser imports the file and gets the same list back.
  Bookmarks imported;
  EXPECT_EQ(imported.ImportNetscapeHtml(html, kNow), size_t{3});
  EXPECT_TRUE(imported.Contains("https://a.example/"));
  EXPECT_TRUE(imported.Contains("https://c.example/path?x=1&y=2"));
  EXPECT_EQ(imported.Folders().size(), size_t{2}) << "folders come across as folders";
  const auto work = imported.InFolder("work");
  ASSERT_TRUE(work.size() == size_t{1});
  EXPECT_EQ(work[0].title, std::string("A & B")) << "entities are decoded on the way in";
  EXPECT_EQ(imported.Search("tags").size(), size_t{1})
      << "a title with a quote and angle brackets survives the round trip";

  // Real files in the wild are messier: duplicate links, uppercase tags, no title.
  Bookmarks messy;
  EXPECT_EQ(messy.ImportNetscapeHtml(
                "<DL><p><dt><A HREF=\"https://dup.example/\">One</A><DT><A HREF=\"https://dup.example/\">"
                "One again</A><DT><A HREF=\"https://bare.example/\"></A></DL>",
                kNow),
            size_t{2})
      << "the duplicate collapses, the untitled one still imports";
  EXPECT_EQ(messy.InFolder("").size(), size_t{2});
  EXPECT_EQ(Bookmarks().ImportNetscapeHtml("", kNow), size_t{0}) << "empty input is fine";
  EXPECT_EQ(Bookmarks().ImportNetscapeHtml("<html>no links here</html>", kNow), size_t{0});
}
