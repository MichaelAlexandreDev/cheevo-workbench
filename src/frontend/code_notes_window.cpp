#include "code_notes_window.hpp"

#include "ui_scale.hpp"
#include "ui_widgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace cw {
namespace {

std::string lowercase_ascii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

bool note_matches_filter(const CodeNote& note, std::string_view source, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    char address[24];
    std::snprintf(address, sizeof(address), "0x%06X", note.address);
    const std::string needle = lowercase_ascii(filter);
    return lowercase_ascii(address).find(needle) != std::string::npos ||
           lowercase_ascii(note.note).find(needle) != std::string::npos ||
           lowercase_ascii(source).find(needle) != std::string::npos ||
           lowercase_ascii(note.author).find(needle) != std::string::npos;
}

} // namespace

void draw_code_notes_window(CodeNotes& notes, CodeNotesUiState& state,
                            const std::filesystem::path& config_dir, MemoryViewerState& viewer,
                            const MemoryReader& memory) {
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(ui_px(900.0f), ui_px(580.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Code Notes", &state.open)) {
        ImGui::End();
        return;
    }

    const auto disabled_wrapped = [](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    ImGui::TextWrapped("RACache directory");
    const float save_width =
        ImGui::CalcTextSize("Save path").x + 2 * ImGui::GetStyle().FramePadding.x;
    const bool inline_save =
        ImGui::GetContentRegionAvail().x >=
        ImGui::GetFontSize() * 18 + save_width + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(inline_save ? ImGui::GetContentRegionAvail().x - save_width -
                                              ImGui::GetStyle().ItemSpacing.x
                                        : -FLT_MIN);
    input_text_string("##ra_cache_directory", state.ra_cache_path_input);
    if (inline_save)
        ImGui::SameLine();
    if (ImGui::Button("Save path")) {
        std::filesystem::path saved_path;
        std::string error;
        if (save_directory_setting(config_dir, "ra-cache-path.txt", state.ra_cache_path_input,
                                   saved_path, error)) {
            state.ra_cache_dir = std::move(saved_path);
            state.cache_message = state.ra_cache_dir.empty()
                                      ? "Server notes disabled."
                                      : "RACache path saved; server notes reloaded.";
            state.cache_message_is_error = false;
            state.notes_loaded = false;
        } else {
            state.cache_message = std::move(error);
            state.cache_message_is_error = true;
        }
    }
    disabled_wrapped(
        "Use the RACache folder that contains Data/; leave empty to disable server notes.");
    if (!state.cache_message.empty()) {
        const ImVec4 color = state.cache_message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                          : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(color, "%s", state.cache_message.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!notes.game_id()) {
        disabled_wrapped("Set the game ID in Status.");
        ImGui::End();
        return;
    }
    ImGui::Text("Game ID: %u", *notes.game_id());
    ImGui::PushTextWrapPos(0.0f);
    draw_code_note_load_errors(notes);
    ImGui::PopTextWrapPos();
    ImGui::TextUnformatted("Filter");
    ImGui::SetNextItemWidth(-FLT_MIN);
    input_text_string("##code_notes_filter", state.filter);

    const float address_width = ImGui::CalcTextSize("0xFFFFFFFF").x;
    const float note_width = ImGui::GetFontSize() * 20;
    const float origin_width =
        std::min(ImGui::GetFontSize() * 14,
                 std::max(ImGui::CalcTextSize("Origin").x,
                          ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize -
                              2 * ImGui::GetStyle().CellPadding.x));
    const float change_width = ImGui::CalcTextSize("Changed").x;
    const float copy_width = ImGui::CalcTextSize("Copy").x + 2 * ImGui::GetStyle().FramePadding.x;
    const float all_width =
        address_width + note_width + origin_width + 6 * ImGui::GetStyle().CellPadding.x + 4;
    const float publish_width = address_width + note_width + change_width + copy_width +
                                8 * ImGui::GetStyle().CellPadding.x + 5;
    constexpr auto table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                 ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                                 ImGuiTableFlags_ScrollY;

    const auto open_note = [&viewer, &memory](const CodeNote& note) {
        jump_to_memory_address(viewer, memory, note.address);
    };
    if (ImGui::BeginTabBar("CodeNotesTabs")) {
        if (ImGui::BeginTabItem("All notes")) {
            struct Row {
                const CodeNote* note = nullptr;
                bool server = false;
            };
            std::vector<Row> rows;
            rows.reserve(notes.user_notes().size() + notes.server_notes().size());
            for (const CodeNote& note : notes.user_notes()) {
                rows.push_back({&note, false});
            }
            for (const CodeNote& note : notes.server_notes()) {
                rows.push_back({&note, true});
            }
            std::stable_sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
                if (left.note->address != right.note->address) {
                    return left.note->address < right.note->address;
                }
                return left.server < right.server;
            });

            if (rows.empty()) {
                disabled_wrapped("No code notes for this game yet.");
            } else if (ImGui::BeginTable(
                           "AllCodeNotes", 3, table_flags,
                           ImVec2(0.0f, std::max(ImGui::GetTextLineHeightWithSpacing() * 4,
                                                 ImGui::GetContentRegionAvail().y)),
                           std::max(ImGui::GetContentRegionAvail().x, all_width))) {
                ImGui::TableSetupColumn(
                    "Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                    address_width);
                ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    "Origin", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                    origin_width);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < rows.size(); ++i) {
                    const Row& row = rows[i];
                    const std::string origin = row.server ? "Server" : "Mine";
                    if (!note_matches_filter(*row.note, origin, state.filter)) {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char address[24];
                    std::snprintf(address, sizeof(address), "0x%06X", row.note->address);
                    if (ImGui::Selectable(address)) {
                        open_note(*row.note);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", row.note->note.c_str());
                    ImGui::TableSetColumnIndex(2);
                    if (row.server && !row.note->author.empty()) {
                        ImGui::TextWrapped("Server (%s)", row.note->author.c_str());
                    } else {
                        ImGui::TextUnformatted(origin.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("To publish")) {
            const std::vector<CodeNote> notes_to_publish = notes.notes_to_publish();
            disabled_wrapped(
                "Copy each new or changed note into RAIntegration. This tool never uploads notes.");
            if (!notes.server_error().empty()) {
                disabled_wrapped("Fix the server cache JSON before comparing notes to publish.");
            } else if (!notes.user_error().empty()) {
                disabled_wrapped("Fix the workbench notes JSON before comparing notes to publish.");
            } else if (notes_to_publish.empty()) {
                disabled_wrapped("There are no new or changed notes to publish.");
            } else if (ImGui::BeginTable(
                           "PublishCodeNotes", 4, table_flags,
                           ImVec2(0.0f, std::max(ImGui::GetTextLineHeightWithSpacing() * 4,
                                                 ImGui::GetContentRegionAvail().y)),
                           std::max(ImGui::GetContentRegionAvail().x, publish_width))) {
                ImGui::TableSetupColumn(
                    "Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                    address_width);
                ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    "Change", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                    change_width);
                ImGui::TableSetupColumn(
                    "Copy", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                    copy_width);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < notes_to_publish.size(); ++i) {
                    const CodeNote& note = notes_to_publish[i];
                    if (!note_matches_filter(note, "Mine", state.filter)) {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char address[24];
                    std::snprintf(address, sizeof(address), "0x%06X", note.address);
                    if (ImGui::Selectable(address)) {
                        open_note(note);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", note.note.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(notes.server_note_at(note.address) ? "Changed" : "New");
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton("Copy")) {
                        ImGui::SetClipboardText(note.note.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace cw
