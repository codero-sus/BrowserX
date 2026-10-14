// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// browserx-benchpage — writes the standalone benchmark page.
//
//   make bench-page                                # out/browserx-bench.html
//   ./out/browserx-benchpage some/where/page.html
//
// The page is one self-contained document with no external requests, produced from
// src/web/bench_page.cc — the same function the browser serves at browserx://bench and
// writes with `--write-bench-page`. Nothing here needs CEF, so it can be produced (and
// diffed, which is what `make check` does for the Android copy of it) anywhere.
//
// Why a tool as well as the browser flag: the Android edition cannot compile C++, so it
// ships the page as an asset. Generating that asset from this source of truth is the only
// way both editions are guaranteed to run the *same* bytes - and make check fails if the
// two ever drift apart.

#include <cstdio>
#include <cstring>
#include <string>

#include "web/bench_page.h"

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "browserx-bench.html";
  if (argc > 2 || (argc == 2 && (std::strcmp(argv[1], "--help") == 0 ||
                                 std::strcmp(argv[1], "-h") == 0))) {
    std::printf("\n  browserx-benchpage - write the offline benchmark page\n\n"
                "  usage: browserx-benchpage [output.html]\n\n"
                "  The page runs the same script/layout/canvas/WebGL tests in any browser,\n"
                "  offline, so two browsers can be compared on identical bytes.\n"
                "  In BrowserX it is also always available at browserx://bench,\n"
                "  and the browser itself writes it with --write-bench-page.\n\n"
                "  BrowserX by codero-sus - https://github.com/codero-sus/BrowserX\n"
                "  BrowserX Personal-Use License: personal, non-commercial use only;\n"
                "  no modification; no redistribution or resale.\n\n");
    return argc == 2 ? 0 : 2;
  }

  const std::string document = browserx::web::BenchPageDocument();
  std::FILE* file = std::fopen(path, "wb");
  if (file == nullptr) {
    std::fprintf(stderr, "browserx-benchpage: cannot write %s\n", path);
    return 1;
  }
  const size_t written = std::fwrite(document.data(), 1, document.size(), file);
  const int closed = std::fclose(file);
  if (written != document.size() || closed != 0) {
    std::fprintf(stderr, "browserx-benchpage: could not write all of %s\n", path);
    return 1;
  }

  std::printf("wrote %s (%zu bytes, self-contained, no network)\n", path, document.size());
  return 0;
}
