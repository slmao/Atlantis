#include <atlantis/asset_system/scene_source.h>

#include <cmath>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kValidTwoNodeSource =
    "atlantis_scene_source_version: 3\n"
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
      "atlantis_scene_source_version: 3\n"
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
      "atlantis_scene_source_version: 3\n"
      "node_count: 0\n"
      "active_camera: none\n");
  REQUIRE(result.isOk());
  CHECK(result.value().nodes.empty());
}

TEST_CASE("parseSceneSource parses a parent reference", "[asset_system][scene]") {
  const std::string_view withParent =
      "atlantis_scene_source_version: 3\n"
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

TEST_CASE("parseSceneSource rejects the superseded version 1 outright, with no dual-version reader",
          "[asset_system][scene]") {
  // Plan 0018 Section P6 / Spec 0018 D5: version 1 sources are rejected
  // outright once material= (version 2) exists -- no compatibility
  // migration. Unchanged by Plan 0019: version 1 stays rejected under
  // version 3's own grammar exactly as it was under version 2's.
  const auto result = parseSceneSource("atlantis_scene_source_version: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseSceneSource rejects the superseded version 2 outright, with no dual-version reader",
          "[asset_system][scene]") {
  // Plan 0019 Section P3/P16: version 2 (pre-light, no light= token) is
  // now also rejected outright, exactly like version 1 already was.
  const auto result = parseSceneSource("atlantis_scene_source_version: 2\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseSceneSource rejects an unrecognized future version line", "[asset_system][scene]") {
  // Plan 0019: this literal must name a value still genuinely
  // unrecognized now that version 3 is the real, accepted version -- 4
  // here, not 3 (matching Plan 0020's own identical precedent).
  const auto result = parseSceneSource("atlantis_scene_source_version: 4\n");
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
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource rejects a wrong field-order line", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "active_camera: none\n"
      "node_count: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseSceneSource rejects a malformed numeric token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=not_a_number parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MalformedNumber);
}

TEST_CASE("parseSceneSource rejects a malformed number within a position/rotation/scale group",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 not_a_number 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MalformedNumber);
}

TEST_CASE("parseSceneSource accepts a non-finite float (Plan 0015 D4's own step 7, not the grammar, rejects it)",
          "[asset_system][scene]") {
  // Unlike parseMeshSource(), this parser deliberately does not reject
  // nan/inf -- that is cookScene()'s own semantic check
  // (SceneCookError::NonFiniteValue), covered by cook_scene_tests.cpp
  // in Step 3, not here.
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=nan 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isOk());
  CHECK(std::isnan(result.value().nodes[0].transform.positionX));
}

TEST_CASE("parseSceneSource rejects an invalid parent token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=not_a_number_or_none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidParentToken);
}

TEST_CASE("parseSceneSource rejects an invalid active_camera token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 0\n"
      "active_camera: not_a_number_or_none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidParentToken);
}

TEST_CASE("parseSceneSource rejects a node line with a wrong token count", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a node line with both mesh and camera groups", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=a.mesh.txt "
      "camera_fov_y=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a mismatched trailing-group prefix", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
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
      "atlantis_scene_source_version: 3\n"
      "node_count: 4000000000\n"
      "active_camera: none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource parses a node with mesh and material", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=meshes/textured_quad_left.mesh.txt material=materials/unlit_textured_quad.material.txt\n");
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].meshLogicalPath.has_value());
  CHECK(*result.value().nodes[0].meshLogicalPath == "meshes/textured_quad_left.mesh.txt");
  REQUIRE(result.value().nodes[0].materialLogicalPath.has_value());
  CHECK(*result.value().nodes[0].materialLogicalPath == "materials/unlit_textured_quad.material.txt");
}

TEST_CASE("parseSceneSource parses a node with mesh but no material (materialLogicalPath absent)",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(kValidTwoNodeSource);
  REQUIRE(result.isOk());
  CHECK_FALSE(result.value().nodes[0].materialLogicalPath.has_value());
}

TEST_CASE("parseSceneSource round-trips a node with mesh and material through serializeSceneSource",
          "[asset_system][scene]") {
  const auto parsedResult = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=meshes/a.mesh.txt material=materials/a.material.txt\n");
  REQUIRE(parsedResult.isOk());

  const std::string serialized = serializeSceneSource(parsedResult.value());
  const auto reparsedResult = parseSceneSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().nodes[0].meshLogicalPath == parsedResult.value().nodes[0].meshLogicalPath);
  CHECK(reparsedResult.value().nodes[0].materialLogicalPath == parsedResult.value().nodes[0].materialLogicalPath);
}

TEST_CASE("parseSceneSource rejects an empty material logical path", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=meshes/a.mesh.txt material=\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource rejects a mismatched 13th-token prefix (material without the material= prefix)",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=meshes/a.mesh.txt not_material=materials/a.material.txt\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects trailing content after the final node line", "[asset_system][scene]") {
  std::string withTrailing(kValidTwoNodeSource);
  withTrailing += "extra garbage line\n";
  const auto result = parseSceneSource(withTrailing);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::TrailingContent);
}

// Spec 0019 D3 / plans/0019-lighting-foundation.md P3, V2: the light=
// grammar's own well-formed shapes.
TEST_CASE("parseSceneSource parses a well-formed directional light node", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=0.2 0.4 0.6 intensity=1.5\n");
  REQUIRE(result.isOk());
  const ParsedSceneSource& parsed = result.value();
  REQUIRE(parsed.nodes.size() == 1);
  REQUIRE(parsed.nodes[0].light.has_value());
  CHECK(parsed.nodes[0].light->kind == DecodedLightKind::Directional);
  CHECK(parsed.nodes[0].light->colorR == 0.2f);
  CHECK(parsed.nodes[0].light->colorG == 0.4f);
  CHECK(parsed.nodes[0].light->colorB == 0.6f);
  CHECK(parsed.nodes[0].light->intensity == 1.5f);
  CHECK(parsed.nodes[0].light->range == 0.0f);
  CHECK_FALSE(parsed.nodes[0].camera.has_value());
  CHECK_FALSE(parsed.nodes[0].meshLogicalPath.has_value());
}

TEST_CASE("parseSceneSource parses a well-formed point light node", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=1.0 2.0 3.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=point color=1.0 1.0 1.0 intensity=3.0 range=5.0\n");
  REQUIRE(result.isOk());
  const ParsedSceneSource& parsed = result.value();
  REQUIRE(parsed.nodes[0].light.has_value());
  CHECK(parsed.nodes[0].light->kind == DecodedLightKind::Point);
  CHECK(parsed.nodes[0].light->intensity == 3.0f);
  CHECK(parsed.nodes[0].light->range == 5.0f);
}

TEST_CASE("serializeSceneSource then parseSceneSource round-trips a light node", "[asset_system][scene][light]") {
  ParsedSceneSource source;
  ParsedSceneNode directionalNode;
  directionalNode.nodeId = 1;
  directionalNode.light = DecodedLight{DecodedLightKind::Directional, 0.25f, 0.5f, 0.75f, 2.0f, 0.0f};
  ParsedSceneNode pointNode;
  pointNode.nodeId = 2;
  pointNode.light = DecodedLight{DecodedLightKind::Point, 1.0f, 0.5f, 0.0f, 4.0f, 8.0f};
  source.nodes = {directionalNode, pointNode};

  const std::string text = serializeSceneSource(source);
  const auto reparsed = parseSceneSource(text);
  REQUIRE(reparsed.isOk());
  REQUIRE(reparsed.value().nodes.size() == 2);

  REQUIRE(reparsed.value().nodes[0].light.has_value());
  CHECK(reparsed.value().nodes[0].light->kind == DecodedLightKind::Directional);
  CHECK(reparsed.value().nodes[0].light->colorR == 0.25f);
  CHECK(reparsed.value().nodes[0].light->colorG == 0.5f);
  CHECK(reparsed.value().nodes[0].light->colorB == 0.75f);
  CHECK(reparsed.value().nodes[0].light->intensity == 2.0f);

  REQUIRE(reparsed.value().nodes[1].light.has_value());
  CHECK(reparsed.value().nodes[1].light->kind == DecodedLightKind::Point);
  CHECK(reparsed.value().nodes[1].light->intensity == 4.0f);
  CHECK(reparsed.value().nodes[1].light->range == 8.0f);
}

TEST_CASE("parseSceneSource rejects an unrecognized light kind token", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=spot color=1.0 1.0 1.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects range= present on a directional light line", "[asset_system][scene][light]") {
  // 17 tokens on a directional line: tokens.size() == 17 dispatches to
  // the point-shaped branch, whose own kindToken == "directional" check
  // then rejects it -- a real, exercised path, not merely inspected.
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=1.0 range=5.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects range= missing on a point light line", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=point color=1.0 1.0 1.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects an out-of-[0,1] color component", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.5 0.0 0.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a negative color component", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=0.0 -0.1 0.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a negative intensity", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=-1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource accepts intensity of exactly zero (a disable-without-delete convenience)",
          "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=0.0\n");
  REQUIRE(result.isOk());
}

TEST_CASE("parseSceneSource rejects a non-positive range on a point light", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=point color=1.0 1.0 1.0 intensity=1.0 range=0.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a non-finite color/intensity/range component", "[asset_system][scene][light]") {
  CHECK(parseSceneSource(
            "atlantis_scene_source_version: 3\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
            "light=directional color=nan 0.0 0.0 intensity=1.0\n")
            .error() == SceneSourceParseError::InvalidComponentGroup);
  CHECK(parseSceneSource(
            "atlantis_scene_source_version: 3\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
            "light=directional color=1.0 1.0 1.0 intensity=inf\n")
            .error() == SceneSourceParseError::InvalidComponentGroup);
  CHECK(parseSceneSource(
            "atlantis_scene_source_version: 3\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
            "light=point color=1.0 1.0 1.0 intensity=1.0 range=inf\n")
            .error() == SceneSourceParseError::InvalidComponentGroup);
}

// Spec 0019 D3/finding 4, V3: the hard, structural light-count cap --
// never a silent, deterministic truncation.
TEST_CASE("parseSceneSource rejects a scene declaring a second directional light", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 3\n"
      "node_count: 2\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=1.0\n"
      "node: node_id=2 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::TooManyLights);
}

TEST_CASE("parseSceneSource rejects a scene declaring a fifth point light", "[asset_system][scene][light]") {
  std::string source =
      "atlantis_scene_source_version: 3\n"
      "node_count: 5\n"
      "active_camera: none\n";
  for (int i = 1; i <= 5; ++i) {
    source += "node: node_id=" + std::to_string(i) +
               " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
               "light=point color=1.0 1.0 1.0 intensity=1.0 range=5.0\n";
  }
  const auto result = parseSceneSource(source);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::TooManyLights);
}

TEST_CASE("parseSceneSource accepts exactly the fixed cap: 1 directional + 4 point lights",
          "[asset_system][scene][light]") {
  std::string source =
      "atlantis_scene_source_version: 3\n"
      "node_count: 5\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=1.0\n";
  for (int i = 2; i <= 5; ++i) {
    source += "node: node_id=" + std::to_string(i) +
               " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
               "light=point color=1.0 1.0 1.0 intensity=1.0 range=5.0\n";
  }
  const auto result = parseSceneSource(source);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes.size() == 5);
}
