#pragma once
#include "StructureRegistry.h"
#include "Blueprint.h"
#include "Palettes.h"
#include <glm/glm.hpp>
#include <cstdlib>
#include <ostream>
#include <sstream>
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
//   Build      buildings kept as plans (game/BuildPlan.h, world/
//              PlanManager.h): foundations, walls and rooms on the
//              3 ft grid, painted from the palette; heights changed
//              without rebuilding (planTool below)
enum class HammerMode : uint8_t { Piece, Blueprint, Copy, Plan };

// The Build tab's tools
//   Foundation  drag a rectangle of grid cells
//   Room        drag a rectangle of grid points: four walls
//   Wall        drag from a point: straight, 45° or 2-to-1
//   Pillar      click: a pillar there, through any wall
//   Paint       click a wall's side: the paint block
//   Height      a wall: up / down 3 (arrows or Page keys);
//               the foundation: the floor up / down 1
//   Remove      a roof, a frame, a pillar, a wall, or the foundation under the cursor
//   Roof        click in a building: a roof over all its walls; drag: over
//               a rectangle; on a roof: it takes the menu's style, peak and
//               overhang, the turn key turns its ridge
//   Frame       a hole in a wall where the cursor is, its size set in
//               the menu, filled with a door, a window or nothing; on a
//               frame already there, it's replaced
//   Stair       drag the way it climbs: a stair up to the next level
//   Fence       drag: a fence or railing, one voxel wide (styles in the menu)
// Levels: Page Up / Page Down (or the menu) pick the storey the tools
// build on; above the ground floor, Foundation lays a floor (a deck)
enum class PlanTool : uint8_t { Foundation, Room, Wall, Pillar, Frame, Roof, Stair, Fence, Paint, Height, Remove, COUNT };
inline const char* planToolName(PlanTool t) {
    static const char* N[] = { "Foundation", "Room", "Wall", "Pillar", "Frame", "Roof", "Stair", "Fence", "Paint", "Height", "Remove" };
    return N[(int)t < (int)PlanTool::COUNT ? (int)t : 0];
}

struct BuildTool {
    HammerMode mode = HammerMode::Piece;
    std::string blueprint;            // the chosen blueprint's name
    BlueprintStyle style;             // how blueprints are dressed when placed
    std::string group = "foundation"; // the build menu's piece filter

    uint16_t   selected = 0;      // structure type (0 = none chosen)
    int        rot      = 0;      // quarter turns (T)
    int        shownRot = 0;      // the turn actually used: a snap (walls on an
                                  // edge, roofs on a wall) may turn the piece
    std::string shownSpec;        // the spec actually used (a door snapped into a
                                  // doorway takes the doorway's size)
    bool       locked   = false;
    glm::ivec3 spot     = {0, 0, 0};

    // ── Made pieces (walls, posts, doors: game/PieceSpec.h) ──
    //   palette  the equipped palette (Palettes tab; "": each piece
    //            as it comes)
    //   height   one height for every wall and post (0: each its own),
    //            so a storey's walls match
    //   sizes    a piece's own width and opening, by type name
    std::string palette;
    int         height = 0;
    std::map<std::string, PieceSpec> sizes;

    // ── The Build tab (plans) ─────────────────────────────
    PlanTool    planTool = PlanTool::Foundation;
    int         planFloor = 3;        // a new foundation's height (voxels)
    int         planWallH = 30;       // a new wall's height (10 ft)
    int         planThick = 3;        // a new wall's thickness
    std::string planPaint;            // the Paint tool's block ("": the palette's)
    int         planPillar = 5;       // a new pillar's size (voxels across)
    bool        planRound = false;    // … round
    int         planFill = 1;         // a new frame's filling: 0 open, 1 a door, 2 a window (plan::Fill)
    int         planOpenW = 10, planOpenH = 24, planSill = 0;   // its size (voxels) and height off the floor
    std::string planDoor;             // the door piece a door frame gets ("": the plan's default)
    int         planLevel = 0;        // the storey the tools build on (0: the ground floor)
    int         planStairW = 9;       // a new stair's width
    int         planFenceStyle = 0;   // plan::FenceStyle
    int         planFenceH = 9;       // a new fence's height
    int         planStairShape = 0;   // plan::StairShape: straight, L, U, spiral
    int         planStairTurn = 1;    // which way an L, U or spiral turns (+1 / -1; T flips it)
    bool        planRails = true;     // plans' own railings round stairwells and open deck edges
    bool        planRailsChanged = false;   // (the menu changed it: PlayerInteraction applies it to every plan)
    bool        planCutActive = false;   // (the build camera is showing a cutaway: main.cpp sets it each frame)
    int         planView = 2;         // the build camera's cutaway: 0 everything, 1 walls up (the levels over hidden), 2 walls down
    float       planHoverY = 0.0f;    // the cursor's height (the level's floor upstairs)
    int         planRoofStyle = 0;    // plan::RoofStyle: gable, hip, shed, flat
    int         planPeak = 15;        // a new roof's rise to the ridge (voxels)
    int         planOverhang = 3;     // … past the walls
    int         planSnap = 0;         // what the cursor snapped to: 0 the grid, 1 a part, 2 free (sprint held)
    // a drag under way (PlayerInteraction): its start and where it is now,
    // in world voxel columns (walls, rooms, pillars) or grid cells (foundations)
    bool        planDrag = false;
    glm::ivec2  planA{ 0 }, planB{ 0 };
    int         planY = 0;            // the ground (or floor) it's on
    bool        planHasCursor = false;
    glm::ivec2  planCursor{ 0 };      // the grid point (or cell) under the crosshair
    bool        planFrameShow = false;  // the Frame tool's outline on the wall aimed at
    glm::vec3   planFrame[4];

    // What the chosen piece is placed with (normalized)
    std::string specFor(const StructureType& t) const {
        if (!t.sizable()) return "";
        PieceSpec p;
        auto it = sizes.find(t.name);
        if (it != sizes.end()) p = it->second;
        if (height > 0 && (t.model == "wall" || t.model == "pillar")) p.h = height;
        if (const BuildPalette* pal = gBuildPalettes.find(palette)) p = gStructures.applyPalette(t, p, pal->spec);
        return gStructures.normalize(t, p);
    }
    std::string spec() const { const StructureType* t = type(); return t ? specFor(*t) : std::string(); }

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
        if (!palette.empty()) out << "build.palette=" << palette << "\n";
        if (height) out << "build.height=" << height << "\n";
        for (auto& [name, p] : sizes) if (!p.empty()) out << "build.size." << name << "=" << p.text() << "\n";
        out << "build.plan=" << (int)planTool << " " << planFloor << " " << planWallH << " " << planThick << " "
            << (planPaint.empty() ? "-" : planPaint) << " " << planPillar << " " << (planRound ? 1 : 0)
            << " " << planFill << " " << planOpenW << " " << planOpenH << " " << planSill
            << " " << planRoofStyle << " " << planPeak << " " << planOverhang
            << " " << planStairW << " " << planFenceStyle << " " << planFenceH
            << " " << planStairShape << " " << planStairTurn << " " << (planRails ? 1 : 0) << " " << planView << "\n";
        if (!planDoor.empty()) out << "build.plandoor=" << planDoor << "\n";
    }
    bool read(const std::string& key, const std::string& value) {
        if (key == "build.type") {       // the old 4 ft foundation → the new one
            selected = gStructures.find(value == "foundation" ? std::string("stone_foundation") : value);
            return true;
        }
        if (key == "build.rot")  { rot = std::atoi(value.c_str()) & 3;  return true; }
        if (key == "build.blueprint") { blueprint = value; return true; }
        if (key == "build.palette") { palette = value; return true; }
        if (key == "build.height") { height = std::clamp(std::atoi(value.c_str()), 0, 90); return true; }
        if (key.rfind("build.size.", 0) == 0) { sizes[key.substr(11)] = PieceSpec::parse(value); return true; }
        if (key == "build.mode") { int m = std::atoi(value.c_str()); mode = m == 1 ? HammerMode::Blueprint : m == 3 ? HammerMode::Plan : HammerMode::Piece; return true; }
        if (key == "build.plandoor") { planDoor = value; return true; }
        if (key == "build.plan") {
            std::istringstream l(value); int t = 0; std::string pp;
            int rd = 0;
            l >> t >> planFloor >> planWallH >> planThick >> pp >> planPillar >> rd;
            if (l >> planFill >> planOpenW >> planOpenH >> planSill) {
                planFill = std::clamp(planFill, 0, 2);
                planOpenW = std::clamp(planOpenW, 2, 60); planOpenH = std::clamp(planOpenH, 2, 80); planSill = std::clamp(planSill, 0, 60);
            }
            if (l >> planRoofStyle >> planPeak >> planOverhang) {
                planRoofStyle = std::clamp(planRoofStyle, 0, 3); planPeak = std::clamp(planPeak, 0, 60); planOverhang = std::clamp(planOverhang, 0, 9);
            }
            if (l >> planStairW >> planFenceStyle >> planFenceH) {
                planStairW = std::clamp(planStairW, 3, 30); planFenceStyle = std::clamp(planFenceStyle, 0, 3); planFenceH = std::clamp(planFenceH, 3, 30);
            }
            if (int rl = 1; l >> planStairShape >> planStairTurn >> rl >> planView) {
                planStairShape = std::clamp(planStairShape, 0, 3); planStairTurn = planStairTurn < 0 ? -1 : 1;
                planRails = rl != 0; planView = std::clamp(planView, 0, 2);
            }
            planRound = rd != 0;
            planPillar = std::clamp(planPillar, 1, 15);
            planTool = (PlanTool)std::clamp(t, 0, (int)PlanTool::COUNT - 1);
            planPaint = pp == "-" ? std::string() : pp;
            return true;
        }
        return false;
    }
    void reset() {
        selected = gStructures.find("stone_foundation"); rot = 0; locked = false; mode = HammerMode::Piece; blueprint.clear();
        palette.clear(); height = 0; sizes.clear();
    }
};
