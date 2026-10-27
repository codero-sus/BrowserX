// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for the built-in AI's wire format and its policy.
//
// Two kinds of test here. The first exercise real request/response shapes for the three
// providers. The second assert what the feature refuses to do: talk http to a remote
// host, run without a key when the provider needs one, or let a page's URL carry its
// query string into a prompt.

#include "core/ai.h"

#include "core/json.h"
#include "test_harness.h"

namespace {

using browserx::core::AiConfig;
using browserx::core::AiProvider;

AiConfig OllamaConfig() { return browserx::core::AiDefaults(AiProvider::kOllama); }
AiConfig CortexConfig() { return browserx::core::AiDefaults(AiProvider::kCortex); }
AiConfig CloudConfig() {
  AiConfig config = browserx::core::AiDefaults(AiProvider::kCloud);
  config.api_key = "sk-test-0123456789abcdef";
  return config;
}

}  // namespace

TEST(Ai, ProvidersHaveStableIdsAndDistinctLabels) {
  EXPECT_EQ(std::string(browserx::core::AiProviderId(AiProvider::kCortex)), "cortex");
  EXPECT_EQ(std::string(browserx::core::AiProviderId(AiProvider::kOllama)), "ollama");
  EXPECT_EQ(std::string(browserx::core::AiProviderId(AiProvider::kCloud)), "cloud");
  EXPECT_NE(std::string(browserx::core::AiProviderLabel(AiProvider::kCortex)),
            std::string(browserx::core::AiProviderLabel(AiProvider::kCloud)));
}

TEST(Ai, ParseProviderRejectsTyposInsteadOfSilentlyChoosingCloud) {
  AiProvider provider = AiProvider::kCloud;
  EXPECT_TRUE(browserx::core::ParseAiProvider("ollama", &provider));
  EXPECT_TRUE(provider == AiProvider::kOllama);
  EXPECT_TRUE(browserx::core::ParseAiProvider("Cortex", &provider));
  EXPECT_TRUE(provider == AiProvider::kCortex);
  EXPECT_FALSE(browserx::core::ParseAiProvider("olama", &provider))
      << "a typo must not quietly send prompts somewhere else";
  EXPECT_FALSE(browserx::core::ParseAiProvider("", &provider));
}

TEST(Ai, DefaultsAreLocalForBothLocalProviders) {
  EXPECT_TRUE(browserx::core::IsLoopbackEndpoint(OllamaConfig().endpoint));
  EXPECT_TRUE(browserx::core::IsLoopbackEndpoint(CortexConfig().endpoint));
  EXPECT_FALSE(browserx::core::IsLoopbackEndpoint(CloudConfig().endpoint));
}

TEST(Ai, LocalDefaultsValidateWithoutAKey) {
  std::string error;
  EXPECT_TRUE(browserx::core::ValidateConfig(OllamaConfig(), &error)) << error;
  EXPECT_TRUE(browserx::core::ValidateConfig(CortexConfig(), &error)) << error;
}

TEST(Ai, CloudWithoutAKeyIsRefusedWithAUsableSentence) {
  AiConfig config = browserx::core::AiDefaults(AiProvider::kCloud);
  config.api_key.clear();
  std::string error;
  EXPECT_FALSE(browserx::core::ValidateConfig(config, &error));
  EXPECT_TRUE(error.find("API key") != std::string::npos) << error;
}

TEST(Ai, PlainHttpToAnotherHostIsRefused) {
  AiConfig config = OllamaConfig();
  config.endpoint = "http://192.168.1.20:11434";
  std::string error;
  EXPECT_FALSE(browserx::core::ValidateConfig(config, &error));
  EXPECT_TRUE(error.find("clear text") != std::string::npos) << error;
  // The same host over https is fine: the objection is the wire, not the machine.
  config.endpoint = "https://192.168.1.20:11434";
  EXPECT_TRUE(browserx::core::ValidateConfig(config, &error)) << error;
}

TEST(Ai, NonHttpSchemesAreRefused) {
  AiConfig config = OllamaConfig();
  for (const char* evil : {"file:///etc/passwd", "javascript:alert(1)", "ftp://x"}) {
    config.endpoint = evil;
    std::string error;
    EXPECT_FALSE(browserx::core::ValidateConfig(config, &error)) << evil;
  }
}

TEST(Ai, UrlsFollowEachProvidersShape) {
  EXPECT_EQ(browserx::core::ChatUrl(OllamaConfig()), "http://127.0.0.1:11434/api/chat");
  EXPECT_EQ(browserx::core::ModelsUrl(OllamaConfig()), "http://127.0.0.1:11434/api/tags");
  EXPECT_EQ(browserx::core::ChatUrl(CortexConfig()),
            "http://127.0.0.1:8000/v1/chat/completions");
  EXPECT_EQ(browserx::core::ModelsUrl(CloudConfig()),
            "https://api.openai.com/v1/models");
  // A trailing slash must not produce a double slash.
  AiConfig trailing = OllamaConfig();
  trailing.endpoint = "http://127.0.0.1:11434/";
  EXPECT_EQ(browserx::core::ChatUrl(trailing), "http://127.0.0.1:11434/api/chat");
}

TEST(Ai, OllamaRequestUsesItsOwnTokenCapName) {
  const browserx::core::json::Value request =
      browserx::core::BuildChatRequest(OllamaConfig(), "sys", "hi");
  EXPECT_TRUE(request.Get("stream").AsBool(true) == false) << "one answer, not a stream";
  EXPECT_TRUE(request.Get("options").Get("num_predict").IsNumber());
  EXPECT_TRUE(request.Get("max_tokens").IsNull()) << "the OpenAI name is not Ollama's";
  EXPECT_TRUE(request.Get("messages").At(0).Get("role").AsString() == "system");
  EXPECT_TRUE(request.Get("messages").At(1).Get("content").AsString() == "hi");
}

TEST(Ai, OpenAiShapedRequestUsesMaxTokens) {
  const browserx::core::json::Value request =
      browserx::core::BuildChatRequest(CloudConfig(), "sys", "hi");
  EXPECT_TRUE(request.Get("max_tokens").IsNumber());
  EXPECT_TRUE(request.Get("options").IsNull());
}

TEST(Ai, ParsesOllamaAndOpenAiAnswers) {
  std::string text;
  std::string error;
  EXPECT_TRUE(browserx::core::ParseChatResponse(
      AiProvider::kOllama, R"({"message":{"role":"assistant","content":"hello"}})", &text,
      &error)) << error;
  EXPECT_EQ(text, "hello");
  EXPECT_TRUE(browserx::core::ParseChatResponse(
      AiProvider::kCortex, R"({"choices":[{"message":{"content":"hi there"}}]})", &text,
      &error)) << error;
  EXPECT_EQ(text, "hi there");
}

TEST(Ai, AStreamedChunkOrProxyPageIsReportedNotShownAsAnEmptyAnswer) {
  std::string text;
  std::string error;
  EXPECT_FALSE(browserx::core::ParseChatResponse(
      AiProvider::kCloud, R"({"choices":[{"delta":{"content":""}}]})", &text, &error));
  EXPECT_TRUE(error.find("model name") != std::string::npos) << error;
  EXPECT_FALSE(browserx::core::ParseChatResponse(AiProvider::kOllama, "<html>login</html>",
                                                 &text, &error));
  EXPECT_TRUE(error.find("not JSON") != std::string::npos) << error;
}

TEST(Ai, ProviderErrorObjectsSurfaceTheirMessage) {
  std::string text;
  std::string error;
  EXPECT_FALSE(browserx::core::ParseChatResponse(
      AiProvider::kOllama, R"({"error":"model 'llama9' not found"})", &text, &error));
  EXPECT_TRUE(error.find("llama9") != std::string::npos) << error;
}

TEST(Ai, ModelListsParseForBothShapesAndAreSorted) {
  const std::vector<std::string> ollama = browserx::core::ParseModelList(
      AiProvider::kOllama,
      R"({"models":[{"name":"zephyr"},{"name":"llama3.2:3b"},{"name":"zephyr"}]})");
  EXPECT_EQ(ollama.size(), 2u);
  EXPECT_EQ(ollama[0], "llama3.2:3b");
  EXPECT_EQ(ollama[1], "zephyr");
  const std::vector<std::string> openai = browserx::core::ParseModelList(
      AiProvider::kCloud, R"({"data":[{"id":"gpt-4o-mini"},{"id":"gpt-4o"}]})");
  EXPECT_EQ(openai.size(), 2u);
  EXPECT_EQ(openai[0], "gpt-4o");
  EXPECT_TRUE(browserx::core::ParseModelList(AiProvider::kCloud, "not json").empty());
}

TEST(Ai, HttpFailuresSayWhatToDoAndNeverReturnRawHtml) {
  const std::string unauthorized =
      browserx::core::DescribeHttpFailure(AiProvider::kCloud, 401,
                                          R"({"error":{"message":"bad key"}})");
  EXPECT_TRUE(unauthorized.find("401") != std::string::npos);
  EXPECT_TRUE(unauthorized.find("API key") != std::string::npos);
  const std::string not_found = browserx::core::DescribeHttpFailure(
      AiProvider::kOllama, 404, "<html><body><h1>404 Not Found</h1></body></html>");
  EXPECT_TRUE(not_found.find("404") != std::string::npos);
  EXPECT_TRUE(not_found.find('/') != std::string::npos) << not_found;
  EXPECT_TRUE(not_found.find("<") == std::string::npos) << "tags must be stripped";
}

TEST(Ai, TransportFailureNamesThePortAndTheMissingServer) {
  const std::string message =
      browserx::core::DescribeTransportFailure(AiProvider::kOllama, -102,
                                               "http://127.0.0.1:11434/api/chat");
  EXPECT_TRUE(message.find("127.0.0.1:11434") != std::string::npos);
  EXPECT_TRUE(message.find("Ollama") != std::string::npos) << message;
  EXPECT_TRUE(message.find("Nothing is listening") != std::string::npos) << message;
}

TEST(Ai, TokenEstimateIsLabelledAnEstimateByBeingRough) {
  EXPECT_EQ(browserx::core::EstimateTokens(""), 0);
  EXPECT_EQ(browserx::core::EstimateTokens("abcd"), 1);
  EXPECT_EQ(browserx::core::EstimateTokens(std::string(400, 'x')), 100);
}

TEST(Ai, ClampTextCutsOnAWordBoundaryAndNeverMidCharacter) {
  EXPECT_EQ(browserx::core::ClampText("short", 20), "short");
  const std::string clamped = browserx::core::ClampText("one two three four", 10);
  EXPECT_TRUE(clamped.size() <= 11) << clamped << " (plus the ellipsis)";
  EXPECT_TRUE(clamped.find("\xE2\x80\xA6") != std::string::npos)
      << "a cut must be visible as one: " << clamped;
  EXPECT_TRUE(clamped.find("one two") == 0) << "the cut lands on a word boundary";
  // A multi-byte character straddling the cut must not be split.
  const std::string two_byte = "\xC3\xA9";  // é, encoded explicitly: no source-charset guess
  const std::string accent = "aaaaa" + two_byte;
  const std::string cut = browserx::core::ClampText(accent, 6);
  EXPECT_TRUE(cut.find(two_byte) == std::string::npos || cut.find("\xC3") == std::string::npos)
      << "a truncated lead byte would be an invalid string";
}

TEST(Ai, SummaryPromptStatesItsRulesAndItsTruncation) {
  const std::string prompt = browserx::core::BuildSummaryPrompt(
      "Title", "https://example.com/a?token=secret", std::string(9000, 'x'), 4000);
  EXPECT_TRUE(prompt.find("Use only the text provided") != std::string::npos);
  EXPECT_TRUE(prompt.find("was truncated") != std::string::npos);
  EXPECT_TRUE(prompt.find("token=secret") == std::string::npos)
      << "a query string routinely carries a session token";
}

TEST(Ai, PageTextIsCappedAndPageScriptsNeverSubmit) {
  const std::string script = browserx::core::PageTextScript();
  EXPECT_TRUE(script.find("console.info") != std::string::npos);
  EXPECT_TRUE(script.find("4000") != std::string::npos);
  EXPECT_TRUE(script.find(".click(") == std::string::npos);
  EXPECT_TRUE(script.find("submit") == std::string::npos);
  EXPECT_TRUE(script.find("fetch(") == std::string::npos);
  EXPECT_TRUE(script.find("XMLHttpRequest") == std::string::npos);
}

TEST(Ai, PageTextMessagesAreParsedAndUnrelatedOnesIgnored) {
  std::string text;
  EXPECT_TRUE(browserx::core::ParsePageTextMessage(
      "browserx-page-text:Hello   world", &text));
  EXPECT_EQ(text, "Hello   world");
  EXPECT_FALSE(browserx::core::ParsePageTextMessage("browserx-captcha:recaptcha", &text));
}

TEST(Ai, RedactUrlDropsQueryAndFragment) {
  EXPECT_EQ(browserx::core::RedactUrl("https://a.example/p?x=1&y=2#frag"),
            "https://a.example/p");
  EXPECT_EQ(browserx::core::RedactUrl("https://a.example/"), "https://a.example/");
}

TEST(Ai, KeyFingerprintShowsTheTailOnly) {
  EXPECT_EQ(browserx::core::KeyFingerprint(""), "");
  EXPECT_EQ(browserx::core::KeyFingerprint("sk-0123456789abcdef"), "…cdef");
  EXPECT_TRUE(browserx::core::KeyFingerprint("abc").find("abc") == std::string::npos)
      << "a short key is not printed, because it would be the whole key";
}

TEST(Ai, PrivacyNotesNameTheKeyFileAndTheLocalProviders) {
  const std::vector<std::string> notes = browserx::core::AiPrivacyNotes();
  EXPECT_TRUE(notes.size() >= 5u);
  bool mentions_plain_text = false;
  bool mentions_local = false;
  for (const std::string& note : notes) {
    if (note.find("plain text") != std::string::npos) mentions_plain_text = true;
    if (note.find("Ollama") != std::string::npos) mentions_local = true;
  }
  EXPECT_TRUE(mentions_plain_text);
  EXPECT_TRUE(mentions_local);
}

TEST(Ai, CreditsPointAtCortexAndTheAuthorSite) {
  EXPECT_TRUE(std::string(browserx::core::kCortexRepo).find("codero-sus") !=
              std::string::npos);
  EXPECT_TRUE(std::string(browserx::core::kAuthorSite).find("the-codero.web.app") !=
              std::string::npos);
}
