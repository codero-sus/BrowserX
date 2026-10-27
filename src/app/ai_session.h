// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the live AI session: settings, the current question, page text, answers.
//
// CEF-free on purpose. Every decision here — what is sent, to which host, with which key,
// and what is kept — is testable without a browser, and the UI layer only has to render
// what this class reports. That matters more for AI than elsewhere: the failure mode of a
// built-in assistant is not a crash, it is quietly sending something the user did not
// choose.
//
// The session holds at most one pending question. Two concurrent answers would need
// generation ids, cancellation and a queue, and none of that is worth it for a side panel.

#ifndef BROWSERX_APP_AI_SESSION_H_
#define BROWSERX_APP_AI_SESSION_H_

#include <string>
#include <vector>

#include "app/http_client.h"
#include "core/ai.h"
#include "core/settings_schema.h"

namespace browserx {
namespace app {

/// One finished exchange, kept for the page. Not persisted: answers are not history.
struct AiTurn {
  std::string question;
  std::string answer;
  std::string error;      // non-empty when the turn failed
  std::string provider;   // id at the time of asking, so a later switch is visible
  std::string model;
  bool used_page_text = false;
  int prompt_tokens_estimate = 0;
  int answer_tokens_estimate = 0;
  std::string timestamp;  // local HH:MM, informational only
};

class AiSession {
 public:
  /// Reads the persisted fields from settings. Endpoint empty means "use the default",
  /// which is resolved here so the UI and the requests agree on one value.
  void LoadFromSettings(const core::Settings& settings);
  /// Writes the live config back. The caller saves settings.json.
  void SaveToSettings(core::Settings* settings) const;

  const core::AiConfig& config() const { return config_; }
  /// Applies a change, then re-validates and re-resolves the endpoint defaults.
  /// Returns false (with `error`) when the new config is not usable.
  bool SetConfig(core::AiConfig config, std::string* error);
  /// Switches provider and installs that provider's defaults for empty fields.
  bool SetProvider(core::AiProvider provider, std::string* error);

  /// The resolved configuration problem, or empty when the session can send.
  std::string Problem() const;

  /// The visible page, from the browser layer. `url` is used for the prompt (redacted)
  /// and `text` is only ever attached when config().send_page_context is true.
  void SetPage(std::string url, std::string title,
               std::vector<std::string> visible_text_lines);
  void ClearPage();
  const std::string& page_url() const { return page_url_; }
  const std::string& page_title() const { return page_title_; }
  bool has_page_text() const { return !page_text_.empty(); }
  size_t page_text_size() const { return page_text_.size(); }
  const std::string& page_text() const { return page_text_; }

  /// The prompt that "Summarise this page" will send, for the page to show before it is
  /// sent. This is the whole point of a local-first design: you can read the request.
  std::string SummaryPrompt() const;

  /// Builds the request for `question`. Returns false with `error` when the session has
  /// no usable config, when the question is empty, or when page text was asked for but
  /// the page has none.
  bool BuildRequest(const std::string& question, bool include_page_text, bool summarize,
                    HttpRequest* request, std::string* error) const;

  /// Records the outcome. `response` is the raw HTTP result; the session parses it so
  /// the UI never has to know the difference between the three providers' shapes.
  AiTurn Complete(const std::string& question, bool used_page_text,
                  const HttpResponse& response);

  /// "Test connection": one GET to the models endpoint, no prompt, no page text.
  HttpRequest TestRequest() const;
  /// Fills the model list from a TestRequest response. Empty on any failure.
  std::vector<std::string> ModelsFromTest(const HttpResponse& response) const;

  const std::vector<AiTurn>& turns() const { return turns_; }
  void ClearTurns() { turns_.clear(); }

  /// What the page prints above the box: provider location, endpoint, key fingerprint.
  std::vector<std::string> StatusLines() const;

  /// The request the last BuildRequest produced, key omitted: for the "show the request"
  /// view. A key is never displayed, not even masked beyond its last four characters.
  std::string DescribeRequest(const HttpRequest& request) const;

 private:
  void ResolveDefaults();
  std::string SystemPrompt() const;

  core::AiConfig config_;
  std::string page_url_;
  std::string page_title_;
  std::string page_text_;  // already whitespace-collapsed and capped
  std::vector<AiTurn> turns_;
  double temperature_ = 0.2;  // kept in config_; these exist only for clarity of intent
  int max_tokens_ = 512;
};

/// A tiny local timestamp for AiTurn ("14:05"). Not a clock source for anything else.
std::string LocalClock();

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_AI_SESSION_H_
