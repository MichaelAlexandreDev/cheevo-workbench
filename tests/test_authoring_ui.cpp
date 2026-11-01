#include "authoring_window.hpp"
#include "cw/devtools/leaderboard_editor.hpp"
#include "cw/devtools/logic_tester.hpp"
#include "cw/devtools/rich_presence_editor.hpp"

#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/cw-authoring-ui-XXXXXX";
        const char* created = mkdtemp(pattern);
        if (created)
            path = created;
    }
    ~TemporaryDirectory() {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
    std::filesystem::path path;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct HeadlessUI {
    HeadlessUI() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 900);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    ~HeadlessUI() { ImGui::DestroyContext(); }

    void frame(cw::AuthoringWindow& window) {
        ImGui::NewFrame();
        window.draw({}, {});
        ImGui::Render();
    }

    // Drive the same buttons a user sees. Their location follows the modal's text line,
    // padding and measured labels, so this also exercises disabled-button behavior.
    bool click_modal(cw::AuthoringWindow& window, int button) {
        frame(window);
        frame(window);
        ImGuiWindow* popup = ImGui::FindWindowByName("Unsaved authoring changes");
        if (!popup || !popup->Active)
            return false;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float save_width = ImGui::CalcTextSize("Save").x + style.FramePadding.x * 2;
        const float discard_width = ImGui::CalcTextSize("Discard").x + style.FramePadding.x * 2;
        const float cancel_width = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2;
        float x = popup->Pos.x + style.WindowPadding.x;
        float width = save_width;
        if (button >= 1) {
            x += save_width + style.ItemSpacing.x;
            width = discard_width;
        }
        if (button >= 2) {
            x += discard_width + style.ItemSpacing.x;
            width = cancel_width;
        }
        const float y = popup->Pos.y + popup->TitleBarHeight + style.WindowPadding.y +
                        ImGui::GetTextLineHeight() + style.ItemSpacing.y +
                        ImGui::GetFrameHeight() * 0.5f;
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x + width * 0.5f, y);
        io.AddMouseButtonEvent(0, true);
        frame(window);
        io.AddMouseButtonEvent(0, false);
        frame(window);
        return true;
    }
};

} // namespace

TEST_CASE("Authoring quit confirmation saves or discards real dirty files") {
    TemporaryDirectory directory;
    REQUIRE_FALSE(directory.path.empty());
    HeadlessUI ui;
    cw::LogicTester tester;
    cw::AuthoringWindow window(tester);
    window.rich_presence_open = true;
    window.leaderboards_open = true;
    std::string error;
    const auto rich = directory.path / "1-Rich.txt";
    const auto user = directory.path / "1-User.txt";
    REQUIRE(window.presence_session().load(rich, error));
    REQUIRE(window.leaderboard_session().load(user, error));
    window.presence_session().set_text("Display:\nSynthetic state\n");
    REQUIRE(window.leaderboard_session().add_leaderboard(error));
    ui.frame(window);
    CHECK_FALSE(window.allow_quit());

    SUBCASE("Cancel leaves both drafts intact") {
        REQUIRE(ui.click_modal(window, 2));
        ui.frame(window);
        CHECK_FALSE(ImGui::FindWindowByName("Unsaved authoring changes")->Active);
        CHECK_FALSE(window.take_quit_request());
        CHECK(window.presence_session().dirty());
        CHECK(window.leaderboard_session().dirty());
        CHECK_FALSE(std::filesystem::exists(rich));
        CHECK_FALSE(std::filesystem::exists(user));
        CHECK_FALSE(window.allow_quit());
    }
    SUBCASE("Save writes both drafts and requests quit") {
        REQUIRE(ui.click_modal(window, 0));
        CHECK(window.take_quit_request());
        CHECK_FALSE(window.take_quit_request());
        CHECK_FALSE(window.presence_session().dirty());
        CHECK_FALSE(window.leaderboard_session().dirty());
        CHECK(read_file(rich) == "Display:\nSynthetic state\n");
        CHECK(read_file(user).find("New leaderboard") != std::string::npos);
    }
    SUBCASE("Discard requests quit without writing files") {
        REQUIRE(ui.click_modal(window, 1));
        CHECK(window.take_quit_request());
        CHECK_FALSE(window.presence_session().dirty());
        CHECK_FALSE(window.leaderboard_session().dirty());
        CHECK_FALSE(std::filesystem::exists(rich));
        CHECK_FALSE(std::filesystem::exists(user));
    }
}

TEST_CASE("Invalid authoring draft disables save and survives game session changes") {
    TemporaryDirectory directory;
    REQUIRE_FALSE(directory.path.empty());
    HeadlessUI ui;
    cw::LogicTester tester;
    cw::AuthoringWindow window(tester);
    cw::BufferMemory memory(std::vector<uint8_t>(64, 0));
    window.rich_presence_open = true;
    window.leaderboards_open = true;
    std::string error;
    REQUIRE(window.presence_session().load(directory.path / "1-Rich.txt", error));
    REQUIRE(window.leaderboard_session().load(directory.path / "1-User.txt", error));
    window.presence_session().set_text("Invalid script\n");
    REQUIRE(window.leaderboard_session().add_leaderboard(error));
    window.sync_live(1, true, memory, 1);
    ui.frame(window);
    window.sync_live(2, true, memory, 2);
    ui.frame(window);
    CHECK(window.presence_session().text() == "Invalid script\n");
    CHECK(window.presence_session().dirty());
    CHECK(window.leaderboard_session().dirty());
    CHECK_FALSE(window.presence_session().validation_error().empty());
    CHECK_FALSE(window.allow_quit());
    REQUIRE(ui.click_modal(window, 0));
    CHECK_FALSE(window.take_quit_request());
    CHECK(window.presence_session().dirty());
    CHECK(window.leaderboard_session().dirty());
    CHECK_FALSE(std::filesystem::exists(directory.path / "1-User.txt"));
    REQUIRE(ui.click_modal(window, 2));
    ui.frame(window);
    CHECK_FALSE(ImGui::FindWindowByName("Unsaved authoring changes")->Active);
    CHECK_FALSE(window.take_quit_request());
}
