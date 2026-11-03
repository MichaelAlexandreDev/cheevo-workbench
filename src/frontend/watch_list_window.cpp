#include "watch_list_window.hpp"

#include "ui_scale.hpp"
#include "ui_widgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <iterator>
#include <string_view>
#include <system_error>
#include <vector>

namespace cw {
namespace {

bool save_bookmark_description(Bookmarks& bookmarks, WatchListUiState& state, uint64_t row_id,
                               const std::string& description) {
    const auto found = std::find_if(bookmarks.items().begin(), bookmarks.items().end(),
                                    [row_id](const auto& item) { return item.row_id == row_id; });
    if (found != bookmarks.items().end()) {
        const size_t index = static_cast<size_t>(std::distance(bookmarks.items().begin(), found));
        std::string error;
        const bool renamed = bookmarks.set_description(index, description, error);
        set_bookmark_message(state.message, state.message_is_error, renamed, error,
                             "Bookmark renamed.", bookmarks.game_id().has_value());
        if (!renamed) {
            return false; // Keep the draft available for another save attempt.
        }
    }
    state.dirty_description_ids.erase(row_id);
    state.description_buffers.erase(row_id);
    return true;
}

} // namespace

bool flush_pending_bookmark_descriptions(Bookmarks& bookmarks, WatchListUiState& state) {
    while (!state.dirty_description_ids.empty()) {
        const uint64_t row_id = *state.dirty_description_ids.begin();
        const auto buffer = state.description_buffers.find(row_id);
        if (buffer == state.description_buffers.end()) {
            state.dirty_description_ids.erase(row_id);
        } else if (!save_bookmark_description(bookmarks, state, row_id, buffer->second)) {
            return false;
        }
    }
    return true;
}

void draw_watch_list(Bookmarks& bookmarks, WatchListUiState& state, MemoryViewerState& viewer,
                     const MemoryReader& memory, uint64_t frame, const CodeNotes& notes,
                     const std::filesystem::path& ra_cache_dir) {
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(ui_px(900.0f), ui_px(460.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Watch List", &state.open)) {
        ImGui::End();
        return;
    }

    if (!bookmarks.game_id()) {
        ImGui::TextWrapped("No game ID: bookmarks are not saved.");
    }
    if (!bookmarks.error().empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", bookmarks.error().c_str());
        ImGui::PopTextWrapPos();
    }
    const bool cache_directory_ready = !ra_cache_dir.empty() && directory_exists(ra_cache_dir);
    const bool cache_actions_enabled = bookmarks.game_id().has_value() && cache_directory_ready;
    const ImGuiStyle& control_style = ImGui::GetStyle();
    const float import_width =
        ImGui::CalcTextSize("Import from RACache").x + 2.0f * control_style.FramePadding.x;
    const float export_width =
        ImGui::CalcTextSize("Export to RACache").x + 2.0f * control_style.FramePadding.x;
    const float action_spacing = control_style.ItemSpacing.x;
    const bool actions_fit =
        ImGui::GetContentRegionAvail().x >= import_width + export_width + action_spacing;
    ImGui::BeginDisabled(!cache_actions_enabled);
    const ImVec2 action_size = actions_fit ? ImVec2(0.0f, 0.0f) : ImVec2(-1.0f, 0.0f);
    if (ImGui::Button("Import from RACache", action_size)) {
        std::string error;
        const auto path = Bookmarks::ra_cache_file_path(ra_cache_dir, *bookmarks.game_id());
        const bool imported = bookmarks.import_file(path, error);
        set_bookmark_message(state.message, state.message_is_error, imported, error,
                             "Imported bookmarks from RACache.", bookmarks.game_id().has_value());
        if (imported) {
            state.description_buffers.clear();
            state.dirty_description_ids.clear();
        }
    }
    if (actions_fit) {
        ImGui::SameLine();
    }
    if (ImGui::Button("Export to RACache", action_size)) {
        const auto path = Bookmarks::ra_cache_file_path(ra_cache_dir, *bookmarks.game_id());
        std::error_code filesystem_error;
        if (std::filesystem::exists(path, filesystem_error)) {
            state.pending_export_path = path;
            state.pending_export_game_id = bookmarks.game_id();
            state.confirm_export = true;
            ImGui::OpenPopup("Replace RACache bookmarks?");
        } else if (filesystem_error) {
            state.message =
                "Could not inspect " + path.string() + ": " + filesystem_error.message();
            state.message_is_error = true;
        } else {
            std::string error;
            const bool exported = bookmarks.export_file(path, false, error);
            set_bookmark_message(state.message, state.message_is_error, exported, error,
                                 "Exported bookmarks to RACache.", bookmarks.game_id().has_value());
        }
    }
    ImGui::EndDisabled();
    if (!cache_directory_ready) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Set a valid RACache directory in Code Notes to import or export.");
        ImGui::PopTextWrapPos();
    }
    if (state.confirm_export) {
        ImGui::OpenPopup("Replace RACache bookmarks?");
    }
    if (ImGui::BeginPopupModal("Replace RACache bookmarks?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Replace the existing file?\n%s",
                           state.pending_export_path.string().c_str());
        const bool same_game = state.pending_export_game_id == bookmarks.game_id();
        if (!same_game) {
            ImGui::TextDisabled("The active game changed. Cancel and start the export again.");
        }
        ImGui::BeginDisabled(!same_game);
        if (ImGui::Button("Replace")) {
            std::string error;
            const bool exported = bookmarks.export_file(state.pending_export_path, true, error);
            set_bookmark_message(state.message, state.message_is_error, exported, error,
                                 "Exported bookmarks to RACache.", bookmarks.game_id().has_value());
            state.confirm_export = false;
            state.pending_export_path.clear();
            state.pending_export_game_id.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.confirm_export = false;
            state.pending_export_path.clear();
            state.pending_export_game_id.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextUnformatted("Address (hex)");
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted = ImGui::InputText(
        "##WatchAddress", state.address_input, sizeof(state.address_input),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);

    const auto combo_min_width = [](const char* preview) {
        const ImGuiStyle& combo_style = ImGui::GetStyle();
        return ImGui::CalcTextSize(preview).x + ImGui::GetFrameHeight() +
               2.0f * combo_style.FramePadding.x;
    };
    const auto draw_size_combo = [&](float width) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Size");
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##WatchSize", memory_value_size_label(state.size))) {
            for (const auto& [size, label] : kMemoryValueSizes) {
                const bool selected = size == state.size;
                if (ImGui::Selectable(label, selected)) {
                    state.size = size;
                    if (!has_byte_order(size)) {
                        state.byte_order = ByteOrder::LittleEndian;
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
    };
    const auto draw_order_combo = [&](float width) {
        const char* order_label =
            state.byte_order == ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Byte order");
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##WatchByteOrder", order_label)) {
            for (const auto& [order, label] : {std::pair{ByteOrder::LittleEndian, "Little-endian"},
                                               std::pair{ByteOrder::BigEndian, "Big-endian"}}) {
                const bool selected = order == state.byte_order;
                if (ImGui::Selectable(label, selected)) {
                    state.byte_order = order;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
    };
    const float fields_width = ImGui::GetContentRegionAvail().x;
    const char* selected_size = memory_value_size_label(state.size);
    const char* selected_order =
        state.byte_order == ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
    const float size_min_width = combo_min_width(selected_size);
    const float order_min_width = combo_min_width(selected_order);
    const float field_spacing = ImGui::GetStyle().ItemSpacing.x;
    if (has_byte_order(state.size) &&
        fields_width >= size_min_width + order_min_width + field_spacing) {
        const float max_size_width = fields_width - order_min_width - field_spacing;
        const float size_width = std::clamp(fields_width * 0.4f, size_min_width, max_size_width);
        draw_size_combo(size_width);
        ImGui::SameLine();
        draw_order_combo(fields_width - size_width - field_spacing);
    } else {
        draw_size_combo(fields_width);
        if (has_byte_order(state.size)) {
            draw_order_combo(fields_width);
        }
    }

    ImGui::TextUnformatted("Label (optional)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##WatchLabel", state.label_input, sizeof(state.label_input));
    if (ImGui::Button("Add", ImVec2(-1.0f, 0.0f)) || submitted) {
        uint32_t address = 0;
        if (!parse_hex_value(state.address_input, address)) {
            state.message = "Enter a valid hexadecimal address.";
            state.message_is_error = true;
        } else {
            const ByteOrder byte_order =
                has_byte_order(state.size) ? state.byte_order : ByteOrder::LittleEndian;
            std::string error;
            const bool added =
                bookmarks.add(address, state.size, byte_order, state.label_input, error);
            set_bookmark_message(state.message, state.message_is_error, added, error,
                                 "Address added to the Watch List.",
                                 bookmarks.game_id().has_value());
            if (added) {
                state.address_input[0] = '\0';
                state.label_input[0] = '\0';
            }
        }
    }

    if (!state.message.empty()) {
        const ImVec4 color = state.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                    : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(color, "%s", state.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::PushTextWrapPos(0.0f);
    draw_code_note_load_errors(notes);
    ImGui::PopTextWrapPos();

    if (bookmarks.size() == 0) {
        ImGui::TextDisabled("No watched addresses yet.");
    } else if (ImGui::BeginTable("WatchListItems", 9,
                                 ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY,
                                 ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, ui_px(88.0f));
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, ui_px(112.0f));
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ui_px(100.0f));
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, ui_px(112.0f));
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, ui_px(70.0f));
        ImGui::TableSetupColumn("Previous", ImGuiTableColumnFlags_WidthFixed, ui_px(100.0f));
        ImGui::TableSetupColumn("Label");
        ImGui::TableSetupColumn("Note");
        const ImGuiStyle& style = ImGui::GetStyle();
        const float actions_width = ImGui::CalcTextSize("Up").x + ImGui::CalcTextSize("Down").x +
                                    ImGui::CalcTextSize("Remove").x + 6.0f * style.FramePadding.x +
                                    2.0f * style.ItemSpacing.x;
        ImGui::TableSetupColumn("Actions",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                actions_width);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        std::optional<std::pair<size_t, size_t>> move_request;
        std::optional<size_t> remove_request;
        std::vector<std::pair<uint64_t, std::string>> rename_requests;
        std::vector<std::pair<size_t, bool>> format_requests;
        const auto& items = bookmarks.items();
        for (size_t i = 0; i < items.size(); ++i) {
            const MemoryBookmark& item = items[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            if (const auto age = bookmarks.frames_since_change(i, frame); age) {
                const float fade = 1.0f - static_cast<float>(*age) /
                                              static_cast<float>(bookmarks.highlight_frames());
                const int alpha = static_cast<int>(45.0f + 105.0f * fade);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(175, 108, 20, alpha));
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", i + 1);

            ImGui::TableSetColumnIndex(1);
            char address[24];
            if (item.address) {
                std::snprintf(address, sizeof(address), "0x%06X", *item.address);
                if (ImGui::Selectable(address)) {
                    jump_to_memory_address(viewer, memory, *item.address);
                }
            } else if (!item.mem_addr.empty()) {
                ImGui::TextUnformatted(item.mem_addr.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Unsupported address: %s", item.mem_addr.c_str());
                }
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(2);
            if (item.supported()) {
                std::string interpretation = memory_value_size_label(*item.size);
                if (has_byte_order(*item.size)) {
                    interpretation += item.byte_order == ByteOrder::LittleEndian ? " LE" : " BE";
                }
                ImGui::TextUnformatted(interpretation.c_str());
            } else {
                ImGui::TextDisabled("unsupported");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s%s%s", item.unsupported_reason.c_str(),
                                      item.mem_addr.empty() ? "" : ": ", item.mem_addr.c_str());
                }
            }

            ImGui::TableSetColumnIndex(3);
            if (!item.supported()) {
                ImGui::TextDisabled("unsupported");
            } else if (item.current_value) {
                if (item.decimal) {
                    ImGui::Text("%u", *item.current_value);
                } else {
                    ImGui::Text("0x%X", *item.current_value);
                }
            } else {
                ImGui::TextDisabled("Unavailable");
            }
            ImGui::TableSetColumnIndex(4);
            if (item.supported()) {
                if (ImGui::SmallButton(item.decimal ? "Dec" : "Hex")) {
                    format_requests.emplace_back(i, !item.decimal);
                }
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(5);
            if (!item.supported()) {
                ImGui::TextDisabled("--");
            } else if (item.previous_value) {
                if (item.decimal) {
                    ImGui::Text("%u", *item.previous_value);
                } else {
                    ImGui::Text("0x%X", *item.previous_value);
                }
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(6);
            auto [buffer, inserted] =
                state.description_buffers.try_emplace(item.row_id, item.description);
            (void)inserted;
            ImGui::SetNextItemWidth(-1.0f);
            const bool description_changed = input_text_string("##description", buffer->second);
            if (description_changed) {
                state.dirty_description_ids.insert(item.row_id);
            }
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                state.dirty_description_ids.contains(item.row_id)) {
                if (buffer->second != item.description) {
                    rename_requests.emplace_back(item.row_id, buffer->second);
                }
            }
            ImGui::TableSetColumnIndex(7);
            if (item.address) {
                draw_note_preview(notes.display_note_at(*item.address));
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(8);
            if (i == 0) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Up") && i > 0) {
                move_request = std::pair{i, i - 1};
            }
            if (i == 0) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (i + 1 == items.size()) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Down") && i + 1 < items.size()) {
                move_request = std::pair{i, i + 1};
            }
            if (i + 1 == items.size()) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                remove_request = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();

        for (const auto& [row_id, description] : rename_requests) {
            save_bookmark_description(bookmarks, state, row_id, description);
        }
        for (const auto& [index, decimal] : format_requests) {
            std::string error;
            const bool changed = bookmarks.set_decimal(index, decimal, error);
            set_bookmark_message(state.message, state.message_is_error, changed, error,
                                 "Bookmark format saved.", bookmarks.game_id().has_value());
        }
        if (remove_request) {
            const uint64_t row_id = bookmarks.items()[*remove_request].row_id;
            std::string error;
            const bool removed = bookmarks.remove(*remove_request, error);
            set_bookmark_message(state.message, state.message_is_error, removed, error,
                                 "Bookmark removed.", bookmarks.game_id().has_value());
            if (removed) {
                state.description_buffers.erase(row_id);
                state.dirty_description_ids.erase(row_id);
            }
        } else if (move_request) {
            std::string error;
            const bool moved = bookmarks.move(move_request->first, move_request->second, error);
            set_bookmark_message(state.message, state.message_is_error, moved, error,
                                 "Bookmark order saved.", bookmarks.game_id().has_value());
        }
    }

    ImGui::End();
}

} // namespace cw
