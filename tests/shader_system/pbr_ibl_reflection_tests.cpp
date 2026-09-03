#include <atlantis/shader_system/descriptor_contract.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("pbr_ibl real reflection matches its five-entry descriptor and 96-byte push-constant contract",
          "[shader_system][pbr_ibl][reflection]") {
  using namespace atlantis::shader_system;
  const auto vertex = loadReflectionMetadata(std::string(ATLANTIS_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.refl.json");
  const auto fragment = loadReflectionMetadata(std::string(ATLANTIS_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.refl.json");
  REQUIRE(vertex.isOk());
  REQUIRE(fragment.isOk());

  const std::vector<DescriptorBinding> full = pbrIblExpectedDescriptorContract();
  const std::vector<DescriptorBinding> expectedVertex{full[0]};
  const std::vector<DescriptorBinding> expectedFragment(full.begin() + 1, full.end());
  CHECK(vertex.value().descriptorBindings == expectedVertex);
  CHECK(fragment.value().descriptorBindings == expectedFragment);
  CHECK((vertex.value().pushConstantRanges ==
         std::vector<PushConstantRange>{{.offsetBytes = 0, .sizeBytes = 96, .stage = ShaderStage::Vertex}}));
  CHECK((fragment.value().pushConstantRanges ==
         std::vector<PushConstantRange>{{.offsetBytes = 0, .sizeBytes = 96, .stage = ShaderStage::Fragment}}));
}
