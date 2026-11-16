// Headless checks for chests and containers: structures.toml
// kind = "chest", world/Chests.h, game/Container.h (an inventory
// any entity can carry), loot tables (loot.toml, rolled only when
// first read), NPC packs, and moves between two inventories.
// Run from the repo root.
#include "TestWorld.h"
#include "world/Chests.h"
#include "world/StructureManager.h"
#include "world/WorldEditor.h"
#include "game/DevKit.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace testworld;

int main() {
    if (!loadData() || !gStructures.load("data/registry/structures.toml")) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }
    const uint16_t CHEST = gStructures.find("wooden_chest");
    const uint16_t STONE = gItems.find("stone"), PLANKS = gItems.find("planks"), SWORD = gItems.find("iron_sword");

    std::printf("the registry\n");
    const StructureType* t = gStructures.get(CHEST);
    check(t && t->kind == StructureKind::Chest && t->slots == 50 && t->health == 30.0f &&
          t->dropItem == PLANKS && t->inBuildMenu, "a wooden chest: 50 slots, 30 health, drops planks, in the build menu");
    if (!t) return 1;
    const VoxelVolume& v = t->shape(0, 0);
    uint16_t W = gRegistry.getIDByName("chest_wood"), I = gRegistry.getIDByName("chest_iron"), L = gRegistry.getIDByName("chest_lock");
    check(W && I && L, "its three blocks exist");
    check(v.size == glm::ivec3(9, 6, 6), "9 x 6 x 6 voxels (3 x 2 x 2 ft)");
    check(v.at(4, 3, 0) == L && v.at(3, 3, 0) == L && v.at(2, 3, 0) == I && v.at(4, 0, 2) == W, "lock in the middle of the iron seam, wood inside");
    for (const char* pack : { "jrpg", "default", "darktest" }) {
        std::ifstream f(std::string("data/resourcepacks/") + pack + "/atlas_config.toml");
        std::stringstream ss; ss << f.rdbuf();
        check(ss.str().find("\"chest_lock\"") != std::string::npos, std::string("textured in the ") + pack + " pack");
    }

    std::printf("storage\n");
    {
        ChestStore store;
        Inventory& c = store.get(7, *t).read();
        check(c.storage, "a chest is a store");
        c.add(STONE, 64);
        check(c.slots[0].itemID == STONE, "it fills from its first slot (no hotbar to keep clear)");
        int cap = std::max<int>(1, gItems.get(STONE).maxStack);
        int left = c.add(STONE, cap * 60);
        check(left == cap * 60 - (cap * 50 - 64), "50 slots hold 50 stacks");
        for (auto& s : c.slots) s.clear();

        Inventory p;
        p.add(SWORD, 1);
        int sw = p.find(SWORD);
        p.toggleEquip(sw);
        check(p.weapon().itemID == SWORD, "the player's sword is equipped");
        Inventory::moveAcross(p, sw, c, 5);
        check(c.slots[5].itemID == SWORD && p.weapon().empty(), "dragged into the chest: there, and no longer equipped");
        Inventory::moveAcross(c, 5, p, 20);
        check(p.slots[20].itemID == SWORD && c.slots[5].empty(), "and back");
        p.add(STONE, 30);
        int st = p.find(STONE);
        c.setSlot(0, STONE, 50);
        int moved = Inventory::sendAcross(p, st, c);
        check(moved == 30 && c.count(STONE) == 80 && p.count(STONE) == 0, "shift click sends a stack across, topping up stacks");
        int before = c.slots[0].count;
        Inventory::moveAcross(c, 0, p, 2, true);
        check(p.slots[2].count == before / 2 && c.slots[0].count == before - before / 2, "right drag takes half");

        c.setSlot(12, PLANKS, 7);
        store.save("/tmp");
        ChestStore back;
        back.load("/tmp");
        Container* bc = back.find(7);
        const Inventory* b = bc ? bc->peek() : nullptr;
        check(b && b->storage && b->slots[12].itemID == PLANKS && b->slots[12].count == 7 && b->count(STONE) == c.count(STONE),
              "contents save and load");
        std::remove("/tmp/chests.dat");
        auto out = store.take(7, t);
        check(store.find(7) == nullptr && !out.empty(), "taken out when it goes");
    }

    std::printf("in the world\n");
    {
        World w;
        WorldEditor ed(w.chunks);
        TerrainGenerator gen(1);
        StructureManager sm(w.chunks, ed, gen);
        ChestStore store;
        std::vector<std::pair<uint16_t, int>> spilled;
        sm.onRemoved = [&](const StructureInstance& si) {
            if (gStructures.get(si.type)->kind == StructureKind::Chest)
                for (auto& p : store.take(si.id, gStructures.get(si.type))) spilled.push_back(p);
        };
        glm::ivec3 at(200, GROUND, 200);
        check(sm.checkPlace(CHEST, at, 0, true).empty(), "it stands on the ground (no foundation needed)");
        StructureInstance* s = sm.place(CHEST, at, 0, EditSource::Command, 0);
        ed.flush();
        check(s && s->health == 30.0f, "placed, with its health");
        check(w.chunks.typeAtVoxel(at.x, at.y + 3, at.z - 3) == L, "its lock faces front");
        uint32_t id = s->id;
        store.get(id, *t).read().add(PLANKS, 40);
        store.get(id, *t).read().add(STONE, 10);
        sm.remove(id, EditSource::Command);
        ed.flush();
        int planks = 0, stone = 0;
        for (auto& [item, n] : spilled) { if (item == PLANKS) planks += n; if (item == STONE) stone += n; }
        check(planks == 40 && stone == 10, "removed: everything in it comes out to spill");
        check(w.chunks.typeAtVoxel(at.x, at.y + 1, at.z) == BLOCK_AIR, "and it's gone from the world");
    }

    std::printf("loot tables\n");
    {
        check(gLoot.get("bandit") && gLoot.get("bandit_stash") && gLoot.get("materials"), "loot.toml loads");
        auto a = gLoot.roll("bandit_stash", 42), b = gLoot.roll("bandit_stash", 42);
        check(!a.empty() && a == b, "a roll is seeded: same table and seed, same items");
        bool differ = false, gear = false;
        uint16_t HELM = gItems.find("iron_helmet"), GAUNT = gItems.find("iron_gauntlets"), GREAVE = gItems.find("iron_greaves"), MAIL = gItems.find("chainmail");
        int nothing = 0;
        for (uint32_t sd = 1; sd < 300; sd++) {
            auto r = gLoot.roll("bandit_stash", sd);
            differ = differ || r != a;
            for (auto& [item, n] : r) gear = gear || item == HELM || item == GAUNT || item == GREAVE || item == MAIL;
            if (gLoot.roll("villager", sd).empty()) nothing++;
        }
        check(differ && gear, "other seeds give other items, nested tables included (iron gear)");
        check(nothing > 10 && nothing < 290, "rolls = [0, 2] and 'nothing' entries sometimes give nothing");
        check(gLoot.roll("no_such_table", 1).empty(), "an unknown table gives nothing");

        Container box(30, "bandit_stash", 42);
        check(!box.opened() && box.peek() == nullptr && box.mayHoldItems(), "unread: no items made yet, only the table");
        std::ostringstream unread; box.write(unread, "c");
        check(unread.str().find(".i") == std::string::npos, "unread saves as just its table and seed");
        Inventory& in = box.read();
        check(box.opened() && in.capacity == 30 && !in.isEmpty(), "first read rolls it: 30 slots, items in");
        int first = 0; for (auto& [item, n] : a) first += n;
        int got = 0; for (auto& st : in.slots) got += st.count;
        check(got == first, "what it rolled is the seeded roll");
        in.add(PLANKS, 5);
        Container again; std::ostringstream out; box.write(out, "c");
        std::istringstream back(out.str()); std::string line;
        while (std::getline(back, line)) { size_t eq = line.find('='); again.readLine(line.substr(0, eq), line.substr(eq + 1), "c"); }
        again.finishLoad();
        check(again.opened() && again.peek()->count(PLANKS) == in.count(PLANKS) && again.loot == "bandit_stash",
              "once read it saves its items, and loads without rolling again");
        Container small(3, "", 0);
        Inventory& sm = small.read();
        int stack = std::max<int>(1, gItems.get(STONE).maxStack);
        check(sm.isEmpty() && sm.add(STONE, stack * 5) == stack * 2, "a 3-slot pack holds 3 stacks");
        Inventory p; p.add(SWORD, 1);
        Inventory::moveAcross(p, p.find(SWORD), sm, 10);
        check(p.find(SWORD) >= 0, "nothing goes past its capacity");
        Container stash(30, "bandit_stash", 9);
        check(!stash.takeAll().empty(), "broken unread: rolled, and everything comes out");
    }

    std::printf("chests placed by players and by commands\n");
    {
        const StructureType* st = gStructures.get(gStructures.find("bandit_stash"));
        check(st && st->kind == StructureKind::Chest && st->slots == 30 && st->loot == "bandit_stash" && !st->inBuildMenu,
              "a Bandit Stash: 30 slots, rolled from bandit_stash, not in the build menu");
        World w;
        WorldEditor ed(w.chunks);
        TerrainGenerator gen(1);
        StructureManager sm(w.chunks, ed, gen);
        ChestStore store;
        sm.onPlaced = [&](const StructureInstance& si, EditSource src) {
            const StructureType* ty = gStructures.get(si.type);
            if (ChestStore::holds(ty) && (src == EditSource::Player || src == EditSource::Dev)) store.makeEmpty(si.id, *ty);
        };
        StructureInstance* built = sm.place(gStructures.find("bandit_stash"), glm::ivec3(100, GROUND, 100), 0, EditSource::Player, 0);
        StructureInstance* typed = sm.place(gStructures.find("bandit_stash"), glm::ivec3(140, GROUND, 100), 0, EditSource::Command, 0);
        ed.flush();
        check(built && store.get(built->id, *st).read().isEmpty(), "built with the hammer: empty (building is no way to make loot)");
        check(typed && !store.get(typed->id, *st).read().isEmpty(), "placed by a command (or a world): rolls its loot when opened");
        StructureInstance* third = sm.place(gStructures.find("bandit_stash"), glm::ivec3(180, GROUND, 100), 0, EditSource::Command, 0);
        auto spill = store.take(third->id, st);
        check(!spill.empty(), "broken before anyone opened it: its loot spills");
    }

    std::printf("NPC packs\n");
    {
        const NpcType* bt = gNpcTypes.find("bandit");
        check(bt && bt->loot == "bandit" && bt->pack == 20, "a bandit carries a 20-slot pack from the bandit table");
        check(gNpcTypes.find("dummy") && gNpcTypes.find("dummy")->pack == 0, "a training dummy carries nothing");
        NpcManager npcs;
        Npc* a = npcs.spawn("bandit", glm::vec3(10, GROUND, 10), glm::vec3(0, 0, 1), 1234);
        Npc* b = npcs.spawn("bandit", glm::vec3(20, GROUND, 10), glm::vec3(0, 0, 1), 1234);
        check(a && a->pack.exists() && !a->pack.opened(), "spawned with its pack unread");
        bool sameRoll = a->pack.read().count(SWORD) == b->pack.read().count(SWORD);
        int ia = 0, ib = 0; for (auto& s2 : a->pack.peek()->slots) ia += s2.count; for (auto& s2 : b->pack.peek()->slots) ib += s2.count;
        check(sameRoll && ia == ib, "the same look seed rolls the same pack");
        a->pack.read().setSlot(0, PLANKS, 3);
        Npc* c = npcs.spawn("guard", glm::vec3(30, GROUND, 10), glm::vec3(0, 0, 1), 99);
        npcs.save("/tmp/npcs_test.dat");
        NpcManager back;
        back.load("/tmp/npcs_test.dat");
        std::remove("/tmp/npcs_test.dat");
        Npc* a2 = back.all().size() == 3 ? back.all()[0].get() : nullptr;
        Npc* c2 = back.all().size() == 3 ? back.all()[2].get() : nullptr;
        check(a2 && a2->pack.opened() && a2->pack.peek()->slots[0].itemID == PLANKS && a2->pack.peek()->slots[0].count == 3,
              "a searched pack saves and loads with the NPC");
        check(c2 && c && !c2->pack.opened() && c2->pack.loot == "guard", "an unsearched one loads still unread");
    }

    std::printf("dev world showcases\n");
    {
        Container show(50, "", 0);
        show.label = "Weapons";
        show.setShowcase(true);
        Inventory& sh = show.read();
        check(sh.showcase, "a showcase chest's items know it");
        int n = devkit::fill(sh, IC_WEAPON);
        check(n == (int)gItems.inCategory(IC_WEAPON).size() && sh.count(SWORD) == 1, "fill weapons: one of each");
        check(devkit::fill(sh, IC_WEAPON) == 0, "filling again adds nothing new");
        Inventory p;
        int ss = sh.find(SWORD);
        Inventory::moveAcross(sh, ss, p, 20);
        check(p.slots[20].itemID == SWORD && sh.count(SWORD) == 1, "dragged out: you get a copy, the showcase keeps its own");
        Inventory::sendAcross(sh, ss, p);
        check(p.count(SWORD) == 2 && sh.count(SWORD) == 1, "shift click: another copy");
        p.add(PLANKS, 5);
        Inventory::sendAcross(p, p.find(PLANKS), sh);
        check(sh.count(PLANKS) == 5 && p.count(PLANKS) == 0, "putting things in works as usual");
        check(show.takeAll().empty() && sh.count(SWORD) == 1, "broken: a showcase spills nothing");
        std::ostringstream out; show.write(out, "c");
        Container back; std::istringstream in(out.str()); std::string line;
        while (std::getline(in, line)) { size_t eq = line.find('='); back.readLine(line.substr(0, eq), line.substr(eq + 1), "c"); }
        back.finishLoad();
        check(back.label == "Weapons" && back.showcase && back.peek() && back.peek()->showcase && back.peek()->count(SWORD) == 1,
              "label and showcase save and load");
        check(devkit::categoryFromName("Armour") == IC_WORN && devkit::categoryFromName("blocks") == IC_BLOCK &&
              devkit::categoryFromName("nonsense") == 0, "kind names: weapons armour tools blocks decor dev all");

        World w;
        WorldEditor ed(w.chunks);
        TerrainGenerator gen(1);
        StructureManager sm(w.chunks, ed, gen);
        ChestStore store;
        glm::vec3 feet(300.0f, (float)GROUND, 300.0f), front(0, 0, -1);
        int placed = devkit::placeKit(sm, store, feet, front, nullptr);
        ed.flush();
        int shelvesWithItems = 0;
        for (auto& sh : devkit::shelves()) shelvesWithItems += !gItems.inCategory(sh.cats).empty();
        check(placed == shelvesWithItems && placed >= 4, "the kit: a chest for each shelf with items (no decor loaded here)");
        int labelled = 0, filled = 0, lockToward = 0;
        uint16_t L = gRegistry.getIDByName("chest_lock");
        sm.forEachInstance([&](const StructureInstance& si) {
            Container* c = store.find(si.id);
            if (!c) return;
            labelled += !c->label.empty() && c->showcase;
            filled += c->peek() && !c->peek()->isEmpty();
            // its lock is on the side toward you (+z of the chest here)
            const VoxelVolume& v = gStructures.get(si.type)->shape(0, si.rot);
            float lz = 0; int cnt = 0;
            for (int y = 0; y < v.size.y; y++) for (int z = 0; z < v.size.z; z++) for (int x = 0; x < v.size.x; x++)
                if (v.at(x, y, z) == L) { lz += z; cnt++; }
            if (cnt && lz / cnt > (v.size.z - 1) * 0.5f) lockToward++;
        });
        check(labelled == placed && filled == placed, "each is a labelled, filled showcase");
        check(lockToward == placed, "each lock faces you");
        int ahead = 0;
        sm.forEachInstance([&](const StructureInstance& si) { ahead += si.origin.z == (int)feet.z - 24; });
        check(ahead == placed, "all in a row 8 ft in front of you");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
