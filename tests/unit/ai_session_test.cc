// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for the AI session: what it sends, to where, and what it refuses to send.
//
// The positive tests prove the three providers' requests are well formed. The negative
// ones are the point of the file: no prompt leaves the machine without a key and a
// validated endpoint, page text is attached only when asked for, and a query string is
// not part of the prompt.

#include "app/ai_session.h"

#include "core/json.h"
#include "test_harness.h"

namespace {

using browserx::app::AiSession;
using browserx::app::HttpResponse;
using browserx::core::AiProvider;

AiSession OllamaSession() {
  AiSession session;
  browserx::core::Settings settings = browserx::core::Settings::Defaults();
  settings.ai_provider = "ollama";
  session.LoadFromSettings(settings);
  return session;
}

AiSession CloudSession() {
  AiSession session;
  browserx::core::Settings settings = browserx::core::Settings::Defaults();
  settings.ai_provider = "cloud";
  settings.ai_api_key = "sk-live-abcdefghijkl";
  session.LoadFromSettings(settings);
  return session;
}

HttpResponse JsonResponse(int status, const std::string& body) {
  HttpResponse response;
  response.ok = true;
  response.status = status;
  response.body = body;
  return response;
}

}  // namespace

TEST(AiSession, DefaultsResolveToConcreteEndpoints) {
  AiSession session = OllamaSession();
  EXPECT_EQ(session.config().endpoint, "http://127.0.0.1:11434");
  EXPECT_EQ(session.Problem(), "");
  const auto request = session.TestRequest();
  EXPECT_EQ(request.url, "http://127.0.0.1:11434/api/tags");
  EXPECT_EQ(request.method, "GET");
}

TEST(AiSession, CloudWithoutAKeyReportsAProblemInsteadOfSending) {
  AiSession session;
  browserx::core::Settings settings = browserx::core::Settings::Defaults();
  settings.ai_provider = "cloud";
  session.LoadFromSettings(settings);  // no key
  EXPECT_TRUE(session.Problem().find("API key") != std::string::npos) << session.Problem();
  browserx::app::HttpRequest request;
  std::string error;
  EXPECT_FALSE(session.BuildRequest("hello", false, false, &request, &error));
  EXPECT_TRUE(error.find("API key") != std::string::npos) << error;
}

TEST(AiSession, BuildingARequestUsesTheProviderShapeAndTheKey) {
  AiSession session = CloudSession();
  browserx::app::HttpRequest request;
  std::string error;
  EXPECT_TRUE(session.BuildRequest("What is 2+2?", false, false, &request, &error)) << error;
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.url, "https://api.openai.com/v1/chat/completions");
  bool saw_auth = false;
  for (const auto& header : request.headers) {
    if (header.name == "Authorization") {
      saw_auth = true;
      EXPECT_TRUE(header.value.find("sk-live-") != std::string::npos);
    }
  }
  EXPECT_TRUE(saw_auth);
  const auto parsed = browserx::core::json::Parse(request.body);
  EXPECT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->Get("messages").At(1).Get("content").AsString(), "What is 2+2?");
}

TEST(AiSession, AnEmptyQuestionIsRefusedRatherThanSentAsAnEmptyPrompt) {
  AiSession session = OllamaSession();
  browserx::app::HttpRequest request;
  std::string error;
  EXPECT_FALSE(session.BuildRequest("   ", false, false, &request, &error));
  EXPECT_TRUE(error.find("question") != std::string::npos) << error;
}

TEST(AiSession, PageTextIsOnlyAttachedWhenItIsWanted) {
  AiSession session = OllamaSession();
  session.SetPage("https://example.com/article?token=secret", "Article",
                  {"first paragraph", "second paragraph"});
  EXPECT_TRUE(session.has_page_text());

  browserx::app::HttpRequest plain;
  std::string error;
  ASSERT_TRUE(session.BuildRequest("what is this about?", false, false, &plain, &error));
  EXPECT_TRUE(plain.body.find("first paragraph") == std::string::npos);

  browserx::app::HttpRequest with_text;
  ASSERT_TRUE(session.BuildRequest("what is this about?", true, false, &with_text, &error));
  EXPECT_TRUE(with_text.body.find("first paragraph") != std::string::npos);
  EXPECT_TRUE(with_text.body.find("token=secret") == std::string::npos)
      << "the URL goes into the prompt; its query string must not";
}

TEST(AiSession, SummariseWithoutAPageSaysSoInsteadOfSendingAnEmptyDocument) {
  AiSession session = OllamaSession();
  browserx::app::HttpRequest request;
  std::string error;
  EXPECT_FALSE(session.BuildRequest("", false, true, &request, &error));
  EXPECT_TRUE(error.find("no page text") != std::string::npos) << error;
}

TEST(AiSession, SummariseUsesThePromptThePageCanShow) {
  AiSession session = OllamaSession();
  session.SetPage("https://example.com/a", "A", {"Alpha beta gamma."});
  browserx::app::HttpRequest request;
  std::string error;
  ASSERT_TRUE(session.BuildRequest("", false, true, &request, &error));
  const auto parsed = browserx::core::json::Parse(request.body);
  ASSERT_TRUE(parsed.has_value());
  const std::string content = parsed->Get("messages").At(1).Get("content").AsString();
  EXPECT_EQ(content, session.SummaryPrompt());
  EXPECT_TRUE(content.find("Alpha beta gamma.") != std::string::npos);
}

TEST(AiSession, SwitchingProviderInstallsDefaultsAndKeepsOnlyWhatMakesSense) {
  AiSession session = CloudSession();
  std::string error;
  ASSERT_TRUE(session.SetProvider(AiProvider::kOllama, &error));
  EXPECT_EQ(session.config().endpoint, "http://127.0.0.1:11434");
  EXPECT_FALSE(session.config().api_key.empty())
      << "the key is kept, so switching back does not need it pasted again";

  ASSERT_TRUE(session.SetProvider(AiProvider::kCortex, &error));
  EXPECT_EQ(session.config().endpoint, "http://127.0.0.1:8000/v1");

  ASSERT_TRUE(session.SetProvider(AiProvider::kCloud, &error));
  EXPECT_EQ(session.config().endpoint, "https://api.openai.com/v1");
  EXPECT_FALSE(session.config().model.empty()) << "a named default beats an empty box";
}

TEST(AiSession, AnInvalidConfigIsRejectedAndTheOldOneSurvives) {
  AiSession session = OllamaSession();
  browserx::core::AiConfig bad = session.config();
  bad.endpoint = "http://example.com:11434";  // clear text off the machine
  std::string error;
  EXPECT_FALSE(session.SetConfig(bad, &error));
  EXPECT_TRUE(error.find("clear text") != std::string::npos) << error;
  EXPECT_EQ(session.config().endpoint, "http://127.0.0.1:11434");
}

TEST(AiSession, FinishingATurnParsesBothShapesAndRecordsAFailureAsOne) {
  AiSession session = OllamaSession();
  const auto ok = session.Complete(
      "hi", false, JsonResponse(200, R"({"message":{"content":"hello"}})"));
  EXPECT_EQ(ok.answer, "hello");
  EXPECT_EQ(ok.provider, "ollama");
  EXPECT_EQ(ok.error, "");
  EXPECT_GT(ok.answer_tokens_estimate, 0);

  HttpResponse refused;
  refused.ok = false;
  refused.error = "could not connect to 127.0.0.1:11434 (Connection refused)";
  refused.net_error = -2;
  const auto failed = session.Complete("again", false, refused);
  EXPECT_TRUE(failed.answer.empty());
  EXPECT_TRUE(failed.error.find("refused") != std::string::npos);
  EXPECT_EQ(session.turns().size(), 2u);
}

TEST(AiSession, AnHttpErrorStatusBecomesAReadableTurn) {
  AiSession session = CloudSession();
  const auto turn = session.Complete(
      "hi", false, JsonResponse(401, R"({"error":{"message":"invalid api key"}})"));
  EXPECT_TRUE(turn.answer.empty());
  EXPECT_TRUE(turn.error.find("401") != std::string::npos) << turn.error;
  EXPECT_TRUE(turn.error.find("API key") != std::string::npos) << turn.error;
}

TEST(AiSession, ModelsComeFromTheTestResponseAndNothingIsInvented) {
  AiSession session = OllamaSession();
  const auto models = session.ModelsFromTest(
      JsonResponse(200, R"({"models":[{"name":"llama3.2"},{"name":"qwen2.5"}]})"));
  EXPECT_EQ(models.size(), 2u);
  EXPECT_TRUE(session.ModelsFromTest(JsonResponse(500, "{}")).empty())
      << "an error response must not look like a model list";
}

TEST(AiSession, StatusLinesNeverPrintTheKey) {
  AiSession session = CloudSession();
  const auto lines = session.StatusLines();
  std::string joined;
  for (const auto& line : lines) joined += line + "\n";
  EXPECT_TRUE(joined.find("sk-live-abcdefghijkl") == std::string::npos)
      << "the full key must never be rendered";
  EXPECT_TRUE(joined.find("API key: ") != std::string::npos) << joined;
  EXPECT_TRUE(joined.find("abcdefghij") == std::string::npos) << joined;
  EXPECT_TRUE(joined.find("plain text") != std::string::npos);
}

TEST(AiSession, TheRequestViewRedactsTheKeyButShowsEverythingElse) {
  AiSession session = CloudSession();
  browserx::app::HttpRequest request;
  std::string error;
  ASSERT_TRUE(session.BuildRequest("hello", false, false, &request, &error));
  const std::string view = session.DescribeRequest(request);
  EXPECT_TRUE(view.find("sk-live-abcdefghijkl") == std::string::npos);
  EXPECT_TRUE(view.find("redacted") != std::string::npos);
  EXPECT_TRUE(view.find("https://api.openai.com/v1/chat/completions") != std::string::npos);
  EXPECT_TRUE(view.find("hello") != std::string::npos);
}

TEST(AiSession, SettingsRoundTripWithoutStoringTheDefaultEndpoint) {
  AiSession session = OllamaSession();
  browserx::core::Settings saved = browserx::core::Settings::Defaults();
  session.SaveToSettings(&saved);
  EXPECT_EQ(saved.ai_provider, "ollama");
  EXPECT_EQ(saved.ai_endpoint, "") << "a default typed or not, it must stay a default";
  AiSession reloaded;
  reloaded.LoadFromSettings(saved);
  EXPECT_EQ(reloaded.config().endpoint, "http://127.0.0.1:11434");
}

TEST(AiSession, LocalClockIsFourDigitsAndAColon) {
  const std::string clock = browserx::app::LocalClock();
  EXPECT_EQ(clock.size(), 5u);
  EXPECT_EQ(clock[2], ':');
}
