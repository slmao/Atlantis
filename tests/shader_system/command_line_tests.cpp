#include <algorithm>

#include <atlantis/shader_system/command_line.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::buildSlangcArgv;
using atlantis::shader_system::buildSpirvValArgv;
using atlantis::shader_system::SlangCompileRequest;
using atlantis::shader_system::SlangShaderStageArg;

namespace {

[[nodiscard]] bool contains(const std::vector<std::string>& argv, const std::string& value) {
  return std::find(argv.begin(), argv.end(), value) != argv.end();
}

}  // namespace

TEST_CASE("buildSlangcArgv() fixes every non-Plan-revisable flag", "[shader_system][command_line]") {
  const SlangCompileRequest request{
      .sourcePath = "minimal_mesh.slang",
      .entryPointName = "vertexMain",
      .stage = SlangShaderStageArg::Vertex,
      .spirvOutputPath = "out.vert.spv",
      .reflectionJsonOutputPath = "out.vert.refl.json",
  };
  const auto argv = buildSlangcArgv("slangc.exe", request);

  REQUIRE(argv.front() == "slangc.exe");
  REQUIRE(contains(argv, "-profile"));
  REQUIRE(contains(argv, "spirv_1_0"));
  REQUIRE(contains(argv, "-warnings-disable"));
  REQUIRE(contains(argv, "50011"));
  REQUIRE(contains(argv, "-target"));
  REQUIRE(contains(argv, "spirv"));
  REQUIRE(contains(argv, "-stage"));
  REQUIRE(contains(argv, "vertex"));
  REQUIRE(contains(argv, "-entry"));
  REQUIRE(contains(argv, "vertexMain"));
  REQUIRE_FALSE(contains(argv, "-fvk-use-entrypoint-name"));
}

TEST_CASE("buildSlangcArgv() selects the fragment stage argument", "[shader_system][command_line]") {
  const SlangCompileRequest request{
      .sourcePath = "minimal_mesh.slang",
      .entryPointName = "fragmentMain",
      .stage = SlangShaderStageArg::Fragment,
      .spirvOutputPath = "out.frag.spv",
      .reflectionJsonOutputPath = "out.frag.refl.json",
  };
  const auto argv = buildSlangcArgv("slangc.exe", request);
  REQUIRE(contains(argv, "fragment"));
  REQUIRE_FALSE(contains(argv, "vertex"));
}

TEST_CASE("buildSlangcArgv() a source path containing a space produces a plain, unquoted argv entry",
          "[shader_system][command_line]") {
  // Quoting is process_launch.cpp's own job (Section 4), never
  // command_line.cpp's -- this module only ever produces a plain
  // std::vector<std::string> argv.
  const SlangCompileRequest request{
      .sourcePath = "C:/Program Files/Atlantis/minimal_mesh.slang",
      .entryPointName = "vertexMain",
      .stage = SlangShaderStageArg::Vertex,
      .spirvOutputPath = "out.vert.spv",
      .reflectionJsonOutputPath = "out.vert.refl.json",
  };
  const auto argv = buildSlangcArgv("slangc.exe", request);
  REQUIRE(contains(argv, "C:/Program Files/Atlantis/minimal_mesh.slang"));
}

TEST_CASE("buildSpirvValArgv() targets vulkan1.0", "[shader_system][command_line]") {
  const auto argv = buildSpirvValArgv("spirv-val.exe", "out.vert.spv");
  REQUIRE(argv.front() == "spirv-val.exe");
  REQUIRE(contains(argv, "--target-env"));
  REQUIRE(contains(argv, "vulkan1.0"));
  REQUIRE(contains(argv, "out.vert.spv"));
}
