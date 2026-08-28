#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/load.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

// Plan 0020 Section P6/V9: a dedicated CPU round-trip test proving a
// real, disclosed normal value written in an authoring source reaches
// loadStaticMeshAsset()'s own returned StaticMeshAssetData bit-for-bit
// -- through the real cookStaticMesh() -> loadStaticMeshAsset() path,
// not the encode/decode unit tests in isolation -- with no clamp, no
// flip, and no normalization. Uses one of Spec 0020 D5's own real
// minimal_cube corner values (mixed component signs), not a uniform
// placeholder, doubling as a real proof that a geometrically
// meaningful, non-uniform-across-components normal survives the full
// pipeline exactly, mirroring mesh_uv_round_trip_tests.cpp's own
// established shape.

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_system_mesh_normal_round_trip_tests" /
              (label + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

void writeFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

// Three real, distinct normal values, not a single repeated
// placeholder: Spec 0020 D5's own minimal_cube v0
// (-0.577350269, -0.577350269, -0.577350269) and v6
// (0.577350269, 0.577350269, 0.577350269) -- both real, disclosed,
// mixed-and-uniform-sign authoring values -- plus a third at the exact
// lower inclusive tolerance boundary (0.99, 0.0, 0.0, lengthSquared ==
// 0.9801), proving the boundary itself survives the round-trip exactly
// too, not merely a comfortably-inside value.
constexpr std::string_view kNormalBearingSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 -0.577350269 -0.577350269 -0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 1.0 1.0 0.99 0.0 0.0\n"
    "index: 0 1 2\n";

[[nodiscard]] const float* vertexFloatsAt(const StaticMeshAssetData& data, std::size_t vertexIndex) {
  return reinterpret_cast<const float*>(data.vertexBytes().data() + vertexIndex * data.vertexStrideBytes());
}

}  // namespace

TEST_CASE("A normal value written in a real authoring source reaches loadStaticMeshAsset()'s own StaticMeshAssetData "
          "bit-for-bit, including a real, mixed-sign value and the exact lower tolerance boundary, with no clamp, "
          "no flip, and no normalization",
          "[asset_system]") {
  TempDirGuard dir("normal_round_trip");
  const fs::path sourcePath = dir.path / "meshes" / "normal_bearing.mesh.txt";
  writeFile(sourcePath, std::string(kNormalBearingSource));

  const fs::path artifactPath = dir.path / "out" / "normal_bearing.amesh";
  const fs::path metadataPath = dir.path / "out" / "normal_bearing.amesh.meta.txt";

  const auto cookResult = cookStaticMesh(sourcePath.string(), "meshes/normal_bearing.mesh.txt", artifactPath.string(),
                                          metadataPath.string());
  REQUIRE(cookResult.isOk());

  const auto loadResult = loadStaticMeshAsset(artifactPath.string(), metadataPath.string());
  REQUIRE(loadResult.isOk());
  const StaticMeshAssetData& data = loadResult.value();

  REQUIRE(data.vertexStrideBytes() == 44);
  REQUIRE(data.vertexCount() == 3);

  // Normal occupies float index 8/9/10 of each 44-byte vertex's own 11
  // floats (offset 32), appended after UV0 (float index 6/7).
  const float* v0 = vertexFloatsAt(data, 0);
  const float* v1 = vertexFloatsAt(data, 1);
  const float* v2 = vertexFloatsAt(data, 2);

  CHECK(v0[8] == -0.577350269f);
  CHECK(v0[9] == -0.577350269f);
  CHECK(v0[10] == -0.577350269f);
  CHECK(v1[8] == 0.577350269f);
  CHECK(v1[9] == 0.577350269f);
  CHECK(v1[10] == 0.577350269f);
  // The exact lower inclusive tolerance boundary (Spec 0020 D3) also
  // survives exactly, bit-for-bit -- not merely rounded to "close
  // enough".
  CHECK(v2[8] == 0.99f);
  CHECK(v2[9] == 0.0f);
  CHECK(v2[10] == 0.0f);

  // Position/color/UV0 are unaffected by normal's own presence --
  // confirms no flip or reordering was introduced anywhere in the same
  // change.
  CHECK(v1[0] == 1.0f);  // positionX
  CHECK(v1[4] == 1.0f);  // colorG
  CHECK(v1[6] == 1.0f);  // uvU
  CHECK(v1[7] == 0.0f);  // uvV
}
