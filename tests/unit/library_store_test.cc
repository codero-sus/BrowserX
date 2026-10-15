// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Unit tests for the on-disk part of the library: the file names, the atomic write, the
// debounce, and the rules about damaged files. These run against a real temporary
// directory, because the interesting bugs here (a file written somewhere else, a corrupt
// file silently replaced, a save that never happens) only appear when files are involved.

#include "app/library_store.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "test_harness.h"

namespace {

using browserx::app::LibraryStore;
using browserx::app::Paths;

/// A directory that cleans itself up, so a failed test cannot leave debris behind.
class TempProfile {
 public:
  TempProfile() {
    const auto base = std::filesystem::temp_directory_path() / "browserx-library-test";
    static int counter = 0;
    dir_ = base / std::to_string(++counter);
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
    std::filesystem::create_directories(dir_, error);
    paths_.profile_dir = dir_.string();
  }
  ~TempProfile() {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  const Paths& paths() const { return paths_; }
  std::string file(const char* name) const { return (dir_ / name).string(); }
  bool exists(const std::string& path) const { return std::filesystem::exists(path); }
  std::string read(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return {};
    std::string out;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, got);
    std::fclose(f);
    return out;
  }
  void write(const std::string& path, const std::string& text) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
  }

 private:
  std::filesystem::path dir_;
  Paths paths_;
};

}  // namespace

TEST(LibraryStore, NewProfileLoadsQuietlyAndSavesWhereThePathsSay) {
  TempProfile profile;
  LibraryStore store;
  store.Load(profile.paths());

  EXPECT_TRUE(store.last_error().empty()) << "a fresh profile is not an error";
  EXPECT_EQ(store.last_note(), std::string("history: none yet; bookmarks: none yet"));
  EXPECT_EQ(store.history().size(), size_t{0});
  EXPECT_FALSE(store.dirty());

  EXPECT_TRUE(store.RecordVisit("https://example.com/", "Example", 1000));
  EXPECT_TRUE(store.dirty()) << "a recorded visit marks the store dirty";
  EXPECT_TRUE(store.SaveNow());
  EXPECT_FALSE(store.dirty());

  // Saved under the profile paths, and nowhere else.
  EXPECT_TRUE(profile.exists(profile.file("history.json")));
  EXPECT_TRUE(profile.exists(profile.file("bookmarks.json")));
  EXPECT_TRUE(profile.read(profile.file("history.json")).find("https://example.com") !=
              std::string::npos);
  EXPECT_TRUE(store.last_error().empty());
}

TEST(LibraryStore, ReloadingGivesBackWhatWasSaved) {
  TempProfile profile;
  {
    LibraryStore store;
    store.Load(profile.paths());
    store.RecordVisit("https://a.example/one", "One", 5000);
    store.RecordVisit("https://b.example/two", "Two", 6000);
    store.bookmarks().Add("https://a.example/one", "One", "reading", 5000);
    EXPECT_TRUE(store.SaveNow());
  }
  LibraryStore reopened;
  reopened.Load(profile.paths());
  EXPECT_EQ(reopened.history().size(), size_t{2});
  EXPECT_EQ(reopened.history().entries()[0].url, std::string("https://b.example/two"))
      << "the order of the file is the order in the UI: newest first";
  EXPECT_EQ(reopened.bookmarks().size(), size_t{1});
  EXPECT_TRUE(reopened.bookmarks().Contains("https://a.example/one"));
  EXPECT_TRUE(reopened.last_note().find("history: 2") != std::string::npos);
  EXPECT_TRUE(reopened.last_note().find("bookmarks: 1") != std::string::npos);
}

TEST(LibraryStore, SaveIfDirtyOnlyWritesWhenSomethingChanged) {
  TempProfile profile;
  LibraryStore store;
  store.Load(profile.paths());
  EXPECT_TRUE(store.SaveIfDirty()) << "nothing to do is success, not a write";
  EXPECT_FALSE(profile.exists(profile.file("history.json")))
      << "an untouched profile must not create files";
  store.RecordVisit("https://example.com/", "Example", 1000);
  EXPECT_TRUE(store.SaveIfDirty());
  EXPECT_TRUE(profile.exists(profile.file("history.json")));
}

TEST(LibraryStore, ADamagedFileIsPreservedNotOverwritten) {
  TempProfile profile;
  profile.write(profile.file("history.json"), "{not json at all");

  LibraryStore store;
  store.Load(profile.paths());
  EXPECT_TRUE(store.history().size() == size_t{0}) << "a damaged file loads as empty";
  EXPECT_FALSE(store.last_error().empty()) << "and the problem is reported, not swallowed";
  EXPECT_TRUE(profile.exists(profile.file("history.json.corrupt")))
      << "the damaged bytes are kept: a parse bug must never delete what someone browsed";
  EXPECT_EQ(profile.read(profile.file("history.json.corrupt")), std::string("{not json at all"));

  // An unreadable-if-wrong-type file is handled the same way.
  TempProfile other;
  other.write(other.file("bookmarks.json"), R"({"entries":"not an array"})");
  LibraryStore second;
  second.Load(other.paths());
  EXPECT_EQ(second.bookmarks().size(), size_t{0});
  EXPECT_TRUE(other.exists(other.file("bookmarks.json.corrupt")));
}

TEST(LibraryStore, LimitsAreAppliedToBothLists) {
  TempProfile profile;
  LibraryStore store;
  store.Load(profile.paths());
  for (int i = 0; i < 6; ++i) {
    store.RecordVisit("https://page.example/" + std::to_string(i), "Page", 1000 + i);
  }
  store.ApplyLimits(/*max_history=*/2, /*max_bookmarks=*/100);
  EXPECT_EQ(store.history().size(), size_t{2});
  EXPECT_EQ(store.history().entries()[0].url, std::string("https://page.example/5"));
  EXPECT_TRUE(store.SaveNow());
  EXPECT_EQ(store.history().limit(), size_t{2});
}
