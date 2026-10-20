// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/ai_controller.h"

#include <utility>

namespace browserx {
namespace app {

AiController::AiController() = default;

AiController::~AiController() { JoinWorker(); }

void AiController::JoinWorker() {
  if (worker_.joinable()) worker_.join();
}

void AiController::LoadFromSettings(const core::Settings& settings) {
  session_.LoadFromSettings(settings);
}

void AiController::SaveToSettings(core::Settings* settings) const {
  session_.SaveToSettings(settings);
}

void AiController::StartWorker(HttpRequest request, bool is_test, std::string question,
                               bool used_page_text) {
  JoinWorker();  // the previous worker has finished; joining it is instant
  busy_.store(true);
  worker_ = std::thread([this, request = std::move(request), is_test,
                         question = std::move(question), used_page_text]() {
    Finished finished;
    finished.is_test = is_test;
    finished.question = question;
    finished.used_page_text = used_page_text;
    finished.response = HttpFetch(request);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      result_ = std::move(finished);
      has_result_ = true;
    }
    busy_.store(false);
  });
}

bool AiController::Ask(const std::string& question, bool include_page_text, bool summarize,
                       std::string* error) {
  if (busy()) {
    if (error != nullptr) *error = "Still waiting for the previous answer.";
    return false;
  }
  HttpRequest request;
  std::string local_error;
  if (!session_.BuildRequest(question, include_page_text, summarize, &request, &local_error)) {
    if (error != nullptr) *error = local_error;
    return false;
  }
  StartWorker(std::move(request), false, question, include_page_text || summarize);
  return true;
}

bool AiController::Test(std::string* error) {
  if (busy()) {
    if (error != nullptr) *error = "Still waiting for the previous request.";
    return false;
  }
  if (const std::string problem = session_.Problem(); !problem.empty()) {
    if (error != nullptr) *error = problem;
    return false;
  }
  StartWorker(session_.TestRequest(), true, std::string(), false);
  return true;
}

bool AiController::Pump() {
  Finished finished;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_result_) return false;
    finished = std::move(result_);
    result_ = Finished{};
    has_result_ = false;
  }
  if (finished.is_test) {
    tested_ = true;
    models_ = session_.ModelsFromTest(finished.response);
    if (!finished.response.ok) {
      test_note_ = "failed: " + finished.response.error;
    } else if (finished.response.status != 200) {
      test_note_ = core::DescribeHttpFailure(session_.config().provider,
                                            finished.response.status, finished.response.body);
    } else if (models_.empty()) {
      test_note_ = "the server answered but listed no models";
    } else {
      test_note_ = std::to_string(models_.size()) + " model" +
                   (models_.size() == 1 ? "" : "s") + " available";
    }
    return true;
  }
  session_.Complete(finished.question, finished.used_page_text, finished.response);
  return true;
}

AiState AiController::Snapshot() const {
  AiState state;
  state.busy = busy_.load();
  state.problem = session_.Problem();
  state.tested = tested_;
  state.test_note = test_note_;
  state.models = models_;
  state.turns = session_.turns();
  state.page_title = session_.page_title();
  state.page_url = session_.page_url();
  state.has_page_text = session_.has_page_text();
  state.context_chars = session_.config().max_context_chars;
  return state;
}

void AiController::ClearTurns() { session_.ClearTurns(); }

}  // namespace app
}  // namespace browserx
