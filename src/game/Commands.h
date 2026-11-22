#pragma once
#include "render/Particles.h"
#include "dungeon/StarterTiles.h"
#include "dungeon/Workshop.h"
#include "worldgen/DungeonGenerator.h"
#include "input/Rumble.h"
#include "DevTools.h"
#include "StructureRegistry.h"
#include "../world/StructureManager.h"
#include "../world/Chests.h"
#include "../world/TerrainOps.h"
#include "Blueprint.h"
#include "Village.h"
#include "Villagers.h"
#include "DevKit.h"
#include "GameMode.h"
#include "ItemRegistry.h"
#include "../entity/Player.h"
#include "../npc/NpcManager.h"
#include "../sim/Simulation.h"
#include "../render/OutlineFx.h"
#include "../npc/Factions.h"
#include "DebugView.h"
#include "../physics/IPhysics.h"
#include "../render/Sky.h"
#include "../world/WorldSaveManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Commands — typed console commands
// ══════════════════════════════════════════════════════════
// Open the console with ` (grave), type a command, Enter.
// The leading slash is optional. "help" lists everything.
//
// Commands are a thin layer: each one parses its words and
// calls the same DevTools / WorldEditor functions the tool
// items use. Scripts and the server will run commands
// through this same registry later.
//
// Coordinates are world voxels. "~" means the player's own
// position on that axis, "~5" means five voxels past it:
//   /pos1 ~ ~ ~        /fill ~-10 ~ ~-10 ~10 ~5 ~10 stone

// Everything a command may touch
struct CommandContext {
    Player&              player;
    DevTools&            dev;
    const RaycastResult& target;      // block under the crosshair
    StructureManager*    structs;     // nullptr outside a world
    std::function<void(GameMode)> changeMode;
    std::function<void()>         saveWorld;
    std::function<void(const std::string&)> log;
    NpcManager*          npcs = nullptr;  // nullptr outside a world
    Simulation*          sim = nullptr;
    ChestStore*          chests = nullptr;  // structures' containers (nullptr outside a world)
    const TerrainGenerator* gen = nullptr;  // the world's generator (nullptr outside a world)
    std::function<void(const std::vector<std::string>&)> portal;   // the game's portals (game/Portals.h)
    WorldSaveManager*    info = nullptr;  // the world's settings (world.vxrp; nullptr outside a world)
    std::function<void(const std::vector<std::string>&)> portalGun;   // the portal gun (game/PortalGun.h)
};

class CommandRegistry {
public:
    using Args = std::vector<std::string>;
    using Fn   = std::function<void(CommandContext&, const Args&)>;

    struct Command {
        std::string usage;   // "fill <x y z> <x y z> <block>"
        std::string help;    // one line
        Fn          fn;
    };

    void add(const std::string& name, const std::string& usage,
             const std::string& help, Fn fn) {
        commands[name] = { usage, help, std::move(fn) };
    }

    // Run one typed line. Returns false if the command does
    // not exist.
    bool run(const std::string& line, CommandContext& ctx) {
        Args words = split(line);
        if (words.empty()) return true;
        std::string name = words[0];
        if (!name.empty() && name[0] == '/') name.erase(0, 1);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        auto it = commands.find(name);
        if (it == commands.end()) {
            ctx.log("unknown command: " + name + "  (try help)");
            return false;
        }
        Args args(words.begin() + 1, words.end());
        it->second.fn(ctx, args);
        return true;
    }

    const std::map<std::string, Command>& all() const { return commands; }

    // ── Parsing helpers for command bodies ────────────────
    static Args split(const std::string& line) {
        Args out;
        std::istringstream in(line);
        std::string w;
        while (in >> w) out.push_back(w);
        return out;
    }

    // One coordinate: "12", "~", "~-4"
    static bool coord(const std::string& s, float base, int& out) {
        if (s.empty()) return false;
        char* end = nullptr;
        if (s[0] == '~') {
            long off = s.size() > 1 ? std::strtol(s.c_str() + 1, &end, 10) : 0;
            if (s.size() > 1 && *end) return false;
            out = (int)std::floor(base) + (int)off;
            return true;
        }
        long v = std::strtol(s.c_str(), &end, 10);
        if (*end) return false;
        out = (int)v;
        return true;
    }

    // Three coordinates starting at args[i]
    static bool point(const Args& a, size_t i, glm::vec3 base, glm::ivec3& out) {
        return a.size() >= i + 3 &&
               coord(a[i],     base.x, out.x) &&
               coord(a[i + 1], base.y, out.y) &&
               coord(a[i + 2], base.z, out.z);
    }

    // Block by name ("air" works). 0xFFFF if unknown.
    static uint16_t block(const std::string& name) {
        if (name == "air") return BLOCK_AIR;
        uint16_t id = gRegistry.getIDByName(name);
        return id ? id : 0xFFFF;
    }

private:
    std::map<std::string, Command> commands;   // sorted for help
};

// ── The built-in commands ─────────────────────────────────
inline void registerDefaultCommands(CommandRegistry& reg) {
    using Args = CommandRegistry::Args;
    using C    = CommandContext;

    // Where a command acts when no coordinates are given:
    // the block under the crosshair, else the player's feet
    auto aimed = [](C& c, bool against) -> glm::ivec3 {
        if (c.target.hit)
            return c.target.blockPos +
                   (against ? glm::ivec3(c.target.normal) : glm::ivec3(0));
        return glm::ivec3(glm::floor(c.player.position));
    };
    auto badBlock = [](C& c, const std::string& n) {
        c.log("unknown block: " + n + "  (try blocks)");
    };

    reg.add("help", "help [command]", "list commands, or show one",
        [&reg](C& c, const Args& a) {
            if (!a.empty()) {
                auto it = reg.all().find(a[0]);
                if (it == reg.all().end()) { c.log("no command " + a[0]); return; }
                c.log(it->second.usage + " - " + it->second.help);
                return;
            }
            std::string line;
            for (auto& [name, cmd] : reg.all()) {
                if (line.size() + name.size() > 70) { c.log(line); line.clear(); }
                line += name + "  ";
            }
            if (!line.empty()) c.log(line);
            c.log("help <command> for details");
        });

    // ── Selection ─────────────────────────────────────────
    auto posCmd = [aimed](int which) {
        return [aimed, which](C& c, const Args& a) {
            glm::ivec3 p;
            if (a.empty()) p = aimed(c, false);
            else if (!CommandRegistry::point(a, 0, c.player.position, p)) {
                c.log("usage: pos" + std::to_string(which) + " [x y z]");
                return;
            }
            c.dev.setPos(which, p);
        };
    };
    reg.add("pos1", "pos1 [x y z]", "selection corner 1 (default: block you look at)", posCmd(1));
    reg.add("pos2", "pos2 [x y z]", "selection corner 2 (default: block you look at)", posCmd(2));
    reg.add("desel", "desel", "clear the selection",
        [](C& c, const Args&) { c.dev.clearSelection(); });
    reg.add("sel", "sel", "show the selection",
        [](C& c, const Args&) {
            if (!c.dev.hasSelection()) { c.log("no selection"); return; }
            c.log(DevTools::str(c.dev.selMin()) + " to " +
                  DevTools::str(c.dev.selMax()) + "  " +
                  DevTools::sizeStr(c.dev.selSize()));
        });

    // ── Filling ───────────────────────────────────────────
    reg.add("set", "set <block>", "fill the selection",
        [badBlock](C& c, const Args& a) {
            if (a.empty()) { c.log("usage: set <block>"); return; }
            uint16_t id = CommandRegistry::block(a[0]);
            if (id == 0xFFFF) { badBlock(c, a[0]); return; }
            c.dev.fillSelection(id, EditSource::Command);
        });
    reg.add("fill", "fill <x y z> <x y z> <block>", "select a box and fill it",
        [badBlock](C& c, const Args& a) {
            glm::ivec3 p1, p2;
            if (a.size() < 7 ||
                !CommandRegistry::point(a, 0, c.player.position, p1) ||
                !CommandRegistry::point(a, 3, c.player.position, p2)) {
                c.log("usage: fill <x y z> <x y z> <block>");
                return;
            }
            uint16_t id = CommandRegistry::block(a[6]);
            if (id == 0xFFFF) { badBlock(c, a[6]); return; }
            c.dev.setPos(1, p1);
            c.dev.setPos(2, p2);
            c.dev.fillSelection(id, EditSource::Command);
        });
    reg.add("replace", "replace <from> <to>", "swap one block type for another in the selection",
        [badBlock](C& c, const Args& a) {
            if (a.size() < 2) { c.log("usage: replace <from> <to>"); return; }
            uint16_t from = CommandRegistry::block(a[0]);
            uint16_t to   = CommandRegistry::block(a[1]);
            if (from == 0xFFFF) { badBlock(c, a[0]); return; }
            if (to   == 0xFFFF) { badBlock(c, a[1]); return; }
            c.dev.replaceSelection(from, to, EditSource::Command);
        });
    reg.add("sphere", "sphere <block> [radius]", "ball of blocks at the block you look at",
        [aimed, badBlock](C& c, const Args& a) {
            if (a.empty()) { c.log("usage: sphere <block> [radius]"); return; }
            uint16_t id = CommandRegistry::block(a[0]);
            if (id == 0xFFFF) { badBlock(c, a[0]); return; }
            float r = a.size() > 1 ? (float)std::atof(a[1].c_str()) : c.dev.brushRadius;
            c.dev.sphere(glm::vec3(aimed(c, true)) + 0.5f, r, id, EditSource::Command);
        });
    reg.add("carve", "carve [radius]", "hollow a ball at the block you look at",
        [aimed](C& c, const Args& a) {
            float r = a.empty() ? c.dev.brushRadius : (float)std::atof(a[0].c_str());
            c.dev.sphere(glm::vec3(aimed(c, false)) + 0.5f, r, BLOCK_AIR, EditSource::Command);
        });
    reg.add("explode", "explode [radius]", "explosion at the block you look at",
        [aimed](C& c, const Args& a) {
            float r = a.empty() ? c.dev.brushRadius : (float)std::atof(a[0].c_str());
            c.dev.explode(glm::vec3(aimed(c, false)) + 0.5f, r,
                          EditSource::Explosion, &c.player);
        });

    // ── Clipboard ─────────────────────────────────────────
    reg.add("copy", "copy", "copy the selection to the clipboard",
        [](C& c, const Args&) { c.dev.copySelection(); });
    reg.add("paste", "paste [x y z] [air]",
        "paste on the block you look at, or at x y z (air: paste air too)",
        [aimed](C& c, const Args& a) {
            glm::ivec3 at = aimed(c, true);
            size_t next = 0;
            if (a.size() >= 3 && CommandRegistry::point(a, 0, c.player.position, at))
                next = 3;
            bool air = a.size() > next && a[next] == "air";
            c.dev.pasteAt(at, EditSource::Command, air);
        });
    reg.add("rotate", "rotate [90|180|270]", "turn the clipboard clockwise",
        [](C& c, const Args& a) {
            int deg = a.empty() ? 90 : std::atoi(a[0].c_str());
            if (deg % 90) { c.log("rotate by 90, 180 or 270"); return; }
            c.dev.rotateClipboard(deg / 90);
        });
    reg.add("flip", "flip <x|z>", "mirror the clipboard",
        [](C& c, const Args& a) {
            if (a.empty() || (a[0] != "x" && a[0] != "z")) {
                c.log("usage: flip <x|z>");
                return;
            }
            c.dev.flipClipboard(a[0][0]);
        });
    reg.add("undo", "undo [times]", "undo the last edit(s)",
        [](C& c, const Args& a) {
            c.dev.undo(a.empty() ? 1 : std::max(1, std::atoi(a[0].c_str())));
        });
    reg.add("redo", "redo [times]", "redo what undo took back",
        [](C& c, const Args& a) {
            c.dev.redo(a.empty() ? 1 : std::max(1, std::atoi(a[0].c_str())));
        });
    reg.add("save", "save <name>", "save the clipboard as a structure file",
        [](C& c, const Args& a) {
            if (a.empty()) { c.log("usage: save <name>"); return; }
            c.dev.saveStructure(a[0]);
        });
    reg.add("load", "load <name>", "load a structure (oak_tree, pine_tree, dead_tree, ancient_oak, or a saved name)",
        [](C& c, const Args& a) {
            if (a.empty()) { c.log("usage: load <name>"); return; }
            c.dev.loadStructure(a[0]);
        });
    reg.add("place", "place <structure> [x y z] [turns]",
        "place a structure from structures.toml as a real structure",
        [aimed](C& c, const Args& a) {
            if (a.empty() || !c.structs) { c.log("usage: place <structure> [x y z] [turns]"); return; }
            uint16_t type = gStructures.find(a[0]);
            if (!type) { c.log("unknown structure: " + a[0] + "  (try structures)"); return; }
            glm::ivec3 at = aimed(c, true);
            size_t next = 1;
            if (a.size() >= 4 && CommandRegistry::point(a, 1, c.player.position, at)) next = 4;
            int rot = a.size() > next ? std::atoi(a[next].c_str()) & 3 : 0;
            uint32_t sup = 0;
            std::string why = c.structs->checkPlace(type, at, rot, false, &sup);
            if (!why.empty()) { c.log("can't place: " + why); return; }
            c.structs->place(type, at, rot, EditSource::Command, sup);
            c.log("placed " + gStructures.get(type)->displayName + " at " + DevTools::str(at));
        });
    reg.add("structures", "structures", "list structures",
        [](C& c, const Args&) {
            auto list = DevTools::listStructures();
            std::string types = "place: ";
            for (uint16_t id : gStructures.all()) types += gStructures.get(id)->name + "  ";
            c.log(types);
            if (list.empty()) { c.log("no structures saved yet"); return; }
            std::string line = "saved: ";
            for (auto& n : list) line += n + "  ";
            c.log(line);
        });

    // ── Blueprints, villages, terrain ─────────────────────
    // The same functions the hammer and the village generator
    // use (game/Blueprint.h, game/Village.h, world/TerrainOps.h)
    reg.add("blueprint", "blueprint [list | place <name> [turns] [palette] | save <name> | reload]",
        "whole buildings: list them, place one at the ground you look at, or save the building you look at",
        [aimed](C& c, const Args& a) {
            std::string sub = a.empty() ? "list" : a[0];
            if (sub == "list") {
                std::string line;
                for (const Blueprint& b : gBlueprints.list()) line += b.name + "  ";
                c.log(line.empty() ? "no blueprints (data/blueprints)" : line);
                return;
            }
            if (sub == "reload") { c.log(std::to_string(gBlueprints.load()) + " blueprints"); return; }
            if (!c.structs || !c.dev.worldEditor()) { c.log("needs a world"); return; }
            if (sub == "place" && a.size() >= 2) {
                const Blueprint* bp = gBlueprints.find(a[1]);
                if (!bp) { c.log("no blueprint " + a[1] + "  (try blueprint list)"); return; }
                int q = a.size() > 2 ? std::atoi(a[2].c_str()) & 3 : 0;
                glm::ivec3 at = aimed(c, true);
                glm::ivec3 sz = bp->sizeTurned(q);
                BlueprintStyle style;
                if (a.size() > 3) {
                    const BuildPalette* pal = gBuildPalettes.find(a[3]);
                    if (!pal) { c.log("no palette " + a[3]); return; }
                    style.palette = pal->spec; style.paletteName = pal->name;
                }
                auto plan = bp->plan({ at.x - sz.x / 2, at.y, at.z - sz.z / 2 }, q, &style);
                std::string why = Blueprint::check(*c.structs, plan, true);
                if (!why.empty()) { c.log("can't place: " + why); return; }
                auto made = bp->paste(*c.structs, *c.dev.worldEditor(), c.chests, plan, EditSource::Command, &style, &why);
                c.log("placed " + bp->display + ", " + std::to_string(made.size()) + " pieces" + (why.empty() ? "" : "  (" + why + ")"));
                return;
            }
            if (sub == "save" && a.size() >= 2) {
                const StructureInstance* s = c.target.hit ? c.structs->instanceAt(c.target.blockPos) : nullptr;
                if (!s) { c.log("look at a piece of the building"); return; }
                Blueprint bp;
                std::string why;
                if (!Blueprint::capture(*c.structs, s->id, c.chests, bp, why)) { c.log(why); return; }
                bp.name = a[1]; bp.display = a[1]; bp.tags = { "player" };
                c.log(gBlueprints.save(bp) ? "saved " + a[1] + ", " + std::to_string(bp.parts.size()) + " pieces"
                                           : std::string("could not save"));
                return;
            }
            c.log("usage: blueprint [list | place <name> [turns] [palette] | save <name> | reload]");
        });
    reg.add("flatten", "flatten [radius] [level]",
        "level the ground in a circle around the block you look at (to its height, or a given level)",
        [](C& c, const Args& a) {
            if (!c.dev.worldEditor() || !c.target.hit) { c.log("look at the ground"); return; }
            float r = a.empty() ? 6.0f : (float)std::atof(a[0].c_str());
            glm::ivec3 p = c.target.blockPos;
            int level = a.size() > 1 ? std::atoi(a[1].c_str()) : p.y + 1;
            terrain::FlattenOptions o;
            o.clearAbove = 24;
            if (c.structs) {
                StructureManager* sm = c.structs;
                o.keep = [sm](glm::ivec3 q) { const StructureInstance* s = sm->instanceAt(q); return s && !s->natural; };
            }
            c.dev.worldEditor()->submit(terrain::flattenDisc(c.dev.worldEditor()->worldRef(), p.x, p.z,
                                                             std::clamp(r, 1.0f, 48.0f), level, o, EditSource::Command));
            c.log("levelled to " + std::to_string(level));
        });
    reg.add("village", "village [seed] [roads 1-4] [length] [palette]",
        "lay out a village around you from the blueprints (game/Village.h)",
        [](C& c, const Args& a) {
            if (!c.structs || !c.dev.worldEditor()) { c.log("needs a world"); return; }
            village::Options o;
            o.seed  = a.size() > 0 ? (uint32_t)std::atoi(a[0].c_str()) : (uint32_t)std::rand();
            o.roads = a.size() > 1 ? std::atoi(a[1].c_str()) : 3;
            if (a.size() > 2) o.roadLen = std::clamp(std::atoi(a[2].c_str()), 30, 300);
            village::pickPalette(o, a.size() > 3 ? a[3] : std::string());
            glm::ivec3 at = glm::ivec3(glm::floor(c.player.position));
            WorldEditor& ed = *c.dev.worldEditor();
            village::Plan p = village::plan(ed.worldRef(), gBlueprints, at, o);
            village::Built b = village::build(p, *c.structs, ed, c.chests, o, EditSource::Command);
            villagers::Populated pop;
            if (c.npcs) pop = villagers::populate(p, b, *c.structs, ed.worldRef(), *c.npcs, o.seed);
            c.log("village " + std::to_string(o.seed) + " (" + o.style.paletteName + "): " + std::to_string(b.buildings) + " buildings, " +
                  std::to_string(b.roads) + " roads, " + std::to_string(pop.villagers) + " villagers (" + std::to_string(pop.jobs) +
                  " with jobs)" + (b.failed ? ", " + std::to_string(b.failed) + " didn't fit" : ""));
            for (const std::string& s : pop.who) c.log("  " + s);
        });

    // ── The combat layout (game/CombatKit.h) ─────────────
    reg.add("kit", "kit [show | off <1|2> <item> | slot <1|2> <x|y|b> <1-3> <item> | charm <1|2> <1-2> <item> | quick <1-8> <item> | reset]",
        "the two loadouts (off hand, X / Y / B, charms) and the 8 quick options; - clears. The inventory screen does the same by dragging",
        [](C& c, const Args& a) {
            CombatKit& k = c.player.kit;
            Inventory& inv = c.player.inventory;
            auto item = [&](const std::string& n) -> uint16_t {
                if (n == "-") return 0;
                uint16_t id = gItems.find(n);
                if (!id) { c.log("no item " + n); return 0; }
                if (inv.find(id) < 0) { c.log("you don't carry " + n + " (give it first)"); return 0; }
                return id;
            };
            auto num = [](const std::string& s, int hi) { int v = std::atoi(s.c_str()) - 1; return v >= 0 && v < hi ? v : -1; };
            std::string sub = a.empty() ? "show" : a[0];
            if (sub == "reset") { k = CombatKit(); }
            else if (sub == "off" && a.size() >= 3) { int s = num(a[1], 2); if (s >= 0) k.sets[s].off = item(a[2]); }
            else if (sub == "slot" && a.size() >= 5) {
                int s = num(a[1], 2), i = num(a[3], 3), b = a[2] == "x" ? 0 : a[2] == "y" ? 1 : a[2] == "b" ? 2 : -1;
                if (s >= 0 && i >= 0 && b >= 0) k.sets[s].slots[b][i] = item(a[4]);
            } else if (sub == "charm" && a.size() >= 4) {
                int s = num(a[1], 2), i = num(a[2], 2);
                if (s >= 0 && i >= 0) k.sets[s].charms[i] = item(a[3]);
            } else if (sub == "quick" && a.size() >= 3) { int i = num(a[1], 8); if (i >= 0) k.quick[i] = item(a[2]); }
            else if (sub != "show") { c.log("usage: kit [show | off <1|2> <item> | slot <1|2> <x|y|b> <1-3> <item> | charm <1|2> <1-2> <item> | quick <1-8> <item> | reset]"); return; }
            k.tidy(inv);
            auto nm = [](uint16_t id) { return id ? gItems.get(id).name : std::string("-"); };
            for (int i = 0; i < CombatKit::SETS; i++) {
                const WeaponSet& w = k.sets[i];
                std::string line = (i == k.setAt ? "> " : "  ") + std::to_string(i + 1) + ": " + nm(k.primary(inv, i)) + " + " + nm(w.off);
                const char* sl[3] = { "  X ", "  Y ", "  B " };
                for (int j = 0; j < 3; j++) {
                    line += sl[j];
                    for (int n = 0; n < CombatKit::PER_SLOT; n++) line += (n ? "," : "") + nm(w.slots[j][n]);
                }
                line += "  charms " + nm(w.charms[0]) + "," + nm(w.charms[1]);
                c.log(line);
            }
            std::string q = "  quick:";
            for (uint16_t id : k.quick) q += " " + nm(id);
            c.log(q);
        });

    // ── Settings ──────────────────────────────────────────
    reg.add("material", "material <block>", "block used by fill, brush and replace tools",
        [badBlock](C& c, const Args& a) {
            if (a.empty()) { c.log("material: " + DevTools::blockName(c.dev.material)); return; }
            uint16_t id = CommandRegistry::block(a[0]);
            if (id == 0xFFFF) { badBlock(c, a[0]); return; }
            c.dev.setMaterial(id);
        });
    reg.add("brush", "brush <radius>", "size of the brush and explosion tools",
        [](C& c, const Args& a) {
            if (!a.empty()) c.dev.setBrush((float)std::atof(a[0].c_str()));
            c.log("brush radius " + std::to_string((int)c.dev.brushRadius));
        });
    reg.add("time", "time [day|noon|dusk|night|midnight|0-24|freeze|run|length <min>]",
            "show or set the time of day",
        [](C& c, const Args& a) {
            if (!c.sim) { c.log("needs a world"); return; }
            if (!a.empty()) {
                const std::string& w = a[0];
                if (w == "day" || w == "morning") c.sim->day.time = 0.32f;
                else if (w == "noon")     c.sim->day.time = 0.5f;
                else if (w == "dusk" || w == "sunset") c.sim->day.time = 0.745f;
                else if (w == "night")    c.sim->day.time = 0.88f;
                else if (w == "midnight") c.sim->day.time = 0.0f;
                else if (w == "dawn" || w == "sunrise") c.sim->day.time = 0.255f;
                else if (w == "freeze")   c.sim->day.frozen = true;
                else if (w == "run")      c.sim->day.frozen = false;
                else if (w == "length" && a.size() > 1) c.sim->day.dayLengthSeconds = std::max(0.5f, (float)std::atof(a[1].c_str())) * 60.0f;
                else c.sim->day.time = std::fmod(std::fabs((float)std::atof(w.c_str())) / 24.0f, 1.0f);
            }
            int mins = (int)(c.sim->day.time * 24.0f * 60.0f);
            char b[64];
            std::snprintf(b, sizeof(b), "time %02d:%02d%s (day %d min)", mins / 60, mins % 60,
                          c.sim->day.frozen ? " frozen" : "", (int)(c.sim->day.dayLengthSeconds / 60.0f));
            c.log(b);
        });
    reg.add("cube", "cube <size>", "width of the Cube Tool's cube, in voxels",
        [](C& c, const Args& a) {
            if (!a.empty()) c.dev.setCube(std::atoi(a[0].c_str()));
            c.log("cube " + std::to_string(c.dev.cubeSize) + " voxels");
        });

    // ── Player ────────────────────────────────────────────
    reg.add("give", "give <item> [count] [player|dev]", "put items in an inventory",
        [](C& c, const Args& a) {
            if (a.empty()) { c.log("usage: give <item> [count] [player|dev]"); return; }
            uint16_t id = gItems.find(a[0]);
            if (!id) { c.log("unknown item: " + a[0] + "  (try items)"); return; }
            int count = a.size() > 1 ? std::max(1, std::atoi(a[1].c_str())) : 1;
            bool dev  = a.size() > 2 ? a[2] == "dev"
                                     : c.player.mode != GameMode::Survival;
            Inventory& inv = dev ? c.player.devInventory : c.player.inventory;
            const ItemDef& d = gItems.get(id);
            int left = inv.add(id, count);
            // a weapon with a free weapon slot is equipped there
            if (d.is(IC_WEAPON) && !d.is(IC_TOOL) && left < count &&
                (inv.equip[(int)EquipSlot::Weapon1] < 0 || inv.equip[(int)EquipSlot::Weapon2] < 0))
                inv.equipItem(id);
            c.log("gave " + std::to_string(count - left) + " " + d.displayName +
                  " to the " + (dev ? "dev" : "player") + " inventory" +
                  (left ? "  (" + std::to_string(left) + " did not fit)" : ""));
        });
    reg.add("clear", "clear [player|dev]", "empty an inventory",
        [](C& c, const Args& a) {
            bool dev = a.empty() ? c.player.mode != GameMode::Survival
                                 : a[0] == "dev";
            Inventory& inv = dev ? c.player.devInventory : c.player.inventory;
            inv.clearAll();
            c.log(std::string(dev ? "dev" : "player") + " inventory emptied");
        });
    reg.add("tp", "tp <x y z>", "move to a position",
        [](C& c, const Args& a) {
            glm::ivec3 p;
            if (!CommandRegistry::point(a, 0, c.player.position, p)) {
                c.log("usage: tp <x y z>");
                return;
            }
            c.player.position = glm::vec3(p) + glm::vec3(0.5f, 0.0f, 0.5f);
            c.player.velocity = glm::vec3(0.0f);
            c.player.ragdoll.reset();
            c.log("moved to " + DevTools::str(p));
        });
    reg.add("mode", "mode <spectator|dev|survival>", "change game mode",
        [](C& c, const Args& a) {
            if (a.empty()) { c.log(std::string("mode: ") + c.player.rules().name); return; }
            GameMode m = gameModeFromKey(a[0] == "test" ? "dev" : a[0], GameMode::COUNT);
            if (m == GameMode::COUNT) { c.log("modes: spectator, dev, survival"); return; }
            c.changeMode(m);
        });
    // ── Body: weight and the ragdoll ─────────────────────
    // A push from one side, as a speed on an average (70 kg)
    // body: heavier bodies move less. Knocks down from about
    // 35 (Ragdoll::KNOCKDOWN_DV) on an average body.
    auto push = [](C& c, float speed, const std::string& side, float lift) {
        glm::vec3 f = c.player.facing;
        f.y = 0.0f;
        f = glm::length(f) > 1e-4f ? glm::normalize(f) : glm::vec3(0, 0, -1);
        glm::vec3 left(f.z, 0.0f, -f.x);
        // dir: the way the body is pushed (from the named side)
        glm::vec3 dir = side == "back" ? f : side == "left" ? -left : side == "right" ? left : -f;
        glm::vec3 at = c.player.bodyPoint(0.62f) - dir * 1.5f;
        glm::vec3 J = glm::normalize(dir + glm::vec3(0.0f, lift, 0.0f)) * speed * Human::REFERENCE_MASS;
        c.player.hit(at, J);
        char b[96];
        std::snprintf(b, sizeof(b), "pushed from the %s: impulse %.0f on %.1f kg = %.1f vox/s",
                      side.c_str(), glm::length(J), c.player.mass, glm::length(J) / c.player.mass);
        c.log(b);
    };
    reg.add("shove", "shove [speed] [front|back|left|right]",
        "push your body (speed on a 70 kg body, default 20)",
        [push](C& c, const Args& a) {
            float speed = a.size() > 0 ? (float)std::atof(a[0].c_str()) : 20.0f;
            push(c, speed, a.size() > 1 ? a[1] : "front", 0.1f);
        });
    reg.add("knockdown", "knockdown [speed] [front|back|left|right]",
        "knock your body down (default 55)",
        [push](C& c, const Args& a) {
            float speed = a.size() > 0 ? (float)std::atof(a[0].c_str()) : 55.0f;
            push(c, speed, a.size() > 1 ? a[1] : "front", 0.35f);
        });
    // ── Combat (game/Combat.h, game/Npcs.h) ───────────────
    reg.add("loot", "loot [roll <table> [times] | <table> | none]",
        "loot tables: list, roll one, or set the storage you aim at to roll from one when next opened",
        [](C& c, const Args& a) {
            auto show = [&](const LootTables::Items& items) {
                if (items.empty()) { c.log("  nothing"); return; }
                std::string line = " ";
                for (auto& [item, n] : items) {
                    std::string part = " " + gItems.get(item).displayName + " x" + std::to_string(n);
                    if (line.size() + part.size() > 70) { c.log(line); line = " "; }
                    line += part;
                }
                c.log(line);
            };
            if (a.empty()) {
                std::string all;
                for (auto& n : gLoot.names()) all += (all.empty() ? "" : "  ") + n;
                c.log("loot tables: " + (all.empty() ? std::string("none") : all));
                return;
            }
            if (a[0] == "roll") {
                if (a.size() < 2 || !gLoot.get(a[1])) { c.log("loot roll <table> [times]  (loot lists them)"); return; }
                int times = a.size() > 2 ? std::clamp(std::atoi(a[2].c_str()), 1, 10) : 1;
                static uint32_t salt = 1;
                for (int i = 0; i < times; i++) { c.log(a[1] + " roll " + std::to_string(i + 1) + ":"); show(gLoot.roll(a[1], salt++ * 2654435761u)); }
                return;
            }
            std::string table = a[0] == "none" ? "" : a[0];
            if (!table.empty() && !gLoot.get(table)) { c.log("no loot table " + table + "  (loot lists them)"); return; }
            const StructureInstance* si = (c.structs && c.target.hit) ? c.structs->instanceAt(c.target.blockPos) : nullptr;
            const StructureType* t = si ? gStructures.get(si->type) : nullptr;
            if (!ChestStore::holds(t) || !c.chests) { c.log("aim at a chest (anything with storage)"); return; }
            static uint32_t salt2 = 7;
            c.chests->get(si->id, *t).reset(table, c.chests->seedFor(si->id) ^ (salt2++ * 0x9E3779B9u));
            c.log(t->displayName + (table.empty() ? ": emptied" : ": rolls " + table + " when next opened (what was in it is gone)"));
        });

    reg.add("chest", "chest [kit | fill <kind|item>... | clear | name [text] | showcase [on|off] | loot <table|none>]",
        "set up the chest you aim at (kinds: weapons armour tools blocks decor dev all); kit: a row of showcase chests",
        [](C& c, const Args& a) {
            if (!c.structs || !c.chests) { c.log("not in a world"); return; }
            if (!a.empty() && a[0] == "kit") {
                int n = devkit::placeKit(*c.structs, *c.chests, c.player.position, c.player.facing, c.log);
                c.log(n ? "placed " + std::to_string(n) + " showcase chests (take a copy; del removes)" : "no room for the kit here");
                return;
            }
            const StructureInstance* si = c.target.hit ? c.structs->instanceAt(c.target.blockPos) : nullptr;
            const StructureType* t = si ? gStructures.get(si->type) : nullptr;
            if (!ChestStore::holds(t)) { c.log("aim at a chest (or: chest kit)"); return; }
            Container& box = c.chests->get(si->id, *t);
            std::string name = box.label.empty() ? t->displayName : box.label;
            std::string what = a.empty() ? "" : a[0];
            if (what.empty()) {
                int stacks = 0;
                if (box.peek()) for (auto& st : box.peek()->slots) stacks += !st.empty();
                c.log(name + ": " + std::to_string(box.capacity) + " slots, " +
                      (box.opened() ? std::to_string(stacks) + " stacks" : "unopened") +
                      (box.loot.empty() ? "" : ", loot " + box.loot) + (box.showcase ? ", showcase" : ""));
            } else if (what == "fill") {
                if (a.size() < 2) { c.log("chest fill <weapons|armour|tools|blocks|decor|dev|all|item>..."); return; }
                Inventory& inv = box.read();
                int n = 0;
                for (size_t i = 1; i < a.size(); i++) {
                    if (uint32_t cat = devkit::categoryFromName(a[i])) { n += devkit::fill(inv, cat); continue; }
                    uint16_t id = gItems.find(a[i]);
                    if (!id) { c.log("no item or kind " + a[i]); continue; }
                    if (inv.add(id, devkit::showCount(id)) == 0) n++;
                }
                c.log(name + ": " + std::to_string(n) + " stacks in" + (inv.freeSlot() < 0 ? " (full)" : ""));
            } else if (what == "clear") {
                for (auto& st : box.read().slots) st.clear();
                c.log(name + ": emptied");
            } else if (what == "name") {
                std::string text;
                for (size_t i = 1; i < a.size(); i++) text += (i > 1 ? " " : "") + a[i];
                box.label = text;
                c.log(text.empty() ? t->displayName + ": name cleared" : "named " + text);
            } else if (what == "showcase") {
                bool on = a.size() > 1 ? (a[1] == "on" || a[1] == "1") : !box.showcase;
                box.read();
                box.setShowcase(on);
                c.log(name + (on ? ": showcase - taking gives a copy" : ": an ordinary chest again"));
            } else if (what == "loot") {
                std::string table = a.size() > 1 && a[1] != "none" ? a[1] : "";
                if (!table.empty() && !gLoot.get(table)) { c.log("no loot table " + table + "  (loot lists them)"); return; }
                static uint32_t salt = 11;
                box.reset(table, c.chests->seedFor(si->id) ^ (salt++ * 0x9E3779B9u));
                c.log(name + (table.empty() ? ": emptied" : ": rolls " + table + " when next opened"));
            } else c.log("chest [kit | fill ... | clear | name [text] | showcase [on|off] | loot <table|none>]");
        });

    reg.add("spawn", "spawn <type> [count] [distance]",
        "spawn NPCs of a type (npcs.toml) in front of you; no type: list them",
        [](C& c, const Args& a) {
            if (!c.npcs) { c.log("no world"); return; }
            if (a.empty() || !gNpcTypes.find(a[0])) {
                std::string l;
                for (auto& id : gNpcTypes.ids()) l += (l.empty() ? "" : ", ") + id;
                c.log("types: " + l);
                return;
            }
            int n = a.size() > 1 ? std::clamp(std::atoi(a[1].c_str()), 1, 12) : 1;
            float dist = a.size() > 2 ? std::clamp((float)std::atof(a[2].c_str()), 4.0f, 200.0f) : 24.0f;
            glm::vec3 f = c.player.facing; f.y = 0.0f;
            f = glm::length(f) > 1e-4f ? glm::normalize(f) : glm::vec3(0, 0, -1);
            glm::vec3 side(f.z, 0.0f, -f.x);
            for (int i = 0; i < n; i++) {
                glm::vec3 at = c.player.position + f * dist + side * (float)((i - (n - 1) * 0.5f) * 8.0f) + glm::vec3(0, 2, 0);
                c.npcs->spawn(a[0], at, -f);
            }
            c.log("spawned " + std::to_string(n) + " " + a[0]);
        });
    reg.add("rep", "rep [faction] [value]",
        "your reputation with the factions (npc/Factions.h); set one",
        [](C& c, const Args& a) {
            if (a.size() >= 2) {
                if (gFactions.find(a[0]) < 0) { c.log("no faction " + a[0]); return; }
                c.player.reputation[a[0]] = std::clamp((float)std::atof(a[1].c_str()), -100.0f, 100.0f);
            }
            std::string l;
            for (int i = 0; i < gFactions.count(); i++) {
                const auto* f = gFactions.get(i);
                auto it = c.player.reputation.find(f->id);
                float v = it == c.player.reputation.end() ? f->playerStanding : it->second;
                char b[64]; std::snprintf(b, sizeof(b), "%s %.0f%s", f->id.c_str(), v,
                                          gFactions.hostile(v) ? " (hostile)" : gFactions.friendly(v) ? " (friendly)" : "");
                l += (l.empty() ? "" : ", ") + std::string(b);
            }
            c.log(l);
        });
    reg.add("smite", "smite [radius]",
        "kill every hostile NPC near you, as if you did it (tests encounter reviews)",
        [](C& c, const Args& a) {
            if (!c.sim) { c.log("no world"); return; }
            float r = a.empty() ? 60.0f : (float)std::atof(a[0].c_str());
            int n = 0;
            std::vector<EntityId> ids;
            for (auto& x : c.sim->npcs().all())
                if (!x->dead() && glm::length(x->position - c.player.position) < r && c.sim->hostile(*x, c.player)) ids.push_back(x->uuid);
            for (EntityId id : ids)
                if (Human* h = c.sim->find(id)) { c.sim->toolHit(c.player.uuid, id, 9999.0f, 999.0f, h->bodyPoint(0.6f), h->facing * -1.0f); n++; }
            c.log("smote " + std::to_string(n));
        });
    reg.add("outline", "outline <effect> [me|npc]",
        "flash an outline effect (data/registry/outline.toml: hurt, blocked, guard_break …) on you or the nearest NPC",
        [](C& c, const Args& a) {
            if (a.empty()) { c.log("outline <effect> [me|npc]"); return; }
            if (!gOutlineFx.effect(a[0])) { c.log("no outline effect '" + a[0] + "'"); return; }
            EntityId who = c.player.uuid;
            if (a.size() > 1 && a[1] == "npc" && c.sim) {
                float best = 1e9f;
                who = 0;
                for (auto& x : c.sim->npcs().all()) {
                    float d = glm::length(x->position - c.player.position);
                    if (!x->dead() && d < best) { best = d; who = x->uuid; }
                }
                if (!who) { c.log("no NPC near"); return; }
            }
            // (charge normally comes from an attack's wind-up; here it plays as a flash)
            gOutlineFx.start(who, a[0]);
            c.log("outline " + a[0]);
        });
    reg.add("portalgun", "portalgun [oneway|twoway|clear]",
        "the portal gun (dev_portal_gun; game/PortalGun.h): its ends; one way (only blue opens) or two way; take them away",
        [](C& c, const Args& a) { if (c.portalGun) c.portalGun(a); else c.log("no world open"); });
    reg.add("portal", "portal [open <dungeon>|close|go|time <minutes>|tp]",
        "the realm's gates and instances (game/Portals.h): what's open; open one (crypt); close it; go through now; set its clock; tp to the gate",
        [](C& c, const Args& a) { if (c.portal) c.portal(a); else c.log("no portals here"); });
    reg.add("dungeon", "dungeon [tp <n>|portal|boss|stair] | map",
        "a Dungeon world (docs/dungeons.md): how it was grown; tp to a tile on the main path (#n), the portal, the stair or the boss; map: the layout",
        [](C& c, const Args& a) {
            using namespace dungeon;
            const DungeonGenerator* g = dynamic_cast<const DungeonGenerator*>(c.gen);
            if (!g) { c.log("not a Dungeon world (New World > Type: Dungeon)"); return; }
            if (!g->generated()) { c.log("the dungeon didn't grow: " + g->dungeonLayout().failure + " " + g->tileReport()); return; }
            const Layout& L = g->dungeonLayout();
            if (a.empty()) {
                c.log("crypt, seed " + std::to_string(L.seed) + " (" + std::to_string(L.attemptsUsed) + " attempts): " + std::to_string(L.tiles.size()) +
                      " tiles, " + std::to_string(L.mainRooms) + " rooms on the main path (" + std::to_string(L.mainPath.size()) + " tiles long), " +
                      std::to_string(L.sideRooms) + " side rooms, " + std::to_string(L.sealed.size()) + " doors walled up. dungeon tp boss, debug dungeon");
                return;
            }
            if (a[0] == "map") {
                int levels = 0;
                for (auto& t : L.tiles) levels = std::max(levels, t.level + 1);
                glm::ivec3 lo(1 << 20), hi(-(1 << 20));
                for (auto& t : L.tiles) { lo = glm::min(lo, t.at); hi = glm::max(hi, t.at + L.variantOf(t).cells); }
                for (int lv = 0; lv < levels; lv++) {
                    c.log("level " + std::to_string(lv + 1) + "  (P portal, R room, # path, s side, S stair, B boss, x dead end)");
                    for (int z = lo.z; z < hi.z; z++) {
                        std::string row;
                        for (int x = lo.x; x < hi.x; x++) {
                            char ch = '.';
                            for (int y = lo.y; y < hi.y; y++) {
                                int t = L.tileAt({ x, y, z });
                                if (t < 0 || L.tiles[(size_t)t].level != lv) continue;
                                const std::string& r = L.tiles[(size_t)t].role;
                                ch = r == "portal" ? 'P' : r == "boss" ? 'B' : r == "stair" ? 'S' : r == "main" ? 'R' : r == "side" ? 's' :
                                     r == "cap" ? 'x' : L.tiles[(size_t)t].pathIndex >= 0 ? '#' : 'o';
                            }
                            row += ch;
                        }
                        c.log(row);
                    }
                }
                return;
            }
            if (a[0] == "tp" && a.size() > 1) {
                int which = -1;
                if (a[1] == "portal") which = L.portal;
                else if (a[1] == "boss") which = L.boss;
                else if (a[1] == "stair") { for (size_t i = 0; i < L.tiles.size(); i++) if (L.tiles[i].role == "stair") which = (int)i; }
                else { int n = std::atoi(a[1].c_str()); if (n >= 0 && n < (int)L.mainPath.size()) which = L.mainPath[(size_t)n]; }
                if (which < 0) { c.log("no such tile (0.." + std::to_string(L.mainPath.size() - 1) + ", portal, stair, boss)"); return; }
                const PlacedTile& t = L.tiles[(size_t)which];
                const TileVariant& v = L.variantOf(t);
                glm::ivec3 w = g->worldOf(t);
                // a spot on the floor that's open
                glm::vec3 to = glm::vec3(w) + glm::vec3(v.vol.size.x * 0.5f, FLOOR, v.vol.size.z * 0.5f);
                for (int r = 0; r < 20; r++) {
                    glm::ivec3 q = glm::ivec3(v.vol.size.x / 2 + (r % 5) * 3 - 6, FLOOR + 1, v.vol.size.z / 2 + (r / 5) * 3 - 6);
                    if (v.vol.at(q.x, q.y, q.z) == BLOCK_AIR && v.vol.at(q.x, q.y + 12, q.z) == BLOCK_AIR) { to = glm::vec3(w + q) + glm::vec3(0.5f, -1.0f, 0.5f); break; }
                }
                c.player.position = to;
                c.player.velocity = glm::vec3(0.0f);
                c.log("to " + v.tile->name + " (" + t.role + (t.pathIndex >= 0 ? ", #" + std::to_string(t.pathIndex) : "") + ")");
                return;
            }
            c.log("dungeon | dungeon map | dungeon tp <n|portal|stair|boss>");
        });
    reg.add("tile", "tile new|edit|socket|marker|kind|weight|min|max|rotate|theme|save|showcase|starter|list|done …",
        "build dungeon tiles (dungeon/Workshop.h, docs/dungeons.md): tile new <name> <w> <h> <d> [kind] (half cells of 16)",
        [](C& c, const Args& a) {
            using namespace dungeon;
            Workshop& ws = gWorkshop;
            std::string sub = a.empty() ? "help" : a[0];
            WorldEditor* ed = c.dev.worldEditor();
            auto needWorld = [&]() { if (!ed) { c.log("no world"); return false; } return true; };
            // a plot in front of you, on the 16 grid, its floor where you stand
            auto plotSpot = [&](glm::ivec3 size) {
                glm::vec3 f(c.player.facing.x, 0.0f, c.player.facing.z);
                f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
                glm::vec3 centre = c.player.position + f * (8.0f + 0.5f * std::max(size.x, size.z));
                glm::ivec3 o((int)std::floor((centre.x - size.x * 0.5f) / HALF) * HALF, (int)std::floor(c.player.position.y) - FLOOR,
                             (int)std::floor((centre.z - size.z * 0.5f) / HALF) * HALF);
                return o;
            };
            auto stamp = [&](const VoxelVolume& vol, glm::ivec3 origin, const char* label) {
                VoxelVolume v = vol; v.anchor = glm::ivec3(0);
                EditBatch b = WorldEditor::paste(v, origin, false, EditSource::Command);
                b.label = label;
                ed->submit(std::move(b));
            };
            auto doorBox = [&](glm::ivec3 cell, int face, glm::ivec3& lo, glm::ivec3& hi) {
                glm::ivec3 o = cell * HALF, s = ws.size();
                int y0 = o.y + FLOOR, y1 = y0 + DOOR_H, a0 = (HALF - DOOR_W) / 2, a1 = a0 + DOOR_W, d = WALL + 1;
                switch (face) {
                case North: lo = { o.x + a0, y0, 0 }; hi = { o.x + a1, y1, d }; return true;
                case South: lo = { o.x + a0, y0, s.z - d }; hi = { o.x + a1, y1, s.z }; return true;
                case West:  lo = { 0, y0, o.z + a0 }; hi = { d, y1, o.z + a1 }; return true;
                case East:  lo = { s.x - d, y0, o.z + a0 }; hi = { s.x, y1, o.z + a1 }; return true;
                case Up:    lo = { o.x + 2, s.y - WALL, o.z + 2 }; hi = { o.x + HALF - 2, s.y, o.z + HALF - 2 }; return true;
                case Down:  lo = { o.x + 2, 0, o.z + 2 }; hi = { o.x + HALF - 2, FLOOR, o.z + HALF - 2 }; return true;
                }
                return false;
            };
            if (sub == "help") {
                c.log("tile new <name> <w> <h> <d> [kind] | edit <name> | socket [wall|door|open|shaft] | marker <key> <value>");
                c.log("tile kind|weight|min|max|rotate|theme <v> | save [name] | showcase [theme] | starter [theme] [over] | list | done");
                return;
            }
            if (sub == "starter") {
                std::string theme = a.size() > 1 ? a[1] : "crypt", report;
                writeStarterTiles(theme, a.size() > 2 && a[2] == "over", report);
                c.log(report);
                return;
            }
            if (sub == "list") {
                TileSet set; std::string why;
                int n = set.load(a.size() > 1 ? a[1] : (ws.editing ? ws.info.theme : "crypt"), &why);
                std::string line;
                for (auto& t : set.all())
                    line += (line.empty() ? "" : ", ") + t->name + " (" + t->info.kind + " " + std::to_string(t->info.cells.x) + "x" +
                            std::to_string(t->info.cells.y) + "x" + std::to_string(t->info.cells.z) + ")";
                c.log(std::to_string(n) + " tiles, " + std::to_string(set.allVariants().size()) + " ways to place them: " + line);
                if (!why.empty()) c.log(why);
                return;
            }
            if (sub == "new") {
                if (!needWorld()) return;
                if (a.size() < 5) { c.log("tile new <name> <w> <h> <d> [kind]   (half cells of 16 voxels: a corridor 1 2 2, a room 4 2 4)"); return; }
                glm::ivec3 cells(std::atoi(a[2].c_str()), std::atoi(a[3].c_str()), std::atoi(a[4].c_str()));
                if (glm::any(glm::lessThan(cells, glm::ivec3(1))) || glm::any(glm::greaterThan(cells, glm::ivec3(16)))) { c.log("sizes 1 to 16"); return; }
                TileBuilder b(a[1], ws.editing ? ws.info.theme : "crypt", a.size() > 5 ? a[5] : "room", cells);
                b.hollow();
                ws.editing = true; ws.name = a[1]; ws.info = b.t.info;
                ws.origin = plotSpot(b.size());
                stamp(b.t.vol, ws.origin, "tile new");
                c.log("tile " + ws.name + ": " + std::to_string(b.size().x) + "x" + std::to_string(b.size().y) + "x" + std::to_string(b.size().z) +
                      " voxels at " + std::to_string(ws.origin.x) + " " + std::to_string(ws.origin.y) + " " + std::to_string(ws.origin.z) +
                      ". Look at an edge and: tile socket door. debug tiles shows the grid");
                return;
            }
            if (sub == "edit") {
                if (!needWorld()) return;
                if (a.size() < 2) { c.log("tile edit <name>"); return; }
                std::string theme = a.size() > 2 ? a[2] : (ws.editing ? ws.info.theme : "crypt"), why;
                Tile t;
                if (!TileSet::loadTile(std::filesystem::path(TileSet::dirFor(theme)) / (a[1] + ".toml"), t, why)) { c.log(a[1] + ": " + why); return; }
                ws.editing = true; ws.name = t.name; ws.info = t.info;
                ws.origin = plotSpot(t.vol.size);
                stamp(t.vol, ws.origin, "tile edit");
                c.log("editing " + t.name + " (" + t.info.kind + "): tile save when done");
                return;
            }
            if (sub == "showcase") {
                if (!needWorld()) return;
                TileSet set; std::string why;
                int n = set.load(a.size() > 1 ? a[1] : "crypt", &why);
                if (!n) { c.log("no tiles (tile starter writes the starter set)"); return; }
                glm::vec3 f(c.player.facing.x, 0.0f, c.player.facing.z);
                f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
                glm::vec3 side(-f.z, 0.0f, f.x);
                ws.showcase.clear();
                glm::vec3 at = c.player.position + f * 20.0f;
                float along = 0.0f;
                for (auto& t : set.all()) {
                    glm::vec3 p = at + side * along;
                    glm::ivec3 o((int)std::floor(p.x / HALF) * HALF, (int)std::floor(c.player.position.y) - FLOOR, (int)std::floor(p.z / HALF) * HALF);
                    stamp(t->vol, o, "tile showcase");
                    ws.showcase.push_back({ o, t->name, t->info });
                    along += std::max(t->vol.size.x, t->vol.size.z) + 16.0f;
                }
                c.log("showcase: " + std::to_string(n) + " tiles in a row to your side (debug tiles labels them)");
                return;
            }
            if (sub == "done") { ws.editing = false; c.log("stopped editing (the plot stays in the world)"); return; }
            if (!ws.editing) { c.log("not editing a tile: tile new or tile edit first"); return; }
            if (sub == "socket") {
                if (!needWorld()) return;
                glm::ivec3 cell; int face;
                if (!c.target.hit || !ws.faceAt(c.target.blockPos, cell, face)) { c.log("look at an outside edge of the tile"); return; }
                std::string now = ws.info.socketAt(cell, face), type;
                if (a.size() > 1) type = a[1];
                else {
                    const auto& types = socketTypes();
                    auto it = std::find(types.begin(), types.end(), now);
                    type = types[(size_t)((it - types.begin()) + 1) % types.size()];
                    if (face < 4 && type == "shaft") type = "wall";
                }
                if (std::find(socketTypes().begin(), socketTypes().end(), type) == socketTypes().end()) { c.log("wall, door, open or shaft"); return; }
                ws.info.setSocket(cell, face, type);
                glm::ivec3 lo, hi;
                if (doorBox(cell, face, lo, hi)) {
                    EditBatch b; b.source = EditSource::Command; b.label = "tile socket";
                    uint16_t wallB = gRegistry.getIDByName("stone_bricks");
                    if (type == "open" && face < 4) { lo.y = cell.y * HALF + FLOOR; hi.y = std::min((cell.y + 1) * HALF, ws.size().y - WALL); if (face == North || face == South) { lo.x = cell.x * HALF; hi.x = lo.x + HALF; } else { lo.z = cell.z * HALF; hi.z = lo.z + HALF; } }
                    for (int y = lo.y; y < hi.y; y++) for (int z = lo.z; z < hi.z; z++) for (int x = lo.x; x < hi.x; x++)
                        b.set(ws.origin.x + x, ws.origin.y + y, ws.origin.z + z, type == "wall" ? wallB : BLOCK_AIR);
                    ed->submit(std::move(b));
                }
                c.log(std::string("socket ") + dirName(face) + " of cell " + std::to_string(cell.x) + " " + std::to_string(cell.y) + " " +
                      std::to_string(cell.z) + ": " + type);
                return;
            }
            if (sub == "marker") {
                if (!c.target.hit || !ws.inside(c.target.blockPos)) { c.log("look at a marker block in the tile"); return; }
                std::string kind = markerKindOf((uint16_t)c.target.blockID);
                if (kind.empty()) { c.log("that's not a marker block (marker_spawner, marker_chest …)"); return; }
                glm::ivec3 at = c.target.blockPos - ws.origin;
                Marker* m = ws.info.markerAt(at);
                if (!m) { ws.info.markers.push_back({ at, kind, {} }); m = &ws.info.markers.back(); }
                m->kind = kind;
                if (a.size() < 3) {
                    std::string s;
                    for (auto& [k, v] : m->props) s += " " + k + "=" + v;
                    c.log(kind + " marker:" + (s.empty() ? " no settings (spawner: spawn <type>, count <lo hi>, rule enter|sight|boss|wave)" : s));
                    return;
                }
                std::string v = a[2];
                for (size_t i = 3; i < a.size(); i++) v += " " + a[i];
                m->props[a[1]] = v;
                c.log(kind + " marker: " + a[1] + " = " + v);
                return;
            }
            if (sub == "kind" && a.size() > 1)   { ws.info.kind = a[1]; c.log("kind " + a[1]); return; }
            if (sub == "theme" && a.size() > 1)  { ws.info.theme = a[1]; c.log("theme " + a[1]); return; }
            if (sub == "weight" && a.size() > 1) { ws.info.weight = (float)std::atof(a[1].c_str()); c.log("weight " + a[1]); return; }
            if (sub == "min" && a.size() > 1)    { ws.info.minCount = std::atoi(a[1].c_str()); c.log("min " + a[1]); return; }
            if (sub == "max" && a.size() > 1)    { ws.info.maxCount = std::atoi(a[1].c_str()); c.log("max " + a[1]); return; }
            if (sub == "rotate" && a.size() > 1) { ws.info.rotate = a[1] != "off" && a[1] != "0" && a[1] != "false"; c.log(std::string("rotate ") + (ws.info.rotate ? "on" : "off")); return; }
            if (sub == "save") {
                if (!needWorld()) return;
                Tile t;
                t.name = a.size() > 1 ? a[1] : ws.name;
                t.info = ws.info;
                t.vol = ed->copy(ws.origin, ws.origin + ws.size() - 1);
                t.vol.anchor = glm::ivec3(0);
                // markers: as the blocks are now (one moved or removed is updated)
                std::vector<Marker> found;
                for (int y = 0; y < t.vol.size.y; y++) for (int z = 0; z < t.vol.size.z; z++) for (int x = 0; x < t.vol.size.x; x++) {
                    std::string k = markerKindOf(t.vol.at(x, y, z));
                    if (k.empty() || k == "secret") continue;
                    Marker m; if (const Marker* old = ws.info.markerAt({ x, y, z })) m = *old;
                    m.at = { x, y, z }; m.kind = k; found.push_back(m);
                }
                t.info.markers = found;
                std::string why;
                if (!TileSet::saveTile(t, TileSet::dirFor(t.info.theme), why)) { c.log("couldn't save: " + why); return; }
                ws.name = t.name;
                c.log("saved " + TileSet::dirFor(t.info.theme) + "/" + t.name + " (" + std::to_string(t.info.sockets.size()) + " sockets, " +
                      std::to_string(found.size()) + " markers)");
                return;
            }
            c.log("tile: unknown '" + sub + "' (tile help)");
        });
    reg.add("particle", "particle <emitter> [count x]",
        "fire a particle burst (data/registry/particles.toml: hit_spark, block_sparks, land_dust …) in front of you; alone, lists them",
        [](C& c, const Args& a) {
            if (a.empty()) {
                std::string ids;
                for (const char* id : { "hit_spark", "hit_puff", "hit_slime", "block_sparks", "block_wood", "parry_flash", "guard_break",
                                        "poise_break", "death_dust", "death_slime", "step_dust", "dodge_dust", "land_dust",
                                        "impact_flash", "wall_dust", "perfect_dodge" })
                    if (gParticles.emitter(id)) ids += std::string(ids.empty() ? "" : ", ") + id;
                c.log(std::to_string(gParticles.emitterCount()) + " emitters: " + ids + "   (particle <id> [count x])");
                return;
            }
            if (!gParticles.emitter(a[0])) { c.log("no emitter '" + a[0] + "' (F5 reloads the file)"); return; }
            if (gSettings.particles <= 0) { c.log("particles are off (Settings > Graphics)"); return; }
            glm::vec3 f(c.player.facing.x, 0.0f, c.player.facing.z);
            f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
            float scale = a.size() > 1 ? std::max(0.1f, (float)std::atof(a[1].c_str())) : 1.0f;
            int n = gParticles.burst(a[0], c.player.position + f * 8.0f + glm::vec3(0, 8, 0), f, 1.0f, scale);
            c.log("particle " + a[0] + ": " + std::to_string(n));
        });
    reg.add("rumble", "rumble [low high seconds]",
        "shake the controller (input/Rumble.h); alone, a test thud",
        [](C& c, const Args& a) {
            float lo = a.size() > 0 ? (float)std::atof(a[0].c_str()) : 0.8f;
            float hi = a.size() > 1 ? (float)std::atof(a[1].c_str()) : 0.4f;
            float t  = a.size() > 2 ? (float)std::atof(a[2].c_str()) : 0.3f;
            if (gSettings.rumble <= 0) { c.log("rumble is off (Settings > General > Controller rumble)"); return; }
            rumble::gRumble.kick(lo, hi, t);
#ifdef _WIN32
            c.log("rumble " + std::to_string(lo).substr(0, 4) + " " + std::to_string(hi).substr(0, 4) + " for " + std::to_string(t).substr(0, 4) + " s (an XInput pad)");
#else
            c.log("rumble: only on Windows (XInput)");
#endif
        });
    reg.add("roaming", "roaming [on|off|now|clear|reload|groups]",
        "roaming enemy spawns (data/registry/roaming.toml): a world setting; alone, what it's doing and why nothing came",
        [](C& c, const Args& a) {
            if (!c.sim || !c.npcs || !c.sim->hasWorld()) { c.log("needs a world"); return; }
            Roaming& r = c.sim->roaming;
            if (!r.biomeAt) { c.log("on a server: its console sets roaming (roaming on|off)"); return; }
            const float hour = c.sim->day.time * 24.0f;
            std::string what = a.empty() ? "" : a[0];
            if (what == "on" || what == "off") {
                r.enabled = what == "on";
                r.resetTimers();
                bool saved = c.info && c.info->setRoamingSpawns(r.enabled);
                c.log(std::string("roaming spawns ") + (r.enabled ? "on" : "off") + (saved ? " (saved with the world)" : " (couldn't save the setting)"));
                return;
            }
            if (what == "reload") {
                bool ok = gRoaming.load();
                c.log(ok ? "roaming.toml: " + std::to_string(gRoaming.groups.size()) + " groups" : "roaming.toml didn't load (see the log)");
                return;
            }
            if (what == "clear") { c.log(std::to_string(r.clear(*c.npcs)) + " roaming NPCs removed"); return; }
            if (what == "groups") {
                if (!gRoaming.loaded) gRoaming.load();
                for (const RoamGroupDef& g : gRoaming.groups) {
                    std::string types;
                    for (auto& [t, w] : g.npcs) types += (types.empty() ? "" : " ") + t;
                    std::string bs;
                    for (auto& b : g.biomes) bs += (bs.empty() ? "" : " ") + b;
                    const char* tm = g.time == RoamGroupDef::Time::Day ? "day" : g.time == RoamGroupDef::Time::Night ? "night" : "any time";
                    c.log(g.id + ": " + types + " x" + std::to_string(g.minSize) + "-" + std::to_string(g.maxSize) + ", " + tm + ", " + bs);
                }
                return;
            }
            Roaming::Who me{ c.player.uuid, c.player.position, c.player.facing };
            if (what == "now") { c.log(r.forceFor(c.sim->physicsRef(), *c.npcs, me, c.sim->roamers(), hour)); return; }
            if (!what.empty()) { c.log("roaming [on|off|now|clear|reload|groups]"); return; }
            // what it's doing
            int biome = r.biomeAt ? r.biomeAt((int)std::floor(me.pos.x), (int)std::floor(me.pos.z)) : -1;
            c.log(std::string("roaming spawns ") + (r.enabled ? "on" : "off") + ", " + (r.isNight(hour) ? "night" : "day") +
                  ", here: " + (biome >= 0 && biome < island::BIOME_COUNT ? RoamingTable::key(biome) : std::string("-")));
            c.log(std::to_string(r.nearCount(*c.npcs, me.pos)) + " roaming near you, " + std::to_string(r.alive(*c.npcs)) + " in the world; " +
                  std::to_string(r.groupsMade) + " groups made (" + std::to_string(r.npcsMade) + " NPCs), " + std::to_string(r.npcsGone) + " gone");
            if (const Roaming::PlayerState* ps = r.stateFor(me.id)) c.log("last try: " + (ps->why.empty() ? std::string("none yet") : ps->why));
            if (!r.lastGroup.empty()) c.log("last group: " + r.lastGroup);
        });
    reg.add("villages", "villages", "the world's village sites (built when a player first comes near) and how far each is",
        [](C& c, const Args&) {
            if (!c.gen) { c.log("needs a world"); return; }
            auto sites = c.gen->villageSites();
            if (sites.empty()) { c.log("this world has no village sites (the village command builds one anywhere)"); return; }
            for (const auto& s : sites) {
                float d = glm::length(glm::vec2(s.at.x - c.player.position.x, s.at.y - c.player.position.z));
                char b[160];
                std::snprintf(b, sizeof b, "%s at %d %d, %.0f ft away   (tp %d 400 %d)", s.name.c_str(), s.at.x, s.at.y, d / 3.0f, s.at.x, s.at.y);
                c.log(b);
            }
        });
    reg.add("spawners", "spawners", "list spawner types (structures.toml) and the spawners placed in this world",
        [](C& c, const Args&) {
            std::string types;
            for (uint16_t id : gStructures.all())
                if (gStructures.get(id)->kind == StructureKind::Spawner) types += (types.empty() ? "" : ", ") + gStructures.get(id)->name;
            c.log("spawner types: " + (types.empty() ? std::string("none") : types) + "   (place <name>)");
            if (!c.sim) return;
            if (c.sim->spawners().all().empty()) { c.log("none placed"); return; }
            for (const SpawnerState& s : c.sim->spawners().all()) {
                const StructureType* t = gStructures.get(s.type);
                char b[160];
                std::snprintf(b, sizeof(b), "%s at %.0f %.0f %.0f: %d/%d alive, %s, %d made", t ? t->name.c_str() : "?",
                              s.at.x, s.at.y, s.at.z, s.alive, t ? t->spawn.maxAlive : 0, s.active ? "awake" : "asleep", s.spawned);
                c.log(b);
            }
        });
    reg.add("despawn", "despawn", "remove every NPC",
        [](C& c, const Args&) {
            if (c.npcs) c.npcs->clear();
            c.log("enemies removed");
        });
    reg.add("debug", "debug [layer] [on|off]",
        "the debug overlay (F3); layers: bodies ragdoll combat labels props sight world",
        [](C& c, const Args& a) {
            if (a.empty()) { gDebug.on = !gDebug.on; c.log(std::string("overlay ") + (gDebug.on ? "on: " : "off: ") + gDebug.list()); return; }
            bool* l = gDebug.find(a[0]);
            if (!l) { c.log("layers: " + gDebug.list()); return; }
            *l = a.size() > 1 ? (a[1] == "on") : !*l;
            gDebug.on = true;
            c.log(a[0] + (*l ? " on" : " off"));
        });
    // Checks the crash report (app/CrashReport.h): really crashes the game
    reg.add("crashtest", "crashtest now", "crash on purpose, to check the crash report in logs/",
        [](C& c, const Args& a) {
            if (a.empty() || a[0] != "now") { c.log("this really crashes the game: type  crashtest now"); return; }
            c.log("crashing...");
            volatile int* p = nullptr;
            *p = 1;
        });
    reg.add("heal", "heal", "full health and stamina",
        [](C& c, const Args&) {
            c.player.health = c.player.maxHealth;
            c.player.stamina = c.player.maxStamina;
            c.log("healed");
        });
    reg.add("mass", "mass", "how heavy your body and gear are",
        [](C& c, const Args&) {
            const BodyMass& m = c.player.body;
            char b[128];
            auto lb = [](float kg) { return kg * 2.2046f; };
            std::snprintf(b, sizeof(b), "body %.1f kg (%.0f lb) + gear %.1f kg (%.0f lb) = %.1f kg (%.0f lb)",
                          m.bodyKg, lb(m.bodyKg), m.gearKg, lb(m.gearKg), m.total(), lb(m.total()));
            c.log(b);
            const Inventory& inv = c.player.activeInventory();
            for (int s = 0; s < WORN_SLOTS; s++)
                if (!inv.worn((WornSlot)s).empty()) {
                    const ItemDef& d = gItems.get(inv.worn((WornSlot)s).itemID);
                    std::snprintf(b, sizeof(b), "  %s: %s %.1f kg", wornSlotKey((WornSlot)s), d.name.c_str(), d.weight);
                    c.log(b);
                }
            if (!inv.weapon().empty()) {
                const ItemDef& d = gItems.get(inv.weapon().itemID);
                std::snprintf(b, sizeof(b), "  weapon (%s): %s %.1f kg",
                              c.player.combatStance ? "in hand" : "on back", d.name.c_str(), d.weight);
                c.log(b);
            }
            if (!c.player.combatStance && !inv.held().empty()) {
                const ItemDef& d = gItems.get(inv.held().itemID);
                std::snprintf(b, sizeof(b), "  in hand: %s %.1f kg", d.name.c_str(), d.weight);
                c.log(b);
            }
        });
    reg.add("wear", "wear <item> | wear off <slot|all>",
        "put on worn gear (head torso hands legs feet back)",
        [](C& c, const Args& a) {
            Inventory& inv = c.player.activeInventory();
            if (a.empty()) {
                std::string t = "worn gear:";
                for (const char* n : { "leather_cap", "iron_helmet", "leather_jerkin", "chainmail", "plate_cuirass",
                                       "iron_gauntlets", "iron_greaves", "leather_boots", "travel_pack" })
                    if (gItems.find(n)) { t += " "; t += n; }
                c.log("usage: wear <item> | wear off <slot|all>");
                c.log(t);
                return;
            }
            if (a[0] == "off") {
                std::string w = a.size() > 1 ? a[1] : "all";
                if (w == "all") { for (int s = 0; s < WORN_SLOTS; s++) inv.takeOff((WornSlot)s); c.log("took everything off"); return; }
                int s = wornSlotFromKey(w.c_str());
                if (s < 0) { c.log("no slot '" + w + "' (head torso hands legs feet back)"); return; }
                inv.takeOff((WornSlot)s);
                c.log("took off " + w);
                return;
            }
            uint16_t id = gItems.find(a[0]);
            if (!id) { c.log("no item '" + a[0] + "'"); return; }
            if (!gItems.get(id).is(IC_WORN)) { c.log(a[0] + " is not worn gear"); return; }
            if (!inv.wear(id)) { c.log("no room in the inventory for " + a[0]); return; }
            const ItemDef& d = gItems.get(id);
            char b[96];
            std::snprintf(b, sizeof(b), "wearing %s on %s (%.1f kg)", d.name.c_str(), wornSlotKey((WornSlot)d.worn), d.weight);
            c.log(b);
        });
    reg.add("ragdoll", "ragdoll [active on|off]",
        "ragdoll state; 'active on' simulates it all the time (dev)",
        [](C& c, const Args& a) {
            Ragdoll& rd = c.player.ragdoll;
            if (a.size() >= 2 && a[0] == "active") {
                rd.alwaysActive = a[1] == "on";
                c.log(std::string("ragdoll always active: ") + (rd.alwaysActive ? "on" : "off"));
                return;
            }
            const char* st = rd.state() == Ragdoll::State::Animated ? "animated"
                           : rd.state() == Ragdoll::State::Reacting ? "reacting"
                           : rd.state() == Ragdoll::State::Limp ? "limp" : "getting up";
            char b[96];
            std::snprintf(b, sizeof(b), "ragdoll %s, strength %.2f, %.1f kg%s", st, rd.strengthNow(),
                          rd.bodyMass(), rd.alwaysActive ? ", always active" : "");
            c.log(b);
        });

    reg.add("heal", "heal", "restore full health",
        [](C& c, const Args&) {
            c.player.health = c.player.maxHealth;
            c.log("healed");
        });
    reg.add("where", "where", "show your position and the block you look at",
        [](C& c, const Args&) {
            c.log("feet " + DevTools::str(glm::ivec3(glm::floor(c.player.position))));
            if (c.target.hit)
                c.log("looking at " + DevTools::blockName((uint16_t)c.target.blockID) +
                      " at " + DevTools::str(c.target.blockPos));
        });
    // ── Finding things ────────────────────────────────────
    reg.add("river", "river", "go to the nearest running water (to look at it flow)",
        [](C& c, const Args&) {
            if (!c.gen) { c.log("no world"); return; }
            glm::ivec3 me = glm::ivec3(glm::floor(c.player.position));
            for (int r = 0; r <= 2000; r += 8)
                for (int k = 0; k < std::max(1, r * 6 / 8); k++) {
                    float a = 6.2832f * k / std::max(1, r * 6 / 8);
                    int x = me.x + (int)(std::cos(a) * r), z = me.z + (int)(std::sin(a) * r);
                    if (glm::length(c.gen->flowAt(x, z)) < 0.05f) continue;
                    TerrainColumn col = c.gen->column(x, z);
                    int y = std::max(col.waterLevel, col.ground) + 1;
                    // stand on the bank a little to one side
                    c.player.position = glm::vec3(x + 0.5f, (float)y + 2.0f, z + 0.5f);
                    c.player.velocity = glm::vec3(0.0f);
                    c.player.ragdoll.reset();
                    c.log("running water at " + DevTools::str(glm::ivec3(x, y, z)));
                    return;
                }
            c.log("no running water within 2000 voxels");
        });
    reg.add("savegame", "savegame", "save the world now",
        [](C& c, const Args&) { c.saveWorld(); });

    // ── Lists ─────────────────────────────────────────────
    reg.add("blocks", "blocks", "list block names",
        [](C& c, const Args&) {
            std::string line;
            for (uint16_t id : gItems.inCategory(IC_BLOCK)) {
                const std::string& n = gItems.get(id).name;
                if (line.size() + n.size() > 70) { c.log(line); line.clear(); }
                line += n + "  ";
            }
            if (!line.empty()) c.log(line);
        });
    reg.add("items", "items [tool|weapon|dev]", "list item names",
        [](C& c, const Args& a) {
            uint32_t cat = IC_TOOL | IC_WEAPON | IC_DEV;
            if (!a.empty()) {
                if      (a[0] == "tool")   cat = IC_TOOL;
                else if (a[0] == "weapon") cat = IC_WEAPON;
                else if (a[0] == "dev")    cat = IC_DEV;
            }
            std::string line;
            for (uint16_t id : gItems.inCategory(cat)) {
                const std::string& n = gItems.get(id).name;
                if (line.size() + n.size() > 70) { c.log(line); line.clear(); }
                line += n + "  ";
            }
            if (!line.empty()) c.log(line);
        });
}
