#pragma once
#include "Transport.h"
#include <memory>
#include <string>

// ══════════════════════════════════════════════════════════
// EnetTransport — the transport between machines (UDP, ENet)
// ══════════════════════════════════════════════════════════
// The same IListener / IConnection the loopback gives
// (net/Transport.h), over the network. ENet itself stays inside
// EnetTransport.cpp, so nothing else includes its headers.
//
//   server:  auto l = net::enetListen(27015, 20);   (null: port in use)
//   client:  auto c = net::enetConnect("192.168.1.5", 27015);
//            connected() is true while connecting or connected,
//            false once refused, timed out or closed; what's sent
//            before the connection is up waits for it
//
// Everything is pumped from the thread that calls poll() / receive().
namespace net {

std::shared_ptr<IListener>   enetListen(int port, int maxClients);
std::shared_ptr<IConnection> enetConnect(const std::string& host, int port);

// "host:port" or "host" (the default port) → parts
inline void splitAddress(const std::string& text, std::string& host, int& port, int defaultPort = 27015) {
    size_t c = text.rfind(':');
    host = c == std::string::npos ? text : text.substr(0, c);
    port = defaultPort;
    if (c != std::string::npos) { int p = std::atoi(text.c_str() + c + 1); if (p > 0 && p < 65536) port = p; }
    if (host.empty()) host = "127.0.0.1";
}

} // namespace net
