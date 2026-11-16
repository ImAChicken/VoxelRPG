#pragma once
#include "StructureRegistry.h"
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
struct BuildTool {
    uint16_t   selected = 0;      // structure type (0 = none chosen)
    int        rot      = 0;      // quarter turns
    bool       locked   = false;
    glm::ivec3 spot     = {0, 0, 0};

    const StructureType* type() const { return gStructures.get(selected); }

    void select(uint16_t type) {
        selected = type;
        locked   = false;
    }

    void write(std::ostream& out) const {
        if (const StructureType* t = type()) out << "build.type=" << t->name << "\n";
        out << "build.rot=" << rot << "\n";
    }
    bool read(const std::string& key, const std::string& value) {
        if (key == "build.type") {       // the old 4 ft foundation → the new one
            selected = gStructures.find(value == "foundation" ? std::string("stone_foundation") : value);
            return true;
        }
        if (key == "build.rot")  { rot = std::atoi(value.c_str()) & 3;  return true; }
        return false;
    }
    void reset() { selected = gStructures.find("stone_foundation"); rot = 0; locked = false; }
};
