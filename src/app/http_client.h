// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// A small HTTP client for the built-in AI's endpoints.
//
// Why not CefURLRequest? Because the AI endpoints are, by default, *loopback* addresses
// with no TLS, and a request born in the browser process would go through Chromium's
// network stack, its proxy configuration and its TLS requirements. For "is Ollama running
// on this machine?" that is the wrong tool, and it would make the answer depend on a
// proxy setting the user chose for browsing. A direct socket says exactly what happened.
//
// Scope, stated so nobody expects more: http:// only, a bounded response (256 KB), a hard
// timeout, no redirects, no cookies, no proxy, no chunked decoding beyond the basic case.
// https:// is refused here and handled by the caller through CEF, which owns TLS. That
// split is deliberate: this file never pretends to be a TLS stack.
//
// The parser is feed-based so it can be unit-tested byte by byte, including a response
// split across packets, which is the case a naive implementation gets wrong.

#ifndef BROWSERX_APP_http_client_h_
#define BROWSERX_APP_http_client_h_

#include <cstddef>
#include <string>
#include <vector>

namespace browserx {
namespace app {

struct HttpHeader {
  std::string name;
  std::string value;
};

struct HttpRequest {
  std::string method = "GET";
  std::string url;  // http://host[:port]/path
  std::vector<HttpHeader> headers;
  std::string body;
  int timeout_ms = 8000;
};

struct HttpResponse {
  /// True when an HTTP response was received and parsed (any status).
  bool ok = false;
  int status = 0;
  std::vector<HttpHeader> headers;
  std::string body;
  /// A transport failure: DNS, refused, timeout, or a URL this client refuses.
  std::string error;
  /// Chromium-style net error (-2 refused, -105 name not resolved, -7 timeout, …) so
  /// the UI can explain a local-provider failure with the vocabulary used elsewhere.
  int net_error = 0;
  /// Set when the header block was larger than the cap (a server answering with
  /// megabytes of headers is not a server this feature can talk to).
  bool headers_too_large = false;

  const std::string* Header(std::string_view name) const;
};

/// Performs one request. Never throws; a failure is `ok == false` with `error` set.
HttpResponse HttpFetch(const HttpRequest& request);

/// Splits `http://host[:port]/path` into pieces. Exposed for tests and for the caller
/// that has to name the host in an error message.
struct ParsedHttpUrl {
  bool valid = false;
  bool tls = false;  // https:// — refused by HttpFetch, accepted by the validator
  std::string host;
  int port = 80;
  std::string path;  // includes the query, never empty ("/")
  std::string error;
};
ParsedHttpUrl ParseHttpUrl(std::string_view url);
bool HostIsLoopback(std::string_view host);

/// A response parser that takes bytes as they arrive. `Done()` is true once the body is
/// complete (by Content-Length, by chunked terminator, or by connection close).
class HttpResponseParser {
 public:
  /// Feeds bytes; returns false when the response violates HTTP badly enough to stop.
  bool Feed(const char* data, size_t size);
  bool Done() const { return done_; }
  bool Failed() const { return failed_; }
  const std::string& Error() const { return error_; }

  int status() const { return status_; }
  const std::vector<HttpHeader>& headers() const { return headers_; }
  const std::string& body() const { return body_; }
  bool headers_too_large() const { return headers_too_large_; }
  /// True when the server said it would close the connection to end the body.
  bool close_delimited() const { return close_delimited_; }

  static constexpr size_t kMaxHeaderBytes = 64 * 1024;
  static constexpr size_t kMaxBodyBytes = 256 * 1024;

  const std::string* Header(std::string_view name) const;

 private:
  bool ParseHead();
  bool DecodeChunked();

  std::string buffer_;  // unparsed bytes (head, then chunk framing)
  bool head_parsed_ = false;
  bool chunked_ = false;
  bool done_ = false;
  bool failed_ = false;
  bool headers_too_large_ = false;
  bool close_delimited_ = false;
  size_t content_length_ = 0;
  int status_ = 0;
  std::vector<HttpHeader> headers_;
  std::string body_;
  std::string error_;
};

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_http_client_h_
