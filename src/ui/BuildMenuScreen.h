#pragma once
#include "../game/BuildPlan.h"
#include "UIScreen.h"
#include "ItemIcon.h"
#include "KeyText.h"
#include "../entity/Player.h"
#include "../game/BuildTool.h"
#include "../game/Palettes.h"
#include "../game/Blueprint.h"
#include "../game/StructureRegistry.h"
#include "../input/InputManager.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Build menu — what the Builder's Hammer can place
// ══════════════════════════════════════════════════════════
// Opened by right clicking with the hammer (Survival and
// Test Play). Three tabs:
//   Pieces      every structure marked menu = true in
//               structures.toml, filtered by part group
//               (foundations, walls, roofs …) along the top. A wall,
//               post or door chosen: its sizes on the right (width,
//               height for every wall and post, the opening)
//   Palettes    what walls, posts and doors are made of (game/
//               Palettes.h): equip one, edit it (a block per role, the
//               plain walls' finish), make a new one; a picture of
//               a wall and door in it, outside and in
//   Blueprints  whole buildings (data/blueprints): pick one to
//               place, or "Copy a building" to save one from
//               the world. The style column restyles them as
//               they go down (stone walls, a tile roof …)
// Click a piece or blueprint to pick it and go back to building.
// Esc or right click closes without changing anything; the
// mouse wheel scrolls a long list.
class BuildMenuScreen : public UIScreen {
public:
    explicit BuildMenuScreen(GLFWwindow* win) : window(win) {}

    void setContext(Player* p, BuildTool* b) {
        player = p; build = b;
        tab = (b && b->mode == HammerMode::Plan) ? 3 : (b && b->mode != HammerMode::Piece) ? 2 : (tab == 1 ? 1 : 0);
    }

    void onEnter() override {
        mouseDown = mousePrev = true;      // ignore the opening click
        rightDown = rightPrev = true;
        scroll = 0.0f;
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        if (!player || !build) return ScreenResult::CloseMenu;
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        mousePrev = mouseDown; rightPrev = rightDown;
        gNav.cursor(window, &mx, &my);
        mouseDown = gNav.button(window, GLFW_MOUSE_BUTTON_LEFT);
        rightDown = gNav.button(window, GLFW_MOUSE_BUTTON_RIGHT);
        result = ScreenResult::Continue;

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);
        float pw = std::min(sw - 20.0f, 1100.0f), ph = std::min(sh - 20.0f, 760.0f);
        float px = (sw - pw) * 0.5f, py = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);

        // tabs
        const char* tabs[4] = { "Pieces", "Palettes", "Blueprints", "Build" };
        float tx = px + 16;
        for (int i = 0; i < 4; i++) {
            float tw = (float)ui.textWidth(tabs[i], 2) + 30, ty = py + 12;
            bool hov = hovered(tx, ty, tw, 34);
            ui.drawRect(tx, ty, tw, 34, tab == i ? theme.bgHover : theme.inputBg);
            ui.drawBorder(tx, ty, tw, 34, tab == i ? theme.accent : theme.border, tab == i ? 2.0f : 1.0f);
            ui.drawTextCentered(tabs[i], tx, ty, tw, 34, hov ? theme.textHover : theme.text, 2);
            if (hov && clicked() && tab != i) { tab = i; scroll = 0.0f; }
            tx += tw + 10;
        }
        bool survival = player->mode == GameMode::Survival;
        ui.drawText(!gStructures.costsEnabled ? "building is free for now" : survival ? "costs come from your inventory" : "Test Play: free",
                    tx + 20, py + 22, theme.textDim, 2);

        float top = py + 56, bottom = py + ph - 30;
        if (tab == 0)      piecesTab(ui, px, top, pw, bottom);
        else if (tab == 1) palettesTab(ui, px, top, pw, bottom);
        else if (tab == 2) blueprintsTab(ui, px, top, pw, bottom);
        else               buildTab(ui, px, top, pw, bottom);

        keytext::draw(ui, tab == 0 ? "{click} to choose   {rotate_piece} turns it   {nudge} {nudge_up} {nudge_down} fine-tune"
                        : tab == 1 ? "{click} a palette to equip it: walls, posts and doors you place take it   < > change a block"
                        : tab == 2 ? "{click} a blueprint to place it   {rotate_piece} turns it   {nudge} fine-tune"
                                   : "{click} a tool, then drag in the world   {undo} undoes   {build_camera}: the build camera",
                      px + 16, py + ph - 22, theme.textDim, 1, theme.accent);
        if (rightDown && !rightPrev) result = ScreenResult::CloseMenu;
        ui.end();
        return result;
    }

private:
    GLFWwindow* window;
    Player*     player = nullptr;
    BuildTool*  build  = nullptr;
    int         tab = 0;
    float       scroll = 0.0f;
    ScreenResult result = ScreenResult::Continue;
    double mx = 0, my = 0;
    bool mouseDown = false, mousePrev = false, rightDown = false, rightPrev = false;
    const float RED[4] = {255, 110, 90, 255};

    // The part groups in menu order, with their names
    struct Group { const char* id; const char* label; };
    static const std::vector<Group>& groups() {
        static const std::vector<Group> g = {
            { "foundation", "Foundations" }, { "wall", "Walls" }, { "gable", "Gables" },
            { "pillar", "Posts" }, { "beam", "Beams" }, { "floor", "Floors" }, { "roof", "Roofs" },
            { "stairs", "Stairs" }, { "door", "Doors" }, { "station", "Stations" },
            { "furniture", "Furniture" }, { "chest", "Chests" }, { "light", "Lights" }, { "fence", "Fences" },
            { "", "Other" },
        };
        return g;
    }
    static bool known(const std::string& group) {
        for (const Group& g : groups()) if (*g.id && group == g.id) return true;
        return false;
    }

    // ── Pieces ────────────────────────────────────────────
    void piecesTab(UIRenderer& ui, float px, float top, float pw, float bottom) {
        const auto& theme = gResourcePacks.getTheme();
        bool survival = player->mode == GameMode::Survival;
        std::vector<uint16_t> all = gStructures.buildMenu();

        // group chips (only groups that have pieces)
        float cx = px + 16, cy = top;
        for (const Group& g : groups()) {
            bool any = false;
            for (uint16_t id : all) {
                const StructureType* t = gStructures.get(id);
                if (t && (*g.id ? t->group == g.id : !known(t->group))) { any = true; break; }
            }
            if (!any) continue;
            float w = (float)ui.textWidth(g.label, 2) + 20;
            if (cx + w > px + pw - 16) { cx = px + 16; cy += 34; }
            bool on = build->group == g.id, hov = hovered(cx, cy, w, 28);
            ui.drawRect(cx, cy, w, 28, on ? theme.bgHover : theme.inputBg);
            ui.drawBorder(cx, cy, w, 28, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
            ui.drawTextCentered(g.label, cx, cy, w, 28, hov ? theme.textHover : theme.text, 2);
            if (hov && clicked()) { build->group = g.id; scroll = 0.0f; }
            cx += w + 6;
        }

        std::vector<uint16_t> list;
        for (uint16_t id : all) {
            const StructureType* t = gStructures.get(id);
            if (t && (build->group.empty() ? !known(t->group) : t->group == build->group)) list.push_back(id);
        }
        // the chosen piece's sizes, on the right
        const StructureType* chosen = build->mode == HammerMode::Piece ? build->type() : nullptr;
        if (chosen && chosen->sizable()) {
            const float sizeW = 360.0f;
            sizesPanel(ui, *chosen, px + pw - 16 - sizeW, cy + 40, sizeW, bottom);
            pw -= sizeW + 16;
        }
        const float tileH = 54.0f, gap = 6.0f;
        // as many columns as the longest name allows
        float longest = 0;
        for (uint16_t id : list) longest = std::max(longest, (float)ui.textWidth(gStructures.get(id)->displayName, 2));
        const int cols = std::clamp(int((pw - 32 + gap) / (longest + 70 + gap)), 1, 3);
        const float tileW = (pw - 32 - (cols - 1) * gap) / cols;
        float listTop = cy + 40;
        int rows = ((int)list.size() + cols - 1) / cols;
        float total = rows * (tileH + gap);
        scrollBy(total, bottom - listTop);
        for (size_t i = 0; i < list.size(); i++) {
            const StructureType* t = gStructures.get(list[i]);
            float x = px + 16 + (i % cols) * (tileW + gap);
            float y = listTop + (i / cols) * (tileH + gap) - scroll;
            if (y < listTop - 1 || y + tileH > bottom) continue;
            bool hov = hovered(x, y, tileW, tileH);
            bool on  = build->mode == HammerMode::Piece && build->selected == t->id;
            bool canPay = !survival || !gStructures.costsEnabled || affordable(t->cost);
            ui.drawRect(x, y, tileW, tileH, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, tileW, tileH, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
            // the material it is built from, as its block icon
            const VoxelTypeDef& b = gRegistry.get(t->block);
            uint16_t mat = gItems.itemForBlock(t->block);
            if (mat) drawItemIcon(ui, gItems.get(mat), x + 6, y + 6, 42);
            else {
                float c[4] = { b.color[0] * 255, b.color[1] * 255, b.color[2] * 255, 255 };
                ui.drawRect(x + 6, y + 6, 42, 42, c);
            }
            ui.drawText(t->displayName, x + 56, y + 8, hov ? theme.textHover : theme.text, 2);
            std::string sizeText = t->feet();
            if (t->sizable()) {                  // as the hammer will place it
                glm::ivec3 z = gStructures.shape(t->id, build->specFor(*t), 0, 0).size;
                auto f = [](int v) { return v % 3 ? std::to_string(v / 3) + "." + std::to_string(v % 3 * 10 / 3) : std::to_string(v / 3); };
                sizeText = f(z.x) + " x " + f(z.y) + " x " + f(z.z) + " ft";
            }
            ui.drawText(sizeText + "   " + t->costText(gStructures.costsEnabled), x + 56, y + 32,
                        canPay ? theme.textDim : RED, 1);
            if (hov && clicked()) {
                build->select(t->id);
                result = ScreenResult::CloseMenu;
            }
        }
    }

    // ── Blueprints ────────────────────────────────────────
    void blueprintsTab(UIRenderer& ui, float px, float top, float pw, float bottom) {
        const auto& theme = gResourcePacks.getTheme();
        bool survival = player->mode == GameMode::Survival;
        const float styleW = 400.0f;
        const float listW = pw - 32 - styleW - 16;

        // Copy a building
        {
            float x = px + 16, y = top, w = listW, h = 44;
            bool hov = hovered(x, y, w, h), on = build->mode == HammerMode::Copy;
            ui.drawRect(x, y, w, h, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, w, h, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
            ui.drawText("Copy a building", x + 12, y + 6, hov ? theme.textHover : theme.text, 2);
            ui.drawText("click any piece of one to save it as a blueprint", x + 12, y + 28, theme.textDim, 1);
            if (hov && clicked()) {
                build->mode = HammerMode::Copy;
                build->locked = false;
                result = ScreenResult::CloseMenu;
            }
        }

        // the blueprints
        const std::vector<Blueprint>& list = gBlueprints.list();
        const float tileH = 54.0f, gap = 6.0f;
        float listTop = top + 54;
        scrollBy(list.size() * (tileH + gap), bottom - listTop);
        if (list.empty())
            ui.drawText("No blueprints yet: copy a building first.", px + 16, listTop + 10, theme.textDim, 2);
        for (size_t i = 0; i < list.size(); i++) {
            const Blueprint& bp = list[i];
            float x = px + 16, y = listTop + i * (tileH + gap) - scroll;
            if (y < listTop - 1 || y + tileH > bottom) continue;
            bool hov = hovered(x, y, listW, tileH);
            bool on = build->mode == HammerMode::Blueprint && build->blueprint == bp.name;
            auto cost = bp.cost(&build->style);
            bool canPay = !survival || !gStructures.costsEnabled || affordable(cost);
            ui.drawRect(x, y, listW, tileH, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, listW, tileH, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
            ui.drawText(bp.display, x + 12, y + 8, hov ? theme.textHover : theme.text, 2);
            std::string tags;
            for (const std::string& t : bp.tags) tags += (tags.empty() ? "" : ", ") + t;
            std::string info = std::to_string(bp.size.x / 3) + " x " + std::to_string(bp.size.z / 3) + " ft   " +
                               std::to_string(bp.parts.size()) + " pieces" + (tags.empty() ? "" : "   " + tags);
            if (gStructures.costsEnabled) info += "   " + costText(cost);
            ui.drawText(info, x + 12, y + 32, canPay ? theme.textDim : RED, 1);
            if (hov && clicked()) {
                build->selectBlueprint(bp.name);
                result = ScreenResult::CloseMenu;
            }
        }

        // the style: materials per part group, chests, ground
        float sx = px + pw - 16 - styleW, sy = top;
        ui.drawText("Style", sx, sy + 4, theme.title, 2);
        sy += 34;
        struct Row { const char* group; const char* label; };
        const Row rows[] = { { "foundation", "Foundations" }, { "wall", "Walls" }, { "gable", "Gables" },
                             { "floor", "Floors" }, { "roof", "Roofs" }, { "pillar", "Posts" } };
        for (const Row& r : rows) {
            std::vector<std::string> mats = gStructures.materialsOfGroup(r.group);
            if (mats.empty()) continue;
            std::string cur = build->style.materialFor(r.group);
            if (choice(ui, sx, sy, styleW, r.label, cur.empty() ? std::string("as built") : cur)) {
                // cycle: as built → each material → as built
                auto it = std::find(mats.begin(), mats.end(), cur);
                if (cur.empty()) build->style.materials[r.group] = mats[0];
                else if (it == mats.end() || it + 1 == mats.end()) build->style.materials.erase(r.group);
                else build->style.materials[r.group] = *(it + 1);
            }
            sy += 40;
        }
        {
            // a palette over every wall, post and door (as built: its own)
            const std::vector<BuildPalette>& pals = gBuildPalettes.list();
            const BuildPalette* cur = gBuildPalettes.find(build->style.paletteName);
            if (choice(ui, sx, sy, styleW, "Palette", cur ? cur->display : std::string("as built"))) {
                int i = -1;
                for (size_t k = 0; k < pals.size(); k++) if (cur && pals[k].name == cur->name) i = (int)k;
                i++;
                if (i >= (int)pals.size()) { build->style.paletteName.clear(); build->style.palette = PieceSpec(); }
                else { build->style.paletteName = pals[i].name; build->style.palette = pals[i].spec; }
            }
            sy += 40;
        }
        sy += 8;
        if (choice(ui, sx, sy, styleW, "Chests", build->style.rollLoot ? "roll loot" : "saved items"))
            build->style.rollLoot = !build->style.rollLoot;
        sy += 40;
        if (choice(ui, sx, sy, styleW, "Ground", build->style.sitePrep ? "level it" : "as it is"))
            build->style.sitePrep = !build->style.sitePrep;
        sy += 40;
        if (choice(ui, sx, sy, styleW, "Sunk into ground", std::to_string(build->style.sink / 3) + " ft"))
            build->style.sink = (build->style.sink + 3) % 12;
        sy += 40;
        if (choice(ui, sx, sy, styleW, "Reset style", ""))
            build->style = BlueprintStyle();
    }

    // ── Build: buildings as plans (game/BuildPlan.h) ──────
    // The tools (BuildTool::planTool), the sizes new ones take, the
    // Paint tool's block. The equipped palette (Palettes tab) gives
    // every role's block; a plan keeps the one it was laid with.
    void buildTab(UIRenderer& ui, float px, float top, float pw, float bottom) {
        (void)bottom;
        const auto& theme = gResourcePacks.getTheme();
        float x = px + 20, y = top + 6;
        ui.drawText("Tools", x, y, theme.textDim, 2);
        y += 26;
        static const char* HELP[] = {
            "drag a rectangle on the ground: a foundation (3 ft cells); upstairs: a floor",
            "drag a rectangle on a foundation: four walls round a room",
            "drag from a point: a wall, straight, 45 or 2-to-1 (sprint: off the grid)",
            "click: a pillar, through any wall there",
            "click a wall: a frame there, with a door, a window or open",
            "click a building: a roof over it (drag: a rectangle); on a roof: restyle, T turns it",
            "drag the way it climbs: a stair up to the next level (the floor above opens over it)",
            "drag: a fence or railing, one voxel wide; over a stair it follows the steps",
            "click a wall's side or a roof: paint it",
            "click a wall: its level 1 ft taller (crouch: lower; sprint: that wall only); a roof: its peak; a foundation: the floor",
            "click a roof, frame, pillar, wall or foundation: take it away",
        };
        // the tools in rows of five; the help for the one pointed at (else the one chosen)
        int shown = (int)build->planTool;
        {
            const float bw = 186, bh = 34;
            for (int i = 0; i < (int)PlanTool::COUNT; i++) {
                float bx = x + (i % 5) * (bw + 8), by = y + (i / 5) * (bh + 6);
                bool on = build->mode == HammerMode::Plan && (int)build->planTool == i;
                bool hov = hovered(bx, by, bw, bh);
                if (hov) shown = i;
                ui.drawRect(bx, by, bw, bh, on ? theme.bgHover : theme.inputBg);
                ui.drawBorder(bx, by, bw, bh, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
                ui.drawTextCentered(planToolName((PlanTool)i), bx, by, bw, bh, hov ? theme.textHover : theme.text, 2);
                if (hov && clicked()) { build->mode = HammerMode::Plan; build->planTool = (PlanTool)i; build->planDrag = false; }
            }
            y += ((int)PlanTool::COUNT + 4) / 5 * (bh + 6) + 4;
            ui.drawText(HELP[std::clamp(shown, 0, (int)PlanTool::COUNT - 1)], x, y, theme.textDim, 1);
            y += 16;
        }
        y += 14;
        ui.drawText("Sizes", x, y, theme.textDim, 2);
        y += 28;
        auto stepper = [&](const char* label, std::string value, int& v, int step, int lo, int hi) {
            ui.drawText(label, x, y + 8, theme.text, 2);
            float bx = x + 300;
            bool l = hovered(bx, y, 34, 30), r = hovered(bx + 150, y, 34, 30);
            ui.drawRect(bx, y, 34, 30, l ? theme.bgHover : theme.inputBg);
            ui.drawTextCentered("<", bx, y, 34, 30, theme.text, 2);
            ui.drawTextCentered(value.c_str(), bx + 34, y, 116, 30, theme.textHover, 2);
            ui.drawRect(bx + 150, y, 34, 30, r ? theme.bgHover : theme.inputBg);
            ui.drawTextCentered(">", bx + 150, y, 34, 30, theme.text, 2);
            if (l && clicked()) v = std::max(lo, v - step);
            if (r && clicked()) v = std::min(hi, v + step);
            y += 38;
        };
        auto ft = [](int v) { return std::to_string(v) + " vox"; };
        // a row of buttons, one chosen
        auto choice = [&](const char* label, const char* const* names, int n, int& v, float bw) {
            ui.drawText(label, x, y + 8, theme.text, 2);
            for (int i = 0; i < n; i++) {
                float bx = x + 300 + i * (bw + 6);
                bool on = v == i, hov = hovered(bx, y, bw, 30);
                ui.drawRect(bx, y, bw, 30, on || hov ? theme.bgHover : theme.inputBg);
                ui.drawBorder(bx, y, bw, 30, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
                ui.drawTextCentered(names[i], bx, y, bw, 30, theme.textHover, 2);
                if (hov && clicked()) v = i;
            }
            y += 38;
        };
        // the plans' own railings, on or off (every plan)
        auto railsRow = [&]() {
            static const char* RL[] = { "off", "on" };
            int r = build->planRails ? 1 : 0;
            choice("Auto railings", RL, 2, r, 90);
            if ((r != 0) != build->planRails) { build->planRails = r != 0; build->planRailsChanged = true; }
        };
        {
            // the storey the tools build on (Page Up / Down in the world too)
            ui.drawText("Level", x, y + 8, theme.text, 2);
            for (int i = 0; i < plan::MAX_LEVELS; i++) {
                float bx = x + 300 + i * 66;
                bool on = build->planLevel == i, hov = hovered(bx, y, 60, 30);
                ui.drawRect(bx, y, 60, 30, on || hov ? theme.bgHover : theme.inputBg);
                ui.drawBorder(bx, y, 60, 30, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
                ui.drawTextCentered(i == 0 ? "G" : std::to_string(i).c_str(), bx, y, 60, 30, theme.textHover, 2);
                if (hov && clicked()) build->planLevel = i;
            }
            y += 38;
            // the build camera's cutaway (V in the build camera)
            static const char* VW[] = { "all", "walls up", "walls down" };
            choice("Build camera view", VW, 3, build->planView, 150);
        }
        if (build->planTool == PlanTool::Stair) {
            static const char* SH[] = { "straight", "L", "U", "spiral" };
            choice("Shape", SH, 4, build->planStairShape, 120);
            if (build->planStairShape != 0) {
                static const char* TN[] = { "left", "right" };
                int t = build->planStairTurn > 0 ? 1 : 0;
                choice("Turns (T)", TN, 2, t, 90);
                build->planStairTurn = t ? 1 : -1;
            }
            stepper(build->planStairShape == 3 ? "Stair radius" : "Stair width", ft(build->planStairW), build->planStairW, 1, 3, 30);
            railsRow();
            y += 10;
        } else if (build->planTool == PlanTool::Fence) {
            ui.drawText("Style", x, y + 8, theme.text, 2);
            for (int i = 0; i < (int)plan::FenceStyle::COUNT; i++) {
                float bx = x + 300 + i * 136;
                bool on = build->planFenceStyle == i, hov = hovered(bx, y, 130, 30);
                ui.drawRect(bx, y, 130, 30, on || hov ? theme.bgHover : theme.inputBg);
                ui.drawBorder(bx, y, 130, 30, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
                ui.drawTextCentered(plan::fenceStyleName((plan::FenceStyle)i), bx, y, 130, 30, theme.textHover, 2);
                if (hov && clicked()) build->planFenceStyle = i;
            }
            y += 38;
            stepper("Fence height", ft(build->planFenceH), build->planFenceH, 1, 3, 30);
            railsRow();
            y += 10;
        } else if (build->planTool == PlanTool::Frame) {
            // what goes in it (choosing one sets its usual size), then the size
            static const char* FILL[] = { "open", "door", "window" };
            ui.drawText("Filled with", x, y + 8, theme.text, 2);
            for (int i = 0; i < 3; i++) {
                float bx = x + 300 + i * 116;
                bool on = build->planFill == i, hov = hovered(bx, y, 110, 30);
                ui.drawRect(bx, y, 110, 30, on || hov ? theme.bgHover : theme.inputBg);
                ui.drawBorder(bx, y, 110, 30, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
                ui.drawTextCentered(FILL[i], bx, y, 110, 30, theme.textHover, 2);
                if (hov && clicked() && build->planFill != i) {
                    build->planFill = i;
                    if (i == 1) { build->planOpenW = 10; build->planOpenH = 24; build->planSill = 0; }
                    if (i == 2) { build->planOpenW = 9; build->planOpenH = 12; build->planSill = 9; }
                }
            }
            y += 38;
            stepper("Frame width", ft(build->planOpenW), build->planOpenW, 1, build->planFill == 1 ? 9 : 2, 60);
            stepper("Frame height", ft(build->planOpenH), build->planOpenH, 1, build->planFill == 1 ? 21 : 2, 80);
            if (build->planFill != 1) stepper("Off the floor", ft(build->planSill), build->planSill, 1, build->planFill == 2 ? 1 : 0, 60);
            if (build->planFill == 1) {
                // which door: any door piece that sizes to its doorway
                ui.drawText("Door", x, y + 8, theme.text, 2);
                float cx = x + 300;
                for (uint16_t tid : gStructures.buildMenu()) {
                    const StructureType* t = gStructures.get(tid);
                    if (!t || t->kind != StructureKind::Door || !t->sizable()) continue;
                    std::string label = t->displayName;
                    float bw = 8.0f * label.size() + 16.0f;
                    if (cx + bw > px + pw - 20) { cx = x + 300; y += 36; }
                    bool on = build->planDoor == t->name || (build->planDoor.empty() && t->name == "plank_ledged_door");
                    bool hov = hovered(cx, y, bw, 30);
                    ui.drawRect(cx, y, bw, 30, on || hov ? theme.bgHover : theme.inputBg);
                    ui.drawBorder(cx, y, bw, 30, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
                    ui.drawTextCentered(label.c_str(), cx, y, bw, 30, theme.textHover, 1);
                    if (hov && clicked()) build->planDoor = t->name;
                    cx += bw + 6;
                }
                y += 38;
            }
            y += 10;
        } else if (build->planTool == PlanTool::Roof) {
            ui.drawText("Style", x, y + 8, theme.text, 2);
            for (int i = 0; i < 4; i++) {
                float bx = x + 300 + i * 96;
                bool on = build->planRoofStyle == i, hov = hovered(bx, y, 90, 30);
                ui.drawRect(bx, y, 90, 30, on || hov ? theme.bgHover : theme.inputBg);
                ui.drawBorder(bx, y, 90, 30, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
                ui.drawTextCentered(plan::roofStyleName((plan::RoofStyle)i), bx, y, 90, 30, theme.textHover, 2);
                if (hov && clicked()) build->planRoofStyle = i;
            }
            y += 38;
            stepper("Peak", ft(build->planPeak), build->planPeak, 3, 0, 60);
            stepper("Overhang", ft(build->planOverhang), build->planOverhang, 1, 0, 9);
            y += 10;
        } else {
            stepper("Foundation height", std::to_string(build->planFloor) + " vox", build->planFloor, 1, 1, plan::MAX_FLOOR);
            stepper("Wall height", std::to_string(build->planWallH / 3) + " ft", build->planWallH, 3, 6, plan::MAX_WALL_H);
            stepper("Wall thickness", std::to_string(build->planThick) + " vox", build->planThick, 1, 1, 7);
            stepper("Pillar size", std::to_string(build->planPillar) + " vox", build->planPillar, 1, 1, 15);
            {
                bool hov = hovered(x + 300, y, 184, 30);
                ui.drawText("Pillar shape", x, y + 8, theme.text, 2);
                ui.drawRect(x + 300, y, 184, 30, hov ? theme.bgHover : theme.inputBg);
                ui.drawTextCentered(build->planRound ? "round" : "square", x + 300, y, 184, 30, theme.textHover, 2);
                if (hov && clicked()) build->planRound = !build->planRound;
                y += 38;
            }
            y += 10;
        }
        const BuildPalette* pal = gBuildPalettes.find(build->palette.empty() ? std::string("tudor") : build->palette);
        ui.drawText(("Palette: " + (pal ? pal->display : std::string("(none)")) + "   (the Palettes tab equips another)").c_str(), x, y, theme.textDim, 2);
        y += 32;
        // the Paint tool's block: the palette's own, or a wall or inside block
        ui.drawText("Paint", x, y, theme.textDim, 2);
        y += 26;
        std::vector<std::string> opts = { "" };
        for (const char* role : { "fill", "paint", "panel", "base", "roof" })
            for (const std::string& n : gBuildPalettes.options(role))
                if (std::find(opts.begin(), opts.end(), n) == opts.end()) opts.push_back(n);
        float cx = x, cw = 150, ch = 30;
        for (const std::string& n : opts) {
            if (cx + cw > px + pw - 20) { cx = x; y += ch + 6; }
            bool on = build->planPaint == n, hov = hovered(cx, y, cw, ch);
            uint16_t id = n.empty() ? 0 : gRegistry.getIDByName(n);
            ui.drawRect(cx, y, cw, ch, on ? theme.bgHover : theme.inputBg);
            ui.drawBorder(cx, y, cw, ch, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
            if (id) {
                const VoxelTypeDef& d = gRegistry.get(id);
                float col[4] = { d.color[0] * 255, d.color[1] * 255, d.color[2] * 255, 255 };
                ui.drawRect(cx + 4, y + 4, 22, ch - 8, col);
            }
            std::string label = n.empty() ? "palette's" : n;
            if (label.size() > 14) label = label.substr(0, 13) + ".";
            ui.drawText(label.c_str(), cx + 32, y + 9, hov ? theme.textHover : theme.text, 1);
            if (hov && clicked()) { build->planPaint = n; build->mode = HammerMode::Plan; build->planTool = PlanTool::Paint; }
            cx += cw + 6;
        }
    }

    // ── Sizes (Pieces tab, a made piece chosen) ───────────
    // Width (this piece), height (every wall and post: a storey
    // matches), the opening; each kept to what works (a door 3 x 7
    // ft at least: StructureRegistry::resolve)
    void sizesPanel(UIRenderer& ui, const StructureType& t, float x, float y, float w, float bottom) {
        const auto& theme = gResourcePacks.getTheme();
        (void)bottom;
        PieceSpec full = gStructures.resolve(t, PieceSpec::parse(build->specFor(t)));
        PieceSpec& own = build->sizes[t.name];
        ui.drawText(t.displayName, x, y, theme.title, 2);
        y += 30;
        auto ft = [](int v) {
            char b[24];
            if (v % 3 == 0) std::snprintf(b, sizeof(b), "%d ft", v / 3);
            else std::snprintf(b, sizeof(b), "%d ft %d in", v / 3, (v % 3) * 4);
            return std::string(b);
        };
        const bool wall = t.model == "wall", door = t.model == "door", pillar = t.model == "pillar";
        int d;
        if (t.model == "foundation") {
            if ((d = stepper(ui, x, y, w, "Width", ft(full.w)))) own.w = full.w + 3 * d;
            y += 40;
            if ((d = stepper(ui, x, y, w, "Depth", ft(full.d)))) own.d = full.d + 3 * d;
            y += 40;
            if ((d = stepper(ui, x, y, w, "Height", ft(full.h)))) own.h = full.h + 3 * d;
            y += 40;
            ui.drawText("Any whole feet. Near another, it lines up on its grid.", x, y + 4, theme.textDim, 1);
            y += 24;
        } else if (t.model == "floor") {
            if ((d = stepper(ui, x, y, w, "Width", ft(full.w)))) own.w = full.w + 3 * d;
            y += 40;
            if ((d = stepper(ui, x, y, w, "Depth", ft(full.d)))) own.d = full.d + 3 * d;
            y += 40;
            ui.drawText("Any whole feet to 40: one piece can cover a hall.", x, y + 4, theme.textDim, 1);
            y += 24;
        } else if (t.model == "slope") {
            if ((d = stepper(ui, x, y, w, "Length", ft(full.w)))) own.w = full.w + 3 * d;
            y += 40;
            ui.drawText("Along the eave, any whole feet. The palette sets its block.", x, y + 4, theme.textDim, 1);
            y += 24;
        } else if (t.model == "gable") {
            ui.drawText("Its size matches a roof span. The palette sets its block.", x, y + 4, theme.textDim, 1);
            y += 24;
        } else if (!door) {
            if ((d = stepper(ui, x, y, w, pillar ? "Thickness" : "Width", pillar ? std::to_string(full.w) + " voxels" : ft(full.w))))
                own.w = pillar ? full.w + d : full.w + 3 * d;
            y += 40;
            if ((d = stepper(ui, x, y, w, "Height (all walls & posts)", ft(full.h))))
                build->height = std::clamp(full.h + 3 * d, 3, 90);
            y += 40;
        } else {
            if ((d = stepper(ui, x, y, w, "Width", ft(full.w)))) own.w = full.w + 3 * d;
            y += 40;
            if ((d = stepper(ui, x, y, w, "Height", ft(full.h)))) own.h = full.h + 3 * d;
            y += 40;
            ui.drawText("A door in a doorway takes its size.", x, y + 4, theme.textDim, 1);
            y += 24;
        }
        if (wall && !t.opening.empty()) {
            const bool isDoor = t.opening == "door";
            if ((d = stepper(ui, x, y, w, isDoor ? "Doorway width" : "Window width", ft(full.openW)))) own.openW = full.openW + 3 * d;
            y += 40;
            if ((d = stepper(ui, x, y, w, isDoor ? "Doorway height" : "Window height", ft(full.openH)))) own.openH = full.openH + 3 * d;
            y += 40;
            if (!isDoor) {
                if ((d = stepper(ui, x, y, w, "Sill height", ft(full.sill)))) own.sill = full.sill + 3 * d;
                y += 40;
            }
            if (isDoor) { ui.drawText("Never under 3 x 7 ft: a person fits.", x, y + 4, theme.textDim, 1); y += 24; }
        }
        if (choice(ui, x, y, w, "Sizes", "reset")) { build->sizes.erase(t.name); if (t.model != "foundation") build->height = 0; }
        y += 48;
        const BuildPalette* pal = gBuildPalettes.find(build->palette);
        ui.drawText("Palette: " + (pal ? pal->display : std::string("none, each as it comes")), x, y, theme.textDim, 2);
        y += 28;
        // what it will look like, outside
        const VoxelVolume& v = gStructures.shape(t.id, build->specFor(t), 0, 0);
        float px = std::min(5.0f, std::min((w - 10) / std::max(1, v.size.x), 150.0f / std::max(1, v.size.y)));
        if (y + v.size.y * px < bottom) elevation(ui, v, nullptr, x, y, px, true);
    }

    // ── Palettes ──────────────────────────────────────────
    std::string palEdit;          // the palette shown in the editor
    void palettesTab(UIRenderer& ui, float px, float top, float pw, float bottom) {
        const auto& theme = gResourcePacks.getTheme();
        const float listW = 330.0f, gap = 6.0f, tileH = 50.0f;
        if (palEdit.empty() || !gBuildPalettes.find(palEdit)) palEdit = build->palette;
        // the list: none, then every palette
        const std::vector<BuildPalette>& all = gBuildPalettes.list();
        float listTop = top, listBottom = bottom - 50;
        gNav.scrollArea(px + 16, listTop, listW, listBottom - listTop);
        scrollBy((all.size() + 1) * (tileH + gap), listBottom - listTop);
        for (int i = -1; i < (int)all.size(); i++) {
            float x = px + 16, y = listTop + (i + 1) * (tileH + gap) - scroll;
            if (y < listTop - 1 || y + tileH > listBottom) continue;
            const BuildPalette* p = i >= 0 ? &all[i] : nullptr;
            const std::string name = p ? p->name : std::string();
            bool hov = hovered(x, y, listW, tileH), on = build->palette == name;
            ui.drawRect(x, y, listW, tileH, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, listW, tileH, on ? theme.accent : theme.border, on ? 2.0f : 1.0f);
            if (p) {
                ui.drawText(p->display, x + 10, y + 6, hov ? theme.textHover : theme.text, 2);
                const char* show[] = { "fill", "post", "beam", "base", "paint", "glass", "door" };
                for (int k = 0; k < 7; k++) swatch(ui, p->spec.block(show[k]), x + 10 + k * 17, y + 30, 15);
                std::string tag = std::string(on ? "equipped  " : "") + (p->edited ? (p->curated ? "edited" : "yours") : "");
                ui.drawText(tag, x + 136, y + 33, on ? theme.accent : theme.textDim, 1);
            } else {
                ui.drawText("None", x + 12, y + 8, hov ? theme.textHover : theme.text, 2);
                ui.drawText("each piece as it comes", x + 12, y + 32, theme.textDim, 1);
            }
            if (hov && clicked()) { build->palette = name; palEdit = name; }
        }
        // a new one: a copy of the equipped one (or the first)
        if (choice(ui, px + 16, bottom - 40, listW, "New palette", "copy")) {
            BuildPalette n;
            const BuildPalette* from = gBuildPalettes.find(build->palette);
            if (!from && !all.empty()) from = &all[0];
            if (from) n.spec = from->spec;
            n.name = gBuildPalettes.freeName("my_palette");
            n.display = "My Palette" + (n.name.size() > 10 ? " " + n.name.substr(11) : std::string());
            gBuildPalettes.save(n);
            build->palette = palEdit = n.name;
        }

        // the editor
        float ex = px + 16 + listW + 20, ew = pw - listW - 52, ey = top;
        const BuildPalette* cur = gBuildPalettes.find(palEdit);
        if (!cur) {
            ui.drawText("Equip a palette to edit it.", ex, ey + 6, theme.textDim, 2);
            ui.drawText("Without one, every wall, post and door is placed in its own blocks.", ex, ey + 34, theme.textDim, 1);
            return;
        }
        BuildPalette p = *cur;           // edits go to a copy, saved when it changes
        bool changed = false;
        ui.drawText(p.display, ex, ey, theme.title, 2);
        ey += 32;
        const float colW = std::min(500.0f, ew - 190.0f);
        float ry = ey;
        auto cycle = [](const std::vector<std::string>& opts, const std::string& v, int d) {
            if (opts.empty()) return v;
            auto it = std::find(opts.begin(), opts.end(), v);
            int i = it == opts.end() ? (d > 0 ? -1 : 0) : (int)(it - opts.begin());
            return opts[(i + d + (int)opts.size()) % opts.size()];
        };
        int d;
        if ((d = arrows(ui, ex, ry, colW, "Outside", p.spec.outside.empty() ? "plain" : p.spec.outside, ""))) {
            p.spec.outside = cycle(wallOutsides(), p.spec.outside.empty() ? "plain" : p.spec.outside, d); changed = true;
        }
        ry += 38;
        if ((d = arrows(ui, ex, ry, colW, "Inside", p.spec.inside.empty() ? "plain" : p.spec.inside, ""))) {
            p.spec.inside = cycle(wallInsides(), p.spec.inside.empty() ? "plain" : p.spec.inside, d); changed = true;
        }
        ry += 36;
        ui.drawText("plain walls only: timber, stone-based, inlaid walls keep theirs", ex, ry, theme.textDim, 1);
        ry += 16;
        for (const PieceRole& r : pieceRoles()) {
            if (ry + 34 > bottom - 44) break;
            std::string b = p.spec.block(r.id);
            if ((d = arrows(ui, ex, ry, colW, r.label, b.empty() ? std::string("as it comes") : blockName(b), b.empty() ? std::string() : b))) {
                p.spec.blocks[r.id] = cycle(gBuildPalettes.options(r.id), b, d); changed = true;
            }
            ry += 38;
        }
        if (changed) gBuildPalettes.save(p);
        // reset an edited curated one, or delete one of the player's
        if (p.edited && choice(ui, ex, bottom - 40, colW, p.curated ? "Back to as it came" : "Delete this palette", "")) {
            gBuildPalettes.reset(p.name);
            if (!gBuildPalettes.find(p.name)) { if (build->palette == p.name) build->palette.clear(); palEdit.clear(); }
            return;
        }

        // the picture: a doorway with a door, a window wall, a timber
        // wall; outside above, inside below
        float vx = ex + colW + 20, vw = ew - colW - 20;
        if (vw < 120) return;
        ui.drawText("Outside", vx, ey, theme.textDim, 1);
        const char* show[] = { "plank_doorway_4", "plank_window_8", "plank_inlay_wall_4" };
        std::vector<std::pair<uint16_t, std::string>> pieces;
        int totalW = 0, maxH = 0;
        for (const char* n : show)
            if (const StructureType* t = gStructures.get(gStructures.find(n))) {
                std::string spec = gStructures.normalize(*t, gStructures.applyPalette(*t, PieceSpec(), p.spec));
                const VoxelVolume& v = gStructures.shape(t->id, spec, 0, 0);
                pieces.push_back({ t->id, spec });
                totalW += v.size.x + 2; maxH = std::max(maxH, v.size.y);
            }
        if (pieces.empty()) return;
        float s = std::min(5.0f, std::min(vw / std::max(1, totalW), (bottom - ey - 60) * 0.5f / std::max(1, maxH)));
        // the door that goes in the doorway, in this palette
        const StructureType* doorT = gStructures.get(gStructures.find("plank_ledged_door"));
        for (int face = 0; face < 2; face++) {
            float cx = vx, cy = ey + 16 + face * (maxH * s + 30);
            if (face == 1) ui.drawText("Inside", vx, cy - 14, theme.textDim, 1);
            for (auto& [id, spec] : pieces) {
                const VoxelVolume& v = gStructures.shape(id, spec, 0, 0);
                const VoxelVolume* door = nullptr;
                if (doorT && gStructures.get(id)->opening == "door") {
                    PieceSpec ds = gStructures.applyPalette(*doorT, PieceSpec(), p.spec);
                    PieceSpec ws = gStructures.resolve(*gStructures.get(id), PieceSpec::parse(spec));
                    ds.w = ws.openW; ds.h = ws.openH;
                    door = &gStructures.shape(doorT->id, gStructures.normalize(*doorT, ds), 0, 0);
                }
                elevation(ui, v, door, cx, cy + (maxH - v.size.y) * s, s, face == 0);
                cx += (v.size.x + 2) * s;
            }
        }
    }

    // ── Drawing blocks ────────────────────────────────────
    static std::string blockName(const std::string& b) {
        if (b == "none") return "none, open";
        std::string n = b;
        for (char& c : n) if (c == '_') c = ' ';
        if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
        return n;
    }
    // A square of a block's side texture (its colour without the atlas)
    static void swatch(UIRenderer& ui, const std::string& block, float x, float y, float s) {
        uint16_t id = block.empty() || block == "none" ? 0 : gRegistry.getIDByName(block);
        if (!id) {
            float c[4] = { 40, 46, 52, 255 };
            ui.drawRect(x, y, s, s, c);
            return;
        }
        if (gIconAtlas.tex && gIconAtlas.tileU > 0.0f) {
            FaceUV f = gResourcePacks.getFaceUV(block);
            float u0 = f.sideX * gIconAtlas.tileU, v0 = f.sideY * gIconAtlas.tileV;
            glm::vec2 p[4] = { { x, y }, { x + s, y }, { x + s, y + s }, { x, y + s } };
            glm::vec2 uv[4] = { { u0, v0 }, { u0 + gIconAtlas.tileU, v0 }, { u0 + gIconAtlas.tileU, v0 + gIconAtlas.tileV }, { u0, v0 + gIconAtlas.tileV } };
            ui.drawTexturedQuad(gIconAtlas.tex, p, uv, 1.0f, 255.0f);
        } else {
            const VoxelTypeDef& d = gRegistry.get(id);
            float c[4] = { d.color[0] * 255, d.color[1] * 255, d.color[2] * 255, 255 };
            ui.drawRect(x, y, s, s, c);
        }
    }
    // A piece seen straight on, each voxel in its own bit of its block's
    // texture (4 voxels a tile, like the world), deeper ones darker:
    // the outside face (outside = true) or the inside (mirrored). A door
    // may stand in its hole
    static void elevation(UIRenderer& ui, const VoxelVolume& v, const VoxelVolume* door, float x, float y, float s, bool outside) {
        const glm::ivec3 n = v.size;
        const int dx0 = door ? (n.x - door->size.x) / 2 : 0;
        for (int yy = 0; yy < n.y; yy++)
            for (int xx = 0; xx < n.x; xx++) {
                int vx = outside ? n.x - 1 - xx : xx;      // seen from -z, +x is on the left
                uint16_t id = BLOCK_AIR;
                int depth = 0;
                for (int k = 0; k < n.z; k++) {
                    int z = outside ? k : n.z - 1 - k;
                    uint16_t b = v.at(vx, yy, z);
                    if (b != BLOCK_AIR) { id = b; depth = k; break; }
                }
                if (id == BLOCK_AIR && door) {
                    int qx = vx - dx0;
                    if (qx >= 0 && qx < door->size.x && yy < door->size.y) { id = door->at(qx, yy, 0); depth = 1; }
                }
                float px = x + xx * s, py = y + (n.y - 1 - yy) * s;
                if (id == BLOCK_AIR) {
                    float c[4] = { 22, 26, 30, 255 };
                    ui.drawRect(px, py, s, s, c);
                    continue;
                }
                float shade = depth == 0 ? 1.0f : depth == 1 ? 0.78f : 0.62f;
                const VoxelTypeDef& d = gRegistry.get(id);
                if (gIconAtlas.tex && gIconAtlas.tileU > 0.0f) {
                    FaceUV f = gResourcePacks.getFaceUV(d.name);
                    float tu = gIconAtlas.tileU / 4, tv = gIconAtlas.tileV / 4;
                    float u0 = f.sideX * gIconAtlas.tileU + (vx & 3) * tu, v0 = f.sideY * gIconAtlas.tileV + (3 - (yy & 3)) * tv;
                    float eu = tu * 0.06f, ev = tv * 0.06f;             // kept off the edges: no bleeding from the next tile
                    glm::vec2 p[4] = { { px, py }, { px + s, py }, { px + s, py + s }, { px, py + s } };
                    glm::vec2 uv[4] = { { u0 + eu, v0 + ev }, { u0 + tu - eu, v0 + ev }, { u0 + tu - eu, v0 + tv - ev }, { u0 + eu, v0 + tv - ev } };
                    ui.drawTexturedQuad(gIconAtlas.tex, p, uv, shade, 255.0f);
                } else {
                    float c[4] = { d.color[0] * 255 * shade, d.color[1] * 255 * shade, d.color[2] * 255 * shade, 255 };
                    ui.drawRect(px, py, s, s, c);
                }
            }
    }

    // A value with - and + (returns -1, 0 or +1)
    int stepper(UIRenderer& ui, float x, float y, float w, const std::string& label, const std::string& value) {
        const auto& theme = gResourcePacks.getTheme();
        ui.drawRect(x, y, w, 34, theme.inputBg);
        ui.drawBorder(x, y, w, 34, theme.border, 1.0f);
        ui.drawText(label, x + 10, y + 9, theme.text, 2);
        int out = 0;
        float bx = x + w - 34;
        for (int k = 0; k < 2; k++) {
            float cx = k == 0 ? bx - 34 - (float)ui.textWidth(value, 2) - 16 : bx;
            bool hov = hovered(cx + 2, y + 2, 30, 30);
            ui.drawRect(cx + 2, y + 2, 30, 30, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(cx + 2, y + 2, 30, 30, theme.border, 1.0f);
            const float* col = hov ? theme.textHover : theme.accent;     // drawn: the font has no +
            ui.drawRect(cx + 10, y + 16, 14, 2, col);
            if (k == 1) ui.drawRect(cx + 16, y + 10, 2, 14, col);
            if (hov && clicked()) out = k == 0 ? -1 : 1;
        }
        float vw = (float)ui.textWidth(value, 2);
        ui.drawText(value, bx - 8 - vw, y + 9, theme.accent, 2);
        return out;
    }
    // A < value > row: the left half steps back, the right half on (the
    // pad's left / right too). block: a swatch beside the value
    int arrows(UIRenderer& ui, float x, float y, float w, const std::string& label, const std::string& value,
               const std::string& block) {
        const auto& theme = gResourcePacks.getTheme();
        bool hov = hovered(x, y, w, 34);
        gNav.setKind(x, y, w, 34, PadNav::Kind::Choice);
        ui.drawRect(x, y, w, 34, hov ? theme.bgHover : theme.inputBg);
        ui.drawBorder(x, y, w, 34, theme.border, 1.0f);
        ui.drawText(label, x + 10, y + 9, theme.text, 2);
        float vw = (float)ui.textWidth(value, 2);
        float vx = x + w - 26 - vw;
        ui.drawText("<", vx - (block.empty() ? 22 : 48), y + 9, theme.textDim, 2);
        if (!block.empty()) swatch(ui, block, vx - 28, y + 7, 20);
        ui.drawText(value, vx, y + 9, hov ? theme.textHover : theme.accent, 2);
        ui.drawText(">", x + w - 18, y + 9, theme.textDim, 2);
        if (hov && clicked()) return mx < x + w * 0.5f ? -1 : 1;
        return 0;
    }

    // A label and a value button that cycles when clicked
    bool choice(UIRenderer& ui, float x, float y, float w, const std::string& label, const std::string& value) {
        const auto& theme = gResourcePacks.getTheme();
        bool hov = hovered(x, y, w, 34);
        ui.drawRect(x, y, w, 34, hov ? theme.bgHover : theme.inputBg);
        ui.drawBorder(x, y, w, 34, theme.border, 1.0f);
        ui.drawText(label, x + 10, y + 9, theme.text, 2);
        float vw = (float)ui.textWidth(value, 2);
        ui.drawText(value, x + w - vw - 10, y + 9, hov ? theme.textHover : theme.accent, 2);
        return hov && clicked();
    }

    void scrollBy(float total, float shown) {
        scroll -= (gInput.getScrollDelta() + gNav.scroll()) * 40.0f;
        scroll = std::clamp(scroll, 0.0f, std::max(0.0f, total - shown));
    }

    static std::string costText(const std::map<uint16_t, int>& cost) {
        std::string s;
        for (auto& [item, n] : cost) s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + gItems.get(item).displayName;
        return s.empty() ? std::string("free") : s;
    }

    template <class C> bool affordable(const C& cost) const {
        for (auto& [item, n] : cost)
            if (player->inventory.count(item) < n) return false;
        return true;
    }
    bool clicked() const { return mouseDown && !mousePrev; }
    bool hovered(float x, float y, float w, float h) const {
        gNav.add(x, y, w, h);                    // every rectangle asked about: the controller can reach it
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }
};
