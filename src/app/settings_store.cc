#include "app/settings_store.h"

#include <algorithm>

#include "app/app_paths.h"
#include "core/format.h"
#include "core/json.h"
#include "core/log.h"

namespace browserx {
namespace app {

bool SettingsStore::Load(const std::string& path) {
  path_ = path;
  settings_ = core::Settings::Defaults();

  std::string content;
  if (!ReadFileToString(path, &content) || content.empty()) {
    last_load_note_ = "no settings file yet - using defaults";
    core::log::InfoF("settings", "%s (%s)", last_load_note_.c_str(), path.c_str());
    return true;
  }

  std::string parse_error;
  auto parsed = core::json::Parse(content, &parse_error);
  if (!parsed.has_value()) {
    last_load_note_ = "settings.json is not valid JSON (" + parse_error + ") - using defaults";
    core::log::WarnF("settings", "%s", last_load_note_.c_str());
    // Keep the broken file for the user to inspect instead of silently deleting.
    WriteStringToFileAtomically(path + ".broken", content);
    return false;
  }

  std::string validation_error;
  if (!settings_.FromJson(*parsed, &validation_error)) {
    last_load_note_ = "settings rejected: " + validation_error + " - using defaults";
    core::log::WarnF("settings", "%s", last_load_note_.c_str());
    WriteStringToFileAtomically(path + ".broken", content);
    settings_ = core::Settings::Defaults();
    return false;
  }

  last_load_note_ = "loaded " + std::to_string(content.size()) + " bytes";
  return true;
}

bool SettingsStore::SaveNow() {
  const std::string json_text = core::json::Serialize(settings_.ToJson(), 2);
  if (!WriteStringToFileAtomically(path_, json_text + "\n")) {
    last_error_ = "could not write " + path_;
    core::log::WarnF("settings", "%s", last_error_.c_str());
    return false;
  }
  dirty_ = false;
  return true;
}

bool SettingsStore::SaveIfDirty() {
  if (!dirty_) return true;
  return SaveNow();
}

void SettingsStore::RecordAppliedPlan(const std::string& mode_id,
                                      const std::vector<std::string>& flags) {
  settings_.applied_mode_at_launch = mode_id;
  settings_.applied_flags_at_launch = flags;
  std::sort(settings_.applied_flags_at_launch.begin(), settings_.applied_flags_at_launch.end());
  MarkDirty();
}

bool SettingsStore::NeedsRestartFor(const std::vector<std::string>& flags) const {
  std::vector<std::string> sorted = flags;
  std::sort(sorted.begin(), sorted.end());
  return sorted != settings_.applied_flags_at_launch;
}

}  // namespace app
}  // namespace browserx
