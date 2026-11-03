#pragma once

#include "input_state.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace cw {

struct OpenGamepad {
    SDL_Gamepad* handle = nullptr;
    std::string name;
};

using Gamepads = std::unordered_map<SDL_JoystickID, OpenGamepad>;

void open_gamepad(SDL_JoystickID id, Gamepads& gamepads);
void close_gamepad(SDL_JoystickID id, Gamepads& gamepads, JoypadState& input);
void open_connected_gamepads(Gamepads& gamepads);
void update_input(JoypadState& input, const Gamepads& gamepads, bool window_focused,
                  bool imgui_wants_keyboard);
std::string pressed_button_names(uint16_t mask);
std::string gamepad_names(const Gamepads& gamepads);

} // namespace cw
