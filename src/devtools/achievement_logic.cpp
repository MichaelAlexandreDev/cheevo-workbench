#include "cw/devtools/achievement_logic.hpp"

#include <rc_error.h>
#include <rc_runtime_types.h>
#include <rcheevos/rc_internal.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace cw {
namespace {

struct FlagCode {
    LogicFlag flag;
    char code;
};
constexpr std::array kFlags{
    FlagCode{LogicFlag::Standard, '\0'},       FlagCode{LogicFlag::PauseIf, 'P'},
    FlagCode{LogicFlag::ResetIf, 'R'},         FlagCode{LogicFlag::AddSource, 'A'},
    FlagCode{LogicFlag::SubSource, 'B'},       FlagCode{LogicFlag::AddHits, 'C'},
    FlagCode{LogicFlag::SubHits, 'D'},         FlagCode{LogicFlag::AndNext, 'N'},
    FlagCode{LogicFlag::OrNext, 'O'},          FlagCode{LogicFlag::Measured, 'M'},
    FlagCode{LogicFlag::MeasuredPercent, 'G'}, FlagCode{LogicFlag::MeasuredIf, 'Q'},
    FlagCode{LogicFlag::AddAddress, 'I'},      FlagCode{LogicFlag::Trigger, 'T'},
    FlagCode{LogicFlag::ResetNextIf, 'Z'},     FlagCode{LogicFlag::Remember, 'K'},
};
struct OperatorCode {
    LogicOperator op;
    std::string_view text;
};
// Longer tokens precede their one-character prefixes.
constexpr std::array kOperators{
    OperatorCode{LogicOperator::Equal, "=="},
    OperatorCode{LogicOperator::NotEqual, "!="},
    OperatorCode{LogicOperator::LessOrEqual, "<="},
    OperatorCode{LogicOperator::GreaterOrEqual, ">="},
    OperatorCode{LogicOperator::Equal, "="},
    OperatorCode{LogicOperator::Less, "<"},
    OperatorCode{LogicOperator::Greater, ">"},
    OperatorCode{LogicOperator::Multiply, "*"},
    OperatorCode{LogicOperator::Divide, "/"},
    OperatorCode{LogicOperator::BitwiseAnd, "&"},
    OperatorCode{LogicOperator::BitwiseXor, "^"},
    OperatorCode{LogicOperator::Modulo, "%"},
    OperatorCode{LogicOperator::Add, "+"},
    OperatorCode{LogicOperator::Subtract, "-"},
};
struct SizeCode {
    LogicMemorySize size;
    int runtime_size;
    std::string_view prefix;
};
constexpr std::array kSizes{
    SizeCode{LogicMemorySize::Bit0, RC_MEMSIZE_BIT_0, "0xM"},
    SizeCode{LogicMemorySize::Bit1, RC_MEMSIZE_BIT_1, "0xN"},
    SizeCode{LogicMemorySize::Bit2, RC_MEMSIZE_BIT_2, "0xO"},
    SizeCode{LogicMemorySize::Bit3, RC_MEMSIZE_BIT_3, "0xP"},
    SizeCode{LogicMemorySize::Bit4, RC_MEMSIZE_BIT_4, "0xQ"},
    SizeCode{LogicMemorySize::Bit5, RC_MEMSIZE_BIT_5, "0xR"},
    SizeCode{LogicMemorySize::Bit6, RC_MEMSIZE_BIT_6, "0xS"},
    SizeCode{LogicMemorySize::Bit7, RC_MEMSIZE_BIT_7, "0xT"},
    SizeCode{LogicMemorySize::LowNibble, RC_MEMSIZE_LOW, "0xL"},
    SizeCode{LogicMemorySize::HighNibble, RC_MEMSIZE_HIGH, "0xU"},
    SizeCode{LogicMemorySize::EightBit, RC_MEMSIZE_8_BITS, "0xH"},
    SizeCode{LogicMemorySize::SixteenBit, RC_MEMSIZE_16_BITS, "0x "},
    SizeCode{LogicMemorySize::TwentyFourBit, RC_MEMSIZE_24_BITS, "0xW"},
    SizeCode{LogicMemorySize::ThirtyTwoBit, RC_MEMSIZE_32_BITS, "0xX"},
    SizeCode{LogicMemorySize::SixteenBitBE, RC_MEMSIZE_16_BITS_BE, "0xI"},
    SizeCode{LogicMemorySize::TwentyFourBitBE, RC_MEMSIZE_24_BITS_BE, "0xJ"},
    SizeCode{LogicMemorySize::ThirtyTwoBitBE, RC_MEMSIZE_32_BITS_BE, "0xG"},
    SizeCode{LogicMemorySize::BitCount, RC_MEMSIZE_BITCOUNT, "0xK"},
    SizeCode{LogicMemorySize::Float, RC_MEMSIZE_FLOAT, "fF"},
    SizeCode{LogicMemorySize::FloatBE, RC_MEMSIZE_FLOAT_BE, "fB"},
    SizeCode{LogicMemorySize::Double32, RC_MEMSIZE_DOUBLE32, "fH"},
    SizeCode{LogicMemorySize::Double32BE, RC_MEMSIZE_DOUBLE32_BE, "fI"},
    SizeCode{LogicMemorySize::Mbf32, RC_MEMSIZE_MBF32, "fM"},
    SizeCode{LogicMemorySize::Mbf32LE, RC_MEMSIZE_MBF32_LE, "fL"},
};

class ParseScratch {
  public:
    ParseScratch() { rc_init_preparse_state(&state); }
    ~ParseScratch() { rc_destroy_preparse_state(&state); }
    ParseScratch(const ParseScratch&) = delete;
    ParseScratch& operator=(const ParseScratch&) = delete;
    rc_preparse_state_t state;
};

bool is_memory(LogicOperandType type) {
    switch (type) {
    case LogicOperandType::Address:
    case LogicOperandType::Delta:
    case LogicOperandType::Prior:
    case LogicOperandType::Bcd:
    case LogicOperandType::Inverted:
        return true;
    default:
        return false;
    }
}

bool same_operand(const LogicOperand& a, const LogicOperand& b) {
    if (a.type != b.type)
        return false;
    if (is_memory(a.type))
        return a.size == b.size && a.value == b.value;
    switch (a.type) {
    case LogicOperandType::Integer:
        return a.value == b.value;
    case LogicOperandType::Float:
        return a.float_value == b.float_value;
    case LogicOperandType::Function:
        return a.function == b.function;
    case LogicOperandType::Recall:
        return true;
    default:
        return false;
    }
}

bool same_group(const LogicGroup& a, const LogicGroup& b) {
    if (a.conditions.size() != b.conditions.size())
        return false;
    for (size_t i = 0; i < a.conditions.size(); ++i) {
        const auto& left = a.conditions[i];
        const auto& right = b.conditions[i];
        if (left.flag != right.flag || left.op != right.op ||
            left.required_hits != right.required_hits || !same_operand(left.left, right.left) ||
            left.right.has_value() != right.right.has_value() ||
            (left.right && !same_operand(*left.right, *right.right)))
            return false;
    }
    return true;
}

std::string location(size_t group, size_t row, size_t byte) {
    return "Group " + std::to_string(group) + ", condition " + std::to_string(row + 1) + ", byte " +
           std::to_string(byte + 1) + ": ";
}

} // namespace

// Only this translation unit accesses preservation metadata. No dirty flags are required:
// comparing current values to their original spelling allows edits to be undone naturally.
struct AchievementLogicFormat {
    static std::optional<std::string> canonical_operand(const LogicOperand& operand) {
        switch (operand.type) {
        case LogicOperandType::Integer:
            return std::to_string(operand.value);
        case LogicOperandType::Float: {
            if (!std::isfinite(operand.float_value))
                return std::nullopt;
            // rcheevos does not accept exponent notation. The shortest fixed representation
            // needs at most ~1100 characters for any finite double, including subnormals.
            std::array<char, 2048> buffer{};
            const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                 operand.float_value, std::chars_format::fixed);
            if (ec != std::errc{})
                return std::nullopt;
            std::string result = "f" + std::string(buffer.data(), end);
            if (result.find('.') == std::string::npos)
                result += ".0";
            return result;
        }
        case LogicOperandType::Recall:
            return "{recall}";
        case LogicOperandType::Function:
            return "@" + operand.function;
        default:
            break;
        }
        if (!is_memory(operand.type))
            return std::nullopt;
        std::string result;
        switch (operand.type) {
        case LogicOperandType::Delta:
            result = "d";
            break;
        case LogicOperandType::Prior:
            result = "p";
            break;
        case LogicOperandType::Bcd:
            result = "b";
            break;
        case LogicOperandType::Inverted:
            result = "~";
            break;
        default:
            break;
        }
        for (const auto& code : kSizes) {
            if (operand.size != code.size)
                continue;
            result += code.prefix;
            std::array<char, 8> digits{};
            const auto [end, ec] =
                std::to_chars(digits.data(), digits.data() + digits.size(), operand.value, 16);
            if (ec != std::errc{})
                return std::nullopt;
            const size_t count = static_cast<size_t>(end - digits.data());
            // RAIntegration's width follows the console context. Six digits cover PS1 RAM;
            // wider addresses are retained rather than truncated.
            result.append(count < 6 ? 6 - count : 0, '0');
            result.append(digits.data(), end);
            return result;
        }
        return std::nullopt;
    }

    static bool read_operand(const char*& cursor, ParseScratch& scratch, LogicOperand& out) {
        const char* start = cursor;
        rc_operand_t parsed{};
        const int result = rc_parse_operand(&parsed, &cursor, &scratch.state.parse);
        if (result < 0 || scratch.state.parse.offset < 0)
            return false;
        switch (parsed.type) {
        case RC_OPERAND_CONST:
            out.type = LogicOperandType::Integer;
            out.value = parsed.value.num;
            break;
        case RC_OPERAND_FP:
            out.type = LogicOperandType::Float;
            out.float_value = parsed.value.dbl;
            break;
        case RC_OPERAND_RECALL:
            out.type = LogicOperandType::Recall;
            break;
        case RC_OPERAND_FUNC:
            out.type = LogicOperandType::Function;
            out.function.assign(start + 1, cursor);
            break;
        default:
            switch (parsed.type) {
            case RC_OPERAND_ADDRESS:
                out.type = LogicOperandType::Address;
                break;
            case RC_OPERAND_DELTA:
                out.type = LogicOperandType::Delta;
                break;
            case RC_OPERAND_PRIOR:
                out.type = LogicOperandType::Prior;
                break;
            case RC_OPERAND_BCD:
                out.type = LogicOperandType::Bcd;
                break;
            case RC_OPERAND_INVERTED:
                out.type = LogicOperandType::Inverted;
                break;
            default:
                return false;
            }
            out.value = parsed.value.memref->address;
            bool found = false;
            for (const auto& code : kSizes) {
                if (code.runtime_size == parsed.size) {
                    out.size = code.size;
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
            break;
        }
        auto canonical = canonical_operand(out);
        if (!canonical)
            return false;
        out.original_text_.assign(start, cursor);
        out.original_canonical_ = std::move(*canonical);
        return true;
    }

    static bool read_condition(const char*& cursor, LogicCondition& out, std::string& error) {
        const char* start = cursor;
        ParseScratch scratch;
        rc_condition_t parsed{};
        const char* end = cursor;
        // Parse in textual order, without applying runtime transformations or sorting. This
        // also finds token boundaries correctly for bit 6 (0xS), floats, and legacy functions.
        rc_parse_condition_internal(&parsed, &end, &scratch.state.parse);
        if (scratch.state.parse.offset < 0) {
            error = rc_error_str(scratch.state.parse.offset);
            return false;
        }
        if (cursor[0] && cursor[1] == ':') {
            const char code =
                cursor[0] >= 'a' && cursor[0] <= 'z' ? cursor[0] - 'a' + 'A' : cursor[0];
            for (const auto& flag : kFlags) {
                if (flag.code == code)
                    out.flag = flag.flag;
            }
            out.original_flag_.assign(cursor, 2);
            cursor += 2;
        }
        out.original_flag_value_ = out.flag;
        if (!read_operand(cursor, scratch, out.left)) {
            error = "Could not read left operand";
            return false;
        }
        out.op = LogicOperator::None;
        out.right.reset();
        if (cursor != end) {
            const std::string_view rest(cursor, static_cast<size_t>(end - cursor));
            for (const auto& op : kOperators) {
                if (rest.starts_with(op.text)) {
                    out.op = op.op;
                    out.original_operator_ = op.text;
                    cursor += op.text.size();
                    break;
                }
            }
            if (out.op == LogicOperator::None) {
                error = "Could not read operator";
                return false;
            }
            out.right.emplace();
            if (!read_operand(cursor, scratch, *out.right)) {
                error = "Could not read right operand";
                return false;
            }
        }
        out.original_operator_value_ = out.op;
        if (cursor != end && (*cursor == '.' || *cursor == '(')) {
            const char* hits_start = cursor++;
            char* number_end = nullptr;
            // Use the pinned parser's strtoul/cast semantics, including legacy signed targets.
            out.required_hits = static_cast<uint32_t>(std::strtoul(cursor, &number_end, 10));
            cursor = number_end + 1;
            out.original_hits_.assign(hits_start, cursor);
        }
        out.original_hits_value_ = out.required_hits;
        if (cursor != end) {
            error = "Unexpected text after condition";
            return false;
        }
        if (parsed.oper == RC_OPERATOR_NONE && out.op != LogicOperator::None) {
            // rcheevos discards comparisons on A/B/I for legacy compatibility. Preserve
            // the complete original only while unchanged; expose the effective operation
            // instead of letting an editor offer comparisons that would do nothing.
            out.legacy_comparison_text_.assign(start, end);
            out.op = LogicOperator::None;
            out.right.reset();
            out.required_hits.reset();
            out.original_operator_.clear();
            out.original_operator_value_ = LogicOperator::None;
            out.original_hits_.clear();
            out.original_hits_value_.reset();
        }
        return true;
    }

    static std::optional<AchievementLogic> parse(std::string_view text, std::string& error) {
        error.clear();
        if (text.size() > kMaxAchievementLogicLength) {
            error = "Achievement logic exceeds the 1 MiB limit";
            return std::nullopt;
        }
        if (text.find('\0') != std::string_view::npos) {
            error = "Achievement logic contains an embedded NUL byte";
            return std::nullopt;
        }
        // Some legacy parser lookaheads read up to two bytes beyond a malformed token.
        // Keep padding past the terminator instead of exposing a caller's string_view storage.
        std::string storage(text);
        storage.append(3, '\0');
        const char* cursor = storage.data();
        AchievementLogic result;
        LogicGroup* group = &result.core;
        size_t group_index = 0;
        while (*cursor) {
            if (*cursor == 'S' || *cursor == 's') {
                const char separator = *cursor++;
                result.alternatives.emplace_back();
                group = &result.alternatives.back();
                group->separator_ = separator;
                ++group_index;
                continue;
            }
            LogicCondition condition;
            const size_t byte = static_cast<size_t>(cursor - storage.data());
            if (!read_condition(cursor, condition, error)) {
                error = location(group_index, group->conditions.size(), byte) + error;
                return std::nullopt;
            }
            group->conditions.push_back(std::move(condition));
            if (*cursor == '_') {
                ++cursor;
                // An underscore must be followed by a condition, never a group boundary/end.
                if (!*cursor || *cursor == 'S' || *cursor == 's') {
                    error = location(group_index, group->conditions.size(),
                                     static_cast<size_t>(cursor - storage.data())) +
                            "Missing condition after '_'";
                    return std::nullopt;
                }
            } else if (*cursor && *cursor != 'S' && *cursor != 's') {
                error = location(group_index, group->conditions.size() - 1,
                                 static_cast<size_t>(cursor - storage.data())) +
                        "Unexpected trailing text";
                return std::nullopt;
            }
        }
        // Whole-trigger validation catches cross-condition constraints such as mismatched
        // measured targets. Unlike this scanner, rc_trigger_size alone accepts trailing junk.
        const int validation = rc_trigger_size(storage.data());
        if (validation < 0) {
            error = std::string("Invalid achievement logic: ") + rc_error_str(validation);
            return std::nullopt;
        }
        return result;
    }

    static bool append_operand(std::string& out, const LogicOperand& operand, std::string& error) {
        const auto canonical = canonical_operand(operand);
        if (!canonical) {
            error = "Invalid operand type, memory size, or non-finite float";
            return false;
        }
        out += *canonical == operand.original_canonical_ ? operand.original_text_ : *canonical;
        return true;
    }

    static bool append_condition(std::string& out, const LogicCondition& condition,
                                 std::string& error) {
        if (!condition.legacy_comparison_text_.empty() &&
            condition.flag == condition.original_flag_value_ &&
            condition.op == LogicOperator::None && !condition.right && !condition.required_hits &&
            canonical_operand(condition.left) == condition.left.original_canonical_) {
            out += condition.legacy_comparison_text_;
            return true;
        }
        if ((condition.flag == LogicFlag::AddSource || condition.flag == LogicFlag::SubSource ||
             condition.flag == LogicFlag::AddAddress) &&
            (condition.op == LogicOperator::Equal || condition.op == LogicOperator::NotEqual ||
             condition.op == LogicOperator::Less || condition.op == LogicOperator::LessOrEqual ||
             condition.op == LogicOperator::Greater ||
             condition.op == LogicOperator::GreaterOrEqual)) {
            error = "Comparisons on AddSource, SubSource, and AddAddress are ignored by rcheevos; "
                    "use an arithmetic operator or no operator";
            return false;
        }
        bool flag_found = false;
        for (const auto& flag : kFlags) {
            if (condition.flag != flag.flag)
                continue;
            flag_found = true;
            if (condition.flag == condition.original_flag_value_)
                out += condition.original_flag_;
            else if (flag.code) {
                out += flag.code;
                out += ':';
            }
            break;
        }
        if (!flag_found) {
            error = "Invalid condition flag";
            return false;
        }
        if (!append_operand(out, condition.left, error))
            return false;
        if (condition.op == LogicOperator::None) {
            if (condition.right || condition.required_hits) {
                error = "A condition without an operator cannot have a right operand or hit target";
                return false;
            }
        } else {
            bool op_found = false;
            for (const auto& op : kOperators) {
                if (condition.op != op.op)
                    continue;
                op_found = true;
                if (condition.op == condition.original_operator_value_)
                    out += condition.original_operator_;
                else
                    out += condition.op == LogicOperator::Equal ? "=" : op.text;
                break;
            }
            if (!op_found || !condition.right) {
                error = "Invalid operator or missing right operand";
                return false;
            }
            if (!append_operand(out, *condition.right, error))
                return false;
        }
        if (condition.required_hits == condition.original_hits_value_)
            out += condition.original_hits_;
        else if (condition.required_hits)
            out += "." + std::to_string(*condition.required_hits) + ".";
        return true;
    }

    static bool append_group(std::string& out, const LogicGroup& group, std::string& error) {
        for (size_t i = 0; i < group.conditions.size(); ++i) {
            if (i)
                out += '_';
            if (!append_condition(out, group.conditions[i], error))
                return false;
            if (out.size() > kMaxAchievementLogicLength) {
                error = "Achievement logic exceeds the 1 MiB limit";
                return false;
            }
        }
        return true;
    }

    static std::optional<std::string> serialize(const AchievementLogic& logic, std::string& error) {
        error.clear();
        std::string out;
        if (!append_group(out, logic.core, error))
            return std::nullopt;
        for (const auto& group : logic.alternatives) {
            out += group.separator_;
            if (!append_group(out, group, error))
                return std::nullopt;
        }
        const auto checked = parse(out, error);
        if (!checked)
            return std::nullopt;
        bool same = same_group(logic.core, checked->core) &&
                    logic.alternatives.size() == checked->alternatives.size();
        if (same) {
            for (size_t i = 0; i < logic.alternatives.size(); ++i) {
                if (!same_group(logic.alternatives[i], checked->alternatives[i])) {
                    same = false;
                    break;
                }
            }
        }
        if (!same) {
            // For example, a float the pinned parser would truncate, or a function name that
            // would swallow the following separator. Never silently save a different model.
            error = "Serialization would change operand values or condition/group boundaries";
            return std::nullopt;
        }
        return out;
    }
};

std::optional<AchievementLogic> parse_achievement_logic(std::string_view text, std::string& error) {
    return AchievementLogicFormat::parse(text, error);
}

std::optional<std::string> serialize_achievement_logic(const AchievementLogic& logic,
                                                       std::string& error) {
    return AchievementLogicFormat::serialize(logic, error);
}

} // namespace cw
