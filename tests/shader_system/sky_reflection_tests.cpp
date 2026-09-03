// Plan 0026 Milestone 4 (ADR-0071 P3): sky.slang's real Slang reflection,
// transformed into Atlantis's own schema by the production shader build,
// is cross-checked against the shared C++ descriptor contract. GPU-
// independent: shader compilation is a build dependency; the test itself
// only reads reflection files. Mirrors
// output_transform_reflection_cross_check_tests.cpp's own shape exactly,
// narrowed to sky's own single variant.

#include <atlantis/shader_system/descriptor_contract.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::DescriptorBinding;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::skyExpectedDescriptorContract;
using atlantis::shader_system::validateDescriptorContract;

namespace {

[[nodiscard]] std::vector<DescriptorBinding> contractForStage(ShaderStage stage) {
  const std::vector<DescriptorBinding> fullContract = skyExpectedDescriptorContract();
  std::vector<DescriptorBinding> scopedContract;
  std::copy_if(fullContract.begin(), fullContract.end(), std::back_inserter(scopedContract),
               [stage](const DescriptorBinding& binding) { return binding.stage == stage; });
  return scopedContract;
}

}  // namespace

TEST_CASE("sky's real reflection matches the shared C++ descriptor contract -- both bindings Fragment-only",
          "[shader_system][sky][reflection]") {
  const std::vector<DescriptorBinding> vertexContract = contractForStage(ShaderStage::Vertex);
  const std::vector<DescriptorBinding> fragmentContract = contractForStage(ShaderStage::Fragment);
  // Plan 0026 P3: confirmed against a real slangc probe -- vertexMain
  // touches neither the camera uniform nor the environment cubemap, so
  // both reflect "used": 0 on the vertex stage and are dropped before
  // reaching ReflectionMetadata::descriptorBindings.
  REQUIRE(vertexContract.empty());
  REQUIRE(fragmentContract.size() == 2);

  const std::string shaderDirectory = ATLANTIS_SKY_SHADER_DIR;
  auto vertexResult = loadReflectionMetadata(shaderDirectory + "/sky.vert.refl.json");
  REQUIRE(vertexResult.isOk());
  CHECK(vertexResult.value().stage == ShaderStage::Vertex);
  CHECK(vertexResult.value().pushConstantRanges.empty());
  CHECK(validateDescriptorContract(vertexResult.value(), vertexContract).isOk());

  auto fragmentResult = loadReflectionMetadata(shaderDirectory + "/sky.frag.refl.json");
  REQUIRE(fragmentResult.isOk());
  CHECK(fragmentResult.value().stage == ShaderStage::Fragment);
  CHECK(fragmentResult.value().pushConstantRanges.empty());
  CHECK(validateDescriptorContract(fragmentResult.value(), fragmentContract).isOk());
}
