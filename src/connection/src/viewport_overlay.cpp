#include <atlantis/connection/viewport_overlay.h>

#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace atlantis::connection {

namespace {

using json::Value;
using atlantis::asset_system::EntityGuid;

[[nodiscard]] bool finite(float x) noexcept { return std::isfinite(x); }

template <std::size_t N>
[[nodiscard]] bool finite(const std::array<float, N>& v) noexcept {
  for (const float x : v) {
    if (!finite(x)) return false;
  }
  return true;
}

[[nodiscard]] bool wellFormed(const OverlayColor& c) noexcept {
  for (const float x : {c.r, c.g, c.b, c.a}) {
    if (!finite(x) || x < 0.0f || x > 1.0f) return false;
  }
  return true;
}

[[nodiscard]] bool positive(float x) noexcept { return finite(x) && x > 0.0f; }

template <std::size_t N>
[[nodiscard]] Value floats(const std::array<float, N>& v) {
  Value out = Value::array();
  for (const float x : v) out.push(Value::number(x));
  return out;
}

[[nodiscard]] Value colorJson(const OverlayColor& c) { return floats(std::array<float, 4>{c.r, c.g, c.b, c.a}); }

[[nodiscard]] Value depthJson(OverlayDepth depth) { return Value::string(depth == OverlayDepth::OnTop ? "OnTop" : "Tested"); }

[[nodiscard]] const Value* member(const Value& object, std::string_view key) {
  return object.isObject() ? object.find(key) : nullptr;
}

template <std::size_t N>
[[nodiscard]] std::optional<std::array<float, N>> floatsOf(const Value* value) {
  if (value == nullptr || !value->isArray() || value->asArray().size() != N) return std::nullopt;
  std::array<float, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    if (!value->asArray()[i].toFloat(out[i])) return std::nullopt;
  }
  return out;
}

[[nodiscard]] std::optional<float> floatOf(const Value* value) {
  float out = 0.0f;
  if (value == nullptr || !value->toFloat(out)) return std::nullopt;
  return out;
}

[[nodiscard]] std::optional<OverlayColor> colorOf(const Value* value) {
  const auto c = floatsOf<4>(value);
  if (!c) return std::nullopt;
  return OverlayColor{(*c)[0], (*c)[1], (*c)[2], (*c)[3]};
}

[[nodiscard]] std::optional<OverlayDepth> depthOf(const Value* value) {
  if (value == nullptr || !value->isString()) return std::nullopt;
  if (value->asString() == "Tested") return OverlayDepth::Tested;
  if (value->asString() == "OnTop") return OverlayDepth::OnTop;
  return std::nullopt;
}

template <typename T, typename Decode>
[[nodiscard]] bool listOf(const Value* value, std::vector<T>& out, Decode decode) {
  if (value == nullptr || !value->isArray()) return false;
  out.reserve(value->asArray().size());
  for (const Value& item : value->asArray()) {
    auto decoded = decode(item);
    if (!decoded) return false;
    out.push_back(std::move(*decoded));
  }
  return true;
}

}  // namespace

bool overlayWellFormed(const ViewportOverlay& overlay) noexcept {
  for (const EntityGuid& entity : overlay.selection) {
    if (entity == EntityGuid{}) return false;
  }
  for (const OverlayGroup& group : overlay.groups) {
    if (group.anchor && (!finite(group.anchor->position) || !positive(group.anchor->pixelsPerUnit))) return false;
    for (const OverlaySegment& s : group.segments) {
      if (!finite(s.a) || !finite(s.b) || !wellFormed(s.color) || !positive(s.widthPixels)) return false;
    }
    for (const OverlayTriangle& t : group.triangles) {
      if (!finite(t.a) || !finite(t.b) || !finite(t.c) || !wellFormed(t.color)) return false;
    }
    for (const OverlayPoint& p : group.points) {
      if (!finite(p.p) || !wellFormed(p.color) || !positive(p.sizePixels)) return false;
    }
  }
  return true;
}

bool overlayWithinLimits(const ViewportOverlay& overlay) noexcept {
  std::size_t segments = 0;
  std::size_t triangles = 0;
  std::size_t points = 0;
  for (const OverlayGroup& group : overlay.groups) {
    segments += group.segments.size();
    triangles += group.triangles.size();
    points += group.points.size();
  }
  return overlay.groups.size() <= kMaxOverlayGroups && overlay.selection.size() <= kMaxOverlaySelection &&
         segments <= kMaxOverlaySegments && triangles <= kMaxOverlayTriangles && points <= kMaxOverlayPoints;
}

Value overlayToJson(const ViewportOverlay& overlay) {
  Value selection = Value::array();
  for (const EntityGuid& entity : overlay.selection) selection.push(Value::string(atlantis::asset_system::toString(entity)));
  Value groups = Value::array();
  for (const OverlayGroup& group : overlay.groups) {
    Value anchor;
    if (group.anchor) {
      anchor = Value::object();
      anchor.set("position", floats(group.anchor->position));
      anchor.set("pixelsPerUnit", Value::number(group.anchor->pixelsPerUnit));
    }
    Value segments = Value::array();
    for (const OverlaySegment& s : group.segments) {
      Value item = Value::object();
      item.set("a", floats(s.a));
      item.set("b", floats(s.b));
      item.set("color", colorJson(s.color));
      item.set("depth", depthJson(s.depth));
      item.set("width", Value::number(s.widthPixels));
      segments.push(std::move(item));
    }
    Value triangles = Value::array();
    for (const OverlayTriangle& t : group.triangles) {
      Value item = Value::object();
      item.set("a", floats(t.a));
      item.set("b", floats(t.b));
      item.set("c", floats(t.c));
      item.set("color", colorJson(t.color));
      item.set("depth", depthJson(t.depth));
      triangles.push(std::move(item));
    }
    Value points = Value::array();
    for (const OverlayPoint& p : group.points) {
      Value item = Value::object();
      item.set("p", floats(p.p));
      item.set("color", colorJson(p.color));
      item.set("depth", depthJson(p.depth));
      item.set("size", Value::number(p.sizePixels));
      points.push(std::move(item));
    }
    Value g = Value::object();
    g.set("anchor", std::move(anchor));
    g.set("segments", std::move(segments));
    g.set("triangles", std::move(triangles));
    g.set("points", std::move(points));
    groups.push(std::move(g));
  }
  Value out = Value::object();
  out.set("selection", std::move(selection));
  out.set("groups", std::move(groups));
  return out;
}

std::optional<ViewportOverlay> overlayFromJson(const Value& value) {
  ViewportOverlay out;
  const bool selected = listOf(member(value, "selection"), out.selection, [](const Value& item) -> std::optional<EntityGuid> {
    if (!item.isString()) return std::nullopt;
    // The nil GUID's text decodes (as the wire's entity values do); overlayWellFormed() refuses it.
    if (item.asString() == "00000000-0000-0000-0000-000000000000") return EntityGuid{};
    auto parsed = atlantis::asset_system::parseEntityGuid(item.asString());
    if (parsed.isErr()) return std::nullopt;
    return parsed.value();
  });
  if (!selected) return std::nullopt;
  const bool grouped = listOf(member(value, "groups"), out.groups, [](const Value& g) -> std::optional<OverlayGroup> {
    OverlayGroup group;
    const Value* anchor = member(g, "anchor");
    if (anchor == nullptr) return std::nullopt;
    if (!anchor->isNull()) {
      const auto position = floatsOf<3>(member(*anchor, "position"));
      const auto scale = floatOf(member(*anchor, "pixelsPerUnit"));
      if (!position || !scale) return std::nullopt;
      group.anchor = OverlayAnchor{*position, *scale};
    }
    const bool ok =
        listOf(member(g, "segments"), group.segments, [](const Value& s) -> std::optional<OverlaySegment> {
          const auto a = floatsOf<3>(member(s, "a"));
          const auto b = floatsOf<3>(member(s, "b"));
          const auto color = colorOf(member(s, "color"));
          const auto depth = depthOf(member(s, "depth"));
          const auto width = floatOf(member(s, "width"));
          if (!a || !b || !color || !depth || !width) return std::nullopt;
          return OverlaySegment{*a, *b, *color, *depth, *width};
        }) &&
        listOf(member(g, "triangles"), group.triangles, [](const Value& t) -> std::optional<OverlayTriangle> {
          const auto a = floatsOf<3>(member(t, "a"));
          const auto b = floatsOf<3>(member(t, "b"));
          const auto c = floatsOf<3>(member(t, "c"));
          const auto color = colorOf(member(t, "color"));
          const auto depth = depthOf(member(t, "depth"));
          if (!a || !b || !c || !color || !depth) return std::nullopt;
          return OverlayTriangle{*a, *b, *c, *color, *depth};
        }) &&
        listOf(member(g, "points"), group.points, [](const Value& p) -> std::optional<OverlayPoint> {
          const auto at = floatsOf<3>(member(p, "p"));
          const auto color = colorOf(member(p, "color"));
          const auto depth = depthOf(member(p, "depth"));
          const auto size = floatOf(member(p, "size"));
          if (!at || !color || !depth || !size) return std::nullopt;
          return OverlayPoint{*at, *color, *depth, *size};
        });
    if (!ok) return std::nullopt;
    return group;
  });
  if (!grouped) return std::nullopt;
  return out;
}

}  // namespace atlantis::connection
