#pragma once

#include <atlantis/world/ecs/component.h>

#include <string_view>
#include <tuple>

#include <atlantis/world/camera.h>
#include <atlantis/world/light.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world_matrix.h>

namespace atlantis::world::ecs {

// Plan 0050 P2 (Spec 0050 R3): World's component types as ECS
// components, each named by its worldSchema() descriptor's qualified name.
// CameraFog, CameraBloom and LightKind are fields of components, not
// components, and are not mapped. Compile-time declarations only; no shared
// state.
template <>
struct ComponentType<Transform> {
  static constexpr std::string_view kName = "world::Transform";
};

template <>
struct ComponentType<Camera> {
  static constexpr std::string_view kName = "world::Camera";
};

template <>
struct ComponentType<Light> {
  static constexpr std::string_view kName = "world::Light";
};

template <>
struct ComponentType<Renderable> {
  static constexpr std::string_view kName = "world::Renderable";
};

// Plan 0051 P1: the bake-resolved world matrix (Spec 0051 R4).
template <>
struct ComponentType<WorldMatrix> {
  static constexpr std::string_view kName = "world::WorldMatrix";
};

// Plan 0050 J4: every World type mapped above, for the schema-table check. A
// new mapping in this module joins this list.
using WorldComponentTypes = std::tuple<Transform, Camera, Light, Renderable, WorldMatrix>;

}  // namespace atlantis::world::ecs
