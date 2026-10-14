// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#ifndef BROWSERX_WEB_BENCH_PAGE_H_
#define BROWSERX_WEB_BENCH_PAGE_H_

#include <string>

namespace browserx {
namespace web {

/// The whole benchmark, as one self-contained HTML document.
///
/// Served as `browserx://bench` and written to disk by `--write-bench-page`, so the
/// identical bytes can be opened in another browser: that is the only kind of speed
/// comparison worth trusting, because both sides ran the same code with no network.
std::string BenchPageDocument();

}  // namespace web
}  // namespace browserx

#endif  // BROWSERX_WEB_BENCH_PAGE_H_
