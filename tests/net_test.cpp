// Headless checks for playing over a connection (net/, server/
// ServerHost.h, client/ClientWorld.h): a server and two clients
// in one process over the loopback transport, with lag and packet
// loss on a simulated clock. Joining, the ground (generated from
// the seed, modified columns sent), your own movement predicted
// and kept in step with the server, other players seen smoothly,
// edits and NPCs arriving, being refused, leaving and coming back.
// Makes a flat world in saves/_net_test and deletes it. Run from
// the repo root.
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include "server/ServerHost.h"
#include "client/ClientWorld.h"
#include "net/EnetTransport.h"
#include <thread>
#include <cstdio>
#include <filesystem>

using namespace testworld;
namespace fs = std::filesystem;

static double gNow = 0.0;

struct TestClient {
    std::unique_ptr<ClientWorld> w;
    std::unique_ptr<ChunkKeeper> keeper;
    HumanIntent intent;
    glm::vec3 facing{0, 0, -1};
    void frame() {
        w->receive();
        if (w->needsGenerator()) { w->buildGenerator(); keeper = std::make_unique<ChunkKeeper>(*w->world, w->chunkSource(), 1); }
        if (keeper) { keeper->update({ w->me.position }); }
        if (keeper) w->step(intent, facing, 0.0f, DT);
        intent.clearOneShots();
    }
    void stop() { if (keeper) keeper->stop(); }
};

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    const char* NAME = "_net_test";
    const std::string W = std::string("saves/") + NAME;
    std::error_code ec;
    fs::remove_all(W, ec);
    fs::remove_all("/tmp/vrpg_cache", ec);
    const uint16_t STONE = gRegistry.getIDByName("stone"), PLANKS = gRegistry.getIDByName("planks");

    Simulation sim;
    GameSession session(sim);
    FlatOptions flat; flat.plants = false;
    session.beginNew(NAME, 3, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
    session.buildGenerator();
    session.attach();
    ChunkKeeper keeper(*session.world, session.chunkSource());
    glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z));
    keeper.loadNow({ spawn });
    // a column changed before anyone joins: clients must get it whole
    glm::ivec3 pillar((int)spawn.x + 12, (int)spawn.y + 4, (int)spawn.z + 12);
    { EditBatch b; b.source = EditSource::Command; b.set(pillar.x, pillar.y, pillar.z, STONE); session.editor->submit(std::move(b)); session.editor->flush(); }
    session.save(true);

    net::LoopbackListener listener([] { return gNow; });
    ServerHost host(session, listener);
    host.log = [](const std::string&) {};
    host.starterKit = { "iron_sword", "planks" };

    std::vector<TestClient*> clients;
    auto tickAll = [&](int n) {
        for (int i = 0; i < n; i++) {
            gNow += DT;
            host.poll();
            host.beforeStep();
            std::vector<glm::vec3> pts{ spawn };
            for (auto& p : sim.players()) pts.push_back(p->position);
            keeper.update(pts);
            session.syncSpawners();
            session.updateEdits();
            sim.step(DT);
            host.afterStep();
            for (TestClient* c : clients) c->frame();
        }
    };
    net::LagSettings lag; lag.latencyMs = 50.0f; lag.jitterMs = 10.0f;
    TestClient A, B;
    A.w = std::make_unique<ClientWorld>(listener.connect(lag), "/tmp/vrpg_cache");
    net::LagSettings lossy = lag; lossy.lossPct = 25.0f;
    B.w = std::make_unique<ClientWorld>(listener.connect(lossy), "/tmp/vrpg_cache");
    clients = { &A, &B };
    Appearance look; look.hairStyle = 3;
    A.w->join("alice", look);
    B.w->join("bob", Appearance());
    tickAll(90);

    std::printf("joining\n");
    check(A.w->state == ClientWorld::State::Joined && B.w->state == ClientWorld::State::Joined, "both join (50 ms lag; bob loses 25% of unreliable packets)");
    check(host.players() == 2 && A.w->myId && A.w->myId != B.w->myId, "two players on the server, each with its own id");
    check(A.w->gen && A.w->info.getWorldType() == WorldType::Flat && A.w->info.getSeed() == 3, "the client builds the same world from the settings it was sent");
    check(glm::length(A.w->me.position - spawn) < 6.0f, "a new player starts at the spawn");

    std::printf("the ground\n");
    int pcx = ChunkManager::chunkCoord(pillar.x), pcz = ChunkManager::chunkCoord(pillar.z);
    check(A.w->isModified(pcx, pcz) && A.w->chunksReceived() >= 1, "the column changed before joining is listed as modified, and sent whole");
    check(A.w->world->typeAtVoxel(pillar.x, pillar.y, pillar.z) == STONE, "with its change in it");
    int ocx = ChunkManager::chunkCoord((int)spawn.x) + 1, ocz = ChunkManager::chunkCoord((int)spawn.z);
    A.keeper->loadNow({ A.w->me.position });     // the ground around her, made on the client's own worker
    check(!A.w->isModified(ocx, ocz) && A.w->world->isLoaded(ocx, ocz) &&
          A.w->world->typeAtVoxel((int)spawn.x + VOXEL_DIM, (int)spawn.y - 1, (int)spawn.z) == session.world->typeAtVoxel((int)spawn.x + VOXEL_DIM, (int)spawn.y - 1, (int)spawn.z),
          "an untouched column is generated by the client, the same as the server's");

    std::printf("moving\n");
    Player* sa = sim.findPlayer(A.w->myId);
    glm::vec3 start = sa->position;
    A.intent.move = glm::vec2(0.0f, -1.0f);
    tickAll(120);
    A.intent.move = glm::vec2(0.0f);
    tickAll(30);
    float moved = glm::length(sa->position - start);
    check(moved > 30.0f, "alice walks on the server (" + std::to_string((int)moved) + " voxels)");
    float off = glm::length(A.w->me.position - sa->position);
    check(off < 0.5f, "her predicted position matches the server's (" + std::to_string(off) + " voxels off)");
    check(A.w->corrections < 10, "with few corrections (" + std::to_string(A.w->corrections) + ")");
    proto::BodyState seen;
    check(B.w->sample(A.w->myId, seen) && glm::length(seen.pos - sa->position) < 3.0f, "bob sees her where she is, despite the loss");
    check(B.w->bodies.count(A.w->myId) && B.w->bodies[A.w->myId].look.hairStyle == 3 && B.w->bodies[A.w->myId].info.name == "alice",
          "with her name and her look");

    std::printf("while she walks, bob sees her move smoothly\n");
    A.intent.move = glm::vec2(1.0f, 0.0f);
    float lastX = -1e9f, worstJump = 0.0f; int samples = 0;
    for (int i = 0; i < 60; i++) {
        tickAll(1);
        if (B.w->sample(A.w->myId, seen)) {
            if (lastX > -1e8f) worstJump = std::max(worstJump, std::fabs(seen.pos.x - lastX));
            lastX = seen.pos.x; samples++;
        }
    }
    A.intent.move = glm::vec2(0.0f);
    tickAll(20);
    check(samples == 60 && worstJump < 2.0f, "no jumps between frames (largest step " + std::to_string(worstJump) + " voxels; walking is 0.5 a tick)");

    std::printf("edits\n");
    glm::ivec3 near((int)sa->position.x + 3, (int)spawn.y + 2, (int)sa->position.z);
    { EditBatch b; b.source = EditSource::Player; b.set(near.x, near.y, near.z, PLANKS); session.editor->submit(std::move(b)); }
    tickAll(20);
    check(A.w->world->typeAtVoxel(near.x, near.y, near.z) == PLANKS && B.w->world->typeAtVoxel(near.x, near.y, near.z) == PLANKS,
          "a block placed on the server shows up for both");

    std::printf("NPCs\n");
    Npc* bandit = sim.npcs().spawn("bandit", sa->position + glm::vec3(30, 0, 30), glm::vec3(0, 0, 1));
    tickAll(20);
    check(A.w->bodies.count(bandit->uuid) && A.w->bodies[bandit->uuid].info.type == "bandit", "an NPC nearby is sent to the clients");
    sim.npcs().all().erase(std::remove_if(sim.npcs().all().begin(), sim.npcs().all().end(),
                                          [&](auto& n) { return n->uuid == bandit->uuid; }), sim.npcs().all().end());
    tickAll(10);
    check(!A.w->bodies.count(bandit->uuid), "and taken away when it's gone");

    std::printf("inventory\n");
    {
        const uint16_t SWORD = gItems.find("iron_sword"), PL = gItems.find("planks");
        Inventory& mine = A.w->me.inventory;
        check(mine.count(SWORD) == 1 && mine.weapon().itemID == SWORD && sa->inventory.count(PL) == 1,
              "a new player gets the server's starter kit, the weapon equipped");
        int from = mine.find(PL);
        mine.move(from, 30);
        A.w->me.combatStance = true;
        tickAll(20);
        check(sa->inventory.slots[30].itemID == PL && sa->combatStance && sa->weaponDamage > 0.0f,
              "rearranging and drawing the weapon reach the server (it can fight)");
        mine.setSlot(40, SWORD, 1);                      // made from nothing
        tickAll(20);
        check(sa->inventory.count(SWORD) == 1 && mine.count(SWORD) == 1, "an arrangement with an item the server never gave is refused and put right");
        sa->inventory.add(PL, 5);                        // a pickup on the server
        tickAll(20);
        check(mine.count(PL) == 6, "what the server adds shows up in the client's inventory");
        check(B.w->bodies[A.w->myId].info.gear.empty(), "bob is told what alice wears (nothing yet)");
    }

    std::printf("fighting\n");
    {
        // bob stands in front of alice; she swings
        Player* sb = sim.findPlayer(B.w->myId);
        sb->position = sa->position + glm::vec3(0, 0, -6);
        sb->velocity = glm::vec3(0);
        A.facing = glm::vec3(0, 0, -1);
        tickAll(30);
        A.w->events.clear(); B.w->events.clear();
        float before = sb->health;
        A.intent.attackLight = true;
        tickAll(60);
        check(sb->health < before, "alice's swing hits bob on the server (" + std::to_string((int)(before - sb->health)) + " damage)");
        check(std::fabs(B.w->me.health - sb->health) < 0.01f, "bob's client knows his health");
        bool ha = false, hb = false;
        for (auto& e : A.w->events) ha = ha || (e.kind == (uint8_t)SimEvent::Kind::Hit && e.a == A.w->myId && e.b == B.w->myId);
        for (auto& e : B.w->events) hb = hb || (e.kind == (uint8_t)SimEvent::Kind::Hit && e.b == B.w->myId);
        check(ha && hb, "and both are told about the hit");
        proto::BodyState st;
        check(B.w->sample(A.w->myId, st) && st.held == "sword" && (st.flags & proto::BodyState::F_COMBAT), "bob sees her with the sword drawn");
        A.w->me.combatStance = false;
        tickAll(10);
        sa = sim.findPlayer(A.w->myId);
    }

    std::printf("refused\n");
    {
        TestClient C;
        C.w = std::make_unique<ClientWorld>(listener.connect(), "/tmp/vrpg_cache");
        clients.push_back(&C);
        C.w->join("alice", Appearance());
        tickAll(5);
        check(C.w->state == ClientWorld::State::Refused && C.w->refusedWhy.find("already playing") != std::string::npos, "a name already playing is refused");
        clients.pop_back();
        auto raw = listener.connect();
        proto::Hello h; h.version = 99; h.name = "old";
        raw->send(proto::hello(h), net::Channel::Reliable);
        tickAll(2);
        net::Packet pk; bool refusedOld = false;
        while (raw->receive(pk)) { wire::Reader r(pk.data); refusedOld = refusedOld || (proto::Msg)r.u8() == proto::Msg::Refused; }
        check(refusedOld, "and so is a client of another version");
        auto junk = listener.connect();
        junk->send({ 1, 2, 3 }, net::Channel::Reliable);
        junk->send({ 2, 0xff, 0xff, 0xff }, net::Channel::Unreliable);
        tickAll(2);
        check(host.players() == 2, "garbled packets change nothing");
    }

    std::printf("leaving and coming back\n");
    glm::vec3 where = sa->position;
    A.w->leave();
    tickAll(10);
    check(host.players() == 1 && !B.w->bodies.count(A.w->myId), "alice leaves: bob stops seeing her");
    session.save(true, [&](const std::string& st) { host.savePlayers(st); });
    check(fs::exists(fs::path(W) / "players" / "alice.dat"), "her player file is saved by name");
    A.stop();
    A.w = std::make_unique<ClientWorld>(listener.connect(lag), "/tmp/vrpg_cache");
    A.keeper.reset();
    A.w->join("alice", Appearance());
    tickAll(40);
    Player* sa2 = sim.findPlayer(A.w->myId);
    check(sa2 && glm::length(sa2->position - where) < 1.0f && glm::length(A.w->me.position - where) < 1.0f, "she comes back where she left");

    std::printf("over the network (ENet, this machine)\n");
    {
        auto lst = net::enetListen(27993, 4);
        check((bool)lst, "a listener opens on a UDP port");
        auto cl = net::enetConnect("127.0.0.1", 27993);
        check((bool)cl, "a client starts connecting");
        if (lst && cl) {
            cl->send({ 7, 8, 9 }, net::Channel::Reliable);          // before the connection is up: waits
            std::shared_ptr<net::IConnection> sv;
            net::Packet pk; bool got = false, back = false;
            for (int i = 0; i < 300 && !back; i++) {
                lst->poll();
                if (!sv) sv = lst->accept();
                if (sv && !got && sv->receive(pk)) {
                    got = pk.data == std::vector<uint8_t>{ 7, 8, 9 } && pk.channel == net::Channel::Reliable;
                    sv->send({ 1 }, net::Channel::Unreliable);
                }
                if (cl->receive(pk)) back = pk.data == std::vector<uint8_t>{ 1 } && pk.channel == net::Channel::Unreliable;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(got, "what the client sent while connecting arrives, reliable");
            check(back, "and the server's reply comes back, unreliable");
            cl->close();
            for (int i = 0; i < 100 && sv && sv->connected(); i++) { lst->poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
            check(sv && !sv->connected(), "closing is seen at the other end");
        }
        auto none = net::enetConnect("127.0.0.1", 27994);          // nobody there
        bool refusedFast = false;
        net::Packet pk;
        for (int i = 0; i < 2400 && none && none->connected(); i++) { none->receive(pk); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        refusedFast = none && !none->connected();
        check(refusedFast, "connecting where no server listens gives up");
    }

    A.stop(); B.stop(); keeper.stop();
    host.kickAll("server stopping");
    session.close();
    fs::remove_all(W, ec);
    fs::remove_all("/tmp/vrpg_cache", ec);
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
