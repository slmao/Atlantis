#include <algorithm>

#include <atlantis/shader_system/reflection_metadata.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::VertexAttributeFormat;
using atlantis::shader_system::PushConstantRange;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::VertexAttributeType;
using atlantis::shader_system::VertexInputAttribute;
using atlantis::shader_system::rhi_integration::MappingError;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toPushConstantSize;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

namespace {

[[nodiscard]] ReflectionMetadata twoAttributeVertexMetadata() {
  ReflectionMetadata metadata;
  metadata.entryPointName = "vertexMain";
  metadata.stage = ShaderStage::Vertex;
  metadata.vertexInputAttributes = {
      VertexInputAttribute{.location = 0, .type = VertexAttributeType::Float3},
      VertexInputAttribute{.location = 1, .type = VertexAttributeType::Float3},
  };
  return metadata;
}

}  // namespace

TEST_CASE("toVertexInputLayout() combines matching reflected/schema entries correctly",
          "[shader_system][rhi_integration][vertex_input_mapping]") {
  const auto metadata = twoAttributeVertexMetadata();
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = 12},
  };

  const auto result = toVertexInputLayout(metadata, schema, 24);
  REQUIRE(result.isOk());
  const auto& layout = result.value();
  REQUIRE(layout.strideBytes == 24);
  REQUIRE(layout.attributes.size() == 2);

  const auto findAttr = [&](std::uint32_t location) {
    return std::find_if(layout.attributes.begin(), layout.attributes.end(),
                         [location](const auto& a) { return a.location == location; });
  };
  const auto attr0 = findAttr(0);
  REQUIRE(attr0 != layout.attributes.end());
  REQUIRE(attr0->offsetBytes == 0);
  REQUIRE(attr0->format == VertexAttributeFormat::Float3);

  const auto attr1 = findAttr(1);
  REQUIRE(attr1 != layout.attributes.end());
  REQUIRE(attr1->offsetBytes == 12);
}

TEST_CASE("toVertexInputLayout() matches by location, not array position",
          "[shader_system][rhi_integration][vertex_input_mapping]") {
  const auto metadata = twoAttributeVertexMetadata();
  // Deliberately reversed order relative to metadata's own attribute
  // order -- matching is by `location`, not position.
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = 12},
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0},
  };

  const auto result = toVertexInputLayout(metadata, schema, 24);
  REQUIRE(result.isOk());
}

TEST_CASE("toVertexInputLayout() a schema entry with no matching reflected location fails",
          "[shader_system][rhi_integration][vertex_input_mapping]") {
  ReflectionMetadata metadata;
  metadata.vertexInputAttributes = {VertexInputAttribute{.location = 0, .type = VertexAttributeType::Float3}};
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 5, .offsetBytes = 0}};  // count matches (1 == 1), but location doesn't

  const auto result = toVertexInputLayout(metadata, schema, 12);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == MappingError::LocationNotFoundInSchema);
}

TEST_CASE("toVertexInputLayout() a reflected location with no matching schema entry fails",
          "[shader_system][rhi_integration][vertex_input_mapping]") {
  const auto metadata = twoAttributeVertexMetadata();
  const std::vector<MeshVertexAttributeSchema> schema = {MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0}};

  const auto result = toVertexInputLayout(metadata, schema, 12);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == MappingError::AttributeCountMismatch);
}

TEST_CASE("toPushConstantSize() sums correctly, including the zero-ranges case",
          "[shader_system][rhi_integration][vertex_input_mapping]") {
  SECTION("no push-constant ranges at all returns 0, not an error") {
    ReflectionMetadata metadata;
    REQUIRE(toPushConstantSize(metadata) == 0);
  }

  SECTION("a single range") {
    ReflectionMetadata metadata;
    metadata.pushConstantRanges = {PushConstantRange{.offsetBytes = 0, .sizeBytes = 64, .stage = ShaderStage::Vertex}};
    REQUIRE(toPushConstantSize(metadata) == 64);
  }

  SECTION("multiple ranges sum together") {
    ReflectionMetadata metadata;
    metadata.pushConstantRanges = {
        PushConstantRange{.offsetBytes = 0, .sizeBytes = 64, .stage = ShaderStage::Vertex},
        PushConstantRange{.offsetBytes = 64, .sizeBytes = 16, .stage = ShaderStage::Vertex},
    };
    REQUIRE(toPushConstantSize(metadata) == 80);
  }
}
