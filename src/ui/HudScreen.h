#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "UIRenderer.h"
#include "ItemIcon.h"
#include "PadGlyphs.h"
#include "KeyText.h"
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
    // Damage numbers (Settings: Damage numbers), placed by the game
    // each frame where the hits landed (screen pixels)
    struct Popup { float x, y; std::string text; float alpha; int scale; float rgb[3]; };
    std::vector<Popup> popups;

    // The debug overlay (F3): labels placed by the game (screen
    // pixels) and a block of text for the corner
    struct DebugLabel { float x, y; std::string text; glm::vec3 color; };
    std::vector<DebugLabel> debugLabels;
    std::vector<std::string> debugText;
    std::string talkingTo;              // an NPC menu is open
    std::string talkLine;               // … what it just said (game/Dialogue.h)
    std::vector<std::string> talkOptions;   // … what you can ask (D-pad / arrow keys)
    int  talkSel = 0;                   // … the one picked (D-pad / arrows)
    bool talkPad = false;               // … the controller was used last (its hints)

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
            for (size_t i = 0; i < review.size(); i++) pw = std::max(pw, i ? keytext::width(ui, review[i], 2) : (float)ui.textWidth(review[i], 3));
            pw += 48.0f;
            float ph = 30.0f + 34.0f + (review.size() - 1) * 24.0f;
            float px = (sw - pw) * 0.5f, py = sh * 0.30f;
            float bg[4] = {12, 10, 8, 210 * a};
            float title[4] = {240, 210, 140, 255 * a};
            float text[4] = {225, 220, 210, 255 * a};
            ui.drawRect(px, py, pw, ph, bg);
            ui.drawText(review[0], px + (pw - ui.textWidth(review[0], 3)) * 0.5f, py + 14, title, 3);
            for (size_t i = 1; i < review.size(); i++)
                keytext::draw(ui, review[i], px + 24, py + 30 + 34 + (i - 1) * 24.0f - 20, text, 2);
        }

        // ── Talking to an NPC (game/Dialogue.h) ───────────
        // its name, what it said (wrapped), and the questions, 1-4
        if (!talkingTo.empty()) {
            const float bw = std::min(sw - 40.0f, 760.0f), x0 = (sw - bw) * 0.5f;
            std::vector<std::string> lines;
            {   // wrap the line to the box
                std::string cur, word;
                auto flush = [&] {
                    std::string trial = cur.empty() ? word : cur + " " + word;
                    if (!cur.empty() && ui.textWidth(trial, 2) > bw - 40.0f) { lines.push_back(cur); cur = word; }
                    else cur = trial;
                    word.clear();
                };
                for (char ch : talkLine) { if (ch == ' ') { if (!word.empty()) flush(); } else word += ch; }
                if (!word.empty()) flush();
                if (!cur.empty()) lines.push_back(cur);
            }
            const float lineH = 24.0f;
            const float bh = 20 + 30 + lines.size() * lineH + 14 + talkOptions.size() * lineH + 34;
            const float y0 = sh * 0.60f - bh * 0.5f;
            float bg[4] = {12, 10, 8, 205};
            float edge[4] = {120, 90, 50, 255};
            float name[4] = {235, 190, 110, 255};
            float say[4] = {240, 232, 210, 255};
            float ask[4] = {200, 200, 190, 255};
            float hint[4] = {150, 150, 140, 255};
            ui.drawRect(x0 - 2, y0 - 2, bw + 4, bh + 4, edge);
            ui.drawRect(x0, y0, bw, bh, bg);
            float y = y0 + 16;
            ui.drawText(talkingTo, x0 + 20, y, name, 3);
            y += 34;
            for (const std::string& l : lines) { ui.drawText(l, x0 + 20, y, say, 2); y += lineH; }
            y += 14;
            float pick[4] = {255, 215, 120, 255};
            float pickBg[4] = {120, 90, 50, 120};
            for (size_t i = 0; i < talkOptions.size(); i++) {
                bool on = (int)i == talkSel;
                if (on) ui.drawRect(x0 + 20, y - 4, bw - 40, lineH, pickBg);
                std::string lead = on ? ">" : " ";
                ui.drawText(lead + "   " + talkOptions[i], x0 + 32, y, on ? pick : ask, 2);
                y += lineH;
            }
            std::string t = talkPad ? "D-pad: choose   A: ask   B: leave"
                                    : "arrow keys: choose   Enter: ask   {interact}: leave";
            if (talkOptions.empty()) t = talkPad ? "B: leave" : "{interact}: leave";
            keytext::draw(ui, t, x0 + bw - 20 - keytext::width(ui, t, 2), y + 6, hint, 2);
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

        // ── Damage numbers ────────────────────────────────
        for (const Popup& p : popups) {
            float w = (float)ui.textWidth(p.text, p.scale);
            float sh[4] = {0, 0, 0, 200 * p.alpha};
            float c[4] = {p.rgb[0], p.rgb[1], p.rgb[2], 255 * p.alpha};
            ui.drawText(p.text, p.x - w * 0.5f + 1, p.y + 1, sh, p.scale);
            ui.drawText(p.text, p.x - w * 0.5f, p.y, c, p.scale);
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
            if (player.mode == GameMode::Survival && gInput.isHeld(InputAction::QuickLayer)) drawQuickPanel(ui, player, sw, sh, true);   // the quick options work out of combat too
            else if (gSettings.controlHints && !gInput.inMenu()) drawControlHints(ui, player, sw, sh);   // (a menu has its own)
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
                    keytext::drawCentred(ui, DevTools::toolHint(d.devTool), sw * 0.5f, top - 40.0f, theme.textDim, 1, theme.accent);
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
            float w = keytext::width(ui, hands.aimLabel, 2);
            ui.drawRect((sw - w) * 0.5f - 6, sh * 0.5f + 22, w + 12, 21, SHADOW);
            keytext::draw(ui, hands.aimLabel, (sw - w) * 0.5f, sh * 0.5f + 25, theme.textHover, 2, theme.accent);
        }

        // ── Tree health while chopping ────────────────────
        if (hands.treeAge < 2.5f && hands.treeMax > 0.0f) {
            float w = 200.0f, x = (sw - w) * 0.5f, y = sh * 0.5f - 60.0f;
            ui.drawText(hands.treeName, x, y - 18, theme.textHover, 2);
            ui.drawRect(x, y, w, 12, SHADOW);
            float green[4] = {90, 200, 90, 255};
            ui.drawRect(x + 2, y + 2, (w - 4) * (hands.treeHealth / hands.treeMax), 8, green);
        }

        // ── The hammer taking a piece down (right button held) ─
        if (float dp = hands.dismantleProgress(); dp >= 0.0f) {
            float w = 200.0f, x = (sw - w) * 0.5f, y = sh * 0.5f - 60.0f;
            ui.drawText("Taking it down", x, y - 18, theme.textHover, 2);
            ui.drawRect(x, y, w, 12, SHADOW);
            ui.drawRect(x + 2, y + 2, (w - 4) * dp, 8, theme.accent);
        }

        // ── Builder's Hammer: what it will build ──────────
        if (hands.hammerActive && build) {
            const StructureType* t = build->type();
            std::string line1 = t ? t->displayName + "   " + t->feet()
                                  : std::string("Builder's Hammer");
            std::string line2 = t ? (player.mode == GameMode::Survival
                                     ? "cost: " + t->costText(gStructures.costsEnabled) : std::string("free in Test Play"))
                                  : std::string("{use_right}: choose a structure");
            std::string line3 = hands.previewWhy.empty()
                ? (build->locked ? "LOCKED - {use_left} builds   {tool_cancel}: let go" : "{use_left} builds")
                : hands.previewWhy;
            if (build->mode == HammerMode::Copy) {
                line1 = "Copy a building";
                line2 = "{use_left} any piece of it to save the whole building";
                line3 = hands.previewWhy.empty() ? hands.aimLabel : hands.previewWhy;
            } else if (build->mode == HammerMode::Blueprint) {
                const Blueprint* bp = build->chosenBlueprint();
                line1 = bp ? bp->display + "   " + std::to_string(bp->size.x / 3) + " x " + std::to_string(bp->size.z / 3) + " ft"
                           : std::string("Blueprint");
                line2 = bp ? std::to_string(bp->parts.size()) + " pieces   {rotate_piece} turns it" + (build->style.sitePrep ? "   levels the ground" : "")
                           : std::string("{use_right}: choose a blueprint");
                if (hands.previewWhy.empty()) line3 = build->locked ? "LOCKED - {use_left} places it   {tool_cancel}: let go" : "{use_left} places it";
            }
            float y = sh - 196.0f;     // above the held item's name
            const std::string* lines[3] = { &line1, &line2, &line3 };
            for (int i = 0; i < 3; i++) {
                float w = keytext::width(ui, *lines[i], 2);
                ui.drawRect((sw - w) * 0.5f - 6, y - 3, w + 12, 21, SHADOW);
                const float* col = i == 2 ? (hands.previewWhy.empty() ? theme.accent : COMBAT_RED)
                                          : (i == 0 ? theme.textHover : theme.textDim);
                keytext::draw(ui, *lines[i], (sw - w) * 0.5f, y, col, 2, theme.accent);
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
            float w = keytext::width(ui, hands.notice, 2);
            float col[4] = { COMBAT_RED[0], COMBAT_RED[1], COMBAT_RED[2], COMBAT_RED[3] * popupAlpha(hands.noticeAge, NOTICE_TIME) };
            keytext::draw(ui, hands.notice, (sw - w) * 0.5f, sh * 0.5f + 40.0f, col, 2);
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
        if (gInput.isHeld(InputAction::QuickLayer)) layer = pad::Layer::Quick;   // its key stands in for Start
        const auto& held = gInput.heldStates();
        float x0 = 20.0f, y0 = sh - 330.0f, W = 300.0f, H = 160.0f;
        ui.drawRect(x0, y0, W, H, SHADOW);
        ui.drawBorder(x0, y0, W, H, theme.border, 1.0f);
        const char* ln = layer == pad::Layer::Combat ? "combat" : layer == pad::Layer::Quick ? "quick (Start held)" : layer == pad::Layer::Tool ? "tool" : layer == pad::Layer::ToolAlt ? "tool, Y held" : "explore";
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

    // ── Out of combat: what the buttons do now ─────────────
    // Bottom right, one line each: the button (its picture with the
    // pad, else the key) and what it does with what's in hand. The
    // held tool changes the list (input/PadLayout.h: the hammer and
    // some dev tools take B, Y and the D-pad). Settings → Control
    // hints turns it off.
    struct Hint { InputAction key; int pad1; int pad2; std::string what; std::string keyText = std::string(); };   // keyText: the keys, said in words
    std::vector<Hint> controlHints(const Player& player) const {
        const bool pad = gInput.padLastUsed();
        const pad::Layer l = gInput.layerNow();
        auto at = [&](InputAction a) { return padglyph::buttonFor(a, l); };
        std::vector<Hint> h;
        auto add = [&](InputAction a, const std::string& what) { h.push_back({ a, at(a), -1, what }); };
        const ItemDef& d = player.activeInventory().heldDef();
        const bool tool = PlayerInteraction::takesPad(player);
        const bool devOk = d.is(IC_DEV) && player.mode != GameMode::Survival;
        // what the triggers / mouse buttons do with this in hand
        std::string left = "use / place", right = "break";
        if (d.is(IC_TOOL) && d.tool == ToolKind::Hammer)   { left = "build"; right = "tap: choose a piece, hold: take one down"; }
        else if (d.is(IC_TOOL) && d.tool == ToolKind::Hoe)   { left = "shape the ground"; right = ""; }
        else if (d.is(IC_TOOL) && d.tool == ToolKind::Blast) { left = "blast"; right = "big blast"; }
        else if (devOk) {
            switch (d.devTool) {
            case DevToolKind::Wand:    left = "corner 1"; right = "corner 2"; break;
            case DevToolKind::Fill:    left = "fill"; right = "clear"; break;
            case DevToolKind::Replace: left = "replace"; right = ""; break;
            case DevToolKind::Brush:   left = "paint"; right = "carve"; break;
            case DevToolKind::Copy:    left = "copy"; right = ""; break;
            case DevToolKind::Paste:   left = "paste"; right = "turn it"; break;
            case DevToolKind::Explode: left = "explode"; right = ""; break;
            case DevToolKind::Breaker: left = "break one"; right = "break 3 x 3"; break;
            case DevToolKind::Cube:    left = "place a cube"; right = "break a cube"; break;
            default: break;
            }
        }
        add(InputAction::UseLeft, left);
        if (!right.empty()) add(InputAction::UseRight, right);
        if (d.is(IC_TOOL) && d.tool == ToolKind::Hammer) add(InputAction::RotatePiece, "turn the piece");
        if (d.is(IC_TOOL) && d.tool == ToolKind::Hoe)    add(InputAction::RotatePiece, "change what it does");
        if (tool) {
            bool size = devOk && (d.devTool == DevToolKind::Brush || d.devTool == DevToolKind::Explode || d.devTool == DevToolKind::Cube);
            if (pad) h.push_back({ InputAction::NudgeForward, pad::Up, pad::Down, size ? "bigger / smaller" : "move it, locks it there" });
            else     h.push_back({ InputAction::NudgeForward, -1, -1, size ? "bigger / smaller" : "move it, Page Up / Down: up / down", "ARROWS" });
            if (!size) {
                if (pad) h.push_back({ InputAction::ToolAlt, pad::toolAltButton(), -1, "hold: D-pad up / down is up / down" });
                add(InputAction::ToolCancel, devOk ? "let go / cancel" : "let go, follow the aim");
            }
        }
        if (pad) h.push_back({ InputAction::ToolbarNext, pad::LB, pad::RB, "tool" });
        else     h.push_back({ InputAction::ToolbarNext, -1, -1, "tool", "WHEEL, 1 - 0" });
        add(InputAction::Jump, "jump");
        if (!(tool && pad)) add(InputAction::Sprint, "sprint / roll");
        if (pad) h.push_back({ InputAction::Interact, pad::quickButton(), padglyph::buttonFor(InputAction::Interact, pad::Layer::Quick), "interact" });
        else     add(InputAction::Interact, "interact");
        add(InputAction::DrawWeapon, "combat");
        if (pad) h.push_back({ InputAction::Inventory, pad::drawButton(), pad::quickButton(), "inventory, menu" });
        else     add(InputAction::Inventory, "inventory");
        if (player.mode == GameMode::Survival) {
            if (pad) h.push_back({ InputAction::QuickLayer, pad::quickButton(), -1, "hold: quick spells" });
            else     add(InputAction::QuickLayer, "hold: quick spells");
        }
        return h;
    }
    void drawControlHints(UIRenderer& ui, const Player& player, float sw, float sh) {
        const auto& theme = gResourcePacks.getTheme();
        const bool pad = gInput.padLastUsed();
        std::vector<Hint> hints = controlHints(player);
        const float lineH = pad ? 25.0f : 15.0f, gh = 14.0f;     // a glyph is gh * 1.5 square
        float right = sw - 16.0f, y = sh - 120.0f - lineH * hints.size();
        for (const Hint& h : hints) {
            // width: the button(s) and the words
            std::string what = "  " + h.what;
            float bw;
            if (pad) bw = gh * 1.5f * (h.pad2 >= 0 ? 2.0f : 1.0f) + (h.pad2 >= 0 ? 6.0f : 0.0f);
            else bw = (float)ui.textWidth(h.keyText.empty() ? InputManager::bindingLabel(gInput.getBinding(h.key)) : h.keyText, 1);
            float tw = bw + (float)ui.textWidth(what, 1), x = right - tw;
            if (pad) ui.drawRect(x - 4, y - 7, tw + 8, 22, SHADOW);   // round the button pictures
            else     ui.drawRect(x - 4, y - 2, tw + 8, 13, SHADOW);
            if (pad) {
                float gx = x;
                if (h.pad1 >= 0) gx += padglyph::keyOrPad(ui, h.pad1, h.key, gx, y, gh, theme.accent);
                if (h.pad2 >= 0) { gx += 6.0f; padglyph::keyOrPad(ui, h.pad2, h.key, gx, y, gh, theme.accent); }
            } else if (!h.keyText.empty()) ui.drawText(h.keyText, x, y, theme.accent, 1);
            else padglyph::keyOrPad(ui, -1, h.key, x, y, gh, theme.accent);
            ui.drawText(what, x + bw, y, theme.text, 1);
            y += lineH;
        }
    }

    // ── Combat: the controller layout (game/CombatKit.h) ─
    // Bottom centre: the weapon set. Bottom right: the face
    // buttons (X / Y / B items; at the bottom the weapon in hand, its
    // button the next weapon set) and the shoulders (LB, LT,
    // RB, RT) with what each does now. Start held: the 8 quick
    // options. Button names follow what was used last: the pad's
    // (A, LB …) or the keys bound to the same actions.
    // (ui/PadGlyphs.h: with the pad, its button's picture, from the
    // layout in use; with the keys, the key's name)
    static int padFor(InputAction a, pad::Layer l = pad::Layer::Combat) { return padglyph::buttonFor(a, l); }
    static constexpr float KIT_CELL = 58.0f;
    // One cell of the button diamond: its item (or a word), and in
    // its corner the button's picture (pad) or the key's name
    void kitCell(UIRenderer& ui, float bx, float by, int padButton, InputAction keyAction, uint16_t item,
                 const std::string& label, bool lit) {
        const auto& theme = gResourcePacks.getTheme();
        const float cell = KIT_CELL;
        float x0 = bx - cell * 0.5f, y0 = by - cell * 0.5f;
        ui.drawRect(x0, y0, cell, cell, lit ? theme.bgHover : SHADOW);
        ui.drawBorder(x0, y0, cell, cell, lit ? WHITE : theme.border, lit ? 2.0f : 1.0f);
        if (item) drawItemIcon(ui, gItems.get(item), x0 + 9, y0 + 9, cell - 18);
        else {
            float lw = (float)ui.textWidth(label, 1);
            ui.drawText(label, bx - lw * 0.5f, by - 4, theme.textDim, 1);
        }
        if (gInput.padLastUsed() && padButton >= 0 && padglyph::draw(ui, padButton, x0, y0, 24.0f)) return;
        std::string key = gInput.padLastUsed() ? std::string(padButton >= 0 ? pad::buttonName(padButton) : "?")
                                               : InputManager::bindingLabel(gInput.getBinding(keyAction));
        float kw = (float)ui.textWidth(key, 1);
        ui.drawRect(x0 + 2, y0 + 2, kw + 6, 12, SHADOW);
        ui.drawText(key, x0 + 5, y0 + 4, theme.accent, 1);
    }
    // Start held (Left Alt), in combat or out: the 8 quick options,
    // bottom right — D-pad on the left, face buttons on the right
    // (out of combat it sits higher, clear of the hotbar: 'raised')
    void drawQuickPanel(UIRenderer& ui, const Player& player, float sw, float sh, bool raised = false) {
        const auto& theme = gResourcePacks.getTheme();
        const CombatKit& kit = player.kit;
        const float cell = KIT_CELL;
        float cx = sw - cell * 2.0f - 30.0f, cy = raised ? sh * 0.42f : sh - cell * 2.0f - 40.0f;
        const auto& held = gInput.heldStates();
        const int pb[8] = { pad::Left, pad::Up, pad::Right, pad::Down, pad::X, pad::Y, pad::B, pad::A };
        const glm::vec2 at[8] = { {-1, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 0}, {0, 1} };
        for (int i = 0; i < CombatKit::QUICK; i++) {
            float ox = i < 4 ? -cell * 3.4f : 0.0f;
            InputAction a = (InputAction)((int)InputAction::ToolbarSlot1 + i);
            int b = pad::buttonOf(a, pad::Layer::Quick);
            kitCell(ui, cx + ox + at[i].x * cell, cy + at[i].y * cell, b >= 0 ? b : pb[i], a, kit.quick[i], "empty", held[(int)a]);
        }
        std::string t = "QUICK";
        ui.drawText(t, cx - cell * 1.7f - ui.textWidth(t, 2) * 0.5f, cy - cell * 2.0f, theme.title, 2);
        // the way to the pause menu, highlighted at the top (with the
        // pad: add Select to the Start already held)
        {
            std::string m = gInput.padLastUsed() ? "{inventory}: pause menu" : "{pause}: pause menu";
            float mw = keytext::width(ui, m, 1), mx = cx - cell * 1.7f - mw * 0.5f, my = cy - cell * 2.0f - 30.0f;
            static const float BACK[4] = { 20, 16, 12, 235 };
            ui.drawRect(mx - 8, my - 8, mw + 16, 24, BACK);
            ui.drawBorder(mx - 8, my - 8, mw + 16, 24, theme.accent, 2.0f);
            keytext::draw(ui, m, mx, my, theme.accent, 1);
        }
        // and interact (LB here; E on the keyboard)
        {
            std::string w = "  interact";
            int b = padglyph::buttonFor(InputAction::Interact, pad::Layer::Quick);
            float kw = padglyph::keyOrPadWidth(ui, InputAction::Interact, 16.0f);
            float tw = kw + (float)ui.textWidth(w, 1), tx = cx - cell * 1.7f - tw * 0.5f, ty = cy + cell * 1.75f;
            ui.drawRect(tx - 4, ty - 2, tw + 8, 13, SHADOW);
            padglyph::keyOrPad(ui, b, InputAction::Interact, tx, ty, 16.0f, theme.accent);
            ui.drawText(w, tx + kw, ty, held[(int)InputAction::Interact] ? WHITE : theme.text, 1);
        }
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
            std::string a = "loadout " + std::to_string(kit.setAt + 1) + "   ", b = ": the other";
            float kw = padglyph::keyOrPadWidth(ui, InputAction::SwapWeapon, 14.0f);
            float nw = (float)ui.textWidth(a, 1) + kw + (float)ui.textWidth(b, 1);
            float nx = (sw - nw) * 0.5f;
            ui.drawRect(nx - 4, y - 16.0f, nw + 8, 13, SHADOW);
            ui.drawText(a, nx, y - 14.0f, theme.text, 1);
            nx += (float)ui.textWidth(a, 1);
            nx += padglyph::keyOrPad(ui, padFor(InputAction::SwapWeapon), InputAction::SwapWeapon, nx, y - 14.0f, 14.0f, theme.accent);
            ui.drawText(b, nx, y - 14.0f, theme.text, 1);
        }

        // the buttons, bottom right
        const float cell = KIT_CELL;
        float cx = sw - cell * 2.0f - 30.0f, cy = sh - cell * 2.0f - 40.0f;     // centre of the diamond
        auto button = [&](float bx, float by, int padButton, InputAction keyAction, uint16_t item, const std::string& label, bool lit) {
            kitCell(ui, bx, by, padButton, keyAction, item, label, lit);
        };
        const auto& held = gInput.heldStates();
        auto on = [&](InputAction a) { return held[(int)a]; };
        if (gInput.isHeld(InputAction::QuickLayer)) { drawQuickPanel(ui, player, sw, sh); return; }
        button(cx - cell, cy, padFor(InputAction::UseSlotX), InputAction::UseSlotX, kit.slotItem(0), "X", on(InputAction::UseSlotX));
        button(cx, cy - cell, padFor(InputAction::UseSlotY), InputAction::UseSlotY, kit.slotItem(1), "Y", on(InputAction::UseSlotY));
        button(cx + cell, cy, padFor(InputAction::UseSlotB), InputAction::UseSlotB, kit.slotItem(2), "B", on(InputAction::UseSlotB));
        // the bottom: the weapon in hand; its button (D-pad down / X)
        // switches to the other loadout's
        button(cx, cy + cell, padFor(InputAction::SwapWeapon), InputAction::SwapWeapon, wpn.itemID, "unarmed",
               on(InputAction::SwapWeapon));
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
        Sh shoulders[4] = { { padFor(InputAction::Roll), InputAction::Roll, "sprint / dodge" }, { padFor(InputAction::Block), InputAction::Block, lt },
                            { padFor(InputAction::UseLeft), InputAction::UseLeft, "light" }, { padFor(InputAction::UseRight), InputAction::UseRight, "heavy" } };
        const float lineH = gInput.padLastUsed() ? 27.0f : 16.0f;
        float right = sw - 24.0f, ty = cy - cell * 1.6f - 4 * lineH;
        for (int i = 0; i < 4; i++, ty += lineH) {
            const Sh& q = shoulders[i];
            std::string what = "  " + q.what;
            float kw = padglyph::keyOrPadWidth(ui, q.a, 16.0f);
            float tw = kw + (float)ui.textWidth(what, 1);
            ui.drawRect(right - tw - 4, ty - 2, tw + 8, 13, SHADOW);
            padglyph::keyOrPad(ui, q.b, q.a, right - tw, ty, 16.0f, theme.accent);
            ui.drawText(what, right - tw + kw, ty, on(q.a) ? WHITE : theme.text, 1);
        }
    }

    // Live state of the held dev tool
    void drawToolStatus(UIRenderer& ui, DevToolKind tool, const DevTools& dev,
                        float sw, float sh, float y) {
        const auto& theme = gResourcePacks.getTheme();
        auto centred = [&](const std::string& t, float yy, const float* col) {
            float w = keytext::width(ui, t, 2);
            ui.drawRect((sw - w) * 0.5f - 6, yy - 3, w + 12, 21, SHADOW);
            keytext::draw(ui, t, (sw - w) * 0.5f, yy, col, 2, theme.accent);
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
                centred("{toolbar_slot_1} or {toolbar_slot_2}   {tool_cancel} cancels", py + 48, theme.textDim);
            }
            break;
        case DevToolKind::Paste:
            if (dev.pasteLocked) {
                std::snprintf(b, sizeof(b), "LOCKED at %d %d %d", dev.pasteSpot.x,
                              dev.pasteSpot.y, dev.pasteSpot.z);
                centred(b, y, theme.accent);
            } else {
                centred("follows crosshair - {nudge} locks it", y, theme.textDim);
            }
            break;
        case DevToolKind::Explode:
            std::snprintf(b, sizeof(b), "blast radius %d   {use_right}: %d",
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
