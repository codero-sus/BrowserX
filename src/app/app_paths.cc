#include "app/app_paths.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/log.h"

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace mybrowser {
namespace app {
namespace {

std::string HomeDir() {
#if defined(_WIN32)
  const char* profile = std::getenv("USERPROFILE");
  return profile != nullptr ? std::string(profile) : std::string(".");
#else
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') return std::string(home);
  const passwd* pw = getpwuid(getuid());
  if (pw != nullptr && pw->pw_dir != nullptr) return std::string(pw->pw_dir);
  return std::string(".");
#endif
}

std::string EnvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string() : std::string(value);
}

}  // namespace

std::string JoinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  const char last = a.back();
  if (last == '/' || last == '\\') return a + b;
  return a + "/" + b;
}

std::string ParentDir(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return std::string();
  if (slash == 0) return std::string("/");
  return path.substr(0, slash);
}

std::string FileName(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string ExecutableDir() {
#if defined(_WIN32)
  char buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  if (length == 0) return std::string();
  return ParentDir(std::string(buffer, length));
#elif defined(__APPLE__)
  char buffer[4096];
  uint32_t size = sizeof(buffer);
  extern int _NSGetExecutablePath(char*, uint32_t*);
  if (_NSGetExecutablePath(buffer, &size) != 0) return std::string();
  char resolved[4096];
  if (realpath(buffer, resolved) == nullptr) return ParentDir(std::string(buffer));
  return ParentDir(std::string(resolved));
#else
  char buffer[4096];
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) return std::string();
  buffer[length] = '\0';
  return ParentDir(std::string(buffer));
#endif
}

bool DirectoryExists(const std::string& path) {
  if (path.empty()) return false;
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesA(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat info {};
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

bool FileExists(const std::string& path) {
  if (path.empty()) return false;
  struct stat info {};
  return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool EnsureDirectory(const std::string& path) {
  if (path.empty()) return false;
  if (DirectoryExists(path)) return true;
  // Create parents first.
  const std::string parent = ParentDir(path);
  if (!parent.empty() && parent != path) {
    EnsureDirectory(parent);
  }
#if defined(_WIN32)
  const int result = _mkdir(path.c_str());
#else
  const int result = mkdir(path.c_str(), 0755);
#endif
  if (result == 0) return true;
  return errno == EEXIST && DirectoryExists(path);
}

bool ReadFileToString(const std::string& path, std::string* out) {
  if (out == nullptr) return false;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  out->clear();
  char buffer[16384];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    out->append(buffer, read);
    if (out->size() > (64u << 20)) break;  // 64 MB safety cap
  }
  std::fclose(file);
  return true;
}

bool WriteStringToFileAtomically(const std::string& path, const std::string& content) {
  const std::string directory = ParentDir(path);
  if (!directory.empty()) EnsureDirectory(directory);
  const std::string temporary = path + ".tmp";
  {
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr) {
      core::log::WarnF("paths", "cannot write %s: %s", temporary.c_str(), std::strerror(errno));
      return false;
    }
    const size_t written = std::fwrite(content.data(), 1, content.size(), file);
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (written != content.size() || !flushed || !closed) {
      core::log::WarnF("paths", "short write to %s", temporary.c_str());
      std::remove(temporary.c_str());
      return false;
    }
  }
#if defined(_WIN32)
  std::remove(path.c_str());  // Windows rename() refuses to overwrite
#endif
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    core::log::WarnF("paths", "cannot replace %s: %s", path.c_str(), std::strerror(errno));
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

bool RemoveFile(const std::string& path) { return std::remove(path.c_str()) == 0; }

uint64_t FileSize(const std::string& path) {
  struct stat info {};
  if (stat(path.c_str(), &info) != 0) return 0;
  return static_cast<uint64_t>(info.st_size);
}

std::string Paths::settings_file() const { return JoinPath(profile_dir, "settings.json"); }
std::string Paths::history_file() const { return JoinPath(profile_dir, "history.json"); }
std::string Paths::bookmarks_file() const { return JoinPath(profile_dir, "bookmarks.json"); }
std::string Paths::session_file() const { return JoinPath(sessions_dir, "last-session.json"); }
std::string Paths::log_file() const { return JoinPath(logs_dir, "mybrowser.log"); }

Paths ResolvePaths() {
  Paths paths;
  paths.executable_dir = ExecutableDir();

  // Portable mode: an explicit flag, or marker files shipped next to the binary.
  const bool portable = !paths.executable_dir.empty() &&
                        (FileExists(JoinPath(paths.executable_dir, "portable_mode")) ||
                         DirectoryExists(JoinPath(paths.executable_dir, "profile")));
  paths.portable = portable;

  const std::string override_dir = EnvOrEmpty("MYBROWSER_PROFILE");
  if (!override_dir.empty()) {
    paths.profile_dir = override_dir;
    paths.portable = true;  // custom profile implies self-contained data
  } else if (portable) {
    paths.profile_dir = JoinPath(paths.executable_dir, "profile");
  } else {
#if defined(_WIN32)
    const std::string base = EnvOrEmpty("LOCALAPPDATA");
    paths.profile_dir = base.empty() ? JoinPath(HomeDir(), "MyBrowser") : JoinPath(base, "MyBrowser");
#elif defined(__APPLE__)
    paths.profile_dir = JoinPath(HomeDir(), "Library/Application Support/MyBrowser");
#else
    const std::string xdg = EnvOrEmpty("XDG_DATA_HOME");
    paths.profile_dir =
        xdg.empty() ? JoinPath(HomeDir(), ".local/share/mybrowser") : JoinPath(xdg, "mybrowser");
#endif
  }

  const std::string cache_override = EnvOrEmpty("MYBROWSER_CACHE");
  if (!cache_override.empty()) {
    paths.cache_dir = cache_override;
  } else if (paths.portable) {
    paths.cache_dir = JoinPath(paths.profile_dir, "cache");
  } else {
#if defined(_WIN32)
    paths.cache_dir = JoinPath(EnvOrEmpty("LOCALAPPDATA").empty() ? paths.profile_dir
                                                                 : EnvOrEmpty("LOCALAPPDATA"),
                               "MyBrowser/cache");
#elif defined(__APPLE__)
    paths.cache_dir = JoinPath(HomeDir(), "Library/Caches/MyBrowser");
#else
    const std::string xdg_cache = EnvOrEmpty("XDG_CACHE_HOME");
    paths.cache_dir =
        xdg_cache.empty() ? JoinPath(HomeDir(), ".cache/mybrowser") : JoinPath(xdg_cache, "mybrowser");
#endif
  }
  paths.code_cache_dir = JoinPath(paths.profile_dir, "code_cache");
  paths.sessions_dir = JoinPath(paths.profile_dir, "sessions");
  paths.logs_dir = JoinPath(paths.profile_dir, "logs");

  const std::string downloads_override = EnvOrEmpty("MYBROWSER_DOWNLOADS");
  if (!downloads_override.empty()) {
    paths.downloads_dir = downloads_override;
  } else if (paths.portable) {
    paths.downloads_dir = JoinPath(paths.executable_dir, "downloads");
  } else {
    paths.downloads_dir = JoinPath(HomeDir(), "Downloads");
  }

  EnsureDirectory(paths.profile_dir);
  EnsureDirectory(paths.cache_dir);
  EnsureDirectory(paths.code_cache_dir);
  EnsureDirectory(paths.sessions_dir);
  EnsureDirectory(paths.logs_dir);

  core::log::InfoF("paths", "profile=%s cache=%s portable=%s", paths.profile_dir.c_str(),
                   paths.cache_dir.c_str(), paths.portable ? "yes" : "no");
  return paths;
}

}  // namespace app
}  // namespace mybrowser
