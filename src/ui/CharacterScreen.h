#pragma once
#include "UIScreen.h"
#include "../character/Appearance.h"
#include "../character/AnimPreview.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Character creator
// ══════════════════════════════════════════════════════════
// Esc -> Character. Tabs of settings on the left, a turning
// preview on the right (drag it to turn it yourself). Every
// change shows at once; Done keeps it, Cancel (or Esc) puts
// the old look back.
//
// Favorites: looks saved under a name, shared by every world
// (saves/character_favorites.txt).
//
// Animations: a viewer for every animation the body can play —
// the game's own states (as the game shows them) and every clip
// in data/animations by category — with play / pause, speed,
// loop and a time bar. main reads animPreview() to drive the
// preview body.
//
// Colours and spacing come from the resource pack's UI theme;
// swatches from data/characters/palettes.toml (a pack can
// replace it with its own characters/palettes.toml).
//
// The preview itself is 3D, drawn by main after this screen
// (CharacterRenderer::drawPreview) into previewRect().
class CharacterScreen : public UIScreen {
public:
    explicit CharacterScreen(GLFWwindow* win) : window(win) {}

    FavoriteLooks favorites;

    // Start editing a look
    void begin(const Appearance& current) {
        original = edit = current;
        accepted = false;
        dragging = -1;
        mouseDown = mousePrev = true;     // ignore the click that opened us
        turn = 20.0f;
        spinning = false;
        nameFocus = false;
        confirmDelete = -1;
        status.clear();
        favorites.load();
    }

    Appearance&       look()       { return edit; }
    const Appearance& look() const { return edit; }
    bool  wasAccepted() const { return accepted; }
    float previewTurn() const { return turn; }
    float previewFocus() const { return focus; }     // 0 whole body … 1 head
    // True if Esc only left the name box (once per press)
    bool  escLeftTyping() { bool u = escUsed || nameFocus; escUsed = false; return u; }

    struct Rect { int x = 0, y = 0, w = 0, h = 0; };

    // What the Animations tab wants the preview to play
    struct AnimView {
        bool  active = false;      // the tab is open
        int   state = -1;          // a game state (animpreview::states()), or
        int   clip = -1;           // a clip (gAnims)
        float time = 0.0f;         // s
    };
    AnimView animPreview() const {
        AnimView v;
        if (tab != 5) return v;
        v.active = true;
        if (animCat == 0) v.state = animSel; else v.clip = animSel;
        v.time = animTime;
        return v;
    }
    Rect previewRect() const { return preview; }

    // Text for the favorite's name (GLFW callbacks via main)
    void onChar(unsigned int c) {
        if (!nameFocus || c < 32 || c > 126 || c == '[' || c == ']') return;
        if (favName.size() < 24) favName += (char)c;
    }
    void onKey(int key) {
        if (!nameFocus) return;
        if (key == GLFW_KEY_BACKSPACE && !favName.empty()) favName.pop_back();
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) saveFavorite();
        if (key == GLFW_KEY_ESCAPE) { nameFocus = false; escUsed = true; }
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        mousePrev = mouseDown;
        double pmx = mx;
        glfwGetCursorPos(window, &mx, &my);
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        caret += dt;

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);

        // ── Layout ────────────────────────────────────────
        float pw = std::min(620.0f, sw * 0.52f), ph = std::min(720.0f, sh - 30.0f);
        float px = 20.0f, py = std::max(15.0f, (sh - ph) * 0.5f);
        drawPanel(ui, px, py, pw, ph);
        ui.drawText("Character", px + 16, py + 12, theme.title, 2);

        // Tabs
        static const char* TABS[] = { "Body", "Face", "Hair", "Outfit", "Favorites", "Animations" };
        {
            const int NT = 6;
            float tx = px + 16, tw = (pw - 32 - 4 * (NT - 1)) / (float)NT, ty = py + 40, th = 30;
            for (int i = 0; i < NT; i++) {
                float bx = tx + i * (tw + 4);
                bool sel = tab == i, hov = hovered(bx, ty, tw, th);
                ui.drawRect(bx, ty, tw, th, sel ? theme.buttonBgHover : (hov ? theme.bgHover : theme.buttonBg));
                ui.drawBorder(bx, ty, tw, th, sel ? theme.accent : theme.buttonBorder, sel ? 2.0f : 1.0f);
                int sc = ui.textWidth(TABS[i], 2) <= tw - 6 ? 2 : 1;
                ui.drawTextCentered(TABS[i], bx, ty, tw, th, sel || hov ? theme.textHover : theme.text, sc);
                if (hov && clicked() && tab != i) { tab = i; nameFocus = false; confirmDelete = -1; if (i == 5) turn = 20.0f; }
            }
            ui.drawRect(px + 16, ty + th + 6, pw - 32, 1, theme.border);
        }

        labelW = 140.0f;
        cx = px + 16 + labelW; cw = pw - 32 - labelW;
        y = py + 86;
        left = px + 16;

        switch (tab) {
        case 0: bodyTab(ui);      break;
        case 1: faceTab(ui);      break;
        case 2: hairTab(ui);      break;
        case 3: outfitTab(ui);    break;
        case 4: favoritesTab(ui, px, pw, py + ph - (float)theme.buttonH - 24); break;
        case 5: animationsTab(ui, px, pw, py + ph - (float)theme.buttonH - 24, dt); break;
        }

        // Buttons
        float bh = (float)theme.buttonH, by = py + ph - bh - 12;
        float bw = (pw - 32 - 20) / 3.0f;
        ScreenResult result = ScreenResult::Continue;
        if (button(ui, "Random", px + 16, by, bw, bh)) {
            uint32_t seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
            edit = Appearance::random(seed);
        }
        if (button(ui, "Cancel", px + 16 + bw + 10, by, bw, bh)) {
            edit = original;
            result = ScreenResult::CloseMenu;
        }
        if (button(ui, "Done", px + 16 + (bw + 10) * 2, by, bw, bh)) {
            accepted = true;
            result = ScreenResult::CloseMenu;
        }

        // ── Preview area (the body is drawn into it after) ─
        float qx = px + pw + 20, qy = py, qw = sw - qx - 20, qh = ph;
        if (qw > 100) {
            ui.drawRect(qx, qy, qw, qh, theme.bg);
            ui.drawBorder(qx, qy, qw, qh, theme.border, 1.0f);
            const char* hint = "drag to turn";
            ui.drawText(hint, qx + (qw - ui.textWidth(hint, 1)) * 0.5f, qy + qh - 18, theme.textDim, 1);
            preview = { (int)qx + 1, (int)qy + 1, (int)qw - 2, (int)qh - 24 };

            bool over = mx >= qx && mx <= qx + qw && my >= qy && my <= qy + qh;
            if (over && clicked()) spinning = true;
            if (!mouseDown) spinning = false;
            if (spinning) turn += (float)(mx - pmx) * 0.6f;
            else if (dragging < 0 && tab != 1 && tab != 5) turn += dt * 20.0f;   // slow turntable (not while watching animations)
            else if (tab == 1 && !spinning) {                          // face tab: ease to the front
                float d = std::remainder(0.0f - turn, 360.0f);
                turn += d * (1.0f - std::exp(-dt * 4.0f));
            }
        } else {
            preview = {};
        }
        // Face and Hair tabs move the camera in on the head
        float want = tab == 1 ? 1.0f : tab == 2 ? (edit.extra == 3 ? 0.55f : 0.8f) : 0.0f;
        focus += (want - focus) * (1.0f - std::exp(-dt * 6.0f));

        if (!mouseDown) dragging = -1;
        edit.clamp();
        ui.end();
        return result;
    }

    // Esc from main: same as Cancel
    void cancel() { edit = original; accepted = false; nameFocus = false; }

private:
    GLFWwindow* window;
    Appearance  edit, original;
    bool   accepted = false;
    int    tab = 0;
    int    dragging = -1;
    bool   spinning = false;
    float  turn = 20.0f, focus = 0.0f, caret = 0.0f;
    double mx = 0, my = 0;
    bool   mouseDown = false, mousePrev = false;
    Rect   preview;

    // animations tab
    int    animCat = 0, animSel = 0, animPage = 0, animSpeed = 3;
    float  animTime = 0.0f;
    bool   animPlaying = true, animLoop = true;

    // favorites tab
    std::string favName, status;
    bool   nameFocus = false, escUsed = false;
    int    confirmDelete = -1, favPage = 0;

    // row layout, set each frame
    float labelW = 140, cx = 0, cw = 0, y = 0, left = 0;

    bool clicked() const { return mouseDown && !mousePrev; }
    bool hovered(float x, float yy, float w, float h) const {
        return mx >= x && mx <= x + w && my >= yy && my <= yy + h;
    }

    // ── Tabs ──────────────────────────────────────────────
    void bodyTab(UIRenderer& ui) {
        label(ui, "Build");
        pickGrid(ui, look::BUILDS, look::count(look::BUILDS), edit.build, 3);
        label(ui, "Height");
        int h = edit.heightVoxels - Appearance::MIN_HEIGHT;
        pickGrid(ui, look::HEIGHTS, look::count(look::HEIGHTS), h, 3);
        edit.heightVoxels = Appearance::MIN_HEIGHT + h;
        label(ui, "Face shape");
        pickGrid(ui, look::FACES, look::count(look::FACES), edit.face, 5);
        struct S { const char* name; float* v; float lo; };
        S rows[] = { { "Shoulders", &edit.shoulders, -1 }, { "Chest", &edit.chest, 0 },
                     { "Hips", &edit.hips, -1 }, { "Limbs", &edit.limbs, -1 }, { "Head", &edit.head, -1 } };
        int id = 0;
        for (S& r : rows) { label(ui, r.name); slider(ui, id++, r.v, r.lo, 1.0f); }
        label(ui, "Skin");
        swatches(ui, gPalettes.skin, edit.skin);
    }

    void faceTab(UIRenderer& ui) {
        label(ui, "Eyes");
        pickGrid(ui, look::EYES, look::count(look::EYES), edit.eyeStyle, 3);
        label(ui, "Expression");
        pickGrid(ui, look::EXPRESSIONS, look::count(look::EXPRESSIONS), edit.expression, 3);
        label(ui, "Eye colour");
        swatches(ui, gPalettes.eyes, edit.eyes);
        // Where the eyes sit on the face (the brows move with them)
        label(ui, "Eye height");
        {
            float v = (float)-edit.eyeHeight;          // right = higher
            slider(ui, 90, &v, -(float)Appearance::EYE_SHIFT, (float)Appearance::EYE_SHIFT);
            edit.eyeHeight = -(int)std::lround(v);
        }
    }

    void hairTab(UIRenderer& ui) {
        label(ui, "Style");
        pickGrid(ui, look::HAIRSTYLES, look::count(look::HAIRSTYLES), edit.hairStyle, 3);
        label(ui, "Colour");
        swatches(ui, gPalettes.hair, edit.hair);
        label(ui, "Extra");
        pickGrid(ui, look::EXTRAS, look::count(look::EXTRAS), edit.extra, 3);
    }

    void outfitTab(UIRenderer& ui) {
        label(ui, "Top");
        pickGrid(ui, look::TOPS, look::count(look::TOPS), edit.top, 3);
        label(ui, "Top colour");
        swatches(ui, gPalettes.cloth, edit.topColor);
        label(ui, "Bottom");
        pickGrid(ui, look::BOTTOMS, look::count(look::BOTTOMS), edit.bottom, 3);
        label(ui, "Bottom colour");
        swatches(ui, gPalettes.cloth, edit.bottomColor);
        label(ui, "Shoes");
        pickGrid(ui, look::SHOES, look::count(look::SHOES), edit.shoes, 3);
    }

    void favoritesTab(UIRenderer& ui, float px, float pw, float bottom) {
        const auto& theme = gResourcePacks.getTheme();
        float bh = (float)theme.buttonH;
        float x = px + 16, w = pw - 32;

        // name + save
        ui.drawText("Save this look as", x, y, theme.text, 2);
        y += 24;
        float inW = w - 110;
        if (clicked()) nameFocus = hovered(x, y, inW, bh);
        drawInput(ui, favName, "name", x, y, inW, nameFocus, std::fmod(caret, 1.0f) < 0.55f);
        if (button(ui, "Save", x + inW + 10, y, 100, bh)) saveFavorite();
        y += bh + 8;
        if (!status.empty()) ui.drawText(status, x, y, theme.accent, 1);
        y += 18;
        ui.drawRect(x, y, w, 1, theme.border);
        y += 10;

        if (favorites.list.empty()) {
            ui.drawText("No favorites yet.", x, y + 4, theme.textDim, 2);
            return;
        }
        // list, a page at a time
        float rowH = 40;
        int perPage = std::max(1, (int)((bottom - y - 40) / rowH));
        int pages = ((int)favorites.list.size() + perPage - 1) / perPage;
        favPage = std::clamp(favPage, 0, pages - 1);
        int first = favPage * perPage;
        for (int i = first; i < std::min((int)favorites.list.size(), first + perPage); i++) {
            const auto& [name, a] = favorites.list[i];
            bool cur = a == edit;
            ui.drawRect(x, y, w, rowH - 4, cur ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, w, rowH - 4, cur ? theme.accent : theme.border, 1.0f);
            // colour chips: skin, hair, top, bottom
            glm::u8vec3 chips[4] = { a.skin, a.hair, a.topColor, a.bottomColor };
            for (int k = 0; k < 4; k++) {
                float c[4] = { (float)chips[k].r, (float)chips[k].g, (float)chips[k].b, 255 };
                ui.drawRect(x + 8 + k * 14, y + 11, 12, 14, c);
            }
            float nx = x + 72, avail = w - 72 - 200;
            std::string shown = name;
            while (!shown.empty() && ui.textWidth(shown, 2) > avail) shown.pop_back();
            ui.drawText(shown, nx, y + (rowH - 4 - ui.textHeight(2)) * 0.5f, theme.text, 2);
            float bx = x + w - 196;
            if (button(ui, "Load", bx, y + 3, 90, rowH - 10)) {
                edit = a; favName = name; status = "Loaded " + name; confirmDelete = -1;
            }
            const char* del = confirmDelete == i ? "Sure?" : "Delete";
            if (button(ui, del, bx + 98, y + 3, 90, rowH - 10)) {
                if (confirmDelete == i) {
                    status = "Deleted " + name;
                    favorites.remove((size_t)i);
                    confirmDelete = -1;
                    return;
                }
                confirmDelete = i;
            }
            y += rowH;
        }
        if (pages > 1) {
            char b[32]; std::snprintf(b, sizeof(b), "%d / %d", favPage + 1, pages);
            float yy = bottom - 34;
            if (button(ui, "<", x, yy, 40, 28)) favPage--;
            ui.drawTextCentered(b, x + 44, yy, 100, 28, theme.textDim, 2);
            if (button(ui, ">", x + 148, yy, 40, 28)) favPage++;
        }
    }

    void saveFavorite() {
        std::string n = favName;
        while (!n.empty() && n.back() == ' ') n.pop_back();
        while (!n.empty() && n.front() == ' ') n.erase(n.begin());
        if (n.empty()) { status = "Type a name first"; nameFocus = true; return; }
        bool replaced = false;
        for (auto& e : favorites.list) if (e.first == n) replaced = true;
        favorites.put(n, edit);
        status = (replaced ? "Updated " : "Saved ") + n;
        favName.clear();
        nameFocus = false;
    }

    // ── Animations ────────────────────────────────────────
    // Categories: "Game" (the game's states) then each clip
    // category; a page of entries; the transport underneath
    std::vector<std::string> animCategories() const {
        std::vector<std::string> c = { "Game" };
        for (int i = 0; i < gAnims.count(); i++) {
            const std::string& k = gAnims.info(i).category;
            if (std::find(c.begin(), c.end(), k) == c.end()) c.push_back(k);
        }
        std::sort(c.begin() + 1, c.end());
        return c;
    }
    void animationsTab(UIRenderer& ui, float px, float pw, float bottom, float dt) {
        const auto& theme = gResourcePacks.getTheme();
        float x = px + 16, w = pw - 32;
        // categories
        std::vector<std::string> cats = animCategories();
        if (animCat >= (int)cats.size()) animCat = 0;
        std::vector<const char*> names;
        for (auto& c : cats) names.push_back(c.c_str());
        float saveCx = cx, saveCw = cw;
        cx = x; cw = w;
        int before = animCat;
        pickGrid(ui, names.data(), (int)names.size(), animCat, 4);
        if (animCat != before) { animSel = 0; animPage = 0; animTime = 0.0f; }
        // the entries of this category: (label, index)
        std::vector<std::pair<std::string, int>> list;
        if (animCat == 0) {
            const auto& st = animpreview::states();
            for (int i = 0; i < (int)st.size(); i++) list.push_back({ st[i].name, i });
        } else {
            for (int i = 0; i < gAnims.count(); i++)
                if (gAnims.info(i).category == cats[(size_t)animCat]) {
                    const ClipInfo& c = gAnims.info(i);
                    list.push_back({ c.name + "  [" + c.pack + "]", i });
                }
            std::sort(list.begin(), list.end());
        }
        bool found = false;
        for (auto& e : list) if (e.second == animSel) found = true;
        if (!found && !list.empty()) { animSel = list[0].second; animTime = 0.0f; }
        // a page of them
        float rowH = 26.0f, transportH = 150.0f;
        int perPage = std::max(1, (int)((bottom - transportH - y) / (rowH + 3)));
        int pages = std::max(1, ((int)list.size() + perPage - 1) / perPage);
        animPage = std::clamp(animPage, 0, pages - 1);
        for (int i = animPage * perPage; i < std::min((int)list.size(), (animPage + 1) * perPage); i++) {
            bool sel = list[(size_t)i].second == animSel, hov = hovered(x, y, w, rowH);
            ui.drawRect(x, y, w, rowH, sel ? theme.buttonBgHover : (hov ? theme.bgHover : theme.buttonBg));
            if (sel) ui.drawBorder(x, y, w, rowH, theme.accent, 2.0f);
            ui.drawText(list[(size_t)i].first, x + 8, y + (rowH - ui.textHeight(1)) * 0.5f, sel || hov ? theme.textHover : theme.text, 1);
            if (hov && clicked() && !sel) { animSel = list[(size_t)i].second; animTime = 0.0f; animPlaying = true; }
            y += rowH + 3;
        }
        // pages
        float ty = bottom - transportH + 6;
        if (pages > 1) {
            char pg[32]; std::snprintf(pg, sizeof(pg), "%d / %d", animPage + 1, pages);
            if (button(ui, "<", x, ty, 40, 26) && animPage > 0) animPage--;
            ui.drawTextCentered(pg, x + 44, ty, 90, 26, theme.text, 1);
            if (button(ui, ">", x + 138, ty, 40, 26) && animPage < pages - 1) animPage++;
        }
        ty += 34;
        // transport: what's playing, the time bar, play / speed / loop
        float dur = animCat == 0 ? animpreview::stateDuration(animSel)
                  : (animSel < gAnims.count() ? gAnims.info(animSel).duration : 1.0f);
        dur = std::max(dur, 0.01f);
        static const float SPEEDS[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f };
        static const char* SPEED_NAMES[] = { "0.1x", "0.25x", "0.5x", "1x", "2x" };
        if (animSpeed < 0 || animSpeed > 4) animSpeed = 3;
        if (animPlaying) {
            animTime += dt * SPEEDS[animSpeed];
            if (animTime > dur) { if (animLoop) animTime = std::fmod(animTime, dur); else { animTime = dur; animPlaying = false; } }
        }
        char info[96];
        bool loops = animCat != 0 && animSel < gAnims.count() && gAnims.info(animSel).loop;
        std::snprintf(info, sizeof(info), "%.2f / %.2f s%s", animTime, dur, loops ? "   - loops" : "");
        ui.drawText(info, x, ty, theme.textDim, 1);
        ty += 16;
        // the time bar (drag to scrub)
        float bx = x, bw = w, bh = 10;
        ui.drawRect(bx, ty, bw, bh, theme.inputBg);
        ui.drawRect(bx, ty, bw * std::clamp(animTime / dur, 0.0f, 1.0f), bh, theme.accent);
        ui.drawBorder(bx, ty, bw, bh, theme.border, 1.0f);
        if (clicked() && hovered(bx, ty - 6, bw, bh + 12)) dragging = 100;
        if (dragging == 100) { animTime = std::clamp((float)(mx - bx) / bw, 0.0f, 1.0f) * dur; animPlaying = false; }
        ty += bh + 10;
        float b1 = 90;
        if (button(ui, animPlaying ? "Pause" : "Play", x, ty, b1, 28)) {
            if (!animPlaying && animTime >= dur) animTime = 0.0f;
            animPlaying = !animPlaying;
        }
        if (button(ui, "<<", x + b1 + 6, ty, 40, 28)) animTime = 0.0f;
        float sx = x + b1 + 52;
        for (int i = 0; i < 5; i++) {
            float bw2 = 52;
            bool sel = i == animSpeed, hov = hovered(sx, ty, bw2, 28);
            ui.drawRect(sx, ty, bw2, 28, sel ? theme.buttonBgHover : (hov ? theme.bgHover : theme.buttonBg));
            ui.drawBorder(sx, ty, bw2, 28, sel ? theme.accent : theme.buttonBorder, sel ? 2.0f : 1.0f);
            ui.drawTextCentered(SPEED_NAMES[i], sx, ty, bw2, 28, sel || hov ? theme.textHover : theme.text, 1);
            if (hov && clicked()) animSpeed = i;
            sx += bw2 + 4;
        }
        if (button(ui, animLoop ? "Loop: on" : "Loop: off", sx + 6, ty, std::max(80.0f, x + w - sx - 6), 28)) animLoop = !animLoop;
        cx = saveCx; cw = saveCw;
    }

    // ── Widgets ───────────────────────────────────────────
    void label(UIRenderer& ui, const char* t) {
        const auto& theme = gResourcePacks.getTheme();
        int sc = ui.textWidth(t, 2) <= labelW - 8 ? 2 : 1;
        ui.drawText(t, left, y + (30 - ui.textHeight(sc)) * 0.5f, theme.text, sc);
    }

    bool button(UIRenderer& ui, const char* text, float x, float yy, float w, float h) {
        const auto& theme = gResourcePacks.getTheme();
        bool hov = hovered(x, yy, w, h);
        ui.drawRect(x, yy, w, h, hov ? theme.buttonBgHover : theme.buttonBg);
        ui.drawBorder(x, yy, w, h, theme.buttonBorder, 1.0f);
        int sc = ui.textWidth(text, 2) <= w - 6 ? 2 : 1;
        ui.drawTextCentered(text, x, yy, w, h, hov ? theme.textHover : theme.text, sc);
        return hov && clicked();
    }

    // Buttons in a grid, one selected; advances y
    void pickGrid(UIRenderer& ui, const char* const* opts, int n, int& selected, int cols) {
        const auto& theme = gResourcePacks.getTheme();
        float gap = 4.0f, bw = (cw - gap * (cols - 1)) / cols, h = 30.0f;
        int rows = (n + cols - 1) / cols;
        for (int i = 0; i < n; i++) {
            float bx = cx + (i % cols) * (bw + gap), by = y + (i / cols) * (h + gap);
            bool hov = hovered(bx, by, bw, h), sel = i == selected;
            ui.drawRect(bx, by, bw, h, sel ? theme.buttonBgHover : (hov ? theme.bgHover : theme.buttonBg));
            ui.drawBorder(bx, by, bw, h, sel ? theme.accent : theme.buttonBorder, sel ? 2.0f : 1.0f);
            int scale = ui.textWidth(opts[i], 2) <= bw - 6 ? 2 : 1;
            ui.drawTextCentered(opts[i], bx, by, bw, h, sel || hov ? theme.textHover : theme.text, scale);
            if (hov && clicked()) selected = i;
        }
        y += rows * (h + gap) + 6;
    }

    // A slider from lo to 1; advances y
    void slider(UIRenderer& ui, int id, float* v, float lo, float hi) {
        const auto& theme = gResourcePacks.getTheme();
        float x = cx, w = cw, sy = y + 11, th = 8.0f, t = (*v - lo) / (hi - lo);
        ui.drawRect(x, sy, w, th, theme.inputBg);
        ui.drawRect(x, sy, w * t, th, theme.buttonBg);
        if (lo < 0) ui.drawRect(x + w * 0.5f - 1, sy - 3, 2, th + 6, theme.border);   // the middle mark
        ui.drawBorder(x, sy, w, th, theme.border, 1.0f);
        float kx = x + w * t;
        ui.drawRect(kx - 6, sy - 6, 12, th + 12, dragging == id ? theme.textHover : theme.accent);
        if (clicked() && hovered(x - 8, sy - 10, w + 16, th + 20)) dragging = id;
        if (dragging == id) {
            float f = std::clamp((float)(mx - x) / w, 0.0f, 1.0f);
            *v = lo + f * (hi - lo);
            if (lo < 0 && std::fabs(*v) < 0.06f) *v = 0.0f;                 // snap to the middle
        }
        y += 38;
    }

    // Colour squares, wrapping; advances y
    void swatches(UIRenderer& ui, const std::vector<glm::u8vec3>& cols, glm::u8vec3& value) {
        const auto& theme = gResourcePacks.getTheme();
        float size = 26.0f, gap = 8.0f;
        int perRow = std::max(1, (int)((cw + gap) / (size + gap)));
        for (size_t i = 0; i < cols.size(); i++) {
            float sx = cx + (i % perRow) * (size + gap), sy = y + 2 + (i / perRow) * (size + gap);
            float c[4] = { (float)cols[i].r, (float)cols[i].g, (float)cols[i].b, 255.0f };
            bool sel = cols[i] == value, hov = hovered(sx, sy, size, size);
            ui.drawRect(sx, sy, size, size, c);
            float o = sel ? 3.0f : 0.0f;
            ui.drawBorder(sx - o, sy - o, size + 2 * o, size + 2 * o,
                          sel ? theme.textHover : (hov ? theme.accent : theme.border), sel ? 2.0f : 1.0f);
            if (hov && clicked()) value = cols[i];
        }
        int rows = ((int)cols.size() + perRow - 1) / perRow;
        y += std::max(1, rows) * (size + gap) + 6;
    }
};
