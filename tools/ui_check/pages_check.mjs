// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app
//
// Renders every page the browser generates in a real DOM and checks what the pages
// promise: self-contained, script runs, palette opens and closes (ARIA state included),
// filters filter, readers have rows, mode tiles explain the switch, no console errors.
//
// Run:  make ui-check          (installs nothing; tells you if jsdom is missing)
//   or: node tools/ui_check/pages_check.mjs
//
// jsdom is a test-only dependency of the *preview harness*, never of the browser: the
// browser's own pages are dependency-free by design.

// Renders every page the browser generates in a real DOM, runs its own script, and checks
// the behaviour the pages promise: navigation, the Ctrl+K palette, the table filters, the
// mode diff line, the history/bookmark readers, and that no external resource is fetched.
import { readdirSync, readFileSync } from 'node:fs';
// jsdom is a test-only dependency: install it beside the checkout (`npm install jsdom`,
// no package.json needed) or point BROWSERX_JSDOM at an installed copy.
const jsdom_spec = process.env.BROWSERX_JSDOM || 'jsdom';
const { JSDOM, VirtualConsole } = await import(jsdom_spec);

const dir = process.argv[2] || '/home/user/MyBrowser/out/pages';
const files = readdirSync(dir).filter((f) => f.endsWith('.html'));
let failures = 0;
const fail = (file, message) => { failures++; console.log(`FAIL [${file}] ${message}`); };
const ok = (file, message) => console.log(`  ok [${file}] ${message}`);

for (const file of files) {
  const html = readFileSync(`${dir}/${file}`, 'utf8');
  const console_errors = [];
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => console_errors.push(String(e.message || e)));
  vc.on('error', (m) => console_errors.push(String(m)));
  const dom = new JSDOM(html, { runScripts: 'dangerously', virtualConsole: vc, url: 'browserx://' + file.replace('.html', '') });
  const doc = dom.window.document;

  // 1. every page must be self-contained: no external fetch of any kind
  for (const el of doc.querySelectorAll('link[href], script[src], img[src], iframe[src]')) {
    const url = el.getAttribute('href') || el.getAttribute('src') || '';
    if (/^https?:/i.test(url)) fail(file, `external resource: ${url}`);
  }
  // 2. the top bar and the nav are on every page
  if (!doc.querySelector('.topbar')) fail(file, 'no top bar');
  const nav = doc.querySelectorAll('.topbar nav a');
  if (nav.length < 5) fail(file, `nav lists only ${nav.length} pages`);
  if (nav.length > 9) fail(file, `nav lists ${nav.length} pages: the bar is meant to be curated, the palette is where every page lives`);
  // 3. Ctrl+K palette: the button opens it, typing filters, Escape closes
  const open = doc.getElementById('palette-open');
  if (!open) {
    fail(file, 'no palette button');
  } else {
    open.dispatchEvent(new dom.window.Event('click', { bubbles: true }));
    const palette = doc.getElementById('palette');
    if (!palette || palette.hidden) fail(file, 'palette did not open');
    const input = doc.getElementById('palette-input');
    input.value = 'telemetry';
    input.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
    const rows = doc.querySelectorAll('#palette-list li');
    if (rows.length === 0) fail(file, 'palette found nothing for "telemetry"');
    if (palette.getAttribute('aria-hidden') !== 'false') {
      fail(file, 'an open palette must announce itself as visible (aria-hidden="false")');
    }
    input.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    if (palette.classList.contains('open')) fail(file, 'Escape did not close the palette');
    if (palette.getAttribute('aria-hidden') !== 'true') {
      fail(file, 'a closed palette must announce itself as hidden');
    }
    // The palette is the only place every page is listed, so it must list all of them.
    open.dispatchEvent(new dom.window.Event('click', { bubbles: true }));
    const all = doc.querySelectorAll('#palette-list li').length;
    if (all < 14) fail(file, `the palette lists ${all} destinations, fewer than the 14 pages`);
    doc.querySelector('#palette-input').dispatchEvent(
      new dom.window.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
  }
  // 4. filters: the pages that have a table filter must actually filter
  const filter = doc.querySelector('[data-filter]');
  if (filter) {
    const target = doc.querySelector(filter.dataset.filter);
    if (!target) {
      fail(file, `filter targets ${filter.dataset.filter}, which does not exist`);
    } else {
      const before = target.querySelectorAll('tbody tr').length;
      filter.value = 'zzz';
      filter.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
      const nothing = [...target.querySelectorAll('tbody tr')].filter((r) => !r.hidden).length;
      if (before === 0) fail(file, 'table has no rows to filter');
      if (nothing !== 0) fail(file, `filter "zzz" left ${nothing} rows visible`);
      const visible = [...target.querySelectorAll('tbody tr')].length;
      const counter = doc.querySelector(`[data-filter-count="${filter.dataset.filter}"]`);
      if (!counter) fail(file, 'no result counter for the filter');
      filter.value = '';
      filter.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
      const restored = [...target.querySelectorAll('tbody tr')].filter((r) => !r.hidden).length;
      if (restored !== visible) fail(file, 'clearing the filter did not restore the rows');
    }
  }
  // 5. the pages that render live data must have rendered it
  if (file === 'history.html') {
    const rows = doc.querySelectorAll('#library-table tbody tr');
    if (rows.length === 0) fail(file, 'history reader rendered no rows');
    if (!doc.body.textContent.includes('Sample data')) fail(file, 'sample data is not labelled as sample');
    if (doc.body.textContent.includes('1970')) fail(file, 'a relative time fell back to the epoch');
  }
  if (file === 'bookmarks.html') {
    if (doc.querySelectorAll('#library-table tbody tr').length === 0) fail(file, 'no bookmark rows');
  }
  if (file === 'modes.html') {
    const diffs = doc.querySelectorAll('.tile .diff');
    if (diffs.length < 10) fail(file, `only ${diffs.length} mode tiles explain what switching does`);
    const active = doc.querySelector('.tile[aria-current="true"]');
    if (active && active.querySelector('.diff')) fail(file, 'the active mode tile claims something would change');
    for (const d of diffs) {
      if (/switchs| 1 switches/.test(d.textContent)) fail(file, `grammar: ${d.textContent}`);
    }
  }
  if (file === 'session.html' && html.includes('__browserxSession')) {
    if (!doc.getElementById('tab-list').children.length) {
      // tab-list is filled by script; with sample session data it must not stay empty
      fail(file, 'the session page did not render the tab list from its own JSON');
    }
  }
  if (console_errors.length) fail(file, `console errors: ${console_errors.slice(0, 2).join(' | ')}`);
  if (!failures) ok(file, 'self-contained, script runs, interactions behave');
  dom.window.close();
}
console.log(failures === 0 ? `\nall ${files.length} pages pass\n` : `\n${failures} problem(s)\n`);
process.exit(failures === 0 ? 0 : 1);
