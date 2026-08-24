#include <atlantis/shader_system/slang_json_transform.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include "json_parser.h"
#include "json_value.h"

// Field-shape notes below are grounded in a real slangc invocation run
// during this Plan's own Implementation (Vulkan SDK 1.4.357.0,
// slang-standard-module-2026.13.1, matching Spec 0008's own Validation
// Evidence toolchain) -- not guessed. Plan 0008 Section 3 explicitly
// defers exact field-name confirmation to "Implementation's own
// fixture-driven development against a fresh real sample"; this is that
// sample. The two real, additive findings beyond Spec 0008's own
// Validation Evidence (which only exercised a "space"/nonzero-set probe
// and a single uniform-buffer "used" case) are recorded at the specific
// points below where they matter: (1) "used" is a JSON number (0/1),
// not a JSON boolean; (2) a pushConstantBuffer-kind bindings[] entry
// carries no "used" field at all, unlike a descriptorTableSlot-kind
// entry -- confirmed by disassembling the compiled fragment SPIR-V,
// which correctly omits the push-constant OpVariable entirely despite
// the raw JSON listing "pushConstants" in the fragment entry point's
// own bindings[] array.

namespace atlantis::shader_system {

namespace {

using detail::JsonArray;
using detail::JsonObject;
using detail::JsonValue;

using TransformResult = atlantis::Result<ReflectionMetadata, TransformError>;

[[nodiscard]] std::optional<std::uint32_t> readUint32Field(const JsonValue& object, const std::string& key) {
  const JsonValue* field = object.find(key);
  if (field == nullptr || !field->isNumber()) return std::nullopt;
  const double raw = field->asNumber();
  if (raw < 0.0 || std::floor(raw) != raw) return std::nullopt;
  if (raw > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) return std::nullopt;
  return static_cast<std::uint32_t>(raw);
}

[[nodiscard]] std::optional<std::string> readStringField(const JsonValue& object, const std::string& key) {
  const JsonValue* field = object.find(key);
  if (field == nullptr || !field->isString()) return std::nullopt;
  return field->asString();
}

// "used" is observed as a JSON number (0 or 1), not a JSON boolean --
// read leniently (accepting either shape) since nothing about that
// choice is architecturally significant here.
[[nodiscard]] bool isTruthy(const JsonValue& value) {
  if (value.isBool()) return value.asBool();
  if (value.isNumber()) return value.asNumber() != 0.0;
  return false;
}

[[nodiscard]] const JsonValue* findByName(const JsonArray& array, const std::string& name) {
  for (const JsonValue& entry : array) {
    if (!entry.isObject()) continue;
    const JsonValue* nameField = entry.find("name");
    if (nameField != nullptr && nameField->isString() && nameField->asString() == name) return &entry;
  }
  return nullptr;
}

// One flattened varying-location entry -- `typeNode` is the leaf's own
// "type" subtree (needed only by the vertex-input caller, which
// converts it to VertexAttributeType; the cross-stage-check callers
// only need `location`).
struct VaryingLeaf {
  std::uint32_t location = 0;
  const JsonValue* typeNode = nullptr;
};

// Recursively walks `node`, collecting every LEAF (a node whose own
// "type" is not itself a struct) whose "binding" object has
// "kind" == bindingKind. A struct-typed node's own outer "binding" (a
// "the whole struct starts at location N" grouping marker -- observed
// on both entryPoints[].parameters[] wrapper objects and
// entryPoints[].result for the vertex stage) is deliberately NOT
// recorded; only its individual fields are -- recording the wrapper too
// would double-count one real attribute as two.
void collectVaryingLeaves(const JsonValue& node, const std::string& bindingKind, std::vector<VaryingLeaf>& out) {
  if (!node.isObject()) return;
  const JsonValue* typeNode = node.find("type");
  const bool isStruct = typeNode != nullptr && typeNode->isObject() && readStringField(*typeNode, "kind") == "struct";

  if (isStruct) {
    const JsonValue* fields = typeNode->find("fields");
    if (fields != nullptr && fields->isArray()) {
      for (const JsonValue& field : fields->asArray()) collectVaryingLeaves(field, bindingKind, out);
    }
    return;
  }

  const JsonValue* binding = node.find("binding");
  if (binding == nullptr || !binding->isObject()) return;
  if (readStringField(*binding, "kind") != bindingKind) return;
  const auto index = readUint32Field(*binding, "index");
  if (!index.has_value()) return;
  out.push_back(VaryingLeaf{*index, typeNode});
}

[[nodiscard]] std::optional<VertexAttributeType> vertexAttributeTypeFromTypeNode(const JsonValue* typeNode) {
  if (typeNode == nullptr || !typeNode->isObject()) return std::nullopt;
  if (readStringField(*typeNode, "kind") != "vector") return std::nullopt;
  const auto elementCount = readUint32Field(*typeNode, "elementCount");
  const JsonValue* elementType = typeNode->find("elementType");
  if (!elementCount.has_value() || (*elementCount != 3 && *elementCount != 2) || elementType == nullptr ||
      !elementType->isObject()) {
    return std::nullopt;
  }
  if (readStringField(*elementType, "kind") != "scalar") return std::nullopt;
  if (readStringField(*elementType, "scalarType") != "float32") return std::nullopt;
  return *elementCount == 3 ? VertexAttributeType::Float3 : VertexAttributeType::Float2;
}

}  // namespace

atlantis::Result<ReflectionMetadata, TransformError> transformSlangReflectionJson(
    const std::filesystem::path& slangRawJsonPath, const std::string& requestedEntryPointName, ShaderStage stage,
    const std::string& sdkProvenance) {
  std::ifstream file(slangRawJsonPath, std::ios::binary);
  if (!file.is_open()) return TransformResult::Err(TransformError::FileNotFound);

  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) return TransformResult::Err(TransformError::FileReadFailed);

  auto parsed = detail::parseJson(buffer.str());
  if (parsed.isErr()) return TransformResult::Err(TransformError::MalformedJson);
  const JsonValue& root = parsed.value();
  if (!root.isObject()) return TransformResult::Err(TransformError::MalformedJson);

  const JsonValue* moduleParameters = root.find("parameters");
  if (moduleParameters == nullptr || !moduleParameters->isArray()) {
    return TransformResult::Err(TransformError::UnexpectedStructure);
  }

  const JsonValue* entryPoints = root.find("entryPoints");
  if (entryPoints == nullptr || !entryPoints->isArray()) return TransformResult::Err(TransformError::UnexpectedStructure);

  const JsonValue* entryPointNode = nullptr;
  for (const JsonValue& candidate : entryPoints->asArray()) {
    if (!candidate.isObject()) continue;
    const auto name = readStringField(candidate, "name");
    if (name.has_value() && *name == requestedEntryPointName) {
      entryPointNode = &candidate;
      break;
    }
  }
  if (entryPointNode == nullptr) return TransformResult::Err(TransformError::EntryPointNotFound);

  ReflectionMetadata metadata;
  metadata.entryPointName = requestedEntryPointName;
  metadata.stage = stage;
  metadata.sdkProvenance = sdkProvenance;

  // Descriptor bindings and push-constant ranges -- driven by this
  // entry point's own "bindings" array (Section 3/5: this is what
  // filters module-scope "parameters" down to what THIS entry point
  // actually uses), cross-referenced against the module-level
  // "parameters" entry of the same name for resource-kind/offset/size
  // detail the "bindings" entry itself does not carry.
  const JsonValue* bindingsField = entryPointNode->find("bindings");
  if (bindingsField != nullptr) {
    if (!bindingsField->isArray()) return TransformResult::Err(TransformError::UnexpectedStructure);

    for (const JsonValue& bindingEntry : bindingsField->asArray()) {
      if (!bindingEntry.isObject()) return TransformResult::Err(TransformError::UnexpectedStructure);
      const auto resourceName = readStringField(bindingEntry, "name");
      const JsonValue* bindingObject = bindingEntry.find("binding");
      if (!resourceName.has_value() || bindingObject == nullptr || !bindingObject->isObject()) {
        return TransformResult::Err(TransformError::UnexpectedStructure);
      }
      const auto kind = readStringField(*bindingObject, "kind");
      if (!kind.has_value()) return TransformResult::Err(TransformError::UnexpectedStructure);

      const JsonValue* moduleParam = findByName(moduleParameters->asArray(), *resourceName);

      if (*kind == "descriptorTableSlot") {
        // "used" is present on every real descriptorTableSlot bindings[]
        // entry observed (both the used==1 and used==0 cases) -- absence
        // is treated as "used" defensively (never silently drops a real
        // binding the contract check downstream still gets a chance to
        // reject), rather than as a structural error.
        const JsonValue* usedField = bindingObject->find("used");
        const bool used = usedField == nullptr || isTruthy(*usedField);
        if (!used) continue;

        const auto index = readUint32Field(*bindingObject, "index");
        if (!index.has_value()) return TransformResult::Err(TransformError::UnexpectedStructure);
        const auto space = readUint32Field(*bindingObject, "space");  // Section 3's positive rule: absent -> 0

        if (moduleParam == nullptr) return TransformResult::Err(TransformError::UnexpectedStructure);
        const JsonValue* moduleType = moduleParam->find("type");
        const auto moduleTypeKind = moduleType != nullptr && moduleType->isObject()
                                         ? readStringField(*moduleType, "kind")
                                         : std::nullopt;
        if (!moduleTypeKind.has_value()) return TransformResult::Err(TransformError::UnexpectedStructure);

        if (*moduleTypeKind == "constantBuffer") {
          metadata.descriptorBindings.push_back(
              DescriptorBinding{.set = space.value_or(0), .binding = *index, .type = DescriptorType::UniformBuffer, .stage = stage});
        } else if (*moduleTypeKind == "resource") {
          // Spec 0016/D6: a combined image sampler ([[vk::binding(N,M)]]
          // Sampler2D) also reflects as a descriptorTableSlot binding --
          // the same top-level kind a uniform buffer uses. The real
          // distinguishing shape is one level deeper, in the module
          // parameter's own type object -- empirically confirmed against
          // a real slangc compile (see this file's own top comment):
          // type.kind == "resource", type.baseShape == "texture2D", and
          // type.combined == true (a JSON boolean) confirms *combined*
          // image+sampler, never a separate Texture2D + SamplerState pair.
          const auto baseShape = readStringField(*moduleType, "baseShape");
          const JsonValue* combinedField = moduleType->find("combined");
          const bool isCombined = combinedField != nullptr && isTruthy(*combinedField);
          if (baseShape.has_value() && *baseShape == "texture2D" && isCombined) {
            metadata.descriptorBindings.push_back(
                DescriptorBinding{.set = space.value_or(0), .binding = *index, .type = DescriptorType::Sampler, .stage = stage});
          } else {
            // A resource binding shape this module does not model (e.g.
            // a separate, non-combined SamplerState/Texture2D pair, or a
            // 3D/cubemap texture) -- still an explicit, named structural
            // error, never silently skipped, matching the constantBuffer
            // branch's own "genuinely new shape, not silently mis-typed"
            // precedent.
            return TransformResult::Err(TransformError::UnexpectedStructure);
          }
        } else {
          // This round's only reflected descriptorTableSlot resource
          // kinds are a uniform buffer and a combined image sampler
          // (Spec 0016/D6's narrow scope) -- anything else reflecting as
          // a descriptorTableSlot is a genuinely new shape this module
          // does not model, not silently mis-typed.
          return TransformResult::Err(TransformError::UnexpectedStructure);
        }

      } else if (*kind == "pushConstantBuffer") {
        // No "used" field is present on this binding kind in real
        // output, even for an entry point whose compiled SPIR-V does
        // not reference it at all (confirmed by disassembly) -- see this
        // file's own top comment. Every pushConstantBuffer entry present
        // in this entry point's own "bindings" is therefore included
        // unconditionally; compile_and_validate.cpp's own push-constant
        // check (Plan Section 5 step 8) is scoped to the vertex stage
        // only, matching this material's own fixed, vertex-only
        // push-constant design -- a stray Fragment-stage
        // PushConstantRange this produces is harmless and unread by any
        // consumer, not a validated shape.
        if (moduleParam == nullptr) return TransformResult::Err(TransformError::UnexpectedStructure);
        const JsonValue* elementVarLayout = nullptr;
        if (const JsonValue* moduleType = moduleParam->find("type"); moduleType != nullptr && moduleType->isObject()) {
          elementVarLayout = moduleType->find("elementVarLayout");
        }
        const JsonValue* sizeBinding =
            elementVarLayout != nullptr && elementVarLayout->isObject() ? elementVarLayout->find("binding") : nullptr;
        if (sizeBinding == nullptr || !sizeBinding->isObject()) {
          return TransformResult::Err(TransformError::UnexpectedStructure);
        }
        const auto offsetBytes = readUint32Field(*sizeBinding, "offset");
        const auto sizeBytes = readUint32Field(*sizeBinding, "size");
        if (!offsetBytes.has_value() || !sizeBytes.has_value()) {
          return TransformResult::Err(TransformError::UnexpectedStructure);
        }
        metadata.pushConstantRanges.push_back(
            PushConstantRange{.offsetBytes = *offsetBytes, .sizeBytes = *sizeBytes, .stage = stage});
      }
      // Any other top-level binding kind is outside this round's modeled
      // scope (Spec 0008/ADR-0030) and is silently skipped here, matching
      // Section 3's general "unknown fields are ignored" rule extended to
      // unknown binding kinds. This is narrower than it was before Spec
      // 0016/D6: descriptorTableSlot itself now recognizes two distinct
      // module-type shapes (constantBuffer, resource+texture2D+combined)
      // instead of one, and any *other* resource shape within a
      // descriptorTableSlot (a non-combined sampler, a non-2D texture, a
      // storage buffer, etc.) is an explicit, named UnexpectedStructure
      // above, not silently absent from descriptorBindings -- only a
      // wholly different top-level `kind` string (neither
      // descriptorTableSlot nor pushConstantBuffer) still falls through
      // to this silent skip.
    }
  }

  // Vertex-input attributes (vertex stage only) -- from this entry
  // point's own function parameters.
  if (stage == ShaderStage::Vertex) {
    const JsonValue* parameters = entryPointNode->find("parameters");
    if (parameters != nullptr) {
      if (!parameters->isArray()) return TransformResult::Err(TransformError::UnexpectedStructure);
      std::vector<VaryingLeaf> leaves;
      for (const JsonValue& parameter : parameters->asArray()) collectVaryingLeaves(parameter, "varyingInput", leaves);
      for (const VaryingLeaf& leaf : leaves) {
        const auto attributeType = vertexAttributeTypeFromTypeNode(leaf.typeNode);
        if (!attributeType.has_value()) return TransformResult::Err(TransformError::UnsupportedVertexAttributeType);
        metadata.vertexInputAttributes.push_back(VertexInputAttribute{.location = leaf.location, .type = *attributeType});
      }
    }

    const JsonValue* result = entryPointNode->find("result");
    if (result != nullptr) {
      std::vector<VaryingLeaf> outputs;
      collectVaryingLeaves(*result, "varyingOutput", outputs);
      for (const VaryingLeaf& leaf : outputs) metadata.varyingOutputLocations.push_back(leaf.location);
    }
  }

  // Varying inputs (fragment stage only) -- for the supplementary
  // cross-stage interface check (Plan Section 5 step 12).
  if (stage == ShaderStage::Fragment) {
    const JsonValue* parameters = entryPointNode->find("parameters");
    if (parameters != nullptr) {
      if (!parameters->isArray()) return TransformResult::Err(TransformError::UnexpectedStructure);
      std::vector<VaryingLeaf> leaves;
      for (const JsonValue& parameter : parameters->asArray()) collectVaryingLeaves(parameter, "varyingInput", leaves);
      for (const VaryingLeaf& leaf : leaves) metadata.varyingInputLocations.push_back(leaf.location);
    }
  }

  return TransformResult::Ok(std::move(metadata));
}

}  // namespace atlantis::shader_system
