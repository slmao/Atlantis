#include <atlantis/shader_system/descriptor_contract.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <vector>

using atlantis::shader_system::ContractMismatchError;
using atlantis::shader_system::DescriptorBinding;
using atlantis::shader_system::DescriptorType;
using atlantis::shader_system::litTexturedExpectedDescriptorContract;
using atlantis::shader_system::minimalRendererExpectedDescriptorContract;
using atlantis::shader_system::pbrIblExpectedDescriptorContract;
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

// Plan 0027 Milestone 5 (ADR-0072 D-7): a fourth contiguous fragment
// sampler at binding 4 (the shadow map) -- was three.
TEST_CASE("pbrIblExpectedDescriptorContract declares uniform visibility and four contiguous fragment samplers",
          "[shader_system][descriptor_contract][pbr_ibl]") {
  const std::vector<DescriptorBinding> expected = {
      {.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
      {.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment},
      {.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
      {.set = 0, .binding = 2, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
      {.set = 0, .binding = 3, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
      {.set = 0, .binding = 4, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
  };
  CHECK(pbrIblExpectedDescriptorContract() == expected);
}

TEST_CASE("pbr IBL fragment contract rejects a missing or mistyped environment binding",
          "[shader_system][descriptor_contract][pbr_ibl]") {
  const std::vector<DescriptorBinding> fragmentExpected = {
      {.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment},
      {.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
      {.set = 0, .binding = 2, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
      {.set = 0, .binding = 3, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
  };
  ReflectionMetadata missing{};
  missing.descriptorBindings.assign(fragmentExpected.begin(), fragmentExpected.end() - 1);
  const auto missingResult = validateDescriptorContract(missing, fragmentExpected);
  REQUIRE(missingResult.isErr());
  CHECK(missingResult.error() == ContractMismatchError::BindingCountMismatch);

  ReflectionMetadata mistyped{};
  mistyped.descriptorBindings = fragmentExpected;
  mistyped.descriptorBindings[2].type = DescriptorType::UniformBuffer;
  const auto mistypedResult = validateDescriptorContract(mistyped, fragmentExpected);
  REQUIRE(mistypedResult.isErr());
  CHECK(mistypedResult.error() == ContractMismatchError::DescriptorTypeMismatch);
}

// Plan 0019 Section P13: litTexturedExpectedDescriptorContract()'s own
// three-entry shape -- {0,0,UniformBuffer,Vertex}, {0,0,UniformBuffer,
// Fragment}, {0,1,Sampler,Fragment}. Tested exactly the way the real
// caller (compile_and_validate.cpp's own validateDescriptorContractForStage())
// uses it: pre-scoped to one stage at a time via the identical
// std::copy_if filter, never the full, unscoped three-entry list in one
// call -- confirming the real usage pattern, not merely the function's
// own isolated return shape.
TEST_CASE("validateDescriptorContract() accepts litTexturedExpectedDescriptorContract()'s own vertex-scoped entry",
          "[shader_system][descriptor_contract][light]") {
  const auto metadata = metadataWithBindings({
      DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
  });
  const std::vector<DescriptorBinding> full = litTexturedExpectedDescriptorContract();
  std::vector<DescriptorBinding> vertexScoped;
  std::copy_if(full.begin(), full.end(), std::back_inserter(vertexScoped),
               [](const DescriptorBinding& b) { return b.stage == ShaderStage::Vertex; });
  REQUIRE(vertexScoped.size() == 1);
  const auto result = validateDescriptorContract(metadata, vertexScoped);
  REQUIRE(result.isOk());
}

TEST_CASE("validateDescriptorContract() accepts litTexturedExpectedDescriptorContract()'s own fragment-scoped "
          "two-entry match",
          "[shader_system][descriptor_contract][light]") {
  const auto metadata = metadataWithBindings({
      DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment},
      DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
  });
  const std::vector<DescriptorBinding> full = litTexturedExpectedDescriptorContract();
  std::vector<DescriptorBinding> fragmentScoped;
  std::copy_if(full.begin(), full.end(), std::back_inserter(fragmentScoped),
               [](const DescriptorBinding& b) { return b.stage == ShaderStage::Fragment; });
  REQUIRE(fragmentScoped.size() == 2);
  const auto result = validateDescriptorContract(metadata, fragmentScoped);
  REQUIRE(result.isOk());
}

// Real, genuine negative cases -- not merely passing-case sanity checks
// (mirrors Plan 0017's own empirical mutation-probe precedent for "does
// this check actually check anything"): a fragment reflection that
// never declares its own uniform-buffer reference fails, proving this
// check is a real, executed gate.
TEST_CASE("validateDescriptorContract() rejects a fragment reflection missing its own uniform-buffer reference "
          "entirely (count mismatch) against the lit-textured fragment-scoped contract",
          "[shader_system][descriptor_contract][light]") {
  const auto metadata = metadataWithBindings({
      DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
  });
  const std::vector<DescriptorBinding> full = litTexturedExpectedDescriptorContract();
  std::vector<DescriptorBinding> fragmentScoped;
  std::copy_if(full.begin(), full.end(), std::back_inserter(fragmentScoped),
               [](const DescriptorBinding& b) { return b.stage == ShaderStage::Fragment; });
  REQUIRE(fragmentScoped.size() == 2);
  const auto result = validateDescriptorContract(metadata, fragmentScoped);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ContractMismatchError::BindingCountMismatch);
}

TEST_CASE("validateDescriptorContract() rejects a fragment reflection whose uniform buffer is at the wrong "
          "binding index (BindingNotFound) against the lit-textured fragment-scoped contract",
          "[shader_system][descriptor_contract][light]") {
  // Same binding COUNT (2) as the real contract, but binding 0's own
  // uniform buffer is missing -- replaced by an unrelated binding 2 --
  // so the size check passes and BindingNotFound is the real, specific
  // reason this fails.
  const auto metadata = metadataWithBindings({
      DescriptorBinding{.set = 0, .binding = 2, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment},
      DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment},
  });
  const std::vector<DescriptorBinding> full = litTexturedExpectedDescriptorContract();
  std::vector<DescriptorBinding> fragmentScoped;
  std::copy_if(full.begin(), full.end(), std::back_inserter(fragmentScoped),
               [](const DescriptorBinding& b) { return b.stage == ShaderStage::Fragment; });
  const auto result = validateDescriptorContract(metadata, fragmentScoped);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == ContractMismatchError::BindingNotFound);
}
