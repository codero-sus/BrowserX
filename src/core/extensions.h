// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — extensions that CEF can actually load, and an honest report when it cannot.
//
// Chromium's extension system is not part of CEF's public contract. CEF can pass unpacked
// extensions to Chromium, and some of them work; the Web Store is not available, many
// `chrome.*` APIs are missing, and a Manifest V3 extension whose logic lives in a service
// worker is likely to do nothing at all. None of that is guessable by a user, so the job
// here is to parse a manifest, decide what is *likely* to work, and say plainly what will
// not - before the extension is loaded and silently half-works.
//
// Parsing lives in core (pure, testable); walking the disk lives in the app layer.
//
// Note on the word "likely": this cannot know whether an arbitrary extension works, and it
// does not pretend to. It reports the specific, checkable facts from the manifest - version,
// background kind, permissions CEF does not implement - and lets the user decide.

#ifndef BROWSERX_CORE_EXTENSIONS_H_
#define BROWSERX_CORE_EXTENSIONS_H_

#include <string>
#include <vector>

namespace browserx {
namespace core {

struct ExtensionInfo {
  /// Directory name (or manifest path's leaf), for display.
  std::string folder;
  /// Absolute path, as passed to Chromium.
  std::string path;
  std::string name;
  std::string version;
  /// The id the manifest declares, if any. Chrome's Web Store ignores it for unpacked
  /// loads (the path decides the id), but it is what tells two copies apart.
  std::string id;
  int manifest_version = 0;
  /// False when the manifest is missing required fields, or version 3 with a service
  /// worker - the case that most often loads and then does nothing.
  bool loadable = false;
  /// Specific reasons it may not work. Empty only when nothing is known to be missing.
  std::vector<std::string> limitations;
};

/// Parses a manifest.json. Returns false with `error` set when it is not usable at all.
bool ParseManifest(const std::string& manifest_json, const std::string& folder,
                   const std::string& path, ExtensionInfo* out, std::string* error);

/// Comma-separated list for `--load-extension`, empty when nothing is loadable.
std::string LoadExtensionValue(const std::vector<ExtensionInfo>& extensions);

/// The same list restricted to those that should be *enabled*: passed to
/// `--disable-extensions-except` so only the chosen folder loads and nothing else sneaks in
/// from Chromium's own state.
std::string DisableExceptValue(const std::vector<ExtensionInfo>& extensions);

/// Plain-language summary for the page: what loads, what will not, and why.
std::string DescribeExtension(const ExtensionInfo& extension);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_EXTENSIONS_H_
