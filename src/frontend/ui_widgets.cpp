#include "ui_widgets.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>

namespace cw {
namespace {

int string_input_callback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* value = static_cast<std::string*>(data->UserData);
        value->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = value->data();
    }
    return 0;
}

} // namespace

bool input_text_string(const char* label, std::string& value, ImGuiInputTextFlags flags) {
    value.reserve(value.size() + 1);
    return ImGui::InputText(label, value.data(), value.capacity() + 1,
                            flags | ImGuiInputTextFlags_CallbackResize, string_input_callback,
                            &value);
}

bool input_text_multiline_string(const char* label, std::string& value, const ImVec2& size,
                                 ImGuiInputTextFlags flags) {
    value.reserve(value.size() + 1);
    return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, size,
                                     flags | ImGuiInputTextFlags_CallbackResize,
                                     string_input_callback, &value);
}

void set_bookmark_message(std::string& message, bool& is_error, bool succeeded,
                          const std::string& error, std::string_view success, bool has_game_id) {
    if (!succeeded) {
        message = error;
        is_error = true;
    } else if (!error.empty()) {
        message = error;
        is_error = false;
    } else {
        message = success;
        is_error = !has_game_id;
    }
}

void draw_note_preview(const CodeNote* note) {
    if (!note) {
        ImGui::TextDisabled("--");
        return;
    }
    const size_t end = note->note.find('\n');
    ImGui::TextUnformatted(note->note.data(), end == std::string::npos
                                                  ? note->note.data() + note->note.size()
                                                  : note->note.data() + end);
}

void draw_code_note_load_errors(const CodeNotes& notes) {
    const ImVec4 color(1.0f, 0.45f, 0.4f, 1.0f);
    if (!notes.user_error().empty()) {
        ImGui::TextColored(color, "%s", notes.user_error().c_str());
    }
    if (!notes.server_error().empty()) {
        ImGui::TextColored(color, "%s", notes.server_error().c_str());
    }
}

bool directory_exists(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(path, error) && !error;
}

const char* memory_value_size_label(MemoryValueSize size) {
    const auto found = std::find_if(kMemoryValueSizes.begin(), kMemoryValueSizes.end(),
                                    [size](const auto& entry) { return entry.first == size; });
    return found == kMemoryValueSizes.end() ? "Unknown" : found->second;
}

bool parse_hex_value(std::string_view text, uint32_t& value) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return error == std::errc{} && end == text.data() + text.size();
}

} // namespace cw
