# BrowserX — improvement ledger

Every row below is a change that exists in this repository right now, and every row is
**checked against the code**: the last column is a string that must be found in the file
named before it. `make check` runs the verification, so an entry cannot survive the thing
it describes.

```bash
python3 tools/check_improvements.py --list   # every entry
python3 tools/check_improvements.py --min N  # the ratchet the build enforces
```

Areas, roughly in the order a user meets them: **UX/UI**, **modes & performance**,
**privacy & security**, **robustness**, **tooling & docs**, **mobile**, **tests**.

Two area names are not categories of feature but promises the project keeps: **Honesty**
(a claim that is only true when the code is, and a page that says what it cannot yet do)
and **Accessibility** (state a screen reader can rely on). **Hygiene** marks removals of
duplication, and rows marked *bug fix* are defects the new tests found in the code they
were written beside.

| # | Area | Improvement | Where | Anchor |
|---|------|-------------|-------|--------|
| 1 | Robustness | Origin of a URL (scheme + host + non-default port) in one tested helper | src/core/url_utils.cc | `std::string OriginOf(std::string_view url_text)` |
| 2 | Privacy | Host can be recognised as an IP literal, so a bare address can be shown for what it is | src/core/url_utils.cc | `bool IsIpLiteralHost(std::string_view host)` |
| 3 | Privacy | Tracking parameters can be stripped from a link without losing the #fragment | src/core/url_utils.cc | `std::string WithoutQueryParam(std::string_view url, std::string_view name)` |
| 4 | Robustness | Query parameters can be read back with percent- and plus-decoding | src/core/url_utils.cc | `std::string QueryParam(std::string_view url, std::string_view name)` |
| 5 | Privacy | Registrable domain (eTLD+1) with a curated multi-label suffix list, so 'example.co.uk' is one site | src/core/url_utils.cc | `std::string RegistrableDomain(std::string_view host)` |
| 6 | Privacy | Same-site comparison across subdomains, using the registrable domain | src/core/url_utils.cc | `bool SameSite(std::string_view a, std::string_view b)` |
| 7 | Privacy | Secure-scheme test for the lock indicator (https/browserx/about/file), strict about http on localhost | src/core/url_utils.cc | `bool SchemeIsSecure(std::string_view url_text)` |
| 8 | Robustness | Localhost detection (localhost, *.localhost, 127.0.0.1, ::1) for the dev-host case | src/core/url_utils.cc | `bool IsLocalhost(std::string_view host)` |
| 9 | Robustness | Stable URL form for de-duplicating history: fragment and trailing slash removed | src/core/url_utils.cc | `std::string NormalizeForCompare(std::string_view url_text)` |
| 10 | Tests | Eight tests pin these helpers, including the two traps they exist for (notexample.com, example.co.uk) | tests/unit/url_utils_test.cc | `TEST(UrlUtils, RegistrableDomainHandlesMultiLabelSuffixes)` |
| 11 | Robustness | Bug fix: stripping a query parameter used to delete the fragment as well | src/core/url_utils.cc | `void SplitUrlParts(std::string_view url, std::string* base, std::string* query` |
| 12 | Robustness | Bug fix: registrable domain collapsed 'www.example.com' to 'com' when no multi-label suffix matched | src/core/url_utils.cc | `const size_t public_suffix_labels = suffix_labels > 0 ? suffix_labels : 1;` |
| 13 | Robustness | Bug fix: a URL with query and fragment could come back with the fragment twice | src/core/url_utils.cc | `std::string JoinUrlParts(const std::string& base, const std::vector<std::string>& pairs,` |
| 14 | UX | Relative timestamps ('just now', '3 days ago') in one tested helper, with the caller owning the clock | src/core/format.cc | `std::string FormatRelativeTime(int64_t then_ms, int64_t now_ms)` |
| 15 | UX | Human list joining for status text ('a, b and c') | src/core/format.cc | `std::string FormatList(const std::vector<std::string>& items, std::string_view conjunction)` |
| 16 | UX | Correct pluralisation in one place (day/days, county/counties, irregulars given) | src/core/format.cc | `std::string Pluralize(int64_t count, std::string_view singular, std::string_view plural)` |
| 17 | UX | Byte sizes with the unit that fits (B/KB/MB/GB) | src/core/format.cc | `std::string FormatSize(uint64_t bytes)` |
| 18 | Robustness | Page titles are cleaned for display: control characters dropped, whitespace collapsed, length capped | src/core/format.cc | `std::string CleanDisplayText(std::string_view text, size_t max_chars)` |
| 19 | Tests | Three tests pin the display helpers, including the negative-duration case and a real C++ escape trap | tests/unit/format_test.cc | `TEST(Format, ListsAndPluralsAvoidTheAwkwardCases)` |
| 20 | UX | The browser remembers where you have been: a history store with de-duplication, visit counts and search | src/core/library.cc | `bool History::Add(std::string_view url, std::string_view title_text, int64_t now_ms)` |
| 21 | UX | Bookmarks with folders, one-call toggling and search | src/core/library.cc | `bool Bookmarks::Toggle(std::string_view url, std::string_view title_text, int64_t now_ms)` |
| 22 | Robustness | History and bookmarks persist as JSON and survive a round trip (bad input reported, never crashing) | src/core/library.cc | `std::string History::ToJson() const` |
| 23 | Robustness | A damaged store file is preserved as .corrupt instead of being silently replaced | src/app/library_store.cc | `".corrupt";` |
| 24 | Robustness | Atomic, debounced writes for both files, on the same tick as the settings | src/app/library_store.cc | `bool LibraryStore::SaveIfDirty()` |
| 25 | UX | Bookmarks export and import in the Netscape format every other browser reads | src/core/library.cc | `std::string Bookmarks::ToNetscapeHtml() const` |
| 26 | Robustness | The bookmark importer follows real folder structure instead of dumping everything into one list | src/core/library.cc | `std::vector<std::string> folder_stack;` |
| 27 | Privacy | Tracking parameters are stripped before an address is stored, so one article is one history entry | src/core/library.cc | `std::string CanonicalKey(std::string_view url)` |
| 28 | UX | browserx://history renders the real store as a table with relative times and a filter | src/web/internal_pages.cc | `std::string RenderHistory(const PageData& data)` |
| 29 | UX | browserx://bookmarks renders the real store, with a folder column | src/web/internal_pages.cc | `std::string RenderBookmarks(const PageData& data)` |
| 30 | UX | Filtering reuses the page's single filter implementation, and the table is complete before any script runs | src/web/internal_pages.cc | `std::string LibraryFilter(std::string_view placeholder, size_t total)` |
| 31 | Honesty | Sample rows are labelled as samples and never presented as the user's own history | src/web/internal_pages.h | `bool library_is_sample = false;` |
| 32 | Security | A page title cannot inject markup into a browser page, pinned by a test with a hostile title | tests/unit/internal_pages_test.cc | `a page title cannot inject script into a browser page` |
| 33 | Tests | The history rules are pinned: de-duplication, caps, unrecordable schemes, title corrections | tests/unit/library_test.cc | `TEST(Library, HistoryIgnoresUnrecordableUrlsAndCapsItself)` |
| 34 | Tests | The store is tested against a real temporary profile, including the damaged-file path | tests/unit/library_store_test.cc | `TEST(LibraryStore, ADamagedFileIsPreservedNotOverwritten)` |
| 35 | Robustness | One HTML escaper in core, shared by the bookmark export and the internal pages | src/core/format.cc | `std::string HtmlEscape(std::string_view text)` |
| 36 | UX | Every finished navigation is recorded once, then refined with the page's real title | src/browser/browser_shell.cc | `void BrowserShellImpl::NoteVisit(BrowserWindow* window, const std::string& url,` |
| 37 | Tooling | make pages writes 11 pages, including the history and bookmark readers, built from the real stores | tools/pages_dump.cc | `data.library_is_sample = true;` |
| 38 | Tooling | check-compile is separate from the ledger gate, so a failing ledger can never hide a compiler error | Makefile | `check-compile: check-headers check-core-purity check-bench-page $(CHECK_OBJS)` |
| 39 | Robustness | bug fix: imported bookmark URLs are normalised, so a duplicate in the file collapses instead of showing twice | src/core/library.cc | `bookmark.url = url::NormalizeForCompare(StringOf(item, "url"));` |
| 40 | Robustness | bug fix: the importer popped its folder stack on </H3>, which left every imported bookmark unfiled | src/core/library.cc | `// Only a closing DL ends a folder.` |
| 41 | UX | Ctrl+Shift+T reopens several closed tabs, one at a time, instead of remembering one URL | src/browser/session.cc | `int Session::ReopenLastClosed(bool activate)` |
| 42 | UX | A reopened tab comes back as it was: title, group, zoom, mute and pinning restored | src/browser/session.cc | `added->title = tab.title;` |
| 43 | UX | Close others, close to the right and close duplicates: three bulk tab actions that get the active tab right | src/browser/session.cc | `size_t Session::CloseOthers(int keep_id)` |
| 44 | UX | Duplicate tabs are found by the same 'same page' rule history uses, so a tracking-parameter copy counts as one | src/browser/session.cc | `const std::string key = core::url::NormalizeForCompare(core::url::StripTrackingParams(tab.url));` |
| 45 | UX | Sort tabs by host, and move one tab to the front, without disturbing pinned tabs or the active one | src/browser/session.cc | `void Session::SortByHost()` |
| 46 | Performance | Sleep the least recently used tabs until only N renderers are awake, never touching the active, pinned or audible ones | src/browser/session.cc | `std::vector<int> Session::SleepBeyondLiveLimit(int max_live)` |
| 47 | UX | A most-recently-used tab list, for a 'recent tabs' menu that matches what you actually did | src/browser/session.cc | `std::vector<int> Session::RecentlyActive(int max_count) const` |
| 48 | Robustness | Pinned tabs survive close-others and close-to-the-right, matching Chromium | src/browser/session.cc | `if (tabs_[i].pinned) continue;  // a pinned tab is never swept away` |
| 49 | Robustness | bug fix: the renderer budget slept the newest tabs and kept the least recently used awake | src/browser/session.cc | `const size_t to_sleep = candidates.size() > budget ? candidates.size() - budget : 0;` |
| 50 | Robustness | bug fix: duplicate detection ignored tracking parameters, so one page counted as two tabs | src/browser/session.cc | `const std::string key = core::url::NormalizeForCompare(core::url::StripTrackingParams(tab.url));` |
| 51 | Hygiene | One owner for the closed-tab stack: the session keeps tabs, and the strip no longer keeps a second list of URLs | src/browser/tab_commands.cc | `const int id = session->ReopenLastClosed(/*activate=*/true);` |
| 52 | Robustness | The zoom helpers clamp what they accept and read a corrupt value as 100% instead of scaling a page to nothing | src/browser/session.cc | `double Session::ZoomFactor(const TabState& tab)` |
| 53 | Tests | Bulk tab closes and the undo stack are pinned, including the pinned-tab protection | tests/unit/tab_commands_test.cc | `TEST(TabCommands, BulkClosesProtectPinnedTabsAndAreUndoable)` |
| 54 | Tests | Sleep order is pinned: the renderer budget sleeps the least recently used tab first | tests/unit/session_test.cc | `TEST(Session, SleepingBeyondTheLiveLimitProtectsWhatMatters)` |
| 55 | Honesty | bug fix: LOW RAM enabled a feature token Chromium ignores, so the mode promised process sharing it never applied | src/core/mode_engine.cc | `ProcessPerSiteUpToMainFrameThreshold:ProcessPerSiteMainFrameThreshold/4/` |
| 56 | Honesty | the catalogue entry is the real feature, with its parameters and defaults documented from the source | src/core/chromium_catalog.cc | `Reuses a renderer process for same-site main frames while the frame count stays under a` |
| 57 | Honesty | feature tokens are parsed the way Chromium parses them (Name, Name<Trial, Name:params), so a tuned feature still resolves to its verified catalogue entry | src/core/chromium_catalog.cc | `std::string FeatureNameOf(std::string_view token)` |
| 58 | Robustness | every mode plan is validated: no repeated switch, no unknown switch, no unreasoned switch, no contradiction | src/core/mode_engine.cc | `bool ValidatePlan(const ModePlan& plan, std::string* problem)` |
| 59 | Robustness | make bench refuses to print or emit JSON for an invalid plan, so a typo cannot reach a running browser | tools/flagplan.cc | `if (!browserx::core::ValidatePlan(plan, &problem)) {` |
| 60 | UX | switching modes is explained flag by flag, sorted and limited, instead of described as 'faster' | src/core/mode_engine.cc | `std::vector<PlanDifference> DiffPlans(const ModePlan& from, const ModePlan& to,` |
| 61 | UX | a one-line diff summary for the status bar and the reports | src/core/mode_engine.cc | `std::string DescribePlanDiff(const std::vector<PlanDifference>& differences)` |
| 62 | Honesty | switches a machine cannot benefit from are explained ('no GPU reported') instead of silently missing | src/core/mode_engine.cc | `std::vector<std::string> ExplainUnusedSwitches(const ModePlan& plan,` |
| 63 | Tests | every one of the 15 mode plans is validated on an unknown profile, so the catalogue and the engine cannot drift | tests/unit/mode_engine_test.cc | `TEST(ModeEngine, EveryModePlanPassesValidation)` |
| 64 | Tests | the invalid-plan cases are pinned: a repeated switch, an uncatalogued switch, an unreasoned switch | tests/unit/mode_engine_test.cc | `TEST(ModeEngine, ValidationCatchesARepeatedSwitchAndAnUnknownOne)` |
| 65 | Tests | the corrected feature token has a test of its own, including the rule that a bare parameter name is never a feature | tests/unit/mode_engine_test.cc | `TEST(ModeEngine, LowRamAdvancedProcessSharingUsesARealFeatureToken)` |
| 66 | Robustness | bug fix: the mode test helper parsed feature tokens by splitting on '/', the same misunderstanding as the bug it was covering | tests/unit/mode_engine_test.cc | `if (browserx::core::FeatureNameOf(feature) == name) return true;` |
| 67 | UX | The native mode menu says what switching changes ('adds 9 switches'), computed by the engine like the modes page | src/browser/browser_window.cc | `std::string summary = core::DescribePlanDiff(` |
| 68 | Robustness | The shell exposes the running plan and a build-plan-for-a-mode call, so the menu cannot disagree with the modes page | src/browser/browser_shell.cc | `core::ModePlan BrowserShellImpl::PlanFor(core::Mode mode) const {` |
| 69 | UX | The modes page explains every switch before you make it, and says nothing for the mode you are already in | src/web/internal_pages.cc | `std::string ModeDiffSummary(const PageData& data, const std::string& mode_id)` |
| 70 | Robustness | bug fix: pluralisation said '3 switchs' and '1 switches'; sibilant endings now take 'es' and every count gets its own noun | src/core/format.cc | `bool EndsWithSibilant(std::string_view word)` |
| 71 | Accessibility | The command palette announces its state (aria-hidden, aria-expanded, role=combobox) instead of relying on a CSS class | src/web/internal_pages.cc | `palette.setAttribute('aria-hidden', 'false');` |
| 72 | Tooling | The preview's page picker takes each page's own <title>, so a new page is labelled correctly with no list to update | preview/serve.py | `title = head[start + len("<title>"):end].replace(" · BrowserX", "").strip()` |
| 73 | Tests | Rendered pages are checked in a real DOM: self-contained, script runs, palette opens and closes, filters filter, readers have rows | tools/ui_check/pages_check.mjs | `if (palette.classList.contains('open')) fail(file, 'Escape did not close the palette');` |
| 74 | Tests | The preview is driven end to end: tabs, live plan data, the page picker and the frame loading a rendered page | preview/app.js | `async function loadPages()` |
| 75 | Tooling | The ledger has a machine-readable mode, so the preview and the build read the same numbers | tools/check_improvements.py | `parser.add_argument("--json"` |
| 76 | Tooling | The preview shows the improvement ledger live: count, areas, target and every entry, read from the checker | preview/serve.py | `def serve_improvements(self) -> None:` |
| 77 | UX | A preview tab that answers 'how far along is this?' with the build's own numbers instead of a claim | preview/app.js | `async function loadImprovements() {` |
| 78 | Security | bug fix: ledger text containing angle brackets was inserted into the preview as markup and swallowed two rows | preview/app.js | `function esc(value) {` |
| 79 | Robustness | The ledger table proves it rendered every entry the checker returned, instead of printing a count it did not verify | preview/app.js | `if (rendered !== (ledger.entries || []).length) {` |
| 80 | Tooling | One make target drives the rendered pages and the preview in a headless DOM, and says how to install jsdom instead of failing | Makefile | `JSDOM_CANDIDATES := $(ROOT)/node_modules/jsdom/lib/api.js` |
| 81 | Tooling | The DOM harnesses live in the repository and find jsdom by configuration, so the UI verification is reproducible off this machine | tools/ui_check/pages_check.mjs | `const jsdom_spec = process.env.BROWSERX_JSDOM || 'jsdom';` |
| 82 | Docs | The plan states the ledger's rules and the two real bugs its checks found, so the count can be argued with | PLAN.md | `## 13. The improvement programme: 200+ changes, each one checked` |
| 83 | Docs | The README documents the ledger and the UI check, and that the browser itself has no JavaScript dependencies | README.md | `## The improvement ledger` |
| 84 | Tooling | make requirements answers 'can I build what here?' by inspecting the machine, per build target | tools/check_build_requirements.py | `def check_core(report: Report) -> None:` |
| 85 | Tooling | It exits non-zero for exactly the target you asked about, so it can gate a script (FOR=core / browser / mobile / preview) | tools/check_build_requirements.py | `return 0 if all(report.ok(group) for group in only) else 1` |
| 86 | Honesty | A C++20 feature probe actually compiles the language features this code uses instead of trusting a version number | tools/check_build_requirements.py | `return (v.starts_with(\"a\") && std::optional<int>(1).has_value()) ? 0 : 1; }` |
| 87 | Honesty | The CEF pin is reported as unpinned when cef.lock.json has no version, instead of printing an empty version as if it were pinned | tools/check_build_requirements.py | `cef.lock.json has no version pinned yet` |
| 88 | Honesty | Offline CEF installs are named in the report (BROWSERX_CEF_ARCHIVE/DIR), so a machine without the CDN still has a documented way in | tools/check_build_requirements.py | `make deps needs ~200 MB of download (or an offline archive)` |
| 89 | Robustness | The requirement check never installs or downloads anything: it looks, and names the package that fixes each gap | tools/check_build_requirements.py | `report.add(group, name, "missing", f"{header} not found", f"apt install {package}")` |
| 90 | Docs | One page states the requirements for all four builds, the versions this project is developed against, and per-distro package names | BUILDING.md | `## The four things you can build, and what each needs` |
| 91 | Docs | The README points at make requirements and BUILDING.md, and the disk figure is the one measured on this machine | BUILDING.md | `build/ at 89 MB, out/ at 35 MB` |
| 92 | Docs | bug fix: the README still said history and bookmarks were not wired up; they now render the real store | README.md | `History and\nbookmarks are real` |
| 93 | Robustness | The ledger checker refuses a row whose description contains a raw pipe, which would silently split the table | tools/check_improvements.py | `the description contains a raw '|', which splits the table into` |
| 94 | Robustness | The ledger checker refuses an anchor containing a backtick, which the row format cannot carry | tools/check_improvements.py | `the anchor contains a backtick, which the row format cannot carry` |
| 95 | Robustness | make ui-check says the preview is not running and skips the drive-through, instead of failing with a stack trace | tools/ui_check/preview_check.mjs | `preview not running at ${base} — skipping the drive-through.` |
| 96 | Hygiene | The CEF header cache is tracked on purpose so make check runs with no network, and make headers refreshes it | .gitignore | `distribution they belong to.` |
| 97 | Honesty | The requirements report states where the CEF headers came from and that they are enough for make check but not for linking | tools/check_build_requirements.py | `headers cached in .cef-headers/ (enough for make check)` |
