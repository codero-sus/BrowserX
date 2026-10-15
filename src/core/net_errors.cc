// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/net_errors.h"

#include <algorithm>

#include "core/format.h"

namespace browserx {
namespace core {
namespace {

using fmt::ToLower;
using fmt::ToUpper;

NetErrorHelp Make(int code, const std::string& name, NetErrorFamily family,
                  const std::string& headline, const std::string& explanation,
                  std::vector<std::string> try_first,
                  bool security_relevant = false, bool caused_by_browser = false) {
  NetErrorHelp help;
  help.code = code;
  help.name = name;
  help.family = family;
  help.headline = headline;
  help.explanation = explanation;
  help.try_first = std::move(try_first);
  help.security_relevant = security_relevant;
  help.caused_by_browser = caused_by_browser;
  return help;
}

/// The table. Every number here was read from upstream `net/base/net_error_list.h`
/// (chromium/chromium) rather than recalled, and tests/unit/net_errors_test.cc checks the
/// number-to-name pairs, so a wrong digit is a failing test rather than a wrong page.
const std::vector<NetErrorHelp>& Table() {
  static const std::vector<NetErrorHelp> table = {
      Make(-3, "ABORTED", NetErrorFamily::kLocalInput, "Loading was stopped",
           "Something cancelled this load before the page arrived - usually pressing Stop, "
           "or clicking a link that replaced it.",
           {"Reload the page if you did not mean to stop it.",
            "If it happened on its own, check browserx://session for what the tab was doing."},
           false, true),
      Make(-6, "FILE_NOT_FOUND", NetErrorFamily::kLocalInput, "That file is not there",
           "The path was opened as a local file, and nothing exists at that path.",
           {"Check the path for a typo - local paths are case sensitive on Linux.",
            "Use the address bar's folder listing by opening the parent folder instead."}),
      Make(-7, "TIMED_OUT", NetErrorFamily::kTimeout, "The operation timed out",
           "Chromium gave the operation a deadline and it passed without completing.",
           {"Try again: a slow server often answers on the second attempt.",
            "Check whether every site is slow or just this one - that tells you which side "
            "the problem is on."}),
      Make(-20, "BLOCKED_BY_CLIENT", NetErrorFamily::kBlocked, "BrowserX blocked this request",
           "This browser chose not to make the request. Nothing about it reached the "
           "network, so the site never saw you ask.",
           {"Open browserx://settings to see the shield and allow the host if you trust it.",
            "If a mode changed what is blocked, browserx://modes shows which one is active "
            "and what it changes."},
           false, true),
      Make(-21, "NETWORK_CHANGED", NetErrorFamily::kConnection, "The network changed",
           "The connection was interrupted because the machine's network changed - Wi-Fi to "
           "mobile data, a VPN connecting, or a cable being unplugged.",
           {"Reload: the request is usually fine on the new network.",
            "If a VPN just connected, wait for it and reload."}),
      Make(-27, "BLOCKED_BY_RESPONSE", NetErrorFamily::kBlocked, "The site refused to be shown here",
           "The response arrived, but its own rules forbid showing it in this context - an "
           "X-Frame-Options or Content-Security-Policy ancestor check, or a "
           "Cross-Origin-Resource-Policy check.",
           {"Open the address in its own tab rather than inside a frame.",
            "If you are the site's author, that header is the thing to look at."},
           false, true),
      Make(-100, "CONNECTION_CLOSED", NetErrorFamily::kConnection, "The server closed the connection",
           "A connection was opened and then closed cleanly before the page was complete.",
           {"Reload the page.",
            "If it repeats, the server is likely restarting or under load rather than "
            "being unreachable."}),
      Make(-101, "CONNECTION_RESET", NetErrorFamily::kConnection, "The connection was reset",
           "The connection was cut off abruptly rather than closed politely, which is what "
           "a crashed server, a middlebox or a firewall looks like from here.",
           {"Reload; a reset is often momentary.",
            "If every site on this network resets, something between you and them is doing "
            "it - try a different network to confirm."}),
      Make(-102, "CONNECTION_REFUSED", NetErrorFamily::kConnection, "The site refused the connection",
           "The address was found, but nothing accepted the connection on that port. This "
           "is the normal answer from a machine that is up but not running a web server.",
           {"Check the port: :8080 on a server that listens on :3000 refuses exactly like "
            "this.",
            "If you are running the server locally, start it and reload.",
            "If the site is not yours, it is down - the name resolved, so the address was "
            "right."}),
      Make(-105, "NAME_NOT_RESOLVED", NetErrorFamily::kDns, "That address could not be found",
           "The name could not be turned into an address, so no connection was made. Often "
           "a typo, sometimes DNS, occasionally a name that only exists on another network.",
           {"Check the spelling - a single wrong letter gives exactly this.",
            "Try another site: if none resolve, it is DNS or the connection, not the name.",
            "If the name exists only on a VPN, connect and reload."}),
      Make(-106, "INTERNET_DISCONNECTED", NetErrorFamily::kDns, "There is no internet connection",
           "The machine reports no working network connection at all.",
           {"Check Wi-Fi or the cable.",
            "If other apps work fine, the browser may be pointed at a proxy that is down."}),
      Make(-107, "SSL_PROTOCOL_ERROR", NetErrorFamily::kTls, "The secure connection failed",
           "Something in the handshake between this browser and the server did not make "
           "sense. It is sometimes a server misconfiguration and sometimes a middlebox "
           "interfering with the connection.",
           {"Reload once: a handshake failure can be a one-off.",
            "If it repeats on a site you trust, its certificate configuration is broken - "
            "and that is worth telling them.",
            "On a network you do not control, treat this as a warning about the network."},
           true),
      Make(-109, "ADDRESS_UNREACHABLE", NetErrorFamily::kConnection, "The address is unreachable",
           "There is no route from this machine to that address - not a refused connection, "
           "an impossible one.",
           {"Check whether the address is on a network you are not connected to.",
            "If it is a private address (10.x, 192.168.x), connect to that network or its "
            "VPN first."}),
      Make(-113, "SSL_VERSION_OR_CIPHER_MISMATCH", NetErrorFamily::kTls, "No shared encryption",
           "The server and this browser do not support a common protocol version or cipher "
           "suite, so no secure connection can be negotiated.",
           {"Update the browser: a server that dropped an old protocol is the usual cause.",
            "If the site is old enough to require a protocol Chromium removed, it cannot be "
            "opened securely any more - and opening it insecurely is not an option this "
            "browser offers."},
           true),
      Make(-118, "CONNECTION_TIMED_OUT", NetErrorFamily::kTimeout, "The connection timed out",
           "The address was known, the connection was attempted, and nothing answered in "
           "time - typical of a firewall that drops packets silently rather than refusing "
           "them.",
           {"Reload once.",
            "Check whether a VPN or firewall is dropping traffic to that host.",
            "If every site times out, the network is dropping traffic - a VPN that failed "
            "to come up does exactly this."}),
      Make(-130, "PROXY_CONNECTION_FAILED", NetErrorFamily::kConnection, "The proxy did not answer",
           "A proxy is configured and the connection to the proxy itself failed.",
           {"Check the proxy settings, or stop using the proxy to confirm this is the cause.",
            "If the proxy is required by your network, it is the proxy that is down."}),
      Make(-131, "MANDATORY_PROXY_CONFIGURATION_FAILED", NetErrorFamily::kConnection,
           "The required proxy configuration could not be fetched",
           "This network says a proxy is mandatory and its configuration could not be "
           "retrieved, so the browser has nothing to route through.",
           {"Reconnect to the network to retry the configuration download.",
            "If you are on a network that requires a proxy script, that script's host may be "
            "unreachable from where you are."}),
      Make(-137, "NAME_RESOLUTION_FAILED", NetErrorFamily::kDns, "Name resolution failed",
           "The lookup itself failed rather than reporting that the name does not exist. "
           "That distinction matters: a typo gives NAME_NOT_RESOLVED, while this is the "
           "resolver being unable to answer at all.",
           {"Try another site to see whether DNS works at all.",
            "If DNS is the problem, a different network or DNS server will confirm it."}),
      Make(-200, "CERT_COMMON_NAME_INVALID", NetErrorFamily::kTls,
           "The certificate is for a different name",
           "The server presented a certificate that does not match the name you asked for. "
           "This is either a badly configured site or an attempt to impersonate one, and "
           "from here the two look the same.",
           {"Check the address: a look-alike host is exactly what this error protects you "
            "from.",
            "If this site is reached through a captive portal or a corporate proxy, that "
            "device is what is substituting the certificate."},
           true),
      Make(-201, "CERT_DATE_INVALID", NetErrorFamily::kTls, "The certificate is not valid now",
           "By this machine's clock the certificate is either expired or not yet valid. "
           "Sometimes the server forgot to renew; sometimes the clock here is wrong.",
           {"Check the system clock - a machine that thinks it is 2019 sees every "
            "certificate as not yet valid.",
            "If the clock is right, the site's certificate has expired."},
           true),
      Make(-202, "CERT_AUTHORITY_INVALID", NetErrorFamily::kTls,
           "The certificate is not trusted by this machine",
           "The certificate is signed by something this machine does not trust - an unknown "
           "authority, or one whose root is not installed here.",
           {"On a corporate network, the interception proxy's root is usually what is "
            "missing.",
            "Otherwise, do not continue: nothing here can tell you whether this is a "
            "misconfiguration or an attack."},
           true),
      Make(-206, "CERT_REVOKED", NetErrorFamily::kTls, "The certificate was revoked",
           "The certificate was withdrawn by the authority that issued it, which usually "
           "means the key is no longer trusted to belong to the site.",
           {"Do not continue past this one.",
            "If the site is yours, its certificate needs reissuing."},
           true),
      Make(-302, "UNKNOWN_URL_SCHEME", NetErrorFamily::kLocalInput,
           "BrowserX does not know that kind of address",
           "The address starts with a scheme this browser has no handler for, so it cannot "
           "even try to open it.",
           {"Check the beginning of the address: a typo there changes what it means.",
            "BrowserX opens http, https, browserx, file and view-source."},
           false, true),
      Make(-310, "TOO_MANY_REDIRECTS", NetErrorFamily::kProtocol,
           "The site redirected in a loop",
           "Following the redirects led back to where it started, so the browser stopped "
           "rather than looping forever.",
           {"Reload once: a session-dependent loop often clears.",
            "If a sign-in page loops, clearing the site's cookies ends the loop."}),
      Make(-324, "EMPTY_RESPONSE", NetErrorFamily::kProtocol,
           "The server sent nothing at all",
           "The connection succeeded and was then closed without a single byte of response.",
           {"Reload; a server mid-restart answers exactly like this.",
            "If the site is behind a proxy, the proxy may be dropping the response."}),
      Make(-379, "HTTP_RESPONSE_CODE_FAILURE", NetErrorFamily::kProtocol,
           "The response code was a failure",
           "The server answered with an error status that the browser could not turn into a "
           "page.",
           {"Reload once.",
            "The status code itself is the answer: 5xx is the server, 4xx is the request."}),
      Make(-400, "CACHE_MISS", NetErrorFamily::kProtocol, "Not in the cache",
           "Something asked the cache for an entry that is not there. This is a normal "
           "result, not a failure: it happens on every normal load.",
           {"Nothing: this is information for the network stack, not a problem to fix."},
           false, false),
      Make(-803, "DNS_TIMED_OUT", NetErrorFamily::kDns, "The name lookup timed out",
           "The DNS server was asked and did not answer in time, so the name is neither "
           "confirmed nor denied.",
           {"Reload: resolvers recover faster than pages.",
            "If it repeats, the DNS server configured for this network is the suspect."}),
  };
  return table;
}

std::string NormalizeName(std::string_view name) {
  std::string upper = ToUpper(fmt::Trim(name));
  if (fmt::StartsWith(upper, "ERR_")) upper = upper.substr(4);
  return upper;
}

}  // namespace

const char* NetErrorFamilyName(NetErrorFamily family) {
  switch (family) {
    case NetErrorFamily::kLocalInput: return "input";
    case NetErrorFamily::kBlocked: return "blocked";
    case NetErrorFamily::kDns: return "dns";
    case NetErrorFamily::kConnection: return "connection";
    case NetErrorFamily::kTimeout: return "timeout";
    case NetErrorFamily::kProtocol: return "protocol";
    case NetErrorFamily::kTls: return "tls";
    case NetErrorFamily::kUnknown: break;
  }
  return "unknown";
}

const std::vector<int>& KnownNetErrorCodes() {
  static const std::vector<int> codes = [] {
    std::vector<int> out;
    out.reserve(Table().size());
    for (const NetErrorHelp& help : Table()) out.push_back(help.code);
    return out;
  }();
  return codes;
}

std::string FormatNetErrorCode(int code) {
  for (const NetErrorHelp& help : Table()) {
    if (help.code == code) return "ERR_" + help.name + " (" + std::to_string(code) + ")";
  }
  // No name invented for a code this table has not read from upstream.
  return "error code " + std::to_string(code);
}

NetErrorHelp ExplainNetError(int code) {
  for (const NetErrorHelp& help : Table()) {
    if (help.code == code) return help;
  }
  return Make(code, "", NetErrorFamily::kUnknown, "This browser cannot explain that failure",
              "The page did not load and the reason was reported as code " +
                  std::to_string(code) +
                  ", which is not one of the codes BrowserX has an explanation for. Rather "
                  "than guess at what it means, it is printed as it came: the code is in the "
                  "status bar and in the log with the URL that failed.",
              {"Reload once - many failures are momentary.",
               "If it repeats, the code above is the exact thing to search for or report."},
              false, true);
}

NetErrorHelp ExplainNetErrorName(std::string_view name) {
  const std::string wanted = NormalizeName(name);
  if (!wanted.empty()) {
    for (const NetErrorHelp& help : Table()) {
      if (help.name == wanted) return help;
    }
  }
  if (wanted.empty()) {
    return Make(0, "", NetErrorFamily::kUnknown, "This browser cannot explain that failure",
                "The page did not load, and no error code came with the report. Nothing is "
                "guessed at here: reload once, and if it repeats the log has the URL.",
                {"Reload once.", "Check browserx://session for what the tab was doing."},
                false, true);
  }
  return Make(0, wanted, NetErrorFamily::kUnknown, "This browser cannot explain that failure",
              "The page did not load and the reason was reported as ERR_" + wanted +
                  ", which is not a code this build has an explanation for. Printing the "
                  "name unchanged is more useful than a confident guess.",
              {"Reload once - many failures are momentary.",
               "Otherwise the name above is the exact thing to search for or report."},
              false, true);
}

int NetErrorCodeForName(std::string_view name) {
  const NetErrorHelp help = ExplainNetErrorName(name);
  return help.family == NetErrorFamily::kUnknown ? 0 : help.code;
}

bool NetErrorWarrantsPage(int code) {
  // Stopping a load is not a failure, and a cache miss is how the cache is supposed to
  // behave. Interrupting someone with a full page for either would train them to dismiss
  // error pages, which is the opposite of what this page is for.
  return code != -3 && code != -400;
}

}  // namespace core
}  // namespace browserx
