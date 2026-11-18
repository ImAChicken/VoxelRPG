#pragma once
#include "UIScreen.h"
#include "ItemIcon.h"
#include "../entity/Player.h"
#include "../game/BuildTool.h"
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
// Test Play). Two tabs:
//   Pieces      every structure marked menu = true in
//               structures.toml, filtered by part group
//               (foundations, walls, roofs …) along the top
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
        tab = (b && b->mode != HammerMode::Piece) ? 1 : 0;
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
        glfwGetCursorPos(window, &mx, &my);
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
        rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        result = ScreenResult::Continue;

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);
        float pw = std::min(sw - 20.0f, 1100.0f), ph = std::min(sh - 20.0f, 760.0f);
        float px = (sw - pw) * 0.5f, py = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);

        // tabs
        const char* tabs[2] = { "Pieces", "Blueprints" };
        float tx = px + 16;
        for (int i = 0; i < 2; i++) {
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
        if (tab == 0) piecesTab(ui, px, top, pw, bottom);
        else          blueprintsTab(ui, px, top, pw, bottom);

        ui.drawText(tab == 0 ? "click to choose   T turns it   arrows / Page Up / Page Down fine-tune   wheel scrolls"
                             : "click a blueprint to place it   T turns it   arrows fine-tune   wheel scrolls",
                    px + 16, py + ph - 22, theme.textDim, 1);
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
            { "furniture", "Furniture" }, { "chest", "Chests" }, { "light", "Lights" }, { "", "Other" },
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
            ui.drawText(t->feet() + "   " + t->costText(gStructures.costsEnabled), x + 56, y + 32,
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
        scroll -= gInput.getScrollDelta() * 40.0f;
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
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }
};
