#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/load.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

// Plan 0017 Section D9/V9: a dedicated CPU round-trip test proving a UV
// value written in a real authoring source reaches loadStaticMeshAsset()'s
// own returned StaticMeshAssetData bit-for-bit -- through the real
// cookStaticMesh() -> loadStaticMeshAsset() path, not the encode/decode
// unit tests in isolation -- with no clamp and no flip. Includes at
// least one UV value outside [0, 1].

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_system_mesh_uv_round_trip_tests" /
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

// One vertex with a UV pair deliberately outside [0, 1] (2.5, -3.25),
// one at the origin, and one at (1, 1) -- three real, distinct UV
// values, not a single repeated placeholder.
constexpr std::string_view kUvBearingSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 2.5 -3.25 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 1.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

[[nodiscard]] const float* vertexFloatsAt(const StaticMeshAssetData& data, std::size_t vertexIndex) {
  return reinterpret_cast<const float*>(data.vertexBytes().data() + vertexIndex * data.vertexStrideBytes());
}

}  // namespace

TEST_CASE("A UV value written in a real authoring source reaches loadStaticMeshAsset()'s own StaticMeshAssetData "
          "bit-for-bit, including a value outside [0, 1], with no clamp and no flip",
          "[asset_system]") {
  TempDirGuard dir("uv_round_trip");
  const fs::path sourcePath = dir.path / "meshes" / "uv_bearing.mesh.txt";
  writeFile(sourcePath, std::string(kUvBearingSource));

  const fs::path artifactPath = dir.path / "out" / "uv_bearing.amesh";
  const fs::path metadataPath = dir.path / "out" / "uv_bearing.amesh.meta.txt";

  const auto cookResult =
      cookStaticMesh(sourcePath.string(), "meshes/uv_bearing.mesh.txt", artifactPath.string(), metadataPath.string());
  REQUIRE(cookResult.isOk());

  const auto loadResult = loadStaticMeshAsset(artifactPath.string(), metadataPath.string());
  REQUIRE(loadResult.isOk());
  const StaticMeshAssetData& data = loadResult.value();

  REQUIRE(data.vertexStrideBytes() == 44);
  REQUIRE(data.vertexCount() == 3);

  // UV0 occupies float index 6/7 of each 44-byte vertex's own 11 floats
  // (offset 24) -- unaffected by Plan 0020's own normal field, which is
  // appended after UV0 (float index 8/9/10, offset 32), not inserted
  // before it.
  const float* v0 = vertexFloatsAt(data, 0);
  const float* v1 = vertexFloatsAt(data, 1);
  const float* v2 = vertexFloatsAt(data, 2);

  CHECK(v0[6] == 0.0f);
  CHECK(v0[7] == 0.0f);
  // The real, load-bearing assertion: a UV value outside [0, 1]
  // survives exactly, bit-for-bit -- proving no clamp is applied
  // anywhere in the authoring -> cook -> artifact -> load path.
  CHECK(v1[6] == 2.5f);
  CHECK(v1[7] == -3.25f);
  CHECK(v2[6] == 1.0f);
  CHECK(v2[7] == 1.0f);

  // Position/color are unaffected by UV0's own presence -- confirms no
  // flip or reordering was introduced anywhere in the same change.
  CHECK(v1[0] == 1.0f);  // positionX
  CHECK(v1[4] == 1.0f);  // colorG
}
