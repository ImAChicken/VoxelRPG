#pragma once
#include <glm/glm.hpp>
#include <string>
#include "UIRenderer.h"
#include "ItemIcon.h"
#include "../entity/Player.h"
#include "../entity/PlayerController.h"
#include "../game/DevTools.h"
#include "../game/Settings.h"
#include "../game/BuildTool.h"
#include "../game/PlayerInteraction.h"
#include "../render/ResourcePack.h"
#include <algorithm>
#include <cstdio>
#include <string>

// ══════════════════════════════════════════════════════════
// HUD — what is drawn over the world while playing
// ══════════════════════════════════════════════════════════
// Which pieces show comes from the player's rules
// (game/GameMode.h):
//
//   Spectator  mode name
//   Test Play  mode name, crosshair, dev toolbar, dev tool
//              hints, selection / clipboard / brush readout
//   Survival   mode name, health, toolbar + weapon slot,
//              crosshair, dig progress
//   Combat     mode name, health, weapon panel, target ring
//
// Draw it after the world and before any menu.
class Hud {
public:
    const BuildTool* build = nullptr;   // for the hammer readout

    // Enemy health bars, placed over their heads by the game
    // each frame (screen pixels, origin top-left)
    struct EnemyBar { float x, y, fill, alpha; bool hurt; };
    std::vector<EnemyBar> enemyBars;
    // The locked-on target (game/LockOn.h): where to put the marker
    struct LockMarker { bool on = false; float x = 0, y = 0; };
    LockMarker lockMarker;

    // The debug overlay (F3): labels placed by the game (screen
    // pixels) and a block of text for the corner
    struct DebugLabel { float x, y; std::string text; glm::vec3 color; };
    std::vector<DebugLabel> debugLabels;
    std::vector<std::string> debugText;
    std::string talkingTo;              // an NPC menu is open (placeholder until menus exist)

    // Encounter bookends (npc/Encounters.h): a banner when a
    // fight starts, a review when it ends. Neither pauses.
    std::string encounterBanner;
    bool        devWorld = false;          // the open world is a dev world (main.cpp sets it)
    float encounterBannerAge = 99.0f;
    std::vector<std::string> review;
    float reviewAge = 99.0f;
    static constexpr float BANNER_TIME = 3.0f, REVIEW_TIME = 10.0f;
    float frameDt = 0.0f;               // set by the game each frame (timers above)

    void draw(UIRenderer& ui, const Player& player,
              const PlayerController& controller,
              const PlayerInteraction& hands, const DevTools& dev) {
        const GameModeRules rules = player.rules();
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth();
        float sh = (float)ui.getHeight();
        bool  combat = (rules.hud == HudKind::Combat);

        ui.begin();

        // ── Mode name, top left (every mode) ──────────────
        std::string label = rules.name;
        float labelW = (float)ui.textWidth(label, 2) + 16.0f;
        ui.drawRect(10.0f, 10.0f, labelW, 28.0f, theme.bg);
        ui.drawBorder(10.0f, 10.0f, labelW, 28.0f,
            combat ? COMBAT_RED : theme.border, 1.0f);
        ui.drawText(label, 18.0f, 16.0f,
            combat ? COMBAT_RED : theme.text, 2);
        if (devWorld) {
            // a dev world saves only when you do (Esc → Save)
            const char* tag = "dev world";
            float tw = (float)ui.textWidth(tag, 1) + 12.0f;
            ui.drawRect(16.0f + labelW, 10.0f, tw, 28.0f, theme.bg);
            ui.drawBorder(16.0f + labelW, 10.0f, tw, 28.0f, theme.accent, 1.0f);
            ui.drawText(tag, 22.0f + labelW, 20.0f, theme.accent, 1);
        }

        // ── Centre of the screen ──────────────────────────
        if (combat)
            drawTargetRing(ui, sw * 0.5f, sh * 0.5f);
        else if (rules.hasBody)
            drawCrosshair(ui, sw * 0.5f, sh * 0.5f);

        // Dig progress under the crosshair
        if (hands.breakProgress > 0.0f && !combat) {
            float w = 60.0f;
            ui.drawRect(sw * 0.5f - w * 0.5f, sh * 0.5f + 14.0f, w, 6.0f, SHADOW);
            ui.drawRect(sw * 0.5f - w * 0.5f, sh * 0.5f + 14.0f,
                        w * hands.breakProgress, 6.0f, WHITE);
        }

        // ── Encounter bookends ────────────────────────────
        encounterBannerAge += frameDt;
        reviewAge += frameDt;
        frameDt = 0.0f;
        if (encounterBannerAge < BANNER_TIME && !encounterBanner.empty()) {
            float a = std::clamp(std::min(encounterBannerAge * 4.0f, (BANNER_TIME - encounterBannerAge) * 2.0f), 0.0f, 1.0f);
            float w = (float)ui.textWidth(encounterBanner, 3);
            float bg[4] = {20, 8, 8, 190 * a};
            float col[4] = {235, 90, 70, 255 * a};
            ui.drawRect(0, sh * 0.2f - 14, sw, 52, bg);
            ui.drawText(encounterBanner, (sw - w) * 0.5f, sh * 0.2f, col, 3);
        }
        if (reviewAge < REVIEW_TIME && !review.empty()) {
            float a = std::clamp(std::min(reviewAge * 4.0f, (REVIEW_TIME - reviewAge) * 1.5f), 0.0f, 1.0f);
            float pw = 0.0f;
            for (size_t i = 0; i < review.size(); i++) pw = std::max(pw, (float)ui.textWidth(review[i], i ? 2 : 3));
            pw += 48.0f;
            float ph = 30.0f + 34.0f + (review.size() - 1) * 24.0f;
            float px = (sw - pw) * 0.5f, py = sh * 0.30f;
            float bg[4] = {12, 10, 8, 210 * a};
            float title[4] = {240, 210, 140, 255 * a};
            float text[4] = {225, 220, 210, 255 * a};
            ui.drawRect(px, py, pw, ph, bg);
            ui.drawText(review[0], px + (pw - ui.textWidth(review[0], 3)) * 0.5f, py + 14, title, 3);
            for (size_t i = 1; i < review.size(); i++)
                ui.drawText(review[i], px + 24, py + 30 + 34 + (i - 1) * 24.0f - 20, text, 2);
        }

        // ── Talking to an NPC (a placeholder menu) ────────
        if (!talkingTo.empty()) {
            std::string t = "Talking to " + talkingTo + "   (E to leave)";
            float w = (float)ui.textWidth(t, 2);
            float bg[4] = {0, 0, 0, 170};
            float col[4] = {240, 230, 200, 255};
            ui.drawRect((sw - w) * 0.5f - 12, sh * 0.62f - 8, w + 24, 34, bg);
            ui.drawText(t, (sw - w) * 0.5f, sh * 0.62f, col, 2);
        }

        // ── Debug overlay ─────────────────────────────────
        for (const DebugLabel& l : debugLabels) {
            float w = (float)ui.textWidth(l.text, 1);
            float bg[4] = {0, 0, 0, 150};
            float col[4] = {l.color.r * 255, l.color.g * 255, l.color.b * 255, 255};
            ui.drawRect(l.x - w * 0.5f - 2, l.y - 1, w + 4, 11, bg);
            ui.drawText(l.text, l.x - w * 0.5f, l.y, col, 1);
        }
        if (!debugText.empty()) {
            float y = 150.0f, w = 0.0f;
            for (const auto& t : debugText) w = std::max(w, (float)ui.textWidth(t, 1));
            float bg[4] = {0, 0, 0, 160};
            ui.drawRect(6, y - 4, w + 12, debugText.size() * 12.0f + 8, bg);
            float col[4] = {230, 230, 230, 255};
            for (const auto& t : debugText) { ui.drawText(t, 12, y, col, 1); y += 12.0f; }
        }

        // ── Enemies' health, over their heads ─────────────
        for (const EnemyBar& b : enemyBars) {
            const float w = 64.0f, h = 7.0f;
            float sh0[4] = {0, 0, 0, 160 * b.alpha};
            float red[4] = {210, 60, 50, 255 * b.alpha};
            ui.drawRect(b.x - w * 0.5f, b.y, w, h, sh0);
            ui.drawRect(b.x - w * 0.5f + 1, b.y + 1, (w - 2) * std::clamp(b.fill, 0.0f, 1.0f), h - 2, red);
        }

        // ── The lock-on marker: a small dotted diamond ────
        if (lockMarker.on) {
            float ink[4] = {20, 14, 10, 230}, gold[4] = {255, 214, 90, 255};
            const float r = 9.0f;
            for (int i = 0; i < 12; i++) {
                float a = i * 3.14159265f / 6.0f;
                float px = lockMarker.x + std::sin(a) * r * (1.0f - std::fabs(std::cos(a)) * 0.0f);
                float py = lockMarker.y - std::cos(a) * r;
                // a diamond: |dx| + |dy| = r
                float dx = std::sin(a), dy = -std::cos(a), k = r / (std::fabs(dx) + std::fabs(dy));
                px = lockMarker.x + dx * k; py = lockMarker.y + dy * k;
                ui.drawRect(px - 2.5f, py - 2.5f, 5, 5, ink);
                ui.drawRect(px - 1.5f, py - 1.5f, 3, 3, gold);
            }
            ui.drawRect(lockMarker.x - 2.0f, lockMarker.y - 2.0f, 4, 4, gold);
        }

        // ── Health (Survival, both stances) and stamina ───
        // bottom left, or above the hotbar's left end when the
        // hotbar reaches that far
        float barsBottom = sh;
        {
            const float k = gSettings.toolbarScale, slot = 44.0f * k, gap = 4.0f * k;
            int n = Inventory::HOTBAR;
            float hw = n * slot + (n - 1) * gap + 12.0f * k + slot + 16.0f * k;
            if ((sw - hw) * 0.5f < 240.0f && rules.hasBody && !combat) barsBottom = sh - slot - 22.0f;
        }
        if (rules.tracksHealth)
            drawHealth(ui, player, 10.0f, barsBottom - 42.0f);
        if (rules.tracksHealth || player.weaponDamage > 0.0f)
            drawStamina(ui, player, 10.0f, barsBottom - (rules.tracksHealth ? 58.0f : 26.0f));
        if (rules.tracksHealth && player.dead()) {
            std::string died = "YOU DIED";
            float w = (float)ui.textWidth(died, 4);
            ui.drawText(died, (sw - w) * 0.5f, sh * 0.38f, COMBAT_RED, 4);
        }

        // ── Bottom centre: toolbar or weapon panel ────────
        if (combat) {
            drawWeaponPanel(ui, player, controller, sw, sh);
        } else if (rules.hasBody) {
            const Inventory& inv = player.activeInventory();
            float top = drawToolbar(ui, inv, sw, sh, player.mode == GameMode::Survival);

            // Name of the held item (and dev tool hint and
            // status) just above the toolbar
            const ItemDef& d = inv.heldDef();
            if (d.valid()) {
                std::string name = d.displayName;
                float w = (float)ui.textWidth(name, 2);
                ui.drawText(name, (sw - w) * 0.5f, top - 26.0f, theme.textHover, 2);
                if (d.is(IC_DEV) && player.mode != GameMode::Survival) {
                    std::string hint = DevTools::toolHint(d.devTool);
                    float hw = (float)ui.textWidth(hint, 1);
                    ui.drawText(hint, (sw - hw) * 0.5f, top - 40.0f, theme.textDim, 1);
                    drawToolStatus(ui, d.devTool, dev, sw, sh, top - 66.0f);
                }
                if (d.is(IC_DEV) && player.mode == GameMode::Survival) {
                    std::string no = "dev tools only work in Test Play";
                    float nw = (float)ui.textWidth(no, 1);
                    ui.drawText(no, (sw - nw) * 0.5f, top - 40.0f, COMBAT_RED, 1);
                }
            }
        }

        // ── Structure under the crosshair ─────────────────
        if (!hands.aimLabel.empty() && !combat) {
            float w = (float)ui.textWidth(hands.aimLabel, 2);
            ui.drawRect((sw - w) * 0.5f - 6, sh * 0.5f + 22, w + 12, 21, SHADOW);
            ui.drawText(hands.aimLabel, (sw - w) * 0.5f, sh * 0.5f + 25,
                        theme.textHover, 2);
        }

        // ── Tree health while chopping ────────────────────
        if (hands.treeAge < 2.5f && hands.treeMax > 0.0f) {
            float w = 200.0f, x = (sw - w) * 0.5f, y = sh * 0.5f - 60.0f;
            ui.drawText(hands.treeName, x, y - 18, theme.textHover, 2);
            ui.drawRect(x, y, w, 12, SHADOW);
            float green[4] = {90, 200, 90, 255};
            ui.drawRect(x + 2, y + 2, (w - 4) * (hands.treeHealth / hands.treeMax), 8, green);
        }

        // ── Builder's Hammer: what it will build ──────────
        if (hands.hammerActive && build) {
            const StructureType* t = build->type();
            std::string line1 = t ? t->displayName + "   " + t->feet()
                                  : std::string("Builder's Hammer");
            std::string line2 = t ? (player.mode == GameMode::Survival
                                     ? "cost: " + t->costText(gStructures.costsEnabled) : std::string("free in Test Play"))
                                  : std::string("right click: choose a structure");
            std::string line3 = hands.previewWhy.empty()
                ? (build->locked ? "LOCKED - left click builds" : "left click builds")
                : hands.previewWhy;
            if (build->mode == HammerMode::Copy) {
                line1 = "Copy a building";
                line2 = "click any piece of it to save the whole building";
                line3 = hands.previewWhy.empty() ? hands.aimLabel : hands.previewWhy;
            } else if (build->mode == HammerMode::Blueprint) {
                const Blueprint* bp = build->chosenBlueprint();
                line1 = bp ? bp->display + "   " + std::to_string(bp->size.x / 3) + " x " + std::to_string(bp->size.z / 3) + " ft"
                           : std::string("Blueprint");
                line2 = bp ? std::to_string(bp->parts.size()) + " pieces   T turns it" + (build->style.sitePrep ? "   levels the ground" : "")
                           : std::string("right click: choose a blueprint");
                if (hands.previewWhy.empty()) line3 = build->locked ? "LOCKED - left click places it" : "left click places it";
            }
            float y = sh - 196.0f;     // above the held item's name
            const std::string* lines[3] = { &line1, &line2, &line3 };
            for (int i = 0; i < 3; i++) {
                float w = (float)ui.textWidth(*lines[i], 2);
                ui.drawRect((sw - w) * 0.5f - 6, y - 3, w + 12, 21, SHADOW);
                const float* col = i == 2 ? (hands.previewWhy.empty() ? theme.accent : COMBAT_RED)
                                          : (i == 0 ? theme.textHover : theme.textDim);
                ui.drawText(*lines[i], (sw - w) * 0.5f, y, col, 2);
                y += 22;
            }
        }

        // ── Dev readout, top right (Test Play) ────────────
        if (player.mode == GameMode::Dev)
            drawDevReadout(ui, dev, sw);

        // ── Short notice ("inventory full") ───────────────
        if (hands.noticeAge < 2.5f) {
            float w = (float)ui.textWidth(hands.notice, 2);
            ui.drawText(hands.notice, (sw - w) * 0.5f, sh * 0.5f + 40.0f,
                        COMBAT_RED, 2);
        }

        // ── The attack in progress (game/Combat.h) ────────
        if (player.attack.active()) {
            std::string note = combat::kindName(player.attack.kind);
            float w = (float)ui.textWidth(note, 2);
            ui.drawText(note, (sw - w) * 0.5f, sh - 150.0f, COMBAT_RED, 2);
        } else if (combat && controller.lastAction == PlayerAction::Guard && controller.guarding) {
            std::string note = playerActionName(controller.lastAction);
            float w = (float)ui.textWidth(note, 2);
            ui.drawText(note, (sw - w) * 0.5f, sh - 150.0f, COMBAT_RED, 2);
        }

        ui.end();
    }

private:
    // Colours are red, green, blue, alpha, each 0..255
    const float WHITE[4]      = {255, 255, 255, 220};
    const float SHADOW[4]     = {0, 0, 0, 160};
    const float HEALTH_RED[4] = {200, 50, 50, 255};
    const float COMBAT_RED[4] = {255, 110, 90, 255};

    void drawCrosshair(UIRenderer& ui, float cx, float cy) {
        ui.drawRect(cx - 7.0f, cy - 1.0f, 14.0f, 2.0f, WHITE);
        ui.drawRect(cx - 1.0f, cy - 7.0f, 2.0f, 14.0f, WHITE);
    }

    // Four corner brackets — where a locked-on target will
    // be framed once combat exists
    void drawTargetRing(UIRenderer& ui, float cx, float cy) {
        const float r = 22.0f, len = 9.0f, t = 2.0f;
        for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            float x = cx + sx * r, y = cy + sy * r;
            ui.drawRect(sx < 0 ? x : x - len, y - t * 0.5f, len, t, COMBAT_RED);
            ui.drawRect(x - t * 0.5f, sy < 0 ? y : y - len, t, len, COMBAT_RED);
        }
    }

    void drawHealth(UIRenderer& ui, const Player& player, float x, float y) {
        const auto& theme = gResourcePacks.getTheme();
        const float w = 220.0f, h = 22.0f;
        float fill = player.maxHealth > 0.0f ? player.health / player.maxHealth : 0.0f;
        fill = std::max(0.0f, std::min(1.0f, fill));
        ui.drawRect(x, y, w, h, SHADOW);
        ui.drawRect(x + 2.0f, y + 2.0f, (w - 4.0f) * fill, h - 4.0f, HEALTH_RED);
        ui.drawBorder(x, y, w, h, theme.border, 1.0f);
        char text[32];
        std::snprintf(text, sizeof(text), "%.0f / %.0f", player.health, player.maxHealth);
        ui.drawTextCentered(text, x, y, w, h, WHITE, 2);
    }

    void drawStamina(UIRenderer& ui, const Player& player, float x, float y) {
        const float w = 220.0f, h = 12.0f;
        float fill = std::clamp(player.stamina / std::max(player.maxStamina, 1.0f), 0.0f, 1.0f);
        const float green[4] = {90, 190, 80, 255};
        const float grey[4]  = {120, 120, 110, 255};
        ui.drawRect(x, y, w, h, SHADOW);
        ui.drawRect(x + 2.0f, y + 2.0f, (w - 4.0f) * fill, h - 4.0f, player.staminaWait > 0.0f ? grey : green);
    }

    // The hotbar: off-hand slots 1 and 2, a gap, main-hand slots
    // 3..0, then the active weapon (Survival only).
    // Size and opacity come from Settings. Returns the top
    // edge so text can sit above it.
    float drawToolbar(UIRenderer& ui, const Inventory& inv,
                      float sw, float sh, bool showWeapon) {
        const auto& theme = gResourcePacks.getTheme();
        const float k    = gSettings.toolbarScale;
        const float a    = gSettings.toolbarOpacity;
        const float slot = 44.0f * k, gap = 4.0f * k;
        const int   n    = Inventory::HOTBAR;
        float total = n * slot + (n - 1) * gap + 12.0f * k + (showWeapon ? slot + 16.0f * k : 0.0f);
        float x = (sw - total) * 0.5f;
        float y = sh - slot - 14.0f;

        float bg[4], edge[4], sel[4], red[4], dim[4], hi[4], white[4];
        fade(theme.bg, a, bg);       fade(theme.border, a, edge);
        fade(theme.accent, a, sel);  fade(COMBAT_RED, a, red);
        fade(theme.textDim, a, dim); fade(theme.textHover, a, hi);
        fade(WHITE, a, white);

        for (int i = 0; i <= n; i++) {
            bool weaponSlot = (i == n);
            if (weaponSlot && !showWeapon) break;
            if (weaponSlot) x += 16.0f * k;
            if (i == Inventory::OFFHAND) x += 12.0f * k;      // off-hand | main hand
            const ItemStack& s = weaponSlot ? inv.weapon() : inv.slots[i];
            bool off = i < Inventory::OFFHAND;
            bool on = !weaponSlot && (i == inv.selected || i == inv.offhand);
            ui.drawRect(x, y, slot, slot, bg);
            ui.drawBorder(x, y, slot, slot,
                weaponSlot ? red : (on ? (off ? hi : sel) : edge), on ? 3.0f : 1.0f);
            if (!s.empty()) {
                const ItemDef& d = gItems.get(s.itemID);
                float m = (d.is(IC_BLOCK) ? 5.0f : 9.0f) * k;
                drawItemIcon(ui, d, x + m, y + m, slot - 2 * m, 255.0f * a);
                if (s.count > 1 && !inv.infinite) {
                    std::string cnt = std::to_string(s.count);
                    ui.drawText(cnt, x + slot - 3 - ui.textWidth(cnt, 1),
                                y + slot - 11, white, 1);
                }
            }
            if (!weaponSlot && k >= 0.7f) {
                char num[2] = { (char)(i == 9 ? '0' : '1' + i), 0 };
                ui.drawText(num, x + 3.0f, y + 3.0f, on ? hi : dim, 1);
                if (!s.empty() && inv.isEquipped(i))
                    ui.drawText("E", x + 3.0f, y + slot - 11.0f, sel, 1);   // equipped
            }
            if (weaponSlot && k >= 0.7f)
                ui.drawText(inv.activeWeapon ? "2" : "1", x + 3.0f, y + 3.0f, red, 1);
            if (off && i == 0 && k >= 0.7f)
                ui.drawText("L", x - 1.0f, y - 11.0f, dim, 1);
            x += slot + gap;
        }
        return y;
    }

    // Copy a colour with its alpha scaled
    static void fade(const float* in, float a, float* out) {
        out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = in[3] * a;
    }

    void drawWeaponPanel(UIRenderer& ui, const Player& player,
                         const PlayerController& controller, float sw, float sh) {
        const auto& theme = gResourcePacks.getTheme();
        const float w = 300.0f, h = 44.0f;
        float x = (sw - w) * 0.5f, y = sh - h - 14.0f;
        const ItemStack& wpn = player.activeInventory().weapon();
        std::string name = wpn.empty() ? "UNARMED"
                                       : gItems.get(wpn.itemID).displayName;
        name += player.activeInventory().activeWeapon ? "  [2]" : "  [1]";
        ui.drawRect(x, y, w, h, theme.bg);
        ui.drawBorder(x, y, w, h, COMBAT_RED, controller.guarding ? 3.0f : 1.0f);
        ui.drawTextCentered(controller.guarding ? "GUARDING" : name,
                            x, y, w, h, controller.guarding ? WHITE : COMBAT_RED, 2);
    }

    // Live state of the held dev tool
    void drawToolStatus(UIRenderer& ui, DevToolKind tool, const DevTools& dev,
                        float sw, float sh, float y) {
        const auto& theme = gResourcePacks.getTheme();
        auto centred = [&](const std::string& t, float yy, const float* col) {
            float w = (float)ui.textWidth(t, 2);
            ui.drawRect((sw - w) * 0.5f - 6, yy - 3, w + 12, 21, SHADOW);
            ui.drawText(t, (sw - w) * 0.5f, yy, col, 2);
        };
        char b[128];
        switch (tool) {
        case DevToolKind::Copy:
            if (dev.copyPending) {
                // The question, in the middle of the screen
                std::snprintf(b, sizeof(b), "Point %d %d %d", dev.copyPoint.x,
                              dev.copyPoint.y, dev.copyPoint.z);
                float py = sh * 0.5f + 46.0f;
                centred(b, py, theme.textHover);
                centred("Set as corner  1  or  2 ?", py + 24, theme.accent);
                centred("press 1 or 2   Backspace cancels", py + 48, theme.textDim);
            }
            break;
        case DevToolKind::Paste:
            if (dev.pasteLocked) {
                std::snprintf(b, sizeof(b), "LOCKED at %d %d %d", dev.pasteSpot.x,
                              dev.pasteSpot.y, dev.pasteSpot.z);
                centred(b, y, theme.accent);
            } else {
                centred("follows crosshair - arrow keys lock it", y, theme.textDim);
            }
            break;
        case DevToolKind::Explode:
            std::snprintf(b, sizeof(b), "blast radius %d   right click %d",
                          (int)dev.brushRadius, (int)(dev.brushRadius * 2));
            centred(b, y, COMBAT_RED);
            break;
        case DevToolKind::Brush:
            std::snprintf(b, sizeof(b), "brush radius %d", (int)dev.brushRadius);
            centred(b, y, theme.accent);
            break;
        case DevToolKind::Cube:
            std::snprintf(b, sizeof(b), "cube %d voxels = %d in   %s", dev.cubeSize, dev.cubeSize * 4,
                          dev.cubeFree ? "off grid" : "on grid");
            centred(b, y, theme.accent);
            break;
        default:
            break;
        }
    }

    void drawDevReadout(UIRenderer& ui, const DevTools& dev, float sw) {
        const auto& theme = gResourcePacks.getTheme();
        std::string lines[4];
        lines[0] = "material " + DevTools::blockName(dev.material);
        lines[1] = "brush " + std::to_string((int)dev.brushRadius);
        lines[2] = dev.hasSelection()
            ? "sel " + std::to_string(dev.selSize().x) + "x" +
              std::to_string(dev.selSize().y) + "x" + std::to_string(dev.selSize().z)
            : (dev.hasPos1 || dev.hasPos2 ? "sel: one corner" : "no selection");
        lines[3] = dev.clipboard
            ? "clip " + std::to_string(dev.clipboard->size.x) + "x" +
              std::to_string(dev.clipboard->size.y) + "x" +
              std::to_string(dev.clipboard->size.z)
            : "clipboard empty";
        float y = 12.0f;
        for (auto& l : lines) {
            float w = (float)ui.textWidth(l, 2);
            ui.drawRect(sw - w - 20.0f, y - 2.0f, w + 10.0f, 19.0f, SHADOW);
            ui.drawText(l, sw - w - 15.0f, y, theme.text, 2);
            y += 20.0f;
        }
    }
};
