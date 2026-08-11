#pragma once

#include <array>

#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>

namespace atlantis::renderer {

// A plain, caller-owned aggregate -- not a scene graph, not registered
// anywhere persistent. mesh/material are borrowed (must outlive the
// Renderer::drawFrame() call they are passed to). objectToWorld is a
// column-major 4x4 float matrix -- Atlantis Core has no public math type
// yet (not part of this spec's scope to add one), so this is a raw,
// fixed-layout array, matching exactly what pushConstant() copies
// verbatim.
struct DrawItem {
  const Mesh* mesh = nullptr;
  const Material* material = nullptr;
  std::array<float, 16> objectToWorld{};
};

}  // namespace atlantis::renderer
