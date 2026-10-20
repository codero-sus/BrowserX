// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/ai_session.h"

#include <ctime>

#include "core/format.h"

namespace browserx {
namespace app {

void AiSession::ResolveDefaults() {
  // An empty endpoint means "the provider's default", resolved to a concrete string so
  // every consumer (the page, the request, the error message) names the same host.
  const core::AiConfig defaults = core::AiDefaults(config_.provider);
  if (core::fmt::Trim(config_.endpoint).empty()) config_.endpoint = defaults.endpoint;
  if (config_.provider == core::AiProvider::kCloud && config_.model.empty()) {
    config_.model = defaults.model;
  }
  // Local providers keep an empty model: the server picks, and the page shows which.
  if (core::fmt::Trim(config_.endpoint) == defaults.endpoint &&
      config_.provider != core::AiProvider::kCloud) {
    config_.model.clear();
  }
}

void AiSession::LoadFromSettings(const core::Settings& settings) {
  core::AiProvider provider = core::AiProvider::kOllama;
  if (!core::ParseAiProvider(settings.ai_provider, &provider)) provider = core::AiProvider::kOllama;
  core::AiConfig config = core::AiDefaults(provider);
  config.endpoint = settings.ai_endpoint;  // empty is resolved by ResolveDefaults()
  config.model = settings.ai_model;
  config.api_key = settings.ai_api_key;
  config.temperature = settings.ai_temperature;
  config.max_tokens = settings.ai_max_tokens;
  config.send_page_context = settings.ai_send_page_context;
  config.max_context_chars = settings.ai_max_context_chars;
  config_ = config;
  ResolveDefaults();
}

void AiSession::SaveToSettings(core::Settings* settings) const {
  if (settings == nullptr) return;
  settings->ai_provider = core::AiProviderId(config_.provider);
  // Save the *default* as empty, so changing a default later is not overridden by a value
  // the user never typed.
  settings->ai_endpoint = config_.endpoint == core::AiDefaults(config_.provider).endpoint
                              ? std::string()
                              : config_.endpoint;
  settings->ai_model = config_.model;
  settings->ai_api_key = config_.api_key;
  settings->ai_temperature = config_.temperature;
  settings->ai_max_tokens = config_.max_tokens;
  settings->ai_send_page_context = config_.send_page_context;
  settings->ai_max_context_chars = config_.max_context_chars;
}

bool AiSession::SetConfig(core::AiConfig config, std::string* error) {
  std::string problem;
  if (!core::ValidateConfig(config, &problem)) {
    if (error != nullptr) *error = problem;
    return false;
  }
  config_ = std::move(config);
  ResolveDefaults();
  return true;
}

bool AiSession::SetProvider(core::AiProvider provider, std::string* error) {
  core::AiConfig config = core::AiDefaults(provider);
  // The key always travels with the session: switching provider or endpoint must not make
  // the user paste a secret again, and "keep it only sometimes" is a rule nobody can
  // predict. It is still only *sent* by the cloud provider (BuildRequest checks).
  config.api_key = config_.api_key;
  config.send_page_context = config_.send_page_context;
  config.max_context_chars = config_.max_context_chars;
  config.model = provider == core::AiProvider::kCloud ? core::AiDefaults(provider).model
                                                     : config_.model;
  return SetConfig(std::move(config), error);
}

std::string AiSession::Problem() const {
  std::string error;
  if (!core::ValidateConfig(config_, &error)) return error;
  return std::string();
}

void AiSession::SetPage(std::string url, std::string title,
                        std::vector<std::string> visible_text_lines) {
  page_url_ = std::move(url);
  page_title_ = std::move(title);
  std::string joined;
  for (const std::string& line : visible_text_lines) {
    if (line.empty()) continue;
    if (!joined.empty()) joined += '\n';
    joined += line;
  }
  page_text_ = joined;
}

void AiSession::ClearPage() {
  page_url_.clear();
  page_title_.clear();
  page_text_.clear();
}

std::string AiSession::SummaryPrompt() const {
  const int cap = config_.max_context_chars;
  return core::BuildSummaryPrompt(page_title_, page_url_, page_text_, cap);
}

std::string AiSession::SystemPrompt() const { return core::AiSystemPrompt(); }

bool AiSession::BuildRequest(const std::string& question, bool include_page_text,
                             bool summarize, HttpRequest* request, std::string* error) const {
  const auto fail = [error](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  if (request == nullptr) return fail("no request to fill");
  if (const std::string problem = Problem(); !problem.empty()) return fail(problem);
  if (core::fmt::Trim(question).empty() && !summarize) return fail("Type a question first.");

  std::string prompt;
  bool used_text = false;
  if (summarize) {
    // Summarising is by definition about the page; the caller does not have to also tick
    // the box. If there is no page text, say so instead of sending an empty document.
    if (page_text_.empty()) return fail("There is no page text to summarise yet.");
    prompt = SummaryPrompt();
    used_text = true;
  } else {
    prompt = question;
    if (include_page_text) {
      if (page_text_.empty()) return fail("No page text was captured yet.");
      prompt += "\n\nUse this page text where it helps, and say when it does not "
                "answer the question:\n\n" +
                core::ClampText(page_text_, static_cast<size_t>(config_.max_context_chars));
      used_text = true;
    }
  }
  (void)used_text;

  request->method = "POST";
  request->url = core::ChatUrl(config_);
  request->timeout_ms = config_.provider == core::AiProvider::kCloud ? 60000 : 120000;
  request->headers.clear();
  request->headers.push_back({"Content-Type", "application/json"});
  request->headers.push_back({"Accept", "application/json"});
  if (!config_.api_key.empty()) {
    request->headers.push_back({"Authorization", "Bearer " + config_.api_key});
  }
  request->body =
      core::json::Serialize(core::BuildChatRequest(config_, SystemPrompt(), prompt));
  return true;
}

AiTurn AiSession::Complete(const std::string& question, bool used_page_text,
                           const HttpResponse& response) {
  AiTurn turn;
  turn.question = question;
  turn.provider = core::AiProviderId(config_.provider);
  turn.model = config_.model.empty() ? "(server default)" : config_.model;
  turn.used_page_text = used_page_text;
  turn.timestamp = LocalClock();
  turn.prompt_tokens_estimate = core::EstimateTokens(question);
  if (!response.ok) {
    turn.error = response.error;
    turns_.push_back(std::move(turn));
    return turns_.back();
  }
  std::string text;
  std::string parse_error;
  if (response.status >= 400) {
    // A 401 body is a JSON error object; parsing it as a chat answer would report
    // "the provider did not answer with text" and bury the actual reason.
    turn.error = core::DescribeHttpFailure(config_.provider, response.status, response.body);
  } else if (!core::ParseChatResponse(config_.provider, response.body, &text, &parse_error)) {
    turn.error = parse_error;
  } else {
    turn.answer = text;
    turn.answer_tokens_estimate = core::EstimateTokens(text);
  }
  turns_.push_back(std::move(turn));
  return turns_.back();
}

HttpRequest AiSession::TestRequest() const {
  HttpRequest request;
  request.method = "GET";
  request.url = core::ModelsUrl(config_);
  request.timeout_ms = 5000;
  request.headers.push_back({"Accept", "application/json"});
  if (!config_.api_key.empty()) {
    request.headers.push_back({"Authorization", "Bearer " + config_.api_key});
  }
  return request;
}

std::vector<std::string> AiSession::ModelsFromTest(const HttpResponse& response) const {
  if (!response.ok || response.status != 200) return {};
  return core::ParseModelList(config_.provider, response.body);
}

std::vector<std::string> AiSession::StatusLines() const {
  std::vector<std::string> lines;
  lines.push_back(std::string("Provider: ") + core::AiProviderLabel(config_.provider));
  lines.push_back(core::AiProviderLocation(config_.provider));
  lines.push_back("Endpoint: " + config_.endpoint);
  lines.push_back("Model: " + (config_.model.empty() ? std::string("(server default)")
                                                      : config_.model));
  if (!config_.api_key.empty()) {
    lines.push_back("API key: " + core::KeyFingerprint(config_.api_key) + " (stored in plain "
                    "text in settings.json)");
  } else if (config_.provider == core::AiProvider::kCloud) {
    lines.push_back("API key: not set - cloud answers cannot be requested yet");
  }
  // Two different facts, and conflating them was a mistake in the first draft: the text
  // is always read into memory so a summary can be requested, but it is only *sent* when
  // the request says so.
  lines.push_back("Page text: read locally, sent only when you ask (up to " +
                  std::to_string(config_.max_context_chars) + " characters)");
  return lines;
}

std::string AiSession::DescribeRequest(const HttpRequest& request) const {
  std::string out = request.method + " " + request.url + "\n";
  for (const HttpHeader& header : request.headers) {
    const std::string lowered = core::fmt::ToLower(header.name);
    if (lowered == "authorization") {
      out += "Authorization: Bearer " + core::KeyFingerprint(header.value) + " (redacted)\n";
      continue;
    }
    out += header.name + ": " + header.value + "\n";
  }
  out += "\n" + request.body;
  return out;
}

std::string LocalClock() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", local.tm_hour, local.tm_min);
  return std::string(buffer);
}

}  // namespace app
}  // namespace browserx
