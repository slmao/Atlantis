#include "fixture/minimal_cube_fixture.h"
#include "support/pixel_diff.h"

#include "import_command.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// Spec 0039 T6 (Plan 0039 Milestone 3.7): the real-data half of this
// workflow's proof. bistro_mesh_246_0 is the largest primitive the glTF
// importer produces -- 127,104 vertices, nearly twice what a 16-bit
// index can address -- and until this Spec it could be decoded but not
// drawn (Plan 0037 Ruling 5).
//
// Opt-in, exactly like the importer's own [bistro] tests: the content is
// a ~2.2 GB fetch that is never committed, so this SKIPs rather than
// FAILs when it is absent (Spec 0037's content policy). It is a
// non-degeneracy check, not a golden: the frame depends on content
// fetched from an upstream repository, which is not a reference
// environment ADR-0042 goldens may rest on.

namespace fs = std::filesystem;

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::kFixtureExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderOneFrame;
using atlantis::image_regression::setUpMinimalCubeFixtureFromAsset;

namespace {

// The importer writes meshes as artifacts directly (ADR-0083 D1), so no
// cooker step stands between the import and this render.
constexpr const char* kLargestMeshStem = "bistro_mesh_246_0";
constexpr std::uint32_t kLargestMeshVertexCount = 127104;

[[nodiscard]] std::uint64_t nonClearPixelCount(const PixelBuffer& frame) {
  // The fixture clears to its own fixed background; any pixel that is
  // meaningfully brighter than that came from the mesh.
  std::uint64_t count = 0;
  for (std::size_t offset = 0; offset + 3 < frame.rgba8.size(); offset += 4) {
    const int red = frame.rgba8[offset];
    const int green = frame.rgba8[offset + 1];
    const int blue = frame.rgba8[offset + 2];
    if (red + green + blue > 90) ++count;
  }
  return count;
}

}  // namespace

TEST_CASE("The largest real imported Bistro mesh renders through the uint32 index path", "[bistro]") {
  const fs::path content{ATLANTIS_BISTRO_CONTENT_DIR};
  if (!fs::exists(content / "bistro.gltf")) {
    SKIP("Bistro content not found at " << content.string() << " -- run tools/content/fetch_bistro.ps1");
  }

  const fs::path work = fs::temp_directory_path() / "atlantis_index_type_gpu_tests" / "bistro_large_mesh";
  fs::remove_all(work);
  fs::create_directories(work);
  const fs::path importDir = work / "import";

  const auto imported = atlantis::gltf_importer::importGltf(content / "bistro.gltf", content, importDir, "bistro");
  REQUIRE(imported.isOk());
  CHECK(imported.value().meshCount == 551);

  const fs::path artifactPath = importDir / (std::string(kLargestMeshStem) + ".amesh");
  const fs::path metadataPath = importDir / (std::string(kLargestMeshStem) + ".amesh.meta.txt");
  REQUIRE(fs::exists(artifactPath));

  // The whole point: loadStaticMeshAsset() accepts this schema-5
  // artifact, scene_load's own composition-root branch is exercised by
  // the same width selection this fixture performs, and the Vulkan
  // Backend binds VK_INDEX_TYPE_UINT32 because the index Buffer says so.
  auto fixture = setUpMinimalCubeFixtureFromAsset(artifactPath.string().c_str(), metadataPath.string().c_str());
  REQUIRE(fixture.isOk());
  REQUIRE(fixture.value().mesh.has_value());
  CHECK(fixture.value().mesh->indexBuffer().indexType() == atlantis::rhi::IndexType::Uint32);

  auto rendered = renderOneFrame(fixture.value());
  REQUIRE(rendered.isOk());
  const PixelBuffer frame = std::move(rendered.value());
  REQUIRE(frame.width == kFixtureExtentPixels);
  REQUIRE(frame.height == kFixtureExtentPixels);

  // Non-degenerate: the mesh is authored at Bistro's own world scale, so
  // the fixture's fixed camera sits inside it and the frame is expected
  // to be largely covered rather than empty. Either way, a frame that is
  // entirely the clear colour would mean nothing was drawn.
  const std::uint64_t covered = nonClearPixelCount(frame);
  INFO("non-clear pixels: " << covered);
  CHECK(covered > 0);

  // Deterministic across two renders of the same mesh.
  auto second = renderOneFrame(fixture.value());
  REQUIRE(second.isOk());
  CHECK(compareBuffers(second.value(), frame).maxChannelDiff == 0);

  CHECK(fixture.value().device->waitIdle().isOk());
  CHECK(kLargestMeshVertexCount > 65535);

  fs::remove_all(work);
}
