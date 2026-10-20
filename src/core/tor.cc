// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/tor.h"

#include <cstdio>
#include <string>

namespace browserx {
namespace core {
namespace {

PlannedSwitch Switch(const char* id, std::string value, const char* reason,
                     const char* provenance) {
  PlannedSwitch entry;
  entry.id = id;
  entry.value = std::move(value);
  entry.reason = reason;
  entry.provenance = provenance;
  // Every one of these is read by Chromium at start-up and cannot be changed later, so
  // the UI can honestly say "restart to apply" instead of pretending it is live.
  entry.restart_required = true;
  return entry;
}

std::string PortString(int port) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d", port);
  return std::string(buffer);
}

}  // namespace

bool ValidTorPort(int port) { return port > 0 && port <= 65535; }

bool ParseTorPort(const std::string& text, int* out) {
  if (text.empty() || text.size() > 5) return false;
  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;   // a letter in a port is a typo, not a port
    value = value * 10 + (c - '0');
  }
  if (!ValidTorPort(value)) return false;
  if (out != nullptr) *out = value;
  return true;
}

TorPlan MakeTorPlan(int port) {
  if (!ValidTorPort(port)) port = kDefaultTorSocksPort;
  TorPlan plan;
  plan.port = port;

  // socks5:// (not socks4://) is what makes Chromium hand *host names* to the proxy
  // instead of resolving them locally first. With socks4 the DNS lookup happens in this
  // process, in clear text, to the local resolver - which defeats the point.
  plan.switches.push_back(Switch(
      "proxy-server", "socks5://127.0.0.1:" + PortString(port),
      "route every request through Tor's SOCKS5 port; socks5 (not socks4) sends host "
      "names to the proxy so DNS is resolved by the exit node",
      "verified (net/docs/proxy.md: SOCKS5 resolves hostnames remotely)"));

  // Belt and braces for the DNS question: if some component resolves a name outside the
  // proxy, this makes that lookup fail instead of succeeding and revealing the site to the
  // local resolver. Loopback is excluded so a local service is still reachable.
  plan.switches.push_back(Switch(
      "host-resolver-rules", "MAP * ~NOTFOUND , EXCLUDE 127.0.0.1",
      "fail every name lookup that is not going through the proxy, so a missed lookup "
      "cannot quietly resolve locally and leak the site being visited",
      "verified (net/base/features / host resolver rules documented in net/docs)"));

  // QUIC runs over UDP and does not use the HTTP proxy. A page that negotiated h3 would
  // connect directly, from this machine's address, while everything else went via Tor.
  plan.switches.push_back(Switch(
      "disable-quic",
      "",
      "QUIC is UDP and bypasses a SOCKS proxy entirely; in Tor mode that is a direct "
      "connection from your real address",
      "verified (net/base/features.cc: kQuicEnabled)"));

  // WebRTC can open UDP sockets outside the proxy and hand a page the machine's real
  // address. This policy is the documented way to forbid non-proxied UDP.
  plan.switches.push_back(Switch(
      "force-webrtc-ip-handling-policy", "disable_non_proxied_udp",
      "WebRTC would otherwise open UDP outside the proxy and reveal this machine's address",
      "verified (content/public/common/content_switches.cc: kForceWebRtcIPHandlingPolicy)"));

  plan.warnings = TorWarnings();
  return plan;
}

std::vector<std::string> TorWarnings() {
  // Ordered by how much a misunderstanding would hurt. The first line is the one that
  // matters: everything after it is a detail of the same point.
  return {
      "BrowserX Tor mode is a SOCKS5 proxy to a Tor process you run. It is not Tor "
      "Browser, and it does not give you Tor Browser's protection.",
      "Fingerprinting: your window size, fonts, screen resolution, timezone, language and "
      "the JavaScript your pages run can identify this browser even behind Tor. Tor Browser "
      "makes all of that uniform; BrowserX does not, and cannot without changing how the "
      "engine behaves.",
      "Unencrypted sites: a Tor exit node can read any page that is served over plain "
      "HTTP, and can change it. The padlock, not the proxy, is what protects the content.",
      "Extensions and downloads can reach the network outside the proxy. Turn extensions "
      "off for a session where anonymity is the point.",
      "Your internet provider can see that you are using Tor, even though not what you "
      "visit. In some networks that is itself identifying.",
      "If anonymity is the requirement rather than a preference, use Tor Browser. That is "
      "not modesty about this feature - it is the accurate answer.",
  };
}

std::string DescribeTorStatus(bool reachable, int port) {
  const std::string where = "127.0.0.1:" + PortString(port);
  if (reachable) {
    return "Tor mode is on and something is listening on " + where +
           ". Traffic is routed through it. Read the limits below: this is a proxy, not "
           "Tor Browser.";
  }
  return "Tor mode is on but nothing is listening on " + where +
         ". Pages will fail to load until a Tor process is running - that failure is the "
         "honest outcome, and better than falling back to a direct connection without "
         "telling you.";
}

std::string TorStatusBarText(bool reachable, int port) {
  const std::string where = "127.0.0.1:" + PortString(port);
  if (reachable) {
    return "Tor: routed through " + where + " (proxy only - not Tor Browser)";
  }
  return "Tor: no Tor process on " + where + " - open browserx://tor";
}

}  // namespace core
}  // namespace browserx
