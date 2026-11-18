#pragma once
#include "StructureRegistry.h"
#include "Blueprint.h"
#include <glm/glm.hpp>
#include <cstdlib>
#include <ostream>
#include <string>

// ══════════════════════════════════════════════════════════
// BuildTool — state of the Builder's Hammer
// ══════════════════════════════════════════════════════════
// The hammer is a player tool (Survival and Test Play):
//   right click   open the build menu, pick a structure
//   left click    place it
//   T             turn it a quarter turn
//   arrows, Page Up / Page Down   fine-tune the position
//                 (locks it in place; walk and look freely)
//   Backspace     release the lock
//
// The placement preview follows the crosshair until an arrow
// key locks it, exactly like the Paste tool.
//
// Modes (tabs of the build menu):
//   Piece      one build piece (above)
//   Blueprint  a whole saved building (game/Blueprint.h): aim at
//              the ground, T turns it, click places it, levelling
//              the ground under it first
//   Copy       click any piece of a building to save the whole
//              building as a blueprint (then it's ready to place)
enum class HammerMode : uint8_t { Piece, Blueprint, Copy };

struct BuildTool {
    HammerMode mode = HammerMode::Piece;
    std::string blueprint;            // the chosen blueprint's name
    BlueprintStyle style;             // how blueprints are dressed when placed
    std::string group = "foundation"; // the build menu's piece filter

    uint16_t   selected = 0;      // structure type (0 = none chosen)
    int        rot      = 0;      // quarter turns (T)
    int        shownRot = 0;      // the turn actually used: a snap (walls on an
                                  // edge, roofs on a wall) may turn the piece
    bool       locked   = false;
    glm::ivec3 spot     = {0, 0, 0};

    const StructureType* type() const { return gStructures.get(selected); }

    void select(uint16_t type) {
        selected = type;
        locked   = false;
        mode     = HammerMode::Piece;
    }
    void selectBlueprint(const std::string& name) {
        blueprint = name;
        locked    = false;
        mode      = HammerMode::Blueprint;
    }
    const Blueprint* chosenBlueprint() const { return gBlueprints.find(blueprint); }

    void write(std::ostream& out) const {
        if (const StructureType* t = type()) out << "build.type=" << t->name << "\n";
        out << "build.rot=" << rot << "\n";
        if (!blueprint.empty()) out << "build.blueprint=" << blueprint << "\n";
        out << "build.mode=" << (int)mode << "\n";
    }
    bool read(const std::string& key, const std::string& value) {
        if (key == "build.type") {       // the old 4 ft foundation → the new one
            selected = gStructures.find(value == "foundation" ? std::string("stone_foundation") : value);
            return true;
        }
        if (key == "build.rot")  { rot = std::atoi(value.c_str()) & 3;  return true; }
        if (key == "build.blueprint") { blueprint = value; return true; }
        if (key == "build.mode") { int m = std::atoi(value.c_str()); mode = m == 1 ? HammerMode::Blueprint : HammerMode::Piece; return true; }
        return false;
    }
    void reset() { selected = gStructures.find("stone_foundation"); rot = 0; locked = false; mode = HammerMode::Piece; blueprint.clear(); }
};
