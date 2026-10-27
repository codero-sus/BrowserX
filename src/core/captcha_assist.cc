// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/captcha_assist.h"

#include <string_view>

namespace browserx {
namespace core {
namespace {

// The detector is one self-contained function, because it is injected as an expression and
// a SyntaxError in here would be invisible on every page. It is written to survive being
// injected twice (the guard on the document) and to report each kind once per page load.
constexpr char kDetector[] = R"JS((function () {
  try {
    if (window.__browserxCaptchaWatch) return "already running";
    window.__browserxCaptchaWatch = true;
    var seen = {};
    var known = [
      ["recaptcha", 'iframe[src*="recaptcha"], div.g-recaptcha, [data-sitekey].g-recaptcha'],
      ["hcaptcha", 'iframe[src*="hcaptcha"], div.h-captcha, [data-hcaptcha-widget-id]'],
      ["turnstile", 'iframe[src*="challenges.cloudflare.com"], div.cf-turnstile'],
      ["arkose", 'iframe[src*="arkoselabs"], iframe[src*="funcaptcha"]'],
      ["image-grid", '[class*="captcha"][class*="grid"], [id*="captcha"][class*="image"]']
    ];
    function report(kind) {
      if (seen[kind]) return;
      seen[kind] = true;
      // Read-only report. The browser watches the console; nothing is sent anywhere else.
      console.info("browserx-captcha:" + kind);
    }
    function scan() {
      for (var i = 0; i < known.length; i++) {
        try {
          if (document.querySelector(known[i][1])) report(known[i][0]);
        } catch (e) { /* a selector that the engine dislikes is not a reason to stop */ }
      }
    }
    scan();
    var observer = new MutationObserver(function () { scan(); });
    observer.observe(document.documentElement || document, { childList: true, subtree: true });
    return "watching";
  } catch (e) {
    return "failed";
  }
})())JS";

constexpr char kHighlight[] = R"JS((function () {
  try {
    var selectors = [
      'iframe[src*="recaptcha"]', 'div.g-recaptcha',
      'iframe[src*="hcaptcha"]', 'div.h-captcha',
      'iframe[src*="challenges.cloudflare.com"]', 'div.cf-turnstile',
      'iframe[src*="arkoselabs"]', 'iframe[src*="funcaptcha"]'
    ];
    for (var i = 0; i < selectors.length; i++) {
      var node = document.querySelector(selectors[i]);
      if (!node) continue;
      // Outline and scroll. Deliberately no focus() and no click(): showing a person where
      // the challenge is must not shade into interacting with it.
      node.style.outline = "3px solid #3ddc84";
      node.style.outlineOffset = "3px";
      node.scrollIntoView({ block: "center", behavior: "smooth" });
      return "shown";
    }
    return "not found";
  } catch (e) {
    return "failed";
  }
})())JS";

struct MessageName {
  std::string_view token;
  CaptchaKind kind;
};

constexpr MessageName kMessages[] = {
    {"browserx-captcha:recaptcha", CaptchaKind::kRecaptcha},
    {"browserx-captcha:hcaptcha", CaptchaKind::kHcaptcha},
    {"browserx-captcha:turnstile", CaptchaKind::kTurnstile},
    {"browserx-captcha:arkose", CaptchaKind::kArkose},
    {"browserx-captcha:image-grid", CaptchaKind::kImageGrid},
};

}  // namespace

bool ParseDetectorMessage(const std::string& message, CaptchaKind* kind) {
  for (const MessageName& entry : kMessages) {
    if (message.find(entry.token) != std::string::npos) {
      if (kind != nullptr) *kind = entry.kind;
      return true;
    }
  }
  return false;
}

const char* CaptchaName(CaptchaKind kind) {
  switch (kind) {
    case CaptchaKind::kRecaptcha: return "reCAPTCHA";
    case CaptchaKind::kHcaptcha: return "hCaptcha";
    case CaptchaKind::kTurnstile: return "Cloudflare Turnstile";
    case CaptchaKind::kArkose: return "Arkose / FunCaptcha";
    case CaptchaKind::kImageGrid: return "an image challenge";
    case CaptchaKind::kUnknown:
    default: return "a challenge";
  }
}

std::string AssistNotice(CaptchaKind kind) {
  std::string text = "This page has ";
  text += CaptchaName(kind);
  text += ". It is there to check that a person is present, so BrowserX will not solve it "
          "or click it for you - that is yours to do. Click \"Show me\" and the browser "
          "will scroll to it and outline it.";
  return text;
}

std::string DetectorScript() { return std::string(kDetector); }

std::string HighlightScript() { return std::string(kHighlight); }

}  // namespace core
}  // namespace browserx
