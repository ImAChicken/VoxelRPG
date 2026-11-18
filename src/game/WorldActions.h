#pragma once
#include "ItemRegistry.h"
#include "Blueprint.h"
#include "AxeSwing.h"
#include "CubeTool.h"
#include "../entity/Player.h"
#include "../physics/IPhysics.h"
#include "../world/WorldEditor.h"
#include "../world/StructureManager.h"
#include "../world/Woodcutting.h"
#include "../world/Chests.h"
#include "../world/TerrainOps.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// WorldActions — what a player's hands do to the world
// ══════════════════════════════════════════════════════════
// The changes themselves, with no keys or mouse: break, dig a
// cube, place a block or plant, open a door, build a piece or a
// blueprint, shape the ground with the hoe, blast, hit with the
// axe. PlayerInteraction (the keys) asks for one as a
// WorldRequest:
//
//   own world   done at once:  act()
//   a server    sent there (net/Protocol.h Request); the server
//               checks it (server/ServerHost.h: the item is
//               carried, the spot in reach, not too many a second)
//               and does the very same act() for that player. The
//               voxels, structures and inventory come back the
//               usual way (Edits, Structures, PlayerState).
//
// Nothing here needs a window, so the dedicated server has it.
// Survival or Test Play is the player's own mode (on a server,
// the server's idea of it), never the request's.
struct WorldRequest {
    enum class Op : uint8_t {
        Break = 1,      // what's aimed at: a voxel, a plant, a piece, a chop
        BreakCube,      // the pickaxe's cube (lo .. hi)
        Place,          // the item used: a block, or a plant / decoration
        Door,           // open / close the door aimed at
        Piece,          // a build piece: name, at lo, turned rot
        Blueprint,      // a whole building: name is its text, at lo, turned rot, style
        Hoe,            // mode (HoeMode) round pos
        Blast,          // an explosion at pos; mode 1: the big one
        AxeHit,         // the blade met voxel pos at point, going dir, speed
        COUNT
    };
    Op          op = Op::Break;
    uint16_t    item = 0;              // the item used (0: hands)
    glm::ivec3  pos{ 0 }, normal{ 0 };  // the voxel aimed at, the face
    glm::ivec3  lo{ 0 }, hi{ 0 };
    int         rot = 0, mode = 0;
    std::string name;
    BlueprintStyle style;
    glm::vec3   point{ 0.0f }, dir{ 0.0f };
    float       speed = 0.0f;

    static const char* opName(Op o) {
        static const char* N[] = { "?", "break", "cube", "place", "door", "piece", "blueprint", "hoe", "blast", "axe" };
        return (int)o < (int)Op::COUNT ? N[(int)o] : "?";
    }
};

class WorldActions {
public:
    // How far the hands reach, in voxels (25 ≈ 8 ft).
    // Test Play reaches much further for building.
    static constexpr float SURVIVAL_REACH = 25.0f;
    static constexpr float DEV_REACH      = 160.0f;
    // Hoe: raise up to this above your feet, lower to this below
    // (a mound or a ditch, not a tower or a well)
    static constexpr int RAISE_MAX = 6, LOWER_MAX = 6;
    enum class HoeMode : int { Level, Raise, Lower, Path, COUNT };
    // Seconds between hoe strokes and blasts (a server allows no faster)
    static constexpr float HOE_EVERY = 0.25f, BLAST_COOLDOWN = 0.35f;

    RaycastResult target;            // block under the crosshair (act: the request's)
    std::string notice;              // short message ("inventory full")
    float noticeAge = 99.0f;
    // Last tree hit, for the health bar
    std::string treeName;
    float treeHealth = 0.0f, treeMax = 0.0f, treeAge = 99.0f;

    // Felled trees, logs and drops (set by the main loop when a
    // world is open; a server leaves it unset)
    Woodcutting* wood = nullptr;
    ChestStore*  chests = nullptr;     // a chest's label in the aim text; blueprints keep chest contents
    // Each say() also goes here (a server sends it to the player)
    std::function<void(const std::string&)> onSay;

    void say(const std::string& s) { notice = s; noticeAge = 0.0f; if (onSay) onSay(s); }

    // ── Do it ─────────────────────────────────────────────
    // The one way every change happens, here or on a server.
    // 'target' is the request's for the length of it.
    void act(const WorldRequest& r, Player& player, WorldEditor& editor, StructureManager& structs) {
        RaycastResult keep = target;
        target = RaycastResult();
        target.hit      = true;
        target.blockPos = r.pos;
        target.normal   = glm::vec3(r.normal);
        target.position = glm::vec3(r.pos) + 0.5f;
        target.blockID  = editor.typeAt(r.pos);
        const bool survival = player.mode == GameMode::Survival;
        const EditSource src = survival ? EditSource::Player : EditSource::Dev;
        const ItemDef& held = gItems.get(r.item);
        using Op = WorldRequest::Op;
        switch (r.op) {
        case Op::Break:
            if (target.blockID != BLOCK_AIR) breakTarget(player, editor, structs, held, survival);
            break;
        case Op::BreakCube: {
            CubeBox c;
            c.lo = glm::min(r.lo, r.hi);
            c.hi = glm::max(r.lo, r.hi);
            if (glm::any(glm::greaterThan(c.hi - c.lo, glm::ivec3(std::max(1, held.cubeSize) - 1)))) break;   // no bigger than the pick's
            breakCube(player, editor, structs, c, survival);
            break;
        }
        case Op::Place:
            if (held.is(IC_DECOR)) placeDecor(player, editor, held, survival);
            else if (held.is(IC_BLOCK)) place(player, editor, held.blockID, src);
            break;
        case Op::Door:      toggleDoor(player, structs, src); break;
        case Op::Piece:     placePiece(player, structs, r, survival, src); break;
        case Op::Blueprint: pasteBlueprint(player, editor, structs, r, survival, src); break;
        case Op::Hoe:       if (held.tool == ToolKind::Hoe) hoe(player, editor, structs, held, (HoeMode)r.mode); break;
        case Op::Blast:
            if (held.tool == ToolKind::Blast)
                blast(editor, glm::vec3(r.pos) + 0.5f, std::max(1.0f, held.toolPower) * (r.mode ? 2.0f : 1.0f), &player);
            break;
        case Op::AxeHit: {
            if (held.tool != ToolKind::Axe) break;
            axeswing::BladeHit h;
            h.hit = true; h.voxel = r.pos; h.point = r.point; h.dir = r.dir; h.speed = r.speed;
            hitWithAxe(player, structs, editor, h, held.toolPower);
            break;
        }
        default: break;
        }
        target = keep;
    }

    // ── Doors ─────────────────────────────────────────────
    void toggleDoor(Player& player, StructureManager& structs, EditSource src) {
        StructureInstance* s = structs.instanceAt(target.blockPos);
        const StructureType* t = s ? structs.typeOf(*s) : nullptr;
        if (!t || t->kind != StructureKind::Door) return;
        std::string why = structs.toggleDoor(*s, src, [&](glm::ivec3 p) { return overlapsBody(player, p); });
        if (!why.empty()) say(why);
    }

    // ── Builder's Hammer: one piece ───────────────────────
    // r.name the structure type, r.lo where (its anchor), r.rot
    void placePiece(Player& player, StructureManager& structs, const WorldRequest& r, bool survival, EditSource src) {
        uint16_t type = gStructures.find(r.name);
        const StructureType* t = gStructures.get(type);
        if (!t || !t->inBuildMenu) { say("no such piece"); return; }
        uint32_t support = 0;
        std::string why = structs.checkPlace(type, r.lo, r.rot & 3, survival, &support);
        if (!why.empty()) { say(why); return; }
        if (survival && gStructures.charges(*t)) {
            for (auto& [item, cnt] : t->cost)
                if (player.inventory.count(item) < cnt) { say("needs " + std::to_string(cnt) + " " + gItems.get(item).displayName); return; }
            for (auto& [item, cnt] : t->cost) player.inventory.take(item, cnt);
        }
        structs.place(type, r.lo, r.rot & 3, src, support);
    }

    // ── Builder's Hammer: a blueprint ─────────────────────
    // r.name the blueprint's text (a player's own copies live only
    // on their computer), r.lo its low corner, r.rot, r.style
    void pasteBlueprint(Player& player, WorldEditor& editor, StructureManager& structs, const WorldRequest& r,
                        bool survival, EditSource src) {
        Blueprint bp;
        std::string why;
        if (!bp.fromText(r.name, why) || bp.parts.empty()) { say("that blueprint can't be read" + (why.empty() ? "" : ": " + why)); return; }
        if (bp.parts.size() > MAX_BLUEPRINT_PARTS) { say("that blueprint is too big"); return; }
        if (survival)
            for (BlueprintPart& p : bp.parts) { p.items.clear(); p.loot.clear(); }   // chests come empty: no items from nothing
        BlueprintStyle style = r.style;
        if (survival) style.rollLoot = false;
        std::vector<BlueprintPlacement> plan = bp.plan(r.lo, r.rot & 3, &style);
        why = Blueprint::check(structs, plan, style.sitePrep);
        if (!why.empty()) { say(why); return; }
        std::map<uint16_t, int> cost = survival && gStructures.costsEnabled ? bp.cost(&style) : std::map<uint16_t, int>();
        for (auto& [item, cnt] : cost)
            if (player.inventory.count(item) < cnt) { say("needs " + std::to_string(cnt) + " " + gItems.get(item).displayName); return; }
        for (auto& [item, cnt] : cost) player.inventory.take(item, cnt);
        why.clear();
        bp.paste(structs, editor, chests, plan, src, &style, &why);
        say(why.empty() ? bp.display + " built" : why);
    }
    static constexpr size_t MAX_BLUEPRINT_PARTS = 2000;

    // ── Hoe ───────────────────────────────────────────────
    // Every shape comes from world/TerrainOps.h, the same
    // functions the console's flatten and the village roads use.
    // Buildings are kept. 'level' is where the player stands.
    void hoe(Player& player, WorldEditor& editor, StructureManager& structs, const ItemDef& held, HoeMode mode) {
        float r = std::clamp(held.toolPower, 2.0f, 16.0f);
        glm::ivec3 c = target.blockPos;
        int level = (int)std::floor(player.position.y + 0.01f);
        terrain::FlattenOptions o;
        o.clearAbove = 24;
        o.maxFill    = 12;
        o.keep = [&structs](glm::ivec3 p) { const StructureInstance* s = structs.instanceAt(p); return s && !s->natural; };
        const ChunkManager& w = editor.worldRef();
        const EditSource src = EditSource::Player;
        int probe = c.y + 24;
        switch (mode) {
        case HoeMode::Level: editor.submit(terrain::flattenDisc(w, c.x, c.z, r, level, o, src)); break;
        case HoeMode::Raise: editor.submit(terrain::shiftDisc(w, c.x, c.z, r, 1, probe, o, src, -1, level + RAISE_MAX)); break;
        case HoeMode::Lower: editor.submit(terrain::shiftDisc(w, c.x, c.z, r, -1, probe, o, src, level - LOWER_MAX)); break;
        default:             editor.submit(terrain::paintDisc(w, c.x, c.z, r * 0.6f, BLOCK_GRAVEL, probe, o, src)); break;
        }
    }

    // ── Blast wand ────────────────────────────────────────
    // A crater, and a push on the player if close (game/DevTools.h
    // explode does the same for the dev tool)
    static constexpr float EXPLOSION_PUSH = 60.0f;
    void blast(WorldEditor& editor, glm::vec3 centre, float radius, Player* player) {
        editor.submit(WorldEditor::explosion(editor.worldRef(), centre, radius, EditSource::Explosion));
        if (!player || !player->rules().hasBody) return;
        glm::vec3 mid = player->position + glm::vec3(0.0f, player->height * 0.5f, 0.0f);
        glm::vec3 d = mid - centre;
        float dist = glm::length(d);
        float reach = radius * 2.0f;
        if (dist >= reach) return;
        glm::vec3 dir = dist > 0.01f ? d / dist : glm::vec3(0, 1, 0);
        dir.y = std::max(dir.y, 0.35f);
        player->hit(centre + d * 0.8f, glm::normalize(dir) * EXPLOSION_PUSH * Human::REFERENCE_MASS * (1.0f - dist / reach));
    }

    // ── Axe hits ──────────────────────────────────────────
    // The blade went into a voxel (game/AxeSwing.h). Damage from
    // the edge's speed:
    //   a tree's wood   health down; at 0 it falls
    //   a fallen log    health down, wood dropped on the ground
    //   anything else   a thunk and some chips, no damage
    // Test Play fells and breaks up in one go.
    void hitWithAxe(Player& player, StructureManager& structs, WorldEditor& editor,
                    const axeswing::BladeHit& hit, float toolPower) {
        bool survival = player.mode == GameMode::Survival;
        EditSource src = survival ? EditSource::Player : EditSource::Dev;
        float dmg = AxeSwing::damageFor(toolPower, hit.speed);
        uint16_t id = editor.typeAt(hit.voxel);

        if (wood) {
            if (Woodcutting::Log* lg = wood->logAt(hit.point)) {
                std::string name = lg->name;
                float maxH = lg->maxHealth;
                float left = std::max(0.0f, lg->health - (survival ? dmg : lg->health));
                auto r = wood->hitLog(*lg, dmg, hit.point, hit.dir, !survival);   // lg may be gone after this
                treeName = name; treeMax = maxH; treeAge = 0.0f;
                treeHealth = r.broken ? 0.0f : left;
                if (r.broken) say(name + " broken up");
                return;
            }
        }
        StructureInstance* s = id == BLOCK_LEAVES ? nullptr : structs.instanceOrNaturalTree(hit.voxel);
        const StructureType* t = s ? structs.typeOf(*s) : nullptr;
        if (t && t->kind == StructureKind::Chest) {
            hitChest(structs, *s, *t, survival ? dmg : s->health + 1.0f, hit.point, -hit.dir, id, src);
            return;
        }
        if (t && t->kind == StructureKind::Tree) {
            if (!survival) dmg = s->health + 1.0f;
            s->health -= dmg;
            treeName = t->displayName; treeMax = t->health; treeAge = 0.0f;
            treeHealth = std::max(0.0f, s->health);
            if (wood) wood->chips(hit.point, id, 7, -hit.dir);
            if (s->health <= 0.0f) {
                glm::vec3 away = glm::vec3(s->origin) - player.position;
                fellTree(structs, *s, away, src, &player);
            }
            return;
        }
        // anything else: the blade stops, nothing is harmed
        if (wood) wood->chips(hit.point, id, 4, -hit.dir);
    }

    // A chest takes a hit like a tree: at 0 it breaks, its
    // contents spill (StructureManager::onRemoved, main.cpp) and
    // it drops what its entry says
    void hitChest(StructureManager& structs, StructureInstance& s, const StructureType& t, float dmg,
                  glm::vec3 point, glm::vec3 away, uint16_t block, EditSource src) {
        s.health -= dmg;
        treeName = t.displayName; treeMax = t.health; treeAge = 0.0f;
        treeHealth = std::max(0.0f, s.health);
        if (wood) wood->chips(point, block, 6, away);
        if (s.health > 0.0f) return;
        glm::vec3 at = glm::vec3(s.origin) + glm::vec3(0.5f, 2.0f, 0.5f);
        say(t.displayName + " broken");
        structs.remove(s.id, src);                    // s is gone after this
        if (t.dropItem && t.dropCount > 0) {
            if (wood) wood->drop(t.dropItem, t.dropCount, at, away);
        }
    }

    // Take a dead tree out of the world and let it fall (away
    // along 'away'), or, with no woodcutting set up, just remove it
    //   who   with no woodcutting (a server: its clients don't see
    //         logs yet), the tree's wood goes straight to them
    void fellTree(StructureManager& structs, StructureInstance& s, glm::vec3 away, EditSource src, Player* who = nullptr) {
        const StructureType* t = structs.typeOf(s);
        if (!t) return;
        say(t->displayName + " falls");
        if (!wood) {
            uint16_t item = t->dropItem;
            int n = t->dropCount;
            structs.chop(s, s.health + 1.0f, src);              // s is gone after this
            if (who && who->mode == GameMode::Survival && item && n > 0 && who->inventory.add(item, n) > 0)
                say("inventory full - some wood lost");
            return;
        }
        Woodcutting::TreeInfo info{ t->displayName, t->dropItem, t->dropCount, t->health };
        glm::ivec3 base = s.origin;
        int stumpTop = base.y + Woodcutting::STUMP_HEIGHT;
        std::vector<VoxelPiece> logVox, leaves;
        structs.fell(s, stumpTop, src, logVox, leaves);       // s is gone after this
        wood->fellTree(std::move(logVox), leaves, away,
                       glm::vec3(base.x + 0.5f, (float)stumpTop, base.z + 0.5f), info);
    }

    // Wood that is part of a natural tree (chopped, not mined)
    static bool isTreeWood(const StructureManager& structs, glm::ivec3 p, uint16_t id) {
        if (id == BLOCK_AIR || id == BLOCK_LEAVES) return false;
        if (gRegistry.get(id).material != "wood") return false;
        return structs.naturalTreeTypeAt(p) != nullptr;
    }

    void devBreak(WorldEditor& editor) {
        if (!target.hit) return;
        editor.apply(WorldEditor::box(target.blockPos, target.blockPos,
                                      BLOCK_AIR, EditSource::Dev, "break"));
    }

    // Break every voxel in the cube, except indestructible
    // ones and anything belonging to a door, building piece or
    // tree. Survival: the voxels go into the inventory.
    void breakCube(Player& player, WorldEditor& editor, StructureManager& structs,
                   const CubeBox& c, bool survival) {
        EditBatch b;
        b.source = survival ? EditSource::Player : EditSource::Dev;
        b.label  = "pickaxe";
        b.undoable    = !survival;
        b.wantChanges = survival;
        b.respectIndestructible = true;
        for (int y = c.lo.y; y <= c.hi.y; y++)
        for (int z = c.lo.z; z <= c.hi.z; z++)
        for (int x = c.lo.x; x <= c.hi.x; x++) {
            glm::ivec3 p(x, y, z);
            uint16_t id = editor.typeAt(p);
            if (id == BLOCK_AIR) continue;
            if (structs.instanceAt(p) || isTreeWood(structs, p, id)) continue;
            b.set(x, y, z, BLOCK_AIR);
        }
        if (b.empty()) return;
        b.wantChanges = true;
        EditResult r = editor.apply(std::move(b));
        popPlantsAbove(player, editor, r, survival);
        if (!survival) return;
        // tally the drops, then add them a stack at a time
        std::vector<std::pair<uint16_t, int>> drops;
        for (const VoxelChange& ch : r.changes) {
            uint16_t item = gItems.itemForBlock(ch.oldID);
            if (!item) continue;
            auto it = std::find_if(drops.begin(), drops.end(), [&](auto& d) { return d.first == item; });
            if (it == drops.end()) drops.push_back({ item, 1 }); else it->second++;
        }
        int lost = 0;
        for (auto& [item, n] : drops) lost += player.inventory.add(item, n);
        if (lost) say("inventory full - " + std::to_string(lost) + " lost");
    }

    // Break whatever is under the crosshair.
    //   survival — chops do damage, pieces refund their cost,
    //              blocks go into the inventory
    void breakTarget(Player& player, WorldEditor& editor, StructureManager& structs,
                     const ItemDef& held, bool survival) {
        if (!target.hit) return;
        EditSource src = survival ? EditSource::Player : EditSource::Dev;
        bool leaves = target.blockID == BLOCK_LEAVES;

        // Wood can belong to a natural tree; leaves just break
        StructureInstance* s = leaves ? nullptr
                                      : structs.instanceOrNaturalTree(target.blockPos);
        const StructureType* t = s ? structs.typeOf(*s) : nullptr;

        if (t && t->kind == StructureKind::Chest) {
            // ── A chest: like a tree, 1 a hit (the axe swings
            // for more); Test Play breaks it at once ───────────
            glm::vec3 p = glm::vec3(target.blockPos) + glm::vec3(0.5f);
            hitChest(structs, *s, *t, survival ? 1.0f : s->health + 1.0f, p, glm::normalize(p - player.position),
                     (uint16_t)target.blockID, src);
            return;
        }
        if (t && t->kind == StructureKind::Tree) {
            // ── Chop ─────────────────────────────────────
            // Hands and other tools do 1 (the axe is swung
            // instead, see axeHit). Test Play fells in one go.
            float dmg = 1.0f;
            if (!survival) dmg = s->health + 1.0f;
            treeName = t->displayName;
            treeMax  = t->health;
            treeHealth = std::max(0.0f, s->health - dmg);
            treeAge  = 0.0f;
            // at 0 it falls over and lies as a log; its wood comes
            // from chopping the log (world/Woodcutting.h)
            s->health -= dmg;
            if (s->health <= 0.0f)
                fellTree(structs, *s, glm::vec3(s->origin) - player.position, src, &player);
            return;
        }

        if (t) {
            // ── A building piece or door: all of it ──────
            auto gone = structs.remove(s->id, src);
            if (gone.size() > 1)
                say(std::to_string(gone.size() - 1) + " piece(s) resting on it collapsed");
            if (survival)
                for (auto& g : gone)
                    if (const StructureType* gt = gStructures.get(g.type); gt && gStructures.charges(*gt))
                        for (auto& [item, n] : gt->cost)
                            player.inventory.add(item, n);
            return;
        }

        // ── A plant: all of it, into the inventory ───────
        if (gRegistry.get((uint16_t)target.blockID).isPlant()) {
            breakPlant(player, editor, target.blockPos, survival, true);
            return;
        }

        // ── An ordinary voxel ─────────────────────────────
        if (!survival) {
            glm::ivec3 at = target.blockPos;
            devBreak(editor);
            EditResult fake;
            fake.changes.push_back({ at, 0, BLOCK_AIR });
            popPlantsAbove(player, editor, fake, false);
            return;
        }
        EditBatch b = WorldEditor::box(target.blockPos, target.blockPos,
                                       BLOCK_AIR, EditSource::Player, "dig");
        b.undoable    = false;
        b.wantChanges = true;
        EditResult r = editor.apply(std::move(b));
        for (const VoxelChange& c : r.changes) {
            uint16_t item = gItems.itemForBlock(c.oldID);
            if (item && player.inventory.add(item, 1) > 0)
                say("inventory full - " + gItems.get(item).displayName + " lost");
        }
        popPlantsAbove(player, editor, r, true);
    }

    // ── Plants (world/Decorations.h) ──────────────────────
    // A plant lives inside its own 4 × 4 square of the world, so
    // its voxels are the plant blocks joined to the one hit,
    // within that square.
    std::vector<glm::ivec3> plantVoxels(WorldEditor& editor, glm::ivec3 p, std::vector<uint16_t>& ids) {
        std::vector<glm::ivec3> out;
        const int PC = decor::PLANT_CELL;
        auto fdiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        int cx = fdiv(p.x, PC), cz = fdiv(p.z, PC);
        std::vector<glm::ivec3> q{ p };
        std::vector<glm::ivec3> seen{ p };
        auto visited = [&](glm::ivec3 v) { return std::find(seen.begin(), seen.end(), v) != seen.end(); };
        for (size_t h = 0; h < q.size() && out.size() < 200; h++) {
            glm::ivec3 v = q[h];
            uint16_t id = editor.typeAt(v);
            if (!gRegistry.get(id).isPlant()) continue;
            out.push_back(v);
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            // corners count too: a leaning grass tip only touches its stalk diagonally
            for (int ox = -1; ox <= 1; ox++) for (int oy = -1; oy <= 1; oy++) for (int oz = -1; oz <= 1; oz++) {
                if (!ox && !oy && !oz) continue;
                glm::ivec3 n = v + glm::ivec3(ox, oy, oz);
                if (fdiv(n.x, PC) != cx || fdiv(n.z, PC) != cz || std::abs(n.y - p.y) > 32 || visited(n)) continue;
                seen.push_back(n);
                q.push_back(n);
            }
        }
        return out;
    }

    // Break the plant at p. It goes into the inventory: the player
    // inventory in Survival, the dev toolbar in Test Play.
    void breakPlant(Player& player, WorldEditor& editor, glm::ivec3 p, bool survival, bool collect) {
        std::vector<uint16_t> ids;
        auto vox = plantVoxels(editor, p, ids);
        if (vox.empty()) return;
        EditBatch b;
        b.source = survival ? EditSource::Player : EditSource::Dev;
        b.label = "pick plant";
        b.undoable = !survival;
        for (const glm::ivec3& v : vox) {
            // under the water line (reeds), water flows back in
            bool wet = false;
            for (int k = 0; k < 4 && !wet; k++) {
                glm::ivec3 n = v + glm::ivec3(k == 0 ? 1 : k == 1 ? -1 : 0, 0, k == 2 ? 1 : k == 3 ? -1 : 0);
                wet = editor.typeAt(n) == BLOCK_WATER;
            }
            b.set(v.x, v.y, v.z, wet ? BLOCK_WATER : BLOCK_AIR);
        }
        editor.apply(std::move(b));
        if (!collect) return;
        const auto& set = *decor::current();
        int def = set.plantFromBlocks(ids);
        uint16_t item = def >= 0 ? gItems.itemForDecor(def) : 0;
        if (!item) return;
        Inventory& inv = survival ? player.inventory : player.devInventory;
        if (!survival && inv.count(item) > 0) { say("picked " + gItems.get(item).displayName); return; }
        if (inv.add(item, 1) > 0) say("inventory full - " + gItems.get(item).displayName + " lost");
        else say("+1 " + gItems.get(item).displayName);
    }

    // Ground dug out from under plants: they come up too
    void popPlantsAbove(Player& player, WorldEditor& editor, const EditResult& r, bool survival) {
        std::vector<glm::ivec3> tops;
        for (const VoxelChange& c : r.changes) {
            glm::ivec3 up = c.pos + glm::ivec3(0, 1, 0);
            if (gRegistry.get(editor.typeAt(up)).isPlant() &&
                std::find(tops.begin(), tops.end(), up) == tops.end()) tops.push_back(up);
            if (tops.size() > 64) break;
        }
        for (const glm::ivec3& t : tops)
            if (gRegistry.get(editor.typeAt(t)).isPlant()) breakPlant(player, editor, t, survival, survival);
    }

    // Left click with a decoration: plants snap into their 4 × 4
    // square on top of the ground (lily pads on water, reeds into
    // shallow water); bushes and landmarks stand where you click.
    int placeCount = 0;
    void placeDecor(Player& player, WorldEditor& editor, const ItemDef& held, bool survival) {
        if (!target.hit) return;
        const decor::Def* d = decor::current()->get(held.decor);
        if (!d || d->variants.empty()) return;
        const decor::Shape& sh = d->variants[(size_t)(placeCount++ % (int)d->variants.size())];
        glm::ivec3 base = target.blockPos + glm::ivec3(target.normal);
        uint16_t hit = (uint16_t)target.blockID;
        bool fillWater = false;
        if (d->layer == decor::Layer::Plant) {
            if (target.normal.y < 0.5f) { say("plants go on top of the ground"); return; }
            if (d->water == decor::Water::Surface) {
                if (hit != BLOCK_WATER) { say(d->display + " floats on water"); return; }
            } else if (d->water == decor::Water::Shallow && hit == BLOCK_WATER) {
                glm::ivec3 g = target.blockPos;                 // down to the floor
                while (g.y > 0 && editor.typeAt(g - glm::ivec3(0, 1, 0)) == BLOCK_WATER) g.y--;
                base = g;
                fillWater = true;
            } else if (hit == BLOCK_WATER || hit == BLOCK_LAVA || gRegistry.get(hit).isPlant()) {
                say("no room for " + d->display); return;
            }
            const int PC = decor::PLANT_CELL;
            auto fdiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
            int cx = fdiv(base.x, PC), cz = fdiv(base.z, PC);
            base.x = cx * PC + std::clamp(base.x - cx * PC, 1, 2);
            base.z = cz * PC + std::clamp(base.z - cz * PC, 1, 2);
            for (int y = base.y - 1; y <= base.y + 8; y++)
                for (int x = cx * PC; x < cx * PC + PC; x++)
                    for (int z = cz * PC; z < cz * PC + PC; z++)
                        if (gRegistry.get(editor.typeAt({ x, y, z })).isPlant()) { say("something is already growing there"); return; }
        }
        EditBatch b;
        b.source = survival ? EditSource::Player : EditSource::Dev;
        b.label = "place " + d->display;
        b.undoable = !survival;
        for (const decor::Vox& v : sh.v) {
            glm::ivec3 q = base + glm::ivec3(v.dx, v.dy, v.dz);
            uint16_t cur = editor.typeAt(q);
            if (v.replace) { if (cur != BLOCK_AIR && cur != BLOCK_WATER && cur != BLOCK_LAVA) b.set(q.x, q.y, q.z, v.id); }
            else if (cur == BLOCK_AIR || (fillWater && cur == BLOCK_WATER)) b.set(q.x, q.y, q.z, v.id);
        }
        if (b.empty()) return;
        EditResult r = editor.apply(std::move(b));
        if (survival && r.changed) useUp(player, held.id);
    }

    static int floorDiv(int a, int b) {
        return (a >= 0) ? a / b : -((-a + b - 1) / b);
    }

    void place(Player& player, WorldEditor& editor, uint16_t blockID,
               EditSource src) {
        if (!target.hit) return;
        glm::ivec3 p = target.blockPos + glm::ivec3(target.normal);
        if (editor.typeAt(p) != BLOCK_AIR) return;
        if (overlapsBody(player, p) && gRegistry.get(blockID).isSolid()) {
            return;   // would trap the player inside the block
        }
        EditBatch b = WorldEditor::box(p, p, blockID, src, "place");
        b.undoable = (src != EditSource::Player);
        EditResult r = editor.apply(std::move(b));
        if (r.changed) useUp(player, gItems.itemForBlock(blockID));
    }

    // Does voxel p overlap the player's body cylinder?
    static bool overlapsBody(const Player& pl, glm::ivec3 p) {
        float bottom = pl.position.y, top = pl.position.y + pl.height;
        if (p.y + 1.0f <= bottom || p.y >= top) return false;
        float cx = std::fmax((float)p.x, std::fmin(pl.position.x, p.x + 1.0f));
        float cz = std::fmax((float)p.z, std::fmin(pl.position.z, p.z + 1.0f));
        float dx = cx - pl.position.x, dz = cz - pl.position.z;
        return dx * dx + dz * dz < Human::RADIUS * Human::RADIUS;
    }

    // An item used up: the one in hand if it's that, else one
    // from anywhere (a server's idea of the hand can lag a click)
    static void useUp(Player& player, uint16_t item) {
        Inventory& inv = player.activeInventory();
        if (!item) return;
        if (inv.heldDef().id == item) inv.consumeHeld();
        else inv.take(item, 1);
    }
};
