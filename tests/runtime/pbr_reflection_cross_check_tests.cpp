// Plan 0023 Milestone 7: real Slang reflection JSON cross-checked
// against the C++ PbrPushConstants/camera-buffer structs, matching
// Plan 0022's own independent-cross-check precedent -- never a shared
// literal trusted from one side alone. GPU-independent: no Device, no
// GPU, no real window -- only a real slangc invocation (a build tool,
// already required, ADR-0025/Plan 0008) and file I/O.

#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

// PbrPushConstants is a PRIVATE Renderer header (src/renderer/src/,
// found and corrected during Plan 0023's own final review -- see that
// header's own comment for why), reached here the same way
// tests/shader_system/json_parser_tests.cpp already reaches
// shader_system's own private json_parser.h: a relative path, never a
// new target_include_directories() entry and never promoting it to
// Renderer's own public include/.
#include "../../src/renderer/src/pbr_push_constants.h"

using atlantis::renderer::PbrPushConstants;
using atlantis::runtime::CameraWorldPositionData;
using atlantis::runtime::FrameLightingData;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::PushConstantRange;
using atlantis::shader_system::ShaderStage;

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::optional<std::string> readWholeFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return std::nullopt;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

struct FieldLayout {
  long offset = -1;
  long size = -1;
};

// A minimal, test-only, purpose-built extractor -- deliberately NOT a
// general JSON parser (this file never links Atlantis::ShaderSystem's
// own private json_parser.h/json_value.h, which are src/-private and
// not part of that module's public API). Finds the first `"fieldName"`
// key in the raw text, then the immediately-following `"offset": <int>`
// and `"size": <int>` tokens -- sufficient for this file's own narrow
// need (a handful of known field names in real, machine-generated,
// deterministically-ordered slangc `-reflection-json` output,
// confirmed directly against a real probe during Implementation), never
// intended as a reusable JSON facility.
[[nodiscard]] std::optional<FieldLayout> findFieldLayout(const std::string& json, const std::string& fieldName) {
  const std::string key = "\"" + fieldName + "\"";
  const std::size_t namePos = json.find(key);
  if (namePos == std::string::npos) return std::nullopt;

  const std::size_t offsetKeyPos = json.find("\"offset\"", namePos);
  const std::size_t sizeKeyPos = json.find("\"size\"", namePos);
  if (offsetKeyPos == std::string::npos || sizeKeyPos == std::string::npos) return std::nullopt;

  const auto parseIntAfterColon = [&json](std::size_t keyPos) -> long {
    const std::size_t colon = json.find(':', keyPos);
    return std::stol(json.substr(colon + 1));
  };
  return FieldLayout{parseIntAfterColon(offsetKeyPos), parseIntAfterColon(sizeKeyPos)};
}

// Invokes slangc directly (std::system(), test-only -- production code
// never invokes a subprocess this way, see process_launch.h/.cpp) to
// re-generate a real, fresh, raw reflection JSON for one stage of the
// CURRENT pbr_direct_lit.slang source -- never a stale, committed
// golden JSON. Every path is double-quoted; std::system() dispatches
// through cmd.exe on Windows, which requires the quoting below to
// parse correctly when any path contains a space.
[[nodiscard]] bool runSlangcReflectionJson(const std::string& entryName, const std::string& stage,
                                            const fs::path& outputJsonPath, const fs::path& outputSpirvPath) {
  const std::string innerCommand = "\"" + std::string(ATLANTIS_SLANGC_EXECUTABLE) + "\" \"" +
                                    std::string(ATLANTIS_PBR_DIRECT_LIT_SLANG_SOURCE) + "\" -entry " + entryName +
                                    " -stage " + stage + " -target spirv -reflection-json \"" +
                                    outputJsonPath.string() + "\" -o \"" + outputSpirvPath.string() + "\"";
  // std::system() dispatches through `cmd.exe /c <command>` on Windows;
  // when <command> both starts AND ends with a quote (true here: the
  // slangc path's own opening quote, the spirv output path's own
  // closing quote), cmd.exe's own quote-stripping heuristic strips
  // those two -- the WRONG pair, since they are not actually the
  // command's own outer delimiters -- corrupting the parse ("invalid
  // file/directory/volume syntax"). The standard, documented Windows
  // workaround: wrap the whole command in one MORE, redundant pair of
  // quotes, so cmd.exe strips that harmless outer pair instead, leaving
  // innerCommand's own quoting intact.
  const std::string command = "\"" + innerCommand + "\"";
  const int rc = std::system(command.c_str());
  return rc == 0 && fs::exists(outputJsonPath);
}

}  // namespace

TEST_CASE("PbrPushConstants: real Slang reflection (both stages, ATLANTIS's own transformed schema) reports "
          "exactly {offset:0, size:96}, matching sizeof(PbrPushConstants)",
          "[shader_system][runtime][pbr][reflection]") {
  static_assert(sizeof(PbrPushConstants) == 96);

  auto vertexResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) +
                                              "/pbr_direct_lit.vert.refl.json");
  REQUIRE(vertexResult.isOk());
  const std::vector<PushConstantRange> expectedVertex = {
      PushConstantRange{.offsetBytes = 0, .sizeBytes = sizeof(PbrPushConstants), .stage = ShaderStage::Vertex}};
  REQUIRE(vertexResult.value().pushConstantRanges == expectedVertex);

  auto fragmentResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) +
                                                "/pbr_direct_lit.frag.refl.json");
  REQUIRE(fragmentResult.isOk());
  const std::vector<PushConstantRange> expectedFragment = {
      PushConstantRange{.offsetBytes = 0, .sizeBytes = sizeof(PbrPushConstants), .stage = ShaderStage::Fragment}};
  REQUIRE(fragmentResult.value().pushConstantRanges == expectedFragment);
}

TEST_CASE("Camera/Lighting/CameraWorldPosition buffer: a real, freshly-generated slangc reflection of "
          "pbr_direct_lit.slang's own CameraUniform struct reports cameraWorldPosition/_pad2 at exactly the "
          "offsets Milestone 2's own C++ layout expects, totalling 320 bytes",
          "[shader_system][runtime][pbr][reflection]") {
  // Independently re-derives the 320-byte total from first principles
  // (never trusting Milestone 2's own literal) -- matching
  // runtime_smoke_gpu_tests.cpp's own kCameraWorldPositionByteOffset
  // precedent.
  constexpr long kExpectedCameraWorldPositionOffset = 2 * 16 * 4 + sizeof(FrameLightingData);  // 304
  constexpr long kExpectedTotalSize =
      kExpectedCameraWorldPositionOffset + sizeof(CameraWorldPositionData);  // 320
  static_assert(kExpectedCameraWorldPositionOffset == 304);
  static_assert(kExpectedTotalSize == 320);

  const fs::path outputDir = fs::temp_directory_path() / "atlantis_pbr_reflection_cross_check_tests";
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  const fs::path jsonPath = outputDir / "pbr_direct_lit_vert_raw_refl.json";
  const fs::path spirvPath = outputDir / "pbr_direct_lit_vert_raw.spv";

  REQUIRE(runSlangcReflectionJson("vertexMain", "vertex", jsonPath, spirvPath));
  const auto jsonText = readWholeFile(jsonPath);
  REQUIRE(jsonText.has_value());

  const auto cameraWorldPosition = findFieldLayout(*jsonText, "cameraWorldPosition");
  REQUIRE(cameraWorldPosition.has_value());
  CHECK(cameraWorldPosition->offset == kExpectedCameraWorldPositionOffset);
  CHECK(cameraWorldPosition->size == 12);  // float3, 12 bytes

  const auto pad2 = findFieldLayout(*jsonText, "_pad2");
  REQUIRE(pad2.has_value());
  CHECK(pad2->offset == kExpectedCameraWorldPositionOffset + 12);  // 316
  CHECK(pad2->size == 4);
  // The real reflected total block size -- the tail field's own
  // offset + size, never assumed equal to the C++ side's own sizeof
  // without this real cross-check.
  CHECK(pad2->offset + pad2->size == kExpectedTotalSize);

  fs::remove_all(outputDir, ec);
}
