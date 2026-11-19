#pragma once
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
            if (!a.empty()) {
                const std::string& w = a[0];
                if (w == "day" || w == "morning") gClock.time = 0.32f;
                else if (w == "noon")     gClock.time = 0.5f;
                else if (w == "dusk" || w == "sunset") gClock.time = 0.745f;
                else if (w == "night")    gClock.time = 0.88f;
                else if (w == "midnight") gClock.time = 0.0f;
                else if (w == "dawn" || w == "sunrise") gClock.time = 0.255f;
                else if (w == "freeze")   gClock.frozen = true;
                else if (w == "run")      gClock.frozen = false;
                else if (w == "length" && a.size() > 1) gClock.dayLengthSeconds = std::max(0.5f, (float)std::atof(a[1].c_str())) * 60.0f;
                else gClock.time = std::fmod(std::fabs((float)std::atof(w.c_str())) / 24.0f, 1.0f);
            }
            int mins = (int)(gClock.time * 24.0f * 60.0f);
            char b[64];
            std::snprintf(b, sizeof(b), "time %02d:%02d%s (day %d min)", mins / 60, mins % 60,
                          gClock.frozen ? " frozen" : "", (int)(gClock.dayLengthSeconds / 60.0f));
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
    reg.add("roaming", "roaming [on|off|now|clear|reload|groups]",
        "roaming enemy spawns (data/registry/roaming.toml): a world setting; alone, what it's doing and why nothing came",
        [](C& c, const Args& a) {
            if (!c.sim || !c.npcs || !c.sim->hasWorld()) { c.log("needs a world"); return; }
            Roaming& r = c.sim->roaming;
            if (!r.biomeAt) { c.log("on a server: its console sets roaming (roaming on|off)"); return; }
            const float hour = gClock.time * 24.0f;
            std::string what = a.empty() ? "" : a[0];
            if (what == "on" || what == "off") {
                r.enabled = what == "on";
                r.resetTimers();
                bool saved = gWorldSave.setRoamingSpawns(r.enabled);
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
