#pragma once

#include "app_state.hpp"
#include "frame_pacer.hpp"
#include "game_ids.hpp"
#include "gamepad_input.hpp"
#include "libretro_core.hpp"
#include "sdl_audio_output.hpp"

namespace cw {

// Recomputes the hash when another game is loaded (0.3 ms for a PS1 disc), and looks up the
// game ID stored for it.
void update_game_identity(UiState& ui, const LibretroCore& core, const GameIds& ids);
// Appended to the Status window: the hash, and the game ID the user types once per hash.
void draw_game_identity(UiState& ui, GameIds& ids, Notice& notice);
void draw_status_window(const LibretroCore& core, const UiState& ui, const SdlAudioOutput& audio,
                        double queued_audio_seconds, const FramePacer& pacer,
                        const JoypadState& input, const Gamepads& gamepads);

} // namespace cw
