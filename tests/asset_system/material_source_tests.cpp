#include <atlantis/asset_system/material_source.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kValidSource =
    "atlantis_material_source_version: 8\n"
    "kind: unlit_textured\n"
    "texture: textures/textured_quad_source_unorm.png\n"
    "filter: linear\n"
    "address_mode: repeat\n";

constexpr std::string_view kValidPbrSource =
    "atlantis_material_source_version: 8\n"
    "kind: pbr_direct_lit\n"
    "texture: textures/textured_quad_source_srgb.png\n"
    "filter: linear\n"
    "address_mode: repeat\n"
    "base_color_factor: 0.8 0.2 0.1 1.0\n"
    "metallic_factor: 0.5\n"
    "roughness_factor: 0.25\n";

}  // namespace

TEST_CASE("parseMaterialSource parses a well-formed material", "[asset_system][material]") {
  const auto result = parseMaterialSource(kValidSource);
  REQUIRE(result.isOk());
  const ParsedMaterialSource& parsed = result.value();
  CHECK(parsed.kind == MaterialKind::UnlitTextured);
  CHECK(parsed.textureLogicalPath == "textures/textured_quad_source_unorm.png");
  CHECK(parsed.filter == MaterialSamplerFilter::Linear);
  CHECK(parsed.addressMode == MaterialSamplerAddressMode::Repeat);
  // Plan 0023 Milestone 1: the 5-line form (no PBR fields) decodes to
  // the documented inert defaults.
  CHECK(parsed.baseColorFactor[0] == 1.0f);
  CHECK(parsed.baseColorFactor[1] == 1.0f);
  CHECK(parsed.baseColorFactor[2] == 1.0f);
  CHECK(parsed.baseColorFactor[3] == 1.0f);
  CHECK(parsed.metallicFactor == 1.0f);
  CHECK(parsed.roughnessFactor == 1.0f);
}

TEST_CASE("parseMaterialSource parses nearest filter and clamp_to_edge address mode", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
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
  CHECK(reparsedResult.value().metallicFactor == parsedResult.value().metallicFactor);
  CHECK(reparsedResult.value().roughnessFactor == parsedResult.value().roughnessFactor);
}

// Plan 0019 P5: parses the new kind: lit_textured token.
TEST_CASE("parseMaterialSource parses kind: lit_textured", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
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
      "atlantis_material_source_version: 8\n"
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

// Plan 0023 Milestone 1: parses the new kind: pbr_direct_lit token, plus
// the 8-line form's own three new numeric fields.
TEST_CASE("parseMaterialSource parses kind: pbr_direct_lit with the 8-line PBR field block",
          "[asset_system][material]") {
  const auto result = parseMaterialSource(kValidPbrSource);
  REQUIRE(result.isOk());
  const ParsedMaterialSource& parsed = result.value();
  CHECK(parsed.kind == MaterialKind::PbrDirectLit);
  CHECK(parsed.baseColorFactor[0] == 0.8f);
  CHECK(parsed.baseColorFactor[1] == 0.2f);
  CHECK(parsed.baseColorFactor[2] == 0.1f);
  CHECK(parsed.baseColorFactor[3] == 1.0f);
  CHECK(parsed.metallicFactor == 0.5f);
  CHECK(parsed.roughnessFactor == 0.25f);
}

TEST_CASE("parseMaterialSource round-trips kind: pbr_direct_lit and its own PBR fields through serializeMaterialSource",
          "[asset_system][material]") {
  const auto parsedResult = parseMaterialSource(kValidPbrSource);
  REQUIRE(parsedResult.isOk());
  const std::string serialized = serializeMaterialSource(parsedResult.value());
  CHECK(serialized.find("kind: pbr_direct_lit\n") != std::string::npos);
  const auto reparsedResult = parseMaterialSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().kind == MaterialKind::PbrDirectLit);
  CHECK(reparsedResult.value().baseColorFactor[0] == parsedResult.value().baseColorFactor[0]);
  CHECK(reparsedResult.value().baseColorFactor[1] == parsedResult.value().baseColorFactor[1]);
  CHECK(reparsedResult.value().baseColorFactor[2] == parsedResult.value().baseColorFactor[2]);
  CHECK(reparsedResult.value().baseColorFactor[3] == parsedResult.value().baseColorFactor[3]);
  CHECK(reparsedResult.value().metallicFactor == parsedResult.value().metallicFactor);
  CHECK(reparsedResult.value().roughnessFactor == parsedResult.value().roughnessFactor);
}

// Real, disclosed regression coverage (found during Plan 0023's own
// final review): std::to_string(float)'s fixed 6-decimal-place
// formatting does not round-trip every float32 value exactly -- this
// would have failed against serializeMaterialSource()'s own former
// std::to_string()-based formatting, proving the fix (std::to_chars(),
// a shortest-round-trip guarantee) is real.
TEST_CASE("parseMaterialSource round-trips a metallic_factor/roughness_factor value std::to_string(float) would "
          "NOT preserve exactly",
          "[asset_system][material]") {
  const auto parsedResult = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.123456789\n"
      "roughness_factor: 0.333333333\n");
  REQUIRE(parsedResult.isOk());
  const std::string serialized = serializeMaterialSource(parsedResult.value());
  const auto reparsedResult = parseMaterialSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().metallicFactor == parsedResult.value().metallicFactor);
  CHECK(reparsedResult.value().roughnessFactor == parsedResult.value().roughnessFactor);
}

TEST_CASE("parseMaterialSource rejects a malformed base_color_factor component", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 0.8 0.2 not-a-number 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MalformedNumber);
}

TEST_CASE("parseMaterialSource rejects a malformed metallic_factor", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: not-a-number\n"
      "roughness_factor: 0.25\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MalformedNumber);
}

TEST_CASE("parseMaterialSource rejects a base_color_factor with the wrong token count", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MalformedNumber);
}

TEST_CASE("parseMaterialSource rejects an unknown source version", "[asset_system][material]") {
  // Plan 0042 Milestone 1: this literal must name a version still
  // genuinely unrecognized now that 8 (this round's own bump) is the real,
  // accepted version -- 9 here, not 8 (moved by hand, never by the
  // mechanical 7 -> 8 replace, which would have made it an accepted one).
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 9\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMaterialSource rejects the retired version 1", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 1\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMaterialSource rejects the retired version 2", "[asset_system][material]") {
  // Plan 0029: version 2 (no normal_map: support) is now also retired,
  // exactly like version 1 already was -- no dual-version reader.
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
      "atlantis_material_source_version: 8\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MissingField);
}

TEST_CASE("parseMaterialSource rejects a source with trailing content (a partial PBR field block)",
          "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "trailing: garbage\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::TrailingContent);
}

TEST_CASE("parseMaterialSource rejects a source with too many lines", "[asset_system][material]") {
  // Plan 0029: the legal ceiling widened from 8 to 9 (the optional
  // normal_map: line). Plan 0035 Milestone 2/ADR-0081: widened again to
  // 11 (PbrClearcoat's own 10/11-line forms) -- this fixture now needs
  // four trailing garbage lines (8 + 4 = 12 > 11), not two, to
  // genuinely exceed the new ceiling; kind stays pbr_direct_lit
  // specifically so a 10-line total (8 + 2 garbage) would otherwise hit
  // ClearcoatFieldsNotSupportedForKind first, not the TrailingContent
  // this test means to exercise.
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n"
      "extra: line\n"
      "another: line\n"
      "third: line\n"
      "fourth: line\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::TrailingContent);
}

TEST_CASE("parseMaterialSource rejects a 9-line source whose own 9th line does not name normal_map:",
          "[asset_system][material]") {
  // Plan 0029: exactly 9 lines is now a legal line COUNT, but the 9th
  // line's own field name is still checked -- a mismatch is
  // FieldOrderMismatch, not TrailingContent (that requires exceeding
  // the line-count ceiling itself, covered above).
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n"
      "extra: line\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMaterialSource rejects a field-order mismatch", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "texture: textures/foo.png\n"
      "kind: unlit_textured\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMaterialSource rejects an unknown kind", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownKind);
}

TEST_CASE("parseMaterialSource rejects an unknown filter", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: bicubic\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownFilter);
}

TEST_CASE("parseMaterialSource rejects an unknown address mode", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: mirror\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownAddressMode);
}

TEST_CASE("parseMaterialSource rejects an empty texture logical path", "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: unlit_textured\n"
      "texture: \n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MissingField);
}

// Plan 0029 Section P5/ADR-0074 Section 1: the 9-line form's own new
// normal_map: line.
TEST_CASE("parseMaterialSource parses the 9-line form's own normal_map: line for kind: pbr_direct_lit",
          "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n"
      "normal_map: textures/foo_normal.png\n");
  REQUIRE(result.isOk());
  CHECK(result.value().normalMapLogicalPath == "textures/foo_normal.png");
}

TEST_CASE("parseMaterialSource round-trips the 9-line form's own normal_map: line through serializeMaterialSource",
          "[asset_system][material]") {
  const auto parsedResult = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n"
      "normal_map: textures/foo_normal.png\n");
  REQUIRE(parsedResult.isOk());
  const std::string serialized = serializeMaterialSource(parsedResult.value());
  CHECK(serialized.find("normal_map: textures/foo_normal.png\n") != std::string::npos);
  const auto reparsedResult = parseMaterialSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().normalMapLogicalPath == "textures/foo_normal.png");
}

TEST_CASE("serializeMaterialSource omits the normal_map: line when absent, round-tripping the 8-line form exactly",
          "[asset_system][material]") {
  const auto parsedResult = parseMaterialSource(kValidPbrSource);
  REQUIRE(parsedResult.isOk());
  REQUIRE(parsedResult.value().normalMapLogicalPath.empty());
  const std::string serialized = serializeMaterialSource(parsedResult.value());
  CHECK(serialized.find("normal_map:") == std::string::npos);
  const auto reparsedResult = parseMaterialSource(serialized);
  REQUIRE(reparsedResult.isOk());
  CHECK(reparsedResult.value().normalMapLogicalPath.empty());
}

TEST_CASE("parseMaterialSource rejects normal_map: for kind: unlit_textured with NormalMapNotSupportedForKind",
          "[asset_system][material]") {
  // ADR-0074 Section 1: a 9-line source is legal only for
  // kind: pbr_direct_lit -- neither unlit_textured.slang nor
  // lit_textured.slang declares a normal-map binding to consume it.
  // The three numeric fields (positionally required for any 9-line
  // shape, kind-independent, exactly like the existing 8-line form)
  // must still be present syntactically to reach the kind check at
  // line 9 -- this is not itself the thing under test.
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n"
      "normal_map: textures/foo_normal.png\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::NormalMapNotSupportedForKind);
}

TEST_CASE("parseMaterialSource rejects normal_map: for kind: lit_textured with NormalMapNotSupportedForKind",
          "[asset_system][material]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: lit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n"
      "normal_map: textures/foo_normal.png\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::NormalMapNotSupportedForKind);
}

TEST_CASE("parseMaterialSource rejects an empty normal_map: value with MissingField", "[asset_system][material]") {
  // ADR-0074 Section 1: the same rule texture:'s own empty-value case
  // already uses -- an author who wants "no normal map" omits the line
  // entirely, exactly like every other optional field in this grammar.
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_direct_lit\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.5\n"
      "roughness_factor: 0.25\n"
      "normal_map: \n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::MissingField);
}

// ---------------------------------------------------------------------------
// Plan 0041 Milestone 1 (Spec 0041 R3/R4, ruling O1): the optional,
// prefix-identified emissive_factor line.
// ---------------------------------------------------------------------------

namespace {

constexpr std::string_view kEightLinePbrPrefix =
    "atlantis_material_source_version: 8\n"
    "kind: pbr_direct_lit\n"
    "texture: textures/foo.png\n"
    "filter: linear\n"
    "address_mode: repeat\n"
    "base_color_factor: 1.0 1.0 1.0 1.0\n"
    "metallic_factor: 0.0\n"
    "roughness_factor: 0.5\n";

}  // namespace

TEST_CASE("parseMaterialSource: an absent emissive_factor line leaves (0, 0, 0)", "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(kEightLinePbrPrefix);
  REQUIRE(result.isOk());
  CHECK(result.value().emissiveFactor[0] == 0.0f);
  CHECK(result.value().emissiveFactor[1] == 0.0f);
  CHECK(result.value().emissiveFactor[2] == 0.0f);
}

TEST_CASE("parseMaterialSource: pbr_direct_lit accepts emissive_factor directly after the factors, HDR values intact",
          "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(std::string(kEightLinePbrPrefix) + "emissive_factor: 40.0 0.5 0.0\n");
  REQUIRE(result.isOk());
  CHECK(result.value().emissiveFactor[0] == 40.0f);
  CHECK(result.value().emissiveFactor[1] == 0.5f);
  CHECK(result.value().emissiveFactor[2] == 0.0f);
  CHECK(result.value().normalMapLogicalPath.empty());
}

TEST_CASE("parseMaterialSource: emissive_factor sits before normal_map (10-line pbr_direct_lit)",
          "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(std::string(kEightLinePbrPrefix) +
                                          "emissive_factor: 1.0 2.0 3.0\n"
                                          "normal_map: textures/n.png\n");
  REQUIRE(result.isOk());
  CHECK(result.value().emissiveFactor[2] == 3.0f);
  CHECK(result.value().normalMapLogicalPath == "textures/n.png");
}

TEST_CASE("parseMaterialSource: after normal_map, emissive_factor is out of order", "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(std::string(kEightLinePbrPrefix) +
                                          "normal_map: textures/n.png\n"
                                          "emissive_factor: 1.0 2.0 3.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMaterialSource: pbr_sheen takes emissive_factor after its own pair (the 12-line maximum)",
          "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_sheen\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.0\n"
      "roughness_factor: 0.5\n"
      "sheen_color: 0.5 0.5 0.5\n"
      "sheen_roughness: 0.3\n"
      "emissive_factor: 0.25 0.5 0.75\n"
      "normal_map: textures/n.png\n");
  REQUIRE(result.isOk());
  CHECK(result.value().sheenRoughness == 0.3f);
  CHECK(result.value().emissiveFactor[1] == 0.5f);
  CHECK(result.value().normalMapLogicalPath == "textures/n.png");
}

TEST_CASE("parseMaterialSource: emissive_factor before a kind's own required pair is out of order",
          "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_clearcoat\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.0\n"
      "roughness_factor: 0.5\n"
      "emissive_factor: 0.25 0.5 0.75\n"
      "clearcoat_factor: 1.0\n"
      "clearcoat_roughness: 0.1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMaterialSource rejects emissive_factor on lit_textured (EmissiveNotSupportedForKind)",
          "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: lit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 1.0\n"
      "roughness_factor: 1.0\n"
      "emissive_factor: 1.0 1.0 1.0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::EmissiveNotSupportedForKind);
}

TEST_CASE("parseMaterialSource rejects a malformed emissive_factor", "[asset_system][material][emissive]") {
  const auto twoTokens = parseMaterialSource(std::string(kEightLinePbrPrefix) + "emissive_factor: 1.0 2.0\n");
  REQUIRE(twoTokens.isErr());
  CHECK(twoTokens.error() == MaterialSourceParseError::MalformedNumber);
  const auto notANumber = parseMaterialSource(std::string(kEightLinePbrPrefix) + "emissive_factor: 1.0 x 2.0\n");
  REQUIRE(notANumber.isErr());
  CHECK(notANumber.error() == MaterialSourceParseError::MalformedNumber);
}

TEST_CASE("parseMaterialSource rejects the retired version 6", "[asset_system][material][emissive]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 6\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownSourceVersion);
}

TEST_CASE("serializeMaterialSource writes emissive_factor only when non-zero, and it round-trips",
          "[asset_system][material][emissive]") {
  const auto plain = parseMaterialSource(kEightLinePbrPrefix);
  REQUIRE(plain.isOk());
  CHECK(serializeMaterialSource(plain.value()).find("emissive_factor") == std::string::npos);

  const auto emissive = parseMaterialSource(std::string(kEightLinePbrPrefix) +
                                            "emissive_factor: 0.1 65504 0.333333333\n"
                                            "normal_map: textures/n.png\n");
  REQUIRE(emissive.isOk());
  const std::string text = serializeMaterialSource(emissive.value());
  CHECK(text.find("emissive_factor: ") != std::string::npos);
  const auto reparsed = parseMaterialSource(text);
  REQUIRE(reparsed.isOk());
  for (int i = 0; i < 3; ++i) CHECK(reparsed.value().emissiveFactor[i] == emissive.value().emissiveFactor[i]);
  CHECK(reparsed.value().normalMapLogicalPath == "textures/n.png");
}

// ---------------------------------------------------------------------------
// Plan 0042 Milestone 1 (Spec 0042 R1-R3, Plan 0042 P1/P2): the optional
// alpha_mode/alpha_cutoff lines, after emissive_factor, walked by the
// generalized optional-line table.
// ---------------------------------------------------------------------------

namespace {

constexpr std::string_view kEightLineSheenPrefix =
    "atlantis_material_source_version: 8\n"
    "kind: pbr_sheen\n"
    "texture: textures/foo.png\n"
    "filter: linear\n"
    "address_mode: repeat\n"
    "base_color_factor: 1.0 1.0 1.0 1.0\n"
    "metallic_factor: 0.0\n"
    "roughness_factor: 0.5\n"
    "sheen_color: 0.5 0.5 0.5\n"
    "sheen_roughness: 0.3\n";

}  // namespace

TEST_CASE("parseMaterialSource: absent alpha lines leave Opaque and cutoff 0.5", "[asset_system][material][transparency]") {
  const auto result = parseMaterialSource(kEightLinePbrPrefix);
  REQUIRE(result.isOk());
  CHECK(result.value().alphaMode == MaterialAlphaMode::Opaque);
  CHECK(result.value().alphaCutoff == 0.5f);
}

TEST_CASE("parseMaterialSource: each alpha_mode value parses", "[asset_system][material][transparency]") {
  const auto opaque = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: opaque\n");
  REQUIRE(opaque.isOk());
  CHECK(opaque.value().alphaMode == MaterialAlphaMode::Opaque);
  const auto mask = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: mask\n");
  REQUIRE(mask.isOk());
  CHECK(mask.value().alphaMode == MaterialAlphaMode::Mask);
  const auto blend = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: blend\n");
  REQUIRE(blend.isOk());
  CHECK(blend.value().alphaMode == MaterialAlphaMode::Blend);
}

TEST_CASE("parseMaterialSource: alpha_cutoff alone, and with alpha_mode", "[asset_system][material][transparency]") {
  const auto cutoffOnly = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_cutoff: 0.25\n");
  REQUIRE(cutoffOnly.isOk());
  CHECK(cutoffOnly.value().alphaMode == MaterialAlphaMode::Opaque);
  CHECK(cutoffOnly.value().alphaCutoff == 0.25f);
  const auto both =
      parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: mask\nalpha_cutoff: 0.75\n");
  REQUIRE(both.isOk());
  CHECK(both.value().alphaMode == MaterialAlphaMode::Mask);
  CHECK(both.value().alphaCutoff == 0.75f);
}

TEST_CASE("parseMaterialSource: all three optional lines plus a kind pair and normal_map (the 14-line maximum)",
          "[asset_system][material][transparency]") {
  const auto result = parseMaterialSource(std::string(kEightLineSheenPrefix) +
                                          "emissive_factor: 0.25 0.5 0.75\n"
                                          "alpha_mode: blend\n"
                                          "alpha_cutoff: 0.1\n"
                                          "normal_map: textures/n.png\n");
  REQUIRE(result.isOk());
  CHECK(result.value().kind == MaterialKind::PbrSheen);
  CHECK(result.value().sheenRoughness == 0.3f);
  CHECK(result.value().emissiveFactor[2] == 0.75f);
  CHECK(result.value().alphaMode == MaterialAlphaMode::Blend);
  CHECK(result.value().alphaCutoff == 0.1f);
  CHECK(result.value().normalMapLogicalPath == "textures/n.png");

  const auto tooMany = parseMaterialSource(std::string(kEightLineSheenPrefix) +
                                           "emissive_factor: 0.25 0.5 0.75\n"
                                           "alpha_mode: blend\n"
                                           "alpha_cutoff: 0.1\n"
                                           "normal_map: textures/n.png\n"
                                           "extra: 1\n");
  REQUIRE(tooMany.isErr());
  CHECK(tooMany.error() == MaterialSourceParseError::TrailingContent);
}

TEST_CASE("parseMaterialSource: optional lines out of table order are FieldOrderMismatch",
          "[asset_system][material][transparency]") {
  const auto modeBeforeEmissive =
      parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: mask\nemissive_factor: 1.0 1.0 1.0\n");
  REQUIRE(modeBeforeEmissive.isErr());
  CHECK(modeBeforeEmissive.error() == MaterialSourceParseError::FieldOrderMismatch);

  const auto cutoffBeforeMode =
      parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_cutoff: 0.5\nalpha_mode: mask\n");
  REQUIRE(cutoffBeforeMode.isErr());
  CHECK(cutoffBeforeMode.error() == MaterialSourceParseError::FieldOrderMismatch);

  const auto duplicate =
      parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: mask\nalpha_mode: blend\n");
  REQUIRE(duplicate.isErr());
  CHECK(duplicate.error() == MaterialSourceParseError::FieldOrderMismatch);

  const auto afterNormalMap =
      parseMaterialSource(std::string(kEightLinePbrPrefix) + "normal_map: textures/n.png\nalpha_mode: mask\n");
  REQUIRE(afterNormalMap.isErr());
  CHECK(afterNormalMap.error() == MaterialSourceParseError::FieldOrderMismatch);

  const auto beforeKindPair = parseMaterialSource(
      "atlantis_material_source_version: 8\n"
      "kind: pbr_anisotropic\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 0.0\n"
      "roughness_factor: 0.5\n"
      "alpha_mode: mask\n"
      "anisotropy_factor: 0.5\n"
      "anisotropy_rotation: 0.1\n");
  REQUIRE(beforeKindPair.isErr());
  CHECK(beforeKindPair.error() == MaterialSourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMaterialSource rejects the alpha lines on lit_textured/unlit_textured (AlphaModeNotSupportedForKind)",
          "[asset_system][material][transparency]") {
  const std::string litPrefix =
      "atlantis_material_source_version: 8\n"
      "kind: lit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n"
      "base_color_factor: 1.0 1.0 1.0 1.0\n"
      "metallic_factor: 1.0\n"
      "roughness_factor: 1.0\n";
  const auto litMode = parseMaterialSource(litPrefix + "alpha_mode: opaque\n");
  REQUIRE(litMode.isErr());
  CHECK(litMode.error() == MaterialSourceParseError::AlphaModeNotSupportedForKind);
  const auto litCutoff = parseMaterialSource(litPrefix + "alpha_cutoff: 0.5\n");
  REQUIRE(litCutoff.isErr());
  CHECK(litCutoff.error() == MaterialSourceParseError::AlphaModeNotSupportedForKind);

  std::string unlitPrefix = litPrefix;
  unlitPrefix.replace(unlitPrefix.find("lit_textured"), 12, "unlit_textured");
  const auto unlitMode = parseMaterialSource(unlitPrefix + "alpha_mode: blend\n");
  REQUIRE(unlitMode.isErr());
  CHECK(unlitMode.error() == MaterialSourceParseError::AlphaModeNotSupportedForKind);
}

TEST_CASE("parseMaterialSource rejects a malformed alpha line", "[asset_system][material][transparency]") {
  const auto unknownMode = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: MASK\n");
  REQUIRE(unknownMode.isErr());
  CHECK(unknownMode.error() == MaterialSourceParseError::UnknownAlphaMode);
  const auto badCutoff = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_cutoff: half\n");
  REQUIRE(badCutoff.isErr());
  CHECK(badCutoff.error() == MaterialSourceParseError::MalformedNumber);
  const auto twoTokens = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_cutoff: 0.5 0.5\n");
  REQUIRE(twoTokens.isErr());
  CHECK(twoTokens.error() == MaterialSourceParseError::MalformedNumber);
}

TEST_CASE("parseMaterialSource rejects the retired version 7", "[asset_system][material][transparency]") {
  const auto result = parseMaterialSource(
      "atlantis_material_source_version: 7\n"
      "kind: unlit_textured\n"
      "texture: textures/foo.png\n"
      "filter: linear\n"
      "address_mode: repeat\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialSourceParseError::UnknownSourceVersion);
}

TEST_CASE("serializeMaterialSource writes the alpha lines only when non-default, and they round-trip",
          "[asset_system][material][transparency]") {
  const auto plain = parseMaterialSource(kEightLinePbrPrefix);
  REQUIRE(plain.isOk());
  const std::string plainText = serializeMaterialSource(plain.value());
  CHECK(plainText.find("alpha_") == std::string::npos);

  const auto masked = parseMaterialSource(std::string(kEightLineSheenPrefix) +
                                          "emissive_factor: 1 2 3\n"
                                          "alpha_mode: mask\n"
                                          "alpha_cutoff: 0.333333333\n"
                                          "normal_map: textures/n.png\n");
  REQUIRE(masked.isOk());
  const std::string text = serializeMaterialSource(masked.value());
  CHECK(text.find("alpha_mode: mask\n") != std::string::npos);
  CHECK(text.find("alpha_cutoff: ") != std::string::npos);
  const auto reparsed = parseMaterialSource(text);
  REQUIRE(reparsed.isOk());
  CHECK(reparsed.value().alphaMode == MaterialAlphaMode::Mask);
  CHECK(reparsed.value().alphaCutoff == masked.value().alphaCutoff);
  CHECK(reparsed.value().emissiveFactor[1] == 2.0f);
  CHECK(reparsed.value().normalMapLogicalPath == "textures/n.png");

  const auto blended = parseMaterialSource(std::string(kEightLinePbrPrefix) + "alpha_mode: blend\n");
  REQUIRE(blended.isOk());
  const std::string blendText = serializeMaterialSource(blended.value());
  CHECK(blendText.find("alpha_mode: blend\n") != std::string::npos);
  CHECK(blendText.find("alpha_cutoff") == std::string::npos);
}
