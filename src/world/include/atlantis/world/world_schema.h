#pragma once

#include <atlantis/schema.h>

#include <span>

namespace atlantis::world {

// Plan 0048 P1/P8 (Spec 0048 R5/R6, ADR-0099 D2): descriptors of World's
// component data -- Transform, CameraFog, CameraBloom, Camera, Light,
// LightKind, Renderable and (Plan 0051 P1) WorldMatrix, in that order (a
// stable listing order, not identity). Immutable static data; safe for concurrent reads.
[[nodiscard]] std::span<const schema::TypeDescriptor> worldSchema() noexcept;

}  // namespace atlantis::world
