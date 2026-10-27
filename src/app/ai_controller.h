// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the AI controller: one session, one request at a time, no blocking.
//
// A model answer can take two minutes. The scheme handler that serves browserx://ai runs
// on CEF's IO thread, which also serves every other request in the process; doing the HTTP
// call there would freeze the whole browser, not just the panel. So the work happens on a
// worker thread, and the UI thread collects the result by calling Pump() on its existing
// tick. No CEF type appears in this file, which also makes the state machine — busy,
// refused-while-busy, completed, failed — testable without a browser.

#ifndef BROWSERX_APP_AI_CONTROLLER_H_
#define BROWSERX_APP_AI_CONTROLLER_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/ai_session.h"
#include "app/http_client.h"

namespace browserx {
namespace app {

/// What the page polls for. Copied under the lock, never referenced.
struct AiState {
  bool busy = false;
  std::string busy_what;   // "answering" | "testing"
  std::string problem;     // config problem, if any
  bool tested = false;     // has a Test ever completed?
  std::string test_note;   // "5 models" | "failed: …"
  std::vector<std::string> models;
  std::vector<AiTurn> turns;
  std::string page_title;
  std::string page_url;
  bool has_page_text = false;
  int context_chars = 0;
};

class AiController {
 public:
  AiController();
  ~AiController();

  AiController(const AiController&) = delete;
  AiController& operator=(const AiController&) = delete;

  /// UI thread. Loads persisted config; safe to call once.
  void LoadFromSettings(const core::Settings& settings);
  void SaveToSettings(core::Settings* settings) const;

  /// UI thread. Starts a question. Returns false with `error` when busy or invalid, in
  /// which case nothing was sent and no thread was started.
  bool Ask(const std::string& question, bool include_page_text, bool summarize,
           std::string* error);
  /// UI thread. Starts a models query (no prompt, no page text).
  bool Test(std::string* error);

  /// UI thread, called from the tick. Applies a finished worker result, if any.
  /// Returns true when the visible state changed (the page should refresh).
  bool Pump();

  /// UI thread.
  AiState Snapshot() const;
  const AiSession& session() const { return session_; }
  AiSession& session() { return session_; }
  bool busy() const { return busy_.load(); }

  /// UI thread. Stops waiting for a worker. The worker itself is detached only at
  /// destruction; a request already on the wire cannot be recalled, and pretending
  /// otherwise would be the kind of dishonesty this feature is built against.
  void ClearTurns();

 private:
  struct Finished {
    bool is_test = false;
    std::string question;
    bool used_page_text = false;
    HttpResponse response;
  };

  void JoinWorker();
  void StartWorker(HttpRequest request, bool is_test, std::string question,
                   bool used_page_text);

  mutable std::mutex mutex_;
  std::thread worker_;
  std::atomic<bool> busy_{false};
  bool has_result_ = false;
  Finished result_;

  bool tested_ = false;
  std::string test_note_;
  std::vector<std::string> models_;

  AiSession session_;  // UI thread only
};

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_AI_CONTROLLER_H_
