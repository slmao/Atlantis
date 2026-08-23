#include <atlantis/asset_system/scene_source.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kValidTwoNodeSource =
    "atlantis_scene_source_version: 1\n"
    "node_count: 2\n"
    "active_camera: 2\n"
    "node: node_id=1 parent=none position=-2.5 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "mesh=meshes/minimal_cube.mesh.txt\n"
    "node: node_id=2 parent=none position=0.0 2.2 7.0 rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 "
    "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n";

}  // namespace

TEST_CASE("parseSceneSource parses a well-formed two-node scene", "[asset_system][scene]") {
  const auto result = parseSceneSource(kValidTwoNodeSource);
  REQUIRE(result.isOk());
  const ParsedSceneSource& parsed = result.value();
  REQUIRE(parsed.nodes.size() == 2);
  REQUIRE(parsed.activeCameraNodeId.has_value());
  CHECK(*parsed.activeCameraNodeId == 2);

  CHECK(parsed.nodes[0].nodeId == 1);
  CHECK_FALSE(parsed.nodes[0].parentNodeId.has_value());
  CHECK(parsed.nodes[0].transform.positionX == -2.5f);
  CHECK(parsed.nodes[0].transform.positionY == 0.0f);
  CHECK(parsed.nodes[0].transform.scaleZ == 1.0f);
  REQUIRE(parsed.nodes[0].meshLogicalPath.has_value());
  CHECK(*parsed.nodes[0].meshLogicalPath == "meshes/minimal_cube.mesh.txt");
  CHECK_FALSE(parsed.nodes[0].camera.has_value());

  CHECK(parsed.nodes[1].nodeId == 2);
  CHECK(parsed.nodes[1].transform.positionY == 2.2f);
  CHECK(parsed.nodes[1].transform.eulerXRadians == -0.3054f);
  REQUIRE(parsed.nodes[1].camera.has_value());
  CHECK(parsed.nodes[1].camera->fovYRadians == 1.0472f);
  CHECK(parsed.nodes[1].camera->nearZ == 0.1f);
  CHECK(parsed.nodes[1].camera->farZ == 100.0f);
  CHECK_FALSE(parsed.nodes[1].meshLogicalPath.has_value());
}

TEST_CASE("parseSceneSource parses a plain node with neither mesh nor camera", "[asset_system][scene]") {
  const std::string_view plainNode =
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";
  const auto result = parseSceneSource(plainNode);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes.size() == 1);
  CHECK_FALSE(result.value().nodes[0].meshLogicalPath.has_value());
  CHECK_FALSE(result.value().nodes[0].camera.has_value());
  CHECK_FALSE(result.value().activeCameraNodeId.has_value());
}

TEST_CASE("parseSceneSource parses node_count of zero (EmptyScene is a cook-time policy, not a grammar rule)",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 0\n"
      "active_camera: none\n");
  REQUIRE(result.isOk());
  CHECK(result.value().nodes.empty());
}

TEST_CASE("parseSceneSource parses a parent reference", "[asset_system][scene]") {
  const std::string_view withParent =
      "atlantis_scene_source_version: 1\n"
      "node_count: 2\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n"
      "node: node_id=2 parent=1 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";
  const auto result = parseSceneSource(withParent);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[1].parentNodeId.has_value());
  CHECK(*result.value().nodes[1].parentNodeId == 1);
}

TEST_CASE("serializeSceneSource then parseSceneSource round-trips exactly-representable values",
          "[asset_system][scene]") {
  const auto reparsed = parseSceneSource(kValidTwoNodeSource);
  REQUIRE(reparsed.isOk());

  const std::string text = serializeSceneSource(reparsed.value());
  const auto reparsedAgain = parseSceneSource(text);
  REQUIRE(reparsedAgain.isOk());

  REQUIRE(reparsedAgain.value().nodes.size() == reparsed.value().nodes.size());
  CHECK(reparsedAgain.value().activeCameraNodeId == reparsed.value().activeCameraNodeId);
  for (std::size_t i = 0; i < reparsed.value().nodes.size(); ++i) {
    const ParsedSceneNode& a = reparsed.value().nodes[i];
    const ParsedSceneNode& b = reparsedAgain.value().nodes[i];
    CHECK(a.nodeId == b.nodeId);
    CHECK(a.parentNodeId == b.parentNodeId);
    CHECK(a.transform.positionX == b.transform.positionX);
    CHECK(a.transform.positionY == b.transform.positionY);
    CHECK(a.transform.positionZ == b.transform.positionZ);
    CHECK(a.transform.eulerXRadians == b.transform.eulerXRadians);
    CHECK(a.meshLogicalPath == b.meshLogicalPath);
    if (a.camera.has_value()) {
      REQUIRE(b.camera.has_value());
      CHECK(a.camera->fovYRadians == b.camera->fovYRadians);
      CHECK(a.camera->nearZ == b.camera->nearZ);
      CHECK(a.camera->farZ == b.camera->farZ);
    } else {
      CHECK_FALSE(b.camera.has_value());
    }
  }
}

TEST_CASE("parseSceneSource rejects an unrecognized version line", "[asset_system][scene]") {
  const auto result = parseSceneSource("atlantis_scene_source_version: 2\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseSceneSource rejects an empty file", "[asset_system][scene]") {
  const auto result = parseSceneSource("");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseSceneSource rejects a truncated file (missing a declared node line)", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource rejects a wrong field-order line", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "active_camera: none\n"
      "node_count: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseSceneSource rejects a malformed numeric token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=not_a_number parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MalformedNumber);
}

TEST_CASE("parseSceneSource rejects a malformed number within a position/rotation/scale group",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 not_a_number 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MalformedNumber);
}

TEST_CASE("parseSceneSource rejects a non-finite float", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=nan 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::NonFiniteFloat);
}

TEST_CASE("parseSceneSource rejects an invalid parent token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=not_a_number_or_none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidParentToken);
}

TEST_CASE("parseSceneSource rejects an invalid active_camera token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 0\n"
      "active_camera: not_a_number_or_none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidParentToken);
}

TEST_CASE("parseSceneSource rejects a node line with a wrong token count", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a node line with both mesh and camera groups", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=a.mesh.txt "
      "camera_fov_y=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a mismatched trailing-group prefix", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "not_mesh=a.mesh.txt\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a huge node_count unsupported by the file's actual line count, without "
          "attempting a huge allocation",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 1\n"
      "node_count: 4000000000\n"
      "active_camera: none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource rejects trailing content after the final node line", "[asset_system][scene]") {
  std::string withTrailing(kValidTwoNodeSource);
  withTrailing += "extra garbage line\n";
  const auto result = parseSceneSource(withTrailing);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::TrailingContent);
}
