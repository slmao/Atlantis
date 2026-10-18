// Plan 0061 M2 (Spec 0061 R2/T3, ADR-0121 D1-D2): the float-identity argument
// for Stage 1's point-light range rejection, made mechanical.
//
// Two test-local transcriptions of the four looping PBR shaders' point-light
// loop (shaders/pbr_ibl/pbr_ibl.slang and its three siblings; the shader's own
// operation order and grouping) -- "before" (the pre-change loop, frozen here)
// and "after" (Spec 0061 R3's shape) -- run over a deterministic corpus and
// must agree bit for bit. A frozen copy of computePbrDirectLighting()'s
// pre-change point loop is the reference for the updated twin. Nothing here
// calls or shares code with a shader: this proves R2 (a)-(d) under the C++
// implementation's IEEE semantics only; the goldens and the full-frame capture
// check the GPU compiler (Plan 0061 P5, P7, P8).

#include <atlantis/asset_system/scene_source.h>
#include <atlantis/runtime/scene_extraction.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::runtime::computePbrDirectLighting;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::kMaxPointLights;
using atlantis::runtime::kMinAlpha;
using atlantis::runtime::kMinDot;
using atlantis::runtime::kPi;
using atlantis::runtime::kPointLightDistanceEpsilon;
using atlantis::runtime::Vec3;

namespace {

// Frozen copies of scene_extraction.cpp's file-local vector helpers, so the
// transcriptions below do not move if that file's helpers ever do.
float length3(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalizeBy(const Vec3& v, float len) { return {v.x / len, v.y / len, v.z / len}; }
Vec3 add3(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub3(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale3(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
Vec3 mul3(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
Vec3 lerp3(const Vec3& a, const Vec3& b, float t) { return add3(scale3(a, 1.0f - t), scale3(b, t)); }

bool sameBits(const Vec3& a, const Vec3& b) {
  return std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) &&
         std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y) &&
         std::bit_cast<std::uint32_t>(a.z) == std::bit_cast<std::uint32_t>(b.z);
}

bool isPositiveZero(float v) { return std::bit_cast<std::uint32_t>(v) == 0U; }
bool hasNegativeZero(const Vec3& v) {
  constexpr std::uint32_t kNegativeZero = 0x80000000U;
  return std::bit_cast<std::uint32_t>(v.x) == kNegativeZero || std::bit_cast<std::uint32_t>(v.y) == kNegativeZero ||
         std::bit_cast<std::uint32_t>(v.z) == kNegativeZero;
}

struct Fragment {
  Vec3 position;
  Vec3 normal;
  Vec3 camera;
  Vec3 baseColor;
  float metallic = 0.0f;
  float roughness = 0.0f;
  std::size_t lighting = 0;  // index into Corpus::lightings
};

struct Corpus {
  std::vector<FrameLightingData> lightings;
  std::vector<Fragment> fragments;
};

// --- The shader transcription -------------------------------------------------

struct Surface {
  Vec3 N, V, F0, diffuseColor;
  float metallic, roughness, alpha;
};

Surface surfaceOf(const Fragment& f) {
  Surface s{};
  s.metallic = std::clamp(f.metallic, 0.0f, 1.0f);
  s.roughness = std::clamp(f.roughness, 0.0f, 1.0f);
  s.alpha = std::max(s.roughness * s.roughness, kMinAlpha);
  s.F0 = lerp3(Vec3{0.04f, 0.04f, 0.04f}, f.baseColor, s.metallic);
  s.diffuseColor = scale3(f.baseColor, 1.0f - s.metallic);
  s.N = normalizeBy(f.normal, length3(f.normal));
  const Vec3 toCamera = sub3(f.camera, f.position);
  s.V = normalizeBy(toCamera, length3(toCamera));
  return s;
}

// The point-light branch body, unchanged by Stage 1 (Spec 0061 R2 (c)):
// `(kD * diffuseColor / kPi + specular) * radiance * NdotL`, in the shader's order.
Vec3 shaderBranchTerm(const Surface& s, const Vec3& L, float NdotL, const Vec3& radiance) {
  const float NdotV = std::max(dot3(s.N, s.V), kMinDot);
  const Vec3 HUnnormalized = add3(L, s.V);
  const Vec3 H = normalizeBy(HUnnormalized, length3(HUnnormalized));
  const float NdotH = std::max(dot3(s.N, H), 0.0f);
  const float VdotH = std::max(dot3(s.V, H), 0.0f);
  const float D = (s.alpha * s.alpha) /
                  (kPi * std::pow(NdotH * NdotH * (s.alpha * s.alpha - 1.0f) + 1.0f, 2.0f));
  const float k = (s.roughness + 1.0f) * (s.roughness + 1.0f) / 8.0f;
  const float G1V = NdotV / (NdotV * (1.0f - k) + k);
  const float G1L = NdotL / (NdotL * (1.0f - k) + k);
  const float G = G1V * G1L;
  const Vec3 F = add3(s.F0, scale3(sub3(Vec3{1.0f, 1.0f, 1.0f}, s.F0), std::pow(1.0f - VdotH, 5.0f)));
  const float specularDenominator = std::max(4.0f * NdotV * NdotL, kMinDot);
  const Vec3 specular = {(D * G * F.x) / specularDenominator, (D * G * F.y) / specularDenominator,
                         (D * G * F.z) / specularDenominator};
  const Vec3 kD = scale3(sub3(Vec3{1.0f, 1.0f, 1.0f}, F), 1.0f - s.metallic);
  const Vec3 diffuse = scale3(mul3(kD, s.diffuseColor), 1.0f / kPi);
  return scale3(mul3(add3(diffuse, specular), radiance), NdotL);
}

enum class Rejection {
  None,             // before: the pre-change loop
  Stage1,           // after: NdotL > 0 && atten > 0 on the same atten value
  NonConservative,  // the self-check: also rejects at dist >= 0.99 * range
};

struct LoopResult {
  Vec3 accumulated{0.0f, 0.0f, 0.0f};
  std::uint32_t evaluated = 0;
  std::uint32_t zeroAttenuationEvaluated = 0;  // before only: NdotL > 0, atten == 0
  bool everySkippableTermPositiveZero = true;  // before only
  bool accumulatorEverNegativeZero = false;    // before only
};

LoopResult shaderPointLoop(const Fragment& f, const FrameLightingData& lighting, Rejection rejection) {
  const Surface s = surfaceOf(f);
  LoopResult r;
  for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
    const FrameLightingData::PointLightGpu& pl = lighting.pointLights[j];
    const Vec3 toLight{pl.position[0] - f.position.x, pl.position[1] - f.position.y, pl.position[2] - f.position.z};
    const float dist = std::max(length3(toLight), kPointLightDistanceEpsilon);
    const Vec3 L{toLight.x / dist, toLight.y / dist, toLight.z / dist};
    const float NdotL = std::max(dot3(s.N, L), 0.0f);
    const float atten = std::clamp(1.0f - dist / pl.range, 0.0f, 1.0f);
    bool evaluate = NdotL > 0.0f;
    if (rejection == Rejection::Stage1) evaluate = evaluate && atten > 0.0f;
    if (rejection == Rejection::NonConservative) evaluate = evaluate && atten > 0.0f && dist < 0.99f * pl.range;
    if (!evaluate) continue;
    const Vec3 radiance{pl.color[0] * pl.intensity * atten, pl.color[1] * pl.intensity * atten,
                        pl.color[2] * pl.intensity * atten};
    const Vec3 term = shaderBranchTerm(s, L, NdotL, radiance);
    if (rejection == Rejection::None && !(atten > 0.0f)) {
      ++r.zeroAttenuationEvaluated;
      if (!isPositiveZero(term.x) || !isPositiveZero(term.y) || !isPositiveZero(term.z)) {
        r.everySkippableTermPositiveZero = false;
      }
    }
    r.accumulated = add3(r.accumulated, term);
    if (rejection == Rejection::None && hasNegativeZero(r.accumulated)) r.accumulatorEverNegativeZero = true;
    ++r.evaluated;
  }
  return r;
}

// computePbrDirectLighting()'s point loop as it was before Stage 1, frozen
// (src/runtime/src/scene_extraction.cpp at 48099b8): its shared lambda's own
// `NdotL <= 0` return was the only guard. The directional loop is omitted --
// Stage 1 leaves it untouched, and the corpus's point lights are the subject.
Vec3 frozenTwinPointLoop(const Fragment& f, const FrameLightingData& lighting) {
  const float metallic = std::clamp(f.metallic, 0.0f, 1.0f);
  const float roughness = std::clamp(f.roughness, 0.0f, 1.0f);
  const float alpha = std::max(roughness * roughness, kMinAlpha);
  const Vec3 F0 = lerp3(Vec3{0.04f, 0.04f, 0.04f}, f.baseColor, metallic);
  const Vec3 diffuseColor = scale3(f.baseColor, 1.0f - metallic);
  const Vec3 N = normalizeBy(f.normal, length3(f.normal));
  const Vec3 toCamera = sub3(f.camera, f.position);
  const Vec3 V = normalizeBy(toCamera, length3(toCamera));
  Vec3 accumulated{0.0f, 0.0f, 0.0f};

  const auto accumulateLight = [&](const Vec3& L, const Vec3& radiance) {
    const float NdotL = std::max(dot3(N, L), 0.0f);
    if (NdotL <= 0.0f) return;
    const float NdotV = std::max(dot3(N, V), kMinDot);
    const Vec3 H = normalizeBy(add3(L, V), length3(add3(L, V)));
    const float NdotH = std::max(dot3(N, H), 0.0f);
    const float VdotH = std::max(dot3(V, H), 0.0f);
    const float D = (alpha * alpha) / (kPi * std::pow(NdotH * NdotH * (alpha * alpha - 1.0f) + 1.0f, 2.0f));
    const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    const float G1V = NdotV / (NdotV * (1.0f - k) + k);
    const float G1L = NdotL / (NdotL * (1.0f - k) + k);
    const float G = G1V * G1L;
    const Vec3 F = add3(F0, scale3(sub3(Vec3{1.0f, 1.0f, 1.0f}, F0), std::pow(1.0f - VdotH, 5.0f)));
    const float specularScalar = (D * G) / std::max(4.0f * NdotV * NdotL, kMinDot);
    const Vec3 specular = scale3(F, specularScalar);
    const Vec3 kD = scale3(sub3(Vec3{1.0f, 1.0f, 1.0f}, F), 1.0f - metallic);
    const Vec3 diffuseTerm = scale3(mul3(kD, diffuseColor), 1.0f / kPi);
    const Vec3 lightContribution = scale3(mul3(add3(diffuseTerm, specular), radiance), NdotL);
    accumulated = add3(accumulated, lightContribution);
  };

  for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
    const FrameLightingData::PointLightGpu& pl = lighting.pointLights[j];
    const Vec3 toLight{pl.position[0] - f.position.x, pl.position[1] - f.position.y, pl.position[2] - f.position.z};
    const float dist = std::max(length3(toLight), kPointLightDistanceEpsilon);
    const Vec3 L = normalizeBy(toLight, dist);
    const float atten = std::clamp(1.0f - dist / pl.range, 0.0f, 1.0f);
    const Vec3 radiance = scale3(Vec3{pl.color[0], pl.color[1], pl.color[2]}, pl.intensity * atten);
    accumulateLight(L, radiance);
  }
  return accumulated;
}

// --- The corpus -----------------------------------------------------------------

FrameLightingData bistroLighting() {
  std::ifstream in(ATLANTIS_BISTRO_OVERLAY_SCENE_PATH, std::ios::binary);
  REQUIRE(in.good());
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  const auto parsed = atlantis::asset_system::parseSceneSource(text);
  REQUIRE(parsed.isOk());

  FrameLightingData lighting{};
  for (const auto& node : parsed.value().nodes) {
    if (!node.light.has_value() || node.light->kind != atlantis::asset_system::DecodedLightKind::Point) continue;
    REQUIRE(lighting.pointLightCount < kMaxPointLights);
    REQUIRE(!node.parentNodeId.has_value());  // overlay lights are roots: world position = local position
    auto& pl = lighting.pointLights[lighting.pointLightCount++];
    pl.position[0] = node.transform.positionX;
    pl.position[1] = node.transform.positionY;
    pl.position[2] = node.transform.positionZ;
    pl.range = node.light->range;
    pl.color[0] = node.light->colorR;
    pl.color[1] = node.light->colorG;
    pl.color[2] = node.light->colorB;
    pl.intensity = node.light->intensity;
  }
  REQUIRE(lighting.pointLightCount == 59);
  return lighting;
}

constexpr std::array<std::array<float, 2>, 6> kMaterials{{{0.0f, 0.0f}, {0.0f, 0.5f}, {0.0f, 1.0f},
                                                         {1.0f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}}};

Vec3 unitVector(std::mt19937& rng) {
  std::normal_distribution<float> n(0.0f, 1.0f);
  for (;;) {
    const Vec3 v{n(rng), n(rng), n(rng)};
    const float len = length3(v);
    if (len > 1e-3f) return normalizeBy(v, len);
  }
}

const Corpus& corpus() {
  static const Corpus built = [] {
    Corpus c;
    std::mt19937 rng(0x0061u);  // fixed seed: the corpus is deterministic
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    c.lightings.push_back(bistroLighting());
    const FrameLightingData& bistro = c.lightings[0];
    const Vec3 benchmarkCamera{-22.0f, 1.7f, 22.0f};

    // Bistro planes: y in {0, 1.5, 3} over the lights' footprint +- 10 m, 2 m grid.
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (std::uint32_t j = 0; j < bistro.pointLightCount; ++j) {
      minX = std::min(minX, bistro.pointLights[j].position[0]);
      maxX = std::max(maxX, bistro.pointLights[j].position[0]);
      minZ = std::min(minZ, bistro.pointLights[j].position[2]);
      maxZ = std::max(maxZ, bistro.pointLights[j].position[2]);
    }
    const std::array<Vec3, 5> axisNormals{{{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    std::size_t cycle = 0;
    for (const float y : {0.0f, 1.5f, 3.0f}) {
      for (float x = minX - 10.0f; x <= maxX + 10.0f; x += 2.0f) {
        for (float z = minZ - 10.0f; z <= maxZ + 10.0f; z += 2.0f) {
          for (std::size_t n = 0; n <= axisNormals.size(); ++n, ++cycle) {
            const auto& m = kMaterials[cycle % kMaterials.size()];
            c.fragments.push_back({{x, y, z}, n < axisNormals.size() ? axisNormals[n] : unitVector(rng),
                                   benchmarkCamera, {unit(rng), unit(rng), unit(rng)}, m[0], m[1], 0});
          }
        }
      }
    }

    // Boundaries: per Bistro light, fragments at distance range, range +- 1 and 2 ULP, along several
    // directions; one at the light's own position (the kPointLightDistanceEpsilon path); N perpendicular
    // to L; L == V; grazing NdotL.
    const std::array<Vec3, 8> directions{{{1, 0, 0}, {0, -1, 0}, {0, 0, 1}, {-0.6f, -0.8f, 0},
                                           {0.48f, -0.6f, 0.64f}, {-0.36f, 0.48f, -0.8f}, {0, 1, 0}, {-1, 0, 0}}};
    for (std::uint32_t j = 0; j < bistro.pointLightCount; ++j) {
      const auto& pl = bistro.pointLights[j];
      const Vec3 lightPos{pl.position[0], pl.position[1], pl.position[2]};
      for (const Vec3& d : directions) {
        float distances[5] = {pl.range, 0, 0, 0, 0};
        distances[1] = std::nextafter(pl.range, 0.0f);
        distances[2] = std::nextafter(distances[1], 0.0f);
        distances[3] = std::nextafter(pl.range, 1e9f);
        distances[4] = std::nextafter(distances[3], 1e9f);
        for (const float t : distances) {
          const Vec3 p = sub3(lightPos, scale3(d, t));  // the light sits at p + d*t
          const auto& m = kMaterials[cycle++ % kMaterials.size()];
          c.fragments.push_back({p, d, benchmarkCamera, {0.8f, 0.6f, 0.4f}, m[0], m[1], 0});
          const Vec3 grazing = normalizeBy(add3(d, Vec3{0.0f, 0.0f, 50.0f}), length3(add3(d, Vec3{0, 0, 50.0f})));
          c.fragments.push_back({p, grazing, benchmarkCamera, {0.8f, 0.6f, 0.4f}, m[0], m[1], 0});
          c.fragments.push_back({p, d, lightPos, {0.5f, 0.5f, 0.5f}, m[0], m[1], 0});  // L == V
        }
        // N perpendicular to L.
        const Vec3 perpendicular = std::abs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        c.fragments.push_back({sub3(lightPos, scale3(d, pl.range * 0.5f)), perpendicular, benchmarkCamera,
                               {0.3f, 0.3f, 0.3f}, 0.0f, 0.5f, 0});
      }
      c.fragments.push_back({lightPos, {0, 1, 0}, benchmarkCamera, {1, 1, 1}, 0.0f, 0.5f, 0});
    }

    // Random lights (the grammar's domains) and fragments.
    FrameLightingData random{};
    random.pointLightCount = kMaxPointLights;
    for (std::uint32_t j = 0; j < kMaxPointLights; ++j) {
      auto& pl = random.pointLights[j];
      for (float& v : pl.position) v = -20.0f + 40.0f * unit(rng);
      pl.range = 0.1f + 9.9f * unit(rng);
      for (float& v : pl.color) v = unit(rng);
      pl.intensity = 20.0f * unit(rng);
    }
    c.lightings.push_back(random);
    for (int i = 0; i < 60000; ++i) {
      const Vec3 p{-20.0f + 40.0f * unit(rng), -20.0f + 40.0f * unit(rng), -20.0f + 40.0f * unit(rng)};
      const Vec3 cam = add3(p, scale3(unitVector(rng), 1.0f + 30.0f * unit(rng)));
      c.fragments.push_back({p, unitVector(rng), cam, {unit(rng), unit(rng), unit(rng)}, unit(rng), unit(rng), 1});
    }
    return c;
  }();
  return built;
}

}  // namespace

TEST_CASE("Stage 1 point-light range rejection is bitwise-identical to the pre-change loop",
          "[runtime][point-light-rejection]") {
  const Corpus& c = corpus();
  REQUIRE(c.fragments.size() >= 100000);

  std::size_t mismatches = 0;
  std::size_t firstMismatch = 0;
  std::uint64_t skipped = 0;
  std::uint64_t evaluatedInRange = 0;
  for (std::size_t i = 0; i < c.fragments.size(); ++i) {
    const Fragment& f = c.fragments[i];
    const LoopResult before = shaderPointLoop(f, c.lightings[f.lighting], Rejection::None);
    const LoopResult after = shaderPointLoop(f, c.lightings[f.lighting], Rejection::Stage1);
    if (!sameBits(before.accumulated, after.accumulated) && mismatches++ == 0) firstMismatch = i;
    skipped += before.evaluated - after.evaluated;
    evaluatedInRange += after.evaluated;
  }
  INFO("first mismatching fragment index " << firstMismatch << " of " << c.fragments.size());
  REQUIRE(mismatches == 0);
  // The corpus must actually exercise both outcomes of the predicate.
  REQUIRE(skipped > 100000);
  REQUIRE(evaluatedInRange > 10000);
}

TEST_CASE("Every point-light term Stage 1 skips is exactly +0, and the accumulator is never -0",
          "[runtime][point-light-rejection]") {
  const Corpus& c = corpus();
  std::uint64_t zeroAttenuationTerms = 0;
  std::size_t nonZeroTerms = 0;
  std::size_t negativeZeroAccumulators = 0;
  for (const Fragment& f : c.fragments) {
    const LoopResult before = shaderPointLoop(f, c.lightings[f.lighting], Rejection::None);
    zeroAttenuationTerms += before.zeroAttenuationEvaluated;
    if (!before.everySkippableTermPositiveZero) ++nonZeroTerms;
    if (before.accumulatorEverNegativeZero) ++negativeZeroAccumulators;
  }
  REQUIRE(zeroAttenuationTerms > 100000);
  REQUIRE(nonZeroTerms == 0);
  REQUIRE(negativeZeroAccumulators == 0);
}

TEST_CASE("A non-conservative rejection is caught by the same corpus (the test can fail)",
          "[runtime][point-light-rejection]") {
  const Corpus& c = corpus();
  std::size_t mismatches = 0;
  for (const Fragment& f : c.fragments) {
    const LoopResult before = shaderPointLoop(f, c.lightings[f.lighting], Rejection::None);
    const LoopResult wrong = shaderPointLoop(f, c.lightings[f.lighting], Rejection::NonConservative);
    if (!sameBits(before.accumulated, wrong.accumulated)) ++mismatches;
  }
  REQUIRE(mismatches > 0);
}

TEST_CASE("computePbrDirectLighting() matches its frozen pre-change point loop bitwise",
          "[runtime][point-light-rejection]") {
  const Corpus& c = corpus();
  std::size_t mismatches = 0;
  std::size_t firstMismatch = 0;
  for (std::size_t i = 0; i < c.fragments.size(); ++i) {
    const Fragment& f = c.fragments[i];
    FrameLightingData pointOnly = c.lightings[f.lighting];
    pointOnly.directionalLightCount = 0;
    const Vec3 twin =
        computePbrDirectLighting(f.position, f.normal, f.camera, f.baseColor, f.metallic, f.roughness, pointOnly);
    if (!sameBits(twin, frozenTwinPointLoop(f, pointOnly)) && mismatches++ == 0) firstMismatch = i;
  }
  INFO("first mismatching fragment index " << firstMismatch << " of " << c.fragments.size());
  REQUIRE(mismatches == 0);
}
