#include "achievement_editor_window.hpp"
#include "cw/devtools/achievement_editor.hpp"
#include "cw/devtools/code_note_draft.hpp"
#include "cw/devtools/editor_live_test.hpp"
#include "ui_scale.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace cw {
namespace {

template <typename T> struct Choice {
    T value;
    const char* name;
};
constexpr std::array kFlags{
    Choice{LogicFlag::Standard, "Standard"},
    Choice{LogicFlag::PauseIf, "PauseIf"},
    Choice{LogicFlag::ResetIf, "ResetIf"},
    Choice{LogicFlag::AddSource, "AddSource"},
    Choice{LogicFlag::SubSource, "SubSource"},
    Choice{LogicFlag::AddHits, "AddHits"},
    Choice{LogicFlag::SubHits, "SubHits"},
    Choice{LogicFlag::AndNext, "AndNext"},
    Choice{LogicFlag::OrNext, "OrNext"},
    Choice{LogicFlag::Measured, "Measured"},
    Choice{LogicFlag::MeasuredPercent, "Measured %"},
    Choice{LogicFlag::MeasuredIf, "MeasuredIf"},
    Choice{LogicFlag::AddAddress, "AddAddress"},
    Choice{LogicFlag::Trigger, "Trigger"},
    Choice{LogicFlag::ResetNextIf, "ResetNextIf"},
    Choice{LogicFlag::Remember, "Remember"},
};
constexpr std::array kTypes{
    Choice{LogicOperandType::Address, "Memory"},    Choice{LogicOperandType::Delta, "Delta"},
    Choice{LogicOperandType::Prior, "Prior"},       Choice{LogicOperandType::Bcd, "BCD"},
    Choice{LogicOperandType::Inverted, "Inverted"}, Choice{LogicOperandType::Integer, "Value"},
    Choice{LogicOperandType::Float, "Float value"}, Choice{LogicOperandType::Recall, "Recall"},
};
constexpr std::array kSizes{
    Choice{LogicMemorySize::Bit0, "Bit 0"},
    Choice{LogicMemorySize::Bit1, "Bit 1"},
    Choice{LogicMemorySize::Bit2, "Bit 2"},
    Choice{LogicMemorySize::Bit3, "Bit 3"},
    Choice{LogicMemorySize::Bit4, "Bit 4"},
    Choice{LogicMemorySize::Bit5, "Bit 5"},
    Choice{LogicMemorySize::Bit6, "Bit 6"},
    Choice{LogicMemorySize::Bit7, "Bit 7"},
    Choice{LogicMemorySize::LowNibble, "Low nibble"},
    Choice{LogicMemorySize::HighNibble, "High nibble"},
    Choice{LogicMemorySize::EightBit, "8-bit"},
    Choice{LogicMemorySize::SixteenBit, "16-bit"},
    Choice{LogicMemorySize::TwentyFourBit, "24-bit"},
    Choice{LogicMemorySize::ThirtyTwoBit, "32-bit"},
    Choice{LogicMemorySize::SixteenBitBE, "16-bit BE"},
    Choice{LogicMemorySize::TwentyFourBitBE, "24-bit BE"},
    Choice{LogicMemorySize::ThirtyTwoBitBE, "32-bit BE"},
    Choice{LogicMemorySize::BitCount, "Bit count"},
    Choice{LogicMemorySize::Float, "Float"},
    Choice{LogicMemorySize::FloatBE, "Float BE"},
    Choice{LogicMemorySize::Double32, "Double32"},
    Choice{LogicMemorySize::Double32BE, "Double32 BE"},
    Choice{LogicMemorySize::Mbf32, "MBF32"},
    Choice{LogicMemorySize::Mbf32LE, "MBF32 LE"},
};
constexpr std::array kComparisons{
    Choice{LogicOperator::Equal, "="},   Choice{LogicOperator::NotEqual, "!="},
    Choice{LogicOperator::Less, "<"},    Choice{LogicOperator::LessOrEqual, "<="},
    Choice{LogicOperator::Greater, ">"}, Choice{LogicOperator::GreaterOrEqual, ">="},
};
constexpr std::array kArithmetic{
    Choice{LogicOperator::None, "None"},    Choice{LogicOperator::Multiply, "*"},
    Choice{LogicOperator::Divide, "/"},     Choice{LogicOperator::BitwiseAnd, "&"},
    Choice{LogicOperator::BitwiseXor, "^"}, Choice{LogicOperator::Modulo, "%"},
    Choice{LogicOperator::Add, "+"},        Choice{LogicOperator::Subtract, "-"},
};
constexpr std::array<uint32_t, 10> kPoints{0, 1, 2, 3, 4, 5, 10, 25, 50, 100};
constexpr std::array kAchievementTypes{
    Choice<std::string_view>{"", "Standard"},
    Choice<std::string_view>{"missable", "Missable"},
    Choice<std::string_view>{"progression", "Progression"},
    Choice<std::string_view>{"win_condition", "Win condition"},
};

int resize_string(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto& text = *static_cast<std::string*>(data->UserData);
        text.resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = text.data();
    }
    return 0;
}
bool text_input(const char* label, std::string& text, bool multiline = false) {
    text.reserve(text.size() + 1);
    if (multiline)
        return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1,
                                         ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 3),
                                         ImGuiInputTextFlags_CallbackResize, resize_string, &text);
    return ImGui::InputText(label, text.data(), text.capacity() + 1,
                            ImGuiInputTextFlags_CallbackResize, resize_string, &text);
}

template <typename T, size_t N>
bool combo(const char* id, T& value, const std::array<Choice<T>, N>& choices) {
    const char* preview = "Preserved value";
    for (const auto& choice : choices)
        if (choice.value == value)
            preview = choice.name;
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(id, preview)) {
        for (const auto& choice : choices) {
            const bool selected = value == choice.value;
            if (ImGui::Selectable(choice.name, selected)) {
                value = choice.value;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}
bool memory_operand(LogicOperandType type) {
    return type == LogicOperandType::Address || type == LogicOperandType::Delta ||
           type == LogicOperandType::Prior || type == LogicOperandType::Bcd ||
           type == LogicOperandType::Inverted;
}
bool modifying_flag(LogicFlag flag) {
    return flag == LogicFlag::AddSource || flag == LogicFlag::SubSource ||
           flag == LogicFlag::AddAddress || flag == LogicFlag::Remember;
}
std::string number_string(uint32_t value, bool hex) {
    if (!hex)
        return std::to_string(value);
    std::array<char, 8> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
    if (ec != std::errc{})
        return {};
    const size_t count = static_cast<size_t>(end - buffer.data());
    return std::string(count < 6 ? 6 - count : 0, '0') + std::string(buffer.data(), end);
}
bool parse_uint(std::string_view text, uint32_t& value, bool hex, bool allow_negative = false) {
    if (text.empty())
        return false;
    if (allow_negative && text.front() == '-') {
        int64_t signed_value = 0;
        const auto [end, ec] =
            std::from_chars(text.data(), text.data() + text.size(), signed_value);
        if (ec != std::errc{} || end != text.data() + text.size() ||
            signed_value < std::numeric_limits<int32_t>::min() || signed_value > 0)
            return false;
        value = static_cast<uint32_t>(signed_value);
        return true;
    }
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        hex = true;
    }
    if (text.empty())
        return false;
    const auto [end, ec] =
        std::from_chars(text.data(), text.data() + text.size(), value, hex ? 16 : 10);
    return ec == std::errc{} && end == text.data() + text.size();
}
void error_text(const std::string& text) {
    if (text.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.48f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}
void continue_row(const char* next_label) {
    const auto& style = ImGui::GetStyle();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float width = ImGui::CalcTextSize(next_label).x + 2 * style.FramePadding.x;
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
        ImGui::SameLine();
}
std::string input_location(std::string_view key) {
    const auto first = key.find('/');
    const auto second = key.find('/', first + 1);
    size_t row = 0;
    std::from_chars(key.data() + first + 1, key.data() + second, row);
    const std::string where =
        key.substr(0, first) == "0" ? "Core" : "Alt " + std::string(key.substr(0, first));
    const auto part = key.substr(second + 1);
    return where + ", condition " + std::to_string(row + 1) + ", " +
           (part == "L"   ? "left operand"
            : part == "R" ? "right operand"
                          : "hit target");
}
LogicCondition default_condition() {
    LogicCondition condition;
    condition.left.type = LogicOperandType::Address;
    condition.left.size = LogicMemorySize::EightBit;
    return condition;
}

} // namespace

struct AchievementEditorWindow::Impl {
    AchievementEditor editor;
    EditorLiveTest live;
    CodeNotes& notes;
    CodeNoteDraft note_draft;
    bool show_note_popup = false;
    bool note_quit_pending = false;
    std::string note_message;
    Impl(LogicTester& tester, CodeNotes& code_notes) : live(tester), notes(code_notes) {}
    uint64_t game_session = 0;
    bool game_loaded = false;
    const MemoryReader* memory = nullptr;
    std::optional<AchievementStatus> live_status;
    bool start_live_requested = false;
    std::string path_input;
    bool path_initialized = false;
    std::string message;
    bool message_error = false;
    uint64_t revision = std::numeric_limits<uint64_t>::max();
    size_t group = 0;
    size_t requested_tab = 0;
    bool select_tab = false;
    struct Buffer {
        std::string text;
        std::string error;
    };
    std::map<std::string, Buffer> buffers;
    struct HiddenTarget {
        std::optional<LogicOperand> right;
        std::optional<uint32_t> hits;
    };
    std::map<std::pair<size_t, size_t>, HiddenTarget> hidden_targets;
    enum class Pending { None, Open, Quit };
    Pending pending = Pending::None;
    bool show_confirmation = false;
    bool quit_requested = false;
    std::filesystem::path pending_path;
    std::string pending_title;

    void note_header() {
        if (notes.game_id())
            ImGui::Text("Code notes: game %u (Status)", *notes.game_id());
        else
            ImGui::TextDisabled("Set a game ID in Status to use code notes.");
        if (!notes.user_error().empty())
            error_text(notes.user_error());
        if (!notes.server_error().empty())
            error_text(notes.server_error());
    }

    void operand_note(const LogicGroup& selected_group, size_t row, bool right,
                      const std::string& buffer_key) {
        const auto& condition = selected_group.conditions[row];
        const auto* operand =
            right ? (condition.right ? &*condition.right : nullptr) : &condition.left;
        if (!operand || !memory_operand(operand->type))
            return;
        const auto address = achievement_note_address(selected_group, row, right);
        const auto buffer = buffers.find(buffer_key);
        if (buffer != buffers.end() && !buffer->second.error.empty()) {
            ImGui::TextDisabled("Invalid address");
            return;
        }
        if (!address) {
            if (row && selected_group.conditions[row - 1].flag == LogicFlag::AddAddress) {
                ImGui::TextDisabled("Indirect offset");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Notes require a direct address. This offset is not the "
                                      "resolved destination of the pointer.");
            }
            return;
        }
        const auto* note = notes.display_note_at(*address);
        ImGui::BeginDisabled(!notes.game_id());
        if (ImGui::SmallButton(note ? "Edit note" : "Add note")) {
            if (note_draft.begin(notes, *address, note_message)) {
                show_note_popup = true;
                note_quit_pending = false;
            }
        }
        ImGui::EndDisabled();
        if (note) {
            // Exact-address notes only; no interpretation of structured/multibyte notes.
            const auto end = note->note.find_first_of("\r\n");
            const size_t line_size = end == std::string::npos ? note->note.size() : end;
            size_t preview_size = std::min<size_t>(line_size, 160);
            // Keep the preview bounded without cutting through a UTF-8 code point.
            while (preview_size < line_size && preview_size > 0 &&
                   (static_cast<unsigned char>(note->note[preview_size]) & 0xC0) == 0x80)
                --preview_size;
            const std::string_view preview(note->note.data(), preview_size);
            ImGui::TextUnformatted(preview.data(), preview.data() + preview.size());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
                ImGui::Text("Game %u, address 0x%06X", *notes.game_id(), *address);
                ImGui::TextUnformatted(notes.user_note_at(*address) ? "Local note"
                                                                    : "Cached server note");
                if (!note->author.empty())
                    ImGui::Text("By %s", note->author.c_str());
                ImGui::TextUnformatted(note->note.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        } else
            ImGui::TextDisabled("No note");
    }

    void note_dialog() {
        if (std::exchange(show_note_popup, false))
            ImGui::OpenPopup("Edit code note");
        const auto* viewport = ImGui::GetMainViewport();
        const ImVec2 maximum(std::max(1.0f, viewport->WorkSize.x - 20),
                             std::max(1.0f, viewport->WorkSize.y - 20));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(cw::ui_px(320.0f), maximum.x), std::min(cw::ui_px(240.0f), maximum.y)),
            maximum);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(cw::ui_px(560.0f), maximum.x), std::min(cw::ui_px(500.0f), maximum.y)),
            ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Edit code note", nullptr))
            return;
        if (note_draft.open()) {
            ImGui::Text("Game %u, address 0x%06X", *note_draft.game_id(), note_draft.address());
            ImGui::TextWrapped("Save updates your local code notes and the To publish list.");
            text_input("##note_text", note_draft.text(), true);
            if (const auto& server = note_draft.server_note()) {
                ImGui::TextUnformatted("Cached server note (read-only)");
                if (!server->author.empty())
                    ImGui::Text("By %s", server->author.c_str());
                if (ImGui::BeginChild("server_note", ImVec2(0, cw::ui_px(90.0f)),
                                      ImGuiChildFlags_Borders)) {
                    ImGui::PushTextWrapPos();
                    ImGui::TextUnformatted(server->note.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndChild();
            }
            if (note_quit_pending)
                ImGui::TextWrapped("Finish or cancel this note edit before closing the app.");
            error_text(note_message);
            if (ImGui::Button("Save local note")) {
                if (note_draft.save(notes, note_message)) {
                    if (std::exchange(note_quit_pending, false))
                        quit_requested = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            continue_row("Cancel");
            if (ImGui::Button("Cancel")) {
                note_draft.cancel();
                note_quit_pending = false;
                note_message.clear();
                ImGui::CloseCurrentPopup();
            }
        } else
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void live_controls() {
        ImGui::Separator();
        if (!live.active()) {
            ImGui::BeginDisabled(!game_loaded || !editor.selected() || inputs_invalid());
            if (ImGui::Button("Start live test"))
                start_live_requested = true;
            ImGui::EndDisabled();
            if (!game_loaded)
                ImGui::TextDisabled("Load game content to test this draft.");
            else
                ImGui::TextWrapped("Tests the selected draft against the current game.");
            if (!live.message().empty())
                ImGui::TextWrapped("%s", live.message().c_str());
        } else {
            if (ImGui::Button("Stop live test"))
                live.stop();
            continue_row("Rearm test");
            const auto status = live.snapshot();
            ImGui::BeginDisabled(!status || status->state == LogicState::Disabled ||
                                 !live.active());
            if (ImGui::Button("Rearm test"))
                live.rearm();
            ImGui::EndDisabled();
        }
        live_status = live.snapshot();
        if (live_status) {
            ImGui::Text("Live state: %s", logic_state_name(live_status->state));
            if (live_status->last_frame)
                ImGui::Text("Last runtime frame: %llu",
                            static_cast<unsigned long long>(*live_status->last_frame));
            else
                ImGui::TextDisabled("No evaluation since start, reset or progress restore.");
            if (live_status->state == LogicState::Disabled)
                error_text("An address is unreadable. Fix the draft and start again.");
            ImGui::TextWrapped("Last result is the cached comparison, before hit targets and "
                               "chaining. Paused, reset or triggered logic may retain older "
                               "results. Reading this table does not evaluate another frame.");
        }
    }

    void live_row(size_t g, size_t row) {
        ImGui::TableNextColumn();
        const ConditionStatus* status = nullptr;
        if (live_status && g < live_status->groups.size() &&
            row < live_status->groups[g].conditions.size())
            status = &live_status->groups[g].conditions[row];
        if (!status || !status->has_comparison)
            ImGui::TextDisabled("-");
        else if (!live_status->last_frame)
            ImGui::TextDisabled("Pending");
        else {
            const char* result = status->caused_reset ? "Reset"
                                 : status->is_true    ? "True"
                                                      : "False";
            const ImVec4 color = status->caused_reset ? ImVec4(1, 0.5f, 0.35f, 1)
                                 : status->is_true    ? ImVec4(0.4f, 0.9f, 0.5f, 1)
                                                      : ImVec4(0.7f, 0.7f, 0.7f, 1);
            ImGui::TextColored(color, "%s", result);
        }
        ImGui::TableNextColumn();
        if (status && status->has_comparison) {
            if (status->required_hits)
                ImGui::Text("%u / %u", status->current_hits, status->required_hits);
            else
                ImGui::Text("%u", status->current_hits);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("This row's own hits; AddHits/SubHits totals are applied "
                                  "separately by rcheevos.");
        } else
            ImGui::TextDisabled("-");
    }

    std::string key(size_t row, const char* part) const {
        return std::to_string(group) + "/" + std::to_string(row) + "/" + part;
    }
    void sync_buffers(bool keep_group = false) {
        if (revision == editor.revision())
            return;
        revision = editor.revision();
        buffers.clear();
        if (!keep_group) {
            hidden_targets.clear();
            group = 0;
        }
        requested_tab = group;
        select_tab = true;
    }
    bool inputs_invalid() const {
        return std::any_of(buffers.begin(), buffers.end(),
                           [](const auto& item) { return !item.second.error.empty(); });
    }
    void refresh_error() {
        std::string error;
        for (const auto& [name, buffer] : buffers) {
            if (!buffer.error.empty()) {
                error = input_location(name) + ": " + buffer.error;
                break;
            }
        }
        editor.set_input_error(std::move(error));
    }
    void report(bool ok, std::string text) {
        message_error = !ok;
        message = std::move(text);
    }
    bool save() {
        refresh_error();
        std::string error;
        if (!editor.save(error)) {
            report(false, error);
            return false;
        }
        report(true, "Saved local achievement file.");
        sync_buffers(true);
        return true;
    }
    void open_file(const std::filesystem::path& path, const std::string& title) {
        std::string error;
        if (!editor.load(path, error)) {
            report(false, error);
            return;
        }
        path_input = editor.path().string();
        path_initialized = true;
        // A suggested title applies only to a new, missing file, not an existing empty title.
        std::error_code exists_error;
        if (!title.empty() && !std::filesystem::exists(editor.path(), exists_error) &&
            !exists_error) {
            editor.document().title = title;
            editor.changed();
        }
        sync_buffers();
        report(true, "Opened local achievement file.");
    }
    void request_open(std::filesystem::path path, std::string title) {
        if (editor.dirty()) {
            pending = Pending::Open;
            pending_path = std::move(path);
            pending_title = std::move(title);
            show_confirmation = true;
        } else
            open_file(path, title);
    }
    void finish_pending() {
        const auto action = pending;
        pending = Pending::None;
        if (action == Pending::Open)
            open_file(pending_path, pending_title);
        else if (action == Pending::Quit)
            quit_requested = true;
    }
    void confirmation() {
        if (show_confirmation) {
            ImGui::OpenPopup("Unsaved achievement changes");
            show_confirmation = false;
        }
        if (ImGui::BeginPopupModal("Unsaved achievement changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save your achievement edits before continuing?");
            ImGui::BeginDisabled(!editor.can_save());
            if (ImGui::Button("Save")) {
                if (save()) {
                    finish_pending();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Discard")) {
                editor.discard_changes();
                sync_buffers();
                finish_pending();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                pending = Pending::None;
                ImGui::CloseCurrentPopup();
            }
            if (message_error)
                error_text(message);
            error_text(editor.input_error());
            error_text(editor.validation_error());
            ImGui::EndPopup();
        }
    }

    bool operand_type(LogicOperand& operand, const LogicOperand* opposite,
                      const std::string& buffer_key) {
        const auto old = operand.type;
        ImGui::BeginDisabled(inputs_invalid());
        bool changed = combo("##type", operand.type, kTypes);
        ImGui::EndDisabled();
        if (changed) {
            buffers.erase(buffer_key);
            if (memory_operand(operand.type) && !memory_operand(old))
                operand.size = opposite && memory_operand(opposite->type)
                                   ? opposite->size
                                   : LogicMemorySize::ThirtyTwoBit;
            if (operand.type == LogicOperandType::Float && old != LogicOperandType::Float)
                operand.float_value = operand.value;
            if (old == LogicOperandType::Float && operand.type != LogicOperandType::Float) {
                // Avoid undefined float-to-integer conversion outside the representable range.
                operand.value =
                    std::isfinite(operand.float_value) &&
                            operand.float_value >= std::numeric_limits<int32_t>::min() &&
                            operand.float_value <= std::numeric_limits<uint32_t>::max()
                        ? static_cast<uint32_t>(static_cast<int64_t>(operand.float_value))
                        : 0;
            }
        }
        return changed;
    }
    bool operand_size(LogicOperand& operand) {
        if (memory_operand(operand.type))
            return combo("##size", operand.size, kSizes);
        ImGui::TextDisabled("-");
        return false;
    }
    bool operand_value(LogicOperand& operand, const std::string& buffer_key) {
        if (operand.type == LogicOperandType::Recall) {
            ImGui::TextDisabled("{recall}");
            return false;
        }
        if (operand.type == LogicOperandType::Function) {
            ImGui::TextDisabled("@%s", operand.function.c_str());
            return false;
        }
        auto it = buffers.find(buffer_key);
        if (it == buffers.end()) {
            std::string initial;
            if (operand.type == LogicOperandType::Float) {
                std::array<char, 128> chars{};
                const auto [end, ec] =
                    std::to_chars(chars.data(), chars.data() + chars.size(), operand.float_value);
                initial = ec == std::errc{} ? std::string(chars.data(), end) : "0";
            } else
                initial = number_string(operand.value, memory_operand(operand.type));
            it = buffers.emplace(buffer_key, Buffer{initial, {}}).first;
        }
        ImGui::SetNextItemWidth(-1);
        const bool changed = text_input("##value", it->second.text);
        if (changed) {
            auto& buffer = it->second;
            bool valid = false;
            if (operand.type == LogicOperandType::Float) {
                double value = 0;
                const auto [end, ec] = std::from_chars(
                    buffer.text.data(), buffer.text.data() + buffer.text.size(), value);
                valid = ec == std::errc{} && end == buffer.text.data() + buffer.text.size() &&
                        std::isfinite(value);
                if (valid)
                    operand.float_value = value;
            } else {
                uint32_t value = 0;
                valid = parse_uint(buffer.text, value, memory_operand(operand.type),
                                   operand.type == LogicOperandType::Integer);
                if (valid)
                    operand.value = value;
            }
            buffer.error = valid ? "" : "Invalid value; use a 32-bit number or a finite float.";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Memory addresses: hexadecimal. Integer values: decimal or 0x-prefixed hex.");
        return changed;
    }
    bool hits(LogicCondition& condition, const std::string& buffer_key) {
        if (condition.op == LogicOperator::None || modifying_flag(condition.flag)) {
            ImGui::TextDisabled("-");
            return false;
        }
        auto [it, inserted] = buffers.try_emplace(buffer_key);
        if (inserted)
            it->second.text =
                condition.required_hits ? std::to_string(*condition.required_hits) : "";
        ImGui::SetNextItemWidth(-1);
        const bool changed = text_input("##hits", it->second.text);
        if (changed) {
            uint32_t value = 0;
            const bool valid = it->second.text.empty() || parse_uint(it->second.text, value, false);
            it->second.error = valid ? "" : "Invalid hit target.";
            if (valid) {
                if (it->second.text.empty())
                    condition.required_hits.reset();
                else
                    condition.required_hits = value;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Required hits. Leave empty for no hit target.");
        return changed;
    }

    enum class Edit { None, Select, New, Delete, AddAlt, DeleteAlt, AddRow, DeleteRow, Up, Down };
    struct Action {
        Edit kind = Edit::None;
        size_t index = 0;
        size_t target_group = 0;
    };

    void hide_target(LogicCondition& row, size_t row_index, bool capture_hits) {
        auto& hidden = hidden_targets[{group, row_index}];
        if (row.right)
            hidden.right = row.right;
        if (capture_hits)
            hidden.hits = row.required_hits;
        row.right.reset();
        row.required_hits.reset();
    }
    void restore_target(LogicCondition& row, size_t row_index) {
        const auto found = hidden_targets.find({group, row_index});
        if (!row.right) {
            if (found != hidden_targets.end() && found->second.right)
                row.right = found->second.right;
            else
                row.right.emplace();
        }
        if (!modifying_flag(row.flag) && found != hidden_targets.end())
            row.required_hits = found->second.hits;
    }

    void conditions(LocalAchievement& achievement, Action& action, bool& changed) {
        if (ImGui::Button("Add Alt"))
            action = {Edit::AddAlt};
        continue_row("Revert invalid inputs");
        if (ImGui::Button("Revert invalid inputs")) {
            buffers.clear();
            editor.set_input_error({});
        }
        if (!ImGui::BeginTabBar("logic_groups"))
            return;
        const size_t group_count = achievement.logic.alternatives.size() + 1;
        const size_t requested_group = requested_tab;
        const bool requested_selection = select_tab;
        for (size_t g = 0; g < group_count; ++g) {
            const std::string label = g == 0 ? "Core" : "Alt " + std::to_string(g);
            const auto flags = requested_selection && requested_group == g
                                   ? ImGuiTabItemFlags_SetSelected
                                   : ImGuiTabItemFlags_None;
            if (!ImGui::BeginTabItem(label.c_str(), nullptr, flags))
                continue;
            group = g;
            if (g == requested_group)
                select_tab = false;
            auto& selected_group =
                g == 0 ? achievement.logic.core : achievement.logic.alternatives[g - 1];
            if (live_status && g < live_status->groups.size() && live_status->groups[g].paused)
                ImGui::TextDisabled("Group paused: other rows may show older results.");
            if (ImGui::Button("Add condition"))
                action = {Edit::AddRow, 0, g};
            if (g) {
                ImGui::SameLine();
                if (ImGui::Button("Remove Alt"))
                    action = {Edit::DeleteAlt, 0, g};
            }
            constexpr auto table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                         ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
                                         ImGuiTableFlags_ScrollY;
            // Fixed minimum content widths keep all controls reachable on narrow windows.
            const float height = std::max(ImGui::GetTextLineHeightWithSpacing() * 5,
                                          ImGui::GetContentRegionAvail().y - 100);
            // Measured before BeginTable: inside the table this reports its scrolling inner width.
            const float visible_width = ImGui::GetContentRegionAvail().x;
            if (ImGui::BeginTable("conditions", 12, table_flags, ImVec2(0, height),
                                  cw::ui_px(1475.0f))) {
                ImGui::TableSetupColumn("Last result", ImGuiTableColumnFlags_WidthFixed,
                                        cw::ui_px(100.0f));
                ImGui::TableSetupColumn("Current hits", ImGuiTableColumnFlags_WidthFixed,
                                        cw::ui_px(100.0f));
                for (const auto* name :
                     {"Flag", "Left type", "Left size", "Address / value", "Compare", "Right type",
                      "Right size", "Address / value ", "Hits", "Actions"})
                    ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_WidthFixed,
                                            cw::ui_px(std::string_view(name) == "Compare" ? 90.0f
                                                      : std::string_view(name) == "Hits" ? 65.0f
                                                                                         : 125.0f));
                // The live columns only hold data during a test; otherwise they just take up room.
                ImGui::TableSetupScrollFreeze(
                    live.active() ? cw::frozen_table_columns(2, cw::ui_px(200.0f), visible_width)
                                  : 0,
                    1);
                ImGui::TableHeadersRow();
                ImGui::PushID(static_cast<int>(g));
                ImGuiListClipper clipper;
                const float row_height = ImGui::GetFrameHeightWithSpacing() +
                                         2 * ImGui::GetTextLineHeightWithSpacing() +
                                         2 * ImGui::GetStyle().CellPadding.y;
                clipper.Begin(static_cast<int>(selected_group.conditions.size()), row_height);
                while (clipper.Step()) {
                    for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd;
                         ++row_index) {
                        const size_t r = static_cast<size_t>(row_index);
                        auto& row = selected_group.conditions[r];
                        ImGui::PushID(static_cast<int>(r));
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                        live_row(g, r);
                        ImGui::TableNextColumn();
                        const auto old_flag = row.flag;
                        ImGui::BeginDisabled(inputs_invalid());
                        if (combo("##flag", row.flag, kFlags)) {
                            if (modifying_flag(row.flag) != modifying_flag(old_flag)) {
                                row.op = modifying_flag(row.flag) ? LogicOperator::None
                                                                  : LogicOperator::Equal;
                                if (row.op == LogicOperator::None)
                                    hide_target(row, r, !modifying_flag(old_flag));
                                else
                                    restore_target(row, r);
                                buffers.erase(key(r, "R"));
                                buffers.erase(key(r, "hits"));
                            }
                            changed = true;
                        }
                        ImGui::EndDisabled();
                        ImGui::TableNextColumn();
                        ImGui::PushID("left");
                        changed |=
                            operand_type(row.left, row.right ? &*row.right : nullptr, key(r, "L"));
                        ImGui::TableNextColumn();
                        changed |= operand_size(row.left);
                        ImGui::TableNextColumn();
                        changed |= operand_value(row.left, key(r, "L"));
                        operand_note(selected_group, r, false, key(r, "L"));
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(inputs_invalid());
                        const bool op_changed = modifying_flag(row.flag)
                                                    ? combo("##op", row.op, kArithmetic)
                                                    : combo("##op", row.op, kComparisons);
                        ImGui::EndDisabled();
                        if (op_changed) {
                            if (row.op == LogicOperator::None) {
                                hide_target(row, r, !modifying_flag(row.flag));
                            } else if (!row.right)
                                restore_target(row, r);
                            buffers.erase(key(r, "R"));
                            buffers.erase(key(r, "hits"));
                            changed = true;
                        }
                        ImGui::TableNextColumn();
                        if (row.right) {
                            ImGui::PushID("right");
                            changed |= operand_type(*row.right, &row.left, key(r, "R"));
                            ImGui::TableNextColumn();
                            changed |= operand_size(*row.right);
                            ImGui::TableNextColumn();
                            changed |= operand_value(*row.right, key(r, "R"));
                            operand_note(selected_group, r, true, key(r, "R"));
                            ImGui::PopID();
                        } else {
                            ImGui::TextDisabled("-");
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("-");
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("-");
                        }
                        ImGui::TableNextColumn();
                        changed |= hits(row, key(r, "hits"));
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton("Up") && r)
                            action = {Edit::Up, r, g};
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Down") && r + 1 < selected_group.conditions.size())
                            action = {Edit::Down, r, g};
                        if (ImGui::SmallButton("Remove"))
                            action = {Edit::DeleteRow, r, g};
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void apply(Action action) {
        std::string error;
        if (action.kind == Edit::None)
            return;
        if (!editor.input_error().empty()) {
            report(false, "Fix or revert invalid inputs before switching or restructuring.");
            return;
        }
        if (action.kind == Edit::Delete) {
            if (!editor.remove_selected(error))
                report(false, error);
            sync_buffers();
            return;
        }
        if (action.kind == Edit::Select) {
            if (!editor.select(action.index, error))
                report(false, error);
            sync_buffers();
            return;
        }
        if (action.kind == Edit::New) {
            if (!editor.add_achievement(error))
                report(false, error);
            sync_buffers();
            return;
        }
        auto* selected = editor.selected();
        if (!selected)
            return;
        auto& logic = selected->logic;
        // Hidden operands follow their condition when rows/groups move, just like visible
        // operands. Clearing their cache here would make a temporary flag change destructive.
        std::map<std::pair<size_t, size_t>, HiddenTarget> remapped;
        for (auto& [position, target] : hidden_targets) {
            auto [g, r] = position;
            if (action.kind == Edit::DeleteAlt) {
                if (g == action.target_group)
                    continue;
                if (g > action.target_group)
                    --g;
            } else if (g == action.target_group) {
                if (action.kind == Edit::DeleteRow) {
                    if (r == action.index)
                        continue;
                    if (r > action.index)
                        --r;
                } else if (action.kind == Edit::Up && action.index) {
                    if (r == action.index)
                        --r;
                    else if (r + 1 == action.index)
                        ++r;
                } else if (action.kind == Edit::Down) {
                    if (r == action.index)
                        ++r;
                    else if (r == action.index + 1)
                        --r;
                }
            }
            remapped.emplace(std::pair{g, r}, std::move(target));
        }
        hidden_targets = std::move(remapped);
        if (action.kind == Edit::AddAlt) {
            logic.alternatives.emplace_back();
            requested_tab = logic.alternatives.size();
            select_tab = true;
        } else if (action.kind == Edit::DeleteAlt && action.target_group &&
                   action.target_group <= logic.alternatives.size()) {
            logic.alternatives.erase(logic.alternatives.begin() +
                                     static_cast<std::ptrdiff_t>(action.target_group - 1));
            group = 0;
            requested_tab = 0;
            select_tab = true;
        } else {
            auto& target = action.target_group == 0
                               ? logic.core
                               : logic.alternatives.at(action.target_group - 1);
            if (action.kind == Edit::AddRow)
                target.conditions.push_back(default_condition());
            else if (action.index < target.conditions.size()) {
                if (action.kind == Edit::DeleteRow)
                    target.conditions.erase(target.conditions.begin() +
                                            static_cast<std::ptrdiff_t>(action.index));
                else if (action.kind == Edit::Up && action.index)
                    std::swap(target.conditions[action.index], target.conditions[action.index - 1]);
                else if (action.kind == Edit::Down && action.index + 1 < target.conditions.size())
                    std::swap(target.conditions[action.index], target.conditions[action.index + 1]);
            }
        }
        buffers.clear();
        editor.changed();
    }

    void contents(const std::filesystem::path& suggested, const std::string& title) {
        sync_buffers();
        if (!path_initialized) {
            if (editor.loaded())
                path_input = editor.path().string();
            else if (!suggested.empty())
                path_input = suggested.string();
            path_initialized = !path_input.empty();
        }
        ImGui::TextUnformatted("User file");
        ImGui::SetNextItemWidth(-1);
        if (text_input("##user_file", path_input))
            path_initialized = true;
        bool open_requested = ImGui::Button("Open / Create");
        continue_row("Current game");
        ImGui::BeginDisabled(suggested.empty());
        const bool current_requested = ImGui::Button("Current game");
        ImGui::EndDisabled();
        continue_row("Save");
        ImGui::BeginDisabled(!editor.can_save());
        bool save_requested = ImGui::Button("Save");
        ImGui::EndDisabled();
        continue_row("Reload");
        ImGui::BeginDisabled(!editor.loaded());
        const bool reload_requested = ImGui::Button("Reload");
        ImGui::EndDisabled();
        if (editor.loaded()) {
            ImGui::TextWrapped("Loaded: %s", editor.path().c_str());
            ImGui::TextDisabled("%s", editor.dirty() ? "Unsaved changes" : "No pending edits");
        }
        Action action;
        bool changed = false;
        if (editor.loaded()) {
            ImGui::SetNextItemWidth(-cw::ui_px(100.0f));
            changed |= text_input("Game title", editor.document().title);
            ImGui::BeginDisabled(!editor.input_error().empty());
            if (ImGui::Button("New achievement"))
                action = {Edit::New};
            ImGui::EndDisabled();
            continue_row("Delete achievement");
            ImGui::BeginDisabled(!editor.selected() || !editor.input_error().empty());
            if (ImGui::Button("Delete achievement"))
                action = {Edit::Delete};
            ImGui::EndDisabled();
            // The asset list wraps above the form on narrow windows instead of squeezing it.
            if (ImGui::BeginChild("asset_list", ImVec2(0, cw::ui_px(100.0f)),
                                  ImGuiChildFlags_Borders)) {
                for (size_t i = 0; i < editor.document().entries.size(); ++i) {
                    const auto* asset =
                        std::get_if<LocalAchievement>(&editor.document().entries[i].asset);
                    if (!asset)
                        continue;
                    const std::string label =
                        (asset->title.empty() ? "Untitled achievement" : asset->title) + "###asset";
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(label.c_str(), editor.selection() == i))
                        action = {Edit::Select, i};
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            if (auto* asset = editor.selected()) {
                ImGui::PushID(static_cast<int>(editor.revision() & 0x7fffffff));
                ImGui::SetNextItemWidth(-cw::ui_px(80.0f));
                changed |= text_input("Title", asset->title);
                ImGui::TextUnformatted("Description");
                changed |= text_input("##description", asset->description, true);
                ImGui::SetNextItemWidth(cw::ui_px(90.0f));
                const std::string points_preview = std::to_string(asset->points);
                if (ImGui::BeginCombo("Points", points_preview.c_str())) {
                    for (const auto points : kPoints) {
                        if (ImGui::Selectable(std::to_string(points).c_str(),
                                              asset->points == points)) {
                            asset->points = points;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetNextItemWidth(cw::ui_px(180.0f));
                std::string_view type = asset->type;
                ImGui::TextUnformatted("Achievement type");
                if (combo("##achievement_type", type, kAchievementTypes)) {
                    asset->type = type;
                    changed = true;
                }
                live_controls();
                note_header();
                conditions(*asset, action, changed);
                ImGui::PopID();
            } else
                ImGui::TextWrapped("Open a local file or create an achievement to begin.");
        } else
            ImGui::TextWrapped(
                "Open an existing -User.txt file, or enter a new file path. Current game uses the "
                "configured RACache, or the workbench's local sets directory.");
        if (changed) {
            editor.changed();
            message.clear();
            message_error = false;
        }
        refresh_error();
        error_text(editor.input_error());
        error_text(editor.validation_error());
        if (message_error)
            error_text(message);
        else if (!message.empty())
            ImGui::TextWrapped("%s", message.c_str());
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            save_requested = true;
        // Defer actions until active text widgets have consumed this frame's edits.
        if (save_requested)
            save();
        apply(action);
        if (current_requested)
            request_open(suggested, title);
        else if (reload_requested)
            request_open(editor.path(), {});
        else if (open_requested)
            request_open(path_input, {});
        live.sync(editor, game_session, game_loaded);
        if (std::exchange(start_live_requested, false) && memory && game_loaded) {
            std::string error;
            if (!live.start(editor, game_session, *memory, error))
                report(false, error);
        }
    }
};

AchievementEditorWindow::AchievementEditorWindow(LogicTester& tester, CodeNotes& notes)
    : impl_(std::make_unique<Impl>(tester, notes)) {}
AchievementEditorWindow::~AchievementEditorWindow() = default;
AchievementEditor& AchievementEditorWindow::session() {
    return impl_->editor;
}
void AchievementEditorWindow::sync_live(uint64_t game_session, bool game_loaded,
                                        const MemoryReader& memory) {
    impl_->game_session = game_session;
    impl_->game_loaded = game_loaded;
    impl_->memory = &memory;
    impl_->live.sync(impl_->editor, game_session, game_loaded);
}
void AchievementEditorWindow::draw(const std::filesystem::path& suggested_file,
                                   const std::string& title) {
    if (open) {
        const auto* viewport = ImGui::GetMainViewport();
        const auto padding = ImGui::GetStyle().WindowPadding;
        const ImVec2 maximum(std::max(1.0f, viewport->WorkSize.x - 2 * padding.x),
                             std::max(1.0f, viewport->WorkSize.y - 2 * padding.y));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(cw::ui_px(380.0f), maximum.x), std::min(cw::ui_px(260.0f), maximum.y)),
            maximum);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(cw::ui_px(1100.0f), maximum.x), std::min(cw::ui_px(780.0f), maximum.y)),
            ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Achievement Editor", &open)) {
            if (!ImGui::IsWindowDocked()) {
                const auto pos = ImGui::GetWindowPos();
                const auto size = ImGui::GetWindowSize();
                ImGui::SetWindowPos(ImVec2(
                    std::clamp(pos.x, viewport->WorkPos.x,
                               std::max(viewport->WorkPos.x,
                                        viewport->WorkPos.x + viewport->WorkSize.x - size.x)),
                    std::clamp(pos.y, viewport->WorkPos.y,
                               std::max(viewport->WorkPos.y,
                                        viewport->WorkPos.y + viewport->WorkSize.y - size.y))));
            }
            impl_->contents(suggested_file, title);
        }
        ImGui::End();
    }
    impl_->confirmation();
    impl_->note_dialog();
}
bool AchievementEditorWindow::allow_quit() {
    if (impl_->note_draft.open()) {
        impl_->note_quit_pending = true;
        open = true;
        return false;
    }
    if (!impl_->editor.dirty())
        return true;
    impl_->pending = Impl::Pending::Quit;
    impl_->show_confirmation = true;
    open = true;
    return false;
}
bool AchievementEditorWindow::take_quit_request() {
    return std::exchange(impl_->quit_requested, false);
}

} // namespace cw
