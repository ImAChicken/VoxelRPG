#pragma once
#include "../input/InputManager.h"
#include "../input/PadLayout.h"
#include "UIRenderer.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// PadNav — every menu, with a controller
// ══════════════════════════════════════════════════════════
// The menus are written for the mouse, and stay that way. This
// is a stand-in mouse the controller drives: each frame the
// screens' clickable rectangles are noted (they already ask
// "is the mouse over this?" of each one: their hovered() helpers
// call add()), the D-pad / left stick moves a focus between them
// by where they sit on screen, and while the pad is the device in
// use, the screens read the mouse from here (cursor(), button())
// and find it on the focused one. So a menu works with the pad
// without knowing about it, and the mouse still works as before.
//
//   D-pad / left stick   move the focus (held: repeats)
//   A                    left button: tap to click, hold and move
//                        to drag (an inventory stack)
//   Y                    right button (drag half a stack)
//   X                    shift click (equip / send across)
//   B                    back: clicks the screen's Back / Resume /
//                        Cancel button, if it has one (back())
//   on a slider or a < value > choice, left / right change it
//   instead of moving the focus (setKind)
//
// main.cpp calls frame() once a frame (before the screens) with
// whether a menu is up, and drawFocus() after them.
class PadNav {
public:
    enum class Kind : uint8_t { Button, Slider, Choice };
    struct Item { float x, y, w, h; Kind kind; float frac; std::string label; };

    // Is the pad driving the menus right now
    bool active() const { return activeNow; }
    // B this frame (and no Back button took it): close the menu
    bool back() const { return backNow && !backClicked; }
    // Up / down with nothing further that way: scroll (a list's
    // wheel: +1 a notch up, -1 down), so long lists keep going
    float scroll() const { return scrollNow; }
    // A screen waiting for a pad button to bind: hands the pad back
    // to it (set each frame, read next frame)
    void pause(bool p) { paused = p; }

    // ── Called by the screens ─────────────────────────────
    // A clickable rectangle (the screens' hover tests call this)
    void add(float x, float y, float w, float h, const std::string& label = std::string()) {
        if (!menuUp || w <= 0.0f || h <= 0.0f) return;
        for (Item& it : items)                          // one button tested twice, or drawn a little off its test
            if (near(it, x, y, w, h)) { if (!label.empty()) it.label = label; return; }
        items.push_back({ x, y, w, h, Kind::Button, 0.0f, label });
    }
    // Mark one as a slider (frac: where its knob is, 0..1) or a
    // < value > choice: left / right change it
    void setKind(float x, float y, float w, float h, Kind k, float frac = 0.0f) {
        add(x, y, w, h);
        for (Item& it : items) if (near(it, x, y, w, h)) { it.kind = k; it.frac = frac; }
    }
    // A scrolling list's window: moving up / down out of it scrolls
    // it first (until it stops moving), then leaves it
    void scrollArea(float x, float y, float w, float h) {
        if (menuUp) areas.push_back({ x, y, w, h, Kind::Button, 0.0f, std::string() });
    }
    // The mouse, as the screens should read it
    void cursor(GLFWwindow* win, double* x, double* y) const {
        if (!activeNow) { glfwGetCursorPos(win, x, y); return; }
        *x = curX; *y = curY;
    }
    bool button(GLFWwindow* win, int b) const {
        if (!activeNow) return glfwGetMouseButton(win, b) == GLFW_PRESS;
        if (b == GLFW_MOUSE_BUTTON_LEFT)  return leftNow;
        if (b == GLFW_MOUSE_BUTTON_RIGHT) return rightNow;
        return false;
    }
    bool shift() const { return activeNow && shiftNow; }

    // ── Called by main.cpp ────────────────────────────────
    void reset() { scrollTried = -1; areas.clear(); lastAreas.clear(); focus = -1; hasFocus = false; items.clear(); last.clear(); clickStep = 0; shiftClick = 0; paused = false; }

    void frame(bool menuOpen) {
        if (menuOpen != menuUp) reset();
        menuUp = menuOpen;
        last.swap(items);
        items.clear();
        lastAreas.swap(areas);
        areas.clear();
        backNow = backClicked = false;
        shiftNow = false;
        scrollNow = 0.0f;
        const pad::State& s = gInput.padNow();
        activeNow = menuUp && s.connected && gInput.padLastUsed() && !paused;
        if (!activeNow) { prevDown = s.down; return; }

        double now = glfwGetTime();
        auto edge = [&](int b) { return s.down[b] && !prevDown[b]; };

        // find the focus again among last frame's rectangles
        focus = -1;
        if (hasFocus) {
            float best = 1e9f;
            for (int i = 0; i < (int)last.size(); i++) {
                float d = std::fabs(cx(last[i]) - fx) + std::fabs(cy(last[i]) - fy);
                if (same(last[i], focusRect.x, focusRect.y, focusRect.w, focusRect.h)) { focus = i; break; }
                if (d < best) { best = d; focus = i; }
            }
        }
        if (focus < 0 && !last.empty()) focus = firstItem();

        // a direction: D-pad or stick, repeating while held
        int dir = -1;                                   // 0 up, 1 right, 2 down, 3 left
        // the stick: only its stronger axis (a push right that drifts
        // a little up is right, not right and up)
        const bool sx = std::fabs(s.left.x) >= std::fabs(s.left.y);
        bool dirDown[4] = { s.down[pad::Up]    || (!sx && s.left.y < -0.5f), s.down[pad::Right] || (sx && s.left.x > 0.5f),
                            s.down[pad::Down]  || (!sx && s.left.y > 0.5f),  s.down[pad::Left]  || (sx && s.left.x < -0.5f) };
        for (int d = 0; d < 4; d++) {
            if (!dirDown[d]) { heldSince[d] = -1.0; continue; }
            if (heldSince[d] < 0.0) { heldSince[d] = now; nextRepeat[d] = now + 0.35; dir = d; }
            else if (now >= nextRepeat[d]) { nextRepeat[d] = now + 0.11; dir = d; }
        }

        // a slider / choice takes left and right for itself
        if (focus >= 0 && (dir == 1 || dir == 3) && last[focus].kind != Kind::Button) {
            const Item& it = last[focus];
            float px;
            if (it.kind == Kind::Slider) {
                float f = std::clamp(it.frac + (dir == 1 ? 0.05f : -0.05f), 0.0f, 1.0f);
                px = it.x + it.w * f;
            } else px = dir == 1 ? it.x + it.w - 4.0f : it.x + 4.0f;
            clickX = px; clickY = it.y + it.h * 0.5f;
            clickStep = 2;                              // down this frame, up the next
        } else if (dir >= 0 && focus >= 0) {
            int to = neighbour(focus, dir);
            // in a scrolling list, going out of it: scroll it instead,
            // unless the last scroll this way moved nothing (its end)
            if (dir == 0 || dir == 2) {
                for (const Item& ar : lastAreas) {
                    if (!inside(ar, last[focus]) || (to >= 0 && inside(ar, last[to]))) continue;
                    float sig = 0.0f;
                    for (const Item& it : last) if (inside(ar, it)) sig += it.y;
                    if (scrollTried == dir && std::fabs(sig - scrollSig) < 0.5f) break;   // at its end: leave
                    scrollTried = dir; scrollSig = sig;
                    to = -2;                                        // scroll, stay
                    break;
                }
                if (to != -2) scrollTried = -1;
            }
            if (std::getenv("VOXELRPG_NAV_DEBUG"))
                std::fprintf(stderr, "nav dir %d from %.0f,%.0f %.0fx%.0f -> %d of %d %s\n", dir, last[focus].x, last[focus].y, last[focus].w, last[focus].h,
                             to, (int)last.size(), to >= 0 ? (std::to_string((int)last[to].x) + "," + std::to_string((int)last[to].y)).c_str() : "");
            if (to >= 0) focus = to;
            else if (dir == 0 || dir == 2) scrollNow = dir == 0 ? 1.0f : -1.0f;

        }

        if (focus >= 0) {
            focusRect = last[focus];
            fx = cx(focusRect); fy = cy(focusRect);
            if (focusRect.kind == Kind::Slider) fx = focusRect.x + focusRect.w * focusRect.frac;   // on its knob, not its middle
            hasFocus = true;
        }

        // the buttons
        bool a = s.down[pad::A], y = s.down[pad::Y], x = s.down[pad::X];
        if (edge(pad::X)) { shiftClick = 2; }
        if (edge(pad::B)) {
            backNow = true;
            for (const Item& it : last)                 // the screen's own way back
                if (it.label == "Back" || it.label == "Resume" || it.label == "Cancel" || it.label == "Close") {
                    clickX = cx(it); clickY = cy(it); clickStep = 2; backClicked = true;
                    break;
                }
        }
        curX = fx; curY = fy;
        leftNow = a;
        rightNow = y;
        if (shiftClick > 0) { shiftNow = true; leftNow = shiftClick == 2; shiftClick--; }
        else if (x) shiftNow = true;
        if (clickStep > 0) {
            curX = clickX; curY = clickY;
            leftNow = clickStep == 2;
            clickStep--;
        }
        prevDown = s.down;
    }

    // An outline round the focus (after the screens have drawn)
    void drawFocus(UIRenderer& ui) const {
        if (!activeNow || !hasFocus) return;
        bool there = false;
        for (const Item& it : items) there = there || same(it, focusRect.x, focusRect.y, focusRect.w, focusRect.h);
        if (!there) return;
        static const float GOLD[4] = { 255, 214, 90, 255 };
        ui.begin();
        ui.drawBorder(focusRect.x - 3, focusRect.y - 3, focusRect.w + 6, focusRect.h + 6, GOLD, 2.0f);
        ui.end();
    }

private:
    std::vector<Item> items, last, areas, lastAreas;
    int   scrollTried = -1;
    float scrollSig = 0.0f;
    bool menuUp = false, activeNow = false;
    int  focus = -1;
    bool hasFocus = false;
    Item focusRect{};
    float fx = 0, fy = 0;
    double curX = 0, curY = 0, clickX = 0, clickY = 0;
    bool leftNow = false, rightNow = false, shiftNow = false;
    int  clickStep = 0, shiftClick = 0;
    bool backNow = false, backClicked = false, paused = false;
    float scrollNow = 0.0f;
    std::array<bool, pad::COUNT> prevDown{};
    double heldSince[4] = { -1, -1, -1, -1 }, nextRepeat[4] = { 0, 0, 0, 0 };

    static float cx(const Item& i) { return i.x + i.w * 0.5f; }
    static bool inside(const Item& area, const Item& i) {
        return cx(i) >= area.x && cx(i) <= area.x + area.w && cy(i) >= area.y && cy(i) <= area.y + area.h;
    }
    static float cy(const Item& i) { return i.y + i.h * 0.5f; }
    static bool near(const Item& i, float x, float y, float w, float h) {
        return std::fabs(cx(i) - (x + w * 0.5f)) < 6.0f && std::fabs(cy(i) - (y + h * 0.5f)) < 6.0f &&
               std::fabs(i.w - w) < 12.0f && std::fabs(i.h - h) < 12.0f;
    }
    static bool same(const Item& i, float x, float y, float w, float h) {
        return std::fabs(i.x - x) < 0.5f && std::fabs(i.y - y) < 0.5f && std::fabs(i.w - w) < 0.5f && std::fabs(i.h - h) < 0.5f;
    }
    // Where a screen's focus starts: its Resume button (the
    // inventory, the pause menu), else the top left
    int firstItem() const {
        for (int i = 0; i < (int)last.size(); i++) if (last[i].label == "Resume") return i;
        int best = 0;
        for (int i = 1; i < (int)last.size(); i++)
            if (last[i].y < last[best].y - 1.0f || (std::fabs(last[i].y - last[best].y) <= 1.0f && last[i].x < last[best].x)) best = i;
        return best;
    }
    // The nearest rectangle that way: straight ahead counts most
    // Left / right keep to the row: first only what shares some of
    // its height, then what's within a row's height of it; never a
    // jump to another row's far end (that read as going up / down)
    int neighbour(int from, int dir) const {
        if (dir == 1 || dir == 3) {
            int b = nearest(from, dir, 0.0f);
            return b >= 0 ? b : nearest(from, dir, std::max(8.0f, last[from].h * 0.5f));
        }
        return nearest(from, dir, 1e9f);
    }
    int nearest(int from, int dir, float maxGap) const {
        const Item& a = last[from];
        int best = -1;
        float bestScore = 1e9f;
        for (int i = 0; i < (int)last.size(); i++) {
            if (i == from) continue;
            const Item& b = last[i];
            float along, gap;
            if (dir == 0 || dir == 2) {
                along = dir == 2 ? cy(b) - cy(a) : cy(a) - cy(b);
                gap = std::max(0.0f, std::max(a.x - (b.x + b.w), b.x - (a.x + a.w)));
            } else {
                along = dir == 1 ? cx(b) - cx(a) : cx(a) - cx(b);
                gap = std::max(0.0f, std::max(a.y - (b.y + b.h), b.y - (a.y + a.h)));
            }
            if (along <= 2.0f || gap > maxGap) continue;
            float score = along + gap * 3.0f;
            if (score < bestScore) { bestScore = score; best = i; }
        }
        return best;
    }
};

inline PadNav gNav;
