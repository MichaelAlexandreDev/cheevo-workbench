#pragma once

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace cw {

inline constexpr uint64_t kKeyboardInputSource = 0;

struct KeyboardBinding {
    SDL_Scancode key;
    unsigned joypad_id;
};

struct GamepadBinding {
    SDL_GamepadButton button;
    unsigned joypad_id;
};

struct GamepadSnapshot {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    Sint16 left_trigger = 0;
    Sint16 right_trigger = 0;
};

std::span<const KeyboardBinding> keyboard_bindings();
std::span<const GamepadBinding> gamepad_bindings();
uint16_t map_keyboard_state(std::span<const bool> keys, bool control_pressed = false);
uint16_t map_gamepad_state(const GamepadSnapshot& state);
bool keyboard_input_allowed(bool window_focused, bool imgui_wants_keyboard);
uint64_t gamepad_input_source(SDL_JoystickID instance_id);
const char* joypad_button_name(unsigned id);

class JoypadState {
  public:
    void set_source(uint64_t source_id, uint16_t buttons);
    void remove_source(uint64_t source_id);
    void release_all();

    uint16_t buttons() const;
    int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) const;

  private:
    std::unordered_map<uint64_t, uint16_t> sources_;
};

} // namespace cw
