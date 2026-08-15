#include <filesystem>
#include <fstream>

#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/reflection_metadata.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::DescriptorBinding;
using atlantis::shader_system::DescriptorType;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::PushConstantRange;
using atlantis::shader_system::ReflectionLoadError;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::saveReflectionMetadata;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::VertexAttributeType;
using atlantis::shader_system::VertexInputAttribute;

namespace {

[[nodiscard]] std::filesystem::path uniqueTempJsonPath(const char* label) {
  return std::filesystem::temp_directory_path() /
         (std::string("atlantis_shader_system_test_") + label + ".json");
}

void writeRawFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << content;
}

}  // namespace

TEST_CASE("saveReflectionMetadata()/loadReflectionMetadata() round-trip a full fixture value",
          "[shader_system][reflection_metadata]") {
  ReflectionMetadata original;
  original.entryPointName = "vertexMain";
  original.stage = ShaderStage::Vertex;
  original.descriptorBindings = {
      DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex}};
  original.pushConstantRanges = {PushConstantRange{.offsetBytes = 0, .sizeBytes = 64, .stage = ShaderStage::Vertex}};
  original.vertexInputAttributes = {
      VertexInputAttribute{.location = 0, .type = VertexAttributeType::Float3},
      VertexInputAttribute{.location = 1, .type = VertexAttributeType::Float3},
  };
  original.varyingOutputLocations = {0};
  original.sdkProvenance = "1.4.357.0 / slang-standard-module-2026.13.1";

  const auto path = uniqueTempJsonPath("roundtrip");
  const auto saveResult = saveReflectionMetadata(original, path);
  REQUIRE(saveResult.isOk());

  const auto loadResult = loadReflectionMetadata(path);
  REQUIRE(loadResult.isOk());
  REQUIRE(loadResult.value() == original);

  std::filesystem::remove(path);
}

TEST_CASE("loadReflectionMetadata() required-field handling", "[shader_system][reflection_metadata]") {
  SECTION("a nonexistent file is FileNotFound") {
    const auto result = loadReflectionMetadata(uniqueTempJsonPath("does_not_exist"));
    REQUIRE(result.isErr());
    REQUIRE(result.error() == ReflectionLoadError::FileNotFound);
  }

  SECTION("missing entryPointName is MissingRequiredField") {
    const auto path = uniqueTempJsonPath("missing_entry_point_name");
    writeRawFile(path, R"({"schemaVersion": 1, "stage": "vertex"})");
    const auto result = loadReflectionMetadata(path);
    REQUIRE(result.isErr());
    REQUIRE(result.error() == ReflectionLoadError::MissingRequiredField);
    std::filesystem::remove(path);
  }

  SECTION("an unknown extra top-level field is ignored, not an error") {
    const auto path = uniqueTempJsonPath("unknown_field");
    writeRawFile(path,
                 R"({"schemaVersion": 1, "entryPointName": "vertexMain", "stage": "vertex", "somethingNew": 42})");
    const auto result = loadReflectionMetadata(path);
    REQUIRE(result.isOk());
    REQUIRE(result.value().entryPointName == "vertexMain");
    std::filesystem::remove(path);
  }

  SECTION("schemaVersion newer than kCurrentSchemaVersion is UnsupportedSchemaVersion") {
    const auto path = uniqueTempJsonPath("future_schema");
    writeRawFile(path, R"({"schemaVersion": 999, "entryPointName": "vertexMain", "stage": "vertex"})");
    const auto result = loadReflectionMetadata(path);
    REQUIRE(result.isErr());
    REQUIRE(result.error() == ReflectionLoadError::UnsupportedSchemaVersion);
    std::filesystem::remove(path);
  }

  SECTION("absent optional arrays default to empty, not an error") {
    const auto path = uniqueTempJsonPath("no_optional_arrays");
    writeRawFile(path, R"({"schemaVersion": 1, "entryPointName": "fragmentMain", "stage": "fragment"})");
    const auto result = loadReflectionMetadata(path);
    REQUIRE(result.isOk());
    REQUIRE(result.value().descriptorBindings.empty());
    REQUIRE(result.value().pushConstantRanges.empty());
    REQUIRE(result.value().vertexInputAttributes.empty());
    std::filesystem::remove(path);
  }
}
