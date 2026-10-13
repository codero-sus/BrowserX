#include "web/internal_pages.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace browserx {
namespace web {
namespace {

constexpr const char* kScheme = "browserx";

/// Shared stylesheet. Deliberately small and dependency-free: the browser's own
/// UI must not cost more than the pages it renders. Same palette as the native
/// chrome (and the preview) so the two never look like different products.
const char* kStyle = R"CSS(
:root {
  --bg: #0d1117; --panel: #161b22; --sunken: #010409; --line: #262d38;
  --text: #e6edf3; --muted: #8b98a7; --accent: #3fb950; --blue: #4493f8;
  --amber: #d29922; --red: #f85149; --purple: #a371f7; --mono: ui-monospace, Menlo, Consolas, monospace;
}
* { box-sizing: border-box; }
body {
  margin: 0; background: var(--bg); color: var(--text);
  font: 14px/1.55 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
header {
  display: flex; align-items: center; gap: 14px; flex-wrap: wrap;
  padding: 14px 26px; border-bottom: 1px solid var(--line); background: var(--sunken);
  position: sticky; top: 0;
}
.brand { font-weight: 800; letter-spacing: .4px; }
.brand span { color: var(--accent); }
nav { display: flex; gap: 6px; flex-wrap: wrap; }
nav a {
  color: var(--muted); text-decoration: none; font-size: 12.5px;
  padding: 4px 11px; border-radius: 999px; border: 1px solid var(--line);
}
nav a:hover { color: var(--text); border-color: #3a4553; }
nav a[aria-current="page"] { color: #d5f5dd; border-color: var(--accent); background: rgba(63,185,80,.14); }
main { max-width: 1080px; margin: 0 auto; padding: 26px; }
h1 { font-size: 24px; margin: 0 0 6px; }
h2 { font-size: 16px; margin: 26px 0 8px; }
p.lede { color: var(--muted); margin: 0 0 20px; max-width: 74ch; }
.card {
  background: var(--panel); border: 1px solid var(--line); border-radius: 10px;
  padding: 18px 20px; margin-bottom: 16px;
}
pre {
  font-family: var(--mono); font-size: 12.5px; color: #b9c4cf;
  white-space: pre-wrap; word-break: break-word; margin: 0;
  background: var(--sunken); border: 1px solid var(--line); border-radius: 8px; padding: 14px;
}
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th {
  text-align: left; font-size: 10.5px; text-transform: uppercase; letter-spacing: .08em;
  color: var(--muted); border-bottom: 1px solid var(--line); padding: 7px 9px;
}
td { padding: 7px 9px; border-bottom: 1px solid #1c222b; vertical-align: top; }
code, .mono { font-family: var(--mono); font-size: 12.5px; }
.chips { display: flex; gap: 8px; flex-wrap: wrap; margin: 4px 0 18px; }
.chip {
  border: 1px solid var(--line); background: var(--sunken); color: var(--muted);
  border-radius: 999px; padding: 6px 15px; font-size: 12.5px; font-weight: 600;
  cursor: pointer; text-decoration: none;
}
.chip:hover { color: var(--text); }
.chip[aria-current="true"] { border-color: var(--accent); color: #d5f5dd; background: rgba(63,185,80,.16); }
.kpis { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 12px; }
.kpi { background: var(--sunken); border: 1px solid var(--line); border-radius: 8px; padding: 11px 13px; }
.kpi .k { font-size: 10.5px; text-transform: uppercase; letter-spacing: .08em; color: var(--muted); }
.kpi .v { font-size: 18px; font-weight: 650; margin-top: 2px; }
.note { color: var(--muted); font-size: 12.5px; margin: 8px 0 0; }
.warn { color: #ffe6c2; }
.pill {
  font-size: 11px; color: var(--muted); border: 1px solid var(--line);
  border-radius: 999px; padding: 3px 10px; white-space: nowrap;
}
.pill.live { color: var(--accent); border-color: #1f6f36; }
kbd {
  font-family: var(--mono); font-size: 12px; background: var(--sunken);
  border: 1px solid var(--line); border-bottom-width: 2px; border-radius: 6px; padding: 2px 7px;
}
footer { padding: 22px 26px 40px; color: var(--muted); font-size: 12px; max-width: 1080px; margin: 0 auto; }
a { color: var(--blue); }
)CSS";

// A tiny script that exists for one reason: the modes page must be able to ask
// the browser to switch mode. It navigates to browserx://modes?set=<id>, which the
// scheme handler intercepts (see ParseModeSwitchRequest) — no privileged bridge
// is exposed to page content, so a compromised page cannot reach the browser.
const char* kScript = R"JS(
document.addEventListener('click', function (event) {
  var chip = event.target.closest('[data-set-mode]');
  if (!chip) return;
  event.preventDefault();
  location.href = 'browserx://modes?set=' + encodeURIComponent(chip.dataset.setMode);
});
)JS";

std::string ToLower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

/// Escapes for both text and attribute contexts.
std::string EscapeImpl(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 16);
  for (char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

/// Escapes JSON so it can live inside a <script> block: the HTML parser ends the
/// block on "</script" regardless of JavaScript syntax, so that sequence must not
/// survive verbatim. Escaping '<' keeps the JSON valid (JS accepts \u003c).
std::string EscapeJsonForScript(std::string_view json) {
  std::string out;
  out.reserve(json.size() + 16);
  for (size_t i = 0; i < json.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(json[i]);
    switch (c) {
      case '<': out += "\\u003c"; break;
      case '>': out += "\\u003e"; break;
      case '&': out += "\\u0026"; break;
      // U+2028/U+2029 are valid JSON string content but line terminators in
      // JavaScript, so they must be escaped before the JSON is inlined.
      case 0xE2: {
        // E2 80 A8 / E2 80 A9 are U+2028 / U+2029.
        const std::string_view rest = json.substr(i);
        if (rest.size() >= 3 && rest.rfind("\u2028", 0) == 0) {
          out += "\\u2028";
          i += 2;
          break;
        }
        if (rest.size() >= 3 && rest.rfind("\u2029", 0) == 0) {
          out += "\\u2029";
          i += 2;
          break;
        }
        out += static_cast<char>(c);
        break;
      }
      default: out += static_cast<char>(c);
    }
  }
  return out;
}

struct HostEntry {
  const char* host;
  PageKind kind;
  const char* title;
};

/// One table is the single source of truth for hosts, titles and the nav bar.
const std::vector<HostEntry>& HostTable() {
  static const std::vector<HostEntry> table = {
      {"newtab", PageKind::kNewTab, "New tab"},
      {"modes", PageKind::kModes, "Modes & performance"},
      {"telemetry", PageKind::kTelemetry, "Telemetry"},
      {"settings", PageKind::kSettings, "Settings"},
      {"shortcuts", PageKind::kShortcuts, "Keyboard shortcuts"},
      {"catalog", PageKind::kCatalog, "Switch catalogue"},
      {"session", PageKind::kSession, "Session"},
      {"about", PageKind::kAbout, "About BrowserX"},
      {"history", PageKind::kHistory, "History"},
      {"bookmarks", PageKind::kBookmarks, "Bookmarks"},
      {"downloads", PageKind::kDownloads, "Downloads"},
      {"find", PageKind::kFind, "Find in page"},
  };
  return table;
}

const HostEntry* EntryFor(PageKind kind) {
  for (const HostEntry& entry : HostTable()) {
    if (entry.kind == kind) return &entry;
  }
  return nullptr;
}

std::string Nav(PageKind active) {
  std::string out = "<nav>";
  for (const HostEntry& entry : HostTable()) {
    // The nav bar stays short: the four pages people actually open, plus all.
    const bool primary = entry.kind == PageKind::kModes || entry.kind == PageKind::kTelemetry ||
                         entry.kind == PageKind::kSettings || entry.kind == PageKind::kAbout;
    if (!primary) continue;
    out += "<a href=\"browserx://";
    out += entry.host;
    out += "\"";
    if (entry.kind == active) out += " aria-current=\"page\"";
    out += ">";
    out += EscapeImpl(entry.title);
    out += "</a>";
  }
  out += "<a href=\"browserx://shortcuts\">Shortcuts</a>";
  out += "<a href=\"browserx://catalog\">Catalogue</a>";
  out += "</nav>";
  return out;
}

std::string Shell(PageKind kind, const PageData& data, const std::string& body) {
  const std::string title = PageTitle(kind);
  std::string html;
  html.reserve(body.size() + 4096);
  html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  html += "<title>" + EscapeImpl(title) + " · BrowserX</title>\n<style>";
  html += kStyle;
  html += "</style>\n</head>\n<body>\n<header>\n<div class=\"brand\">Browser<span>X</span></div>\n";
  html += Nav(kind);
  html += "\n<div style=\"margin-left:auto;display:flex;gap:8px;align-items:center\">";
  html += "<span class=\"pill live\">" + EscapeImpl(data.active_mode_label) + " mode</span>";
  html += "<span class=\"pill\">v" + EscapeImpl(data.version) + "</span>";
  html += "</div>\n</header>\n<main>\n";
  html += body;
  html += "\n</main>\n<footer>\n<p>BrowserX — an ULTRA-optimised Chromium browser by <a href=\"";
  html += EscapeImpl(data.author_url);
  html += "\" rel=\"noopener\">" + EscapeImpl(data.author) + "</a>. ";
  html += "<a href=\"" + EscapeImpl(data.website_url) + "\" rel=\"noopener\">" + EscapeImpl(data.website) + "</a> · ";
  html += "<a href=\"" + EscapeImpl(data.repository_url) + "\" rel=\"noopener\">" + EscapeImpl(data.repository) + "</a></p>\n";
  html += "<p>This page is generated by the browser itself — no network request, no framework, no telemetry. ";
  html += "Credits and the licences of Chromium/CEF: <a href=\"browserx://about\">browserx://about</a>.</p>\n</footer>\n";
  html += "<script>";
  html += kScript;
  html += "</script>\n</body>\n</html>\n";
  return html;
}

/// A block that tells the truth when data is missing, instead of rendering an
/// empty table the user cannot interpret.
std::string Missing(std::string_view what, std::string_view how_to_get) {
  std::string out = "<div class=\"card\"><p class=\"lede warn\">";
  out += EscapeImpl(what);
  out += " is not available yet.</p><p class=\"note\">";
  out += EscapeImpl(how_to_get);
  out += "</p></div>";
  return out;
}

std::string PreBlockOrMissing(const std::string& text, std::string_view what,
                              std::string_view how_to_get) {
  if (text.empty()) return Missing(what, how_to_get);
  std::string out = "<div class=\"card\"><pre>";
  out += EscapeImpl(text);
  out += "</pre></div>";
  return out;
}

// ---------------------------------------------------------------- the pages --
std::string RenderNewTab(const PageData& data) {
  std::string out;
  out += "<h1>BrowserX</h1>\n<p class=\"lede\">Fast, low-memory browsing on Chromium. "
         "Switch modes any time — the ones that need a relaunch tell you so first.</p>\n";
  out += "<div class=\"chips\">";
  out += "<a class=\"chip\" href=\"browserx://modes\">Modes</a>";
  out += "<a class=\"chip\" href=\"browserx://telemetry\">Telemetry</a>";
  out += "<a class=\"chip\" href=\"browserx://catalog\">Switch catalogue</a>";
  out += "<a class=\"chip\" href=\"browserx://session\">Session</a>";
  out += "<a class=\"chip\" href=\"browserx://about\">About</a>";
  out += "</div>\n";
  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Current mode</h2>";
  out += "<p class=\"lede\" style=\"margin:0\">" + EscapeImpl(data.active_mode_label) + " — " +
         EscapeImpl(data.runtime_summary.empty() ? "runtime policy loading" : data.runtime_summary) +
         "</p></div>\n";
  if (!data.profile_report.empty()) {
    out += "<div class=\"card\"><h2 style=\"margin-top:0\">This machine</h2><pre>";
    out += EscapeImpl(data.profile_report);
    out += "</pre></div>\n";
  }
  return out;
}

std::string RenderModes(const PageData& data) {
  std::string out;
  out += "<h1>Modes</h1>\n<p class=\"lede\">Each mode is a switch plan plus a runtime policy. "
         "Boot switches need a relaunch; everything else applies immediately.</p>\n";

  out += "<div class=\"chips\" id=\"mode-chips\">";
  for (const char* mode : {"ultra", "gaming", "high-performance", "low-ram", "eco"}) {
    const bool active = data.active_mode_id == mode;
    out += "<a class=\"chip\" data-set-mode=\"" + std::string(mode) + "\" href=\"browserx://modes?set=" +
           mode + "\" aria-current=\"";
    out += active ? "true" : "false";
    out += "\">" + EscapeImpl(mode) + "</a>";
  }
  out += "</div>\n";

  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Active plan</h2>";
  if (data.plan_text.empty()) {
    out += "<p class=\"lede warn\">No plan has been built yet.</p>";
  } else {
    out += "<pre>" + EscapeImpl(data.plan_text) + "</pre>";
  }
  out += "</div>\n";

  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Every mode, resolved for this machine</h2>\n";
  out += "<p class=\"note\">Generated by the mode engine; the same numbers `make bench` prints.</p>\n";
  if (data.plans_json.empty()) {
    out += "<p class=\"lede warn\">Plan data unavailable.</p>";
  } else {
    // The raw JSON goes to the client so the page can render it without a second
    // request; the fallback <pre> keeps it readable when scripting is disabled.
    out += "<div id=\"plans\"></div>";
    out += "<script>window.__browserxPlans = " + EscapeJsonForScript(data.plans_json) + ";</script>";
    out += "<script>(function(){var el=document.getElementById('plans');if(!el||!window.__browserxPlans)return;";
    out += "var rows=window.__browserxPlans.map(function(p){return '<h3>'+p.label+'</h3><p class=\"note\">'+(p.tagline||'')+'</p>'";
    out += "+'<pre>'+p.command_line.replace(/&/g,'&amp;').replace(/</g,'&lt;')+'</pre>';}).join('');";
    out += "el.innerHTML=rows;})();</script>";
    out += "<details><summary class=\"note\">Raw JSON</summary><pre>";
    out += EscapeImpl(data.plans_json);
    out += "</pre></details>";
  }
  out += "</div>\n";
  return out;
}

std::string RenderTelemetry(const PageData& data) {
  std::string out;
  out += "<h1>Telemetry</h1>\n<p class=\"lede\">Live memory pressure, tab lifecycle and the actions "
         "the memory guard has taken. Sampling interval follows the active mode.</p>\n";
  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Runtime policy</h2>";
  out += "<p class=\"note\">" + EscapeImpl(data.runtime_summary.empty()
                                               ? "no runtime policy data"
                                               : data.runtime_summary) +
         "</p></div>\n";
  if (data.session_json.empty()) {
    out += Missing("Session telemetry", "Open a tab and reload this page.");
  } else {
    out += "<div class=\"card\"><h2 style=\"margin-top:0\">Session as the browser sees it</h2>";
    out += "<p class=\"note\">This is the exact JSON the browser persists for session restore.</p>";
    out += "<pre>" + EscapeImpl(data.session_json) + "</pre></div>\n";
  }
  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Escalation ladder</h2>";
  out += "<p class=\"note\">On rising memory pressure BrowserX trims caches, unloads idle tabs "
         "(renderer destroyed, URL and scroll position kept), then warns. Discarding is disabled in "
         "GAME and HIGH PERFORMANCE modes on purpose.</p></div>\n";
  return out;
}

std::string RenderSettings(const PageData& data) {
  std::string out;
  out += "<h1>Settings</h1>\n<p class=\"lede\">Settings live in a JSON file next to your profile "
         "and are validated on load — an invalid value falls back to the default instead of "
         "breaking the browser.</p>\n";
  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Where things live</h2>";
  out += "<table><tbody>";
  for (const auto& [what, where] :
       std::vector<std::pair<const char*, const char*>>{{"Settings", "settings.json"},
                                                        {"Session", "sessions/last-session.json"},
                                                        {"HTTP cache", "cache/"},
                                                        {"Logs", "logs/browserx.log"}}) {
    out += "<tr><td>" + std::string(what) + "</td><td class=\"mono\">" + std::string(where) +
           "</td></tr>";
  }
  out += "</tbody></table></div>\n";
  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Optimisation switches in force</h2>\n";
  out += PreBlockOrMissing(data.catalog_report, "The switch catalogue",
                           "Run `make bench ARGS=\"--catalog\"` in the source tree.");
  out += "</div>\n";
  return out;
}

std::string RenderShortcuts(const PageData& data) {
  (void)data;
  struct Row {
    const char* keys;
    const char* action;
  };
  static const Row rows[] = {
      {"Ctrl+T", "New tab"},           {"Ctrl+W", "Close tab"},
      {"Ctrl+Shift+T", "Reopen closed tab"}, {"Ctrl+Tab", "Next tab"},
      {"Ctrl+Shift+Tab", "Previous tab"}, {"Ctrl+Shift+D", "Duplicate tab"},
      {"Ctrl+Shift+P", "Pin / unpin tab"}, {"Ctrl+Shift+M", "Mute / unmute tab"},
      {"Ctrl+1…5", "Switch mode (ULTRA…ECO)"}, {"Ctrl+L", "Focus the address bar"},
      {"Ctrl+F", "Find in page"},       {"F5 / Ctrl+R", "Reload"},
      {"F11", "Fullscreen"},            {"Ctrl+Shift+I", "Developer tools"},
      {"Ctrl++ / Ctrl+-", "Zoom in / out"}, {"Ctrl+0", "Reset zoom"},
      {"Ctrl+Shift+Q", "Close window"},
  };
  std::string out;
  out += "<h1>Keyboard shortcuts</h1>\n<p class=\"lede\">Everything a browser needs, and mode "
         "switching on Ctrl+1…5 so changing performance costs one keypress.</p>\n";
  out += "<div class=\"card\"><table><thead><tr><th>Keys</th><th>Action</th></tr></thead><tbody>";
  for (const Row& row : rows) {
    out += "<tr><td><kbd>" + EscapeImpl(row.keys) + "</kbd></td><td>" + EscapeImpl(row.action) +
           "</td></tr>";
  }
  out += "</tbody></table></div>\n";
  return out;
}

std::string RenderCatalog(const PageData& data) {
  std::string out;
  out += "<h1>Switch catalogue</h1>\n<p class=\"lede\">Every Chromium switch BrowserX knows about, "
         "with the source file it was verified against. Switches marked <em>removed</em> do not "
         "exist upstream any more and are never applied.</p>\n";
  out += "<div class=\"card\">\n";
  out += PreBlockOrMissing(data.catalog_report, "The switch catalogue",
                           "Run `make bench ARGS=\"--catalog\"` in the source tree, or open "
                           "browserx://modes.");
  out += "</div>\n";
  return out;
}

std::string RenderSession(const PageData& data) {
  std::string out;
  out += "<h1>Session</h1>\n<p class=\"lede\">The tabs BrowserX would restore right now. "
         "Sleeping tabs keep their URL, title and scroll position but no renderer process — that is "
         "where most of the memory saving comes from.</p>\n";
  out += "<div class=\"card\">\n";
  out += PreBlockOrMissing(data.session_json, "Session state",
                           "Open a tab, then reload this page.");
  out += "</div>\n";
  return out;
}

std::string RenderAbout(const PageData& data) {
  std::string out;
  out += "<h1>About BrowserX</h1>\n<p class=\"lede\">An ULTRA-optimised Chromium browser in C++20 "
         "on CEF, with five performance modes, Auto-Pilot and a tracker shield. The mobile edition "
         "runs the same performance core behind an Android front end.</p>\n";
  out += "<div class=\"card\"><h2 style=\"margin-top:0\">Build</h2><table><tbody>";
  out += "<tr><td>Version</td><td class=\"mono\">" + EscapeImpl(data.version) + "</td></tr>";
  out += "<tr><td>Engine</td><td class=\"mono\">Chromium via CEF</td></tr>";
  out += "<tr><td>Core</td><td class=\"mono\">src/core (no CEF dependency, unit tested)</td></tr>";
  out += "<tr><td>Active mode</td><td class=\"mono\">" + EscapeImpl(data.active_mode_id) +
         "</td></tr>";
  out += "</tbody></table></div>\n";

  out += "<div class=\"card\" id=\"credits\"><h2 style=\"margin-top:0\">Credits</h2>";
  out += "<p class=\"lede\" style=\"margin-bottom:10px\">BrowserX is made by <strong>" +
         EscapeImpl(data.author) + "</strong>.</p>";
  out += "<table><tbody>";
  out += "<tr><td>GitHub</td><td><a href=\"" + EscapeImpl(data.author_url) +
         "\" rel=\"noopener\">" + EscapeImpl(data.author_url) + "</a></td></tr>";
  out += "<tr><td>Website</td><td><a href=\"" + EscapeImpl(data.website_url) +
         "\" rel=\"noopener\">" + EscapeImpl(data.website) + "</a></td></tr>";
  out += "<tr><td>Repository</td><td><a href=\"" + EscapeImpl(data.repository_url) +
         "\" rel=\"noopener\">" + EscapeImpl(data.repository) + "</a></td></tr>";
  out += "<tr><td>Licence</td><td class=\"mono\">" + EscapeImpl(kProjectLicence) +
         " (BrowserX), third-party licences for the Chromium/CEF runtime</td></tr>";
  out += "</tbody></table>";
  out += "<h3>Built on</h3>";
  out += "<ul class=\"lede\" style=\"margin:0\">";
  out += "<li><a href=\"https://www.chromium.org/\" rel=\"noopener\">Chromium</a> — the engine: "
         "Blink, V8, the network stack, the sandboxed process model (BSD-3-Clause + third parties).</li>";
  out += "<li><a href=\"https://github.com/chromiumembedded/cef\" rel=\"noopener\">CEF</a> — "
         "embedding Chromium in C++: process model, Views toolkit, resource loading (BSD-3-Clause).</li>";
  out += "<li><a href=\"https://source.chromium.org/\" rel=\"noopener\">Chromium source</a> — "
         "the ground truth behind every switch in the catalogue: each one records the file it was "
         "verified against.</li>";
  out += "</ul>";
  out += "<p class=\"note\">The Chromium/CEF runtime BrowserX downloads and packages stays under "
         "its own licences; BrowserX claims no copyright over it. Full credits: "
         "<span class=\"mono\">CREDITS.md</span> in the source tree.</p>";
  out += "</div>\n";

  if (!data.profile_report.empty()) {
    out += "<div class=\"card\"><h2 style=\"margin-top:0\">Detected machine</h2><pre>";
    out += EscapeImpl(data.profile_report);
    out += "</pre></div>\n";
  }
  if (!data.extra_about_html.empty()) {
    out += "<div class=\"card\">" + data.extra_about_html + "</div>\n";
  }
  return out;
}

/// Pages whose data store exists but is not part of this build yet. They say so
/// plainly rather than showing an empty list that looks like "you have no
/// history", which would be a lie.
std::string RenderNotWired(PageKind kind) {
  std::string out = "<h1>";
  out += EscapeImpl(PageTitle(kind));
  out += "</h1>\n<p class=\"lede\">This page is part of the v1 feature set but its store is not "
         "wired up in this build yet — the browser is being built stage by stage, and the storage "
         "layer lands with the history/bookmark service.</p>\n";
  out += "<div class=\"card\"><p class=\"note\">Nothing is hidden from you here: this page "
         "intentionally does not render an empty list, because an empty list would look like "
         "\"you have no ";
  out += EscapeImpl(ToLower(PageTitle(kind)));
  out += "\" when the truth is \"this is not implemented yet\".</p>";
  out += "<p class=\"note\">Until then, session restore (browserx://session) and the mode engine "
         "(browserx://modes) cover the data that exists today.</p></div>\n";
  return out;
}

std::string RenderNotFound(std::string_view host) {
  std::string out;
  out += "<h1>No such page</h1>\n<p class=\"lede\"><code>browserx://";
  out += EscapeImpl(host);
  out += "</code> does not exist in this build.</p>\n<div class=\"card\"><h2 style=\"margin-top:0\">"
         "These do</h2><ul>";
  for (const std::string& known : AllInternalHosts()) {
    out += "<li><a href=\"browserx://" + EscapeImpl(known) + "\">browserx://" + EscapeImpl(known) +
           "</a> — " + EscapeImpl(PageTitle(KindForHost(known))) + "</li>";
  }
  out += "</ul></div>\n";
  return out;
}

std::string RenderFind(const PageData& data) {
  std::string out;
  out += "<h1>Find in page</h1>\n<p class=\"lede\">Press Ctrl+F for the find bar, or use the "
         "shortcuts below. The bar stays out of the way of the page and reports match counts "
         "as you type.</p>\n";
  out += "<div class=\"card\"><table><thead><tr><th>Keys</th><th>Action</th></tr></thead><tbody>";
  for (const auto& [keys, action] :
       std::vector<std::pair<const char*, const char*>>{{"Ctrl+F", "Open the find bar"},
                                                        {"Enter", "Next match"},
                                                        {"Shift+Enter", "Previous match"},
                                                        {"Escape", "Close the find bar"}}) {
    out += "<tr><td><kbd>" + std::string(keys) + "</kbd></td><td>" + std::string(action) +
           "</td></tr>";
  }
  out += "</tbody></table></div>\n";
  if (!data.session_json.empty()) {
    out += "<div class=\"card\"><p class=\"note\">Tip: Ctrl+F searches only the page you are "
           "looking at. browserx://session searches the tab list.</p></div>\n";
  }
  return out;
}

}  // namespace

// ------------------------------------------------------------------ API -----
bool ParseInternalUrl(std::string_view url, InternalUrl* out) {
  if (out == nullptr) return false;
  const size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) return false;
  if (ToLower(url.substr(0, scheme_end)) != kScheme) return false;

  out->scheme = kScheme;
  std::string_view rest = url.substr(scheme_end + 3);
  // The authority ends at the first '/' *or* '?': "browserx://modes?set=eco" has a
  // query and no path, and dropping the query there would break mode switching.
  const size_t path_start = rest.find_first_of("/?");
  std::string_view authority = path_start == std::string_view::npos ? rest : rest.substr(0, path_start);
  std::string_view after = path_start == std::string_view::npos ? std::string_view() : rest.substr(path_start);

  // Credentials and ports have no meaning for an internal page; treat them as
  // part of the host so a malformed URL becomes "unknown page" and not a crash.
  if (authority.empty() || authority.find('@') != std::string_view::npos ||
      authority.find(':') != std::string_view::npos) {
    out->host = ToLower(authority);
    out->path = "/";
    out->query.clear();
    return true;
  }

  out->host = ToLower(authority);
  const size_t query_start = after.find('?');
  if (query_start == std::string_view::npos) {
    out->path = after.empty() ? "/" : std::string(after);
    out->query.clear();
  } else {
    out->path = after.substr(0, query_start).empty() ? "/" : std::string(after.substr(0, query_start));
    out->query = std::string(after.substr(query_start + 1));
  }
  return true;
}

PageKind KindForHost(std::string_view host) {
  const std::string lowered = ToLower(host);
  // "browserx://" with nothing after it behaves like the new-tab page.
  if (lowered.empty()) return PageKind::kNewTab;
  for (const HostEntry& entry : HostTable()) {
    if (lowered == entry.host) return entry.kind;
  }
  // A couple of aliases people type by instinct.
  if (lowered == "new-tab" || lowered == "home") return PageKind::kNewTab;
  if (lowered == "flags" || lowered == "switches") return PageKind::kCatalog;
  if (lowered == "performance" || lowered == "mode") return PageKind::kModes;
  return PageKind::kUnknown;
}

const char* PageName(PageKind kind) {
  const HostEntry* entry = EntryFor(kind);
  return entry != nullptr ? entry->host : "unknown";
}

const char* PageTitle(PageKind kind) {
  if (kind == PageKind::kUnknown) return "No such page";
  const HostEntry* entry = EntryFor(kind);
  return entry != nullptr ? entry->title : "No such page";
}

const std::vector<std::string>& AllInternalHosts() {
  static const std::vector<std::string> hosts = [] {
    std::vector<std::string> out;
    for (const HostEntry& entry : HostTable()) out.emplace_back(entry.host);
    return out;
  }();
  return hosts;
}

std::string ContentTypeFor(PageKind kind) {
  (void)kind;  // every page is HTML today; kept as a function so that changes.
  return "text/html; charset=utf-8";
}

std::string EscapeHtml(std::string_view text) { return EscapeImpl(text); }

bool ParseModeSwitchRequest(const InternalUrl& url, std::string* mode_id_out) {
  if (KindForHost(url.host) != PageKind::kModes) return false;
  const size_t key = url.query.find("set=");
  if (key == std::string::npos) return false;
  size_t start = key + 4;
  size_t end = url.query.find('&', start);
  std::string value = url.query.substr(start, end == std::string::npos ? end : end - start);
  // Percent-decode the few characters a mode id could contain before validation;
  // the id itself is validated against the engine's list by the caller.
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int high = hex(value[i + 1]);
      const int low = hex(value[i + 2]);
      if (high >= 0 && low >= 0) {
        decoded += static_cast<char>(high * 16 + low);
        i += 2;
        continue;
      }
    }
    decoded += value[i];
  }
  // A mode id is lowercase letters and dashes only; anything else is refused
  // here rather than passed on to the engine.
  if (decoded.empty() || decoded.size() > 32) return false;
  for (char c : decoded) {
    if (!((c >= 'a' && c <= 'z') || c == '-')) return false;
  }
  if (mode_id_out != nullptr) *mode_id_out = decoded;
  return true;
}

std::string RenderPage(PageKind kind, const PageData& data) {
  switch (kind) {
    case PageKind::kNewTab: return Shell(kind, data, RenderNewTab(data));
    case PageKind::kModes: return Shell(kind, data, RenderModes(data));
    case PageKind::kTelemetry: return Shell(kind, data, RenderTelemetry(data));
    case PageKind::kSettings: return Shell(kind, data, RenderSettings(data));
    case PageKind::kShortcuts: return Shell(kind, data, RenderShortcuts(data));
    case PageKind::kCatalog: return Shell(kind, data, RenderCatalog(data));
    case PageKind::kSession: return Shell(kind, data, RenderSession(data));
    case PageKind::kAbout: return Shell(kind, data, RenderAbout(data));
    case PageKind::kFind: return Shell(kind, data, RenderFind(data));
    case PageKind::kHistory:
    case PageKind::kBookmarks:
    case PageKind::kDownloads: return Shell(kind, data, RenderNotWired(kind));
    case PageKind::kUnknown: break;
  }
  return Shell(PageKind::kUnknown, data,
               RenderNotFound(data.requested_host.empty() ? "unknown" : data.requested_host));
}

}  // namespace web
}  // namespace browserx
