#include "generate_csharp_bindings.h"

#include <atlantis/assert.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::tools::sdk_codegen {

namespace {

using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeId;
using schema::TypeKind;

using GenResult = atlantis::Result<std::string, CodegenFailure>;

// C# reserved keywords (contextual keywords are legal identifiers).
constexpr auto kCSharpKeywords = std::to_array<std::string_view>({
    "abstract", "as",       "base",     "bool",      "break",    "byte",      "case",     "catch",
    "char",     "checked",  "class",    "const",     "continue", "decimal",   "default",  "delegate",
    "do",       "double",   "else",     "enum",      "event",    "explicit",  "extern",   "false",
    "finally",  "fixed",    "float",    "for",       "foreach",  "goto",      "if",       "implicit",
    "in",       "int",      "interface", "internal", "is",       "lock",      "long",     "namespace",
    "new",      "null",     "object",   "operator",  "out",      "override",  "params",   "private",
    "protected", "public",  "readonly", "ref",       "return",   "sbyte",     "sealed",   "short",
    "sizeof",   "stackalloc", "static", "string",    "struct",   "switch",    "this",     "throw",
    "true",     "try",      "typeof",   "uint",      "ulong",    "unchecked", "unsafe",   "ushort",
    "using",    "virtual",  "void",     "volatile",  "while",
});

// Names the generated file declares itself in the namespace or in every
// component.
constexpr auto kGeneratorNames = std::to_array<std::string_view>({"Fields", "Bindings", "LeafIds"});

constexpr std::string_view kLib = "global::Atlantis.Gameplay.";

[[nodiscard]] bool isReserved(std::string_view name) {
  return std::find(kCSharpKeywords.begin(), kCSharpKeywords.end(), name) != kCSharpKeywords.end() ||
         std::find(kGeneratorNames.begin(), kGeneratorNames.end(), name) != kGeneratorNames.end();
}

[[nodiscard]] std::string pascal(std::string_view name) {
  std::string out(name);
  if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

[[nodiscard]] std::string hexU(std::uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "0x%016llXUL", static_cast<unsigned long long>(value));
  return buffer;
}

[[nodiscard]] std::string_view kindName(TypeKind kind) {
  switch (kind) {
    case TypeKind::Primitive: return "Primitive";
    case TypeKind::Struct: return "Struct";
    case TypeKind::Enum: return "Enum";
  }
  ATLANTIS_CHECK_MSG(false, "kindName(): unhandled TypeKind");
  return "";
}

[[nodiscard]] std::string_view primitiveName(PrimitiveKind kind) {
  switch (kind) {
    case PrimitiveKind::UInt64: return "UInt64";
    case PrimitiveKind::Float32: return "Float32";
    case PrimitiveKind::Vec3Float32: return "Vec3Float32";
    case PrimitiveKind::Vec4Float32: return "Vec4Float32";
    case PrimitiveKind::AssetGuid: return "AssetGuid";
    case PrimitiveKind::EntityGuid: return "EntityGuid";
  }
  ATLANTIS_CHECK_MSG(false, "primitiveName(): unhandled PrimitiveKind");
  return "";
}

// The C# type of a primitive, and the Leaf helper suffix that converts it.
[[nodiscard]] std::string primitiveCSharp(PrimitiveKind kind) {
  switch (kind) {
    case PrimitiveKind::UInt64: return "ulong";
    case PrimitiveKind::Float32: return "float";
    case PrimitiveKind::Vec3Float32: return "global::System.Numerics.Vector3";
    case PrimitiveKind::Vec4Float32: return "global::System.Numerics.Vector4";
    case PrimitiveKind::AssetGuid: return std::string(kLib) + "AssetGuid";
    case PrimitiveKind::EntityGuid: return std::string(kLib) + "EntityGuid";
  }
  ATLANTIS_CHECK_MSG(false, "primitiveCSharp(): unhandled PrimitiveKind");
  return "";
}

[[nodiscard]] std::string_view primitiveLeaf(PrimitiveKind kind) {
  switch (kind) {
    case PrimitiveKind::UInt64: return "U64";
    case PrimitiveKind::Float32: return "F32";
    case PrimitiveKind::Vec3Float32: return "Vec3";
    case PrimitiveKind::Vec4Float32: return "Vec4";
    case PrimitiveKind::AssetGuid: return "Asset";
    case PrimitiveKind::EntityGuid: return "Entity";
  }
  ATLANTIS_CHECK_MSG(false, "primitiveLeaf(): unhandled PrimitiveKind");
  return "";
}

class Generator {
 public:
  Generator(std::span<const TypeDescriptor> schema, const CSharpOptions& options)
      : schema_(schema), options_(options), ns_("global::" + options.ns) {}

  GenResult run() {
    if (!schema::isWellFormed(schema_)) return fail(CodegenError::IllFormedSchema, "");
    if (auto checked = validate(); checked.isErr()) return GenResult::Err(checked.error());
    for (const TypeDescriptor& type : schema_) {
      if (type.kind != TypeKind::Struct) continue;
      for (const FieldDescriptor& field : type.fields) {
        if (field.kind == TypeKind::Struct) nested_.insert(field.type.value);
      }
    }
    if (auto checked = validateHandles(); checked.isErr()) return GenResult::Err(checked.error());
    banner();
    enums();
    structs();
    fields();
    bindings();
    return GenResult::Ok(std::move(out_));
  }

 private:
  struct Leaf {
    const FieldDescriptor* field = nullptr;
    std::string member;  // C# access path below the value, e.g. "Fog.Density"
    std::string path;    // the canonical property path, e.g. "Camera.fog.density"
  };

  static GenResult fail(CodegenError error, std::string_view name) {
    return GenResult::Err(CodegenFailure{error, std::string(name)});
  }

  using Check = atlantis::Result<int, CodegenFailure>;

  Check validate() const {
    for (const TypeDescriptor& type : schema_) {
      if (!type.name.starts_with(options_.modulePrefix)) return Check::Err({CodegenError::InvalidName, std::string(type.name)});
      const std::string_view local = localName(type);
      if (!schema::detail::isIdentifier(local)) return Check::Err({CodegenError::InvalidName, std::string(type.name)});
      if (isReserved(local)) return Check::Err({CodegenError::ReservedName, std::string(type.name)});
      std::set<std::string> members;
      for (const FieldDescriptor& field : type.fields) {
        const std::string name = pascal(field.name);
        // Only the PascalCased name reaches C# (a schema field `fixed` becomes `Fixed`).
        if (isReserved(name)) return Check::Err({CodegenError::ReservedName, std::string(field.name)});
        // CS0542: a member may not share its enclosing type's name.
        if (name == local) return Check::Err({CodegenError::InvalidName, std::string(field.name)});
        // Two schema names mapping to one C# name.
        if (!members.insert(name).second) return Check::Err({CodegenError::InvalidName, std::string(field.name)});
        if (field.kind == TypeKind::Struct && schema::hasFlags(field.flags, FieldFlags::Optional)) {
          return Check::Err({CodegenError::UnsupportedField, std::string(field.name)});
        }
      }
      std::set<std::string> constants;
      for (const auto& constant : type.constants) {
        if (isReserved(constant.name)) return Check::Err({CodegenError::ReservedName, std::string(constant.name)});
        if (!constants.insert(std::string(constant.name)).second) {
          return Check::Err({CodegenError::InvalidName, std::string(constant.name)});
        }
      }
    }
    return Check::Ok(0);
  }

  // The handle classes nest one static class per struct field
  // (Fields.Camera.Fog); a member inside such a class may not share its name
  // (CS0542).
  Check validateHandles() const {
    for (const TypeDescriptor& type : schema_) {
      if (!isComponent(type)) continue;
      if (auto checked = validateSet(type, std::string(localName(type))); checked.isErr()) return checked;
    }
    return Check::Ok(0);
  }

  Check validateSet(const TypeDescriptor& type, const std::string& className) const {
    for (const FieldDescriptor& field : type.fields) {
      const std::string name = pascal(field.name);
      if (name == className) return Check::Err({CodegenError::InvalidName, std::string(field.name)});
      if (field.kind == TypeKind::Struct) {
        if (auto checked = validateSet(typeOf(field.type), name); checked.isErr()) return checked;
      }
    }
    return Check::Ok(0);
  }

  [[nodiscard]] std::string_view localName(const TypeDescriptor& type) const {
    return std::string_view(type.name).substr(options_.modulePrefix.size());
  }

  [[nodiscard]] const TypeDescriptor& typeOf(TypeId id) const {
    const TypeDescriptor* found = schema::detail::findType(schema_, id);
    ATLANTIS_CHECK_MSG(found != nullptr, "generateCSharpBindings(): a referenced type is not in the (well-formed) table");
    return *found;
  }

  [[nodiscard]] std::size_t indexOf(TypeId id) const {
    for (std::size_t i = 0; i < schema_.size(); ++i) {
      if (schema_[i].id == id) return i;
    }
    ATLANTIS_CHECK_MSG(false, "generateCSharpBindings(): a type is not in the table");
    return 0;
  }

  [[nodiscard]] std::string qualified(const TypeDescriptor& type) const {
    return ns_ + "." + std::string(localName(type));
  }

  [[nodiscard]] bool isComponent(const TypeDescriptor& type) const {
    return type.kind == TypeKind::Struct && !nested_.contains(type.id.value);
  }

  [[nodiscard]] bool optional(const FieldDescriptor& field) const {
    return schema::hasFlags(field.flags, FieldFlags::Optional);
  }

  [[nodiscard]] std::string csharpType(const FieldDescriptor& field) const {
    std::string type = field.kind == TypeKind::Primitive ? primitiveCSharp(field.primitive) : qualified(typeOf(field.type));
    if (optional(field)) type += "?";
    return type;
  }

  // The Leaf helper pair for a leaf: (encode, decode) method groups.
  [[nodiscard]] std::pair<std::string, std::string> leafHelpers(const FieldDescriptor& field) const {
    const std::string leaf = std::string(kLib) + "Leaf.";
    const std::string opt = optional(field) ? "Opt" : "";
    if (field.kind == TypeKind::Enum) {
      const std::string arg = "<" + qualified(typeOf(field.type)) + ">";
      return {leaf + opt + "Enum" + arg, leaf + "Try" + opt + "Enum" + arg};
    }
    const std::string kind(primitiveLeaf(field.primitive));
    return {leaf + opt + kind, leaf + "Try" + opt + kind};
  }

  void line(std::string_view text = {}) {
    out_ += text;
    out_ += '\n';
  }

  void banner() {
    line("// <auto-generated>");
    line("// GENERATED by atlantis_sdk_codegen --lang csharp from " + options_.source + " -- do not edit.");
    line("// Regenerate: " + options_.regenerate);
    line("// </auto-generated>");
    line("//");
    line("// Spec 0058 / ADR-0112 D3: value types, enums, typed field handles and bindings for");
    line("// every type of that schema. Value-initialized members are zeros, nulls and enum");
    line("// value 0 -- not the World's defaults; enum value 0 is a declared constant only where");
    line("// the type's binding says ZeroIsDeclared.");
    line("#nullable enable");
    line();
    line("namespace " + options_.ns + ";");
  }

  void enums() {
    for (const TypeDescriptor& type : schema_) {
      if (type.kind != TypeKind::Enum) continue;
      line();
      line("public enum " + std::string(localName(type)) + " : long");
      line("{");
      for (const auto& constant : type.constants) {
        line("    " + std::string(constant.name) + " = " + std::to_string(constant.value) + ",");
      }
      line("}");
    }
  }

  void collectLeaves(const TypeDescriptor& type, const std::string& member, const std::string& path,
                     std::vector<Leaf>& out) const {
    for (const FieldDescriptor& field : type.fields) {
      const std::string childMember = member.empty() ? pascal(field.name) : member + "." + pascal(field.name);
      const std::string childPath = path + "." + std::string(field.name);
      if (field.kind == TypeKind::Struct) {
        collectLeaves(typeOf(field.type), childMember, childPath, out);
      } else {
        out.push_back(Leaf{&field, childMember, childPath});
      }
    }
  }

  void structs() {
    for (const TypeDescriptor& type : schema_) {
      if (type.kind != TypeKind::Struct) continue;
      const std::string name(localName(type));
      line();
      if (!isComponent(type)) {
        line("public record struct " + name);
        line("{");
        for (const FieldDescriptor& field : type.fields) line("    public " + csharpType(field) + " " + pascal(field.name) + ";");
        line("}");
        continue;
      }
      std::vector<Leaf> leaves;
      collectLeaves(type, "", name, leaves);
      bool allEditable = true;
      for (const Leaf& leaf : leaves) allEditable = allEditable && schema::hasFlags(leaf.field->flags, FieldFlags::Editable);
      const std::string iface = std::string(kLib) + "IComponent<" + name + ">";
      line("public record struct " + name + " : " + std::string(kLib) + (allEditable ? "IEditableComponent<" : "IComponent<") + name + ">");
      line("{");
      for (const FieldDescriptor& field : type.fields) line("    public " + csharpType(field) + " " + pascal(field.name) + ";");
      line();
      line("    private static readonly " + std::string(kLib) + "FieldId[] LeafIds =");
      line("    {");
      for (const Leaf& leaf : leaves) line("        new(" + hexU(leaf.field->id.value) + "),  // " + leaf.path);
      line("    };");
      line();
      line("    static " + std::string(kLib) + "TypeBinding " + iface + ".Binding => Bindings.Types[" +
           std::to_string(indexOf(type.id)) + "];");
      line("    static " + std::string(kLib) + "TypeBinding[] " + iface + ".Module => Bindings.Types;");
      line("    static " + std::string(kLib) + "FieldId[] " + iface + ".Leaves => LeafIds;");
      line("    static bool " + iface + ".AllEditable => " + (allEditable ? "true" : "false") + ";");
      line();
      line("    static void " + iface + ".Write(in " + name + " value, global::System.Span<" + std::string(kLib) +
           "PropertyValue> output)");
      line("    {");
      for (std::size_t i = 0; i < leaves.size(); ++i) {
        line("        output[" + std::to_string(i) + "] = " + leafHelpers(*leaves[i].field).first + "(value." +
             leaves[i].member + ");");
      }
      line("    }");
      line();
      line("    static bool " + iface + ".TryRead(global::System.ReadOnlySpan<" + std::string(kLib) +
           "PropertyValue> input, out " + name + " value)");
      line("    {");
      line("        value = default;");
      for (std::size_t i = 0; i < leaves.size(); ++i) {
        line("        if (!" + leafHelpers(*leaves[i].field).second + "(input[" + std::to_string(i) + "], out value." +
             leaves[i].member + ")) return false;");
      }
      line("        return true;");
      line("    }");
      line("}");
    }
  }

  void handleSet(const TypeDescriptor& component, const TypeDescriptor& type, const std::string& className,
                 const std::string& path, const std::string& indent) {
    line(indent + "public static class " + className);
    line(indent + "{");
    for (const FieldDescriptor& field : type.fields) {
      const std::string childPath = path + "." + std::string(field.name);
      if (field.kind == TypeKind::Struct) {
        handleSet(component, typeOf(field.type), pascal(field.name), childPath, indent + "    ");
        continue;
      }
      const bool editable = schema::hasFlags(field.flags, FieldFlags::Editable);
      const auto [encode, decode] = leafHelpers(field);
      line(indent + "    public static readonly " + std::string(kLib) + (editable ? "Field<" : "ReadOnlyField<") +
           qualified(component) + ", " + csharpType(field) + "> " + pascal(field.name) + " =");
      line(indent + "        new(new " + std::string(kLib) + "TypeId(" + hexU(component.id.value) + "), new " +
           std::string(kLib) + "FieldId(" + hexU(field.id.value) + "), \"" + childPath + "\", " + encode + ", " +
           decode + ");");
    }
    line(indent + "}");
  }

  void fields() {
    line();
    line("// Typed field handles: one class per component; members mirror the canonical");
    line("// path (Fields.Light.Intensity, Fields.Camera.Fog.Density).");
    line("public static class Fields");
    line("{");
    bool first = true;
    for (const TypeDescriptor& type : schema_) {
      if (!isComponent(type)) continue;
      if (!first) line();
      first = false;
      handleSet(type, type, std::string(localName(type)), std::string(localName(type)), "    ");
    }
    line("}");
  }

  void bindings() {
    line();
    line("// What each type was generated from (ids, kinds, flags, referenced types,");
    line("// constants, versions), checked against the connected Runtime's schema.");
    line("public static class Bindings");
    line("{");
    line("    public static readonly " + std::string(kLib) + "TypeBinding[] Types =");
    line("    {");
    for (const TypeDescriptor& type : schema_) {
      bool zero = false;
      for (const auto& constant : type.constants) zero = zero || constant.value == 0;
      line("        new(\"" + std::string(type.name) + "\", new " + std::string(kLib) + "TypeId(" + hexU(type.id.value) +
           "), " + std::string(kLib) + "TypeKind." + std::string(kindName(type.kind)) + ", " +
           std::to_string(type.schemaVersion) + "u,");
      if (type.fields.empty()) {
        line("            global::System.Array.Empty<" + std::string(kLib) + "FieldBinding>(),");
      } else {
        line("            new " + std::string(kLib) + "FieldBinding[]");
        line("            {");
        for (const FieldDescriptor& field : type.fields) {
          line("                new(\"" + std::string(field.name) + "\", new " + std::string(kLib) + "FieldId(" +
               hexU(field.id.value) + "), " + std::string(kLib) + "TypeKind." + std::string(kindName(field.kind)) +
               ", " + std::string(kLib) + "PrimitiveKind." + std::string(primitiveName(field.primitive)) + ", new " +
               std::string(kLib) + "TypeId(" + hexU(field.type.value) + "), " + (optional(field) ? "true" : "false") +
               ", " + (schema::hasFlags(field.flags, FieldFlags::Editable) ? "true" : "false") + "),");
        }
        line("            },");
      }
      if (type.constants.empty()) {
        line("            global::System.Array.Empty<" + std::string(kLib) + "EnumConstantBinding>(),");
      } else {
        line("            new " + std::string(kLib) + "EnumConstantBinding[]");
        line("            {");
        for (const auto& constant : type.constants) {
          line("                new(\"" + std::string(constant.name) + "\", " + std::to_string(constant.value) + "L),");
        }
        line("            },");
      }
      line(std::string("            ") + (zero ? "true" : "false") + "),");
    }
    line("    };");
    line("}");
  }

  std::span<const TypeDescriptor> schema_;
  const CSharpOptions& options_;
  std::string ns_;  // "global::" + the namespace
  std::set<std::uint64_t> nested_;
  std::string out_;
};

}  // namespace

CSharpOptions worldCSharpOptions() {
  CSharpOptions options;
  options.modulePrefix = "world::";
  options.ns = "Atlantis.Gameplay.World";
  options.source = "world::worldSchema()";
  options.regenerate =
      "atlantis_sdk_codegen --lang csharp --out src/csharp/Atlantis.Gameplay/Generated/World.g.cs";
  return options;
}

atlantis::Result<std::string, CodegenFailure> generateCSharpBindings(std::span<const schema::TypeDescriptor> schema,
                                                                     const CSharpOptions& options) {
  return Generator(schema, options).run();
}

}  // namespace atlantis::tools::sdk_codegen
