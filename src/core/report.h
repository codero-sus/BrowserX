// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — human and machine readable reports of a resolved plan.
//
// One implementation, three callers: the browser (`--flag-plan`, the Mode panel,
// the diagnostics page), the `browserx-flagplan` tool (`make bench`) and the
// unit tests. Because they all print the same string, "the browser really does
// apply what `make bench` shows" is a property you can check by eye.
//
// Pure string building - no I/O, no globals, no CEF.

#ifndef BROWSERX_CORE_REPORT_H_
#define BROWSERX_CORE_REPORT_H_

#include <string>
#include <vector>

#include "core/mode_engine.h"
#include "core/system_profile.h"

namespace browserx {
namespace core {
namespace report {

/// Full text report for one mode: switches with reasons, runtime policy, notes,
/// warnings and the resolved command line. `quiet` prints only the latter.
std::string RenderPlanText(const ModePlan& plan, const SystemProfile& profile, bool quiet = false);

/// Machine-readable plan (stable keys; used by CI to diff plans between commits).
std::string RenderPlanJson(const ModePlan& plan, const SystemProfile& profile);

/// Plan for a single mode as a JSON array of one element.
std::string RenderPlansJson(const std::vector<ModePlan>& plans, const SystemProfile& profile);

/// "+ only in A / + only in B / shared" comparison of two modes.
std::string RenderPlanDiff(const ModePlan& plan_a, const ModePlan& plan_b);

/// A one-line-per-mode table, with the recommendation for this machine marked.
std::string RenderModeList(const SystemProfile& profile);

/// Detected hardware plus what BrowserX makes of it.
std::string RenderProfileReport(const SystemProfile& profile);

/// The catalogue footer printed after a plan ("every switch is verified").
std::string RenderCatalogFooter();

/// Small helper used by the panels and the status bar.
std::string RenderRuntimePolicySummary(const RuntimePolicy& policy);

}  // namespace report
}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_REPORT_H_
