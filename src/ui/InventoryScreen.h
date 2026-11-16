#pragma once
#include "UIScreen.h"
#include "ItemIcon.h"
#include "../entity/Player.h"
#include "../game/Inventory.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <string>

// ══════════════════════════════════════════════════════════
// Inventory screen (I)
// ══════════════════════════════════════════════════════════
// Three windows side by side:
//   left    a placeholder, kept for another menu (crafting,
//           a chest, a trade …)
//   middle  the 50 slots: the hotbar on top (slots 1 and 2
//           are the off-hand), then the pack
//   right   equipment: Weapon 1 and 2, armour, rings and an
//           amulet. An equipped item stays in its inventory
//           slot; the slot is marked E and the equipment slot
//           shows it.
//
// Mouse:
//   drag              move or swap items (equipment follows)
//   right drag        move half a stack
//   drag onto equip   equip it there; drag an equipment slot
//                     away to take it off
//   shift click       equip / take off
//   click a hotbar    select it (off-hand: in hand / out)
//   1..0 over a slot  swap it with that hotbar slot
// I or Esc closes; X swaps weapons.
class InventoryScreen : public UIScreen {
public:
    explicit InventoryScreen(GLFWwindow* win) : window(win) {}

    void setContext(Player* p) { player = p; }

    void onEnter() override {
        mouseDown = mousePrev = true;      // ignore the opening click
        rightDown = rightPrev = true;
        drag = -1; dragEquip = -1;
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        if (!player) return ScreenResult::CloseMenu;
        Inventory& inv = player->activeInventory();
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        mousePrev = mouseDown; rightPrev = rightDown;
        glfwGetCursorPos(window, &mx, &my);
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
        rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);

        // ── Layout ────────────────────────────────────────
        const float gap = 4.0f, pad = 16.0f, sideW = 250.0f, between = 12.0f;
        slot = std::clamp((sw - 2 * sideW - 2 * between - 2 * pad - 9 * gap - 40.0f) / 10.0f, 30.0f, 52.0f);
        float gridW = 10 * slot + 9 * gap;
        float midW = gridW + 2 * pad;
        float es = std::clamp((sh - 20.0f - 46.0f - 16.0f - 44.0f) / EQUIP_SLOTS - 4.0f, 22.0f, 32.0f);   // equipment slot size
        float needGrid = 52.0f + slot + 22.0f + 4 * slot + 3 * gap + 90.0f;
        float needEquip = 46.0f + EQUIP_SLOTS * (es + 4.0f) + 16.0f + 44.0f;
        float ph = std::min(sh - 20.0f, std::max(needGrid, needEquip));
        float total = sideW + between + midW + between + sideW;
        float x0 = std::max(10.0f, (sw - total) * 0.5f), py = std::max(10.0f, (sh - ph) * 0.5f);
        float lx = x0, mxp = lx + sideW + between, rx = mxp + midW + between;

        hoverSlot = -1; hoverEquip = -1;

        // ── Left: placeholder ─────────────────────────────
        drawPanel(ui, lx, py, sideW, ph);
        ui.drawText("Placeholder", lx + pad, py + 14, theme.title, 2);
        ui.drawText("room for another menu", lx + pad, py + 44, theme.textDim, 1);

        // ── Middle: the slots ─────────────────────────────
        drawPanel(ui, mxp, py, midW, ph);
        ui.drawText(player->mode == GameMode::Survival ? "Inventory" : "Inventory - Test Play", mxp + pad, py + 14, theme.title, 2);
        float gx = mxp + pad, gy = py + 52;
        ui.drawText("off-hand", gx, gy - 12, theme.textDim, 1);
        ui.drawText("hotbar", gx + 2 * (slot + gap) + 8, gy - 12, theme.textDim, 1);
        for (int i = 0; i < Inventory::HOTBAR; i++) {
            float sx = gx + i * (slot + gap) + (i >= Inventory::OFFHAND ? 0.0f : 0.0f);
            drawSlot(ui, inv, i, sx, gy);
        }
        float py2 = gy + slot + 22;
        ui.drawText("pack", gx, py2 - 12, theme.textDim, 1);
        for (int i = Inventory::HOTBAR; i < Inventory::SLOTS; i++) {
            int k = i - Inventory::HOTBAR;
            drawSlot(ui, inv, i, gx + (k % 10) * (slot + gap), py2 + (k / 10) * (slot + gap));
        }
        float infoY = py2 + 4 * (slot + gap) + 8;

        // ── Right: equipment ──────────────────────────────
        drawPanel(ui, rx, py, sideW, ph);
        ui.drawText("Equipment", rx + pad, py + 14, theme.title, 2);
        float ey = py + 46, rowH = es + 4;
        for (int e = 0; e < EQUIP_SLOTS; e++) {
            if (e == EQUIP_WORN_FIRST || e == (int)EquipSlot::Ring1) ey += 8;     // groups
            drawEquip(ui, inv, e, rx + pad, ey, es);
            ey += rowH;
        }
        if (button(ui, "Swap weapons  X", rx + pad, ey + 6, sideW - 2 * pad, 26)) inv.swapWeapons();

        // ── Info line: what's under the mouse ─────────────
        const ItemStack* hs = nullptr;
        int hsEquip = -1;
        if (hoverSlot >= 0 && !inv.slots[hoverSlot].empty()) { hs = &inv.slots[hoverSlot]; hsEquip = inv.equippedAs(hoverSlot); }
        if (hoverEquip >= 0 && !inv.equipped((EquipSlot)hoverEquip).empty()) { hs = &inv.equipped((EquipSlot)hoverEquip); hsEquip = hoverEquip; }
        if (hs && drag < 0 && dragEquip < 0) {
            const ItemDef& d = gItems.get(hs->itemID);
            std::string t = d.displayName;
            if (hs->count > 1) t += "  x" + std::to_string(hs->count);
            ui.drawText(t, gx, infoY, theme.textHover, 2);
            std::string s2 = tags(d);
            if (hsEquip >= 0) s2 += "   equipped: " + std::string(equipSlotName((EquipSlot)hsEquip));
            ui.drawText(s2, gx, infoY + 22, theme.textDim, 1);
        } else {
            ui.drawText("drag to move   shift click: equip   right drag: half", gx, infoY + 6, theme.textDim, 1);
            char w[64];
            std::snprintf(w, sizeof(w), "carrying %.1f kg", carried(inv));
            ui.drawText(w, gx, infoY + 22, theme.textDim, 1);
        }

        // ── Mouse ─────────────────────────────────────────
        bool press = mouseDown && !mousePrev, rpress = rightDown && !rightPrev;
        bool release = !mouseDown && mousePrev, rrelease = !rightDown && rightPrev;
        if ((press || rpress) && drag < 0 && dragEquip < 0) {
            if (hoverSlot >= 0 && !inv.slots[hoverSlot].empty()) {
                if (shift && press) inv.toggleEquip(hoverSlot);
                else { drag = hoverSlot; dragHalf = rpress; dragX = mx; dragY = my; }
            } else if (hoverEquip >= 0 && !inv.equipped((EquipSlot)hoverEquip).empty()) {
                if (shift && press) inv.unequip((EquipSlot)hoverEquip);
                else { dragEquip = hoverEquip; dragX = mx; dragY = my; }
            } else if (press && hoverSlot >= 0 && hoverSlot < Inventory::HOTBAR) {
                inv.pressHotbarKey(hoverSlot);              // empty hotbar slot: select it
            }
        }
        if (drag >= 0 && ((dragHalf && rrelease) || (!dragHalf && release))) {
            bool moved = std::abs(mx - dragX) + std::abs(my - dragY) > 4.0;
            if (hoverSlot >= 0 && hoverSlot != drag) inv.move(drag, hoverSlot, dragHalf);
            else if (hoverEquip >= 0) inv.equipFrom(drag, (EquipSlot)hoverEquip);
            else if (!moved && hoverSlot == drag && drag < Inventory::HOTBAR) inv.pressHotbarKey(drag);
            drag = -1;
        }
        if (dragEquip >= 0 && release) {
            int s = inv.equip[dragEquip];
            if (hoverEquip >= 0 && hoverEquip != dragEquip) {
                // to another equipment slot it fits (weapon 1 <-> 2, ring <-> ring)
                int other = inv.equip[hoverEquip];
                if (s >= 0 && Inventory::fits(inv.slots[s].itemID, (EquipSlot)hoverEquip)) {
                    inv.equip[hoverEquip] = s;
                    inv.equip[dragEquip] = (other >= 0 && Inventory::fits(inv.slots[other].itemID, (EquipSlot)dragEquip)) ? other : -1;
                }
            } else if (hoverEquip < 0) {
                inv.unequip((EquipSlot)dragEquip);           // dragged away: take it off
            }
            dragEquip = -1;
        }
        // 1..0 over a slot: swap it with that hotbar slot
        if (hoverSlot >= 0 && drag < 0) {
            for (int k = 0; k < Inventory::HOTBAR; k++) {
                int key = k == 9 ? GLFW_KEY_0 : GLFW_KEY_1 + k;
                bool down = glfwGetKey(window, key) == GLFW_PRESS;
                if (down && !keyPrev[k] && hoverSlot != k) inv.move(hoverSlot, k);
                keyPrev[k] = down;
            }
        } else {
            for (int k = 0; k < Inventory::HOTBAR; k++)
                keyPrev[k] = glfwGetKey(window, k == 9 ? GLFW_KEY_0 : GLFW_KEY_1 + k) == GLFW_PRESS;
        }

        // the dragged item follows the mouse
        int dragging = drag >= 0 ? drag : (dragEquip >= 0 ? inv.equip[dragEquip] : -1);
        if (dragging >= 0 && !inv.slots[dragging].empty())
            swatch(ui, gItems.get(inv.slots[dragging].itemID), (float)mx - slot * 0.35f, (float)my - slot * 0.35f, slot * 0.7f);

        ui.end();
        return ScreenResult::Continue;
    }

private:
    GLFWwindow* window = nullptr;
    Player*     player = nullptr;
    double mx = 0, my = 0, dragX = 0, dragY = 0;
    bool   mouseDown = false, mousePrev = false, rightDown = false, rightPrev = false;
    bool   keyPrev[Inventory::HOTBAR] = {};
    float  slot = 48.0f;
    int    hoverSlot = -1, hoverEquip = -1;
    int    drag = -1, dragEquip = -1;
    bool   dragHalf = false;

    bool hovered(float x, float y, float w, float h) const {
        return mx >= x && mx < x + w && my >= y && my < y + h;
    }

    bool button(UIRenderer& ui, const char* text, float x, float y, float w, float h) {
        const auto& theme = gResourcePacks.getTheme();
        bool hov = hovered(x, y, w, h);
        ui.drawRect(x, y, w, h, hov ? theme.buttonBgHover : theme.buttonBg);
        ui.drawBorder(x, y, w, h, theme.buttonBorder, 1.0f);
        ui.drawTextCentered(text, x, y, w, h, hov ? theme.textHover : theme.text, 1);
        return hov && mouseDown && !mousePrev;
    }

    void drawSlot(UIRenderer& ui, const Inventory& inv, int i, float x, float y) {
        const auto& theme = gResourcePacks.getTheme();
        bool hov = hovered(x, y, slot, slot);
        if (hov) hoverSlot = i;
        const ItemStack& s = inv.slots[i];
        bool sel = i == inv.selected || i == inv.offhand;
        bool off = i < Inventory::OFFHAND;
        bool eq = !s.empty() && inv.isEquipped(i);
        bool fromHere = drag == i;
        float tint[4] = { theme.bg[0], theme.bg[1], theme.bg[2], theme.bg[3] };
        if (off) { tint[0] *= 0.8f; tint[1] *= 0.9f; tint[2] = std::min(255.0f, tint[2] * 1.4f + 10.0f); }
        ui.drawRect(x, y, slot, slot, hov ? theme.bgHover : tint);
        ui.drawBorder(x, y, slot, slot, sel ? theme.textHover : (eq ? theme.accent : theme.border), sel ? 3.0f : (eq ? 2.0f : 1.0f));
        if (!s.empty() && !fromHere) {
            const ItemDef& d = gItems.get(s.itemID);
            float m = slot * 0.18f;
            swatch(ui, d, x + m, y + m, slot - 2 * m);
            if (s.count > 1 && !inv.infinite) {
                std::string c = std::to_string(s.count);
                ui.drawText(c, x + slot - 3 - ui.textWidth(c, 1), y + slot - 11, theme.textHover, 1);
            }
            if (eq) ui.drawText("E", x + 3, y + slot - 11, theme.accent, 1);
        }
        if (i < Inventory::HOTBAR) {
            char num[2] = { (char)(i == 9 ? '0' : '1' + i), 0 };
            ui.drawText(num, x + 3, y + 3, sel ? theme.textHover : theme.textDim, 1);
        }
    }

    void drawEquip(UIRenderer& ui, const Inventory& inv, int e, float x, float y, float s) {
        const auto& theme = gResourcePacks.getTheme();
        bool hov = hovered(x, y, 220.0f, s);
        if (hov) hoverEquip = e;
        const ItemStack& st = inv.equipped((EquipSlot)e);
        bool active = (e == (int)EquipSlot::Weapon1 || e == (int)EquipSlot::Weapon2) && e == (int)inv.weaponSlot();
        // would the dragged item fit here
        int dragging = drag >= 0 ? drag : -1;
        bool fits = dragging >= 0 && !inv.slots[dragging].empty() && Inventory::fits(inv.slots[dragging].itemID, (EquipSlot)e);
        ui.drawRect(x, y, s, s, hov ? theme.bgHover : theme.bg);
        ui.drawBorder(x, y, s, s, fits ? theme.textHover : (active ? theme.accent : theme.border), fits || active ? 2.0f : 1.0f);
        if (!st.empty() && dragEquip != e) swatch(ui, gItems.get(st.itemID), x + 4, y + 4, s - 8);
        std::string label = equipSlotName((EquipSlot)e);
        if (active) label += "  - drawn";
        ui.drawText(label, x + s + 8, y + 2, active ? theme.accent : theme.textDim, 1);
        std::string item = st.empty() ? (e >= (int)EquipSlot::Ring1 ? "empty - none yet" : "empty")
                                      : gItems.get(st.itemID).displayName;
        ui.drawText(item.size() > 22 ? item.substr(0, 21) + "." : item, x + s + 8, y + s - 12,
                    st.empty() ? theme.textDim : theme.text, 1);
    }

    // The item's icon (ui/ItemIcon.h: blocks in their textures)
    static void swatch(UIRenderer& ui, const ItemDef& d, float x, float y, float s) {
        drawItemIcon(ui, d, x, y, s);
    }

    static std::string tags(const ItemDef& d) {
        std::string t;
        auto add = [&](bool on, const std::string& n) { if (on) { if (!t.empty()) t += ", "; t += n; } };
        add(d.is(IC_BLOCK), "block"); add(d.is(IC_TOOL), "tool"); add(d.is(IC_WEAPON), "weapon");
        add(d.is(IC_DEV), "dev"); add(d.is(IC_WORN), std::string("worn: ") + wornSlotKey((WornSlot)d.worn));
        add(!d.accessory.empty(), d.accessory);
        char b[48];
        if (d.canFight()) { std::snprintf(b, sizeof(b), "damage %.0f", d.damage); add(true, b); }
        if (d.canFight() && !d.is(IC_WEAPON)) add(true, "fights when equipped");
        if (d.weight > 0.0f) { std::snprintf(b, sizeof(b), "%.1f kg", d.weight); add(true, b); }
        return t;
    }

    static float carried(const Inventory& inv) {
        float kg = 0.0f;
        for (const ItemStack& s : inv.slots) if (!s.empty()) kg += gItems.get(s.itemID).weight * s.count;
        return kg;
    }
};
