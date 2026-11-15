#pragma once
#include "DevTools.h"
#include "StructureRegistry.h"
#include "../world/StructureManager.h"
#include "GameMode.h"
#include "ItemRegistry.h"
#include "../entity/Player.h"
#include "../physics/IPhysics.h"
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
            if (d.is(IC_WEAPON) && !d.is(IC_TOOL)) {
                inv.setSlot(-1, id, 1);
                c.log(d.displayName + " put in the " +
                      (dev ? "dev" : "player") + " weapon slot");
                return;
            }
            int left = inv.add(id, count);
            c.log("gave " + std::to_string(count - left) + " " + d.displayName +
                  " to the " + (dev ? "dev" : "player") + " inventory" +
                  (left ? "  (" + std::to_string(left) + " did not fit)" : ""));
        });
    reg.add("clear", "clear [player|dev]", "empty an inventory",
        [](C& c, const Args& a) {
            bool dev = a.empty() ? c.player.mode != GameMode::Survival
                                 : a[0] == "dev";
            Inventory& inv = dev ? c.player.devInventory : c.player.inventory;
            for (auto& s : inv.toolbar) s.clear();
            inv.weapon.clear();
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
            c.log("moved to " + DevTools::str(p));
        });
    reg.add("mode", "mode <spectator|dev|survival>", "change game mode",
        [](C& c, const Args& a) {
            if (a.empty()) { c.log(std::string("mode: ") + c.player.rules().name); return; }
            GameMode m = gameModeFromKey(a[0] == "test" ? "dev" : a[0], GameMode::COUNT);
            if (m == GameMode::COUNT) { c.log("modes: spectator, dev, survival"); return; }
            c.changeMode(m);
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
