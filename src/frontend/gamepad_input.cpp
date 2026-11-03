#include "gamepad_input.hpp"

#include <imgui.h>
#include <libretro.h>
#include <string_view>

namespace cw {

void open_gamepad(SDL_JoystickID id, Gamepads& gamepads) {
    if (gamepads.contains(id) || !SDL_IsGamepad(id)) {
        return;
    }
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (!handle) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Could not open gamepad %d: %s", id, SDL_GetError());
        return;
    }
    const char* name = SDL_GetGamepadName(handle);
    gamepads.emplace(id, OpenGamepad{handle, name ? name : "Unknown gamepad"});
    SDL_Log("Opened gamepad %s (id %d)", name ? name : "Unknown gamepad", id);
}

void close_gamepad(SDL_JoystickID id, Gamepads& gamepads, JoypadState& input) {
    input.remove_source(gamepad_input_source(id));
    const auto found = gamepads.find(id);
    if (found == gamepads.end()) {
        return;
    }
    SDL_CloseGamepad(found->second.handle);
    SDL_Log("Closed gamepad %s (id %d)", found->second.name.c_str(), id);
    gamepads.erase(found);
}

void open_connected_gamepads(Gamepads& gamepads) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids && count > 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Could not enumerate gamepads: %s", SDL_GetError());
        return;
    }
    for (int i = 0; i < count; ++i) {
        open_gamepad(ids[i], gamepads);
    }
    SDL_free(ids);
}

void update_input(JoypadState& input, const Gamepads& gamepads, bool window_focused,
                  bool imgui_wants_keyboard) {
    if (!window_focused) {
        input.release_all();
        return;
    }

    if (keyboard_input_allowed(window_focused, imgui_wants_keyboard)) {
        int key_count = 0;
        const bool* keys = SDL_GetKeyboardState(&key_count);
        uint16_t keyboard_mask = 0;
        if (keys && key_count > 0) {
            const bool control_pressed = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
            keyboard_mask = map_keyboard_state(
                std::span<const bool>(keys, static_cast<size_t>(key_count)), control_pressed);
        }
        input.set_source(kKeyboardInputSource, keyboard_mask);
    } else {
        input.remove_source(kKeyboardInputSource);
    }

    for (const auto& [id, gamepad] : gamepads) {
        GamepadSnapshot snapshot;
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
            snapshot.buttons[static_cast<size_t>(button)] =
                SDL_GetGamepadButton(gamepad.handle, static_cast<SDL_GamepadButton>(button));
        }
        snapshot.left_trigger = SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        snapshot.right_trigger = SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        input.set_source(gamepad_input_source(id), map_gamepad_state(snapshot));
    }
}

std::string pressed_button_names(uint16_t mask) {
    std::string names;
    for (unsigned id = 0; id < 16; ++id) {
        if ((mask & static_cast<uint16_t>(1U << id)) == 0) {
            continue;
        }
        if (!names.empty()) {
            names += ", ";
        }
        names += joypad_button_name(id);
    }
    return names.empty() ? "none" : names;
}

std::string gamepad_names(const Gamepads& gamepads) {
    std::string names;
    for (const auto& [id, gamepad] : gamepads) {
        (void)id;
        if (!names.empty()) {
            names += ", ";
        }
        names += gamepad.name;
    }
    return names.empty() ? "none" : names;
}

} // namespace cw
