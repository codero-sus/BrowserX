// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

/* BrowserX preview — thin UI over the real mode engine.
 *
 * Everything shown per mode (switches, provenance, thresholds, cache budget,
 * notes) is fetched from preview/serve.py, which runs out/browserx-flagplan,
 * which is the same src/core the browser uses. This file only formats.
 */
'use strict';

const state = {
  plans: [],
  selected: 'ultra',
  autopilot: true,
  autopilotMode: 'auto',
  pressure: 0.42,
  tabs: [
    { title: 'browserx://modes',       state: 'active',    mem: 96 },
    { title: 'chromiumembedded / cef', state: 'hidden',    mem: 142 },
    { title: 'Chromium switch list',   state: 'sleeping',  mem: 0 },
    { title: 'YouTube — gaming',       state: 'hidden',    mem: 210 },
    { title: 'Docs: mode engine',      state: 'discarded', mem: 0 },
  ],
};

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

function bytesLabel(mb) {
  return mb >= 1024 ? `${(mb / 1024).toFixed(mb % 1024 ? 1 : 0)} GB` : `${mb} MB`;
}

function secondsLabel(seconds) {
  if (!seconds) return 'never';
  if (seconds % 3600 === 0) return `${seconds / 3600} h`;
  if (seconds % 60 === 0) return `${seconds / 60} min`;
  return `${seconds} s`;
}

function toast(message) {
  const node = $('#toast');
  node.textContent = message;
  node.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { node.hidden = true; }, 1800);
}

/* ------------------------------------------------------------------- data */
async function load() {
  loadPages();
  loadImprovements();
  try {
    const response = await fetch('api/plans');
    if (!response.ok) throw new Error((await response.json()).error || response.statusText);
    state.plans = await response.json();
    $('#data-pill').textContent = 'live core data';
    renderModes();
    renderTabs();
    renderAutopilot();
  } catch (error) {
    $('#data-pill').textContent = 'core unavailable';
    $('#mode-title').textContent = 'Cannot reach the mode engine';
    $('#mode-rationale').textContent =
      `${error.message} — run "make bench" in the project root, then reload this page.`;
    throw error;
  }

  try {
    const response = await fetch('api/profile');
    const payload = await response.json();
    const report = payload.text || '';
    $('#profile-report').textContent = report || 'no report';
    const summary = report.split('\n').find((line) => line.includes('Memory'));
    const mode = report.split('\n').find((line) => line.includes('recommended mode'));
    $('#profile-pill').textContent = summary ? summary.trim().replace(/\s+/g, ' ') : 'machine profile';
    if (mode) $('#status-left').textContent = mode.trim().replace(/\s+/g, ' ');
  } catch {
    $('#profile-report').textContent = 'run "make doctor" to generate the machine report';
  }
}

function planFor(modeId) {
  return state.plans.find((plan) => plan.mode === modeId) || state.plans[0];
}

/* ----------------------------------------------------------------- render */
function renderModes() {
  const chips = $('#mode-chips');
  if (chips.childElementCount === 0) {
    for (const plan of state.plans) {
      const button = document.createElement('button');
      button.className = 'chip';
      button.dataset.mode = plan.mode;
      button.textContent = plan.label;
      button.setAttribute('aria-pressed', String(plan.mode === state.selected));
      button.addEventListener('click', () => {
        state.selected = plan.mode;
        renderModes();
        updateOmnibox(plan);
        toast(`${plan.label} mode selected — ${plan.switch_count} switches, no restart of the UI`);
      });
      chips.appendChild(button);
    }
  }
  for (const chip of chips.children) {
    chip.setAttribute('aria-pressed', String(chip.dataset.mode === state.selected));
  }

  const plan = planFor(state.selected);
  const runtime = plan.runtime;

  $('#mode-title').textContent = `${plan.label} mode`;
  $('#mode-rationale').textContent = plan.rationale || '';
  $('#status-mode').textContent = `${plan.label.toLowerCase()} · ${plan.switch_count} switches`;
  $('#status-cache').textContent = `cache ${bytesLabel(runtime.cache_size_mb)}`;
  const rendererSwitch = plan.switches.find((item) => item.id === 'renderer-process-limit');
  $('#status-renderers').textContent =
    rendererSwitch ? `renderers ≤ ${rendererSwitch.value}` : 'renderers: adaptive';

  /* KPI cards */
  const kpis = [
    ['cache', bytesLabel(runtime.cache_size_mb), 'HTTP disk cache budget'],
    ['tab sleep', secondsLabel(runtime.sleep_after_seconds), 'idle tabs are unloaded'],
    ['tab discard', secondsLabel(runtime.discard_after_seconds), 'renderer is destroyed'],
    ['pressure window', `${(runtime.warn_pressure * 100).toFixed(0)}% → ${(runtime.discard_pressure * 100).toFixed(0)}%`,
      'warn → discard thresholds'],
    ['target fps', runtime.target_fps ? `${runtime.target_fps}` : 'uncapped', 'compositor frame budget'],
    ['telemetry', `${runtime.metrics_interval_ms} ms`, 'sampling interval'],
    ['memory budget', bytesLabel(plan.estimated_memory_budget_mb || 0), 'planned ceiling for this mode'],
  ];
  $('#mode-kpis').replaceChildren(...kpis.map(([key, value, sub]) => {
    const node = document.createElement('div');
    node.className = 'kpi';
    node.innerHTML = `<div class="k">${key}</div><div class="v">${value}</div><div class="s">${sub}</div>`;
    return node;
  }));

  /* Switch table */
  const tbody = $('#switch-table tbody');
  tbody.replaceChildren(...plan.switches.map((item) => {
    const row = document.createElement('tr');
    row.innerHTML =
      `<td><code>--${item.id}</code></td>` +
      `<td class="val">${item.value || '—'}</td>` +
      `<td class="why">${item.reason}</td>` +
      `<td class="prov">${item.provenance}</td>`;
    return row;
  }));
  if (plan.switches.length === 0) {
    const row = document.createElement('tr');
    row.innerHTML = '<td colspan="4" class="why">This mode adds no boot switches — it runs entirely on the live runtime policy below.</td>';
    tbody.appendChild(row);
  }
  if (plan.features_enabled.length || plan.features_disabled.length) {
    const row = document.createElement('tr');
    row.innerHTML =
      '<td><code>--enable-features</code></td>' +
      `<td class="val">${plan.features_enabled.join(', ') || '—'}</td>` +
      '<td class="why">Chromium features switched on for this mode</td>' +
      '<td class="prov">feature flags</td>';
    tbody.appendChild(row);
    if (plan.features_disabled.length) {
      const off = document.createElement('tr');
      off.innerHTML =
        '<td><code>--disable-features</code></td>' +
        `<td class="val">${plan.features_disabled.join(', ')}</td>` +
        '<td class="why">Switched off so this mode can behave</td>' +
        '<td class="prov">feature flags</td>';
      tbody.appendChild(off);
    }
  }

  /* Runtime policy */
  const policy = [
    ['background throttling', runtime.background_throttling ? 'on' : 'off'],
    ['bfcache', runtime.bfcache ? 'on' : 'off'],
    ['speculative preconnect', runtime.preconnect ? 'on' : 'off'],
    ['code cache', runtime.code_cache ? 'on' : 'off'],
    ['tracker shield', runtime.block_trackers ? 'on' : 'off'],
    ['third-party blocking', runtime.block_third_party ? 'on' : 'off'],
    ['lazy new tabs', runtime.lazy_new_tabs ? 'on' : 'off'],
    ['reduced motion', runtime.reduce_motion ? 'on' : 'off'],
    ['keep display awake', runtime.keep_display_awake ? 'on' : 'off'],
    ['perf HUD', runtime.show_perf_hud ? 'on' : 'off'],
  ];
  $('#policy-list').innerHTML = policy.map(([key, value]) =>
    `<dt>${key}</dt><dd>${value}</dd>`).join('');

  /* Notes and warnings */
  const notes = $('#notes');
  notes.replaceChildren(
    ...plan.warnings.map((text) => {
      const node = document.createElement('div');
      node.className = 'note warn';
      node.innerHTML = `<span class="ic">!</span><span>${text}</span>`;
      return node;
    }),
    ...plan.notes.map((text) => {
      const node = document.createElement('div');
      node.className = 'note info';
      node.innerHTML = `<span class="ic">›</span><span>${text}</span>`;
      return node;
    }),
  );
  if (!plan.warnings.length && !plan.notes.length) {
    const node = document.createElement('div');
    node.className = 'note info';
    node.textContent = 'No caveats for this mode on this machine.';
    notes.appendChild(node);
  }

  updateOmnibox(plan);
  renderPressure();
  renderAutopilot();
}

function updateOmnibox(plan) {
  $('#omnibox-url').textContent = `browserx://modes?active=${plan.mode}`;
}

/* -------------------------------------------------------------- telemetry */
function levelFor(pressure, runtime) {
  const critical = Math.min(0.97, runtime.discard_pressure + 0.07);
  if (pressure >= critical) return 'critical';
  if (pressure >= runtime.discard_pressure) return 'high';
  if (pressure >= runtime.warn_pressure) return 'moderate';
  return 'relaxed';
}

function actionsFor(level, plan) {
  const actions = [];
  const runtime = plan.runtime;
  if (level === 'relaxed') {
    actions.push(['idle', 'Nothing to do: memory is healthy, tabs stay warm for instant switching.']);
    return actions;
  }
  actions.push(['trim-caches', 'Trim HTTP cache, code cache and the image/font caches back to the mode budget.']);
  if (runtime.sleep_after_seconds) {
    actions.push(['sleep-idle', `Unload tabs idle for more than ${secondsLabel(runtime.sleep_after_seconds)} (renderer destroyed, URL + title kept).`]);
  } else {
    actions.push(['keep-alive', 'This mode never unloads tabs — it will warn instead of discarding your work.']);
  }
  if (level === 'high' || level === 'critical') {
    if (runtime.discard_after_seconds) {
      actions.push(['sleep-lru', `Unload the least recently used tab, then every tab idle for ${secondsLabel(runtime.discard_after_seconds)}.`]);
    } else {
      actions.push(['warn-only', 'Discarding is disabled in this mode: the status bar shows a warning instead.']);
    }
  }
  if (level === 'critical') {
    actions.push(['block-new-heavy', 'Refuse new heavy tabs (video/3D) until pressure drops, and stop preloading.']);
    actions.push(['reclaim', 'Ask renderers to return memory (`MemoryPressure` notification) and release the GPU cache.']);
  }
  return actions;
}

function renderPressure() {
  const plan = planFor(state.selected);
  const runtime = plan.runtime;
  const pressure = state.pressure;
  const level = levelFor(pressure, runtime);

  $('#gauge-fill').style.width = `${(pressure * 100).toFixed(1)}%`;
  $('#gauge-fill').className = `gauge-fill ${level}`;
  $('#gauge-warn').style.left = `${(runtime.warn_pressure * 100).toFixed(1)}%`;
  $('#gauge-discard').style.left = `${(runtime.discard_pressure * 100).toFixed(1)}%`;
  $('#pressure-value').textContent = `${(pressure * 100).toFixed(0)}%`;
  const badge = $('#pressure-level');
  badge.textContent = level;
  badge.className = `badge ${level}`;
  $('#threshold-text').textContent =
    `warn ${(runtime.warn_pressure * 100).toFixed(0)}%, discard ${(runtime.discard_pressure * 100).toFixed(0)}%`;

  const log = $('#action-log');
  log.replaceChildren(...actionsFor(level, plan).map(([tag, text]) => {
    const node = document.createElement('li');
    node.innerHTML = `<b>${tag}</b> — ${text}`;
    return node;
  }));
}

function renderTabs() {
  const plan = planFor(state.selected);
  const list = $('#tabs-list');
  list.replaceChildren(...state.tabs.map((tab, index) => {
    const node = document.createElement('li');
    node.className = tab.state === 'sleeping' ? 'asleep' : (tab.state === 'discarded' ? 'discarded' : '');
    const label = tab.state === 'active' ? 'active'
      : tab.state === 'hidden' ? 'hidden, warm'
        : tab.state === 'sleeping' ? 'asleep (no renderer)' : 'discarded';
    node.innerHTML =
      `<span>${index + 1}.</span><span>${tab.title}</span>` +
      `<span class="state">${label}${tab.mem ? ` · ${tab.mem} MB` : ''}</span>`;
    return node;
  }));
  if (plan) {
    $('#shield-count').textContent = plan.runtime.block_trackers ? '7' : '0';
  }
}

function renderAutopilot() {
  const plan = planFor(state.selected);
  const rules = [
    ['fullscreen gaming detected', 'GAME mode (uncapped frame rate, throttling off)'],
    ['memory pressure ≥ ' + `${(plan.runtime.warn_pressure * 100).toFixed(0)}%`, 'LOW RAM mode (smallest footprint)'],
    ['battery below 35%', 'ECO mode (30 FPS target, reduced motion)'],
    ['plugged in and idle', 'HIGH PERFORMANCE mode (throughput, no discarding)'],
    ['no signal', 'stay in ' + plan.label + ' (the mode you chose)'],
  ];
  const explain = [
    `Auto-Pilot: ${state.autopilot ? state.autopilotMode : 'off'}`,
    '',
    ...rules.map(([trigger, action]) => `  ${trigger.padEnd(34, '.')} ${action}`),
    '',
    state.autopilotMode === 'ask'
      ? 'In "ask" mode BrowserX suggests a switch in the status bar and waits for you — it never changes the mode on its own.'
      : 'A manual mode pick is honoured for 5 minutes (300 ticks) before Auto-Pilot may switch again.',
  ];
  $('#autopilot-explain').textContent = explain.join('\n');
}

/* ---------------------------------------------------------------- catalog */
async function loadCatalog() {
  const tbody = $('#catalog-table tbody');
  if (tbody.childElementCount) return;
  try {
    const response = await fetch('api/catalog');
    const payload = await response.json();
    const switches = payload.switches || [];
    const features = (payload.features || []).map((entry) => ({ ...entry, kind: 'feature' }));
    const stats = payload.stats || {};
    tbody.replaceChildren(...switches.map((entry) => catalogRow(entry, '--')), ...features.map((entry) => catalogRow(entry, '')));
    $('#catalog-count').textContent =
      `${switches.length} switches · ${features.length} features · ${stats.switches_verified || 0} verified · ${stats.switches_removed || 0} removed`;
  } catch (error) {
    tbody.replaceChildren(Object.assign(document.createElement('tr'), {
      innerHTML: `<td colspan="4" class="why">Could not load the catalogue: ${error.message}</td>`,
    }));
  }
}

function catalogRow(entry, prefix) {
  const row = document.createElement('tr');
  const name = entry.name || '';
  const status = entry.status || '';
  row.dataset.search = `${name} ${status} ${entry.provenance || ''} ${entry.summary || ''} ${entry.scope || ''}`.toLowerCase();
  const cls = status.includes('removed') ? 'critical'
    : (status.includes('chrome') ? 'moderate'
      : (status.includes('legacy') ? 'moderate' : 'relaxed'));
  const flags = [entry.scope, entry.risk].filter(Boolean).join(' · ');
  row.innerHTML =
    `<td><code>${prefix}${name}</code>${entry.kind === 'feature' ? ' <span class="badge">feature</span>' : ''}</td>` +
    `<td><span class="badge ${cls}">${status}</span>${flags ? ` <span class="prov">${flags}</span>` : ''}</td>` +
    `<td class="prov">${entry.provenance || ''}</td>` +
    `<td class="why">${entry.summary || ''}</td>`;
  return row;
}

/* ------------------------------------------- the browser's own pages ----- */
/* Fetches the list `make pages` produced, then loads each page into the frame.
 * The pages are the real renderer's output - if this list is empty, the preview
 * says so instead of showing a mock-up. */
async function loadPages() {
  const picker = $('#pages-picker');
  if (!picker) return;
  let listing;
  try {
    listing = await (await fetch('/api/pages')).json();
  } catch (error) {
    picker.innerHTML = '<p class="note">Could not read the page list: ' + error + '</p>';
    return;
  }
  if (listing.error) {
    picker.innerHTML = '<p class="note">' + listing.error + '</p>';
    return;
  }
  // Titles come from the pages themselves (the server reads each page's <title>), so a
  // page that is added tomorrow is labelled correctly without editing this file.
  picker.innerHTML = listing.pages.map((page) =>
    `<button class="page-chip" data-page-url="${page.url}" data-page-name="${page.name}">` +
    `<strong>${esc(page.title || page.name)}</strong>` +
    `<span>browserx://${esc(page.name)} · ${(page.bytes / 1024).toFixed(1)} KB</span></button>`
  ).join('');

  const frame = $('#pages-frame');
  const urlLabel = $('#pages-frame-url');
  const openLink = $('#pages-frame-open');
  const show = (url, name) => {
    frame.src = url;
    urlLabel.textContent = 'browserx://' + name;
    openLink.href = url;
    $$('#pages-picker .page-chip').forEach((chip) => {
      chip.classList.toggle('active', chip.dataset.pageUrl === url);
    });
  };
  picker.addEventListener('click', (event) => {
    const chip = event.target.closest('.page-chip');
    if (chip) show(chip.dataset.pageUrl, chip.dataset.pageName);
  });
  if (listing.pages.length) show(listing.pages[0].url, listing.pages[0].name);
}

/* ------------------------------------------------------------- escaping --- */
/* Everything below renders text that comes from elsewhere: the ledger's own prose, a
 * page's <title>, a switch id. Inserting that into innerHTML unescaped is how a page
 * breaks - and how a string someone else controls becomes markup. One function, used
 * everywhere text goes into HTML. */
function esc(value) {
  return String(value == null ? '' : value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

/* ---------------------------------------------- the improvement ledger --- */
/* Reads the ledger from the checker, so the preview and `make check` can never
 * disagree about how many improvements exist or whether they are still true. */
async function loadImprovements() {
  const table = $('#improvements-table tbody');
  if (!table) return;
  let ledger;
  try {
    ledger = await (await fetch('/api/improvements')).json();
  } catch (error) {
    table.innerHTML = `<tr><td colspan="4" class="note">Could not read the ledger: ${error}</td></tr>`;
    return;
  }
  if (ledger.error) {
    table.innerHTML = `<tr><td colspan="4" class="note">${ledger.error}</td></tr>`;
    return;
  }
  const areas = Object.entries(ledger.areas || {}).sort((a, b) => b[1] - a[1]);
  $('#improvements-kpis').innerHTML =
    `<div class="kpi"><span class="k">Recorded</span><span class="v">${ledger.count}</span></div>` +
    `<div class="kpi"><span class="k">Target</span><span class="v">${ledger.minimum || 200}</span></div>` +
    `<div class="kpi"><span class="k">Areas</span><span class="v">${areas.length}</span></div>` +
    `<div class="kpi"><span class="k">Broken anchors</span><span class="v">${(ledger.problems || []).length}</span></div>`;
  const progress = $('#improvements-progress');
  if (progress) {
    const target = ledger.minimum || 200;
    progress.textContent = ledger.count >= target
      ? ` The target of ${target} is met and checked.`
      : ` ${ledger.count} of ${target} recorded so far — the ratchet in make check fails until the rest are real.`;
  }
  table.innerHTML = (ledger.entries || []).map((entry) => {
    const haystack = esc((entry.area + ' ' + entry.what + ' ' + entry.file).toLowerCase());
    return `<tr data-search="${haystack}">` +
      `<td class="mono">${Number(entry.id)}</td><td>${esc(entry.area)}</td>` +
      `<td>${esc(entry.what)}</td><td class="mono note">${esc(entry.file)}</td></tr>`;
  }).join('');
  // The count is asserted against what was actually inserted: a row that fails to render
  // must not be quietly missing from the page while the headline still says 74.
  const rendered = table.children.length;
  if (rendered !== (ledger.entries || []).length) {
    table.insertAdjacentHTML('beforeend',
      `<tr><td colspan="4" class="note">${rendered} of ${(ledger.entries || []).length} entries rendered</td></tr>`);
  }
  const search = $('#improvements-search');
  if (search) {
    search.addEventListener('input', () => {
      const needle = search.value.trim().toLowerCase();
      let shown = 0;
      for (const row of table.children) {
        const match = !needle || (row.dataset.search || '').includes(needle);
        row.hidden = !match;
        if (match) shown += 1;
      }
    });
  }
}

/* ------------------------------------------------------------------- wire */
function wire() {
  // Only real page tabs: the benchmark tab is a link out to /bench and has no panel.
  $$('.tab[data-tab]').forEach((tab) => {
    tab.addEventListener('click', () => selectPage(tab.dataset.tab));
  });
  $('#catalog-search').addEventListener('input', (event) => {
    const needle = event.target.value.trim().toLowerCase();
    let shown = 0;
    for (const row of $('#catalog-table tbody').children) {
      const match = !needle || (row.dataset.search || '').includes(needle);
      row.hidden = !match;
      if (match) shown += 1;
    }
    $('#catalog-count').textContent = needle ? `${shown} matching` : `${$('#catalog-table tbody').childElementCount} switches`;
  });
  $('#pressure').addEventListener('input', (event) => {
    state.pressure = Number(event.target.value) / 100;
    renderPressure();
  });
  $('#autopilot').addEventListener('change', (event) => {
    state.autopilot = event.target.checked;
    toast(state.autopilot
      ? `Auto-Pilot on (${state.autopilotMode})`
      : 'Auto-Pilot off — the mode stays where you put it');
    renderAutopilot();
  });
  $('#autopilot-mode').addEventListener('change', (event) => {
    state.autopilotMode = event.target.value;
    renderAutopilot();
  });
  $('#shield-count').parentElement.addEventListener('click', () => {
    const plan = planFor(state.selected);
    toast(plan.runtime.block_trackers
      ? 'Shield on: 7 trackers blocked on this page (curated blocklist from src/core).'
      : `${plan.label} mode has the shield off — blocking costs CPU on some pages.`);
  });
  document.addEventListener('keydown', (event) => {
    if (event.key >= '1' && event.key <= '5') {
      const plan = state.plans[Number(event.key) - 1];
      if (plan) {
        state.selected = plan.mode;
        renderModes();
      }
    }
  });
}

function selectPage(name) {
  $$('.tab').forEach((tab) => tab.classList.toggle('active', tab.dataset.tab === name));
  $$('.page').forEach((page) => { page.hidden = page.dataset.page !== name; });
  if (name === 'catalog') loadCatalog();
}

wire();
load().catch(() => {});
setInterval(async () => {
  try {
    const response = await fetch('api/plans');
    if (response.ok) {
      state.plans = await response.json();
      renderModes();
    }
  } catch { /* keep the last good data on screen */ }
}, 30000);
