#include "authoring_window.hpp"
#include "ui_scale.hpp"

#include "cw/devtools/leaderboard_editor.hpp"
#include "cw/devtools/logic_tester.hpp"
#include "cw/devtools/rich_presence_editor.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace cw {
namespace {

int resize_string(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto& text = *static_cast<std::string*>(data->UserData);
        text.resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = text.data();
    }
    return 0;
}

bool text_input(const char* label, std::string& text, bool multiline = false, float lines = 4.0f) {
    text.reserve(text.size() + 1);
    if (multiline) {
        return ImGui::InputTextMultiline(
            label, text.data(), text.capacity() + 1,
            ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * lines),
            ImGuiInputTextFlags_CallbackResize, resize_string, &text);
    }
    return ImGui::InputText(label, text.data(), text.capacity() + 1,
                            ImGuiInputTextFlags_CallbackResize, resize_string, &text);
}

void error_text(std::string_view message) {
    if (message.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.48f, 0.4f, 1.0f));
    ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
    ImGui::PopStyleColor();
}

void disabled_wrapped(const char* message) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", message);
    ImGui::PopStyleColor();
}

void button_wrap(const char* next_label) {
    const auto& style = ImGui::GetStyle();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float width = ImGui::CalcTextSize(next_label).x + 2.0f * style.FramePadding.x;
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
        ImGui::SameLine();
}

void clamp_active_window(const ImGuiViewport& viewport) {
    const ImVec2 work_end(viewport.WorkPos.x + viewport.WorkSize.x,
                          viewport.WorkPos.y + viewport.WorkSize.y);
    const ImVec2 size_limit(std::max(1.0f, viewport.WorkSize.x - 12.0f),
                            std::max(1.0f, viewport.WorkSize.y - 12.0f));
    ImVec2 size = ImGui::GetWindowSize();
    if (size.x > size_limit.x || size.y > size_limit.y) {
        size.x = std::min(size.x, size_limit.x);
        size.y = std::min(size.y, size_limit.y);
        ImGui::SetWindowSize(size, ImGuiCond_Always);
    }

    ImVec2 position = ImGui::GetWindowPos();
    const float max_x = std::max(viewport.WorkPos.x, work_end.x - size.x);
    const float max_y = std::max(viewport.WorkPos.y, work_end.y - size.y);
    position.x = std::clamp(position.x, viewport.WorkPos.x, max_x);
    position.y = std::clamp(position.y, viewport.WorkPos.y, max_y);
    if (position.x != ImGui::GetWindowPos().x || position.y != ImGui::GetWindowPos().y)
        ImGui::SetWindowPos(position, ImGuiCond_Always);
}

void prepare_window(const ImGuiViewport& viewport, ImVec2 offset, ImVec2 desired_size) {
    const ImVec2 maximum(std::max(1.0f, viewport.WorkSize.x - 12.0f),
                         std::max(1.0f, viewport.WorkSize.y - 12.0f));
    const ImVec2 minimum(std::min(cw::ui_px(340.0f), maximum.x),
                         std::min(cw::ui_px(240.0f), maximum.y));
    desired_size.x = std::clamp(desired_size.x, minimum.x, maximum.x);
    desired_size.y = std::clamp(desired_size.y, minimum.y, maximum.y);
    ImGui::SetNextWindowSizeConstraints(minimum, maximum);
    ImGui::SetNextWindowSize(desired_size, ImGuiCond_FirstUseEver);
    const ImVec2 max_pos(
        std::max(viewport.WorkPos.x, viewport.WorkPos.x + viewport.WorkSize.x - desired_size.x),
        std::max(viewport.WorkPos.y, viewport.WorkPos.y + viewport.WorkSize.y - desired_size.y));
    ImGui::SetNextWindowPos(
        ImVec2(std::clamp(viewport.WorkPos.x + offset.x, viewport.WorkPos.x, max_pos.x),
               std::clamp(viewport.WorkPos.y + offset.y, viewport.WorkPos.y, max_pos.y)),
        ImGuiCond_FirstUseEver);
}

std::string leaderboard_signature(const LeaderboardDraft& draft) {
    std::string signature;
    const auto append = [&signature](std::string_view field) {
        signature += std::to_string(field.size());
        signature.push_back(':');
        signature.append(field);
    };
    append(draft.start);
    append(draft.cancel);
    append(draft.submit);
    append(draft.value);
    append(draft.format);
    return signature;
}

constexpr std::array<const char*, 24> kLeaderboardFormats{
    "FRAMES",    "FLOAT1", "FLOAT2", "FLOAT3",   "FLOAT4",       "FLOAT5",
    "FLOAT6",    "FIXED1", "FIXED2", "FIXED3",   "TIME",         "TIMESECS",
    "THOUSANDS", "TENS",   "SECS",   "SCORE",    "SECS_AS_MINS", "MILLISECS",
    "MINUTES",   "POINTS", "VALUE",  "UNSIGNED", "OTHER",        "HUNDREDS"};

} // namespace

struct AuthoringWindow::Impl {
    explicit Impl(LogicTester& logic_tester) : tester(logic_tester) {}

    LogicTester& tester;
    RichPresenceEditor presence;
    LeaderboardEditor leaderboards;

    std::string presence_path_input;
    std::string leaderboard_path_input;
    bool presence_path_initialized = false;
    bool leaderboard_path_initialized = false;
    std::string presence_message;
    std::string leaderboard_message;
    bool presence_message_error = false;
    bool leaderboard_message_error = false;

    uint64_t game_session = 0;
    bool game_loaded = false;
    const MemoryReader* memory = nullptr;
    uint64_t latest_runtime_frame = 0;
    bool presence_script_cache_valid = false;
    uint64_t presence_script_cache_revision = 0;
    std::string cached_normalized_presence;

    struct PresencePreview {
        bool active = false;
        std::filesystem::path path;
        std::string script;
        uint64_t game_session = 0;
    } preview;

    struct LeaderboardTest {
        std::optional<uint32_t> id;
        std::filesystem::path path;
        uint64_t selection_version = 0;
        uint64_t edit_version = 0;
        uint64_t game_session = 0;
        std::string signature;
        std::deque<LogicEvent> events;
    } leaderboard_test;

    enum class FileTarget { Presence, Leaderboards };
    enum class FileOperation { Open, Reload };
    struct FileRequest {
        FileTarget target = FileTarget::Presence;
        FileOperation operation = FileOperation::Open;
        std::filesystem::path path;
    };
    enum class PendingKind { None, File, Quit };
    PendingKind pending_kind = PendingKind::None;
    FileRequest pending_file;
    bool open_confirmation = false;
    bool quit_requested = false;
    std::string confirmation_error;

    void report(FileTarget target, bool ok, std::string message) {
        if (target == FileTarget::Presence) {
            presence_message_error = !ok;
            presence_message = std::move(message);
        } else {
            leaderboard_message_error = !ok;
            leaderboard_message = std::move(message);
        }
    }

    bool dirty(FileTarget target) const {
        return target == FileTarget::Presence ? presence.dirty() : leaderboards.dirty();
    }

    bool can_save(FileTarget target) const {
        return target == FileTarget::Presence ? presence.can_save() : leaderboards.can_save();
    }

    bool save(FileTarget target) {
        std::string error;
        const bool ok =
            target == FileTarget::Presence ? presence.save(error) : leaderboards.save(error);
        report(target, ok, ok ? "Saved local file." : std::move(error));
        if (ok && target == FileTarget::Presence)
            reconcile_presence();
        if (ok && target == FileTarget::Leaderboards)
            reconcile_leaderboard_test();
        return ok;
    }

    bool perform_file_request(const FileRequest& request) {
        std::string error;
        bool ok = false;
        if (request.target == FileTarget::Presence) {
            ok = presence.load(request.path, error);
            if (ok) {
                presence_path_input = presence.path().string();
                presence_path_initialized = true;
                report(request.target, true, "Opened local Rich Presence file.");
                reconcile_presence();
            }
        } else {
            ok = leaderboards.load(request.path, error);
            if (ok) {
                leaderboard_path_input = leaderboards.path().string();
                leaderboard_path_initialized = true;
                report(request.target, true, "Opened local user file.");
                reconcile_leaderboard_test();
            }
        }
        if (!ok)
            report(request.target, false, std::move(error));
        return ok;
    }

    void request_file(FileRequest request) {
        if (request.path.empty()) {
            report(request.target, false, "Enter a file path first.");
            return;
        }
        if (request.operation == FileOperation::Reload) {
            const bool loaded =
                request.target == FileTarget::Presence ? presence.loaded() : leaderboards.loaded();
            if (!loaded) {
                report(request.target, false, "Load a file before reloading it.");
                return;
            }
        }
        if (dirty(request.target)) {
            pending_kind = PendingKind::File;
            pending_file = std::move(request);
            confirmation_error.clear();
            open_confirmation = true;
            return;
        }
        perform_file_request(request);
    }

    bool stop_presence_preview(std::string message = {}) {
        if (!preview.active)
            return true;
        if (tester.rich_presence_script() == preview.script) {
            std::string error;
            if (!tester.set_rich_presence({}, error)) {
                report(FileTarget::Presence, false, "Could not stop the preview: " + error);
                return false;
            }
        }
        preview = {};
        if (!message.empty())
            report(FileTarget::Presence, true, std::move(message));
        return true;
    }

    const std::string& normalized_presence_script() {
        const uint64_t revision = presence.revision();
        if (!presence_script_cache_valid || presence_script_cache_revision != revision) {
            cached_normalized_presence = normalize_rich_presence(presence.text());
            presence_script_cache_revision = revision;
            presence_script_cache_valid = true;
        }
        return cached_normalized_presence;
    }

    void reconcile_presence() {
        if (!preview.active)
            return;
        if (!game_loaded || game_session != preview.game_session) {
            stop_presence_preview("Preview stopped because the game session changed.");
        } else if (presence.path() != preview.path) {
            stop_presence_preview("Preview stopped because the loaded file changed.");
        } else if (normalized_presence_script() != preview.script) {
            stop_presence_preview("Preview stopped because the script changed.");
        }
    }

    void start_presence_preview() {
        if (!game_loaded || !memory) {
            report(FileTarget::Presence, false, "Load game content before starting a preview.");
            return;
        }
        if (!presence.loaded()) {
            report(FileTarget::Presence, false, "Open a Rich Presence file before previewing it.");
            return;
        }
        const std::string& script = normalized_presence_script();
        if (script.empty()) {
            report(FileTarget::Presence, false,
                   "An empty Rich Presence draft cannot be previewed.");
            return;
        }
        if (!presence.validation_error().empty()) {
            report(FileTarget::Presence, false, presence.validation_error());
            return;
        }
        std::string error;
        if (!tester.set_rich_presence(script, error)) {
            report(FileTarget::Presence, false, std::move(error));
            return;
        }
        preview.active = true;
        preview.path = presence.path();
        preview.script = script;
        preview.game_session = game_session;
        report(FileTarget::Presence, true, "Rich Presence preview started.");
    }

    void stop_leaderboard_test(bool remove_from_runtime, std::string message = {}) {
        const auto id = leaderboard_test.id;
        if (remove_from_runtime && id)
            tester.remove_leaderboard(*id);
        leaderboard_test = {};
        if (!message.empty())
            report(FileTarget::Leaderboards, true, std::move(message));
    }

    bool runtime_has_leaderboard(uint32_t id) const {
        const auto& definitions = tester.leaderboards();
        return std::any_of(definitions.begin(), definitions.end(),
                           [id](const TestedLeaderboard& item) { return item.id == id; });
    }

    void reconcile_leaderboard_test() {
        if (!leaderboard_test.id)
            return;
        const uint32_t id = *leaderboard_test.id;
        if (!runtime_has_leaderboard(id)) {
            stop_leaderboard_test(false, "Leaderboard test was removed from the runtime.");
            return;
        }
        if (!game_loaded || game_session != leaderboard_test.game_session) {
            stop_leaderboard_test(true,
                                  "Leaderboard test stopped because the game session changed.");
            return;
        }
        if (!leaderboards.loaded() || leaderboards.path() != leaderboard_test.path ||
            leaderboards.selection_version() != leaderboard_test.selection_version) {
            stop_leaderboard_test(
                true, "Leaderboard test stopped because the file or selection changed.");
            return;
        }
        if (leaderboards.edit_version() != leaderboard_test.edit_version) {
            const std::string signature = leaderboard_signature(leaderboards.draft());
            if (signature != leaderboard_test.signature) {
                stop_leaderboard_test(
                    true, "Leaderboard test stopped because its logic or format changed.");
                return;
            }
            // Metadata edits increment edit_version too. Preserve the active runtime test when
            // only title, description, or lower-is-better changed.
            leaderboard_test.edit_version = leaderboards.edit_version();
        }
    }

    void start_leaderboard_test() {
        if (!game_loaded || !memory) {
            report(FileTarget::Leaderboards, false, "Load game content before starting a test.");
            return;
        }
        if (!leaderboards.loaded()) {
            report(FileTarget::Leaderboards, false,
                   "Open a user file before testing a leaderboard.");
            return;
        }
        std::string error;
        const auto definition = leaderboards.selected_definition(error);
        if (!definition) {
            report(FileTarget::Leaderboards, false, std::move(error));
            return;
        }
        const auto id = tester.add_leaderboard(*definition, error);
        if (!id) {
            report(FileTarget::Leaderboards, false, std::move(error));
            return;
        }

        leaderboard_test.id = *id;
        leaderboard_test.path = leaderboards.path();
        leaderboard_test.selection_version = leaderboards.selection_version();
        leaderboard_test.edit_version = leaderboards.edit_version();
        leaderboard_test.game_session = game_session;
        leaderboard_test.signature = leaderboard_signature(leaderboards.draft());
        leaderboard_test.events.clear();

        // The window API does not receive the core frame counter. Use the newest frame observed
        // through record_events(); before the first evaluated frame this is zero.
        const auto events = tester.validate_addresses(*memory, latest_runtime_frame);
        record_events(events);
        report(FileTarget::Leaderboards, true, "Offline leaderboard test started.");
    }

    void record_events(const std::vector<LogicEvent>& events) {
        for (const auto& event : events) {
            latest_runtime_frame = std::max(latest_runtime_frame, event.frame);
            if (!leaderboard_test.id || event.id != *leaderboard_test.id)
                continue;
            leaderboard_test.events.push_back(event);
            while (leaderboard_test.events.size() > 50)
                leaderboard_test.events.pop_front();
        }
        if (leaderboard_test.id && !runtime_has_leaderboard(*leaderboard_test.id))
            stop_leaderboard_test(false, "Leaderboard test was removed from the runtime.");
    }

    void draw_presence(bool& open, const std::filesystem::path& suggested_file) {
        if (!open)
            return;
        const auto* viewport = ImGui::GetMainViewport();
        prepare_window(*viewport, ImVec2(28.0f, 32.0f),
                       ImVec2(cw::ui_px(680.0f), cw::ui_px(720.0f)));
        if (ImGui::Begin("Rich Presence authoring", &open)) {
            if (!ImGui::IsWindowDocked())
                clamp_active_window(*viewport);
            ImGui::TextWrapped("Local Rich Presence file");
            ImGui::TextWrapped("File path");
            ImGui::SetNextItemWidth(-1.0f);
            if (text_input("##presence_path", presence_path_input))
                presence_path_initialized = true;
            if (presence.loaded()) {
                ImGui::TextWrapped("Loaded: %s", presence.path().string().c_str());
                if (presence.dirty())
                    ImGui::TextWrapped("Unsaved changes");
            } else {
                disabled_wrapped("No file loaded");
            }

            enum class Action { None, Open, Current, Reload, Save, Start, Stop };
            Action action = Action::None;
            if (ImGui::Button("Open / Create"))
                action = Action::Open;
            button_wrap("Current game");
            ImGui::BeginDisabled(suggested_file.empty());
            if (ImGui::Button("Current game"))
                action = Action::Current;
            ImGui::EndDisabled();
            button_wrap("Reload");
            ImGui::BeginDisabled(!presence.loaded());
            if (ImGui::Button("Reload"))
                action = Action::Reload;
            ImGui::EndDisabled();
            button_wrap("Save");
            ImGui::BeginDisabled(!presence.can_save());
            if (ImGui::Button("Save"))
                action = Action::Save;
            ImGui::EndDisabled();

            ImGui::Separator();
            std::string script = presence.text();
            ImGui::TextWrapped("Rich Presence script");
            ImGui::SetNextItemWidth(-1.0f);
            const bool script_changed = text_input("##presence_script", script, true, 14.0f);
            if (script_changed)
                presence.set_text(std::move(script));
            reconcile_presence();

            if (!presence.loaded())
                disabled_wrapped("Open or create a local Rich Presence file to begin.");
            if (!presence.validation_error().empty())
                error_text(presence.validation_error());

            if (preview.active) {
                if (ImGui::Button("Stop preview"))
                    action = Action::Stop;
            } else {
                const std::string& normalized = normalized_presence_script();
                const bool disabled = !game_loaded || !presence.loaded() || normalized.empty() ||
                                      !presence.validation_error().empty();
                ImGui::BeginDisabled(disabled);
                if (ImGui::Button("Start preview"))
                    action = Action::Start;
                ImGui::EndDisabled();
            }
            if (preview.active) {
                button_wrap("Preview active");
                ImGui::TextWrapped("Preview active");
            }
            if (const auto frame = tester.rich_presence_frame())
                ImGui::TextWrapped("Cached output from frame %llu:",
                                   static_cast<unsigned long long>(*frame));
            else
                disabled_wrapped("No cached Rich Presence frame yet.");
            const std::string& output = tester.rich_presence_output();
            if (!output.empty())
                ImGui::TextWrapped("%s", output.c_str());
            else
                disabled_wrapped("(empty output)");
            if (tester.rich_presence_truncated())
                ImGui::TextWrapped("The cached output was truncated to fit its runtime buffer.");

            if (!presence_message.empty()) {
                if (presence_message_error)
                    error_text(presence_message);
                else
                    ImGui::TextWrapped("%s", presence_message.c_str());
            }

            // Execute commands after the path and script widgets have consumed this frame's text.
            switch (action) {
            case Action::Open:
                request_file({FileTarget::Presence, FileOperation::Open,
                              std::filesystem::path(presence_path_input)});
                break;
            case Action::Current:
                presence_path_input = suggested_file.string();
                presence_path_initialized = true;
                request_file({FileTarget::Presence, FileOperation::Open, suggested_file});
                break;
            case Action::Reload:
                request_file({FileTarget::Presence, FileOperation::Reload, presence.path()});
                break;
            case Action::Save:
                save(FileTarget::Presence);
                break;
            case Action::Start:
                start_presence_preview();
                break;
            case Action::Stop:
                stop_presence_preview("Rich Presence preview stopped.");
                break;
            case Action::None:
                break;
            }
        }
        ImGui::End();
    }

    void draw_leaderboard_list(std::optional<size_t>& requested_selection, bool& add_requested,
                               bool& delete_requested) {
        ImGui::TextWrapped("Leaderboards in this user file");
        if (leaderboards.loaded()) {
            const auto& entries = leaderboards.document().entries;
            size_t board_number = 0;
            for (size_t index = 0; index < entries.size(); ++index) {
                const auto* board = std::get_if<LocalLeaderboard>(&entries[index].asset);
                if (!board)
                    continue;
                ++board_number;
                const bool selected = leaderboards.selection() == index;
                const std::string& title = selected ? leaderboards.draft().title : board->title;
                const std::string label = "L" + std::to_string(board->id) + "  " +
                                          (title.empty() ? "(untitled leaderboard)" : title) +
                                          "###leaderboard_" + std::to_string(board->id) + "_" +
                                          std::to_string(index);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None,
                                      ImVec2(-1.0f, 0.0f)))
                    requested_selection = index;
            }
            if (board_number == 0)
                disabled_wrapped("This file has no leaderboard records.");
        } else {
            disabled_wrapped("Open a user file to list its leaderboard records.");
        }

        ImGui::BeginDisabled(!leaderboards.loaded());
        if (ImGui::Button("New leaderboard"))
            add_requested = true;
        button_wrap("Delete selected");
        ImGui::BeginDisabled(!leaderboards.selection().has_value());
        if (ImGui::Button("Delete selected"))
            delete_requested = true;
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }

    void draw_leaderboard_fields(LeaderboardDraft& draft, bool& changed) {
        if (!leaderboards.loaded() || !leaderboards.selection())
            return;
        ImGui::Separator();
        ImGui::TextWrapped("Selected leaderboard");
        changed |= [&] {
            ImGui::TextWrapped("Title");
            ImGui::SetNextItemWidth(-1.0f);
            return text_input("##leaderboard_title", draft.title);
        }();
        changed |= [&] {
            ImGui::TextWrapped("Description");
            ImGui::SetNextItemWidth(-1.0f);
            return text_input("##leaderboard_description", draft.description, true, 3.0f);
        }();
        changed |= [&] {
            ImGui::TextWrapped("Start trigger (STA)");
            ImGui::SetNextItemWidth(-1.0f);
            return text_input("##leaderboard_start", draft.start, true, 3.0f);
        }();
        changed |= [&] {
            ImGui::TextWrapped("Cancel trigger (CAN)");
            ImGui::SetNextItemWidth(-1.0f);
            return text_input("##leaderboard_cancel", draft.cancel, true, 3.0f);
        }();
        changed |= [&] {
            ImGui::TextWrapped("Submit trigger (SUB)");
            ImGui::SetNextItemWidth(-1.0f);
            return text_input("##leaderboard_submit", draft.submit, true, 3.0f);
        }();
        changed |= [&] {
            ImGui::TextWrapped("Value expression (VAL)");
            ImGui::SetNextItemWidth(-1.0f);
            return text_input("##leaderboard_value", draft.value, true, 3.0f);
        }();

        ImGui::TextWrapped("Format");
        const char* format_preview = draft.format.c_str();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##leaderboard_format", format_preview)) {
            for (const char* format : kLeaderboardFormats) {
                const bool selected = draft.format == format;
                if (ImGui::Selectable(format, selected)) {
                    draft.format = format;
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        changed |= ImGui::Checkbox("Lower is better", &draft.lower_is_better);
    }

    void draw_leaderboard_test_controls() {
        ImGui::Separator();
        const auto id = leaderboard_test.id;
        bool start_requested = false;
        bool stop_requested = false;
        bool rearm_requested = false;
        if (!id) {
            const bool valid_definition =
                leaderboards.selection().has_value() && leaderboards.validation_error().empty();
            ImGui::BeginDisabled(!game_loaded || !leaderboards.loaded() || !valid_definition);
            if (ImGui::Button("Start offline test"))
                start_requested = true;
            ImGui::EndDisabled();
            if (!game_loaded)
                disabled_wrapped("Load game content before testing this leaderboard.");
        } else {
            if (ImGui::Button("Stop test"))
                stop_requested = true;
            button_wrap("Rearm test");
            const auto status = tester.leaderboard_snapshot(*id);
            ImGui::BeginDisabled(!status || status->state == LeaderboardState::Disabled);
            if (ImGui::Button("Rearm test"))
                rearm_requested = true;
            ImGui::EndDisabled();

            if (status) {
                ImGui::TextWrapped("Test state: %s", leaderboard_state_name(status->state));
                ImGui::TextWrapped("Value: %s (%d)", status->formatted_value.c_str(),
                                   status->value);
                if (status->last_frame)
                    ImGui::TextWrapped("Last runtime frame: %llu",
                                       static_cast<unsigned long long>(*status->last_frame));
                else
                    disabled_wrapped("No runtime evaluation yet.");
                if (status->state == LeaderboardState::Disabled)
                    error_text(
                        "A required address is unreadable. Reload the game before retesting.");
            } else {
                disabled_wrapped("This test is no longer present in the shared runtime.");
            }

            if (ImGui::BeginChild("##leaderboard_events", ImVec2(0.0f, cw::ui_px(130.0f)),
                                  ImGuiChildFlags_Borders)) {
                if (leaderboard_test.events.empty())
                    disabled_wrapped("No events for this leaderboard yet.");
                for (auto event = leaderboard_test.events.rbegin();
                     event != leaderboard_test.events.rend(); ++event) {
                    if (event->kind == LogicEventKind::LeaderboardDisabled)
                        ImGui::TextWrapped("Frame %llu  %s  address 0x%06X",
                                           static_cast<unsigned long long>(event->frame),
                                           logic_event_name(event->kind),
                                           static_cast<uint32_t>(event->value));
                    else if (event->kind == LogicEventKind::LeaderboardCanceled)
                        ImGui::TextWrapped("Frame %llu  %s",
                                           static_cast<unsigned long long>(event->frame),
                                           logic_event_name(event->kind));
                    else
                        ImGui::TextWrapped("Frame %llu  %s  value %d",
                                           static_cast<unsigned long long>(event->frame),
                                           logic_event_name(event->kind), event->value);
                }
            }
            ImGui::EndChild();
        }

        if (!leaderboard_message.empty()) {
            if (leaderboard_message_error)
                error_text(leaderboard_message);
            else
                ImGui::TextWrapped("%s", leaderboard_message.c_str());
        }

        if (stop_requested) {
            stop_leaderboard_test(true, "Offline leaderboard test stopped.");
        } else if (rearm_requested && id) {
            if (tester.rearm_leaderboard(*id))
                report(FileTarget::Leaderboards, true, "Leaderboard test rearmed.");
            else
                report(FileTarget::Leaderboards, false,
                       "Could not rearm a disabled or missing leaderboard test.");
        } else if (start_requested) {
            start_leaderboard_test();
        }
    }

    void draw_leaderboards(bool& open, const std::filesystem::path& suggested_file) {
        if (!open)
            return;
        const auto* viewport = ImGui::GetMainViewport();
        prepare_window(*viewport, ImVec2(64.0f, 64.0f),
                       ImVec2(cw::ui_px(740.0f), cw::ui_px(840.0f)));
        if (ImGui::Begin("Leaderboard authoring", &open)) {
            if (!ImGui::IsWindowDocked())
                clamp_active_window(*viewport);
            ImGui::TextWrapped("Local RACache User.txt file");
            ImGui::TextWrapped("File path");
            ImGui::SetNextItemWidth(-1.0f);
            if (text_input("##leaderboard_path", leaderboard_path_input))
                leaderboard_path_initialized = true;
            if (leaderboards.loaded()) {
                ImGui::TextWrapped("Loaded: %s", leaderboards.path().string().c_str());
                if (leaderboards.dirty())
                    ImGui::TextWrapped("Unsaved changes");
            } else {
                disabled_wrapped("No file loaded");
            }

            enum class Action { None, Open, Current, Reload, Save, Add, Delete, Select };
            Action action = Action::None;
            std::optional<size_t> requested_selection;
            if (ImGui::Button("Open / Create"))
                action = Action::Open;
            button_wrap("Current game");
            ImGui::BeginDisabled(suggested_file.empty());
            if (ImGui::Button("Current game"))
                action = Action::Current;
            ImGui::EndDisabled();
            button_wrap("Reload");
            ImGui::BeginDisabled(!leaderboards.loaded());
            if (ImGui::Button("Reload"))
                action = Action::Reload;
            ImGui::EndDisabled();
            button_wrap("Save");
            ImGui::BeginDisabled(!leaderboards.can_save());
            if (ImGui::Button("Save"))
                action = Action::Save;
            ImGui::EndDisabled();

            ImGui::Separator();
            bool add_requested = false;
            bool delete_requested = false;
            draw_leaderboard_list(requested_selection, add_requested, delete_requested);
            if (add_requested)
                action = Action::Add;
            if (delete_requested)
                action = Action::Delete;
            if (requested_selection)
                action = Action::Select;

            LeaderboardDraft edited_draft = leaderboards.draft();
            bool draft_changed = false;
            draw_leaderboard_fields(edited_draft, draft_changed);
            if (draft_changed) {
                leaderboards.draft() = std::move(edited_draft);
                leaderboards.changed();
            }
            reconcile_leaderboard_test();

            if (leaderboards.loaded() && leaderboards.selection() &&
                !leaderboards.validation_error().empty())
                error_text(leaderboards.validation_error());
            if (!leaderboards.loaded())
                disabled_wrapped("Open or create a user file to add and edit leaderboards.");

            switch (action) {
            case Action::Open:
                request_file({FileTarget::Leaderboards, FileOperation::Open,
                              std::filesystem::path(leaderboard_path_input)});
                break;
            case Action::Current:
                leaderboard_path_input = suggested_file.string();
                leaderboard_path_initialized = true;
                request_file({FileTarget::Leaderboards, FileOperation::Open, suggested_file});
                break;
            case Action::Reload:
                request_file(
                    {FileTarget::Leaderboards, FileOperation::Reload, leaderboards.path()});
                break;
            case Action::Save:
                save(FileTarget::Leaderboards);
                break;
            case Action::Add: {
                std::string error;
                if (leaderboards.add_leaderboard(error))
                    report(FileTarget::Leaderboards, true, "Added a local leaderboard draft.");
                else
                    report(FileTarget::Leaderboards, false, std::move(error));
                reconcile_leaderboard_test();
                break;
            }
            case Action::Delete: {
                std::string error;
                if (leaderboards.remove_selected(error))
                    report(FileTarget::Leaderboards, true,
                           "Removed the selected leaderboard draft.");
                else
                    report(FileTarget::Leaderboards, false, std::move(error));
                reconcile_leaderboard_test();
                break;
            }
            case Action::Select: {
                std::string error;
                if (leaderboards.select(*requested_selection, error))
                    report(FileTarget::Leaderboards, true, "Selected a leaderboard.");
                else
                    report(FileTarget::Leaderboards, false, std::move(error));
                reconcile_leaderboard_test();
                break;
            }
            case Action::None:
                break;
            }

            draw_leaderboard_test_controls();
        }
        ImGui::End();
    }

    bool pending_can_save() const {
        if (pending_kind == PendingKind::Quit)
            return (!presence.dirty() || presence.can_save()) &&
                   (!leaderboards.dirty() || leaderboards.can_save());
        if (pending_kind == PendingKind::File)
            return can_save(pending_file.target);
        return false;
    }

    bool save_pending() {
        if (pending_kind == PendingKind::File)
            return save(pending_file.target);
        if (pending_kind != PendingKind::Quit)
            return false;
        if (presence.dirty() && !save(FileTarget::Presence)) {
            confirmation_error = presence_message;
            return false;
        }
        if (leaderboards.dirty() && !save(FileTarget::Leaderboards)) {
            confirmation_error = leaderboard_message;
            return false;
        }
        return true;
    }

    void discard_pending() {
        if (pending_kind == PendingKind::File) {
            if (pending_file.target == FileTarget::Presence)
                presence.discard_changes();
            else
                leaderboards.discard_changes();
            if (pending_file.target == FileTarget::Presence)
                reconcile_presence();
            else
                reconcile_leaderboard_test();
        } else if (pending_kind == PendingKind::Quit) {
            if (presence.dirty())
                presence.discard_changes();
            if (leaderboards.dirty())
                leaderboards.discard_changes();
            reconcile_presence();
            reconcile_leaderboard_test();
        }
    }

    void draw_confirmation() {
        if (open_confirmation) {
            ImGui::OpenPopup("Unsaved authoring changes");
            open_confirmation = false;
        }
        const auto* viewport = ImGui::GetMainViewport();
        const ImVec2 maximum(std::max(1.0f, viewport->WorkSize.x - 20.0f),
                             std::max(1.0f, viewport->WorkSize.y - 20.0f));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(cw::ui_px(340.0f), maximum.x), std::min(cw::ui_px(180.0f), maximum.y)),
            maximum);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(cw::ui_px(500.0f), maximum.x), std::min(cw::ui_px(240.0f), maximum.y)),
            ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Unsaved authoring changes", nullptr))
            return;

        clamp_active_window(*viewport);
        if (pending_kind == PendingKind::Quit) {
            ImGui::TextWrapped("Save both dirty local authoring drafts before quitting?");
        } else if (pending_kind == PendingKind::File) {
            const char* name =
                pending_file.target == FileTarget::Presence ? "Rich Presence" : "leaderboard";
            ImGui::TextWrapped("Save the %s draft before continuing?", name);
        }

        ImGui::BeginDisabled(!pending_can_save());
        if (ImGui::Button("Save")) {
            const PendingKind action = pending_kind;
            const FileRequest file = pending_file;
            if (save_pending()) {
                pending_kind = PendingKind::None;
                confirmation_error.clear();
                if (action == PendingKind::Quit)
                    quit_requested = true;
                else
                    perform_file_request(file);
                ImGui::CloseCurrentPopup();
            } else if (confirmation_error.empty()) {
                confirmation_error =
                    action == PendingKind::File
                        ? (file.target == FileTarget::Presence ? presence_message
                                                               : leaderboard_message)
                        : "Saving the local drafts failed.";
            }
        }
        ImGui::EndDisabled();
        button_wrap("Discard");
        if (ImGui::Button("Discard")) {
            const PendingKind action = pending_kind;
            const FileRequest file = pending_file;
            discard_pending();
            pending_kind = PendingKind::None;
            confirmation_error.clear();
            if (action == PendingKind::Quit)
                quit_requested = true;
            else
                perform_file_request(file);
            ImGui::CloseCurrentPopup();
        }
        button_wrap("Cancel");
        if (ImGui::Button("Cancel")) {
            pending_kind = PendingKind::None;
            confirmation_error.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!confirmation_error.empty())
            error_text(confirmation_error);
        if (pending_kind == PendingKind::File) {
            if (pending_file.target == FileTarget::Presence && presence_message_error)
                error_text(presence_message);
            if (pending_file.target == FileTarget::Leaderboards && leaderboard_message_error)
                error_text(leaderboard_message);
        }
        ImGui::EndPopup();
    }
};

AuthoringWindow::AuthoringWindow(LogicTester& tester) : impl_(std::make_unique<Impl>(tester)) {}
AuthoringWindow::~AuthoringWindow() {
    impl_->stop_presence_preview();
    impl_->stop_leaderboard_test(true);
}

void AuthoringWindow::draw(const std::filesystem::path& suggested_rich_file,
                           const std::filesystem::path& suggested_user_file) {
    if (!impl_->presence_path_initialized && !suggested_rich_file.empty()) {
        impl_->presence_path_input = suggested_rich_file.string();
        impl_->presence_path_initialized = true;
    }
    if (!impl_->leaderboard_path_initialized && !suggested_user_file.empty()) {
        impl_->leaderboard_path_input = suggested_user_file.string();
        impl_->leaderboard_path_initialized = true;
    }
    impl_->draw_presence(rich_presence_open, suggested_rich_file);
    impl_->draw_leaderboards(leaderboards_open, suggested_user_file);
    impl_->draw_confirmation();
}

void AuthoringWindow::sync_live(uint64_t game_session, bool game_loaded, const MemoryReader& memory,
                                uint64_t frame) {
    impl_->game_session = game_session;
    impl_->game_loaded = game_loaded;
    impl_->memory = game_loaded ? &memory : nullptr;
    impl_->latest_runtime_frame = frame;
    impl_->reconcile_presence();
    impl_->reconcile_leaderboard_test();
}

void AuthoringWindow::record_events(const std::vector<LogicEvent>& events) {
    impl_->record_events(events);
}

bool AuthoringWindow::allow_quit() {
    if (impl_->pending_kind == Impl::PendingKind::File)
        return false;
    if (!impl_->presence.dirty() && !impl_->leaderboards.dirty())
        return true;
    if (impl_->pending_kind != Impl::PendingKind::Quit) {
        impl_->pending_kind = Impl::PendingKind::Quit;
        impl_->confirmation_error.clear();
        impl_->open_confirmation = true;
    }
    return false;
}

bool AuthoringWindow::take_quit_request() {
    return std::exchange(impl_->quit_requested, false);
}

RichPresenceEditor& AuthoringWindow::presence_session() {
    return impl_->presence;
}

LeaderboardEditor& AuthoringWindow::leaderboard_session() {
    return impl_->leaderboards;
}

} // namespace cw
