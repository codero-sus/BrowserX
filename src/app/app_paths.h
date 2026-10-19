// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — profile and directory layout.
//
//   <profile>/                 user data (settings, history, bookmarks, logs)
//     settings.json
//     history.json
//     bookmarks.json
//     sessions/last-session.json
//   <cache>/                   Chromium HTTP cache + code cache
//   <downloads>/               default download target
//
// Portable mode: if a file named "portable_mode" (or a directory named
// "profile") exists next to the executable, everything lives there instead of
// in the user's home directory - handy for USB sticks and testing.

#ifndef BROWSERX_APP_APP_PATHS_H_
#define BROWSERX_APP_APP_PATHS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace browserx {
namespace app {

struct Paths {
  std::string executable_dir;
  std::string profile_dir;
  std::string cache_dir;
  std::string code_cache_dir;
  std::string downloads_dir;
  std::string sessions_dir;
  std::string logs_dir;
  bool portable = false;

  std::string settings_file() const;
  std::string history_file() const;
  std::string bookmarks_file() const;
  std::string session_file() const;
  std::string log_file() const;
};

/// Resolves all paths for this launch (creates directories as needed).
Paths ResolvePaths();

/// Directory containing the running executable (empty on failure).
std::string ExecutableDir();

// --- tiny filesystem helpers (no exceptions, no CEF dependency) --------------
bool DirectoryExists(const std::string& path);
bool FileExists(const std::string& path);
/// Recursively creates `path` (like `mkdir -p`). Returns false on failure.
bool EnsureDirectory(const std::string& path);
/// Reads a whole file; returns false when it cannot be read.
bool ReadFileToString(const std::string& path, std::string* out);
/// Writes a file atomically (temporary file + rename) so a crash never leaves
/// a half-written settings file behind.
bool WriteStringToFileAtomically(const std::string& path, const std::string& content);
bool RemoveFile(const std::string& path);
std::string JoinPath(const std::string& a, const std::string& b);
std::string ParentDir(const std::string& path);
std::string FileName(const std::string& path);
/// Size of a file in bytes (0 when missing).
uint64_t FileSize(const std::string& path);

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_APP_PATHS_H_
