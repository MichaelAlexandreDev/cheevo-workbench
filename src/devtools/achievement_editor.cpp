#include "cw/devtools/achievement_editor.hpp"

#include <algorithm>
#include <utility>
#include <variant>

namespace cw {

std::optional<uint32_t> achievement_note_address(const LogicGroup& group, size_t row,
                                                 bool right_operand) {
    if (row >= group.conditions.size() ||
        (row > 0 && group.conditions[row - 1].flag == LogicFlag::AddAddress))
        return std::nullopt;
    const auto& condition = group.conditions[row];
    const auto* operand =
        right_operand ? (condition.right ? &*condition.right : nullptr) : &condition.left;
    if (!operand)
        return std::nullopt;
    switch (operand->type) {
    case LogicOperandType::Address:
    case LogicOperandType::Delta:
    case LogicOperandType::Prior:
    case LogicOperandType::Bcd:
    case LogicOperandType::Inverted:
        return operand->value;
    default:
        return std::nullopt;
    }
}

bool AchievementEditor::load(const std::filesystem::path& path, std::string& error) {
    if (dirty()) {
        error = "Discard or save the current changes before loading another user file.";
        return false;
    }

    // UserFileStore clears its own state when a load fails. Load into a candidate so a bad path
    // or malformed file cannot replace a session that is already open in this editor.
    UserFileStore candidate;
    if (!candidate.load(path, error))
        return false;

    store_ = std::move(candidate);
    document_ = store_.document();
    loaded_ = true;
    dirty_ = false;
    input_error_.clear();
    validate();
    select_first();
    ++revision_;
    ++selection_version_;
    error.clear();
    return true;
}

bool AchievementEditor::save(std::string& error) {
    if (!loaded_) {
        error = "Load a user file before saving.";
        return false;
    }

    validate();
    if (!input_error_.empty()) {
        error = input_error_;
        return false;
    }
    if (!validation_error_.empty()) {
        error = validation_error_;
        return false;
    }
    if (!store_.writable()) {
        error = "The loaded user file is not writable.";
        return false;
    }

    if (!store_.save(document_, error))
        return false;

    document_ = store_.document();
    dirty_ = false;
    input_error_.clear();
    validation_error_.clear();
    ++revision_;
    error.clear();
    return true;
}

void AchievementEditor::discard_changes() {
    document_ = store_.document();
    dirty_ = false;
    input_error_.clear();
    validate();
    select_first();
    ++revision_;
    ++selection_version_;
}

LocalAchievement* AchievementEditor::selected() {
    if (!selection_ || *selection_ >= document_.entries.size())
        return nullptr;
    return std::get_if<LocalAchievement>(&document_.entries[*selection_].asset);
}

const LocalAchievement* AchievementEditor::selected() const {
    if (!selection_ || *selection_ >= document_.entries.size())
        return nullptr;
    return std::get_if<LocalAchievement>(&document_.entries[*selection_].asset);
}

bool AchievementEditor::select(size_t document_index, std::string& error) {
    if (!loaded_) {
        error = "Load a user file before selecting an achievement.";
        return false;
    }
    if (!input_error_.empty()) {
        error = input_error_;
        return false;
    }
    if (document_index >= document_.entries.size()) {
        error = "Achievement selection is outside the user file.";
        return false;
    }
    if (!std::holds_alternative<LocalAchievement>(document_.entries[document_index].asset)) {
        error = "The selected user file entry is not an achievement.";
        return false;
    }

    if (selection_ != document_index) {
        selection_ = document_index;
        ++revision_;
        ++selection_version_;
    }
    error.clear();
    return true;
}

bool AchievementEditor::add_achievement(std::string& error) {
    if (!loaded_) {
        error = "Load a user file before adding an achievement.";
        return false;
    }
    if (!input_error_.empty()) {
        error = input_error_;
        return false;
    }

    LocalAchievement achievement;
    achievement.title = "New achievement";

    LogicCondition condition;
    condition.left.type = LogicOperandType::Address;
    condition.left.size = LogicMemorySize::EightBit;
    condition.left.value = 0;
    condition.op = LogicOperator::Equal;
    LogicOperand expected_value;
    expected_value.type = LogicOperandType::Integer;
    expected_value.value = 0;
    condition.right = expected_value;
    achievement.logic.core.conditions.push_back(std::move(condition));

    UserFileEntry entry;
    entry.asset = std::move(achievement);
    document_.entries.push_back(std::move(entry));
    selection_ = document_.entries.size() - 1;
    dirty_ = true;
    validate();
    ++revision_;
    ++selection_version_;
    error.clear();
    return true;
}

bool AchievementEditor::remove_selected(std::string& error) {
    if (!loaded_) {
        error = "Load a user file before removing an achievement.";
        return false;
    }
    if (!selection_ || *selection_ >= document_.entries.size() ||
        !std::holds_alternative<LocalAchievement>(document_.entries[*selection_].asset)) {
        error = "Select an achievement before removing it.";
        return false;
    }

    const size_t removed_index = *selection_;
    document_.entries.erase(document_.entries.begin() + static_cast<std::ptrdiff_t>(removed_index));

    selection_.reset();
    for (size_t index = removed_index; index < document_.entries.size(); ++index) {
        if (std::holds_alternative<LocalAchievement>(document_.entries[index].asset)) {
            selection_ = index;
            break;
        }
    }
    if (!selection_) {
        size_t index = std::min(removed_index, document_.entries.size());
        while (index > 0) {
            --index;
            if (std::holds_alternative<LocalAchievement>(document_.entries[index].asset)) {
                selection_ = index;
                break;
            }
        }
    }

    input_error_.clear();
    dirty_ = true;
    validate();
    ++revision_;
    ++selection_version_;
    error.clear();
    return true;
}

void AchievementEditor::set_input_error(std::string error) {
    if (input_error_ != error)
        ++edit_version_;
    if (!error.empty())
        dirty_ = true;
    input_error_ = std::move(error);
}

void AchievementEditor::changed() {
    ++edit_version_;
    dirty_ = true;
    validate();
}

bool AchievementEditor::can_save() const {
    return loaded_ && input_error_.empty() && validation_error_.empty() && store_.writable();
}

void AchievementEditor::validate() {
    std::string error;
    if (!serialize_user_file(document_, error))
        validation_error_ = std::move(error);
    else
        validation_error_.clear();
}

void AchievementEditor::select_first() {
    selection_.reset();
    for (size_t index = 0; index < document_.entries.size(); ++index) {
        if (std::holds_alternative<LocalAchievement>(document_.entries[index].asset)) {
            selection_ = index;
            return;
        }
    }
}

} // namespace cw
