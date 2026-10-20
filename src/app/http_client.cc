// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/http_client.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <string_view>

#include "core/format.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace browserx {
namespace app {
namespace {

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string TrimSpaces(std::string_view text) {
  size_t start = 0;
  size_t end = text.size();
  while (start < end && (text[start] == ' ' || text[start] == '\t')) ++start;
  while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r')) {
    --end;
  }
  return std::string(text.substr(start, end - start));
}

// --- socket lifetime -------------------------------------------------------
// Winsock needs one process-wide startup. A shared counter keeps the call cheap and
// correct whether the first request comes from the AI page or from a probe.
#if defined(_WIN32)
void EnsureWinsock() {
  static bool started = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  (void)started;
}
#endif

struct SocketHandle {
#ifdef _WIN32
  SOCKET handle = INVALID_SOCKET;
  bool valid() const { return handle != INVALID_SOCKET; }
  void Close() {
    if (valid()) closesocket(handle);
    handle = INVALID_SOCKET;
  }
#else
  int handle = -1;
  bool valid() const { return handle >= 0; }
  void Close() {
    if (valid()) close(handle);
    handle = -1;
  }
#endif
  ~SocketHandle() { Close(); }
};

void SetNonBlocking(SocketHandle& socket) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(socket.handle, FIONBIO, &mode);
#else
  const int flags = fcntl(socket.handle, F_GETFL, 0);
  if (flags >= 0) fcntl(socket.handle, F_SETFL, flags | O_NONBLOCK);
#endif
}

/// Maps a socket error *code* to a Chromium-style net error, so the UI has one
/// vocabulary. Taking the code as an argument matters: a non-blocking connect fails with
/// EINPROGRESS in errno and the real reason only in SO_ERROR, and a message that says
/// "operation now in progress" after a refused connection is worse than no message.
int NetErrorFromConnectCode(int code) {
#ifdef _WIN32
  if (code == WSAECONNREFUSED) return -2;
  if (code == WSAETIMEDOUT) return -7;
  if (code == WSAHOST_NOT_FOUND || code == WSATRY_AGAIN) return -105;
  if (code == WSAENETUNREACH || code == WSAEHOSTUNREACH) return -106;
  return -2;
#else
  if (code == ECONNREFUSED) return -2;
  if (code == ETIMEDOUT) return -7;
  if (code == EHOSTUNREACH || code == ENETUNREACH) return -106;
  return -2;
#endif
}

/// The same code as words, because "errno 111" is not an explanation.
std::string ConnectCodeText(int code) {
#ifdef _WIN32
  switch (code) {
    case WSAECONNREFUSED: return "connection refused - nothing is listening on that port";
    case WSAETIMEDOUT: return "connection timed out";
    case WSAHOST_NOT_FOUND: return "host not found";
    case WSAENETUNREACH: return "network unreachable";
    case WSAEHOSTUNREACH: return "host unreachable";
    default: return "socket error " + std::to_string(code);
  }
#else
  switch (code) {
    case ECONNREFUSED: return "connection refused - nothing is listening on that port";
    case ETIMEDOUT: return "connection timed out";
    case ENETUNREACH: return "network unreachable";
    case EHOSTUNREACH: return "host unreachable";
    default: return std::string(std::strerror(code));
  }
#endif
}

std::string SocketErrnoText() {
#ifdef _WIN32
  return "socket error " + std::to_string(WSAGetLastError());
#else
  return std::string(std::strerror(errno));
#endif
}

bool WaitWritable(SocketHandle& socket, int timeout_ms) {
  fd_set writable;
  fd_set errors;
  FD_ZERO(&writable);
  FD_ZERO(&errors);
  FD_SET(socket.handle, &writable);
  FD_SET(socket.handle, &errors);
  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
  const int ready = select(0, nullptr, &writable, &errors, &tv);
#else
  const int ready = select(socket.handle + 1, nullptr, &writable, &errors, &tv);
#endif
  if (ready <= 0) return false;
  return FD_ISSET(socket.handle, &writable) != 0;
}

bool WaitReadable(SocketHandle& socket, int timeout_ms) {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(socket.handle, &readable);
  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
  const int ready = select(0, &readable, nullptr, nullptr, &tv);
#else
  const int ready = select(socket.handle + 1, &readable, nullptr, nullptr, &tv);
#endif
  return ready > 0 && FD_ISSET(socket.handle, &readable) != 0;
}

}  // namespace

const std::string* HttpResponse::Header(std::string_view name) const {
  const std::string wanted = Lower(name);
  for (const HttpHeader& header : headers) {
    if (Lower(header.name) == wanted) return &header.value;
  }
  return nullptr;
}

ParsedHttpUrl ParseHttpUrl(std::string_view url) {
  ParsedHttpUrl parsed;
  const std::string trimmed = core::fmt::Trim(url);
  if (trimmed.empty()) {
    parsed.error = "empty URL";
    return parsed;
  }
  size_t at = 0;
  if (trimmed.compare(0, 7, "http://") == 0) {
    at = 7;
  } else if (trimmed.compare(0, 8, "https://") == 0) {
    at = 8;
    parsed.tls = true;
  } else {
    parsed.error = "only http:// and https:// URLs are understood";
    return parsed;
  }
  const std::string rest = trimmed.substr(at);
  const size_t slash = rest.find_first_of("/?#");
  const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
  parsed.path = slash == std::string::npos ? "/" : rest.substr(slash);
  if (!parsed.path.empty() && parsed.path[0] != '/') parsed.path = "/" + parsed.path;
  if (authority.empty()) {
    parsed.error = "no host in URL";
    return parsed;
  }
  if (authority.find('@') != std::string::npos) {
    parsed.error = "credentials in a URL are refused";
    return parsed;
  }
  if (authority.front() == '[') {  // [::1]:11434
    const size_t close = authority.find(']');
    if (close == std::string::npos) {
      parsed.error = "unterminated IPv6 literal";
      return parsed;
    }
    parsed.host = authority.substr(1, close - 1);
    if (close + 1 < authority.size() && authority[close + 1] == ':') {
      parsed.port = std::atoi(authority.c_str() + close + 2);
    }
  } else {
    const size_t colon = authority.find(':');
    parsed.host = colon == std::string::npos ? authority : authority.substr(0, colon);
    if (colon != std::string::npos) parsed.port = std::atoi(authority.c_str() + colon + 1);
  }
  if (parsed.host.empty()) {
    parsed.error = "no host in URL";
    return parsed;
  }
  if (parsed.port < 1 || parsed.port > 65535) {
    parsed.error = "port out of range";
    return parsed;
  }
  parsed.valid = true;
  return parsed;
}

bool HostIsLoopback(std::string_view host) {
  const std::string lowered = Lower(host);
  if (lowered == "localhost" || lowered == "::1") return true;
  // 127.0.0.0/8 only, parsed as an address. A prefix test would accept
  // "127.0.0.1.example.com", which resolves wherever its owner likes.
  const std::vector<std::string> parts = core::fmt::Split(lowered, '.');
  if (parts.size() != 4 || parts[0] != "127") return false;
  for (const std::string& part : parts) {
    if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) return false;
  }
  return true;
}

bool HttpResponseParser::Feed(const char* data, size_t size) {
  if (done_ || failed_) return true;
  buffer_.append(data, size);

  if (!head_parsed_) {
    if (!ParseHead()) return !failed_;
  }
  if (head_parsed_) {
    if (chunked_) {
      DecodeChunked();
    } else if (content_length_ > 0) {
      const size_t have = body_.size();
      const size_t need = content_length_ > have ? content_length_ - have : 0;
      const size_t take = std::min(need, buffer_.size());
      body_.append(buffer_, 0, take);
      buffer_.erase(0, take);
      if (body_.size() >= content_length_) done_ = true;
      if (body_.size() > kMaxBodyBytes) {
        failed_ = true;
        error_ = "response larger than the 256 KB cap";
      }
    } else {
      // No length and not chunked: the body ends when the connection does.
      close_delimited_ = true;
      if (body_.size() + buffer_.size() > kMaxBodyBytes) {
        failed_ = true;
        error_ = "response larger than the 256 KB cap";
        return false;
      }
      body_ += buffer_;
      buffer_.clear();
    }
  }
  return !failed_;
}

bool HttpResponseParser::ParseHead() {
  size_t head_end = buffer_.find("\r\n\r\n");
  size_t separator = 4;
  if (head_end == std::string::npos) {
    head_end = buffer_.find("\n\n");
    separator = 2;
  }
  if (head_end == std::string::npos) {
    if (buffer_.size() > kMaxHeaderBytes) {
      headers_too_large_ = true;
      failed_ = true;
      error_ = "HTTP headers larger than 64 KB";
      return false;
    }
    return false;  // need more bytes
  }
  if (head_end > kMaxHeaderBytes) {
    headers_too_large_ = true;
    failed_ = true;
    error_ = "HTTP headers larger than 64 KB";
    return false;
  }

  const std::string head = buffer_.substr(0, head_end);
  buffer_.erase(0, head_end + separator);
  const std::vector<std::string> lines = core::fmt::SplitAny(head, "\r\n");
  if (lines.empty()) {
    failed_ = true;
    error_ = "empty HTTP response";
    return false;
  }
  // "HTTP/1.1 200 OK"
  const std::vector<std::string> status_parts = core::fmt::Split(lines[0], ' ');
  if (status_parts.size() < 2 || status_parts[0].rfind("HTTP/", 0) != 0) {
    failed_ = true;
    error_ = "not an HTTP response: " + core::fmt::Trim(lines[0]).substr(0, 60);
    return false;
  }
  status_ = std::atoi(status_parts[1].c_str());
  if (status_ < 100 || status_ > 599) {
    failed_ = true;
    error_ = "HTTP status " + status_parts[1] + " is not a status code";
    return false;
  }
  for (size_t i = 1; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (line.empty()) continue;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;  // a header without a colon is ignored
    HttpHeader header;
    header.name = core::fmt::Trim(line.substr(0, colon));
    header.value = TrimSpaces(std::string_view(line).substr(colon + 1));
    headers_.push_back(std::move(header));
  }
  head_parsed_ = true;

  if (const std::string* encoding = Header("transfer-encoding")) {
    chunked_ = Lower(*encoding).find("chunked") != std::string::npos;
  }
  if (!chunked_) {
    if (const std::string* length = Header("content-length")) {
      content_length_ = static_cast<size_t>(std::strtoull(length->c_str(), nullptr, 10));
      if (content_length_ > kMaxBodyBytes) {
        failed_ = true;
        error_ = "server announced a body larger than the 256 KB cap";
        return false;
      }
      if (content_length_ == 0) done_ = true;
    }
  }
  // Informational 1xx and 204/304 have no body.
  if (status_ == 204 || status_ == 304) done_ = true;
  return true;
}

bool HttpResponseParser::DecodeChunked() {
  while (!done_ && !failed_) {
    // A pending chunk we have not finished reading.
    if (!buffer_.empty()) {
      const size_t line_end = buffer_.find("\r\n");
      if (line_end == std::string::npos) {
        if (buffer_.size() > 1024) {  // a chunk-size line is short; more is nonsense
          failed_ = true;
          error_ = "chunked encoding: size line too long";
        }
        return !failed_;
      }
      const std::string size_text = buffer_.substr(0, line_end);
      char* end = nullptr;
      const unsigned long long chunk =
          std::strtoull(size_text.c_str(), &end, 16);  // hex
      if (end == size_text.c_str()) {
        failed_ = true;
        error_ = "chunked encoding: bad chunk size";
        return false;
      }
      if (chunk == 0) {
        // Zero chunk: trailers (if any) end with a blank line. Either way, we are done.
        const size_t trailer_end = buffer_.find("\r\n\r\n");
        if (trailer_end == std::string::npos && buffer_.size() < 1024) {
          // Wait for trailers; if none arrive, the connection close ends it.
          return true;
        }
        done_ = true;
        return true;
      }
      const size_t needed = line_end + 2 + static_cast<size_t>(chunk) + 2;
      if (buffer_.size() < needed) {
        // Keep what we have; more bytes are coming.
        if (body_.size() + buffer_.size() > kMaxBodyBytes) {
          failed_ = true;
          error_ = "response larger than the 256 KB cap";
          return false;
        }
        return true;
      }
      if (body_.size() + chunk > kMaxBodyBytes) {
        failed_ = true;
        error_ = "response larger than the 256 KB cap";
        return false;
      }
      body_.append(buffer_, line_end + 2, static_cast<size_t>(chunk));
      buffer_.erase(0, needed);
      continue;
    }
    return true;
  }
  return !failed_;
}

const std::string* HttpResponseParser::Header(std::string_view name) const {
  const std::string wanted = Lower(name);
  for (const HttpHeader& header : headers_) {
    if (Lower(header.name) == wanted) return &header.value;
  }
  return nullptr;
}

HttpResponse HttpFetch(const HttpRequest& request) {
  HttpResponse response;
  const ParsedHttpUrl url = ParseHttpUrl(request.url);
  if (!url.valid) {
    response.error = url.error;
    response.net_error = -300;  // ERR_INVALID_URL
    return response;
  }
  if (url.tls) {
    response.error = "https:// is not handled by this client; the caller uses CEF for TLS";
    response.net_error = -300;
    return response;
  }
  // This client is for local tools. Sending a prompt to an arbitrary host over plain HTTP
  // is exactly what the AI config refuses, so it refuses here too rather than relying on
  // its only current caller to have checked.
  if (!HostIsLoopback(url.host)) {
    response.error =
        "refusing plain http to a non-loopback host: the request would travel in clear text";
    response.net_error = -300;
    return response;
  }
#if defined(_WIN32)
  EnsureWinsock();
#endif

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* resolved = nullptr;
  const std::string port_text = std::to_string(url.port);
  if (getaddrinfo(url.host.c_str(), port_text.c_str(), &hints, &resolved) != 0 ||
      resolved == nullptr) {
    response.error = "could not resolve " + url.host;
    response.net_error = -105;  // ERR_NAME_NOT_RESOLVED
    return response;
  }

  SocketHandle socket;
  int last_connect_code = 0;
  for (addrinfo* candidate = resolved; candidate != nullptr; candidate = candidate->ai_next) {
#ifdef _WIN32
    socket.handle = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (socket.handle == INVALID_SOCKET) continue;
#else
    socket.handle = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (socket.handle < 0) continue;
#endif
    SetNonBlocking(socket);
    const int connected =
        ::connect(socket.handle, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen));
    bool ok = connected == 0;
    int failure = 0;
    if (!ok && WaitWritable(socket, request.timeout_ms)) {
      // This read *clears* the socket's pending error, so the value has to be kept:
      // asking a second time would return 0 and report "EINPROGRESS", which is what the
      // first version of this code did after a refused connection.
      int so_error = 0;
      socklen_t length = sizeof(so_error);
      if (getsockopt(socket.handle, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&so_error), &length) == 0) {
        ok = so_error == 0;
        failure = so_error;
      }
    }
    if (ok) break;
    if (last_connect_code == 0) {
      if (failure == 0) {
#ifdef _WIN32
        failure = WSAGetLastError();
#else
        failure = errno;
#endif
      }
      last_connect_code = failure;
    }
    socket.Close();
  }
  freeaddrinfo(resolved);
  if (!socket.valid()) {
    if (last_connect_code == 0) last_connect_code = ETIMEDOUT;
    response.error = "could not connect to " + url.host + ":" + port_text + " (" +
                     ConnectCodeText(last_connect_code) + ")";
    response.net_error = NetErrorFromConnectCode(last_connect_code);
    return response;
  }

  std::string request_text;
  request_text += request.method + " " + url.path + " HTTP/1.1\r\n";
  request_text += "Host: " + url.host + (url.port == 80 ? "" : ":" + port_text) + "\r\n";
  request_text += "Connection: close\r\n";
  request_text += "Accept: application/json\r\n";
  request_text += "User-Agent: BrowserX\r\n";
  if (!request.body.empty()) {
    request_text += "Content-Type: application/json\r\n";
    request_text += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
  }
  for (const HttpHeader& header : request.headers) {
    request_text += header.name + ": " + header.value + "\r\n";
  }
  request_text += "\r\n";
  request_text += request.body;

  size_t sent = 0;
  while (sent < request_text.size()) {
    if (!WaitWritable(socket, request.timeout_ms)) {
      response.error = "timed out sending the request";
      response.net_error = -7;
      return response;
    }
    const int wrote = static_cast<int>(
        ::send(socket.handle, request_text.data() + sent,
               static_cast<int>(request_text.size() - sent), 0));
    if (wrote <= 0) {
      response.error = "connection closed while sending (" + SocketErrnoText() + ")";
      response.net_error = -100;  // ERR_CONNECTION_CLOSED
      return response;
    }
    sent += static_cast<size_t>(wrote);
  }

  HttpResponseParser parser;
  char buffer[8192];
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(request.timeout_ms);
  while (!parser.Done()) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      if (!parser.Failed() && parser.Done() == false && parser.body().empty() &&
          parser.status() == 0) {
        response.error = "timed out after " + std::to_string(request.timeout_ms) + " ms";
        response.net_error = -7;
        return response;
      }
      break;  // partial but useful: the parser decides what it has
    }
    const int remaining = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    if (!WaitReadable(socket, std::min(remaining, 250))) continue;
    const int got = static_cast<int>(::recv(socket.handle, buffer, sizeof(buffer), 0));
    if (got <= 0) break;  // close (or error) ends a close-delimited body
    if (!parser.Feed(buffer, static_cast<size_t>(got))) break;
  }

  if (parser.Failed()) {
    response.error = parser.Error();
    response.net_error = -324;  // ERR_EMPTY_RESPONSE is the closest single code
    return response;
  }
  if (parser.status() == 0) {
    response.error = "the server closed the connection without answering";
    response.net_error = -324;
    return response;
  }
  response.ok = true;
  response.status = parser.status();
  response.headers = parser.headers();
  response.body = parser.body();
  response.headers_too_large = parser.headers_too_large();
  return response;
}

}  // namespace app
}  // namespace browserx
