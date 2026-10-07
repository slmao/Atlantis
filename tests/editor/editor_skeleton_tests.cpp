#include "editor_fixture.h"

#include <atlantis/editor/editor.h>

#include <imgui.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

// Plan 0056 M1 (P3, J8): the editor builds its font atlas once, through the
// UI library's core alone, runs UI frames headless, and keeps its UI context
// to itself -- one context per Editor, none left installed between calls.

using atlantis::editor::Editor;
using atlantis::editor::FrameContext;
using atlantis::editor::InputEvent;
using atlantis::editor::UiTexture;
using atlantis::editor::test::EditorFixture;

TEST_CASE("the editor builds an RGBA8 font atlas once, with no dynamic font textures", "[editor][m1]") {
  EditorFixture fixture;
  const auto atlas = fixture.editor->fontAtlas();
  REQUIRE(atlas.width > 0);
  REQUIRE(atlas.height > 0);
  CHECK(atlas.rgba.size() == static_cast<std::size_t>(atlas.width) * atlas.height * 4);
  // Glyph pixels exist: some texel is opaque.
  bool anyOpaque = false;
  for (std::size_t i = 3; i < atlas.rgba.size(); i += 4) anyOpaque = anyOpaque || atlas.rgba[i] == 0xFF;
  CHECK(anyOpaque);
  // Running frames does not change it (it is uploaded once).
  fixture.editor->frame({}, FrameContext{1280, 720});
  fixture.editor->frame({}, FrameContext{1280, 720});
  const auto again = fixture.editor->fontAtlas();
  CHECK(again.width == atlas.width);
  CHECK(again.height == atlas.height);
  CHECK(again.rgba == atlas.rgba);
}

TEST_CASE("each Editor owns its own UI context and leaves none installed", "[editor][m1]") {
  REQUIRE(ImGui::GetCurrentContext() == nullptr);
  EditorFixture first;
  EditorFixture second;
  CHECK(ImGui::GetCurrentContext() == nullptr);
  for (int i = 0; i < 3; ++i) {
    first.editor->frame({}, FrameContext{1280, 720});
    CHECK(ImGui::GetCurrentContext() == nullptr);
    second.editor->frame({}, FrameContext{640, 480});
    CHECK(ImGui::GetCurrentContext() == nullptr);
  }
  CHECK(first.editor->drawList().displayWidth == 1280);
  CHECK(second.editor->drawList().displayWidth == 640);
  CHECK_FALSE(first.editor->drawList().commands.empty());
  CHECK_FALSE(second.editor->drawList().commands.empty());
}

TEST_CASE("the fixed layout reports a Viewport size and samples the Viewport texture", "[editor][m1]") {
  EditorFixture fixture;
  CHECK(fixture.editor->viewportSize().width == 0);
  fixture.editor->frame({}, FrameContext{1280, 720});
  fixture.editor->frame({}, FrameContext{1280, 720});
  const auto size = fixture.editor->viewportSize();
  CHECK(size.width > 0);
  CHECK(size.height > 0);
  CHECK(size.width < 1280);
  CHECK(size.height < 720);
  bool viewport = false;
  bool font = false;
  for (const auto& command : fixture.editor->drawList().commands) {
    viewport = viewport || command.texture == UiTexture::Viewport;
    font = font || command.texture == UiTexture::FontAtlas;
  }
  CHECK(viewport);
  CHECK(font);
}
