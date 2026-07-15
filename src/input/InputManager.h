#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <toml++/toml.hpp>
#include "InputAction.h"

// A single bindable input source
struct InputBinding {
    enum class Type : uint8_t {
        Key,
        MouseButton,
        MouseScrollUp,
        MouseScrollDown,
        GamepadButton,
        GamepadAxis,
        None,
    };

    Type  type    = Type::None;
    int   code    = 0;
    float axisDir = 1.0f;
};

// Manages all player input
// Gameplay code calls isHeld/isPressed/isReleased
// Never sees GLFW constants directly
// Bindings loaded from TOML — fully rebindable

class InputManager {
public:
    bool loadKeyboard(
        const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            std::cerr << "Keyboard config not found: "
                      << path << "\n";
            loadDefaults();
            return false;
        }
        try {
            auto tbl = toml::parse_file(path.string());
            loadSection(tbl, "movement");
            loadSection(tbl, "actions");
            loadSection(tbl, "camera");
            loadSection(tbl, "dev");
            loadSection(tbl, "system");
            std::cout << "Keyboard bindings loaded: "
                      << path << "\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Keyboard config error: "
                      << e.what() << "\n";
            loadDefaults();
            return false;
        }
    }

    // Call once per frame before processing input
    void poll(GLFWwindow* window) {
        prevState    = currState;
        prevScroll   = scrollDelta;
        scrollDelta  = 0.0f;

        // Poll keyboard + mouse buttons
        for (auto& [action, binding] : bindings) {
            int idx = (int)action;
            if (idx < 0 ||
                idx >= (int)InputAction::COUNT) continue;

            switch (binding.type) {
                case InputBinding::Type::Key:
                    currState[idx] =
                        glfwGetKey(window, binding.code)
                        == GLFW_PRESS;
                    break;
                case InputBinding::Type::MouseButton:
                    currState[idx] =
                        glfwGetMouseButton(
                            window, binding.code)
                        == GLFW_PRESS;
                    break;
                case InputBinding::Type::MouseScrollUp:
                    currState[idx] =
                        (scrollDelta > 0.0f);
                    break;
                case InputBinding::Type::MouseScrollDown:
                    currState[idx] =
                        (scrollDelta < 0.0f);
                    break;
                default:
                    currState[idx] = false;
                    break;
            }
        }

        // Mouse delta
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        mouseDelta.x = (float)(mx - lastMouseX);
        mouseDelta.y = (float)(my - lastMouseY);
        lastMouseX   = mx;
        lastMouseY   = my;
    }

    // Is action currently held down
    bool isHeld(InputAction action) const {
        int idx = (int)action;
        if (idx < 0 || idx >= (int)InputAction::COUNT)
            return false;
        return currState[idx];
    }

    // Was action just pressed this frame
    bool isPressed(InputAction action) const {
        int idx = (int)action;
        if (idx < 0 || idx >= (int)InputAction::COUNT)
            return false;
        return currState[idx] && !prevState[idx];
    }

    // Was action just released this frame
    bool isReleased(InputAction action) const {
        int idx = (int)action;
        if (idx < 0 || idx >= (int)InputAction::COUNT)
            return false;
        return !currState[idx] && prevState[idx];
    }

    // Mouse movement this frame in pixels
    glm::vec2 getMouseDelta() const {
        return mouseDelta;
    }

    // Scroll wheel delta this frame
    float getScrollDelta() const {
        return scrollDelta;
    }

    // Called by GLFW scroll callback
    void onScroll(float yOffset) {
        scrollDelta += yOffset;
    }

    // Called by GLFW char callback (for text input)
    void onChar(unsigned int codepoint) {
        lastChar = codepoint;
        hasChar  = true;
    }

    bool consumeChar(unsigned int& out) {
        if (!hasChar) return false;
        out     = lastChar;
        hasChar = false;
        return true;
    }

    // Rebind an action at runtime
    void rebind(InputAction action,
                InputBinding binding) {
        bindings[action] = binding;
    }

    // Reset mouse delta (call when capturing cursor)
    void resetMouseDelta(GLFWwindow* window) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        lastMouseX   = mx;
        lastMouseY   = my;
        mouseDelta   = {0, 0};
    }

private:
    std::unordered_map<InputAction,
                       InputBinding> bindings;

    std::array<bool, (int)InputAction::COUNT>
        currState{};
    std::array<bool, (int)InputAction::COUNT>
        prevState{};

    glm::vec2 mouseDelta  = {0, 0};
    float     scrollDelta = 0.0f;
    float     prevScroll  = 0.0f;
    double    lastMouseX  = 0;
    double    lastMouseY  = 0;

    unsigned int lastChar = 0;
    bool         hasChar  = false;

    // ── Key name lookup ───────────────────────────────────
    int keyNameToCode(const std::string& name) {
        // Letters
        if (name.size() == 1 &&
            name[0] >= 'A' && name[0] <= 'Z')
            return GLFW_KEY_A + (name[0] - 'A');

        // Common keys
        if (name == "SPACE")        return GLFW_KEY_SPACE;
        if (name == "LEFT_SHIFT")   return GLFW_KEY_LEFT_SHIFT;
        if (name == "RIGHT_SHIFT")  return GLFW_KEY_RIGHT_SHIFT;
        if (name == "LEFT_CONTROL") return GLFW_KEY_LEFT_CONTROL;
        if (name == "RIGHT_CONTROL")return GLFW_KEY_RIGHT_CONTROL;
        if (name == "LEFT_ALT")     return GLFW_KEY_LEFT_ALT;
        if (name == "TAB")          return GLFW_KEY_TAB;
        if (name == "ESCAPE")       return GLFW_KEY_ESCAPE;
        if (name == "ENTER")        return GLFW_KEY_ENTER;
        if (name == "BACKSPACE")    return GLFW_KEY_BACKSPACE;
        if (name == "GRAVE")        return GLFW_KEY_GRAVE_ACCENT;
        if (name == "LEFT_BRACKET") return GLFW_KEY_LEFT_BRACKET;
        if (name == "RIGHT_BRACKET")return GLFW_KEY_RIGHT_BRACKET;

        // Function keys
        if (name == "F1")  return GLFW_KEY_F1;
        if (name == "F2")  return GLFW_KEY_F2;
        if (name == "F3")  return GLFW_KEY_F3;
        if (name == "F4")  return GLFW_KEY_F4;
        if (name == "F5")  return GLFW_KEY_F5;
        if (name == "F6")  return GLFW_KEY_F6;
        if (name == "F7")  return GLFW_KEY_F7;
        if (name == "F8")  return GLFW_KEY_F8;
        if (name == "F9")  return GLFW_KEY_F9;
        if (name == "F10") return GLFW_KEY_F10;
        if (name == "F11") return GLFW_KEY_F11;
        if (name == "F12") return GLFW_KEY_F12;

        // Number keys
        if (name == "0") return GLFW_KEY_0;
        if (name == "1") return GLFW_KEY_1;
        if (name == "2") return GLFW_KEY_2;
        if (name == "3") return GLFW_KEY_3;
        if (name == "4") return GLFW_KEY_4;
        if (name == "5") return GLFW_KEY_5;
        if (name == "6") return GLFW_KEY_6;
        if (name == "7") return GLFW_KEY_7;
        if (name == "8") return GLFW_KEY_8;
        if (name == "9") return GLFW_KEY_9;

        // Arrow keys
        if (name == "UP")    return GLFW_KEY_UP;
        if (name == "DOWN")  return GLFW_KEY_DOWN;
        if (name == "LEFT")  return GLFW_KEY_LEFT;
        if (name == "RIGHT") return GLFW_KEY_RIGHT;

        return GLFW_KEY_UNKNOWN;
    }

    int mouseButtonNameToCode(const std::string& name) {
        if (name == "MOUSE_LEFT")   return GLFW_MOUSE_BUTTON_LEFT;
        if (name == "MOUSE_RIGHT")  return GLFW_MOUSE_BUTTON_RIGHT;
        if (name == "MOUSE_MIDDLE") return GLFW_MOUSE_BUTTON_MIDDLE;
        return -1;
    }

    void loadSection(const toml::table& tbl,
                     const std::string& section) {
        auto* s = tbl[section].as_table();
        if (!s) return;

        for (auto& [key, val] : *s) {
            std::string actionName =
                section + "_" + std::string(key.str());
            InputAction action = nameToAction(actionName);
            if (action == InputAction::COUNT) continue;

            std::string bindStr = val.value_or("");
            InputBinding binding;

            if (bindStr == "MOUSE_SCROLL_UP") {
                binding.type = InputBinding::Type
                                   ::MouseScrollUp;
            } else if (bindStr == "MOUSE_SCROLL_DOWN") {
                binding.type = InputBinding::Type
                                   ::MouseScrollDown;
            } else {
                int mb = mouseButtonNameToCode(bindStr);
                if (mb >= 0) {
                    binding.type =
                        InputBinding::Type::MouseButton;
                    binding.code = mb;
                } else {
                    binding.type =
                        InputBinding::Type::Key;
                    binding.code =
                        keyNameToCode(bindStr);
                }
            }

            bindings[action] = binding;
        }
    }

    InputAction nameToAction(const std::string& n) {
        static const std::unordered_map<
            std::string, InputAction> map = {
            {"movement_move_forward",   InputAction::MoveForward},
            {"movement_move_backward",  InputAction::MoveBackward},
            {"movement_strafe_left",    InputAction::StrafeLeft},
            {"movement_strafe_right",   InputAction::StrafeRight},
            {"movement_jump",           InputAction::Jump},
            {"movement_crouch",         InputAction::Crouch},
            {"movement_sprint",         InputAction::Sprint},
            {"movement_roll",           InputAction::Roll},
            {"actions_use_left",        InputAction::UseLeft},
            {"actions_use_right",       InputAction::UseRight},
            {"actions_block",           InputAction::Block},
            {"actions_draw_weapon",     InputAction::DrawWeapon},
            {"actions_interact",        InputAction::Interact},
            {"actions_ledge_grab",      InputAction::LedgeGrab},
            {"actions_inventory",       InputAction::Inventory},
            {"actions_toolbar_next",    InputAction::ToolbarNext},
            {"actions_toolbar_prev",    InputAction::ToolbarPrev},
            {"actions_toolbar_slot_1",  InputAction::ToolbarSlot1},
            {"actions_toolbar_slot_2",  InputAction::ToolbarSlot2},
            {"actions_toolbar_slot_3",  InputAction::ToolbarSlot3},
            {"actions_toolbar_slot_4",  InputAction::ToolbarSlot4},
            {"actions_toolbar_slot_5",  InputAction::ToolbarSlot5},
            {"actions_toolbar_slot_6",  InputAction::ToolbarSlot6},
            {"actions_toolbar_slot_7",  InputAction::ToolbarSlot7},
            {"actions_toolbar_slot_8",  InputAction::ToolbarSlot8},
            {"camera_camera_toggle",    InputAction::CameraToggle},
            {"dev_toggle_fly",          InputAction::ToggleFly},
            {"dev_toggle_debug",        InputAction::ToggleDebug},
            {"dev_toggle_console",      InputAction::ToggleConsole},
            {"dev_gamemode_cycle",      InputAction::GamemodeCycle},
            {"dev_reload_registry",     InputAction::ReloadRegistry},
            {"dev_reload_pack",         InputAction::ReloadPack},
            {"system_pause",            InputAction::Pause},
            {"system_screenshot",       InputAction::Screenshot},
        };
        auto it = map.find(n);
        return it != map.end()
            ? it->second : InputAction::COUNT;
    }

    void loadDefaults() {
        // Hardcoded fallback if TOML missing
        auto bind = [&](InputAction a, int key) {
            InputBinding b;
            b.type = InputBinding::Type::Key;
            b.code = key;
            bindings[a] = b;
        };
        auto bindMouse = [&](InputAction a, int btn) {
            InputBinding b;
            b.type = InputBinding::Type::MouseButton;
            b.code = btn;
            bindings[a] = b;
        };

        bind(InputAction::MoveForward,   GLFW_KEY_W);
        bind(InputAction::MoveBackward,  GLFW_KEY_S);
        bind(InputAction::StrafeLeft,    GLFW_KEY_A);
        bind(InputAction::StrafeRight,   GLFW_KEY_D);
        bind(InputAction::Jump,          GLFW_KEY_SPACE);
        bind(InputAction::Crouch,        GLFW_KEY_LEFT_CONTROL);
        bind(InputAction::Sprint,        GLFW_KEY_LEFT_SHIFT);
        bind(InputAction::Roll,          GLFW_KEY_LEFT_SHIFT);
        bind(InputAction::Interact,      GLFW_KEY_E);
        bind(InputAction::Inventory,     GLFW_KEY_TAB);
        bind(InputAction::DrawWeapon,    GLFW_KEY_R);
        bind(InputAction::Block,         GLFW_KEY_Q);
        bind(InputAction::ToggleFly,     GLFW_KEY_V);
        bind(InputAction::ToggleDebug,   GLFW_KEY_F3);
        bind(InputAction::ToggleConsole, GLFW_KEY_GRAVE_ACCENT);
        bind(InputAction::GamemodeCycle, GLFW_KEY_F4);
        bind(InputAction::ReloadRegistry,GLFW_KEY_F6);
        bind(InputAction::ReloadPack,    GLFW_KEY_F7);
        bind(InputAction::CameraToggle,  GLFW_KEY_F5);
        bind(InputAction::Pause,         GLFW_KEY_ESCAPE);
        bind(InputAction::Screenshot,    GLFW_KEY_F2);
        bind(InputAction::ToolbarSlot1,  GLFW_KEY_1);
        bind(InputAction::ToolbarSlot2,  GLFW_KEY_2);
        bind(InputAction::ToolbarSlot3,  GLFW_KEY_3);
        bind(InputAction::ToolbarSlot4,  GLFW_KEY_4);
        bind(InputAction::ToolbarSlot5,  GLFW_KEY_5);
        bind(InputAction::ToolbarSlot6,  GLFW_KEY_6);
        bind(InputAction::ToolbarSlot7,  GLFW_KEY_7);
        bind(InputAction::ToolbarSlot8,  GLFW_KEY_8);
        bindMouse(InputAction::UseLeft,  GLFW_MOUSE_BUTTON_LEFT);
        bindMouse(InputAction::UseRight, GLFW_MOUSE_BUTTON_RIGHT);

        std::cout << "InputManager: using default bindings\n";
    }
};

extern InputManager gInput;
