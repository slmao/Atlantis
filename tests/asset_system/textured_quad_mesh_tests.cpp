#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/logical_path.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// Plan 0017 Milestone 2: proves the two independent quad mesh assets
// declared in assets/CMakeLists.txt (textured_quad_left,
// textured_quad_right) cook and load correctly through the real
// authoring -> cook -> artifact -> load path -- not a hand-constructed
// in-test fixture -- with the exact vertex count/positions/UVs
// transcribed from textured_quad_fixture.cpp's own former
// kLeftQuadVertices/kRightQuadVertices arrays, and with two distinct
// AssetIds (ADR-0044).
//
// Plan 0020: vertexStrideBytes() widened from 32 to 44 (the new
// position+color+UV0+normal layout) -- found during Implementation, not
// enumerated by Plan 0020's own Pre-draft verification (this file has
// no embedded mesh-source text and does not construct MeshSourceVertex
// directly, so neither of that Plan's own two search methods found it;
// it instead asserts hardcoded expected values against the real,
// already-cooked artifact). Position/UV assertions below are unaffected
// -- both remain at their own existing float indices (0-2, 6-7), since
// normal is appended after UV0, not inserted before it.

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] const float* vertexFloatsAt(const StaticMeshAssetData& data, std::size_t vertexIndex) {
  return reinterpret_cast<const float*>(data.vertexBytes().data() + vertexIndex * data.vertexStrideBytes());
}

}  // namespace

TEST_CASE("The textured_quad_left mesh asset cooks and loads with its own expected positions and UVs",
          "[asset_system][texture_fixture]") {
  const auto result =
      loadStaticMeshAsset(ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH);
  REQUIRE(result.isOk());
  const StaticMeshAssetData& data = result.value();

  REQUIRE(data.vertexStrideBytes() == 44);
  REQUIRE(data.vertexCount() == 4);
  REQUIRE(data.indexCount() == 6);

  // position.xy, uv -- transcribed value-for-value from
  // textured_quad_fixture.cpp's own former kLeftQuadVertices.
  const struct {
    float x, y, u, v;
  } expected[4] = {
      {-0.9f, -0.5f, 0.0f, 1.0f},
      {-0.1f, -0.5f, 1.0f, 1.0f},
      {-0.1f, 0.5f, 1.0f, 0.0f},
      {-0.9f, 0.5f, 0.0f, 0.0f},
  };
  for (std::size_t i = 0; i < 4; ++i) {
    const float* v = vertexFloatsAt(data, i);
    CHECK(v[0] == expected[i].x);
    CHECK(v[1] == expected[i].y);
    CHECK(v[2] == 0.0f);  // positionZ
    CHECK(v[6] == expected[i].u);
    CHECK(v[7] == expected[i].v);
  }

  const std::vector<std::uint16_t> expectedIndices{0, 1, 2, 2, 3, 0};
  CHECK(data.indices() == expectedIndices);
}

TEST_CASE("The textured_quad_right mesh asset cooks and loads with its own expected positions and UVs",
          "[asset_system][texture_fixture]") {
  const auto result =
      loadStaticMeshAsset(ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH);
  REQUIRE(result.isOk());
  const StaticMeshAssetData& data = result.value();

  REQUIRE(data.vertexStrideBytes() == 44);
  REQUIRE(data.vertexCount() == 4);
  REQUIRE(data.indexCount() == 6);

  // Transcribed value-for-value from textured_quad_fixture.cpp's own
  // former kRightQuadVertices.
  const struct {
    float x, y, u, v;
  } expected[4] = {
      {0.1f, -0.5f, 0.0f, 1.0f},
      {0.9f, -0.5f, 1.0f, 1.0f},
      {0.9f, 0.5f, 1.0f, 0.0f},
      {0.1f, 0.5f, 0.0f, 0.0f},
  };
  for (std::size_t i = 0; i < 4; ++i) {
    const float* v = vertexFloatsAt(data, i);
    CHECK(v[0] == expected[i].x);
    CHECK(v[1] == expected[i].y);
    CHECK(v[2] == 0.0f);  // positionZ
    CHECK(v[6] == expected[i].u);
    CHECK(v[7] == expected[i].v);
  }

  const std::vector<std::uint16_t> expectedIndices{0, 1, 2, 2, 3, 0};
  CHECK(data.indices() == expectedIndices);
}

TEST_CASE("textured_quad_left and textured_quad_right have distinct AssetIds derived from their own distinct "
          "normalized logical paths",
          "[asset_system][texture_fixture]") {
  const auto leftResult =
      loadStaticMeshAsset(ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH);
  const auto rightResult =
      loadStaticMeshAsset(ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH);
  REQUIRE(leftResult.isOk());
  REQUIRE(rightResult.isOk());

  const auto expectedLeftPath = normalizeLogicalPath("meshes/textured_quad_left.mesh.txt");
  const auto expectedRightPath = normalizeLogicalPath("meshes/textured_quad_right.mesh.txt");
  REQUIRE(expectedLeftPath.isOk());
  REQUIRE(expectedRightPath.isOk());
  CHECK(expectedLeftPath.value() != expectedRightPath.value());
  CHECK(computeAssetId(expectedLeftPath.value()) != computeAssetId(expectedRightPath.value()));
}
