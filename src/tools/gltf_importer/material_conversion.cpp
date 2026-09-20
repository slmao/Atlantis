#include "material_conversion.h"

#include <algorithm>
#include <cmath>

namespace atlantis::gltf_importer {

namespace {

// Reference: KhronosGroup/glTF, extensions/2.0/Archived/
// KHR_materials_pbrSpecularGlossiness/examples/convert-between-workflows-bjs/
// js/babylon.pbrUtilities.js, PbrUtilities.ConvertToMetallicRoughness (read at
// commit 11136cfa; linked from the extension README's Appendix). Reproduced
// operation for operation, including its constants. The two BRDF models are
// not equivalent: the result is an approximation with no published error
// bound (KhronosGroup/glTF issue #1903), and the metallic solve uses scalar
// perceived brightness, so a chromatic F0 on a partial metal loses hue.
constexpr double kDielectricSpecular = 0.04;
constexpr double kEpsilon = 1e-6;

// babylon.colorExtensions.js getPerceivedBrightness.
[[nodiscard]] double perceivedBrightness(double r, double g, double b) {
  return std::sqrt(0.299 * r * r + 0.587 * g * g + 0.114 * b * b);
}

// Root of d*m^2 + b*m + c = 0 for the metalness m (babylon's solveMetallic).
[[nodiscard]] double solveMetallic(double diffuse, double specular, double oneMinusSpecularStrength) {
  if (specular < kDielectricSpecular) return 0.0;
  const double a = kDielectricSpecular;
  const double b = diffuse * oneMinusSpecularStrength / (1.0 - kDielectricSpecular) + specular - 2.0 * kDielectricSpecular;
  const double c = kDielectricSpecular - specular;
  const double discriminant = b * b - 4.0 * a * c;
  return std::clamp((-b + std::sqrt(discriminant)) / (2.0 * a), 0.0, 1.0);
}

}  // namespace

MetallicRoughnessFactors convertSpecularGlossiness(const SpecularGlossinessFactors& input) noexcept {
  const double diffuse[3] = {input.diffuse[0], input.diffuse[1], input.diffuse[2]};
  const double specular[3] = {input.specular[0], input.specular[1], input.specular[2]};

  const double oneMinusSpecularStrength = 1.0 - std::max({specular[0], specular[1], specular[2]});
  const double metallic =
      solveMetallic(perceivedBrightness(diffuse[0], diffuse[1], diffuse[2]),
                    perceivedBrightness(specular[0], specular[1], specular[2]), oneMinusSpecularStrength);

  const double diffuseScale =
      oneMinusSpecularStrength / (1.0 - kDielectricSpecular) / std::max(1.0 - metallic, kEpsilon);
  const double specularScale = 1.0 / std::max(metallic, kEpsilon);
  const double blend = metallic * metallic;

  MetallicRoughnessFactors result;
  for (int i = 0; i < 3; ++i) {
    const double fromDiffuse = diffuse[i] * diffuseScale;
    const double fromSpecular = (specular[i] - kDielectricSpecular * (1.0 - metallic)) * specularScale;
    const double lerped = fromDiffuse + (fromSpecular - fromDiffuse) * blend;
    result.baseColor[i] = static_cast<float>(std::clamp(lerped, 0.0, 1.0));
  }
  result.baseColor[3] = input.diffuse[3];
  result.metallic = static_cast<float>(metallic);
  result.roughness = static_cast<float>(std::clamp(1.0 - static_cast<double>(input.glossiness), 0.0, 1.0));
  return result;
}

}  // namespace atlantis::gltf_importer
