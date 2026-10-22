#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/scene_types.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <atlantis/asset_system/asset_guid.h>

namespace {

// Plan 0047 M3: the GUID a test's references name (matches the literals,
// which were written with the same derivation).
[[nodiscard]] atlantis::asset_system::AssetGuid testAssetGuid(std::string_view key) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(), key);
}

// Plan 0047 P9: the guid= a generated node line carries -- the same
// derivation the literal node lines use, keyed by node_id.
[[nodiscard]] std::string testNodeGuidToken(std::uint32_t nodeId) {
  return " guid=" + atlantis::asset_system::toString(atlantis::asset_system::deriveEntityGuid(
                        atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(),
                        "node/" + std::to_string(nodeId)));
}

}  // namespace
using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kValidTwoNodeSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 2\n"
    "active_camera: 2\n"
    "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=-2.5 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "mesh=d27e38e4-1121-88bf-817e-97f8b83724a5\n"
    "node: node_id=2 guid=e68122c6-18b2-8f1f-b185-358f58780754 parent=none position=0.0 2.2 7.0 rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 "
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
  REQUIRE(parsed.nodes[0].meshAsset.has_value());
  CHECK(*parsed.nodes[0].meshAsset == testAssetGuid("meshes/minimal_cube.mesh.txt"));
  CHECK_FALSE(parsed.nodes[0].camera.has_value());

  CHECK(parsed.nodes[1].nodeId == 2);
  CHECK(parsed.nodes[1].transform.positionY == 2.2f);
  CHECK(parsed.nodes[1].transform.eulerXRadians == -0.3054f);
  REQUIRE(parsed.nodes[1].camera.has_value());
  CHECK(parsed.nodes[1].camera->fovYRadians == 1.0472f);
  CHECK(parsed.nodes[1].camera->nearZ == 0.1f);
  CHECK(parsed.nodes[1].camera->farZ == 100.0f);
  CHECK(parsed.nodes[1].camera->exposureCompensationEv == 0.0f);  // 14-token line, no camera_exposure_ev= token
  CHECK_FALSE(parsed.nodes[1].meshAsset.has_value());
}

TEST_CASE("parseSceneSource parses a camera node's optional 15th camera_exposure_ev= token",
          "[asset_system][scene]") {
  constexpr std::string_view source =
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: 1\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0 camera_exposure_ev=1.5\n";
  const auto result = parseSceneSource(source);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].camera.has_value());
  CHECK(result.value().nodes[0].camera->exposureCompensationEv == 1.5f);
}

TEST_CASE("parseSceneSource rejects a camera node's 15th token with the wrong prefix", "[asset_system][scene]") {
  constexpr std::string_view source =
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: 1\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0 camera_wrong_prefix=1.5\n";
  const auto result = parseSceneSource(source);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource parses a plain node with neither mesh nor camera", "[asset_system][scene]") {
  const std::string_view plainNode =
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";
  const auto result = parseSceneSource(plainNode);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes.size() == 1);
  CHECK_FALSE(result.value().nodes[0].meshAsset.has_value());
  CHECK_FALSE(result.value().nodes[0].camera.has_value());
  CHECK_FALSE(result.value().activeCameraNodeId.has_value());
}

TEST_CASE("parseSceneSource parses node_count of zero (EmptyScene is a cook-time policy, not a grammar rule)",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 0\n"
      "active_camera: none\n");
  REQUIRE(result.isOk());
  CHECK(result.value().nodes.empty());
}

TEST_CASE("parseSceneSource parses a parent reference", "[asset_system][scene]") {
  const std::string_view withParent =
      "atlantis_scene_source_version: 7\n"
      "node_count: 2\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n"
      "node: node_id=2 guid=e68122c6-18b2-8f1f-b185-358f58780754 parent=1 position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";
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
    CHECK(a.meshAsset == b.meshAsset);
    if (a.camera.has_value()) {
      REQUIRE(b.camera.has_value());
      CHECK(a.camera->fovYRadians == b.camera->fovYRadians);
      CHECK(a.camera->nearZ == b.camera->nearZ);
      CHECK(a.camera->farZ == b.camera->farZ);
      CHECK(a.camera->exposureCompensationEv == b.camera->exposureCompensationEv);
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
  // Plan 0047: this literal must name a value still genuinely
  // unrecognized now that version 7 is the real, accepted version -- 8
  // here (the Plan 0044/0043/0031/0020/0019 precedent).
  const auto result = parseSceneSource("atlantis_scene_source_version: 8\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseSceneSource rejects the superseded version 5 outright, with no dual-version reader",
          "[asset_system][scene][bloom]") {
  // Plan 0044: version 5 (pre-bloom) is now also rejected outright, even
  // for a node line version 6 accepts unchanged.
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 5\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=0 guid=e68122c6-1ab2-8f1f-b185-358f587809ca parent=none position=0 0 0 rotation=0 0 0 scale=1 1 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseSceneSource rejects the superseded version 4 outright, with no dual-version reader",
          "[asset_system][scene][fog]") {
  // Plan 0043: version 4 (pre-fog) is now also rejected outright, even
  // for a node line version 5 accepts unchanged.
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 4\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=0 guid=e68122c6-1ab2-8f1f-b185-358f587809ca parent=none position=0 0 0 rotation=0 0 0 scale=1 1 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseSceneSource rejects the superseded version 3 outright, with no dual-version reader",
          "[asset_system][scene]") {
  // Plan 0031: version 3 (pre-exposure, no camera_exposure_ev= token)
  // is now also rejected outright, exactly like versions 1 and 2
  // already were.
  const auto result = parseSceneSource("atlantis_scene_source_version: 3\n");
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
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource rejects a wrong field-order line", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "active_camera: none\n"
      "node_count: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseSceneSource rejects a malformed numeric token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=not_a_number guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MalformedNumber);
}

TEST_CASE("parseSceneSource rejects a malformed number within a position/rotation/scale group",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 not_a_number 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
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
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=nan 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isOk());
  CHECK(std::isnan(result.value().nodes[0].transform.positionX));
}

TEST_CASE("parseSceneSource rejects an invalid parent token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=not_a_number_or_none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidParentToken);
}

TEST_CASE("parseSceneSource rejects an invalid active_camera token", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 0\n"
      "active_camera: not_a_number_or_none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidParentToken);
}

TEST_CASE("parseSceneSource rejects a node line with a wrong token count", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a node line with both mesh and camera groups", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=6d89031d-aa70-8ebc-aff2-2cf69a2fdc69 "
      "camera_fov_y=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a mismatched trailing-group prefix", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "not_mesh=a.mesh.txt\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a huge node_count unsupported by the file's actual line count, without "
          "attempting a huge allocation",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 4000000000\n"
      "active_camera: none\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource parses a node with mesh and material", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=27af59de-0d4a-8df8-be15-826a2d29bc2f material=872a03e2-463b-82e3-9498-2566388634dd\n");
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].meshAsset.has_value());
  CHECK(*result.value().nodes[0].meshAsset == testAssetGuid("meshes/textured_quad_left.mesh.txt"));
  REQUIRE(result.value().nodes[0].materialAsset.has_value());
  CHECK(*result.value().nodes[0].materialAsset == testAssetGuid("materials/unlit_textured_quad.material.txt"));
}

TEST_CASE("parseSceneSource parses a node with mesh but no material (materialAsset absent)",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(kValidTwoNodeSource);
  REQUIRE(result.isOk());
  CHECK_FALSE(result.value().nodes[0].materialAsset.has_value());
}

TEST_CASE("parseSceneSource round-trips a node with mesh and material through serializeSceneSource",
          "[asset_system][scene]") {
  const auto parsedResult = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=a256357f-0ad1-851b-baf3-a0aac6a9e491 material=b09ded7e-cad9-8712-a2b2-974ae43ef5d6\n");
  REQUIRE(parsedResult.isOk());

  const std::string serialized = serializeSceneSource(parsedResult.value());
  const auto reparsedResult = parseSceneSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().nodes[0].meshAsset == parsedResult.value().nodes[0].meshAsset);
  CHECK(reparsedResult.value().nodes[0].materialAsset == parsedResult.value().nodes[0].materialAsset);
}

TEST_CASE("parseSceneSource rejects an empty material logical path", "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=a256357f-0ad1-851b-baf3-a0aac6a9e491 material=\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::MissingField);
}

TEST_CASE("parseSceneSource rejects a mismatched 13th-token prefix (material without the material= prefix)",
          "[asset_system][scene]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "mesh=a256357f-0ad1-851b-baf3-a0aac6a9e491 not_material=materials/a.material.txt\n");
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

// Spec 0019 D3 / docs/plans/0019-lighting-foundation.md P3, V2: the light=
// grammar's own well-formed shapes.
TEST_CASE("parseSceneSource parses a well-formed directional light node", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
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
  CHECK_FALSE(parsed.nodes[0].meshAsset.has_value());
}

TEST_CASE("parseSceneSource parses a well-formed point light node", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=1.0 2.0 3.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
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
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=spot color=1.0 1.0 1.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects range= present on a directional light line", "[asset_system][scene][light]") {
  // 17 tokens on a directional line: tokens.size() == 17 dispatches to
  // the point-shaped branch, whose own kindToken == "directional" check
  // then rejects it -- a real, exercised path, not merely inspected.
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=1.0 range=5.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects range= missing on a point light line", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=point color=1.0 1.0 1.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects an out-of-[0,1] color component", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.5 0.0 0.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a negative color component", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=0.0 -0.1 0.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a negative intensity", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=-1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource accepts intensity of exactly zero (a disable-without-delete convenience)",
          "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=0.0\n");
  REQUIRE(result.isOk());
}

TEST_CASE("parseSceneSource rejects a non-positive range on a point light", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=point color=1.0 1.0 1.0 intensity=1.0 range=0.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
}

TEST_CASE("parseSceneSource rejects a non-finite color/intensity/range component", "[asset_system][scene][light]") {
  CHECK(parseSceneSource(
            "atlantis_scene_source_version: 7\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
            "light=directional color=nan 0.0 0.0 intensity=1.0\n")
            .error() == SceneSourceParseError::InvalidComponentGroup);
  CHECK(parseSceneSource(
            "atlantis_scene_source_version: 7\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
            "light=directional color=1.0 1.0 1.0 intensity=inf\n")
            .error() == SceneSourceParseError::InvalidComponentGroup);
  CHECK(parseSceneSource(
            "atlantis_scene_source_version: 7\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
            "light=point color=1.0 1.0 1.0 intensity=1.0 range=inf\n")
            .error() == SceneSourceParseError::InvalidComponentGroup);
}

// Spec 0019 D3/finding 4, V3: the hard, structural light-count cap --
// never a silent, deterministic truncation.
TEST_CASE("parseSceneSource rejects a scene declaring a second directional light", "[asset_system][scene][light]") {
  const auto result = parseSceneSource(
      "atlantis_scene_source_version: 7\n"
      "node_count: 2\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=1.0\n"
      "node: node_id=2 guid=e68122c6-18b2-8f1f-b185-358f58780754 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
      "light=directional color=1.0 1.0 1.0 intensity=1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::TooManyLights);
}

// Plan 0040 Milestone 1: the cap is kMaxPointLightsPerScene (64, was 4).
TEST_CASE("parseSceneSource rejects a scene declaring kMaxPointLightsPerScene + 1 point lights",
          "[asset_system][scene][light]") {
  constexpr std::uint32_t kCount = kMaxPointLightsPerScene + 1;
  std::string source = "atlantis_scene_source_version: 7\nnode_count: " + std::to_string(kCount) +
                       "\nactive_camera: none\n";
  for (std::uint32_t i = 1; i <= kCount; ++i) {
    source += "node: node_id=" + std::to_string(i) + testNodeGuidToken(static_cast<std::uint32_t>(i)) +
               " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
               "light=point color=1.0 1.0 1.0 intensity=1.0 range=5.0\n";
  }
  const auto result = parseSceneSource(source);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneSourceParseError::TooManyLights);
}

TEST_CASE("parseSceneSource accepts exactly the fixed cap: 1 directional + kMaxPointLightsPerScene point lights",
          "[asset_system][scene][light]") {
  constexpr std::uint32_t kCount = 1 + kMaxPointLightsPerScene;
  std::string source = "atlantis_scene_source_version: 7\nnode_count: " + std::to_string(kCount) +
                       "\nactive_camera: none\n"
                       "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
                       "light=directional color=1.0 1.0 1.0 intensity=1.0\n";
  for (std::uint32_t i = 2; i <= kCount; ++i) {
    source += "node: node_id=" + std::to_string(i) + testNodeGuidToken(static_cast<std::uint32_t>(i)) +
               " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
               "light=point color=1.0 1.0 1.0 intensity=1.0 range=5.0\n";
  }
  const auto result = parseSceneSource(source);
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes.size() == kCount);
}

// ---------------------------------------------------------------------
// Plan 0043 P1: the optional camera fog group (scene source v5).
// ---------------------------------------------------------------------

namespace {

[[nodiscard]] std::string oneCameraNodeSource(std::string_view cameraSuffix) {
  return std::string(
             "atlantis_scene_source_version: 7\n"
             "node_count: 1\n"
             "active_camera: 1\n"
             "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
             "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0") +
         std::string(cameraSuffix) + "\n";
}

constexpr std::string_view kFogGroup = " fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0";

void checkParsedFog(const DecodedCameraFog& fog) {
  CHECK(fog.density == 0.05f);
  CHECK(fog.height == 1.5f);
  CHECK(fog.heightFalloff == 0.25f);
  CHECK(fog.maxOpacity == 0.9f);
  CHECK(fog.colorR == 0.5f);
  CHECK(fog.colorG == 0.75f);
  CHECK(fog.colorB == 2.0f);
}

}  // namespace

TEST_CASE("parseSceneSource: a camera node without a fog group carries the fog-off defaults",
          "[asset_system][scene][fog]") {
  const auto result = parseSceneSource(oneCameraNodeSource(""));
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].camera.has_value());
  const DecodedCameraFog& fog = result.value().nodes[0].camera->fog;
  CHECK(fog.density == 0.0f);
  CHECK(fog.height == 0.0f);
  CHECK(fog.heightFalloff == 0.0f);
  CHECK(fog.maxOpacity == 1.0f);
  CHECK(fog.colorR == 1.0f);
  CHECK(fog.colorG == 1.0f);
  CHECK(fog.colorB == 1.0f);
}

TEST_CASE("parseSceneSource parses a camera fog group without the exposure token (21 tokens)",
          "[asset_system][scene][fog]") {
  const auto result = parseSceneSource(oneCameraNodeSource(kFogGroup));
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].camera.has_value());
  CHECK(result.value().nodes[0].camera->exposureCompensationEv == 0.0f);
  checkParsedFog(result.value().nodes[0].camera->fog);
}

TEST_CASE("parseSceneSource parses a camera fog group after the exposure token (22 tokens)",
          "[asset_system][scene][fog]") {
  const auto result =
      parseSceneSource(oneCameraNodeSource(std::string(" camera_exposure_ev=-1.5") + std::string(kFogGroup)));
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].camera.has_value());
  CHECK(result.value().nodes[0].camera->exposureCompensationEv == -1.5f);
  checkParsedFog(result.value().nodes[0].camera->fog);
}

TEST_CASE("parseSceneSource: the fog grammar rejects only non-numbers -- the value domain is cook's",
          "[asset_system][scene][fog]") {
  const auto result = parseSceneSource(oneCameraNodeSource(" fog=-1 0 -2 7 fog_color=-1 inf 1e9"));
  REQUIRE(result.isOk());
  CHECK(result.value().nodes[0].camera->fog.density == -1.0f);
  CHECK(std::isinf(result.value().nodes[0].camera->fog.colorG));
}

TEST_CASE("parseSceneSource rejects a malformed fog group", "[asset_system][scene][fog]") {
  struct Case {
    const char* suffix;
    SceneSourceParseError expected;
  };
  const Case cases[] = {
      // Truncated or over-long groups.
      {" fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75", SceneSourceParseError::InvalidComponentGroup},
      {" fog=0.05 1.5 0.25 0.9", SceneSourceParseError::InvalidComponentGroup},
      {" fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0 3.0", SceneSourceParseError::InvalidComponentGroup},
      // The group must trail the camera fields (and exposure).
      {" fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0 camera_exposure_ev=1.0",
       SceneSourceParseError::InvalidComponentGroup},
      // The fifth token must carry fog_color=.
      {" fog=0.05 1.5 0.25 0.9 color=0.5 0.75 2.0", SceneSourceParseError::InvalidComponentGroup},
      // A fog_color= group without fog= is an unknown token shape.
      {" fog_color=0.5 0.75 2.0", SceneSourceParseError::InvalidComponentGroup},
      // Non-numbers in each position.
      {" fog=x 1.5 0.25 0.9 fog_color=0.5 0.75 2.0", SceneSourceParseError::MalformedNumber},
      {" fog=0.05 1.5 y 0.9 fog_color=0.5 0.75 2.0", SceneSourceParseError::MalformedNumber},
      {" fog=0.05 1.5 0.25 0.9 fog_color=z 0.75 2.0", SceneSourceParseError::MalformedNumber},
      {" fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 w", SceneSourceParseError::MalformedNumber},
  };
  for (const Case& c : cases) {
    DYNAMIC_SECTION(c.suffix) {
      const auto result = parseSceneSource(oneCameraNodeSource(c.suffix));
      REQUIRE(result.isErr());
      CHECK(result.error() == c.expected);
    }
  }
}

TEST_CASE("parseSceneSource rejects a fog group on a non-camera node", "[asset_system][scene][fog]") {
  const std::string base =
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0";
  const char* nodeSuffixes[] = {
      "",                                                     // plain node
      " mesh=d27e38e4-1121-88bf-817e-97f8b83724a5",                   // mesh
      " light=directional color=1 1 1 intensity=1",           // light, 16 + 7
      " light=point color=1 1 1 intensity=1 range=5",         // light, 17 + 7
  };
  for (const char* nodeSuffix : nodeSuffixes) {
    DYNAMIC_SECTION(nodeSuffix) {
      const auto result = parseSceneSource(base + nodeSuffix + std::string(kFogGroup) + "\n");
      REQUIRE(result.isErr());
      CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
    }
  }
}

TEST_CASE("serializeSceneSource then parseSceneSource round-trips a camera fog group",
          "[asset_system][scene][fog]") {
  const auto parsed = parseSceneSource(oneCameraNodeSource(kFogGroup));
  REQUIRE(parsed.isOk());
  const std::string text = serializeSceneSource(parsed.value());
  CHECK(text.find(" fog=") != std::string::npos);
  CHECK(text.find(" fog_color=") != std::string::npos);
  const auto reparsed = parseSceneSource(text);
  REQUIRE(reparsed.isOk());
  REQUIRE(reparsed.value().nodes[0].camera.has_value());
  checkParsedFog(reparsed.value().nodes[0].camera->fog);
}

TEST_CASE("serializeSceneSource keeps a fog group that is off but valued", "[asset_system][scene][fog]") {
  // Plan 0065 P1 (fixes Plan 0049 F5): density 0 is fog off, but the other
  // values are ordinary values (Plan 0049 ruling J4) -- the group is written
  // whenever any of its seven values is non-zero, so none is lost.
  const auto parsed = parseSceneSource(oneCameraNodeSource(" fog=0 1.5 0.25 0.9 fog_color=0.5 0.75 2.0"));
  REQUIRE(parsed.isOk());
  const std::string text = serializeSceneSource(parsed.value());
  CHECK(text.find(" fog=0 1.5 0.25 0.9 fog_color=0.5 0.75 2 bloom=0 1\n") != std::string::npos);
  const auto reparsed = parseSceneSource(text);
  REQUIRE(reparsed.isOk());
  const DecodedCameraFog& fog = reparsed.value().nodes[0].camera->fog;
  CHECK(fog.density == 0.0f);
  CHECK(fog.height == 1.5f);
  CHECK(fog.heightFalloff == 0.25f);
  CHECK(fog.maxOpacity == 0.9f);
  CHECK(fog.colorR == 0.5f);
  CHECK(fog.colorG == 0.75f);
  CHECK(fog.colorB == 2.0f);
}

// ---------------------------------------------------------------------
// Plan 0044 P1: the optional camera bloom group (scene source v6), and
// the ordered [fog][bloom] trailing-group pre-pass.
// ---------------------------------------------------------------------

TEST_CASE("parseSceneSource: a camera node without a bloom group carries bloom off", "[asset_system][scene][bloom]") {
  const auto result = parseSceneSource(oneCameraNodeSource(""));
  REQUIRE(result.isOk());
  REQUIRE(result.value().nodes[0].camera.has_value());
  CHECK(result.value().nodes[0].camera->bloom.strength == 0.0f);
  CHECK(result.value().nodes[0].camera->bloom.threshold == 1.0f);
}

TEST_CASE("parseSceneSource parses a bloom group alone, after exposure, and after a fog group",
          "[asset_system][scene][bloom]") {
  struct Case {
    const char* suffix;
    bool expectFog;
    float exposure;
  };
  const Case cases[] = {
      {" bloom=0.25 1.5", false, 0.0f},                                      // 16 tokens: not a light
      {" camera_exposure_ev=-1.5 bloom=0.25 1.5", false, -1.5f},             // 17 tokens
      {" fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0 bloom=0.25 1.5", true, 0.0f},
      {" camera_exposure_ev=-1.5 fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0 bloom=0.25 1.5", true, -1.5f},
  };
  for (const Case& c : cases) {
    DYNAMIC_SECTION(c.suffix) {
      const auto result = parseSceneSource(oneCameraNodeSource(c.suffix));
      REQUIRE(result.isOk());
      REQUIRE(result.value().nodes[0].camera.has_value());
      const DecodedCamera& camera = *result.value().nodes[0].camera;
      CHECK(camera.bloom.strength == 0.25f);
      CHECK(camera.bloom.threshold == 1.5f);
      CHECK(camera.exposureCompensationEv == c.exposure);
      if (c.expectFog) {
        checkParsedFog(camera.fog);
      } else {
        CHECK(camera.fog.density == 0.0f);
      }
    }
  }
}

TEST_CASE("parseSceneSource rejects a malformed or misplaced bloom group", "[asset_system][scene][bloom]") {
  struct Case {
    const char* suffix;
    SceneSourceParseError expected;
  };
  const Case cases[] = {
      {" bloom=0.25", SceneSourceParseError::InvalidComponentGroup},                  // truncated
      {" bloom=0.25 1.5 2.0", SceneSourceParseError::InvalidComponentGroup},          // trailing token
      {" bloom=0.25 1.5 fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0",                 // out of order
       SceneSourceParseError::InvalidComponentGroup},
      {" bloom=0.25 1.5 camera_exposure_ev=1.0", SceneSourceParseError::InvalidComponentGroup},
      {" fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0 bloom=0.25",                     // truncated after fog
       SceneSourceParseError::InvalidComponentGroup},
      {" fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0 bloom=0.25 1.5 x",
       SceneSourceParseError::InvalidComponentGroup},
      {" bloom=x 1.5", SceneSourceParseError::MalformedNumber},
      {" bloom=0.25 y", SceneSourceParseError::MalformedNumber},
  };
  for (const Case& c : cases) {
    DYNAMIC_SECTION(c.suffix) {
      const auto result = parseSceneSource(oneCameraNodeSource(c.suffix));
      REQUIRE(result.isErr());
      CHECK(result.error() == c.expected);
    }
  }
}

TEST_CASE("parseSceneSource rejects a bloom group on a non-camera node", "[asset_system][scene][bloom]") {
  const std::string base =
      "atlantis_scene_source_version: 7\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0";
  const char* nodeSuffixes[] = {
      "",
      " mesh=d27e38e4-1121-88bf-817e-97f8b83724a5",
      " light=directional color=1 1 1 intensity=1",
      " light=point color=1 1 1 intensity=1 range=5",
  };
  for (const char* nodeSuffix : nodeSuffixes) {
    DYNAMIC_SECTION(nodeSuffix) {
      const auto result = parseSceneSource(base + nodeSuffix + " bloom=0.25 1.5\n");
      REQUIRE(result.isErr());
      CHECK(result.error() == SceneSourceParseError::InvalidComponentGroup);
    }
  }
}

TEST_CASE("parseSceneSource: the bloom grammar rejects only non-numbers -- the value domain is cook's",
          "[asset_system][scene][bloom]") {
  const auto result = parseSceneSource(oneCameraNodeSource(" bloom=2 -1"));
  REQUIRE(result.isOk());
  CHECK(result.value().nodes[0].camera->bloom.strength == 2.0f);
  CHECK(result.value().nodes[0].camera->bloom.threshold == -1.0f);
}

TEST_CASE("serializeSceneSource round-trips a bloom group, with and without fog, and keeps an off group's threshold",
          "[asset_system][scene][bloom]") {
  for (const char* suffix : {" bloom=0.25 1.5", " fog=0.05 1.5 0.25 0.9 fog_color=0.5 0.75 2.0 bloom=0.25 1.5"}) {
    DYNAMIC_SECTION(suffix) {
      const auto parsed = parseSceneSource(oneCameraNodeSource(suffix));
      REQUIRE(parsed.isOk());
      const std::string text = serializeSceneSource(parsed.value());
      CHECK(text.find(" bloom=") != std::string::npos);
      const auto reparsed = parseSceneSource(text);
      REQUIRE(reparsed.isOk());
      CHECK(reparsed.value().nodes[0].camera->bloom.strength == 0.25f);
      CHECK(reparsed.value().nodes[0].camera->bloom.threshold == 1.5f);
    }
  }
  // Plan 0065 P1 (fixes Plan 0049 F5): strength 0 is bloom off, and the
  // threshold is still written whenever either value is non-zero.
  const auto off = parseSceneSource(oneCameraNodeSource(" bloom=0 3.0"));
  REQUIRE(off.isOk());
  const std::string offText = serializeSceneSource(off.value());
  CHECK(offText.find(" bloom=0 3\n") != std::string::npos);
  const auto offReparsed = parseSceneSource(offText);
  REQUIRE(offReparsed.isOk());
  CHECK(offReparsed.value().nodes[0].camera->bloom.strength == 0.0f);
  CHECK(offReparsed.value().nodes[0].camera->bloom.threshold == 3.0f);
}

// ---------------------------------------------------------------------
// Plan 0065 M1 (P1; Spec 0065 Correction 2026-10-22, ruling J10): the
// serializer is lossless -- shortest round-trip floats, and fog and bloom
// groups written whenever any of their values is non-zero.
// ---------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string readSourceText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

[[nodiscard]] bool sameBits(float a, float b) {
  return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

[[nodiscard]] std::vector<float> floatsOf(const ParsedSceneNode& n) {
  const DecodedTransform& t = n.transform;
  std::vector<float> out = {t.positionX,     t.positionY,     t.positionZ, t.eulerXRadians, t.eulerYRadians,
                            t.eulerZRadians, t.scaleX,        t.scaleY,    t.scaleZ};
  if (n.camera.has_value()) {
    const DecodedCamera& c = *n.camera;
    out.insert(out.end(), {c.fovYRadians, c.nearZ, c.farZ, c.exposureCompensationEv, c.fog.density, c.fog.height,
                           c.fog.heightFalloff, c.fog.maxOpacity, c.fog.colorR, c.fog.colorG, c.fog.colorB,
                           c.bloom.strength, c.bloom.threshold});
  }
  if (n.light.has_value()) {
    const DecodedLight& l = *n.light;
    out.insert(out.end(), {l.colorR, l.colorG, l.colorB, l.intensity, l.range});
  }
  return out;
}

// Every value of two parsed sources, floats compared bit for bit.
void checkSameBits(const ParsedSceneSource& a, const ParsedSceneSource& b) {
  CHECK(a.activeCameraNodeId == b.activeCameraNodeId);
  REQUIRE(a.nodes.size() == b.nodes.size());
  for (std::size_t i = 0; i < a.nodes.size(); ++i) {
    INFO("node " << i);
    const ParsedSceneNode& x = a.nodes[i];
    const ParsedSceneNode& y = b.nodes[i];
    CHECK(x.nodeId == y.nodeId);
    CHECK(x.entityGuid == y.entityGuid);
    CHECK(x.parentNodeId == y.parentNodeId);
    CHECK(x.meshAsset == y.meshAsset);
    CHECK(x.materialAsset == y.materialAsset);
    CHECK(x.camera.has_value() == y.camera.has_value());
    CHECK(x.light.has_value() == y.light.has_value());
    if (x.light.has_value() && y.light.has_value()) CHECK(x.light->kind == y.light->kind);
    const std::vector<float> fx = floatsOf(x);
    const std::vector<float> fy = floatsOf(y);
    REQUIRE(fx.size() == fy.size());
    for (std::size_t f = 0; f < fx.size(); ++f) {
      INFO("float " << f);
      CHECK(sameBits(fx[f], fy[f]));
    }
  }
}

// parse -> serialize -> parse gives every value bit for bit, and serialize is
// idempotent.
void checkLosslessAndIdempotent(const std::string& sourceText) {
  const auto parsed = parseSceneSource(sourceText);
  REQUIRE(parsed.isOk());
  const std::string text = serializeSceneSource(parsed.value());
  const auto reparsed = parseSceneSource(text);
  REQUIRE(reparsed.isOk());
  checkSameBits(parsed.value(), reparsed.value());
  CHECK(serializeSceneSource(reparsed.value()) == text);
}

// The committed scene sources: every scene, the test fixtures, and the Bistro
// overlay.
[[nodiscard]] std::vector<fs::path> committedSceneSources() {
  std::vector<fs::path> out;
  const fs::path assets{ATLANTIS_ASSETS_DIR};
  for (const fs::path& dir : {assets / "scenes", assets / "_test_fixtures"}) {
    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.path().filename().string().ends_with(".scene.txt")) out.push_back(entry.path());
    }
  }
  out.push_back(assets / "bistro" / "bistro_overlay.scene.txt");
  return out;
}

}  // namespace

TEST_CASE("serializeSceneSource is lossless and idempotent over every committed scene source",
          "[asset_system][scene][serializer]") {
  const std::vector<fs::path> sources = committedSceneSources();
  REQUIRE(sources.size() == 28);  // 26 scenes, the CMake-declaration fixture, the Bistro overlay
  for (const fs::path& source : sources) {
    INFO(source.string());
    checkLosslessAndIdempotent(readSourceText(source));
  }
}

#if defined(ATLANTIS_BISTRO_SCENE_IMPORT_DIR)
TEST_CASE("serializeSceneSource is lossless and idempotent over Bistro's generated source",
          "[asset_system][scene][serializer][content]") {
  const fs::path source = fs::path{ATLANTIS_BISTRO_SCENE_IMPORT_DIR} / "bistro" / "bistro.scene.txt";
  if (!fs::exists(source)) SKIP("Bistro content not imported");
  checkLosslessAndIdempotent(readSourceText(source));
}
#endif

TEST_CASE("serializeSceneSource keeps the floats six decimals could not hold, bit for bit",
          "[asset_system][scene][serializer]") {
  const float hard[] = {std::numeric_limits<float>::denorm_min(),
                        1.0e-40f,  // a subnormal
                        -std::numeric_limits<float>::denorm_min(),
                        1e30f,
                        -0.0f,
                        0.1234567f,
                        -0.6435011f,
                        1e-7f,
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::min(),
                        16777217.0f};
  for (const float value : hard) {
    INFO(value);
    ParsedSceneSource source;
    ParsedSceneNode camera;
    camera.nodeId = 1;
    camera.transform = DecodedTransform{value, value, value, value, value, value, value, value, value};
    DecodedCamera c;
    c.fovYRadians = value;
    c.nearZ = value;
    c.farZ = value;
    c.exposureCompensationEv = value;
    c.fog = DecodedCameraFog{value, value, value, value, value, value, value};
    c.bloom = DecodedCameraBloom{value, value};
    if (value == 0.0f) {
      // An all-zero group is not written (the last test below); keep one
      // value non-zero so -0 is carried in the others.
      c.fog.colorR = 1.0f;
      c.bloom.threshold = 1.0f;
    }
    camera.camera = c;
    source.nodes = {camera};
    // A point light carries the value where the grammar's light domain allows
    // it: colour in [0, 1], intensity >= 0, range > 0.
    const float magnitude = std::fabs(value);
    if (magnitude > 0.0f) {
      ParsedSceneNode light;
      light.nodeId = 2;
      light.light = DecodedLight{DecodedLightKind::Point, magnitude <= 1.0f ? magnitude : 1.0f, 0.0f, 1.0f, magnitude,
                                 magnitude};
      source.nodes.push_back(light);
    }

    const std::string text = serializeSceneSource(source);
    const auto reparsed = parseSceneSource(text);
    REQUIRE(reparsed.isOk());
    checkSameBits(source, reparsed.value());
    CHECK(serializeSceneSource(reparsed.value()) == text);
  }
}

TEST_CASE("serializeSceneSource writes the shortest round-trip text (ruling J10)", "[asset_system][scene][serializer]") {
  ParsedSceneSource source;
  ParsedSceneNode node;
  node.nodeId = 1;
  node.transform = DecodedTransform{0.0f, -0.0f, 1.0472f, 1e-7f, 0.1f, -2.5f, 1.0f, 100.0f, 1e30f};
  source.nodes = {node};
  const std::string text = serializeSceneSource(source);
  CHECK(text.find(" position=0 -0 1.0472 rotation=1e-07 0.1 -2.5 scale=1 100 1e+30\n") != std::string::npos);
}

TEST_CASE("serializeSceneSource writes no fog or bloom group whose values are all zero",
          "[asset_system][scene][serializer]") {
  // The rule as Spec 0065's Correction states it: a group is written when any
  // of its values is non-zero. An all-zero group is omitted, and reads back as
  // the no-group defaults; cookSceneToMemory() refuses such a document
  // (NotRepresentableInSource) rather than narrowing it.
  const auto parsed = parseSceneSource(oneCameraNodeSource(" fog=0 0 0 0 fog_color=0 0 0 bloom=0 0"));
  REQUIRE(parsed.isOk());
  const std::string text = serializeSceneSource(parsed.value());
  CHECK(text.find("fog") == std::string::npos);
  CHECK(text.find("bloom") == std::string::npos);

  // A camera with the no-group defaults (colour 1, max opacity 1, threshold 1)
  // writes both groups, at those defaults.
  const auto defaults = parseSceneSource(oneCameraNodeSource(""));
  REQUIRE(defaults.isOk());
  CHECK(serializeSceneSource(defaults.value()).find(" fog=0 0 0 1 fog_color=1 1 1 bloom=0 1\n") != std::string::npos);
}
