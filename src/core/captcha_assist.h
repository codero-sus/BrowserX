// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — CAPTCHA assist: help the person, never act for them.
//
// A CAPTCHA exists to establish that a human is present. A browser that solves one defeats
// the thing it was asked to check, and a browser that clicks it on your behalf is doing
// something you did not do. So this feature is deliberately the weakest kind of assistance:
//
//   * it *observes* the page, read-only, for the well-known challenge widgets;
//   * it tells the person where the challenge is and what it wants;
//   * on request it scrolls to the widget and outlines it.
//
// It never solves, never submits, never clicks, never types, and never sends the page to a
// solving service. Those are not missing features - they are the policy, stated here and
// asserted by tests, because a helpful-looking "auto-solve" is the one thing in this area
// that must not quietly appear later.
//
// Detection reports through the console channel (console.info("browserx-captcha:<kind>")),
// which the browser already reads, so no new IPC surface is introduced for it.

#ifndef BROWSERX_CORE_CAPTCHA_ASSIST_H_
#define BROWSERX_CORE_CAPTCHA_ASSIST_H_

#include <string>
#include <vector>

namespace browserx {
namespace core {

/// The policy, as a fact the tests can check and the UI can quote.
constexpr bool kCaptchaAssistNeverActs = true;

/// What kind of challenge was seen. kUnknown means "something that looks like a challenge,
/// but not one of the widgets we know" - still worth telling the person about.
enum class CaptchaKind {
  kUnknown = 0,
  kRecaptcha,
  kHcaptcha,
  kTurnstile,
  kArkose,
  kImageGrid,
};

/// Parses the console line the detector emits. Returns false for every other line, so the
/// browser's console handler can pass it through untouched.
bool ParseDetectorMessage(const std::string& message, CaptchaKind* kind);

/// Human name for the widget ("reCAPTCHA", ...).
const char* CaptchaName(CaptchaKind kind);

/// What the person is told. Always says that BrowserX will not act, because a notice that
/// only said "CAPTCHA found" would invite the assumption that something is being done.
std::string AssistNotice(CaptchaKind kind);

/// The injected detector. Read-only: it observes the DOM and reports, and the test asserts
/// that it contains no click/submit/type call. Safe to run on any page; does nothing on a
/// page without a challenge.
std::string DetectorScript();

/// The injected highlighter, run only when the person asks to be shown the challenge.
/// Scrolls to it and outlines it. Still no clicking.
std::string HighlightScript();

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_CAPTCHA_ASSIST_H_
