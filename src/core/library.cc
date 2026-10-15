// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/library.h"

#include <algorithm>

#include "core/format.h"
#include "core/json.h"
#include "core/url_utils.h"

namespace browserx {
namespace core {
namespace {

constexpr int kLibraryFormatVersion = 1;

std::string StringOf(const json::Value& object, const char* key, std::string_view fallback = "") {
  if (!object.IsObject()) return std::string(fallback);
  return object.Get(key).AsString(fallback);
}

int64_t IntOf(const json::Value& object, const char* key, int64_t fallback) {
  if (!object.IsObject()) return fallback;
  return object.Get(key).AsInt64(fallback);
}

const json::Value* ArrayOf(const json::Value& object, const char* key) {
  if (!object.IsObject()) return nullptr;
  const json::Value& found = object.Get(key);
  if (!found.IsArray()) return nullptr;
  return &found;
}

/// A URL as the history should remember it: canonical form for comparison, but the
/// original for display. Empty when the URL should not be recorded at all.
std::string CanonicalKey(std::string_view url) {
  return url::NormalizeForCompare(url::StripTrackingParams(std::string(url)));
}

}  // namespace

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
void History::SetLimit(size_t max_entries) {
  limit_ = max_entries == 0 ? kDefaultLimit : max_entries;
  if (entries_.size() > limit_) entries_.resize(limit_);
}

bool History::IsRecordable(std::string_view url) {
  const std::string scheme = url::SchemeOf(url);
  // javascript: and data: are not places; about: and browserx: pages are the browser
  // itself, and cluttering "recently visited" with them would bury the real answer.
  return scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "file";
}

bool History::Add(std::string_view url, std::string_view title_text, int64_t now_ms) {
  if (url.empty() || !IsRecordable(url)) return false;
  const std::string key = CanonicalKey(url);
  if (key.empty()) return false;

  const std::string clean_title = fmt::CleanDisplayText(title_text, 200);
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].url != key) continue;
    // Seen before: it moves to the front and its count goes up. Its title is replaced
    // only if the new one is better - a page that reports "" on a later visit must not
    // erase the title we already showed the user.
    Visit moved = entries_[i];
    moved.visited_ms = now_ms;
    moved.visit_count += 1;
    if (!clean_title.empty()) moved.title = clean_title;
    entries_.erase(entries_.begin() + static_cast<long>(i));
    entries_.insert(entries_.begin(), std::move(moved));
    return true;
  }

  Visit visit;
  visit.url = key;
  visit.title = clean_title;
  visit.visited_ms = now_ms;
  visit.visit_count = 1;
  entries_.insert(entries_.begin(), std::move(visit));
  if (entries_.size() > limit_) entries_.resize(limit_);
  return true;
}

void History::SetTitle(std::string_view url, std::string_view title_text) {
  const std::string clean_title = fmt::CleanDisplayText(title_text, 200);
  if (clean_title.empty()) return;
  const std::string key = CanonicalKey(url);
  for (Visit& visit : entries_) {
    if (visit.url == key) {
      visit.title = clean_title;
      return;
    }
  }
}

bool History::Remove(std::string_view url) {
  const std::string key = CanonicalKey(url);
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const Visit& visit) { return visit.url == key; });
  if (found == entries_.end()) return false;
  entries_.erase(found);
  return true;
}

size_t History::ClearSince(int64_t since_ms) {
  const size_t before = entries_.size();
  if (since_ms <= 0) {
    entries_.clear();
    return before;
  }
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const Visit& visit) { return visit.visited_ms >= since_ms; }),
                 entries_.end());
  return before - entries_.size();
}

int64_t History::total_visits() const {
  int64_t total = 0;
  for (const Visit& visit : entries_) total += visit.visit_count;
  return total;
}

std::vector<Visit> History::Search(std::string_view text, size_t max_results) const {
  std::vector<Visit> out;
  const std::string needle = fmt::Trim(text);
  for (const Visit& visit : entries_) {
    if (!needle.empty() &&
        !fmt::ContainsCaseInsensitive(visit.url, needle) &&
        !fmt::ContainsCaseInsensitive(visit.title, needle)) {
      continue;
    }
    out.push_back(visit);
    if (out.size() >= max_results) break;
  }
  return out;
}

std::vector<Visit> History::MostVisited(size_t max_results) const {
  std::vector<Visit> sorted = entries_;
  std::sort(sorted.begin(), sorted.end(), [](const Visit& a, const Visit& b) {
    if (a.visit_count != b.visit_count) return a.visit_count > b.visit_count;
    return a.visited_ms > b.visited_ms;  // ties go to the more recent page
  });
  if (sorted.size() > max_results) sorted.resize(max_results);
  return sorted;
}

std::vector<Visit> History::Since(int64_t since_ms, size_t max_results) const {
  std::vector<Visit> out;
  for (const Visit& visit : entries_) {
    if (visit.visited_ms < since_ms) break;  // entries are newest-first
    out.push_back(visit);
    if (out.size() >= max_results) break;
  }
  return out;
}

std::string History::ToJson() const {
  json::Value::Object root;
  root["version"] = json::Value(static_cast<int64_t>(kLibraryFormatVersion));
  json::Value::Array array;
  for (const Visit& visit : entries_) {
    json::Value::Object item;
    item["url"] = json::Value(visit.url);
    item["title"] = json::Value(visit.title);
    item["visited_ms"] = json::Value(visit.visited_ms);
    item["visits"] = json::Value(visit.visit_count);
    array.push_back(json::Value(item));
  }
  root["entries"] = json::Value(array);
  return json::Serialize(json::Value(root), 2);
}

bool History::FromJson(std::string_view json_text, std::string* error) {
  if (json_text.empty()) return true;  // an empty file is an empty history, not an error
  std::string parse_error;
  const auto parsed = json::Parse(json_text, &parse_error);
  if (!parsed.has_value()) {
    if (error != nullptr) *error = "invalid history JSON: " + parse_error;
    return false;
  }
  const json::Value* array = ArrayOf(*parsed, "entries");
  if (array == nullptr) {
    if (error != nullptr) *error = "history root has no entries array";
    return false;
  }
  entries_.clear();
  for (const json::Value& item : array->AsArray()) {
    Visit visit;
    visit.url = StringOf(item, "url");
    visit.title = StringOf(item, "title");
    visit.visited_ms = IntOf(item, "visited_ms", 0);
    visit.visit_count = IntOf(item, "visits", 1);
    if (visit.url.empty() || !IsRecordable(visit.url)) continue;  // also drops old junk
    entries_.push_back(std::move(visit));
  }
  // Newest first, whatever order the file was in: the rest of the class assumes it.
  std::sort(entries_.begin(), entries_.end(),
            [](const Visit& a, const Visit& b) { return a.visited_ms > b.visited_ms; });
  if (entries_.size() > limit_) entries_.resize(limit_);
  return true;
}

// ---------------------------------------------------------------------------
// Bookmarks
// ---------------------------------------------------------------------------
bool Bookmarks::IsBookmarkUrl(std::string_view url) {
  const std::string scheme = url::SchemeOf(url);
  return scheme == "http" || scheme == "https" || scheme == "browserx" || scheme == "ftp" ||
         scheme == "file";
}

bool Bookmarks::Add(std::string_view url, std::string_view title_text, std::string_view folder,
                    int64_t now_ms) {
  if (url.empty() || !IsBookmarkUrl(url)) return false;
  const std::string key = url::NormalizeForCompare(url);
  if (key.empty()) return false;
  if (Contains(key)) return false;  // already bookmarked: not an error, just a no-op

  Bookmark bookmark;
  bookmark.url = key;
  bookmark.title = fmt::CleanDisplayText(title_text, 200);
  bookmark.folder = fmt::CleanDisplayText(folder, 60);
  bookmark.added_ms = now_ms;
  entries_.push_back(std::move(bookmark));
  if (entries_.size() > limit_) entries_.erase(entries_.begin());
  return true;
}

bool Bookmarks::Remove(std::string_view url) {
  const std::string key = url::NormalizeForCompare(url);
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const Bookmark& b) { return b.url == key; });
  if (found == entries_.end()) return false;
  entries_.erase(found);
  return true;
}

bool Bookmarks::Contains(std::string_view url) const {
  const std::string key = url::NormalizeForCompare(url);
  return std::any_of(entries_.begin(), entries_.end(),
                     [&](const Bookmark& b) { return b.url == key; });
}

bool Bookmarks::Toggle(std::string_view url, std::string_view title_text, int64_t now_ms) {
  if (Contains(url)) {
    Remove(url);
    return false;
  }
  Add(url, title_text, "", now_ms);
  return Contains(url);
}

bool Bookmarks::SetFolder(std::string_view url, std::string_view folder) {
  const std::string key = url::NormalizeForCompare(url);
  for (Bookmark& bookmark : entries_) {
    if (bookmark.url != key) continue;
    bookmark.folder = fmt::CleanDisplayText(folder, 60);
    return true;
  }
  return false;
}

std::vector<Bookmark> Bookmarks::Search(std::string_view text) const {
  std::vector<Bookmark> out;
  const std::string needle = fmt::Trim(text);
  for (const Bookmark& bookmark : entries_) {
    if (!needle.empty() && !fmt::ContainsCaseInsensitive(bookmark.url, needle) &&
        !fmt::ContainsCaseInsensitive(bookmark.title, needle) &&
        !fmt::ContainsCaseInsensitive(bookmark.folder, needle)) {
      continue;
    }
    out.push_back(bookmark);
  }
  return out;
}

std::vector<Bookmark> Bookmarks::InFolder(std::string_view folder) const {
  std::vector<Bookmark> out;
  for (const Bookmark& bookmark : entries_) {
    if (bookmark.folder == folder) out.push_back(bookmark);
  }
  return out;
}

std::vector<std::string> Bookmarks::Folders() const {
  std::vector<std::string> out;
  for (const Bookmark& bookmark : entries_) {
    if (bookmark.folder.empty()) continue;
    if (std::find(out.begin(), out.end(), bookmark.folder) == out.end()) {
      out.push_back(bookmark.folder);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string Bookmarks::ToJson() const {
  json::Value::Object root;
  root["version"] = json::Value(static_cast<int64_t>(kLibraryFormatVersion));
  json::Value::Array array;
  for (const Bookmark& bookmark : entries_) {
    json::Value::Object item;
    item["url"] = json::Value(bookmark.url);
    item["title"] = json::Value(bookmark.title);
    item["folder"] = json::Value(bookmark.folder);
    item["added_ms"] = json::Value(bookmark.added_ms);
    array.push_back(json::Value(item));
  }
  root["entries"] = json::Value(array);
  return json::Serialize(json::Value(root), 2);
}

bool Bookmarks::FromJson(std::string_view json_text, std::string* error) {
  if (json_text.empty()) return true;
  std::string parse_error;
  const auto parsed = json::Parse(json_text, &parse_error);
  if (!parsed.has_value()) {
    if (error != nullptr) *error = "invalid bookmarks JSON: " + parse_error;
    return false;
  }
  const json::Value* array = ArrayOf(*parsed, "entries");
  if (array == nullptr) {
    if (error != nullptr) *error = "bookmarks root has no entries array";
    return false;
  }
  entries_.clear();
  for (const json::Value& item : array->AsArray()) {
    Bookmark bookmark;
    bookmark.url = url::NormalizeForCompare(StringOf(item, "url"));
    bookmark.title = StringOf(item, "title");
    bookmark.folder = StringOf(item, "folder");
    bookmark.added_ms = IntOf(item, "added_ms", 0);
    if (bookmark.url.empty() || !IsBookmarkUrl(bookmark.url)) continue;
    if (Contains(bookmark.url)) continue;  // a duplicate in the file collapses
    entries_.push_back(std::move(bookmark));
  }
  if (entries_.size() > limit_) entries_.resize(limit_);
  return true;
}

std::string Bookmarks::ToNetscapeHtml() const {
  std::string out;
  out += "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n";
  out += "<!-- This is an automatically generated file. It will be read and overwritten. -->\n";
  out += "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n";
  out += "<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n";

  // Foldered entries first, each in its own <DT><H3>, then everything unfiled - which
  // is how every other browser writes it, so the file survives a round trip.
  for (const std::string& folder : Folders()) {
    out += "    <DT><H3>" + fmt::HtmlEscape(folder) + "</H3>\n    <DL><p>\n";
    for (const Bookmark& bookmark : InFolder(folder)) {
      out += "        <DT><A HREF=\"" + fmt::HtmlEscape(bookmark.url) + "\">" +
             fmt::HtmlEscape(bookmark.title.empty() ? bookmark.url : bookmark.title) +
             "</A>\n";
    }
    out += "    </DL><p>\n";
  }
  for (const Bookmark& bookmark : entries_) {
    if (!bookmark.folder.empty()) continue;
    out += "    <DT><A HREF=\"" + fmt::HtmlEscape(bookmark.url) + "\">" +
           fmt::HtmlEscape(bookmark.title.empty() ? bookmark.url : bookmark.title) + "</A>\n";
  }
  out += "</DL><p>\n";
  return out;
}

size_t Bookmarks::ImportNetscapeHtml(std::string_view html, int64_t now_ms) {
  size_t added = 0;
  std::vector<std::string> folder_stack;  // nested H3 headings, innermost last
  size_t cursor = 0;

  auto lowercase = [](std::string_view text) { return fmt::ToLower(text); };

  while (true) {
    const size_t tag_start = html.find('<', cursor);
    if (tag_start == std::string_view::npos) break;
    const size_t tag_end = html.find('>', tag_start);
    if (tag_end == std::string_view::npos) break;
    const std::string tag = lowercase(html.substr(tag_start + 1, tag_end - tag_start - 1));
    cursor = tag_end + 1;

    // Text that follows an opening tag, up to the next '<'.
    auto text_after = [&](size_t from) {
      const size_t stop = html.find('<', from);
      const size_t length = stop == std::string_view::npos ? std::string_view::npos : stop - from;
      return fmt::Trim(html.substr(from, length));
    };

    if (tag.rfind("h3", 0) == 0) {
      const std::string folder = fmt::CleanDisplayText(text_after(cursor), 60);
      folder_stack.push_back(folder);
      continue;
    }
    // Only a closing DL ends a folder. A closing H3 must NOT: in this format the
    // heading is immediately followed by the DL that holds its contents, and popping on
    // "</h3>" would leave every imported bookmark unfiled.
    if (tag.rfind("/dl", 0) == 0) {
      if (!folder_stack.empty()) folder_stack.pop_back();
      continue;
    }
    if (tag.rfind("a ", 0) != 0 && tag != "a") continue;  // not an anchor

    // <A HREF="url">Title</A>
    const size_t href = tag.find("href=");
    if (href == std::string::npos) continue;
    size_t value_start = href + 5;
    while (value_start < tag.size() && (tag[value_start] == ' ' || tag[value_start] == '\t')) {
      ++value_start;
    }
    if (value_start >= tag.size()) continue;
    const char quote = tag[value_start];
    if (quote != '"' && quote != '\'') continue;
    const size_t value_end = tag.find(quote, value_start + 1);
    if (value_end == std::string::npos) continue;
    std::string url_text = tag.substr(value_start + 1, value_end - value_start - 1);

    // Exports write "&amp;" inside HREF, so an imported address has to be decoded or the
    // page would be loaded from a URL that does not exist. (Percent-encoded addresses are
    // already correct and are left alone.)
    url_text = fmt::ReplaceAll(url_text, "&amp;", "&");

    // The title is whatever sits between this tag and the next one.
    std::string title_text = text_after(cursor);
    title_text = fmt::ReplaceAll(title_text, "&amp;", "&");
    title_text = fmt::ReplaceAll(title_text, "&lt;", "<");
    title_text = fmt::ReplaceAll(title_text, "&gt;", ">");
    title_text = fmt::ReplaceAll(title_text, "&quot;", "\"");
    title_text = fmt::ReplaceAll(title_text, "&#39;", "'");

    const std::string folder = folder_stack.empty() ? std::string() : folder_stack.back();
    if (Add(url_text, title_text, folder, now_ms)) ++added;
  }
  return added;
}

}  // namespace core
}  // namespace browserx
