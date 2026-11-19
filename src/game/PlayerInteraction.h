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
#include "../world/Chests.h"
#include "../world/TerrainOps.h"
#include "AxeSwing.h"
#include "BuildTool.h"
#include "WorldActions.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <map>
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
class PlayerInteraction : public WorldActions {
public:
    // Does the tool in hand take more of the pad (its tool layer:
    // B lets go, the D-pad moves, Y up / down — input/PadLayout.h)
    static bool takesPad(const Player& p) {
        const ItemDef& h = p.activeInventory().heldDef();
        if (h.is(IC_TOOL) && h.tool == ToolKind::Hammer) return true;
        if (!h.is(IC_DEV) || p.mode == GameMode::Survival) return false;
        switch (h.devTool) {
        case DevToolKind::Copy: case DevToolKind::Paste: case DevToolKind::Explode:
        case DevToolKind::Brush: case DevToolKind::Cube: return true;
        default: return false;
        }
    }

    // How far the hands reach, in voxels (25 ≈ 8 ft).
    // Test Play reaches much further for building.
    // Held button repeat in Test Play, seconds
    static constexpr float DEV_REPEAT     = 0.18f;

    // ── Results the HUD reads ─────────────────────────────
    // (target, notice, the tree bar: WorldActions)
    float breakProgress = 0.0f;      // 0..1 while digging

    // What the crosshair is on, if it is part of a structure:
    // "Wooden Door  (E: open)"
    std::string aimLabel;

    // Builder's Hammer preview (drawn by the main loop)
    bool        hammerActive = false;   // hammer held this frame
    // Blueprint and Copy modes: the boxes of every piece, to outline
    std::vector<std::pair<glm::ivec3, glm::ivec3>> previewBoxes;
    bool        previewHas   = false;   // there is a spot to show
    glm::ivec3  previewSpot  = {0, 0, 0};
    std::string previewWhy;             // "" = can place, else why not

    // Set when the hammer's right click asks for the build menu
    bool wantBuildMenu = false;
    // The hammer taking a piece down (held right button): s held so far
    // (< 0: not held), the piece, and whether it's done this hold
    static constexpr float DISMANTLE_HOLD = 0.6f;
    float    dismantleHeld = -1.0f;
    uint32_t dismantleId = 0;
    bool     dismantleDone = false;
    float dismantleProgress() const { return dismantleId && !dismantleDone && dismantleHeld >= 0.0f ? std::min(1.0f, dismantleHeld / DISMANTLE_HOLD) : -1.0f; }

    // Cube tools (pickaxe, dev Cube Tool): outlines to draw
    bool    cubeBreakShown = false, cubePlaceShown = false;
    CubeBox cubeBreak, cubePlace;

    std::function<void(const std::string&)> log =
        [](const std::string&) {};

    // On a server's client: every change is sent there as a
    // request (WorldRequest) and done by the server, with the same
    // WorldActions::act. Unset: done here.
    std::function<void(const WorldRequest&)> remote;
    // A chest E was pressed on (structure instance id; main.cpp
    // opens it in the inventory screen and sets this back to 0)
    uint32_t openChest = 0;
    // A station E was pressed on (workbench, forge …; main.cpp
    // opens its menu and sets this back to 0)
    uint32_t openStation = 0;

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
            if (t && t->kind == StructureKind::Door) submit(request(WorldRequest::Op::Door, 0), player, editor, structs);
            if (t && t->slots > 0) {                                  // a chest, or anything with storage
                if (remote) say("chests on a server: not yet");
                else openChest = aimed->id;
            }
            else if (t && t->kind == StructureKind::Station) openStation = aimed->id;
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
            hammerKeys(player, editor, structs, build, survival, src, dt);
            return;
        }

        // ── Dev tools (Test Play only) ────────────────────
        if (held.is(IC_DEV)) {
            breakProgress = 0.0f;
            if (survival) return;   // dev items do nothing in Survival
            if (remote) {           // they work on the world directly: your own worlds only
                if (gInput.isPressed(InputAction::UseLeft) || gInput.isPressed(InputAction::UseRight))
                    say("dev tools work in your own worlds, not on a server");
                return;
            }
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

        // ── Hoe: shapes the ground (T: level, raise, lower, path)
        if (held.is(IC_TOOL) && held.tool == ToolKind::Hoe) {
            breakProgress = 0.0f;
            hoeKeys(player, editor, structs, held, dt);
            return;
        }
        // ── Blast wand: explosions (left; right: twice as big)
        if (held.is(IC_TOOL) && held.tool == ToolKind::Blast) {
            breakProgress = 0.0f;
            blastCooldown = std::max(0.0f, blastCooldown - dt);
            if (target.hit && blastCooldown <= 0.0f) {
                bool l = gInput.isPressed(InputAction::UseLeft), r = gInput.isPressed(InputAction::UseRight);
                if (l || r) {
                    WorldRequest q = request(WorldRequest::Op::Blast, held.id);
                    q.mode = r ? 1 : 0;
                    submit(q, player, editor, structs);
                    blastCooldown = BLAST_COOLDOWN;
                }
            }
            aimLabel = "Blast Wand   {use_left}: blast   {use_right}: big blast";
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
            WorldRequest q = request(WorldRequest::Op::BreakCube, held.id);
            q.lo = cubeBreak.lo; q.hi = cubeBreak.hi;
            if (survival) {
                if (cubeDigTimer(editor, held, dt)) submit(q, player, editor, structs);
            } else {
                devRepeat(InputAction::UseRight, breakTimer, dt, [&] { submit(q, player, editor, structs); });
            }
            return;
        }

        // ── Break (right button) ──────────────────────────
        // Structures come down as one piece; tree trunks are
        // chopped; anything else breaks one voxel.
        if (survival) {
            if (digTimer(held, dt) && target.hit) submit(request(WorldRequest::Op::Break, held.id), player, editor, structs);
        } else {
            devRepeat(InputAction::UseRight, breakTimer, dt, [&] {
                if (target.hit) submit(request(WorldRequest::Op::Break, held.id), player, editor, structs);
            });
        }

        // ── Place (left button) ───────────────────────────
        if (target.hit && (held.is(IC_DECOR) || held.is(IC_BLOCK))) {
            WorldRequest q = request(WorldRequest::Op::Place, held.id);
            if (survival || held.is(IC_DECOR)) {
                if (gInput.isPressed(InputAction::UseLeft)) submit(q, player, editor, structs);
            } else {
                devRepeat(InputAction::UseLeft, placeTimer, dt, [&] { submit(q, player, editor, structs); });
            }
        }
    }


    // ── Axe hits ──────────────────────────────────────────
    // The main loop traces the blade (game/AxeSwing.h); what it
    // met is done here, or asked of the server
    void axeHit(Player& player, StructureManager& structs, WorldEditor& editor,
                const axeswing::BladeHit& hit, float toolPower) {
        if (!remote) { hitWithAxe(player, structs, editor, hit, toolPower); return; }
        WorldRequest q;
        q.op = WorldRequest::Op::AxeHit;
        q.item = player.activeInventory().heldDef().id;
        q.pos = hit.voxel; q.point = hit.point; q.dir = hit.dir; q.speed = hit.speed;
        remote(q);
    }

    // A request about what's aimed at
    WorldRequest request(WorldRequest::Op op, uint16_t item) const {
        WorldRequest q;
        q.op = op;
        q.item = item;
        q.pos = target.blockPos;
        q.normal = glm::ivec3(glm::round(target.normal));
        return q;
    }
    // Done here, or sent to the server
    void submit(const WorldRequest& q, Player& player, WorldEditor& editor, StructureManager& structs) {
        uint16_t before = editor.typeAt(q.pos);
        const ItemDef& used = gItems.get(q.item);
        if (remote) remote(q);
        else act(q, player, editor, structs);
        if (onRequest) onRequest(q, before, used.is(IC_BLOCK) ? used.blockID : 0);
    }
    // Each request made, after (the game plays its sound: audio/SoundDirector.h).
    // before: the block that was at q.pos; placed: the block put down, if any
    std::function<void(const WorldRequest&, uint16_t before, uint16_t placed)> onRequest;

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
    // Tool alt held (the pad's Y, Right Alt): forward / back go up / down
    static glm::ivec3 altFwd(glm::ivec3 fwd) { return gInput.isHeld(InputAction::ToolAlt) ? glm::ivec3(0, 1, 0) : fwd; }

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
                { InputAction::NudgeForward, altFwd(fwd) },
                { InputAction::NudgeBack,   -altFwd(fwd) },
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


    // Pressed: act now. Held: act again every DEV_REPEAT.
    template <typename Fn>
    void devRepeat(InputAction button, float& timer, float dt, Fn&& act) {
        if (gInput.isPressed(button)) { act(); timer = DEV_REPEAT * 1.8f; return; }
        if (!gInput.isHeld(button)) return;
        timer -= dt;
        if (timer <= 0.0f) { act(); timer = DEV_REPEAT; }
    }


    // ── Cube digging ──────────────────────────────────────
    glm::ivec3 cubeDigging = {0, -1, 0};   // lo corner of the cube being dug
    int   cubeDiggingSize = 0;
    float cubeNeed = 0.0f;                 // seconds for that cube


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
            aimLabel = t->displayName + (s->state ? "   {interact}: close" : "   {interact}: open");
        else if (t->kind == StructureKind::Station)
            aimLabel = t->displayName + "   {interact}: use";
        else if (t->kind == StructureKind::Chest) {
            char b[112];
            std::string name = t->displayName;
            if (Container* c = chests ? chests->find(s->id) : nullptr) {
                if (!c->label.empty()) name = c->label;
                if (c->showcase) name += " - showcase";
            }
            std::snprintf(b, sizeof(b), "%s  %.0f / %.0f   {interact}: open", name.c_str(), s->health, t->health);
            aimLabel = b;
        }
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
    void hammerKeys(Player& player, WorldEditor& editor, StructureManager& structs, BuildTool& build,
                    bool survival, EditSource src, float dt) {
        hammerActive = true;
        previewBoxes.clear();
        // Right button (RT): a tap opens the build menu; held on a placed
        // piece, it takes the piece down (Survival: its cost back, like
        // breaking it; what rested on it falls too)
        {
            StructureInstance* aimed = target.hit ? structs.instanceAt(target.blockPos) : nullptr;
            const StructureType* at = aimed ? structs.typeOf(*aimed) : nullptr;
            const bool takeable = at && (at->kind == StructureKind::Build || at->kind == StructureKind::Door ||
                                         at->kind == StructureKind::Station);
            if (takeable && dismantleHeld < 0.0f) aimLabel += "   hold {use_right}: take down";
            if (gInput.isPressed(InputAction::UseRight)) { dismantleHeld = 0.0f; dismantleId = takeable ? aimed->id : 0; dismantleDone = false; }
            if (dismantleHeld >= 0.0f && gInput.isHeld(InputAction::UseRight)) {
                dismantleHeld += dt;
                // still on the same piece, held long enough: down it comes
                if (dismantleId && !dismantleDone && (!takeable || aimed->id != dismantleId)) dismantleId = 0;   // looked away
                if (dismantleId && !dismantleDone && dismantleHeld >= DISMANTLE_HOLD) {
                    submit(request(WorldRequest::Op::Break, player.activeInventory().heldDef().id), player, editor, structs);
                    dismantleDone = true;
                }
            }
            if (dismantleHeld >= 0.0f && !gInput.isHeld(InputAction::UseRight)) {
                if (!dismantleDone && dismantleHeld < DISMANTLE_HOLD * 0.6f) wantBuildMenu = true;   // a tap
                dismantleHeld = -1.0f;
                dismantleId = 0;
            }
        }
        // A copied building whose palettes could be fewer: asked once, on
        // the blueprint only (the building itself is never changed)
        if (!mergeAsk.empty()) {
            const Blueprint* bp = gBlueprints.find(mergeAsk);
            if (!bp) { mergeAsk.clear(); }
            else {
                previewHas = false;
                previewWhy = "";
                aimLabel = std::to_string(bp->palettes.size()) + " palettes in " + bp->display + ": " +
                           std::to_string(bp->mergeable()) + " can merge (they never disagree).  {use_left}: merge them   {tool_cancel}: keep them";
                if (gInput.isPressed(InputAction::UseLeft)) {
                    Blueprint b = *bp;
                    int n = b.mergePalettes();
                    say(gBlueprints.save(b) ? "merged: " + std::to_string(b.palettes.size()) + " palette" + (b.palettes.size() == 1 ? "" : "s") +
                                              " (" + std::to_string(n) + " fewer)" : "could not save the blueprint");
                    mergeAsk.clear();
                } else if (gInput.isPressed(InputAction::ToolCancel)) {
                    say("kept " + std::to_string(bp->palettes.size()) + " palettes");
                    mergeAsk.clear();
                }
                return;
            }
        }
        if (build.mode == HammerMode::Copy)      { copyKeys(structs, build); return; }
        if (build.mode == HammerMode::Blueprint) { blueprintKeys(player, editor, structs, build, survival, src, dt); return; }
        const StructureType* t = build.type();
        if (!t) {
            previewHas = false;
            previewWhy = "{use_right}: choose a structure";
            return;
        }
        if (gInput.isPressed(InputAction::RotatePiece)) build.rot = (build.rot + 1) & 3;
        if (!build.locked) build.shownRot = build.rot;
        if (gInput.isPressed(InputAction::ToolCancel))  build.locked = false;

        std::string spec = build.spec();            // its own sizes and the equipped palette
        if (!build.locked) build.shownSpec = spec;
        glm::ivec3 fwd = std::fabs(lookDir.x) > std::fabs(lookDir.z)
            ? glm::ivec3(lookDir.x > 0 ? 1 : -1, 0, 0)
            : glm::ivec3(0, 0, lookDir.z > 0 ? 1 : -1);

        // Where it goes: the locked spot, or against the face
        // under the crosshair (snapped to the piece's grid)
        previewHas = build.locked || target.hit;
        if (!build.locked && target.hit) {
            glm::ivec3 spot = target.blockPos + glm::ivec3(target.normal);
            int snapRot = build.rot;
            if (structs.snapTo(build.selected, build.rot, target.blockPos, glm::ivec3(target.normal),
                               lookDir, spot, snapRot, &spec)) {
                // against the piece aimed at (StructureManager::snapTo); a
                // door comes back sized to its doorway
                build.shownRot = snapRot;
                build.shownSpec = spec;
            } else if (t->foundation && structs.alignFoundation(build.selected, build.rot, target.blockPos,
                                                         glm::ivec3(target.normal), spot, build.shownSpec)) {
                // lined up with the foundation beside it (StructureManager::alignFoundation)
            } else if (t->snap > 0) {
                // on the grid of the foundation it's aimed at (floors
                // line up with it), else the world's
                glm::ivec3 g0(0);
                if (const StructureInstance* f = structs.instanceAt(target.blockPos);
                    f && structs.typeOf(*f) && structs.typeOf(*f)->foundation) {
                    glm::ivec3 hi;
                    structs.bounds(*f, g0, hi);
                }
                const VoxelVolume& shape = gStructures.shape(build.selected, build.shownSpec, 0, build.shownRot);
                glm::ivec3 o = spot - shape.anchor - g0;
                o.x = floorDiv(o.x, t->snap) * t->snap;
                o.z = floorDiv(o.z, t->snap) * t->snap;
                spot = o + g0 + shape.anchor;
            }
            build.spot = spot;
        }

        // Arrows / Page keys: lock, then move (Shift = 8)
        glm::ivec3 right(-fwd.z, 0, fwd.x);
        int n = gInput.isHeld(InputAction::Sprint) ? 8 : 1;
        struct { InputAction a; glm::ivec3 d; } keys[] = {
            { InputAction::NudgeForward, altFwd(fwd) },  { InputAction::NudgeBack, -altFwd(fwd) },
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
                                                     build.shownRot, survival, &support, build.shownSpec)
                                : "aim at the ground";
        const bool pay = survival && gStructures.charges(*t);
        if (previewWhy.empty() && pay)
            for (auto& [item, cnt] : gStructures.costOf(*t, build.shownSpec)) {
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
            WorldRequest q = request(WorldRequest::Op::Piece, player.activeInventory().heldDef().id);
            q.name = t->name; q.lo = build.spot; q.rot = build.shownRot; q.spec = build.shownSpec;
            submit(q, player, editor, structs);      // pays, checks again, places (WorldActions::placePiece)
        }
        (void)src; (void)support;
    }

    // ── Hoe ───────────────────────────────────────────────
    // Valheim style: T picks what it does, left click (held:
    // repeated) does it round where you aim. Every shape comes
    // from world/TerrainOps.h, the same functions the console's
    // flatten and the village roads use. Buildings are kept.
    HoeMode hoeMode = HoeMode::Level;           // (WorldActions: the modes, RAISE_MAX / LOWER_MAX)
    float   hoeTimer = 0.0f, blastCooldown = 0.0f;
    static const char* hoeName(HoeMode m) {
        switch (m) {
        case HoeMode::Level: return "level";
        case HoeMode::Raise: return "raise";
        case HoeMode::Lower: return "lower";
        default:             return "path";
        }
    }

    void hoeKeys(Player& player, WorldEditor& editor, StructureManager& structs, const ItemDef& held, float dt) {
        if (gInput.isPressed(InputAction::RotatePiece))
            hoeMode = (HoeMode)(((int)hoeMode + 1) % (int)HoeMode::COUNT);
        aimLabel = std::string("Hoe: ") + hoeName(hoeMode) + "   {rotate_piece}: change";
        if (!target.hit) return;
        float r = std::clamp(held.toolPower, 2.0f, 16.0f);
        glm::ivec3 c = target.blockPos;
        // show the patch it works on
        int ri = (int)std::ceil(r);
        int level = (int)std::floor(player.position.y + 0.01f);   // where you stand
        cubePlace.lo = glm::ivec3(c.x - ri, hoeMode == HoeMode::Level ? level - 1 : c.y, c.z - ri);
        cubePlace.hi = glm::ivec3(c.x + ri, hoeMode == HoeMode::Level ? level - 1 : c.y, c.z + ri);
        cubePlaceShown = true;

        hoeTimer = std::max(0.0f, hoeTimer - dt);
        if (!gInput.isHeld(InputAction::UseLeft) || hoeTimer > 0.0f) return;
        hoeTimer = HOE_EVERY;
        WorldRequest q = request(WorldRequest::Op::Hoe, held.id);
        q.mode = (int)hoeMode;
        submit(q, player, editor, structs);      // WorldActions::hoe
    }

    // ── Hammer, Copy tab: click a building to save it ─────
    uint32_t  copyFor = 0;          // the piece the outline was worked out for
    Blueprint copyShown;
    glm::ivec3 copyLo{ 0 };

    std::string mergeAsk;          // a just-copied blueprint whose palettes could merge (asking)

    void copyKeys(StructureManager& structs, BuildTool& build) {
        previewHas = false;
        const StructureInstance* a = target.hit ? structs.instanceAt(target.blockPos) : nullptr;
        if (!a || a->natural) {
            copyFor = 0;
            previewWhy = "aim at a building to copy it";
            return;
        }
        std::string why;
        if (a->id != copyFor) {     // work it out once per piece aimed at
            copyFor = a->id;
            if (!Blueprint::capture(structs, a->id, chests, copyShown, why, 600, &copyLo)) copyShown.parts.clear();
        }
        if (copyShown.parts.empty()) { previewWhy = "that is not a building piece"; return; }
        for (const BlueprintPlacement& p : copyShown.plan(copyLo, 0))
            previewBoxes.push_back({ p.lo, p.hi });
        previewWhy = "";
        aimLabel = "click: save this building, " + std::to_string(copyShown.parts.size()) + " pieces";
        if (gInput.isPressed(InputAction::UseLeft)) {
            Blueprint bp = copyShown;
            bp.name    = gBlueprints.freeName("building");
            bp.display = "Building " + bp.name.substr(std::min<size_t>(bp.name.size(), 9));
            if (bp.name == "building") bp.display = "Building";
            bp.tags    = { "player" };
            if (gBlueprints.save(bp)) {
                say("saved as " + bp.name + " - ready to place");
                build.selectBlueprint(bp.name);
                if (bp.mergeable() > 0) mergeAsk = bp.name;    // ask before merging any palettes
            } else say("could not save the blueprint");
            copyFor = 0;
        }
    }

    // ── Hammer, Blueprints tab: place a whole building ────
    void blueprintKeys(Player& player, WorldEditor& editor, StructureManager& structs, BuildTool& build,
                       bool survival, EditSource src, float dt) {
        const Blueprint* bp = build.chosenBlueprint();
        if (const BuildPalette* pal = gBuildPalettes.find(build.style.paletteName)) build.style.palette = pal->spec;   // kept up with edits
        if (!bp) {
            previewHas = false;
            previewWhy = "{use_right}: choose a blueprint";
            return;
        }
        if (gInput.isPressed(InputAction::RotatePiece)) build.rot = (build.rot + 1) & 3;
        if (gInput.isPressed(InputAction::ToolCancel))  build.locked = false;
        const glm::ivec3 sz = bp->sizeTurned(build.rot);
        glm::ivec3 fwd = std::fabs(lookDir.x) > std::fabs(lookDir.z)
            ? glm::ivec3(lookDir.x > 0 ? 1 : -1, 0, 0)
            : glm::ivec3(0, 0, lookDir.z > 0 ? 1 : -1);

        // centred on the ground aimed at (build.spot: its low corner)
        previewHas = build.locked || target.hit;
        if (!build.locked && target.hit) {
            glm::ivec3 at = target.blockPos + glm::ivec3(target.normal);
            build.spot = glm::ivec3(at.x - sz.x / 2, at.y - build.style.sink, at.z - sz.z / 2);
        }
        glm::ivec3 right(-fwd.z, 0, fwd.x);
        int n = gInput.isHeld(InputAction::Sprint) ? 8 : 1;
        struct { InputAction a; glm::ivec3 d; } keys[] = {
            { InputAction::NudgeForward, altFwd(fwd) },  { InputAction::NudgeBack, -altFwd(fwd) },
            { InputAction::NudgeRight, right },  { InputAction::NudgeLeft, -right },
            { InputAction::NudgeUp, {0, 1, 0} }, { InputAction::NudgeDown, {0, -1, 0} },
        };
        for (int i = 0; i < 6; i++)
            if (repeatKey(keys[i].a, nudgeTimers[i], dt)) {
                if (!build.locked) {
                    if (!target.hit)
                        build.spot = glm::ivec3(glm::floor(player.position)) + fwd * (sz.x + 8);
                    build.locked = true;
                    previewHas = true;
                }
                build.spot += keys[i].d * n;
            }
        previewSpot = build.spot;
        if (!previewHas) { previewWhy = "aim at the ground"; return; }

        std::vector<BlueprintPlacement> plan = bp->plan(build.spot, build.rot, &build.style);
        for (const BlueprintPlacement& p : plan) previewBoxes.push_back({ p.lo, p.hi });
        previewWhy = Blueprint::check(structs, plan, build.style.sitePrep);
        const bool pay = survival && gStructures.costsEnabled;
        std::map<uint16_t, int> cost = pay ? bp->cost(&build.style) : std::map<uint16_t, int>();
        if (previewWhy.empty())
            for (auto& [item, cnt] : cost)
                if (player.inventory.count(item) < cnt) {
                    previewWhy = "needs " + std::to_string(cnt) + " " + gItems.get(item).displayName;
                    break;
                }
        aimLabel = bp->display + "   {rotate_piece}: turn";

        if (gInput.isPressed(InputAction::UseLeft)) {
            if (!previewWhy.empty()) { say(previewWhy); return; }
            WorldRequest q = request(WorldRequest::Op::Blueprint, player.activeInventory().heldDef().id);
            q.name = bp->text(); q.lo = build.spot; q.rot = build.rot; q.style = build.style;
            submit(q, player, editor, structs);      // pays, checks again, builds (WorldActions::pasteBlueprint)
            build.locked = false;
        }
        (void)src;
    }


    // Middle click. Test Play: the block goes into the held
    // slot (or the first empty one) and becomes the dev
    // tools' material. Survival: select it if you have it.
    void pickBlock(Player& player, DevTools& dev, uint16_t blockID,
                   bool survival) {
        uint16_t item = gItems.itemForBlock(blockID);
        if (!item) return;
        Inventory& inv = player.activeInventory();
        for (int i = Inventory::FIRST_MAIN; i < Inventory::HOTBAR; i++)
            if (inv.slots[i].itemID == item) {
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
            for (int i = Inventory::FIRST_MAIN; i < Inventory::HOTBAR; i++)
                if (inv.slots[i].empty()) { slot = i; break; }
            if (slot < 0) return;
        }
        inv.setSlot(slot, item, 1);
        inv.select(slot);
    }
};
