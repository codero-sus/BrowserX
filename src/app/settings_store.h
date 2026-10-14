// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — settings persistence (settings.json in the profile).
//
// The store owns the in-memory Settings object and is responsible for:
//   * loading them at start-up (falling back to defaults on any problem),
//   * writing them back atomically and debounced (no disk churn while the user
//     drags a slider),
//   * recording which switch plan was actually applied at launch, so the UI can
//     tell the truth about "restart required".

#ifndef BROWSERX_APP_SETTINGS_STORE_H_
#define BROWSERX_APP_SETTINGS_STORE_H_

#include <string>
#include <vector>

#include "core/settings_schema.h"

namespace browserx {
namespace app {

class SettingsStore {
 public:
  bool Load(const std::string& path);
  /// Returns false (and logs) when the file cannot be written.
  bool SaveNow();
  /// Marks dirty; SaveIfDirty() is called by the telemetry tick.
  void MarkDirty() { dirty_ = true; }
  bool SaveIfDirty();

  const core::Settings& settings() const { return settings_; }
  core::Settings& mutable_settings() { return settings_; }

  /// Records the plan applied at launch (used for the restart banner).
  void RecordAppliedPlan(const std::string& mode_id, const std::vector<std::string>& flags);
  /// True when `flags` differ from what is currently applied.
  bool NeedsRestartFor(const std::vector<std::string>& flags) const;

  const std::string& path() const { return path_; }
  const std::string& last_error() const { return last_error_; }
  const std::string& last_load_note() const { return last_load_note_; }

 private:
  core::Settings settings_ = core::Settings::Defaults();
  std::string path_;
  std::string last_error_;
  std::string last_load_note_;
  bool dirty_ = false;
};

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_SETTINGS_STORE_H_
