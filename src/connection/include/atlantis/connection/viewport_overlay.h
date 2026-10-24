#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/json.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Spec 0066 R1 / ADR-0132 D1 (Plan 0066 P1): a client's Viewport overlay -- a
// selection (outlined by the Runtime from the entities' meshes) and groups of
// primitives from a closed set, drawn by the Runtime after the scene. A group
// is world (its points are world positions) or screen-constant (an anchor: its
// points are offsets from the anchor, one unit drawn as `pixelsPerUnit` pixels
// at the anchor's view depth, from the frame's own camera). Plain values, with
// the JSON form atlantis.remote/1 carries them in (remote_protocol.md section
// 6.6), which the CLI's `runtime overlay set` reads too. No Runtime type.
namespace atlantis::connection {

// Straight alpha, authored in sRGB, each channel in [0, 1].
struct OverlayColor {
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
  friend bool operator==(const OverlayColor&, const OverlayColor&) = default;
};

// Tested against the scene's depth, or drawn on top of it.
enum class OverlayDepth : std::uint8_t { Tested, OnTop };

struct OverlaySegment {
  std::array<float, 3> a{};
  std::array<float, 3> b{};
  OverlayColor color;
  OverlayDepth depth = OverlayDepth::Tested;
  float widthPixels = 1.0f;
  friend bool operator==(const OverlaySegment&, const OverlaySegment&) = default;
};

struct OverlayTriangle {
  std::array<float, 3> a{};
  std::array<float, 3> b{};
  std::array<float, 3> c{};
  OverlayColor color;
  OverlayDepth depth = OverlayDepth::Tested;
  friend bool operator==(const OverlayTriangle&, const OverlayTriangle&) = default;
};

struct OverlayPoint {
  std::array<float, 3> p{};
  OverlayColor color;
  OverlayDepth depth = OverlayDepth::Tested;
  float sizePixels = 4.0f;
  friend bool operator==(const OverlayPoint&, const OverlayPoint&) = default;
};

// A screen-constant group's anchor (Spec 0066 Q2, S-a).
struct OverlayAnchor {
  std::array<float, 3> position{};
  float pixelsPerUnit = 1.0f;
  friend bool operator==(const OverlayAnchor&, const OverlayAnchor&) = default;
};

struct OverlayGroup {
  std::optional<OverlayAnchor> anchor;  // none: a world group
  std::vector<OverlaySegment> segments;
  std::vector<OverlayTriangle> triangles;
  std::vector<OverlayPoint> points;
  friend bool operator==(const OverlayGroup&, const OverlayGroup&) = default;
};

struct ViewportOverlay {
  std::vector<atlantis::asset_system::EntityGuid> selection;
  std::vector<OverlayGroup> groups;
  friend bool operator==(const ViewportOverlay&, const ViewportOverlay&) = default;
};

// Plan 0066 J6: one client's limits (primitives counted across its groups).
inline constexpr std::size_t kMaxOverlayGroups = 256;
inline constexpr std::size_t kMaxOverlaySegments = 65536;
inline constexpr std::size_t kMaxOverlayTriangles = 65536;
inline constexpr std::size_t kMaxOverlayPoints = 16384;
inline constexpr std::size_t kMaxOverlaySelection = 256;

// Every float finite, every colour channel in [0, 1], every width, size and
// pixel scale positive, and no nil GUID selected.
[[nodiscard]] bool overlayWellFormed(const ViewportOverlay& overlay) noexcept;
// Within one client's limits.
[[nodiscard]] bool overlayWithinLimits(const ViewportOverlay& overlay) noexcept;

// The JSON form: {"selection":["<guid>"...],"groups":[{"anchor":null|{"position":[x,y,z],"pixelsPerUnit":n},
// "segments":[{"a":[3],"b":[3],"color":[4],"depth":"Tested"|"OnTop","width":n}...],
// "triangles":[{"a","b","c","color","depth"}...],"points":[{"p","color","depth","size"}...]}...]}.
// Decoding refuses anything malformed (a missing member, a wrong kind, a non-number float); it does not check
// overlayWellFormed().
[[nodiscard]] json::Value overlayToJson(const ViewportOverlay& overlay);
[[nodiscard]] std::optional<ViewportOverlay> overlayFromJson(const json::Value& value);

}  // namespace atlantis::connection
