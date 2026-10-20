// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/tor_probe.h"

#include <chrono>
#include <cstring>
#include <string>

#include "core/tor.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace browserx {
namespace app {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void CloseSocket(SocketHandle handle) { closesocket(handle); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void CloseSocket(SocketHandle handle) { close(handle); }
#endif

/// Winsock needs a per-process start-up. Doing it here rather than at program start keeps
/// it next to the only code that uses sockets, and makes a failure to initialise a
/// "cannot probe" answer instead of a crash.
bool EnsureSockets() {
#if defined(_WIN32)
  static bool started = false;
  static bool ok = false;
  if (!started) {
    WSADATA data{};
    ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    started = true;
  }
  return ok;
#else
  return true;
#endif
}

/// Makes a socket non-blocking so connect() can be given a deadline with select().
bool SetNonBlocking(SocketHandle handle) {
#if defined(_WIN32)
  u_long mode = 1;
  return ioctlsocket(handle, FIONBIO, &mode) == 0;
#else
  const int flags = fcntl(handle, F_GETFL, 0);
  return flags >= 0 && fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

}  // namespace

TorProbeResult ProbeTor(int port, int timeout_ms) {
  TorProbeResult result;
  result.port = port;
  const auto started = std::chrono::steady_clock::now();
  auto finish = [&result, started](bool reachable, std::string detail) {
    result.reachable = reachable;
    result.detail = std::move(detail);
    result.elapsed_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
    return result;
  };

  if (!core::ValidTorPort(port)) {
    return finish(false, "that is not a valid port number");
  }
  if (!EnsureSockets()) {
    return finish(false, "this system's socket library could not be started");
  }

  const SocketHandle handle = ::socket(AF_INET, SOCK_STREAM, 0);
  if (handle == kInvalidSocket) {
    return finish(false, "could not create a socket to test the port");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 - never a remote host

  bool reachable = false;
  std::string detail;

  const bool non_blocking = SetNonBlocking(handle);
  const int connected =
      ::connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (connected == 0) {
    reachable = true;
    detail = "connected immediately";
  } else if (!non_blocking) {
    detail = "could not make the test socket non-blocking";
  } else {
    // In progress (or failed outright). select() waits for writability, which for a socket
    // mid-connect means "connected or failed", so the deadline is honoured either way.
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(handle, &writable);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    const int ready = ::select(static_cast<int>(handle) + 1, nullptr, &writable, nullptr,
                               &timeout);
    if (ready > 0) {
      int error = 0;
#if defined(_WIN32)
      int length = sizeof(error);
      if (getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0
          && error == 0) {
        reachable = true;
      }
#else
      socklen_t length = sizeof(error);
      if (getsockopt(handle, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0) {
        reachable = true;
      }
#endif
      detail = reachable ? "connected" : "the port refused the connection";
    } else if (ready == 0) {
      detail = "nothing answered within " + std::to_string(timeout_ms) + " ms";
    } else {
      detail = "the connection test failed";
    }
  }

  CloseSocket(handle);
  return finish(reachable, detail);
}

}  // namespace app
}  // namespace browserx
