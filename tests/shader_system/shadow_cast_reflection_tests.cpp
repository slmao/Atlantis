// Plan 0027 Milestone 4 (ADR-0072 D-3): shadow_cast.slang's real Slang
// reflection, transformed into Atlantis's own schema by the production
// shader build, is cross-checked against the shared C++ descriptor
// contract. GPU-independent: shader compilation is a build dependency;
// the test itself only reads reflection files. Mirrors
// sky_reflection_tests.cpp's own shape exactly, inverted: here the
// vertex stage carries the one real binding and the fragment stage is
// empty (the opposite of sky's own fragment-only shape), since
// shadow_cast's own fragmentMain() is void and reads no resource.

#include <atlantis/shader_system/descriptor_contract.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::DescriptorBinding;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::PushConstantRange;
using atlantis::shader_system::shadowCastExpectedDescriptorContract;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::validateDescriptorContract;

namespace {

[[nodiscard]] std::vector<DescriptorBinding> contractForStage(ShaderStage stage) {
  const std::vector<DescriptorBinding> fullContract = shadowCastExpectedDescriptorContract();
  std::vector<DescriptorBinding> scopedContract;
  std::copy_if(fullContract.begin(), fullContract.end(), std::back_inserter(scopedContract),
               [stage](const DescriptorBinding& binding) { return binding.stage == stage; });
  return scopedContract;
}

}  // namespace

TEST_CASE("shadow_cast's real reflection matches the shared C++ descriptor contract -- one Vertex-only binding",
          "[shader_system][shadow_cast][reflection]") {
  const std::vector<DescriptorBinding> vertexContract = contractForStage(ShaderStage::Vertex);
  const std::vector<DescriptorBinding> fragmentContract = contractForStage(ShaderStage::Fragment);
  // Confirmed against a real slangc probe during this Plan's own
  // drafting: vertexMain reads lightSpace ("used": 1); fragmentMain
  // reflects it too ("used": 0, dropped before reaching
  // ReflectionMetadata::descriptorBindings) and has no result/color
  // output at all.
  REQUIRE(vertexContract.size() == 1);
  REQUIRE(fragmentContract.empty());

  const std::string shaderDirectory = ATLANTIS_SHADOW_CAST_SHADER_DIR;
  auto vertexResult = loadReflectionMetadata(shaderDirectory + "/shadow_cast.vert.refl.json");
  REQUIRE(vertexResult.isOk());
  CHECK(vertexResult.value().stage == ShaderStage::Vertex);
  // ADR-0072 D-3: one ObjectToWorldOnly-shaped (64-byte) push constant,
  // not the 96-byte PBR shape.
  const std::vector<PushConstantRange> expectedPushConstants{
      PushConstantRange{.offsetBytes = 0, .sizeBytes = sizeof(float) * 16, .stage = ShaderStage::Vertex}};
  CHECK(vertexResult.value().pushConstantRanges == expectedPushConstants);
  CHECK(validateDescriptorContract(vertexResult.value(), vertexContract).isOk());

  auto fragmentResult = loadReflectionMetadata(shaderDirectory + "/shadow_cast.frag.refl.json");
  REQUIRE(fragmentResult.isOk());
  CHECK(fragmentResult.value().stage == ShaderStage::Fragment);
  CHECK(validateDescriptorContract(fragmentResult.value(), fragmentContract).isOk());
}
