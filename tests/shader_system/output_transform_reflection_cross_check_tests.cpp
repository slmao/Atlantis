// Plan 0024 Milestone 8 (ADR-0068 D-10): both output-transform shader
// variants' real Slang reflection, transformed into Atlantis's own
// schema by the production shader build, is cross-checked against the
// shared C++ descriptor contract. GPU-independent: shader compilation
// is a build dependency; the test itself only reads reflection files.

#include <atlantis/shader_system/descriptor_contract.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::DescriptorBinding;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::outputTransformExpectedDescriptorContract;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::validateDescriptorContract;

namespace {

struct ShaderVariant {
  const char* name;
  const char* outputDirectory;
  const char* artifactStem;
};

[[nodiscard]] std::vector<DescriptorBinding> contractForStage(ShaderStage stage) {
  const std::vector<DescriptorBinding> fullContract = outputTransformExpectedDescriptorContract();
  std::vector<DescriptorBinding> scopedContract;
  std::copy_if(fullContract.begin(), fullContract.end(), std::back_inserter(scopedContract),
               [stage](const DescriptorBinding& binding) { return binding.stage == stage; });
  return scopedContract;
}

}  // namespace

TEST_CASE("Both output-transform variants' real reflection matches the shared C++ descriptor contract",
          "[shader_system][output_transform][reflection]") {
  const std::array variants = {
      ShaderVariant{"unorm", ATLANTIS_OUTPUT_TRANSFORM_UNORM_SHADER_DIR, "output_transform_unorm"},
      ShaderVariant{"srgb", ATLANTIS_OUTPUT_TRANSFORM_SRGB_SHADER_DIR, "output_transform_srgb"},
  };

  const std::vector<DescriptorBinding> vertexContract = contractForStage(ShaderStage::Vertex);
  const std::vector<DescriptorBinding> fragmentContract = contractForStage(ShaderStage::Fragment);
  REQUIRE(vertexContract.empty());
  REQUIRE(fragmentContract.size() == 1);

  for (const ShaderVariant& variant : variants) {
    DYNAMIC_SECTION(variant.name) {
      const std::string shaderDirectory = variant.outputDirectory;
      auto vertexResult = loadReflectionMetadata(shaderDirectory + "/" + variant.artifactStem + ".vert.refl.json");
      REQUIRE(vertexResult.isOk());
      CHECK(vertexResult.value().stage == ShaderStage::Vertex);
      CHECK(vertexResult.value().pushConstantRanges.empty());
      CHECK(validateDescriptorContract(vertexResult.value(), vertexContract).isOk());

      auto fragmentResult =
          loadReflectionMetadata(shaderDirectory + "/" + variant.artifactStem + ".frag.refl.json");
      REQUIRE(fragmentResult.isOk());
      CHECK(fragmentResult.value().stage == ShaderStage::Fragment);
      CHECK(fragmentResult.value().pushConstantRanges.empty());
      CHECK(validateDescriptorContract(fragmentResult.value(), fragmentContract).isOk());
    }
  }
}
