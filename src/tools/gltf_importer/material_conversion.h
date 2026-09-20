#pragma once

#include <array>

namespace atlantis::gltf_importer {

// ADR-0083 D3 / Plan 0037 "D3 -- specular-glossiness conversion": the
// KHR_materials_pbrSpecularGlossiness factor -> metallic-roughness
// conversion, applied once at import time to materials WITHOUT a
// specularGlossinessTexture (Plan 0037 Ruling 2 routes textured ones to a
// dielectric fallback instead). All inputs and outputs are linear-space
// factors in [0, 1]. Pure and thread-safe.
struct SpecularGlossinessFactors {
  std::array<float, 4> diffuse{1.0f, 1.0f, 1.0f, 1.0f};  // RGBA; alpha is opacity
  std::array<float, 3> specular{1.0f, 1.0f, 1.0f};
  float glossiness = 1.0f;
};

struct MetallicRoughnessFactors {
  std::array<float, 4> baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  float metallic = 1.0f;
  float roughness = 1.0f;
};

[[nodiscard]] MetallicRoughnessFactors convertSpecularGlossiness(const SpecularGlossinessFactors& input) noexcept;

}  // namespace atlantis::gltf_importer
