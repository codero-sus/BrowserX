// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — small string/number formatting helpers used across core, the
// native UI and the tooling. Header-mostly, no dependencies, unit-tested.

#ifndef BROWSERX_CORE_FORMAT_H_
#define BROWSERX_CORE_FORMAT_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace core {
namespace fmt {

// ---- string helpers ---------------------------------------------------------
std::string ToLower(std::string_view text);
std::string ToUpper(std::string_view text);
std::string Trim(std::string_view text);
std::string TrimLeft(std::string_view text);
std::string TrimRight(std::string_view text);
bool StartsWith(std::string_view text, std::string_view prefix);
bool EndsWith(std::string_view text, std::string_view suffix);
bool Contains(std::string_view text, std::string_view needle);
bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle);
bool EqualsCaseInsensitive(std::string_view a, std::string_view b);
std::string ReplaceAll(std::string_view text, std::string_view from, std::string_view to);
/// Escapes &, <, >, ", ' for HTML text and attribute contexts. Kept in core because both
/// a bookmark export file and the browser's own pages need the same escaping, and two
/// copies of it would eventually disagree.
std::string HtmlEscape(std::string_view text);
std::vector<std::string> Split(std::string_view text, char separator, bool skip_empty = true);
std::vector<std::string> SplitAny(std::string_view text, std::string_view separators, bool skip_empty = true);
std::string Join(const std::vector<std::string>& parts, std::string_view separator);
std::string Truncate(std::string_view text, size_t max_chars, std::string_view ellipsis = "\xE2\x80\xA6");
std::string PadRight(std::string_view text, size_t width);
std::string PadLeft(std::string_view text, size_t width);
/// Removes control characters (keeps valid UTF-8 bytes).
std::string SanitizeText(std::string_view text);

// ---- numbers ----------------------------------------------------------------
std::string FormatBytes(uint64_t bytes, int decimals = 1);
/// "12.4 MiB/s" from bytes per second.
std::string FormatRate(double bytes_per_second, int decimals = 1);
std::string FormatPercent(double percent, int decimals = 0);
std::string FormatNumber(int64_t value);              // 1234567 -> "1,234,567"
std::string FormatDouble(double value, int decimals);  // fixed decimals
std::string FormatMs(double milliseconds);             // "12ms" / "1.23s"
std::string FormatDuration(int64_t seconds);           // "2h 14m"
std::string FormatFps(double fps);
std::string FormatMemoryMiB(uint64_t mib);
std::string FormatSigned(double value, int decimals = 1);

// ---- parsing ----------------------------------------------------------------
bool ParseBool(std::string_view text, bool* out);
bool ParseInt(std::string_view text, int64_t* out);
bool ParseDouble(std::string_view text, double* out);
/// "512", "512MB", "2g" -> megabytes. Returns false on garbage.
bool ParseSizeMb(std::string_view text, int64_t* out_mb);

/// "just now" / "4 min ago" / "3 h ago" / "2 days ago", for history and the session
/// list. `now_ms` and `then_ms` are both milliseconds since the epoch, so the caller
/// owns the clock and the formatting stays testable.
std::string FormatRelativeTime(int64_t then_ms, int64_t now_ms);
/// "a", "a and b", "a, b and c" - a list a person would write, with an Oxford-free
/// join (the browser has no room for an Oxford comma in a status bar).
std::string FormatList(const std::vector<std::string>& items, std::string_view conjunction = "and");
/// "1 tab" / "3 tabs" - correct English in one call, so no caller has to write "s?".
std::string Pluralize(int64_t count, std::string_view singular, std::string_view plural = "");
/// "1.5 KB" style size with the unit that fits; 0 means "0 B".
std::string FormatSize(uint64_t bytes);
/// Strips control characters and collapses whitespace runs: titles come from web pages
/// and end up in menus, tabs and JSON, so one place has to make them safe to display.
std::string CleanDisplayText(std::string_view text, size_t max_chars = 160);

/// Sparkline from a series of 0..1 values using block glyphs (for telemetry UI).
std::string Sparkline(const std::vector<double>& values, int buckets = 24);

}  // namespace fmt
}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_FORMAT_H_
