// "tool"-labeled (Plan 0008 Section 9): needs the real Vulkan-SDK-
// provided slangc/spirv-val on the build machine, but no GPU/Vulkan
// device.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <atlantis/shader_system/command_line.h>

#include <catch2/catch_test_macros.hpp>

#include "../../../src/tools/shader_compiler/compile_and_validate.h"
#include "../../../src/tools/shader_compiler/process_launch.h"

using atlantis::shader_system::buildSlangcArgv;
using atlantis::shader_system::buildSpirvValArgv;
using atlantis::shader_system::SlangCompileRequest;
using atlantis::shader_system::SlangShaderStageArg;
using atlantis::tools::shader_compiler::CompileAndValidateRequest;
using atlantis::tools::shader_compiler::compileAndValidate;
using atlantis::tools::shader_compiler::launchProcess;

namespace {

[[nodiscard]] std::filesystem::path vulkanSdkBinDir() {
  char* rawValue = nullptr;
  std::size_t length = 0;
  const errno_t error = _dupenv_s(&rawValue, &length, "VULKAN_SDK");
  std::unique_ptr<char, decltype(&free)> owned(rawValue, &free);
  REQUIRE(error == 0);
  REQUIRE(owned != nullptr);
  return std::filesystem::path(owned.get()) / "Bin";
}

[[nodiscard]] std::filesystem::path fixturesDir() {
  return std::filesystem::path(ATLANTIS_SHADER_COMPILER_TEST_FIXTURES_DIR);
}

[[nodiscard]] std::filesystem::path scratchDir() {
  auto dir = std::filesystem::temp_directory_path() / "atlantis_toolchain_integration_tests";
  std::filesystem::create_directories(dir);
  return dir;
}

// Reads the SPIR-V binary header's own version word (bytes 4-7,
// interpreted as a native-endian uint32 -- confirmed against a real
// compiled artifact during this Plan's own implementation) and returns
// (major, minor).
[[nodiscard]] std::pair<int, int> readSpirvVersion(const std::filesystem::path& spirvPath) {
  std::ifstream file(spirvPath, std::ios::binary);
  REQUIRE(file.is_open());
  std::uint32_t header[2] = {0, 0};
  file.read(reinterpret_cast<char*>(header), sizeof(header));
  REQUIRE(file.good());
  REQUIRE(header[0] == 0x07230203u);
  const std::uint32_t versionWord = header[1];
  return {static_cast<int>((versionWord >> 16) & 0xFF), static_cast<int>((versionWord >> 8) & 0xFF)};
}

[[nodiscard]] std::vector<char> readAllBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  REQUIRE(file.is_open());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("A real slangc compile with -profile spirv_1_0 emits SPIR-V 1.0", "[tools][shader_compiler][tool]") {
  const auto slangc = vulkanSdkBinDir() / "slangc.exe";
  const auto outDir = scratchDir();
  const auto spirvPath = outDir / "profile_1_0.spv";
  const auto reflPath = outDir / "profile_1_0.refl.json";

  SlangCompileRequest request;
  request.sourcePath = fixturesDir() / "valid_probe.slang";
  request.entryPointName = "vertexMain";
  request.stage = SlangShaderStageArg::Vertex;
  request.spirvOutputPath = spirvPath;
  request.reflectionJsonOutputPath = reflPath;

  const auto argv = buildSlangcArgv(slangc, request);
  const auto result = launchProcess(slangc, argv);
  REQUIRE(result.isOk());
  REQUIRE(result.value().exitCode == 0);

  const auto [major, minor] = readSpirvVersion(spirvPath);
  REQUIRE(major == 1);
  REQUIRE(minor == 0);
}

TEST_CASE("Missing -profile demonstrably emits a different SPIR-V version (ADR-0028 regression case)",
          "[tools][shader_compiler][tool]") {
  // Simulates an accidental future regression back to relying on
  // slangc's own undecorated default -- deliberately bypasses
  // buildSlangcArgv() (which always includes -profile spirv_1_0) to
  // build a raw argv without it.
  const auto slangc = vulkanSdkBinDir() / "slangc.exe";
  const auto outDir = scratchDir();
  const auto spirvPath = outDir / "no_profile.spv";

  const std::vector<std::string> argv = {slangc.string(),
                                          "-target",
                                          "spirv",
                                          "-stage",
                                          "vertex",
                                          "-entry",
                                          "vertexMain",
                                          "-o",
                                          spirvPath.string(),
                                          (fixturesDir() / "valid_probe.slang").string()};
  const auto result = launchProcess(slangc, argv);
  REQUIRE(result.isOk());
  REQUIRE(result.value().exitCode == 0);

  const auto [major, minor] = readSpirvVersion(spirvPath);
  REQUIRE_FALSE((major == 1 && minor == 0));
}

TEST_CASE("A real spirv-val --target-env vulkan1.0 run against a compliant artifact exits 0",
          "[tools][shader_compiler][tool]") {
  const auto slangc = vulkanSdkBinDir() / "slangc.exe";
  const auto spirvVal = vulkanSdkBinDir() / "spirv-val.exe";
  const auto outDir = scratchDir();
  const auto spirvPath = outDir / "for_spirv_val.spv";
  const auto reflPath = outDir / "for_spirv_val.refl.json";

  SlangCompileRequest request;
  request.sourcePath = fixturesDir() / "valid_probe.slang";
  request.entryPointName = "vertexMain";
  request.stage = SlangShaderStageArg::Vertex;
  request.spirvOutputPath = spirvPath;
  request.reflectionJsonOutputPath = reflPath;
  REQUIRE(launchProcess(slangc, buildSlangcArgv(slangc, request)).value().exitCode == 0);

  const auto result = launchProcess(spirvVal, buildSpirvValArgv(spirvVal, spirvPath));
  REQUIRE(result.isOk());
  REQUIRE(result.value().exitCode == 0);
}

TEST_CASE("Suppressing warning 50011 produces a byte-identical .spv to the unsuppressed run",
          "[tools][shader_compiler][tool]") {
  // Test-case name deliberately does not start with "-warnings-disable"
  // -- CTest invokes a single Catch2 test case by passing its exact
  // name as an argv token, and Catch2's own CLI parser misreads a name
  // starting with "-" as an (unrecognized) flag rather than a test-name
  // filter (discovered while writing this test: registering this case
  // under a name starting with "-warnings-disable" made ctest fail with
  // "Unrecognised token: -warnings-disable" before the test body ever
  // ran).
  const auto slangc = vulkanSdkBinDir() / "slangc.exe";
  const auto outDir = scratchDir();

  SlangCompileRequest suppressedRequest;
  suppressedRequest.sourcePath = fixturesDir() / "valid_probe.slang";
  suppressedRequest.entryPointName = "vertexMain";
  suppressedRequest.stage = SlangShaderStageArg::Vertex;
  suppressedRequest.spirvOutputPath = outDir / "suppressed.spv";
  suppressedRequest.reflectionJsonOutputPath = outDir / "suppressed.refl.json";
  const auto suppressedResult = launchProcess(slangc, buildSlangcArgv(slangc, suppressedRequest));
  REQUIRE(suppressedResult.isOk());
  REQUIRE(suppressedResult.value().exitCode == 0);
  REQUIRE(suppressedResult.value().diagnostics.find("E50011") == std::string::npos);

  const auto unsuppressedSpirv = outDir / "unsuppressed.spv";
  const std::vector<std::string> unsuppressedArgv = {slangc.string(),
                                                       "-target",
                                                       "spirv",
                                                       "-profile",
                                                       "spirv_1_0",
                                                       "-stage",
                                                       "vertex",
                                                       "-entry",
                                                       "vertexMain",
                                                       "-o",
                                                       unsuppressedSpirv.string(),
                                                       (fixturesDir() / "valid_probe.slang").string()};
  const auto unsuppressedResult = launchProcess(slangc, unsuppressedArgv);
  REQUIRE(unsuppressedResult.isOk());
  REQUIRE(unsuppressedResult.value().exitCode == 0);
  REQUIRE(unsuppressedResult.value().diagnostics.find("E50011") != std::string::npos);

  REQUIRE(readAllBytes(suppressedRequest.spirvOutputPath) == readAllBytes(unsuppressedSpirv));
}

TEST_CASE("A deliberately-invalid .slang fixture fails compileAndValidate() cleanly, publishing nothing",
          "[tools][shader_compiler][tool]") {
  const auto outDir = scratchDir() / "invalid_fixture_out";
  std::filesystem::remove_all(outDir);
  std::filesystem::create_directories(outDir);

  CompileAndValidateRequest request;
  request.slangcPath = vulkanSdkBinDir() / "slangc.exe";
  request.spirvValPath = vulkanSdkBinDir() / "spirv-val.exe";
  request.sourcePath = fixturesDir() / "invalid_probe.slang";
  request.vertexEntry = "vertexMain";
  request.fragmentEntry = "fragmentMain";
  request.outputDir = outDir;
  request.expectedContract = "minimal-renderer";
  request.stampPath = outDir / "invalid_probe.stamp";

  const int exitCode = compileAndValidate(request);
  REQUIRE(exitCode != 0);
  REQUIRE_FALSE(std::filesystem::exists(request.stampPath));

  std::size_t remainingEntries = 0;
  for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(outDir)) ++remainingEntries;
  REQUIRE(remainingEntries == 0);
}

TEST_CASE("compileAndValidate() run twice on identical input produces byte-identical output",
          "[tools][shader_compiler][tool]") {
  const auto outDir = scratchDir() / "determinism_out";
  std::filesystem::remove_all(outDir);
  std::filesystem::create_directories(outDir);

  CompileAndValidateRequest request;
  request.slangcPath = vulkanSdkBinDir() / "slangc.exe";
  request.spirvValPath = vulkanSdkBinDir() / "spirv-val.exe";
  request.sourcePath = fixturesDir() / "valid_probe.slang";
  request.vertexEntry = "vertexMain";
  request.fragmentEntry = "fragmentMain";
  request.outputDir = outDir;
  request.expectedContract = "minimal-renderer";
  request.stampPath = outDir / "valid_probe.stamp";

  REQUIRE(compileAndValidate(request) == 0);
  const auto firstSpv = readAllBytes(outDir / "valid_probe.vert.spv");
  const auto firstRefl = readAllBytes(outDir / "valid_probe.vert.refl.json");

  std::filesystem::remove(request.stampPath);
  REQUIRE(compileAndValidate(request) == 0);
  const auto secondSpv = readAllBytes(outDir / "valid_probe.vert.spv");
  const auto secondRefl = readAllBytes(outDir / "valid_probe.vert.refl.json");

  REQUIRE(firstSpv == secondSpv);
  REQUIRE(firstRefl == secondRefl);
}

TEST_CASE("Partial-publish recovery: a publish failure leaves no stamp, and a subsequent ordinary re-run succeeds",
          "[tools][shader_compiler][tool]") {
  const auto outDir = scratchDir() / "partial_publish_out";
  std::filesystem::remove_all(outDir);
  std::filesystem::create_directories(outDir);

  CompileAndValidateRequest request;
  request.slangcPath = vulkanSdkBinDir() / "slangc.exe";
  request.spirvValPath = vulkanSdkBinDir() / "spirv-val.exe";
  request.sourcePath = fixturesDir() / "valid_probe.slang";
  request.vertexEntry = "vertexMain";
  request.fragmentEntry = "fragmentMain";
  request.outputDir = outDir;
  request.expectedContract = "minimal-renderer";
  request.stampPath = outDir / "valid_probe.stamp";

  // Injects a step-14d failure: a directory occupies the path
  // std::filesystem::rename() needs for one of the four final
  // artifacts, so that specific rename fails deterministically.
  const auto blockedPath = outDir / "valid_probe.frag.refl.json";
  std::filesystem::create_directories(blockedPath);

  const int firstExitCode = compileAndValidate(request);
  REQUIRE(firstExitCode != 0);
  REQUIRE_FALSE(std::filesystem::exists(request.stampPath));

  // Remove the injected failure and re-run ordinarily.
  std::filesystem::remove_all(blockedPath);
  const int secondExitCode = compileAndValidate(request);
  REQUIRE(secondExitCode == 0);
  REQUIRE(std::filesystem::exists(request.stampPath));
  REQUIRE(std::filesystem::exists(outDir / "valid_probe.vert.spv"));
  REQUIRE(std::filesystem::exists(outDir / "valid_probe.frag.spv"));
  REQUIRE(std::filesystem::exists(outDir / "valid_probe.vert.refl.json"));
  REQUIRE(std::filesystem::exists(outDir / "valid_probe.frag.refl.json"));
}

TEST_CASE("Missing slangc/spirv-val fails atlantis_shader_compiler itself cleanly", "[tools][shader_compiler][tool]") {
  const auto outDir = scratchDir() / "missing_tool_out";
  std::filesystem::remove_all(outDir);
  std::filesystem::create_directories(outDir);

  CompileAndValidateRequest request;
  request.slangcPath = outDir / "does_not_exist_slangc.exe";
  request.spirvValPath = vulkanSdkBinDir() / "spirv-val.exe";
  request.sourcePath = fixturesDir() / "valid_probe.slang";
  request.vertexEntry = "vertexMain";
  request.fragmentEntry = "fragmentMain";
  request.outputDir = outDir;
  request.expectedContract = "minimal-renderer";
  request.stampPath = outDir / "valid_probe.stamp";

  REQUIRE(compileAndValidate(request) != 0);
  REQUIRE_FALSE(std::filesystem::exists(request.stampPath));
}
