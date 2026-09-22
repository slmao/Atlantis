#include "fixture/index_type_meshes.h"
#include "fixture/minimal_cube_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/types.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>

// Spec 0039 T2/T3 (Plan 0039 Milestone 3): the two GPU-side proofs that
// index-type parameterization works and costs nothing visually.
//
// T2 renders one authored cube twice -- once from a schema-4 (uint16)
// artifact and once from a schema-5 (uint32) one -- and requires the two
// frames to be bit-identical. It needs no golden: the comparison is
// between two live frames, the precedent being lighting_demo_gpu_tests'
// own correct-versus-wrong comparison. ADR-0042's tolerances are
// non-configurable and are used here at their zero setting, so a
// non-zero difference is a design finding to report, never a tolerance
// to widen (Plan 0039's own risk gate).
//
// T3 draws a 362x362 grid -- 131,044 vertices, over twice what a 16-bit
// index can address -- through the same fixture, and checks both its
// golden and, directly, that the half of the grid drawn from indices
// above 65,535 actually reaches the framebuffer.

namespace fs = std::filesystem;

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::kFixtureExtentPixels;
using atlantis::image_regression::kLargeIndexGridHighBandFirstVertex;
using atlantis::image_regression::kLargeIndexGridIndexCount;
using atlantis::image_regression::kLargeIndexGridVertexCount;
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::MeshArtifactPaths;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderOneFrame;
using atlantis::image_regression::setUpMinimalCubeFixtureFromAsset;
using atlantis::image_regression::writeEquivalenceCubeV4;
using atlantis::image_regression::writeEquivalenceCubeV5;
using atlantis::image_regression::writeFailureArtifacts;
using atlantis::image_regression::writeLargeIndexGridV5;

namespace {

constexpr const char* kLargeGridGoldenName = "index_type_large_grid/index_type_large_grid_512x512_rgba8unorm";
constexpr const char* kLargeGridGoldenSlug = "index_type_large_grid_512x512_rgba8unorm";

[[nodiscard]] fs::path goldenPngPath(const std::string& goldenName) {
  return fs::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".png");
}

[[nodiscard]] fs::path goldenSidecarPath(const std::string& goldenName) {
  return fs::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".sidecar.txt");
}

// Per-process tag: catch_discover_tests runs each TEST_CASE in its own
// process under ctest -j, and the counter below restarts at 0 in each.
const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_index_type_gpu_tests" /
             (label + "_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

[[nodiscard]] PixelBuffer renderArtifact(const MeshArtifactPaths& paths) {
  auto fixture = setUpMinimalCubeFixtureFromAsset(paths.artifactPath.c_str(), paths.metadataPath.c_str());
  REQUIRE(fixture.isOk());
  auto rendered = renderOneFrame(fixture.value());
  REQUIRE(rendered.isOk());
  return std::move(rendered.value());
}

struct ChannelDominanceCounts {
  std::uint64_t redDominant = 0;
  std::uint64_t blueDominant = 0;
};

// A frame that is uniformly the clear colour would compare equal to any
// other such frame and prove nothing, so every comparison here is
// paired with this: the frame must actually contain more than one
// colour.
[[nodiscard]] bool frameHasVaryingColour(const PixelBuffer& frame) {
  if (frame.rgba8.size() < 8) return false;
  for (std::size_t offset = 4; offset + 3 < frame.rgba8.size(); offset += 4) {
    if (frame.rgba8[offset] != frame.rgba8[0] || frame.rgba8[offset + 1] != frame.rgba8[1] ||
        frame.rgba8[offset + 2] != frame.rgba8[2]) {
      return true;
    }
  }
  return false;
}

// The grid's two colour bands are pure red and pure blue, so a
// dominance test needs no tolerance calibration: a lit or blended pixel
// still lands on the side its vertex colour came from.
[[nodiscard]] ChannelDominanceCounts countBands(const PixelBuffer& frame) {
  ChannelDominanceCounts counts;
  for (std::size_t offset = 0; offset + 3 < frame.rgba8.size(); offset += 4) {
    const int red = frame.rgba8[offset];
    const int green = frame.rgba8[offset + 1];
    const int blue = frame.rgba8[offset + 2];
    if (red > 40 && red > blue * 2 && red > green * 2) ++counts.redDominant;
    if (blue > 40 && blue > red * 2 && blue > green * 2) ++counts.blueDominant;
  }
  return counts;
}

}  // namespace

TEST_CASE("A schema-4 and a schema-5 encoding of one mesh render bit-identical frames", "[gpu]") {
  TempDirGuard dir("equivalence");
  const MeshArtifactPaths v4 = writeEquivalenceCubeV4(dir.path);
  const MeshArtifactPaths v5 = writeEquivalenceCubeV5(dir.path);

  const PixelBuffer v4Frame = renderArtifact(v4);
  const PixelBuffer v5Frame = renderArtifact(v5);

  REQUIRE(v4Frame.width == kFixtureExtentPixels);
  REQUIRE(v4Frame.height == kFixtureExtentPixels);
  REQUIRE(v5Frame.width == v4Frame.width);
  REQUIRE(v5Frame.height == v4Frame.height);

  const auto report = compareBuffers(v5Frame, v4Frame);
  INFO("maxChannelDiff=" << report.maxChannelDiff << " outOfTolerance=" << report.outOfToleranceCount);
  CHECK(report.maxChannelDiff == 0);
  CHECK(report.outOfToleranceCount == 0);
  CHECK(report.passed);

  // Both renders actually drew the cube: a frame that were uniformly
  // the clear colour would compare equal to another such frame and
  // prove nothing at all.
  CHECK(frameHasVaryingColour(v4Frame));
  CHECK(frameHasVaryingColour(v5Frame));
}

TEST_CASE("A 362x362 uint32 mesh draws the half of itself addressed above the uint16 ceiling", "[gpu]") {
  TempDirGuard dir("large_index_grid");
  const MeshArtifactPaths grid = writeLargeIndexGridV5(dir.path);

  // The mesh really is past the ceiling, and by the margin ruling Q2
  // asked for rather than the few rows a minimal grid would give.
  STATIC_REQUIRE(kLargeIndexGridVertexCount == 131044);
  STATIC_REQUIRE(kLargeIndexGridIndexCount == 781926);
  STATIC_REQUIRE(kLargeIndexGridVertexCount > kLargeIndexGridHighBandFirstVertex);
  // 65,508 of 131,044 vertices are above the ceiling -- 49.99%, the
  // "half the rows" margin ruling Q2 asked for, not a few rows.
  STATIC_REQUIRE(kLargeIndexGridVertexCount - kLargeIndexGridHighBandFirstVertex == 65508);
  STATIC_REQUIRE((kLargeIndexGridVertexCount - kLargeIndexGridHighBandFirstVertex) * 100 >
                 kLargeIndexGridVertexCount * 49);

  const PixelBuffer frame = renderArtifact(grid);
  REQUIRE(frame.width == kFixtureExtentPixels);
  REQUIRE(frame.height == kFixtureExtentPixels);

  // The truncation assertion, independent of any golden: the blue band
  // is drawn only from vertex indices at or above 65,536. Under a
  // 16-bit index those wrap to low indices and collapse onto the grid's
  // red corner, so blue would vanish while red survived. Both bands are
  // roughly half the grid, so neither can be a stray pixel.
  const auto bands = countBands(frame);
  INFO("redDominant=" << bands.redDominant << " blueDominant=" << bands.blueDominant);
  CHECK(bands.redDominant > 1000);
  CHECK(bands.blueDominant > 1000);

  // Rendering is deterministic for the same fixture and mesh.
  const PixelBuffer again = renderArtifact(grid);
  CHECK(compareBuffers(again, frame).maxChannelDiff == 0);

  auto golden = loadAndValidateGolden(goldenPngPath(kLargeGridGoldenName), goldenSidecarPath(kLargeGridGoldenName));
  {
    INFO("INVALID GOLDEN: the committed large-index-grid golden must load and validate cleanly");
    REQUIRE(golden.isOk());
  }
  REQUIRE(frame.width == golden.value().pixels.width);
  REQUIRE(frame.height == golden.value().pixels.height);

  const auto report = compareBuffers(frame, golden.value().pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR), kLargeGridGoldenSlug,
                                frame, golden.value().pixels);
  }
  INFO("maxChannelDiff=" << report.maxChannelDiff << " outOfTolerance=" << report.outOfToleranceCount);
  CHECK(report.passed);
}

TEST_CASE("createMesh's uint32 overload builds a four-bytes-per-index Uint32 index Buffer", "[gpu]") {
  // Plan 0039 Milestone 3.3 asked for this GPU-independent, against a
  // recording fake device. There is no fake rhi::Device in the tree and
  // building one means faking eleven pure virtuals and seven further
  // resource types for a two-line assertion, so it is asserted here
  // instead, against the real Device the fixture already creates --
  // strictly stronger evidence, since it is the Buffer the backend
  // actually binds. Disclosed as a deviation in the PR.
  TempDirGuard dir("buffer_shape");
  const MeshArtifactPaths v4 = writeEquivalenceCubeV4(dir.path);
  const MeshArtifactPaths v5 = writeEquivalenceCubeV5(dir.path);

  auto v4Fixture = setUpMinimalCubeFixtureFromAsset(v4.artifactPath.c_str(), v4.metadataPath.c_str());
  auto v5Fixture = setUpMinimalCubeFixtureFromAsset(v5.artifactPath.c_str(), v5.metadataPath.c_str());
  REQUIRE(v4Fixture.isOk());
  REQUIRE(v5Fixture.isOk());
  REQUIRE(v4Fixture.value().mesh.has_value());
  REQUIRE(v5Fixture.value().mesh.has_value());

  const atlantis::rhi::Buffer& v4Indices = v4Fixture.value().mesh->indexBuffer();
  const atlantis::rhi::Buffer& v5Indices = v5Fixture.value().mesh->indexBuffer();

  CHECK(v4Indices.indexType() == atlantis::rhi::IndexType::Uint16);
  CHECK(v5Indices.indexType() == atlantis::rhi::IndexType::Uint32);
  CHECK(v4Fixture.value().mesh->indexCount() == 36);
  CHECK(v5Fixture.value().mesh->indexCount() == 36);
  CHECK(v4Indices.sizeBytes() == 36 * sizeof(std::uint16_t));
  CHECK(v5Indices.sizeBytes() == 36 * sizeof(std::uint32_t));
}
