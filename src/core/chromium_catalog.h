// BrowserX — verified Chromium command-line switch catalogue.
//
// WHY THIS FILE EXISTS
// -------------------
// Most "optimised Chromium browser" projects ship long lists of switches copied
// from blog posts. A large fraction of those switches were removed years ago, or
// never existed, so they do nothing at all. This catalogue instead records, for
// every switch BrowserX can apply:
//
//   * where the switch is *declared* in Chromium's source (file path), and
//   * how it was verified, and
//   * whether it is safe, advanced, or lab-only, and
//   * which process scope it affects.
//
// Verification method (see docs/FLAGS.md for the full report):
//   * Chromium source files fetched from github.com/chromium/chromium (ref: main)
//     and searched for the exact string literal, e.g. in
//     base/base_switches.h, ui/gl/gl_switches.cc, gpu/config/gpu_switches.cc,
//     cc/base/switches.cc, media/base/media_switches.cc,
//     content/public/common/content_switches.cc, sandbox/policy/switches.cc,
//     components/viz/common/switches.cc, net/disk_cache/cache_util.cc, ...
//   * a secondary cross-check against the community-maintained switch index.
//
// Entries are tagged:
//   kVerified   literal found in Chromium source; safe to apply.
//   kChromeOnly declared in chrome/ (the browser product, not the content
//               layer). Under CEF these may be no-ops, so BrowserX does not
//               rely on them; provided for completeness/lab use.
//   kLegacy     historically real; consumer could not be confirmed in current
//               Chromium. Never applied unless the user opts into lab flags.
//   kRemoved    confirmed absent from current Chromium (or superseded by a
//               default). Catalogued so the code and docs can never claim to
//               "optimise" with a flag that does nothing.

#ifndef BROWSERX_CORE_CHROMIUM_CATALOG_H_
#define BROWSERX_CORE_CHROMIUM_CATALOG_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace core {

/// Date of the verification sweep. Update when re-verifying against a newer
/// Chromium revision.
inline constexpr const char* kCatalogVerifiedOn = "2026-10-12";
inline constexpr const char* kCatalogChromiumRef = "main";

enum class Provenance {
  kVerified,
  kChromeOnly,
  kLegacy,
  kRemoved,
};

/// Which Chromium process the switch must be passed to.
enum class Scope {
  kAll,       // browser + every child process
  kBrowser,   // browser (main) process only
  kRenderer,  // renderer processes
  kGpu,       // GPU process
  kUtility,   // utility processes (network, audio, ...)
};

enum class Risk {
  kSafe,      // no user-visible downside in normal use
  kAdvanced,  // meaningful trade-off (usually security or stability)
  kLab,       // may break pages/browsing; opt-in only
};

struct SwitchEntry {
  const char* id;          // e.g. "disable-gpu-vsync"
  bool requires_value;     // true when the switch takes a value
  const char* value_hint;  // example/default value, for documentation + plans
  Provenance provenance;
  Scope scope;
  Risk risk;
  const char* source;      // Chromium source file that declares it
  const char* summary;     // one-line, human explanation
};

struct FeatureEntry {
  const char* name;        // e.g. "NetworkServiceInProcess2"
  Provenance provenance;
  Risk risk;
  const char* source;
  const char* summary;
};

const char* ProvenanceLabel(Provenance provenance);
const char* ScopeLabel(Scope scope);
const char* RiskLabel(Risk risk);

/// Complete catalogue (alphabetical by id).
const std::vector<SwitchEntry>& AllSwitches();
const std::vector<FeatureEntry>& AllFeatures();

/// Lookup; returns nullptr when unknown.
const SwitchEntry* FindSwitch(std::string_view id);
const FeatureEntry* FindFeature(std::string_view name);

/// True when the switch is safe to place on the command line (i.e. verified or
/// explicitly Chrome-only, and not catalogued as removed).
bool IsApplicable(const SwitchEntry& entry);
/// True when applying the switch needs non-default user consent.
bool RequiresOptIn(const SwitchEntry& entry);

struct CatalogStats {
  size_t switches_total = 0;
  size_t switches_verified = 0;
  size_t switches_chrome_only = 0;
  size_t switches_legacy = 0;
  size_t switches_removed = 0;
  size_t features_total = 0;
  size_t features_verified = 0;
  size_t features_legacy = 0;
  size_t features_removed = 0;
};
CatalogStats GetCatalogStats();

/// Multi-line, human-readable provenance report (used by `make bench` and the
/// in-app About/Diagnostics panel).
std::string RenderCatalogReport(bool include_removed = false);

/// URL to the Chromium code search for a switch literal (evidence links).
std::string SourceSearchUrl(std::string_view literal);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_CHROMIUM_CATALOG_H_
