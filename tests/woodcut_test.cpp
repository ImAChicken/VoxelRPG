// ══════════════════════════════════════════════════════════
// woodcut_test — the axe, felling, logs and wood drops, headless
// ══════════════════════════════════════════════════════════
// Run from the repo root (it loads data/registry/*.toml).
//   axe item      a tool, not a weapon; forces third person
//   axe model     voxel mesh builds, edge points sit on the blade
//   swing         the edge leads through the strike, lands in
//                 front at chest-to-head height, speed sets damage
//   blade hit     a swing at a trunk in front hits it in the strike
//   felling       a tree falls over away from the axe and settles
//                 as a lying log that stays an entity; leaves break off;
//                 the log is solid (bodies, drops and the axe meet it)
//   heavy         a falling log knocks a body it lands on
//   log           chopping drops all the tree's wood, then it's gone
//   drops         wood settles on the ground and is picked up
//   save / load   logs and drops survive a save
#include "world/Woodcutting.h"
#include "character/CharacterAnimator.h"
#include "character/HeldItemModel.h"
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

// gRegistry and gItems come from src/voxel/VoxelRegistry.cpp and
// src/game/ItemRegistry.cpp, built in with this file

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

// A world in a map: ground below y = 0, plus whatever is set
struct MapWorld {
    std::map<std::tuple<int, int, int>, uint16_t> v;
    uint16_t at(glm::ivec3 p) const {
        auto it = v.find({ p.x, p.y, p.z });
        if (it != v.end()) return it->second;
        return p.y < 0 ? BLOCK_STONE : BLOCK_AIR;
    }
    bool solid(int x, int y, int z) const { return gRegistry.get(at({ x, y, z })).isSolid(); }
    SolidFn solidFn() { return [this](int x, int y, int z) { return solid(x, y, z); }; }
};

int main() {
    if (!gRegistry.load("data/registry/blocks.toml") || !gItems.load(gRegistry, "data/registry/items.toml")) {
        std::printf("run from the repo root\n");
        return 2;
    }
    std::printf("woodcut_test\n");
    const uint16_t LOG_Y = gRegistry.getIDByName("log_y"), LOG_X = gRegistry.getIDByName("log_x"),
                   LOG_Z = gRegistry.getIDByName("log_z"), LEAVES = gRegistry.getIDByName("leaves");
    const uint16_t AXE = gItems.find("iron_axe"), WOOD = gItems.find("log_y");

    // ── The item ───────────────────────────────────────────
    {
        std::printf("axe item\n");
        const ItemDef& d = gItems.get(AXE);
        check(d.is(IC_TOOL) && !d.is(IC_WEAPON), "iron_axe is a tool and not a weapon");
        check(d.tool == ToolKind::Axe && d.heldModel == "axe", "an axe, drawn with the axe model");
        Player p;
        p.setMode(GameMode::Survival);
        check(!p.rules().forceThirdPerson, "empty hands: camera is the player's choice");
        p.inventory.setSlot(0, AXE, 1);
        p.inventory.select(0);
        check(p.rules().forceThirdPerson, "axe in hand forces third person");
        p.inventory.select(1);
        check(!p.rules().forceThirdPerson, "put away: back to the player's choice");
    }

    // ── The model ──────────────────────────────────────────
    CharacterModel model = CharacterModel::build(Appearance{});
    {
        std::printf("axe model\n");
        auto shards = AxeGeometry::mesh(model, HairMesh::Shards);
        auto cubes  = AxeGeometry::mesh(model, HairMesh::Voxels);
        std::printf("    %zu triangles as shards, %zu as voxels\n", shards.size() / 3, cubes.size() / 3);
        check(shards.size() > 300 && cubes.size() > 300, "the voxel axe meshes both ways");
        // the edge points sit at the blade, about 2 ft from the fist
        auto bp = AxeGeometry::bladePoints(model);
        float reach = glm::length(bp[1] - AxeGeometry::grip(model));
        std::printf("    edge %.1f in from the fist\n", reach);
        check(reach > 15.0f && reach < 25.0f, "edge 15-25 in from the fist");
    }

    // ── The swing ──────────────────────────────────────────
    float peak = 0.0f, impactY = 0.0f, impactZ = 0.0f, lead = 0.0f;
    {
        std::printf("swing\n");
        CharacterAnimator an;
        AnimInput in; in.grounded = true; in.facingYawDeg = 90.0f;   // facing +Z
        auto bp = AxeGeometry::bladePoints(model);
        glm::vec3 prev(0);
        const float dt = 1.0f / 120.0f;
        for (int f = 0; f <= 120; f++) {
            in.swing = f * dt;
            const Pose& p = an.update(model, in, dt);
            BoneMats W;
            worldBoneMatrices(model, p, glm::vec3(0), W);
            glm::vec3 e(W[B_WRIST_R] * glm::vec4(bp[1], 1.0f));
            if (f && in.swing >= AxeSwing::STRIKE_START && in.swing <= AxeSwing::STRIKE_END) {
                float sp = glm::length(e - prev) / dt;
                if (sp > peak) {
                    peak = sp; impactY = e.y; impactZ = e.z;
                    glm::vec3 edge = glm::normalize(glm::mat3(W[B_WRIST_R]) * AxeGeometry::edgeDir());
                    lead = glm::dot(edge, glm::normalize(e - prev));
                }
            }
            prev = e;
        }
        std::printf("    fastest %.0f vox/s, %.1f vox up, %.1f vox ahead, edge leads by %.2f\n", peak, impactY, impactZ, lead);
        check(lead > 0.6f, "the cutting edge leads the swing");
        check(impactZ > 4.0f, "the blade is fastest in front of the body");
        check(peak > AxeSwing::FULL_SPEED, "a full swing reaches full-damage speed");
        float slow = AxeSwing::damageFor(6.0f, 60.0f), full = AxeSwing::damageFor(6.0f, AxeSwing::FULL_SPEED);
        std::printf("    damage: early (60 vox/s) %.1f, full speed %.1f\n", slow, full);
        check(slow < full && std::fabs(full - 6.0f) < 1e-3f, "speed sets the damage, up to tool power");
    }

    // ── The blade hits a trunk in front ────────────────────
    {
        std::printf("blade hit\n");
        MapWorld w;
        for (int y = 0; y < 40; y++) for (int x = -1; x <= 1; x++) for (int z = 7; z <= 9; z++) w.v[{x, y, z}] = LOG_Y;
        CharacterAnimator an;
        AnimInput in; in.grounded = true; in.facingYawDeg = 90.0f;
        AxeSwing sw; sw.start();
        auto bp = AxeGeometry::bladePoints(model);
        axeswing::BladeHit hit;
        float hitAt = -1;
        const float dt = 1.0f / 60.0f;
        while (sw.active()) {
            in.swing = sw.t;
            const Pose& p = an.update(model, in, dt);
            BoneMats W;
            worldBoneMatrices(model, p, glm::vec3(0.0f, 0.0f, 0.0f), W);
            std::array<glm::vec3, 3> now;
            for (int k = 0; k < 3; k++) now[k] = glm::vec3(W[B_WRIST_R] * glm::vec4(bp[k], 1.0f));
            if (sw.striking() && sw.traceValid) {
                auto h = axeswing::trace(sw.lastBlade, now, dt, [&](glm::ivec3 v) { return w.solid(v.x, v.y, v.z) && v.y >= 0; });
                if (h.hit) { hit = h; hitAt = sw.t; sw.hitDone = true; sw.stop = AxeSwing::HITSTOP; }
            }
            sw.lastBlade = now; sw.traceValid = true;
            sw.advance(dt);
        }
        std::printf("    hit voxel (%d %d %d) at t=%.2f, edge %.0f vox/s -> %.1f damage\n", hit.voxel.x, hit.voxel.y,
                    hit.voxel.z, hitAt, hit.speed, AxeSwing::damageFor(6.0f, hit.speed));
        check(hit.hit, "the swing hits the trunk");
        check(hitAt >= AxeSwing::STRIKE_START && hitAt <= AxeSwing::STRIKE_END, "during the strike");
        check(hit.voxel.y >= 6 && hit.voxel.y <= 17, "at chest to head height");
    }

    // ── Looking down, the chop reaches a log on the ground ─
    {
        std::printf("downward chop\n");
        auto swingAt = [&](float pitch) {
            MapWorld w;
            for (int x = -6; x <= 6; x++) for (int y = 0; y < 3; y++) for (int z = 3; z <= 6; z++) w.v[{x, y, z}] = LOG_X;
            CharacterAnimator an;
            AnimInput in; in.grounded = true; in.facingYawDeg = 90.0f; in.lookPitchDeg = pitch;
            AxeSwing sw; sw.start();
            auto bp = AxeGeometry::bladePoints(model);
            const float dt = 1.0f / 60.0f;
            while (sw.active()) {
                in.swing = sw.t;
                BoneMats W;
                worldBoneMatrices(model, an.update(model, in, dt), glm::vec3(0), W);
                std::array<glm::vec3, 3> now;
                for (int k = 0; k < 3; k++) now[k] = glm::vec3(W[B_WRIST_R] * glm::vec4(bp[k], 1.0f));
                if (sw.striking() && sw.traceValid &&
                    axeswing::trace(sw.lastBlade, now, dt, [&](glm::ivec3 v) { return v.y >= 0 && w.solid(v.x, v.y, v.z); }).hit)
                    return true;
                sw.lastBlade = now; sw.traceValid = true;
                sw.advance(dt);
            }
            return false;
        };
        bool level = swingAt(0.0f), down = swingAt(-35.0f);
        std::printf("    log lying 3-6 ahead: looking level %s, looking 35 deg down %s\n",
                    level ? "hits" : "misses", down ? "hits" : "misses");
        check(down, "looking down, the swing reaches a log on the ground");
    }

    // A simple tree: 3×3 trunk 30 tall (log_y) on a 2-voxel stump, a leaf crown
    auto makeTree = [&](MapWorld& w, std::vector<VoxelPiece>& wood, std::vector<VoxelPiece>& leaves, int ox) {
        for (int y = 0; y < 30; y++) for (int x = -1; x <= 1; x++) for (int z = -1; z <= 1; z++) {
            glm::ivec3 p(ox + x, y, z);
            if (y < Woodcutting::STUMP_HEIGHT) w.v[{p.x, p.y, p.z}] = LOG_Y;     // the stump stays
            else wood.push_back({ p, LOG_Y });
        }
        for (int y = 22; y < 34; y++) for (int x = -5; x <= 5; x++) for (int z = -5; z <= 5; z++)
            if (x * x + z * z + (y - 28) * (y - 28) < 30 && !(std::abs(x) <= 1 && std::abs(z) <= 1 && y < 30))
                leaves.push_back({ { ox + x, y, z }, LEAVES });
    };
    Woodcutting::TreeInfo info{ "Oak", WOOD, 24, 40.0f };

    // ── Felling ────────────────────────────────────────────
    {
        std::printf("felling\n");
        MapWorld w;
        std::vector<VoxelPiece> wood, leaves;
        makeTree(w, wood, leaves, 0);
        Woodcutting wc;
        wc.fellTree(wood, leaves, glm::vec3(1, 0, 0), glm::vec3(0, Woodcutting::STUMP_HEIGHT, 0), info);
        check(wc.allLogs().size() == 1, "the tree becomes one body");
        const RigidVoxelBody& body = wc.allLogs()[0]->body;
        std::printf("    %zu wood voxels, %.0f kg; %zu leaf particles\n", wood.size(), body.massKg(), wc.allParticles().size());
        check(body.massKg() > 100.0f, "heavy: over 100 kg");
        check(!wc.allParticles().empty(), "the leaves break off");
        int ticks = 0;
        while (!body.settled() && ticks < 60 * 20) { wc.tick(1.0f / 60.0f, w.solidFn(), nullptr); ticks++; }
        check(body.settled() && wc.logCount() == 1, "it settles and stays a log entity");
        glm::vec3 up = body.axisUp(), c = body.centre();
        int inGround = 0;
        glm::mat4 T = body.transform();
        for (const VoxelPiece& v : body.voxels())
            if (glm::vec3(T * glm::vec4(glm::vec3(v.p) + 0.5f, 1.0f)).y < -0.3f) inGround++;
        std::printf("    down after %.1f s: trunk along (%.2f %.2f %.2f), centre at (%.1f %.1f %.1f)\n", ticks / 60.0f,
                    up.x, up.y, up.z, c.x, c.y, c.z);
        check(std::fabs(up.y) < 0.4f, "it lies down");
        check(c.x > 8.0f, "it fell away from the axe (+X)");
        check(inGround == 0, "nothing sunk into the ground");
        check(w.at({ 0, 0, 0 }) == LOG_Y && w.at({ 0, 1, 0 }) == LOG_Y, "the stump still stands");
        check(ticks < 60 * 12, "down and still within 12 s");
        // solid: the log's voxels stop things, the air beside it doesn't
        glm::vec3 inside(T * glm::vec4(glm::vec3(wood[wood.size() / 2].p) + 0.5f, 1.0f));
        glm::ivec3 iv = glm::ivec3(glm::floor(inside));
        check(wc.solidAt(iv.x, iv.y, iv.z), "the log is solid where it lies");
        check(!wc.solidAt(iv.x, iv.y + 6, iv.z), "the air above it is not");
        check(wc.logAt(inside) != nullptr, "the axe finds the log by a point on it");
    }

    // ── It falls the way it is pushed, whichever way ───────
    {
        std::printf("fall direction\n");
        const glm::vec3 dirs[4] = { {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}, {0, 0, 1} };
        float worst = 1.0f;
        for (const glm::vec3& d : dirs) {
            MapWorld w;
            std::vector<VoxelPiece> wood, leaves;
            makeTree(w, wood, leaves, 0);
            Woodcutting wc;
            wc.fellTree(wood, leaves, d, glm::vec3(0.5f, 2, 0.5f), info);
            for (int i = 0; i < 60 * 15 && !wc.allLogs()[0]->body.settled(); i++) wc.tick(1.0f / 60.0f, w.solidFn(), nullptr);
            glm::vec3 mid = wc.allLogs()[0]->body.centre() - glm::vec3(0.5f, 0, 0.5f);
            mid.y = 0;
            float along = glm::dot(glm::normalize(mid), d);
            std::printf("    pushed (%2.0f %2.0f): log centre at (%5.1f %5.1f), %.2f along the push\n", d.x, d.z, mid.x, mid.z, along);
            worst = std::min(worst, along);
        }
        check(worst > 0.9f, "every tree falls the way the axe pushed it");
    }

    // ── A falling log is heavy ─────────────────────────────
    {
        std::printf("heavy\n");
        MapWorld w;
        std::vector<VoxelPiece> wood, leaves;
        makeTree(w, wood, leaves, 0);
        Woodcutting wc;
        Player pl;
        pl.setMode(GameMode::Survival);
        pl.position = glm::vec3(18.5f, 0.0f, 0.5f);          // where it will land
        wc.fellTree(wood, leaves, glm::vec3(1, 0, 0), glm::vec3(0, 2, 0), info);
        bool hit = false;
        float J = 0;
        for (int i = 0; i < 60 * 6 && !hit; i++) {
            wc.tick(1.0f / 60.0f, w.solidFn(), &pl);
            if (!pl.hits.empty()) { hit = true; J = glm::length(pl.hits[0].impulse); }
        }
        std::printf("    blow %.0f kg·vox/s = %.0f vox/s on a 70 kg body\n", J, J / 70.0f);
        check(hit, "a body under it is hit");
        check(J / 70.0f > Ragdoll::KNOCKDOWN_DV, "hard enough to knock it down");
    }

    // ── Chopping the log, wood on the ground, pickup ───────
    {
        std::printf("log\n");
        MapWorld w;
        std::vector<VoxelPiece> wood, leaves;
        makeTree(w, wood, leaves, 0);
        Woodcutting wc;
        wc.fellTree(wood, leaves, glm::vec3(1, 0, 0), glm::vec3(0, 2, 0), info);
        for (int i = 0; i < 60 * 15 && !wc.allLogs()[0]->body.settled(); i++) wc.tick(1.0f / 60.0f, w.solidFn(), nullptr);
        Woodcutting::Log* log = wc.allLogs()[0].get();
        glm::vec3 at = log->body.centre();
        check(wc.logAt(at) == log, "the log is found at its middle");
        if (log) {
            std::printf("    %s: %.0f health, %d wood\n", log->name.c_str(), log->health, log->totalWood);
            int hits = 0, dropped = 0;
            bool broken = false;
            while (!broken && hits < 50) {
                auto r = wc.hitLog(*log, 6.0f, at, glm::vec3(1, 0, 0), false);
                dropped += r.dropped; broken = r.broken; hits++;
            }
            std::printf("    %d hits, %d wood dropped\n", hits, dropped);
            check(broken, "the log breaks up");
            check(dropped == info.dropCount, "every piece of the tree's wood drops");
            check(hits == 5, "5 full hits for an oak log (30 health / 6)");
            check(wc.logCount() == 0 && !wc.solidAt((int)at.x, (int)at.y, (int)at.z), "the log is gone");
        }
        // the drops land
        for (int i = 0; i < 180; i++) wc.tick(1.0f / 60.0f, w.solidFn(), nullptr);
        int total = 0, resting = 0;
        float lowest = 1e9f;
        for (auto& d : wc.itemDrops()) { total += d.count; resting += d.resting; lowest = std::min(lowest, d.pos.y); }
        std::printf("    %zu drops holding %d wood, %d resting, lowest at y %.2f\n", wc.itemDrops().size(), total, resting, lowest);
        check(total == info.dropCount, "all the wood is lying on the ground");
        check(resting == (int)wc.itemDrops().size() && lowest > 0.0f, "drops rest on top of the ground");

        // save / load
        std::filesystem::create_directories("/tmp/woodcut_test");
        wc.save("/tmp/woodcut_test");
        Woodcutting back;
        back.load("/tmp/woodcut_test");
        int t2 = 0; for (auto& d : back.itemDrops()) t2 += d.count;
        check(t2 == total, "drops survive save and load");

        // walk over and pick up
        Player pl;
        pl.setMode(GameMode::Survival);
        const auto& d0 = wc.itemDrops().front();
        pl.position = d0.pos + glm::vec3(1.0f, -0.6f, 0.0f);
        int before = pl.inventory.count(WOOD);
        for (int i = 0; i < 240; i++) {
            // walk along the drops
            if (!wc.itemDrops().empty()) pl.position = wc.itemDrops().front().pos + glm::vec3(1.5f, -0.6f, 0);
            wc.tick(1.0f / 60.0f, w.solidFn(), &pl);
        }
        int got = pl.inventory.count(WOOD) - before;
        std::printf("    picked up %d (\"%s\")\n", got, wc.lastPickup.c_str());
        check(got == info.dropCount && wc.itemDrops().empty(), "walking over the wood picks it all up");
    }

    // ── A saved log comes back where it lay ────────────────
    {
        std::printf("save / load\n");
        MapWorld w;
        std::vector<VoxelPiece> wood, leaves;
        makeTree(w, wood, leaves, 0);
        Woodcutting wc;
        wc.fellTree(wood, leaves, glm::vec3(0, 0, 1), glm::vec3(0.5f, 2, 0.5f), info);
        for (int i = 0; i < 60 * 15 && !wc.allLogs()[0]->body.settled(); i++) wc.tick(1.0f / 60.0f, w.solidFn(), nullptr);
        wc.allLogs()[0]->health = 12.0f;
        glm::vec3 c = wc.allLogs()[0]->body.centre();
        wc.save("/tmp/woodcut_test");
        Woodcutting back;
        back.load("/tmp/woodcut_test");
        bool same = back.logCount() == 1 && glm::length(back.allLogs()[0]->body.centre() - c) < 0.01f &&
                    back.allLogs()[0]->body.occupies(c) == wc.allLogs()[0]->body.occupies(c) &&
                    back.allLogs()[0]->body.settled();
        check(same, "the log comes back in the same pose, at rest");
        check(back.logCount() == 1 && std::fabs(back.allLogs()[0]->health - 12.0f) < 1e-3f &&
              back.allLogs()[0]->name == "Fallen Oak", "with its health and name");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
