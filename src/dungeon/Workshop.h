#pragma once
// ══════════════════════════════════════════════════════════
// The tile workshop: building dungeon tiles in a world
// ══════════════════════════════════════════════════════════
// (docs/dungeons.md, step 1.) Any world works; a flat stone one is
// easiest. Console:
//   tile new <name> <w> <h> <d> [kind]   a blank tile (sizes in half
//                                        cells of 16) in front of you:
//                                        walls, floor, ceiling
//   tile edit <name>                     an existing tile, to change
//   tile socket [wall|door|open|shaft]   the edge square you're
//                                        looking at (no type: the next
//                                        one); a door cuts its doorway,
//                                        a wall fills it back
//   tile marker <key> <value>            a setting on the marker block
//                                        you're looking at (spawn,
//                                        count, rule …)
//   tile kind|weight|min|max|rotate|theme <value>
//   tile save [name]                     writes it (data/dungeons/
//                                        <theme>/tiles)
//   tile showcase [theme]                every tile in a row, to look over
//   tile starter [theme] [over]          the generated starter set
//   tile list [theme]
//   tile done                            stop editing (the plot stays)
// Debug layer "tiles" (debug tiles): the plot's box, its half-cell
// grid, each socket as a coloured square (door green, open white,
// shaft yellow), each marker with its settings; the same for every
// tile in a showcase.

#include "Tile.h"
#include <string>
#include <vector>

namespace dungeon {

struct Workshop {
    bool        editing = false;
    std::string name;
    glm::ivec3  origin{0};       // the plot's low corner (world voxels)
    TileInfo    info;
    // tiles laid out by "tile showcase": where, and what
    struct Shown { glm::ivec3 origin; std::string name; TileInfo info; };
    std::vector<Shown> showcase;

    glm::ivec3 size() const { return info.voxels(); }
    bool inside(glm::ivec3 w) const {
        glm::ivec3 p = w - origin;
        return editing && p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < size().x && p.y < size().y && p.z < size().z;
    }
    // The outside face square of the plot nearest a world voxel: false
    // if it's not on (or right at) the plot's skin
    bool faceAt(glm::ivec3 w, glm::ivec3& cell, int& face) const {
        if (!editing) return false;
        glm::ivec3 p = w - origin, s = size();
        if (p.x < -1 || p.y < -1 || p.z < -1 || p.x > s.x || p.y > s.y || p.z > s.z) return false;
        int d[6] = { p.z, s.x - 1 - p.x, s.z - 1 - p.z, p.x, s.y - 1 - p.y, p.y };   // n e s w up down
        face = 0;
        for (int i = 1; i < 6; i++) if (d[i] < d[face]) face = i;
        if (d[face] > WALL + 1) return false;                 // well inside: not on a face
        cell = glm::clamp(glm::ivec3(glm::floor(glm::vec3(glm::clamp(p, glm::ivec3(0), s - 1)) / (float)HALF)),
                          glm::ivec3(0), info.cells - 1);
        return info.outside(cell, face);
    }
};

inline Workshop gWorkshop;

} // namespace dungeon
