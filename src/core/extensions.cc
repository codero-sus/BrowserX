// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/extensions.h"

#include <string>
#include <vector>

#include "core/json.h"

namespace browserx {
namespace core {
namespace {

/// Permissions CEF does not implement. Naming them is what turns "it might not work" into
/// something a person can act on. Kept short on purpose: a long list would imply precision
/// this cannot have.
const char* const kUnsupportedPermissions[] = {
    "debugger",       // chrome.debugger: not exposed
    "tabCapture",     // tab media capture: not exposed
    "desktopCapture",
    "nativeMessaging",  // needs a host outside the browser CEF does not manage
    "declarativeWebRequest",
};

bool IsUnsupported(const std::string& permission) {
  for (const char* known : kUnsupportedPermissions) {
    if (permission == known) return true;
  }
  return false;
}

}  // namespace

bool ParseManifest(const std::string& manifest_json, const std::string& folder,
                   const std::string& path, ExtensionInfo* out, std::string* error) {
  if (out == nullptr) return false;
  ExtensionInfo info;
  info.folder = folder;
  info.path = path;

  const std::optional<json::Value> parsed = json::Parse(manifest_json, error);
  if (!parsed.has_value()) {
    if (error != nullptr && error->empty()) *error = "manifest.json is not valid JSON";
    return false;
  }
  if (!parsed->IsObject()) {
    if (error != nullptr) *error = "manifest.json must contain an object";
    return false;
  }

  info.name = parsed->Get("name").AsString();
  info.version = parsed->Get("version").AsString();
  const json::Value& mv = parsed->Get("manifest_version");
  info.manifest_version = mv.IsNumber() ? static_cast<int>(mv.AsDouble()) : 0;

  if (info.name.empty()) {
    // Chromium rejects a manifest without a name; reporting it here is more useful than
    // letting the extension vanish at start-up with nothing said.
    if (error != nullptr) *error = "manifest.json has no \"name\"";
    return false;
  }
  if (info.manifest_version != 2 && info.manifest_version != 3) {
    if (error != nullptr) {
      *error = "manifest_version is " + std::to_string(info.manifest_version) +
               "; Chromium loads 2 or 3";
    }
    return false;
  }

  // --- what is likely to be missing -----------------------------------------
  if (info.manifest_version == 3) {
    // MV3 moved background logic into a service worker, and CEF's support for that is the
    // weakest part. Saying so up front is the difference between "broken extension" and
    // "understood limitation".
    const json::Value& background = parsed->Get("background");
    if (background.IsObject() && background.Get("service_worker").IsString()) {
      info.limitations.push_back(
          "Manifest V3 with a background service worker: CEF's support for this is "
          "partial, so background behaviour may not run.");
    }
  }

  const json::Value& permissions = parsed->Get("permissions");
  if (permissions.IsArray()) {
    std::vector<std::string> missing;
    for (const json::Value& entry : permissions.AsArray()) {
      if (!entry.IsString()) continue;
      const std::string permission = entry.AsString();
      if (IsUnsupported(permission)) missing.push_back(permission);
    }
    if (!missing.empty()) {
      std::string text = "asks for permissions CEF does not implement: ";
      for (size_t i = 0; i < missing.size(); ++i) {
        if (i != 0) text += ", ";
        text += missing[i];
      }
      info.limitations.push_back(text);
    }
  }

  const json::Value& host_permissions = parsed->Get("host_permissions");
  if (host_permissions.IsArray() && !host_permissions.AsArray().empty()) {
    info.limitations.push_back(
        "requests host permissions; whether content scripts are injected depends on the "
        "site, so check that the extension actually does something.");
  }

  info.limitations.push_back(
      "the Chrome Web Store is not available in BrowserX: extensions are loaded unpacked "
      "from a folder, and cannot be installed from a store page.");

  info.loadable = true;
  *out = std::move(info);
  return true;
}

std::string LoadExtensionValue(const std::vector<ExtensionInfo>& extensions) {
  std::string value;
  for (const ExtensionInfo& extension : extensions) {
    if (!extension.loadable || extension.path.empty()) continue;
    if (!value.empty()) value += ",";
    value += extension.path;
  }
  return value;
}

std::string DisableExceptValue(const std::vector<ExtensionInfo>& extensions) {
  return LoadExtensionValue(extensions);
}

std::string DescribeExtension(const ExtensionInfo& extension) {
  std::string text = extension.name.empty() ? extension.folder : extension.name;
  if (!extension.version.empty()) text += " " + extension.version;
  text += " (Manifest V";
  text += std::to_string(extension.manifest_version);
  text += ")";
  return text;
}

}  // namespace core
}  // namespace browserx
