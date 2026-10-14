// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/format.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace browserx {
namespace core {
namespace fmt {
namespace {

bool IsUtf8Continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

}  // namespace

std::string ToLower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

std::string ToUpper(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

std::string TrimLeft(std::string_view text) {
  size_t i = 0;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
  return std::string(text.substr(i));
}

std::string TrimRight(std::string_view text) {
  size_t n = text.size();
  while (n > 0 && std::isspace(static_cast<unsigned char>(text[n - 1]))) --n;
  return std::string(text.substr(0, n));
}

std::string Trim(std::string_view text) { return TrimLeft(TrimRight(text)); }

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool Contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  const std::string lower_needle = ToLower(needle);
  const std::string lower_hay = ToLower(haystack);
  return lower_hay.find(lower_needle) != std::string::npos;
}

bool EqualsCaseInsensitive(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string ReplaceAll(std::string_view text, std::string_view from, std::string_view to) {
  if (from.empty()) return std::string(text);
  std::string out;
  out.reserve(text.size());
  size_t pos = 0;
  while (true) {
    const size_t hit = text.find(from, pos);
    if (hit == std::string_view::npos) {
      out.append(text.substr(pos));
      break;
    }
    out.append(text.substr(pos, hit - pos));
    out.append(to);
    pos = hit + from.size();
  }
  return out;
}

std::vector<std::string> Split(std::string_view text, char separator, bool skip_empty) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t hit = text.find(separator, start);
    const std::string_view piece =
        (hit == std::string_view::npos) ? text.substr(start) : text.substr(start, hit - start);
    if (!piece.empty() || !skip_empty) parts.emplace_back(piece);
    if (hit == std::string_view::npos) break;
    start = hit + 1;
  }
  return parts;
}

std::vector<std::string> SplitAny(std::string_view text, std::string_view separators, bool skip_empty) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : text) {
    if (separators.find(c) != std::string_view::npos) {
      if (!current.empty() || !skip_empty) parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty() || !skip_empty) parts.push_back(current);
  return parts;
}

std::string Join(const std::vector<std::string>& parts, std::string_view separator) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) out.append(separator);
    out.append(parts[i]);
  }
  return out;
}

std::string Truncate(std::string_view text, size_t max_chars, std::string_view ellipsis) {
  if (text.size() <= max_chars) return std::string(text);
  size_t cut = max_chars;
  // Do not split a UTF-8 sequence.
  while (cut > 0 && IsUtf8Continuation(static_cast<unsigned char>(text[cut]))) --cut;
  std::string out(text.substr(0, cut));
  out.append(ellipsis);
  return out;
}

std::string PadRight(std::string_view text, size_t width) {
  std::string out(text);
  while (out.size() < width) out.push_back(' ');
  return out;
}

std::string PadLeft(std::string_view text, size_t width) {
  std::string out;
  if (width > text.size()) out.append(width - text.size(), ' ');
  out.append(text);
  return out;
}

std::string SanitizeText(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u == 0x09 || u == 0x0A) {
      out.push_back(' ');
    } else if (u >= 0x20) {
      out.push_back(c);
    }
  }
  return out;
}

std::string FormatBytes(uint64_t bytes, int decimals) {
  const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.*f %s", decimals, value, units[unit]);
  return std::string(buf);
}

std::string FormatRate(double bytes_per_second, int decimals) {
  if (bytes_per_second <= 0.0) return "0 B/s";
  return FormatBytes(static_cast<uint64_t>(bytes_per_second), decimals) + "/s";
}

std::string FormatPercent(double percent, int decimals) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.*f%%", decimals, percent);
  return std::string(buf);
}

std::string FormatNumber(int64_t value) {
  const bool negative = value < 0;
  uint64_t magnitude = negative ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
  std::string digits = std::to_string(magnitude);
  std::string out;
  int count = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (count == 3) {
      out.push_back(',');
      count = 0;
    }
    out.push_back(*it);
    ++count;
  }
  if (negative) out.push_back('-');
  return std::string(out.rbegin(), out.rend());
}

std::string FormatDouble(double value, int decimals) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
  return std::string(buf);
}

std::string FormatMs(double milliseconds) {
  char buf[48];
  if (milliseconds < 1000.0) {
    std::snprintf(buf, sizeof(buf), "%.0f ms", milliseconds);
  } else if (milliseconds < 60000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f s", milliseconds / 1000.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f min", milliseconds / 60000.0);
  }
  return std::string(buf);
}

std::string FormatDuration(int64_t seconds) {
  if (seconds < 0) seconds = 0;
  const int64_t hours = seconds / 3600;
  const int64_t minutes = (seconds % 3600) / 60;
  const int64_t secs = seconds % 60;
  char buf[64];
  if (hours > 0) {
    std::snprintf(buf, sizeof(buf), "%lldh %02lldm", static_cast<long long>(hours),
                  static_cast<long long>(minutes));
  } else if (minutes > 0) {
    std::snprintf(buf, sizeof(buf), "%lldm %02llds", static_cast<long long>(minutes),
                  static_cast<long long>(secs));
  } else {
    std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(secs));
  }
  return std::string(buf);
}

std::string FormatFps(double fps) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.0f FPS", fps);
  return std::string(buf);
}

std::string FormatMemoryMiB(uint64_t mib) { return FormatBytes(mib * 1024ULL * 1024ULL, 0); }

std::string FormatSigned(double value, int decimals) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%+.*f", decimals, value);
  return std::string(buf);
}

bool ParseBool(std::string_view text, bool* out) {
  const std::string lower = ToLower(Trim(text));
  if (lower == "true" || lower == "1" || lower == "yes" || lower == "on" || lower == "enabled") {
    *out = true;
    return true;
  }
  if (lower == "false" || lower == "0" || lower == "no" || lower == "off" || lower == "disabled") {
    *out = false;
    return true;
  }
  return false;
}

bool ParseInt(std::string_view text, int64_t* out) {
  const std::string trimmed = Trim(text);
  if (trimmed.empty()) return false;
  char* end = nullptr;
  errno = 0;
  const long long value = std::strtoll(trimmed.c_str(), &end, 10);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') return false;
  *out = static_cast<int64_t>(value);
  return true;
}

bool ParseDouble(std::string_view text, double* out) {
  const std::string trimmed = Trim(text);
  if (trimmed.empty()) return false;
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(trimmed.c_str(), &end);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') return false;
  *out = value;
  return true;
}

bool ParseSizeMb(std::string_view text, int64_t* out_mb) {
  std::string trimmed = ToLower(Trim(text));
  if (trimmed.empty()) return false;
  double multiplier = 1.0;
  if (EndsWith(trimmed, "gb") || EndsWith(trimmed, "g")) {
    multiplier = 1024.0;
    trimmed = TrimRight(trimmed.substr(0, trimmed.size() - (EndsWith(trimmed, "gb") ? 2 : 1)));
  } else if (EndsWith(trimmed, "mb") || EndsWith(trimmed, "m")) {
    multiplier = 1.0;
    trimmed = TrimRight(trimmed.substr(0, trimmed.size() - (EndsWith(trimmed, "mb") ? 2 : 1)));
  } else if (EndsWith(trimmed, "kb") || EndsWith(trimmed, "k")) {
    multiplier = 1.0 / 1024.0;
    trimmed = TrimRight(trimmed.substr(0, trimmed.size() - (EndsWith(trimmed, "kb") ? 2 : 1)));
  }
  double value = 0.0;
  if (!ParseDouble(trimmed, &value) || value < 0.0) return false;
  *out_mb = static_cast<int64_t>(value * multiplier + 0.5);
  return true;
}

std::string Sparkline(const std::vector<double>& values, int buckets) {
  if (values.empty() || buckets <= 0) return std::string();
  // Block glyphs U+2581..U+2588 give 8 levels.
  static const char* kBlocks[] = {"\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
                                  "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88"};
  std::string out;
  const size_t count = static_cast<size_t>(buckets);
  if (values.size() <= count) {
    for (const double v : values) {
      const double clamped = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
      const int level = static_cast<int>(clamped * 7.0 + 0.5);
      out.append(kBlocks[level < 0 ? 0 : (level > 7 ? 7 : level)]);
    }
    return out;
  }
  const double step = static_cast<double>(values.size()) / static_cast<double>(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t begin = static_cast<size_t>(static_cast<double>(i) * step);
    size_t end = static_cast<size_t>(static_cast<double>(i + 1) * step);
    if (end <= begin) end = begin + 1;
    if (end > values.size()) end = values.size();
    double sum = 0.0;
    for (size_t j = begin; j < end; ++j) sum += values[j];
    const double average = sum / static_cast<double>(end - begin);
    const double clamped = average < 0.0 ? 0.0 : (average > 1.0 ? 1.0 : average);
    const int level = static_cast<int>(clamped * 7.0 + 0.5);
    out.append(kBlocks[level < 0 ? 0 : (level > 7 ? 7 : level)]);
  }
  return out;
}

}  // namespace fmt
}  // namespace core
}  // namespace browserx
