#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>

#include "../fixture/lighting_demo_fixture.h"
#include "../support/pixel_diff.h"
#include "../support/png_codec.h"
#include "../support/provenance.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

// Standalone developer tool -- never CTest-registered, mirrors
// material_demo_main.cpp's own exact structure (Plan 0019 Milestone
// 10b). ADR-0042's own two-phase golden capture process, matching every
// prior golden generator in this directory: this tool is only ever run
// against an already-committed, clean working tree (Step 1 below), so a
// golden it produces always has a real, resolvable source_revision
// pointing at a commit that already contains this exact fixture code.

namespace {

using atlantis::image_regression::encodePng;
using atlantis::image_regression::LightingDemoFixture;
using atlantis::image_regression::parseEnvironmentProvenance;
using atlantis::image_regression::parseGoldenProvenance;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::Provenance;
using atlantis::image_regression::renderLightingDemoFrame;
using atlantis::image_regression::serializeGoldenProvenance;
using atlantis::image_regression::setUpLightingDemoFixture;
using atlantis::runtime::BootstrapConfig;

struct GitCommandResult {
  int exitCode = 0;
  std::string stdOut;
};

[[nodiscard]] std::optional<GitCommandResult> runGitCommand(const std::string& command) {
  FILE* pipe = _popen(command.c_str(), "r");
  if (pipe == nullptr) return std::nullopt;

  std::string output;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }
  GitCommandResult result;
  result.exitCode = _pclose(pipe);
  result.stdOut = std::move(output);
  return result;
}

[[nodiscard]] std::string trim(const std::string& text) {
  const std::size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const std::size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(start, end - start + 1);
}

[[nodiscard]] std::string readFileToString(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::string currentUtcIso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
  std::tm utcTm{};
  gmtime_s(&utcTm, &nowTimeT);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
  return buffer;
}

void printProvenanceFieldIfDifferent(const char* fieldName, const std::string& oldValue,
                                      const std::string& newValue) {
  if (oldValue != newValue) {
    ATLANTIS_LOG_INFO("  {}: {} -> {}", fieldName, oldValue, newValue);
  }
}

// Mirrors material_demo_main.cpp's own identical population of the
// lighting_demo_scene's own cooked paths and both built-in shader
// pairs -- every value below comes from a configure-time compile
// definition, never a CLI argument.
[[nodiscard]] BootstrapConfig buildConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_lighting_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_lighting_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_lighting_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  // Plan 0023 Milestone 5: LightingDemoFixture's own further-widened
  // realizePendingMaterials() call requires a real pbrDirectLit* trio
  // too, even though lighting_demo_scene never realizes one.
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_LIGHTING_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  atlantis::log::setMinLevel(atlantis::LogLevel::Info);

  if (argc != 2) {
    ATLANTIS_LOG_ERROR("usage: atlantis_image_regression_lighting_demo_golden_generator <golden-name>");
    ATLANTIS_LOG_ERROR(
        "  e.g.: atlantis_image_regression_lighting_demo_golden_generator "
        "lighting_demo/lighting_demo_512x512_rgba8unorm");
    return 2;
  }
  const std::string goldenName = argv[1];

  const auto statusResult = runGitCommand("git status --porcelain");
  if (!statusResult.has_value()) {
    ATLANTIS_LOG_ERROR("failed to invoke git -- confirm it is installed and on PATH");
    return 1;
  }
  if (statusResult->exitCode != 0) {
    ATLANTIS_LOG_ERROR("git status --porcelain exited with code {}", statusResult->exitCode);
    return 1;
  }
  if (!statusResult->stdOut.empty()) {
    ATLANTIS_LOG_ERROR("working tree is not clean; commit or stash changes before regenerating a golden");
    return 1;
  }

  const auto revParseResult = runGitCommand("git rev-parse HEAD");
  if (!revParseResult.has_value()) {
    ATLANTIS_LOG_ERROR("failed to invoke git -- confirm it is installed and on PATH");
    return 1;
  }
  if (revParseResult->exitCode != 0) {
    ATLANTIS_LOG_ERROR("git rev-parse HEAD exited with code {}", revParseResult->exitCode);
    return 1;
  }
  const std::string sourceRevision = trim(revParseResult->stdOut);

  const std::string environmentFilePath = ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE;
  const auto trackedCheckResult =
      runGitCommand("git ls-files --error-unmatch \"" + environmentFilePath + "\" 2>nul");
  if (!trackedCheckResult.has_value()) {
    ATLANTIS_LOG_ERROR("failed to invoke git -- confirm it is installed and on PATH");
    return 1;
  }
  if (trackedCheckResult->exitCode == 0) {
    ATLANTIS_LOG_ERROR(
        "current_environment.sidecar.txt must never be committed; found tracked in git -- run "
        "`git rm --cached tests/image_regression/current_environment.sidecar.txt` before proceeding");
    return 1;
  }

  if (!std::filesystem::exists(environmentFilePath)) {
    ATLANTIS_LOG_ERROR("expected environment file at: {}", environmentFilePath);
    ATLANTIS_LOG_ERROR(
        "copy tests/image_regression/current_environment.sidecar.txt.example there and fill it in for this "
        "machine");
    return 1;
  }
  const auto environmentProvenanceResult = parseEnvironmentProvenance(readFileToString(environmentFilePath));
  if (environmentProvenanceResult.isErr()) {
    ATLANTIS_LOG_ERROR("failed to parse environment file at: {}", environmentFilePath);
    return 1;
  }
  const auto& environmentProvenance = environmentProvenanceResult.value();

  auto fixtureResult = setUpLightingDemoFixture(buildConfig());
  if (fixtureResult.isErr()) {
    ATLANTIS_LOG_ERROR("setUpLightingDemoFixture() failed");
    return 1;
  }
  LightingDemoFixture fixture = std::move(fixtureResult.value());

  auto renderResult = renderLightingDemoFrame(fixture);
  const auto finalWaitResult = fixture.device->waitIdle();
  if (renderResult.isErr()) {
    ATLANTIS_LOG_ERROR("renderLightingDemoFrame() failed");
    return 1;
  }
  if (finalWaitResult.isErr()) {
    ATLANTIS_LOG_ERROR("waitIdle() failed after render");
    return 1;
  }
  const PixelBuffer capturedPixels = std::move(renderResult.value());

  Provenance provenance;
  provenance.captureDate = currentUtcIso8601();
  provenance.sourceRevision = sourceRevision;
  provenance.gpuVendor = environmentProvenance.gpuVendor;
  provenance.gpuModel = environmentProvenance.gpuModel;
  provenance.driverVersion = environmentProvenance.driverVersion;
  provenance.osBuild = environmentProvenance.osBuild;
  provenance.vulkanLoaderApiVersion = environmentProvenance.vulkanLoaderApiVersion;
  provenance.vulkanRequestedInstanceApiVersion = environmentProvenance.vulkanRequestedInstanceApiVersion;
  provenance.vulkanPhysicalDeviceApiVersion = environmentProvenance.vulkanPhysicalDeviceApiVersion;
  provenance.extentWidth = atlantis::image_regression::kLightingDemoExtentPixels;
  provenance.extentHeight = atlantis::image_regression::kLightingDemoExtentPixels;
  provenance.format = "Rgba8Unorm";

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::filesystem::path pngPath = goldensDir / (goldenName + ".png");
  const std::filesystem::path sidecarPath = goldensDir / (goldenName + ".sidecar.txt");

  if (std::filesystem::exists(pngPath) && std::filesystem::exists(sidecarPath)) {
    const auto oldProvenanceResult = parseGoldenProvenance(readFileToString(sidecarPath));
    if (oldProvenanceResult.isOk()) {
      const auto& oldProvenance = oldProvenanceResult.value();
      ATLANTIS_LOG_INFO("Existing golden found at {} -- fields that will change:", pngPath.string());
      printProvenanceFieldIfDifferent("capture_date", oldProvenance.captureDate, provenance.captureDate);
      printProvenanceFieldIfDifferent("source_revision", oldProvenance.sourceRevision, provenance.sourceRevision);
      printProvenanceFieldIfDifferent("gpu_vendor", oldProvenance.gpuVendor, provenance.gpuVendor);
      printProvenanceFieldIfDifferent("gpu_model", oldProvenance.gpuModel, provenance.gpuModel);
      printProvenanceFieldIfDifferent("driver_version", oldProvenance.driverVersion, provenance.driverVersion);
      printProvenanceFieldIfDifferent("os_build", oldProvenance.osBuild, provenance.osBuild);
      printProvenanceFieldIfDifferent("vulkan_loader_api_version", oldProvenance.vulkanLoaderApiVersion,
                                       provenance.vulkanLoaderApiVersion);
      printProvenanceFieldIfDifferent("vulkan_requested_instance_api_version",
                                       oldProvenance.vulkanRequestedInstanceApiVersion,
                                       provenance.vulkanRequestedInstanceApiVersion);
      printProvenanceFieldIfDifferent("vulkan_physical_device_api_version",
                                       oldProvenance.vulkanPhysicalDeviceApiVersion,
                                       provenance.vulkanPhysicalDeviceApiVersion);
    } else {
      ATLANTIS_LOG_INFO("Existing golden found at {}, but its sidecar could not be parsed -- overwriting.",
                         pngPath.string());
    }
  } else {
    ATLANTIS_LOG_INFO("No existing golden at {} -- writing new golden.", pngPath.string());
  }

  std::filesystem::create_directories(pngPath.parent_path());

  if (encodePng(pngPath, capturedPixels).isErr()) {
    ATLANTIS_LOG_ERROR("failed to write PNG to {}", pngPath.string());
    return 1;
  }

  std::ofstream sidecarOut(sidecarPath, std::ios::binary);
  const std::string sidecarText = serializeGoldenProvenance(provenance);
  sidecarOut << sidecarText;
  if (!sidecarOut) {
    ATLANTIS_LOG_ERROR("failed to write sidecar to {}", sidecarPath.string());
    return 1;
  }

  ATLANTIS_LOG_INFO("Golden captured: {}", pngPath.string());
  ATLANTIS_LOG_INFO("  source_revision: {}", provenance.sourceRevision);
  ATLANTIS_LOG_INFO("  capture_date: {}", provenance.captureDate);
  return 0;
}
