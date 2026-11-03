#include "memory_search_window.hpp"

#include "libretro_core.hpp"
#include "ui_scale.hpp"
#include "ui_widgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <utility>

namespace cw {

void draw_memory_search(uint64_t game_session_generation, LibretroCore& core,
                        MemorySearchState& state, MemoryViewerState& viewer, Bookmarks& bookmarks,
                        const CodeNotes& notes) {
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(ui_px(900.0f), ui_px(600.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Search", &state.open)) {
        ImGui::End();
        return;
    }

    const MemoryReader& memory = core.memory();
    const std::vector<MemoryRegionInfo> regions = memory.regions();
    if (state.previous_regions != regions || state.previous_game_path != core.game_path() ||
        state.previous_session_generation != game_session_generation) {
        state.search.invalidate();
        state.selected_region = 0;
        state.previous_regions = regions;
        state.previous_game_path = core.game_path();
        state.previous_session_generation = game_session_generation;
    }

    if (!core.is_game_loaded() || memory.size() == 0 || regions.empty()) {
        state.search.invalidate();
        ImGui::TextDisabled("Load a game with readable memory to start a search.");
        ImGui::End();
        return;
    }
    if (!notes.game_id()) {
        ImGui::TextDisabled("Defina o game ID na janela Status.");
    }
    draw_code_note_load_errors(notes);

    if (state.selected_region < 0 || static_cast<size_t>(state.selected_region) > regions.size()) {
        state.selected_region = 0;
    }

    std::string selected_range = "All";
    if (state.selected_region > 0) {
        const MemoryRegionInfo& region = regions[static_cast<size_t>(state.selected_region - 1)];
        selected_range = region.description.empty() ? "Memory" : region.description;
        char range[64];
        std::snprintf(range, sizeof(range), "  0x%06X-0x%06X", region.start, region.end);
        selected_range += range;
        if (!region.available) {
            selected_range += " (not fully available)";
        }
    }
    ImGui::SetNextItemWidth(ui_px(220.0f));
    if (ImGui::BeginCombo("Range", selected_range.c_str())) {
        const bool all_selected = state.selected_region == 0;
        if (ImGui::Selectable("All", all_selected)) {
            state.selected_region = 0;
        }
        if (all_selected) {
            ImGui::SetItemDefaultFocus();
        }
        for (size_t i = 0; i < regions.size(); ++i) {
            const MemoryRegionInfo& region = regions[i];
            std::string label = region.description.empty() ? "Memory" : region.description;
            char range[64];
            std::snprintf(range, sizeof(range), "  0x%06X-0x%06X", region.start, region.end);
            label += range;
            if (!region.available) {
                label += " (not fully available)";
            }
            const bool selected = state.selected_region == static_cast<int>(i + 1);
            if (ImGui::Selectable(label.c_str(), selected)) {
                state.selected_region = static_cast<int>(i + 1);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const char* selected_size = state.all_bit_positions
                                    ? "All bits (0–7)"
                                    : kMemoryValueSizes[static_cast<size_t>(state.size)].second;
    same_line_if_fits(labeled_item_width(ui_px(110.0f), "Size"));
    ImGui::SetNextItemWidth(ui_px(110.0f));
    if (ImGui::BeginCombo("Size", selected_size)) {
        const bool all_bits_selected = state.all_bit_positions;
        if (ImGui::Selectable("All bits (0–7)", all_bits_selected)) {
            state.all_bit_positions = true;
        }
        if (all_bits_selected) {
            ImGui::SetItemDefaultFocus();
        }
        for (const auto& [size, label] : kMemoryValueSizes) {
            const bool selected = !state.all_bit_positions && size == state.size;
            if (ImGui::Selectable(label, selected)) {
                state.size = size;
                state.all_bit_positions = false;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    same_line_if_fits(labeled_item_width(ui_px(140.0f), "Byte order"));
    const char* order_label =
        state.byte_order == ByteOrder::LittleEndian ? "Little-endian" : "Big-endian";
    ImGui::SetNextItemWidth(ui_px(140.0f));
    if (ImGui::BeginCombo("Byte order", order_label)) {
        for (const auto& [order, label] : {std::pair{ByteOrder::LittleEndian, "Little-endian"},
                                           std::pair{ByteOrder::BigEndian, "Big-endian"}}) {
            const bool selected = state.byte_order == order;
            if (ImGui::Selectable(label, selected)) {
                state.byte_order = order;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const bool supports_alignment =
        !state.all_bit_positions &&
        (state.size == MemoryValueSize::SixteenBit || state.size == MemoryValueSize::ThirtyTwoBit);
    if (!supports_alignment) {
        state.aligned = false;
    }
    same_line_if_fits(ImGui::GetFrameHeight() + labeled_item_width(0.0f, "Aligned"));
    ImGui::BeginDisabled(!supports_alignment);
    ImGui::Checkbox("Aligned", &state.aligned);
    ImGui::EndDisabled();

    MemorySearchConfig config{
        .size = state.size,
        .byte_order = state.byte_order,
        .range_start = state.selected_region == 0
                           ? 0
                           : regions[static_cast<size_t>(state.selected_region - 1)].start,
        .range_end = state.selected_region == 0
                         ? memory.size() - 1U
                         : std::min(regions[static_cast<size_t>(state.selected_region - 1)].end,
                                    memory.size() - 1U),
        .aligned = state.aligned,
        .all_bit_positions = state.all_bit_positions};
    state.search.configure(config);

    constexpr std::array<std::pair<MemorySearchComparison, const char*>, 6> kSearchComparisons = {
        {{MemorySearchComparison::Equal, "="},
         {MemorySearchComparison::NotEqual, "!="},
         {MemorySearchComparison::Less, "<"},
         {MemorySearchComparison::Greater, ">"},
         {MemorySearchComparison::LessOrEqual, "<="},
         {MemorySearchComparison::GreaterOrEqual, ">="}}};
    constexpr std::array<std::pair<MemorySearchReference, const char*>, 3> kSearchReferences = {
        {{MemorySearchReference::Previous, "Previous snapshot"},
         {MemorySearchReference::Initial, "First snapshot"},
         {MemorySearchReference::Constant, "Value"}}};

    if (ImGui::Button("New Search")) {
        if (state.search.start(memory)) {
            state.message.clear();
            state.message_is_error = false;
        } else {
            state.message = "Could not snapshot this memory range.";
            state.message_is_error = true;
        }
    }
    same_line_if_fits(labeled_item_width(ui_px(70.0f), "Compare"));
    const char* comparison_label = "!=";
    for (const auto& [comparison, label] : kSearchComparisons) {
        if (comparison == state.comparison) {
            comparison_label = label;
            break;
        }
    }
    ImGui::SetNextItemWidth(ui_px(70.0f));
    if (ImGui::BeginCombo("Compare", comparison_label)) {
        for (const auto& [comparison, label] : kSearchComparisons) {
            const bool selected = state.comparison == comparison;
            if (ImGui::Selectable(label, selected)) {
                state.comparison = comparison;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    same_line_if_fits(labeled_item_width(ui_px(150.0f), "Compare with"));
    const char* reference_label = "Previous snapshot";
    for (const auto& [reference, label] : kSearchReferences) {
        if (reference == state.reference) {
            reference_label = label;
            break;
        }
    }
    ImGui::SetNextItemWidth(ui_px(150.0f));
    if (ImGui::BeginCombo("Compare with", reference_label)) {
        for (const auto& [reference, label] : kSearchReferences) {
            const bool selected = state.reference == reference;
            if (ImGui::Selectable(label, selected)) {
                state.reference = reference;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (state.reference == MemorySearchReference::Constant) {
        same_line_if_fits(labeled_item_width(ui_px(100.0f), "Value (hex)"));
        ImGui::SetNextItemWidth(ui_px(100.0f));
        ImGui::InputText("Value (hex)", state.constant_input, sizeof(state.constant_input),
                         ImGuiInputTextFlags_CharsHexadecimal);
    }

    const bool has_search = state.search.active();
    same_line_if_fits(ImGui::CalcTextSize("Filter").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    ImGui::BeginDisabled(!has_search);
    if (ImGui::Button("Filter")) {
        uint32_t constant = 0;
        bool valid = true;
        if (state.reference == MemorySearchReference::Constant &&
            (!parse_hex_value(state.constant_input, constant) ||
             constant > (state.all_bit_positions ? 1U : memory_value_max(state.size)))) {
            state.message = "Enter a valid hexadecimal value for the selected size.";
            state.message_is_error = true;
            valid = false;
        }
        if (valid) {
            if (state.search.filter(memory, state.comparison, state.reference, constant)) {
                state.message.clear();
                state.message_is_error = false;
            } else {
                state.message = "Could not apply this filter.";
                state.message_is_error = true;
            }
        }
    }
    same_line_if_fits(ImGui::CalcTextSize("Undo").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    ImGui::BeginDisabled(!has_search || state.search.undo_count() == 0);
    if (ImGui::Button("Undo")) {
        if (state.search.undo()) {
            state.message.clear();
            state.message_is_error = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (state.search.invalidated()) {
        ImGui::TextDisabled("Search settings changed; start a new search.");
    }
    if (state.search.active()) {
        ImGui::Text("Candidates: %zu  (up to %zu filters can be undone)",
                    state.search.candidate_count(), MemorySearch::kMaxUndoSteps);
    } else {
        ImGui::TextDisabled("No active search.");
    }
    if (!state.message.empty()) {
        const ImVec4 color = state.message_is_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                                    : ImVec4(0.55f, 0.9f, 0.6f, 1.0f);
        ImGui::TextColored(color, "%s", state.message.c_str());
    }

    if (state.search.active()) {
        if (state.search.candidate_count() == 0) {
            ImGui::TextDisabled("No candidates remain. Undo a filter or start a new search.");
        } else if (ImGui::BeginTable("MemorySearchResults", 6,
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                     ImVec2(0.0f, 0.0f))) {
            ImGui::TableSetupColumn("Address");
            ImGui::TableSetupColumn("Current");
            ImGui::TableSetupColumn("Previous");
            ImGui::TableSetupColumn("First snapshot");
            ImGui::TableSetupColumn("Note");
            ImGui::TableSetupColumn("Watch", ImGuiTableColumnFlags_WidthFixed, ui_px(64.0f));
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            const size_t visible_count =
                std::min<size_t>(state.search.candidate_count(),
                                 static_cast<size_t>(std::numeric_limits<int>::max()));
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible_count));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto result = state.search.result_at(memory, static_cast<size_t>(row));
                    if (!result) {
                        continue;
                    }
                    ImGui::PushID(row);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char address[24];
                    if (result->bit_index) {
                        std::snprintf(address, sizeof(address), "0x%06X.Bit%u", result->address,
                                      static_cast<unsigned>(*result->bit_index));
                    } else {
                        std::snprintf(address, sizeof(address), "0x%06X", result->address);
                    }
                    if (ImGui::Selectable(address) && result->current) {
                        jump_to_memory_address(viewer, memory, result->address);
                    }
                    ImGui::TableSetColumnIndex(1);
                    if (result->current) {
                        ImGui::Text("0x%X", *result->current);
                    } else {
                        ImGui::TextDisabled("--");
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("0x%X", result->previous);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("0x%X", result->initial);
                    ImGui::TableSetColumnIndex(4);
                    draw_note_preview(notes.display_note_at(result->address));
                    ImGui::TableSetColumnIndex(5);
                    if (ImGui::SmallButton("Watch")) {
                        MemoryValueSize watch_size = state.size;
                        ByteOrder watch_order = state.byte_order;
                        if (result->bit_index) {
                            watch_size = static_cast<MemoryValueSize>(
                                static_cast<unsigned>(MemoryValueSize::Bit0) + *result->bit_index);
                            watch_order = ByteOrder::LittleEndian;
                        } else if (!has_byte_order(watch_size)) {
                            watch_order = ByteOrder::LittleEndian;
                        }
                        std::string error;
                        const bool added =
                            bookmarks.add(result->address, watch_size, watch_order, {}, error);
                        set_bookmark_message(state.message, state.message_is_error, added, error,
                                             "Address added to Watch List.",
                                             bookmarks.game_id().has_value());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

} // namespace cw
