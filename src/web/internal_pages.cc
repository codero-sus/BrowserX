// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "web/internal_pages.h"

#include "web/bench_page.h"

#include "core/format.h"
#include "core/json.h"
#include "core/mode_engine.h"
#include "core/net_errors.h"
#include "core/url_utils.h"

#include <ctime>

#include <algorithm>
#include <cstdio>
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
/* BrowserX internal-page design system.
   Dependency-free on purpose: the browser's own UI must not cost more than the
   pages it renders, and these pages must work with no network at all. Every colour
   is a token, so the whole product - chrome, pages, preview - stays one design. */
:root {
  color-scheme: dark;
  --bg: #07090d;
  --glow-1: rgba(61, 220, 132, .10);
  --glow-2: rgba(74, 168, 255, .10);
  --panel: #101620;
  --panel-2: #151d29;
  --sunken: #0a0e14;
  --line: rgba(255, 255, 255, .08);
  --line-strong: rgba(255, 255, 255, .17);
  --text: #e9eff7;
  --muted: #94a3b8;
  --faint: #6b7a90;
  --accent: #3ddc84;
  --blue: #4aa8ff;
  --amber: #e3b341;
  --red: #ff7b72;
  --purple: #b083f0;
  --mono: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  --r-xs: 6px; --r-sm: 10px; --r: 14px; --r-lg: 20px; --r-full: 999px;
  --sh-1: 0 1px 2px rgba(0, 0, 0, .5);
  --sh-2: 0 18px 40px -24px rgba(0, 0, 0, .9);
  --ring: 0 0 0 2px rgba(74, 168, 255, .55);
  --speed: .18s;
}
/* The pages follow the system like everything else in the browser does. */
@media (prefers-color-scheme: light) {
  :root {
    color-scheme: light;
    --bg: #f6f8fb;
    --glow-1: rgba(18, 163, 92, .12);
    --glow-2: rgba(31, 111, 235, .12);
    --panel: #ffffff;
    --panel-2: #f1f5f9;
    --sunken: #eef2f7;
    --line: rgba(9, 20, 35, .10);
    --line-strong: rgba(9, 20, 35, .22);
    --text: #0d1520;
    --muted: #566577;
    --faint: #7b8aa0;
    --accent: #12a35c;
    --blue: #1f6feb;
    --amber: #a86a00;
    --red: #c62f2f;
    --purple: #7d40c9;
    --sh-1: 0 1px 2px rgba(15, 30, 50, .07);
    --sh-2: 0 18px 40px -26px rgba(15, 30, 50, .35);
  }
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body {
  margin: 0;
  min-height: 100vh;
  color: var(--text);
  font: 14px/1.55 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  -webkit-font-smoothing: antialiased;
  background: var(--bg);
  background:
    radial-gradient(1100px 540px at 10% -10%, var(--glow-1), transparent 62%),
    radial-gradient(900px 500px at 90% -14%, var(--glow-2), transparent 62%),
    var(--bg);
  background-attachment: fixed;
}
::selection { background: color-mix(in srgb, var(--blue) 40%, transparent); }
:focus-visible { outline: none; box-shadow: var(--ring); border-radius: var(--r-sm); }
::-webkit-scrollbar { width: 11px; height: 11px; }
::-webkit-scrollbar-thumb {
  background: var(--line-strong); border-radius: var(--r-full);
  border: 3px solid transparent; background-clip: content-box;
}
::-webkit-scrollbar-thumb:hover { background-clip: content-box; background-color: var(--faint); }

/* --- top bar ------------------------------------------------------------- */
.topbar {
  position: sticky; top: 0; z-index: 30;
  display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
  padding: 11px 22px;
  border-bottom: 1px solid var(--line);
  background: var(--bg);
  background: color-mix(in srgb, var(--bg) 78%, transparent);
  backdrop-filter: blur(16px) saturate(150%);
}
.brand { display: flex; align-items: center; gap: 9px; font-weight: 800; letter-spacing: .2px; }
.brand .mark {
  width: 23px; height: 23px; border-radius: 7px; display: grid; place-items: center;
  background: linear-gradient(135deg, var(--accent), var(--blue));
  color: #04130a; font-size: 13px; font-weight: 900; letter-spacing: 0;
  box-shadow: 0 4px 14px -6px color-mix(in srgb, var(--accent) 70%, transparent);
}
.brand .word { font-size: 15px; }
.brand .word span { color: var(--accent); }
nav { display: flex; gap: 4px; flex-wrap: wrap; }
nav a {
  color: var(--muted); text-decoration: none; font-size: 12.5px; font-weight: 600;
  padding: 5px 12px; border-radius: var(--r-full); border: 1px solid transparent;
  transition: color var(--speed), background var(--speed), border-color var(--speed);
}
nav a:hover { color: var(--text); background: var(--panel-2); }
nav a[aria-current="page"] {
  color: var(--text); border-color: var(--line-strong); background: var(--panel-2);
}
.spacer { margin-left: auto; }
.tools { display: flex; align-items: center; gap: 8px; }
.pill {
  display: inline-flex; align-items: center; gap: 6px;
  font-size: 11.5px; font-weight: 600; color: var(--muted);
  border: 1px solid var(--line); border-radius: var(--r-full); padding: 4px 11px;
  white-space: nowrap; background: var(--panel);
}
.pill .dot {
  width: 6px; height: 6px; border-radius: 50%; background: var(--accent);
  box-shadow: 0 0 0 3px color-mix(in srgb, var(--accent) 20%, transparent);
}
.pill.mode { color: var(--text); border-color: color-mix(in srgb, var(--accent) 40%, var(--line)); }
.btn {
  font: inherit; font-size: 12.5px; font-weight: 600; color: var(--text); cursor: pointer;
  background: var(--panel); border: 1px solid var(--line); border-radius: var(--r-sm);
  padding: 5px 12px; display: inline-flex; align-items: center; gap: 7px;
  transition: background var(--speed), border-color var(--speed), transform var(--speed);
}
.btn:hover { background: var(--panel-2); border-color: var(--line-strong); }
.btn:active { transform: translateY(1px); }
.btn kbd { border: none; background: none; padding: 0; color: var(--faint); }

/* --- layout -------------------------------------------------------------- */
main { max-width: 1080px; margin: 0 auto; padding: 26px 22px 8px; }
h1 { font-size: clamp(22px, 2.6vw, 30px); line-height: 1.2; margin: 0 0 8px; letter-spacing: -.02em; }
h2 { font-size: 15px; margin: 0 0 10px; letter-spacing: -.01em; }
h3 { font-size: 13.5px; margin: 0 0 4px; }
p.lede { color: var(--muted); margin: 0 0 20px; max-width: 74ch; }
p.lede strong { color: var(--text); font-weight: 650; }
.note { color: var(--muted); font-size: 12.5px; margin: 6px 0 0; }
.note.tight { margin: 0; }
.warn { color: var(--amber); }
.grid { display: grid; gap: 12px; grid-template-columns: repeat(auto-fill, minmax(228px, 1fr)); }
.row { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }
.sep { height: 1px; background: var(--line); margin: 22px 0; border: 0; }

/* --- surfaces ------------------------------------------------------------ */
.card {
  background: var(--panel); border: 1px solid var(--line); border-radius: var(--r);
  padding: 18px 20px; margin-bottom: 16px; box-shadow: var(--sh-1);
  transition: border-color var(--speed);
}
.card:hover { border-color: var(--line-strong); }
.card > :first-child { margin-top: 0; }
.card > :last-child { margin-bottom: 0; }
.card.flat { box-shadow: none; background: transparent; }
.tile {
  position: relative; display: block; overflow: hidden; text-decoration: none; color: inherit;
  background: var(--panel); border: 1px solid var(--line); border-radius: var(--r);
  padding: 14px 16px; transition: transform var(--speed), box-shadow var(--speed), border-color var(--speed);
}
.tile::before {
  content: ""; position: absolute; left: 0; top: 0; bottom: 0; width: 3px;
  background: linear-gradient(var(--accent), var(--blue));
  opacity: 0; transition: opacity var(--speed);
}
.tile:hover { transform: translateY(-2px); box-shadow: var(--sh-2); border-color: var(--line-strong); }
.tile:hover::before { opacity: .55; }
.tile[aria-current="true"] { border-color: color-mix(in srgb, var(--accent) 45%, var(--line)); }
.tile[aria-current="true"]::before { opacity: 1; }
.tile .name { font-weight: 700; font-size: 13.5px; display: flex; align-items: center; gap: 8px; }
.tile .sub { color: var(--muted); font-size: 12px; margin-top: 3px; }
.tag {
  font-size: 10px; font-weight: 700; text-transform: uppercase; letter-spacing: .07em;
  padding: 2px 7px; border-radius: var(--r-full);
  border: 1px solid var(--line-strong); color: var(--muted);
}
.tag.on { color: var(--accent); border-color: color-mix(in srgb, var(--accent) 45%, var(--line)); }

/* --- hero / start page --------------------------------------------------- */
.hero { text-align: center; padding: 34px 0 22px; }
.hero h1 { font-size: clamp(28px, 5vw, 46px); margin-bottom: 10px; }
.hero p { color: var(--muted); margin: 0 auto 22px; max-width: 60ch; }
.search {
  display: flex; align-items: center; gap: 10px; max-width: 620px; margin: 0 auto;
  background: var(--panel); border: 1px solid var(--line-strong); border-radius: var(--r-full);
  padding: 9px 10px 9px 18px; box-shadow: var(--sh-2);
  transition: border-color var(--speed), box-shadow var(--speed);
}
.search:focus-within { border-color: color-mix(in srgb, var(--blue) 55%, var(--line-strong)); }
.search input {
  flex: 1; min-width: 0; border: 0; background: none; color: var(--text);
  font: inherit; font-size: 14.5px; outline: none;
}
.search input::placeholder { color: var(--faint); }
.search .go {
  border: 0; border-radius: var(--r-full); padding: 7px 16px; cursor: pointer;
  font: inherit; font-size: 13px; font-weight: 700; color: #04130a;
  background: linear-gradient(135deg, var(--accent), var(--blue));
}
.search .go:hover { filter: brightness(1.08); }

/* --- key/value, kpis, meters -------------------------------------------- */
.tile .diff { margin-top: 6px; color: var(--muted); font-size: 11px; }
.num, th.num { text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
table.library td { padding: 8px 10px 8px 0; border-top: 1px solid var(--line); vertical-align: top; }
table.library tr:first-child td { border-top: 0; }
table.library .when { white-space: nowrap; color: var(--muted); width: 104px; }
table.library .host { font-size: 11.5px; color: var(--faint); margin-top: 2px; }
.field { display: block; }
.field > span { display: block; font-size: 10.5px; text-transform: uppercase; letter-spacing: .08em; color: var(--faint); margin-bottom: 6px; }
.kpis { display: grid; grid-template-columns: repeat(auto-fit, minmax(148px, 1fr)); gap: 12px; }
.kpi {
  background: var(--sunken); border: 1px solid var(--line);
  border-radius: var(--r-sm); padding: 11px 13px;
}
.kpi .k { font-size: 10.5px; text-transform: uppercase; letter-spacing: .08em; color: var(--faint); }
.kpi .v {
  font-size: 19px; font-weight: 700; margin-top: 2px;
  font-variant-numeric: tabular-nums; letter-spacing: -.01em;
}
.meter {
  height: 6px; border-radius: var(--r-full); background: var(--sunken);
  border: 1px solid var(--line); overflow: hidden; margin-top: 7px;
}
.meter > i { display: block; height: 100%; background: linear-gradient(90deg, var(--accent), var(--blue)); }
.meter.warn > i { background: linear-gradient(90deg, var(--amber), var(--red)); }

/* --- tables, code, keyboard --------------------------------------------- */
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th {
  text-align: left; font-size: 10.5px; text-transform: uppercase; letter-spacing: .08em;
  color: var(--faint); border-bottom: 1px solid var(--line); padding: 8px 10px; font-weight: 700;
}
td { padding: 8px 10px; border-bottom: 1px solid var(--line); vertical-align: top; }
tbody tr:last-child td { border-bottom: 0; }
tbody tr { transition: background var(--speed); }
tbody tr:hover { background: var(--panel-2); }
code, .mono { font-family: var(--mono); font-size: 12.5px; }
pre {
  font-family: var(--mono); font-size: 12.5px; color: var(--muted);
  white-space: pre-wrap; word-break: break-word; margin: 0;
  background: var(--sunken); border: 1px solid var(--line); border-radius: var(--r-sm); padding: 14px;
  tab-size: 2;
}
kbd {
  font-family: var(--mono); font-size: 11.5px; color: var(--text);
  background: var(--sunken); border: 1px solid var(--line-strong);
  border-bottom-width: 2px; border-radius: var(--r-xs); padding: 2px 7px; white-space: nowrap;
}
details { border-top: 1px solid var(--line); padding-top: 12px; margin-top: 12px; }
summary { cursor: pointer; color: var(--muted); font-size: 12.5px; }
summary:hover { color: var(--text); }
a { color: var(--blue); text-decoration-color: color-mix(in srgb, var(--blue) 40%, transparent); }
a:hover { text-decoration-color: currentColor; }
input[type="text"], input[type="search"] {
  background: var(--sunken); border: 1px solid var(--line); color: var(--text);
  border-radius: var(--r-sm); padding: 7px 11px; font: inherit; font-size: 13px; width: 100%;
}
input[type="text"]::placeholder, input[type="search"]::placeholder { color: var(--faint); }
.empty { color: var(--faint); font-size: 13px; padding: 10px 2px; }

/* --- command palette ----------------------------------------------------- */
.palette {
  position: fixed; inset: 0; z-index: 100; display: none;
  align-items: flex-start; justify-content: center;
  padding: 12vh 16px 16px;
  background: color-mix(in srgb, #000 55%, transparent);
  backdrop-filter: blur(3px);
}
.palette.open { display: flex; }
.palette .box {
  width: min(620px, 100%); background: var(--panel); border: 1px solid var(--line-strong);
  border-radius: var(--r); box-shadow: var(--sh-2); overflow: hidden;
}
.palette input {
  width: 100%; border: 0; border-bottom: 1px solid var(--line); border-radius: 0;
  background: var(--panel); padding: 14px 16px; font-size: 14.5px;
}
.palette ul { list-style: none; margin: 0; padding: 6px; max-height: 46vh; overflow: auto; }
.palette li {
  display: flex; align-items: center; gap: 10px; padding: 9px 12px;
  border-radius: var(--r-sm); cursor: pointer; font-size: 13.5px;
}
.palette li[aria-selected="true"] { background: var(--panel-2); }
.palette li .kind {
  margin-left: auto; font-size: 10px; text-transform: uppercase; letter-spacing: .07em;
  color: var(--faint);
}
.palette .hint { padding: 9px 16px; border-top: 1px solid var(--line); color: var(--faint); font-size: 11.5px; }

/* --- footer -------------------------------------------------------------- */
footer {
  max-width: 1080px; margin: 0 auto; padding: 26px 22px 44px;
  color: var(--muted); font-size: 12px; border-top: 1px solid var(--line);
}
footer p { margin: 0 0 6px; }
footer .licence { color: var(--faint); }

@media (max-width: 720px) {
  main { padding: 18px 14px 8px; }
  .topbar { padding: 10px 14px; }
  nav { order: 3; width: 100%; }
  .hero { padding: 18px 0 12px; }
}
@media (prefers-reduced-motion: reduce) {
  * { transition: none !important; animation: none !important; }
  html { scroll-behavior: auto; }
}
)CSS";

// The page script: mode switching, the command palette (Ctrl+K), table filters and
// the live tab list. It is deliberately small and dependency-free, and it cannot do
// anything a link could not: the one browser-owned action - switching mode - is a
// navigation to browserx://modes?set=<id>, which the scheme handler validates (see
// ParseModeSwitchRequest). No privileged bridge is exposed to page content, which
// matters because this file also renders inside ordinary browsing sessions.
const char* kScript = R"JS(
/* BrowserX internal-page behaviour. No framework, no network, no privileged bridge:
   the only thing this can ask the browser to do is navigate, which is what a link
   does anyway. The one browser-owned action (switching mode) goes through
   browserx://modes?set=<id>, which the scheme handler validates. */
(function () {
  'use strict';

  function escape(text) {
    return String(text == null ? '' : text)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }

  // --- mode switching ------------------------------------------------------
  document.addEventListener('click', function (event) {
    var chip = event.target.closest('[data-set-mode]');
    if (!chip) return;
    event.preventDefault();
    location.href = 'browserx://modes?set=' + encodeURIComponent(chip.dataset.setMode);
  });

  // --- start-page search ---------------------------------------------------
  // The template comes from the user's settings, so the page searches where the
  // omnibox searches. Typing a URL goes straight there, exactly like the omnibox.
  var searchForm = document.getElementById('search-form');
  if (searchForm) {
    searchForm.addEventListener('submit', function (event) {
      event.preventDefault();
      var input = document.getElementById('search-input');
      var text = input ? input.value.trim() : '';
      if (!text) return;
      var looksLikeUrl = /^[a-z][a-z0-9+.-]*:\/\//i.test(text) ||
                         /^[^\s/]+\.[a-z]{2,}([/:?#].*)?$/i.test(text);
      if (looksLikeUrl) {
        location.href = /^[a-z][a-z0-9+.-]*:\/\//i.test(text) ? text : 'https://' + text;
        return;
      }
      var template = window.__browserxSearchTemplate || 'https://duckduckgo.com/?q=%s';
      location.href = template.indexOf('%s') >= 0
        ? template.replace('%s', encodeURIComponent(text))
        : template + encodeURIComponent(text);
    });
  }

  // --- table filters: <input data-filter="#table-id"> ----------------------
  document.querySelectorAll('[data-filter]').forEach(function (input) {
    var target = document.querySelector(input.dataset.filter);
    if (!target) return;
    input.addEventListener('input', function () {
      var needle = input.value.trim().toLowerCase();
      var rows = target.querySelectorAll('tbody tr');
      var shown = 0;
      rows.forEach(function (row) {
        var haystack = (row.dataset.search || row.textContent).toLowerCase();
        var match = !needle || haystack.indexOf(needle) >= 0;
        row.hidden = !match;
        if (match) shown++;
      });
      var counter = document.querySelector('[data-filter-count="' + input.dataset.filter + '"]');
      if (counter) counter.textContent = shown === rows.length ? rows.length + ' shown'
                                                               : shown + ' of ' + rows.length;
    });
  });

  // --- live session rendering (telemetry + session pages) ------------------
  // The raw JSON stays on the page behind a <details>; this is a reading of it,
  // and if the data is not there the page already says so instead of an empty list.
  function renderTabs() {
    var host = document.getElementById('tab-list');
    var session = window.__browserxSession;
    if (!host || !session || !session.tabs) return;
    var tabs = session.tabs;
    var active = session.active_id;
    var sleeping = tabs.filter(function (t) { return t.sleeping; }).length;
    var pinned = tabs.filter(function (t) { return t.pinned; }).length;
    var muted = tabs.filter(function (t) { return t.muted; }).length;
    var counters = document.getElementById('tab-counters');
    if (counters) {
      counters.innerHTML =
        '<div class="kpi"><div class="k">Tabs</div><div class="v">' + tabs.length + '</div></div>' +
        '<div class="kpi"><div class="k">Sleeping</div><div class="v">' + sleeping + '</div>' +
        '<div class="meter' + (sleeping ? ' warn' : '') + '"><i style="width:' +
        (tabs.length ? Math.round(sleeping / tabs.length * 100) : 0) + '%"></i></div></div>' +
        '<div class="kpi"><div class="k">Pinned</div><div class="v">' + pinned + '</div></div>' +
        '<div class="kpi"><div class="k">Muted</div><div class="v">' + muted + '</div></div>';
    }
    if (tabs.length === 0) { host.innerHTML = '<p class="empty">No tabs open.</p>'; return; }
    host.innerHTML = tabs.map(function (tab) {
      var tags = '';
      if (tab.id === active) tags += '<span class="tag on">active</span>';
      if (tab.sleeping) tags += '<span class="tag">asleep</span>';
      if (tab.pinned) tags += '<span class="tag">pinned</span>';
      if (tab.muted) tags += '<span class="tag">muted</span>';
      if (tab.group) tags += '<span class="tag">' + escape(tab.group) + '</span>';
      return '<div class="tile"><div class="name">' + escape(tab.title || tab.url || 'untitled') +
             '</div><div class="sub mono">' + escape(tab.url || '') + '</div>' +
             '<div class="row" style="margin-top:8px">' + tags + '</div></div>';
    }).join('');
  }
  renderTabs();

  // --- every mode's plan, rendered as cards --------------------------------
  function renderPlans() {
    var host = document.getElementById('plans');
    if (!host || !window.__browserxPlans) return;
    host.innerHTML = window.__browserxPlans.map(function (plan) {
      var count = (plan.switches || []).length;
      return '<details class="card flat"><summary><strong>' + escape(plan.label || plan.mode) +
             '</strong> <span class="note">' + escape(plan.tagline || '') + '</span></summary>' +
             '<p class="note">' + count + ' boot switches · ' +
             escape(plan.estimated_memory_budget_mb ? '~' + plan.estimated_memory_budget_mb + ' MB budget' : 'budget unavailable') +
             '</p><pre>' + escape(plan.command_line || '') + '</pre></details>';
    }).join('');
  }
  renderPlans();

  // --- command palette -----------------------------------------------------
  // Ctrl+K (the shortcut every modern app uses) opens a filterable list of every
  // page and every mode. It exists on the browser's own pages, where the browser
  // is the thing being controlled; a web page never gets this.
  var palette = document.getElementById('palette');
  if (!palette) return;

  var input = document.getElementById('palette-input');
  var list = document.getElementById('palette-list');
  var items = [];

  (window.__browserxNav || []).forEach(function (page) {
    items.push({ label: page.title, detail: page.url, kind: 'page', url: page.url });
  });
  (window.__browserxModes || []).forEach(function (mode) {
    items.push({
      label: mode.label, detail: mode.tagline || '', kind: 'mode',
      url: 'browserx://modes?set=' + encodeURIComponent(mode.id)
    });
  });

  var filtered = items;
  var selected = 0;

  function draw() {
    if (filtered.length === 0) {
      list.innerHTML = '<li class="empty">Nothing matches.</li>';
      return;
    }
    list.innerHTML = filtered.map(function (item, index) {
      return '<li role="option" data-index="' + index + '" aria-selected="' +
             (index === selected) + '">' + escape(item.label) +
             (item.detail ? ' <span class="note tight">' + escape(item.detail) + '</span>' : '') +
             '<span class="kind">' + item.kind + '</span></li>';
    }).join('');
    var current = list.querySelector('li[aria-selected="true"]');
    if (current && current.scrollIntoView) current.scrollIntoView({ block: 'nearest' });
  }

  function filter() {
    var needle = input.value.trim().toLowerCase();
    filtered = items.filter(function (item) {
      return !needle || (item.label + ' ' + item.detail).toLowerCase().indexOf(needle) >= 0;
    });
    selected = 0;
    draw();
  }

  function open() {
    palette.classList.add('open');
    // The class only decides what the eye sees. aria-hidden and the input's focus state are
    // what a screen reader and a test can both rely on, and they are set together so the
    // visual state can never disagree with the announced one.
    palette.setAttribute('aria-hidden', 'false');
    input.setAttribute('aria-expanded', 'true');
    input.value = '';
    filter();
    input.focus();
  }

  function close() {
    palette.classList.remove('open');
    palette.setAttribute('aria-hidden', 'true');
    input.setAttribute('aria-expanded', 'false');
  }

  function go() {
    var item = filtered[selected];
    if (item) location.href = item.url;
  }

  document.addEventListener('keydown', function (event) {
    if ((event.ctrlKey || event.metaKey) && !event.shiftKey && event.key.toLowerCase() === 'k') {
      event.preventDefault();
      palette.classList.contains('open') ? close() : open();
      return;
    }
    if (!palette.classList.contains('open')) return;
    if (event.key === 'Escape') { event.preventDefault(); close(); }
    else if (event.key === 'ArrowDown') {
      event.preventDefault();
      selected = Math.min(selected + 1, filtered.length - 1);
      draw();
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      selected = Math.max(selected - 1, 0);
      draw();
    } else if (event.key === 'Enter') {
      event.preventDefault();
      go();
    }
  });

  input.addEventListener('input', filter);
  list.addEventListener('click', function (event) {
    var row = event.target.closest('li[data-index]');
    if (!row) return;
    selected = Number(row.dataset.index);
    go();
  });
  palette.addEventListener('click', function (event) {
    if (event.target === palette) close();
  });
  var trigger = document.getElementById('palette-open');
  if (trigger) trigger.addEventListener('click', open);
})();
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
      {"error", PageKind::kError, "What went wrong"},
      {"bench", PageKind::kBench, "Benchmark"},
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

/// A JavaScript string literal, escaped for HTML script context. Used for the small
/// data blocks the pages hand to the script (navigation, modes, the search template):
/// they are built here rather than serialised from JSON so a page never has to fetch
/// anything, and every value that comes from user settings is escaped.
std::string JsString(std::string_view text) {
  std::string out = "\"";
  out.reserve(text.size() + 8);
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '<': out += "\\u003c"; break;
      case '>': out += "\\u003e"; break;
      case '&': out += "\\u0026"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out += static_cast<char>(c);
        }
        break;
    }
  }
  out += "\"";
  return out;
}

/// The command palette: a filterable list of every page and every mode, opened with
/// Ctrl+K. Rendered here (not built by script) so it is present and inert even when
/// scripting is off, and so the palette cannot drift from the real page list.
std::string Palette() {
  std::string out;
  out += "<div class=\"palette\" id=\"palette\" role=\"dialog\" aria-modal=\"true\" ";
  // Closed by default, and it says so: a dialog that is closed but not marked hidden is
  // invisible to sighted users and perfectly visible to a screen reader.
  out += "aria-hidden=\"true\" aria-label=\"Command palette\">\n<div class=\"box\">\n";
  out += "<input id=\"palette-input\" type=\"text\" autocomplete=\"off\" spellcheck=\"false\" ";
  out += "role=\"combobox\" aria-expanded=\"false\" aria-controls=\"palette-list\" ";
  out += "aria-autocomplete=\"list\" placeholder=\"Jump to a page, or type a mode name to switch to it\" ";
  out += "aria-label=\"Jump to a page or switch a mode\">\n";
  out += "<ul id=\"palette-list\" role=\"listbox\" aria-label=\"Results\"></ul>\n";
  out += "<div class=\"hint\">Ctrl+K toggles this · &uarr;&darr; choose · Enter opens · Esc closes</div>\n";
  out += "</div>\n</div>\n";
  return out;
}

/// The data blocks every page gets: where the palette can go, what the modes are, and
/// which search engine the user configured. Kept in one function so no page can hand
/// the script a different view of the browser than another page does.
std::string PageScriptData(const PageData& data) {
  std::string out = "<script>\n";
  out += "window.__browserxNav = [";
  bool first = true;
  for (const HostEntry& entry : HostTable()) {
    // The palette is a jumping-off point: the pages that render content, not the
    // placeholders that say their storage is not wired yet.
    if (entry.kind == PageKind::kUnknown) continue;
    if (!first) out += ",";
    first = false;
    out += "{title:" + JsString(entry.title) +
           ",url:" + JsString(std::string("browserx://") + entry.host) + "}";
  }
  out += "];\n";
  out += "window.__browserxModes = [";
  first = true;
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    const std::string id = info.id != nullptr ? info.id : "";
    if (!first) out += ",";
    first = false;
    out += "{id:" + JsString(id) + ",label:" + JsString(info.label != nullptr ? info.label : id) +
           ",tagline:" + JsString(info.tagline != nullptr ? info.tagline : "") +
           ",active:" + (data.active_mode_id == id ? "true" : "false") + "}";
  }
  out += "];\n";
  out += "window.__browserxSearchTemplate = " + JsString(data.search_template) + ";\n";
  if (!data.session_json.empty()) {
    out += "window.__browserxSession = " + EscapeJsonForScript(data.session_json) + ";\n";
  }
  out += "</script>\n";
  return out;
}

std::string Shell(PageKind kind, const PageData& data, const std::string& body) {
  const std::string title = PageTitle(kind);
  std::string html;
  html.reserve(body.size() + 16384);
  html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  html += "<meta name=\"color-scheme\" content=\"dark light\">\n";
  // The page title is what the tab strip shows, so it stays the first thing in <head>.
  html += "<title>" + EscapeImpl(title) + " · BrowserX</title>\n<style>";
  html += kStyle;
  html += "</style>\n</head>\n<body>\n";

  html += "<header class=\"topbar\">\n";
  html += "<div class=\"brand\"><span class=\"mark\">X</span>";
  html += "<span class=\"word\">Browser<span>X</span></span></div>\n";
  html += Nav(kind);
  html += "\n<div class=\"spacer\"></div>\n<div class=\"tools\">\n";
  html += "<button class=\"btn\" id=\"palette-open\" type=\"button\" ";
  html += "title=\"Jump to a page or switch a mode\">Jump to… <kbd>Ctrl+K</kbd></button>\n";
  html += "<span class=\"pill mode\"><span class=\"dot\"></span>";
  html += EscapeImpl(data.active_mode_label) + "</span>\n";
  html += "<span class=\"pill\" title=\"BrowserX version\">v" + EscapeImpl(data.version) + "</span>\n";
  html += "</div>\n</header>\n<main>\n";
  html += body;
  html += "\n</main>\n";

  html += Palette();

  html += "<footer>\n";
  html += "<p>BrowserX — an ULTRA-optimised Chromium browser by <a href=\"";
  html += EscapeImpl(data.author_url);
  html += "\" rel=\"noopener\">" + EscapeImpl(data.author) + "</a>. ";
  html += "<a href=\"" + EscapeImpl(data.website_url) + "\" rel=\"noopener\">" + EscapeImpl(data.website) +
          "</a> · <a href=\"" + EscapeImpl(data.repository_url) + "\" rel=\"noopener\">" +
          EscapeImpl(data.repository) + "</a></p>\n";
  html += "<p class=\"licence\">" + std::string(kProjectLicence) + " — " +
          std::string(kProjectLicenceTerms) + ". Source-available, not open source.</p>\n";
  html += "<p>Generated by the browser itself: no network request, no framework, no telemetry. ";
  html += "Chromium/CEF licences and full credits: <a href=\"browserx://about\">browserx://about</a>.</p>\n";
  html += "</footer>\n";

  html += PageScriptData(data);
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
  out += "<section class=\"hero\">\n";
  out += "<h1>Fast, light, and honest about both.</h1>\n";
  out += "<p>Chromium underneath, a performance mode on every decision above it. "
         "Fifteen modes, a tracker shield and a memory guard that tells you what it did.</p>\n";
  out += "<form class=\"search\" id=\"search-form\" role=\"search\" autocomplete=\"off\">\n";
  out += "<input id=\"search-input\" type=\"text\" spellcheck=\"false\" "
         "placeholder=\"Search the web, or type an address\" "
         "aria-label=\"Search the web or enter an address\">\n";
  out += "<button class=\"go\" type=\"submit\">Search</button>\n";
  out += "</form>\n";
  out += "<p class=\"note tight\" style=\"margin-top:12px\">Searches with the engine from your "
         "settings; anything that looks like an address goes straight there.</p>\n";
  out += "</section>\n";

  // The pages people actually open, one click away.
  out += "<div class=\"grid\">\n";
  struct Shortcut {
    const char* host;
    const char* label;
    const char* sub;
  };
  static const Shortcut shortcuts[] = {
      {"modes", "Modes", "Switch performance profile"},
      {"telemetry", "Telemetry", "Memory, tabs, what the guard did"},
      {"session", "Session", "Tabs that would be restored"},
      {"bench", "Benchmark", "Same file, any browser, offline"},
      {"catalog", "Switch catalogue", "Every flag and its provenance"},
      {"shortcuts", "Keyboard shortcuts", "Everything on one page"},
      {"settings", "Settings", "Stored as JSON, validated on load"},
      {"about", "About BrowserX", "Licence and credits"},
  };
  for (const Shortcut& shortcut : shortcuts) {
    out += "<a class=\"tile\" href=\"browserx://";
    out += shortcut.host;
    out += "\"><div class=\"name\">" + EscapeImpl(shortcut.label) +
           "</div><div class=\"sub\">" + EscapeImpl(shortcut.sub) + "</div></a>\n";
  }
  out += "</div>\n";

  // Switching mode is the most common reason to open a new tab in this browser, so
  // the switcher lives here too - generated from the engine, never hand-written.
  out += "<h2 style=\"margin-top:26px\">Performance modes</h2>\n";
  out += "<p class=\"note\" style=\"margin-bottom:12px\">One click switches. The ones that need "
         "a relaunch say so before they do it.</p>\n";
  out += "<div class=\"grid\">\n";
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    const std::string id = info.id != nullptr ? info.id : "";
    const bool active = data.active_mode_id == id;
    out += "<a class=\"tile\" data-set-mode=\"" + EscapeImpl(id) + "\" href=\"browserx://modes?set=" +
           EscapeImpl(id) + "\" aria-current=\"" + (active ? "true" : "false") + "\">";
    out += "<div class=\"name\">" + EscapeImpl(info.label != nullptr ? info.label : id);
    if (active) out += " <span class=\"tag on\">active</span>";
    out += "</div><div class=\"sub\">" + EscapeImpl(info.tagline != nullptr ? info.tagline : "") +
           "</div></a>\n";
  }
  out += "</div>\n";

  out += "<div class=\"card\" style=\"margin-top:22px\"><h2>Right now</h2>\n";
  out += "<div class=\"kpis\">\n";
  out += "<div class=\"kpi\"><div class=\"k\">Mode</div><div class=\"v\">" +
         EscapeImpl(data.active_mode_label) + "</div></div>\n";
  out += "<div class=\"kpi\"><div class=\"k\">Version</div><div class=\"v\">v" +
         EscapeImpl(data.version) + "</div></div>\n";
  out += "</div>\n";
  out += "<p class=\"note\">" +
         EscapeImpl(data.runtime_summary.empty() ? "Runtime policy is loading."
                                                : data.runtime_summary) +
         "</p>\n";
  out += "</div>\n";
  if (!data.profile_report.empty()) {
    out += "<div class=\"card\"><h2>This machine</h2><pre>";
    out += EscapeImpl(data.profile_report);
    out += "</pre><p class=\"note\">Full detail: <a href=\"browserx://telemetry\">"
           "browserx://telemetry</a>.</p></div>\n";
  }
  return out;
}

/// The engine's summary of what switching to `mode_id` would change, or empty when there
/// is nothing to say (unknown mode, no data, or the mode is already active).
std::string ModeDiffSummary(const PageData& data, const std::string& mode_id) {
  if (data.mode_diffs_json.empty() || mode_id.empty() || mode_id == data.active_mode_id) {
    return std::string();
  }
  std::string error;
  const auto parsed = core::json::Parse(data.mode_diffs_json, &error);
  if (!parsed.has_value()) return std::string();
  for (const core::json::Value& entry : parsed->AsArray()) {
    if (entry.Get("id").AsString() == mode_id) return entry.Get("summary").AsString();
  }
  return std::string();
}

std::string RenderModes(const PageData& data) {
  std::string out;
  out += "<h1>Modes</h1>\n<p class=\"lede\">Each mode is a Chromium switch plan plus a runtime "
         "policy. Boot switches are applied on the next launch; everything else applies "
         "immediately. Nothing here is a marketing number — open "
         "<a href=\"browserx://bench\">the benchmark</a> to measure it yourself.</p>\n";

  // Chips come from the engine's mode list, so a new mode appears here by itself.
  out += "<div class=\"grid\" id=\"mode-chips\">\n";
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    const std::string id = info.id != nullptr ? info.id : "";
    const bool active = data.active_mode_id == id;
    out += "<a class=\"tile\" data-set-mode=\"" + EscapeImpl(id) + "\" href=\"browserx://modes?set=" +
           EscapeImpl(id) + "\" title=\"" + EscapeImpl(info.tagline != nullptr ? info.tagline : "") +
           "\" aria-current=\"" + (active ? "true" : "false") + "\">";
    out += "<div class=\"name\">" + EscapeImpl(info.label != nullptr ? info.label : id);
    if (active) out += " <span class=\"tag on\">active</span>";
    out += "</div><div class=\"sub\">" + EscapeImpl(info.tagline != nullptr ? info.tagline : "") +
           "</div>";
    // What clicking this would change, from the engine's own diff against the active mode:
    // not "GAME is fast" but "adds 10 and removes 2 switches, and here they are".
    const std::string diff_summary = ModeDiffSummary(data, id);
    if (!diff_summary.empty()) {
      out += "<div class=\"sub mono diff\">vs " + EscapeImpl(data.active_mode_label) + ": " +
             EscapeImpl(diff_summary) + "</div>";
    }
    out += "</a>\n";
  }
  out += "</div>\n";

  out += "<div class=\"card\" style=\"margin-top:20px\"><h2>Active plan</h2>\n";
  if (data.plan_text.empty()) {
    out += "<p class=\"lede warn\">No plan has been built yet.</p>";
  } else {
    out += "<pre>" + EscapeImpl(data.plan_text) + "</pre>";
  }
  out += "</div>\n";

  out += "<h2>Every mode, resolved for this machine</h2>\n";
  out += "<p class=\"note\" style=\"margin-bottom:12px\">Generated by the mode engine — the same "
         "numbers <code>make bench</code> prints. Each switch carries the reason it is there.</p>\n";
  if (data.plans_json.empty()) {
    out += "<div class=\"card\"><p class=\"lede warn\">Plan data unavailable.</p></div>";
  } else {
    // The raw JSON goes to the client so the page can render it without a second
    // request; the fallback <pre> keeps it readable when scripting is disabled.
    out += "<div id=\"plans\"></div>\n";
    out += "<script>window.__browserxPlans = " + EscapeJsonForScript(data.plans_json) + ";</script>\n";
    out += "<div class=\"card\"><details style=\"border:0;margin:0;padding:0\">"
           "<summary>Raw JSON (what the engine produced)</summary><pre>";
    out += EscapeImpl(data.plans_json);
    out += "</pre></details></div>\n";
  }
  return out;
}

std::string RenderTelemetry(const PageData& data) {
  std::string out;
  out += "<h1>Telemetry</h1>\n<p class=\"lede\">Live memory pressure, the tab lifecycle and what "
         "the memory guard has actually done. The sampling interval follows the active mode, and "
         "nothing here leaves the machine.</p>\n";

  if (data.session_json.empty()) {
    out += "<div class=\"card\"><h2>Runtime policy</h2><p class=\"note\">" +
           EscapeImpl(data.runtime_summary.empty() ? "no runtime policy data" : data.runtime_summary) +
           "</p></div>\n";
    out += Missing("Session telemetry", "Open a tab and reload this page.");
    out += "<div class=\"card\"><h2>Escalation ladder</h2>\n";
    out += "<p class=\"note\">On rising memory pressure BrowserX trims caches, unloads idle tabs "
           "(renderer destroyed, URL and scroll position kept), then warns. Discarding is disabled "
           "in GAME and HIGH PERFORMANCE modes on purpose.</p></div>\n";
    return out;
  }

  out += "<div class=\"card\"><h2>Tabs</h2>\n";
  out += "<div class=\"kpis\" id=\"tab-counters\"></div>\n";
  out += "<p class=\"note\">A sleeping tab keeps its URL, title and scroll position but no renderer "
         "process — that is where most of the memory saving comes from.</p>\n";
  out += "<div class=\"grid\" id=\"tab-list\" style=\"margin-top:14px\"></div>\n";
  out += "</div>\n";

  out += "<div class=\"card\"><h2>Runtime policy</h2><p class=\"note\">" +
         EscapeImpl(data.runtime_summary.empty() ? "no runtime policy data" : data.runtime_summary) +
         "</p></div>\n";

  out += "<div class=\"card\"><h2>Escalation ladder</h2>\n";
  out += "<table><thead><tr><th>Step</th><th>What happens</th><th>Reversible</th></tr></thead><tbody>";
  struct Step {
    const char* step;
    const char* what;
    const char* back;
  };
  static const Step steps[] = {
      {"1 · trim", "Caches are trimmed before anything is closed", "yes"},
      {"2 · sleep", "Idle tabs lose their renderer, keep URL/title/scroll", "yes, on click"},
      {"3 · sleep LRU", "The least recently used tab follows the same path", "yes, on click"},
      {"4 · warn", "The status bar says memory is tight instead of acting silently", "—"},
  };
  for (const Step& step : steps) {
    out += "<tr><td class=\"mono\">" + std::string(step.step) + "</td><td>" + std::string(step.what) +
           "</td><td>" + std::string(step.back) + "</td></tr>";
  }
  out += "</tbody></table></div>\n";

  out += "<div class=\"card\"><details style=\"border:0;margin:0;padding:0\">";
  out += "<summary>Session JSON (exactly what the browser persists for restore)</summary><pre>";
  out += EscapeImpl(data.session_json);
  out += "</pre></details></div>\n";
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
    const char* group;
  };
  static const Row rows[] = {
      {"Ctrl+T", "New tab", "tabs"},
      {"Ctrl+W", "Close tab", "tabs"},
      {"Ctrl+Shift+T", "Reopen closed tab", "tabs"},
      {"Ctrl+Tab", "Next tab", "tabs"},
      {"Ctrl+Shift+Tab", "Previous tab", "tabs"},
      {"Ctrl+Shift+D", "Duplicate tab", "tabs"},
      {"Ctrl+Shift+P", "Pin / unpin tab", "tabs"},
      {"Ctrl+Shift+M", "Mute / unmute tab", "tabs"},
      {"Ctrl+Shift+G", "Add tab to a group", "tabs"},
      {"Ctrl+1…9", "Switch mode", "modes"},
      {"Ctrl+K", "Command palette — jump to any page or mode", "modes"},
      {"Ctrl+L", "Focus the address bar", "navigation"},
      {"F5 / Ctrl+R", "Reload", "navigation"},
      {"Ctrl+F", "Find in page", "navigation"},
      {"Ctrl++ / Ctrl+-", "Zoom in / out", "navigation"},
      {"Ctrl+0", "Reset zoom", "navigation"},
      {"F11", "Fullscreen", "navigation"},
      {"Ctrl+Shift+I", "Developer tools", "developer"},
      {"Ctrl+Shift+B", "Benchmark page (offline, same file in any browser)", "developer"},
      {"Ctrl+Shift+Q", "Close window", "window"},
  };
  std::string out;
  out += "<h1>Keyboard shortcuts</h1>\n<p class=\"lede\">Everything a browser needs, and mode "
         "switching on Ctrl+1…9 so changing performance profile costs one keypress.</p>\n";
  out += "<div class=\"card\">\n";
  out += "<div class=\"row\" style=\"margin-bottom:12px\">";
  out += "<input type=\"search\" data-filter=\"#shortcut-table\" placeholder=\"Filter shortcuts…\" "
         "aria-label=\"Filter shortcuts\">";
  out += "<span class=\"pill\" data-filter-count=\"#shortcut-table\"></span></div>\n";
  out += "<table id=\"shortcut-table\"><thead><tr><th style=\"width:210px\">Keys</th>"
         "<th>Action</th><th style=\"width:110px\">Group</th></tr></thead><tbody>\n";
  for (const Row& row : rows) {
    out += "<tr data-search=\"" + EscapeImpl(row.keys) + " " + EscapeImpl(row.action) + " " +
           EscapeImpl(row.group) + "\"><td><kbd>" + EscapeImpl(row.keys) + "</kbd></td><td>" +
           EscapeImpl(row.action) + "</td><td class=\"note tight\">" + EscapeImpl(row.group) +
           "</td></tr>\n";
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
  if (data.session_json.empty()) {
    out += Missing("Session state", "Open a tab, then reload this page.");
    return out;
  }
  out += "<div class=\"card\"><div class=\"kpis\" id=\"tab-counters\"></div>";
  out += "<div class=\"grid\" id=\"tab-list\" style=\"margin-top:14px\"></div></div>\n";
  out += "<div class=\"card\"><details style=\"border:0;margin:0;padding:0\">";
  out += "<summary>Session JSON (the file that gets restored)</summary><pre>";
  out += EscapeImpl(data.session_json);
  out += "</pre></details></div>\n";
  return out;
}

std::string RenderAbout(const PageData& data) {
  std::string out;
  out += "<h1>About BrowserX</h1>\n<p class=\"lede\">An ULTRA-optimised Chromium browser in C++20 "
         "on CEF, with fifteen performance modes, Auto-Pilot and a tracker shield. The mobile edition "
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
         EscapeImpl(data.author) + "</strong> and is licensed for <strong>" +
         EscapeImpl(kProjectLicenceTerms) + "</strong>. Please respect that: it is what keeps the "
         "project alive.</p>";
  out += "<table><tbody>";
  out += "<tr><td>GitHub</td><td><a href=\"" + EscapeImpl(data.author_url) +
         "\" rel=\"noopener\">" + EscapeImpl(data.author_url) + "</a></td></tr>";
  out += "<tr><td>Website</td><td><a href=\"" + EscapeImpl(data.website_url) +
         "\" rel=\"noopener\">" + EscapeImpl(data.website) + "</a></td></tr>";
  out += "<tr><td>Repository</td><td><a href=\"" + EscapeImpl(data.repository_url) +
         "\" rel=\"noopener\">" + EscapeImpl(data.repository) + "</a></td></tr>";
  out += "<tr><td>Licence</td><td><a href=\"" + EscapeImpl(kProjectLicenceUrl) +
         "\" rel=\"noopener\">" + EscapeImpl(kProjectLicence) + "</a><br>"
         "<span class=\"note\">" + EscapeImpl(kProjectLicenceTerms) +
         ". Source-available, not open source. The bundled Chromium/CEF runtime keeps its own "
         "licences.</span></td></tr>";
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

// ---------------------------------------------------------------------------
// History and bookmarks
// ---------------------------------------------------------------------------
// These two pages read the same JSON the store writes. The rows are rendered here, in
// C++, rather than by script in the page, for one reason: BrowserX's own pages have to
// work with JavaScript switched off. The script at the bottom only adds filtering.

/// Milliseconds since the epoch, from the browser when it supplied one, otherwise from
/// the system clock. Never negative, so a page can always say when something happened.
int64_t PageNowMs(const PageData& data) {
  if (data.now_ms > 0) return data.now_ms;
  return static_cast<int64_t>(std::time(nullptr)) * 1000;
}

/// A readable host for a history row ("news.example"), falling back to the raw URL.
std::string HostLabel(std::string_view url_text) {
  const std::string host = core::url::HostOf(url_text);
  return host.empty() ? std::string(url_text) : host;
}

/// One row of a library table: when, what, and one more column the caller chooses
/// (visits for history, folder for bookmarks).
std::string LibraryRow(const std::string& url_text, const std::string& title_text, int64_t when_ms,
                      int64_t now_ms, const std::string& extra) {
  const std::string label = title_text.empty() ? HostLabel(url_text) : title_text;
  const std::string when = core::fmt::FormatRelativeTime(when_ms, now_ms);
  std::string out = "<tr data-search=\"" +
                    EscapeImpl(core::fmt::ToLower(url_text + " " + title_text + " " + extra)) + "\">";
  out += "<td class=\"when\"><time datetime=\"" + std::to_string(when_ms / 1000) +
         "\" title=\"" + EscapeImpl(when) + "\">" + EscapeImpl(when) + "</time></td>";
  out += "<td><a href=\"" + EscapeImpl(url_text) + "\">" + EscapeImpl(label) + "</a>";
  out += "<div class=\"host mono\">" + EscapeImpl(HostLabel(url_text)) + "</div></td>";
  out += "<td class=\"num\">" + EscapeImpl(extra) + "</td>";
  out += "</tr>\n";
  return out;
}

std::string LibraryTable(const std::vector<std::string>& rows, const char* extra_header) {
  std::string out = "<div class=\"card\"><table class=\"library\" id=\"library-table\">";
  out += "<thead><tr><th>When</th><th>Page</th><th class=\"num\">" + std::string(extra_header) +
         "</th></tr></thead><tbody>\n";
  for (const std::string& row : rows) out += row;
  out += "</tbody></table></div>\n";
  return out;
}

/// The filter box. It reuses the page's existing `data-filter` mechanism, so there is
/// exactly one filter implementation in the browser and it is covered by the page tests.
/// The table below is complete before the script runs: with scripting off nothing is lost.
std::string LibraryFilter(std::string_view placeholder, size_t total) {
  std::string out = "<div class=\"card\"><label class=\"field\"><span>Filter</span>";
  out += "<input type=\"search\" data-filter=\"#library-table\" placeholder=\"" +
         EscapeImpl(placeholder) + "\" autocomplete=\"off\"></label>";
  out += "<span class=\"pill\" data-filter-count=\"#library-table\">" +
         EscapeImpl(core::fmt::Pluralize(static_cast<int64_t>(total), "row")) + "</span>";
  out += "</div>\n";
  return out;
}

std::string RenderHistory(const PageData& data) {
  std::string out;
  out += "<h1>History</h1>\n<p class=\"lede\">Every page BrowserX has recorded, newest "
         "first. Tracking parameters are stripped before an address is stored, the list is "
         "capped, and nothing here ever leaves this machine.</p>\n";

  if (data.history_json.empty()) {
    return out + Missing("History", "Browse a page, then open this page again.");
  }
  std::string parse_error;
  auto parsed = core::json::Parse(data.history_json, &parse_error);
  if (!parsed.has_value()) {
    return out + Missing("History", "The history file could not be read: " + parse_error);
  }
  const core::json::Value& entries = parsed->Get("entries");
  const int64_t now_ms = PageNowMs(data);

  std::vector<std::string> rows;
  int64_t visits = 0;
  for (const core::json::Value& item : entries.AsArray()) {
    const std::string url_text = item.Get("url").AsString();
    if (url_text.empty()) continue;
    const int64_t count = item.Get("visits").AsInt64(1);
    visits += count;
    rows.push_back(LibraryRow(url_text, item.Get("title").AsString(),
                              item.Get("visited_ms").AsInt64(0), now_ms,
                              core::fmt::FormatNumber(count)));
  }

  if (data.library_is_sample) {
    out += "<p class=\"note warn\">Sample data: these rows are not this browser's history "
           "yet — the reader below is live, the store behind it fills as you browse.</p>\n";
  }
  if (rows.empty()) {
    return out + "<div class=\"card\"><p class=\"lede\">Nothing recorded yet. The first page "
                 "you open will appear here.</p></div>\n";
  }

  std::string kpis = "<div class=\"kpis\">";
  kpis += "<div class=\"kpi\"><span class=\"v\">" + EscapeImpl(core::fmt::FormatNumber(static_cast<int64_t>(rows.size()))) +
          "</span><span class=\"k\">pages</span></div>";
  kpis += "<div class=\"kpi\"><span class=\"v\">" + EscapeImpl(core::fmt::FormatNumber(visits)) +
          "</span><span class=\"k\">visits</span></div>";
  kpis += "</div>\n";
  out += kpis;
  out += LibraryFilter("Filter history", rows.size());
  out += LibraryTable(rows, "Visits");
  return out;
}

std::string RenderBookmarks(const PageData& data) {
  std::string out;
  out += "<h1>Bookmarks</h1>\n<p class=\"lede\">Pages you kept. Exported in the Netscape "
         "bookmark format, the one every browser still reads, so your list is yours and can "
         "leave with you.</p>\n";

  if (data.bookmarks_json.empty()) {
    return out + Missing("Bookmarks", "Press the star in the toolbar on a page you want to keep.");
  }
  std::string parse_error;
  auto parsed = core::json::Parse(data.bookmarks_json, &parse_error);
  if (!parsed.has_value()) {
    return out + Missing("Bookmarks", "The bookmarks file could not be read: " + parse_error);
  }
  const core::json::Value& entries = parsed->Get("entries");
  const int64_t now_ms = PageNowMs(data);

  std::vector<std::string> rows;
  std::vector<std::string> folders;
  for (const core::json::Value& item : entries.AsArray()) {
    const std::string url_text = item.Get("url").AsString();
    if (url_text.empty()) continue;
    const std::string folder = item.Get("folder").AsString();
    if (!folder.empty() &&
        std::find(folders.begin(), folders.end(), folder) == folders.end()) {
      folders.push_back(folder);
    }
    rows.push_back(LibraryRow(url_text, item.Get("title").AsString(),
                              item.Get("added_ms").AsInt64(0), now_ms,
                              folder.empty() ? std::string("\u2014") : folder));
  }

  if (data.library_is_sample) {
    out += "<p class=\"note warn\">Sample data: these rows are not this browser's bookmarks "
           "yet — the reader below is live, the store behind it fills as you bookmark.</p>\n";
  }
  if (rows.empty()) {
    return out + "<div class=\"card\"><p class=\"lede\">No bookmarks yet. Anything you keep "
                 "shows up here, with its folder if it has one.</p></div>\n";
  }

  std::string kpis = "<div class=\"kpis\">";
  kpis += "<div class=\"kpi\"><span class=\"v\">" + EscapeImpl(core::fmt::FormatNumber(static_cast<int64_t>(rows.size()))) +
          "</span><span class=\"k\">bookmarks</span></div>";
  kpis += "<div class=\"kpi\"><span class=\"v\">" + EscapeImpl(core::fmt::FormatNumber(static_cast<int64_t>(folders.size()))) +
          "</span><span class=\"k\">folders</span></div>";
  kpis += "</div>\n";
  out += kpis;
  out += LibraryFilter("Filter bookmarks", rows.size());
  out += LibraryTable(rows, "Folder");
  return out;
}

std::string RenderNotWired(PageKind kind) {
  std::string out = "<h1>";
  out += EscapeImpl(PageTitle(kind));
  out += "</h1>\n<p class=\"lede\">This page is part of the v1 feature set but its download item store is "
         "not wired up in this build yet — the browser is being built stage by stage, and the "
         "download list needs the CEF download callbacks, which the current build does not "
         "subscribe to yet.</p>\n";
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

/// The error page. It exists because "ERR_CONNECTION_REFUSED" is not an answer: the
/// user gets a sentence, what it usually means, and the two or three things worth trying.
/// Everything shown comes from core::ExplainNetError, so the wording is unit tested, and
/// an unknown code is reported as unknown with its number rather than guessed at.
std::string RenderError(const PageData& data) {
  const core::NetErrorHelp help = core::ExplainNetError(data.error_code);
  const bool known = help.family != core::NetErrorFamily::kUnknown;

  std::string out;
  out += "<h1>" + EscapeImpl(help.headline) + "</h1>\n";
  out += "<p class=\"lede\">" + EscapeImpl(help.explanation) + "</p>\n";

  // The address that failed, shown as it came: without it, an error page is a complaint
  // about nothing in particular.
  out += "<div class=\"card\"><table><tbody>";
  if (!data.error_url.empty()) {
    out += "<tr><th>Address</th><td><code>" + EscapeImpl(data.error_url) + "</code></td></tr>";
  }
  out += "<tr><th>What Chromium reported</th><td><code>" +
         EscapeImpl(core::FormatNetErrorCode(data.error_code)) + "</code></td></tr>";
  if (!data.error_text.empty()) {
    out += "<tr><th>Its own wording</th><td>" + EscapeImpl(data.error_text) + "</td></tr>";
  }
  out += "<tr><th>Kind of failure</th><td><code>" +
         EscapeImpl(core::NetErrorFamilyName(help.family)) + "</code></td></tr>";
  out += "</tbody></table></div>\n";

  out += "<div class=\"card\"><h2>What to try</h2><ol>";
  for (const std::string& step : help.try_first) {
    out += "<li>" + EscapeImpl(step) + "</li>";
  }
  out += "</ol></div>\n";

  // Two things must never be implied wrongly: that a security failure is a formality, and
  // that BrowserX is blameless when the browser itself made the decision.
  if (help.security_relevant) {
    out += "<div class=\"card warn\"><p class=\"note\"><strong>This one is about trust, "
           "not convenience.</strong> The connection could not be proven to be the site you "
           "asked for. BrowserX does not offer a \"continue anyway\" button for this, on "
           "purpose: the only safe next step is the one in the list above.</p></div>\n";
  }
  if (help.caused_by_browser) {
    out += "<div class=\"card\"><p class=\"note\">BrowserX made this decision, not the "
           "network and not the site - so it is also the thing you can change.</p></div>\n";
  }
  if (!known) {
    out += "<div class=\"card\"><p class=\"note\">This build has no explanation for that "
           "code, which is why none is offered: the number and Chromium's own wording are "
           "printed instead, because a wrong explanation would send you looking in the wrong "
           "place. browserx://about lists the version to report it against.</p></div>\n";
  }
  if (data.error_is_sample) {
    out += "<div class=\"card\"><p class=\"note\">This is an example page, not a real "
           "failure: nothing failed while rendering it. It is here so the layout can be read "
           "and checked without breaking a connection on purpose.</p></div>\n";
  }
  // The list of everything this build can explain, on the page itself: a promise that the
  // full list is "somewhere else" is how a page ends up describing a page that does not
  // exist. Codes the table does not know are absent here and are reported as unknown above.
  out += "<details class=\"card\"><summary>Every failure code this build can explain (" +
         std::to_string(core::KnownNetErrorCodes().size()) + ")</summary><table>"
         "<thead><tr><th>Code</th><th>Means</th></tr></thead><tbody>";
  for (int code : core::KnownNetErrorCodes()) {
    const core::NetErrorHelp help = core::ExplainNetError(code);
    out += "<tr><td><code>" + EscapeImpl(help.name) + "</code></td><td>" +
           EscapeImpl(help.headline) + "</td></tr>";
  }
  out += "</tbody></table><p class=\"note\">Anything not in this list is shown as its "
         "code and Chromium's wording, with no invented explanation.</p></details>\n";
  out += "<p class=\"note\">Errors are also written to the log with the URL and the code, "
         "so a report can name both.</p>\n";
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

namespace {
/// Percent-decodes a query value. Same rules as ParseModeSwitchRequest: a query string is
/// not text until it has been decoded once.
std::string DecodeQueryValue(std::string_view value) {
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
    decoded += value[i] == '+' ? ' ' : value[i];
  }
  return decoded;
}
}  // namespace

bool ParseErrorRequest(const InternalUrl& url, int* code_out, std::string* url_out) {
  if (KindForHost(url.host) != PageKind::kError) return false;
  bool found = false;
  for (const std::string& pair : core::fmt::Split(url.query, '&')) {
    const size_t equals = pair.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = pair.substr(0, equals);
    const std::string value = DecodeQueryValue(pair.substr(equals + 1));
    if (key == "code" && !value.empty()) {
      // Three spellings reach here: the number Chromium reports, "ERR_" plus a name, and
      // the bare name. All three are how people and logs refer to the same failure.
      int64_t number = 0;
      if (core::fmt::ParseInt(value, &number) && number < 0) {
        // A negative number outside the table is still a real code, and showing it as
        // unknown is the honest answer; only a positive or malformed value is a
        // parse failure.
        if (code_out != nullptr) *code_out = static_cast<int>(number);
        found = true;
      } else {
        const int by_name = core::NetErrorCodeForName(value);
        if (by_name != 0) {
          if (code_out != nullptr) *code_out = by_name;
          found = true;
        }
      }
    } else if (key == "url" && url_out != nullptr) {
      *url_out = value;
    }
  }
  return found;
}

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
    case PageKind::kError: return Shell(kind, data, RenderError(data));
    // The benchmark is served as its own complete document, not wrapped in Shell():
    // the same bytes have to work standalone in another browser, so it carries its own
    // styling. --write-bench-page writes exactly this.
    case PageKind::kBench: return BenchPageDocument();
    case PageKind::kHistory: return Shell(kind, data, RenderHistory(data));
    case PageKind::kBookmarks: return Shell(kind, data, RenderBookmarks(data));
    case PageKind::kDownloads: return Shell(kind, data, RenderNotWired(kind));
    case PageKind::kUnknown: break;
  }
  return Shell(PageKind::kUnknown, data,
               RenderNotFound(data.requested_host.empty() ? "unknown" : data.requested_host));
}

}  // namespace web
}  // namespace browserx
