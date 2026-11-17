#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <vector>

// ══════════════════════════════════════════════════════════
// Transport — how messages get between server and clients
// ══════════════════════════════════════════════════════════
// Two channels:
//   Reliable    arrives, in order (joining, chunks, edits, events)
//   Unreliable  may be lost or late (snapshots, inputs: a newer
//               one replaces it anyway)
//
// The server listens (IListener) and gets one IConnection per
// client; a client holds the other end. Two kinds:
//   Loopback    both ends in one process, with adjustable lag,
//               jitter and loss: the game's own server for single
//               player, and tests
//   ENet        UDP between machines (net/EnetTransport.h, phase 3)
// Nothing above this file knows which it's talking through.
namespace net {

enum class Channel : uint8_t { Reliable = 0, Unreliable = 1 };

struct Packet {
    std::vector<uint8_t> data;
    Channel channel = Channel::Reliable;
};

class IConnection {
public:
    virtual ~IConnection() = default;
    virtual void send(std::vector<uint8_t> data, Channel ch) = 0;
    virtual bool receive(Packet& out) = 0;     // false: nothing waiting
    virtual bool connected() const = 0;
    virtual void close() = 0;
    virtual float pingMs() const = 0;          // round trip, as measured
    virtual void poll() {}                     // pump the network without handing anything out
};

class IListener {
public:
    virtual ~IListener() = default;
    // A newly connected client, or null
    virtual std::shared_ptr<IConnection> accept() = 0;
    virtual void poll() {}                     // pump the network (ENet); loopback needs nothing
};

// ── Loopback ──────────────────────────────────────────────
// Lag: one way, in ms. Loss and jitter only touch Unreliable;
// Reliable packets are only ever late, in order.
struct LagSettings {
    float latencyMs = 0.0f;
    float jitterMs  = 0.0f;
    float lossPct   = 0.0f;
};

// The time messages are delivered by. Real time by default;
// tests can drive it so lag works in simulated seconds.
using ClockFn = std::function<double()>;   // seconds
inline ClockFn realClock() {
    return [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
}

class LoopbackPipe {   // one direction
public:
    LoopbackPipe(const LagSettings& l, ClockFn c, uint32_t seed) : lag(l), now(std::move(c)), rng(seed) {}
    void push(std::vector<uint8_t> d, Channel ch) {
        std::lock_guard<std::mutex> g(m);
        double t = now();
        if (ch == Channel::Unreliable && lag.lossPct > 0.0f && dist(rng) * 100.0f < lag.lossPct) return;
        double at = t + (lag.latencyMs + (lag.jitterMs > 0 ? (dist(rng) * 2.0f - 1.0f) * lag.jitterMs : 0.0f)) / 1000.0;
        if (ch == Channel::Reliable) { at = std::max(at, lastReliable); lastReliable = at; }
        q.push_back({ at, Packet{ std::move(d), ch } });
    }
    bool pop(Packet& out) {
        std::lock_guard<std::mutex> g(m);
        double t = now();
        for (auto it = q.begin(); it != q.end(); ++it)
            if (it->first <= t) {
                // reliable ones keep their order: one can't pass an earlier one
                if (it->second.channel == Channel::Reliable)
                    for (auto jt = q.begin(); jt != it; ++jt)
                        if (jt->second.channel == Channel::Reliable) goto next;
                out = std::move(it->second);
                q.erase(it);
                return true;
            next:;
            }
        return false;
    }
    LagSettings lag;
private:
    ClockFn now;
    std::mutex m;
    std::deque<std::pair<double, Packet>> q;
    double lastReliable = 0.0;
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist{ 0.0f, 1.0f };
};

class LoopbackConnection : public IConnection {
public:
    LoopbackConnection(std::shared_ptr<LoopbackPipe> out, std::shared_ptr<LoopbackPipe> in,
                       std::shared_ptr<bool> open)
        : outPipe(std::move(out)), inPipe(std::move(in)), isOpen(std::move(open)) {}
    void send(std::vector<uint8_t> d, Channel ch) override { if (*isOpen) outPipe->push(std::move(d), ch); }
    bool receive(Packet& p) override { return inPipe->pop(p); }
    bool connected() const override { return *isOpen; }
    void close() override { *isOpen = false; }
    float pingMs() const override { return (outPipe->lag.latencyMs + inPipe->lag.latencyMs); }
    // change the lag on both directions (console: lag <ms>)
    void setLag(const LagSettings& l) { outPipe->lag = l; inPipe->lag = l; }
private:
    std::shared_ptr<LoopbackPipe> outPipe, inPipe;
    std::shared_ptr<bool> isOpen;
};

class LoopbackListener : public IListener {
public:
    explicit LoopbackListener(ClockFn c = realClock()) : clock(std::move(c)) {}
    // A client's end, already connected; the server gets the other
    // from accept()
    std::shared_ptr<LoopbackConnection> connect(const LagSettings& lag = LagSettings()) {
        auto toServer = std::make_shared<LoopbackPipe>(lag, clock, seed++);
        auto toClient = std::make_shared<LoopbackPipe>(lag, clock, seed++);
        auto open = std::make_shared<bool>(true);
        auto serverEnd = std::make_shared<LoopbackConnection>(toClient, toServer, open);
        auto clientEnd = std::make_shared<LoopbackConnection>(toServer, toClient, open);
        std::lock_guard<std::mutex> g(m);
        waiting.push_back(serverEnd);
        return clientEnd;
    }
    std::shared_ptr<IConnection> accept() override {
        std::lock_guard<std::mutex> g(m);
        if (waiting.empty()) return nullptr;
        auto c = waiting.front(); waiting.pop_front();
        return c;
    }
private:
    ClockFn clock;
    std::mutex m;
    std::deque<std::shared_ptr<IConnection>> waiting;
    uint32_t seed = 1;
};

} // namespace net
