// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "web/internal_scheme.h"

#include <chrono>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "app/cef_host.h"
#include "core/json.h"
#include "browser/browser_shell.h"
#include "core/chromium_catalog.h"
#include "core/mode_engine.h"
#include "core/report.h"
#include "core/system_profile.h"
#include "include/cef_request.h"
#include "include/cef_response.h"

#ifndef BROWSERX_VERSION
#define BROWSERX_VERSION "dev"
#endif

#ifndef BROWSERX_GIT
#define BROWSERX_GIT "unknown"
#endif

namespace browserx {
namespace web {
namespace {

/// Serves one rendered page. CEF calls Read() repeatedly; we keep the bytes and
/// hand out offsets, which is the cheapest possible implementation and keeps the
/// whole response in one buffer for Content-Length.
class InternalResourceHandler : public CefResourceHandler {
 public:
  explicit InternalResourceHandler(std::string body, std::string mime)
      : body_(std::move(body)), mime_(std::move(mime)) {}

  bool Open(CefRefPtr<CefRequest> request, bool& handle_request,
            CefRefPtr<CefCallback> callback) override {
    (void)request;
    (void)callback;
    // Answered synchronously: every internal page is already in memory.
    handle_request = true;
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t& response_length,
                          CefString& redirect_url) override {
    response->SetStatus(200);
    response->SetStatusText("OK");
    response->SetMimeType(mime_);
    response->SetHeaderByName("Cache-Control", "no-store", true);
    response_length = static_cast<int64_t>(body_.size());
    redirect_url.clear();
  }

  bool Read(void* data_out, int bytes_to_read, int& bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override {
    (void)callback;
    const int64_t remaining = static_cast<int64_t>(body_.size()) - offset_;
    if (remaining <= 0) {
      bytes_read = 0;
      return false;  // response complete
    }
    const int chunk = static_cast<int>(std::min<int64_t>(remaining, bytes_to_read));
    std::memcpy(data_out, body_.data() + offset_, static_cast<size_t>(chunk));
    offset_ += chunk;
    bytes_read = chunk;
    return true;
  }

  bool Skip(int64_t bytes_to_skip, int64_t& bytes_skipped,
            CefRefPtr<CefResourceSkipCallback> callback) override {
    (void)callback;
    const int64_t remaining = static_cast<int64_t>(body_.size()) - offset_;
    bytes_skipped = std::min(bytes_to_skip, remaining);
    offset_ += bytes_skipped;
    return bytes_skipped > 0;
  }

  void Cancel() override { cancelled_ = true; }

 private:
  std::string body_;
  std::string mime_;
  int64_t offset_ = 0;
  bool cancelled_ = false;

  IMPLEMENT_REFCOUNTING(InternalResourceHandler);
};

class InternalSchemeFactory : public CefSchemeHandlerFactory {
 public:
  explicit InternalSchemeFactory(app::CefHost* host) : host_(host) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    (void)browser;
    (void)frame;
    (void)scheme_name;
    if (request == nullptr || host_ == nullptr) return nullptr;

    const std::string url = request->GetURL().ToString();
    InternalUrl parsed;
    if (!ParseInternalUrl(url, &parsed)) return nullptr;

    // browserx://modes?set=<id> is the only privileged request an internal page can
    // make: it changes the mode and then renders the page for the new state.
    if (ParseModeSwitchRequest(parsed, nullptr)) {
      std::string summary;
      ApplyModeSwitchRequest(host_, url, &summary);
    }

    PageData data = BuildPageData(*host_);
    data.requested_host = parsed.host;
    // browserx://security?url=<encoded>: the page explains the connection the indicator
    // was clicked on. Decoded here - a query string is not text until it is decoded once,
    // and the page must not do that itself with string surgery.
    if (parsed.host == "security" && !parsed.query.empty()) {
      const std::string key = "url=";
      const size_t at = parsed.query.find(key);
      if (at != std::string::npos) {
        std::string raw = parsed.query.substr(at + key.size());
        const size_t amp = raw.find('&');
        if (amp != std::string::npos) raw = raw.substr(0, amp);
        std::string decoded;
        for (size_t i = 0; i < raw.size(); ++i) {
          if (raw[i] == '%' && i + 2 < raw.size()) {
            const auto hex = [](char c) -> int {
              if (c >= '0' && c <= '9') return c - '0';
              if (c >= 'a' && c <= 'f') return c - 'a' + 10;
              if (c >= 'A' && c <= 'F') return c - 'A' + 10;
              return -1;
            };
            const int hi = hex(raw[i + 1]);
            const int lo = hex(raw[i + 2]);
            if (hi >= 0 && lo >= 0) {
              decoded += static_cast<char>(hi * 16 + lo);
              i += 2;
              continue;
            }
          }
          decoded += raw[i] == '+' ? ' ' : raw[i];
        }
        data.security_url = decoded;
      }
    }
    const PageKind kind = KindForHost(parsed.host);
    return new InternalResourceHandler(RenderPage(kind, data), ContentTypeFor(kind));
  }

 private:
  app::CefHost* host_ = nullptr;  // owned by the app for the process lifetime

  IMPLEMENT_REFCOUNTING(InternalSchemeFactory);
};

}  // namespace

PageData BuildPageData(const app::CefHost& host) {
  PageData data;
  data.version = BROWSERX_VERSION;
  data.git_revision = BROWSERX_GIT;

  // --- native features -----------------------------------------------------
  // Read from the same host state the boot flags were built from, so the page cannot
  // describe a Tor configuration different from the one in force.
  data.tor_on = host.options().tor;
  data.tor_port = host.tor_plan().port;
  data.tor_reachable = host.tor_probe().reachable;
  if (data.tor_on) {
    data.tor_status = core::DescribeTorStatus(data.tor_reachable, data.tor_port);
    data.tor_warnings = host.tor_plan().warnings;
  }
  data.extensions_on = host.options().extensions;
  data.extensions_dir = host.extension_scan().directory;
  for (const core::ExtensionInfo& extension : host.extension_scan().extensions) {
    data.extension_lines.push_back(core::DescribeExtension(extension));
  }
  data.extension_refused = host.extension_scan().refused;

  const app::CefHost::RuleImport& imported = host.rule_import();
  data.rules_file = imported.file;
  data.rules_present = imported.file_present;
  data.rules_added = imported.report.added;
  data.rules_duplicates = imported.report.duplicates;
  data.rules_skipped = imported.report.skipped;
  data.rules_examples = imported.report.examples;
  const BrowserShell* shell = host.shell();
  const core::Blocklist* shield = shell != nullptr ? shell->blocklist() : nullptr;
  if (shield != nullptr) {
    data.shield_on = shield->enabled();
    data.shield_rules = shield->rule_count();
    for (const core::BlockCategory category :
         {core::BlockCategory::kAds, core::BlockCategory::kAnalytics,
          core::BlockCategory::kSocial, core::BlockCategory::kCrypto,
          core::BlockCategory::kFingerprint}) {
      size_t count = 0;
      for (const core::BlockRule& rule : shield->rules()) {
        if (rule.category == category) ++count;
      }
      data.shield_categories.push_back(std::string(core::BlockCategoryLabel(category)) +
                                       " - " + std::to_string(count) + " rules");
    }
  }

  const core::ModeInfo& info = core::GetModeInfo(host.plan().mode);
  data.active_mode_id = info.id != nullptr ? info.id : "ultra";
  data.active_mode_label = info.label != nullptr ? info.label : "ULTRA";
  data.runtime_summary = core::report::RenderRuntimePolicySummary(host.plan().runtime);
  data.plan_text = host.plan().ToReport();
  data.profile_report = core::DescribeProfile(host.system_profile());
  data.catalog_report = core::RenderCatalogReport(false);
  // The start page's search box uses the same template as the omnibox, so the page
  // cannot search somewhere the user did not configure.
  data.search_template = host.settings().settings().search_template;

  // Every mode, resolved for this machine: the same JSON `make bench --json`
  // prints, so what the page shows is what the browser would apply.
  core::EngineContext context;
  context.profile = &host.system_profile();
  context.overrides = host.settings().settings().MakeContext(host.plan().mode,
                                                            &host.system_profile())
                          .overrides;
  std::vector<core::ModePlan> plans;
  for (const core::ModeInfo& mode_info : core::AllModes()) {
    plans.push_back(core::BuildPlan(mode_info.mode, context));
  }
  data.plans_json = core::report::RenderPlansJson(plans, host.system_profile());

  // What would change if you switched: computed by the engine from the same plans, so the
  // modes page can say "adds 4 switches, removes 2" instead of "faster".
  {
    const core::ModePlan* active_plan = nullptr;
    for (const core::ModePlan& plan : plans) {
      if (plan.mode == host.plan().mode) active_plan = &plan;
    }
    core::json::Value::Array diffs;
    for (const core::ModePlan& plan : plans) {
      const core::ModeInfo& info = core::GetModeInfo(plan.mode);
      const std::vector<core::PlanDifference> against =
          active_plan != nullptr ? core::DiffPlans(*active_plan, plan, /*max_items=*/12)
                                 : std::vector<core::PlanDifference>();
      core::json::Value::Object entry;
      entry["id"] = core::json::Value(info.id != nullptr ? info.id : "");
      entry["label"] = core::json::Value(info.label != nullptr ? info.label : "");
      entry["summary"] = core::json::Value(core::DescribePlanDiff(against));
      core::json::Value::Array items;
      for (const core::PlanDifference& difference : against) {
        core::json::Value::Object item;
        item["flag"] = core::json::Value(difference.ToFlag());
        item["from"] = core::json::Value(difference.from_value);
        item["to"] = core::json::Value(difference.to_value);
        item["reason"] = core::json::Value(difference.to_reason.empty() ? difference.from_reason
                                                                       : difference.to_reason);
        items.push_back(core::json::Value(item));
      }
      entry["items"] = core::json::Value(items);
      diffs.push_back(core::json::Value(entry));
    }
    data.mode_diffs_json = core::json::Serialize(core::json::Value(diffs), 0);
  }

  // History and bookmarks, straight from the store the browser writes. The pages render
  // rows from this JSON, so browserx://history shows the same list the file holds.
  data.history_json = host.library().history().ToJson();
  data.bookmarks_json = host.library().bookmarks().ToJson();
  data.now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  // Session state, when the shell has a window open. The interface returns an
  // empty string otherwise, and the pages report that honestly.
  if (host.shell() != nullptr) {
    data.session_json = host.shell()->SessionJson();
  }
  return data;
}

bool ApplyModeSwitchRequest(app::CefHost* host, const std::string& url, std::string* summary) {
  if (host == nullptr) return false;
  InternalUrl parsed;
  if (!ParseInternalUrl(url, &parsed)) return false;

  std::string mode_id;
  if (!ParseModeSwitchRequest(parsed, &mode_id)) return false;

  core::Mode mode = core::Mode::kUltra;
  if (!core::ParseMode(mode_id, &mode)) return false;  // unknown id: no-op, no crash

  // The engine validates the id. The shell owns the live policy (metrics, HUD,
  // window indicators), so it applies the mode when it exists; without a window
  // yet, the host still updates the plan so the first window starts in it.
  if (host->shell() != nullptr) {
    host->shell()->ApplyMode(mode);
    std::string rendered;
    host->ReplanFor(host->plan().mode, &rendered);  // no-op rebuild for the summary
  } else {
    std::string result;
    host->ReplanFor(mode, &result);
    if (summary != nullptr) *summary = result;
  }
  if (host->shell() != nullptr) {
    const core::ModeInfo& info = core::GetModeInfo(mode);
    std::string description = info.label != nullptr ? std::string(info.label) : mode_id;
    description += host->restart_pending()
                       ? " selected - restart BrowserX to apply the boot switches"
                       : " active";
    if (summary != nullptr) *summary = description;
  }
  return true;
}

bool RegisterInternalSchemeHandler(app::CefHost* host) {
  if (host == nullptr) return false;
  // An empty domain means "every host under this scheme".
  return CefRegisterSchemeHandlerFactory("browserx", "", new InternalSchemeFactory(host));
}

}  // namespace web
}  // namespace browserx
