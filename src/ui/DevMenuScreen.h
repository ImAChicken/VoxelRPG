#pragma once
#include "UIScreen.h"
#include "../entity/Player.h"
#include "../game/DevTools.h"
#include "../game/BuildTool.h"
#include "../game/ItemRegistry.h"
#include "../input/InputManager.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Dev Menu — every block, tool and weapon in one place
// ══════════════════════════════════════════════════════════
// Opened from the pause menu (Esc -> Dev Menu) outside
// Survival. Tabs along the top; each tab is a PAGE. Adding a
// page later is one addPage() call in the constructor — a
// page is just a title and a draw function.
//
// Item pages (Blocks, Player Tools, Weapons, Dev Tools):
//   left click   put the item in the selected toolbar slot
//                (weapons go to the weapon slot)
//   right click  on a block: make it the dev tools' material
//
// "Giving to" picks which inventory receives items: the dev
// inventory (default) or the player inventory, so you can
// kit out your Survival character from here too.
//
// Toolbar along the bottom: left click selects a slot,
// right click empties it.
class DevMenuScreen : public UIScreen {
public:
    explicit DevMenuScreen(GLFWwindow* win) : window(win) {
        addItemPage("Blocks",       IC_BLOCK);
        addItemPage("Player Tools", IC_TOOL);
        addItemPage("Weapons",      IC_WEAPON);
        addItemPage("Dev Tools",    IC_DEV);
        addPage("Structures", [this](UIRenderer& ui, const PageArea& a) {
            drawStructures(ui, a);
        });
    }

    // What the menu edits; set before showing it
    void setContext(Player* p, DevTools* d, BuildTool* b) {
        player = p; dev = d; build = b;
    }

    void onEnter() override {
        giveToPlayer = false;
        scroll = 0.0f;
        structures = structureCatalog();     // pick up new /save files
        mousePressed = rightPressed = true;   // ignore the click that opened us
    }

    // ── Pages ─────────────────────────────────────────────
    struct PageArea { float x, y, w, h; };
    struct Page {
        std::string title;
        std::function<void(UIRenderer&, const PageArea&)> draw;
    };
    void addPage(const std::string& title,
                 std::function<void(UIRenderer&, const PageArea&)> draw) {
        pages.push_back({ title, std::move(draw) });
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        if (!player || !dev) return ScreenResult::CloseMenu;
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        pollMouse();
        hoverText.clear();
        hoverHint.clear();

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);

        float pw = std::min(1060.0f, sw - 40.0f);
        float ph = std::min(640.0f,  sh - 40.0f);
        float px = (sw - pw) * 0.5f, py = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);
        ui.drawText("Dev Menu", px + 16, py + 14, theme.title, 2);

        // ── Tabs ──────────────────────────────────────────
        float tx = px + 170.0f;
        for (size_t i = 0; i < pages.size(); i++) {
            float w = (float)ui.textWidth(pages[i].title, 2) + 24.0f;
            bool on = (i == current), hov = hovered(tx, py + 8, w, 30);
            ui.drawRect(tx, py + 8, w, 30,
                        on ? theme.buttonBgHover : (hov ? theme.bgHover : theme.buttonBg));
            ui.drawBorder(tx, py + 8, w, 30, on ? theme.accent : theme.buttonBorder,
                          on ? 2.0f : 1.0f);
            ui.drawTextCentered(pages[i].title, tx, py + 8, w, 30,
                                on ? theme.textHover : theme.text, 2);
            if (hov && clicked()) { current = i; scroll = 0.0f; }
            tx += w + 6.0f;
        }

        // ── Which inventory receives items ────────────────
        float gy = py + 50.0f;
        ui.drawText("Giving to:", px + 16, gy + 8, theme.textDim, 2);
        toggle(ui, "Dev inventory",    px + 190, gy, !giveToPlayer, false);
        toggle(ui, "Player inventory", px + 486, gy,  giveToPlayer, true);
        char mat[96];
        std::snprintf(mat, sizeof(mat), "Material: %s",
                      DevTools::blockName(dev->material).c_str());
        ui.drawText(mat, px + pw - 16 - ui.textWidth(mat, 2), gy + 8,
                    theme.accent, 2);

        // ── Current page ──────────────────────────────────
        PageArea area { px + 16, py + 92, pw - 32, ph - 92 - 120 };
        if (current < pages.size()) pages[current].draw(ui, area);

        // ── Toolbar of the receiving inventory ────────────
        drawToolbar(ui, px + 16, py + ph - 110, pw - 32);

        // ── Back ──────────────────────────────────────────
        float bw = (float)theme.buttonW, bh = (float)theme.buttonH;
        float bx = px + pw - bw - 16, by = py + ph - bh - 12;
        bool backHov = hovered(bx, by, bw, bh);
        drawButton(ui, "Back", bx, by, backHov);
        ScreenResult result = ScreenResult::Continue;
        if (backHov && clicked()) result = ScreenResult::CloseMenu;

        // ── Hover description, beside the toolbar title ───
        float hx = px + 16 + ui.textWidth("Player inventory", 2) + 32;
        if (!hoverText.empty())
            ui.drawText(hoverText, hx, py + ph - 116, theme.textHover, 2);
        if (!hoverHint.empty())
            ui.drawText(hoverHint, hx, py + ph - 97, theme.textDim, 1);

        ui.end();
        return result;
    }

private:
    GLFWwindow* window;
    Player*     player = nullptr;
    DevTools*   dev    = nullptr;
    BuildTool*  build  = nullptr;

    std::vector<Page> pages;
    std::vector<StructureEntry> structures;
    size_t current      = 0;
    bool   giveToPlayer = false;
    float  scroll       = 0.0f;
    std::string hoverText, hoverHint;

    double mx = 0, my = 0;
    bool mousePressed = false, mousePrev = false;
    bool rightPressed = false, rightPrev = false;

    Inventory& target() {
        return giveToPlayer ? player->inventory : player->devInventory;
    }

    // ── Input ─────────────────────────────────────────────
    void pollMouse() {
        mousePrev = mousePressed;
        rightPrev = rightPressed;
        glfwGetCursorPos(window, &mx, &my);
        mousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
        rightPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    }
    bool clicked()      const { return mousePressed && !mousePrev; }
    bool rightClicked() const { return rightPressed && !rightPrev; }
    bool hovered(float x, float y, float w, float h) const {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }

    void toggle(UIRenderer& ui, const std::string& label, float x, float y,
                bool on, bool toPlayer) {
        const auto& theme = gResourcePacks.getTheme();
        float w = 288.0f, h = 30.0f;
        bool hov = hovered(x, y, w, h);
        ui.drawRect(x, y, w, h, on ? theme.buttonBgHover : theme.buttonBg);
        ui.drawBorder(x, y, w, h, on ? theme.accent : theme.buttonBorder,
                      on ? 2.0f : 1.0f);
        ui.drawTextCentered(label, x, y, w, h, on ? theme.textHover : theme.text, 2);
        if (hov && clicked()) giveToPlayer = toPlayer;
    }

    // ── A grid of every item in one category ──────────────
    void addItemPage(const std::string& title, uint32_t category) {
        addPage(title, [this, category](UIRenderer& ui, const PageArea& a) {
            drawItemGrid(ui, a, gItems.inCategory(category));
        });
    }

    void drawItemGrid(UIRenderer& ui, const PageArea& a,
                      const std::vector<uint16_t>& ids) {
        const auto& theme = gResourcePacks.getTheme();
        const float tileW = 248.0f, tileH = 36.0f, gap = 6.0f;
        int cols = std::max(1, (int)((a.w + gap) / (tileW + gap)));
        int rows = ((int)ids.size() + cols - 1) / cols;
        float contentH = rows * (tileH + gap);

        // Mouse wheel scrolls a long page
        if (hovered(a.x, a.y, a.w, a.h)) {
            scroll -= gInput.getScrollDelta() * 40.0f;
            scroll = std::clamp(scroll, 0.0f, std::max(0.0f, contentH - a.h));
        }

        if (ids.empty())
            ui.drawText("Nothing in this category yet.", a.x, a.y + 8,
                        theme.textDim, 2);

        for (size_t i = 0; i < ids.size(); i++) {
            int col = (int)i % cols, row = (int)i / cols;
            float x = a.x + col * (tileW + gap);
            float y = a.y + row * (tileH + gap) - scroll;
            if (y < a.y - 1 || y + tileH > a.y + a.h + 1) continue;   // clipped

            const ItemDef& d = gItems.get(ids[i]);
            bool hov = hovered(x, y, tileW, tileH);
            bool isMat = d.is(IC_BLOCK) && d.blockID == dev->material;
            ui.drawRect(x, y, tileW, tileH, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, tileW, tileH,
                          isMat ? theme.accent : theme.border, isMat ? 2.0f : 1.0f);
            swatch(ui, d, x + 6, y + 6, 24);
            ui.drawText(fit(d.displayName, 13), x + 38, y + 10,
                        hov ? theme.textHover : theme.text, 2);

            if (hov) {
                hoverText = describe(d);
                hoverHint = hint(d);
                if (clicked()) give(d);
                if (rightClicked() && d.is(IC_BLOCK)) dev->setMaterial(d.blockID);
            }
        }
    }

    // ── Structures page ───────────────────────────────────
    // Top: every structure in structures.toml (building
    // pieces, doors, trees). Click one: the Builder's Hammer
    // goes in your hand with it chosen, and it is placed as a
    // real structure (a tree you can chop, a door that opens).
    // Below: files saved with /save — those go in the
    // clipboard and are placed as plain blocks with the Paste
    // Tool. Both use arrows / Page Up / Page Down to fine-tune.
    void drawStructures(UIRenderer& ui, const PageArea& a) {
        const auto& theme = gResourcePacks.getTheme();
        const float tileW = 248.0f, tileH = 36.0f, gap = 6.0f;
        int cols = std::max(1, (int)((a.w + gap) / (tileW + gap)));
        float y = a.y;
        int col = 0;

        auto tile = [&](const std::string& name, const float* swatch, bool on) -> bool {
            float x = a.x + col * (tileW + gap);
            bool hov = hovered(x, y, tileW, tileH);
            ui.drawRect(x, y, tileW, tileH, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, tileW, tileH, on ? theme.accent : theme.border,
                          on ? 2.0f : 1.0f);
            ui.drawRect(x + 6, y + 6, 24, 24, swatch);
            ui.drawText(fit(name, 13), x + 38, y + 10, hov ? theme.textHover : theme.text, 2);
            if (++col == cols) { col = 0; y += tileH + gap; }
            return hov;
        };
        auto newRow = [&]() { if (col) { col = 0; y += tileH + gap; } };

        ui.drawText("Structures  (Builder's Hammer)", a.x, y + 4, theme.textDim, 2);
        y += 28;
        for (uint16_t id : gStructures.all()) {
            const StructureType* t = gStructures.get(id);
            const VoxelTypeDef& b = gRegistry.get(t->kind == StructureKind::Tree
                                                  ? (uint16_t)BLOCK_LEAVES : t->block);
            float c[4] = { b.color[0] * 255, b.color[1] * 255, b.color[2] * 255, 255 };
            if (tile(t->displayName, c, build && build->selected == id)) {
                hoverText = t->displayName + "  " + t->feet();
                hoverHint = "click: hold the Builder's Hammer with this chosen";
                if (clicked() && build) {
                    build->select(id);
                    holdItem("builders_hammer");
                }
            }
        }
        newRow();
        y += 8;
        ui.drawText("Saved with /save  (Paste Tool, plain blocks)", a.x, y + 4,
                    theme.textDim, 2);
        y += 28;
        int saved = 0;
        for (const StructureEntry& e : structures) {
            if (e.builtIn) continue;
            saved++;
            float c[4] = {200, 140, 200, 255};
            if (tile(e.name, c, false)) {
                hoverText = e.name + "  [saved]";
                hoverHint = "click: load it and hold the Paste Tool";
                if (clicked() && dev->loadCatalogEntry(e)) holdItem("dev_paste");
            }
        }
        if (!saved)
            ui.drawText("Copy something and /save <name> to add it here.",
                        a.x, y + 6, theme.textDim, 2);
    }

    // Select an item in the dev inventory, adding it to the
    // selected slot if it is not there yet
    void holdItem(const char* name) {
        Inventory& inv = player->devInventory;
        uint16_t item = gItems.find(name);
        if (!item) return;
        for (int i = 0; i < Inventory::TOOLBAR_SLOTS; i++)
            if (inv.toolbar[i].itemID == item) { inv.select(i); return; }
        inv.setSlot(inv.selected, item, 1);
    }

    // Put an item into the receiving inventory
    void give(const ItemDef& d) {
        Inventory& inv = target();
        if (d.is(IC_WEAPON) && !d.is(IC_TOOL)) {
            inv.setSlot(-1, d.id, 1);              // weapon slot
            return;
        }
        int count = 1;
        if (d.is(IC_BLOCK) && !inv.infinite) count = 64;
        inv.setSlot(inv.selected, d.id, count);
        if (d.is(IC_BLOCK)) dev->setMaterial(d.blockID);
    }

    void drawToolbar(UIRenderer& ui, float x, float y, float w) {
        const auto& theme = gResourcePacks.getTheme();
        Inventory& inv = target();
        ui.drawText(giveToPlayer ? "Player inventory" : "Dev inventory",
                    x, y, theme.textDim, 2);
        const float slot = 52.0f, gap = 6.0f;
        float sx = x, sy = y + 22;
        for (int i = 0; i <= Inventory::TOOLBAR_SLOTS; i++) {
            bool weaponSlot = (i == Inventory::TOOLBAR_SLOTS);
            if (weaponSlot) sx += 24.0f;
            const ItemStack& s = weaponSlot ? inv.weapon : inv.toolbar[i];
            bool on  = !weaponSlot && i == inv.selected;
            bool hov = hovered(sx, sy, slot, slot);
            ui.drawRect(sx, sy, slot, slot, hov ? theme.bgHover : theme.bg);
            ui.drawBorder(sx, sy, slot, slot, on ? theme.accent : theme.border,
                          on ? 3.0f : 1.0f);
            if (!s.empty()) {
                const ItemDef& d = gItems.get(s.itemID);
                swatch(ui, d, sx + 10, sy + 10, 32);
                if (s.count > 1 && !inv.infinite) {
                    std::string c = std::to_string(s.count);
                    ui.drawText(c, sx + slot - 4 - ui.textWidth(c, 1),
                                sy + slot - 12, theme.textHover, 1);
                }
                if (hov) { hoverText = describe(d); hoverHint = hint(d); }
            }
            if (weaponSlot)
                ui.drawText("weapon", sx, sy + slot + 4, theme.textDim, 1);
            if (hov && clicked() && !weaponSlot) inv.select(i);
            if (hov && rightClicked()) {
                if (weaponSlot) inv.weapon.clear();
                else            inv.toolbar[i].clear();
            }
            sx += slot + gap;
        }
        (void)w;
    }

    // A coloured square standing in for an icon. Tools and
    // weapons get a letter so they can be told apart.
    static void swatch(UIRenderer& ui, const ItemDef& d, float x, float y, float s) {
        float c[4] = { d.color[0] * 255.0f, d.color[1] * 255.0f,
                       d.color[2] * 255.0f, 255.0f };
        float edge[4] = {0, 0, 0, 200};
        ui.drawRect(x, y, s, s, c);
        ui.drawBorder(x, y, s, s, edge, 1.0f);
        const char* mark = d.is(IC_DEV) ? "D" : d.is(IC_WEAPON) && d.is(IC_TOOL) ? "X"
                         : d.is(IC_WEAPON) ? "W" : d.is(IC_TOOL) ? "T" : nullptr;
        if (mark) {
            float ink[4] = {10, 10, 10, 255};
            ui.drawTextCentered(mark, x, y, s, s, ink, 2);
        }
    }

    static std::string fit(const std::string& s, size_t n) {
        return s.size() <= n ? s : s.substr(0, n - 1) + ".";
    }

    static std::string describe(const ItemDef& d) {
        std::string t = d.displayName + "  [";
        bool first = true;
        auto tag = [&](bool on, const char* n) {
            if (!on) return;
            if (!first) t += ", ";
            t += n; first = false;
        };
        tag(d.is(IC_BLOCK), "block"); tag(d.is(IC_TOOL), "tool");
        tag(d.is(IC_WEAPON), "weapon"); tag(d.is(IC_DEV), "dev");
        t += "]";
        char b[64];
        if (d.is(IC_TOOL)) { std::snprintf(b, sizeof(b), "  dig x%.0f", d.toolPower); t += b; }
        if (d.is(IC_WEAPON)) { std::snprintf(b, sizeof(b), "  dmg %.0f", d.damage); t += b; }
        return t.size() > 46 ? t.substr(0, 46) : t;
    }

    // Second line: how to use it
    static std::string hint(const ItemDef& d) {
        if (d.is(IC_DEV))    return DevTools::toolHint(d.devTool);
        if (d.is(IC_BLOCK))  return "left click: put in slot   right click: set dev material";
        if (d.is(IC_WEAPON) && !d.is(IC_TOOL)) return "left click: put in the weapon slot (used with R in Survival)";
        return "left click: put in the selected slot";
    }
};
