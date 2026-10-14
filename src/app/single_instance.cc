// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/single_instance.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/app_paths.h"
#include "core/format.h"
#include "core/log.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace browserx {
namespace app {
namespace {

int ReadPid(const std::string& path) {
  std::string content;
  if (!ReadFileToString(path, &content)) return 0;
  int64_t pid = 0;
  if (!core::fmt::ParseInt(core::fmt::Trim(content), &pid)) return 0;
  return static_cast<int>(pid);
}

}  // namespace

InstanceLock::~InstanceLock() { Release(); }

bool InstanceLock::Acquire(const std::string& profile_dir, int* holder_pid) {
  if (holder_pid != nullptr) *holder_pid = 0;
  EnsureDirectory(profile_dir);
  path_ = JoinPath(profile_dir, "instance.lock");

#if defined(_WIN32)
  std::string name = "BrowserX.Instance." + profile_dir;
  for (char& c : name) {
    if (c == '\\' || c == '/') c = '.';
  }
  HANDLE handle = CreateMutexA(nullptr, TRUE, name.c_str());
  if (handle == nullptr) return true;  // be permissive rather than blocking
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(handle);
    if (holder_pid != nullptr) *holder_pid = ReadPid(path_);
    return false;
  }
  handle_ = handle;
  held_ = true;
#else
  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd_ < 0) {
    core::log::WarnF("instance", "cannot open %s (%s): continuing without a guard",
                     path_.c_str(), std::strerror(errno));
    return true;
  }
  if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
    if (holder_pid != nullptr) *holder_pid = ReadPid(path_);
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  held_ = true;
  // Record our pid so the *next* launch can tell the user who holds the lock.
  char buffer[32];
  const int length = std::snprintf(buffer, sizeof(buffer), "%d\n", static_cast<int>(::getpid()));
  if (::ftruncate(fd_, 0) == 0) {
    ssize_t ignored = ::write(fd_, buffer, static_cast<size_t>(length));
    (void)ignored;
  }
#endif
  return true;
}

void InstanceLock::Release() {
  if (!held_) return;
#if defined(_WIN32)
  if (handle_ != nullptr) {
    ReleaseMutex(static_cast<HANDLE>(handle_));
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
#else
  if (fd_ >= 0) {
    ::flock(fd_, LOCK_UN);
    ::close(fd_);
    fd_ = -1;
  }
#endif
  held_ = false;
}

}  // namespace app
}  // namespace browserx
