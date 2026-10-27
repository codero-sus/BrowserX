// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for CAPTCHA assist. The point of this file is the negative space: what the injected
// script must never contain, and what the notice must never promise.
//
// A CAPTCHA is a test of whether a human is present. Code that solves one, or clicks one on
// the user's behalf, defeats the test and acts without the user. So the assertions here are
// mostly "this must not appear", and they are the reason a future well-meaning contributor
// cannot quietly turn assistance into automation.

#include "core/captcha_assist.h"

#include <string>

#include "test_harness.h"

using namespace browserx::core;

namespace {

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

bool ContainsAny(const std::string& text, const std::initializer_list<const char*>& needles) {
  for (const char* needle : needles) {
    if (Contains(text, needle)) return true;
  }
  return false;
}

}  // namespace

TEST(CaptchaAssist, TheDetectorNeverActsOnThePage) {
  const std::string script = DetectorScript();
  // No interaction with the challenge, in any spelling. The list is deliberately broader
  // than the obvious .click(): requestSubmit, dispatchEvent and form.submit all amount to
  // the same thing.
  EXPECT_TRUE(!ContainsAny(script, {".click(", "requestSubmit", "dispatchEvent",
                                    "submit()", ".focus(", "setAttribute('value'",
                                    "sendKeys", "type(", ".value ="}));
  EXPECT_TRUE(Contains(script, "MutationObserver"));
}

TEST(CaptchaAssist, TheHighlightOnlyShowsAndNeverFocusesOrClicks) {
  const std::string script = HighlightScript();
  EXPECT_TRUE(Contains(script, "scrollIntoView"));
  EXPECT_TRUE(Contains(script, "outline"));
  EXPECT_TRUE(!ContainsAny(script, {".click(", ".focus(", "submit", "dispatchEvent"}));
}

TEST(CaptchaAssist, NoSolvingServiceIsEverContacted) {
  // The detector reports to the console only. A network call would mean the page's
  // challenge was shipped somewhere else, which is exactly the behaviour this feature
  // refuses.
  const std::string script = DetectorScript() + HighlightScript();
  EXPECT_TRUE(!ContainsAny(script, {"fetch(", "XMLHttpRequest", "WebSocket", "sendBeacon",
                                    ".src =", "http://", "https://"}));
}

TEST(CaptchaAssist, ThePolicyIsStatedInCode) {
  EXPECT_TRUE(kCaptchaAssistNeverActs);
}

TEST(CaptchaAssist, DetectorMessagesAreParsedAndEverythingElseIgnored) {
  CaptchaKind kind = CaptchaKind::kUnknown;
  EXPECT_TRUE(ParseDetectorMessage("browserx-captcha:recaptcha", &kind));
  EXPECT_TRUE(kind == CaptchaKind::kRecaptcha);
  EXPECT_TRUE(ParseDetectorMessage("browserx-captcha:hcaptcha", &kind));
  EXPECT_TRUE(kind == CaptchaKind::kHcaptcha);
  EXPECT_TRUE(ParseDetectorMessage("browserx-captcha:turnstile", &kind));
  EXPECT_TRUE(kind == CaptchaKind::kTurnstile);
  EXPECT_TRUE(ParseDetectorMessage("browserx-captcha:arkose", &kind));
  EXPECT_TRUE(kind == CaptchaKind::kArkose);
  EXPECT_TRUE(ParseDetectorMessage("browserx-captcha:image-grid", &kind));
  EXPECT_TRUE(kind == CaptchaKind::kImageGrid);

  // Ordinary console output must pass through: the console handler sees every message a
  // page logs, and claiming one of them would be a false positive on every site.
  EXPECT_TRUE(!ParseDetectorMessage("hello", &kind));
  EXPECT_TRUE(!ParseDetectorMessage("", &kind));
  EXPECT_TRUE(!ParseDetectorMessage("[violation] click handler took 50ms", &kind));
  EXPECT_TRUE(!ParseDetectorMessage("browserx-captcha:", &kind));
}

TEST(CaptchaAssist, EveryNoticeSaysBrowserXWillNotAct) {
  for (const CaptchaKind kind : {CaptchaKind::kRecaptcha, CaptchaKind::kHcaptcha,
                                 CaptchaKind::kTurnstile, CaptchaKind::kArkose,
                                 CaptchaKind::kImageGrid, CaptchaKind::kUnknown}) {
    const std::string notice = AssistNotice(kind);
    EXPECT_TRUE(!notice.empty());
    // The two halves that must never be lost: it will not solve, and it is the user's to do.
    EXPECT_TRUE(Contains(notice, "not solve"));
    EXPECT_TRUE(Contains(notice, "yours to do"));
    EXPECT_TRUE(Contains(notice, "Show me"));
  }
  EXPECT_TRUE(!Contains(AssistNotice(CaptchaKind::kRecaptcha), "solved for you"));
}

TEST(CaptchaAssist, NamesAreSpecific) {
  EXPECT_TRUE(std::string(CaptchaName(CaptchaKind::kRecaptcha)) == "reCAPTCHA");
  EXPECT_TRUE(std::string(CaptchaName(CaptchaKind::kHcaptcha)) == "hCaptcha");
  EXPECT_TRUE(std::string(CaptchaName(CaptchaKind::kTurnstile)).find("Turnstile") !=
              std::string::npos);
}
