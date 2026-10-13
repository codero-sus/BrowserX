// MyBrowser — live telemetry: what this browser is costing you, right now.
//
// Beyond the process counts CEF gives us, the sampler walks /proc to add up the
// resident memory of the whole process tree (browser + GPU + every renderer +
// utility process). That number is the one users care about, and it is the one
// the LOW RAM mode is judged by, so it drives the telemetry panel, the HUD and
// the headless `--benchmark` mode.
//
// The parsing is kept pure (see ParseProcStatLine / ParseProcStatmLine) so it can
// be unit-tested without touching a real /proc.

#ifndef MYBROWSER_APP_METRICS_SAMPLER_H_
#define MYBROWSER_APP_METRICS_SAMPLER_H_

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/memory_policy.h"
#include "core/system_profile.h"

namespace mybrowser {
namespace app {

/// One /proc/<pid>/stat record. `comm` may contain spaces/parentheses, which is
/// why real parsing starts after the last ')'.
struct ProcStat {
  int pid = 0;
  int parent_pid = 0;
  uint64_t utime_ticks = 0;
  uint64_t stime_ticks = 0;
  uint64_t rss_pages = 0;
};

/// Parses a line of /proc/<pid>/stat. Returns false when malformed.
bool ParseProcStatLine(const std::string& line, ProcStat* out);
/// Parses /proc/<pid>/statm (fields are pages). Returns false when malformed.
bool ParseProcStatmLine(const std::string& line, uint64_t* resident_pages);

struct TelemetrySample {
  int tick = 0;

  // Memory (MiB).
  uint64_t system_total_mb = 0;
  uint64_t system_available_mb = 0;
  uint64_t browser_rss_mb = 0;   // this process
  uint64_t children_rss_mb = 0;  // GPU + renderers + utilities
  uint64_t total_rss_mb = 0;     // browser + children
  double pressure = 0.0;         // 0..1, as evaluated by the memory policy

  // CPU.
  double process_cpu_percent = 0.0;  // whole process tree, 100% = one core

  // Processes.
  int process_count = 0;
  int renderer_count = 0;

  // Tabs (from the session manager).
  int tab_count = 0;
  int hidden_tabs = 0;
  int sleeping_tabs = 0;
  int discarded_tabs = 0;

  // Accumulated totals.
  uint64_t peak_total_rss_mb = 0;
  uint64_t bytes_saved_by_shield = 0;
  int blocked_requests = 0;
  int startup_ms = 0;

  core::PressureLevel pressure_level = core::PressureLevel::kRelaxed;
  std::string mode_id;
  std::string age_text;
};

class MetricsSampler {
 public:
  /// Reads the system state, walks the process tree and stores a new sample.
  /// `snapshot` carries the tab counts the browser layer knows about.
  void Tick(const core::MemorySnapshot& snapshot, const std::string& mode_id);

  const TelemetrySample& latest() const { return latest_; }
  const std::deque<TelemetrySample>& history() const { return history_; }
  int tick_count() const { return tick_count_; }

  /// MiB of the last N samples, oldest first (for the sparkline).
  std::vector<double> RssSeries() const;
  std::string RssSparkline() const;
  std::string RenderPanelText() const;
  std::string RenderOneLineStatus() const;

  /// Milliseconds spent in the slowest tick so far (sanity check for the
  /// "telemetry must not cost anything" claim).
  double max_tick_ms() const { return max_tick_ms_; }

  void set_startup_ms(int ms) { startup_ms_ = ms; }
  /// The static system profile, detected once by the app layer.
  void SetSystemProfile(const core::SystemProfile& profile) { profile_ = profile; }
  /// CEF's live process info (renderer/GPU/utility counts) beats a /proc guess.
  void set_renderer_count_hint(int count) { renderer_count_hint_ = count; }
  void set_shield_totals(uint64_t bytes_saved, int blocked) {
    shield_bytes_saved_ = bytes_saved;
    shield_blocked_ = blocked;
  }

  static constexpr size_t kHistoryDepth = 60;

 private:
  /// Walks /proc and sums the resident memory of this process and its children.
  void SampleProcessTree(uint64_t* rss_mb, int* processes, uint64_t* cpu_ticks);

  TelemetrySample latest_;
  std::deque<TelemetrySample> history_;
  std::deque<double> cpu_history_;
  int tick_count_ = 0;
  int startup_ms_ = 0;
  uint64_t shield_bytes_saved_ = 0;
  int shield_blocked_ = 0;
  uint64_t peak_total_rss_mb_ = 0;
  uint64_t previous_tree_ticks_ = 0;
  core::SystemProfile profile_;
  int renderer_count_hint_ = 0;
  double max_tick_ms_ = 0.0;
};

}  // namespace app
}  // namespace mybrowser

#endif  // MYBROWSER_APP_METRICS_SAMPLER_H_
