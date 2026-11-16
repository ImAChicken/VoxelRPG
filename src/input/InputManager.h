#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
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

    // Same physical input? (key/button code only matters
    // for keys and mouse buttons)
    bool sameAs(const InputBinding& o) const {
        if (type != o.type || type == Type::None) return false;
        if (type == Type::Key || type == Type::MouseButton) return code == o.code;
        return true;
    }
};

// One row of the action table: where the action lives in
// keyboard.toml, what the Controls screen calls it, and its
// default binding.
struct ActionInfo {
    InputAction action;
    const char* section;     // [movement], [actions] …
    const char* key;         // move_forward …
    const char* label;       // "Move forward"
    const char* defaultBind; // "W", "MOUSE_LEFT", "MOUSE_SCROLL_UP"
};

// ══════════════════════════════════════════════════════════
// InputManager — every key and button, by action
// ══════════════════════════════════════════════════════════
// Gameplay code calls isHeld / isPressed / isReleased with an
// action and never sees GLFW key codes. Bindings come from
// data/controls/keyboard.toml and can be changed in game
// (Esc → Controls), which writes the file back.
//
// actionTable() below is the single list of actions: it is
// used to load and save the file, to fill the Controls
// screen, and as the defaults for "Reset to defaults".
class InputManager {
public:
    static const std::vector<ActionInfo>& actionTable() {
        static const std::vector<ActionInfo> table = {
            // movement
            {InputAction::MoveForward,  "movement", "move_forward",  "Move forward",  "W"},
            {InputAction::MoveBackward, "movement", "move_backward", "Move back",     "S"},
            {InputAction::StrafeLeft,   "movement", "strafe_left",   "Strafe left",   "A"},
            {InputAction::StrafeRight,  "movement", "strafe_right",  "Strafe right",  "D"},
            {InputAction::Jump,         "movement", "jump",          "Jump",          "SPACE"},
            {InputAction::Crouch,       "movement", "crouch",        "Crouch",        "LEFT_CONTROL"},
            {InputAction::Sprint,       "movement", "sprint",        "Sprint (hold)", "LEFT_SHIFT"},
            {InputAction::Roll,         "movement", "roll",          "Roll (tap)",    "LEFT_SHIFT"},
            // actions
            {InputAction::UseLeft,      "actions", "use_left",       "Use / place / attack", "MOUSE_LEFT"},
            {InputAction::UseRight,     "actions", "use_right",      "Break / heavy attack", "MOUSE_RIGHT"},
            {InputAction::Block,        "actions", "block",          "Guard",          "Q"},
            {InputAction::DrawWeapon,   "actions", "draw_weapon",    "Draw weapon",    "R"},
            {InputAction::Interact,     "actions", "interact",       "Interact",       "E"},
            {InputAction::LedgeGrab,    "actions", "ledge_grab",     "Ledge grab",     "F"},
            {InputAction::Inventory,    "actions", "inventory",      "Inventory",      "I"},
            {InputAction::SwapWeapon,   "actions", "swap_weapon",    "Swap weapon 1 / 2", "X"},
            {InputAction::LockOn,       "actions", "lock_on",        "Lock on",        "TAB"},
            {InputAction::ToolbarNext,  "actions", "toolbar_next",   "Next tool",      "MOUSE_SCROLL_DOWN"},
            {InputAction::ToolbarPrev,  "actions", "toolbar_prev",   "Previous tool",  "MOUSE_SCROLL_UP"},
            {InputAction::ToolbarSlot1, "actions", "toolbar_slot_1", "Off-hand 1",     "1"},
            {InputAction::ToolbarSlot2, "actions", "toolbar_slot_2", "Off-hand 2",     "2"},
            {InputAction::ToolbarSlot3, "actions", "toolbar_slot_3", "Hotbar slot 3",  "3"},
            {InputAction::ToolbarSlot4, "actions", "toolbar_slot_4", "Hotbar slot 4",  "4"},
            {InputAction::ToolbarSlot5, "actions", "toolbar_slot_5", "Hotbar slot 5",  "5"},
            {InputAction::ToolbarSlot6, "actions", "toolbar_slot_6", "Hotbar slot 6",  "6"},
            {InputAction::ToolbarSlot7, "actions", "toolbar_slot_7", "Hotbar slot 7",  "7"},
            {InputAction::ToolbarSlot8, "actions", "toolbar_slot_8", "Hotbar slot 8",  "8"},
            {InputAction::ToolbarSlot9, "actions", "toolbar_slot_9", "Hotbar slot 9",  "9"},
            {InputAction::ToolbarSlot10,"actions", "toolbar_slot_10","Hotbar slot 10", "0"},
            // camera
            {InputAction::CameraToggle, "camera", "camera_toggle",   "1st / 3rd person", "F5"},
            // dev
            {InputAction::ToggleFly,    "dev", "toggle_fly",         "Fly (Test Play)",  "V"},
            {InputAction::ToggleDebug,  "dev", "toggle_debug",       "Debug overlay",    "F3"},
            {InputAction::ToggleConsole,"dev", "toggle_console",     "Console",          "GRAVE"},
            {InputAction::GamemodeCycle,"dev", "gamemode_cycle",     "Next game mode",   "F4"},
            {InputAction::ReloadRegistry,"dev","reload_registry",    "Reload blocks/items", "F6"},
            {InputAction::ReloadPack,   "dev", "reload_pack",        "Reload resource pack", "F7"},
            {InputAction::PickBlock,    "dev", "pick_block",         "Pick block",       "MOUSE_MIDDLE"},
            {InputAction::Undo,         "dev", "undo",               "Undo edit",        "Z"},
            {InputAction::Redo,         "dev", "redo",               "Redo edit",        "Y"},
            {InputAction::BrushBigger,  "dev", "brush_bigger",       "Brush bigger",     "EQUAL"},
            {InputAction::BrushSmaller, "dev", "brush_smaller",      "Brush smaller",    "MINUS"},
            {InputAction::NudgeForward, "dev", "nudge_forward",      "Nudge paste away / size up", "UP"},
            {InputAction::NudgeBack,    "dev", "nudge_back",         "Nudge paste closer / size down", "DOWN"},
            {InputAction::NudgeLeft,    "dev", "nudge_left",         "Nudge paste left", "LEFT"},
            {InputAction::NudgeRight,   "dev", "nudge_right",        "Nudge paste right","RIGHT"},
            {InputAction::NudgeUp,      "dev", "nudge_up",           "Nudge paste up",   "PAGE_UP"},
            {InputAction::NudgeDown,    "dev", "nudge_down",         "Nudge paste down", "PAGE_DOWN"},
            {InputAction::ToolCancel,   "dev", "tool_cancel",        "Release paste / cancel", "BACKSPACE"},
            {InputAction::RotatePiece,  "actions", "rotate_piece",   "Turn structure (hammer)", "T"},
            // system
            {InputAction::Pause,        "system", "pause",           "Pause menu",       "ESCAPE"},
            {InputAction::Screenshot,   "system", "screenshot",      "Screenshot",       "F2"},
        };
        return table;
    }

    // Actions that are MEANT to share a key (one button,
    // two jobs) — the Controls screen does not flag them
    static bool allowedToShare(InputAction a, InputAction b) {
        auto pair = [&](InputAction x, InputAction y) {
            return (a == x && b == y) || (a == y && b == x);
        };
        return pair(InputAction::Sprint, InputAction::Roll);
    }

    bool loadKeyboard(const std::filesystem::path& path) {
        keyboardPath = path;
        resetToDefaults();           // anything missing keeps its default
        if (!std::filesystem::exists(path)) {
            std::cerr << "Keyboard config not found: " << path
                      << " (using defaults)\n";
            return false;
        }
        try {
            auto tbl = toml::parse_file(path.string());
            for (const ActionInfo& info : actionTable()) {
                auto* sec = tbl[info.section].as_table();
                if (!sec) continue;
                auto node = (*sec)[info.key];
                if (!node) continue;
                InputBinding b;
                if (parseBinding(node.value_or(std::string()), b))
                    bindings[info.action] = b;
            }
            std::cout << "Keyboard bindings loaded: " << path << "\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Keyboard config error: " << e.what() << "\n";
            return false;
        }
    }

    // Write every binding back to keyboard.toml
    bool saveKeyboard() const {
        if (keyboardPath.empty()) return false;
        std::ofstream f(keyboardPath);
        if (!f) return false;
        f << "# VoxelRPG Keyboard + Mouse Controls\n"
          << "# Change these in game (Esc -> Controls) or edit here.\n"
          << "# Keys: W, SPACE, LEFT_SHIFT, F5, UP, PAGE_UP, GRAVE ...\n"
          << "# Mouse: MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE,\n"
          << "#        MOUSE_SCROLL_UP, MOUSE_SCROLL_DOWN\n";
        std::string section;
        for (const ActionInfo& info : actionTable()) {
            if (section != info.section) {
                section = info.section;
                f << "\n[" << section << "]\n";
            }
            std::string key = info.key;
            f << key << std::string(key.size() < 16 ? 16 - key.size() : 1, ' ')
              << "= \"" << bindingName(getBinding(info.action)) << "\""
              << "   # " << info.label << "\n";
        }
        return (bool)f;
    }

    void resetToDefaults() {
        bindings.clear();
        for (const ActionInfo& info : actionTable()) {
            InputBinding b;
            if (parseBinding(info.defaultBind, b)) bindings[info.action] = b;
        }
    }

    InputBinding getBinding(InputAction action) const {
        auto it = bindings.find(action);
        return it != bindings.end() ? it->second : InputBinding();
    }

    void rebind(InputAction action, InputBinding binding) {
        bindings[action] = binding;
    }

    // ── Per frame ─────────────────────────────────────────
    // Call once per frame, after glfwPollEvents()
    void poll(GLFWwindow* window) {
        prevState = currState;

        // Scroll arrives through the callback during
        // glfwPollEvents(). Take this frame's total, then
        // start counting afresh for the next frame.
        frameScroll  = scrollAccum;
        scrollAccum  = 0.0f;

        for (auto& [action, binding] : bindings) {
            int idx = (int)action;
            if (idx < 0 || idx >= (int)InputAction::COUNT) continue;
            switch (binding.type) {
            case InputBinding::Type::Key:
                currState[idx] = binding.code != GLFW_KEY_UNKNOWN &&
                    glfwGetKey(window, binding.code) == GLFW_PRESS;
                break;
            case InputBinding::Type::MouseButton:
                currState[idx] =
                    glfwGetMouseButton(window, binding.code) == GLFW_PRESS;
                break;
            // A wheel notch is a fresh press every frame it
            // happens, so fast scrolling steps through slots
            case InputBinding::Type::MouseScrollUp:
                currState[idx] = frameScroll > 0.0f;
                prevState[idx] = false;
                break;
            case InputBinding::Type::MouseScrollDown:
                currState[idx] = frameScroll < 0.0f;
                prevState[idx] = false;
                break;
            default:
                currState[idx] = false;
                break;
            }
        }

        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        mouseDelta.x = (float)(mx - lastMouseX);
        mouseDelta.y = (float)(my - lastMouseY);
        lastMouseX = mx;
        lastMouseY = my;
    }

    bool isHeld(InputAction action) const {
        int idx = (int)action;
        if (idx < 0 || idx >= (int)InputAction::COUNT) return false;
        return currState[idx];
    }
    bool isPressed(InputAction action) const {
        int idx = (int)action;
        if (idx < 0 || idx >= (int)InputAction::COUNT) return false;
        return currState[idx] && !prevState[idx];
    }
    bool isReleased(InputAction action) const {
        int idx = (int)action;
        if (idx < 0 || idx >= (int)InputAction::COUNT) return false;
        return !currState[idx] && prevState[idx];
    }

    glm::vec2 getMouseDelta()  const { return mouseDelta; }
    float     getScrollDelta() const { return frameScroll; }

    // GLFW scroll callback
    void onScroll(float yOffset) { scrollAccum += yOffset; }

    // GLFW char callback (text input)
    void onChar(unsigned int codepoint) { lastChar = codepoint; hasChar = true; }
    bool consumeChar(unsigned int& out) {
        if (!hasChar) return false;
        out = lastChar;
        hasChar = false;
        return true;
    }

    void resetMouseDelta(GLFWwindow* window) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        lastMouseX = mx;
        lastMouseY = my;
        mouseDelta = {0, 0};
    }

    // ── Names ─────────────────────────────────────────────
    // Binding → the text used in keyboard.toml ("LEFT_SHIFT")
    static std::string bindingName(const InputBinding& b) {
        switch (b.type) {
        case InputBinding::Type::MouseScrollUp:   return "MOUSE_SCROLL_UP";
        case InputBinding::Type::MouseScrollDown: return "MOUSE_SCROLL_DOWN";
        case InputBinding::Type::MouseButton:
            if (b.code == GLFW_MOUSE_BUTTON_LEFT)   return "MOUSE_LEFT";
            if (b.code == GLFW_MOUSE_BUTTON_RIGHT)  return "MOUSE_RIGHT";
            if (b.code == GLFW_MOUSE_BUTTON_MIDDLE) return "MOUSE_MIDDLE";
            return "MOUSE_" + std::to_string(b.code + 1);
        case InputBinding::Type::Key: {
            if (b.code >= GLFW_KEY_A && b.code <= GLFW_KEY_Z)
                return std::string(1, (char)('A' + b.code - GLFW_KEY_A));
            if (b.code >= GLFW_KEY_0 && b.code <= GLFW_KEY_9)
                return std::string(1, (char)('0' + b.code - GLFW_KEY_0));
            if (b.code >= GLFW_KEY_F1 && b.code <= GLFW_KEY_F12)
                return "F" + std::to_string(b.code - GLFW_KEY_F1 + 1);
            for (auto& [name, code] : namedKeys())
                if (code == b.code) return name;
            return "UNKNOWN";
        }
        default: return "NONE";
        }
    }

    // Shorter form for the Controls screen
    static std::string bindingLabel(const InputBinding& b) {
        std::string n = bindingName(b);
        if (n == "MOUSE_SCROLL_UP")   return "WHEEL UP";
        if (n == "MOUSE_SCROLL_DOWN") return "WHEEL DOWN";
        if (n == "MOUSE_LEFT")        return "LEFT CLICK";
        if (n == "MOUSE_RIGHT")       return "RIGHT CLICK";
        if (n == "MOUSE_MIDDLE")      return "MIDDLE CLICK";
        if (n == "GRAVE")             return "` (GRAVE)";
        for (char& c : n) if (c == '_') c = ' ';
        return n;
    }

    // Text from keyboard.toml → binding. False if unknown.
    static bool parseBinding(const std::string& s, InputBinding& out) {
        out = InputBinding();
        if (s == "MOUSE_SCROLL_UP")   { out.type = InputBinding::Type::MouseScrollUp;   return true; }
        if (s == "MOUSE_SCROLL_DOWN") { out.type = InputBinding::Type::MouseScrollDown; return true; }
        if (s == "MOUSE_LEFT")   { out.type = InputBinding::Type::MouseButton; out.code = GLFW_MOUSE_BUTTON_LEFT;   return true; }
        if (s == "MOUSE_RIGHT")  { out.type = InputBinding::Type::MouseButton; out.code = GLFW_MOUSE_BUTTON_RIGHT;  return true; }
        if (s == "MOUSE_MIDDLE") { out.type = InputBinding::Type::MouseButton; out.code = GLFW_MOUSE_BUTTON_MIDDLE; return true; }
        if (s.rfind("MOUSE_", 0) == 0 && s.size() > 6 && std::isdigit((unsigned char)s[6])) {
            out.type = InputBinding::Type::MouseButton;
            out.code = std::atoi(s.c_str() + 6) - 1;
            return true;
        }
        int code = keyCode(s);
        if (code == GLFW_KEY_UNKNOWN) return false;
        out.type = InputBinding::Type::Key;
        out.code = code;
        return true;
    }

    static int keyCode(const std::string& name) {
        if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
            return GLFW_KEY_A + (name[0] - 'A');
        if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
            return GLFW_KEY_0 + (name[0] - '0');
        if (name.size() >= 2 && name[0] == 'F' && std::isdigit((unsigned char)name[1])) {
            int n = std::atoi(name.c_str() + 1);
            if (n >= 1 && n <= 12) return GLFW_KEY_F1 + n - 1;
        }
        for (auto& [n, code] : namedKeys())
            if (n == name) return code;
        return GLFW_KEY_UNKNOWN;
    }

private:
    std::unordered_map<InputAction, InputBinding> bindings;
    std::filesystem::path keyboardPath;

    std::array<bool, (int)InputAction::COUNT> currState{};
    std::array<bool, (int)InputAction::COUNT> prevState{};

    glm::vec2 mouseDelta  = {0, 0};
    float     scrollAccum = 0.0f;   // collected by the callback
    float     frameScroll = 0.0f;   // this frame's total
    double    lastMouseX  = 0;
    double    lastMouseY  = 0;

    unsigned int lastChar = 0;
    bool         hasChar  = false;

    static const std::vector<std::pair<std::string, int>>& namedKeys() {
        static const std::vector<std::pair<std::string, int>> keys = {
            {"SPACE", GLFW_KEY_SPACE}, {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT},
            {"RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT}, {"LEFT_CONTROL", GLFW_KEY_LEFT_CONTROL},
            {"RIGHT_CONTROL", GLFW_KEY_RIGHT_CONTROL}, {"LEFT_ALT", GLFW_KEY_LEFT_ALT},
            {"RIGHT_ALT", GLFW_KEY_RIGHT_ALT}, {"TAB", GLFW_KEY_TAB},
            {"ESCAPE", GLFW_KEY_ESCAPE}, {"ENTER", GLFW_KEY_ENTER},
            {"BACKSPACE", GLFW_KEY_BACKSPACE}, {"GRAVE", GLFW_KEY_GRAVE_ACCENT},
            {"LEFT_BRACKET", GLFW_KEY_LEFT_BRACKET}, {"RIGHT_BRACKET", GLFW_KEY_RIGHT_BRACKET},
            {"MINUS", GLFW_KEY_MINUS}, {"EQUAL", GLFW_KEY_EQUAL},
            {"COMMA", GLFW_KEY_COMMA}, {"PERIOD", GLFW_KEY_PERIOD},
            {"SLASH", GLFW_KEY_SLASH}, {"BACKSLASH", GLFW_KEY_BACKSLASH},
            {"SEMICOLON", GLFW_KEY_SEMICOLON}, {"APOSTROPHE", GLFW_KEY_APOSTROPHE},
            {"UP", GLFW_KEY_UP}, {"DOWN", GLFW_KEY_DOWN},
            {"LEFT", GLFW_KEY_LEFT}, {"RIGHT", GLFW_KEY_RIGHT},
            {"PAGE_UP", GLFW_KEY_PAGE_UP}, {"PAGE_DOWN", GLFW_KEY_PAGE_DOWN},
            {"HOME", GLFW_KEY_HOME}, {"END", GLFW_KEY_END},
            {"INSERT", GLFW_KEY_INSERT}, {"DELETE", GLFW_KEY_DELETE},
            {"CAPS_LOCK", GLFW_KEY_CAPS_LOCK},
            {"KP_0", GLFW_KEY_KP_0}, {"KP_1", GLFW_KEY_KP_1}, {"KP_2", GLFW_KEY_KP_2},
            {"KP_3", GLFW_KEY_KP_3}, {"KP_4", GLFW_KEY_KP_4}, {"KP_5", GLFW_KEY_KP_5},
            {"KP_6", GLFW_KEY_KP_6}, {"KP_7", GLFW_KEY_KP_7}, {"KP_8", GLFW_KEY_KP_8},
            {"KP_9", GLFW_KEY_KP_9}, {"KP_ENTER", GLFW_KEY_KP_ENTER},
        };
        return keys;
    }
};

extern InputManager gInput;
