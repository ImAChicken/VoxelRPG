#include "EnetTransport.h"
#include <enet/enet.h>
#include <deque>
#include <mutex>
#include <vector>

// ENet behind net::IListener / net::IConnection (EnetTransport.h).
// Channel 0 reliable, channel 1 unreliable (sequenced: an older
// packet arriving after a newer one is dropped by ENet).
namespace net {
namespace {

struct EnetLib {
    EnetLib() { enet_initialize(); }
    ~EnetLib() { enet_deinitialize(); }
};
void useEnet() { static EnetLib lib; }

void sendOn(ENetPeer* peer, const std::vector<uint8_t>& d, Channel ch) {
    ENetPacket* p = enet_packet_create(d.data(), d.size(), ch == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (enet_peer_send(peer, ch == Channel::Reliable ? 0 : 1, p) < 0) enet_packet_destroy(p);
}

// One client, as the server sees it
class ServerPeer : public IConnection {
public:
    explicit ServerPeer(ENetPeer* p) : peer(p) {}
    void send(std::vector<uint8_t> d, Channel ch) override { if (open) sendOn(peer, d, ch); }
    bool receive(Packet& out) override {
        if (inbox.empty()) return false;
        out = std::move(inbox.front()); inbox.pop_front();
        return true;
    }
    bool connected() const override { return open; }
    void close() override { if (open) { enet_peer_disconnect_later(peer, 0); open = false; } }
    float pingMs() const override { return open ? (float)peer->roundTripTime : 0.0f; }
    ENetPeer* peer;
    bool open = true;
    std::deque<Packet> inbox;
};

class Listener : public IListener {
public:
    explicit Listener(ENetHost* h) : host(h) {}
    ~Listener() override {
        for (size_t i = 0; i < host->peerCount; i++) if (host->peers[i].state == ENET_PEER_STATE_CONNECTED) enet_peer_disconnect_now(&host->peers[i], 0);
        enet_host_destroy(host);
    }
    void poll() override {
        ENetEvent e;
        while (enet_host_service(host, &e, 0) > 0) {
            switch (e.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                auto c = std::make_shared<ServerPeer>(e.peer);
                e.peer->data = c.get();
                peers.push_back(c);
                waiting.push_back(c);
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
                if (auto* c = (ServerPeer*)e.peer->data)
                    c->inbox.push_back({ std::vector<uint8_t>(e.packet->data, e.packet->data + e.packet->dataLength),
                                         e.channelID == 0 ? Channel::Reliable : Channel::Unreliable });
                enet_packet_destroy(e.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (auto* c = (ServerPeer*)e.peer->data) {
                    c->open = false;
                    e.peer->data = nullptr;
                    for (size_t i = 0; i < peers.size(); i++) if (peers[i].get() == c) { peers.erase(peers.begin() + i); break; }
                }
                break;
            default: break;
            }
        }
    }
    std::shared_ptr<IConnection> accept() override {
        if (waiting.empty()) return nullptr;
        auto c = waiting.front(); waiting.pop_front();
        return c;
    }
private:
    ENetHost* host;
    std::vector<std::shared_ptr<ServerPeer>> peers;      // keeps them alive while ENet points at them
    std::deque<std::shared_ptr<IConnection>> waiting;
};

// The client's end
class ClientConn : public IConnection {
public:
    ClientConn(ENetHost* h, ENetPeer* p) : host(h), peer(p) {}
    ~ClientConn() override {
        if (peer && up) { enet_peer_disconnect_now(peer, 0); }
        enet_host_destroy(host);
    }
    void send(std::vector<uint8_t> d, Channel ch) override {
        if (!open) return;
        if (!up) { if (ch == Channel::Reliable) early.push_back(std::move(d)); return; }   // waits for the connection
        sendOn(peer, d, ch);
    }
    bool receive(Packet& out) override {
        pump();
        if (inbox.empty()) return false;
        out = std::move(inbox.front()); inbox.pop_front();
        return true;
    }
    bool connected() const override { return open; }
    void close() override {
        if (!open) return;
        if (up) { enet_peer_disconnect(peer, 0); enet_host_flush(host); }
        open = false;
    }
    float pingMs() const override { return up ? (float)peer->roundTripTime : 0.0f; }
    void poll() override { pump(); }
private:
    void pump() {
        ENetEvent e;
        while (enet_host_service(host, &e, 0) > 0) {
            switch (e.type) {
            case ENET_EVENT_TYPE_CONNECT:
                up = true;
                for (auto& d : early) sendOn(peer, d, Channel::Reliable);
                early.clear();
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                inbox.push_back({ std::vector<uint8_t>(e.packet->data, e.packet->data + e.packet->dataLength),
                                  e.channelID == 0 ? Channel::Reliable : Channel::Unreliable });
                enet_packet_destroy(e.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT: up = false; open = false; break;
            default: break;
            }
        }
    }
    ENetHost* host;
    ENetPeer* peer;
    bool up = false, open = true;
    std::vector<std::vector<uint8_t>> early;
    std::deque<Packet> inbox;
};

} // namespace

std::shared_ptr<IListener> enetListen(int port, int maxClients) {
    useEnet();
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = (enet_uint16)port;
    ENetHost* h = enet_host_create(&a, (size_t)maxClients + 4, 2, 0, 0);    // a few spare, to refuse politely
    if (!h) return nullptr;
    return std::make_shared<Listener>(h);
}

std::shared_ptr<IConnection> enetConnect(const std::string& hostName, int port) {
    useEnet();
    ENetHost* h = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!h) return nullptr;
    ENetAddress a;
    if (enet_address_set_host(&a, hostName.c_str()) < 0) { enet_host_destroy(h); return nullptr; }
    a.port = (enet_uint16)port;
    ENetPeer* p = enet_host_connect(h, &a, 2, 0);
    if (!p) { enet_host_destroy(h); return nullptr; }
    enet_peer_timeout(p, 0, 3000, 6000);      // no answer: gives up after about 8 s (the default is half a minute)
    return std::make_shared<ClientConn>(h, p);
}

} // namespace net
