#include "status_window.hpp"

#include "cw/hash/game_hash.hpp"
#include "ui_scale.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <imgui.h>
#include <optional>
#include <rc_consoles.h>

namespace cw {
namespace {

// Console and memory regions as RetroAchievements sees them (the memory viewer comes in 2.2).
void draw_memory_summary(const LibretroCore& core) {
    const MemoryReader& memory = core.memory();
    if (memory.size() == 0) {
        ImGui::TextDisabled("Memory: not mapped");
        return;
    }
    ImGui::Text("Memory: %s, %u bytes", rc_console_name(core.console_id()), memory.size());
    for (const MemoryRegionInfo& region : memory.regions()) {
        ImGui::BulletText("0x%06X-0x%06X %s%s%s", region.start, region.end,
                          region.description.c_str(), region.available ? "" : " (unavailable)",
                          region.available && !region.writable ? " (read-only)" : "");
    }
}

} // namespace

// Recomputes the hash when another game is loaded (0.3 ms for a PS1 disc), and looks up the
// game ID stored for it.
void update_game_identity(UiState& ui, const LibretroCore& core, const GameIds& ids) {
    // Keyed on the session too, not just the path: reopening a file that was replaced under the
    // same name must hash the new content.
    if (core.game_path() == ui.identified_game_path &&
        ui.identified_generation == ui.game_session_generation) {
        return;
    }
    ui.identified_game_path = core.game_path();
    ui.identified_generation = ui.game_session_generation;
    ui.game_hash.clear();
    ui.game_hash_error.clear();
    ui.game_id.reset();
    ui.game_id_input[0] = '\0';
    if (ui.identified_game_path.empty()) {
        return;
    }
    if (core.console_id() == RC_CONSOLE_UNKNOWN) {
        ui.game_hash_error = "Unknown console for this core: no RetroAchievements hash";
        return;
    }
    if (const auto hash =
            compute_game_hash(ui.identified_game_path, core.console_id(), ui.game_hash_error)) {
        ui.game_hash = *hash;
        ui.game_id = ids.find(*hash);
        if (ui.game_id) {
            std::snprintf(ui.game_id_input, sizeof(ui.game_id_input), "%u", *ui.game_id);
        }
        SDL_Log("Game hash %s, game ID %s", hash->c_str(),
                ui.game_id ? std::to_string(*ui.game_id).c_str() : "not set");
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Game hash: %s", ui.game_hash_error.c_str());
    }
}

// Appended to the Status window: the hash, and the game ID the user types once per hash.
void draw_game_identity(UiState& ui, GameIds& ids, Notice& notice) {
    if (ui.identified_game_path.empty()) {
        return;
    }
    ImGui::Begin("Status");
    ImGui::Separator();
    if (ui.game_hash.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Hash: %s", ui.game_hash_error.c_str());
        ImGui::End();
        return;
    }
    ImGui::Text("Hash: %s", ui.game_hash.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(ui.game_hash.c_str());
    }
    ImGui::SetNextItemWidth(ui_px(100.0f));
    ImGui::InputText("##game_id", ui.game_id_input, sizeof(ui.game_id_input),
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button("Save game ID")) {
        const unsigned long value = std::strtoul(ui.game_id_input, nullptr, 10);
        std::string error;
        if (value > UINT32_MAX) {
            show_notice(notice, "Game ID too large", false);
        } else if (ids.set(ui.game_hash, static_cast<uint32_t>(value), error)) {
            ui.game_id = value == 0 ? std::nullopt : std::optional<uint32_t>(value);
            show_notice(notice, value == 0 ? "Game ID cleared" : "Game ID saved", true);
        } else {
            show_notice(notice, error, false);
        }
    }
    if (ui.game_id) {
        ImGui::Text("Game ID: %u", *ui.game_id);
    } else {
        ImGui::TextDisabled("Game ID not set: type the number from the game's page on");
        ImGui::TextDisabled("retroachievements.org (e.g. 18238) and save it once.");
    }
    ImGui::End();
}

void draw_status_window(const LibretroCore& core, const UiState& ui, const SdlAudioOutput& audio,
                        double queued_audio_seconds, const FramePacer& pacer,
                        const JoypadState& input, const Gamepads& gamepads) {
    ImGui::Begin("Status");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("cheevo-workbench %s", CW_VERSION);
    ImGui::Separator();

    if (!ui.status_message.empty()) {
        const ImVec4 color =
            ui.status_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", ui.status_message.c_str());
    }
    if (!core.is_loaded()) {
        ImGui::TextDisabled("No core loaded.");
    } else {
        const auto& info = core.system_info();
        ImGui::Text("Core: %s %s", info.name.c_str(), info.version.c_str());
        ImGui::Text("BIOS directory: %s", core.system_directory().c_str());
        ImGui::Text("Core options: %zu", core.options().options().size());
        if (!core.is_game_loaded()) {
            ImGui::TextDisabled("No game loaded.");
        } else {
            const retro_system_av_info& av = core.av_info();
            ImGui::Text("Geometry: %ux%u (max %ux%u)", av.geometry.base_width,
                        av.geometry.base_height, av.geometry.max_width, av.geometry.max_height);
            ImGui::Text("Last frame: %ux%u, aspect %.3f", core.last_frame_width(),
                        core.last_frame_height(), av.geometry.aspect_ratio);
            ImGui::Text("Images received: %llu",
                        static_cast<unsigned long long>(core.video_frame().serial));
            ImGui::Text("Core timing: %.3f fps, audio %.0f Hz", av.timing.fps,
                        av.timing.sample_rate);
            ImGui::Text("Frames run: %llu", static_cast<unsigned long long>(core.frame_count()));
            ImGui::Text("Measured FPS: %.2f", pacer.measured_fps());
            ImGui::Text("Session: %s", ui.session.paused() ? "Paused" : "Running");
            draw_memory_summary(core);
        }
    }

    ImGui::Separator();
    const std::string audio_status = audio.status_text();
    if (audio.has_error()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Audio: %s", audio_status.c_str());
    } else {
        ImGui::Text("Audio: %s", audio_status.c_str());
    }
    ImGui::Text("Audio queue: %.1f ms", queued_audio_seconds * 1000.0);
    ImGui::Text("Frame pacing: %s", pacer.mode() == FramePacer::Mode::Audio ? "audio" : "clock");
    const std::string connected_gamepads = gamepad_names(gamepads);
    const std::string pressed_buttons = pressed_button_names(input.buttons());
    ImGui::Text("Gamepads: %s", connected_gamepads.c_str());
    ImGui::Text("Pressed: %s", pressed_buttons.c_str());
    ImGui::Text("UI: %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::TextDisabled("P pause, N frame advance, Ctrl+R reset");
    ImGui::TextDisabled("Shift+F1-F4 save, F1-F4 load states");
    ImGui::PopTextWrapPos();
    ImGui::End();
}

} // namespace cw
