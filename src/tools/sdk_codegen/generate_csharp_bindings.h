#pragma once

#include "generate_bindings.h"

#include <atlantis/result.h>
#include <atlantis/schema.h>

#include <span>
#include <string>

// Spec 0058 R2 (ruling Q2), ADR-0112 D3, Plan 0058 P5: the generator's C#
// backend. A pure function like generateBindings(): a descriptor table in,
// one C# source file out -- LF only, no timestamp, the same bytes for the
// same table. Its output is committed (src/csharp/Atlantis.Gameplay/
// Generated/World.g.cs) and checked for staleness by the C++ generator tests,
// so no .NET is needed to know it is current.
//
// Type map: UInt64 -> ulong; Float32 -> float; Vec3/Vec4Float32 ->
// System.Numerics.Vector3/Vector4; AssetGuid/EntityGuid -> the library's
// AssetGuid/EntityGuid; enum -> `enum X : long` with the constants' exact
// values; struct -> `public record struct X` with public fields; Optional ->
// T?. Names: types keep their schema names, fields are PascalCased; a
// collision, a member named like its enclosing type, a C# keyword or a
// generator-reserved name fails generation (ADR-0111 D5 extended).
namespace atlantis::tools::sdk_codegen {

struct CSharpOptions {
  std::string modulePrefix;  // e.g. "world::"
  std::string ns;            // e.g. "Atlantis.Gameplay.World"
  std::string source;        // the banner's "from ..." description
  std::string regenerate;    // the banner's regeneration instruction
};

// The options for World's C# bindings, shared by the tool and the staleness
// test.
[[nodiscard]] CSharpOptions worldCSharpOptions();

[[nodiscard]] atlantis::Result<std::string, CodegenFailure> generateCSharpBindings(
    std::span<const schema::TypeDescriptor> schema, const CSharpOptions& options);

}  // namespace atlantis::tools::sdk_codegen
