#include "cw/devtools/user_file.hpp"

#include <rc_error.h>
#include <rcheevos/rc_internal.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <utility>

namespace cw {
namespace {

struct TextLine {
    std::string_view text;
    std::string_view ending;
};
TextLine next_line(std::string_view text, size_t& position) {
    const size_t start = position;
    const size_t newline = text.find('\n', start);
    const size_t stop = newline == std::string_view::npos ? text.size() : newline;
    size_t body_end = stop;
    if (body_end > start && text[body_end - 1] == '\r')
        --body_end;
    position = newline == std::string_view::npos ? text.size() : newline + 1;
    return {text.substr(start, body_end - start), text.substr(body_end, position - body_end)};
}

struct Field {
    std::string token;
    std::string value;
    bool delimiter = false;
};

bool read_field(std::string_view line, size_t& position, Field& field, bool quoted,
                bool legacy_trigger, bool unused, std::string& error) {
    const size_t start = position;
    if (quoted && (position == line.size() || line[position] != '"')) {
        error = "Expected a quoted logic/value field";
        return false;
    }
    if (!unused && position < line.size() && line[position] == '"') {
        ++position;
        bool closed = false;
        while (position < line.size()) {
            char c = line[position++];
            if (c == '"') {
                closed = true;
                break;
            }
            if (c == '\\') {
                if (position == line.size())
                    break;
                c = line[position++];
                switch (c) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                default:
                    break; // RAIntegration drops the slash for unknown escapes too.
                }
            }
            field.value += c;
        }
        if (!closed) {
            error = "Unterminated quoted field";
            return false;
        }
        if (position < line.size() && line[position] != ':') {
            error = "Unexpected text after closing quote";
            return false;
        }
    } else {
        while (position < line.size()) {
            if (line[position] == ':') {
                // This is RAIntegration's legacy unquoted-trigger whitelist, not the full
                // grammar. Modern flags must be inside a quoted trigger field.
                constexpr std::string_view flags = "ABCMNOPRTabcmnoprt";
                if (!legacy_trigger || position == start ||
                    flags.find(line[position - 1]) == std::string_view::npos)
                    break;
            }
            ++position;
        }
        field.value.assign(line.substr(start, position - start));
    }
    field.token.assign(line.substr(start, position - start));
    field.delimiter = position < line.size() && line[position] == ':';
    if (field.delimiter)
        ++position;
    return true;
}

bool uint_field(std::string_view text, uint32_t& value, bool allow_empty = false) {
    if (text.empty()) {
        value = 0;
        return allow_empty;
    }
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && end == text.data() + text.size();
}

std::string identifier(uint32_t id, std::optional<uint32_t> subset) {
    std::string result = std::to_string(id);
    if (subset)
        result += "|" + std::to_string(*subset);
    return result;
}

bool parse_identifier(std::string_view text, uint32_t& id, std::optional<uint32_t>& subset,
                      bool leaderboard, std::string& error) {
    const size_t pipe = text.find('|');
    if (!uint_field(text.substr(0, pipe), id, leaderboard)) {
        error = "Invalid or overflowing unsigned asset ID";
        return false;
    }
    if (pipe != std::string_view::npos) {
        uint32_t parsed = 0;
        if (!uint_field(text.substr(pipe + 1), parsed, true)) {
            error = "Invalid or overflowing subset ID";
            return false;
        }
        subset = parsed;
    }
    return true;
}

bool validate_value(std::string_view value, std::string& error) {
    if (value.empty())
        return true; // Drafts in local files may be incomplete.
    if (value.size() > kMaxAchievementLogicLength || value.find('\0') != std::string_view::npos) {
        error = "Invalid or oversized leaderboard value";
        return false;
    }
    std::string storage(value);
    storage.append(4, '\0');
    const char* cursor = storage.data();
    rc_preparse_state_t scratch;
    rc_init_preparse_state(&scratch);
    rc_value_t parsed{};
    rc_parse_value_internal(&parsed, &cursor, &scratch.parse);
    const int code = scratch.parse.offset;
    const bool consumed = *cursor == '\0';
    rc_destroy_preparse_state(&scratch);
    if (code < 0)
        error = std::string("Invalid leaderboard value: ") + rc_error_str(code);
    else if (!consumed)
        error = "Unexpected text after leaderboard value";
    return code >= 0 && consumed;
}

bool header_valid(std::string_view text) {
    return text.find_first_of("\r\n") == std::string_view::npos &&
           text.find('\0') == std::string_view::npos;
}

std::optional<std::string> encode_field(std::string_view text, bool force_quote,
                                        std::string& error) {
    if (text.find('\0') != std::string_view::npos) {
        error = "Text field contains an embedded NUL byte";
        return std::nullopt;
    }
    if (!force_quote && text.find_first_of(":\"\\\r\n\t") == std::string_view::npos)
        return std::string(text);
    std::string result = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }
    result += '"';
    return result;
}

std::string line_error(size_t line, size_t field, std::string_view message) {
    return "Line " + std::to_string(line) + ", field " + std::to_string(field + 1) + ": " +
           std::string(message);
}

std::string_view line_ending(const std::string& original, bool needs_separator,
                             const std::string& fallback) {
    if (needs_separator && original.find('\n') == std::string::npos)
        return original == "\r" ? std::string_view("\r\n") : std::string_view(fallback);
    return original;
}

} // namespace

struct UserFileFormat {
    static std::optional<std::vector<std::string>> achievement_values(const LocalAchievement& asset,
                                                                      std::string& error) {
        auto logic = serialize_achievement_logic(asset.logic, error);
        if (!logic)
            return std::nullopt;
        std::vector<std::string> values(14);
        // Unused legacy fields are intentionally carried through, unlike RAIntegration's
        // writer which clears them. This editor must not discard data it does not own.
        for (const size_t i : {4u, 5u, 9u, 10u, 11u, 12u}) {
            if (i < asset.original_.values_.size())
                values[i] = asset.original_.values_[i];
        }
        values[0] = identifier(asset.id, asset.subset_id);
        values[1] = std::move(*logic);
        values[2] = asset.title;
        values[3] = asset.description;
        values[6] = asset.type;
        values[7] = asset.author;
        values[8] = std::to_string(asset.points);
        values[13] = asset.badge;
        if (values[13].size() < 5)
            values[13].insert(0, 5 - values[13].size(), '0');
        return values;
    }

    static std::optional<std::vector<std::string>> leaderboard_values(const LocalLeaderboard& asset,
                                                                      std::string& error) {
        std::vector<std::string> values(9);
        values[0] = identifier(asset.id, asset.subset_id);
        const std::array<const AchievementLogic*, 3> triggers{&asset.start, &asset.cancel,
                                                              &asset.submit};
        for (size_t i = 0; i < triggers.size(); ++i) {
            auto logic = serialize_achievement_logic(*triggers[i], error);
            if (!logic)
                return std::nullopt;
            values[i + 1] = std::move(*logic);
        }
        if (!validate_value(asset.value, error))
            return std::nullopt;
        values[4] = asset.value;
        values[5] = asset.format;
        values[6] = asset.title;
        values[7] = asset.description;
        values[8] = asset.lower_is_better ? "1" : "0";
        return values;
    }

    static bool read_asset(std::string_view line, UserFileEntry& entry, size_t line_number,
                           std::string& error) {
        const bool leaderboard = line[0] == 'L';
        size_t position = leaderboard ? 1 : 0;
        std::vector<Field> fields;
        const size_t maximum = leaderboard ? 9 : 14;
        for (size_t i = 0; i < maximum; ++i) {
            if (i && !fields.back().delimiter) {
                if (leaderboard && i >= 7)
                    break;
                error = line_error(line_number, i, "Missing field");
                return false;
            }
            const bool unused = !leaderboard && (i == 4 || i == 5 || (i >= 9 && i <= 12));
            Field field;
            if (!read_field(line, position, field, leaderboard && i >= 1 && i <= 4,
                            !leaderboard && i == 1, unused, error)) {
                error = line_error(line_number, i, error);
                return false;
            }
            // RAIntegration requires title/badge to be present, even if quoted empty. A
            // trailing delimiter alone cannot supply those mandatory final text fields.
            if (((!leaderboard && i == 13) || (leaderboard && i == 6)) && field.token.empty() &&
                !field.delimiter) {
                error = line_error(line_number, i, "Missing text field");
                return false;
            }
            if ((i == 0 || i == 8) && !field.token.empty() && field.token.front() == '"') {
                error = line_error(line_number, i, "Numeric fields cannot be quoted");
                return false;
            }
            fields.push_back(std::move(field));
        }
        if (!fields[0].delimiter) {
            error = line_error(line_number, 0, "Missing asset ID delimiter");
            return false;
        }
        UserAssetText metadata;
        for (const auto& field : fields)
            metadata.tokens_.push_back(field.token);
        if (fields.size() == maximum && fields.back().delimiter)
            metadata.tail_ = ":" + std::string(line.substr(position));

        if (leaderboard) {
            LocalLeaderboard asset;
            if (!parse_identifier(fields[0].value, asset.id, asset.subset_id, true, error)) {
                error = line_error(line_number, 0, error);
                return false;
            }
            const std::array<AchievementLogic*, 3> triggers{&asset.start, &asset.cancel,
                                                            &asset.submit};
            for (size_t i = 0; i < triggers.size(); ++i) {
                auto logic = parse_achievement_logic(fields[i + 1].value, error);
                if (!logic) {
                    error = line_error(line_number, i + 1, error);
                    return false;
                }
                *triggers[i] = std::move(*logic);
            }
            asset.value = fields[4].value;
            if (!validate_value(asset.value, error)) {
                error = line_error(line_number, 4, error);
                return false;
            }
            asset.format = fields[5].value;
            asset.title = fields[6].value;
            if (fields.size() >= 8)
                asset.description = fields[7].value;
            if (fields.size() >= 9) {
                uint32_t lower = 0;
                if (!uint_field(fields[8].value, lower, true)) {
                    error = line_error(line_number, 8, "Invalid lower-is-better number");
                    return false;
                }
                asset.lower_is_better = lower != 0;
            }
            asset.original_ = std::move(metadata);
            auto values = leaderboard_values(asset, error);
            if (!values) {
                error = line_error(line_number, 0, error);
                return false;
            }
            asset.original_.values_ = std::move(*values);
            entry.asset = std::move(asset);
        } else {
            LocalAchievement asset;
            if (!parse_identifier(fields[0].value, asset.id, asset.subset_id, false, error)) {
                error = line_error(line_number, 0, error);
                return false;
            }
            auto logic = parse_achievement_logic(fields[1].value, error);
            if (!logic) {
                error = line_error(line_number, 1, error);
                return false;
            }
            asset.logic = std::move(*logic);
            asset.title = fields[2].value;
            asset.description = fields[3].value;
            asset.type = fields[6].value;
            asset.author = fields[7].value;
            if (!uint_field(fields[8].value, asset.points, true)) {
                error = line_error(line_number, 8, "Invalid or overflowing unsigned points");
                return false;
            }
            asset.badge = fields[13].value;
            if (asset.badge.size() < 5)
                asset.badge.insert(0, 5 - asset.badge.size(), '0');
            asset.original_ = std::move(metadata);
            asset.original_.values_.resize(maximum);
            for (size_t i = 0; i < maximum; ++i)
                asset.original_.values_[i] = fields[i].value;
            auto values = achievement_values(asset, error);
            if (!values) {
                error = line_error(line_number, 0, error);
                return false;
            }
            asset.original_.values_ = std::move(*values);
            entry.asset = std::move(asset);
        }
        return true;
    }

    static bool unique_id(std::set<std::pair<bool, uint32_t>>& ids, const UserFileEntry& entry,
                          size_t line, std::string& error) {
        bool leaderboard = false;
        uint32_t id = 0;
        if (const auto* asset = std::get_if<LocalAchievement>(&entry.asset))
            id = asset->id;
        else if (const auto* board = std::get_if<LocalLeaderboard>(&entry.asset)) {
            id = board->id;
            leaderboard = true;
        }
        if (id && !ids.emplace(leaderboard, id).second) {
            error = line_error(line, 0, "Duplicate asset ID for the same asset type");
            return false;
        }
        return true;
    }

    static std::optional<UserFileDocument> parse(std::string_view text, std::string& error) {
        error.clear();
        if (text.size() > kMaxUserFileSize || text.find('\0') != std::string_view::npos) {
            error = "User file exceeds 16 MiB or contains an embedded NUL byte";
            return std::nullopt;
        }
        size_t position = 0;
        if (text.empty()) {
            error = "Line 1: missing version header";
            return std::nullopt;
        }
        const auto version = next_line(text, position);
        if (position == text.size()) {
            error = "Line 2: missing game title header";
            return std::nullopt;
        }
        const auto title = next_line(text, position);
        if (!header_valid(version.text) || !header_valid(title.text)) {
            error = "User file headers must be single lines";
            return std::nullopt;
        }
        UserFileDocument document;
        document.version = version.text;
        document.title = title.text;
        document.version_ending_ = version.ending;
        document.title_ending_ = title.ending;
        document.default_ending_ = version.ending == "\r\n" ? "\r\n" : "\n";
        std::set<std::pair<bool, uint32_t>> ids;
        size_t line_number = 2;
        while (position < text.size()) {
            ++line_number;
            const auto line = next_line(text, position);
            UserFileEntry entry;
            entry.ending_ = line.ending;
            entry.has_original_ending_ = true;
            const bool achievement =
                !line.text.empty() && line.text[0] >= '0' && line.text[0] <= '9';
            const bool leaderboard = !line.text.empty() && line.text[0] == 'L';
            if (achievement || leaderboard) {
                if (!read_asset(line.text, entry, line_number, error) ||
                    !unique_id(ids, entry, line_number, error))
                    return std::nullopt;
            } else {
                PreservedUserLine raw;
                raw.text_ = line.text;
                entry.asset = std::move(raw);
            }
            document.entries.push_back(std::move(entry));
        }
        return document;
    }

    static std::optional<std::string> write_asset(const std::vector<std::string>& values,
                                                  const UserAssetText& original, bool leaderboard,
                                                  std::string& error) {
        size_t count = values.size();
        if (leaderboard && !original.tokens_.empty()) {
            count = original.tokens_.size();
            for (size_t i = count; i < values.size(); ++i) {
                if (values[i] != original.values_[i])
                    count = i + 1;
            }
        }
        std::string result = leaderboard ? "L" : "";
        for (size_t i = 0; i < count; ++i) {
            if (i)
                result += ':';
            if (i < original.tokens_.size() && values[i] == original.values_[i])
                result += original.tokens_[i];
            else {
                const bool force_quote = leaderboard ? i >= 1 && i <= 4 : i == 1;
                auto token = encode_field(values[i], force_quote, error);
                if (!token)
                    return std::nullopt;
                result += *token;
            }
        }
        result += original.tail_;
        return result;
    }

    static std::optional<std::string> serialize(const UserFileDocument& document,
                                                std::string& error) {
        error.clear();
        if (!header_valid(document.version) || !header_valid(document.title)) {
            error = "Version and game title headers must be single lines without NUL bytes";
            return std::nullopt;
        }
        std::string result = document.version + document.version_ending_ + document.title;
        result += line_ending(document.title_ending_,
                              !document.entries.empty() ||
                                  (document.title.empty() && document.title_ending_.empty()),
                              document.default_ending_);
        std::set<std::pair<bool, uint32_t>> ids;
        for (size_t i = 0; i < document.entries.size(); ++i) {
            const auto& entry = document.entries[i];
            if (!unique_id(ids, entry, i + 3, error))
                return std::nullopt;
            std::optional<std::string> line;
            if (const auto* asset = std::get_if<LocalAchievement>(&entry.asset)) {
                auto values = achievement_values(*asset, error);
                if (values)
                    line = write_asset(*values, asset->original_, false, error);
            } else if (const auto* board = std::get_if<LocalLeaderboard>(&entry.asset)) {
                auto values = leaderboard_values(*board, error);
                if (values)
                    line = write_asset(*values, board->original_, true, error);
            } else {
                const auto& raw = std::get<PreservedUserLine>(entry.asset);
                // Only the parser can populate this text. It excludes LF/NUL but may contain
                // other uninterpreted bytes that must survive an unrelated asset edit.
                line = raw.text_;
            }
            if (!line) {
                error = "Line " + std::to_string(i + 3) + ": " + error;
                return std::nullopt;
            }
            result += *line;
            const auto& ending =
                entry.has_original_ending_ ? entry.ending_ : document.default_ending_;
            result +=
                line_ending(ending, i + 1 < document.entries.size(), document.default_ending_);
            if (result.size() > kMaxUserFileSize) {
                error = "User file exceeds 16 MiB";
                return std::nullopt;
            }
        }
        // A closing quote or delimiter cannot inject a record into a field encoded above.
        // Re-reading also verifies optional legacy fields, value expressions, and header shape.
        if (!parse(result, error))
            return std::nullopt;
        return result;
    }
};

std::optional<UserFileDocument> parse_user_file(std::string_view text, std::string& error) {
    return UserFileFormat::parse(text, error);
}
bool validate_leaderboard_value(std::string_view value, std::string& error) {
    error.clear();
    return validate_value(value, error);
}
std::optional<std::string> serialize_user_file(const UserFileDocument& document,
                                               std::string& error) {
    return UserFileFormat::serialize(document, error);
}
std::filesystem::path ra_user_file_path(const std::filesystem::path& racache, uint32_t game_id) {
    if (racache.empty() || !game_id)
        return {};
    return racache / "Data" / (std::to_string(game_id) + "-User.txt");
}

} // namespace cw
