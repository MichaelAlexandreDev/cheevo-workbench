#include "logic_tester_window.hpp"

#include "libretro_core.hpp"
#include "ui_scale.hpp"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace cw {
namespace {

constexpr size_t kLogicLogSize = 200;

std::string achievement_title(const LogicTester& tester, uint32_t id) {
    for (const TestedAchievement& a : tester.achievements()) {
        if (a.id == id) {
            return a.title.empty() ? "#" + std::to_string(id) : a.title;
        }
    }
    return "#" + std::to_string(id);
}

ImVec4 logic_state_color(LogicState state) {
    switch (state) {
    case LogicState::Triggered:
        return ImVec4(0.35f, 0.9f, 0.4f, 1.0f);
    case LogicState::Primed:
        return ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
    case LogicState::Disabled:
        return ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
    case LogicState::Paused:
    case LogicState::Waiting:
        return ImVec4(0.65f, 0.65f, 0.7f, 1.0f);
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

} // namespace

void record_logic_events(const std::vector<LogicEvent>& events, const LogicTester& tester,
                         AchievementTesterUi& state, Notice& notice) {
    for (const LogicEvent& event : events) {
        switch (event.kind) {
        case LogicEventKind::LeaderboardStarted:
        case LogicEventKind::LeaderboardCanceled:
        case LogicEventKind::LeaderboardUpdated:
        case LogicEventKind::LeaderboardSubmitted:
        case LogicEventKind::LeaderboardDisabled:
            continue; // kept in the leaderboard editor's own event log
        default:
            break;
        }
        const std::string title = achievement_title(tester, event.id);
        if (event.kind == LogicEventKind::Triggered) {
            show_notice(notice, "Achievement triggered: " + title, true);
        } else if (event.kind == LogicEventKind::Disabled) {
            char address[16];
            std::snprintf(address, sizeof(address), "0x%06X", static_cast<uint32_t>(event.value));
            show_notice(notice, title + " disabled: address " + address + " cannot be read", false);
        }
        state.log.push_front({.event = event, .title = title});
        if (state.log.size() > kLogicLogSize) {
            state.log.pop_back();
        }
    }
}

// Resets and revalidates when another game session starts (or the first one does).
void sync_achievement_tester(LogicTester& tester, AchievementTesterUi& state,
                             uint64_t game_session_generation, const LibretroCore& core,
                             Notice& notice) {
    if (!core.is_game_loaded() || state.synced_generation == game_session_generation) {
        return;
    }
    state.synced_generation = game_session_generation;
    tester.reset();
    record_logic_events(tester.validate_addresses(core.memory(), core.frame_count()), tester, state,
                        notice);
}

void draw_achievement_tester(LogicTester& tester, AchievementTesterUi& state,
                             const LibretroCore& core, Notice& notice) {
    if (!state.open) {
        return;
    }
    if (!ImGui::Begin("Achievement Tester", &state.open)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Type achievement logic and watch it live. Nothing is unlocked or sent "
                       "anywhere: this only evaluates the conditions every emulated frame.");
    ImGui::SetNextItemWidth(ui_px(220.0f));
    input_text_string("Title", state.title);
    ImGui::SetNextItemWidth(-ui_px(80.0f));
    const bool submitted =
        input_text_string("Logic", state.logic, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Add") || submitted) {
        std::string error;
        if (tester.add(state.title, state.logic, error)) {
            state.add_error.clear();
            state.title.clear();
            state.logic.clear();
            if (core.is_game_loaded()) {
                record_logic_events(tester.validate_addresses(core.memory(), core.frame_count()),
                                    tester, state, notice);
            }
        } else {
            state.add_error = error;
        }
    }
    if (!state.add_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", state.add_error.c_str());
    }
    if (!core.is_game_loaded()) {
        ImGui::TextDisabled("No game loaded: achievements are evaluated once a game runs.");
    }

    std::optional<uint32_t> rearm_id;
    std::optional<uint32_t> remove_id;
    // The buttons' column depends on the font, and a table keeps its column widths: without saved
    // settings and with the font size in its ID, it always starts from the current size.
    const std::string table_id = "achievements_" + std::to_string(ImGui::GetFontSize());
    if (ImGui::BeginTable(table_id.c_str(), 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Title");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Measured");
        ImGui::TableSetupColumn("Logic");
        // Fixed to fit its buttons, because this table does not scroll sideways.
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::TableSetupColumn("",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                ImGui::CalcTextSize("Re-arm").x + ImGui::CalcTextSize("Remove").x +
                                    style.FramePadding.x * 4.0f + style.ItemSpacing.x);
        ImGui::TableHeadersRow();
        for (const TestedAchievement& a : tester.achievements()) {
            ImGui::PushID(static_cast<int>(a.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(a.title.empty() ? "(untitled)" : a.title.c_str());
            ImGui::TableSetColumnIndex(1);
            const LogicState logic_state = tester.state(a.id);
            ImGui::TextColored(logic_state_color(logic_state), "%s", logic_state_name(logic_state));
            ImGui::TableSetColumnIndex(2);
            if (const auto measured = tester.measured(a.id)) {
                ImGui::Text("%u / %u", measured->first, measured->second);
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(a.logic.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", a.logic.c_str());
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(logic_state == LogicState::Disabled);
            if (ImGui::SmallButton("Re-arm")) {
                rearm_id = a.id;
            }
            ImGui::EndDisabled();
            if (logic_state == LogicState::Disabled &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Its address cannot be read in this game; it is checked again "
                                  "when a game is loaded.");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                remove_id = a.id;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // Applied after the table loop, which iterates the same list.
    if (rearm_id) {
        tester.rearm(*rearm_id);
    }
    if (remove_id) {
        tester.remove(*remove_id);
    }

    ImGui::SeparatorText("Events (newest first)");
    if (ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (const LoggedLogicEvent& entry : state.log) {
            ImGui::Text("frame %llu  %s  %s", static_cast<unsigned long long>(entry.event.frame),
                        logic_event_name(entry.event.kind), entry.title.c_str());
            if (entry.event.kind == LogicEventKind::Progress) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%d)", entry.event.value);
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace cw
