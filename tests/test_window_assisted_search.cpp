// Characterization tests for the Assisted Search window (assisted_search_window.cpp), driven
// through its real widgets over synthetic memory. The search itself is unit-tested in
// test_assisted_search.cpp; these tests freeze what the window does with it.

#include "window_test_support.hpp"

#include "assisted_search_window.hpp"

#include "cw/devtools/memory.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace {

constexpr const char* kWindow = "Assisted Search";

struct AssistedSearchFixture {
    cw::BufferMemory memory{std::vector<uint8_t>(64)};
    cw::AssistedSearchWindow window;
    bool allow_keyboard = true;
    std::optional<uint32_t> opened;
    cw_test::UiDriver driver{[this] {
        window.handle_hotkey(allow_keyboard);
        if (const auto address = window.draw(memory, true))
            opened = address;
    }};

    AssistedSearchFixture() {
        window.open = true;
        window.sync(1, 0, true);
        driver.frame();
    }

    // One emulated frame: the application observes after running the core, then draws.
    void play(int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            window.observe(memory);
            driver.frame();
        }
    }

    // Byte 5 changes at every marked moment; byte 9 changes at other times.
    void marked_events(bool use_hotkey) {
        for (int event = 0; event < 3; ++event) {
            play(5);
            memory.bytes()[9] ^= 1;
            play(5);
            memory.bytes()[5] ^= 1;
            play(1);
            if (use_hotkey)
                driver.key(ImGuiKey_M, ImGuiMod_Ctrl);
            else
                driver.click(kWindow, "Mark moment (Ctrl+M)");
            play(5);
        }
        play(40); // results refresh twice per second of ImGui time
    }

    void start() {
        driver.click(kWindow, "Start observation");
        REQUIRE(driver.has(kWindow, "Restart observation"));
    }

    bool lists(const char* address) const { return driver.has(kWindow, address); }
};

} // namespace

TEST_CASE("Assisted Search lists changing addresses and opens one in the Memory Viewer") {
    AssistedSearchFixture app;
    CHECK(app.driver.has(kWindow, "Start observation"));
    app.start();
    for (int frame = 1; frame <= 80; ++frame) {
        if (frame % 12 == 0)
            app.memory.bytes()[5] ^= 4;
        app.play();
    }
    app.play(40);
    REQUIRE(app.lists("0x000005"));
    CHECK_FALSE(app.lists("0x000006"));
    CHECK_FALSE(app.opened.has_value());

    app.driver.click(kWindow, "0x000005");
    CHECK(app.opened == 5u);
}

TEST_CASE("Marking a moment keeps the addresses that changed at it") {
    AssistedSearchFixture app;
    app.start();
    app.marked_events(false);
    CHECK(app.lists("0x000005"));
    CHECK_FALSE(app.lists("0x000009"));
}

TEST_CASE("Ctrl+M marks a moment only while the keyboard is free for it") {
    SUBCASE("allowed") {
        AssistedSearchFixture app;
        app.start();
        app.marked_events(true);
        CHECK(app.lists("0x000005"));
        CHECK_FALSE(app.lists("0x000009"));
    }
    SUBCASE("a text field or another window has the keyboard") {
        AssistedSearchFixture app;
        app.allow_keyboard = false;
        app.start();
        app.marked_events(true);
        CHECK(app.lists("0x000005"));
        CHECK(app.lists("0x000009"));
    }
}

TEST_CASE("Stopping the observation freezes the results") {
    AssistedSearchFixture app;
    app.start();
    for (int frame = 1; frame <= 60; ++frame) {
        if (frame % 12 == 0)
            app.memory.bytes()[5] ^= 4;
        app.play();
    }
    app.driver.click(kWindow, "Stop observation");
    for (int frame = 1; frame <= 60; ++frame) {
        if (frame % 12 == 0)
            app.memory.bytes()[30] ^= 4;
        app.play();
    }
    CHECK(app.lists("0x000005"));
    CHECK_FALSE(app.lists("0x00001E"));
    CHECK(app.driver.has(kWindow, "Restart observation"));
}
