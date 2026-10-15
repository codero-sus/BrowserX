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
| 37 | Tooling | make pages writes every internal page, including the history and bookmark readers, built from the real stores (the count is asserted by make ui-check rather than written here, because a number in prose goes stale) | tools/pages_dump.cc | `data.library_is_sample = true;` |
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
| 98 | Tooling | One updater implementation behind a Linux launcher and a Windows launcher, so the two platforms cannot drift into two versions of update | tools/update.py | `The Windows launchers cannot be executed here` |
| 99 | Honesty | An available update and a failure have different exit codes - 10 with --check, 1 for anything that failed - so a script can tell them apart | tools/update.py | `EXIT_UPDATE_AVAILABLE = 10` |
| 100 | Robustness | The updater refuses to pull over a dirty working tree and lists the files it would have lost | tools/update.py | `refusing to update: a pull over local changes can lose them.` |
| 101 | Robustness | A failed build still records the commit the update moved to, so a broken update can always be rolled back | tools/update.py | `The state file is what makes --rollback possible` |
| 102 | Hygiene | The updater never runs git clean, because the run profile - history, bookmarks, settings - lives inside the checkout | tools/update.py | `ever runs git clean` |
| 103 | Robustness | The core rebuild is make pages rather than make test, so a suite does not run twice: verification runs the tests once, afterwards, over the new tree | tools/update.py | `targets = ["all"] if step == "browser" else ["pages"]` |
| 104 | Hygiene | The Windows launchers are checked as code instead of trusted: the self-test rejects a stale %ERRORLEVEL% inside a block and a single & joining two commands, for every .cmd in the repository | tools/update.py | `no command in {cmd_name} uses %ERRORLEVEL%` |
| 105 | Robustness | The Windows launcher checks every step and passes the exit code through, so the 10 that means update available survives to the caller | tools/update.cmd | `endlocal & exit /b %RC%` |
| 106 | Honesty | The Windows last-resort path (no Python, no WSL) updates only the source and names how to get the full build, instead of implying it built the browser | tools/update.cmd | `Source updated. To build it as well, install Python 3` |
| 107 | Hygiene | gitattributes pins CRLF for the batch file and LF for sources, so a Windows checkout gets a launcher cmd.exe can actually read | .gitattributes | `*.cmd` |
| 108 | Tooling | make update and make update-check give the same updater a make entry point, with UPDATE_ARGS passed straight through | Makefile | `tools/update.py` |
| 109 | Robustness | A rollback with --stash stashes local changes first, instead of letting reset --hard discard the very changes the flag promised to keep | tools/update.py | `stashing local changes before the rollback (--stash)` |
| 110 | Honesty | A rollback rebuilds and verifies, and says so when the commit it rolled back to does not build here either | tools/update.py | `the rolled-back commit does not build here either` |
| 111 | Tests | The updater self-test covers the plan decision logic, the state file and the batch-script rules | tools/update.py | `self-test passed` |
| 112 | Docs | BUILDING.md documents updating, the exit codes and what each flag does | BUILDING.md | `## Updating BrowserX` |
| 113 | Honesty | A dry run prints the exact plan - pull, build, verify - and changes nothing | tools/update.py | `nothing was changed. The plan:` |
| 114 | Honesty | The error page prints the code and the address that failed, because a paraphrase without them cannot be searched or reported | src/web/internal_pages.cc | `What Chromium reported` |
| 115 | Honesty | A failure code this build cannot explain is reported as unknown, with its number, instead of being explained by guesswork | src/core/net_errors.cc | `cannot explain that failure` |
| 116 | Tests | Every error code is pinned to upstream's number in a test - which caught three certificate codes first drafted from memory in the wrong order | tests/unit/net_errors_test.cc | `{"CERT_COMMON_NAME_INVALID", -200}` |
| 117 | Security | Every certificate failure is marked as a trust problem, and the test asserts that nothing else is, so the marking keeps meaning something | tests/unit/net_errors_test.cc | `CertificateFailuresAreMarkedAsSecurityRelevant` |
| 118 | Security | The error page says out loud that it offers no way past a trust failure, rather than leaving that to be discovered | src/web/internal_pages.cc | `This one is about trust` |
| 119 | Honesty | The error explanations say when BrowserX itself made the decision, so the network is not blamed for the shield | src/core/net_errors.cc | `caused_by_browser` |
| 120 | Robustness | Stopping a load and a cache miss are not failures worth a page, so the browser does not train people to dismiss its error pages | src/core/net_errors.cc | `bool NetErrorWarrantsPage(int code)` |
| 121 | Honesty | The error page lists every code it can explain, so what it does not know is visible rather than implied | src/web/internal_pages.cc | `Every failure code this build can explain` |
| 122 | Security | A failed browserx page is reported as a bug in this build rather than as a network failure that did not happen | src/browser/browser_window.cc | `This browserx:// page could not be built` |
| 123 | UX | A failed load opens a page that explains it, instead of only flashing the status bar | src/browser/browser_window.cc | `browserx://error?code=` |
| 124 | Security | The error page escapes the address and wording the network supplied, and never renders the failed address as a link to click | tests/unit/internal_pages_test.cc | `ErrorPageEscapesWhatTheNetworkGaveIt` |
| 125 | UX | The page dump renders the error page as a labelled example, so its layout can be read without breaking a connection on purpose | tools/pages_dump.cc | `data.error_is_sample = true;` |
| 126 | Docs | The README names every internal page that exists, and the UI check fails when one of them stops rendering | README.md | `fails if one of them stops rendering` |
| 127 | Honesty | The ledger no longer states a page count in prose, which had already gone stale - the count is asserted by a check instead | docs/IMPROVEMENTS.md | `the count is asserted by make ui-check rather than written here` |
| 128 | Tooling | A Windows build path that compiles the same sources the POSIX build does, with MSVC, and no CMake or solution file to keep in sync | tools/build_windows.py | `print-app-sources` |
| 129 | Honesty | The Windows build proves it compiles the same file list as the verified Linux build by asking the Makefile for its list, rather than copying it and hoping | tools/build_windows.py | `both builds compile the same` |
| 130 | Security | A portability audit refuses POSIX-only includes that are not behind a platform guard, naming the file and line, because that is how a Windows build dies in a file that looks fine | tools/build_windows.py | `POSIX-only include(s) that would break the Windows build` |
| 131 | Tooling | make check-windows runs both Windows checks on any machine, so the Windows path cannot rot between the rare times it is built | Makefile | `check: check-compile check-improvements check-windows` |
| 132 | Tooling | make requirements answers the Windows question directly: each tool, its size, and what it is for | tools/check_build_requirements.py | `def check_windows(report: Report) -> None:` |
| 133 | Honesty | The Windows requirements report says when it is running on a machine that is not Windows, instead of reporting a Linux box as a broken Windows one | tools/check_build_requirements.py | `cannot probe a real toolchain` |
| 134 | Robustness | The CEF fetcher refuses to hand a build the wrong platform's distribution: a pin recorded on another platform is named, not silently used | tools/fetch_cef.py | `this machine builds` |
| 135 | Tooling | CEF can be fetched for a named platform, so preparing a Windows build from Linux is sayable, and a typo is rejected immediately | tools/fetch_cef.py | `KNOWN_PLATFORMS` |
| 136 | Robustness | The updater builds with MSVC on Windows instead of reporting that the browser cannot be built there | tools/update.py | `def is_windows() -> bool:` |
| 137 | Tests | The Windows build decision is a pure function tested on both platforms, so the branch that cannot be exercised on Linux still is | tools/update.py | `def build_command(step: str, args, windows` |
| 138 | Robustness | On Windows the updater stages the CEF runtime next to the exe, because an updated exe beside a missing DLL is not an update | tools/update.py | `build_windows.py --stage` |
| 139 | Honesty | The Windows launcher's first choice is native Python, so the update is a Windows build rather than a WSL one that produces a Linux binary | tools/update.cmd | `Native Python: the real path, and it builds with MSVC` |
| 140 | Tooling | The Windows build launcher is a launcher for the Python build, so there is one build description rather than a batch file that drifts away from it | tools/build_windows.cmd | `all the work is tools` |
| 141 | Hygiene | The batch-file checks cover every .cmd in the repository, because a rule enforced on one file is enforced by luck | tools/update.py | `for cmd_name in ("update.cmd", "build_windows.cmd")` |
| 142 | Honesty | CEF's binding layer is compiled with WRAPPING_CEF_SHARED and without the application's defines, matching the rule the verified POSIX build uses instead of guessing | tools/build_windows.py | `WRAPPER_DEFINES` |
| 143 | UX | The Windows build dry run shows one source of each kind, because the application, the tools and CEF's wrapper are not compiled the same way | tools/build_windows.py | `One of each kind, because they are not compiled the same way` |
| 144 | Docs | BUILDING.md answers the Windows question with the tool list, the sizes and the exact commands | BUILDING.md | `## Building the browser on Windows` |
| 145 | Honesty | BUILDING.md states plainly that the Windows path is written and statically checked but has not yet been compiled by MSVC on a real machine | BUILDING.md | `has not yet been compiled by MSVC` |
| 146 | Docs | The requirements table names the Windows target beside the Linux one, so neither is discovered by surprise | BUILDING.md | `The full browser on Windows` |
| 147 | Docs | The README gives the Windows build its own commands next to the Linux ones, instead of leaving Windows to be guessed at | README.md | `build_windows.cmd --stage` |
| 148 | Security | make clean keeps out/profile, because in portable mode that directory is the entire run profile - the one thing in this tree that cannot be regenerated | Makefile | `kept    $(OUT_DIR)/profile (your history, bookmarks and settings)` |
| 149 | Tooling | make space reports what the build is holding, measured rather than estimated, and deletes nothing | Makefile | `space-apply:` |
| 150 | Hygiene | The disk report classifies every item by what removing it costs - a recompile, a 200 MB re-download - instead of reporting sizes alone | tools/disk_report.py | `make deps   (about 200 MB of download` |
| 151 | Security | The removal tool refuses the sources, the git history and the headers, and checks that refusal immediately before each delete rather than once at startup | tools/disk_report.py | `is never removed by this tool` |
| 152 | Tests | The disk tool's safety was tested by pointing it at a sandbox with a fake profile, and by forcing it to remove src/, .git and docs/ - all three refused | tools/disk_report.py | `def assert_safe_to_remove(path: Path) -> None:` |
| 153 | Hygiene | distclean says what it costs to undo, instead of removing a 200 MB download in silence | Makefile | `getting them back: make deps` |
| 154 | Docs | BrowserX.vsconfig names the three Visual Studio components this build needs, so the installer imports a minimal selection instead of the recommended pile | BrowserX.vsconfig | `Microsoft.VisualStudio.Component.Windows11SDK.26100` |
| 155 | Docs | BUILDING.md separates installing the compiler from installing the extras, with the space each one costs and which are not needed | BUILDING.md | `### Trimming Visual Studio` |
| 156 | UX | The Windows install instructions use the Visual Studio Installer first, with the exact components to keep ticked and the ones to untick | BUILDING.md | `Untick CMake, vcpkg, Clang, ATL/MFC` |
| 157 | Honesty | The documented winget commands carry the agreement flags, because without them the first run stops at a prompt and the following commands become that prompt's answers | BUILDING.md | `Paste one line, wait for it to finish` |
| 158 | Docs | The Windows SDK question is answered directly: any one works, take the newest, take exactly one, and the installer's three offerings are ranked | BUILDING.md | `Exactly one.` |
| 159 | Honesty | BUILDING.md states that the SDK does not set the oldest Windows the browser runs on - Chromium does, and that floor has been Windows 10 since Chromium 110 | BUILDING.md | `Windows 10 or later** (Chrome 109 was` |
| 160 | Robustness | The requirements report and the build share one SDK ordering, so the report cannot name a different SDK than the build will use | tools/build_windows.py | `def sdk_version_key(name: str) -> list[int]:` |
| 161 | Tooling | The report names which installed SDK will be used, and how many extra ones are taking up space | tools/check_build_requirements.py | `the build uses only` |
| 162 | Tooling | The requirements report answers the SDK question even on a machine that has no Windows toolchain, by naming what to tick | tools/check_build_requirements.py | `Install exactly one - the build uses the newest` |
| 163 | Tests | The Windows build has a self-test for what does not need Windows: which SDK wins, that CEF's wrapper is compiled its own way, and the source rule | tools/build_windows.py | `def self_test() -> int:` |
| 164 | Robustness | The per-source compile decisions are pure functions rather than inline code, so the rule that the wrapper must not see our headers is checkable anywhere | tools/build_windows.py | `def includes_for_source(source: Path, cef_root: Path` |
| 165 | Docs | The SDK table lists the versions a real Visual Studio installer offers, so the choice on screen can be matched against the guidance line by line | BUILDING.md | `Windows 11 SDK (10.0.22621) or (10.0.22000)` |
| 166 | Honesty | A newer SDK is not treated as a better one: the build names an SDK above the released 10.0.26100 instead of choosing it in silence, because an SDK numbered above the released build is usually an Insider preview | tools/build_windows.py | `RECOMMENDED_SDK = "10.0.26100"` |
| 167 | Robustness | The SDK comparison uses major.minor.build, so the same SDK named with its build component is not mistaken for a newer one | tools/build_windows.py | `Compared on major.minor.build only` |
| 168 | Tests | The build self-test covers the false alarm that a full SDK directory name would otherwise cause, alongside the newer-SDK note and the recommended one staying quiet | tools/build_windows.py | `the same SDK named with its build component is not a newer SDK` |
| 169 | Honesty | BUILDING.md says what a higher SDK number usually is, instead of telling people that newer is simply better | BUILDING.md | `It is a Windows Insider **preview** SDK` |
| 170 | Tooling | A Tkinter window that builds the browser, so the steps can be run without a command line - and with no pip install, because the machine is already short of space | tools/build_gui.py | `root.title("BrowserX — build")` |
| 171 | Honesty | The window is a front-end and only ever runs commands that already exist, so it cannot disagree with the command line about how the browser is built | tools/build_gui.py | `def jobs(has_cef: bool, has_toolchain: bool` |
| 172 | UX | Every button the machine cannot use is greyed out, and the reason names the fix rather than just refusing | tools/build_gui.py | `needs the CEF binaries: use “Fetch CEF” first` |
| 173 | Tests | The window's logic is checked without opening a window, so make check covers it on a machine that has no tkinter at all | tools/build_gui.py | `def self_test() -> int:` |
| 174 | Tests | The process runner is tested for real: output arrives line by line in order, a job's exit code is passed through, and stopping a job kills it without killing the window | tools/build_gui.py | `the job's output arrives line by line, in order` |
| 175 | Security | Stopping a build kills the whole process tree, and the child gets its own session so the Stop button can never signal the window or its parent shell | tools/build_gui.py | `start_new_session=not IS_WINDOWS` |
| 176 | Robustness | A run sends exactly one terminal message, because two of them raced: a cancelled build was reported as a failure when the killed process's exit code arrived after the stop | tools/build_gui.py | `self.output.put(("done", "stopped" if self.stopped else str(code)))` |
| 177 | UX | The updater's exit 10 reads as an available update in the window rather than as a failure, because the window knows the updater's contract | tools/build_gui.py | `an update is available` |
| 178 | UX | --check prints exactly what the window would show, so the state can be read from a script or a machine with no display | tools/build_gui.py | `def show_state() -> int:` |
| 179 | Honesty | Without Tkinter the window says which package provides it and points at the command line, instead of failing with an import traceback | tools/build_gui.py | `Tkinter is not available in this Python` |
| 180 | Tests | The job list is checked mechanically for the bug the headless view caught: the interpreter is prefixed to a .py file and to nothing else | tools/build_gui.py | `head == sys.executable` |
| 181 | Tooling | make gui and make check-gui give the window and its checks an entry point, and make check runs the checks on every build | Makefile | `check: check-compile check-improvements check-windows check-gui` |
| 182 | Docs | BUILDING.md documents the window, what it runs, and the two ways to see its state without a display | BUILDING.md | `### Or build it from a window` |
