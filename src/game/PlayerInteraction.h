#pragma once
#include "DevTools.h"
#include "ItemRegistry.h"
#include "../entity/Player.h"
#include "../input/InputManager.h"
#include "../physics/IPhysics.h"
#include "../render/Camera.h"
#include "../world/WorldEditor.h"
#include "../world/StructureManager.h"
#include "../world/Woodcutting.h"
#include "AxeSwing.h"
#include "BuildTool.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <functional>
#include <string>

// ══════════════════════════════════════════════════════════
// PlayerInteraction — what the player's hands do to the world
// ══════════════════════════════════════════════════════════
// Runs each frame while the player has a body and the mode
// allows world interaction (Survival stance, Test Play).
// Combat stance never gets here: weapons stay in combat.
//
// Buttons (from keyboard.toml):
//   right click   break (use_right "remove")
//   left click    place the held block, or use the held tool
//   middle click  pick: copy the block you look at
//
// The axe is different: it is swung, Valheim style (left click,
// hold to keep swinging), and what it hits is found by tracing
// its blade through the world (game/AxeSwing.h; the main loop
// calls axeHit()). Trees fall over and become logs
// (world/Woodcutting.h).
//
// Pickaxes break a whole cube (the item's "cube" size, see
// game/CubeTool.h): on a grid of that size, or off the grid
// while Shift is held. Doors, building pieces and trees are
// still taken down whole, as with any other tool.
//
// Survival
//   Breaking takes time: block hardness, sped up by the
//   right tool (pickaxe on stone, shovel on dirt, axe on
//   wood). The block goes into the player inventory.
//   Placing uses one block up.
// Test Play
//   Breaking is instant and drops nothing; placing never
//   runs out. Holding a button repeats. Dev tools do their
//   own thing (game/DevTools.h).
//
// Every change goes through WorldEditor with source Player
// (Survival) or Dev (Test Play).
class PlayerInteraction {
public:
    // How far the hands reach, in voxels (25 ≈ 8 ft).
    // Test Play reaches much further for building.
    static constexpr float SURVIVAL_REACH = 25.0f;
    static constexpr float DEV_REACH      = 160.0f;
    // Held button repeat in Test Play, seconds
    static constexpr float DEV_REPEAT     = 0.18f;

    // ── Results the HUD reads ─────────────────────────────
    RaycastResult target;            // block under the crosshair
    float breakProgress = 0.0f;      // 0..1 while digging
    std::string notice;              // short message ("inventory full")
    float noticeAge = 99.0f;

    // What the crosshair is on, if it is part of a structure:
    // "Wooden Door  (E: open)"
    std::string aimLabel;

    // Last tree hit, for the health bar
    std::string treeName;
    float treeHealth = 0.0f, treeMax = 0.0f, treeAge = 99.0f;

    // Builder's Hammer preview (drawn by the main loop)
    bool        hammerActive = false;   // hammer held this frame
    bool        previewHas   = false;   // there is a spot to show
    glm::ivec3  previewSpot  = {0, 0, 0};
    std::string previewWhy;             // "" = can place, else why not

    // Set when the hammer's right click asks for the build menu
    bool wantBuildMenu = false;

    // Cube tools (pickaxe, dev Cube Tool): outlines to draw
    bool    cubeBreakShown = false, cubePlaceShown = false;
    CubeBox cubeBreak, cubePlace;

    std::function<void(const std::string&)> log =
        [](const std::string&) {};

    // Felled trees, logs and drops (set by the main loop when a
    // world is open)
    Woodcutting* wood = nullptr;

    // Look for the block under the crosshair. Call every
    // frame a body exists, before update().
    //   eyes — where the reach is measured from
    void aim(const Camera& camera, glm::vec3 eyes,
             const IPhysics& physics, bool devReach) {
        float reach = devReach ? DEV_REACH : SURVIVAL_REACH;
        // In third person the ray starts at the camera, so
        // add the camera's distance behind the eyes
        reach += glm::length(camera.position - eyes);
        target = physics.raycast(camera.position, camera.front, reach);
        lookDir = camera.front;
    }

    void clearTarget() {
        target = RaycastResult();
        breakProgress = 0.0f;
    }

    void update(Player& player, WorldEditor& editor, DevTools& dev,
                StructureManager& structs, BuildTool& build, float dt) {
        noticeAge += dt;
        treeAge   += dt;
        hammerActive = false;
        cubeBreakShown = cubePlaceShown = false;
        aimLabel.clear();
        const GameModeRules rules = player.rules();
        if (!rules.worldInteraction) { breakProgress = 0.0f; return; }

        bool survival = (player.mode == GameMode::Survival);
        EditSource src = survival ? EditSource::Player : EditSource::Dev;
        Inventory& inv = player.activeInventory();
        const ItemDef& held = inv.heldDef();

        // ── What structure is under the crosshair ─────────
        StructureInstance* aimed = target.hit ? structs.instanceAt(target.blockPos) : nullptr;
        describeAim(structs, aimed);

        // ── E: doors ──────────────────────────────────────
        if (gInput.isPressed(InputAction::Interact) && aimed) {
            const StructureType* t = structs.typeOf(*aimed);
            if (t && t->kind == StructureKind::Door) {
                std::string why = structs.toggleDoor(*aimed, src,
                    [&](glm::ivec3 p) { return overlapsBody(player, p); });
                if (!why.empty()) say(why);
            }
        }

        // ── Middle click: pick block ──────────────────────
        if (gInput.isPressed(InputAction::PickBlock) && target.hit) {
            pickBlock(player, dev, (uint16_t)target.blockID, survival);
        }

        // A tool-specific mode ends when that tool is put away
        DevToolKind tool = (held.is(IC_DEV) && !survival) ? held.devTool
                                                          : DevToolKind::None;
        if (tool != DevToolKind::Copy)  dev.cancelCopyPrompt();
        if (tool != DevToolKind::Paste) dev.pasteLocked = false;
        bool hammer = held.is(IC_TOOL) && held.tool == ToolKind::Hammer;
        if (!hammer) build.locked = false;

        // ── Builder's Hammer ──────────────────────────────
        if (hammer) {
            breakProgress = 0.0f;
            hammerKeys(player, structs, build, survival, src, dt);
            return;
        }

        // ── Dev tools (Test Play only) ────────────────────
        if (held.is(IC_DEV)) {
            breakProgress = 0.0f;
            if (survival) return;   // dev items do nothing in Survival
            devToolKeys(dev, tool, player, dt);
            if (tool == DevToolKind::Cube) {
                dev.cubeFree = gInput.isHeld(InputAction::Sprint);
                if (target.hit) {
                    cubePlace = dev.cubeTarget(target, true);
                    cubeBreak = dev.cubeTarget(target, false);
                    cubePlaceShown = cubeBreakShown = true;
                }
            }
            if (gInput.isPressed(InputAction::UseLeft))
                dev.useTool(held.devTool, true, target, player);
            if (gInput.isPressed(InputAction::UseRight))
                dev.useTool(held.devTool, false, target, player);
            return;
        }

        // ── Axe: swung, not dug with ──────────────────────
        // Left click (held: again and again) starts a swing; the
        // hit itself comes from the blade's path (axeHit)
        if (held.is(IC_TOOL) && held.tool == ToolKind::Axe) {
            breakProgress = 0.0f;
            if (gInput.isHeld(InputAction::UseLeft) && !player.swing.active() &&
                !player.ragdoll.controlsLocked())
                player.swing.start();
            return;
        }

        // ── Pickaxe: a cube at a time ─────────────────────
        bool pick = held.is(IC_TOOL) && held.tool == ToolKind::Pickaxe && held.cubeSize > 0;
        bool aimPlant = target.hit && gRegistry.get((uint16_t)target.blockID).isPlant();
        if (pick && target.hit && !aimed && !aimPlant && !isTreeWood(structs, target.blockPos, (uint16_t)target.blockID)) {
            cubeBreak = cubeFor(target.blockPos, glm::ivec3(target.normal), held.cubeSize,
                                !gInput.isHeld(InputAction::Sprint), false);
            cubeBreakShown = true;
            if (survival) {
                if (cubeDigTimer(editor, held, dt))
                    breakCube(player, editor, structs, cubeBreak, true);
            } else {
                devRepeat(InputAction::UseRight, breakTimer, dt, [&] {
                    breakCube(player, editor, structs, cubeBreak, false);
                });
            }
            return;
        }

        // ── Break (right button) ──────────────────────────
        // Structures come down as one piece; tree trunks are
        // chopped; anything else breaks one voxel.
        if (survival) {
            if (digTimer(held, dt))
                breakTarget(player, editor, structs, held, true);
        } else {
            devRepeat(InputAction::UseRight, breakTimer, dt, [&] {
                breakTarget(player, editor, structs, held, false);
            });
        }

        // ── Place (left button) ───────────────────────────
        if (held.is(IC_DECOR)) {
            if (gInput.isPressed(InputAction::UseLeft)) placeDecor(player, editor, held, survival);
        }
        else if (held.is(IC_BLOCK)) {
            if (survival) {
                if (gInput.isPressed(InputAction::UseLeft))
                    place(player, editor, held.blockID, EditSource::Player);
            } else {
                devRepeat(InputAction::UseLeft, placeTimer, dt, [&] {
                    place(player, editor, held.blockID, EditSource::Dev);
                });
            }
        }
    }

    // ── Axe hits ──────────────────────────────────────────
    // The blade went into a voxel (game/AxeSwing.h). Damage from
    // the edge's speed:
    //   a tree's wood   health down; at 0 it falls
    //   a fallen log    health down, wood dropped on the ground
    //   anything else   a thunk and some chips, no damage
    // Test Play fells and breaks up in one go.
    void axeHit(Player& player, StructureManager& structs, WorldEditor& editor,
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
        if (t && t->kind == StructureKind::Tree) {
            if (!survival) dmg = s->health + 1.0f;
            s->health -= dmg;
            treeName = t->displayName; treeMax = t->health; treeAge = 0.0f;
            treeHealth = std::max(0.0f, s->health);
            if (wood) wood->chips(hit.point, id, 7, -hit.dir);
            if (s->health <= 0.0f) {
                glm::vec3 away = glm::vec3(s->origin) - player.position;
                fellTree(structs, *s, away, src);
            }
            return;
        }
        // anything else: the blade stops, nothing is harmed
        if (wood) wood->chips(hit.point, id, 4, -hit.dir);
    }

    // Take a dead tree out of the world and let it fall (away
    // along 'away'), or, with no woodcutting set up, just remove it
    void fellTree(StructureManager& structs, StructureInstance& s, glm::vec3 away, EditSource src) {
        const StructureType* t = structs.typeOf(s);
        if (!t) return;
        say(t->displayName + " falls");
        if (!wood) { structs.chop(s, s.health + 1.0f, src); return; }
        Woodcutting::TreeInfo info{ t->displayName, t->dropItem, t->dropCount, t->health };
        glm::ivec3 base = s.origin;
        int stumpTop = base.y + Woodcutting::STUMP_HEIGHT;
        std::vector<VoxelPiece> logVox, leaves;
        structs.fell(s, stumpTop, src, logVox, leaves);       // s is gone after this
        wood->fellTree(std::move(logVox), leaves, away,
                       glm::vec3(base.x + 0.5f, (float)stumpTop, base.z + 0.5f), info);
    }

    // While the copy prompt is up, 1 and 2 answer it instead
    // of picking toolbar slots
    static bool blocksToolbarKeys(const DevTools& dev) { return dev.copyPending; }

private:
    float breakTimer = 0.0f, placeTimer = 0.0f;
    glm::vec3 lookDir = {0, 0, -1};
    // Hold-to-repeat timers for the nudge keys
    float nudgeTimers[7] = {};

    // Pressed: true now. Held: true again after a pause,
    // then every 'every' seconds.
    bool repeatKey(InputAction a, float& timer, float dt, float every = 0.07f) {
        if (gInput.isPressed(a)) { timer = 0.35f; return true; }
        if (!gInput.isHeld(a)) return false;
        timer -= dt;
        if (timer <= 0.0f) { timer = every; return true; }
        return false;
    }

    // Arrow / Page keys and the copy prompt, per dev tool
    void devToolKeys(DevTools& dev, DevToolKind tool, const Player& player,
                     float dt) {
        if (tool == DevToolKind::Copy && dev.copyPending) {
            if (gInput.isPressed(InputAction::ToolbarSlot1)) dev.answerCopyPrompt(1);
            if (gInput.isPressed(InputAction::ToolbarSlot2)) dev.answerCopyPrompt(2);
            if (gInput.isPressed(InputAction::ToolCancel))   dev.cancelCopyPrompt();
        }

        if (tool == DevToolKind::Paste) {
            // Forward = the horizontal axis you are looking
            // along most; right is a quarter turn from it.
            // Shift moves 8 at a time.
            glm::ivec3 fwd = std::fabs(lookDir.x) > std::fabs(lookDir.z)
                ? glm::ivec3(lookDir.x > 0 ? 1 : -1, 0, 0)
                : glm::ivec3(0, 0, lookDir.z > 0 ? 1 : -1);
            glm::ivec3 right(-fwd.z, 0, fwd.x);
            int n = gInput.isHeld(InputAction::Sprint) ? 8 : 1;
            struct { InputAction a; glm::ivec3 d; } keys[] = {
                { InputAction::NudgeForward, fwd },
                { InputAction::NudgeBack,   -fwd },
                { InputAction::NudgeRight,   right },
                { InputAction::NudgeLeft,   -right },
                { InputAction::NudgeUp,      glm::ivec3(0, 1, 0) },
                { InputAction::NudgeDown,    glm::ivec3(0, -1, 0) },
            };
            // Nothing under the crosshair: lock 24 voxels in
            // front of the player, at their feet
            glm::ivec3 ahead = glm::ivec3(glm::floor(player.position)) + fwd * 24;
            for (int i = 0; i < 6; i++)
                if (repeatKey(keys[i].a, nudgeTimers[i], dt))
                    dev.nudgePaste(target, keys[i].d * n, ahead);
            if (gInput.isPressed(InputAction::ToolCancel)) dev.releasePaste();
        }

        if (tool == DevToolKind::Cube) {
            if (repeatKey(InputAction::NudgeForward, nudgeTimers[0], dt, 0.09f))
                dev.setCube(dev.cubeSize + 1);
            if (repeatKey(InputAction::NudgeBack, nudgeTimers[1], dt, 0.09f))
                dev.setCube(dev.cubeSize - 1);
        }

        if (tool == DevToolKind::Explode || tool == DevToolKind::Brush) {
            if (repeatKey(InputAction::NudgeForward, nudgeTimers[0], dt, 0.09f))
                dev.setBrush(dev.brushRadius + 1.0f);
            if (repeatKey(InputAction::NudgeBack, nudgeTimers[1], dt, 0.09f))
                dev.setBrush(dev.brushRadius - 1.0f);
        }
    }
    glm::ivec3 digging = {0, -1, 0};   // block being dug

    void say(const std::string& s) { notice = s; noticeAge = 0.0f; }

    // Pressed: act now. Held: act again every DEV_REPEAT.
    template <typename Fn>
    void devRepeat(InputAction button, float& timer, float dt, Fn&& act) {
        if (gInput.isPressed(button)) { act(); timer = DEV_REPEAT * 1.8f; return; }
        if (!gInput.isHeld(button)) return;
        timer -= dt;
        if (timer <= 0.0f) { act(); timer = DEV_REPEAT; }
    }

    void devBreak(WorldEditor& editor) {
        if (!target.hit) return;
        editor.apply(WorldEditor::box(target.blockPos, target.blockPos,
                                      BLOCK_AIR, EditSource::Dev, "break"));
    }

    // ── Cube digging ──────────────────────────────────────
    glm::ivec3 cubeDigging = {0, -1, 0};   // lo corner of the cube being dug
    int   cubeDiggingSize = 0;
    float cubeNeed = 0.0f;                 // seconds for that cube

    // Wood that is part of a natural tree (chopped, not mined)
    static bool isTreeWood(const StructureManager& structs, glm::ivec3 p, uint16_t id) {
        if (id == BLOCK_AIR || id == BLOCK_LEAVES) return false;
        if (gRegistry.get(id).material != "wood") return false;
        return structs.naturalTreeTypeAt(p) != nullptr;
    }

    // Survival: hold the button on a cube until its time has
    // run — as long as its hardest voxel would take alone, so
    // a cube of stone digs as fast as one stone voxel used to.
    bool cubeDigTimer(WorldEditor& editor, const ItemDef& held, float dt) {
        if (!gInput.isHeld(InputAction::UseRight)) { breakProgress = 0.0f; return false; }
        if (cubeBreak.lo != cubeDigging || cubeBreak.size() != cubeDiggingSize) {
            cubeDigging = cubeBreak.lo;
            cubeDiggingSize = cubeBreak.size();
            breakProgress = 0.0f;
            cubeNeed = -1.0f;                      // nothing breakable
            for (int y = cubeBreak.lo.y; y <= cubeBreak.hi.y; y++)
            for (int z = cubeBreak.lo.z; z <= cubeBreak.hi.z; z++)
            for (int x = cubeBreak.lo.x; x <= cubeBreak.hi.x; x++) {
                uint16_t id = editor.typeAt({ x, y, z });
                if (id == BLOCK_AIR) continue;
                const VoxelTypeDef& b = gRegistry.get(id);
                if (!b.isDestructible()) continue;
                cubeNeed = std::max(cubeNeed, breakSeconds(b, held));
            }
        }
        if (cubeNeed < 0.0f) { breakProgress = 0.0f; return false; }
        breakProgress += cubeNeed > 0.0f ? dt / cubeNeed : 1.0f;
        if (breakProgress < 1.0f) return false;
        breakProgress = 0.0f;
        cubeDiggingSize = 0;                       // measure again next time
        return true;
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

    // Survival digging: hold the button on one block until
    // its time (hardness, sped up by the right tool) has run.
    // True on the frame it finishes.
    bool digTimer(const ItemDef& held, float dt) {
        if (!gInput.isHeld(InputAction::UseRight) || !target.hit) {
            breakProgress = 0.0f;
            return false;
        }
        if (target.blockPos != digging) {      // new block: start over
            digging = target.blockPos;
            breakProgress = 0.0f;
        }
        const VoxelTypeDef& block = gRegistry.get((uint16_t)target.blockID);
        if (!block.isDestructible()) { breakProgress = 0.0f; return false; }
        // instant blocks (plants) still take a moment, so holding the
        // button doesn't sweep away everything the crosshair passes
        float need = std::max(breakSeconds(block, held), 0.18f);
        breakProgress += dt / need;
        if (breakProgress < 1.0f) return false;
        breakProgress = 0.0f;
        return true;
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
                fellTree(structs, *s, glm::vec3(s->origin) - player.position, src);
            return;
        }

        if (t) {
            // ── A building piece or door: all of it ──────
            auto gone = structs.remove(s->id, src);
            if (gone.size() > 1)
                say(std::to_string(gone.size() - 1) + " piece(s) resting on it collapsed");
            if (survival)
                for (auto& g : gone)
                    if (const StructureType* gt = gStructures.get(g.type))
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
        if (survival && r.changed) player.inventory.consumeHeld();
    }

    // "Wooden Door (E: open)" etc. for the HUD
    void describeAim(StructureManager& structs, StructureInstance* s) {
        if (!s && wood && target.hit)
            if (const Woodcutting::Log* lg = wood->logAt(target.position)) {
                char b[96];
                std::snprintf(b, sizeof(b), "%s  %.0f / %.0f", lg->name.c_str(), lg->health, lg->maxHealth);
                aimLabel = b;
                return;
            }
        if (!s) {
            // A natural tree nobody has chopped yet
            if (target.hit && target.blockID != BLOCK_LEAVES)
                if (const StructureType* nt = structs.naturalTreeTypeAt(target.blockPos)) {
                    char b[96];
                    std::snprintf(b, sizeof(b), "%s  %.0f / %.0f", nt->displayName.c_str(),
                                  nt->health, nt->health);
                    aimLabel = b;
                }
            return;
        }
        const StructureType* t = structs.typeOf(*s);
        if (!t) return;
        if (t->kind == StructureKind::Door)
            aimLabel = t->displayName + (s->state ? "   E: close" : "   E: open");
        else if (t->kind == StructureKind::Tree) {
            char b[96];
            std::snprintf(b, sizeof(b), "%s  %.0f / %.0f", t->displayName.c_str(),
                          s->health, t->health);
            aimLabel = b;
        } else {
            uint32_t root = structs.rootOf(*s);
            aimLabel = t->displayName + (root ? "   (on a foundation)" : "");
        }
    }

    // ── Builder's Hammer ──────────────────────────────────
    void hammerKeys(Player& player, StructureManager& structs, BuildTool& build,
                    bool survival, EditSource src, float dt) {
        hammerActive = true;
        if (gInput.isPressed(InputAction::UseRight)) wantBuildMenu = true;
        const StructureType* t = build.type();
        if (!t) {
            previewHas = false;
            previewWhy = "right click to choose a structure";
            return;
        }
        if (gInput.isPressed(InputAction::RotatePiece)) build.rot = (build.rot + 1) & 3;
        if (gInput.isPressed(InputAction::ToolCancel))  build.locked = false;

        const VoxelVolume& shape = t->shape(0, build.rot);
        glm::ivec3 fwd = std::fabs(lookDir.x) > std::fabs(lookDir.z)
            ? glm::ivec3(lookDir.x > 0 ? 1 : -1, 0, 0)
            : glm::ivec3(0, 0, lookDir.z > 0 ? 1 : -1);

        // Where it goes: the locked spot, or against the face
        // under the crosshair (snapped to the piece's grid)
        previewHas = build.locked || target.hit;
        if (!build.locked && target.hit) {
            glm::ivec3 spot = target.blockPos + glm::ivec3(target.normal);
            if (t->snap > 0) {
                glm::ivec3 o = spot - shape.anchor;
                o.x = floorDiv(o.x, t->snap) * t->snap;
                o.z = floorDiv(o.z, t->snap) * t->snap;
                spot = o + shape.anchor;
            }
            build.spot = spot;
        }

        // Arrows / Page keys: lock, then move (Shift = 8)
        glm::ivec3 right(-fwd.z, 0, fwd.x);
        int n = gInput.isHeld(InputAction::Sprint) ? 8 : 1;
        struct { InputAction a; glm::ivec3 d; } keys[] = {
            { InputAction::NudgeForward, fwd },  { InputAction::NudgeBack, -fwd },
            { InputAction::NudgeRight, right },  { InputAction::NudgeLeft, -right },
            { InputAction::NudgeUp, {0, 1, 0} }, { InputAction::NudgeDown, {0, -1, 0} },
        };
        for (int i = 0; i < 6; i++)
            if (repeatKey(keys[i].a, nudgeTimers[i], dt)) {
                if (!build.locked) {
                    if (!target.hit)
                        build.spot = glm::ivec3(glm::floor(player.position)) + fwd * 24;
                    build.locked = true;
                    previewHas = true;
                }
                build.spot += keys[i].d * n;
            }
        previewSpot = build.spot;

        // Can it go there?
        uint32_t support = 0;
        previewWhy = previewHas ? structs.checkPlace(build.selected, build.spot,
                                                     build.rot, survival, &support)
                                : "aim at the ground";
        if (previewWhy.empty() && survival)
            for (auto& [item, cnt] : t->cost) {
                int have = player.inventory.count(item);
                if (have < cnt) {
                    previewWhy = "needs " + std::to_string(cnt) + " " +
                                 gItems.get(item).displayName + " (have " +
                                 std::to_string(have) + ")";
                    break;
                }
            }

        // Left click: build it
        if (gInput.isPressed(InputAction::UseLeft)) {
            if (!previewWhy.empty()) { say(previewWhy); return; }
            if (survival)
                for (auto& [item, cnt] : t->cost) player.inventory.take(item, cnt);
            structs.place(build.selected, build.spot, build.rot, src, support);
        }
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
        if (r.changed) player.activeInventory().consumeHeld();
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

    // Middle click. Test Play: the block goes into the held
    // slot (or the first empty one) and becomes the dev
    // tools' material. Survival: select it if you have it.
    void pickBlock(Player& player, DevTools& dev, uint16_t blockID,
                   bool survival) {
        uint16_t item = gItems.itemForBlock(blockID);
        if (!item) return;
        Inventory& inv = player.activeInventory();
        for (int i = 0; i < Inventory::TOOLBAR_SLOTS; i++)
            if (inv.toolbar[i].itemID == item) {
                inv.select(i);
                if (!survival) dev.setMaterial(blockID);
                return;
            }
        if (survival) return;
        dev.setMaterial(blockID);
        int slot = inv.selected;
        if (!inv.held().empty() && !inv.heldDef().is(IC_BLOCK)) {
            // Don't overwrite a tool: use the first empty slot
            slot = -1;
            for (int i = 0; i < Inventory::TOOLBAR_SLOTS; i++)
                if (inv.toolbar[i].empty()) { slot = i; break; }
            if (slot < 0) return;
        }
        inv.setSlot(slot, item, 1);
        inv.select(slot);
    }
};
