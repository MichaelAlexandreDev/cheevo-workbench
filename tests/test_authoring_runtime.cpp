// Offline runtime tests for authored achievements, leaderboards, and Rich Presence.

#include "cw/devtools/logic_tester.hpp"

#include <doctest/doctest.h>
#include <rhash/md5.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool has_event(const std::vector<cw::LogicEvent>& events, uint32_t id, cw::LogicEventKind kind) {
    return std::any_of(events.begin(), events.end(), [&](const cw::LogicEvent& event) {
        return event.id == id && event.kind == kind;
    });
}

std::optional<cw::LogicEvent> find_event(const std::vector<cw::LogicEvent>& events, uint32_t id,
                                         cw::LogicEventKind kind) {
    const auto found = std::find_if(events.begin(), events.end(), [&](const cw::LogicEvent& event) {
        return event.id == id && event.kind == kind;
    });
    if (found == events.end())
        return std::nullopt;
    return *found;
}

cw::AchievementLogic parse_logic(std::string_view text) {
    std::string error;
    auto logic = cw::parse_achievement_logic(text, error);
    if (!logic)
        throw std::runtime_error("Invalid test logic: " + error);
    return std::move(*logic);
}

cw::LocalLeaderboard make_leaderboard(uint32_t definition_id, std::string_view start,
                                      std::string_view cancel, std::string_view submit,
                                      std::string_view value, std::string_view format) {
    cw::LocalLeaderboard board;
    board.id = definition_id;
    board.start = parse_logic(start);
    board.cancel = parse_logic(cancel);
    board.submit = parse_logic(submit);
    board.value = value;
    board.format = format;
    board.title = "Synthetic leaderboard";
    board.description = "Synthetic record";
    return board;
}

uint32_t read_u32(std::span<const uint8_t> data, size_t offset) {
    return uint32_t{data[offset]} | uint32_t{data[offset + 1]} << 8 |
           uint32_t{data[offset + 2]} << 16 | uint32_t{data[offset + 3]} << 24;
}

void write_u32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        data[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
}

size_t chunk_offset(std::span<const uint8_t> data, std::string_view chunk_id) {
    size_t offset = 4;
    while (offset + 8 <= data.size()) {
        if (chunk_id.size() == 4 &&
            std::memcmp(data.data() + offset, chunk_id.data(), chunk_id.size()) == 0)
            return offset;

        const size_t size = read_u32(data, offset + 4);
        if (size > data.size() - offset - 8)
            break;
        offset += 8 + size;
    }
    return data.size();
}

size_t chunk_size(std::span<const uint8_t> data, std::string_view chunk_id) {
    const size_t offset = chunk_offset(data, chunk_id);
    return offset == data.size() ? 0 : read_u32(data, offset + 4);
}

bool erase_chunk_bytes(std::vector<uint8_t>& data, std::string_view chunk_id, size_t payload_offset,
                       size_t count) {
    const size_t offset = chunk_offset(data, chunk_id);
    if (offset == data.size())
        return false;
    const size_t old_size = read_u32(data, offset + 4);
    if (payload_offset > old_size || count > old_size - payload_offset)
        return false;

    const size_t begin = offset + 8 + payload_offset;
    data.erase(data.begin() + static_cast<std::ptrdiff_t>(begin),
               data.begin() + static_cast<std::ptrdiff_t>(begin + count));
    write_u32(data, offset + 4, static_cast<uint32_t>(old_size - count));
    return true;
}

bool erase_chunk_tail(std::vector<uint8_t>& data, std::string_view chunk_id, size_t count) {
    const size_t offset = chunk_offset(data, chunk_id);
    if (offset == data.size())
        return false;
    const size_t size = read_u32(data, offset + 4);
    if (count > size)
        return false;
    return erase_chunk_bytes(data, chunk_id, size - count, count);
}

void refresh_rap_checksum(std::vector<uint8_t>& data) {
    md5_state_t checksum;
    md5_init(&checksum);
    md5_append(&checksum, data.data(), static_cast<int>(data.size() - 16));
    md5_finish(&checksum, data.data() + data.size() - 16);
}

constexpr std::string_view kPresenceScript = "Lookup:Mode\n"
                                             "0=Idle\n"
                                             "1=Battle\n"
                                             "*=Unknown\n"
                                             "\n"
                                             "Format:Score\n"
                                             "FormatType=SCORE\n"
                                             "\n"
                                             "Display:\n"
                                             "?0xH0010=1.3.?Ready @Score(0xH0013) @Mode(0xH0011) "
                                             "@Number(d0xH0012)/@Number(p0xH0012)\n"
                                             "Default @Mode(0xH0011) @Score(0xH0013) "
                                             "@Number(d0xH0012)/@Number(p0xH0012)\n";

constexpr std::string_view kProgressPresenceScript = "Format:Count\n"
                                                     "FormatType=VALUE\n"
                                                     "\n"
                                                     "Display:\n"
                                                     "?0xH0010=1.3.?Ready @Count(M:0xH0015=1.4.)\n"
                                                     "Default @Count(M:0xH0015=1.4.)\n";

struct ProgressIds {
    uint32_t achievement = 0;
    uint32_t leaderboard = 0;
};

bool seed_progress_scenario(cw::LogicTester& tester, cw::BufferMemory& memory, ProgressIds& ids,
                            std::vector<uint8_t>& progress, std::string& error) {
    const auto achievement = tester.add("Five measured hits", "M:0xH0010=1.5.", error);
    if (!achievement)
        return false;
    ids.achievement = *achievement;

    auto definition =
        make_leaderboard(77, "0xH0014=1", "0xH0011=8", "0xH0012=9", "0xH0013", "SCORE");
    const auto leaderboard = tester.add_leaderboard(definition, error);
    if (!leaderboard)
        return false;
    ids.leaderboard = *leaderboard;

    if (!tester.set_rich_presence(std::string(kProgressPresenceScript), error))
        return false;

    tester.do_frame(memory, 1); // all start/display conditions are false once
    memory.bytes()[0x10] = 1;
    memory.bytes()[0x13] = 7;
    memory.bytes()[0x14] = 1;
    memory.bytes()[0x15] = 1;
    tester.do_frame(memory, 2); // one achievement/RP hit and a started leaderboard
    tester.do_frame(memory, 3); // two achievement/RP/value hits
    return tester.serialize_progress(progress, error);
}

} // namespace

TEST_CASE("Leaderboards report exact lifecycle frames, cached values, and RA formats") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;

    const auto score_id = tester.add_leaderboard(
        make_leaderboard(51, "0xH0010=1", "0xH0011=1", "0xH0012=1", "0xH0013", "SCORE"), error);
    const auto frames_id = tester.add_leaderboard(
        make_leaderboard(52, "0xH0010=1", "0xH0011=1", "0xH0012=1", "0xH0013", "FRAMES"), error);
    REQUIRE_MESSAGE(score_id, error);
    REQUIRE_MESSAGE(frames_id, error);

    const auto waiting = tester.leaderboard_snapshot(*score_id);
    REQUIRE(waiting);
    CHECK(waiting->state == cw::LeaderboardState::Waiting);
    CHECK_FALSE(waiting->last_frame);

    auto events = tester.do_frame(memory, 1); // false start activates both leaderboards
    CHECK_FALSE(has_event(events, *score_id, cw::LogicEventKind::LeaderboardStarted));
    CHECK(tester.leaderboard_snapshot(*score_id)->state == cw::LeaderboardState::Active);

    memory.bytes()[0x10] = 1;
    memory.bytes()[0x13] = 60;
    events = tester.do_frame(memory, 2);
    const auto started = find_event(events, *score_id, cw::LogicEventKind::LeaderboardStarted);
    REQUIRE(started);
    CHECK(started->frame == 2);
    CHECK(has_event(events, *frames_id, cw::LogicEventKind::LeaderboardStarted));

    const auto score = tester.leaderboard_snapshot(*score_id);
    const auto frames = tester.leaderboard_snapshot(*frames_id);
    REQUIRE((score && frames));
    CHECK(score->state == cw::LeaderboardState::Started);
    CHECK(score->value == 60);
    CHECK(score->formatted_value == "000060");
    CHECK(frames->formatted_value == "0:01.00");
    REQUIRE(score->last_frame);
    CHECK(*score->last_frame == 2);

    memory.bytes()[0x13] = 99;
    const auto cached = tester.leaderboard_snapshot(*score_id);
    REQUIRE(cached);
    CHECK(cached->value == 60); // reading a snapshot must not evaluate changed memory
    CHECK(*cached->last_frame == 2);

    events = tester.do_frame(memory, 3);
    const auto updated = find_event(events, *score_id, cw::LogicEventKind::LeaderboardUpdated);
    REQUIRE(updated);
    CHECK(updated->frame == 3);
    CHECK(tester.leaderboard_snapshot(*score_id)->value == 99);

    memory.bytes()[0x11] = 1;
    events = tester.do_frame(memory, 4);
    const auto canceled = find_event(events, *score_id, cw::LogicEventKind::LeaderboardCanceled);
    REQUIRE(canceled);
    CHECK(canceled->frame == 4);
    CHECK(tester.leaderboard_snapshot(*score_id)->state == cw::LeaderboardState::Canceled);

    REQUIRE(tester.rearm_leaderboard(*score_id));
    CHECK(tester.leaderboard_snapshot(*score_id)->state == cw::LeaderboardState::Waiting);
    memory.bytes()[0x11] = 0;
    memory.bytes()[0x10] = 0;
    tester.do_frame(memory, 5); // start false re-arms the waiting state
    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 6);
    memory.bytes()[0x12] = 1;
    memory.bytes()[0x13] = 150; // final value may change on the submit frame
    events = tester.do_frame(memory, 7);
    const auto submitted = find_event(events, *score_id, cw::LogicEventKind::LeaderboardSubmitted);
    REQUIRE(submitted);
    CHECK(submitted->frame == 7);
    CHECK(submitted->value == 150);
    CHECK(tester.leaderboard_snapshot(*score_id)->value == 150);
    CHECK(tester.leaderboard_snapshot(*score_id)->state == cw::LeaderboardState::Triggered);

    CHECK(tester.remove_leaderboard(*frames_id));
    CHECK_FALSE(tester.remove_leaderboard(*frames_id));
    CHECK_FALSE(tester.rearm_leaderboard(*frames_id));
    CHECK_FALSE(tester.leaderboard_snapshot(*frames_id));
}

TEST_CASE("Unreadable leaderboard addresses disable the board until the memory map is reset") {
    cw::BufferMemory small(std::vector<uint8_t>(0x100, 0));
    cw::BufferMemory large(std::vector<uint8_t>(0x1000, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add_leaderboard(
        make_leaderboard(60, "0xH0800=1", "0xH0011=1", "0xH0012=1", "0xH0013", "VALUE"), error);
    REQUIRE_MESSAGE(id, error);

    const auto disabled = tester.validate_addresses(small, 11);
    const auto event = find_event(disabled, *id, cw::LogicEventKind::LeaderboardDisabled);
    REQUIRE(event);
    CHECK(event->frame == 11);
    CHECK(event->value == 0x0800);
    CHECK(tester.leaderboard_snapshot(*id)->state == cw::LeaderboardState::Disabled);
    CHECK_FALSE(tester.rearm_leaderboard(*id));

    tester.reset();
    CHECK(tester.validate_addresses(large, 12).empty());
    CHECK(tester.leaderboard_snapshot(*id)->state == cw::LeaderboardState::Waiting);
    large.bytes()[0x0800] = 0;
    tester.do_frame(large, 13);
    large.bytes()[0x0800] = 1;
    CHECK(has_event(tester.do_frame(large, 14), *id, cw::LogicEventKind::LeaderboardStarted));
}

TEST_CASE("Rich Presence truncation preserves complete UTF-8 characters") {
    cw::BufferMemory memory(std::vector<uint8_t>(1, 0));
    cw::LogicTester tester;
    std::string error;

    SUBCASE("A complete character ends at the boundary") {
        const std::string prefix(16382, 'a');
        REQUIRE_MESSAGE(tester.set_rich_presence("Display:\n" + prefix + "\xC3\xA9" + "x\n", error),
                        error);
        tester.do_frame(memory, 1);
        CHECK(tester.rich_presence_truncated());
        CHECK(tester.rich_presence_output() == prefix + "\xC3\xA9");
    }
    SUBCASE("A character is split at the boundary") {
        const std::string prefix(16383, 'a');
        REQUIRE_MESSAGE(tester.set_rich_presence("Display:\n" + prefix + "\xC3\xA9" + "x\n", error),
                        error);
        tester.do_frame(memory, 1);
        CHECK(tester.rich_presence_truncated());
        CHECK(tester.rich_presence_output() == prefix);
    }
}

TEST_CASE("Rich Presence caches lookup, format, hit-target, delta, and prior output by frame") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    REQUIRE_MESSAGE(tester.set_rich_presence(std::string(kPresenceScript), error), error);
    CHECK(tester.rich_presence_script() == kPresenceScript);

    tester.do_frame(memory, 1);
    CHECK(tester.rich_presence_output() == "Default Idle 000000 0/0");
    CHECK(tester.rich_presence_frame() == 1);

    memory.bytes()[0x10] = 1;
    memory.bytes()[0x11] = 1;
    memory.bytes()[0x12] = 4;
    memory.bytes()[0x13] = 42;
    tester.do_frame(memory, 2); // conditional display has one of three required hits
    CHECK(tester.rich_presence_output() == "Default Battle 000042 0/0");
    CHECK(tester.rich_presence_frame() == 2);

    memory.bytes()[0x11] = 0;
    memory.bytes()[0x12] = 9;
    memory.bytes()[0x13] = 88;
    CHECK(tester.rich_presence_output() == "Default Battle 000042 0/0");
    CHECK(tester.rich_presence_frame() == 2); // neither accessor reads or evaluates memory

    tester.do_frame(memory, 3);
    CHECK(tester.rich_presence_output() == "Default Idle 000088 4/4");
    tester.do_frame(memory, 4); // third consecutive hit activates the conditional line
    CHECK(tester.rich_presence_output() == "Ready 000088 Idle 9/4");
    CHECK(tester.rich_presence_frame() == 4);

    const std::string invalid = "Display:\n?I:0x0x0010=1?Broken\nDefault\n";
    CHECK_FALSE(tester.set_rich_presence(invalid, error));
    CHECK(error.find("line 2") != std::string::npos);
    CHECK(tester.rich_presence_script() == kPresenceScript);
    CHECK(tester.rich_presence_output() == "Ready 000088 Idle 9/4");

    memory.bytes()[0x13] = 90;
    CHECK(tester.rich_presence_output() == "Ready 000088 Idle 9/4");
    tester.do_frame(memory, 5);
    CHECK(tester.rich_presence_output() == "Ready 000090 Idle 9/4");
}

TEST_CASE("Replacing and clearing Rich Presence preserves achievement and leaderboard progress") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto achievement = tester.add("Five hits", "M:0xH0010=1.5.", error);
    const auto leaderboard = tester.add_leaderboard(
        make_leaderboard(61, "0xH0014=1", "0xH0011=8", "0xH0012=9", "0xH0013", "SCORE"), error);
    REQUIRE_MESSAGE((achievement && leaderboard), error);
    REQUIRE_MESSAGE(tester.set_rich_presence("Display:\nOld display\n", error), error);

    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    memory.bytes()[0x13] = 7;
    memory.bytes()[0x14] = 1;
    tester.do_frame(memory, 2);
    tester.do_frame(memory, 3);
    REQUIRE(tester.measured(*achievement));
    CHECK(tester.measured(*achievement)->first == 2);
    REQUIRE(tester.leaderboard_snapshot(*leaderboard));
    CHECK(tester.leaderboard_snapshot(*leaderboard)->state == cw::LeaderboardState::Started);

    REQUIRE_MESSAGE(tester.set_rich_presence("Display:\nNew display\n", error), error);
    CHECK(tester.rich_presence_script() == "Display:\nNew display\n");
    REQUIRE(tester.measured(*achievement));
    CHECK(tester.measured(*achievement)->first == 2);
    CHECK(tester.leaderboard_snapshot(*leaderboard)->state == cw::LeaderboardState::Started);

    REQUIRE_MESSAGE(tester.set_rich_presence("", error), error);
    CHECK(tester.rich_presence_script().empty());
    CHECK(tester.rich_presence_output().empty());
    REQUIRE(tester.measured(*achievement));
    CHECK(tester.measured(*achievement)->first == 2);
    CHECK(tester.leaderboard_snapshot(*leaderboard)->state == cw::LeaderboardState::Started);

    memory.bytes()[0x12] = 9;
    CHECK(has_event(tester.do_frame(memory, 4), *leaderboard,
                    cw::LogicEventKind::LeaderboardSubmitted));
    CHECK_FALSE(has_event(tester.do_frame(memory, 5), *achievement, cw::LogicEventKind::Triggered));
    const auto completed = tester.do_frame(memory, 6);
    const auto achievement_event =
        find_event(completed, *achievement, cw::LogicEventKind::Triggered);
    REQUIRE(achievement_event);
    CHECK(achievement_event->frame == 6);
}

TEST_CASE("RAP progress resumes partial hits, a started leaderboard, and conditional presence") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    ProgressIds ids;
    std::string error;
    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(seed_progress_scenario(tester, memory, ids, progress, error), error);

    CHECK(chunk_size(progress, "CWRC") == 16);
    CHECK(chunk_size(progress, "LBRD") > 20);
    CHECK(chunk_size(progress, "RICH") > 16);
    CHECK(chunk_size(progress, "VARS") > 4);
    CHECK(tester.measured(ids.achievement)->first == 2);
    REQUIRE(tester.leaderboard_snapshot(ids.leaderboard));
    CHECK(tester.leaderboard_snapshot(ids.leaderboard)->state == cw::LeaderboardState::Started);

    memory.bytes()[0x13] = 8;
    const auto frame_four = tester.do_frame(memory, 4);
    CHECK(has_event(frame_four, ids.leaderboard, cw::LogicEventKind::LeaderboardUpdated));
    CHECK(tester.rich_presence_output().starts_with("Ready "));
    memory.bytes()[0x12] = 9;
    CHECK(has_event(tester.do_frame(memory, 5), ids.leaderboard,
                    cw::LogicEventKind::LeaderboardSubmitted));
    const auto frame_six = tester.do_frame(memory, 6);
    CHECK(has_event(frame_six, ids.achievement, cw::LogicEventKind::Triggered));

    REQUIRE_MESSAGE(tester.deserialize_progress(progress, error), error);
    CHECK(tester.state(ids.achievement) == cw::LogicState::Active);
    const auto restored_measurement = tester.measured(ids.achievement);
    REQUIRE(restored_measurement);
    CHECK(restored_measurement->first == 2);
    CHECK(restored_measurement->second == 5);
    const auto restored_leaderboard = tester.leaderboard_snapshot(ids.leaderboard);
    REQUIRE(restored_leaderboard);
    CHECK(restored_leaderboard->state == cw::LeaderboardState::Started);
    CHECK(restored_leaderboard->value == 7);

    memory.bytes()[0x12] = 0;
    memory.bytes()[0x13] = 9;
    const auto resumed = tester.do_frame(memory, 7);
    const auto update =
        find_event(resumed, ids.leaderboard, cw::LogicEventKind::LeaderboardUpdated);
    REQUIRE(update);
    CHECK(update->frame == 7);
    CHECK(tester.rich_presence_output().starts_with("Ready "));
    CHECK_FALSE(has_event(resumed, ids.achievement, cw::LogicEventKind::Triggered));
    CHECK_FALSE(
        has_event(tester.do_frame(memory, 8), ids.achievement, cw::LogicEventKind::Triggered));
    const auto completed = tester.do_frame(memory, 9);
    const auto triggered = find_event(completed, ids.achievement, cw::LogicEventKind::Triggered);
    REQUIRE(triggered);
    CHECK(triggered->frame == 9);
}

TEST_CASE("Changed or removed definitions do not consume unmatched nested progress") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester saved;
    ProgressIds saved_ids;
    std::string error;
    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(seed_progress_scenario(saved, memory, saved_ids, progress, error), error);

    SUBCASE(
        "A changed RP script resets its displays and variables, retaining achievement and board") {
        cw::LogicTester current;
        const auto achievement = current.add("Five measured hits", "M:0xH0010=1.5.", error);
        const auto board = current.add_leaderboard(
            make_leaderboard(77, "0xH0014=1", "0xH0011=8", "0xH0012=9", "0xH0013", "SCORE"), error);
        REQUIRE_MESSAGE((achievement && board), error);
        const std::string changed_script =
            "Display:\n?0xH0010=1.3.?Changed ready\nChanged default\n";
        REQUIRE_MESSAGE(current.set_rich_presence(changed_script, error), error);

        REQUIRE_MESSAGE(current.deserialize_progress(progress, error), error);
        REQUIRE(current.measured(*achievement));
        CHECK(current.measured(*achievement)->first == 2);
        CHECK(current.leaderboard_snapshot(*board)->state == cw::LeaderboardState::Started);
        CHECK(current.rich_presence_script() == changed_script);

        memory.bytes()[0x10] = 0;
        current.do_frame(memory, 4); // the new definition must observe false before it can hit
        CHECK(current.rich_presence_output() == "Changed default");
        memory.bytes()[0x10] = 1;
        current.do_frame(memory, 5);
        CHECK(current.rich_presence_output() == "Changed default");
        current.do_frame(memory, 6);
        CHECK(current.rich_presence_output() == "Changed default");
        current.do_frame(memory, 7);
        CHECK(current.rich_presence_output() == "Changed ready");
    }

    SUBCASE("An edited leaderboard starts fresh while the matching achievement restores") {
        cw::LogicTester current;
        const auto achievement = current.add("Five measured hits", "M:0xH0010=1.5.", error);
        const auto board = current.add_leaderboard(
            make_leaderboard(77, "0xH0014=1", "0xH0011=7", "0xH0012=9", "0xH0013", "SCORE"), error);
        REQUIRE_MESSAGE((achievement && board), error);
        REQUIRE_MESSAGE(current.set_rich_presence(std::string(kProgressPresenceScript), error),
                        error);

        REQUIRE_MESSAGE(current.deserialize_progress(progress, error), error);
        REQUIRE(current.measured(*achievement));
        CHECK(current.measured(*achievement)->first == 2);
        CHECK(current.leaderboard_snapshot(*board)->state == cw::LeaderboardState::Waiting);
        memory.bytes()[0x14] = 0;
        current.do_frame(memory, 4);
        CHECK(current.leaderboard_snapshot(*board)->state == cw::LeaderboardState::Active);
    }

    SUBCASE("An empty target safely discards memory-backed progress and extension chunks") {
        cw::LogicTester empty;
        REQUIRE_MESSAGE(empty.deserialize_progress(progress, error), error);
        CHECK(empty.achievements().empty());
        CHECK(empty.leaderboards().empty());
        CHECK(empty.rich_presence_script().empty());
    }
}

TEST_CASE("Malformed nested RAP chunks are rejected transactionally after checksum refresh") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    ProgressIds ids;
    std::string error;
    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(seed_progress_scenario(tester, memory, ids, progress, error), error);
    const std::string output_before = tester.rich_presence_output();

    const auto reject_without_mutation = [&](const std::vector<uint8_t>& malformed) {
        error.clear();
        CHECK_FALSE(tester.deserialize_progress(malformed, error));
        CHECK_FALSE(error.empty());
        CHECK(tester.state(ids.achievement) == cw::LogicState::Active);
        const auto measurement = tester.measured(ids.achievement);
        REQUIRE(measurement);
        CHECK(measurement->first == 2);
        const auto board = tester.leaderboard_snapshot(ids.leaderboard);
        REQUIRE(board);
        CHECK(board->state == cw::LeaderboardState::Started);
        CHECK(board->value == 7);
        CHECK(tester.rich_presence_output() == output_before);

        std::vector<uint8_t> after_failure;
        REQUIRE_MESSAGE(tester.serialize_progress(after_failure, error), error);
        CHECK(after_failure == progress);
    };

    SUBCASE("LBRD start condition payload is truncated") {
        auto malformed = progress;
        // Leaderboard payload begins with id, checksum, state, then the start trigger. Remove its
        // first condition's hit/flag pair while keeping all following chunks correctly framed.
        REQUIRE(erase_chunk_bytes(malformed, "LBRD", 36, 8));
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("LBRD value payload is truncated") {
        auto malformed = progress;
        const size_t payload_size = chunk_size(malformed, "LBRD");
        REQUIRE(payload_size >= 8);
        REQUIRE(erase_chunk_tail(malformed, "LBRD", 8));
        refresh_rap_checksum(malformed);
        CHECK(chunk_size(malformed, "LBRD") == payload_size - 8);
        reject_without_mutation(malformed);
    }

    SUBCASE("RICH conditional trigger is truncated") {
        auto malformed = progress;
        REQUIRE(erase_chunk_tail(malformed, "RICH", 8));
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("VARS count exceeds the defined variables") {
        auto malformed = progress;
        const size_t variables = chunk_offset(malformed, "VARS");
        REQUIRE(variables < malformed.size());
        write_u32(malformed, variables + 8, 0xFFFFFFFF);
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("VARS contains an unknown variable hash") {
        auto malformed = progress;
        const size_t variables = chunk_offset(malformed, "VARS");
        REQUIRE(variables < malformed.size());
        const uint32_t old_hash = read_u32(malformed, variables + 12);
        write_u32(malformed, variables + 12, old_hash ^ 1);
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("VARS condition flag omits its serialized condition data") {
        auto malformed = progress;
        const size_t variables = chunk_offset(malformed, "VARS");
        REQUIRE(variables < malformed.size());
        const uint32_t flags = read_u32(malformed, variables + 16);
        REQUIRE((flags & 0x01000000) != 0);
        write_u32(malformed, variables + 16, flags & ~0x01000000u);
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("VARS condition set is truncated") {
        auto malformed = progress;
        REQUIRE(erase_chunk_tail(malformed, "VARS", 8));
        refresh_rap_checksum(malformed);
        reject_without_mutation(malformed);
    }

    SUBCASE("CWRC checksum extension has an invalid length") {
        auto malformed = progress;
        REQUIRE(erase_chunk_tail(malformed, "CWRC", 4));
        refresh_rap_checksum(malformed);
        CHECK(chunk_size(malformed, "CWRC") == 12);
        reject_without_mutation(malformed);
    }
}

TEST_CASE("Truncated RAP progress is rejected without changing the running state") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    ProgressIds ids;
    std::string error;
    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(seed_progress_scenario(tester, memory, ids, progress, error), error);

    for (size_t size = 0; size < progress.size(); ++size) {
        error.clear();
        const std::span<const uint8_t> truncated(progress.data(), size);
        CHECK_FALSE(tester.deserialize_progress(truncated, error));
        CHECK(tester.state(ids.achievement) == cw::LogicState::Active);
        const auto measurement = tester.measured(ids.achievement);
        REQUIRE(measurement);
        CHECK(measurement->first == 2);
        CHECK(tester.leaderboard_snapshot(ids.leaderboard)->state == cw::LeaderboardState::Started);
    }
}

TEST_CASE("CWLB preserves disabled local leaderboards and rejects malformed terminal records") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester saved;
    std::string error;
    const auto saved_id = saved.add_leaderboard(
        make_leaderboard(80, "0xH0999=1", "0xH0011=1", "0xH0012=1", "0xH0013", "VALUE"), error);
    REQUIRE_MESSAGE(saved_id, error);
    REQUIRE(has_event(saved.validate_addresses(memory, 1), *saved_id,
                      cw::LogicEventKind::LeaderboardDisabled));
    CHECK(saved.leaderboard_snapshot(*saved_id)->state == cw::LeaderboardState::Disabled);

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(saved.serialize_progress(progress, error), error);
    const size_t disabled_offset = chunk_offset(progress, "CWLB");
    REQUIRE(disabled_offset < progress.size());
    CHECK(read_u32(progress, disabled_offset + 4) == 28);

    cw::LogicTester restored;
    const auto restored_id = restored.add_leaderboard(
        make_leaderboard(80, "0xH0999=1", "0xH0011=1", "0xH0012=1", "0xH0013", "VALUE"), error);
    REQUIRE_MESSAGE(restored_id, error);
    REQUIRE_MESSAGE(restored.deserialize_progress(progress, error), error);
    CHECK(restored.leaderboard_snapshot(*restored_id)->state == cw::LeaderboardState::Disabled);
    CHECK_FALSE(restored.rearm_leaderboard(*restored_id));

    auto malformed = progress;
    REQUIRE(erase_chunk_tail(malformed, "CWLB", 4));
    refresh_rap_checksum(malformed);
    CHECK(chunk_size(malformed, "CWLB") == 24);
    const auto before = restored.leaderboard_snapshot(*restored_id);
    REQUIRE(before);
    CHECK_FALSE(restored.deserialize_progress(malformed, error));
    CHECK(restored.leaderboard_snapshot(*restored_id)->state == before->state);
}

TEST_CASE("Unmatched Rich Presence chunks are filtered before unsafe native parsing") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester saved;
    ProgressIds saved_ids;
    std::string error;
    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(seed_progress_scenario(saved, memory, saved_ids, progress, error), error);

    auto unsafe = progress;
    const size_t variables = chunk_offset(unsafe, "VARS");
    REQUIRE(variables < unsafe.size());
    write_u32(unsafe, variables + 8, 0xFFFFFFFF); // would exceed the target script's value list
    REQUIRE(erase_chunk_tail(unsafe, "RICH", 8)); // incomplete old conditional trigger payload
    refresh_rap_checksum(unsafe);

    SUBCASE("A changed script starts with fresh variables and display hits") {
        cw::LogicTester changed;
        const auto achievement = changed.add("Five measured hits", "M:0xH0010=1.5.", error);
        const auto leaderboard = changed.add_leaderboard(
            make_leaderboard(77, "0xH0014=1", "0xH0011=8", "0xH0012=9", "0xH0013", "SCORE"), error);
        REQUIRE_MESSAGE((achievement && leaderboard), error);
        REQUIRE_MESSAGE(
            changed.set_rich_presence("Display:\n?0xH0010=1.3.?New ready\nNew default\n", error),
            error);

        REQUIRE_MESSAGE(changed.deserialize_progress(unsafe, error), error);
        CHECK(changed.measured(*achievement)->first == 2);
        CHECK(changed.leaderboard_snapshot(*leaderboard)->state == cw::LeaderboardState::Started);
        memory.bytes()[0x10] = 0;
        changed.do_frame(memory, 4);
        CHECK(changed.rich_presence_output() == "New default");
        memory.bytes()[0x10] = 1;
        changed.do_frame(memory, 5);
        CHECK(changed.rich_presence_output() == "New default");
        changed.do_frame(memory, 6);
        CHECK(changed.rich_presence_output() == "New default");
        changed.do_frame(memory, 7);
        CHECK(changed.rich_presence_output() == "New ready");
    }

    SUBCASE("A runtime without Rich Presence drops those chunks") {
        cw::LogicTester no_presence;
        const auto achievement = no_presence.add("Five measured hits", "M:0xH0010=1.5.", error);
        const auto leaderboard = no_presence.add_leaderboard(
            make_leaderboard(77, "0xH0014=1", "0xH0011=8", "0xH0012=9", "0xH0013", "SCORE"), error);
        REQUIRE_MESSAGE((achievement && leaderboard), error);

        REQUIRE_MESSAGE(no_presence.deserialize_progress(unsafe, error), error);
        CHECK(no_presence.measured(*achievement)->first == 2);
        CHECK(no_presence.leaderboard_snapshot(*leaderboard)->state ==
              cw::LeaderboardState::Started);
        CHECK(no_presence.rich_presence_script().empty());
    }
}

TEST_CASE("Leaderboard value fields reject embedded runtime clauses") {
    cw::LogicTester tester;
    std::string error;
    const auto board =
        make_leaderboard(1, "0xH10=1", "0xH11=1", "0xH12=1", "0xH13::PRO:0xH14", "VALUE");
    CHECK_FALSE(tester.add_leaderboard(board, error));
    CHECK(tester.leaderboards().empty());
}

TEST_CASE("A constant leaderboard supports the documented frame-advance test") {
    cw::LogicTester tester;
    cw::BufferMemory memory(std::vector<uint8_t>(1, 0));
    std::string error;
    const auto id = tester.add_leaderboard(
        make_leaderboard(1, "0=0.2.", "0=1", "0=0.5.", "M:123", "SCORE"), error);
    REQUIRE_MESSAGE(id, error);
    tester.do_frame(memory, 1);
    CHECK(tester.leaderboard_snapshot(*id)->state == cw::LeaderboardState::Active);
    tester.do_frame(memory, 2);
    CHECK(tester.leaderboard_snapshot(*id)->state == cw::LeaderboardState::Started);
    std::vector<uint8_t> progress;
    REQUIRE(tester.serialize_progress(progress, error));
    tester.do_frame(memory, 3);
    tester.do_frame(memory, 4);
    CHECK(tester.leaderboard_snapshot(*id)->state == cw::LeaderboardState::Started);
    const auto events = tester.do_frame(memory, 5);
    CHECK(has_event(events, *id, cw::LogicEventKind::LeaderboardSubmitted));
    CHECK(tester.leaderboard_snapshot(*id)->formatted_value == "000123");
    REQUIRE_MESSAGE(tester.deserialize_progress(progress, error), error);
    CHECK(tester.leaderboard_snapshot(*id)->state == cw::LeaderboardState::Started);
    CHECK(tester.leaderboard_snapshot(*id)->value == 123);
}
