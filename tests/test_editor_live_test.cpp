// Live editor testing and per-condition snapshots, using only synthetic memory and temp files.

#include "cw/devtools/editor_live_test.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

cw::AchievementLogic parse_logic(std::string_view source) {
    std::string error;
    auto result = cw::parse_achievement_logic(source, error);
    if (!result)
        throw std::runtime_error("Could not parse synthetic achievement logic: " + error);
    return std::move(*result);
}

cw::LocalAchievement make_achievement(std::string title, std::string_view logic) {
    cw::LocalAchievement result;
    result.title = std::move(title);
    result.description = "Synthetic test achievement";
    result.author = "Test author";
    result.logic = parse_logic(logic);
    return result;
}

void write_user_file(const std::filesystem::path& path,
                     const std::vector<cw::LocalAchievement>& achievements) {
    cw::UserFileDocument document;
    document.title = "Synthetic game";
    for (const auto& achievement : achievements) {
        cw::UserFileEntry entry;
        entry.asset = achievement;
        document.entries.push_back(std::move(entry));
    }

    std::string error;
    const auto serialized = cw::serialize_user_file(document, error);
    if (!serialized)
        throw std::runtime_error("Could not serialize synthetic user file: " + error);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output ||
        !output.write(serialized->data(), static_cast<std::streamsize>(serialized->size())))
        throw std::runtime_error("Could not write synthetic user file: " + path.string());
}

bool has_event(const std::vector<cw::LogicEvent>& events, uint32_t id, cw::LogicEventKind kind) {
    return std::any_of(events.begin(), events.end(), [&](const cw::LogicEvent& event) {
        return event.id == id && event.kind == kind;
    });
}

} // namespace

TEST_CASE("Condition snapshots map reordered runtime storage to textual groups") {
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Ordered rows",
                               "0xH0010=1.2._P:0xH0011=1.3._R:0xH0012=1.4._A:0xH0013_0xH0014=1.5."
                               "S0xH0015=1.6.S0xH0016=1.7.",
                               error);
    REQUIRE_MESSAGE(id, error);

    const auto status = tester.snapshot(*id);
    REQUIRE(status.has_value());
    REQUIRE(status->groups.size() == 3);

    const auto& core = status->groups[0];
    REQUIRE(core.conditions.size() == 5);
    CHECK(core.conditions[0].required_hits == 2);   // first Standard condition
    CHECK(core.conditions[1].required_hits == 3);   // PauseIf
    CHECK(core.conditions[2].required_hits == 4);   // ResetIf
    CHECK_FALSE(core.conditions[3].has_comparison); // AddSource helper row
    CHECK(core.conditions[3].required_hits == 0);
    CHECK(core.conditions[4].required_hits == 5); // condition that completes AddSource

    REQUIRE(status->groups[1].conditions.size() == 1);
    CHECK(status->groups[1].conditions[0].required_hits == 6); // first Alt
    REQUIRE(status->groups[2].conditions.size() == 1);
    CHECK(status->groups[2].conditions[0].required_hits == 7); // second Alt
}

TEST_CASE("Snapshots preserve a leading empty Core and later empty Alt groups") {
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Empty core", "S0xH0010=1S", error);
    REQUIRE_MESSAGE(id, error);

    const auto status = tester.snapshot(*id);
    REQUIRE(status.has_value());
    REQUIRE(status->groups.size() == 3);
    CHECK(status->groups[0].conditions.empty());
    REQUIRE(status->groups[1].conditions.size() == 1);
    CHECK(status->groups[2].conditions.empty());
}

TEST_CASE("Snapshots expose raw truth and accumulated hits without evaluating") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Two hits and a guard", "0xH0010=1.2._0xH0011=1", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1); // false once, moving from Waiting to Active
    auto status = tester.snapshot(*id);
    REQUIRE(status.has_value());
    CHECK(status->state == cw::LogicState::Active);
    REQUIRE(status->groups.size() == 1);
    REQUIRE(status->groups[0].conditions.size() == 2);
    CHECK_FALSE(status->groups[0].conditions[0].is_true);
    CHECK(status->groups[0].conditions[0].current_hits == 0);
    CHECK(status->groups[0].conditions[0].required_hits == 2);

    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 2);
    const auto first_read = tester.snapshot(*id);
    const auto second_read = tester.snapshot(*id);
    REQUIRE(first_read.has_value());
    REQUIRE(second_read.has_value());
    CHECK(first_read->last_frame == 2);
    CHECK(second_read->last_frame == 2);
    CHECK(first_read->groups[0].conditions[0].is_true);
    CHECK(first_read->groups[0].conditions[0].current_hits == 1);
    CHECK(second_read->groups[0].conditions[0].current_hits == 1);

    tester.do_frame(memory, 3);
    status = tester.snapshot(*id);
    REQUIRE(status.has_value());
    CHECK(status->groups[0].conditions[0].current_hits == 2);
    CHECK(status->groups[0].conditions[0].is_true);

    memory.bytes()[0x10] = 0;
    tester.do_frame(memory, 4);
    status = tester.snapshot(*id);
    REQUIRE(status.has_value());
    CHECK(status->state == cw::LogicState::Active);           // the guard remains false
    CHECK_FALSE(status->groups[0].conditions[0].is_true);     // raw comparison this frame
    CHECK(status->groups[0].conditions[0].current_hits == 2); // target remains satisfied
    CHECK(status->groups[0].conditions[0].required_hits == 2);
}

TEST_CASE("A ResetIf snapshot marks the cause and shows reset hit counters") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Reset progress", "0xH0010=1.3._R:0xH0011=1", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 2);
    auto status = tester.snapshot(*id);
    REQUIRE(status.has_value());
    REQUIRE(status->groups[0].conditions.size() == 2);
    CHECK(status->groups[0].conditions[0].current_hits == 1);

    memory.bytes()[0x11] = 1;
    const auto events = tester.do_frame(memory, 3);
    CHECK(has_event(events, *id, cw::LogicEventKind::Reset));

    status = tester.snapshot(*id);
    REQUIRE(status.has_value());
    CHECK(status->groups[0].conditions[0].current_hits == 0);
    CHECK(status->groups[0].conditions[1].caused_reset);
    CHECK(status->groups[0].conditions[1].is_true);
    CHECK(status->groups[0].conditions[1].current_hits == 0);
}

TEST_CASE("A paused group retains the last evaluated rows after PauseIf stops processing") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Paused rows", "P:0xH0010=1_0xH0011=1.3.", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1);
    memory.bytes()[0x11] = 1;
    tester.do_frame(memory, 2); // the non-pause condition has one hit

    memory.bytes()[0x10] = 1;
    memory.bytes()[0x11] = 0;
    tester.do_frame(memory, 3); // PauseIf stops before the second condition
    auto before = tester.snapshot(*id);
    REQUIRE(before.has_value());
    CHECK(before->state == cw::LogicState::Paused);
    REQUIRE(before->groups[0].conditions.size() == 2);
    CHECK(before->groups[0].paused);
    CHECK(before->groups[0].conditions[1].is_true);
    CHECK(before->groups[0].conditions[1].current_hits == 1);

    tester.do_frame(memory, 4);
    const auto after = tester.snapshot(*id);
    REQUIRE(after.has_value());
    CHECK(after->groups[0].paused);
    CHECK(after->groups[0].conditions[1].is_true); // cached from the last evaluated frame
    CHECK(after->groups[0].conditions[1].current_hits == 1);
    CHECK(after->last_frame == 4);
}

TEST_CASE("Runtime progress restore rebuilds condition snapshots with saved hits") {
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    std::string error;
    const auto id = tester.add("Four hits", "0xH0010=1.4._0xH0011=1", error);
    REQUIRE_MESSAGE(id, error);

    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    tester.do_frame(memory, 2);
    tester.do_frame(memory, 3);
    auto before = tester.snapshot(*id);
    REQUIRE(before.has_value());
    REQUIRE(before->groups[0].conditions.size() == 2);
    CHECK(before->groups[0].conditions[0].current_hits == 2);
    CHECK(before->last_frame == 3);

    std::vector<uint8_t> progress;
    REQUIRE_MESSAGE(tester.serialize_progress(progress, error), error);

    tester.do_frame(memory, 4);
    auto advanced = tester.snapshot(*id);
    REQUIRE(advanced.has_value());
    CHECK(advanced->groups[0].conditions[0].current_hits == 3);

    REQUIRE_MESSAGE(tester.deserialize_progress(progress, error), error);
    const auto restored = tester.snapshot(*id);
    REQUIRE(restored.has_value());
    CHECK(restored->state == cw::LogicState::Active);
    CHECK_FALSE(restored->last_frame.has_value()); // restored progress has no frame timestamp
    REQUIRE(restored->groups[0].conditions.size() == 2);
    CHECK(restored->groups[0].conditions[0].current_hits == 2);
    CHECK(restored->groups[0].conditions[0].is_true);

    tester.do_frame(memory, 5); // snapshot now follows the newly rebuilt runtime
    const auto next = tester.snapshot(*id);
    REQUIRE(next.has_value());
    CHECK(next->groups[0].conditions[0].current_hits == 3);
    CHECK(next->last_frame == 5);
}

TEST_CASE("Editor live test keeps metadata edits and unrelated shared runtime entries") {
    cw_test::TempDir directory;
    const auto user_file = directory.path() / "18238-User.txt";
    write_user_file(user_file, {make_achievement("Draft", "0xH0010=1")});

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE_MESSAGE(editor.load(user_file, error), error);
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    const auto other_id = tester.add("Other test", "0xH0030=1.3.", error);
    REQUIRE_MESSAGE(other_id, error);

    cw::EditorLiveTest live(tester);
    REQUIRE_MESSAGE(live.start(editor, 10, memory, error), error);
    REQUIRE(live.id().has_value());
    const uint32_t editor_id = *live.id();
    CHECK(tester.achievements().size() == 2);

    tester.do_frame(memory, 1);
    memory.bytes()[0x30] = 1;
    tester.do_frame(memory, 2);
    auto other = tester.snapshot(*other_id);
    REQUIRE(other.has_value());
    REQUIRE(other->groups[0].conditions.size() == 1);
    CHECK(other->groups[0].conditions[0].current_hits == 1);

    editor.selected()->title = "Renamed draft";
    editor.selected()->description = "Metadata changed while testing";
    editor.changed();
    REQUIRE_MESSAGE(editor.save(error), error);
    live.sync(editor, 10, true);
    CHECK(live.active());
    REQUIRE(live.snapshot().has_value());
    CHECK(tester.snapshot(editor_id).has_value());

    memory.bytes()[0x30] = 1;
    tester.do_frame(memory, 3);
    other = tester.snapshot(*other_id);
    REQUIRE(other.has_value());
    CHECK(other->groups[0].conditions[0].current_hits == 2);

    live.stop();
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(editor_id).has_value());
    CHECK(tester.achievements().size() == 1);
    other = tester.snapshot(*other_id);
    REQUIRE(other.has_value());
    CHECK(other->state == cw::LogicState::Active);
    CHECK(other->groups[0].conditions[0].current_hits == 2);
    CHECK(other->last_frame == 3);
}

TEST_CASE("Editor live test stops when its selection or game binding changes") {
    cw_test::TempDir directory;
    const auto first_file = directory.path() / "first-User.txt";
    const auto second_file = directory.path() / "second-User.txt";
    write_user_file(first_file, {make_achievement("First", "0xH0010=1"),
                                 make_achievement("Second", "0xH0011=1")});
    write_user_file(second_file, {make_achievement("Third", "0xH0012=1")});

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE_MESSAGE(editor.load(first_file, error), error);
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    cw::EditorLiveTest live(tester);

    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    auto id = live.id();
    REQUIRE(id.has_value());
    REQUIRE_MESSAGE(editor.load(first_file, error), error); // same path, same first selection
    REQUIRE(editor.selection() == 0);
    REQUIRE(editor.selected() != nullptr);
    CHECK(cw::serialize_achievement_logic(editor.selected()->logic, error) ==
          std::optional<std::string>("0xH0010=1"));
    live.sync(editor, 20, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());
    CHECK(live.message() == "Live test stopped: the selected achievement changed.");

    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    id = live.id();
    REQUIRE(id.has_value());
    REQUIRE(editor.select(1, error));
    live.sync(editor, 20, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());
    CHECK(live.message() == "Live test stopped: the selected achievement changed.");

    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    id = live.id();
    REQUIRE(id.has_value());
    live.sync(editor, 21, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());
    CHECK(live.message() == "Live test stopped: the game session changed.");

    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    id = live.id();
    REQUIRE(id.has_value());
    live.sync(editor, 20, false);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());

    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    id = live.id();
    REQUIRE(id.has_value());
    REQUIRE_MESSAGE(editor.load(second_file, error), error);
    live.sync(editor, 20, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());

    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    id = live.id();
    REQUIRE(id.has_value());
    editor.set_input_error("Points input is invalid");
    live.sync(editor, 20, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());

    editor.set_input_error({});
    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    id = live.id();
    REQUIRE(id.has_value());
    editor.selected()->logic = parse_logic("0xH0012=2");
    editor.changed();
    live.sync(editor, 20, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());

    REQUIRE_MESSAGE(live.start(editor, 20, memory, error), error);
    id = live.id();
    REQUIRE(id.has_value());
    REQUIRE_MESSAGE(editor.remove_selected(error), error);
    live.sync(editor, 20, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());
}

TEST_CASE("Editor live test detects removal when the next asset has identical logic") {
    cw_test::TempDir directory;
    const auto user_file = directory.path() / "18238-User.txt";
    write_user_file(user_file, {make_achievement("First", "0xH0010=1"),
                                make_achievement("Identical next", "0xH0010=1")});

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE_MESSAGE(editor.load(user_file, error), error);
    REQUIRE(editor.selection() == 0);
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    cw::EditorLiveTest live(tester);
    REQUIRE_MESSAGE(live.start(editor, 25, memory, error), error);
    const auto id = live.id();
    REQUIRE(id.has_value());

    REQUIRE_MESSAGE(editor.remove_selected(error), error);
    REQUIRE(editor.selection() == 0); // same vector index, now the next achievement
    REQUIRE(editor.selected() != nullptr);
    CHECK(editor.selected()->title == "Identical next");
    CHECK(cw::serialize_achievement_logic(editor.selected()->logic, error) ==
          std::optional<std::string>("0xH0010=1"));

    live.sync(editor, 25, true);
    CHECK_FALSE(live.active());
    CHECK_FALSE(tester.snapshot(*id).has_value());
    CHECK(live.message() == "Live test stopped: the selected achievement changed.");
}

TEST_CASE("Editor live test rearm clears terminal progress and stops after triggering") {
    cw_test::TempDir directory;
    const auto user_file = directory.path() / "18238-User.txt";
    write_user_file(user_file, {make_achievement("Trigger me", "0xH0010=1")});

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE_MESSAGE(editor.load(user_file, error), error);
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    cw::EditorLiveTest live(tester);
    REQUIRE_MESSAGE(live.start(editor, 30, memory, error), error);
    const auto id = live.id();
    REQUIRE(id.has_value());

    tester.do_frame(memory, 1);
    memory.bytes()[0x10] = 1;
    const auto events = tester.do_frame(memory, 2);
    CHECK(has_event(events, *id, cw::LogicEventKind::Triggered));
    auto status = live.snapshot();
    REQUIRE(status.has_value());
    CHECK(status->state == cw::LogicState::Triggered);
    CHECK(status->last_frame == 2);

    memory.bytes()[0x10] = 0;
    tester.do_frame(memory, 3);
    status = live.snapshot();
    REQUIRE(status.has_value());
    CHECK(status->state == cw::LogicState::Triggered);
    CHECK(status->last_frame == 2); // terminal achievements are no longer evaluated

    REQUIRE(live.rearm());
    status = live.snapshot();
    REQUIRE(status.has_value());
    CHECK(status->state == cw::LogicState::Waiting);
    CHECK_FALSE(status->last_frame.has_value());
    REQUIRE(status->groups[0].conditions.size() == 1);
    CHECK(status->groups[0].conditions[0].current_hits == 0);

    tester.do_frame(memory, 4);
    memory.bytes()[0x10] = 1;
    CHECK(has_event(tester.do_frame(memory, 5), *id, cw::LogicEventKind::Triggered));
    CHECK(live.snapshot()->state == cw::LogicState::Triggered);
}

TEST_CASE("An editor live test reports invalid memory addresses and refuses rearm") {
    cw_test::TempDir directory;
    const auto user_file = directory.path() / "18238-User.txt";
    write_user_file(user_file, {make_achievement("Outside memory", "0xH0200=1")});

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE_MESSAGE(editor.load(user_file, error), error);
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    cw::EditorLiveTest live(tester);
    REQUIRE_MESSAGE(live.start(editor, 40, memory, error), error);

    const auto id = live.id();
    REQUIRE(id.has_value());
    const auto status = live.snapshot();
    REQUIRE(status.has_value());
    CHECK(status->state == cw::LogicState::Disabled);
    CHECK_FALSE(live.rearm());
    CHECK(live.active()); // disabled until a new memory map/session is loaded
}

TEST_CASE("A runtime entry removed outside the editor stops the attached live test") {
    cw_test::TempDir directory;
    const auto user_file = directory.path() / "18238-User.txt";
    write_user_file(user_file, {make_achievement("Draft", "0xH0010=1")});

    cw::AchievementEditor editor;
    std::string error;
    REQUIRE_MESSAGE(editor.load(user_file, error), error);
    cw::BufferMemory memory(std::vector<uint8_t>(0x100, 0));
    cw::LogicTester tester;
    cw::EditorLiveTest live(tester);
    REQUIRE_MESSAGE(live.start(editor, 50, memory, error), error);

    const auto id = live.id();
    REQUIRE(id.has_value());
    REQUIRE(tester.remove(*id));
    live.sync(editor, 50, true);
    CHECK_FALSE(live.active());
    CHECK(live.message() == "Live test stopped: its runtime entry was removed.");
}
