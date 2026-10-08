#include "generate_bindings.h"

#include <atlantis/assert.h>

#include <algorithm>
#include <array>
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

// C++20 keywords and alternative tokens, and the contextual identifiers
// final, override, import and module.
constexpr auto kKeywords = std::to_array<std::string_view>({
    "alignas",   "alignof",      "and",          "and_eq",    "asm",          "auto",      "bitand",
    "bitor",     "bool",         "break",        "case",      "catch",        "char",      "char8_t",
    "char16_t",  "char32_t",     "class",        "compl",     "concept",      "const",     "consteval",
    "constexpr", "constinit",    "const_cast",   "continue",  "co_await",     "co_return", "co_yield",
    "decltype",  "default",      "delete",       "do",        "double",       "dynamic_cast", "else",
    "enum",      "explicit",     "export",       "extern",    "false",        "float",     "for",
    "friend",    "goto",         "if",           "inline",    "int",          "long",      "mutable",
    "namespace", "new",          "noexcept",     "not",       "not_eq",       "nullptr",   "operator",
    "or",        "or_eq",        "private",      "protected", "public",       "register",  "reinterpret_cast",
    "requires",  "return",       "short",        "signed",    "sizeof",       "static",    "static_assert",
    "static_cast", "struct",     "switch",       "template",  "this",         "thread_local", "throw",
    "true",      "try",          "typedef",      "typeid",    "typename",     "union",     "unsigned",
    "using",     "virtual",      "void",         "volatile",  "wchar_t",      "while",     "xor",
    "xor_eq",    "final",        "override",     "import",    "module",
});

// Names the generated header declares itself, in the target namespace.
constexpr std::array<std::string_view, 3> kGeneratorNames{"fields", "binding_data", "kTypeBindings"};

[[nodiscard]] bool isReserved(std::string_view name) {
  if (std::find(kKeywords.begin(), kKeywords.end(), name) != kKeywords.end()) return true;
  if (std::find(kGeneratorNames.begin(), kGeneratorNames.end(), name) != kGeneratorNames.end()) return true;
  if (name.find("__") != std::string_view::npos) return true;
  return name.size() >= 2 && name[0] == '_' && name[1] >= 'A' && name[1] <= 'Z';
}

[[nodiscard]] std::string hex(std::uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "0x%016llxULL", static_cast<unsigned long long>(value));
  return buffer;
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

[[nodiscard]] std::string_view kindName(TypeKind kind) {
  switch (kind) {
    case TypeKind::Primitive: return "Primitive";
    case TypeKind::Struct: return "Struct";
    case TypeKind::Enum: return "Enum";
  }
  ATLANTIS_CHECK_MSG(false, "kindName(): unhandled TypeKind");
  return "";
}

[[nodiscard]] std::string_view primitiveCppType(PrimitiveKind kind) {
  switch (kind) {
    case PrimitiveKind::UInt64: return "std::uint64_t";
    case PrimitiveKind::Float32: return "float";
    case PrimitiveKind::Vec3Float32: return "std::array<float, 3>";
    case PrimitiveKind::Vec4Float32: return "std::array<float, 4>";
    case PrimitiveKind::AssetGuid: return "::atlantis::asset_system::AssetGuid";
    case PrimitiveKind::EntityGuid: return "::atlantis::asset_system::EntityGuid";
  }
  ATLANTIS_CHECK_MSG(false, "primitiveCppType(): unhandled PrimitiveKind");
  return "";
}

class Generator {
 public:
  Generator(std::span<const TypeDescriptor> schema, const GeneratorOptions& options)
      : schema_(schema), options_(options), ns_("::" + options.targetNamespace) {}

  GenResult run() {
    if (!schema::isWellFormed(schema_)) return fail(CodegenError::IllFormedSchema, "");
    if (auto checked = validate(); checked.isErr()) return GenResult::Err(checked.error());
    for (const TypeDescriptor& type : schema_) {
      if (type.kind != TypeKind::Struct) continue;
      for (const FieldDescriptor& field : type.fields) {
        if (field.kind == TypeKind::Struct) nested_.insert(field.type.value);
      }
    }
    banner();
    enums();
    structs();
    bindings();
    fieldSets();
    traits();
    return GenResult::Ok(std::move(out_));
  }

 private:
  struct Leaf {
    const FieldDescriptor* field = nullptr;
    std::string member;  // the access path below the value, e.g. "fog.color"
    std::string path;    // the canonical property path, e.g. "Camera.fog.color"
  };

  static GenResult fail(CodegenError error, std::string_view name) {
    return GenResult::Err(CodegenFailure{error, std::string(name)});
  }

  atlantis::Result<int, CodegenFailure> validate() const {
    using R = atlantis::Result<int, CodegenFailure>;
    for (const TypeDescriptor& type : schema_) {
      if (!type.name.starts_with(options_.modulePrefix)) return R::Err({CodegenError::InvalidName, std::string(type.name)});
      const std::string_view local = localName(type);
      if (!schema::detail::isIdentifier(local)) return R::Err({CodegenError::InvalidName, std::string(type.name)});
      if (isReserved(local)) return R::Err({CodegenError::ReservedName, std::string(type.name)});
      for (const FieldDescriptor& field : type.fields) {
        if (isReserved(field.name)) return R::Err({CodegenError::ReservedName, std::string(field.name)});
        if (field.kind == TypeKind::Struct && schema::hasFlags(field.flags, FieldFlags::Optional)) {
          return R::Err({CodegenError::UnsupportedField, std::string(field.name)});
        }
      }
      for (const auto& constant : type.constants) {
        if (isReserved(constant.name)) return R::Err({CodegenError::ReservedName, std::string(constant.name)});
      }
    }
    return R::Ok(0);
  }

  [[nodiscard]] std::string_view localName(const TypeDescriptor& type) const {
    return std::string_view(type.name).substr(options_.modulePrefix.size());
  }

  [[nodiscard]] const TypeDescriptor& typeOf(TypeId id) const {
    const TypeDescriptor* found = schema::detail::findType(schema_, id);
    ATLANTIS_CHECK_MSG(found != nullptr, "generateBindings(): a referenced type is not in the (well-formed) table");
    return *found;
  }

  [[nodiscard]] std::string qualified(const TypeDescriptor& type) const { return ns_ + "::" + std::string(localName(type)); }

  [[nodiscard]] std::string cppType(const FieldDescriptor& field) const {
    std::string type = field.kind == TypeKind::Primitive ? std::string(primitiveCppType(field.primitive))
                                                         : qualified(typeOf(field.type));
    if (schema::hasFlags(field.flags, FieldFlags::Optional)) type = "std::optional<" + type + ">";
    return type;
  }

  [[nodiscard]] bool isComponent(const TypeDescriptor& type) const {
    return type.kind == TypeKind::Struct && !nested_.contains(type.id.value);
  }

  void line(std::string_view text = {}) {
    out_ += text;
    out_ += '\n';
  }

  void banner() {
    line("// GENERATED by atlantis_sdk_codegen from " + options_.source + " -- do not edit.");
    line("// Regenerate: " + options_.regenerate);
    line("//");
    line("// Spec 0057 / ADR-0111: value structs, enums, typed field handles and bindings");
    line("// for every type of that schema. Value-initialized members are zeros, empty");
    line("// optionals and enum value 0 -- not the World's defaults; enum value 0 is a");
    line("// declared constant only where the type's binding says zeroIsDeclared.");
    line("#pragma once");
    line();
    line("#include <atlantis/gameplay/binding.h>");
    line();
    line("#include <array>");
    line("#include <cstddef>");
    line("#include <cstdint>");
    line("#include <optional>");
    line("#include <span>");
    line();
    line("namespace " + options_.targetNamespace + " {");
  }

  void enums() {
    for (const TypeDescriptor& type : schema_) {
      if (type.kind != TypeKind::Enum) continue;
      line();
      line("enum class " + std::string(localName(type)) + " : std::int64_t {");
      for (const auto& constant : type.constants) {
        line("  " + std::string(constant.name) + " = " + std::to_string(constant.value) + ",");
      }
      line("};");
    }
  }

  // Structs in dependency order: a nested struct before the struct holding
  // it, otherwise table order.
  void structs() {
    std::set<std::uint64_t> done;
    for (const TypeDescriptor& type : schema_) {
      if (type.kind == TypeKind::Struct) emitStruct(type, done);
    }
  }

  void emitStruct(const TypeDescriptor& type, std::set<std::uint64_t>& done) {
    if (done.contains(type.id.value)) return;
    done.insert(type.id.value);
    for (const FieldDescriptor& field : type.fields) {
      if (field.kind == TypeKind::Struct) emitStruct(typeOf(field.type), done);
    }
    line();
    line("struct " + std::string(localName(type)) + " {");
    for (const FieldDescriptor& field : type.fields) line("  " + cppType(field) + " " + std::string(field.name) + "{};");
    line("};");
  }

  void bindings() {
    line();
    line("namespace binding_data {");
    for (const TypeDescriptor& type : schema_) {
      const std::string name(localName(type));
      if (type.kind == TypeKind::Struct) {
        line("inline constexpr std::array<::atlantis::gameplay::FieldBinding, " + std::to_string(type.fields.size()) +
             "> k" + name + "{{");
        for (const FieldDescriptor& field : type.fields) {
          line("    {\"" + std::string(field.name) + "\", ::atlantis::schema::FieldId{" + hex(field.id.value) +
               "}, ::atlantis::schema::TypeKind::" + std::string(kindName(field.kind)) +
               ", ::atlantis::schema::PrimitiveKind::" + std::string(primitiveName(field.primitive)) +
               ", ::atlantis::schema::TypeId{" + hex(field.type.value) + "}, " +
               (schema::hasFlags(field.flags, FieldFlags::Optional) ? "true" : "false") + ", " +
               (schema::hasFlags(field.flags, FieldFlags::Editable) ? "true" : "false") + "},");
        }
        line("}};");
      } else {
        line("inline constexpr std::array<::atlantis::gameplay::EnumConstantBinding, " +
             std::to_string(type.constants.size()) + "> k" + name + "{{");
        for (const auto& constant : type.constants) {
          line("    {\"" + std::string(constant.name) + "\", " + std::to_string(constant.value) + "},");
        }
        line("}};");
      }
    }
    line("}  // namespace binding_data");
    line();
    line("inline constexpr std::array<::atlantis::gameplay::TypeBinding, " + std::to_string(schema_.size()) +
         "> kTypeBindings{{");
    for (const TypeDescriptor& type : schema_) {
      const std::string name(localName(type));
      const bool isEnum = type.kind == TypeKind::Enum;
      bool zero = false;
      for (const auto& constant : type.constants) zero = zero || constant.value == 0;
      line("    {\"" + std::string(type.name) + "\", ::atlantis::schema::TypeId{" + hex(type.id.value) +
           "}, ::atlantis::schema::TypeKind::" + std::string(kindName(type.kind)) + ", " +
           std::to_string(type.schemaVersion) + ", " + (isEnum ? "{}" : "binding_data::k" + name) + ", " +
           (isEnum ? "binding_data::k" + name : "{}") + ", " + (zero ? "true" : "false") + "},");
    }
    line("}};");
    line("static_assert(::atlantis::gameplay::bindingsMatchNames(kTypeBindings));");
  }

  // Leaves of `type`, depth first in descriptor order (connection::text's
  // leavesOf order, Spec 0049 J8).
  void collectLeaves(const TypeDescriptor& type, const std::string& member, const std::string& path,
                     std::vector<Leaf>& out) const {
    for (const FieldDescriptor& field : type.fields) {
      const std::string childMember = member.empty() ? std::string(field.name) : member + "." + std::string(field.name);
      const std::string childPath = path + "." + std::string(field.name);
      if (field.kind == TypeKind::Struct) {
        collectLeaves(typeOf(field.type), childMember, childPath, out);
      } else {
        out.push_back(Leaf{&field, childMember, childPath});
      }
    }
  }

  // The handle-set struct for `type` under `component` (named
  // <Component>[_<nested path>]FieldSet), nested sets first.
  std::string emitSet(const TypeDescriptor& component, const TypeDescriptor& type, const std::string& setName) {
    for (const FieldDescriptor& field : type.fields) {
      if (field.kind == TypeKind::Struct) emitSet(component, typeOf(field.type), setName + "_" + std::string(field.name));
    }
    line("struct " + setName + "FieldSet {");
    for (const FieldDescriptor& field : type.fields) {
      if (field.kind == TypeKind::Struct) {
        line("  " + setName + "_" + std::string(field.name) + "FieldSet " + std::string(field.name) + ";");
      } else {
        const bool editable = schema::hasFlags(field.flags, FieldFlags::Editable);
        line(std::string("  ::atlantis::gameplay::") + (editable ? "Field<" : "ReadOnlyField<") + qualified(component) +
             ", " + cppType(field) + "> " + std::string(field.name) + ";");
      }
    }
    line("};");
    return setName + "FieldSet";
  }

  void emitInitializer(const TypeDescriptor& component, const TypeDescriptor& type, const std::string& path,
                       const std::string& indent) {
    for (const FieldDescriptor& field : type.fields) {
      const std::string childPath = path + "." + std::string(field.name);
      if (field.kind == TypeKind::Struct) {
        line(indent + "." + std::string(field.name) + " =");
        line(indent + "    {");
        emitInitializer(component, typeOf(field.type), childPath, indent + "        ");
        line(indent + "    },");
      } else {
        line(indent + "." + std::string(field.name) + " = {::atlantis::schema::TypeId{" + hex(component.id.value) +
             "}, ::atlantis::schema::FieldId{" + hex(field.id.value) + "}, \"" + childPath + "\"},");
      }
    }
  }

  void fieldSets() {
    line();
    line("// Typed field handles: one object per component; members mirror the");
    line("// canonical path (fields::Light.intensity, fields::Camera.fog.density).");
    line("namespace fields {");
    for (const TypeDescriptor& type : schema_) {
      if (!isComponent(type)) continue;
      const std::string name(localName(type));
      line();
      const std::string setType = emitSet(type, type, name);
      line("inline constexpr " + setType + " " + name + "{");
      emitInitializer(type, type, name, "    ");
      line("};");
    }
    line("}  // namespace fields");
    line();
    line("}  // namespace " + options_.targetNamespace);
  }

  void traits() {
    line();
    line("namespace atlantis::gameplay {");
    std::size_t index = 0;
    for (const TypeDescriptor& type : schema_) {
      line();
      line("template <>");
      line("struct BindingOf<" + qualified(type) + "> {");
      line("  static constexpr std::span<const TypeBinding> table = " + ns_ + "::kTypeBindings;");
      line("  static constexpr std::size_t index = " + std::to_string(index++) + ";");
      line("};");
    }
    for (const TypeDescriptor& type : schema_) {
      if (!isComponent(type)) continue;
      std::vector<Leaf> leaves;
      collectLeaves(type, "", std::string(localName(type)), leaves);
      bool allEditable = true;
      for (const Leaf& leaf : leaves) allEditable = allEditable && schema::hasFlags(leaf.field->flags, FieldFlags::Editable);
      const std::string count = std::to_string(leaves.size());
      line();
      line("template <>");
      line("struct Codec<" + qualified(type) + "> {");
      line(std::string("  static constexpr bool allEditable = ") + (allEditable ? "true" : "false") + ";");
      line("  static constexpr std::array<::atlantis::schema::FieldId, " + count + "> leaves{{");
      for (const Leaf& leaf : leaves) line("      ::atlantis::schema::FieldId{" + hex(leaf.field->id.value) + "},  // " + leaf.path);
      line("  }};");
      line("  static void write(const " + qualified(type) + "& value, std::span<PropertyValue> out) {");
      for (std::size_t i = 0; i < leaves.size(); ++i) {
        line("    out[" + std::to_string(i) + "] = toPropertyValue(value." + leaves[i].member + ");");
      }
      line("  }");
      line("  [[nodiscard]] static bool read(std::span<const PropertyValue> in, " + qualified(type) + "& value) {");
      for (std::size_t i = 0; i < leaves.size(); ++i) {
        line("    if (!fromPropertyValue(in[" + std::to_string(i) + "], value." + leaves[i].member + ")) return false;");
      }
      line("    return true;");
      line("  }");
      line("};");
    }
    line();
    line("}  // namespace atlantis::gameplay");
  }

  std::span<const TypeDescriptor> schema_;
  const GeneratorOptions& options_;
  std::string ns_;  // "::" + the target namespace
  std::set<std::uint64_t> nested_;
  std::string out_;
};

}  // namespace

std::string_view toString(CodegenError error) noexcept {
  switch (error) {
    case CodegenError::IllFormedSchema: return "IllFormedSchema";
    case CodegenError::InvalidName: return "InvalidName";
    case CodegenError::ReservedName: return "ReservedName";
    case CodegenError::UnsupportedField: return "UnsupportedField";
  }
  ATLANTIS_CHECK_MSG(false, "toString(CodegenError): unhandled enumerator");
  return "(unrecognized CodegenError)";
}

GeneratorOptions worldBindingOptions() {
  GeneratorOptions options;
  options.modulePrefix = "world::";
  options.targetNamespace = "atlantis::gameplay::world";
  options.source = "world::worldSchema()";
  options.regenerate = "atlantis_sdk_codegen --out src/gameplay_sdk/include/atlantis/gameplay/generated/world.h";
  return options;
}

atlantis::Result<std::string, CodegenFailure> generateBindings(std::span<const schema::TypeDescriptor> schema,
                                                               const GeneratorOptions& options) {
  return Generator(schema, options).run();
}

}  // namespace atlantis::tools::sdk_codegen
