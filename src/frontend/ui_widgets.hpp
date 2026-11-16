#pragma once

#include "cw/devtools/code_notes.hpp"
#include "cw/memory/memory.hpp"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

// Small widgets and helpers shared by the tool windows.
namespace cw {

// Short message drawn over the game for a moment (e.g. "Saved slot 1"); also logged.
struct Notice {
    std::string text;
    std::chrono::steady_clock::time_point until{};
};

constexpr std::array<std::pair<MemoryValueSize, const char*>, 14> kMemoryValueSizes = {{
    {MemoryValueSize::Bit0, "Bit 0"},
    {MemoryValueSize::Bit1, "Bit 1"},
    {MemoryValueSize::Bit2, "Bit 2"},
    {MemoryValueSize::Bit3, "Bit 3"},
    {MemoryValueSize::Bit4, "Bit 4"},
    {MemoryValueSize::Bit5, "Bit 5"},
    {MemoryValueSize::Bit6, "Bit 6"},
    {MemoryValueSize::Bit7, "Bit 7"},
    {MemoryValueSize::LowNibble, "Low nibble"},
    {MemoryValueSize::HighNibble, "High nibble"},
    {MemoryValueSize::EightBit, "8-bit"},
    {MemoryValueSize::SixteenBit, "16-bit"},
    {MemoryValueSize::TwentyFourBit, "24-bit"},
    {MemoryValueSize::ThirtyTwoBit, "32-bit"},
}};

constexpr bool has_byte_order(MemoryValueSize size) {
    return size == MemoryValueSize::SixteenBit || size == MemoryValueSize::TwentyFourBit ||
           size == MemoryValueSize::ThirtyTwoBit;
}

void show_notice(Notice& notice, const std::string& text, bool ok);

const char* memory_value_size_label(MemoryValueSize size);
bool parse_hex_value(std::string_view text, uint32_t& value);
bool directory_exists(const std::filesystem::path& path);
// Remembers a directory in `filename` under the configuration directory. An empty path clears it.
bool save_directory_setting(const std::filesystem::path& config_dir, const char* filename,
                            const std::string& path_text, std::filesystem::path& saved_path,
                            std::string& error);

bool input_text_string(const char* label, std::string& value, ImGuiInputTextFlags flags = 0);
bool input_text_multiline_string(const char* label, std::string& value, const ImVec2& size,
                                 ImGuiInputTextFlags flags = 0);

void set_bookmark_message(std::string& message, bool& is_error, bool succeeded,
                          const std::string& error, std::string_view success, bool has_game_id);
void draw_note_preview(const CodeNote* note);
void draw_code_note_load_errors(const CodeNotes& notes);

} // namespace cw
