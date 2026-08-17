#include "support/golden_validity.h"
#include "support/pixel_diff.h"
#include "support/png_codec.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// This file never defines stb's own write-implementation macro --
// support/png_codec.cpp is the ONE translation unit in this repository
// that does (ADR-0041).
#include <stb_image_write.h>

using atlantis::image_regression::encodePng;
using atlantis::image_regression::GoldenValidityError;
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::writeFailureArtifacts;

namespace {

// Removes its listed paths (or, for TempDirGuard, an entire directory
// tree) at scope exit -- including via Catch2's own REQUIRE-failure
// stack unwinding (a genuine C++ exception -- this project never
// disables exceptions) -- so a failing test case still cleans up its
// own temp fixtures, not only a fully-passing one.
struct TempFilesGuard {
  std::vector<std::filesystem::path> paths;
  ~TempFilesGuard() {
    for (const auto& path : paths) std::filesystem::remove(path);
  }
};

struct TempDirGuard {
  std::filesystem::path dir;
  ~TempDirGuard() { std::filesystem::remove_all(dir); }
};

[[nodiscard]] std::filesystem::path tempDirForThisTest() {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "atlantis_image_regression_golden_validity_test";
  std::filesystem::create_directories(dir);
  return dir;
}

[[nodiscard]] PixelBuffer makeValidGoldenPixels() {
  PixelBuffer buffer;
  buffer.width = 2;
  buffer.height = 2;
  buffer.rgba8 = {10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
  return buffer;
}

[[nodiscard]] std::string wellFormedSidecarFor(std::uint32_t width, std::uint32_t height, const std::string& format) {
  return "schema_version: 1\n"
         "capture_date: 2026-08-17T00:00:00Z\n"
         "source_revision: 217db1a30c0934c66afa1dfbba8fdbfbe60fea67\n"
         "gpu_vendor: Intel\n"
         "gpu_model: Intel(R) Arc(TM) B370 GPU\n"
         "driver_version: 101.8509\n"
         "os_build: Windows 11 Home, Build 26200\n"
         "vulkan_loader_api_version: 1.4.357\n"
         "vulkan_requested_instance_api_version: 1.3.0\n"
         "vulkan_physical_device_api_version: 1.4.335\n"
         "extent_width: " +
         std::to_string(width) +
         "\n"
         "extent_height: " +
         std::to_string(height) +
         "\n"
         "format: " +
         format + "\n";
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary);
  file << text;
}

}  // namespace

TEST_CASE("loadAndValidateGolden: a valid, matching golden/sidecar pair loads successfully",
          "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "valid.png";
  const std::filesystem::path sidecarPath = dir / "valid.sidecar.txt";
  const TempFilesGuard guard{{pngPath, sidecarPath}};

  REQUIRE(encodePng(pngPath, makeValidGoldenPixels()).isOk());
  writeTextFile(sidecarPath, wellFormedSidecarFor(2, 2, "Rgba8Unorm"));

  const auto result = loadAndValidateGolden(pngPath, sidecarPath);
  REQUIRE(result.isOk());
  REQUIRE(result.value().pixels.width == 2);
  REQUIRE(result.value().pixels.height == 2);
  REQUIRE(result.value().provenance.format == "Rgba8Unorm");
}

TEST_CASE("loadAndValidateGolden: MissingPngFile when the PNG does not exist", "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "does_not_exist.png";
  const std::filesystem::path sidecarPath = dir / "does_not_exist.sidecar.txt";

  const auto result = loadAndValidateGolden(pngPath, sidecarPath);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::MissingPngFile);
}

TEST_CASE("loadAndValidateGolden: MissingSidecarFile when the PNG exists but the sidecar does not",
          "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "png_only.png";
  const std::filesystem::path sidecarPath = dir / "png_only.sidecar.txt";
  const TempFilesGuard guard{{pngPath, sidecarPath}};

  REQUIRE(encodePng(pngPath, makeValidGoldenPixels()).isOk());

  const auto result = loadAndValidateGolden(pngPath, sidecarPath);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::MissingSidecarFile);
}

TEST_CASE("loadAndValidateGolden: PngDecodeFailed for a corrupt PNG file", "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "corrupt.png";
  const std::filesystem::path sidecarPath = dir / "corrupt.sidecar.txt";
  const TempFilesGuard guard{{pngPath, sidecarPath}};

  writeTextFile(pngPath, "this is not a PNG file");
  writeTextFile(sidecarPath, wellFormedSidecarFor(2, 2, "Rgba8Unorm"));

  const auto result = loadAndValidateGolden(pngPath, sidecarPath);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::PngDecodeFailed);
}

TEST_CASE("loadAndValidateGolden: ChannelCountMismatch for a real 3-channel PNG",
          "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "three_channel.png";
  const std::filesystem::path sidecarPath = dir / "three_channel.sidecar.txt";
  const TempFilesGuard guard{{pngPath, sidecarPath}};

  const std::vector<std::uint8_t> rgb(2 * 2 * 3, 128);
  REQUIRE(stbi_write_png(pngPath.string().c_str(), 2, 2, 3, rgb.data(), 2 * 3) != 0);
  writeTextFile(sidecarPath, wellFormedSidecarFor(2, 2, "Rgba8Unorm"));

  const auto result = loadAndValidateGolden(pngPath, sidecarPath);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::ChannelCountMismatch);
}

TEST_CASE("loadAndValidateGolden: SidecarFormatExtentMismatch when the sidecar's recorded extent disagrees",
          "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "extent_mismatch.png";
  const std::filesystem::path sidecarPath = dir / "extent_mismatch.sidecar.txt";
  const TempFilesGuard guard{{pngPath, sidecarPath}};

  REQUIRE(encodePng(pngPath, makeValidGoldenPixels()).isOk());
  writeTextFile(sidecarPath, wellFormedSidecarFor(999, 999, "Rgba8Unorm"));

  const auto result = loadAndValidateGolden(pngPath, sidecarPath);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::SidecarFormatExtentMismatch);
}

TEST_CASE("loadAndValidateGolden: SidecarFormatExtentMismatch for a structurally mismatched sidecar/PNG pairing",
          "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "pairing_a.png";
  // Correctly formed as a sidecar file, but its name does not correspond
  // to pngPath's own stem ("pairing_a") -- a caller-supplied mismatched
  // pair, which is what this step catches.
  const std::filesystem::path wrongSidecarPath = dir / "pairing_b.sidecar.txt";
  const TempFilesGuard guard{{pngPath, wrongSidecarPath}};

  REQUIRE(encodePng(pngPath, makeValidGoldenPixels()).isOk());
  writeTextFile(wrongSidecarPath, wellFormedSidecarFor(2, 2, "Rgba8Unorm"));

  const auto result = loadAndValidateGolden(pngPath, wrongSidecarPath);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::SidecarFormatExtentMismatch);
}

TEST_CASE("loadAndValidateGolden: SidecarMalformed for an internally-inconsistent sidecar",
          "[image_regression][golden_validity]") {
  const std::filesystem::path dir = tempDirForThisTest();
  const std::filesystem::path pngPath = dir / "malformed_sidecar.png";
  const std::filesystem::path sidecarPath = dir / "malformed_sidecar.sidecar.txt";
  const TempFilesGuard guard{{pngPath, sidecarPath}};

  REQUIRE(encodePng(pngPath, makeValidGoldenPixels()).isOk());
  writeTextFile(sidecarPath, "not a valid sidecar\n");

  const auto result = loadAndValidateGolden(pngPath, sidecarPath);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == GoldenValidityError::SidecarMalformed);
}

TEST_CASE("writeFailureArtifacts: produces two correctly-named, decodable PNG files",
          "[image_regression][golden_validity]") {
  const std::filesystem::path outputDir = tempDirForThisTest() / "failure_artifacts_output";
  const TempDirGuard dirGuard{outputDir};
  const std::string goldenSlug = "test_slug";

  PixelBuffer actual = makeValidGoldenPixels();
  actual.rgba8[0] = 255;  // deliberately differ from golden
  const PixelBuffer golden = makeValidGoldenPixels();

  const auto result = writeFailureArtifacts(outputDir, goldenSlug, actual, golden);
  REQUIRE(result.isOk());

  const std::filesystem::path actualPath = outputDir / (goldenSlug + "_actual.png");
  const std::filesystem::path diffPath = outputDir / (goldenSlug + "_diff.png");
  REQUIRE(std::filesystem::exists(actualPath));
  REQUIRE(std::filesystem::exists(diffPath));

  REQUIRE(atlantis::image_regression::decodePng(actualPath).isOk());
  REQUIRE(atlantis::image_regression::decodePng(diffPath).isOk());
}
