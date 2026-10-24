#include <atlantis/connection/viewport_overlay.h>

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <string>

// Plan 0066 M2 (Spec 0066 R1; ADR-0132 D1; J6): the Viewport overlay value --
// what is well formed, the per-client limits, and the JSON form
// (remote_protocol.md section 6.6) that the wire, the CLI and the C# SDK share.

namespace {

namespace connection = atlantis::connection;
namespace json = atlantis::connection::json;
using connection::OverlayDepth;

[[nodiscard]] connection::ViewportOverlay sample() {
  connection::ViewportOverlay overlay;
  overlay.selection = {atlantis::asset_system::parseEntityGuid("52052052-0001-4052-8052-000000000001").value()};
  connection::OverlayGroup world;
  world.segments.push_back({{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, -2.25f}, {1.0f, 0.25f, 0.0f, 1.0f}, OverlayDepth::Tested, 2.0f});
  world.triangles.push_back(
      {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.2f, 0.4f, 0.6f, 0.75f}, OverlayDepth::OnTop});
  world.points.push_back({{0.1f, 0.2f, 0.3f}, {1.0f, 1.0f, 1.0f, 1.0f}, OverlayDepth::OnTop, 6.5f});
  connection::OverlayGroup gizmo;
  gizmo.anchor = connection::OverlayAnchor{{1.5f, -2.0f, 4.0f}, 96.0f};
  gizmo.segments.push_back({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, OverlayDepth::OnTop, 3.0f});
  overlay.groups = {world, gizmo};
  return overlay;
}

[[nodiscard]] std::optional<connection::ViewportOverlay> fromText(const std::string& text) {
  const auto parsed = json::parse(text);
  REQUIRE(parsed.isOk());
  return connection::overlayFromJson(parsed.value());
}

}  // namespace

TEST_CASE("viewport overlay: the JSON form round-trips, member for member", "[connection][overlay]") {
  const connection::ViewportOverlay overlay = sample();
  const json::Value value = connection::overlayToJson(overlay);
  CHECK(json::write(value) ==
        R"({"selection":["52052052-0001-4052-8052-000000000001"],"groups":[{"anchor":null,)"
        R"("segments":[{"a":[0,0,0],"b":[1,0.5,-2.25],"color":[1,0.25,0,1],"depth":"Tested","width":2}],)"
        R"("triangles":[{"a":[0,0,0],"b":[1,0,0],"c":[0,1,0],"color":[0.2,0.4,0.6,0.75],"depth":"OnTop"}],)"
        R"("points":[{"p":[0.1,0.2,0.3],"color":[1,1,1,1],"depth":"OnTop","size":6.5}]},)"
        R"({"anchor":{"position":[1.5,-2,4],"pixelsPerUnit":96},)"
        R"("segments":[{"a":[0,0,0],"b":[1,0,0],"color":[1,0,0,1],"depth":"OnTop","width":3}],)"
        R"("triangles":[],"points":[]}]})");
  const auto back = connection::overlayFromJson(value);
  REQUIRE(back.has_value());
  CHECK(*back == overlay);
  CHECK(connection::overlayFromJson(connection::overlayToJson(connection::ViewportOverlay{})) ==
        std::optional<connection::ViewportOverlay>{connection::ViewportOverlay{}});
}

TEST_CASE("viewport overlay: a malformed JSON form is refused", "[connection][overlay]") {
  const std::string group = R"({"anchor":null,"segments":[],"triangles":[],"points":[]})";
  CHECK(fromText(R"({"selection":[],"groups":[)" + group + "]}").has_value());
  CHECK_FALSE(fromText(R"({"selection":[]})"));
  CHECK_FALSE(fromText(R"({"groups":[]})"));
  CHECK_FALSE(fromText("[]"));
  CHECK_FALSE(fromText(R"({"selection":[7],"groups":[]})"));
  CHECK_FALSE(fromText(R"({"selection":["not-a-guid"],"groups":[]})"));
  // The nil GUID decodes, as the wire's entity values do -- and is not well formed.
  const auto nil = fromText(R"({"selection":["00000000-0000-0000-0000-000000000000"],"groups":[]})");
  REQUIRE(nil.has_value());
  CHECK_FALSE(connection::overlayWellFormed(*nil));
  CHECK_FALSE(fromText(R"({"selection":[],"groups":[{"segments":[],"triangles":[],"points":[]}]})"));
  CHECK_FALSE(fromText(R"({"selection":[],"groups":[{"anchor":{"position":[0,0]},"segments":[],"triangles":[],)"
                       R"("points":[]}]})"));
  CHECK_FALSE(fromText(R"({"selection":[],"groups":[{"anchor":null,"segments":[{"a":[0,0,0],"b":[1,0,0],)"
                       R"("color":[1,1,1,1],"depth":"Behind","width":1}],"triangles":[],"points":[]}]})"));
  CHECK_FALSE(fromText(R"({"selection":[],"groups":[{"anchor":null,"segments":[],"triangles":[],)"
                       R"("points":[{"p":[0,0,0],"color":[1,1,1],"depth":"OnTop","size":4}]}]})"));
  CHECK_FALSE(fromText(R"({"selection":[],"groups":[{"anchor":null,"segments":[],"triangles":[],)"
                       R"("points":[{"p":[0,0,1e39],"color":[1,1,1,1],"depth":"OnTop","size":4}]}]})"));
}

TEST_CASE("viewport overlay: well formed -- finite, colours in [0, 1], positive sizes, no nil entity",
          "[connection][overlay]") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  CHECK(connection::overlayWellFormed(connection::ViewportOverlay{}));
  CHECK(connection::overlayWellFormed(sample()));
  const auto refuses = [](auto change) {
    connection::ViewportOverlay overlay = sample();
    change(overlay);
    return !connection::overlayWellFormed(overlay);
  };
  CHECK(refuses([](auto& o) { o.selection.push_back(atlantis::asset_system::EntityGuid{}); }));
  CHECK(refuses([&](auto& o) { o.groups[0].segments[0].a[1] = nan; }));
  CHECK(refuses([&](auto& o) { o.groups[0].triangles[0].c[2] = inf; }));
  CHECK(refuses([](auto& o) { o.groups[0].segments[0].color.r = 1.5f; }));
  CHECK(refuses([](auto& o) { o.groups[0].triangles[0].color.a = -0.25f; }));
  CHECK(refuses([](auto& o) { o.groups[0].segments[0].widthPixels = 0.0f; }));
  CHECK(refuses([](auto& o) { o.groups[0].points[0].sizePixels = -1.0f; }));
  CHECK(refuses([&](auto& o) { o.groups[0].points[0].p[0] = nan; }));
  CHECK(refuses([](auto& o) { o.groups[1].anchor->pixelsPerUnit = 0.0f; }));
  CHECK(refuses([&](auto& o) { o.groups[1].anchor->position[0] = inf; }));
  CHECK_FALSE(refuses([](auto& o) { o.groups[0].segments[0].color = {0.0f, 0.0f, 0.0f, 0.0f}; }));  // the bounds are in
}

TEST_CASE("viewport overlay: the per-client limits (J6), totals across groups", "[connection][overlay]") {
  connection::ViewportOverlay overlay;
  overlay.groups.resize(connection::kMaxOverlayGroups);
  CHECK(connection::overlayWithinLimits(overlay));
  overlay.groups.emplace_back();
  CHECK_FALSE(connection::overlayWithinLimits(overlay));

  overlay = {};
  overlay.selection.resize(connection::kMaxOverlaySelection);
  CHECK(connection::overlayWithinLimits(overlay));
  overlay.selection.emplace_back();
  CHECK_FALSE(connection::overlayWithinLimits(overlay));

  overlay = {};
  overlay.groups.resize(2);
  overlay.groups[0].segments.resize(connection::kMaxOverlaySegments / 2);
  overlay.groups[1].segments.resize(connection::kMaxOverlaySegments / 2);
  overlay.groups[0].triangles.resize(connection::kMaxOverlayTriangles);
  overlay.groups[1].points.resize(connection::kMaxOverlayPoints);
  CHECK(connection::overlayWithinLimits(overlay));
  overlay.groups[1].segments.emplace_back();
  CHECK_FALSE(connection::overlayWithinLimits(overlay));
  overlay.groups[1].segments.pop_back();
  overlay.groups[1].triangles.emplace_back();
  CHECK_FALSE(connection::overlayWithinLimits(overlay));
  overlay.groups[1].triangles.clear();
  overlay.groups[0].points.emplace_back();
  CHECK_FALSE(connection::overlayWithinLimits(overlay));
}
