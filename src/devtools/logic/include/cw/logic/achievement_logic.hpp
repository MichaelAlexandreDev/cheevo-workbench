#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cw {

enum class LogicFlag {
    Standard,
    PauseIf,
    ResetIf,
    AddSource,
    SubSource,
    AddHits,
    SubHits,
    AndNext,
    OrNext,
    Measured,
    MeasuredPercent,
    MeasuredIf,
    AddAddress,
    Trigger,
    ResetNextIf,
    Remember,
};

enum class LogicOperandType {
    Address,
    Delta,
    Prior,
    Bcd,
    Inverted,
    Integer,
    Float,
    Recall,
    Function
};

// Logic supports formats not offered by the memory viewer (bit count and floating point).
enum class LogicMemorySize {
    Bit0,
    Bit1,
    Bit2,
    Bit3,
    Bit4,
    Bit5,
    Bit6,
    Bit7,
    LowNibble,
    HighNibble,
    EightBit,
    SixteenBit,
    TwentyFourBit,
    ThirtyTwoBit,
    SixteenBitBE,
    TwentyFourBitBE,
    ThirtyTwoBitBE,
    BitCount,
    Float,
    FloatBE,
    Double32,
    Double32BE,
    Mbf32,
    Mbf32LE,
};

enum class LogicOperator {
    None,
    Equal,
    NotEqual,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    Multiply,
    Divide,
    BitwiseAnd,
    BitwiseXor,
    Modulo,
    Add,
    Subtract,
};

struct LogicOperand {
    LogicOperandType type = LogicOperandType::Integer;
    LogicMemorySize size = LogicMemorySize::ThirtyTwoBit;
    uint32_t value = 0; // Address or unsigned integer (negative constants use two's complement).
    double float_value = 0.0;
    // Legacy @name operands are preserved; rcheevos does not implement their evaluation.
    std::string function;

  private:
    std::string original_text_;
    std::string original_canonical_;
    friend struct AchievementLogicFormat;
};

struct LogicCondition {
    LogicFlag flag = LogicFlag::Standard;
    LogicOperand left;
    LogicOperator op = LogicOperator::Equal;
    std::optional<LogicOperand> right = LogicOperand{};
    // Absence differs from an explicit .0. or (0): reset() removes the suffix entirely.
    std::optional<uint32_t> required_hits;

  private:
    std::string original_flag_;
    LogicFlag original_flag_value_ = LogicFlag::Standard;
    std::string original_operator_;
    LogicOperator original_operator_value_ = LogicOperator::None;
    std::string original_hits_;
    std::optional<uint32_t> original_hits_value_;
    // Legacy comparisons on AddSource/SubSource/AddAddress are ignored by the runtime.
    // Expose their effective op=None/right=nullopt, retaining text only until edited.
    std::string legacy_comparison_text_;
    friend struct AchievementLogicFormat;
};

struct LogicGroup {
    std::vector<LogicCondition> conditions;

  private:
    char separator_ = 'S';
    friend struct AchievementLogicFormat;
};

struct AchievementLogic {
    LogicGroup core;
    std::vector<LogicGroup> alternatives;
};

// Both APIs are transactional: failure returns nullopt with a diagnostic and never edits input.
// Empty groups are supported, as in RAIntegration. Unchanged tokens retain their original text;
// edited/new tokens use RAIntegration-compatible spelling. Full results are checked by rcheevos.
// No runtime pointers, hit counters, file I/O, or server access belong to this editable model.
inline constexpr size_t kMaxAchievementLogicLength = 1024 * 1024;
std::optional<AchievementLogic> parse_achievement_logic(std::string_view text, std::string& error);
std::optional<std::string> serialize_achievement_logic(const AchievementLogic& logic,
                                                       std::string& error);

} // namespace cw
