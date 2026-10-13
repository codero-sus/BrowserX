// BrowserX — system detection.
//
// The profile is what makes the modes *adaptive* rather than hard-coded: a
// 4 GB laptop and a 64 GB workstation do not deserve the same renderer limit,
// cache size, or raster thread count.
//
// All parsing helpers are pure functions over strings so they can be unit
// tested on any platform (see tests/unit/system_profile_test.cc).

#ifndef BROWSERX_CORE_SYSTEM_PROFILE_H_
#define BROWSERX_CORE_SYSTEM_PROFILE_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace browserx {
namespace core {

enum class OS { kLinux, kMac, kWindows, kUnknown };

enum class BatteryState { kUnknown, kDischarging, kCharging, kFull, kNotPresent };

enum class SessionType { kUnknown, kX11, kWayland, kHeadless };

enum class GpuTier { kUnknown, kSoftware, kIntegrated, kDiscrete };

struct SystemProfile {
  OS os = OS::kUnknown;
  std::string os_name;      // e.g. "Debian GNU/Linux 12"
  std::string arch;         // e.g. "x86_64"
  std::string cpu_model;    // e.g. "AMD Ryzen 7 5800H"
  int cpu_logical = 1;
  int cpu_physical = 1;
  uint64_t total_ram_mb = 0;
  uint64_t available_ram_mb = 0;
  uint64_t total_swap_mb = 0;
  uint64_t shm_total_mb = 0;   // /dev/shm size (0 = unknown/absent)
  bool containerized = false;  // docker/podman/kubernetes detected
  bool running_in_vm = false;

  BatteryState battery = BatteryState::kUnknown;
  double battery_percent = -1.0;   // -1 = unknown
  int battery_seconds_left = -1;

  SessionType session = SessionType::kUnknown;
  int display_hz = 60;             // best-known refresh rate
  int display_count = 1;
  int display_scale_percent = 100;

  GpuTier gpu_tier = GpuTier::kUnknown;
  std::string gpu_vendor;          // e.g. "nvidia", "intel", "amd", "mesa"
  std::string gpu_renderer;
  bool has_accelerated_video = false;  // VA-API/V4L2 available hints
  bool dual_gpu = false;

  bool low_shm = false;      // /dev/shm < 256 MB (crashes without a workaround)
  bool low_ram_hint = false; // <= 4 GB total RAM

  // ---- derived recommendations (all pure) --------------------------------
  bool IsLowRam() const;
  bool IsVeryLowRam() const;   // <= 2 GB
  int RecommendedRendererLimit() const;
  int RecommendedRasterThreads() const;
  int RecommendedCacheSizeMb() const;
  bool ShouldUseDevShmWorkaround() const;
  bool ShouldPreferSoftwareGl() const;
};

/// String parsers (exposed for tests and reused by the platform code).
void ParseMemInfoFile(std::string_view content, uint64_t* total_mb, uint64_t* available_mb,
                      uint64_t* swap_total_mb);
int CountLogicalCpus(std::string_view cpuinfo_content);
int CountPhysicalCores(std::string_view cpuinfo_content);
std::string ExtractModelName(std::string_view cpuinfo_content);
BatteryState ParseBatteryStatus(std::string_view status);
bool ParseOsRelease(std::string_view content, std::string* pretty_name);
int ParseRefreshRateFromModetest(std::string_view content);

/// Fully-detected profile for the current machine (reads /proc, /sys, /etc).
SystemProfile DetectSystemProfile();

/// Overwrite the runtime-varying fields (RAM availability, battery) without
/// re-running the expensive detection.
void RefreshLiveMetrics(SystemProfile* profile);

/// Multi-line, human-readable dump (used by `make doctor` and the About panel).
std::string DescribeProfile(const SystemProfile& profile);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_SYSTEM_PROFILE_H_
