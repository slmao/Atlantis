#include <atlantis/shader_system/reflection_loader.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include "json_parser.h"
#include "json_value.h"

namespace atlantis::shader_system {

namespace {

using detail::JsonArray;
using detail::JsonObject;
using detail::JsonValue;

using LoadResult = atlantis::Result<ReflectionMetadata, ReflectionLoadError>;

[[nodiscard]] std::optional<std::uint32_t> readUint32(const JsonValue& value) {
  if (!value.isNumber()) return std::nullopt;
  const double raw = value.asNumber();
  if (raw < 0.0 || std::floor(raw) != raw) return std::nullopt;
  if (raw > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) return std::nullopt;
  return static_cast<std::uint32_t>(raw);
}

[[nodiscard]] std::optional<ShaderStage> parseStage(const std::string& text) {
  if (text == "vertex") return ShaderStage::Vertex;
  if (text == "fragment") return ShaderStage::Fragment;
  return std::nullopt;
}

[[nodiscard]] const char* stageToString(ShaderStage stage) {
  switch (stage) {
    case ShaderStage::Vertex:
      return "vertex";
    case ShaderStage::Fragment:
      return "fragment";
  }
  return "vertex";
}

[[nodiscard]] std::optional<DescriptorType> parseDescriptorType(const std::string& text) {
  if (text == "uniformBuffer") return DescriptorType::UniformBuffer;
  if (text == "sampler") return DescriptorType::Sampler;  // Spec 0016/D6
  return std::nullopt;
}

[[nodiscard]] const char* descriptorTypeToString(DescriptorType type) {
  switch (type) {
    case DescriptorType::UniformBuffer:
      return "uniformBuffer";
    case DescriptorType::Sampler:
      return "sampler";
  }
  return "uniformBuffer";
}

[[nodiscard]] std::optional<VertexAttributeType> parseVertexAttributeType(const std::string& text) {
  if (text == "float3") return VertexAttributeType::Float3;
  if (text == "float2") return VertexAttributeType::Float2;  // Spec 0016/D6
  return std::nullopt;
}

[[nodiscard]] const char* vertexAttributeTypeToString(VertexAttributeType type) {
  switch (type) {
    case VertexAttributeType::Float3:
      return "float3";
    case VertexAttributeType::Float2:
      return "float2";
  }
  return "float3";
}

// Reads an optional array-of-object field, applying `readEntry` to each
// element. Absence of `key` entirely is not an error (Section 3: these
// arrays default to empty when absent). A present-but-malformed shape
// (not an array, or an element `readEntry` rejects) is `false`.
template <typename T, typename ReadEntry>
[[nodiscard]] bool readOptionalArray(const JsonValue& object, const std::string& key, std::vector<T>& out,
                                      ReadEntry readEntry) {
  const JsonValue* field = object.find(key);
  if (field == nullptr) return true;
  if (!field->isArray()) return false;
  for (const JsonValue& element : field->asArray()) {
    std::optional<T> parsed = readEntry(element);
    if (!parsed.has_value()) return false;
    out.push_back(std::move(*parsed));
  }
  return true;
}

[[nodiscard]] std::optional<DescriptorBinding> readDescriptorBinding(const JsonValue& value) {
  if (!value.isObject()) return std::nullopt;
  const JsonValue* setField = value.find("set");
  const JsonValue* bindingField = value.find("binding");
  const JsonValue* typeField = value.find("type");
  const JsonValue* stageField = value.find("stage");
  if (setField == nullptr || bindingField == nullptr || typeField == nullptr || stageField == nullptr) {
    return std::nullopt;
  }
  const auto set = readUint32(*setField);
  const auto binding = readUint32(*bindingField);
  if (!set.has_value() || !binding.has_value() || !typeField->isString() || !stageField->isString()) {
    return std::nullopt;
  }
  const auto type = parseDescriptorType(typeField->asString());
  const auto stage = parseStage(stageField->asString());
  if (!type.has_value() || !stage.has_value()) return std::nullopt;

  DescriptorBinding result;
  result.set = *set;
  result.binding = *binding;
  result.type = *type;
  result.stage = *stage;
  return result;
}

[[nodiscard]] std::optional<PushConstantRange> readPushConstantRange(const JsonValue& value) {
  if (!value.isObject()) return std::nullopt;
  const JsonValue* offsetField = value.find("offsetBytes");
  const JsonValue* sizeField = value.find("sizeBytes");
  const JsonValue* stageField = value.find("stage");
  if (offsetField == nullptr || sizeField == nullptr || stageField == nullptr) return std::nullopt;
  const auto offset = readUint32(*offsetField);
  const auto size = readUint32(*sizeField);
  if (!offset.has_value() || !size.has_value() || !stageField->isString()) return std::nullopt;
  const auto stage = parseStage(stageField->asString());
  if (!stage.has_value()) return std::nullopt;

  PushConstantRange result;
  result.offsetBytes = *offset;
  result.sizeBytes = *size;
  result.stage = *stage;
  return result;
}

[[nodiscard]] std::optional<VertexInputAttribute> readVertexInputAttribute(const JsonValue& value) {
  if (!value.isObject()) return std::nullopt;
  const JsonValue* locationField = value.find("location");
  const JsonValue* typeField = value.find("type");
  if (locationField == nullptr || typeField == nullptr) return std::nullopt;
  const auto location = readUint32(*locationField);
  if (!location.has_value() || !typeField->isString()) return std::nullopt;
  const auto type = parseVertexAttributeType(typeField->asString());
  if (!type.has_value()) return std::nullopt;

  VertexInputAttribute result;
  result.location = *location;
  result.type = *type;
  return result;
}

[[nodiscard]] std::optional<std::uint32_t> readLocationEntry(const JsonValue& value) { return readUint32(value); }

void writeJsonString(std::ostringstream& out, const std::string& text) {
  out << '"';
  for (const char c : text) {
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << c;
    }
  }
  out << '"';
}

}  // namespace

atlantis::Result<ReflectionMetadata, ReflectionLoadError> loadReflectionMetadata(
    const std::filesystem::path& jsonPath) {
  std::ifstream file(jsonPath, std::ios::binary);
  if (!file.is_open()) return LoadResult::Err(ReflectionLoadError::FileNotFound);

  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) return LoadResult::Err(ReflectionLoadError::FileReadFailed);
  const std::string content = buffer.str();

  auto parsed = detail::parseJson(content);
  if (parsed.isErr()) return LoadResult::Err(ReflectionLoadError::MalformedJson);
  const JsonValue& root = parsed.value();
  if (!root.isObject()) return LoadResult::Err(ReflectionLoadError::MalformedJson);

  const JsonValue* schemaVersionField = root.find("schemaVersion");
  const JsonValue* entryPointNameField = root.find("entryPointName");
  const JsonValue* stageField = root.find("stage");
  if (schemaVersionField == nullptr || entryPointNameField == nullptr || stageField == nullptr) {
    return LoadResult::Err(ReflectionLoadError::MissingRequiredField);
  }
  if (!schemaVersionField->isNumber() || !entryPointNameField->isString() || !stageField->isString()) {
    return LoadResult::Err(ReflectionLoadError::MalformedJson);
  }

  const auto schemaVersionValue = readUint32(*schemaVersionField);
  if (!schemaVersionValue.has_value()) return LoadResult::Err(ReflectionLoadError::MalformedJson);
  if (static_cast<int>(*schemaVersionValue) > ReflectionMetadata::kCurrentSchemaVersion) {
    return LoadResult::Err(ReflectionLoadError::UnsupportedSchemaVersion);
  }

  const auto stageValue = parseStage(stageField->asString());
  if (!stageValue.has_value()) return LoadResult::Err(ReflectionLoadError::MalformedJson);

  ReflectionMetadata metadata;
  metadata.schemaVersion = static_cast<int>(*schemaVersionValue);
  metadata.entryPointName = entryPointNameField->asString();
  metadata.stage = *stageValue;

  if (!readOptionalArray(root, "descriptorBindings", metadata.descriptorBindings, readDescriptorBinding)) {
    return LoadResult::Err(ReflectionLoadError::MalformedJson);
  }
  if (!readOptionalArray(root, "pushConstantRanges", metadata.pushConstantRanges, readPushConstantRange)) {
    return LoadResult::Err(ReflectionLoadError::MalformedJson);
  }
  if (!readOptionalArray(root, "vertexInputAttributes", metadata.vertexInputAttributes, readVertexInputAttribute)) {
    return LoadResult::Err(ReflectionLoadError::MalformedJson);
  }
  if (!readOptionalArray(root, "varyingOutputLocations", metadata.varyingOutputLocations, readLocationEntry)) {
    return LoadResult::Err(ReflectionLoadError::MalformedJson);
  }
  if (!readOptionalArray(root, "varyingInputLocations", metadata.varyingInputLocations, readLocationEntry)) {
    return LoadResult::Err(ReflectionLoadError::MalformedJson);
  }

  if (const JsonValue* provenanceField = root.find("sdkProvenance");
      provenanceField != nullptr && provenanceField->isString()) {
    metadata.sdkProvenance = provenanceField->asString();
  }

  return LoadResult::Ok(std::move(metadata));
}

atlantis::Result<std::monostate, ReflectionSaveError> saveReflectionMetadata(const ReflectionMetadata& metadata,
                                                                               const std::filesystem::path& jsonPath) {
  std::ostringstream out;
  out << "{\n";
  out << "  \"schemaVersion\": " << metadata.schemaVersion << ",\n";
  out << "  \"entryPointName\": ";
  writeJsonString(out, metadata.entryPointName);
  out << ",\n";
  out << "  \"stage\": \"" << stageToString(metadata.stage) << "\",\n";

  out << "  \"descriptorBindings\": [";
  for (std::size_t i = 0; i < metadata.descriptorBindings.size(); ++i) {
    const auto& binding = metadata.descriptorBindings[i];
    out << (i == 0 ? "\n    " : ",\n    ");
    out << "{\"set\": " << binding.set << ", \"binding\": " << binding.binding << ", \"type\": \""
        << descriptorTypeToString(binding.type) << "\", \"stage\": \"" << stageToString(binding.stage) << "\"}";
  }
  out << (metadata.descriptorBindings.empty() ? "],\n" : "\n  ],\n");

  out << "  \"pushConstantRanges\": [";
  for (std::size_t i = 0; i < metadata.pushConstantRanges.size(); ++i) {
    const auto& range = metadata.pushConstantRanges[i];
    out << (i == 0 ? "\n    " : ",\n    ");
    out << "{\"offsetBytes\": " << range.offsetBytes << ", \"sizeBytes\": " << range.sizeBytes << ", \"stage\": \""
        << stageToString(range.stage) << "\"}";
  }
  out << (metadata.pushConstantRanges.empty() ? "],\n" : "\n  ],\n");

  out << "  \"vertexInputAttributes\": [";
  for (std::size_t i = 0; i < metadata.vertexInputAttributes.size(); ++i) {
    const auto& attribute = metadata.vertexInputAttributes[i];
    out << (i == 0 ? "\n    " : ",\n    ");
    out << "{\"location\": " << attribute.location << ", \"type\": \"" << vertexAttributeTypeToString(attribute.type)
        << "\"}";
  }
  out << (metadata.vertexInputAttributes.empty() ? "],\n" : "\n  ],\n");

  auto writeLocationArray = [&out](const std::vector<std::uint32_t>& locations) {
    out << "[";
    for (std::size_t i = 0; i < locations.size(); ++i) {
      out << (i == 0 ? "" : ", ") << locations[i];
    }
    out << "]";
  };
  out << "  \"varyingOutputLocations\": ";
  writeLocationArray(metadata.varyingOutputLocations);
  out << ",\n";
  out << "  \"varyingInputLocations\": ";
  writeLocationArray(metadata.varyingInputLocations);
  out << ",\n";

  out << "  \"sdkProvenance\": ";
  writeJsonString(out, metadata.sdkProvenance);
  out << "\n}\n";

  std::ofstream file(jsonPath, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    return atlantis::Result<std::monostate, ReflectionSaveError>::Err(ReflectionSaveError::FileWriteFailed);
  }
  const std::string text = out.str();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!file.good()) {
    return atlantis::Result<std::monostate, ReflectionSaveError>::Err(ReflectionSaveError::FileWriteFailed);
  }
  return atlantis::Result<std::monostate, ReflectionSaveError>::Ok(std::monostate{});
}

}  // namespace atlantis::shader_system
