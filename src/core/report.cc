// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/report.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/chromium_catalog.h"
#include "core/format.h"

namespace browserx {
namespace core {
namespace report {
namespace {

using fmt::FormatDuration;
using fmt::FormatPercent;
using fmt::Join;
using fmt::PadRight;

std::string JsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

bool Contains(const std::vector<std::string>& list, const std::string& value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

/// Number of switches both plans share (after sorting, so it is well defined).
size_t SharedFlags(const ModePlan& a, const ModePlan& b) {
  const std::vector<std::string> left = a.boot.ToSortedFlagList();
  const std::vector<std::string> right = b.boot.ToSortedFlagList();
  std::vector<std::string> intersection;
  std::set_intersection(left.begin(), left.end(), right.begin(), right.end(),
                        std::back_inserter(intersection));
  return intersection.size();
}

}  // namespace

std::string RenderPlanText(const ModePlan& plan, const SystemProfile& profile, bool quiet) {
  const ModeInfo& info = GetModeInfo(plan.mode);
  if (quiet) return plan.boot.ToCommandLine() + "\n";

  std::string out;
  out += "\n  " + std::string(info.icon) + " " + info.label + "  (" + info.id + ")\n";
  out += "  " + std::string(info.tagline) + "\n";
  out += "  -------------------------------------------------------------------------\n";
  out += "  boot switches (applied before Chromium starts):\n";
  out += plan.boot.ToReport();

  out += "\n  runtime policy (applied live, no restart):\n";
  out += "    background throttling : " + std::string(plan.runtime.background_throttling ? "ON" : "OFF") + "\n";
  const std::string fps_text = plan.runtime.target_fps <= 0.0
                                   ? std::string("uncapped")
                                   : std::to_string(static_cast<int>(plan.runtime.target_fps)) + " FPS";
  out += "    frame rate target     : " + fps_text + "\n";
  out += "    tab sleep / discard   : " +
         (plan.runtime.sleep_after_seconds == 0 ? std::string("never")
                                                : FormatDuration(plan.runtime.sleep_after_seconds)) +
         " / " +
         (plan.runtime.discard_after_seconds == 0
              ? std::string("never")
              : FormatDuration(plan.runtime.discard_after_seconds)) +
         "\n";
  out += "    memory thresholds     : warn " + FormatPercent(plan.runtime.warn_pressure * 100.0, 0) +
         ", act " + FormatPercent(plan.runtime.discard_pressure * 100.0, 0) + "\n";
  out += "    cache budget          : " + std::to_string(plan.runtime.cache_size_mb) + " MB\n";
  out += "    keep display awake    : " + std::string(plan.runtime.keep_display_awake ? "yes" : "no") + "\n";
  out += "    bfcache / preconnect  : " + std::string(plan.runtime.bfcache ? "on" : "off") + " / " +
         std::string(plan.runtime.preconnect ? "on" : "off") + "\n";
  out += "    perf HUD              : " + std::string(plan.runtime.show_perf_hud ? "on" : "off") + "\n";
  out += "    telemetry interval    : " + std::to_string(plan.runtime.metrics_interval_ms) + " ms\n";
  if (plan.estimated_memory_budget_mb > 0) {
    out += "    memory budget (est.)  : " + std::to_string(plan.estimated_memory_budget_mb) + " MB\n";
  }

  if (!plan.notes.empty()) {
    out += "\n  notes:\n";
    for (const std::string& note : plan.notes) out += "    - " + note + "\n";
  }
  if (!plan.warnings.empty()) {
    out += "\n  warnings:\n";
    for (const std::string& warning : plan.warnings) out += "    ! " + warning + "\n";
  }

  out += "\n  resolved: BrowserX " + plan.boot.ToCommandLine() + "\n";
  if (profile.total_ram_mb != 0) {
    out += "  (plan generated for " + std::to_string(profile.total_ram_mb) + " MB RAM / " +
           std::to_string(profile.cpu_physical) + " cores / shm " +
           std::to_string(profile.shm_total_mb) + " MB)\n";
  }
  return out;
}

std::string RenderPlanJson(const ModePlan& plan, const SystemProfile& profile) {
  const ModeInfo& info = GetModeInfo(plan.mode);
  std::string out = "{\n";
  out += "  \"mode\": \"" + std::string(info.id) + "\",\n";
  out += "  \"label\": \"" + std::string(info.label) + "\",\n";
  out += "  \"switch_count\": " + std::to_string(plan.boot.switches().size()) + ",\n";
  out += "  \"child_switch_count\": " + std::to_string(plan.child.switches().size()) + ",\n";
  out += "  \"command_line\": \"" + JsonEscape(plan.boot.ToCommandLine()) + "\",\n";

  out += "  \"features_enabled\": [";
  const auto& enabled = plan.boot.enabled_features();
  for (size_t i = 0; i < enabled.size(); ++i) {
    out += (i == 0 ? "\"" : ", \"") + JsonEscape(enabled[i]) + "\"";
  }
  out += "],\n";
  out += "  \"features_disabled\": [";
  const auto& disabled = plan.boot.disabled_features();
  for (size_t i = 0; i < disabled.size(); ++i) {
    out += (i == 0 ? "\"" : ", \"") + JsonEscape(disabled[i]) + "\"";
  }
  out += "],\n";

  out += "  \"switches\": [\n";
  const auto& switches = plan.boot.switches();
  for (size_t i = 0; i < switches.size(); ++i) {
    out += "    {\"id\": \"" + JsonEscape(switches[i].id) + "\", \"value\": \"" +
           JsonEscape(switches[i].value) + "\", \"provenance\": \"" +
           JsonEscape(switches[i].provenance) + "\", \"reason\": \"" +
           JsonEscape(switches[i].reason) + "\"}";
    out += (i + 1 == switches.size()) ? "\n" : ",\n";
  }
  out += "  ],\n";

  // The UI panels (browserx://modes, the status bar, the preview) render this
  // object directly, so every field the runtime policy exposes is emitted here.
  const auto flag = [](bool value) { return std::string(value ? "true" : "false"); };
  out += "  \"runtime\": {\n";
  out += "    \"background_throttling\": " + flag(plan.runtime.background_throttling) + ",\n";
  out += "    \"target_fps\": " + std::to_string(static_cast<int>(plan.runtime.target_fps)) + ",\n";
  out += "    \"sleep_after_seconds\": " + std::to_string(plan.runtime.sleep_after_seconds) + ",\n";
  out += "    \"discard_after_seconds\": " + std::to_string(plan.runtime.discard_after_seconds) + ",\n";
  out += "    \"warn_pressure\": " + std::to_string(plan.runtime.warn_pressure) + ",\n";
  out += "    \"discard_pressure\": " + std::to_string(plan.runtime.discard_pressure) + ",\n";
  out += "    \"cache_size_mb\": " + std::to_string(plan.runtime.cache_size_mb) + ",\n";
  out += "    \"metrics_interval_ms\": " + std::to_string(plan.runtime.metrics_interval_ms) + ",\n";
  out += "    \"keep_display_awake\": " + flag(plan.runtime.keep_display_awake) + ",\n";
  out += "    \"always_on_top\": " + flag(plan.runtime.always_on_top) + ",\n";
  out += "    \"bfcache\": " + flag(plan.runtime.bfcache) + ",\n";
  out += "    \"preconnect\": " + flag(plan.runtime.preconnect) + ",\n";
  out += "    \"code_cache\": " + flag(plan.runtime.code_cache) + ",\n";
  out += "    \"reduce_motion\": " + flag(plan.runtime.reduce_motion) + ",\n";
  out += "    \"block_trackers\": " + flag(plan.runtime.block_trackers) + ",\n";
  out += "    \"block_third_party\": " + flag(plan.runtime.block_third_party) + ",\n";
  out += "    \"block_images\": " + flag(plan.runtime.block_images) + ",\n";
  out += "    \"block_media\": " + flag(plan.runtime.block_media) + ",\n";
  out += "    \"block_fonts\": " + flag(plan.runtime.block_fonts) + ",\n";
  out += "    \"lazy_new_tabs\": " + flag(plan.runtime.lazy_new_tabs) + ",\n";
  out += "    \"show_perf_hud\": " + flag(plan.runtime.show_perf_hud) + ",\n";
  out += "    \"show_status_counters\": " + flag(plan.runtime.show_status_counters) + "\n";
  out += "  },\n";

  // Rationale + caveats: the internal pages show these next to the switches,
  // and they are the honest part (what this mode does *not* do for you).
  out += "  \"rationale\": \"" + JsonEscape(info.detail != nullptr ? info.detail : "") + "\",\n";
  out += "  \"tagline\": \"" + JsonEscape(info.tagline != nullptr ? info.tagline : "") + "\",\n";
  out += "  \"icon\": \"" + JsonEscape(info.icon != nullptr ? info.icon : "") + "\",\n";
  out += "  \"notes\": [";
  for (size_t i = 0; i < plan.notes.size(); ++i) {
    out += (i == 0 ? "\n    \"" : ",\n    \"") + JsonEscape(plan.notes[i]) + "\"";
  }
  out += plan.notes.empty() ? "],\n" : "\n  ],\n";
  out += "  \"warnings\": [";
  for (size_t i = 0; i < plan.warnings.size(); ++i) {
    out += (i == 0 ? "\n    \"" : ",\n    \"") + JsonEscape(plan.warnings[i]) + "\"";
  }
  out += plan.warnings.empty() ? "],\n" : "\n  ],\n";

  out += "  \"estimated_memory_budget_mb\": " + std::to_string(plan.estimated_memory_budget_mb) + ",\n";
  out += "  \"system\": {\"os\": \"" + JsonEscape(profile.os_name) + "\", \"ram_mb\": " +
         std::to_string(profile.total_ram_mb) + ", \"cores\": " +
         std::to_string(profile.cpu_physical) + ", \"shm_mb\": " +
         std::to_string(profile.shm_total_mb) + "}\n";
  out += "}";
  return out;
}

std::string RenderPlansJson(const std::vector<ModePlan>& plans, const SystemProfile& profile) {
  std::string out = "[\n";
  for (size_t i = 0; i < plans.size(); ++i) {
    out += RenderPlanJson(plans[i], profile);
    out += (i + 1 == plans.size()) ? "\n" : ",\n";
  }
  out += "]\n";
  return out;
}

std::string RenderPlanDiff(const ModePlan& plan_a, const ModePlan& plan_b) {
  const std::vector<std::string> left = plan_a.boot.ToSortedFlagList();
  const std::vector<std::string> right = plan_b.boot.ToSortedFlagList();
  const std::string label_a = GetModeInfo(plan_a.mode).label;
  const std::string label_b = GetModeInfo(plan_b.mode).label;

  std::string out = "\n  " + label_a + "  vs  " + label_b +
                    "\n  ---------------------------------------------------------------\n";
  out += "\n  only in " + label_a + ":\n";
  for (const std::string& flag : left) {
    if (!Contains(right, flag)) out += "    + " + flag + "\n";
  }
  out += "\n  only in " + label_b + ":\n";
  for (const std::string& flag : right) {
    if (!Contains(left, flag)) out += "    + " + flag + "\n";
  }
  out += "\n  shared: " + std::to_string(SharedFlags(plan_a, plan_b)) + " flags\n";
  return out;
}

std::string RenderModeList(const SystemProfile& profile) {
  const Mode recommended = DefaultModeFor(profile);
  std::string out;
  out += "\n  BrowserX modes\n";
  out += "  =====================================================================\n";
  for (const ModeInfo& info : AllModes()) {
    const ModePlan plan = [&] {
      EngineContext context;
      context.profile = &profile;
      return BuildPlan(info.mode, context);
    }();
    out += "\n  " + std::string(info.icon) + "  " + PadRight(info.label, 16) + " --mode=" + info.id;
    if (info.mode == recommended) out += "   <- recommended for this machine";
    out += "\n     " + std::string(info.tagline) + "\n";
    out += "     boot switches: " + std::to_string(plan.boot.switches().size()) +
           ", cache: " + std::to_string(plan.runtime.cache_size_mb) + " MB";
    if (plan.estimated_memory_budget_mb > 0) {
      out += ", memory budget ~" + std::to_string(plan.estimated_memory_budget_mb) + " MB";
    }
    out += "\n";
    out += "     " + std::string(info.detail) + "\n";
  }
  out += "\n";
  return out;
}

std::string RenderProfileReport(const SystemProfile& profile) {
  return "\n" + DescribeProfile(profile);
}

std::string RenderCatalogFooter() {
  const CatalogStats stats = GetCatalogStats();
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer),
                "\n  catalogue: %zu switches (%zu verified, %zu chrome-only, %zu legacy, %zu "
                "removed), %zu features\n"
                "  every switch above is checked against this catalogue - no cargo-culted flags.\n\n",
                stats.switches_total, stats.switches_verified, stats.switches_chrome_only,
                stats.switches_legacy, stats.switches_removed, stats.features_total);
  return std::string(buffer);
}

std::string RenderRuntimePolicySummary(const RuntimePolicy& policy) {
  std::string out;
  out += "throttling=" + std::string(policy.background_throttling ? "on" : "off");
  out += " fps=" + (policy.target_fps <= 0.0 ? std::string("uncapped")
                                            : std::to_string(static_cast<int>(policy.target_fps)));
  out += " sleep=" + (policy.sleep_after_seconds == 0 ? std::string("never")
                                                    : FormatDuration(policy.sleep_after_seconds));
  out += " cache=" + std::to_string(policy.cache_size_mb) + "MB";
  out += " shield=" + std::string(policy.block_trackers ? "on" : "off");
  return out;
}

}  // namespace report

// ModePlan::ToReport() is declared in mode_engine.h. It is defined here so that
// every rendering of a plan - text, JSON, diff - lives in this one file.
std::string ModePlan::ToReport() const {
  return report::RenderPlanText(*this, SystemProfile{}, /*quiet=*/false);
}

}  // namespace core
}  // namespace browserx
