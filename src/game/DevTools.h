#pragma once
#include "../world/WorldEditor.h"
#include "../world/VoxelVolume.h"
#include "../world/StructureCatalog.h"
#include "../physics/IPhysics.h"
#include "../entity/Player.h"
#include "ItemRegistry.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// DevTools — building operations shared by tools and commands
// ══════════════════════════════════════════════════════════
// Every operation here exists ONCE and is called from two
// places: the dev tool items (left/right click in Test Play)
// and the typed console commands (/set, /copy, /paste …).
// Scripts and the server will call the same functions later.
//
// All world changes go through WorldEditor, tagged with the
// source that asked for them.
//
// State kept here (and saved with the player):
//   selection  two corners, set by the wand or /pos1 /pos2
//   material   the block fill / brush / replace place
//   brush      radius for the brush and explosion tools
//   clipboard  what copy captured (not saved — use /save)
class DevTools {
public:
    // Explosion push on a body inside the blast, in voxels/s
    // at the centre, fading to nothing at the edge
    static constexpr float EXPLOSION_PUSH = 60.0f;
    static constexpr float BRUSH_MIN = 1.0f, BRUSH_MAX = 48.0f;

    // Where structure files live
    static constexpr const char* STRUCTURE_DIR = "data/structures";

    // ── State ─────────────────────────────────────────────
    bool       hasPos1 = false, hasPos2 = false;
    glm::ivec3 pos1 = {0, 0, 0}, pos2 = {0, 0, 0};
    uint16_t   material    = BLOCK_STONE;
    float      brushRadius = 6.0f;
    std::shared_ptr<VoxelVolume> clipboard;

    // Copy tool: a clicked point waiting for "corner 1 or 2?"
    bool       copyPending = false;
    glm::ivec3 copyPoint   = {0, 0, 0};

    // Paste tool: once nudged, the paste position is locked
    // here and no longer follows the crosshair
    bool       pasteLocked = false;
    glm::ivec3 pasteSpot   = {0, 0, 0};

    // Paste lands here: the locked spot, else the target
    glm::ivec3 pasteTarget(const RaycastResult& hit) const {
        if (pasteLocked) return pasteSpot;
        return hit.blockPos + glm::ivec3(hit.normal);
    }
    bool pasteHasTarget(const RaycastResult& hit) const {
        return pasteLocked || hit.hit;
    }

    // Move the locked paste (locks it first if needed).
    //   fallback — where to lock when the crosshair is not on
    //              any block (a spot in front of the player)
    void nudgePaste(const RaycastResult& hit, glm::ivec3 step,
                    glm::ivec3 fallback) {
        if (!clipboard) { log("clipboard is empty - copy something first"); return; }
        if (!pasteLocked) {
            pasteSpot   = hit.hit ? hit.blockPos + glm::ivec3(hit.normal)
                                  : fallback;
            pasteLocked = true;
            log("paste locked - arrows / Page Up / Page Down move it, Backspace releases");
        }
        pasteSpot += step;
    }

    void releasePaste() {
        if (pasteLocked) log("paste released - it follows the crosshair again");
        pasteLocked = false;
    }

    // The copy prompt: assign the clicked point
    void answerCopyPrompt(int which) {
        if (!copyPending) return;
        copyPending = false;
        setPos(which, copyPoint);
    }
    void cancelCopyPrompt() {
        if (copyPending) log("point not used");
        copyPending = false;
    }

    // Messages for the player (console log + HUD)
    std::function<void(const std::string&)> log =
        [](const std::string&) {};

    // Point at the open world's editor (nullptr = no world)
    void attach(WorldEditor* ed) { editor = ed; }
    bool ready() const { return editor != nullptr; }

    // ── Selection ─────────────────────────────────────────
    bool hasSelection() const { return hasPos1 && hasPos2; }
    glm::ivec3 selMin()  const { return glm::min(pos1, pos2); }
    glm::ivec3 selMax()  const { return glm::max(pos1, pos2); }
    glm::ivec3 selSize() const { return selMax() - selMin() + glm::ivec3(1); }
    size_t     selVoxels() const {
        glm::ivec3 s = selSize();
        return (size_t)s.x * s.y * s.z;
    }

    void setPos(int which, glm::ivec3 p) {
        if (which == 1) { pos1 = p; hasPos1 = true; }
        else            { pos2 = p; hasPos2 = true; }
        std::string msg = "corner " + std::to_string(which) + " = " +
                          str(p);
        if (hasSelection()) msg += "  (" + sizeStr(selSize()) + ")";
        log(msg);
    }

    void clearSelection() {
        hasPos1 = hasPos2 = false;
        log("selection cleared");
    }

    // ── Operations ────────────────────────────────────────
    // Each returns quietly if no world is open. Big jobs are
    // queued by the editor and report when finished.

    void fillSelection(uint16_t id, EditSource src) {
        if (!needSelection()) return;
        EditBatch b = WorldEditor::box(pos1, pos2, id, src,
            id == BLOCK_AIR ? "clear" : "fill");
        report(b, id == BLOCK_AIR ? "cleared" : "filled");
        editor->submit(std::move(b));
    }

    void replaceSelection(uint16_t from, uint16_t to, EditSource src) {
        if (!needSelection()) return;
        EditBatch b = WorldEditor::box(pos1, pos2, to, src, "replace");
        b.onlyReplace = from;
        report(b, "replaced " + blockName(from) + " ->");
        editor->submit(std::move(b));
    }

    void sphere(glm::vec3 centre, float radius, uint16_t id,
                EditSource src) {
        if (!editor) return;
        EditBatch b = WorldEditor::sphere(centre, radius, id, src,
            id == BLOCK_AIR ? "carve" : "sphere");
        editor->submit(std::move(b));
    }

    // Crater plus a push on the player if they are close.
    // (Other bodies get pushed once entities exist.)
    void explode(glm::vec3 centre, float radius, EditSource src,
                 Player* player) {
        if (!editor) return;
        editor->submit(WorldEditor::explosion(editorWorld(), centre,
                                              radius, src));
        if (player && player->rules().hasBody) {
            glm::vec3 mid = player->position +
                glm::vec3(0.0f, player->height * 0.5f, 0.0f);
            glm::vec3 d = mid - centre;
            float dist = glm::length(d);
            float reach = radius * 2.0f;
            if (dist < reach) {
                glm::vec3 dir = dist > 0.01f ? d / dist
                                             : glm::vec3(0, 1, 0);
                dir.y = std::max(dir.y, 0.35f);   // always some lift
                player->velocity += glm::normalize(dir) *
                    EXPLOSION_PUSH * (1.0f - dist / reach);
                player->grounded = false;
            }
        }
    }

    void copySelection() {
        if (!needSelection()) return;
        clipboard = std::make_shared<VoxelVolume>(
            editor->copy(pos1, pos2));
        log("copied " + sizeStr(clipboard->size) +
            " - paste places its bottom centre on the target");
    }

    //   withAir — also paste the clipboard's air (carves)
    void pasteAt(glm::ivec3 target, EditSource src, bool withAir = false) {
        if (!editor) return;
        if (!clipboard || clipboard->empty()) {
            log("clipboard is empty - copy something first");
            return;
        }
        EditBatch b = WorldEditor::paste(*clipboard, target, !withAir, src);
        report(b, "pasted");
        editor->submit(std::move(b));
    }

    void rotateClipboard(int quarterTurns) {
        if (!clipboard) { log("clipboard is empty"); return; }
        clipboard = std::make_shared<VoxelVolume>(
            clipboard->rotatedY(quarterTurns));
        log("clipboard turned " + std::to_string(quarterTurns * 90 % 360) +
            "deg -> " + sizeStr(clipboard->size));
    }

    void flipClipboard(char axis) {
        if (!clipboard) { log("clipboard is empty"); return; }
        clipboard = std::make_shared<VoxelVolume>(
            clipboard->mirrored(axis));
        log(std::string("clipboard mirrored along ") + axis);
    }

    void undo(int times = 1) {
        if (!editor) return;
        for (int i = 0; i < times; i++) {
            std::string what = editor->undo();
            if (what.empty()) { log("nothing to undo"); return; }
            log("undid " + what);
        }
    }

    void redo(int times = 1) {
        if (!editor) return;
        for (int i = 0; i < times; i++) {
            std::string what = editor->redo();
            if (what.empty()) { log("nothing to redo"); return; }
            log("redid " + what);
        }
    }

    // Structures: the clipboard to / from data/structures
    bool saveStructure(const std::string& name) {
        if (!clipboard) { log("clipboard is empty"); return false; }
        if (!safeName(name)) { log("bad name: " + name); return false; }
        auto path = structurePath(name);
        if (!clipboard->save(path)) {
            log("could not write " + path.string());
            return false;
        }
        log("saved " + path.string());
        return true;
    }

    // Put a catalog structure (a tree, or a saved file) in
    // the clipboard, ready for the Paste tool
    bool loadCatalogEntry(const StructureEntry& e) {
        if (!e.builtIn) return loadStructure(e.file);
        ChunkPtr chunk = e.make();
        auto vol = std::make_shared<VoxelVolume>(volumeFromStructureChunk(*chunk));
        if (vol->empty()) { log(e.name + " is empty"); return false; }
        clipboard   = vol;
        pasteLocked = false;
        log("loaded " + e.name + " (" + sizeStr(vol->size) + ")");
        return true;
    }

    //   name — a saved structure, or a built-in one written
    //          with underscores ("oak_tree", "ancient_oak")
    bool loadStructure(const std::string& name) {
        for (const StructureEntry& e : structureCatalog()) {
            if (!e.builtIn) continue;
            std::string key = e.name;
            for (char& c : key) c = (c == ' ') ? '_' : (char)std::tolower((unsigned char)c);
            if (key == name) return loadCatalogEntry(e);
        }
        if (!safeName(name)) { log("bad name: " + name); return false; }
        auto vol = std::make_shared<VoxelVolume>();
        if (!vol->load(structurePath(name))) {
            log("no structure called " + name);
            return false;
        }
        clipboard   = vol;
        pasteLocked = false;
        log("loaded " + name + " into the clipboard (" +
            sizeStr(vol->size) + ")");
        return true;
    }

    static std::vector<std::string> listStructures() {
        std::vector<std::string> out;
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(STRUCTURE_DIR, ec))
            if (e.path().extension() == ".vxvol")
                out.push_back(e.path().stem().string());
        std::sort(out.begin(), out.end());
        return out;
    }

    void setMaterial(uint16_t id) {
        material = id;
        log("material: " + blockName(id));
    }

    void setBrush(float r) {
        brushRadius = std::clamp(r, BRUSH_MIN, BRUSH_MAX);
    }

    // ── Dev tool items ────────────────────────────────────
    // What a held dev tool does on a click. 'primary' is the
    // left button. 'hit' is the block under the crosshair.
    void useTool(DevToolKind kind, bool primary,
                 const RaycastResult& hit, Player& player) {
        if (!editor) return;
        const EditSource src = EditSource::Dev;
        glm::ivec3 block   = hit.blockPos;
        glm::ivec3 against = hit.blockPos + glm::ivec3(hit.normal);

        switch (kind) {
        case DevToolKind::Wand:
            if (!hit.hit) return;
            setPos(primary ? 1 : 2, block);
            break;
        case DevToolKind::Fill:
            fillSelection(primary ? material : BLOCK_AIR, src);
            break;
        case DevToolKind::Replace:
            if (!hit.hit) return;
            // left: the clicked block type → material
            // right: material → air (clears just that block type)
            if (primary) replaceSelection((uint16_t)hit.blockID, material, src);
            else         replaceSelection(material, BLOCK_AIR, src);
            break;
        case DevToolKind::Brush:
            if (!hit.hit) return;
            sphere(glm::vec3(primary ? against : block) + 0.5f,
                   brushRadius, primary ? material : BLOCK_AIR, src);
            break;
        case DevToolKind::Copy:
            // left: pick a point, then answer 1 or 2
            // right: copy the selection
            if (primary) {
                if (!hit.hit) return;
                copyPoint   = block;
                copyPending = true;
            } else {
                copySelection();
            }
            break;
        case DevToolKind::Paste:
            if (primary) {
                if (pasteHasTarget(hit)) pasteAt(pasteTarget(hit), src);
            } else {
                rotateClipboard(1);
            }
            break;
        case DevToolKind::Explode:
            if (!hit.hit) return;
            explode(glm::vec3(block) + 0.5f,
                    primary ? brushRadius : brushRadius * 2.0f,
                    EditSource::Explosion, &player);
            break;
        case DevToolKind::Breaker:
            if (!hit.hit) return;
            if (primary)
                editor->apply(WorldEditor::box(block, block, BLOCK_AIR,
                                               src, "break"));
            else
                editor->apply(WorldEditor::box(block - glm::ivec3(1),
                    block + glm::ivec3(1), BLOCK_AIR, src, "break 3x3"));
            break;
        default:
            break;
        }
    }

    // One-line help for the HUD while a dev tool is held
    static const char* toolHint(DevToolKind kind) {
        switch (kind) {
        case DevToolKind::Wand:    return "L: corner 1   R: corner 2";
        case DevToolKind::Fill:    return "L: fill selection   R: clear selection";
        case DevToolKind::Replace: return "L: clicked type -> material   R: material -> air";
        case DevToolKind::Brush:   return "L: sphere   R: carve   Up/Down: size";
        case DevToolKind::Copy:    return "L: pick a point (then 1 or 2)   R: copy selection";
        case DevToolKind::Paste:   return "L: paste   R: turn 90   arrows/PgUp/PgDn: move   Bksp: release";
        case DevToolKind::Explode: return "L: blast   R: double blast   Up/Down: size";
        case DevToolKind::Breaker: return "L: break   R: break 3x3x3";
        default:                   return "";
        }
    }

    // ── Save / load (lines in player.dat) ─────────────────
    void write(std::ostream& out) const {
        out << "devtools.material=" << blockName(material) << "\n";
        out << "devtools.brush=" << brushRadius << "\n";
        if (hasPos1) out << "devtools.pos1=" << pos1.x << " " << pos1.y
                         << " " << pos1.z << "\n";
        if (hasPos2) out << "devtools.pos2=" << pos2.x << " " << pos2.y
                         << " " << pos2.z << "\n";
    }

    bool read(const std::string& key, const std::string& value) {
        if (key == "devtools.material") {
            uint16_t id = gRegistry.getIDByName(value);
            if (id) material = id;
        } else if (key == "devtools.brush") {
            setBrush((float)std::atof(value.c_str()));
        } else if (key == "devtools.pos1" || key == "devtools.pos2") {
            glm::ivec3 p;
            if (std::sscanf(value.c_str(), "%d %d %d",
                            &p.x, &p.y, &p.z) == 3) {
                if (key.back() == '1') { pos1 = p; hasPos1 = true; }
                else                   { pos2 = p; hasPos2 = true; }
            }
        } else {
            return false;
        }
        return true;
    }

    void resetForNewWorld() {
        copyPending = pasteLocked = false;
        hasPos1 = hasPos2 = false;
        material    = BLOCK_STONE;
        brushRadius = 6.0f;
        clipboard.reset();
    }

    // ── Helpers (also used by commands) ───────────────────
    static std::string blockName(uint16_t id) {
        return id == BLOCK_AIR ? "air" : gRegistry.get(id).name;
    }
    static std::string str(glm::ivec3 p) {
        char b[64];
        std::snprintf(b, sizeof(b), "%d %d %d", p.x, p.y, p.z);
        return b;
    }
    static std::string sizeStr(glm::ivec3 s) {
        char b[96];
        std::snprintf(b, sizeof(b), "%d x %d x %d = %lld voxels",
                      s.x, s.y, s.z, (long long)s.x * s.y * s.z);
        return b;
    }

private:
    WorldEditor* editor = nullptr;

    const ChunkManager& editorWorld() const { return editor->worldRef(); }

    bool needSelection() {
        if (!editor) return false;
        if (!hasSelection()) {
            log("no selection - set both corners with the wand or /pos1 /pos2");
            return false;
        }
        return true;
    }

    // Report the outcome when a batch finishes (it may be
    // queued and finish a few frames later)
    void report(EditBatch& b, const std::string& verb) {
        auto logFn = log;
        b.onDone = [logFn, verb](const EditResult& r) {
            std::string msg = verb + " " + std::to_string(r.changed) +
                              " voxels";
            if (r.skippedUnloaded)
                msg += "  (" + std::to_string(r.skippedUnloaded) +
                       " skipped: chunk not loaded)";
            logFn(msg);
        };
    }

    static bool safeName(const std::string& n) {
        if (n.empty() || n.size() > 64) return false;
        for (char c : n)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
                return false;
        return true;
    }

    static std::filesystem::path structurePath(const std::string& name) {
        return std::filesystem::path(STRUCTURE_DIR) / (name + ".vxvol");
    }
};
