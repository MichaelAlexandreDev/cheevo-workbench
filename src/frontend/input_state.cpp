#include "input_state.hpp"

#include <libretro.h>

#include <array>
#include <bit>

namespace cw {

namespace {

constexpr std::array<KeyboardBinding, 16> kKeyboardBindings = {
    KeyboardBinding{SDL_SCANCODE_Z, RETRO_DEVICE_ID_JOYPAD_B},
    KeyboardBinding{SDL_SCANCODE_A, RETRO_DEVICE_ID_JOYPAD_Y},
    KeyboardBinding{SDL_SCANCODE_RSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT},
    KeyboardBinding{SDL_SCANCODE_RETURN, RETRO_DEVICE_ID_JOYPAD_START},
    KeyboardBinding{SDL_SCANCODE_UP, RETRO_DEVICE_ID_JOYPAD_UP},
    KeyboardBinding{SDL_SCANCODE_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
    KeyboardBinding{SDL_SCANCODE_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT},
    KeyboardBinding{SDL_SCANCODE_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
    KeyboardBinding{SDL_SCANCODE_X, RETRO_DEVICE_ID_JOYPAD_A},
    KeyboardBinding{SDL_SCANCODE_S, RETRO_DEVICE_ID_JOYPAD_X},
    KeyboardBinding{SDL_SCANCODE_Q, RETRO_DEVICE_ID_JOYPAD_L},
    KeyboardBinding{SDL_SCANCODE_W, RETRO_DEVICE_ID_JOYPAD_R},
    KeyboardBinding{SDL_SCANCODE_E, RETRO_DEVICE_ID_JOYPAD_L2},
    KeyboardBinding{SDL_SCANCODE_R, RETRO_DEVICE_ID_JOYPAD_R2},
    KeyboardBinding{SDL_SCANCODE_1, RETRO_DEVICE_ID_JOYPAD_L3},
    KeyboardBinding{SDL_SCANCODE_2, RETRO_DEVICE_ID_JOYPAD_R3},
};

constexpr std::array<GamepadBinding, 14> kGamepadBindings = {
    GamepadBinding{SDL_GAMEPAD_BUTTON_SOUTH, RETRO_DEVICE_ID_JOYPAD_B},
    GamepadBinding{SDL_GAMEPAD_BUTTON_WEST, RETRO_DEVICE_ID_JOYPAD_Y},
    GamepadBinding{SDL_GAMEPAD_BUTTON_BACK, RETRO_DEVICE_ID_JOYPAD_SELECT},
    GamepadBinding{SDL_GAMEPAD_BUTTON_START, RETRO_DEVICE_ID_JOYPAD_START},
    GamepadBinding{SDL_GAMEPAD_BUTTON_DPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP},
    GamepadBinding{SDL_GAMEPAD_BUTTON_DPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
    GamepadBinding{SDL_GAMEPAD_BUTTON_DPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT},
    GamepadBinding{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
    GamepadBinding{SDL_GAMEPAD_BUTTON_EAST, RETRO_DEVICE_ID_JOYPAD_A},
    GamepadBinding{SDL_GAMEPAD_BUTTON_NORTH, RETRO_DEVICE_ID_JOYPAD_X},
    GamepadBinding{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_L},
    GamepadBinding{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_R},
    GamepadBinding{SDL_GAMEPAD_BUTTON_LEFT_STICK, RETRO_DEVICE_ID_JOYPAD_L3},
    GamepadBinding{SDL_GAMEPAD_BUTTON_RIGHT_STICK, RETRO_DEVICE_ID_JOYPAD_R3},
};

constexpr std::array<const char*, 16> kJoypadButtonNames = {
    "B", "Y", "Select", "Start", "Up", "Down", "Left", "Right",
    "A", "X", "L",      "R",     "L2", "R2",   "L3",   "R3",
};

constexpr uint64_t kGamepadSourceTag = uint64_t{1} << 32;
constexpr Sint16 kTriggerThreshold = 16383;

} // namespace

std::span<const KeyboardBinding> keyboard_bindings() {
    return kKeyboardBindings;
}

std::span<const GamepadBinding> gamepad_bindings() {
    return kGamepadBindings;
}

uint16_t map_keyboard_state(std::span<const bool> keys, bool control_pressed) {
    auto held = [&keys](SDL_Scancode key) {
        const auto index = static_cast<size_t>(key);
        return index < keys.size() && keys[index];
    };
    // Shift+F1..F4 saves a state (main.cpp). While one of those F keys is down, Shift is the
    // hotkey's modifier: without this, saving with Right Shift would also press Select.
    const bool state_hotkey_held = held(SDL_SCANCODE_F1) || held(SDL_SCANCODE_F2) ||
                                   held(SDL_SCANCODE_F3) || held(SDL_SCANCODE_F4);
    uint16_t result = 0;
    for (const KeyboardBinding& binding : kKeyboardBindings) {
        if (control_pressed && (binding.key == SDL_SCANCODE_R || binding.key == SDL_SCANCODE_Q)) {
            continue; // Ctrl+R and Ctrl+Q are frontend shortcuts, not game buttons.
        }
        const bool is_modifier =
            binding.key == SDL_SCANCODE_LSHIFT || binding.key == SDL_SCANCODE_RSHIFT;
        if (held(binding.key) && !(is_modifier && state_hotkey_held)) {
            result |= static_cast<uint16_t>(1U << binding.joypad_id);
        }
    }
    return result;
}

uint16_t map_gamepad_state(const GamepadSnapshot& state) {
    uint16_t result = 0;
    for (const GamepadBinding& binding : kGamepadBindings) {
        if (state.buttons[static_cast<size_t>(binding.button)]) {
            result |= static_cast<uint16_t>(1U << binding.joypad_id);
        }
    }
    if (state.left_trigger > kTriggerThreshold) {
        result |= static_cast<uint16_t>(1U << RETRO_DEVICE_ID_JOYPAD_L2);
    }
    if (state.right_trigger > kTriggerThreshold) {
        result |= static_cast<uint16_t>(1U << RETRO_DEVICE_ID_JOYPAD_R2);
    }
    return result;
}

bool keyboard_input_allowed(bool window_focused, bool imgui_wants_keyboard) {
    return window_focused && !imgui_wants_keyboard;
}

uint64_t gamepad_input_source(SDL_JoystickID instance_id) {
    return kGamepadSourceTag | static_cast<uint32_t>(instance_id);
}

const char* joypad_button_name(unsigned id) {
    return id < kJoypadButtonNames.size() ? kJoypadButtonNames[id] : "Unknown";
}

void JoypadState::set_source(uint64_t source_id, uint16_t buttons) {
    sources_[source_id] = buttons;
}

void JoypadState::remove_source(uint64_t source_id) {
    sources_.erase(source_id);
}

void JoypadState::release_all() {
    sources_.clear();
}

uint16_t JoypadState::buttons() const {
    uint16_t result = 0;
    for (const auto& [source, mask] : sources_) {
        (void)source;
        result |= mask;
    }
    return result;
}

int16_t JoypadState::input_state(unsigned port, unsigned device, unsigned index,
                                 unsigned id) const {
    if (port != 0 || device != RETRO_DEVICE_JOYPAD || index != 0) {
        return 0;
    }
    const uint16_t state = buttons();
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        return std::bit_cast<int16_t>(state);
    }
    if (id <= RETRO_DEVICE_ID_JOYPAD_R3) {
        return static_cast<int16_t>((state >> id) & 1U);
    }
    return 0;
}

} // namespace cw
