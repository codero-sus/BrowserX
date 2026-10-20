// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for the AI features' HTTP client and response parser.
//
// The parser is tested against the responses real servers send — including one split
// across packets and one chunked — because the AI endpoints are local servers that are
// often old, plain-HTTP and occasionally wrong about Content-Length.

#include "app/http_client.h"

#include "test_harness.h"

namespace {

using browserx::app::HttpResponseParser;
using browserx::app::ParseHttpUrl;

std::vector<char> Bytes(const std::string& text) { return std::vector<char>(text.begin(), text.end()); }

}  // namespace

TEST(HttpClient, ParsesLoopbackUrlsWithPortsAndPaths) {
  const auto url = ParseHttpUrl("http://127.0.0.1:11434/api/chat");
  EXPECT_TRUE(url.valid);
  EXPECT_EQ(url.host, "127.0.0.1");
  EXPECT_EQ(url.port, 11434);
  EXPECT_EQ(url.path, "/api/chat");
  EXPECT_FALSE(url.tls);

  const auto bare = ParseHttpUrl("http://localhost");
  EXPECT_TRUE(bare.valid);
  EXPECT_EQ(bare.path, "/");
  EXPECT_EQ(bare.port, 80);

  const auto https = ParseHttpUrl("https://api.openai.com/v1/chat/completions");
  EXPECT_TRUE(https.valid);
  EXPECT_TRUE(https.tls);
  EXPECT_EQ(https.port, 80) << "tls defaults are the caller's business, not guessed here";
}

TEST(HttpClient, RefusesUrlsItCannotHonour) {
  EXPECT_FALSE(ParseHttpUrl("").valid);
  EXPECT_FALSE(ParseHttpUrl("file:///etc/passwd").valid);
  EXPECT_FALSE(ParseHttpUrl("http://user:pass@127.0.0.1/").valid)
      << "credentials in a URL must not be forwarded or logged";
  EXPECT_FALSE(ParseHttpUrl("http://127.0.0.1:70000/").valid);
  EXPECT_FALSE(ParseHttpUrl("http:///nohost").valid);
}

TEST(HttpClient, KnowsWhatLoopbackIs) {
  EXPECT_TRUE(browserx::app::HostIsLoopback("127.0.0.1"));
  EXPECT_TRUE(browserx::app::HostIsLoopback("127.9.9.9"));
  EXPECT_TRUE(browserx::app::HostIsLoopback("localhost"));
  EXPECT_TRUE(browserx::app::HostIsLoopback("::1"));
  EXPECT_FALSE(browserx::app::HostIsLoopback("192.168.1.2"));
  EXPECT_FALSE(browserx::app::HostIsLoopback("example.com"));
  EXPECT_FALSE(browserx::app::HostIsLoopback("127.0.0.1.example.com"));
}

TEST(HttpClient, ParsesAContentLengthResponse) {
  HttpResponseParser parser;
  const std::string response =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 7\r\n\r\n{\"a\":1}";
  EXPECT_TRUE(parser.Feed(response.data(), response.size()));
  EXPECT_TRUE(parser.Done());
  EXPECT_FALSE(parser.Failed());
  EXPECT_EQ(parser.status(), 200);
  EXPECT_EQ(parser.body(), "{\"a\":1}");
  EXPECT_TRUE(parser.Header("content-type") != nullptr);
  EXPECT_EQ(*parser.Header("content-type"), "application/json");
}

TEST(HttpClient, ParsesAResponseSplitAcrossPackets) {
  HttpResponseParser parser;
  const std::string response =
      "HTTP/1.1 201 Created\r\nContent-Length: 11\r\n\r\nhello world";
  for (const char c : response) {
    ASSERT_TRUE(parser.Feed(&c, 1));
  }
  EXPECT_TRUE(parser.Done());
  EXPECT_EQ(parser.status(), 201);
  EXPECT_EQ(parser.body(), "hello world");
}

TEST(HttpClient, ParsesAChunkedResponse) {
  HttpResponseParser parser;
  const std::string response =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
      "5\r\nhello\r\n"
      "6\r\n world\r\n"
      "0\r\n\r\n";
  // Feed in two pieces, splitting a chunk size from its data.
  const size_t cut = response.find("hello") + 2;
  ASSERT_TRUE(parser.Feed(response.data(), cut));
  ASSERT_TRUE(parser.Feed(response.data() + cut, response.size() - cut));
  EXPECT_TRUE(parser.Done());
  EXPECT_EQ(parser.status(), 200);
  EXPECT_EQ(parser.body(), "hello world");
}

TEST(HttpClient, ACloseDelimitedBodyEndsWhenTheSocketDoes) {
  HttpResponseParser parser;
  const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
  ASSERT_TRUE(parser.Feed(head.data(), head.size()));
  ASSERT_TRUE(parser.Feed("no length", 9));
  EXPECT_FALSE(parser.Done()) << "only the connection can end this body";
  EXPECT_TRUE(parser.close_delimited());
  EXPECT_EQ(parser.body(), "no length");
}

TEST(HttpClient, EmptyAndNoContentResponsesFinish) {
  HttpResponseParser empty;
  const std::string response = "HTTP/1.1 204 No Content\r\n\r\n";
  ASSERT_TRUE(empty.Feed(response.data(), response.size()));
  EXPECT_TRUE(empty.Done());
  EXPECT_EQ(empty.body(), "");
}

TEST(HttpClient, AnHtmlProxyPageIsNotAnHttpStatus) {
  HttpResponseParser parser;
  const std::string response = "<html>proxy says no</html>";
  ASSERT_TRUE(parser.Feed(response.data(), response.size()));
  EXPECT_FALSE(parser.Failed()) << "more bytes could still make a response";
  EXPECT_EQ(parser.status(), 0);
}

TEST(HttpClient, RejectsAnOversizedBodyBeforeReadingIt) {
  HttpResponseParser parser;
  const std::string response =
      "HTTP/1.1 200 OK\r\nContent-Length: 999999999\r\n\r\n";
  // Feed is allowed (and expected) to report the failure; either way it must be Failed.
  parser.Feed(response.data(), response.size());
  EXPECT_TRUE(parser.Failed());
  EXPECT_TRUE(parser.Error().find("256 KB") != std::string::npos) << parser.Error();
}

TEST(HttpClient, RejectsOversizedHeaders) {
  HttpResponseParser parser;
  std::string response = "HTTP/1.1 200 OK\r\n";
  response.append(70000, 'x');
  parser.Feed(response.data(), response.size());
  EXPECT_TRUE(parser.Failed());
  EXPECT_TRUE(parser.headers_too_large());
}

TEST(HttpClient, RefusesPlainHttpToARemoteHostBeforeOpeningAnything) {
  browserx::app::HttpRequest request;
  request.url = "http://example.com/v1/chat/completions";
  const auto response = browserx::app::HttpFetch(request);
  EXPECT_FALSE(response.ok);
  EXPECT_TRUE(response.error.find("clear text") != std::string::npos) << response.error;
}

TEST(HttpClient, RefusesHttpsInsteadOfPretendingToSpeakIt) {
  browserx::app::HttpRequest request;
  request.url = "https://127.0.0.1:11434/api/chat";
  const auto response = browserx::app::HttpFetch(request);
  EXPECT_FALSE(response.ok);
  EXPECT_TRUE(response.error.find("TLS") != std::string::npos) << response.error;
}

TEST(HttpClient, FailsFastWithANetErrorWhenNothingIsListening) {
  browserx::app::HttpRequest request;
  request.url = "http://127.0.0.1:1/api/chat";  // port 1 is not a model server
  request.timeout_ms = 600;
  const auto response = browserx::app::HttpFetch(request);
  EXPECT_FALSE(response.ok);
  EXPECT_TRUE(response.net_error < 0);
  EXPECT_TRUE(response.error.find("127.0.0.1") != std::string::npos) << response.error;
}

// Regression: the first version read SO_ERROR twice. The first read cleared it, so a
// refused connection reported "Operation now in progress" — the EINPROGRESS of the
// non-blocking connect, not the reason it failed.
TEST(HttpClient, ARefusedConnectionSaysRefusedNotInProgress) {
  browserx::app::HttpRequest request;
  request.url = "http://127.0.0.1:1/api/chat";
  request.timeout_ms = 800;
  const auto response = browserx::app::HttpFetch(request);
  EXPECT_FALSE(response.ok);
  EXPECT_TRUE(response.error.find("refused") != std::string::npos) << response.error;
  EXPECT_TRUE(response.error.find("in progress") == std::string::npos) << response.error;
  EXPECT_EQ(response.net_error, -2) << "ERR_CONNECTION_REFUSED";
}
