#include <atlantis/shader_system/descriptor_contract.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::ContractMismatchError;
using atlantis::shader_system::DescriptorBinding;
using atlantis::shader_system::DescriptorType;
using atlantis::shader_system::minimalRendererExpectedDescriptorContract;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::texturedMaterialExpectedDescriptorContract;
using atlantis::shader_system::validateDescriptorContract;

namespace {

[[nodiscard]] ReflectionMetadata metadataWithBindings(std::vector<DescriptorBinding> bindings) {
  ReflectionMetadata metadata;
  metadata.entryPointName = "vertexMain";
  metadata.stage = ShaderStage::Vertex;
  metadata.descriptorBindings = std::move(bindings);
  return metadata;
}

}  // namespace

TEST_CASE("validateDescriptorContract() accepts an exact {set:0, binding:0} match",
          "[shader_system][descriptor_contract]") {
  const auto metadata = metadataWithBindings(
      {DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex}});
  const auto result = validateDescriptorContract(metadata, minimalRendererExpectedDescriptorContract());
  REQUIRE(result.isOk());
}

TEST_CASE("validateDescriptorContract() rejects a nonzero descriptor set against the fixed {0,0} contract",
          "[shader_system][descriptor_contract]") {
  // The second half of the parser-vs-contract split (Plan 0008 Section
  // 2/3): the parser already accepted {set: 2, binding: 3} into a valid
  // ReflectionMetadata (slang_json_transform_tests.cpp's own case);
  // THIS is the separate, later, contract-level rejection.
  const auto metadata = metadataWithBindings(
      {DescriptorBinding{.set = 2, .binding = 3, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex}});
  const auto result = validateDescriptorContract(metadata, minimalRendererExpectedDescriptorContract());
  REQUIRE(result.isErr());
  REQUIRE((result.error() == ContractMismatchError::BindingNotFound ||
           result.error() == ContractMismatchError::UnexpectedExtraBinding));
}

TEST_CASE("validateDescriptorContract() other mismatch variants", "[shader_system][descriptor_contract]") {
  SECTION("wrong binding count") {
    const auto metadata = metadataWithBindings({
        DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
        DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
    });
    const auto result = validateDescriptorContract(metadata, minimalRendererExpectedDescriptorContract());
    REQUIRE(result.isErr());
    REQUIRE(result.error() == ContractMismatchError::BindingCountMismatch);
  }

  SECTION("expected binding not found") {
    const auto metadata = metadataWithBindings(
        {DescriptorBinding{.set = 0, .binding = 5, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex}});
    const auto result = validateDescriptorContract(metadata, minimalRendererExpectedDescriptorContract());
    REQUIRE(result.isErr());
    REQUIRE(result.error() == ContractMismatchError::BindingNotFound);
  }

  SECTION("wrong stage") {
    const auto metadata = metadataWithBindings({DescriptorBinding{
        .set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment}});
    const auto result = validateDescriptorContract(metadata, minimalRendererExpectedDescriptorContract());
    REQUIRE(result.isErr());
    REQUIRE(result.error() == ContractMismatchError::StageMismatch);
  }
}

TEST_CASE("validateDescriptorContract() accepts an exact two-binding match against the textured contract",
          "[shader_system][descriptor_contract]") {
  // Spec 0016/D6: {set 0, binding 0, UniformBuffer, Vertex} + {set 0,
  // binding 1, Sampler, Fragment}.
  const auto metadata = metadataWithBindings({
      DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
      DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
  });
  const auto result = validateDescriptorContract(metadata, texturedMaterialExpectedDescriptorContract());
  REQUIRE(result.isOk());
}

TEST_CASE("validateDescriptorContract() rejects a UniformBuffer where the textured contract expects a Sampler",
          "[shader_system][descriptor_contract]") {
  const auto metadata = metadataWithBindings({
      DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
      DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment},
  });
  const auto result = validateDescriptorContract(metadata, texturedMaterialExpectedDescriptorContract());
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ContractMismatchError::DescriptorTypeMismatch);
}
