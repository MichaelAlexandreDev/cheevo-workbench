#include "cw/devtools/leaderboard_editor.hpp"

#include <rc_error.h>
#include <rc_runtime_types.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <variant>

namespace cw {
namespace {

constexpr size_t kMaxLeaderboardDefinitionSize = 65535;

bool known_format(std::string_view format) {
    // rc_parse_format maps unrecognized input to VALUE, so membership must be checked separately
    // before consulting the pinned parser.
    constexpr std::array<std::string_view, 24> kKnownFormats{
        "FRAMES",    "FLOAT1", "FLOAT2", "FLOAT3",   "FLOAT4",       "FLOAT5",
        "FLOAT6",    "FIXED1", "FIXED2", "FIXED3",   "TIME",         "TIMESECS",
        "THOUSANDS", "TENS",   "SECS",   "SCORE",    "SECS_AS_MINS", "MILLISECS",
        "MINUTES",   "POINTS", "VALUE",  "UNSIGNED", "OTHER",        "HUNDREDS"};
    if (std::find(kKnownFormats.begin(), kKnownFormats.end(), format) == kKnownFormats.end())
        return false;

    const std::string storage(format);
    const int parsed = rc_parse_format(storage.c_str());
    return parsed >= RC_FORMAT_FRAMES && parsed <= RC_FORMAT_UNFORMATTED;
}

bool contains_nul(std::string_view text) {
    return text.find('\0') != std::string_view::npos;
}

bool same_draft(const LeaderboardDraft& a, const LeaderboardDraft& b) {
    return a.title == b.title && a.description == b.description && a.start == b.start &&
           a.cancel == b.cancel && a.submit == b.submit && a.value == b.value &&
           a.format == b.format && a.lower_is_better == b.lower_is_better;
}

} // namespace

std::optional<std::string> leaderboard_definition(const LocalLeaderboard& leaderboard,
                                                  std::string& error) {
    const auto start = serialize_achievement_logic(leaderboard.start, error);
    if (!start) {
        error = "Invalid leaderboard start trigger: " + error;
        return std::nullopt;
    }
    const auto submit = serialize_achievement_logic(leaderboard.submit, error);
    if (!submit) {
        error = "Invalid leaderboard submit trigger: " + error;
        return std::nullopt;
    }
    const auto cancel = serialize_achievement_logic(leaderboard.cancel, error);
    if (!cancel) {
        error = "Invalid leaderboard cancel trigger: " + error;
        return std::nullopt;
    }

    if (leaderboard.value.empty()) {
        error = "Leaderboard value cannot be empty for testing or authoring.";
        return std::nullopt;
    }
    if (!validate_leaderboard_value(leaderboard.value, error)) {
        return std::nullopt;
    }

    std::string definition =
        "STA:" + *start + "::SUB:" + *submit + "::CAN:" + *cancel + "::VAL:" + leaderboard.value;
    if (definition.size() > kMaxLeaderboardDefinitionSize) {
        error = "Combined leaderboard definition exceeds the 65535-byte limit";
        return std::nullopt;
    }
    if (contains_nul(definition)) {
        error = "Combined leaderboard definition contains an embedded NUL byte";
        return std::nullopt;
    }

    const int result = rc_lboard_size(definition.c_str());
    if (result < 0) {
        error = std::string("Invalid combined leaderboard definition: ") + rc_error_str(result);
        return std::nullopt;
    }

    error.clear();
    return definition;
}

bool LeaderboardEditor::load(const std::filesystem::path& path, std::string& error) {
    if (dirty_) {
        error = "Save or discard the current changes before loading another user file.";
        return false;
    }

    // Loading through a candidate keeps the current session intact if the new path is invalid.
    UserFileStore candidate;
    if (!candidate.load(path, error))
        return false;

    store_ = std::move(candidate);
    document_ = store_.document();
    loaded_ = true;
    dirty_ = false;
    select_first();
    load_draft_from_selection();
    validate();
    ++selection_version_;
    ++edit_version_;
    error.clear();
    return true;
}

bool LeaderboardEditor::save(std::string& error) {
    if (!loaded_) {
        error = "Load a user file before saving.";
        return false;
    }

    validate();
    if (!validation_error_.empty()) {
        error = validation_error_;
        return false;
    }

    // Keep the editable document unchanged unless both serialization and atomic disk replacement
    // succeed. The store reparses its successful write to normalize its preservation metadata.
    UserFileDocument candidate = document_;
    if (!apply_draft(candidate, error))
        return false;
    if (!serialize_user_file(candidate, error))
        return false;
    if (!store_.save(candidate, error))
        return false;

    document_ = store_.document();
    dirty_ = false;
    load_draft_from_selection();
    validate();
    error.clear();
    return true;
}

void LeaderboardEditor::discard_changes() {
    document_ = store_.document();
    dirty_ = false;
    select_first();
    load_draft_from_selection();
    validate();
    ++selection_version_;
    ++edit_version_;
}

bool LeaderboardEditor::can_save() const {
    return loaded_ && store_.writable() && validation_error_.empty();
}

bool LeaderboardEditor::select(size_t document_index, std::string& error) {
    if (!loaded_) {
        error = "Load a user file before selecting a leaderboard.";
        return false;
    }
    if (document_index >= document_.entries.size()) {
        error = "Leaderboard selection is outside the user file.";
        return false;
    }
    if (!std::holds_alternative<LocalLeaderboard>(document_.entries[document_index].asset)) {
        error = "The selected user file entry is not a leaderboard.";
        return false;
    }
    if (selection_ == document_index) {
        error.clear();
        return true;
    }

    // Local files may contain unfinished definitions. An unchanged record can be left as-is;
    // only edited raw fields need materializing before switching the selection.
    if (same_draft(draft_, loaded_draft_)) {
        selection_ = document_index;
        load_draft_from_selection();
        validate();
        ++selection_version_;
        error.clear();
        return true;
    }

    validate();
    if (!validation_error_.empty()) {
        error = validation_error_;
        return false;
    }

    UserFileDocument candidate = document_;
    const auto materialized = make_selected_definition(error);
    if (!materialized)
        return false;

    if (selection_) {
        auto* current = std::get_if<LocalLeaderboard>(&candidate.entries[*selection_].asset);
        if (!current) {
            error = "The current leaderboard selection is no longer valid.";
            return false;
        }
        LeaderboardDraft previous;
        previous.title = current->title;
        previous.description = current->description;
        std::string logic_error;
        const auto start = serialize_achievement_logic(current->start, logic_error);
        const auto cancel = serialize_achievement_logic(current->cancel, logic_error);
        const auto submit = serialize_achievement_logic(current->submit, logic_error);
        if (!start || !cancel || !submit) {
            error = "Could not serialize the current leaderboard before switching selection.";
            return false;
        }
        previous.start = *start;
        previous.cancel = *cancel;
        previous.submit = *submit;
        previous.value = current->value;
        previous.format = current->format;
        previous.lower_is_better = current->lower_is_better;
        if (!same_draft(previous, draft_))
            dirty_ = true;
        *current = *materialized;
    }

    document_ = std::move(candidate);
    selection_ = document_index;
    load_draft_from_selection();
    validate();
    ++selection_version_;
    error.clear();
    return true;
}

bool LeaderboardEditor::add_leaderboard(std::string& error) {
    if (!loaded_) {
        error = "Load a user file before adding a leaderboard.";
        return false;
    }

    if (!same_draft(draft_, loaded_draft_)) {
        validate();
        if (!validation_error_.empty()) {
            error = validation_error_;
            return false;
        }
        if (selection_ && !apply_draft(document_, error))
            return false;
    }

    LocalLeaderboard leaderboard;
    leaderboard.title = "New leaderboard";
    auto start = parse_achievement_logic("0xH000010=1", error);
    auto cancel = parse_achievement_logic("0xH000011=1", error);
    auto submit = parse_achievement_logic("0xH000012=1", error);
    if (!start || !cancel || !submit)
        return false;
    leaderboard.start = std::move(*start);
    leaderboard.cancel = std::move(*cancel);
    leaderboard.submit = std::move(*submit);
    leaderboard.value = "0xH000013";
    leaderboard.format = "VALUE";

    std::string definition_error;
    if (!leaderboard_definition(leaderboard, definition_error)) {
        error = std::move(definition_error);
        return false;
    }

    UserFileEntry entry;
    entry.asset = std::move(leaderboard);
    document_.entries.push_back(std::move(entry));
    selection_ = document_.entries.size() - 1;
    load_draft_from_selection();
    dirty_ = true;
    validate();
    ++selection_version_;
    ++edit_version_;
    error.clear();
    return true;
}

bool LeaderboardEditor::remove_selected(std::string& error) {
    if (!loaded_) {
        error = "Load a user file before removing a leaderboard.";
        return false;
    }
    if (!selection_ || *selection_ >= document_.entries.size() ||
        !std::holds_alternative<LocalLeaderboard>(document_.entries[*selection_].asset)) {
        error = "Select a leaderboard before removing it.";
        return false;
    }

    const size_t removed_index = *selection_;
    document_.entries.erase(document_.entries.begin() + static_cast<std::ptrdiff_t>(removed_index));
    selection_.reset();
    for (size_t index = removed_index; index < document_.entries.size(); ++index) {
        if (std::holds_alternative<LocalLeaderboard>(document_.entries[index].asset)) {
            selection_ = index;
            break;
        }
    }
    if (!selection_) {
        size_t index = std::min(removed_index, document_.entries.size());
        while (index > 0) {
            --index;
            if (std::holds_alternative<LocalLeaderboard>(document_.entries[index].asset)) {
                selection_ = index;
                break;
            }
        }
    }

    load_draft_from_selection();
    dirty_ = true;
    validate();
    ++selection_version_;
    ++edit_version_;
    error.clear();
    return true;
}

void LeaderboardEditor::changed() {
    ++edit_version_;
    dirty_ = true;
    validate();
}

std::optional<LocalLeaderboard> LeaderboardEditor::selected_definition(std::string& error) const {
    return make_selected_definition(error);
}

std::optional<LocalLeaderboard>
LeaderboardEditor::make_selected_definition(std::string& error) const {
    if (!loaded_) {
        error = "Load a user file before reading a leaderboard definition.";
        return std::nullopt;
    }
    if (!selection_ || *selection_ >= document_.entries.size()) {
        error = "Select a leaderboard before reading its definition.";
        return std::nullopt;
    }
    const auto* original = std::get_if<LocalLeaderboard>(&document_.entries[*selection_].asset);
    if (!original) {
        error = "The selected user file entry is not a leaderboard.";
        return std::nullopt;
    }

    for (const auto* field : {&draft_.title, &draft_.description, &draft_.start, &draft_.cancel,
                              &draft_.submit, &draft_.value, &draft_.format}) {
        if (contains_nul(*field)) {
            error = "Leaderboard draft contains an embedded NUL byte";
            return std::nullopt;
        }
    }

    LocalLeaderboard result = *original;
    auto start = parse_achievement_logic(draft_.start, error);
    if (!start) {
        error = "Invalid leaderboard start trigger: " + error;
        return std::nullopt;
    }
    auto cancel = parse_achievement_logic(draft_.cancel, error);
    if (!cancel) {
        error = "Invalid leaderboard cancel trigger: " + error;
        return std::nullopt;
    }
    auto submit = parse_achievement_logic(draft_.submit, error);
    if (!submit) {
        error = "Invalid leaderboard submit trigger: " + error;
        return std::nullopt;
    }

    result.start = std::move(*start);
    result.cancel = std::move(*cancel);
    result.submit = std::move(*submit);
    result.value = draft_.value;
    result.format = draft_.format;
    result.title = draft_.title;
    result.description = draft_.description;
    result.lower_is_better = draft_.lower_is_better;

    std::string definition_error;
    if (!leaderboard_definition(result, definition_error)) {
        error = std::move(definition_error);
        return std::nullopt;
    }
    if (draft_.format != original_format_ && !known_format(draft_.format)) {
        error = "Unknown leaderboard format; choose a format supported by rcheevos.";
        return std::nullopt;
    }

    error.clear();
    return result;
}

bool LeaderboardEditor::apply_draft(UserFileDocument& document, std::string& error) const {
    if (!selection_)
        return true;
    if (*selection_ >= document.entries.size()) {
        error = "Leaderboard selection is outside the user file.";
        return false;
    }

    auto definition = make_selected_definition(error);
    if (!definition)
        return false;
    auto* target = std::get_if<LocalLeaderboard>(&document.entries[*selection_].asset);
    if (!target) {
        error = "The selected user file entry is not a leaderboard.";
        return false;
    }
    *target = std::move(*definition);
    error.clear();
    return true;
}

void LeaderboardEditor::validate() {
    if (!loaded_) {
        validation_error_ = "Load a user file before editing leaderboards.";
        return;
    }

    if (selection_) {
        std::string error;
        if (!make_selected_definition(error)) {
            validation_error_ = std::move(error);
            return;
        }
    }

    UserFileDocument candidate = document_;
    std::string error;
    if (!apply_draft(candidate, error)) {
        validation_error_ = std::move(error);
        return;
    }
    if (!serialize_user_file(candidate, error)) {
        validation_error_ = std::move(error);
        return;
    }
    validation_error_.clear();
}

void LeaderboardEditor::load_draft_from_selection() {
    draft_ = {};
    loaded_draft_ = {};
    original_format_.clear();
    if (!selection_ || *selection_ >= document_.entries.size())
        return;
    const auto* leaderboard = std::get_if<LocalLeaderboard>(&document_.entries[*selection_].asset);
    if (!leaderboard)
        return;

    draft_.title = leaderboard->title;
    draft_.description = leaderboard->description;
    std::string error;
    const auto start = serialize_achievement_logic(leaderboard->start, error);
    const auto cancel = serialize_achievement_logic(leaderboard->cancel, error);
    const auto submit = serialize_achievement_logic(leaderboard->submit, error);
    if (!start || !cancel || !submit) {
        validation_error_ = "Could not serialize the selected leaderboard triggers: " + error;
        return;
    }
    draft_.start = *start;
    draft_.cancel = *cancel;
    draft_.submit = *submit;
    draft_.value = leaderboard->value;
    draft_.format = leaderboard->format;
    draft_.lower_is_better = leaderboard->lower_is_better;
    original_format_ = leaderboard->format;
    loaded_draft_ = draft_;
}

void LeaderboardEditor::select_first() {
    selection_.reset();
    for (size_t index = 0; index < document_.entries.size(); ++index) {
        if (std::holds_alternative<LocalLeaderboard>(document_.entries[index].asset)) {
            selection_ = index;
            return;
        }
    }
}

} // namespace cw
