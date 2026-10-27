// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/ai.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

#include "core/format.h"
#include "core/net_errors.h"

namespace browserx {
namespace core {
namespace {

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string StripTrailingSlashes(std::string text) {
  while (!text.empty() && text.back() == '/') text.pop_back();
  return text;
}

/// `scheme://host[:port][/path]` -> host, lower-cased, without brackets around an IPv6
/// literal. Empty when there is no authority at all.
std::string HostOf(std::string_view url) {
  const size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) return std::string();
  std::string_view rest = url.substr(scheme_end + 3);
  const size_t slash = rest.find_first_of("/?#");
  if (slash != std::string_view::npos) rest = rest.substr(0, slash);
  // Drop userinfo, which a URL may carry but a host must not.
  const size_t at = rest.rfind('@');
  if (at != std::string_view::npos) rest = rest.substr(at + 1);
  if (rest.empty()) return std::string();
  std::string host;
  if (rest.front() == '[') {  // [::1]:11434
    const size_t close = rest.find(']');
    if (close == std::string_view::npos) return std::string();
    host = std::string(rest.substr(1, close - 1));
  } else {
    const size_t colon = rest.find(':');
    host = std::string(colon == std::string_view::npos ? rest : rest.substr(0, colon));
  }
  return fmt::ToLower(host);
}

/// Flattens a body for display: strips tags crudely, collapses whitespace, caps length.
/// Used only for error snippets, never for answers.
std::string FlattenSnippet(std::string_view body, size_t max_chars) {
  std::string out;
  out.reserve(std::min<size_t>(body.size(), max_chars * 2));
  bool in_tag = false;
  for (const char c : body) {
    if (c == '<') {
      in_tag = true;
      continue;
    }
    if (c == '>') {
      in_tag = false;
      continue;
    }
    if (in_tag) continue;
    out.push_back(std::isspace(static_cast<unsigned char>(c)) ? ' ' : c);
  }
  std::string collapsed;
  collapsed.reserve(out.size());
  bool last_space = false;
  for (const char c : out) {
    const bool space = c == ' ';
    if (space && last_space) continue;
    collapsed.push_back(c);
    last_space = space;
  }
  return ClampText(fmt::Trim(collapsed), max_chars);
}

}  // namespace

const char* AiProviderId(AiProvider provider) {
  switch (provider) {
    case AiProvider::kCortex: return "cortex";
    case AiProvider::kOllama: return "ollama";
    case AiProvider::kCloud: return "cloud";
  }
  return "ollama";
}

const char* AiProviderLabel(AiProvider provider) {
  switch (provider) {
    case AiProvider::kCortex: return "Cortex LLMHoster";
    case AiProvider::kOllama: return "Ollama";
    case AiProvider::kCloud: return "Cloud (OpenAI-compatible)";
  }
  return "Ollama";
}

const char* AiProviderLocation(AiProvider provider) {
  switch (provider) {
    case AiProvider::kCortex:
      return "Runs on this machine (Cortex_LLMHoster by codero-sus). Your words do not "
             "leave it.";
    case AiProvider::kOllama:
      return "Runs on this machine (Ollama). Your words do not leave it.";
    case AiProvider::kCloud:
      return "Runs on a server you choose. The question, and any page text you attach, is "
             "sent there.";
  }
  return "";
}

bool ParseAiProvider(std::string_view id, AiProvider* provider) {
  const std::string lowered = fmt::ToLower(fmt::Trim(id));
  AiProvider parsed = AiProvider::kOllama;
  if (lowered == "cortex" || lowered == "cortex_llmhoster" || lowered == "llmhoster") {
    parsed = AiProvider::kCortex;
  } else if (lowered == "ollama") {
    parsed = AiProvider::kOllama;
  } else if (lowered == "cloud" || lowered == "openai" || lowered == "openai-compatible") {
    parsed = AiProvider::kCloud;
  } else {
    return false;
  }
  if (provider != nullptr) *provider = parsed;
  return true;
}

AiConfig AiDefaults(AiProvider provider) {
  AiConfig config;
  config.provider = provider;
  switch (provider) {
    case AiProvider::kCortex:
      // Cortex_LLMHoster has no documented port yet; 8000 is the usual OpenAI-compatible
      // local-server default (vLLM, llama.cpp's server, LM Studio's proxy). The page
      // says the number is a default, not a discovery.
      config.endpoint = "http://127.0.0.1:8000/v1";
      break;
    case AiProvider::kOllama:
      config.endpoint = "http://127.0.0.1:11434";
      break;
    case AiProvider::kCloud:
      config.endpoint = "https://api.openai.com/v1";
      config.model = "gpt-4o-mini";  // a named default is clearer than an empty box
      break;
  }
  return config;
}

bool IsLoopbackEndpoint(std::string_view url) {
  const std::string host = HostOf(url);
  if (host == "localhost" || host == "::1" || host == "0:0:0:0:0:0:0:1") return true;
  // 127.0.0.0/8, written as four dotted parts.
  const std::vector<std::string> parts = fmt::Split(host, '.');
  return parts.size() == 4 && parts[0] == "127";
}

bool ValidateConfig(const AiConfig& config, std::string* error) {
  const auto fail = [error](const std::string& text) {
    if (error != nullptr) *error = text;
    return false;
  };
  const std::string endpoint = fmt::Trim(config.endpoint);
  if (endpoint.empty()) return fail("No endpoint set for " +
                                    std::string(AiProviderLabel(config.provider)) + ".");
  const bool https = StartsWith(endpoint, "https://");
  const bool http = StartsWith(endpoint, "http://");
  if (!https && !http) {
    return fail("The endpoint must start with http:// or https:// — other schemes are "
                "refused so a pasted value cannot make the browser read a local file.");
  }
  if (endpoint.find('<') != std::string::npos || endpoint.find(' ') != std::string::npos) {
    return fail("The endpoint contains characters a URL cannot contain.");
  }
  if (http && !IsLoopbackEndpoint(endpoint)) {
    return fail("A plain http:// endpoint is only allowed on this machine. An API key and "
                "a prompt sent over http to another host travel in clear text.");
  }
  if (config.provider == AiProvider::kCloud) {
    if (!StartsWith(endpoint, "https://")) {
      return fail("Cloud endpoints must use https://.");
    }
    if (fmt::Trim(config.api_key).empty()) {
      return fail("No API key set. The cloud provider needs one; local providers do not.");
    }
  }
  if (config.temperature < 0.0 || config.temperature > 2.0) {
    return fail("Temperature must be between 0 and 2.");
  }
  if (config.max_tokens < 32 || config.max_tokens > 8192) {
    return fail("Max tokens must be between 32 and 8192.");
  }
  if (config.max_context_chars < 0 || config.max_context_chars > 20000) {
    return fail("The page-text limit must be between 0 and 20000 characters.");
  }
  return true;
}

std::string ChatUrl(const AiConfig& config) {
  const std::string base = StripTrailingSlashes(fmt::Trim(config.endpoint));
  if (config.provider == AiProvider::kOllama) return base + "/api/chat";
  return base + "/chat/completions";
}

std::string ModelsUrl(const AiConfig& config) {
  const std::string base = StripTrailingSlashes(fmt::Trim(config.endpoint));
  if (config.provider == AiProvider::kOllama) return base + "/api/tags";
  return base + "/models";
}

json::Value BuildChatRequest(const AiConfig& config, std::string_view system_prompt,
                             std::string_view user_prompt) {
  json::Value root;
  root["model"] = json::Value(config.model);  // empty is allowed: the host picks
  root["stream"] = json::Value(false);        // one answer, one response, no SSE parsing
  root["temperature"] = json::Value(config.temperature);
  // Ollama spells the cap differently; sending the OpenAI name would be silently ignored.
  if (config.provider == AiProvider::kOllama) {
    root["options"]["num_predict"] = json::Value(static_cast<double>(config.max_tokens));
  } else {
    root["max_tokens"] = json::Value(static_cast<double>(config.max_tokens));
  }
  json::Value messages;
  json::Value system;
  system["role"] = json::Value("system");
  system["content"] = json::Value(std::string(system_prompt));
  json::Value user;
  user["role"] = json::Value("user");
  user["content"] = json::Value(std::string(user_prompt));
  messages.Push(std::move(system));
  messages.Push(std::move(user));
  root["messages"] = std::move(messages);
  return root;
}

bool ParseChatResponse(AiProvider provider, std::string_view body, std::string* text,
                       std::string* error) {
  const auto fail = [error](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  std::string parse_error;
  const std::optional<json::Value> parsed = json::Parse(body, &parse_error);
  if (!parsed.has_value()) {
    return fail("The answer was not JSON (" + parse_error +
                "). A proxy or login page may have answered instead of the model.");
  }
  const json::Value& root = *parsed;
  if (root.Get("error").IsString() || root.Get("error").IsObject()) {
    const std::string message = root.Get("error").IsString()
                                    ? root.Get("error").AsString()
                                    : root.Get("error").Get("message").AsString();
    return fail(message.empty() ? "The provider reported an error." : message);
  }

  std::string answer;
  if (provider == AiProvider::kOllama) {
    answer = root.Get("message").Get("content").AsString();
    if (answer.empty() && root.Get("response").IsString()) {
      answer = root.Get("response").AsString();  // /api/generate shape, as a courtesy
    }
  } else {
    const json::Value& choices = root.Get("choices");
    answer = choices.At(0).Get("message").Get("content").AsString();
    if (answer.empty()) answer = choices.At(0).Get("text").AsString();
    // Some OpenAI-compatible servers stream by default even when asked not to; a chunk
    // array has "delta" instead of "message". Say so plainly rather than show "".
    if (answer.empty() && choices.At(0).Get("delta").IsObject()) {
      answer = choices.At(0).Get("delta").Get("content").AsString();
    }
  }
  if (fmt::Trim(answer).empty()) {
    return fail("The provider answered, but not with any text (check that the model name "
                "exists and that the server is a chat, not a completion, endpoint).");
  }
  if (text != nullptr) *text = answer;
  return true;
}

std::vector<std::string> ParseModelList(AiProvider provider, std::string_view body) {
  std::vector<std::string> models;
  std::string error;
  const std::optional<json::Value> parsed = json::Parse(body, &error);
  if (!parsed.has_value()) return models;
  const json::Value& root = *parsed;
  if (provider == AiProvider::kOllama) {
    for (const json::Value& entry : root.Get("models").AsArray()) {
      const std::string name = entry.Get("name").AsString();
      if (!name.empty()) models.push_back(name);
    }
  } else {
    for (const json::Value& entry : root.Get("data").AsArray()) {
      const std::string id = entry.Get("id").AsString();
      if (!id.empty()) models.push_back(id);
    }
    // Some local hosts answer /v1/models with a bare array.
    if (models.empty() && root.IsArray()) {
      for (const json::Value& entry : root.AsArray()) {
        const std::string id = entry.IsString() ? entry.AsString() : entry.Get("id").AsString();
        if (!id.empty()) models.push_back(id);
      }
    }
  }
  std::sort(models.begin(), models.end());
  models.erase(std::unique(models.begin(), models.end()), models.end());
  return models;
}

std::string DescribeHttpFailure(AiProvider provider, int status, std::string_view body) {
  std::string meaning;
  if (status == 401 || status == 403) {
    meaning = "The provider rejected the API key. Check the key, or whether it is allowed "
              "to use this model.";
  } else if (status == 404) {
    meaning = std::string("Nothing is served at that path on ") +
              AiProviderLabel(provider) + ". Check the endpoint (it usually ends in /v1) "
              "and the model name.";
  } else if (status == 429) {
    meaning = "The provider says you are sending too much (rate limit or quota).";
  } else if (status >= 500) {
    meaning = "The provider failed on its side. The same request may work in a minute.";
  } else if (status == 400) {
    meaning = "The provider did not accept the request. The body may name the offending "
              "field.";
  } else {
    meaning = "The provider answered with an error.";
  }
  std::string out = "HTTP " + std::to_string(status) + " — " + meaning;
  const std::string snippet = FlattenSnippet(body, 240);
  if (!snippet.empty()) out += " Provider said: " + snippet;
  return out;
}

std::string DescribeTransportFailure(AiProvider provider, int net_error,
                                     std::string_view endpoint) {
  const NetErrorHelp help = ExplainNetError(net_error);
  std::string out = "Could not reach " + std::string(endpoint);
  if (!help.name.empty()) out += " (" + help.name + ")";
  if (!help.headline.empty()) out += ". " + help.headline + ".";
  if (provider != AiProvider::kCloud) {
    out += " For a local provider this usually means the server is not running: start " +
           std::string(AiProviderLabel(provider)) + " and try again.";
  }
  if (net_error == -102) {  // ERR_CONNECTION_REFUSED, worth naming explicitly
    out += " Nothing is listening on that port.";
  }
  return out;
}

int EstimateTokens(std::string_view text) {
  if (text.empty()) return 0;
  return static_cast<int>((text.size() + 3) / 4);
}

std::string ClampText(std::string_view text, size_t max_chars) {
  if (text.size() <= max_chars) return std::string(text);
  if (max_chars == 0) return std::string();
  // Cut on a word boundary when there is one near the end; never mid-UTF-8 sequence.
  size_t cut = max_chars;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
  const size_t space = text.rfind(' ', cut);
  if (space != std::string_view::npos && space > max_chars / 2) cut = space;
  return std::string(text.substr(0, cut)) + "…";
}

std::string BuildSummaryPrompt(std::string_view title, std::string_view url,
                               std::string_view page_text, int max_context_chars) {
  const std::string trimmed = fmt::Trim(page_text);
  const size_t cap = max_context_chars > 0 ? static_cast<size_t>(max_context_chars) : 4000;
  const std::string body = ClampText(trimmed, cap);
  std::string out;
  out += "Summarise the web page below.\n\n";
  out += "Rules:\n";
  out += "- Use only the text provided. If it does not answer something, say so instead of "
         "guessing.\n";
  out += "- Five bullets at most, then one line starting \"In short:\".\n";
  out += "- No preamble, no restating these rules.\n\n";
  out += "Title: " + std::string(title.empty() ? "(none)" : title) + "\n";
  out += "Address: " + RedactUrl(url) + "\n";
  if (trimmed.size() > body.size()) {
    out += "Note: the text was truncated at " + std::to_string(cap) + " characters.\n";
  }
  out += "\n--- page text begins ---\n" + body + "\n--- page text ends ---\n";
  return out;
}

const char* AiSystemPrompt() {
  return "You are the assistant built into BrowserX, a performance browser. Answer in "
         "plain language and keep it short. If the material you were given does not "
         "contain the answer, say that plainly rather than filling the gap. Never claim "
         "to have read a page that was not included in the request.";
}

const char* AiAnswerDisclaimer() {
  return "Generated text, not a source. Check anything that matters against the page it "
         "came from.";
}

std::vector<std::string> AiPrivacyNotes() {
  return {
      "No request is made until you ask a question or press Test.",
      "The question goes to the provider selected above, and nowhere else. BrowserX has "
      "no AI server of its own.",
      "Local providers (Cortex LLMHoster, Ollama) keep the prompt on this machine. A cloud "
      "provider receives it, and its own policy applies.",
      "The visible text of the page you are viewing is read into this browser's memory so "
      "it can be summarised on request. It is not sent anywhere until you press Ask, and "
      "never for a browserx:// page.",
      "A cloud API key is stored in this profile's settings.json in plain text. Anyone who "
      "can read that file can read the key; use a key limited to what you need.",
      "Answers are logged nowhere by BrowserX. The provider's logs are the provider's "
      "business, not something this browser can promise.",
  };
}

std::string KeyFingerprint(std::string_view key) {
  const std::string trimmed = fmt::Trim(key);
  if (trimmed.empty()) return std::string();
  if (trimmed.size() <= 4) return "set (too short to show)";
  return "…" + trimmed.substr(trimmed.size() - 4);
}

std::string RedactUrl(std::string_view url) {
  std::string out(url);
  const size_t question = out.find('?');
  if (question != std::string::npos) out = out.substr(0, question);
  const size_t fragment = out.find('#');
  if (fragment != std::string::npos) out = out.substr(0, fragment);
  return out;
}

const char* PageTextScript() {
  // One expression, guarded against double injection, capped before it leaves the page.
  // It reports through the console channel the browser already reads, like the CAPTCHA
  // detector, so no new IPC surface is added for it.
  return R"JS((function () {
  try {
    if (window.__browserxPageText) return "already asked";
    window.__browserxPageText = true;
    var main = document.querySelector("main, article, [role=main]") || document.body;
    var text = (main && main.innerText ? main.innerText : "").replace(/\s+/g, " ").trim();
    if (text.length > 4000) text = text.slice(0, 4000) + "…";
    console.info("browserx-page-text:" + text);
    return "sent";
  } catch (e) {
    return "failed";
  }
})())JS";
}

bool ParsePageTextMessage(const std::string& message, std::string* text) {
  constexpr char kMarker[] = "browserx-page-text:";
  const size_t at = message.find(kMarker);
  if (at == std::string::npos) return false;
  if (text != nullptr) *text = message.substr(at + sizeof(kMarker) - 1);
  return true;
}

}  // namespace core
}  // namespace browserx
