#include "memory_viewer_window.hpp"

#include "code_notes_window.hpp"
#include "libretro_core.hpp"
#include "ui_scale.hpp"
#include "ui_widgets.hpp"

#include <imgui.h>

#include <algorithm>

namespace cw {
namespace {

void set_memory_address_input(MemoryViewerState& viewer, uint32_t address) {
    std::snprintf(viewer.address_input, sizeof(viewer.address_input), "%06X", address);
}

void set_memory_edit_value(MemoryViewerState& viewer, const MemoryReader& memory) {
    if (!viewer.has_selection) {
        viewer.value_input[0] = '\0';
        viewer.edit_value_initialized = false;
        return;
    }

    const auto value =
        decode_memory_value(memory, viewer.selected_address, viewer.edit_size, viewer.edit_order);
    if (!value) {
        viewer.value_input[0] = '\0';
    } else {
        std::snprintf(viewer.value_input, sizeof(viewer.value_input), "%X", *value);
    }
    viewer.edit_address = viewer.selected_address;
    viewer.edit_value_initialized = true;
}

void draw_memory_values(const MemoryReader& memory, uint32_t address) {
    const auto show_value = [&memory, address](const char* label, MemoryValueSize size,
                                               ByteOrder order) {
        const auto value = decode_memory_value(memory, address, size, order);
        if (value) {
            ImGui::Text("%-16s 0x%X", label, *value);
        } else {
            ImGui::TextDisabled("%-16s --", label);
        }
    };

    ImGui::Text("All interpretations");
    for (const auto& [size, label] : kMemoryValueSizes) {
        if (size == MemoryValueSize::SixteenBit || size == MemoryValueSize::TwentyFourBit ||
            size == MemoryValueSize::ThirtyTwoBit) {
            char little_label[32];
            char big_label[32];
            std::snprintf(little_label, sizeof(little_label), "%s LE", label);
            std::snprintf(big_label, sizeof(big_label), "%s BE", label);
            show_value(little_label, size, ByteOrder::LittleEndian);
            show_value(big_label, size, ByteOrder::BigEndian);
        } else {
            show_value(label, size, ByteOrder::LittleEndian);
        }
    }
}

void draw_memory_value_editor(LibretroCore& core, MemoryViewerState& viewer, Bookmarks& bookmarks) {
    const MemoryReader& memory = core.memory();
    if (!viewer.has_selection) {
        ImGui::TextDisabled("Select a byte in the memory grid.");
        return;
    }

    if (!viewer.edit_value_initialized || viewer.edit_address != viewer.selected_address) {
        set_memory_edit_value(viewer, memory);
    }

    char selected_address[48];
    std::snprintf(selected_address, sizeof(selected_address), "Selected address: 0x%06X",
                  viewer.selected_address);
    const float watch_button_width =
        ImGui::CalcTextSize("Watch").x + 2.0f * ImGui::GetStyle().FramePadding.x;
    const float address_row_width = ImGui::CalcTextSize(selected_address).x +
                                    ImGui::GetStyle().ItemSpacing.x + watch_button_width;
    const bool watch_fits_beside_address = address_row_width <= ImGui::GetContentRegionAvail().x;
    if (watch_fits_beside_address) {
        ImGui::TextUnformatted(selected_address);
        ImGui::SameLine();
    } else {
        ImGui::TextWrapped("%s", selected_address);
    }
    const ImVec2 watch_button_size =
        watch_fits_beside_address ? ImVec2(watch_button_width, 0.0f) : ImVec2(-1.0f, 0.0f);
    if (ImGui::Button("Watch", watch_button_size)) {
        std::string error;
        const bool added =
            bookmarks.add(viewer.selected_address, viewer.edit_size, viewer.edit_order, {}, error);
        set_bookmark_message(viewer.message, viewer.message_is_error, added, error,
                             "Address added to Watch List.", bookmarks.game_id().has_value());
    }
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    draw_memory_values(memory, viewer.selected_address);
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    ImGui::TextWrapped("Edit value (hex)");
    const auto combo_min_width = [](const char* preview) {
        const ImGuiStyle& combo_style = ImGui::GetStyle();
        return ImGui::CalcTextSize(preview).x + ImGui::GetFrameHeight() +
               2.0f * combo_style.FramePadding.x;
    };
    const auto draw_size_combo = [&](float width) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Size");
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##MemoryEditSize",
                              kMemoryValueSizes[static_cast<size_t>(viewer.edit_size)].second)) {
            for (const auto& [size, label] : kMemoryValueSizes) {
                const bool selected = viewer.edit_size == size;
                if (ImGui::Selectable(label, selected)) {
                    viewer.edit_size = size;
                    if (!has_byte_order(size)) {
                        viewer.edit_order = ByteOrder::LittleEndian;
                    }
                    viewer.edit_value_initialized = false;
                    set_memory_edit_value(viewer, memory);
                    viewer.message.clear();
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
            viewer.edit_order == ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Byte order");
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##MemoryEditByteOrder", order_label)) {
            for (const auto& [order, label] : {std::pair{ByteOrder::LittleEndian, "Little-endian"},
                                               std::pair{ByteOrder::BigEndian, "Big-endian"}}) {
                const bool selected = viewer.edit_order == order;
                if (ImGui::Selectable(label, selected)) {
                    viewer.edit_order = order;
                    viewer.edit_value_initialized = false;
                    set_memory_edit_value(viewer, memory);
                    viewer.message.clear();
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
    const char* selected_size = kMemoryValueSizes[static_cast<size_t>(viewer.edit_size)].second;
    const char* selected_order =
        viewer.edit_order == ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
    const float size_min_width = combo_min_width(selected_size);
    const float order_min_width = combo_min_width(selected_order);
    const float field_spacing = ImGui::GetStyle().ItemSpacing.x;
    if (has_byte_order(viewer.edit_size) &&
        fields_width >= size_min_width + order_min_width + field_spacing) {
        const float max_size_width = fields_width - order_min_width - field_spacing;
        const float size_width = std::clamp(fields_width * 0.4f, size_min_width, max_size_width);
        draw_size_combo(size_width);
        ImGui::SameLine();
        draw_order_combo(fields_width - size_width - field_spacing);
    } else {
        draw_size_combo(fields_width);
        if (has_byte_order(viewer.edit_size)) {
            draw_order_combo(fields_width);
        }
    }

    ImGui::TextUnformatted("Value (hex)");
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted = ImGui::InputText(
        "##MemoryEditValue", viewer.value_input, sizeof(viewer.value_input),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Write value", ImVec2(-1.0f, 0.0f)) || submitted) {
        uint32_t value = 0;
        if (!parse_hex_value(viewer.value_input, value)) {
            viewer.message = "Enter a hexadecimal value.";
            viewer.message_is_error = true;
        } else if (value > memory_value_max(viewer.edit_size)) {
            char maximum[48];
            std::snprintf(maximum, sizeof(maximum), "Value exceeds the selected size (max 0x%X).",
                          memory_value_max(viewer.edit_size));
            viewer.message = maximum;
            viewer.message_is_error = true;
        } else {
            const unsigned count = memory_value_byte_count(viewer.edit_size);
            std::array<uint8_t, 4> bytes{};
            if (memory.read(viewer.selected_address, std::span(bytes.data(), count)) != count) {
                viewer.message = "Selected value is not fully available.";
                viewer.message_is_error = true;
            } else {
                const auto encode = encode_memory_value(value, viewer.edit_size, viewer.edit_order,
                                                        std::span(bytes.data(), count));
                if (encode == MemoryValueEncodeError::TooLarge) {
                    viewer.message = "Value exceeds the selected size.";
                    viewer.message_is_error = true;
                } else if (encode != MemoryValueEncodeError::None) {
                    viewer.message = "Could not encode the selected value.";
                    viewer.message_is_error = true;
                } else {
                    const MemoryWriteError result = core.memory_writer().write(
                        viewer.selected_address, std::span(bytes.data(), count));
                    viewer.message = memory_write_error_message(result);
                    viewer.message_is_error = result != MemoryWriteError::None;
                    if (result == MemoryWriteError::None) {
                        viewer.edit_value_initialized = false;
                    }
                }
            }
        }
    }

    if (!viewer.message.empty()) {
        const ImVec4 color = viewer.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                     : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(color, "%s", viewer.message.c_str());
        ImGui::PopTextWrapPos();
    }
}

void draw_memory_code_note_editor(MemoryViewerState& viewer, CodeNotesUiState& state,
                                  CodeNotes& notes) {
    const uint32_t address = viewer.selected_address;
    if (state.note_editor_address != address) {
        state.note_editor_address = address;
        const CodeNote* note = notes.user_note_at(address);
        state.note_editor = note ? note->note : "";
        state.editor_message.clear();
        state.editor_message_is_error = false;
    }

    ImGui::SeparatorText("Code Note");
    if (!notes.game_id()) {
        ImGui::TextDisabled("Defina o game ID na janela Status.");
        return;
    }
    if (!notes.user_error().empty()) {
        ImGui::TextWrapped("%s", notes.user_error().c_str());
    }
    ImGui::Text("Address 0x%06X", address);
    input_text_multiline_string("My note", state.note_editor, ImVec2(-FLT_MIN, ui_px(100.0f)));
    if (ImGui::Button("Save note")) {
        std::string error;
        if (notes.set_note(address, state.note_editor, error)) {
            state.editor_message = "Saved local note.";
            state.editor_message_is_error = false;
        } else {
            state.editor_message = std::move(error);
            state.editor_message_is_error = true;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(notes.user_note_at(address) == nullptr);
    if (ImGui::Button("Delete note")) {
        std::string error;
        if (notes.remove_note(address, error)) {
            state.note_editor.clear();
            state.editor_message = "Deleted local note.";
            state.editor_message_is_error = false;
        } else {
            state.editor_message = std::move(error);
            state.editor_message_is_error = true;
        }
    }
    ImGui::EndDisabled();

    if (const CodeNote* server_note = notes.server_note_at(address)) {
        ImGui::SeparatorText("RAIntegration server note (read-only)");
        if (!server_note->author.empty()) {
            ImGui::Text("By %s", server_note->author.c_str());
        }
        ImGui::TextWrapped("%s", server_note->note.c_str());
    }
    if (!notes.server_error().empty()) {
        ImGui::TextWrapped("%s", notes.server_error().c_str());
    }
    if (!state.editor_message.empty()) {
        const ImVec4 color = state.editor_message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                           : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", state.editor_message.c_str());
    }
}

} // namespace

void draw_memory_viewer(LibretroCore& core, MemoryViewerState& viewer, Bookmarks& bookmarks,
                        CodeNotes& notes, CodeNotesUiState& notes_ui) {
    if (!viewer.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(ui_px(1050.0f), ui_px(650.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Viewer", &viewer.open)) {
        ImGui::End();
        return;
    }

    const MemoryReader& memory = core.memory();
    const std::vector<MemoryRegionInfo> regions = memory.regions();
    if (viewer.previous_regions != regions || viewer.previous_game_path != core.game_path()) {
        viewer.changes.clear();
        viewer.previous_regions = regions;
        viewer.previous_game_path = core.game_path();
    }
    if (viewer.selected_region < 0 ||
        static_cast<size_t>(viewer.selected_region) >= regions.size()) {
        viewer.selected_region = 0;
    }

    if (!core.is_game_loaded() || regions.empty()) {
        ImGui::TextDisabled("Load a game with an exposed memory map to use the viewer.");
        viewer.changes.clear();
        ImGui::End();
        return;
    }

    if (viewer.pending_jump_address) {
        const uint32_t address = *viewer.pending_jump_address;
        const auto found = std::find_if(regions.begin(), regions.end(), [address](const auto& r) {
            return r.start <= address && address <= r.end;
        });
        if (found != regions.end()) {
            viewer.selected_region = static_cast<int>(std::distance(regions.begin(), found));
            viewer.selected_address = address;
            set_memory_address_input(viewer, address);
            viewer.has_selection = true;
            viewer.edit_value_initialized = false;
            viewer.scroll_to_address = address;
            viewer.pending_jump_address.reset();
        }
    }

    const auto region_label = [&regions](size_t index) {
        const MemoryRegionInfo& region = regions[index];
        std::string label = region.description.empty() ? "Memory" : region.description;
        char range[64];
        std::snprintf(range, sizeof(range), "  0x%06X-0x%06X", region.start, region.end);
        label += range;
        if (!region.available) {
            label += " (unavailable)";
        } else if (!region.writable) {
            label += " (read-only)";
        }
        return label;
    };

    const std::string selected_label = region_label(static_cast<size_t>(viewer.selected_region));
    ImGui::SetNextItemWidth(ui_px(260.0f));
    if (ImGui::BeginCombo("Region", selected_label.c_str())) {
        for (size_t i = 0; i < regions.size(); ++i) {
            const std::string label = region_label(i);
            const bool selected = static_cast<int>(i) == viewer.selected_region;
            if (ImGui::Selectable(label.c_str(), selected)) {
                viewer.selected_region = static_cast<int>(i);
                viewer.selected_address = regions[i].start;
                set_memory_address_input(viewer, viewer.selected_address);
                viewer.has_selection = true;
                viewer.edit_value_initialized = false;
                viewer.scroll_to_address = regions[i].start;
                viewer.changes.clear();
                viewer.message.clear();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    same_line_if_fits(labeled_item_width(ui_px(150.0f), "Address (hex)"));
    ImGui::SetNextItemWidth(ui_px(150.0f));
    const bool go_submitted = ImGui::InputText(
        "Address (hex)", viewer.address_input, sizeof(viewer.address_input),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    same_line_if_fits(ImGui::CalcTextSize("Go").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    if (ImGui::Button("Go") || go_submitted) {
        uint32_t address = 0;
        if (!parse_hex_value(viewer.address_input, address)) {
            viewer.message = "Enter a valid hexadecimal address.";
            viewer.message_is_error = true;
        } else if (address >= memory.size()) {
            viewer.message = "Address is outside the memory space.";
            viewer.message_is_error = true;
        } else {
            const auto found =
                std::find_if(regions.begin(), regions.end(), [address](const auto& r) {
                    return r.start <= address && address <= r.end;
                });
            if (found == regions.end()) {
                viewer.message = "Address is not inside a mapped region.";
                viewer.message_is_error = true;
            } else {
                viewer.selected_region = static_cast<int>(std::distance(regions.begin(), found));
                viewer.selected_address = address;
                set_memory_address_input(viewer, address);
                viewer.has_selection = true;
                viewer.edit_value_initialized = false;
                viewer.scroll_to_address = address;
                viewer.changes.clear();
                viewer.message.clear();
            }
        }
    }

    const MemoryRegionInfo& region = regions[static_cast<size_t>(viewer.selected_region)];
    if (!viewer.has_selection && region.start < memory.size()) {
        viewer.selected_address = region.start;
        set_memory_address_input(viewer, region.start);
        viewer.has_selection = true;
    }
    ImGui::Text("0x%06X-0x%06X  %s  %s", region.start, region.end,
                region.available ? "available" : "unavailable",
                !region.available ? "not writable" : (region.writable ? "writable" : "read-only"));
    if (!viewer.message.empty()) {
        const ImVec4 color = viewer.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                     : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", viewer.message.c_str());
    }
    ImGui::Separator();

    const uint64_t visible_end =
        memory.size() == 0 ? 0 : std::min<uint64_t>(region.end, memory.size() - 1U);
    const bool has_rows = memory.size() > 0 && region.start <= visible_end;
    const uint64_t byte_count = has_rows ? visible_end - region.start + 1U : 0;
    const uint64_t row_count = (byte_count + 15U) / 16U;
    const float line_height = ImGui::GetFrameHeight();
    const float grid_width = std::max(350.0f, ImGui::GetContentRegionAvail().x * 0.62f);
    if (ImGui::BeginChild("MemoryGrid", ImVec2(grid_width, 0.0f), ImGuiChildFlags_Borders)) {
        if (!has_rows) {
            ImGui::TextDisabled("This region has no addresses in the current memory map.");
        } else {
            if (viewer.scroll_to_address) {
                const uint64_t offset = *viewer.scroll_to_address - region.start;
                ImGui::SetScrollY(static_cast<float>((offset / 16U) * line_height));
                viewer.scroll_to_address.reset();
            }

            std::vector<MemoryByteSnapshot> current_visible;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(row_count), line_height);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const uint64_t row_address64 = region.start + static_cast<uint64_t>(row) * 16U;
                    const uint32_t row_address = static_cast<uint32_t>(row_address64);
                    const size_t row_bytes = static_cast<size_t>(
                        std::min<uint64_t>(16U, visible_end - row_address64 + 1U));
                    std::array<uint8_t, 16> bytes{};
                    const size_t read_count =
                        memory.read(row_address, std::span(bytes.data(), row_bytes));
                    for (size_t i = 0; i < read_count; ++i) {
                        current_visible.push_back(
                            {.address = row_address + static_cast<uint32_t>(i), .value = bytes[i]});
                    }

                    ImGui::PushID(row);
                    ImGui::Text("0x%06X", row_address);
                    for (size_t i = 0; i < row_bytes; ++i) {
                        ImGui::SameLine(0.0f, i == 0 ? 10.0f : (i % 4 == 0 ? 9.0f : 4.0f));
                        if (i >= read_count) {
                            ImGui::TextDisabled("--");
                            continue;
                        }

                        const uint32_t address = row_address + static_cast<uint32_t>(i);
                        // Brightest right after the change, fading over the highlight window.
                        const std::optional<uint64_t> age =
                            viewer.changes.frames_since_change(address, core.frame_count());
                        const bool is_changed = age.has_value();
                        const bool selected =
                            viewer.has_selection && viewer.selected_address == address;
                        // "###byte": ImGui identifies a widget by its label, and this label is the
                        // byte's value. A byte that changes every frame would get a new identity
                        // between mouse press and release, and the click would be lost. After
                        // "###" only the fixed part counts (PushID(i) below keeps it unique).
                        char byte_label[16];
                        std::snprintf(byte_label, sizeof(byte_label), "%02X###byte", bytes[i]);
                        if (is_changed) {
                            const float fade =
                                1.0f - static_cast<float>(*age) /
                                           static_cast<float>(viewer.changes.window());
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                                  ImVec4(1.0f, 0.75f, 0.25f, 0.35f + 0.65f * fade));
                        }
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(byte_label, selected, 0,
                                              ImVec2(ui_px(24.0f), 0.0f))) {
                            viewer.selected_address = address;
                            set_memory_address_input(viewer, address);
                            viewer.has_selection = true;
                            viewer.edit_value_initialized = false;
                            viewer.message.clear();
                        }
                        ImGui::PopID();
                        if (is_changed) {
                            ImGui::PopStyleColor();
                        }
                    }
                    ImGui::PopID();
                }
            }
            viewer.changes.observe(current_visible, core.frame_count());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("SelectedValue", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        draw_memory_value_editor(core, viewer, bookmarks);
        draw_memory_code_note_editor(viewer, notes_ui, notes);
    }
    ImGui::EndChild();
    ImGui::End();
}

void jump_to_memory_address(MemoryViewerState& viewer, const MemoryReader& memory,
                            uint32_t address) {
    viewer.open = true;
    viewer.pending_jump_address = address;
    set_memory_address_input(viewer, address);

    const std::vector<MemoryRegionInfo> regions = memory.regions();
    const auto found = std::find_if(regions.begin(), regions.end(), [address](const auto& region) {
        return region.start <= address && address <= region.end;
    });
    if (found == regions.end()) {
        return;
    }

    viewer.selected_region = static_cast<int>(std::distance(regions.begin(), found));
    viewer.selected_address = address;
    set_memory_address_input(viewer, address);
    viewer.has_selection = true;
    viewer.edit_value_initialized = false;
    viewer.scroll_to_address = address;
    viewer.pending_jump_address.reset();
    viewer.message.clear();
}

} // namespace cw
