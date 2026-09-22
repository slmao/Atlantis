// Plan 0023 Milestone 7: real Slang reflection JSON cross-checked
// against the C++ PbrPushConstants/camera-buffer structs, matching
// Plan 0022's own independent-cross-check precedent -- never a shared
// literal trusted from one side alone. GPU-independent: no Device, no
// GPU, no real window -- only a real slangc invocation (a build tool,
// already required, ADR-0025/Plan 0008) and file I/O.

#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <cstddef>
#include <cstdlib>
#include <iterator>
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
#include "../../src/renderer/src/pbr_anisotropic_push_constants.h"
#include "../../src/renderer/src/pbr_clearcoat_push_constants.h"
#include "../../src/renderer/src/pbr_push_constants.h"
#include "../../src/renderer/src/pbr_sheen_push_constants.h"

using atlantis::renderer::PbrAnisotropicPushConstants;
using atlantis::renderer::PbrClearcoatPushConstants;
using atlantis::renderer::PbrPushConstants;
using atlantis::renderer::PbrSheenPushConstants;
using atlantis::runtime::CameraWorldPositionData;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::kCameraUniformBufferSizeBytes;
using atlantis::runtime::kCameraUniformIrradianceShOffsetBytes;
using atlantis::runtime::kCameraUniformLightingOffsetBytes;
using atlantis::runtime::kCameraUniformLightSpaceOffsetBytes;
using atlantis::runtime::kCameraUniformWorldPositionOffsetBytes;
using atlantis::runtime::kMaxPointLights;
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
  long elementStride = -1;
};

// A minimal, test-only, purpose-built extractor -- deliberately NOT a
// general JSON parser (this file never links Atlantis::ShaderSystem's
// own private json_parser.h/json_value.h, which are src/-private and
// not part of that module's public API). Finds the first `"fieldName"`
// key in the raw text, then the `"offset"`/`"size"`/`"elementStride"`
// tokens of that field's own `"binding"` -- sufficient for this file's
// own narrow need (a handful of known field names in real,
// machine-generated, deterministically-ordered slangc
// `-reflection-json` output, confirmed directly against a real probe
// during Implementation), never intended as a reusable JSON facility.
//
// Plan 0040 Milestone 2: slangc emits a field's `"type"` object BEFORE
// its `"binding"`, and for a struct-array field (pointLights) that type
// object carries the element struct's own nested field bindings --
// "the first offset after the name" would then be PointLightGpu::position's
// 0, not the array's 176. The field's `"type"` object is therefore
// skipped by brace matching before its binding is read; for the scalar,
// vector and matrix fields the earlier tests read, the result is
// unchanged.
//
// Plan 0041 Milestone 2: searchFrom starts the search inside one
// parameter (e.g. just after "pushConstants"), so a field name that also
// occurs earlier in the document -- elementVarLayout, which every
// constant buffer has -- resolves to that parameter's own.
[[nodiscard]] std::optional<FieldLayout> findFieldLayout(const std::string& json, const std::string& fieldName,
                                                         std::size_t searchFrom = 0) {
  const std::string key = "\"" + fieldName + "\"";
  const std::size_t namePos = json.find(key, searchFrom);
  if (namePos == std::string::npos) return std::nullopt;

  const std::size_t typeKeyPos = json.find("\"type\"", namePos);
  if (typeKeyPos == std::string::npos) return std::nullopt;
  std::size_t cursor = json.find('{', typeKeyPos);
  if (cursor == std::string::npos) return std::nullopt;
  int depth = 0;
  for (; cursor < json.size(); ++cursor) {
    if (json[cursor] == '{') ++depth;
    if (json[cursor] == '}' && --depth == 0) break;
  }
  if (cursor >= json.size()) return std::nullopt;

  const std::size_t bindingPos = json.find("\"binding\"", cursor);
  if (bindingPos == std::string::npos) return std::nullopt;
  const std::size_t offsetKeyPos = json.find("\"offset\"", bindingPos);
  const std::size_t sizeKeyPos = json.find("\"size\"", bindingPos);
  const std::size_t strideKeyPos = json.find("\"elementStride\"", bindingPos);
  if (offsetKeyPos == std::string::npos || sizeKeyPos == std::string::npos || strideKeyPos == std::string::npos) {
    return std::nullopt;
  }

  const auto parseIntAfterColon = [&json](std::size_t keyPos) -> long {
    const std::size_t colon = json.find(':', keyPos);
    return std::stol(json.substr(colon + 1));
  };
  return FieldLayout{parseIntAfterColon(offsetKeyPos), parseIntAfterColon(sizeKeyPos),
                     parseIntAfterColon(strideKeyPos)};
}

// Invokes slangc directly (std::system(), test-only -- production code
// never invokes a subprocess this way, see process_launch.h/.cpp) to
// re-generate a real, fresh, raw reflection JSON for one stage of the
// CURRENT pbr_direct_lit.slang source -- never a stale, committed
// golden JSON. Every path is double-quoted; std::system() dispatches
// through cmd.exe on Windows, which requires the quoting below to
// parse correctly when any path contains a space.
[[nodiscard]] bool runSlangcReflectionJson(const fs::path& sourcePath, const std::string& entryName, const std::string& stage,
                                            const fs::path& outputJsonPath, const fs::path& outputSpirvPath) {
  const std::string innerCommand = "\"" + std::string(ATLANTIS_SLANGC_EXECUTABLE) + "\" \"" +
                                    sourcePath.string() + "\" -entry " + entryName +
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
          "exactly {offset:0, size:112}, matching sizeof(PbrPushConstants)",
          "[shader_system][runtime][pbr][reflection]") {
  static_assert(sizeof(PbrPushConstants) == 112);  // Plan 0041 Milestone 2: 96 + emissiveFactor

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
          "offsets Milestone 2's own C++ layout expects (2240 bytes through _pad2 since Plan 0040; Plan 0027 M5 appends "
          "a 144-byte explicit pad plus the 128-byte light-space tail after it, checked separately below)",
          "[shader_system][runtime][pbr][reflection]") {
  // Independently re-derives the through-_pad2 total from first principles
  // (never trusting Milestone 2's own literal) -- matching
  // runtime_smoke_gpu_tests.cpp's own kCameraWorldPositionByteOffset
  // precedent.
  // Plan 0040: 304 -> 2224 and 320 -> 2240 with the 4 -> 64 widening.
  constexpr long kExpectedCameraWorldPositionOffset = 2 * 16 * 4 + sizeof(FrameLightingData);  // 2224
  constexpr long kExpectedTotalSize =
      kExpectedCameraWorldPositionOffset + sizeof(CameraWorldPositionData);  // 2240
  static_assert(kExpectedCameraWorldPositionOffset == 2224);
  static_assert(kExpectedTotalSize == 2240);

  const fs::path outputDir = fs::temp_directory_path() / "atlantis_pbr_reflection_cross_check_tests";
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  const fs::path jsonPath = outputDir / "pbr_direct_lit_vert_raw_refl.json";
  const fs::path spirvPath = outputDir / "pbr_direct_lit_vert_raw.spv";

  REQUIRE(runSlangcReflectionJson(ATLANTIS_PBR_DIRECT_LIT_SLANG_SOURCE, "vertexMain", "vertex", jsonPath, spirvPath));
  const auto jsonText = readWholeFile(jsonPath);
  REQUIRE(jsonText.has_value());

  const auto cameraWorldPosition = findFieldLayout(*jsonText, "cameraWorldPosition");
  REQUIRE(cameraWorldPosition.has_value());
  CHECK(cameraWorldPosition->offset == kExpectedCameraWorldPositionOffset);
  CHECK(cameraWorldPosition->size == 12);  // float3, 12 bytes

  const auto pad2 = findFieldLayout(*jsonText, "_pad2");
  REQUIRE(pad2.has_value());
  CHECK(pad2->offset == kExpectedCameraWorldPositionOffset + 12);  // 2236
  CHECK(pad2->size == 4);
  // The real reflected total block size -- the tail field's own
  // offset + size, never assumed equal to the C++ side's own sizeof
  // without this real cross-check.
  CHECK(pad2->offset + pad2->size == kExpectedTotalSize);

  fs::remove_all(outputDir, ec);
}

// Plan 0040 Milestone 2: the offsets in this and the two TEST_CASEs below
// were literals (320 / 464 / 528 / 592) until the 4 -> 64 widening moved
// them by 1920 bytes; they now read the derived scene_extraction.h
// constants, so the reflected value is compared against what the C++
// writers actually use rather than against a second hand-kept number.
TEST_CASE("pbr_ibl CameraUniform appends nine float4 SH coefficients directly after cameraWorldPosition/_pad2",
          "[shader_system][runtime][pbr_ibl][reflection]") {
  const fs::path outputDir = fs::temp_directory_path() / "atlantis_pbr_ibl_reflection_cross_check_tests";
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  const fs::path jsonPath = outputDir / "pbr_ibl_vert_raw_refl.json";
  const fs::path spirvPath = outputDir / "pbr_ibl_vert_raw.spv";

  REQUIRE(runSlangcReflectionJson(ATLANTIS_PBR_IBL_SLANG_SOURCE, "vertexMain", "vertex", jsonPath, spirvPath));
  const auto jsonText = readWholeFile(jsonPath);
  REQUIRE(jsonText.has_value());
  const auto irradianceSh = findFieldLayout(*jsonText, "irradianceSh");
  REQUIRE(irradianceSh.has_value());
  CHECK(irradianceSh->offset == static_cast<long>(kCameraUniformIrradianceShOffsetBytes));  // 2240
  CHECK(irradianceSh->size == 9 * 4 * 4);
  CHECK(irradianceSh->offset + irradianceSh->size == static_cast<long>(kCameraUniformLightSpaceOffsetBytes));

  fs::remove_all(outputDir, ec);
}

// Plan 0027 Milestone 5 (ADR-0072 D-6): both shaders' own light-space
// pair lands at the identical 464-byte offset -- pbr_direct_lit.slang
// reaches it via an explicit 144-byte pad (checked here for the first
// time; pbr_ibl.slang already reaches it naturally via irradianceSh,
// confirmed by the TEST_CASE above). Real, freshly-generated reflection
// for both shaders, never a stale golden.
TEST_CASE("pbr_direct_lit CameraUniform: an explicit 144-byte pad plus the 128-byte light-space pair reach the "
          "same light-space offset pbr_ibl's own irradianceSh tail uses, totalling kCameraUniformBufferSizeBytes",
          "[shader_system][runtime][pbr][reflection]") {
  const fs::path outputDir = fs::temp_directory_path() / "atlantis_pbr_direct_lit_shadow_reflection_cross_check_tests";
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  const fs::path jsonPath = outputDir / "pbr_direct_lit_vert_raw_refl.json";
  const fs::path spirvPath = outputDir / "pbr_direct_lit_vert_raw.spv";

  REQUIRE(runSlangcReflectionJson(ATLANTIS_PBR_DIRECT_LIT_SLANG_SOURCE, "vertexMain", "vertex", jsonPath, spirvPath));
  const auto jsonText = readWholeFile(jsonPath);
  REQUIRE(jsonText.has_value());

  const auto shadowPad = findFieldLayout(*jsonText, "_shadowPad");
  REQUIRE(shadowPad.has_value());
  CHECK(shadowPad->offset == static_cast<long>(kCameraUniformIrradianceShOffsetBytes));
  CHECK(shadowPad->size == 9 * 4 * 4);  // float4[9], matching pbr_ibl's own irradianceSh size exactly
  CHECK(shadowPad->offset + shadowPad->size == static_cast<long>(kCameraUniformLightSpaceOffsetBytes));

  const auto lightSpaceView = findFieldLayout(*jsonText, "lightSpaceView");
  REQUIRE(lightSpaceView.has_value());
  CHECK(lightSpaceView->offset == static_cast<long>(kCameraUniformLightSpaceOffsetBytes));  // 2384
  CHECK(lightSpaceView->size == 64);

  const auto lightSpaceProjection = findFieldLayout(*jsonText, "lightSpaceProjection");
  REQUIRE(lightSpaceProjection.has_value());
  CHECK(lightSpaceProjection->offset == static_cast<long>(kCameraUniformLightSpaceOffsetBytes) + 64);
  CHECK(lightSpaceProjection->size == 64);
  CHECK(lightSpaceProjection->offset + lightSpaceProjection->size ==
        static_cast<long>(kCameraUniformBufferSizeBytes));  // 2512

  fs::remove_all(outputDir, ec);
}

TEST_CASE("pbr_ibl CameraUniform: the light-space pair follows irradianceSh, totalling kCameraUniformBufferSizeBytes -- "
          "the identical tail offset pbr_direct_lit's own explicit pad reaches",
          "[shader_system][runtime][pbr_ibl][reflection]") {
  const fs::path outputDir = fs::temp_directory_path() / "atlantis_pbr_ibl_shadow_reflection_cross_check_tests";
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  const fs::path jsonPath = outputDir / "pbr_ibl_vert_raw_refl.json";
  const fs::path spirvPath = outputDir / "pbr_ibl_vert_raw.spv";

  REQUIRE(runSlangcReflectionJson(ATLANTIS_PBR_IBL_SLANG_SOURCE, "vertexMain", "vertex", jsonPath, spirvPath));
  const auto jsonText = readWholeFile(jsonPath);
  REQUIRE(jsonText.has_value());

  const auto lightSpaceView = findFieldLayout(*jsonText, "lightSpaceView");
  REQUIRE(lightSpaceView.has_value());
  CHECK(lightSpaceView->offset == static_cast<long>(kCameraUniformLightSpaceOffsetBytes));
  CHECK(lightSpaceView->size == 64);

  const auto lightSpaceProjection = findFieldLayout(*jsonText, "lightSpaceProjection");
  REQUIRE(lightSpaceProjection.has_value());
  CHECK(lightSpaceProjection->offset == static_cast<long>(kCameraUniformLightSpaceOffsetBytes) + 64);
  CHECK(lightSpaceProjection->size == 64);
  CHECK(lightSpaceProjection->offset + lightSpaceProjection->size ==
        static_cast<long>(kCameraUniformBufferSizeBytes));

  fs::remove_all(outputDir, ec);
}

// Plan 0040 Milestone 2 (Spec 0040 ruling O4, mandatory): all twelve
// descriptions of the camera/lighting uniform -- the eleven .slang
// CameraUniform declarations and scene_extraction.h -- are proven to
// agree field by field, each shader reflected fresh from its own source
// by a real slangc run. The tests above keep their narrower historical
// checks on pbr_direct_lit/pbr_ibl; this is the one that makes a twelfth
// description impossible to drift silently. lit_textured's block stops
// after pointLights (it never declared the tail regions); the ten PBR
// variants all carry the full tail, two of them (pbr_direct_lit*) via
// the explicit _shadowPad twin of irradianceSh.
TEST_CASE("CameraUniform: a real slangc reflection of every one of the eleven declaring shaders matches the C++ "
          "layout constants field for field (Plan 0040 O4, 11/11)",
          "[shader_system][runtime][pbr][reflection][lighting]") {
  struct ShaderCase {
    const char* name;
    const char* shRegionField;  // nullptr: the block ends after pointLights
  };
  const ShaderCase cases[] = {
      {"lit_textured", nullptr},
      {"pbr_direct_lit", "_shadowPad"},
      {"pbr_direct_lit_normal_map", "_shadowPad"},
      {"pbr_ibl", "irradianceSh"},
      {"pbr_ibl_normal_map", "irradianceSh"},
      {"pbr_anisotropic_ibl", "irradianceSh"},
      {"pbr_anisotropic_ibl_normal_map", "irradianceSh"},
      {"pbr_clearcoat_ibl", "irradianceSh"},
      {"pbr_clearcoat_ibl_normal_map", "irradianceSh"},
      {"pbr_sheen_ibl", "irradianceSh"},
      {"pbr_sheen_ibl_normal_map", "irradianceSh"},
  };
  static_assert(std::size(cases) == 11);

  constexpr long kLighting = static_cast<long>(kCameraUniformLightingOffsetBytes);
  constexpr long kPointLightsOffset = kLighting + static_cast<long>(offsetof(FrameLightingData, pointLights));
  constexpr long kPointLightStride = sizeof(FrameLightingData::PointLightGpu);
  static_assert(kPointLightsOffset == 176);
  static_assert(kPointLightStride == 32);

  const fs::path outputDir = fs::temp_directory_path() / "atlantis_camera_uniform_11_of_11_cross_check_tests";
  std::error_code ec;
  fs::create_directories(outputDir, ec);

  int shadersChecked = 0;
  for (const ShaderCase& shader : cases) {
    INFO("shader: " << shader.name);
    const fs::path source =
        fs::path(ATLANTIS_SHADER_SOURCE_ROOT) / shader.name / (std::string(shader.name) + ".slang");
    const fs::path jsonPath = outputDir / (std::string(shader.name) + "_vert_raw_refl.json");
    const fs::path spirvPath = outputDir / (std::string(shader.name) + "_vert_raw.spv");
    REQUIRE(runSlangcReflectionJson(source, "vertexMain", "vertex", jsonPath, spirvPath));
    const auto jsonText = readWholeFile(jsonPath);
    REQUIRE(jsonText.has_value());

    const auto directionalLightCount = findFieldLayout(*jsonText, "directionalLightCount");
    const auto pointLightCount = findFieldLayout(*jsonText, "pointLightCount");
    const auto directionalLights = findFieldLayout(*jsonText, "directionalLights");
    const auto pointLights = findFieldLayout(*jsonText, "pointLights");
    REQUIRE(directionalLightCount.has_value());
    REQUIRE(pointLightCount.has_value());
    REQUIRE(directionalLights.has_value());
    REQUIRE(pointLights.has_value());
    CHECK(directionalLightCount->offset ==
          kLighting + static_cast<long>(offsetof(FrameLightingData, directionalLightCount)));
    CHECK(pointLightCount->offset == kLighting + static_cast<long>(offsetof(FrameLightingData, pointLightCount)));
    CHECK(directionalLights->offset ==
          kLighting + static_cast<long>(offsetof(FrameLightingData, directionalLights)));
    CHECK(directionalLights->size == static_cast<long>(sizeof(FrameLightingData::directionalLights)));
    CHECK(pointLights->offset == kPointLightsOffset);
    CHECK(pointLights->elementStride == kPointLightStride);
    CHECK(pointLights->size == static_cast<long>(kMaxPointLights) * kPointLightStride);  // 64 * 32 = 2048
    CHECK(pointLights->offset + pointLights->size == static_cast<long>(kCameraUniformWorldPositionOffsetBytes));

    if (shader.shRegionField == nullptr) {
      // lit_textured: the block's last field is pointLights, so its total
      // is 2224 -- and the tail fields must genuinely be absent, not
      // merely unchecked.
      CHECK_FALSE(findFieldLayout(*jsonText, "cameraWorldPosition").has_value());
      CHECK_FALSE(findFieldLayout(*jsonText, "lightSpaceProjection").has_value());
    } else {
      const auto cameraWorldPosition = findFieldLayout(*jsonText, "cameraWorldPosition");
      const auto shRegion = findFieldLayout(*jsonText, shader.shRegionField);
      const auto lightSpaceView = findFieldLayout(*jsonText, "lightSpaceView");
      const auto lightSpaceProjection = findFieldLayout(*jsonText, "lightSpaceProjection");
      REQUIRE(cameraWorldPosition.has_value());
      REQUIRE(shRegion.has_value());
      REQUIRE(lightSpaceView.has_value());
      REQUIRE(lightSpaceProjection.has_value());
      CHECK(cameraWorldPosition->offset == static_cast<long>(kCameraUniformWorldPositionOffsetBytes));  // 2224
      CHECK(shRegion->offset == static_cast<long>(kCameraUniformIrradianceShOffsetBytes));             // 2240
      CHECK(shRegion->size == 9 * 4 * 4);
      CHECK(lightSpaceView->offset == static_cast<long>(kCameraUniformLightSpaceOffsetBytes));         // 2384
      CHECK(lightSpaceProjection->offset + lightSpaceProjection->size ==
            static_cast<long>(kCameraUniformBufferSizeBytes));  // 2512
    }
    ++shadersChecked;
  }
  CHECK(shadersChecked == 11);

  fs::remove_all(outputDir, ec);
}

// Plan 0041 Milestone 2 (Spec 0041 R5, ADR-0089 Decision 4): the push-
// constant block of all ten PBR shaders, reflected fresh from each
// shader's own source by a real slangc run, against the C++ struct its
// Renderer payload pushes -- emissiveFactor's offset and the whole
// block's size. The block size here IS the pipeline range the engine
// declares (slang_json_transform.cpp takes elementVarLayout's own size),
// so this ties together all three hand-kept copies of it: the Renderer's
// static_asserts (sizeof below), atlantis_shader_compiler's expectation
// (compile_and_validate.cpp), and Runtime's pushConstantSizeBytesFor()
// -- any one drifting from the shader fails a build, this test, or a
// Layers-fatal draw.
TEST_CASE("PBR push constants: a real slangc reflection of every one of the ten PBR shaders places emissiveFactor "
          "and sizes the block exactly as its C++ struct does (Plan 0041, 10/10)",
          "[shader_system][runtime][pbr][reflection][emissive]") {
  struct ShaderCase {
    const char* name;
    long emissiveOffset;
    long blockSize;
  };
  const ShaderCase cases[] = {
      {"pbr_direct_lit", offsetof(PbrPushConstants, emissiveFactor), sizeof(PbrPushConstants)},
      {"pbr_direct_lit_normal_map", offsetof(PbrPushConstants, emissiveFactor), sizeof(PbrPushConstants)},
      {"pbr_ibl", offsetof(PbrPushConstants, emissiveFactor), sizeof(PbrPushConstants)},
      {"pbr_ibl_normal_map", offsetof(PbrPushConstants, emissiveFactor), sizeof(PbrPushConstants)},
      {"pbr_clearcoat_ibl", offsetof(PbrClearcoatPushConstants, emissiveFactor), sizeof(PbrClearcoatPushConstants)},
      {"pbr_clearcoat_ibl_normal_map", offsetof(PbrClearcoatPushConstants, emissiveFactor),
       sizeof(PbrClearcoatPushConstants)},
      {"pbr_sheen_ibl", offsetof(PbrSheenPushConstants, emissiveFactor), sizeof(PbrSheenPushConstants)},
      {"pbr_sheen_ibl_normal_map", offsetof(PbrSheenPushConstants, emissiveFactor), sizeof(PbrSheenPushConstants)},
      {"pbr_anisotropic_ibl", offsetof(PbrAnisotropicPushConstants, emissiveFactor),
       sizeof(PbrAnisotropicPushConstants)},
      {"pbr_anisotropic_ibl_normal_map", offsetof(PbrAnisotropicPushConstants, emissiveFactor),
       sizeof(PbrAnisotropicPushConstants)},
  };
  static_assert(std::size(cases) == 10);
  static_assert(sizeof(PbrSheenPushConstants) == 128);  // exactly the Vulkan-guaranteed maxPushConstantsSize

  const fs::path outputDir = fs::temp_directory_path() / "atlantis_push_constant_10_of_10_cross_check_tests";
  std::error_code ec;
  fs::create_directories(outputDir, ec);

  int shadersChecked = 0;
  for (const ShaderCase& shader : cases) {
    INFO("shader: " << shader.name);
    const fs::path source =
        fs::path(ATLANTIS_SHADER_SOURCE_ROOT) / shader.name / (std::string(shader.name) + ".slang");
    const fs::path jsonPath = outputDir / (std::string(shader.name) + "_vert_raw_refl.json");
    const fs::path spirvPath = outputDir / (std::string(shader.name) + "_vert_raw.spv");
    REQUIRE(runSlangcReflectionJson(source, "vertexMain", "vertex", jsonPath, spirvPath));
    const auto jsonText = readWholeFile(jsonPath);
    REQUIRE(jsonText.has_value());

    const std::size_t pushConstantsPos = jsonText->find("\"pushConstants\"");
    REQUIRE(pushConstantsPos != std::string::npos);
    const auto block = findFieldLayout(*jsonText, "elementVarLayout", pushConstantsPos);
    const auto emissive = findFieldLayout(*jsonText, "emissiveFactor", pushConstantsPos);
    REQUIRE(block.has_value());
    REQUIRE(emissive.has_value());
    CHECK(emissive->offset == shader.emissiveOffset);
    CHECK(emissive->size == 12);  // float3
    CHECK(block->size == shader.blockSize);
    CHECK(block->size <= 128);  // Vulkan's guaranteed maxPushConstantsSize
    ++shadersChecked;
  }
  CHECK(shadersChecked == 10);

  fs::remove_all(outputDir, ec);
}
