// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for extension manifests.
//
// CEF can load unpacked extensions and cannot promise they work, so the report is the
// feature. These tests pin the two directions that matter: a manifest that Chromium would
// reject is refused here with a reason, and a manifest that will load but partly fail says
// which parts will fail before the user relies on it.

#include "core/extensions.h"

#include <string>
#include <vector>

#include "test_harness.h"

using namespace browserx::core;

namespace {

bool Mentions(const std::vector<std::string>& lines, const std::string& text) {
  for (const std::string& line : lines) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

std::string MV2(const std::string& extra = "") {
  return R"({"manifest_version": 2, "name": "Plain extension", "version": "1.2.3")" +
         extra + "}";
}

}  // namespace

TEST(Extensions, APlainManifestTwoExtensionIsLoadable) {
  ExtensionInfo info;
  std::string error;
  EXPECT_TRUE(ParseManifest(MV2(), "plain", "/ext/plain", &info, &error));
  EXPECT_TRUE(info.name == "Plain extension");
  EXPECT_TRUE(info.version == "1.2.3");
  EXPECT_TRUE(info.manifest_version == 2);
  EXPECT_TRUE(info.loadable);
  // Even a simple extension gets the Web Store limitation: it is a fact about the browser,
  // not about this extension, and it is the thing users are most surprised by.
  EXPECT_TRUE(Mentions(info.limitations, "Web Store"));
}

TEST(Extensions, AManifestWithoutANameIsRefusedWithAReason) {
  ExtensionInfo info;
  std::string error;
  EXPECT_TRUE(!ParseManifest(R"({"manifest_version": 2, "version": "1.0"})",
                             "nameless", "/ext/x", &info, &error));
  EXPECT_TRUE(error.find("name") != std::string::npos);
}

TEST(Extensions, InvalidJsonIsRefusedRatherThanCrashing) {
  ExtensionInfo info;
  std::string error;
  EXPECT_TRUE(!ParseManifest("{not json", "broken", "/ext/x", &info, &error));
  EXPECT_TRUE(!ParseManifest("[1,2,3]", "array", "/ext/x", &info, &error));
  EXPECT_TRUE(!error.empty());
}

TEST(Extensions, AManifestVersionChromiumCannotLoadIsRefused) {
  ExtensionInfo info;
  std::string error;
  EXPECT_TRUE(!ParseManifest(R"({"manifest_version": 1, "name": "old"})",
                             "old", "/ext/old", &info, &error));
  EXPECT_TRUE(error.find("2 or 3") != std::string::npos);
  EXPECT_TRUE(!ParseManifest(R"({"name": "no version"})", "v", "/ext/v", &info, &error));
}

TEST(Extensions, ManifestThreeWithAServiceWorkerSaysSo) {
  const std::string manifest = R"({
    "manifest_version": 3,
    "name": "Background worker",
    "version": "2.0",
    "background": {"service_worker": "worker.js"}
  })";
  ExtensionInfo info;
  std::string error;
  EXPECT_TRUE(ParseManifest(manifest, "mv3", "/ext/mv3", &info, &error));
  EXPECT_TRUE(info.loadable);
  EXPECT_TRUE(Mentions(info.limitations, "service worker"));
  // "partial" is the honest word; "unsupported" would be wrong in the other direction.
  EXPECT_TRUE(Mentions(info.limitations, "partial"));
}

TEST(Extensions, UnsupportedPermissionsAreNamedIndividually) {
  const std::string manifest = R"({
    "manifest_version": 2,
    "name": "Debugger and capture",
    "version": "1.0",
    "permissions": ["storage", "debugger", "tabCapture"]
  })";
  ExtensionInfo info;
  std::string error;
  EXPECT_TRUE(ParseManifest(manifest, "debug", "/ext/debug", &info, &error));
  EXPECT_TRUE(Mentions(info.limitations, "debugger"));
  EXPECT_TRUE(Mentions(info.limitations, "tabCapture"));
  // A permission we do support must not be listed as missing - a report that names
  // everything is a report nobody reads.
  EXPECT_TRUE(!Mentions(info.limitations, "storage"));
}

TEST(Extensions, TheLoadSwitchListsOnlyLoadablePaths) {
  ExtensionInfo good;
  std::string error;
  ParseManifest(MV2(), "good", "/ext/good", &good, &error);
  ExtensionInfo bad;
  ParseManifest("{broken", "bad", "/ext/bad", &bad, &error);

  std::vector<ExtensionInfo> all = {good, bad};
  EXPECT_TRUE(LoadExtensionValue(all) == "/ext/good");
  EXPECT_TRUE(DisableExceptValue(all) == "/ext/good");
  EXPECT_TRUE(LoadExtensionValue({}) == "");
  EXPECT_TRUE(LoadExtensionValue({bad}) == "");
}

TEST(Extensions, TwoExtensionsAreCommaSeparatedTheWayChromiumReadsThem) {
  ExtensionInfo a;
  ExtensionInfo b;
  std::string error;
  ParseManifest(MV2(), "a", "/ext/a", &a, &error);
  ParseManifest(MV2(), "b", "/ext/b", &b, &error);
  EXPECT_TRUE(LoadExtensionValue({a, b}) == "/ext/a,/ext/b");
}

TEST(Extensions, TheDescriptionNamesTheVersionAndTheManifestGeneration) {
  ExtensionInfo info;
  std::string error;
  ParseManifest(MV2(), "plain", "/ext/plain", &info, &error);
  const std::string text = DescribeExtension(info);
  EXPECT_TRUE(text.find("Plain extension") != std::string::npos);
  EXPECT_TRUE(text.find("1.2.3") != std::string::npos);
  EXPECT_TRUE(text.find("Manifest V2") != std::string::npos);
}
