#include <atlantis/asset_system/scene_semantic_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_system_schema.h>
#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/scene_types.h>

// Plan 0049 M5 / P9 (Spec 0049 R4, R5, R8; rulings J1-J4): the unchanged
// scene source and artifact codecs held to sceneSchema(). The registries
// below are driven by the schema: a schema field, domain or constraint
// without a case fails the completeness tests.

namespace {

namespace fs = std::filesystem;
namespace as = atlantis::asset_system;
namespace scene = atlantis::asset_system::scene;
using atlantis::schema::FieldId;

// ---------------------------------------------------------------- helpers

[[nodiscard]] as::EntityGuid entity(std::uint8_t n) {
  as::GuidBytes bytes{};
  bytes[0] = std::byte{0xe0};
  bytes[15] = std::byte{n};
  return as::entityGuidFromBytes(bytes).value();
}

[[nodiscard]] as::AssetGuid asset(std::uint8_t n) {
  as::GuidBytes bytes{};
  bytes[0] = std::byte{0xa0};
  bytes[15] = std::byte{n};
  return as::assetGuidFromBytes(bytes).value();
}

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDir {
  fs::path path = fs::temp_directory_path() / "atlantis_scene_conformance_tests" /
                  (gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
};

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

[[nodiscard]] std::vector<std::byte> readBytes(const fs::path& path) {
  const std::string text = readText(path);
  std::vector<std::byte> bytes(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) bytes[i] = static_cast<std::byte>(text[i]);
  return bytes;
}

// The source-side outcome of a scene text: a parse error, a cook error, or a
// cooked artifact.
struct Cooked {
  std::optional<as::SceneSourceParseError> parseError;
  std::optional<as::SceneCookError> cookError;
  std::vector<std::byte> artifact;
};

[[nodiscard]] Cooked cook(const std::string& text) {
  Cooked out;
  const auto parsed = as::parseSceneSource(text);
  if (parsed.isErr()) {
    out.parseError = parsed.error();
    return out;
  }
  TempDir dir;
  const fs::path source = dir.path / "scene.scene.txt";
  {
    std::ofstream file(source, std::ios::binary);
    file << text;
  }
  const fs::path artifact = dir.path / "scene.ascene";
  const fs::path metadata = dir.path / "scene.ascene.meta.txt";
  const auto result = as::cookScene(source.string(), asset(0xff), artifact.string(), metadata.string());
  if (result.isErr()) {
    out.cookError = result.error();
    return out;
  }
  out.artifact = readBytes(artifact);
  return out;
}

[[nodiscard]] std::string textOf(const scene::AuthoringScene& s) {
  const auto parsed = scene::toParsedSceneSource(s);
  REQUIRE(parsed.isOk());
  return as::serializeSceneSource(parsed.value());
}

[[nodiscard]] scene::AuthoringScene authoringOf(const std::string& text) {
  const auto parsed = as::parseSceneSource(text);
  REQUIRE(parsed.isOk());
  const auto authored = scene::toAuthoringScene(parsed.value());
  REQUIRE(authored.isOk());
  return authored.value();
}

// ------------------------------------------- the expected artifact projection
//
// Spec 0049 R5: semantic -> artifact is a declared projection -- asset GUIDs
// become assetKey()s, node references become indices, node order is kept.
// Implemented here independently of the library's own mapping.

[[nodiscard]] as::DecodedSceneArtifact project(const scene::AuthoringScene& s) {
  std::map<as::EntityGuid, std::size_t> indexOf;
  for (std::size_t i = 0; i < s.nodes.size(); ++i) indexOf.emplace(s.nodes[i].guid, i);

  as::DecodedSceneArtifact out;
  for (const scene::AuthoringNode& n : s.nodes) {
    as::ValidatedSceneNode v;
    const auto& t = n.transform;
    v.transform = {t.localPosition[0], t.localPosition[1], t.localPosition[2],
                   t.localEulerAnglesRadians[0], t.localEulerAnglesRadians[1], t.localEulerAnglesRadians[2],
                   t.localScale[0], t.localScale[1], t.localScale[2]};
    if (n.camera.has_value()) {
      const auto& c = *n.camera;
      as::DecodedCamera d;
      d.fovYRadians = c.fovYRadians;
      d.nearZ = c.nearZ;
      d.farZ = c.farZ;
      d.exposureCompensationEv = c.exposureCompensationEv;
      d.fog = {c.fog.color[0], c.fog.color[1], c.fog.color[2], c.fog.density, c.fog.height, c.fog.heightFalloff,
               c.fog.maxOpacity};
      d.bloom = {c.bloom.strength, c.bloom.threshold};
      v.camera = d;
    }
    if (n.renderable.has_value()) {
      v.renderable = as::DecodedRenderable{
          as::assetKey(n.renderable->meshAsset),
          n.renderable->materialAsset.has_value() ? std::optional<as::AssetId>(as::assetKey(*n.renderable->materialAsset))
                                                  : std::nullopt};
    }
    if (n.light.has_value()) {
      const auto& l = *n.light;
      v.light = as::DecodedLight{l.kind == scene::LightKind::Point ? as::DecodedLightKind::Point
                                                                   : as::DecodedLightKind::Directional,
                                 l.color[0], l.color[1], l.color[2], l.intensity, l.range};
    }
    out.nodes.push_back(v);
    out.parents.push_back(n.parent.has_value() ? std::optional<std::size_t>(indexOf.at(*n.parent)) : std::nullopt);
    out.entityGuids.push_back(n.guid);
  }
  if (s.activeCamera.has_value()) out.activeCameraIndex = indexOf.at(*s.activeCamera);
  return out;
}

[[nodiscard]] bool same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

[[nodiscard]] bool same(const as::DecodedTransform& a, const as::DecodedTransform& b) {
  return same(a.positionX, b.positionX) && same(a.positionY, b.positionY) && same(a.positionZ, b.positionZ) &&
         same(a.eulerXRadians, b.eulerXRadians) && same(a.eulerYRadians, b.eulerYRadians) &&
         same(a.eulerZRadians, b.eulerZRadians) && same(a.scaleX, b.scaleX) && same(a.scaleY, b.scaleY) &&
         same(a.scaleZ, b.scaleZ);
}

[[nodiscard]] bool same(const as::DecodedCamera& a, const as::DecodedCamera& b) {
  return same(a.fovYRadians, b.fovYRadians) && same(a.nearZ, b.nearZ) && same(a.farZ, b.farZ) &&
         same(a.exposureCompensationEv, b.exposureCompensationEv) && same(a.fog.colorR, b.fog.colorR) &&
         same(a.fog.colorG, b.fog.colorG) && same(a.fog.colorB, b.fog.colorB) && same(a.fog.density, b.fog.density) &&
         same(a.fog.height, b.fog.height) && same(a.fog.heightFalloff, b.fog.heightFalloff) &&
         same(a.fog.maxOpacity, b.fog.maxOpacity) && same(a.bloom.strength, b.bloom.strength) &&
         same(a.bloom.threshold, b.bloom.threshold);
}

[[nodiscard]] bool same(const as::DecodedRenderable& a, const as::DecodedRenderable& b) {
  return a.meshAsset == b.meshAsset && a.materialAsset == b.materialAsset;
}

[[nodiscard]] bool same(const as::DecodedLight& a, const as::DecodedLight& b) {
  return a.kind == b.kind && same(a.colorR, b.colorR) && same(a.colorG, b.colorG) && same(a.colorB, b.colorB) &&
         same(a.intensity, b.intensity) && same(a.range, b.range);
}

template <typename T>
[[nodiscard]] bool same(const std::optional<T>& a, const std::optional<T>& b) {
  return a.has_value() == b.has_value() && (!a.has_value() || same(*a, *b));
}

[[nodiscard]] bool same(const as::DecodedSceneArtifact& a, const as::DecodedSceneArtifact& b) {
  if (a.nodes.size() != b.nodes.size()) return false;
  for (std::size_t i = 0; i < a.nodes.size(); ++i) {
    if (!same(a.nodes[i].transform, b.nodes[i].transform) || !same(a.nodes[i].camera, b.nodes[i].camera) ||
        !same(a.nodes[i].renderable, b.nodes[i].renderable) || !same(a.nodes[i].light, b.nodes[i].light)) {
      return false;
    }
  }
  return a.parents == b.parents && a.activeCameraIndex == b.activeCameraIndex && a.entityGuids == b.entityGuids;
}

// ------------------------------------------------------- artifact patching

// Record layout per scene_artifact.h (kSceneArtifactSchemaVersion 7).
constexpr std::size_t kHeaderNodeCount = 8;
constexpr std::size_t kHeaderActiveCameraIndex = 16;
constexpr std::size_t kHasCamera = 36;
constexpr std::size_t kHasLight = 116;
constexpr std::size_t kLightKind = 120;
constexpr std::size_t kLightColor = 124;
constexpr std::size_t kLightIntensity = 136;
constexpr std::size_t kLightRange = 140;
constexpr std::size_t kHasParent = 144;
constexpr std::size_t kParentIndex = 148;
constexpr std::size_t kEntityGuid = 152;

[[nodiscard]] std::size_t recordOffset(std::size_t node, std::size_t field) {
  return as::kSceneArtifactHeaderSizeBytes + node * as::kSceneArtifactNodeRecordSizeBytes + field;
}

void putU32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t v) {
  for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::byte>((v >> (8 * i)) & 0xffu);
}
void putU64(std::vector<std::byte>& bytes, std::size_t at, std::uint64_t v) {
  for (std::size_t i = 0; i < 8; ++i) bytes[at + i] = static_cast<std::byte>((v >> (8 * i)) & 0xffu);
}
void putF32(std::vector<std::byte>& bytes, std::size_t at, float v) { putU32(bytes, at, std::bit_cast<std::uint32_t>(v)); }

// --------------------------------------------------------------- the base

// Node 0: the active camera, fog and bloom on (so the serializer emits every
// fog/bloom field -- ruling J3's exactly-representable domain). Node 1: a
// renderable with a material, parented to node 0. Node 2: a point light.
// Every value is exact in six decimals.
constexpr std::size_t kCameraNode = 0;
constexpr std::size_t kMeshNode = 1;
constexpr std::size_t kLightNode = 2;

[[nodiscard]] scene::AuthoringScene baseScene() {
  scene::AuthoringScene s;
  scene::AuthoringNode camera;
  camera.guid = entity(1);
  camera.transform.localPosition = {0.0f, 1.0f, 5.0f};
  scene::Camera c;
  c.fovYRadians = 1.0f;
  c.nearZ = 0.5f;
  c.farZ = 64.0f;
  c.exposureCompensationEv = 1.5f;
  c.fog = {{0.5f, 0.25f, 0.75f}, 0.125f, 2.0f, 0.25f, 0.75f};
  c.bloom = {0.5f, 1.5f};
  camera.camera = c;

  scene::AuthoringNode mesh;
  mesh.guid = entity(2);
  mesh.parent = entity(1);
  mesh.transform.localPosition = {1.0f, 0.0f, 0.0f};
  mesh.renderable = scene::Renderable{asset(1), asset(2)};

  scene::AuthoringNode light;
  light.guid = entity(3);
  light.light = scene::Light{scene::LightKind::Point, {0.5f, 0.25f, 1.0f}, 2.0f, 4.0f};

  s.nodes = {camera, mesh, light};
  s.activeCamera = entity(1);
  return s;
}

void replaceOnce(std::string& text, std::string_view from, std::string_view to) {
  const auto at = text.find(from);
  REQUIRE(at != std::string::npos);
  text.replace(at, from.size(), to);
}

// The source-side expectation: rejected by the parser or by the cooker.
struct SourceExpectation {
  std::optional<as::SceneSourceParseError> parse;
  std::optional<as::SceneCookError> cook;
};

[[nodiscard]] SourceExpectation parseFails(as::SceneSourceParseError e) { return {e, std::nullopt}; }
[[nodiscard]] SourceExpectation cookFails(as::SceneCookError e) { return {std::nullopt, e}; }

void expectRejected(const std::string& text, const SourceExpectation& expected) {
  const Cooked result = cook(text);
  if (expected.parse.has_value()) {
    REQUIRE(result.parseError.has_value());
    CHECK(*result.parseError == *expected.parse);
  } else {
    REQUIRE_FALSE(result.parseError.has_value());
    REQUIRE(result.cookError.has_value());
    CHECK(*result.cookError == *expected.cook);
  }
}

// --------------------------------------------------------- field registry
//
// One case per non-struct field of the semantic component types: where it
// lives in the base scene and in the artifact record, how to set a float
// component, a valid perturbation, and today's source-side rejection of a
// domain violation (R4 trace).

using FloatSetter = void (*)(scene::AuthoringScene&, std::size_t component, float value);
using Perturbation = void (*)(scene::AuthoringScene&);

struct FieldCase {
  std::string_view owner;
  std::string_view field;
  std::size_t node;
  std::size_t artifactOffset;
  std::size_t components;  // floats: 1 or 3; 0 = not a float field
  FloatSetter set;
  Perturbation perturb;
  SourceExpectation violation;  // for float domains
};

[[nodiscard]] std::vector<FieldCase> fieldCases() {
  using as::SceneCookError;
  using as::SceneSourceParseError;
  const auto nonFinite = cookFails(SceneCookError::NonFiniteValue);
  const auto lightGroup = parseFails(SceneSourceParseError::InvalidComponentGroup);
  constexpr std::string_view kT = "asset_system::scene::Transform";
  constexpr std::string_view kC = "asset_system::scene::Camera";
  constexpr std::string_view kF = "asset_system::scene::CameraFog";
  constexpr std::string_view kB = "asset_system::scene::CameraBloom";
  constexpr std::string_view kL = "asset_system::scene::Light";
  constexpr std::string_view kR = "asset_system::scene::Renderable";
  return {
      FieldCase{kT, "localPosition", kMeshNode, 0, 3,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kMeshNode].transform.localPosition[c] = v; },
          [](scene::AuthoringScene& s) { s.nodes[kMeshNode].transform.localPosition = {1.5f, -2.0f, 0.25f}; }, nonFinite},
      FieldCase{kT, "localEulerAnglesRadians", kMeshNode, 12, 3,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kMeshNode].transform.localEulerAnglesRadians[c] = v; },
          [](scene::AuthoringScene& s) { s.nodes[kMeshNode].transform.localEulerAnglesRadians = {0.5f, -0.25f, 0.125f}; }, nonFinite},
      FieldCase{kT, "localScale", kMeshNode, 24, 3,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kMeshNode].transform.localScale[c] = v; },
          [](scene::AuthoringScene& s) { s.nodes[kMeshNode].transform.localScale = {2.0f, 0.5f, 3.0f}; }, nonFinite},
      FieldCase{kC, "fovYRadians", kCameraNode, 40, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->fovYRadians = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->fovYRadians = 0.75f; }, nonFinite},
      FieldCase{kC, "nearZ", kCameraNode, 44, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->nearZ = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->nearZ = 0.25f; }, nonFinite},
      FieldCase{kC, "farZ", kCameraNode, 48, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->farZ = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->farZ = 128.0f; }, nonFinite},
      FieldCase{kC, "exposureCompensationEv", kCameraNode, 52, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->exposureCompensationEv = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->exposureCompensationEv = -3.5f; }, nonFinite},
      FieldCase{kF, "color", kCameraNode, 56, 3,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->fog.color[c] = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->fog.color = {0.125f, 2.0f, 8.0f}; }, nonFinite},
      FieldCase{kF, "density", kCameraNode, 68, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->fog.density = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->fog.density = 0.25f; }, nonFinite},
      FieldCase{kF, "height", kCameraNode, 72, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->fog.height = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->fog.height = -4.0f; }, nonFinite},
      FieldCase{kF, "heightFalloff", kCameraNode, 76, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->fog.heightFalloff = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->fog.heightFalloff = 0.5f; }, nonFinite},
      FieldCase{kF, "maxOpacity", kCameraNode, 80, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->fog.maxOpacity = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->fog.maxOpacity = 0.5f; }, nonFinite},
      FieldCase{kB, "strength", kCameraNode, 84, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->bloom.strength = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->bloom.strength = 0.25f; }, nonFinite},
      FieldCase{kB, "threshold", kCameraNode, 88, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kCameraNode].camera->bloom.threshold = v; },
          [](scene::AuthoringScene& s) { s.nodes[kCameraNode].camera->bloom.threshold = 3.0f; }, nonFinite},
      FieldCase{kL, "color", kLightNode, kLightColor, 3,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kLightNode].light->color[c] = v; },
          [](scene::AuthoringScene& s) { s.nodes[kLightNode].light->color = {1.0f, 0.5f, 0.125f}; }, lightGroup},
      FieldCase{kL, "intensity", kLightNode, kLightIntensity, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kLightNode].light->intensity = v; },
          [](scene::AuthoringScene& s) { s.nodes[kLightNode].light->intensity = 6.0f; }, lightGroup},
      FieldCase{kL, "range", kLightNode, kLightRange, 1,
          [](scene::AuthoringScene& s, [[maybe_unused]] std::size_t c, float v) { s.nodes[kLightNode].light->range = v; },
          [](scene::AuthoringScene& s) { s.nodes[kLightNode].light->range = 8.0f; }, lightGroup},
      // Non-float fields: their domain cases are written out below.
      FieldCase{kL, "kind", kLightNode, kLightKind, 0, nullptr,
          [](scene::AuthoringScene& s) {
            s.nodes[kLightNode].light->kind = scene::LightKind::Directional;
            s.nodes[kLightNode].light->range = 0.0f;
          },
          {}},
      FieldCase{kR, "meshAsset", kMeshNode, 96, 0, nullptr,
          [](scene::AuthoringScene& s) { s.nodes[kMeshNode].renderable->meshAsset = asset(4); }, {}},
      FieldCase{kR, "materialAsset", kMeshNode, 108, 0, nullptr,
          [](scene::AuthoringScene& s) { s.nodes[kMeshNode].renderable->materialAsset = asset(5); }, {}},
  };
}

// The FieldIds of every non-struct field the scene schema's components reach.
[[nodiscard]] std::set<FieldId> semanticLeafFields() {
  std::set<FieldId> out;
  std::vector<atlantis::schema::TypeId> pending;
  for (const auto& rule : scene::sceneSchema().components) pending.push_back(rule.component);
  std::set<atlantis::schema::TypeId> seen;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!seen.insert(id).second) continue;
    for (const auto& type : as::assetSystemSchema()) {
      if (type.id != id) continue;
      for (const auto& field : type.fields) {
        if (field.kind == atlantis::schema::TypeKind::Struct) {
          pending.push_back(field.type);
        } else {
          out.insert(field.id);
        }
      }
    }
  }
  return out;
}

[[nodiscard]] FieldId idOf(const FieldCase& c) { return atlantis::schema::fieldId(c.owner, c.field); }

[[nodiscard]] const FieldCase& caseFor(const std::vector<FieldCase>& cases, FieldId id) {
  for (const auto& c : cases) {
    if (idOf(c) == id) return c;
  }
  FAIL("no field case");
  return cases.front();
}

// ---------------------------------------------------------- corpus

[[nodiscard]] fs::path assetsDir() { return fs::path{ATLANTIS_ASSETS_DIR}; }

struct CorpusScene {
  fs::path source;
  fs::path artifact;
};

// Every committed scene with a cooked artifact (Plan 0049 P10). The Bistro
// overlay is not an asset, so it has no artifact; its mapping round trip is
// in authoring_scene_mapping_tests.cpp.
[[nodiscard]] std::vector<CorpusScene> cookedCorpus() {
  std::vector<CorpusScene> out;
  for (const auto& entry : fs::directory_iterator(assetsDir() / "scenes")) {
    const std::string name = entry.path().filename().string();
    if (!name.ends_with(".scene.txt")) continue;
    const std::string stem = name.substr(0, name.size() - std::string_view{".scene.txt"}.size());
    out.push_back({entry.path(), fs::path{ATLANTIS_COOKED_SCENES_DIR} / (stem + ".ascene")});
  }
  out.push_back({assetsDir() / "_test_fixtures" / "cmake_scene_declaration_test.scene.txt",
                 fs::path{ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH}});
  return out;
}

}  // namespace

// ============================================================ declarations

static_assert(as::kSceneSourceSemanticVersion == scene::kSemanticVersion,
              "the scene source serializer must declare the current scene semantic version (Spec 0049 R5/R6)");
static_assert(as::kSceneArtifactSemanticVersion == scene::kSemanticVersion,
              "the scene artifact serializer must declare the current scene semantic version (Spec 0049 R5/R6)");

// ============================================================ R5: projection

TEST_CASE("conformance: every cooked corpus scene equals the projection of its semantic model",
          "[asset_system][scene][conformance]") {
  const auto corpus = cookedCorpus();
  REQUIRE(corpus.size() == 27);  // 26 scenes and the CMake-declaration fixture
  for (const auto& entry : corpus) {
    INFO(entry.source.string());
    REQUIRE(fs::exists(entry.artifact));
    const auto decoded = as::decodeSceneArtifact(readBytes(entry.artifact));
    REQUIRE(decoded.isOk());
    CHECK(same(decoded.value(), project(authoringOf(readText(entry.source)))));
  }
}

#if defined(ATLANTIS_BISTRO_SCENE_IMPORT_DIR)
TEST_CASE("conformance: the imported Bistro scene equals the projection of its semantic model",
          "[asset_system][scene][conformance][content]") {
  const fs::path source = fs::path{ATLANTIS_BISTRO_SCENE_IMPORT_DIR} / "bistro" / "bistro.scene.txt";
  const fs::path artifact{ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH};
  if (!fs::exists(source) || !fs::exists(artifact)) SKIP("Bistro content not imported");
  const auto decoded = as::decodeSceneArtifact(readBytes(artifact));
  REQUIRE(decoded.isOk());
  CHECK(same(decoded.value(), project(authoringOf(readText(source)))));
}
#endif

// ======================================================= R8: field coverage

TEST_CASE("conformance: the field registry covers exactly the schema's semantic fields",
          "[asset_system][scene][conformance]") {
  std::set<FieldId> registered;
  for (const auto& c : fieldCases()) CHECK(registered.insert(idOf(c)).second);
  CHECK(registered == semanticLeafFields());
}

TEST_CASE("conformance: every semantic field survives the source and artifact round trips",
          "[asset_system][scene][conformance]") {
  const scene::AuthoringScene base = baseScene();
  for (const FieldCase& c : fieldCases()) {
    INFO(c.owner << "." << c.field);
    scene::AuthoringScene perturbed = base;
    c.perturb(perturbed);
    REQUIRE_FALSE(perturbed == base);

    // Exact at the mapping layer (ruling J3).
    const auto parsed = scene::toParsedSceneSource(perturbed);
    REQUIRE(parsed.isOk());
    const auto mapped = scene::toAuthoringScene(parsed.value());
    REQUIRE(mapped.isOk());
    CHECK(mapped.value() == perturbed);

    // Text round trip, within the serializer's exactly-representable values.
    const std::string text = as::serializeSceneSource(parsed.value());
    CHECK(authoringOf(text) == perturbed);

    // Cook -> decode equals the projection, and the field is carried.
    const Cooked cooked = cook(text);
    REQUIRE(cooked.artifact.size() > 0);
    const auto decoded = as::decodeSceneArtifact(cooked.artifact);
    REQUIRE(decoded.isOk());
    CHECK(same(decoded.value(), project(perturbed)));
    CHECK_FALSE(same(project(perturbed), project(base)));
  }
}

// ================================================ R4/R8: domain coverage

TEST_CASE("conformance: every field domain has a case, and violations are rejected on both sides",
          "[asset_system][scene][conformance]") {
  const auto cases = fieldCases();
  const scene::AuthoringScene base = baseScene();
  const Cooked baseCooked = cook(textOf(base));
  REQUIRE(baseCooked.artifact.size() > 0);
  const float inf = std::numeric_limits<float>::infinity();

  std::set<FieldId> withDomain;
  for (const scene::FieldDomain& domain : scene::sceneSchema().domains) {
    CHECK(withDomain.insert(domain.field).second);
    const FieldCase& c = caseFor(cases, domain.field);
    INFO(c.owner << "." << c.field << " " << scene::toString(domain.kind));

    // value, component, accepted?
    struct Probe {
      float value;
      std::size_t component;
      bool accepted;
    };
    std::vector<Probe> probes;
    const std::size_t last = c.components == 0 ? 0 : c.components - 1;
    switch (domain.kind) {
      case scene::DomainKind::Finite: probes = {{inf, 0, false}}; break;
      case scene::DomainKind::Closed:
        probes = {{domain.min - 1.0f, 0, false},
                  {domain.max + 1.0f, last, false},
                  {inf, 0, false},
                  {domain.min, 0, true},
                  {domain.max, last, true}};
        break;
      case scene::DomainKind::AtLeast: probes = {{domain.min - 1.0f, 0, false}, {inf, 0, false}, {domain.min, 0, true}}; break;
      case scene::DomainKind::NonNilGuid:
      case scene::DomainKind::Enumerated: break;  // written out below
    }
    for (const Probe& p : probes) {
      INFO("value " << p.value << " component " << p.component);
      REQUIRE(c.set != nullptr);
      // Source side.
      scene::AuthoringScene s = base;
      c.set(s, p.component, p.value);
      if (p.accepted) {
        const Cooked accepted = cook(textOf(s));
        CHECK_FALSE(accepted.parseError.has_value());
        CHECK_FALSE(accepted.cookError.has_value());
      } else {
        expectRejected(textOf(s), c.violation);
      }
      // Decode side: the same value patched into a valid artifact.
      std::vector<std::byte> bytes = baseCooked.artifact;
      putF32(bytes, recordOffset(c.node, c.artifactOffset + 4 * p.component), p.value);
      const auto decoded = as::decodeSceneArtifact(bytes);
      if (p.accepted) {
        CHECK(decoded.isOk());
      } else {
        REQUIRE(decoded.isErr());
        CHECK(decoded.error() == as::SceneArtifactDecodeError::NonFiniteValue);
      }
    }
  }
  CHECK(withDomain == semanticLeafFields());
}

TEST_CASE("conformance: Light.kind's Enumerated domain is enforced on both sides",
          "[asset_system][scene][conformance]") {
  const scene::AuthoringScene base = baseScene();
  const Cooked baseCooked = cook(textOf(base));
  REQUIRE(baseCooked.artifact.size() > 0);
  std::string text = textOf(base);
  replaceOnce(text, "light=point", "light=spot");
  expectRejected(text, parseFails(as::SceneSourceParseError::InvalidComponentGroup));
  std::vector<std::byte> bytes = baseCooked.artifact;
  putU32(bytes, recordOffset(kLightNode, kLightKind), 2);
  const auto decoded = as::decodeSceneArtifact(bytes);
  REQUIRE(decoded.isErr());
  CHECK(decoded.error() == as::SceneArtifactDecodeError::NonFiniteValue);
}

TEST_CASE("conformance: Renderable's NonNilGuid domain -- source rejects; decode side is KnownGap F3 (ruling J1)",
          "[asset_system][scene][conformance]") {
  const scene::AuthoringScene base = baseScene();
  const Cooked baseCooked = cook(textOf(base));
  REQUIRE(baseCooked.artifact.size() > 0);
  for (const std::size_t offset : {std::size_t{96}, std::size_t{108}}) {
    INFO("record offset " << offset);
    std::string text = textOf(base);
    const std::string guid = as::toString(offset == 96 ? asset(1) : asset(2));
    replaceOnce(text, guid, "00000000-0000-0000-0000-000000000000");
    expectRejected(text, parseFails(as::SceneSourceParseError::MalformedGuid));
    // F3: the artifact carries keys only and the decoder accepts key 0.
    std::vector<std::byte> bytes = baseCooked.artifact;
    putU64(bytes, recordOffset(kMeshNode, offset), 0);
    CHECK(as::decodeSceneArtifact(bytes).isOk());
  }
}

// =================================================== R4/R8: constraints

namespace {

enum class DecodeMarker { Rejected, KnownGapAccepted };

struct ConstraintCase {
  scene::Constraint id;
  std::string sourceText;
  SourceExpectation source;
  std::vector<std::byte> artifact;  // a valid artifact, patched
  DecodeMarker marker = DecodeMarker::Rejected;
  as::SceneArtifactDecodeError decodeError = as::SceneArtifactDecodeError::NonFiniteValue;
};

[[nodiscard]] scene::AuthoringNode lightNode(std::uint8_t guid, scene::LightKind kind) {
  scene::AuthoringNode n;
  n.guid = entity(guid);
  n.light = scene::Light{kind, {0.5f, 0.5f, 0.5f}, 1.0f, kind == scene::LightKind::Point ? 1.0f : 0.0f};
  return n;
}

[[nodiscard]] std::vector<ConstraintCase> constraintCases() {
  using as::SceneArtifactDecodeError;
  using as::SceneCookError;
  using as::SceneSourceParseError;
  using scene::Constraint;
  const scene::AuthoringScene base = baseScene();
  const std::string baseText = textOf(base);
  const std::vector<std::byte> baseArtifact = cook(baseText).artifact;
  REQUIRE(baseArtifact.size() > 0);
  std::vector<ConstraintCase> out;

  {  // NonEmptyDocument
    ConstraintCase c{Constraint::NonEmptyDocument,
                     "atlantis_scene_source_version: 7\nnode_count: 0\nactive_camera: none\n",
                     cookFails(SceneCookError::EmptyScene), baseArtifact};
    putU32(c.artifact, kHeaderNodeCount, 0);
    c.decodeError = SceneArtifactDecodeError::EmptyScene;
    out.push_back(c);
  }
  {  // NodeGuidNonNil
    ConstraintCase c{Constraint::NodeGuidNonNil, baseText, cookFails(SceneCookError::NilEntityGuid), baseArtifact};
    replaceOnce(c.sourceText, as::toString(entity(3)), "00000000-0000-0000-0000-000000000000");
    for (std::size_t i = 0; i < 16; ++i) c.artifact[recordOffset(kLightNode, kEntityGuid + i)] = std::byte{0};
    c.decodeError = SceneArtifactDecodeError::NilEntityGuid;
    out.push_back(c);
  }
  {  // NodeGuidUnique
    ConstraintCase c{Constraint::NodeGuidUnique, baseText, cookFails(SceneCookError::DuplicateEntityGuid),
                     baseArtifact};
    replaceOnce(c.sourceText, "guid=" + as::toString(entity(3)), "guid=" + as::toString(entity(2)));
    for (std::size_t i = 0; i < 16; ++i) {
      c.artifact[recordOffset(kLightNode, kEntityGuid + i)] = c.artifact[recordOffset(kMeshNode, kEntityGuid + i)];
    }
    c.decodeError = SceneArtifactDecodeError::DuplicateEntityGuid;
    out.push_back(c);
  }
  {  // ParentExists
    ConstraintCase c{Constraint::ParentExists, baseText, cookFails(SceneCookError::UndeclaredParentReference),
                     baseArtifact};
    replaceOnce(c.sourceText, "parent=1", "parent=99");
    putU32(c.artifact, recordOffset(kMeshNode, kParentIndex), 3);
    c.decodeError = SceneArtifactDecodeError::OutOfRangeParentIndex;
    out.push_back(c);
  }
  {  // ParentAcyclic: node 1 (the camera) under node 2, which is under node 1.
    ConstraintCase c{Constraint::ParentAcyclic, baseText, cookFails(SceneCookError::ParentCycle), baseArtifact};
    replaceOnce(c.sourceText, "parent=none", "parent=2");
    putU32(c.artifact, recordOffset(kCameraNode, kHasParent), 1);
    putU32(c.artifact, recordOffset(kCameraNode, kParentIndex), 1);
    c.decodeError = SceneArtifactDecodeError::CyclicParent;
    out.push_back(c);
  }
  {  // ActiveCameraExists
    ConstraintCase c{Constraint::ActiveCameraExists, baseText,
                     cookFails(SceneCookError::UndeclaredActiveCameraReference), baseArtifact};
    replaceOnce(c.sourceText, "active_camera: 1", "active_camera: 99");
    putU32(c.artifact, kHeaderActiveCameraIndex, 3);
    c.decodeError = SceneArtifactDecodeError::OutOfRangeActiveCameraIndex;
    out.push_back(c);
  }
  {  // ActiveCameraHasCamera
    ConstraintCase c{Constraint::ActiveCameraHasCamera, baseText,
                     cookFails(SceneCookError::ActiveCameraMissingCamera), baseArtifact};
    replaceOnce(c.sourceText, "active_camera: 1", "active_camera: 2");
    putU32(c.artifact, kHeaderActiveCameraIndex, 1);
    c.decodeError = SceneArtifactDecodeError::ActiveCameraMissingCamera;
    out.push_back(c);
  }
  {  // ComponentExclusivity: the grammar cannot author it; the decoder accepts
     // it (KnownGap F1, ruling J1).
    ConstraintCase c{Constraint::ComponentExclusivity, baseText,
                     parseFails(SceneSourceParseError::InvalidComponentGroup), baseArtifact};
    const std::string material = "material=" + as::toString(asset(2));
    replaceOnce(c.sourceText, material, material + " camera_fov_y=1.0 camera_near_z=0.5 camera_far_z=10.0");
    putU32(c.artifact, recordOffset(kCameraNode, kHasLight), 1);  // a camera node that is also a light
    c.marker = DecodeMarker::KnownGapAccepted;
    out.push_back(c);
  }
  {  // LightRangeMatchesKind: a point light with range 0.
    scene::AuthoringScene s = base;
    s.nodes[kLightNode].light->range = 0.0f;
    ConstraintCase c{Constraint::LightRangeMatchesKind, textOf(s),
                     parseFails(SceneSourceParseError::InvalidComponentGroup), baseArtifact};
    putF32(c.artifact, recordOffset(kLightNode, kLightRange), 0.0f);
    out.push_back(c);
  }
  {  // MaxDirectionalLights: two directional lights.
    scene::AuthoringScene s = base;
    s.nodes.push_back(lightNode(10, scene::LightKind::Directional));
    s.nodes.push_back(lightNode(11, scene::LightKind::Directional));
    scene::AuthoringScene valid = base;
    valid.nodes.push_back(lightNode(10, scene::LightKind::Directional));
    ConstraintCase c{Constraint::MaxDirectionalLights, textOf(s),
                     parseFails(SceneSourceParseError::TooManyLights), cook(textOf(valid)).artifact};
    putU32(c.artifact, recordOffset(kLightNode, kLightKind), 0);  // the point light becomes directional
    putF32(c.artifact, recordOffset(kLightNode, kLightRange), 0.0f);
    c.decodeError = SceneArtifactDecodeError::TooManyLights;
    out.push_back(c);
  }
  {  // MaxPointLights: kMaxPointLightsPerScene + 1 point lights.
    scene::AuthoringScene s = base;
    scene::AuthoringScene valid = base;
    for (std::uint32_t i = 1; i < as::kMaxPointLightsPerScene; ++i) {
      s.nodes.push_back(lightNode(static_cast<std::uint8_t>(16 + i), scene::LightKind::Point));
      valid.nodes.push_back(lightNode(static_cast<std::uint8_t>(16 + i), scene::LightKind::Point));
    }
    s.nodes.push_back(lightNode(200, scene::LightKind::Point));
    scene::AuthoringNode plain;  // becomes the 65th point light in the artifact
    plain.guid = entity(201);
    valid.nodes.push_back(plain);
    ConstraintCase c{Constraint::MaxPointLights, textOf(s), parseFails(SceneSourceParseError::TooManyLights),
                     cook(textOf(valid)).artifact};
    const std::size_t extra = valid.nodes.size() - 1;
    putU32(c.artifact, recordOffset(extra, kHasLight), 1);
    putU32(c.artifact, recordOffset(extra, kLightKind), 1);
    putF32(c.artifact, recordOffset(extra, kLightColor), 0.5f);
    putF32(c.artifact, recordOffset(extra, kLightIntensity), 1.0f);
    putF32(c.artifact, recordOffset(extra, kLightRange), 1.0f);
    c.decodeError = SceneArtifactDecodeError::TooManyLights;
    out.push_back(c);
  }
  return out;
}

}  // namespace

TEST_CASE("conformance: every scene constraint has exactly one case", "[asset_system][scene][conformance]") {
  std::set<scene::Constraint> registered;
  for (const auto& c : constraintCases()) CHECK(registered.insert(c.id).second);
  std::set<scene::Constraint> inSchema;
  for (const auto& rule : scene::sceneSchema().constraints) inSchema.insert(rule.id);
  CHECK(registered == inSchema);
}

TEST_CASE("conformance: every scene constraint is enforced as today's code enforces it",
          "[asset_system][scene][conformance]") {
  for (const ConstraintCase& c : constraintCases()) {
    INFO(scene::toString(c.id));
    expectRejected(c.sourceText, c.source);
    REQUIRE(c.artifact.size() > 0);
    const auto decoded = as::decodeSceneArtifact(c.artifact);
    if (c.marker == DecodeMarker::KnownGapAccepted) {
      CHECK(decoded.isOk());  // recorded gap: update this case if the decoder is ever tightened
    } else {
      REQUIRE(decoded.isErr());
      CHECK(decoded.error() == c.decodeError);
    }
  }
}

TEST_CASE("conformance: a directional light with a range is rejected on both sides",
          "[asset_system][scene][conformance]") {
  // LightRangeMatchesKind's directional half (R4 trace row 20, finding F2):
  // the grammar has no range token for it; the decoder rejects range != 0.
  scene::AuthoringScene s = baseScene();
  s.nodes[kLightNode].light->kind = scene::LightKind::Directional;
  s.nodes[kLightNode].light->range = 0.0f;
  std::string text = textOf(s);
  const std::string intensity = "intensity=2.000000";
  replaceOnce(text, intensity, intensity + " range=1.000000");
  expectRejected(text, parseFails(as::SceneSourceParseError::InvalidComponentGroup));

  std::vector<std::byte> bytes = cook(textOf(s)).artifact;
  REQUIRE(bytes.size() > 0);
  putF32(bytes, recordOffset(kLightNode, kLightRange), 1.0f);
  const auto decoded = as::decodeSceneArtifact(bytes);
  REQUIRE(decoded.isErr());
  CHECK(decoded.error() == as::SceneArtifactDecodeError::NonFiniteValue);
}
