#include <filesystem>
#include <fstream>

#include <atlantis/shader_system/reflection_metadata.h>
#include <atlantis/shader_system/slang_json_transform.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::shader_system::DescriptorType;
using atlantis::shader_system::ShaderStage;
using atlantis::shader_system::transformSlangReflectionJson;
using atlantis::shader_system::TransformError;
using atlantis::shader_system::VertexAttributeType;

namespace {

// ATLANTIS_SHADER_SYSTEM_TEST_FIXTURES_DIR is injected by
// tests/shader_system/CMakeLists.txt as an absolute path to
// tests/shader_system/fixtures/ -- independent of whatever working
// directory ctest happens to invoke this executable from.
[[nodiscard]] std::filesystem::path fixturePath(const char* name) {
  return std::filesystem::path(ATLANTIS_SHADER_SYSTEM_TEST_FIXTURES_DIR) / name;
}

[[nodiscard]] std::filesystem::path writeTempFixture(const char* label, const std::string& content) {
  const auto path = std::filesystem::temp_directory_path() /
                     (std::string("atlantis_slang_json_transform_test_") + label + ".json");
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << content;
  return path;
}

}  // namespace

TEST_CASE("transformSlangReflectionJson() parses a real captured vertex-stage sample",
          "[shader_system][slang_json_transform]") {
  const auto result =
      transformSlangReflectionJson(fixturePath("vertex_reflection_sample.json"), "vertexMain", ShaderStage::Vertex,
                                    "test-provenance");
  REQUIRE(result.isOk());
  const auto& metadata = result.value();

  REQUIRE(metadata.entryPointName == "vertexMain");
  REQUIRE(metadata.stage == ShaderStage::Vertex);

  // camera: descriptorTableSlot, set 0 (absent "space"), binding 0, used
  // by vertexMain -- present.
  REQUIRE(metadata.descriptorBindings.size() == 1);
  REQUIRE(metadata.descriptorBindings[0].set == 0);
  REQUIRE(metadata.descriptorBindings[0].binding == 0);
  REQUIRE(metadata.descriptorBindings[0].type == DescriptorType::UniformBuffer);

  // pushConstants: offset 0, size 64 (one float4x4).
  REQUIRE(metadata.pushConstantRanges.size() == 1);
  REQUIRE(metadata.pushConstantRanges[0].offsetBytes == 0);
  REQUIRE(metadata.pushConstantRanges[0].sizeBytes == 64);

  // position @0, color @1 -- both explicit [[vk::location(N)]], both Float3.
  REQUIRE(metadata.vertexInputAttributes.size() == 2);
  REQUIRE(metadata.vertexInputAttributes[0].location == 0);
  REQUIRE(metadata.vertexInputAttributes[0].type == VertexAttributeType::Float3);
  REQUIRE(metadata.vertexInputAttributes[1].location == 1);
  REQUIRE(metadata.vertexInputAttributes[1].type == VertexAttributeType::Float3);

  // color is the only real varying output (SV_Position carries no
  // binding at all -- Spec 0008's own Validation Evidence finding).
  REQUIRE(metadata.varyingOutputLocations == std::vector<std::uint32_t>{0});
  REQUIRE(metadata.varyingInputLocations.empty());
}

TEST_CASE("transformSlangReflectionJson() parses a real captured fragment-stage sample, filtering by used",
          "[shader_system][slang_json_transform]") {
  const auto result = transformSlangReflectionJson(fixturePath("fragment_reflection_sample.json"), "fragmentMain",
                                                     ShaderStage::Fragment, "test-provenance");
  REQUIRE(result.isOk());
  const auto& metadata = result.value();

  REQUIRE(metadata.entryPointName == "fragmentMain");
  REQUIRE(metadata.stage == ShaderStage::Fragment);

  // camera is module-scope but used == 0 for fragmentMain -- the
  // module-level-"parameters"-vs-entry-point-"bindings"-"used" filtering
  // rule (Plan 0008 Section 3) means it must NOT appear here, even
  // though the top-level "parameters" array lists it.
  REQUIRE(metadata.descriptorBindings.empty());

  // varying input: color @0 (position/SV_Position carries no binding).
  REQUIRE(metadata.varyingInputLocations == std::vector<std::uint32_t>{0});
  REQUIRE(metadata.vertexInputAttributes.empty());
  REQUIRE(metadata.varyingOutputLocations.empty());
}

TEST_CASE("transformSlangReflectionJson() parses a nonzero descriptor set (positive parser case)",
          "[shader_system][slang_json_transform]") {
  // Reuses ADR-0030's own [JSON-verified] {"kind": "descriptorTableSlot",
  // "space": 2, "index": 3} shape -- a real captured sample, not a
  // hand-written guess. This is a parser-level, expected-to-SUCCEED
  // case: slang_json_transform.cpp parses ANY set value, it does not
  // fail closed on a nonzero one (Plan 0008 Section 2/3). Contract-level
  // rejection of this same shape is descriptor_contract_tests.cpp's own,
  // separate case.
  const auto result = transformSlangReflectionJson(fixturePath("nonzero_set_reflection_sample.json"), "vertexMain",
                                                     ShaderStage::Vertex, "test-provenance");
  REQUIRE(result.isOk());
  const auto& metadata = result.value();
  REQUIRE(metadata.descriptorBindings.size() == 1);
  REQUIRE(metadata.descriptorBindings[0].set == 2);
  REQUIRE(metadata.descriptorBindings[0].binding == 3);
}

TEST_CASE("transformSlangReflectionJson() rejects a malformed \"space\"/\"index\" value",
          "[shader_system][slang_json_transform]") {
  // Distinct from, and not conflated with, the nonzero-set POSITIVE case
  // above -- a non-integer "index" is a resource-limit-driven parse
  // failure (Plan 0008 Section 3), not a nonzero-set-specific rejection.
  const auto path = writeTempFixture("malformed_index", R"({
    "parameters": [{"name": "camera", "binding": {"kind": "descriptorTableSlot", "index": 0},
                     "type": {"kind": "constantBuffer"}}],
    "entryPoints": [{"name": "vertexMain", "stage": "vertex",
                      "bindings": [{"name": "camera",
                                    "binding": {"kind": "descriptorTableSlot", "index": "not-a-number", "used": 1}}]}]
  })");
  const auto result = transformSlangReflectionJson(path, "vertexMain", ShaderStage::Vertex, "test-provenance");
  REQUIRE(result.isErr());
  REQUIRE(result.error() == TransformError::UnexpectedStructure);
  std::filesystem::remove(path);
}

TEST_CASE("transformSlangReflectionJson() requested entry point not found", "[shader_system][slang_json_transform]") {
  const auto result = transformSlangReflectionJson(fixturePath("vertex_reflection_sample.json"), "doesNotExist",
                                                     ShaderStage::Vertex, "test-provenance");
  REQUIRE(result.isErr());
  REQUIRE(result.error() == TransformError::EntryPointNotFound);
}

TEST_CASE("transformSlangReflectionJson() rejects malformed/unexpected top-level structure",
          "[shader_system][slang_json_transform]") {
  SECTION("not even valid JSON") {
    const auto path = writeTempFixture("not_json", "{not json at all");
    const auto result = transformSlangReflectionJson(path, "vertexMain", ShaderStage::Vertex, "p");
    REQUIRE(result.isErr());
    REQUIRE(result.error() == TransformError::MalformedJson);
    std::filesystem::remove(path);
  }

  SECTION("valid JSON but entryPoints is missing") {
    const auto path = writeTempFixture("missing_entry_points", R"({"parameters": []})");
    const auto result = transformSlangReflectionJson(path, "vertexMain", ShaderStage::Vertex, "p");
    REQUIRE(result.isErr());
    REQUIRE(result.error() == TransformError::UnexpectedStructure);
    std::filesystem::remove(path);
  }

  SECTION("nonexistent file") {
    const auto result = transformSlangReflectionJson(fixturePath("does_not_exist.json"), "vertexMain",
                                                       ShaderStage::Vertex, "p");
    REQUIRE(result.isErr());
    REQUIRE(result.error() == TransformError::FileNotFound);
  }
}

TEST_CASE("transformSlangReflectionJson() push-constant offset/size regression (issue #5676)",
          "[shader_system][slang_json_transform]") {
  // Cross-checks the reflected push-constant offset/size against the
  // shader's own declared layout, not merely that *a* value was
  // returned -- the concrete regression test Plan 0008 Section 3/9 both
  // require, against the real captured sample.
  const auto result =
      transformSlangReflectionJson(fixturePath("vertex_reflection_sample.json"), "vertexMain", ShaderStage::Vertex,
                                    "test-provenance");
  REQUIRE(result.isOk());
  REQUIRE(result.value().pushConstantRanges.size() == 1);
  REQUIRE(result.value().pushConstantRanges[0].offsetBytes == 0);
  REQUIRE(result.value().pushConstantRanges[0].sizeBytes == 64);
}
