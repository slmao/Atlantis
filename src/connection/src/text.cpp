#include <atlantis/connection/text.h>

#include <atlantis/assert.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace atlantis::connection::text {

namespace access = atlantis::world::access;
using atlantis::asset_system::EntityGuid;
using schema::FieldDescriptor;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeId;
using schema::TypeKind;

namespace {

template <typename T>
using TextResult = atlantis::Result<T, TextError>;

// Whole-token strtof in the C locale (Plan 0054 P5: Atlantis never calls
// setlocale). "nan" and "inf" parse; the boundary refuses them.
[[nodiscard]] bool parseFloat(std::string_view token, float& out) {
  if (token.empty()) return false;
  const std::string owned(token);
  char* end = nullptr;
  errno = 0;
  const float value = std::strtof(owned.c_str(), &end);
  if (end != owned.c_str() + owned.size()) return false;
  out = value;
  return true;
}

[[nodiscard]] bool parseUInt64(std::string_view token, std::uint64_t& out) {
  if (token.empty()) return false;
  for (const char c : token) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;  // no sign, no space
  }
  const std::string owned(token);
  char* end = nullptr;
  errno = 0;
  const unsigned long long value = std::strtoull(owned.c_str(), &end, 10);
  if (errno == ERANGE || end != owned.c_str() + owned.size()) return false;
  out = static_cast<std::uint64_t>(value);
  return true;
}

// %.9g: nine significant digits round-trip every float exactly.
[[nodiscard]] std::string formatFloat(float value) {
  std::array<char, 32> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
  return std::string(buffer.data());
}

[[nodiscard]] std::size_t tokenCount(const FieldDescriptor& leaf) {
  if (leaf.kind == TypeKind::Primitive && leaf.primitive == PrimitiveKind::Vec3Float32) return 3;
  if (leaf.kind == TypeKind::Primitive && leaf.primitive == PrimitiveKind::Vec4Float32) return 4;
  return 1;
}

void collectLeaves(std::span<const TypeDescriptor> schema, const TypeDescriptor& type, const std::string& prefix,
                   std::vector<LeafPath>& out) {
  for (const FieldDescriptor& field : type.fields) {
    std::string path = prefix + "." + std::string(field.name);
    if (field.kind == TypeKind::Struct) {
      const TypeDescriptor* nested = findType(schema, field.type);
      ATLANTIS_CHECK_MSG(nested != nullptr, "text::leavesOf(): a struct field's type is not in the schema");
      collectLeaves(schema, *nested, path, out);
    } else {
      out.push_back(LeafPath{std::move(path), &field});
    }
  }
}

}  // namespace

std::string_view toString(TextError error) noexcept {
  switch (error) {
    case TextError::MalformedGuid: return "MalformedGuid";
    case TextError::UnknownType: return "UnknownType";
    case TextError::UnknownField: return "UnknownField";
    case TextError::NotALeaf: return "NotALeaf";
    case TextError::WrongValueCount: return "WrongValueCount";
    case TextError::MalformedNumber: return "MalformedNumber";
    case TextError::UnknownEnumConstant: return "UnknownEnumConstant";
  }
  ATLANTIS_CHECK_MSG(false, "toString(TextError): unhandled enumerator");
  return "(unrecognized TextError)";
}

TextResult<EntityGuid> parseEntity(std::string_view text) {
  auto parsed = atlantis::asset_system::parseEntityGuid(text);
  if (parsed.isErr()) return TextResult<EntityGuid>::Err(TextError::MalformedGuid);
  return TextResult<EntityGuid>::Ok(parsed.value());
}

std::string formatEntity(const EntityGuid& entity) { return atlantis::asset_system::toString(entity); }

std::string_view shortName(std::string_view qualifiedName) noexcept {
  const auto separator = qualifiedName.rfind("::");
  return separator == std::string_view::npos ? qualifiedName : qualifiedName.substr(separator + 2);
}

const TypeDescriptor* findType(std::span<const TypeDescriptor> schema, std::string_view name) noexcept {
  for (const TypeDescriptor& type : schema) {
    if (type.name == name || shortName(type.name) == name) return &type;
  }
  return nullptr;
}

const TypeDescriptor* findType(std::span<const TypeDescriptor> schema, TypeId id) noexcept {
  for (const TypeDescriptor& type : schema) {
    if (type.id == id) return &type;
  }
  return nullptr;
}

TextResult<ResolvedPath> parsePath(std::span<const TypeDescriptor> schema, std::string_view text) {
  using ResultT = TextResult<ResolvedPath>;
  const auto dot = text.find('.');
  const TypeDescriptor* type = findType(schema, text.substr(0, dot));
  if (type == nullptr || type->kind != TypeKind::Struct) return ResultT::Err(TextError::UnknownType);
  if (dot == std::string_view::npos) return ResultT::Err(TextError::NotALeaf);
  const TypeId component = type->id;
  std::string_view rest = text.substr(dot + 1);
  while (true) {
    const auto next = rest.find('.');
    const std::string_view name = rest.substr(0, next);
    const FieldDescriptor* field = nullptr;
    for (const FieldDescriptor& candidate : type->fields) {
      if (candidate.name == name) field = &candidate;
    }
    if (field == nullptr) return ResultT::Err(TextError::UnknownField);
    if (field->kind != TypeKind::Struct) {
      if (next != std::string_view::npos) return ResultT::Err(TextError::UnknownField);  // a leaf has no fields
      return ResultT::Ok(ResolvedPath{component, field});
    }
    if (next == std::string_view::npos) return ResultT::Err(TextError::NotALeaf);
    type = findType(schema, field->type);
    if (type == nullptr) return ResultT::Err(TextError::UnknownType);
    rest = rest.substr(next + 1);
  }
}

std::vector<LeafPath> leavesOf(std::span<const TypeDescriptor> schema, TypeId type) {
  std::vector<LeafPath> leaves;
  const TypeDescriptor* descriptor = findType(schema, type);
  if (descriptor != nullptr) collectLeaves(schema, *descriptor, std::string(shortName(descriptor->name)), leaves);
  return leaves;
}

TextResult<access::PropertyValue> parseValue(std::span<const TypeDescriptor> schema, const FieldDescriptor& leaf,
                                             std::span<const std::string_view> tokens) {
  using ResultT = TextResult<access::PropertyValue>;
  if (tokens.size() == 1 && tokens[0] == "none" && schema::hasFlags(leaf.flags, schema::FieldFlags::Optional)) {
    return ResultT::Ok(access::Absent{});
  }
  if (tokens.size() != tokenCount(leaf)) return ResultT::Err(TextError::WrongValueCount);
  if (leaf.kind == TypeKind::Enum) {
    const TypeDescriptor* enumType = findType(schema, leaf.type);
    if (enumType != nullptr) {
      for (const auto& constant : enumType->constants) {
        if (constant.name == tokens[0]) return ResultT::Ok(access::EnumValue{constant.value});
      }
    }
    return ResultT::Err(TextError::UnknownEnumConstant);
  }
  switch (leaf.primitive) {
    case PrimitiveKind::UInt64: {
      std::uint64_t value = 0;
      if (!parseUInt64(tokens[0], value)) return ResultT::Err(TextError::MalformedNumber);
      return ResultT::Ok(value);
    }
    case PrimitiveKind::Float32: {
      float value = 0.0f;
      if (!parseFloat(tokens[0], value)) return ResultT::Err(TextError::MalformedNumber);
      return ResultT::Ok(value);
    }
    case PrimitiveKind::Vec3Float32: {
      std::array<float, 3> value{};
      for (std::size_t i = 0; i < 3; ++i) {
        if (!parseFloat(tokens[i], value[i])) return ResultT::Err(TextError::MalformedNumber);
      }
      return ResultT::Ok(value);
    }
    case PrimitiveKind::Vec4Float32: {
      std::array<float, 4> value{};
      for (std::size_t i = 0; i < 4; ++i) {
        if (!parseFloat(tokens[i], value[i])) return ResultT::Err(TextError::MalformedNumber);
      }
      return ResultT::Ok(value);
    }
    case PrimitiveKind::AssetGuid: {
      auto guid = atlantis::asset_system::parseAssetGuid(tokens[0]);
      if (guid.isErr()) return ResultT::Err(TextError::MalformedGuid);
      return ResultT::Ok(guid.value());
    }
    case PrimitiveKind::EntityGuid: {
      auto guid = atlantis::asset_system::parseEntityGuid(tokens[0]);
      if (guid.isErr()) return ResultT::Err(TextError::MalformedGuid);
      return ResultT::Ok(guid.value());
    }
  }
  return ResultT::Err(TextError::MalformedNumber);
}

std::string formatValue(std::span<const TypeDescriptor> schema, const FieldDescriptor& leaf,
                        const access::PropertyValue& value) {
  return std::visit(
      [&](const auto& v) -> std::string {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::uint64_t>) {
          return std::to_string(v);
        } else if constexpr (std::is_same_v<V, float>) {
          return formatFloat(v);
        } else if constexpr (std::is_same_v<V, std::array<float, 3>> || std::is_same_v<V, std::array<float, 4>>) {
          std::string out;
          for (const float component : v) out += (out.empty() ? "" : " ") + formatFloat(component);
          return out;
        } else if constexpr (std::is_same_v<V, atlantis::asset_system::AssetGuid> || std::is_same_v<V, EntityGuid>) {
          return atlantis::asset_system::toString(v);
        } else if constexpr (std::is_same_v<V, access::EnumValue>) {
          if (const TypeDescriptor* enumType = findType(schema, leaf.type)) {
            for (const auto& constant : enumType->constants) {
              if (constant.value == v.value) return std::string(constant.name);
            }
          }
          return std::to_string(v.value);  // not a declared constant: the boundary never stores one
        } else {
          static_assert(std::is_same_v<V, access::Absent>);
          return "none";
        }
      },
      value);
}

}  // namespace atlantis::connection::text
