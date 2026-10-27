#include "input_state.hpp"

#include <doctest/doctest.h>
#include <libretro.h>

#include <array>
#include <cstdint>

namespace {

uint16_t button_bit(unsigned id) {
    return static_cast<uint16_t>(1U << id);
}

} // namespace

TEST_CASE("Keyboard bindings map every configured scancode to its libretro button") {
    std::array<bool, SDL_SCANCODE_COUNT> keys{};

    for (const cw::KeyboardBinding binding : cw::keyboard_bindings()) {
        CAPTURE(binding.key);
        CAPTURE(binding.joypad_id);
        keys.fill(false);
        keys[static_cast<size_t>(binding.key)] = true;
        CHECK(cw::map_keyboard_state(keys) == button_bit(binding.joypad_id));
    }

    CHECK(cw::map_keyboard_state(std::span<const bool>(keys.data(), 0)) == 0);
}

TEST_CASE("Gamepad buttons and triggers map to the RetroPad layout") {
    for (const cw::GamepadBinding binding : cw::gamepad_bindings()) {
        CAPTURE(binding.button);
        CAPTURE(binding.joypad_id);
        cw::GamepadSnapshot state;
        state.buttons[static_cast<size_t>(binding.button)] = true;
        CHECK(cw::map_gamepad_state(state) == button_bit(binding.joypad_id));
    }

    cw::GamepadSnapshot triggers;
    triggers.left_trigger = 16383;
    triggers.right_trigger = 16383;
    CHECK(cw::map_gamepad_state(triggers) == 0);
    triggers.left_trigger = 16384;
    CHECK(cw::map_gamepad_state(triggers) == button_bit(RETRO_DEVICE_ID_JOYPAD_L2));
    triggers.left_trigger = 0;
    triggers.right_trigger = 16384;
    CHECK(cw::map_gamepad_state(triggers) == button_bit(RETRO_DEVICE_ID_JOYPAD_R2));
}

TEST_CASE("Joypad sources combine, disconnect, reconnect, and release on focus loss") {
    cw::JoypadState state;
    constexpr uint64_t keyboard = cw::kKeyboardInputSource;
    constexpr uint64_t first_gamepad = 0x100000001ULL;
    constexpr uint64_t second_gamepad = 0x100000002ULL;

    state.set_source(keyboard, button_bit(RETRO_DEVICE_ID_JOYPAD_A));
    state.set_source(first_gamepad, button_bit(RETRO_DEVICE_ID_JOYPAD_B));
    CHECK(state.buttons() ==
          (button_bit(RETRO_DEVICE_ID_JOYPAD_A) | button_bit(RETRO_DEVICE_ID_JOYPAD_B)));

    state.remove_source(first_gamepad);
    CHECK(state.buttons() == button_bit(RETRO_DEVICE_ID_JOYPAD_A));

    state.set_source(first_gamepad, button_bit(RETRO_DEVICE_ID_JOYPAD_B));
    state.set_source(second_gamepad, button_bit(RETRO_DEVICE_ID_JOYPAD_START));
    CHECK(state.buttons() ==
          (button_bit(RETRO_DEVICE_ID_JOYPAD_A) | button_bit(RETRO_DEVICE_ID_JOYPAD_B) |
           button_bit(RETRO_DEVICE_ID_JOYPAD_START)));

    state.set_source(second_gamepad, button_bit(RETRO_DEVICE_ID_JOYPAD_B) |
                                         button_bit(RETRO_DEVICE_ID_JOYPAD_START));
    CHECK(state.buttons() ==
          (button_bit(RETRO_DEVICE_ID_JOYPAD_A) | button_bit(RETRO_DEVICE_ID_JOYPAD_B) |
           button_bit(RETRO_DEVICE_ID_JOYPAD_START)));

    state.remove_source(first_gamepad);
    CHECK(state.buttons() ==
          (button_bit(RETRO_DEVICE_ID_JOYPAD_A) | button_bit(RETRO_DEVICE_ID_JOYPAD_B) |
           button_bit(RETRO_DEVICE_ID_JOYPAD_START)));
    state.remove_source(second_gamepad);
    CHECK(state.buttons() == button_bit(RETRO_DEVICE_ID_JOYPAD_A));

    state.set_source(first_gamepad, button_bit(RETRO_DEVICE_ID_JOYPAD_X));
    CHECK(state.buttons() ==
          (button_bit(RETRO_DEVICE_ID_JOYPAD_A) | button_bit(RETRO_DEVICE_ID_JOYPAD_X)));
    state.release_all();
    CHECK(state.buttons() == 0);
}

TEST_CASE("JoypadState implements the libretro joypad state queries") {
    cw::JoypadState state;
    constexpr uint16_t mask = 0x8421;
    state.set_source(1, mask);

    for (unsigned id = 0; id < 16; ++id) {
        CHECK(state.input_state(0, RETRO_DEVICE_JOYPAD, 0, id) ==
              static_cast<int16_t>((mask >> id) & 1U));
    }
    CHECK(static_cast<uint16_t>(
              state.input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK)) == mask);
    CHECK(state.input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) == 0);
    CHECK(state.input_state(0, RETRO_DEVICE_ANALOG, 0, RETRO_DEVICE_ID_JOYPAD_A) == 0);
    CHECK(state.input_state(0, RETRO_DEVICE_JOYPAD, 1, RETRO_DEVICE_ID_JOYPAD_A) == 0);
    CHECK(state.input_state(0, RETRO_DEVICE_JOYPAD, 0, 16) == 0);
}

TEST_CASE("Keyboard input is gated by focus and ImGui capture") {
    CHECK(cw::keyboard_input_allowed(true, false));
    CHECK_FALSE(cw::keyboard_input_allowed(true, true));
    CHECK_FALSE(cw::keyboard_input_allowed(false, false));
    CHECK_FALSE(cw::keyboard_input_allowed(false, true));
}

TEST_CASE("Shift held for a save-state hotkey does not press Select") {
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    keys[SDL_SCANCODE_RSHIFT] = true;
    CHECK(cw::map_keyboard_state(keys) == button_bit(RETRO_DEVICE_ID_JOYPAD_SELECT));

    keys[SDL_SCANCODE_F1] = true; // Shift+F1 = save slot 1
    CHECK(cw::map_keyboard_state(keys) == 0);

    keys[SDL_SCANCODE_Z] = true; // other buttons still reach the game meanwhile
    CHECK(cw::map_keyboard_state(keys) == button_bit(RETRO_DEVICE_ID_JOYPAD_B));
}

TEST_CASE("Ctrl+R and Ctrl+Q shortcuts do not also press game buttons") {
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    keys[SDL_SCANCODE_R] = true;
    CHECK(cw::map_keyboard_state(keys) == button_bit(RETRO_DEVICE_ID_JOYPAD_R2));
    CHECK(cw::map_keyboard_state(keys, true) == 0);

    keys.fill(false);
    keys[SDL_SCANCODE_Q] = true;
    CHECK(cw::map_keyboard_state(keys) == button_bit(RETRO_DEVICE_ID_JOYPAD_L));
    CHECK(cw::map_keyboard_state(keys, true) == 0);

    keys[SDL_SCANCODE_Z] = true;
    CHECK(cw::map_keyboard_state(keys, true) == button_bit(RETRO_DEVICE_ID_JOYPAD_B));
}
