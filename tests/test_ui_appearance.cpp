#include "ui_appearance.hpp"
#include "ui_scale.hpp"

#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.DisplaySize = ImVec2(1600, 900);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::StyleColorsDark();
    }
    ~ImGuiFixture() { ImGui::DestroyContext(); }
    ImGuiFixture(const ImGuiFixture&) = delete;
    ImGuiFixture& operator=(const ImGuiFixture&) = delete;
};

struct TempDir {
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("cw_ui_settings_" + std::to_string(::getpid()) + "_" + std::to_string(++counter));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    std::filesystem::path path;
    inline static int counter = 0;
};

void write_text(const std::filesystem::path& file, const std::string& text) {
    std::ofstream output(file, std::ios::binary);
    output << text;
}

void run_frame() {
    ImGui::NewFrame();
    ImGui::EndFrame();
}

} // namespace

TEST_CASE("UI settings round trip through their file") {
    TempDir dir;
    const auto file = dir.path / "ui-settings.txt";
    cw::UiSettings settings;
    settings.font_size = 24;
    settings.font_path = "/usr/share/fonts/example with spaces/Example.ttc";
    settings.font_index = 3;

    std::string error;
    REQUIRE(cw::save_ui_settings(file, settings, error));
    CHECK(cw::load_ui_settings(file) == settings);

    int entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
        (void)entry;
        ++entries;
    }
    CHECK(entries == 1); // no temporary file left behind
}

TEST_CASE("Damaged UI settings fall back to the defaults") {
    TempDir dir;
    const auto file = dir.path / "ui-settings.txt";
    const cw::UiSettings defaults;

    CHECK(cw::load_ui_settings({}) == defaults);
    CHECK(cw::load_ui_settings(file) == defaults);

    write_text(file, "not our header\nfont_size\t30\n");
    CHECK(cw::load_ui_settings(file) == defaults);

    write_text(file, "# cheevo-workbench ui settings v1\n"
                     "font_size\tbig\nfont_path\trelative/font.ttf\nfont_index\t-1\n"
                     "future_key\tvalue\nno tab here\n");
    CHECK(cw::load_ui_settings(file) == defaults);
}

TEST_CASE("UI font size is clamped when loading and saving") {
    TempDir dir;
    const auto file = dir.path / "ui-settings.txt";
    write_text(file, "# cheevo-workbench ui settings v1\nfont_size\t999\n");
    CHECK(cw::load_ui_settings(file).font_size == cw::UiSettings::kMaxFontSize);
    write_text(file, "# cheevo-workbench ui settings v1\nfont_size\t1\n");
    CHECK(cw::load_ui_settings(file).font_size == cw::UiSettings::kMinFontSize);

    cw::UiSettings huge;
    huge.font_size = 999;
    std::string error;
    REQUIRE(cw::save_ui_settings(file, huge, error));
    CHECK(cw::load_ui_settings(file).font_size == cw::UiSettings::kMaxFontSize);
}

TEST_CASE("UI settings refuse a font path that would break the file format") {
    TempDir dir;
    const auto file = dir.path / "ui-settings.txt";
    cw::UiSettings settings;
    settings.font_path = "/fonts/line\nbreak.ttf";
    std::string error;
    CHECK_FALSE(cw::save_ui_settings(file, settings, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("UI appearance applies a saved font size and scales measurements") {
    TempDir dir;
    const auto file = dir.path / "ui-settings.txt";
    write_text(file, "# cheevo-workbench ui settings v1\nfont_size\t26\n");
    ImGuiFixture fixture;
    const float base_padding = ImGui::GetStyle().WindowPadding.x;

    cw::UiAppearance appearance(file, 1.5f, false);
    CHECK(ImGui::GetStyle().WindowPadding.x == doctest::Approx(base_padding * 3.0f));
    ImGui::NewFrame();
    CHECK(ImGui::GetFontSize() == doctest::Approx(39.0f));
    CHECK(cw::ui_px(100.0f) == doctest::Approx(300.0f));
    ImGui::EndFrame();
}

TEST_CASE("UI appearance applies changes only between frames and saves them") {
    TempDir dir;
    const auto file = dir.path / "ui-settings.txt";
    ImGuiFixture fixture;
    cw::UiAppearance appearance(file, 1.0f, false);
    const ImVec4 text_color = ImGui::GetStyle().Colors[ImGuiCol_Text];

    ImGui::NewFrame();
    CHECK(ImGui::GetFontSize() == doctest::Approx(13.0f));
    cw::UiSettings bigger = appearance.settings();
    bigger.font_size = 20;
    appearance.change(bigger);
    CHECK(ImGui::GetFontSize() == doctest::Approx(13.0f));
    CHECK_FALSE(std::filesystem::exists(file));
    ImGui::EndFrame();

    appearance.begin_frame();
    ImGui::NewFrame();
    CHECK(ImGui::GetFontSize() == doctest::Approx(20.0f));
    const ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyphNoFallback('A');
    REQUIRE(glyph != nullptr);
    CHECK(glyph->Visible);
    ImGui::EndFrame();
    CHECK(cw::load_ui_settings(file).font_size == 20);

    // Repeated rescaling starts from the base style, so measurements never compound.
    const float padding_at_20 = ImGui::GetStyle().WindowPadding.x;
    bigger.font_size = 13;
    appearance.change(bigger);
    appearance.begin_frame();
    run_frame();
    bigger.font_size = 20;
    appearance.change(bigger);
    appearance.begin_frame();
    run_frame();
    CHECK(ImGui::GetStyle().WindowPadding.x == doctest::Approx(padding_at_20));
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_Text].x == doctest::Approx(text_color.x));
}

TEST_CASE("UI appearance survives an unusable font file") {
    TempDir dir;
    const auto garbage = dir.path / "garbage.ttf";
    const auto damaged = dir.path / "damaged.ttf";
    write_text(garbage, std::string(4096, 'x'));
    // A valid signature with no tables behind it passes the first check and fails inside ImGui.
    write_text(damaged, std::string("\0\1\0\0", 4) + std::string(4096, '\0'));
    const auto missing = dir.path / "missing.ttf";

    for (const auto& path : {garbage, damaged, missing}) {
        CAPTURE(path.string());
        const auto file = dir.path / "ui-settings.txt";
        cw::UiSettings settings;
        settings.font_path = path;
        std::string error;
        REQUIRE(cw::save_ui_settings(file, settings, error));

        ImGuiFixture fixture;
        cw::UiAppearance appearance(file, 1.0f, false);
        CHECK_FALSE(appearance.message().empty());
        ImGui::NewFrame();
        const ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyphNoFallback('A');
        REQUIRE(glyph != nullptr);
        CHECK(glyph->Visible);
        ImGui::EndFrame();
    }
}

TEST_CASE("System fonts are listed sorted and at least one can be loaded") {
    const auto fonts = cw::list_system_fonts();
    for (size_t i = 1; i < fonts.size(); ++i)
        CHECK(fonts[i - 1].label != fonts[i].label);
    if (fonts.empty())
        return; // fontconfig is optional

    TempDir dir;
    const auto file = dir.path / "ui-settings.txt";
    cw::UiSettings settings;
    settings.font_path = fonts.front().path;
    settings.font_index = fonts.front().index;
    std::string error;
    REQUIRE(cw::save_ui_settings(file, settings, error));
    ImGuiFixture fixture;
    cw::UiAppearance appearance(file, 1.0f, false);
    CHECK(appearance.settings().font_path == fonts.front().path);
    run_frame();
}

TEST_CASE("Reset layout undocks windows and keeps them inside the viewport") {
    ImGuiFixture fixture;
    ImGuiID dockspace = 0;
    const auto draw_windows = [&](bool dock_tool) {
        ImGui::NewFrame();
        dockspace =
            ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (dock_tool)
            ImGui::SetNextWindowDockID(ImGui::DockBuilderGetCentralNode(dockspace)->ID);
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("Status");
        ImGui::End();
        ImGui::SetNextWindowPos(ImVec2(5000, 5000), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(4000, 3000), ImGuiCond_FirstUseEver);
        ImGui::Begin("Lost");
        ImGui::End();
        ImGui::EndFrame();
    };

    draw_windows(true);
    draw_windows(false);
    ImGuiWindow* status = ImGui::FindWindowByName("Status");
    ImGuiWindow* lost = ImGui::FindWindowByName("Lost");
    REQUIRE(status != nullptr);
    REQUIRE(lost != nullptr);
    CHECK(status->DockId != 0);

    cw::reset_window_layout();
    draw_windows(false);
    draw_windows(false);

    CHECK(status->DockId == 0);
    CHECK_FALSE(status->DockIsActive);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    for (const ImGuiWindow* window : {status, lost}) {
        CAPTURE(window->Name);
        CHECK(window->Pos.x >= 0.0f);
        CHECK(window->Pos.y >= 0.0f);
        CHECK(window->Pos.x + window->Size.x <= display.x);
        CHECK(window->Pos.y + window->Size.y <= display.y);
    }
    // The dock host is not a tool window: resetting must leave it covering the viewport.
    CHECK(ImGui::DockBuilderGetCentralNode(dockspace) != nullptr);
}

TEST_CASE("Tables freeze their leading columns only when enough width remains") {
    CHECK(cw::frozen_table_columns(2, 200.0f, 1000.0f) == 2);
    CHECK(cw::frozen_table_columns(2, 400.0f, 1000.0f) == 2); // exactly the allowed share
    CHECK(cw::frozen_table_columns(2, 401.0f, 1000.0f) == 0);
    CHECK(cw::frozen_table_columns(2, 370.0f, 600.0f) == 0); // large font in a narrow window
}
