// A player with no screen, for trying a server: joins, walks a
// square near where it arrives, and swings now and then.
//   net_bot [address[:port]] [name] [seconds]     (from the repo root)
// e.g. three of them:  net_bot 127.0.0.1 bot1 600 &  net_bot 127.0.0.1 bot2 600 & ...
#include "TestWorld.h"
#include "client/ClientWorld.h"
#include "net/EnetTransport.h"
#include "server/ChunkKeeper.h"
#include <chrono>
#include <cstdio>
#include <thread>

using namespace testworld;

int main(int argc, char** argv) {
    std::string address = argc > 1 ? argv[1] : "127.0.0.1", name = argc > 2 ? argv[2] : "bot";
    float seconds = argc > 3 ? (float)std::atof(argv[3]) : 60.0f;
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gHeldModels.loadDir("data/models/held");
    decor::current() = decor::Loader::load(gRegistry, "data/registry/decorations.toml");
    gItems.addDecor(*decor::current());
    gStructures.load("data/registry/structures.toml");
    gStructures.addDecorTrees(*decor::current());
    std::string host; int port;
    net::splitAddress(address, host, port);
    auto conn = net::enetConnect(host, port);
    if (!conn) { std::printf("can't look up %s\n", host.c_str()); return 1; }
    ClientWorld w(conn, "servercache/bots/" + name);
    Appearance look = Appearance::random((uint32_t)std::hash<std::string>()(name));
    w.join(name, look);
    std::unique_ptr<ChunkKeeper> keeper;
    auto next = std::chrono::steady_clock::now();
    int tick = 0;
    bool said = false;
    while (tick < (int)(seconds * 60.0f)) {
        w.receive();
        if (w.state == ClientWorld::State::Refused) { std::printf("refused: %s\n", w.refusedWhy.c_str()); return 1; }
        if (w.state == ClientWorld::State::Closed) { std::printf("connection closed\n"); return 1; }
        if (w.needsGenerator()) { w.buildGenerator(); keeper = std::make_unique<ChunkKeeper>(*w.world, w.chunkSource(), 1); }
        if (keeper) {
            if (!said) { std::printf("%s joined at %.0f %.0f %.0f\n", name.c_str(), w.me.position.x, w.me.position.y, w.me.position.z); std::fflush(stdout); said = true; }
            keeper->update({ w.me.position });
            w.me.combatStance = true;                       // weapon drawn
            HumanIntent in;
            int leg = (tick / 150) % 4;                     // 2.5 s a side
            glm::vec3 dirs[4] = { {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1} };
            in.move = glm::vec2(dirs[leg].x, dirs[leg].z);
            if (tick % 150 > 110) { in.move = glm::vec2(0.0f); in.attackLight = tick % 150 == 120; }
            w.step(in, dirs[leg], 0.0f, DT);
            tick++;
        }
        next += std::chrono::microseconds(16667);
        std::this_thread::sleep_until(next);
    }
    w.leave();
    for (int i = 0; i < 20; i++) { w.receive(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (keeper) keeper->stop();
    std::printf("%s left (%d corrections)\n", name.c_str(), w.corrections);
    return 0;
}
