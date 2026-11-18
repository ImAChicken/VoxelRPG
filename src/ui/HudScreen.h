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
    // How long each popup stays (seconds); all fade out over their
    // last FADE. One place, so they can be tuned together.
    static constexpr float NOTICE_TIME = 1.8f, BANNER_TIME = 2.2f, REVIEW_TIME = 5.0f, FADE = 0.4f;
    static float popupAlpha(float age, float life) {
        return std::clamp(std::min(age * 6.0f, (life - age) / FADE), 0.0f, 1.0f);
    }
    float frameDt = 0.0f;               // set by the game each frame (timers above)
    bool  padDebug = false;             // the controller debug overlay (console: debug pad)

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
        // the review goes as soon as you fight again or a new fight starts
        if (player.attack.active() || encounterBannerAge < reviewAge) reviewAge = std::max(reviewAge, REVIEW_TIME - FADE);
        if (encounterBannerAge < BANNER_TIME && !encounterBanner.empty()) {
            float a = popupAlpha(encounterBannerAge, BANNER_TIME);
            float w = (float)ui.textWidth(encounterBanner, 3);
            float bg[4] = {20, 8, 8, 190 * a};
            float col[4] = {235, 90, 70, 255 * a};
            ui.drawRect(0, sh * 0.2f - 14, sw, 52, bg);
            ui.drawText(encounterBanner, (sw - w) * 0.5f, sh * 0.2f, col, 3);
        }
        if (reviewAge < REVIEW_TIME && !review.empty()) {
            float a = popupAlpha(reviewAge, REVIEW_TIME);
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

        // ── Controller debug (debug pad): the pad's buttons,
        //    lit by the pad or by the keys standing in for them ─
        if (padDebug) drawPadDebug(ui, sw, sh);

        // ── Dev readout, top right (Test Play) ────────────
        if (player.mode == GameMode::Dev)
            drawDevReadout(ui, dev, sw);

        // ── Short notice ("inventory full") ───────────────
        if (hands.noticeAge < NOTICE_TIME) {
            float w = (float)ui.textWidth(hands.notice, 2);
            float col[4] = { COMBAT_RED[0], COMBAT_RED[1], COMBAT_RED[2], COMBAT_RED[3] * popupAlpha(hands.noticeAge, NOTICE_TIME) };
            ui.drawText(hands.notice, (sw - w) * 0.5f, sh * 0.5f + 40.0f, col, 2);
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

    // A small controller, bottom left: each button lit while it
    // (or the key bound to the same action in this layer) is down;
    // the sticks as dots; the layer's name
    void drawPadDebug(UIRenderer& ui, float sw, float sh) {
        (void)sw;
        const auto& theme = gResourcePacks.getTheme();
        const pad::State& s = gInput.padNow();
        pad::Layer layer = gInput.layerNow();
        if (layer == pad::Layer::Combat && gInput.isHeld(InputAction::QuickLayer)) layer = pad::Layer::Quick;   // its key stands in for Start
        const auto& held = gInput.heldStates();
        float x0 = 20.0f, y0 = sh - 330.0f, W = 300.0f, H = 160.0f;
        ui.drawRect(x0, y0, W, H, SHADOW);
        ui.drawBorder(x0, y0, W, H, theme.border, 1.0f);
        const char* ln = layer == pad::Layer::Combat ? "combat" : layer == pad::Layer::Quick ? "quick (Start held)" : "explore";
        ui.drawText(std::string(s.connected ? "pad" : "no pad: keys shown") + "   " + ln, x0 + 8, y0 + 6, theme.textDim, 1);
        auto btn = [&](int b, float bx, float by, float bw = 30.0f, float bh = 18.0f) {
            bool lit = pad::lit(s, (pad::Button)b, layer, held);
            ui.drawRect(x0 + bx, y0 + by, bw, bh, lit ? theme.accent : theme.inputBg);
            ui.drawBorder(x0 + bx, y0 + by, bw, bh, theme.border, 1.0f);
            const char* n = pad::buttonName(b);
            float tw = (float)ui.textWidth(n, 1);
            ui.drawText(n, x0 + bx + (bw - tw) * 0.5f, y0 + by + 5, lit ? WHITE : theme.textDim, 1);
        };
        btn(pad::LT, 14, 22); btn(pad::LB, 14, 42);
        btn(pad::RT, W - 44, 22); btn(pad::RB, W - 44, 42);
        btn(pad::Back, 96, 22, 50); btn(pad::Start, 154, 22, 50);
        btn(pad::Up, 52, 84, 40); btn(pad::Left, 10, 104, 40); btn(pad::Right, 94, 104, 40); btn(pad::Down, 52, 124, 40);
        btn(pad::Y, W - 74, 84); btn(pad::X, W - 104, 104); btn(pad::B, W - 44, 104); btn(pad::A, W - 74, 124);
        btn(pad::L3, 112, 128, 36); btn(pad::R3, 152, 128, 36);
        // sticks
        auto stick = [&](glm::vec2 v, float sx, float sy) {
            ui.drawBorder(x0 + sx - 14, y0 + sy - 14, 28, 28, theme.border, 1.0f);
            ui.drawRect(x0 + sx + v.x * 10 - 3, y0 + sy + v.y * 10 - 3, 6, 6, WHITE);
        };
        stick(s.left, 130, 96); stick(s.right, 170, 96);
    }

    // ── Combat: the controller layout (game/CombatKit.h) ─
    // Bottom centre: the weapon set. Bottom right: the face
    // buttons (X / Y / B items, A jump) and the shoulders (LB, LT,
    // RB, RT) with what each does now. Start held: the 8 quick
    // options. Button names follow what was used last: the pad's
    // (A, LB …) or the keys bound to the same actions.
    std::string glyph(int padButton, InputAction keyAction) const {
        if (gInput.padLastUsed()) return pad::buttonName(padButton);
        return InputManager::bindingLabel(gInput.getBinding(keyAction));
    }
    void drawWeaponPanel(UIRenderer& ui, const Player& player,
                         const PlayerController& controller, float sw, float sh) {
        const auto& theme = gResourcePacks.getTheme();
        const CombatKit& kit = player.kit;
        const Inventory& inv = player.inventory;

        // the weapon set, bottom centre
        const float w = 360.0f, h = 44.0f;
        float x = (sw - w) * 0.5f, y = sh - h - 14.0f;
        const ItemStack& wpn = inv.weapon();
        std::string name = wpn.empty() ? "UNARMED" : gItems.get(wpn.itemID).displayName;
        if (kit.current().off) name += " + " + gItems.get(kit.current().off).displayName;
        ui.drawRect(x, y, w, h, theme.bg);
        ui.drawBorder(x, y, w, h, COMBAT_RED, controller.guarding ? 3.0f : 1.0f);
        ui.drawTextCentered(controller.guarding ? "GUARDING" : name, x, y, w, h, controller.guarding ? WHITE : COMBAT_RED, 2);
        {
            std::string n = "loadout " + std::to_string(kit.setAt + 1) + "   " +
                            glyph(pad::Down, InputAction::SwapWeapon) + ": the other";
            float nw = (float)ui.textWidth(n, 1);
            ui.drawRect((sw - nw) * 0.5f - 4, y - 16.0f, nw + 8, 13, SHADOW);
            ui.drawText(n, (sw - nw) * 0.5f, y - 14.0f, theme.text, 1);
        }

        // the buttons, bottom right
        const float cell = 58.0f;
        float cx = sw - cell * 2.0f - 30.0f, cy = sh - cell * 2.0f - 40.0f;     // centre of the diamond
        auto button = [&](float bx, float by, const std::string& key, uint16_t item, const std::string& label, bool lit) {
            float x0 = bx - cell * 0.5f, y0 = by - cell * 0.5f;
            ui.drawRect(x0, y0, cell, cell, lit ? theme.bgHover : SHADOW);
            ui.drawBorder(x0, y0, cell, cell, lit ? WHITE : theme.border, lit ? 2.0f : 1.0f);
            if (item) drawItemIcon(ui, gItems.get(item), x0 + 9, y0 + 9, cell - 18);
            else {
                float lw = (float)ui.textWidth(label, 1);
                ui.drawText(label, bx - lw * 0.5f, by - 4, theme.textDim, 1);
            }
            float kw = (float)ui.textWidth(key, 1);
            ui.drawRect(x0 + 2, y0 + 2, kw + 6, 12, SHADOW);
            ui.drawText(key, x0 + 5, y0 + 4, theme.accent, 1);
        };
        const auto& held = gInput.heldStates();
        auto on = [&](InputAction a) { return held[(int)a]; };
        if (gInput.isHeld(InputAction::QuickLayer)) {
            // the quick layer: D-pad and the face buttons, 8 fixed
            const int pb[8] = { pad::Left, pad::Up, pad::Right, pad::Down, pad::X, pad::Y, pad::B, pad::A };
            const glm::vec2 at[8] = { {-1, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 0}, {0, 1} };
            for (int i = 0; i < CombatKit::QUICK; i++) {
                float ox = i < 4 ? -cell * 3.4f : 0.0f;
                std::string key = gInput.padLastUsed() ? std::string(pad::buttonName(pb[i]))
                                                       : InputManager::bindingLabel(gInput.getBinding((InputAction)((int)InputAction::ToolbarSlot1 + i)));
                button(cx + ox + at[i].x * cell, cy + at[i].y * cell, key, kit.quick[i], "empty",
                       on((InputAction)((int)InputAction::ToolbarSlot1 + i)));
            }
            std::string t = "QUICK";
            ui.drawText(t, cx - cell * 1.7f - ui.textWidth(t, 2) * 0.5f, cy - cell * 2.0f, theme.title, 2);
            return;
        }
        button(cx - cell, cy, glyph(pad::X, InputAction::UseSlotX), kit.slotItem(0), "X", on(InputAction::UseSlotX));
        button(cx, cy - cell, glyph(pad::Y, InputAction::UseSlotY), kit.slotItem(1), "Y", on(InputAction::UseSlotY));
        button(cx + cell, cy, glyph(pad::B, InputAction::UseSlotB), kit.slotItem(2), "B", on(InputAction::UseSlotB));
        button(cx, cy + cell, glyph(pad::A, InputAction::Jump), 0, "jump", on(InputAction::Jump));
        // the item names of the three slots, beside the diamond
        for (int i = 0; i < 3; i++) {
            uint16_t it = kit.slotItem(i);
            if (!it) continue;
            const char* side[3] = { "X ", "Y ", "B " };
            std::string n = std::string(side[i]) + gItems.get(it).displayName;
            float nx = cx - cell * 1.6f - ui.textWidth(n, 1), ny = cy + cell * 0.7f + i * 14.0f;
            ui.drawRect(nx - 4, ny - 2, ui.textWidth(n, 1) + 8, 13, SHADOW);
            ui.drawText(n, nx, ny, theme.text, 1);
        }
        // keys: the D-pad's jobs have keys of their own, and the
        // quick layer is a held key; say which
        if (!gInput.padLastUsed()) {
            auto k = [](InputAction a) { return InputManager::bindingLabel(gInput.getBinding(a)); };
            std::string n = k(InputAction::CycleSlotX) + " " + k(InputAction::CycleSlotY) + " " + k(InputAction::CycleSlotB) +
                            ": next X Y B   hold " + k(InputAction::QuickLayer) + ": quick";
            float nw = (float)ui.textWidth(n, 1), nx = sw - 24.0f - nw, ny = cy + cell * 1.6f;
            ui.drawRect(nx - 4, ny - 2, nw + 8, 13, SHADOW);
            ui.drawText(n, nx, ny, theme.textDim, 1);
        }
        // the shoulders, above
        uint16_t castItem = 0;
        CombatKit::Special sp = kit.special(wpn.itemID, &castItem);
        std::string lt = sp == CombatKit::Special::Guard ? "guard" : sp == CombatKit::Special::Heavy ? "heavy"
                       : sp == CombatKit::Special::Cast ? gItems.get(castItem).displayName : "-";
        struct Sh { int b; InputAction a; std::string what; };
        // the shoulders: one line each above the diamond, right
        // aligned (key names can be long), LB LT then RB RT
        Sh shoulders[4] = { { pad::LB, InputAction::Roll, "sprint / dodge" }, { pad::LT, InputAction::Block, lt },
                            { pad::RB, InputAction::UseLeft, "light" }, { pad::RT, InputAction::UseRight, "heavy" } };
        float right = sw - 24.0f, ty = cy - cell * 1.6f - 4 * 16.0f;
        for (int i = 0; i < 4; i++, ty += 16.0f) {
            const Sh& q = shoulders[i];
            std::string key = glyph(q.b, q.a), t = key + "  " + q.what;
            float tw = (float)ui.textWidth(t, 1);
            ui.drawRect(right - tw - 4, ty - 2, tw + 8, 13, SHADOW);
            ui.drawText(key, right - tw, ty, theme.accent, 1);
            ui.drawText("  " + q.what, right - tw + ui.textWidth(key, 1), ty, on(q.a) ? WHITE : theme.text, 1);
        }
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
