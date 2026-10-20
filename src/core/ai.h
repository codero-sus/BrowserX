// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — built-in AI: local first, cloud only if you ask, and nothing hidden.
//
// Three providers, one shape:
//
//   * Cortex LLMHoster (https://github.com/codero-sus/Cortex_LLMHoster) — codero-sus's own
//     local LLM hoster. It publishes no wire format yet (one commit, description only), so
//     BrowserX talks to it through the OpenAI-compatible chat-completions shape that local
//     hosts almost universally expose, at an endpoint the user sets. When its API appears,
//     only `ChatUrl()`/`ParseChatResponse()` need to change.
//   * Ollama — the machine-local runner people actually have installed. Its native
//     `/api/chat` and `/api/tags` are used, because that is what it documents.
//   * Cloud — any OpenAI-compatible endpoint (OpenAI itself, OpenRouter, Groq, …), which is
//     the only one of the three that leaves the machine. Therefore it requires an explicit
//     key and an https endpoint, and the page says so before the first request.
//
// What this module is *not*: a silent assistant. It builds requests, parses answers and
// explains failures; it never sends anything until a person asks a question, never sends
// page text unless the person ticked that box, and never stores a key anywhere except the
// profile's settings.json (plain text — stated, not implied).
//
// The module is CEF-free on purpose: the wire format is testable without a browser, and
// the network layer (src/browser/ai_controller.cc) only has to move bytes.

#ifndef BROWSERX_CORE_AI_H_
#define BROWSERX_CORE_AI_H_

#include <string>
#include <vector>

#include "core/json.h"

namespace browserx {
namespace core {

enum class AiProvider {
  kCortex = 0,  // Cortex_LLMHoster, local, OpenAI-compatible
  kOllama,      // Ollama, local, native /api/chat
  kCloud,       // any OpenAI-compatible endpoint that needs a key
};

const char* AiProviderId(AiProvider provider);       // "cortex" | "ollama" | "cloud"
const char* AiProviderLabel(AiProvider provider);    // "Cortex LLMHoster" | …
/// One sentence: where the model runs, and therefore who can see the prompt.
const char* AiProviderLocation(AiProvider provider);
/// Parses an id; false for anything unknown (never a silent default — a typo that
/// quietly switched to cloud would send text off the machine).
bool ParseAiProvider(std::string_view id, AiProvider* provider);

struct AiConfig {
  AiProvider provider = AiProvider::kOllama;
  /// Base URL without a trailing slash. http:// only for loopback; https:// otherwise.
  std::string endpoint;
  /// Empty means "let the provider pick its default model", which is what a fresh
  /// install has. The UI shows the resolved name once /models answers.
  std::string model;
  /// Cloud key. Never printed, never logged; the UI shows a fingerprint only.
  std::string api_key;
  double temperature = 0.2;
  int max_tokens = 512;
  /// Whether "summarise this page" may include the page's visible text.
  bool send_page_context = false;
  /// Hard cap on context characters, so a giant page cannot become a giant bill.
  int max_context_chars = 4000;
};

/// The endpoint/model defaults for a provider. Cloud has no key, so it is not usable
/// until one is set — which `ValidateConfig` reports rather than pretending.
AiConfig AiDefaults(AiProvider provider);

/// Checks scheme, loopback rule, key presence and ranges. Returns false with a sentence
/// the UI can show verbatim.
bool ValidateConfig(const AiConfig& config, std::string* error);

/// True when the endpoint's host is loopback (127.0.0.0/8, ::1, localhost).
bool IsLoopbackEndpoint(std::string_view url);

/// `{base}/chat/completions` (OpenAI shape) or `{base}/api/chat` (Ollama).
std::string ChatUrl(const AiConfig& config);
/// `{base}/models` or `{base}/api/tags`.
std::string ModelsUrl(const AiConfig& config);

/// The request body. Deterministic key order, no timestamps, so a test can compare it.
json::Value BuildChatRequest(const AiConfig& config, std::string_view system_prompt,
                             std::string_view user_prompt);

/// Extracts the assistant text. Returns false with `error` when the body is not an
/// answer this provider could have produced (an HTML error page, a truncated stream, a
/// JSON shape from a different API).
bool ParseChatResponse(AiProvider provider, std::string_view body, std::string* text,
                       std::string* error);

/// Model names from a /models or /api/tags answer, sorted and de-duplicated.
std::vector<std::string> ParseModelList(AiProvider provider, std::string_view body);

/// A short, sanitised explanation of an HTTP failure: status, what it usually means for
/// this provider, and at most a snippet of the body (which may be HTML — it is flattened).
std::string DescribeHttpFailure(AiProvider provider, int status, std::string_view body);

/// A transport-level failure (connection refused, DNS, timeout) from a Chromium net
/// error code. The caller supplies the human wording for the endpoint it tried.
std::string DescribeTransportFailure(AiProvider provider, int net_error,
                                     std::string_view endpoint);

/// Approximate token count: characters/4, rounded up. An estimate, and labelled as one
/// wherever it is shown — a guessed number presented as a measured one is a lie.
int EstimateTokens(std::string_view text);

/// Collapses whitespace and caps length on a word boundary; appends "…" when truncated.
std::string ClampText(std::string_view text, size_t max_chars);

/// The prompt used by "Summarise this page". Bounded, and explicit that the model must
/// not invent what the page does not say.
std::string BuildSummaryPrompt(std::string_view title, std::string_view url,
                               std::string_view page_text, int max_context_chars);

/// The system prompt for every request: short answers, admit uncertainty, no pretending
/// to have read anything that was not sent.
const char* AiSystemPrompt();

/// Shown above every answer, because a generated sentence is not a source.
const char* AiAnswerDisclaimer();

/// What leaves the machine, stated as facts (one per line) rather than as reassurance.
std::vector<std::string> AiPrivacyNotes();

/// `sk-…abcd` style: enough to recognise a key, not enough to use it. Empty stays empty.
std::string KeyFingerprint(std::string_view key);

/// Hides query and fragment of a URL before it is shown on an AI page: a query string
/// routinely carries session tokens, and the AI page is a page like any other.
std::string RedactUrl(std::string_view url);

/// The script injected to collect the page's visible text (read-only). Reports over the
/// console channel as `browserx-page-text:<text>`.
const char* PageTextScript();
/// Parses a console message produced by PageTextScript. Empty text when the marker is
/// absent. The caller pairs it with the URL it asked about.
bool ParsePageTextMessage(const std::string& message, std::string* text);

/// The Cortex_LLMHoster repository, for the page and for the credits.
constexpr char kCortexRepo[] = "https://github.com/codero-sus/Cortex_LLMHoster";
constexpr char kAuthorSite[] = "https://the-codero.web.app";

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_AI_H_
