#include <atlantis/asset_system/material_source.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kValidSource =
    "atlantis_material_source_version: 1\n"
    "kind: unlit_textured\n"
    "texture: textures/textured_quad_source_unorm.png\n"
    "filter: linear\n"
    "address_mode: repeat\n";

}  // namespace

TEST_CASE("parseMaterialSource parses a well-formed material", "[asset_system][material]") {
  const auto result = parseMaterialSource(kValidSource);
  REQUIRE(result.isOk());
  const ParsedMaterialSource& parsed = result.value();
  CHECK(parsed.kind == MaterialKind::UnlitTextured);
  CHECK(parsed.textureLogicalPath == "textures/textured_quad_source_unorm.png");
  CHECK(parsed.filter == MaterialSamplerFilter::Linear);
  CHECK(parsed.addressMode == MaterialSamplerAddressMode::Repeat);
}

TEST_CASE("parseMaterialSource parses nearest filter and clamp_to_edge address mode", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: nearest\n"
      "address_mode: clamp_to_edge\n");
  REQUIRE(result.isOk());
  CHECK(result.value().filter == MaterialSamplerFilter::Nearest);
  CHECK(result.value().addressMode == MaterialSamplerAddressMode::ClampToEdge);
}

TEST_CASE("parseMaterialSource round-trips through serializeMaterialSource", "[asset_system][material]") {
  const auto parsedResult = parseMaterialSource(kValidSource);
  REQUIRE(parsedResult.isOk());
  const std::string serialized = serializeMaterialSource(parsedResult.value());
  const auto reparsedResult = parseMaterialSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().kind == parsedResult.value().kind);
  CHECK(reparsedResult.value().textureLogicalPath == parsedResult.value().textureLogicalPath);
  CHECK(reparsedResult.value().filter == parsedResult.value().filter);
  CHECK(reparsedResult.value().addressMode == parsedResult.value().addressMode);
}

// Plan 0019 P5: parses the new kind: lit_textured token.
TEST_CASE("parseMaterialSource parses kind: lit_textured", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: lit_textured\n"
      "texture: textures/textured_quad_source_unorm.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isOk());
  CHECK(result.value().kind == MaterialKind::LitTextured);
}

// Real regression coverage: serializeMaterialSource() once hardcoded
// "unlit_textured" unconditionally (correct only by coincidence, since
// exactly one MaterialKind existed at the time) -- this test would have
// failed against that bug, proving Plan 0019's own fix (selecting the
// token from source.kind) is real, not merely decorative.
TEST_CASE("parseMaterialSource round-trips kind: lit_textured through serializeMaterialSource",
          "[asset_system][material]") {
  const auto parsedResult = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: lit_textured\n"
      "texture: textures/a.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(parsedResult.isOk());
  const std::string serialized = serializeMaterialSource(parsedResult.value());
  CHECK(serialized.find("kind: lit_textured\n") != std::string::npos);
  const auto reparsedResult = parseMaterialSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().kind == MaterialKind::LitTextured);
}

TEST_CASE("parseMaterialSource rejects an unknown source version", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 2\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMaterialSource rejects a source with too few lines", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MissingField);
}

TEST_CASE("parseMaterialSource rejects a source with trailing content", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "trailing: garbage\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::TrailingContent);
}

TEST_CASE("parseMaterialSource rejects a field-order mismatch", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "texture: textures/foo.png\n"
      "kind: unlit_textured\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMaterialSource rejects an unknown kind", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: pbr\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownKind);
}

TEST_CASE("parseMaterialSource rejects an unknown filter", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: bicubic\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownFilter);
}

TEST_CASE("parseMaterialSource rejects an unknown address mode", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: mirror\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownAddressMode);
}

TEST_CASE("parseMaterialSource rejects an empty texture logical path", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: unlit_textured\n"
      "texture: \n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MissingField);
}
