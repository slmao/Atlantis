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

// --- Stage 2: the clustered association (Plan 0061 Stage 2 M2; Spec 0061 Revision 1 S2-R2/S2-T2; ADR-0122) ---
//
// Three more test-local transcriptions: (a) the shader's cluster assignment from worldPosition (Plan P3), (b) a
// brute-force reference builder (every light whose inflated sphere meets a cluster's corner AABB), and (c) the
// mask-walking loop with Stage 1's body. Revision 1's three conditions are encoded below, each with a deliberately
// wrong variant the test must catch: (1) the margin, (2) the mapping, (3) the index order.

namespace {

using atlantis::runtime::CameraMatrices;
using atlantis::runtime::lookAtMatrix;
using atlantis::runtime::Mat4;
using atlantis::runtime::perspectiveMatrix;

// Plan P2's grid, test-local; M3 pins scene_extraction.h's constants to these.
constexpr std::uint32_t kTestGridX = 12;
constexpr std::uint32_t kTestGridY = 7;
constexpr std::uint32_t kTestGridZ = 16;
constexpr std::uint32_t kTestClusterCount = kTestGridX * kTestGridY * kTestGridZ;
constexpr float kTestNearSliceFarZ = 3.0f;  // z1

struct TestClusterFrame {
  CameraMatrices camera;
  Vec3 eye, right, up, back;
  float sx = 0.0f, sy = 0.0f, zNear = 0.0f, zFar = 0.0f, logScale = 0.0f;
  std::array<float, kTestGridZ + 1> sliceBound{};
};

// The header and slice bounds as Plan P2/P4 define them, from the camera matrices alone.
TestClusterFrame testClusterFrame(const CameraMatrices& camera) {
  TestClusterFrame f;
  f.camera = camera;
  const Mat4& v = camera.view;
  const Mat4& p = camera.projection;
  f.right = {v[0], v[4], v[8]};
  f.up = {v[1], v[5], v[9]};
  f.back = {v[2], v[6], v[10]};
  f.eye = {-(v[12] * f.right.x + v[13] * f.up.x + v[14] * f.back.x),
           -(v[12] * f.right.y + v[13] * f.up.y + v[14] * f.back.y),
           -(v[12] * f.right.z + v[13] * f.up.z + v[14] * f.back.z)};
  f.sx = p[0];
  f.sy = p[5];
  f.zNear = p[14] / p[10];
  f.zFar = p[14] / (p[10] + 1.0f);
  f.logScale = static_cast<float>(kTestGridZ - 1) / std::log(f.zFar / kTestNearSliceFarZ);
  f.sliceBound[0] = std::min(f.zNear, kTestNearSliceFarZ) * (1.0f - 1e-3f);
  for (std::uint32_t k = 1; k < kTestGridZ; ++k) {
    f.sliceBound[k] = kTestNearSliceFarZ * std::exp(static_cast<float>(k - 1) / f.logScale);
  }
  f.sliceBound[kTestGridZ] = std::max(f.zFar, kTestNearSliceFarZ) * (1.0f + 1e-3f);
  return f;
}

std::array<float, 4> transform(const Mat4& m, float x, float y, float z, float w) {  // column-major
  return {m[0] * x + m[4] * y + m[8] * z + m[12] * w, m[1] * x + m[5] * y + m[9] * z + m[13] * w,
          m[2] * x + m[6] * y + m[10] * z + m[14] * w, m[3] * x + m[7] * y + m[11] * z + m[15] * w};
}

// (a) Plan P3, transcribed: the cluster of a world position, from the frame's view and projection. xShift > 0 is
// the deliberately wrong mapping of Condition 2's self-check.
std::uint32_t shaderClusterIndex(const TestClusterFrame& f, const Vec3& p, int xShift = 0) {
  const auto view = transform(f.camera.view, p.x, p.y, p.z, 1.0f);
  const auto clip = transform(f.camera.projection, view[0], view[1], view[2], view[3]);
  const float ndcX = clip[0] / clip[3];
  const float ndcY = clip[1] / clip[3];
  const int gx = static_cast<int>(kTestGridX), gy = static_cast<int>(kTestGridY), gz = static_cast<int>(kTestGridZ);
  int x = std::clamp(static_cast<int>(std::floor((ndcX * 0.5f + 0.5f) * static_cast<float>(gx))), 0, gx - 1);
  x = std::clamp(x + xShift, 0, gx - 1);
  const int y = std::clamp(static_cast<int>(std::floor((ndcY * 0.5f + 0.5f) * static_cast<float>(gy))), 0, gy - 1);
  const float viewZ = -view[2];
  int z = 0;
  if (viewZ >= kTestNearSliceFarZ) {
    z = std::clamp(1 + static_cast<int>(std::floor(std::log(viewZ / kTestNearSliceFarZ) * f.logScale)), 1, gz - 1);
  }
  return static_cast<std::uint32_t>(x + gx * (y + gy * z));
}

Vec3 clusterPoint(const TestClusterFrame& f, float ndcX, float ndcY, float depth) {
  const float vx = ndcX * depth / f.sx;
  const float vy = ndcY * depth / f.sy;
  return {f.eye.x + vx * f.right.x + vy * f.up.x - depth * f.back.x,
          f.eye.y + vx * f.right.y + vy * f.up.y - depth * f.back.y,
          f.eye.z + vx * f.right.z + vy * f.up.z - depth * f.back.z};
}

// (b) The brute-force reference builder: every light whose sphere meets a cluster's corner AABB. withMargin false
// with radiusScale < 1 is Condition 1's deliberately wrong (under-sized) variant.
std::vector<std::uint64_t> oracleMasks(const TestClusterFrame& f, const FrameLightingData& lighting,
                                       bool withMargin = true, float radiusScale = 1.0f) {
  std::vector<std::uint64_t> masks(kTestClusterCount, 0);
  for (std::uint32_t z = 0; z < kTestGridZ; ++z) {
    for (std::uint32_t y = 0; y < kTestGridY; ++y) {
      for (std::uint32_t x = 0; x < kTestGridX; ++x) {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (std::uint32_t c = 0; c < 8; ++c) {
          const Vec3 q = clusterPoint(f, -1.0f + 2.0f * static_cast<float>(x + (c & 1u)) / kTestGridX,
                                      -1.0f + 2.0f * static_cast<float>(y + ((c >> 1) & 1u)) / kTestGridY,
                                      f.sliceBound[z + ((c >> 2) & 1u)]);
          const float qv[3] = {q.x, q.y, q.z};
          for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], qv[i]);
            hi[i] = std::max(hi[i], qv[i]);
          }
        }
        const std::uint32_t cluster = x + kTestGridX * (y + kTestGridY * z);
        for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
          const auto& pl = lighting.pointLights[j];
          const float radius = withMargin ? pl.range * (1.0f + 1e-3f) + 0.01f : pl.range * radiusScale;
          float d2 = 0.0f;
          for (int i = 0; i < 3; ++i) {
            const float e = std::max({lo[i] - pl.position[i], 0.0f, pl.position[i] - hi[i]});
            d2 += e * e;
          }
          if (d2 <= radius * radius) masks[cluster] |= std::uint64_t{1} << j;
        }
      }
    }
  }
  return masks;
}

std::uint64_t allLightsMask(const FrameLightingData& lighting) {
  return lighting.pointLightCount >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << lighting.pointLightCount) - 1;
}

// (c) The mask-walking loop: the set bits low to high (descending is Condition 3's deliberately wrong order), each
// light through Stage 1's body exactly as shaderPointLoop() transcribes it.
Vec3 shaderMaskWalk(const Fragment& f, const FrameLightingData& lighting, std::uint64_t mask, bool descending = false) {
  const Surface s = surfaceOf(f);
  Vec3 accumulated{0.0f, 0.0f, 0.0f};
  for (int step = 0; step < 64; ++step) {
    const std::uint32_t j = static_cast<std::uint32_t>(descending ? 63 - step : step);
    if (((mask >> j) & 1u) == 0 || j >= lighting.pointLightCount) continue;
    const FrameLightingData::PointLightGpu& pl = lighting.pointLights[j];
    const Vec3 toLight{pl.position[0] - f.position.x, pl.position[1] - f.position.y, pl.position[2] - f.position.z};
    const float dist = std::max(length3(toLight), kPointLightDistanceEpsilon);
    const Vec3 L{toLight.x / dist, toLight.y / dist, toLight.z / dist};
    const float NdotL = std::max(dot3(s.N, L), 0.0f);
    const float atten = std::clamp(1.0f - dist / pl.range, 0.0f, 1.0f);
    if (!(NdotL > 0.0f && atten > 0.0f)) continue;
    const Vec3 radiance{pl.color[0] * pl.intensity * atten, pl.color[1] * pl.intensity * atten,
                        pl.color[2] * pl.intensity * atten};
    accumulated = add3(accumulated, shaderBranchTerm(s, L, NdotL, radiance));
  }
  return accumulated;
}

// Conservativeness at one fragment: every light with transcribed atten > 0 is set in the mask.
bool everyReachingLightInMask(const Fragment& f, const FrameLightingData& lighting, std::uint64_t mask) {
  for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
    const auto& pl = lighting.pointLights[j];
    const Vec3 toLight{pl.position[0] - f.position.x, pl.position[1] - f.position.y, pl.position[2] - f.position.z};
    const float dist = std::max(length3(toLight), kPointLightDistanceEpsilon);
    const float atten = std::clamp(1.0f - dist / pl.range, 0.0f, 1.0f);
    if (atten > 0.0f && ((mask >> j) & 1u) == 0) return false;
  }
  return true;
}

struct ClusteredCase {
  Fragment fragment;   // fragment.lighting indexes ClusteredCorpus::lightings
  std::size_t frame;   // index into ClusteredCorpus::frames
};

struct ClusteredCorpus {
  std::vector<FrameLightingData> lightings;
  std::vector<TestClusterFrame> frames;
  std::vector<ClusteredCase> cases;
};

const ClusteredCorpus& clusteredCorpus() {
  static const ClusteredCorpus built = [] {
    ClusteredCorpus c;
    std::mt19937 rng(0x0062u);  // fixed seed: deterministic
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    c.lightings.push_back(corpus().lightings[0]);  // Bistro's 59 overlay lights
    FrameLightingData dense{};                     // 64 random lights across the same footprint, overlapping
    dense.pointLightCount = kMaxPointLights;
    for (std::uint32_t j = 0; j < kMaxPointLights; ++j) {
      auto& pl = dense.pointLights[j];
      pl.position[0] = -45.0f + 90.0f * unit(rng);
      pl.position[1] = 8.0f * unit(rng);
      pl.position[2] = -35.0f + 85.0f * unit(rng);
      pl.range = 1.0f + 9.0f * unit(rng);
      for (float& v : pl.color) v = unit(rng);
      pl.intensity = 20.0f * unit(rng);
    }
    c.lightings.push_back(dense);

    // The benchmark camera and three seeded random cameras over the footprint.
    const float fov = 1.0472f, aspect = 16.0f / 9.0f, nearZ = 0.1f, farZ = 200.0f;
    std::vector<std::array<float, 6>> poses = {{-22.0f, 1.7f, 22.0f, 0.0f, -0.86f, 0.0f}};
    for (int i = 0; i < 3; ++i) {
      poses.push_back({-35.0f + 60.0f * unit(rng), 1.5f + 4.5f * unit(rng), -25.0f + 60.0f * unit(rng),
                       -0.3f + 0.6f * unit(rng), -3.14159f + 6.28318f * unit(rng), 0.0f});
    }
    for (const auto& pose : poses) {
      const float pitch = pose[3], yaw = pose[4];
      const Vec3 forward{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
      CameraMatrices camera;
      camera.view = lookAtMatrix(pose[0], pose[1], pose[2], pose[0] + forward.x, pose[1] + forward.y,
                                 pose[2] + forward.z);
      camera.projection = perspectiveMatrix(fov, aspect, nearZ, farZ);
      c.frames.push_back(testClusterFrame(camera));
    }

    // Fragments inside every cluster of every frame, for both light sets: interiors, and points on cluster faces,
    // edges and corners at +-1 and +-2 ULP in ndc and on slice boundaries.
    const auto nudged = [&](float v) {
      const int k = static_cast<int>(unit(rng) * 5.0f);  // 0, +-1, +-2 ULP
      float r = v;
      for (int i = 0; i < (k + 1) / 2; ++i) r = std::nextafter(r, k % 2 ? 1e9f : -1e9f);
      return r;
    };
    std::size_t cycle = 0;
    for (std::size_t frame = 0; frame < c.frames.size(); ++frame) {
      const TestClusterFrame& f = c.frames[frame];
      for (std::size_t lights = 0; lights < c.lightings.size(); ++lights) {
        for (std::uint32_t z = 0; z < kTestGridZ; ++z) {
          const float d0 = std::max(f.sliceBound[z], f.zNear), d1 = std::min(f.sliceBound[z + 1], f.zFar);
          for (std::uint32_t y = 0; y < kTestGridY; ++y) {
            for (std::uint32_t x = 0; x < kTestGridX; ++x) {
              for (int sample = 0; sample < 4; ++sample) {
                float u = unit(rng), v = unit(rng), w = unit(rng);
                const bool boundary = sample >= 2;
                if (boundary) {  // snap one to three coordinates onto the cluster's faces
                  if (unit(rng) < 0.6f) u = unit(rng) < 0.5f ? 0.0f : 1.0f;
                  if (unit(rng) < 0.6f) v = unit(rng) < 0.5f ? 0.0f : 1.0f;
                  if (unit(rng) < 0.6f) w = unit(rng) < 0.5f ? 0.0f : 1.0f;
                }
                float ndcX = -1.0f + 2.0f * (static_cast<float>(x) + u) / kTestGridX;
                float ndcY = -1.0f + 2.0f * (static_cast<float>(y) + v) / kTestGridY;
                float depth = d0 + (d1 - d0) * w;
                if (boundary) {
                  ndcX = nudged(ndcX);
                  ndcY = nudged(ndcY);
                  depth = nudged(depth);
                }
                if (depth < f.zNear || depth > f.zFar || std::abs(ndcX) > 1.0f || std::abs(ndcY) > 1.0f) continue;
                const auto& m = kMaterials[cycle++ % kMaterials.size()];
                Fragment fr{clusterPoint(f, ndcX, ndcY, depth), unitVector(rng), f.eye,
                            {unit(rng), unit(rng), unit(rng)}, m[0], m[1], lights};
                c.cases.push_back({fr, frame});
              }
            }
          }
        }
        // Near-range fragments for Condition 1's self-check: inside the frustum, 0.92 * range from a light.
        const FrameLightingData& lighting = c.lightings[lights];
        for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
          const auto& pl = lighting.pointLights[j];
          for (int k = 0; k < 24; ++k) {
            const Vec3 dir = unitVector(rng);
            const Vec3 p{pl.position[0] + dir.x * 0.92f * pl.range, pl.position[1] + dir.y * 0.92f * pl.range,
                         pl.position[2] + dir.z * 0.92f * pl.range};
            const auto view = transform(f.camera.view, p.x, p.y, p.z, 1.0f);
            const auto clip = transform(f.camera.projection, view[0], view[1], view[2], view[3]);
            const float depth = -view[2];
            if (depth < f.zNear || depth > f.zFar || std::abs(clip[0] / clip[3]) > 1.0f ||
                std::abs(clip[1] / clip[3]) > 1.0f) {
              continue;
            }
            const auto& m = kMaterials[cycle++ % kMaterials.size()];
            c.cases.push_back({Fragment{p, unitVector(rng), f.eye, {0.7f, 0.6f, 0.5f}, m[0], m[1], lights}, frame});
          }
        }
      }
    }
    return c;
  }();
  return built;
}

// The oracle masks per (frame, light set), built once.
const std::vector<std::vector<std::uint64_t>>& corpusOracleMasks() {
  static const std::vector<std::vector<std::uint64_t>> masks = [] {
    const ClusteredCorpus& c = clusteredCorpus();
    std::vector<std::vector<std::uint64_t>> out;
    for (const auto& frame : c.frames) {
      for (const auto& lighting : c.lightings) out.push_back(oracleMasks(frame, lighting));
    }
    return out;
  }();
  return masks;
}

const std::vector<std::uint64_t>& oracleFor(const ClusteredCase& k) {
  return corpusOracleMasks()[k.frame * clusteredCorpus().lightings.size() + k.fragment.lighting];
}

}  // namespace

TEST_CASE("Clustered association, Condition 1 (margin): every light a fragment reaches is in its cluster's mask, "
          "and an under-sized radius is caught",
          "[runtime][point-light-rejection][clustered-lights]") {
  const ClusteredCorpus& c = clusteredCorpus();
  REQUIRE(c.cases.size() >= 40000);
  std::size_t misses = 0;
  std::uint64_t reachingChecks = 0;
  for (const ClusteredCase& k : c.cases) {
    const FrameLightingData& lighting = c.lightings[k.fragment.lighting];
    const std::uint64_t mask = oracleFor(k)[shaderClusterIndex(c.frames[k.frame], k.fragment.position)];
    if (!everyReachingLightInMask(k.fragment, lighting, mask)) ++misses;
    for (std::uint32_t j = 0; j < lighting.pointLightCount; ++j) {
      const auto& pl = lighting.pointLights[j];
      const Vec3 t{pl.position[0] - k.fragment.position.x, pl.position[1] - k.fragment.position.y,
                   pl.position[2] - k.fragment.position.z};
      if (std::clamp(1.0f - std::max(length3(t), kPointLightDistanceEpsilon) / pl.range, 0.0f, 1.0f) > 0.0f) {
        ++reachingChecks;
      }
    }
  }
  REQUIRE(reachingChecks > 20000);
  REQUIRE(misses == 0);

  // The deliberately wrong variant: no margin and radii at 0.9 * range must leave some reaching light out.
  std::size_t wrongMisses = 0;
  for (std::size_t frame = 0; frame < c.frames.size(); ++frame) {
    for (std::size_t lights = 0; lights < c.lightings.size(); ++lights) {
      const auto shrunk = oracleMasks(c.frames[frame], c.lightings[lights], false, 0.9f);
      for (const ClusteredCase& k : c.cases) {
        if (k.frame != frame || k.fragment.lighting != lights) continue;
        if (!everyReachingLightInMask(k.fragment, c.lightings[lights],
                                      shrunk[shaderClusterIndex(c.frames[frame], k.fragment.position)])) {
          ++wrongMisses;
        }
      }
    }
  }
  REQUIRE(wrongMisses > 0);
}

TEST_CASE("Clustered association, Condition 2 (mapping): fragments on cluster faces and corners, re-projected "
          "through the shader's assignment, keep every reaching light; a shifted assignment is caught",
          "[runtime][point-light-rejection][clustered-lights]") {
  const ClusteredCorpus& c = clusteredCorpus();
  std::size_t misses = 0;
  std::size_t shiftedMisses = 0;
  for (const ClusteredCase& k : c.cases) {
    const TestClusterFrame& f = c.frames[k.frame];
    const FrameLightingData& lighting = c.lightings[k.fragment.lighting];
    const auto& masks = oracleFor(k);
    if (!everyReachingLightInMask(k.fragment, lighting, masks[shaderClusterIndex(f, k.fragment.position)])) ++misses;
    if (!everyReachingLightInMask(k.fragment, lighting, masks[shaderClusterIndex(f, k.fragment.position, 1)])) {
      ++shiftedMisses;
    }
  }
  REQUIRE(misses == 0);
  REQUIRE(shiftedMisses > 0);
}

TEST_CASE("Clustered association, Condition 3 (order): the ascending mask walk equals Stage 1's loop bit for bit, "
          "with the reference masks and with the all-lights mask; a descending walk is caught",
          "[runtime][point-light-rejection][clustered-lights]") {
  const ClusteredCorpus& c = clusteredCorpus();
  std::size_t mismatches = 0, allLightsMismatches = 0, descendingMismatches = 0;
  std::size_t firstMismatch = 0;
  for (std::size_t i = 0; i < c.cases.size(); ++i) {
    const ClusteredCase& k = c.cases[i];
    const FrameLightingData& lighting = c.lightings[k.fragment.lighting];
    const std::uint64_t mask = oracleFor(k)[shaderClusterIndex(c.frames[k.frame], k.fragment.position)];
    const Vec3 stage1 = shaderPointLoop(k.fragment, lighting, Rejection::Stage1).accumulated;
    if (!sameBits(shaderMaskWalk(k.fragment, lighting, mask), stage1) && mismatches++ == 0) firstMismatch = i;
    if (!sameBits(shaderMaskWalk(k.fragment, lighting, allLightsMask(lighting)), stage1)) ++allLightsMismatches;
    if (!sameBits(shaderMaskWalk(k.fragment, lighting, allLightsMask(lighting), true), stage1)) ++descendingMismatches;
  }
  INFO("first mismatching case " << firstMismatch << " of " << c.cases.size());
  REQUIRE(mismatches == 0);
  REQUIRE(allLightsMismatches == 0);
  REQUIRE(descendingMismatches > 0);
}

// Plan 0061 Stage 2 M3 (Plan P7, as ruled at the M1 checkpoint): the real builder against the conservativeness
// property itself -- its masks are not compared bit for bit with the corner-AABB oracle, since the per-light
// half-space test it uses (rev 3) is neither a subset nor a superset of the AABB test.
static_assert(atlantis::runtime::kClusterGridX == kTestGridX && atlantis::runtime::kClusterGridY == kTestGridY &&
              atlantis::runtime::kClusterGridZ == kTestGridZ);
static_assert(atlantis::runtime::kClusterNearSliceFarZ == kTestNearSliceFarZ);

TEST_CASE("buildClusterLightData(): its header matches the transcription, and its masks keep every reaching light; "
          "a builder fed shrunk ranges is caught",
          "[runtime][point-light-rejection][clustered-lights]") {
  const ClusteredCorpus& c = clusteredCorpus();
  const auto bits = [](float v) { return std::bit_cast<std::uint32_t>(v); };
  std::vector<std::vector<std::uint64_t>> real;
  std::vector<std::vector<std::uint64_t>> shrunk;
  for (const TestClusterFrame& f : c.frames) {
    for (const FrameLightingData& lighting : c.lightings) {
      const atlantis::runtime::ClusterLightData data = atlantis::runtime::buildClusterLightData(lighting, f.camera);
      REQUIRE(data.grid[0] == kTestGridX);
      REQUIRE(data.grid[1] == kTestGridY);
      REQUIRE(data.grid[2] == kTestGridZ);
      REQUIRE(bits(data.slicing[0]) == bits(f.zNear));
      REQUIRE(bits(data.slicing[1]) == bits(kTestNearSliceFarZ));
      REQUIRE(bits(data.slicing[2]) == bits(f.zFar));
      REQUIRE(bits(data.slicing[3]) == bits(f.logScale));
      std::vector<std::uint64_t> masks(kTestClusterCount);
      for (std::uint32_t cl = 0; cl < kTestClusterCount; ++cl) {
        masks[cl] = data.masks[2 * cl] | (std::uint64_t{data.masks[2 * cl + 1]} << 32);
        REQUIRE((masks[cl] & ~allLightsMask(lighting)) == 0);  // no bit at or past pointLightCount
      }
      real.push_back(std::move(masks));

      FrameLightingData smaller = lighting;  // the deliberately wrong input: every range at 0.9x
      for (std::uint32_t j = 0; j < smaller.pointLightCount; ++j) smaller.pointLights[j].range *= 0.9f;
      const atlantis::runtime::ClusterLightData wrong = atlantis::runtime::buildClusterLightData(smaller, f.camera);
      std::vector<std::uint64_t> wrongMasks(kTestClusterCount);
      for (std::uint32_t cl = 0; cl < kTestClusterCount; ++cl) {
        wrongMasks[cl] = wrong.masks[2 * cl] | (std::uint64_t{wrong.masks[2 * cl + 1]} << 32);
      }
      shrunk.push_back(std::move(wrongMasks));
    }
  }
  std::size_t misses = 0, wrongMisses = 0;
  for (const ClusteredCase& k : c.cases) {
    const std::size_t set = k.frame * c.lightings.size() + k.fragment.lighting;
    const std::uint32_t cluster = shaderClusterIndex(c.frames[k.frame], k.fragment.position);
    const FrameLightingData& lighting = c.lightings[k.fragment.lighting];
    if (!everyReachingLightInMask(k.fragment, lighting, real[set][cluster])) ++misses;
    if (!everyReachingLightInMask(k.fragment, lighting, shrunk[set][cluster])) ++wrongMisses;
  }
  REQUIRE(misses == 0);
  REQUIRE(wrongMisses > 0);
}
