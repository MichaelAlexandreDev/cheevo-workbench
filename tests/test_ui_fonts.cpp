#include "ui_fonts.hpp"

#include <doctest/doctest.h>
#include <imgui.h>

namespace {

void check_fonts(bool discover_system_font) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    const auto path = cw::configure_ui_fonts(*io.Fonts, discover_system_font);
    if (!discover_system_font)
        CHECK(path.empty());
    ImGui::NewFrame();
    ImFontBaked* baked = ImGui::GetFontBaked();
    const ImFontGlyph* latin = baked->FindGlyphNoFallback('A');
    REQUIRE(latin != nullptr);
    CHECK(latin->SourceIdx == 0);
    CHECK(latin->Visible);
    if (!path.empty()) {
        for (ImWchar character : {ImWchar(0x3042), ImWchar(0x30a2), ImWchar(0x65e5)}) {
            const ImFontGlyph* glyph = baked->FindGlyphNoFallback(character);
            REQUIRE(glyph != nullptr);
            CHECK(glyph->SourceIdx == 1);
            CHECK(glyph->Visible);
            CHECK(glyph->X1 > glyph->X0);
            CHECK(glyph->Y1 > glyph->Y0);
        }
    }
    ImGui::EndFrame();
    ImGui::DestroyContext();
}

} // namespace

TEST_CASE("UI fonts retain the default when no system font is available") {
    check_fonts(false);
}

TEST_CASE("UI fonts rasterize Japanese glyphs when a system font is available") {
    check_fonts(true);
}
