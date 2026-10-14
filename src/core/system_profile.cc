// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/system_profile.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <vector>

#include "core/format.h"
#include "core/log.h"

#if defined(__linux__)
#include <sys/statvfs.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(_WIN32)
// Windows detection lives in src/platforms/win/; the core stays portable.
#endif

namespace browserx {
namespace core {
namespace {

// ---------------------------------------------------------------------------
// Small file helpers (best effort: a missing file is never fatal).
// ---------------------------------------------------------------------------
bool ReadFile(const char* path, std::string* out) {
  std::FILE* file = std::fopen(path, "rb");
  if (file == nullptr) return false;
  char buffer[8192];
  out->clear();
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    out->append(buffer, read);
    if (out->size() > (1u << 20)) break;  // 1 MB is plenty for everything we read
  }
  std::fclose(file);
  return true;
}

std::string ReadFileOr(const char* path, const char* fallback = "") {
  std::string content;
  if (ReadFile(path, &content)) return fmt::Trim(content);
  return fallback;
}

std::string GetenvString(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string() : std::string(value);
}

bool FileExists(const char* path) {
  std::FILE* file = std::fopen(path, "rb");
  if (file == nullptr) return false;
  std::fclose(file);
  return true;
}

#if defined(__linux__)
uint64_t ReadSysfsMb(const char* path) {
  const std::string text = ReadFileOr(path);
  int64_t value = 0;
  if (fmt::ParseInt(text, &value) && value > 0) return static_cast<uint64_t>(value);
  return 0;
}
#endif

const char* BatteryStateName(BatteryState state) {
  switch (state) {
    case BatteryState::kDischarging: return "discharging";
    case BatteryState::kCharging: return "charging";
    case BatteryState::kFull: return "full";
    case BatteryState::kNotPresent: return "not-present";
    default: return "unknown";
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Parsers (pure, unit-tested)
// ---------------------------------------------------------------------------
void ParseMemInfoFile(std::string_view content, uint64_t* total_mb, uint64_t* available_mb,
                      uint64_t* swap_total_mb) {
  if (total_mb != nullptr) *total_mb = 0;
  if (available_mb != nullptr) *available_mb = 0;
  if (swap_total_mb != nullptr) *swap_total_mb = 0;

  uint64_t mem_available_kb = 0;
  uint64_t mem_free_kb = 0;
  bool saw_available = false;

  std::istringstream stream{std::string(content)};
  std::string line;
  while (std::getline(stream, line)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = fmt::Trim(line.substr(0, colon));
    const std::string rest = fmt::Trim(line.substr(colon + 1));
    int64_t kb = 0;
    {
      // Values look like "16331164 kB"; take the leading number.
      const size_t space = rest.find(' ');
      const std::string number = (space == std::string::npos) ? rest : rest.substr(0, space);
      if (!fmt::ParseInt(number, &kb)) continue;
    }
    if (key == "MemTotal" && total_mb != nullptr) {
      *total_mb = static_cast<uint64_t>(kb) / 1024;
    } else if (key == "MemAvailable") {
      mem_available_kb = static_cast<uint64_t>(kb);
      saw_available = true;
    } else if (key == "MemFree") {
      mem_free_kb = static_cast<uint64_t>(kb);
    } else if (key == "SwapTotal" && swap_total_mb != nullptr) {
      *swap_total_mb = static_cast<uint64_t>(kb) / 1024;
    }
  }
  if (available_mb != nullptr) {
    // MemAvailable is the honest number; fall back to MemFree on old kernels.
    *available_mb = (saw_available ? mem_available_kb : mem_free_kb) / 1024;
  }
}

int CountLogicalCpus(std::string_view cpuinfo_content) {
  int count = 0;
  std::istringstream stream{std::string(cpuinfo_content)};
  std::string line;
  while (std::getline(stream, line)) {
    if (fmt::StartsWith(line, "processor")) ++count;
  }
  return count > 0 ? count : 1;
}

int CountPhysicalCores(std::string_view cpuinfo_content) {
  // Group by (physical id, core id) pairs, which is what /proc/cpuinfo exposes.
  struct Pair {
    std::string physical;
    std::string core;
  };
  std::vector<Pair> seen;
  std::istringstream stream{std::string(cpuinfo_content)};
  std::string line;
  std::string current_physical;
  std::string current_core;
  auto flush = [&]() {
    if (current_physical.empty() && current_core.empty()) return;
    for (const Pair& pair : seen) {
      if (pair.physical == current_physical && pair.core == current_core) return;
    }
    seen.push_back(Pair{current_physical, current_core});
  };
  while (std::getline(stream, line)) {
    if (line.empty()) {
      flush();
      current_physical.clear();
      current_core.clear();
      continue;
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = fmt::Trim(line.substr(0, colon));
    const std::string value = fmt::Trim(line.substr(colon + 1));
    if (key == "physical id") {
      current_physical = value;
    } else if (key == "core id") {
      current_core = value;
    }
  }
  flush();
  const int cores = static_cast<int>(seen.size());
  return cores > 0 ? cores : CountLogicalCpus(cpuinfo_content);
}

std::string ExtractModelName(std::string_view cpuinfo_content) {
  std::istringstream stream{std::string(cpuinfo_content)};
  std::string line;
  while (std::getline(stream, line)) {
    if (fmt::StartsWith(line, "model name") || fmt::StartsWith(line, "Hardware") ||
        fmt::StartsWith(line, "Model")) {
      const size_t colon = line.find(':');
      if (colon != std::string::npos) return fmt::Trim(line.substr(colon + 1));
    }
  }
  return std::string();
}

BatteryState ParseBatteryStatus(std::string_view status) {
  const std::string value = fmt::ToLower(fmt::Trim(status));
  if (value == "discharging") return BatteryState::kDischarging;
  if (value == "charging") return BatteryState::kCharging;
  if (value == "full") return BatteryState::kFull;
  if (value == "not charging") return BatteryState::kFull;
  if (value == "unknown") return BatteryState::kUnknown;
  return BatteryState::kUnknown;
}

bool ParseOsRelease(std::string_view content, std::string* pretty_name) {
  std::istringstream stream{std::string(content)};
  std::string line;
  std::string name;
  std::string version;
  std::string pretty;
  while (std::getline(stream, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = fmt::Trim(line.substr(0, eq));
    std::string value = fmt::Trim(line.substr(eq + 1));
    if (value.size() >= 2 && (value.front() == '"' && value.back() == '"')) {
      value = value.substr(1, value.size() - 2);
    }
    if (key == "PRETTY_NAME") {
      pretty = value;
    } else if (key == "NAME") {
      name = value;
    } else if (key == "VERSION_ID") {
      version = value;
    }
  }
  if (pretty_name != nullptr) {
    if (!pretty.empty()) {
      *pretty_name = pretty;
    } else if (!name.empty()) {
      *pretty_name = version.empty() ? name : name + " " + version;
    } else {
      return false;
    }
  }
  return !pretty.empty() || !name.empty();
}

int ParseRefreshRateFromModetest(std::string_view content) {
  // Looks for "... 60.00 1920x1080 ..." style output from modetest/drm_info.
  int best = 0;
  std::istringstream stream{std::string(content)};
  std::string line;
  while (std::getline(stream, line)) {
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] != '.' && !std::isdigit(static_cast<unsigned char>(line[i]))) continue;
      double value = 0.0;
      if (!fmt::ParseDouble(line.substr(i, 6), &value)) continue;
      if (value > 20.0 && value < 1000.0) {
        const int rounded = static_cast<int>(value + 0.5);
        // Prefer the highest plausible refresh rate (the panel's real mode).
        if (rounded > best && rounded % 1 == 0) best = rounded;
      }
      while (i < line.size() && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) {
        ++i;
      }
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Derived recommendations
// ---------------------------------------------------------------------------
bool SystemProfile::IsLowRam() const {
  return total_ram_mb != 0 && total_ram_mb <= 4096;
}

bool SystemProfile::IsVeryLowRam() const {
  return total_ram_mb != 0 && total_ram_mb <= 2048;
}

int SystemProfile::RecommendedRendererLimit() const {
  // Roughly one renderer per GB of RAM, clamped to a sane window: this is the
  // heuristic Chromium itself uses for its "low end device" mode, applied more
  // gradually.
  if (total_ram_mb == 0) return 0;  // unknown: leave Chromium's default
  uint64_t per_gb = total_ram_mb / 1024;
  int limit = static_cast<int>(per_gb);
  if (limit < 2) limit = 2;
  if (limit > 12) limit = 12;
  // Never exceed the number of physical cores by much: more renderers than
  // cores just adds context switching and duplicated memory.
  const int core_cap = cpu_physical > 0 ? cpu_physical * 2 : 8;
  if (limit > core_cap) limit = core_cap;
  return limit;
}

int SystemProfile::RecommendedRasterThreads() const {
  int threads = cpu_physical > 0 ? cpu_physical / 2 : 2;
  if (threads < 1) threads = 1;
  if (threads > 4) threads = 4;
  return threads;
}

int SystemProfile::RecommendedCacheSizeMb() const {
  if (IsVeryLowRam()) return 48;
  if (IsLowRam()) return 96;
  if (total_ram_mb >= 16384) return 512;
  if (total_ram_mb >= 8192) return 320;
  return 192;
}

bool SystemProfile::ShouldUseDevShmWorkaround() const {
  // Chromium needs a decent /dev/shm; containers often give 64 MB.
  if (shm_total_mb == 0) return containerized;  // unknown: trust the container hint
  return shm_total_mb < 256;
}

bool SystemProfile::ShouldPreferSoftwareGl() const {
  return gpu_tier == GpuTier::kSoftware ||
         (gpu_tier == GpuTier::kUnknown && session == SessionType::kHeadless);
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------
SystemProfile DetectSystemProfile() {
  SystemProfile profile;
  std::string arch = "unknown";
#if defined(__x86_64__) || defined(_M_X64)
  arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  arch = "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
  arch = "x86";
#elif defined(__arm__) || defined(_M_ARM)
  arch = "arm";
#endif
  profile.arch = arch;

  unsigned hw_concurrency = std::thread::hardware_concurrency();
  profile.cpu_logical = hw_concurrency > 0 ? static_cast<int>(hw_concurrency) : 1;
  profile.cpu_physical = profile.cpu_logical;

#if defined(__linux__)
  profile.os = OS::kLinux;
  std::string os_release;
  if (ReadFile("/etc/os-release", &os_release)) {
    std::string pretty;
    if (ParseOsRelease(os_release, &pretty)) profile.os_name = pretty;
  }
  if (profile.os_name.empty()) profile.os_name = "Linux";

  std::string cpuinfo;
  if (ReadFile("/proc/cpuinfo", &cpuinfo)) {
    profile.cpu_logical = CountLogicalCpus(cpuinfo);
    profile.cpu_physical = CountPhysicalCores(cpuinfo);
    profile.cpu_model = ExtractModelName(cpuinfo);
  }
  if (profile.cpu_physical <= 0) profile.cpu_physical = profile.cpu_logical;

  std::string meminfo;
  if (ReadFile("/proc/meminfo", &meminfo)) {
    ParseMemInfoFile(meminfo, &profile.total_ram_mb, &profile.available_ram_mb,
                     &profile.total_swap_mb);
  }

  // /dev/shm size: use statvfs-free approach by reading /proc/mounts + statfs.
  {
#if !defined(_WIN32)
    struct statvfs shm_info {};
    if (statvfs("/dev/shm", &shm_info) == 0 && shm_info.f_blocks > 0) {
      profile.shm_total_mb =
          static_cast<uint64_t>(shm_info.f_blocks) * (shm_info.f_frsize / 1024) / 1024;
    }
#endif
  }

  // Container detection: Docker writes /.dockerenv; cgroup v1/v2 reveal others.
  profile.containerized = FileExists("/.dockerenv") || FileExists("/run/.containerenv");
  if (!profile.containerized) {
    const std::string cgroup = ReadFileOr("/proc/1/cgroup");
    if (cgroup.find("docker") != std::string::npos ||
        cgroup.find("kubepods") != std::string::npos ||
        cgroup.find("containerd") != std::string::npos ||
        cgroup.find("podman") != std::string::npos) {
      profile.containerized = true;
    }
  }
  const std::string product = ReadFileOr("/sys/class/dmi/id/product_name");
  if (product.find("VirtualBox") != std::string::npos ||
      product.find("VMware") != std::string::npos ||
      product.find("QEMU") != std::string::npos ||
      product.find("KVM") != std::string::npos ||
      product.find("Microsoft Corporation") != std::string::npos) {
    profile.running_in_vm = true;
  }

  // Battery: /sys/class/power_supply/BAT*/{status,capacity}.
  for (int index = 0; index < 4 && profile.battery == BatteryState::kUnknown; ++index) {
    char status_path[128];
    char capacity_path[128];
    std::snprintf(status_path, sizeof(status_path), "/sys/class/power_supply/BAT%d/status", index);
    std::snprintf(capacity_path, sizeof(capacity_path), "/sys/class/power_supply/BAT%d/capacity", index);
    const std::string status = ReadFileOr(status_path);
    if (status.empty()) continue;
    profile.battery = ParseBatteryStatus(status);
    const std::string capacity = ReadFileOr(capacity_path);
    int64_t percent = -1;
    if (fmt::ParseInt(capacity, &percent)) profile.battery_percent = static_cast<double>(percent);
  }
  if (profile.battery == BatteryState::kUnknown) profile.battery = BatteryState::kNotPresent;

  // Session type.
  if (!GetenvString("WAYLAND_DISPLAY").empty()) {
    profile.session = SessionType::kWayland;
  } else if (!GetenvString("DISPLAY").empty()) {
    profile.session = SessionType::kX11;
  } else {
    profile.session = SessionType::kHeadless;
  }

  // GPU hints from sysfs (vendor ids are stable enough for tiering).
  {
    const std::string vendor = ReadFileOr("/sys/class/drm/card0/device/vendor");
    const std::string device = ReadFileOr("/sys/class/drm/card0/device/device");
    if (!vendor.empty()) {
      if (vendor.find("0x10de") != std::string::npos) {
        profile.gpu_vendor = "nvidia";
        profile.gpu_tier = GpuTier::kDiscrete;
      } else if (vendor.find("0x1002") != std::string::npos ||
                 vendor.find("0x1022") != std::string::npos) {
        profile.gpu_vendor = "amd";
        profile.gpu_tier = GpuTier::kIntegrated;
      } else if (vendor.find("0x8086") != std::string::npos) {
        profile.gpu_vendor = "intel";
        profile.gpu_tier = GpuTier::kIntegrated;
      } else if (vendor.find("0x1af4") != std::string::npos) {
        // virtio-gpu (VMs): software-ish path.
        profile.gpu_vendor = "virtio";
        profile.gpu_tier = GpuTier::kSoftware;
      }
    }
    if (!device.empty()) profile.gpu_renderer = "pci:" + device;
    // A second DRM card hints at a dual-GPU (hybrid) laptop.
    if (FileExists("/sys/class/drm/card1/device/vendor")) profile.dual_gpu = true;
    if (profile.gpu_vendor == "nvidia" && FileExists("/dev/nvidia0")) {
      profile.gpu_tier = GpuTier::kDiscrete;
    }
  }
  // VA-API presence hints (hardware video decode).
  profile.has_accelerated_video = FileExists("/dev/dri/renderD128") &&
                                  (profile.gpu_vendor == "intel" || profile.gpu_vendor == "amd" ||
                                   FileExists("/dev/nvidia0"));

  if (profile.total_ram_mb > 0) {
    profile.low_ram_hint = profile.total_ram_mb <= 4096;
  }
  profile.low_shm = profile.shm_total_mb > 0 && profile.shm_total_mb < 256;
#elif defined(__APPLE__)
  profile.os = OS::kMac;
  profile.os_name = "macOS";
  size_t size = sizeof(uint64_t);
  uint64_t mem_bytes = 0;
  if (sysctlbyname("hw.memsize", &mem_bytes, &size, nullptr, 0) == 0) {
    profile.total_ram_mb = mem_bytes / (1024 * 1024);
  }
  size_t cores_size = sizeof(int);
  int cores = 0;
  if (sysctlbyname("hw.physicalcpu", &cores, &cores_size, nullptr, 0) == 0 && cores > 0) {
    profile.cpu_physical = cores;
  }
  profile.low_ram_hint = profile.total_ram_mb > 0 && profile.total_ram_mb <= 8192;
  profile.gpu_vendor = "apple";
  profile.gpu_tier = GpuTier::kIntegrated;
#elif defined(_WIN32)
  profile.os = OS::kWindows;
  profile.os_name = "Windows";
#else
  profile.os = OS::kUnknown;
  profile.os_name = "Unknown";
#endif

  if (profile.cpu_physical <= 0) profile.cpu_physical = profile.cpu_logical;
  if (profile.os_name.empty()) profile.os_name = "Unknown";
  if (profile.shm_total_mb > 0 && profile.shm_total_mb < 256) profile.low_shm = true;

  log::InfoF("profile", "%s %s | %d cores (%d physical) | %llu MB RAM | shm %llu MB | battery %s",
             profile.os_name.c_str(), profile.arch.c_str(), profile.cpu_logical,
             profile.cpu_physical, static_cast<unsigned long long>(profile.total_ram_mb),
             static_cast<unsigned long long>(profile.shm_total_mb),
             BatteryStateName(profile.battery));
  return profile;
}

void RefreshLiveMetrics(SystemProfile* profile) {
  if (profile == nullptr) return;
#if defined(__linux__)
  std::string meminfo;
  if (ReadFile("/proc/meminfo", &meminfo)) {
    uint64_t total = profile->total_ram_mb;
    uint64_t available = profile->available_ram_mb;
    ParseMemInfoFile(meminfo, &total, &available, nullptr);
    if (total != 0) profile->total_ram_mb = total;
    profile->available_ram_mb = available;
  }
  for (int index = 0; index < 4; ++index) {
    char status_path[128];
    std::snprintf(status_path, sizeof(status_path), "/sys/class/power_supply/BAT%d/status", index);
    const std::string status = ReadFileOr(status_path);
    if (status.empty()) return;
    profile->battery = ParseBatteryStatus(status);
    char capacity_path[128];
    std::snprintf(capacity_path, sizeof(capacity_path), "/sys/class/power_supply/BAT%d/capacity", index);
    int64_t percent = -1;
    if (fmt::ParseInt(ReadFileOr(capacity_path), &percent)) {
      profile->battery_percent = static_cast<double>(percent);
    }
    return;
  }
#else
  (void)profile;
#endif
}

std::string DescribeProfile(const SystemProfile& profile) {
  std::ostringstream out;
  out << "System profile\n";
  out << "  OS            : " << profile.os_name << " (" << profile.arch << ")\n";
  out << "  CPU           : " << (profile.cpu_model.empty() ? "unknown" : profile.cpu_model) << "\n";
  out << "  Cores         : " << profile.cpu_logical << " logical / " << profile.cpu_physical
      << " physical\n";
  out << "  Memory        : " << profile.total_ram_mb << " MB total, " << profile.available_ram_mb
      << " MB available, " << profile.total_swap_mb << " MB swap\n";
  out << "  /dev/shm      : " << profile.shm_total_mb << " MB\n";
  out << "  Container     : " << (profile.containerized ? "yes" : "no")
      << " | VM: " << (profile.running_in_vm ? "yes" : "no") << "\n";
  out << "  Battery       : " << BatteryStateName(profile.battery);
  if (profile.battery_percent >= 0) out << " (" << static_cast<int>(profile.battery_percent) << "%)";
  out << "\n";
  out << "  Session       : "
      << (profile.session == SessionType::kWayland ? "Wayland"
                                                   : (profile.session == SessionType::kX11 ? "X11" : "headless"))
      << ", " << profile.display_hz << " Hz, " << profile.display_count << " display(s)\n";
  out << "  GPU           : " << (profile.gpu_vendor.empty() ? "unknown" : profile.gpu_vendor);
  switch (profile.gpu_tier) {
    case GpuTier::kDiscrete: out << " (discrete)"; break;
    case GpuTier::kIntegrated: out << " (integrated)"; break;
    case GpuTier::kSoftware: out << " (software)"; break;
    default: out << " (unknown tier)"; break;
  }
  out << ", accelerated video: " << (profile.has_accelerated_video ? "yes" : "no")
      << ", dual GPU: " << (profile.dual_gpu ? "yes" : "no") << "\n";
  out << "  Recommendations: renderer limit " << profile.RecommendedRendererLimit()
      << ", raster threads " << profile.RecommendedRasterThreads() << ", cache "
      << profile.RecommendedCacheSizeMb() << " MB"
      << (profile.ShouldUseDevShmWorkaround() ? ", disable-dev-shm-usage" : "") << "\n";
  return out.str();
}

}  // namespace core
}  // namespace browserx
