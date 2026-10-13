// BrowserX — single-instance guard.
//
// Chromium refuses to share a profile between two browser processes; without a
// guard the second launch exits with a cryptic message. We take an advisory lock
// on <profile>/instance.lock first and can then say something useful — and the
// lock is released automatically by the OS if we crash.

#ifndef BROWSERX_APP_SINGLE_INSTANCE_H_
#define BROWSERX_APP_SINGLE_INSTANCE_H_

#include <string>

namespace browserx {
namespace app {

class InstanceLock {
 public:
  InstanceLock() = default;
  ~InstanceLock();
  InstanceLock(const InstanceLock&) = delete;
  InstanceLock& operator=(const InstanceLock&) = delete;

  /// Tries to become the primary instance for `profile_dir`.
  /// On failure `holder_pid` receives the pid recorded in the lock file (0 when
  /// unknown) so the caller can tell the user what is already running.
  bool Acquire(const std::string& profile_dir, int* holder_pid);

  void Release();
  bool held() const { return held_; }

 private:
  bool held_ = false;
  std::string path_;
#if defined(_WIN32)
  void* handle_ = nullptr;
#else
  int fd_ = -1;
#endif
};

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_SINGLE_INSTANCE_H_
