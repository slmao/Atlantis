// Plan 0057 M1 (Spec 0057 R3, R6, R7; ADR-0111 D1/D2/D5; J2, J4): the
// binding generator -- deterministic, LF-only, refusing what it cannot emit --
// and the staleness of the two committed headers it produced. A stale header
// fails here with the command that regenerates it; the expected text is
// written to the build tree (J4), never over the committed file.

#include "generate_bindings.h"
#include "synthetic_schema.h"

#include <atlantis/connection/text.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace codegen = atlantis::tools::sdk_codegen;
namespace schema = atlantis::schema;

[[nodiscard]] codegen::GeneratorOptions syntheticOptions() {
  codegen::GeneratorOptions options;
  options.modulePrefix = "synthetic::";
  options.targetNamespace = "atlantis::gameplay::synthetic";
  options.source = "the synthetic test schema (tests/tools/sdk_codegen/synthetic_schema.h)";
  options.regenerate =
      "run atlantis_sdk_codegen_tests; on a mismatch copy <build>/sdk_codegen/synthetic.h.expected over this file";
  return options;
}

[[nodiscard]] std::string readBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string generate(std::span<const schema::TypeDescriptor> table,
                                   const codegen::GeneratorOptions& options) {
  const auto generated = codegen::generateBindings(table, options);
  REQUIRE(generated.isOk());
  return generated.value();
}

// J4: compares the committed bytes; on a mismatch writes the expected text
// to the build tree and fails with how to regenerate.
void checkCurrent(const std::string& expected, const fs::path& committed, std::string_view name,
                  std::string_view regenerate) {
  const std::string actual = readBytes(committed);
  if (actual == expected) {
    SUCCEED();
    return;
  }
  const fs::path dir = fs::path(ATLANTIS_SDK_CODEGEN_EXPECTED_DIR);
  fs::create_directories(dir);
  const fs::path out = dir / (std::string(name) + ".expected");
  {
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    file << expected;
  }
  FAIL(committed.generic_string() << " is stale (" << actual.size() << " bytes committed, " << expected.size()
                                  << " generated).\nThe expected text is in " << out.generic_string()
                                  << ".\nRegenerate: " << regenerate);
}

[[nodiscard]] std::size_t occurrences(const std::string& text, const std::string& needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}

}  // namespace

TEST_CASE("the committed World bindings are what the generator writes for worldSchema()",
          "[tools][sdk_codegen][staleness]") {
  checkCurrent(generate(atlantis::world::worldSchema(), codegen::worldBindingOptions()), ATLANTIS_SDK_GENERATED_WORLD_H,
               "world.h", codegen::worldBindingOptions().regenerate);
}

TEST_CASE("the committed synthetic bindings are what the generator writes for the synthetic schema",
          "[tools][sdk_codegen][staleness]") {
  checkCurrent(generate(atlantis::test::synthetic_schema::table(), syntheticOptions()),
               ATLANTIS_SDK_GENERATED_SYNTHETIC_H, "synthetic.h", syntheticOptions().regenerate);
}

TEST_CASE("the generator is deterministic and writes LF only", "[tools][sdk_codegen]") {
  const std::string first = generate(atlantis::world::worldSchema(), codegen::worldBindingOptions());
  CHECK(generate(atlantis::world::worldSchema(), codegen::worldBindingOptions()) == first);
  CHECK(first.find('\r') == std::string::npos);
  const std::string synthetic = generate(atlantis::test::synthetic_schema::table(), syntheticOptions());
  CHECK(generate(atlantis::test::synthetic_schema::table(), syntheticOptions()) == synthetic);
  CHECK(synthetic.find('\r') == std::string::npos);
}

TEST_CASE("generated code carries no byte offset and no World C++ type (R6)", "[tools][sdk_codegen]") {
  for (const std::string& text : {generate(atlantis::world::worldSchema(), codegen::worldBindingOptions()),
                                  generate(atlantis::test::synthetic_schema::table(), syntheticOptions())}) {
    CHECK(text.find("byteOffset") == std::string::npos);
    CHECK(text.find("atlantis::world::") == std::string::npos);
    CHECK(text.find("atlantis/world/") == std::string::npos);
  }
}

TEST_CASE("every leaf of every World component has exactly one typed handle", "[tools][sdk_codegen]") {
  const auto table = atlantis::world::worldSchema();
  const std::string text = generate(table, codegen::worldBindingOptions());
  for (const std::string_view component : {"Transform", "Camera", "Light", "Renderable", "WorldMatrix"}) {
    const auto* type = atlantis::connection::text::findType(table, component);
    REQUIRE(type != nullptr);
    const auto leaves = atlantis::connection::text::leavesOf(table, type->id);
    REQUIRE_FALSE(leaves.empty());
    for (const auto& leaf : leaves) {
      INFO(leaf.path);
      CHECK(occurrences(text, "\"" + leaf.path + "\"") == 1);
    }
  }
  // The nested structs are not components: no handle object of their own.
  CHECK(text.find("inline constexpr CameraFogFieldSet") == std::string::npos);
  CHECK(text.find("inline constexpr CameraBloomFieldSet") == std::string::npos);
}

TEST_CASE("enumerators carry the constants' exact values, never ordinals (R10)", "[tools][sdk_codegen]") {
  const std::string text = generate(atlantis::test::synthetic_schema::table(), syntheticOptions());
  CHECK(text.find("enum class Mode : std::int64_t {\n  A = 3,\n  B = -2,\n  C = 7,\n};") != std::string::npos);
  CHECK(text.find("enum class Phase : std::int64_t {\n  First = 5,\n  Zero = 0,\n  Last = 9,\n};") !=
        std::string::npos);
  const std::string world = generate(atlantis::world::worldSchema(), codegen::worldBindingOptions());
  CHECK(world.find("enum class LightKind : std::int64_t {\n  Directional = 0,\n  Point = 1,\n};") !=
        std::string::npos);
}

TEST_CASE("the generator refuses what it cannot emit", "[tools][sdk_codegen]") {
  using schema::FieldDescriptor;
  using schema::FieldFlags;
  using schema::PrimitiveKind;
  using schema::TypeDescriptor;
  using schema::TypeKind;
  const auto failureOf = [](std::span<const TypeDescriptor> table) {
    const auto generated = codegen::generateBindings(table, syntheticOptions());
    REQUIRE(generated.isErr());
    return generated.error();
  };
  const auto field = [](std::string_view owner, std::string_view name) {
    return FieldDescriptor{schema::fieldId(owner, name), name, TypeKind::Primitive, PrimitiveKind::Float32,
                           schema::TypeId{}, FieldFlags::Editable, 0};
  };

  SECTION("a C++ keyword as a type name") {
    static constexpr std::array fields{field("synthetic::delete", "x")};
    const std::array table{TypeDescriptor{schema::typeId("synthetic::delete"), "synthetic::delete", TypeKind::Struct,
                                          1, fields, {}}};
    CHECK(failureOf(table).error == codegen::CodegenError::ReservedName);
  }
  SECTION("a keyword as a field name") {
    static constexpr std::array fields{field("synthetic::T", "class")};
    const std::array table{TypeDescriptor{schema::typeId("synthetic::T"), "synthetic::T", TypeKind::Struct, 1, fields, {}}};
    CHECK(failureOf(table).error == codegen::CodegenError::ReservedName);
    CHECK(failureOf(table).name == "class");
  }
  SECTION("a name the generated header uses itself") {
    static constexpr std::array fields{field("synthetic::fields", "x")};
    const std::array table{TypeDescriptor{schema::typeId("synthetic::fields"), "synthetic::fields", TypeKind::Struct,
                                          1, fields, {}}};
    CHECK(failureOf(table).error == codegen::CodegenError::ReservedName);
  }
  SECTION("a type outside the module prefix, or nested below it") {
    static constexpr std::array outside{field("other::T", "x")};
    const std::array table{TypeDescriptor{schema::typeId("other::T"), "other::T", TypeKind::Struct, 1, outside, {}}};
    CHECK(failureOf(table).error == codegen::CodegenError::InvalidName);
    static constexpr std::array nested{field("synthetic::a::T", "x")};
    const std::array deeper{
        TypeDescriptor{schema::typeId("synthetic::a::T"), "synthetic::a::T", TypeKind::Struct, 1, nested, {}}};
    CHECK(failureOf(deeper).error == codegen::CodegenError::InvalidName);
  }
  SECTION("an ill-formed table (an id that is not its name's hash)") {
    static constexpr std::array fields{FieldDescriptor{schema::FieldId{1}, "x", TypeKind::Primitive,
                                                       PrimitiveKind::Float32, schema::TypeId{}, FieldFlags::Editable,
                                                       0}};
    const std::array table{TypeDescriptor{schema::typeId("synthetic::T"), "synthetic::T", TypeKind::Struct, 1, fields, {}}};
    CHECK(failureOf(table).error == codegen::CodegenError::IllFormedSchema);
  }
  SECTION("an Optional struct field") {
    static constexpr std::array inner{field("synthetic::Inner", "x")};
    static constexpr std::array outer{FieldDescriptor{schema::fieldId("synthetic::Outer", "inner"), "inner",
                                                      TypeKind::Struct, PrimitiveKind{},
                                                      schema::typeId("synthetic::Inner"),
                                                      FieldFlags::Editable | FieldFlags::Optional, 0}};
    const std::array table{
        TypeDescriptor{schema::typeId("synthetic::Inner"), "synthetic::Inner", TypeKind::Struct, 1, inner, {}},
        TypeDescriptor{schema::typeId("synthetic::Outer"), "synthetic::Outer", TypeKind::Struct, 1, outer, {}}};
    CHECK(failureOf(table).error == codegen::CodegenError::UnsupportedField);
  }
}
