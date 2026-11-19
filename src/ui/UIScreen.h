#pragma once
#include "UIRenderer.h"
#include "../render/ResourcePack.h"
#include "PadNav.h"

enum class ScreenResult {
    Continue,
    NewWorld,
    LoadWorld,
    ResumeGame,
    SaveAndQuit,
    Quit,
    OpenDevMenu,    // pause menu → dev menu
    CloseMenu,      // a sub-menu → back to the pause menu
    OpenControls,   // pause menu → key bindings
    OpenSettings,   // pause menu → settings
    OpenCharacter,  // pause menu → character creator
    SaveGame,       // pause menu → save, and stay
    QuitNoSave,     // pause menu → leave without saving (dev worlds; disconnecting from a server)
    JoinServer,     // main menu → connect to a server (MainMenuScreen::getJoinAddress / getJoinName)
};

class UIScreen {
public:
    virtual ~UIScreen() = default;

    virtual ScreenResult update(
        UIRenderer& ui,
        float deltaTime) = 0;

    virtual void onEnter() {}
    virtual void onExit()  {}

protected:
    bool drawButton(UIRenderer& ui,
                    const std::string& label,
                    float x, float y,
                    bool hovered,
                    int scale = 2,
                    float width = 0.0f) {          // 0: the theme's button width
        const auto& theme = gResourcePacks.getTheme();
        float w = width > 0.0f ? width : (float)theme.buttonW;
        float h = (float)theme.buttonH;
        gNav.add(x, y, w, h, label);               // the controller can reach it (ui/PadNav.h)

        const float* bg = hovered
            ? theme.buttonBgHover : theme.buttonBg;
        ui.drawRect(x, y, w, h, bg);
        ui.drawBorder(x, y, w, h, theme.buttonBorder, 1.0f);

        const float* tc = hovered
            ? theme.textHover : theme.text;
        ui.drawTextCentered(label, x, y, w, h, tc, scale);
        return false;
    }

    void drawInput(UIRenderer& ui,
                   const std::string& value,
                   const std::string& placeholder,
                   float x, float y, float w,
                   bool focused,
                   bool showCursor) {
        const auto& theme = gResourcePacks.getTheme();
        float h = (float)theme.buttonH;

        ui.drawRect(x, y, w, h, theme.inputBg);
        const float* border = focused
            ? theme.accent : theme.inputBorder;
        ui.drawBorder(x, y, w, h, border, 1.0f);

        float ty = y + (h - ui.textHeight(2)) * 0.5f;

        if (value.empty() && !focused) {
            ui.drawText(placeholder,
                x + theme.buttonPad, ty,
                theme.textDim, 2);
        } else {
            ui.drawText(value,
                x + theme.buttonPad, ty,
                theme.text, 2);

            if (focused && showCursor) {
                float cursorX = x + theme.buttonPad
                    + ui.textWidth(value, 2);
                ui.drawRect(cursorX, y + 6,
                    2.0f, h - 12.0f,
                    theme.inputCursor);
            }
        }
    }

    void drawPanel(UIRenderer& ui,
                   float x, float y,
                   float w, float h) {
        const auto& theme = gResourcePacks.getTheme();
        ui.drawRect(x, y, w, h, theme.bg);
        ui.drawBorder(x, y, w, h, theme.border, 1.0f);
    }
};
