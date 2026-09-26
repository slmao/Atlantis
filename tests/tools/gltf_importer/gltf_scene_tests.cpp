#include "gltf_test_builder.h"
#include "import_command.h"
#include "scene_import.h"
#include "scene_transform.h"

#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/scene_source.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

// Plan 0037 Milestone 5: the scene-graph slice (ADR-0083 D5/D6).

namespace fs = std::filesystem;
using atlantis::gltf_importer::composeTransform;
using atlantis::gltf_importer::decomposeMatrix;
using atlantis::gltf_importer::DecomposedTransform;
using atlantis::gltf_importer::eulerFromQuaternion;
using atlantis::gltf_importer::GltfImportError;
using atlantis::gltf_importer::GltfImportSummary;
using atlantis::gltf_importer::importGltf;

namespace {

constexpr double kPi = 3.14159265358979323846;
const double kHalfSqrt2 = std::sqrt(0.5);

struct SceneRun {
  fs::path dir;
  fs::path outputDir;
  atlantis::Result<GltfImportSummary, GltfImportError> result;
};

SceneRun runSceneImport(const std::string& testName, const gltf_test::PrimitiveSpec& spec,
                        const std::optional<std::string>& overlayText = std::nullopt) {
  const fs::path dir = gltf_test::freshDirectory(testName);
  const fs::path input = gltf_test::writeGltf(dir, spec);
  const fs::path outputDir = dir / "out";
  std::optional<fs::path> overlayPath;
  if (overlayText) {
    overlayPath = dir / "overlay.scene.txt";
    std::ofstream(*overlayPath, std::ios::binary) << *overlayText;
  }
  return SceneRun{dir, outputDir, importGltf(input, dir, outputDir, "t", overlayPath)};
}

atlantis::asset_system::ParsedSceneSource parsedScene(const fs::path& outputDir) {
  std::ifstream in(outputDir / "t/t.scene.txt", std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto parsed = atlantis::asset_system::parseSceneSource(text);
  REQUIRE(parsed.isOk());
  return parsed.value();
}

bool reportContains(const GltfImportSummary& summary, const std::string& needle) {
  return std::any_of(summary.reportLines.begin(), summary.reportLines.end(),
                     [&](const std::string& line) { return line.find(needle) != std::string::npos; });
}

// Column-major rotation of a unit quaternion (x, y, z, w), for comparing
// against composeTransform() of the recovered Euler angles.
std::array<double, 9> quaternionMatrix(double x, double y, double z, double w) {
  return {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
          2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
          2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
}

std::array<double, 16> multiply(const std::array<double, 16>& a, const std::array<double, 16>& b) {
  std::array<double, 16> out{};
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      for (int k = 0; k < 4; ++k) out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    }
  }
  return out;
}

DecomposedTransform fromParsed(const atlantis::asset_system::DecodedTransform& t) {
  DecomposedTransform d;
  d.translation = {t.positionX, t.positionY, t.positionZ};
  d.eulerRadians = {t.eulerXRadians, t.eulerYRadians, t.eulerZRadians};
  d.scale = {t.scaleX, t.scaleY, t.scaleZ};
  return d;
}

}  // namespace

TEST_CASE("Quaternion to Euler matches hand-computed angles for the Ry*Rx*Rz convention",
          "[gltf_importer][scene]") {
  struct Case {
    const char* name;
    std::array<double, 4> q;
    std::array<double, 3> euler;  // pitch x, yaw y, roll z
  };
  const Case cases[] = {
      {"identity", {0, 0, 0, 1}, {0, 0, 0}},
      {"yaw 90 about +Y", {0, kHalfSqrt2, 0, kHalfSqrt2}, {0, kPi / 2, 0}},
      {"roll 90 about +Z", {0, 0, kHalfSqrt2, kHalfSqrt2}, {0, 0, kPi / 2}},
      {"pitch 30 about +X", {std::sin(kPi / 12), 0, 0, std::cos(kPi / 12)}, {kPi / 6, 0, 0}},
      // Gimbal lock: pitch +90 couples yaw and roll; roll is fixed to 0.
      {"pitch 90 (gimbal lock)", {kHalfSqrt2, 0, 0, kHalfSqrt2}, {kPi / 2, 0, 0}},
      // A non-normalized input is normalized first.
      {"yaw 90, quaternion scaled by 3", {0, 3 * kHalfSqrt2, 0, 3 * kHalfSqrt2}, {0, kPi / 2, 0}},
  };
  for (const Case& c : cases) {
    INFO(c.name);
    const auto e = eulerFromQuaternion(c.q);
    for (int i = 0; i < 3; ++i) CHECK(e[i] == Catch::Approx(c.euler[i]).margin(1e-12));
  }
}

TEST_CASE("Euler angles recovered from quaternions recompose to the same rotation in every octant",
          "[gltf_importer][scene]") {
  int checked = 0;
  for (const double ax : {-0.8, 0.3}) {
    for (const double ay : {-0.5, 0.9}) {
      for (const double az : {-0.2, 0.6}) {
        for (const double angle : {0.4, 2.1, 3.0}) {
          const double n = std::sqrt(ax * ax + ay * ay + az * az);
          const double s = std::sin(angle / 2) / n;
          const double x = ax * s, y = ay * s, z = az * s, w = std::cos(angle / 2);
          DecomposedTransform t;
          t.eulerRadians = eulerFromQuaternion({x, y, z, w});
          const auto m = composeTransform(t);
          const auto q = quaternionMatrix(x, y, z, w);
          for (int c = 0; c < 3; ++c) {
            for (int r = 0; r < 3; ++r) CHECK(m[c * 4 + r] == Catch::Approx(q[c * 3 + r]).margin(1e-12));
          }
          ++checked;
        }
      }
    }
  }
  CHECK(checked == 24);
}

TEST_CASE("Matrix decomposition round-trips T, R and non-uniform S, and rejects pathological matrices",
          "[gltf_importer][scene]") {
  DecomposedTransform in;
  in.translation = {4, -5, 6};
  in.eulerRadians = {0.3, -0.7, 1.1};
  in.scale = {2, 3, 0.5};
  const auto decomposed = decomposeMatrix(composeTransform(in));
  REQUIRE(decomposed.isOk());
  for (int i = 0; i < 3; ++i) {
    CHECK(decomposed.value().translation[i] == Catch::Approx(in.translation[i]).margin(1e-12));
    CHECK(decomposed.value().eulerRadians[i] == Catch::Approx(in.eulerRadians[i]).margin(1e-12));
    CHECK(decomposed.value().scale[i] == Catch::Approx(in.scale[i]).margin(1e-12));
  }

  const std::array<double, 16> sheared = {1, 0, 0, 0, 0.5, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  CHECK(decomposeMatrix(sheared).error() == GltfImportError::NonDecomposableMatrix);
  const std::array<double, 16> projective = {1, 0, 0, 0.2, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  CHECK(decomposeMatrix(projective).error() == GltfImportError::NonDecomposableMatrix);
  const std::array<double, 16> zeroScale = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  CHECK(decomposeMatrix(zeroScale).error() == GltfImportError::NonDecomposableMatrix);
  const std::array<double, 16> mirrored = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  CHECK(decomposeMatrix(mirrored).error() == GltfImportError::NegativeDeterminant);
}

TEST_CASE("A node hierarchy becomes parent-linked node lines with local transforms and repeated mesh instances",
          "[gltf_importer][scene]") {
  auto spec = gltf_test::unitQuad();
  const double s15 = std::sin(kPi / 12), c15 = std::cos(kPi / 12);
  spec.nodesJson = "[{\"translation\":[1,2,3],\"rotation\":[0,0.7071067811865476,0,0.7071067811865476],"
                   "\"children\":[1,2]},"
                   "{\"mesh\":0,\"scale\":[1,2,3]},"
                   "{\"translation\":[0,1,0],\"children\":[3]},"
                   "{\"mesh\":0,\"rotation\":[" +
                   std::to_string(s15) + ",0,0," + std::to_string(c15) + "]}]";
  const SceneRun run = runSceneImport("scene_hierarchy", spec);
  REQUIRE(run.result.isOk());
  const GltfImportSummary& summary = run.result.value();
  CHECK(summary.sceneNodeLines == 4);
  CHECK(summary.sceneMeshLines == 2);
  CHECK(summary.sceneMaxDepth == 3);
  CHECK(summary.meshesInstancedMoreThanOnce == 1);
  CHECK(summary.nonUniformScaleNodes == 1);
  CHECK(summary.primitivesWithoutMaterial == 2);

  const auto scene = parsedScene(run.outputDir);
  REQUIRE(scene.nodes.size() == 4);
  CHECK_FALSE(scene.activeCameraNodeId.has_value());
  // Pre-order, node_id = glTF index + 1.
  CHECK(scene.nodes[0].nodeId == 1);
  CHECK_FALSE(scene.nodes[0].parentNodeId.has_value());
  CHECK(scene.nodes[1].nodeId == 2);
  CHECK(scene.nodes[1].parentNodeId == 1u);
  CHECK(scene.nodes[2].nodeId == 3);
  CHECK(scene.nodes[2].parentNodeId == 1u);
  CHECK(scene.nodes[3].nodeId == 4);
  CHECK(scene.nodes[3].parentNodeId == 3u);
  CHECK(scene.nodes[1].meshLogicalPath == std::string("meshes/t/mesh_0_0"));
  CHECK(scene.nodes[3].meshLogicalPath == std::string("meshes/t/mesh_0_0"));
  CHECK_FALSE(scene.nodes[1].materialLogicalPath.has_value());
  CHECK(scene.nodes[0].transform.positionY == 2.0f);
  CHECK(scene.nodes[0].transform.eulerYRadians == Catch::Approx(kPi / 2).margin(1e-6));
  CHECK(scene.nodes[1].transform.scaleY == 2.0f);
  CHECK(scene.nodes[3].transform.eulerXRadians == Catch::Approx(kPi / 6).margin(1e-6));

  // World translation of node 4 = T(1,2,3) * Ry(90) * T(0,1,0) * origin = (1,3,3).
  std::array<double, 16> world = composeTransform(fromParsed(scene.nodes[0].transform));
  world = multiply(world, composeTransform(fromParsed(scene.nodes[2].transform)));
  world = multiply(world, composeTransform(fromParsed(scene.nodes[3].transform)));
  CHECK(world[12] == Catch::Approx(1.0).margin(1e-6));
  CHECK(world[13] == Catch::Approx(3.0).margin(1e-6));
  CHECK(world[14] == Catch::Approx(3.0).margin(1e-6));

  // Cook-level round trip through the real scene cooker and decoder.
  const fs::path artifact = run.dir / "cooked/t.ascene";
  const fs::path metadata = run.dir / "cooked/t.ascene.meta.txt";
  REQUIRE(atlantis::asset_system::cookScene((run.outputDir / "t/t.scene.txt").string(), artifact.string(),
                                            metadata.string())
              .isOk());
  const auto decoded = atlantis::asset_system::decodeScene(artifact.string(), metadata.string());
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().nodeCount() == 4);
  std::size_t roots = 0;
  for (std::size_t i = 0; i < decoded.value().nodeCount(); ++i) roots += decoded.value().parentOf(i).has_value() ? 0 : 1;
  CHECK(roots == 1);

  const std::string manifest = [&] {
    std::ifstream in(run.outputDir / "cook_manifest.txt", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }();
  CHECK(manifest.find("--kind=scene --source={import_dir}/t/t.scene.txt --asset-root={import_dir}") !=
        std::string::npos);
}

TEST_CASE("A matrix node is decomposed into TRS", "[gltf_importer][scene]") {
  DecomposedTransform in;
  in.translation = {4, 5, 6};
  in.eulerRadians = {0.3, -0.7, 1.1};
  in.scale = {2, 3, 0.5};
  const auto m = composeTransform(in);
  std::string matrix;
  for (int i = 0; i < 16; ++i) matrix += (i ? "," : "") + std::to_string(m[i]);
  auto spec = gltf_test::unitQuad();
  spec.nodesJson = "[{\"mesh\":0,\"matrix\":[" + matrix + "]}]";
  const SceneRun run = runSceneImport("scene_matrix_node", spec);
  REQUIRE(run.result.isOk());
  const auto t = parsedScene(run.outputDir).nodes[0].transform;
  CHECK(t.positionX == Catch::Approx(4).margin(1e-5));
  CHECK(t.positionZ == Catch::Approx(6).margin(1e-5));
  CHECK(t.eulerXRadians == Catch::Approx(0.3).margin(1e-5));
  CHECK(t.eulerYRadians == Catch::Approx(-0.7).margin(1e-5));
  CHECK(t.eulerZRadians == Catch::Approx(1.1).margin(1e-5));
  CHECK(t.scaleX == Catch::Approx(2).margin(1e-5));
  CHECK(t.scaleZ == Catch::Approx(0.5).margin(1e-5));
}

TEST_CASE("Pathological node transforms fail with named errors and leave no output", "[gltf_importer][scene]") {
  struct Case {
    const char* name;
    const char* nodes;
    GltfImportError expected;
  };
  const Case cases[] = {
      {"sheared_matrix", "[{\"mesh\":0,\"matrix\":[1,0,0,0,0.5,1,0,0,0,0,1,0,0,0,0,1]}]",
       GltfImportError::NonDecomposableMatrix},
      {"mirroring_matrix", "[{\"mesh\":0,\"matrix\":[-1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}]",
       GltfImportError::NegativeDeterminant},
      {"negative_scale", "[{\"mesh\":0,\"scale\":[-1,1,1]}]", GltfImportError::NegativeDeterminant},
      {"zero_quaternion", "[{\"mesh\":0,\"rotation\":[0,0,0,0]}]", GltfImportError::InvalidNodeTransform},
  };
  for (const Case& c : cases) {
    INFO(c.name);
    auto spec = gltf_test::unitQuad();
    spec.nodesJson = c.nodes;
    const SceneRun run = runSceneImport(std::string("scene_reject_") + c.name, spec);
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == c.expected);
    CHECK_FALSE(fs::exists(run.outputDir));
  }
}

TEST_CASE("KHR_lights_punctual point and directional lights become light lines; range uses the placeholder",
          "[gltf_importer][scene]") {
  auto spec = gltf_test::unitQuad();
  spec.extensionsUsedJson = "[\"KHR_lights_punctual\"]";
  spec.topLevelExtensionsJson =
      "{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"point\",\"color\":[1,0.5,0.25],\"intensity\":40,"
      "\"range\":7},{\"type\":\"directional\",\"intensity\":3}]}}";
  // Node 0 has a mesh AND the directional light: the light moves to a
  // synthetic child (one of mesh/camera/light per line).
  spec.nodesJson =
      "[{\"mesh\":0,\"children\":[1],\"extensions\":{\"KHR_lights_punctual\":{\"light\":1}}},"
      "{\"translation\":[0,2,0],\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}}]";
  const SceneRun run = runSceneImport("scene_lights", spec);
  REQUIRE(run.result.isOk());
  const GltfImportSummary& summary = run.result.value();
  CHECK(summary.sceneLightLines == 2);
  CHECK(summary.syntheticNodes == 1);
  CHECK(reportContains(summary, "glTF range 7 discarded, range= set to kImportedPointLightRangePlaceholder = 10000 (approximately infinite"));
  CHECK(reportContains(summary, "directional, intensity 3 recorded as the raw glTF value"));

  const auto scene = parsedScene(run.outputDir);
  REQUIRE(scene.nodes.size() == 3);
  CHECK(scene.nodes[0].nodeId == 1);
  CHECK(scene.nodes[0].meshLogicalPath.has_value());
  CHECK(scene.nodes[1].nodeId == 3);  // synthetic: numbered after the 2 glTF nodes
  CHECK(scene.nodes[1].parentNodeId == 1u);
  REQUIRE(scene.nodes[1].light.has_value());
  CHECK(scene.nodes[1].light->kind == atlantis::asset_system::DecodedLightKind::Directional);
  CHECK(scene.nodes[1].light->intensity == 3.0f);
  REQUIRE(scene.nodes[2].light.has_value());
  CHECK(scene.nodes[2].nodeId == 2);
  CHECK(scene.nodes[2].light->kind == atlantis::asset_system::DecodedLightKind::Point);
  CHECK(scene.nodes[2].light->colorG == 0.5f);
  CHECK(scene.nodes[2].light->intensity == 40.0f);
  CHECK(scene.nodes[2].light->range == atlantis::gltf_importer::detail::kImportedPointLightRangePlaceholder);
}

TEST_CASE("Spot lights and excess lights are named errors", "[gltf_importer][scene]") {
  {
    auto spec = gltf_test::unitQuad();
    spec.extensionsUsedJson = "[\"KHR_lights_punctual\"]";
    spec.topLevelExtensionsJson = "{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"spot\",\"spot\":{}}]}}";
    spec.nodesJson = "[{\"mesh\":0,\"children\":[1]},{\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}}]";
    const SceneRun run = runSceneImport("scene_reject_spot", spec);
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == GltfImportError::UnsupportedLightType);
    CHECK_FALSE(fs::exists(run.outputDir));
  }
  {
    auto spec = gltf_test::unitQuad();
    spec.extensionsUsedJson = "[\"KHR_lights_punctual\"]";
    spec.topLevelExtensionsJson = "{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"directional\"}]}}";
    spec.nodesJson =
        "[{\"mesh\":0,\"children\":[1,2]},{\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}},"
        "{\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}}]";
    const SceneRun run = runSceneImport("scene_reject_two_directional", spec);
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == GltfImportError::TooManyLights);
  }
}

TEST_CASE("A camera node imports as a plain transform node and the camera is reported as dropped",
          "[gltf_importer][scene]") {
  auto spec = gltf_test::unitQuad();
  spec.camerasJson = "[{\"type\":\"perspective\",\"perspective\":{\"yfov\":0.8,\"znear\":0.1}}]";
  spec.nodesJson = "[{\"mesh\":0,\"children\":[1]},{\"camera\":0,\"translation\":[0,0,5]}]";
  const SceneRun run = runSceneImport("scene_camera", spec);
  REQUIRE(run.result.isOk());
  CHECK(run.result.value().camerasDropped == 1);
  CHECK(reportContains(run.result.value(), "camera dropped"));
  const auto scene = parsedScene(run.outputDir);
  REQUIRE(scene.nodes.size() == 2);
  CHECK_FALSE(scene.nodes[1].camera.has_value());
  CHECK_FALSE(scene.nodes[1].light.has_value());
  CHECK(scene.nodes[1].transform.positionZ == 5.0f);
  CHECK_FALSE(scene.activeCameraNodeId.has_value());
}

// ---------------------------------------------------------------------------
// Plan 0046 Milestone 2 (Plan 0046 P7, ADR-0094 Decision 3): the importer's
// light cap is the grammar's (1 directional + 64 point), and --overlay
// merges a scene source of cameras and lights into the imported scene.
// ---------------------------------------------------------------------------

namespace {

// n point-light nodes as children of the quad's node, all sharing light 0.
gltf_test::PrimitiveSpec quadWithPointLights(std::size_t n) {
  auto spec = gltf_test::unitQuad();
  spec.extensionsUsedJson = "[\"KHR_lights_punctual\"]";
  spec.topLevelExtensionsJson = "{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"point\"}]}}";
  std::string children;
  std::string lightNodes;
  for (std::size_t i = 0; i < n; ++i) {
    children += (i == 0 ? "" : ",") + std::to_string(i + 1);
    lightNodes += ",{\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}}";
  }
  spec.nodesJson = "[{\"mesh\":0,\"children\":[" + children + "]}" + lightNodes + "]";
  return spec;
}

std::string overlayScene(const std::vector<std::string>& nodeLines, const std::string& activeCamera) {
  std::string text = "atlantis_scene_source_version: 6\nnode_count: " + std::to_string(nodeLines.size()) +
                     "\nactive_camera: " + activeCamera + "\n";
  for (const std::string& line : nodeLines) text += line + "\n";
  return text;
}

const std::string kCameraNode =
    "node: node_id=10 parent=none position=8 1.7 24 rotation=0.05 0.785 0 scale=1 1 1 camera_fov_y=1.0472 "
    "camera_near_z=0.1 camera_far_z=200 camera_exposure_ev=-1 fog=0.015 0 0.15 0.7 fog_color=0.25 0.2 0.14 "
    "bloom=0.15 1.5";
const std::string kPointNode =
    "node: node_id=20 parent=none position=-39.61 3.26 -5.03 rotation=0 0 0 scale=1 1 1 light=point "
    "color=1 0.8 0.55 intensity=8 range=12";
const std::string kChildPointNode =
    "node: node_id=21 parent=20 position=0 1 0 rotation=0 0 0 scale=1 1 1 light=point color=1 0.85 0.6 "
    "intensity=3 range=5";
const std::string kMoonNode =
    "node: node_id=30 parent=none position=0 0 0 rotation=-1 2.4 0 scale=1 1 1 light=directional "
    "color=0.6 0.7 1 intensity=0.1";

}  // namespace

TEST_CASE("The importer's light cap is the grammar's: 64 point lights import, 65 are TooManyLights",
          "[gltf_importer][scene]") {
  {
    const SceneRun run = runSceneImport("scene_64_point_lights", quadWithPointLights(64));
    REQUIRE(run.result.isOk());
    CHECK(run.result.value().sceneLightLines == 64);
  }
  {
    const SceneRun run = runSceneImport("scene_65_point_lights", quadWithPointLights(65));
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == GltfImportError::TooManyLights);
    CHECK_FALSE(fs::exists(run.outputDir));
  }
}

TEST_CASE("The importer's overlay appends its camera and lights after every imported id, renumbered, and "
          "activates the camera",
          "[gltf_importer][scene][overlay]") {
  auto spec = gltf_test::unitQuad();
  spec.nodesJson = "[{\"mesh\":0,\"children\":[1]},{\"translation\":[0,1,0]}]";
  const SceneRun run =
      runSceneImport("scene_overlay_merge", spec, overlayScene({kCameraNode, kPointNode, kChildPointNode, kMoonNode}, "10"));
  REQUIRE(run.result.isOk());
  CHECK(run.result.value().overlayNodeLines == 4);
  CHECK(run.result.value().sceneNodeLines == 6);
  CHECK(run.result.value().sceneLightLines == 3);
  CHECK(reportContains(run.result.value(), "overlay: 4 node lines appended from id 3 (3 light), active_camera 3"));

  const auto scene = parsedScene(run.outputDir);
  REQUIRE(scene.nodes.size() == 6);
  // Imported ids 1-2 untouched; overlay 10/20/21/30 -> 3/4/5/6 in order.
  CHECK(scene.nodes[0].nodeId == 1);
  CHECK(scene.nodes[1].nodeId == 2);
  CHECK(scene.nodes[2].nodeId == 3);
  REQUIRE(scene.nodes[2].camera.has_value());
  CHECK(scene.nodes[2].camera->fovYRadians == Catch::Approx(1.0472f));
  CHECK(scene.nodes[2].camera->exposureCompensationEv == -1.0f);
  CHECK(scene.nodes[2].camera->fog.density == Catch::Approx(0.015f));
  CHECK(scene.nodes[2].camera->bloom.threshold == 1.5f);
  CHECK(scene.nodes[3].nodeId == 4);
  REQUIRE(scene.nodes[3].light.has_value());
  CHECK(scene.nodes[3].light->range == 12.0f);
  CHECK(scene.nodes[3].transform.positionX == Catch::Approx(-39.61f));
  CHECK(scene.nodes[4].nodeId == 5);
  CHECK(scene.nodes[4].parentNodeId == 4u);  // the parent renumbered with its child
  CHECK(scene.nodes[5].nodeId == 6);
  CHECK(scene.nodes[5].light->kind == atlantis::asset_system::DecodedLightKind::Directional);
  CHECK(scene.activeCameraNodeId == 3u);

  // The merged scene cooks.
  const auto cooked = atlantis::asset_system::cookScene((run.outputDir / "t/t.scene.txt").string(),
                                                        (run.dir / "t.ascene").string(),
                                                        (run.dir / "t.ascene.meta.txt").string());
  CHECK(cooked.isOk());
}

TEST_CASE("The importer's overlay rejects a renderable node, a parent outside it, a second camera, an unreadable or malformed "
          "file, and a merged light count past the cap -- each by name, leaving no output",
          "[gltf_importer][scene][overlay]") {
  auto spec = gltf_test::unitQuad();
  const auto expectError = [&](const std::string& name, const std::optional<std::string>& overlay,
                               GltfImportError expected) {
    INFO(name);
    const SceneRun run = runSceneImport(name, spec, overlay);
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == expected);
    CHECK_FALSE(fs::exists(run.outputDir));
  };
  expectError("overlay_renderable",
              overlayScene({"node: node_id=1 parent=none position=0 0 0 rotation=0 0 0 scale=1 1 1 "
                            "mesh=meshes/t/mesh_0_0 material=t/materials/0.material.txt"},
                           "none"),
              GltfImportError::OverlayRenderableNode);
  expectError("overlay_parent_outside",
              overlayScene({"node: node_id=5 parent=1 position=0 0 0 rotation=0 0 0 scale=1 1 1 light=point "
                            "color=1 1 1 intensity=1 range=1"},
                           "none"),
              GltfImportError::OverlayParentOutsideOverlay);
  std::string secondCamera = kCameraNode;
  secondCamera.replace(secondCamera.find("node_id=10"), 10, "node_id=11");
  expectError("overlay_second_camera", overlayScene({kCameraNode, secondCamera}, "10"),
              GltfImportError::OverlaySecondCamera);
  expectError("overlay_malformed", std::string("atlantis_scene_source_version: 6\nnode_count: 1\n"),
              GltfImportError::OverlayMalformed);
  std::string secondMoon = kMoonNode;
  secondMoon.replace(secondMoon.find("node_id=30"), 10, "node_id=31");
  expectError("overlay_two_directional", overlayScene({kMoonNode, secondMoon}, "none"),
              GltfImportError::TooManyLights);

  // The glTF's own lights count toward the cap with the overlay's: 64
  // imported + 1 overlay point light is 65.
  {
    const SceneRun run = runSceneImport("overlay_cap_combined", quadWithPointLights(64), overlayScene({kPointNode}, "none"));
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == GltfImportError::TooManyLights);
  }
  {
    const fs::path dir = gltf_test::freshDirectory("overlay_unreadable");
    const fs::path input = gltf_test::writeGltf(dir, spec);
    const auto result = importGltf(input, dir, dir / "out", "t", dir / "absent.scene.txt");
    REQUIRE(result.isErr());
    CHECK(result.error() == GltfImportError::OverlayUnreadable);
  }
}
