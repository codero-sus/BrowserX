#include "app/metrics_sampler.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/app_paths.h"
#include "core/format.h"
#include "core/log.h"
#include "core/memory_policy.h"

#if !defined(_WIN32)
#include <dirent.h>
#include <unistd.h>
#endif

namespace browserx {
namespace app {
namespace {

constexpr uint64_t kPageBytes = 4096;  // only used when sysconf is unavailable
constexpr uint64_t kBytesPerMb = 1024ull * 1024ull;

uint64_t PageBytes() {
#if defined(_WIN32)
  return kPageBytes;
#else
  const long size = sysconf(_SC_PAGESIZE);
  return size > 0 ? static_cast<uint64_t>(size) : kPageBytes;
#endif
}

double TicksPerSecond() {
#if defined(_WIN32)
  return 100.0;
#else
  const long hz = sysconf(_SC_CLK_TCK);
  return hz > 0 ? static_cast<double>(hz) : 100.0;
#endif
}

double NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

}  // namespace

bool ParseProcStatLine(const std::string& line, ProcStat* out) {
  if (out == nullptr || line.empty()) return false;
  // Format: pid (comm) state ppid ... utime stime ... rss
  // `comm` can contain anything except ')', including spaces and newlines, so we
  // anchor on the last ')' in the line.
  const size_t close = line.rfind(')');
  if (close == std::string::npos) return false;
  const size_t open = line.find('(');
  if (open == std::string::npos || open > close) return false;

  int64_t pid = 0;
  if (!core::fmt::ParseInt(core::fmt::Trim(line.substr(0, open)), &pid)) return false;

  std::vector<std::string> fields =
      core::fmt::Split(line.substr(close + 1), ' ', /*skip_empty=*/true);
  // fields[0] = state, fields[1] = ppid, ... utime = 13, stime = 14, rss = 23
  if (fields.size() < 24) return false;

  int64_t parent = 0;
  int64_t utime = 0;
  int64_t stime = 0;
  int64_t rss = 0;
  if (!core::fmt::ParseInt(fields[1], &parent)) return false;
  if (!core::fmt::ParseInt(fields[13], &utime)) return false;
  if (!core::fmt::ParseInt(fields[14], &stime)) return false;
  if (!core::fmt::ParseInt(fields[23], &rss)) return false;

  out->pid = static_cast<int>(pid);
  out->parent_pid = static_cast<int>(parent);
  out->utime_ticks = static_cast<uint64_t>(utime < 0 ? 0 : utime);
  out->stime_ticks = static_cast<uint64_t>(stime < 0 ? 0 : stime);
  out->rss_pages = static_cast<uint64_t>(rss < 0 ? 0 : rss);
  return true;
}

bool ParseProcStatmLine(const std::string& line, uint64_t* resident_pages) {
  if (resident_pages == nullptr) return false;
  // "size resident shared text lib data dt"
  std::vector<std::string> fields = core::fmt::Split(line, ' ', true);
  if (fields.size() < 2) return false;
  int64_t resident = 0;
  if (!core::fmt::ParseInt(fields[1], &resident) || resident < 0) return false;
  *resident_pages = static_cast<uint64_t>(resident);
  return true;
}

void MetricsSampler::SampleProcessTree(uint64_t* rss_mb, int* processes, uint64_t* cpu_ticks) {
  if (rss_mb != nullptr) *rss_mb = 0;
  if (processes != nullptr) *processes = 0;
  if (cpu_ticks != nullptr) *cpu_ticks = 0;
#if defined(_WIN32)
  // Without /proc we fall back to the CEF-provided process counts only; the
  // Windows build reports 0 here and the panel says "n/a".
  return;
#else
  const int self = static_cast<int>(getpid());

  uint64_t total_pages = 0;
  int count = 0;
  uint64_t ticks = 0;

  // This process first.
  {
    std::string content;
    if (ReadFileToString("/proc/self/statm", &content)) {
      uint64_t pages = 0;
      if (ParseProcStatmLine(content, &pages)) total_pages += pages;
    }
    content.clear();
    if (ReadFileToString("/proc/self/stat", &content)) {
      ProcStat stat;
      if (ParseProcStatLine(core::fmt::Trim(content), &stat)) {
        ticks += stat.utime_ticks + stat.stime_ticks;
      }
    }
  }
  count = 1;

  // Then every direct or transitive descendant. CEF's helper processes are all
  // spawned by the browser process which is `self`, but we walk the whole tree
  // (bounded by kMaxWalk) so a future zygote layer cannot hide memory from us.
  DIR* dir = opendir("/proc");
  if (dir == nullptr) return;
  struct Entry {
    int pid;
    int parent;
    uint64_t pages;
    uint64_t ticks;
  };
  std::vector<Entry> entries;
  entries.reserve(64);
  while (dirent* item = readdir(dir)) {
    if (item->d_name[0] < '0' || item->d_name[0] > '9') continue;
    const int pid = std::atoi(item->d_name);
    if (pid <= 0 || pid == self) continue;
    std::string content;
    const std::string base = std::string("/proc/") + item->d_name;
    if (!ReadFileToString(base + "/stat", &content)) continue;
    ProcStat stat;
    if (!ParseProcStatLine(core::fmt::Trim(content), &stat)) continue;
    Entry entry;
    entry.pid = stat.pid;
    entry.parent = stat.parent_pid;
    entry.pages = stat.rss_pages;
    entry.ticks = stat.utime_ticks + stat.stime_ticks;
    entries.push_back(entry);
    if (entries.size() > 512) break;  // bounded work on pathological systems
  }
  closedir(dir);

  // Mark descendants iteratively (cheap: at most a few hundred processes).
  std::vector<int> frontier{self};
  std::vector<bool> included(entries.size(), false);
  for (size_t pass = 0; pass < 8 && !frontier.empty(); ++pass) {
    std::vector<int> next;
    for (const int parent : frontier) {
      for (size_t i = 0; i < entries.size(); ++i) {
        if (included[i] || entries[i].parent != parent) continue;
        included[i] = true;
        next.push_back(entries[i].pid);
        total_pages += entries[i].pages;
        ticks += entries[i].ticks;
        ++count;
      }
    }
    frontier = next;
  }

  if (rss_mb != nullptr) *rss_mb = (total_pages * PageBytes()) / kBytesPerMb;
  if (processes != nullptr) *processes = count;
  if (cpu_ticks != nullptr) *cpu_ticks = ticks;
#endif
}

void MetricsSampler::Tick(const core::MemorySnapshot& snapshot, const std::string& mode_id) {
  const double started = NowMs();

  TelemetrySample sample;
  sample.tick = ++tick_count_;
  sample.mode_id = mode_id;
  sample.startup_ms = startup_ms_;

  // The static profile is detected once at start-up; only the live numbers are
  // refreshed here so a telemetry tick stays in the microsecond range.
  core::RefreshLiveMetrics(&profile_);
  sample.system_total_mb = profile_.total_ram_mb;
  sample.system_available_mb = profile_.available_ram_mb;

  uint64_t tree_rss_mb = 0;
  int process_count = 0;
  uint64_t cpu_ticks = 0;
  SampleProcessTree(&tree_rss_mb, &process_count, &cpu_ticks);
  sample.process_count = process_count;

  // CEF knows the renderer count better than a /proc walk does.
  sample.renderer_count = renderer_count_hint_ > 0 ? renderer_count_hint_ : process_count - 1;
  if (sample.renderer_count < 0) sample.renderer_count = 0;

  sample.total_rss_mb = tree_rss_mb;
  uint64_t browser_pages = 0;
  std::string statm;
  if (ReadFileToString("/proc/self/statm", &statm) && ParseProcStatmLine(statm, &browser_pages)) {
    sample.browser_rss_mb = (browser_pages * PageBytes()) / kBytesPerMb;
  }
  sample.children_rss_mb = sample.total_rss_mb > sample.browser_rss_mb
                               ? sample.total_rss_mb - sample.browser_rss_mb
                               : 0;

  if (sample.total_rss_mb > peak_total_rss_mb_) peak_total_rss_mb_ = sample.total_rss_mb;
  sample.peak_total_rss_mb = peak_total_rss_mb_;

  // CPU: ticks consumed since the previous sample, as a percentage of one core.
  if (previous_tree_ticks_ > 0 && cpu_ticks >= previous_tree_ticks_) {
    const uint64_t delta = cpu_ticks - previous_tree_ticks_;
    const double tick_hz = TicksPerSecond();
    sample.process_cpu_percent = static_cast<double>(delta) / tick_hz * 100.0;
  }
  previous_tree_ticks_ = cpu_ticks;

  sample.pressure = core::MemoryPolicy::ComputePressure(snapshot);
  sample.pressure_level =
      core::MemoryPolicy::LevelFor(sample.pressure, 0.75, 0.88);
  sample.tab_count = snapshot.tab_count;
  sample.hidden_tabs = snapshot.hidden_tabs;
  sample.sleeping_tabs = snapshot.sleeping_tabs;
  sample.discarded_tabs = snapshot.sleeping_tabs;
  sample.bytes_saved_by_shield = shield_bytes_saved_;
  sample.blocked_requests = shield_blocked_;

  latest_ = sample;
  history_.push_back(sample);
  while (history_.size() > kHistoryDepth) history_.pop_front();

  cpu_history_.push_back(sample.process_cpu_percent);
  while (cpu_history_.size() > kHistoryDepth) cpu_history_.pop_front();

  const double elapsed = NowMs() - started;
  if (elapsed > max_tick_ms_) max_tick_ms_ = elapsed;
}

std::vector<double> MetricsSampler::RssSeries() const {
  std::vector<double> series;
  series.reserve(history_.size());
  for (const TelemetrySample& sample : history_) {
    series.push_back(static_cast<double>(sample.total_rss_mb));
  }
  return series;
}

std::string MetricsSampler::RssSparkline() const {
  const std::vector<double> series = RssSeries();
  if (series.size() < 2) return std::string();
  return core::fmt::Sparkline(series, 24);
}

std::string MetricsSampler::RenderOneLineStatus() const {
  const TelemetrySample& sample = latest_;
  std::string line = "tick #" + std::to_string(sample.tick);
  line += "  mode=" + sample.mode_id;
  line += "  ram=" + core::fmt::FormatMemoryMiB(sample.total_rss_mb) + " (browser " +
          core::fmt::FormatMemoryMiB(sample.browser_rss_mb) + " + children " +
          core::fmt::FormatMemoryMiB(sample.children_rss_mb) + ")";
  line += "  sys=" + core::fmt::FormatMemoryMiB(sample.system_available_mb) + " free";
  line += "  cpu=" + core::fmt::FormatDouble(sample.process_cpu_percent, 0) + "%";
  line += "  procs=" + std::to_string(sample.process_count);
  line += "  tabs=" + std::to_string(sample.tab_count) + "/" + std::to_string(sample.hidden_tabs) +
          " hidden";
  line += "  pressure=" + core::fmt::FormatPercent(sample.pressure * 100.0, 0) + " (" +
          core::PressureLevelLabel(sample.pressure_level) + ")";
  return line;
}

std::string MetricsSampler::RenderPanelText() const {
  const TelemetrySample& sample = latest_;
  std::string text;
  text += "BROWSERX TELEMETRY\n";
  text += "=====================================================\n";
  text += "Mode                : " + sample.mode_id + "\n";
  text += "Uptime ticks        : " + std::to_string(sample.tick) + "\n";
  text += "Startup             : " + std::to_string(sample.startup_ms) + " ms\n";
  text += "\n";
  text += "MEMORY\n";
  text += "  Browser + helpers : " + core::fmt::FormatMemoryMiB(sample.total_rss_mb) + "\n";
  text += "  Browser process   : " + core::fmt::FormatMemoryMiB(sample.browser_rss_mb) + "\n";
  text += "  Renderers + GPU   : " + core::fmt::FormatMemoryMiB(sample.children_rss_mb) + "\n";
  text += "  Peak this session : " + core::fmt::FormatMemoryMiB(sample.peak_total_rss_mb) + "\n";
  text += "  System available  : " + core::fmt::FormatMemoryMiB(sample.system_available_mb) + " of " +
          core::fmt::FormatMemoryMiB(sample.system_total_mb) + "\n";
  text += "  Pressure          : " + core::fmt::FormatPercent(sample.pressure * 100.0, 0) + " (" +
          core::PressureLevelLabel(sample.pressure_level) + ")\n";
  text += "  Trend             : " + RssSparkline() + "  (recent samples, MiB)\n";
  text += "\n";
  text += "TABS\n";
  text += "  Open              : " + std::to_string(sample.tab_count) + "\n";
  text += "  Hidden            : " + std::to_string(sample.hidden_tabs) + "\n";
  text += "  Asleep/discarded  : " + std::to_string(sample.sleeping_tabs) + "\n";
  text += "\n";
  text += "COST\n";
  text += "  Processes         : " + std::to_string(sample.process_count) + "\n";
  text += "  CPU (tree)        : " + core::fmt::FormatDouble(sample.process_cpu_percent, 1) + "%\n";
  text += "  Telemetry overhead: worst tick " + core::fmt::FormatMs(max_tick_ms_) + "\n";
  text += "\n";
  text += "SHIELD\n";
  text += "  Requests blocked  : " + core::fmt::FormatNumber(sample.blocked_requests) + "\n";
  text += "  Data saved        : " + core::fmt::FormatBytes(sample.bytes_saved_by_shield) + "\n";
  return text;
}

}  // namespace app
}  // namespace browserx
