// Drives the preview UI against the running server: the tabs, the live plan data, the
// page picker and the frame that loads the browser's own rendered pages.
// jsdom is a test-only dependency: install it beside the checkout (`npm install jsdom`,
// no package.json needed) or point BROWSERX_JSDOM at an installed copy.
const jsdom_spec = process.env.BROWSERX_JSDOM || 'jsdom';
const { JSDOM, VirtualConsole } = await import(jsdom_spec);

const base = process.argv[2] || 'http://localhost:8770';

// The preview server is not part of the build: if it is not running, say so and stop. This
// is a skip, not a pass and not a failure - a crashed stack trace would tell the reader
// nothing about the pages.
try {
  await fetch(`${base}/`, { signal: AbortSignal.timeout(4000) });
} catch {
  console.log(`\n  preview not running at ${base} — skipping the drive-through.`);
  console.log('  start it with:  make preview\n');
  process.exit(0);
}

const html = await (await fetch(`${base}/`)).text();
const errors = [];
const vc = new VirtualConsole();
vc.on('jsdomError', (e) => errors.push(String(e.message || e)));

const dom = new JSDOM(html, {
  runScripts: 'dangerously',
  resources: 'usable',
  virtualConsole: vc,
  url: `${base}/`,
  pretendToBeVisual: true,
  // jsdom has no fetch of its own, so the preview's own error path would trigger
  // ("Could not read the page list") and nothing past the first request would run.
  // A real browser has it; give the harness the node implementation, resolved against the
  // preview's own origin so relative URLs behave the same way.
  beforeParse(window) {
    window.fetch = (input, init) => fetch(new URL(String(input), `${base}/`), init);
  },
});
const { window } = dom;
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
await wait(2500); // let app.js fetch the live data
const doc = window.document;

let failures = 0;
const check = (cond, label) => {
  if (cond) console.log(`  ok   ${label}`);
  else { failures++; console.log(`  FAIL ${label}`); }
};

check(doc.querySelectorAll('.tab').length >= 4, `preview has ${doc.querySelectorAll('.tab').length} tabs`);

// the pages tab: a picker fed by /api/pages, and a frame that loads one of them
const pagesChip = [...doc.querySelectorAll('.tab')].find((t) => /pages/i.test(t.textContent));
console.log(`  ·  tabs: ${[...doc.querySelectorAll('.tab')].map((t) => t.textContent.trim().replace(/\u00d7$/, '')).join(', ')}`);
if (pagesChip) {
  pagesChip.dispatchEvent(new window.Event('click', { bubbles: true }));
  await wait(1500);
  const chips = doc.querySelectorAll('#pages-picker .page-chip, #pages-picker button, #pages-picker a');
  check(chips.length >= 10, `page picker lists ${chips.length} pages`) && console.log('  ·  first chips: ' + [...chips].slice(0,3).map(c=>c.textContent.trim().slice(0,22)).join(' | '));
  const frame = doc.querySelector('#pages-frame');
  const src = frame ? frame.getAttribute('src') : null;
  check(Boolean(src && /\/pages\/.+\.html$/.test(src)), `frame loads a rendered page (${src})`);
  const modes = [...chips].find((c) => /modes/i.test(c.textContent));
  if (modes) {
    modes.dispatchEvent(new window.Event('click', { bubbles: true }));
    await wait(800);
    const after = doc.querySelector('#pages-frame').getAttribute('src');
    check(/modes\.html$/.test(after), `clicking a chip loads that page (${after})`);
  }
} else {
  failures++;
  console.log('  FAIL no pages tab in the preview');
}

// the improvements tab: the ledger, read from the checker, shown in the preview
const improvements = doc.querySelector('.tab[data-tab="improvements"]');
check(Boolean(improvements), 'the preview has an Improvements tab');
if (improvements) {
  improvements.dispatchEvent(new window.Event('click', { bubbles: true }));
  await wait(500);
  const panel = doc.querySelector('section[data-page="improvements"]');
  check(Boolean(panel && !panel.hidden), 'clicking it opens the ledger panel');
  const rows = panel ? panel.querySelectorAll('#improvements-table tbody tr') : [];
  const apiCount = (await (await fetch(`${base}/api/improvements`)).json()).entries.length;
  check(rows.length === apiCount,
    `the ledger table lists every entry (${rows.length} rows for ${apiCount} entries)`);
  const text = panel ? panel.textContent : '';
  check(text.includes('<title>'), 'text containing angle brackets is shown as text, not as markup');
  const kpis = panel ? panel.querySelectorAll('#improvements-kpis .kpi').length : 0;
  check(kpis >= 3, `the panel shows ${kpis} headline numbers`);
  const api = await (await fetch(`${base}/api/improvements`)).json();
  check(api.ok === true && api.count >= 50, `the API reports ${api.count} improvements, ok=${api.ok}`);
  check((api.problems || []).length === 0, `every anchor still exists (${(api.problems || []).length} problems)`);
}

check(errors.length === 0, `no console errors (${errors.slice(0, 2).join(' | ')})`);
console.log(failures === 0 ? '\npreview UI pass\n' : `\n${failures} preview problem(s)\n`);
window.close();
process.exit(failures === 0 ? 0 : 1);
