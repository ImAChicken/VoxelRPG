#pragma once
#include "DevTools.h"
#include "ItemRegistry.h"
#include "../entity/Player.h"
#include "../input/InputManager.h"
#include "../physics/IPhysics.h"
#include "../render/Camera.h"
#include "../world/WorldEditor.h"
#include "../world/StructureManager.h"
#include "BuildTool.h"
#include <glm/glm.hpp>
#include <cmath>
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

    std::function<void(const std::string&)> log =
        [](const std::string&) {};

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
            if (gInput.isPressed(InputAction::UseLeft))
                dev.useTool(held.devTool, true, target, player);
            if (gInput.isPressed(InputAction::UseRight))
                dev.useTool(held.devTool, false, target, player);
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
        if (held.is(IC_BLOCK)) {
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
        float need = breakSeconds(block, held);
        breakProgress += (need > 0.0f) ? dt / need : 1.0f;
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
            // An axe does its power in damage, anything else 1.
            // Test Play fells in one go.
            float dmg = (held.is(IC_TOOL) && held.tool == ToolKind::Axe)
                      ? std::max(1.0f, held.toolPower) : 1.0f;
            if (!survival) dmg = s->health + 1.0f;
            treeName = t->displayName;
            treeMax  = t->health;
            treeHealth = std::max(0.0f, s->health - dmg);
            treeAge  = 0.0f;
            uint16_t drop = t->dropItem;
            int dropN = t->dropCount;
            if (structs.chop(*s, dmg, src)) {
                say(t->displayName + " falls");
                if (survival && drop) {
                    int left = player.inventory.add(drop, dropN);
                    if (left) say("inventory full - " + std::to_string(left) + " lost");
                }
            }
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

        // ── An ordinary voxel ─────────────────────────────
        if (!survival) { devBreak(editor); return; }
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
    }

    // "Wooden Door (E: open)" etc. for the HUD
    void describeAim(StructureManager& structs, StructureInstance* s) {
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
