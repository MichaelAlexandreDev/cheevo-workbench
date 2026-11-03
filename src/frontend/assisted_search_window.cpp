#include "assisted_search_window.hpp"
#include "ui_scale.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace cw {
namespace {
const char* kind_name(AssistedSearchKind kind) {
    switch (kind) {
    case AssistedSearchKind::Flag:
        return "Flag";
    case AssistedSearchKind::Area:
        return "Area / mode";
    case AssistedSearchKind::Counter:
        return "Counter";
    }
    return "Unknown";
}
} // namespace

void AssistedSearchWindow::sync(uint64_t session, uint64_t discontinuity, bool game_loaded) {
    if (synced_ && (session != session_ || discontinuity != discontinuity_ || !game_loaded)) {
        if (search_.active())
            message_ = "Observation cleared after a game change, reset or state load.";
        search_.reset();
        results_.clear();
        collecting_ = false;
    }
    session_ = session;
    discontinuity_ = discontinuity;
    synced_ = true;
}

void AssistedSearchWindow::observe(const MemoryReader& memory) {
    if (!collecting_)
        return;
    const auto start = std::chrono::steady_clock::now();
    if (!search_.observe(memory)) {
        collecting_ = false;
        results_.clear();
        message_ = search_.error();
    }
    last_observation_ms_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void AssistedSearchWindow::mark() {
    if (!search_.mark(label_, same_value_)) {
        message_ = search_.error();
        return;
    }
    filter_marks_ = true;
    next_refresh_ = 0;
    message_ = "Moment marked. Run a few more frames to check stability after the event.";
}

void AssistedSearchWindow::handle_hotkey(bool allow_keyboard) {
    if (allow_keyboard && collecting_ && !search_.mark_pending() &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_M, ImGuiInputFlags_RouteGlobal))
        mark();
}

std::optional<uint32_t> AssistedSearchWindow::draw(const MemoryReader& memory, bool game_loaded) {
    if (!open)
        return {};
    ImGui::SetNextWindowSize(ImVec2(cw::ui_px(850.0f), cw::ui_px(550.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Assisted Search", &open)) {
        ImGui::End();
        return {};
    }
    std::optional<uint32_t> selected;
    ImGui::TextWrapped("Observe 8-bit values while playing. Flags, areas and counters are "
                       "suggestions; confirm their meaning in Memory Viewer.");
    ImGui::BeginDisabled(!game_loaded || memory.size() == 0);
    if (ImGui::Button(search_.active() ? "Restart observation" : "Start observation")) {
        collecting_ = search_.start(memory);
        results_.clear();
        filter_marks_ = false;
        next_refresh_ = 0;
        message_ = collecting_ ? "Observing. Play normally, then mark an event to narrow results."
                               : search_.error();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!collecting_);
    if (ImGui::Button("Stop observation")) {
        collecting_ = false;
        search_.cancel_mark();
        if (search_.mark_count(label_) == 0)
            filter_marks_ = false;
        next_refresh_ = 0;
        message_ = "Observation stopped. Results are frozen; restart begins a new history.";
    }
    ImGui::EndDisabled();
    if (!game_loaded)
        ImGui::TextDisabled("Load a game with readable memory first.");
    if (search_.active()) {
        ImGui::Text("%s | %llu frames | %zu bytes | %.1f MiB history | last frame %.3f ms",
                    collecting_ ? "Observing" : "Stopped",
                    static_cast<unsigned long long>(search_.frame_count()), search_.tracked_bytes(),
                    static_cast<double>(search_.memory_usage_bytes()) / (1024 * 1024),
                    last_observation_ms_);
        ImGui::TextWrapped(
            "Pausing emulation pauses observation. Frame advance observes one frame. "
            "Closing this window keeps observation running.");
    }
    ImGui::Separator();
    if (ImGui::InputText("Moment label", label_, sizeof(label_)))
        next_refresh_ = 0;
    ImGui::Checkbox("Same place again (match the first mark's value)", &same_value_);
    ImGui::BeginDisabled(!collecting_ || search_.mark_pending() || label_[0] == '\0');
    if (ImGui::Button("Mark moment (Ctrl+M)"))
        mark();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("%zu completed marks for this label%s", search_.mark_count(label_),
                search_.mark_pending() ? " | waiting for more frames" : "");
    ImGui::TextWrapped("Mark near a change, with stable frames before and after it. Reuse a label "
                       "for the same kind of event to intersect candidates.");
    if (ImGui::Checkbox("Only candidates matching this label", &filter_marks_))
        next_refresh_ = 0;
    if (ImGui::Combo("Kind", &kind_, "All\0Flags\0Areas / modes\0Counters\0"))
        next_refresh_ = 0;
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());

    if (search_.active() && ImGui::GetTime() >= next_refresh_) {
        const auto kind = kind_ == 0 ? std::optional<AssistedSearchKind>{}
                                     : std::optional{static_cast<AssistedSearchKind>(kind_ - 1)};
        results_ = search_.results(
            200, filter_marks_ ? std::string_view(label_) : std::string_view{}, kind);
        if (filter_marks_ && label_[0] == '\0')
            results_.clear();
        next_refresh_ = ImGui::GetTime() + 0.5;
    }
    ImGui::TextDisabled(
        "Top %zu candidates (refreshed twice per second). Click an address to inspect.",
        results_.size());
    if (ImGui::BeginTable("Candidates", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Value (hex)");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Score");
        ImGui::TableSetupColumn("Changes");
        ImGui::TableSetupColumn("Bit");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& row : results_) {
            ImGui::PushID(static_cast<int>(row.address));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char address[16];
            std::snprintf(address, sizeof(address), "0x%06X", row.address);
            if (ImGui::Selectable(address))
                selected = row.address;
            ImGui::TableNextColumn();
            ImGui::Text("%02X", static_cast<unsigned>(row.value));
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Last change: observation frame %llu",
                            static_cast<unsigned long long>(row.last_change_frame));
                ImGui::TextUnformatted("Values seen (first four):");
                for (size_t i = 0; i < std::min<size_t>(row.distinct_count, 4); ++i) {
                    ImGui::SameLine();
                    ImGui::Text("%02X", static_cast<unsigned>(row.distinct_values[i]));
                }
                if (row.distinct_count > 4)
                    ImGui::TextUnformatted("Additional values were observed.");
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kind_name(row.kind));
            ImGui::TableNextColumn();
            ImGui::Text("%u", row.score);
            ImGui::TableNextColumn();
            ImGui::Text("%u", row.changes);
            ImGui::TableNextColumn();
            if (row.bit_index)
                ImGui::Text("%u", static_cast<unsigned>(*row.bit_index));
            else
                ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
    return selected;
}
} // namespace cw
