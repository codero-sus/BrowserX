// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — finding unpacked extensions on disk.
//
// Chromium takes a comma-separated list of directories. The interesting work is not the
// list, it is deciding which directories to look in and reporting what was found without
// inventing certainty: a folder without a manifest is not an extension, and a manifest
// Chromium would reject is refused here with the reason it would have been rejected there.
//
// Scanning happens once at start-up, so a change to the folder needs a restart. The page
// says so rather than implying a live reload.

#ifndef BROWSERX_APP_EXTENSIONS_STORE_H_
#define BROWSERX_APP_EXTENSIONS_STORE_H_

#include <string>
#include <vector>

#include "core/extensions.h"

namespace browserx {
namespace app {

struct ExtensionScan {
  /// The directory that was scanned, for reporting even when it does not exist.
  std::string directory;
  bool directory_exists = false;
  std::vector<core::ExtensionInfo> extensions;
  /// Folders that looked like extensions and were refused, with why.
  std::vector<std::string> refused;
  /// Non-fatal problems (unreadable folder, and so on).
  std::vector<std::string> problems;
};

/// Scans `<profile>/extensions/*/manifest.json` (one level, like a checkout of unpacked
/// extensions). Never throws; a missing directory is a normal outcome, not an error.
ExtensionScan ScanExtensions(const std::string& directory);

/// The directory extensions are loaded from, given a profile directory.
std::string ExtensionsDirectory(const std::string& profile_dir);

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_EXTENSIONS_STORE_H_
