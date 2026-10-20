// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/extensions_store.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/app_paths.h"
#include "core/log.h"

namespace fs = std::filesystem;

namespace browserx {
namespace app {
namespace {

/// A manifest bigger than this is not a manifest. The limit exists because reading an
/// arbitrary file into memory on start-up should have a bound.
constexpr std::uintmax_t kMaxManifestBytes = 1024 * 1024;

bool ReadTextFile(const fs::path& path, std::string* out) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > kMaxManifestBytes) return false;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *out = buffer.str();
  return true;
}

}  // namespace

std::string ExtensionsDirectory(const std::string& profile_dir) {
  return JoinPath(profile_dir, "extensions");
}

ExtensionScan ScanExtensions(const std::string& directory) {
  ExtensionScan scan;
  scan.directory = directory;

  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    // Not an error: most users have no extensions, and saying "missing" on a first run
    // would be noise.
    return scan;
  }
  scan.directory_exists = true;

  std::vector<fs::path> folders;
  for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
    if (error) break;
    if (!entry.is_directory()) continue;
    folders.push_back(entry.path());
  }
  if (error) {
    scan.problems.push_back("the extensions folder could not be read completely");
  }
  std::sort(folders.begin(), folders.end());

  for (const fs::path& folder : folders) {
    const fs::path manifest = folder / "manifest.json";
    if (!fs::is_regular_file(manifest, error)) {
      // A folder without a manifest is simply not an extension. Listing it as "refused"
      // would be noise - but a folder that *contains* a manifest we cannot read is worth
      // saying, because the user meant to install something.
      continue;
    }
    std::string text;
    if (!ReadTextFile(manifest, &text)) {
      scan.refused.push_back(folder.filename().string() + ": manifest.json could not be read");
      continue;
    }
    core::ExtensionInfo info;
    std::string reason;
    if (!core::ParseManifest(text, folder.filename().string(), folder.string(), &info,
                             &reason)) {
      scan.refused.push_back(folder.filename().string() + ": " + reason);
      continue;
    }
    scan.extensions.push_back(std::move(info));
  }

  core::log::InfoF("extensions", "%s: %zu loadable, %zu refused", directory.c_str(),
                   scan.extensions.size(), scan.refused.size());
  return scan;
}

}  // namespace app
}  // namespace browserx
