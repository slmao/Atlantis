#pragma once

#include <atlantis/result.h>
#include <atlantis/schema.h>

#include <span>
#include <string>
#include <string_view>

// Spec 0057 R3/R6/R7, ADR-0111 D1/D2/D5, Plan 0057 P3: the schema-to-C++
// binding generator. A pure function: a descriptor table in, one header's
// text out -- LF line endings, no timestamp, the same bytes for the same
// table. It reads the (hand-authored, ADR-0099 D4) tables; it never
// generates one. Host-only (Tools); the output is committed and checked for
// staleness by tests (Plan 0057 J4).
namespace atlantis::tools::sdk_codegen {

struct GeneratorOptions {
  std::string modulePrefix;     // every type's qualified name starts with it, e.g. "world::"
  std::string targetNamespace;  // e.g. "atlantis::gameplay::world"
  std::string source;           // the banner's "from ..." description
  std::string regenerate;       // the banner's regeneration instruction
};

enum class CodegenError {
  IllFormedSchema,   // schema::isWellFormed() is false
  InvalidName,       // not an identifier, or a type outside the module prefix (or nested below it)
  ReservedName,      // a C++ keyword, a reserved identifier, or a name the generated header uses itself
  UnsupportedField,  // a shape the generated C++ cannot carry (an Optional struct field)
};

[[nodiscard]] std::string_view toString(CodegenError error) noexcept;

// The options for World's bindings -- the committed
// src/gameplay_sdk/include/atlantis/gameplay/generated/world.h -- shared by
// the tool and the staleness test so both produce the same bytes.
[[nodiscard]] GeneratorOptions worldBindingOptions();

struct CodegenFailure {
  CodegenError error = CodegenError::IllFormedSchema;
  std::string name;  // the offending type, field or constant name ("" for IllFormedSchema)
};

// The binding header for `schema` (ADR-0111 D2):
//   - one `enum class X : std::int64_t` per enum, enumerators with the
//     constants' exact values (never ordinals);
//   - one value struct per struct, members value-initialized;
//   - the binding table (kTypeBindings) with every id, kind, flag,
//     referenced TypeId, constant, schemaVersion and zeroIsDeclared, and a
//     static_assert that every id is its name's hash;
//   - per component (a struct no other struct nests), a field-handle object
//     in `fields` whose members mirror the canonical path, and a Codec;
//   - a BindingOf specialization per type.
// Never a byte offset or a source C++ type name.
[[nodiscard]] atlantis::Result<std::string, CodegenFailure> generateBindings(
    std::span<const schema::TypeDescriptor> schema, const GeneratorOptions& options);

}  // namespace atlantis::tools::sdk_codegen
