#include "cw/devtools/achievement_logic.hpp"
#include "cw/devtools/logic_tester.hpp"

#include <doctest/doctest.h>
#include <rc_runtime_types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void check_rcheevos_accepts(std::string_view logic) {
    const std::string terminated(logic);
    const int size = rc_trigger_size(terminated.c_str());
    CHECK(size > 0);
    if (size <= 0) {
        return;
    }

    // rc_parse_trigger writes into caller-owned, suitably aligned storage of rc_trigger_size().
    const size_t words =
        (static_cast<size_t>(size) + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
    std::vector<std::max_align_t> buffer(words);
    CHECK(rc_parse_trigger(buffer.data(), terminated.c_str(), nullptr, 0) != nullptr);
}

void check_round_trip(std::string_view logic) {
    std::string error = "stale error";
    const auto parsed = cw::parse_achievement_logic(logic, error);
    REQUIRE_MESSAGE(parsed.has_value(), error);
    CHECK(error.empty());

    const auto serialized = cw::serialize_achievement_logic(*parsed, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(error.empty());
    CHECK(*serialized == logic);
    check_rcheevos_accepts(*serialized);
}

std::optional<std::string> serialize_model(const cw::AchievementLogic& logic) {
    std::string error = "stale error";
    const auto serialized = cw::serialize_achievement_logic(logic, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(error.empty());
    if (serialized) {
        check_rcheevos_accepts(*serialized);
    }
    return serialized;
}

bool has_event(const std::vector<cw::LogicEvent>& events, uint32_t id, cw::LogicEventKind kind) {
    return std::any_of(events.begin(), events.end(), [id, kind](const cw::LogicEvent& event) {
        return event.id == id && event.kind == kind;
    });
}

} // namespace

TEST_CASE("Achievement logic preserves original spelling and legacy syntax") {
    const std::vector<std::string> fixtures = {
        "p:0Xh00aF==0xH0000",
        "0xH000A=1(03)",
        "0xH000A=1(0)",
        "0xH000a=1.03.",
        "0xH000A=1_0xL000b!=d0xL000bS0xX000C>=hA",
        "0xH0001=1_0xH0002=2S0xH0003=3SR:0xH0004=4S",
        "0xH0001=1s0xH0002=2sR:0xH0003=3",
        "0x1234=5",
        "A:0xH0010>5_0xH0020=1",
    };

    for (const auto& fixture : fixtures) {
        CAPTURE(fixture);
        check_round_trip(fixture);
    }
}

TEST_CASE("Empty core and alternative groups round trip as RAIntegration permits") {
    for (const std::string_view fixture : {"", "S", "SS", "S0xH0001=1S"}) {
        CAPTURE(fixture);
        check_round_trip(fixture);
    }
}

TEST_CASE("S in a bit-six address is not an alternative-group separator") {
    const std::string_view fixture = "0xS0010=1_0xH0020=0";
    std::string error;
    const auto parsed = cw::parse_achievement_logic(fixture, error);
    REQUIRE_MESSAGE(parsed.has_value(), error);
    REQUIRE(parsed->core.conditions.size() == 2);
    CHECK(parsed->alternatives.empty());

    const auto serialized = cw::serialize_achievement_logic(*parsed, error);
    REQUIRE_MESSAGE(serialized.has_value(), error);
    CHECK(*serialized == fixture);
    check_rcheevos_accepts(*serialized);
}

TEST_CASE("Every rcheevos condition flag round trips") {
    using Entry = std::pair<std::string_view, cw::LogicFlag>;
    const std::vector<Entry> fixtures = {
        {"P:0xH0010=1", cw::LogicFlag::PauseIf},
        {"R:0xH0010=1", cw::LogicFlag::ResetIf},
        {"A:0xH0010+1", cw::LogicFlag::AddSource},
        {"B:0xH0010-1", cw::LogicFlag::SubSource},
        {"C:0xH0010=1", cw::LogicFlag::AddHits},
        {"D:0xH0010=1", cw::LogicFlag::SubHits},
        {"N:0xH0010=1", cw::LogicFlag::AndNext},
        {"O:0xH0010=1", cw::LogicFlag::OrNext},
        {"M:0xH0010=5", cw::LogicFlag::Measured},
        {"G:0xH0010=100", cw::LogicFlag::MeasuredPercent},
        {"Q:0xH0010=1", cw::LogicFlag::MeasuredIf},
        {"I:0xH0010+1", cw::LogicFlag::AddAddress},
        {"T:0xH0010=1", cw::LogicFlag::Trigger},
        {"Z:0xH0010=1", cw::LogicFlag::ResetNextIf},
        {"K:0xH0010", cw::LogicFlag::Remember},
    };

    for (const auto& [fixture, expected_flag] : fixtures) {
        CAPTURE(fixture);
        std::string error;
        const auto parsed = cw::parse_achievement_logic(fixture, error);
        REQUIRE_MESSAGE(parsed.has_value(), error);
        REQUIRE(parsed->core.conditions.size() == 1);
        CHECK(parsed->core.conditions[0].flag == expected_flag);
        CHECK(error.empty());

        const auto serialized = cw::serialize_achievement_logic(*parsed, error);
        REQUIRE_MESSAGE(serialized.has_value(), error);
        CHECK(*serialized == fixture);
        CHECK(error.empty());
        check_rcheevos_accepts(*serialized);
    }
}

TEST_CASE("All RAIntegration memory sizes map to the editable operand model") {
    using Entry = std::pair<std::string_view, cw::LogicMemorySize>;
    const std::vector<Entry> fixtures = {
        {"0xM0010=1", cw::LogicMemorySize::Bit0},
        {"0xN0010=1", cw::LogicMemorySize::Bit1},
        {"0xO0010=1", cw::LogicMemorySize::Bit2},
        {"0xP0010=1", cw::LogicMemorySize::Bit3},
        {"0xQ0010=1", cw::LogicMemorySize::Bit4},
        {"0xR0010=1", cw::LogicMemorySize::Bit5},
        {"0xS0010=1", cw::LogicMemorySize::Bit6},
        {"0xT0010=1", cw::LogicMemorySize::Bit7},
        {"0xL0010=1", cw::LogicMemorySize::LowNibble},
        {"0xU0010=1", cw::LogicMemorySize::HighNibble},
        {"0xH0010=1", cw::LogicMemorySize::EightBit},
        {"0x 0010=1", cw::LogicMemorySize::SixteenBit},
        {"0x1234=1", cw::LogicMemorySize::SixteenBit},
        {"0xW0010=1", cw::LogicMemorySize::TwentyFourBit},
        {"0xX0010=1", cw::LogicMemorySize::ThirtyTwoBit},
        {"0xI0010=1", cw::LogicMemorySize::SixteenBitBE},
        {"0xJ0010=1", cw::LogicMemorySize::TwentyFourBitBE},
        {"0xG0010=1", cw::LogicMemorySize::ThirtyTwoBitBE},
        {"0xK0010=1", cw::LogicMemorySize::BitCount},
        {"fF0010=f1.5", cw::LogicMemorySize::Float},
        {"fB0010=f1.5", cw::LogicMemorySize::FloatBE},
        {"fH0010=f1.5", cw::LogicMemorySize::Double32},
        {"fI0010=f1.5", cw::LogicMemorySize::Double32BE},
        {"fM0010=f1.5", cw::LogicMemorySize::Mbf32},
        {"fL0010=f1.5", cw::LogicMemorySize::Mbf32LE},
    };

    for (const auto& [fixture, expected_size] : fixtures) {
        CAPTURE(fixture);
        std::string error;
        const auto parsed = cw::parse_achievement_logic(fixture, error);
        REQUIRE_MESSAGE(parsed.has_value(), error);
        REQUIRE(parsed->core.conditions.size() == 1);
        CHECK(parsed->core.conditions[0].left.type == cw::LogicOperandType::Address);
        CHECK(parsed->core.conditions[0].left.size == expected_size);
        CHECK(error.empty());

        const auto serialized = cw::serialize_achievement_logic(*parsed, error);
        REQUIRE_MESSAGE(serialized.has_value(), error);
        CHECK(*serialized == fixture);
        check_rcheevos_accepts(*serialized);

        auto edited = *parsed;
        edited.core.conditions[0].left.value = 0x20;
        const auto edited_serialized = serialize_model(edited);
        REQUIRE(edited_serialized.has_value());
        const auto reparsed = cw::parse_achievement_logic(*edited_serialized, error);
        REQUIRE_MESSAGE(reparsed.has_value(), error);
        REQUIRE(reparsed->core.conditions.size() == 1);
        CHECK(reparsed->core.conditions[0].left.type == cw::LogicOperandType::Address);
        CHECK(reparsed->core.conditions[0].left.size == expected_size);
        CHECK(reparsed->core.conditions[0].left.value == 0x20);
    }
}

TEST_CASE("Operand modifiers, functions, recall, and numeric constants round trip") {
    const std::vector<std::string> fixtures = {
        "d0xH0010=0xH0011",     "p0xH0010=0xH0011", "b0xH0010=1",  "~0xH0010=0",
        "K:0xH0010_{recall}=5", "@Legacy_Name1=5",  "0xH0010=-5",  "0xH0011=+5",
        "0xH0012=h1F",          "0xH0013=f-1.25",   "0xH0014=v-5", "0xH0015=V+5",
    };

    for (const auto& fixture : fixtures) {
        CAPTURE(fixture);
        check_round_trip(fixture);
    }

    std::string error;
    const auto signed_logic = cw::parse_achievement_logic("0xH0010=-5_0xH0011=h1F", error);
    REQUIRE_MESSAGE(signed_logic.has_value(), error);
    REQUIRE(signed_logic->core.conditions.size() == 2);
    REQUIRE(signed_logic->core.conditions[0].right.has_value());
    REQUIRE(signed_logic->core.conditions[1].right.has_value());
    CHECK(signed_logic->core.conditions[0].right->type == cw::LogicOperandType::Integer);
    CHECK(signed_logic->core.conditions[0].right->value == static_cast<uint32_t>(-5));
    CHECK(signed_logic->core.conditions[1].right->value == 0x1Fu);
}

TEST_CASE("Edited delta, prior, BCD, and inverted operands retain their size and modifier") {
    using Entry = std::pair<std::string_view, cw::LogicOperandType>;
    const std::vector<Entry> fixtures = {
        {"d0xH0010=0xH0011", cw::LogicOperandType::Delta},
        {"p0xH0010=0xH0011", cw::LogicOperandType::Prior},
        {"b0xH0010=1", cw::LogicOperandType::Bcd},
        {"~0xH0010=0", cw::LogicOperandType::Inverted},
    };

    for (const auto& [fixture, expected_type] : fixtures) {
        CAPTURE(fixture);
        std::string error;
        auto parsed = cw::parse_achievement_logic(fixture, error);
        REQUIRE_MESSAGE(parsed.has_value(), error);
        REQUIRE(parsed->core.conditions.size() == 1);
        parsed->core.conditions[0].left.value = 0x20;

        const auto serialized = serialize_model(*parsed);
        REQUIRE(serialized.has_value());
        const auto reparsed = cw::parse_achievement_logic(*serialized, error);
        REQUIRE_MESSAGE(reparsed.has_value(), error);
        REQUIRE(reparsed->core.conditions.size() == 1);
        CHECK(reparsed->core.conditions[0].left.type == expected_type);
        CHECK(reparsed->core.conditions[0].left.size == cw::LogicMemorySize::EightBit);
        CHECK(reparsed->core.conditions[0].left.value == 0x20);
    }
}

TEST_CASE("AddSource, AddAddress, and arithmetic operators preserve their chains") {
    const std::vector<std::string> fixtures = {
        "A:0xH0010_A:0xH0011_M:0xH0012=10",
        "I:0xX0010_0xH0000=1",
        "0xH0010=5S0xH0020=1SI:0x 1234_A:0xH0021_0xH0030=2",
        "A:0xH0010",
        "A:0xH0010*2",
        "A:0xH0010/2",
        "A:0xH0010&2",
        "A:0xH0010^2",
        "A:0xH0010%2",
        "A:0xH0010+2",
        "A:0xH0010-2",
    };

    for (const auto& fixture : fixtures) {
        CAPTURE(fixture);
        check_round_trip(fixture);
    }
}

TEST_CASE("Editing operators and inserting, moving, and deleting rows and groups serializes") {
    std::string error;
    auto parsed = cw::parse_achievement_logic("0xH0010=1_0xH0020=2S0xH0030=3S0xH0040=4", error);
    REQUIRE_MESSAGE(parsed.has_value(), error);
    REQUIRE(parsed->core.conditions.size() == 2);
    REQUIRE(parsed->alternatives.size() == 2);

    parsed->core.conditions[0].op = cw::LogicOperator::NotEqual;
    auto inserted_row = parsed->core.conditions[0];
    inserted_row.left.value = 0x50;
    inserted_row.op = cw::LogicOperator::Less;
    inserted_row.right->value = 5;
    parsed->core.conditions.insert(parsed->core.conditions.begin() + 1, inserted_row);

    auto serialized = serialize_model(*parsed);
    REQUIRE(serialized.has_value());
    CHECK(*serialized == "0xH0010!=1_0xH000050<5_0xH0020=2S0xH0030=3S0xH0040=4");

    std::rotate(parsed->core.conditions.begin() + 1, parsed->core.conditions.begin() + 2,
                parsed->core.conditions.end());
    serialized = serialize_model(*parsed);
    REQUIRE(serialized.has_value());
    CHECK(*serialized == "0xH0010!=1_0xH0020=2_0xH000050<5S0xH0030=3S0xH0040=4");

    parsed->core.conditions.erase(parsed->core.conditions.begin() + 1);
    serialized = serialize_model(*parsed);
    REQUIRE(serialized.has_value());
    CHECK(*serialized == "0xH0010!=1_0xH000050<5S0xH0030=3S0xH0040=4");

    cw::LogicGroup inserted_group;
    auto group_condition = parsed->core.conditions.back();
    group_condition.left.value = 0x60;
    group_condition.right->value = 6;
    inserted_group.conditions.push_back(group_condition);
    parsed->alternatives.insert(parsed->alternatives.begin() + 1, inserted_group);
    serialized = serialize_model(*parsed);
    REQUIRE(serialized.has_value());
    CHECK(*serialized == "0xH0010!=1_0xH000050<5S0xH0030=3S0xH000060<6S0xH0040=4");

    std::rotate(parsed->alternatives.begin() + 1, parsed->alternatives.begin() + 2,
                parsed->alternatives.end());
    serialized = serialize_model(*parsed);
    REQUIRE(serialized.has_value());
    CHECK(*serialized == "0xH0010!=1_0xH000050<5S0xH0030=3S0xH0040=4S0xH000060<6");

    parsed->alternatives.erase(parsed->alternatives.begin());
    serialized = serialize_model(*parsed);
    REQUIRE(serialized.has_value());
    CHECK(*serialized == "0xH0010!=1_0xH000050<5S0xH0040=4S0xH000060<6");
}

TEST_CASE("A new model serializes edited flag, operand type and size, value, hits, and float") {
    cw::AchievementLogic logic;
    cw::LogicCondition condition;
    condition.flag = cw::LogicFlag::ResetIf;
    condition.left.type = cw::LogicOperandType::Delta;
    condition.left.size = cw::LogicMemorySize::ThirtyTwoBitBE;
    condition.left.value = 0x1234;
    condition.op = cw::LogicOperator::LessOrEqual;
    condition.right->type = cw::LogicOperandType::Float;
    condition.right->float_value = 1.5;
    condition.required_hits = 3;
    logic.core.conditions.push_back(condition);

    const auto serialized = serialize_model(logic);
    REQUIRE(serialized.has_value());
    CHECK(*serialized == "R:d0xG001234<=f1.5.3.");
    check_round_trip(*serialized);
}

TEST_CASE("Parser rejects malformed suffixes, embedded NUL, and unknown flags") {
    const std::vector<std::string> malformed = {
        "0xH0010=", "0xH0010=1 trailing", "0xH0010=1_", "X:0xH0010=1", "0xH0010=1.2", "M:0xH0010",
    };

    for (const auto& fixture : malformed) {
        CAPTURE(fixture);
        std::string error;
        CHECK_FALSE(cw::parse_achievement_logic(fixture, error).has_value());
        CHECK_FALSE(error.empty());
    }

    std::string embedded_nul = "0xH0010=1";
    embedded_nul.push_back('\0');
    embedded_nul += "_0xH0020=2";
    std::string error;
    CHECK_FALSE(cw::parse_achievement_logic(embedded_nul, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("Serializer rejects invalid editable models and reports diagnostics") {
    std::string error;
    const auto parsed = cw::parse_achievement_logic("0xH0010=1", error);
    REQUIRE_MESSAGE(parsed.has_value(), error);

    auto invalid_flag = *parsed;
    invalid_flag.core.conditions[0].flag = static_cast<cw::LogicFlag>(255);
    error.clear();
    CHECK_FALSE(cw::serialize_achievement_logic(invalid_flag, error).has_value());
    CHECK_FALSE(error.empty());

    auto invalid_operator = *parsed;
    invalid_operator.core.conditions[0].op = static_cast<cw::LogicOperator>(255);
    error.clear();
    CHECK_FALSE(cw::serialize_achievement_logic(invalid_operator, error).has_value());
    CHECK_FALSE(error.empty());

    auto missing_operator = *parsed;
    missing_operator.core.conditions[0].op = cw::LogicOperator::None;
    missing_operator.core.conditions[0].right.reset();
    error.clear();
    CHECK_FALSE(cw::serialize_achievement_logic(missing_operator, error).has_value());
    CHECK_FALSE(error.empty());

    auto invalid_operand = *parsed;
    invalid_operand.core.conditions[0].left.type = static_cast<cw::LogicOperandType>(255);
    error.clear();
    CHECK_FALSE(cw::serialize_achievement_logic(invalid_operand, error).has_value());
    CHECK_FALSE(error.empty());

    auto invalid_float = *parsed;
    invalid_float.core.conditions[0].right->type = cw::LogicOperandType::Float;
    invalid_float.core.conditions[0].right->float_value = std::numeric_limits<double>::quiet_NaN();
    error.clear();
    CHECK_FALSE(cw::serialize_achievement_logic(invalid_float, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("Edited equality logic matches the original on synthetic memory") {
    const std::string original_logic = "0xH0010=5";
    std::string error;
    auto model = cw::parse_achievement_logic(original_logic, error);
    REQUIRE_MESSAGE(model.has_value(), error);

    auto upper_bound = model->core.conditions.front();
    model->core.conditions.front().op = cw::LogicOperator::GreaterOrEqual;
    upper_bound.op = cw::LogicOperator::LessOrEqual;
    model->core.conditions.push_back(upper_bound);

    const auto edited_logic = cw::serialize_achievement_logic(*model, error);
    REQUIRE_MESSAGE(edited_logic.has_value(), error);
    CHECK(*edited_logic == "0xH0010>=5_0xH0010<=5");
    check_rcheevos_accepts(*edited_logic);

    cw::BufferMemory original_memory(std::vector<uint8_t>(0x40, 0));
    cw::BufferMemory edited_memory(std::vector<uint8_t>(0x40, 0));
    cw::LogicTester original_tester;
    cw::LogicTester edited_tester;
    const auto original_id = original_tester.add("Original", original_logic, error);
    REQUIRE_MESSAGE(original_id.has_value(), error);
    CHECK(error.empty());
    const auto edited_id = edited_tester.add("Edited", *edited_logic, error);
    REQUIRE_MESSAGE(edited_id.has_value(), error);
    CHECK(error.empty());

    uint64_t frame = 0;
    for (const uint8_t value : {uint8_t{0}, uint8_t{4}, uint8_t{5}, uint8_t{6}}) {
        ++frame;
        original_memory.bytes()[0x10] = value;
        edited_memory.bytes()[0x10] = value;

        const auto original_events = original_tester.do_frame(original_memory, frame);
        const auto edited_events = edited_tester.do_frame(edited_memory, frame);
        REQUIRE(original_events.size() == edited_events.size());
        for (size_t index = 0; index < original_events.size(); ++index) {
            CHECK(original_events[index].id == edited_events[index].id);
            CHECK(original_events[index].kind == edited_events[index].kind);
            CHECK(original_events[index].value == edited_events[index].value);
            CHECK(original_events[index].frame == edited_events[index].frame);
        }
        CHECK(has_event(original_events, *original_id, cw::LogicEventKind::Triggered) ==
              (value == 5));
        CHECK(has_event(edited_events, *edited_id, cw::LogicEventKind::Triggered) == (value == 5));
        CHECK(original_tester.state(*original_id) == edited_tester.state(*edited_id));
    }
}

TEST_CASE("Editing one field preserves unrelated spelling and supports undo") {
    const std::string original = "p:0Xh00aF==hA(03)";
    std::string error;
    auto model = cw::parse_achievement_logic(original, error);
    REQUIRE_MESSAGE(model.has_value(), error);
    auto& row = model->core.conditions.front();
    row.right->value = 11;
    CHECK(serialize_model(*model) == "p:0Xh00aF==11(03)");
    row.right->value = 10;
    CHECK(serialize_model(*model) == original);
    row.required_hits = 5;
    CHECK(serialize_model(*model) == "p:0Xh00aF==hA.5.");
    row.required_hits.reset();
    row.flag = cw::LogicFlag::ResetIf;
    CHECK(serialize_model(*model) == "R:0Xh00aF==hA");
    row.flag = cw::LogicFlag::Standard;
    CHECK(serialize_model(*model) == "0Xh00aF==hA");
}

TEST_CASE("Explicit zero hits can be removed before removing an operator") {
    for (const std::string_view original : {"0xH1=1.0.", "0xH1=1(0)"}) {
        std::string error;
        auto model = cw::parse_achievement_logic(original, error);
        REQUIRE_MESSAGE(model.has_value(), error);
        auto& row = model->core.conditions.front();
        REQUIRE(row.required_hits.has_value());
        CHECK(*row.required_hits == 0);
        CHECK(serialize_model(*model) == original);
        row.required_hits.reset();
        CHECK(serialize_model(*model) == "0xH1=1");
        row.flag = cw::LogicFlag::AddSource;
        row.op = cw::LogicOperator::None;
        row.right.reset();
        CHECK(serialize_model(*model) == "A:0xH1");
    }
}

TEST_CASE("Legacy ignored comparisons expose their effective operation and preserve only unchanged "
          "text") {
    for (const std::string_view original : {"A:0xH1=5.7.", "b:0xH1>5(0)", "I:0xH1!=9"}) {
        CAPTURE(original);
        std::string error;
        auto model = cw::parse_achievement_logic(original, error);
        REQUIRE_MESSAGE(model.has_value(), error);
        auto& row = model->core.conditions.front();
        CHECK(row.op == cw::LogicOperator::None);
        CHECK_FALSE(row.right.has_value());
        CHECK_FALSE(row.required_hits.has_value());
        CHECK(serialize_model(*model) == original);
        row.left.value = 2;
        const auto changed = serialize_model(*model);
        REQUIRE(changed.has_value());
        CHECK(changed->substr(2) == "0xH000002");
        row.left.value = 1;
        CHECK(serialize_model(*model) == original);
        // Newly requesting an ineffective comparison must fail, not silently discard a field.
        row.op = cw::LogicOperator::Equal;
        row.right.emplace().value = 3;
        CHECK_FALSE(cw::serialize_achievement_logic(*model, error).has_value());
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("New edited floats cannot silently lose precision in the pinned parser") {
    check_round_trip("fF000010=f0.123456789123");
    std::string error;
    auto model = cw::parse_achievement_logic("fF000010=f1.5", error);
    REQUIRE_MESSAGE(model.has_value(), error);
    auto& value = model->core.conditions.front().right->float_value;
    value = 12.25;
    CHECK(serialize_model(*model) == "fF000010=f12.25");
    value = 0.0000000001;
    CHECK_FALSE(cw::serialize_achievement_logic(*model, error).has_value());
    CHECK_FALSE(error.empty());
    value = std::numeric_limits<double>::infinity();
    CHECK_FALSE(cw::serialize_achievement_logic(*model, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("Full trigger constraints and complete consumption are both enforced") {
    for (const std::string_view bad :
         {"M:0xH1=10SM:0xH2=20", "M:0xH1=10_M:0xH2=10", "0xH1=1)", "0xH1=1$0xH2=2",
          "0xH1=1_S0xH2=2", "h", "H", "f", "0x", "{", "@"}) {
        CAPTURE(bad);
        std::string error;
        CHECK_FALSE(cw::parse_achievement_logic(bad, error).has_value());
        CHECK_FALSE(error.empty());
    }
    std::string error;
    const std::string oversized(cw::kMaxAchievementLogicLength + 1, 'S');
    CHECK_FALSE(cw::parse_achievement_logic(oversized, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("A function name cannot silently inject a condition or group") {
    std::string error;
    auto model = cw::parse_achievement_logic("@Legacy=1", error);
    REQUIRE_MESSAGE(model.has_value(), error);
    model->core.conditions[0].left.function = "Legacy=1S0xH2";
    CHECK_FALSE(cw::serialize_achievement_logic(*model, error).has_value());
    CHECK_FALSE(error.empty());
}
