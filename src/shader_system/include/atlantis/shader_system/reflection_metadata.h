#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace atlantis::shader_system {

// Mirrors atlantis::rhi::VertexAttributeFormat's own shape -- see
// reflection_metadata.cpp's mapping from Slang's scalar-type/
// element-count pair. A reflected attribute whose Slang type does not
// map to a value here is a transform-time error
// (TransformError::UnsupportedVertexAttributeType), never silently
// coerced.
enum class VertexAttributeType {
  Float3,
  Float2,  // new (Spec 0016/D6) -- the interleaved Vertex layout's UV attribute
};

enum class ShaderStage {
  Vertex,
  Fragment,
};

enum class DescriptorType {
  UniformBuffer,  // ADR-0030's narrow scope
  Sampler,  // new (Spec 0016/D6) -- a combined image sampler, matching
            // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER's own shape
};

// [JSON-verified against a real Slang reflection sample, per Spec 0008's
// Validation Evidence and ADR-0030's own "Descriptor set index
// [JSON-verified, with a parsing hazard]" entry] set/binding is read
// from Slang's own "descriptorTableSlot" binding kind's "space"/"index"
// fields. A dedicated probe compiling a [[vk::binding(3, 2)]] resource
// emitted {"kind": "descriptorTableSlot", "space": 2, "index": 3},
// confirmed against the same module's disassembled SPIR-V
// (DescriptorSet 2 / Binding 3); a [[vk::binding(0, 0)]] resource
// emitted no "space" key at all. slang_json_transform.cpp therefore
// parses ANY set value the JSON reports (0 or otherwise) into this
// field -- it does not fail closed on a nonzero set. What DOES reject a
// nonzero set is a separate, later step: Minimal Renderer's own fixed
// expected contract (minimalRendererExpectedDescriptorContract(),
// descriptor_contract.h) only accepts set 0 / binding 0 -- a
// nonzero-set shader parses into a perfectly valid ReflectionMetadata
// and then fails validateDescriptorContract() with a real, specific
// ContractMismatchError. Parsing capability and contract acceptance are
// deliberately two separate, independently-testable layers.
struct DescriptorBinding {
  std::uint32_t set = 0;
  std::uint32_t binding = 0;
  DescriptorType type = DescriptorType::UniformBuffer;
  ShaderStage stage = ShaderStage::Vertex;
};

[[nodiscard]] bool operator==(const DescriptorBinding& lhs, const DescriptorBinding& rhs);

struct PushConstantRange {
  std::uint32_t offsetBytes = 0;
  std::uint32_t sizeBytes = 0;
  ShaderStage stage = ShaderStage::Vertex;
};

[[nodiscard]] bool operator==(const PushConstantRange& lhs, const PushConstantRange& rhs);

// location/type are Shader-System-reflected (from the shader's own
// explicit [[vk::location(X)]] attribute and Slang type, ADR-0030).
// offsetBytes/strideBytes are deliberately ABSENT from this type -- no
// shader reflection tool can derive a host-side interleaved
// vertex-buffer layout from shader source (ADR-0030's own Decision).
// Those two values live on the *caller*-supplied
// MeshVertexAttributeSchema (rhi_integration/vertex_input_mapping.h),
// combined with this type only at the point toVertexInputLayout() runs.
struct VertexInputAttribute {
  std::uint32_t location = 0;
  VertexAttributeType type = VertexAttributeType::Float3;
};

[[nodiscard]] bool operator==(const VertexInputAttribute& lhs, const VertexInputAttribute& rhs);

// The single, Atlantis-owned, versioned schema this whole module reads
// and writes -- populated FROM Slang's own raw -reflection-json output
// by slang_json_transform.cpp, never Slang's raw JSON re-exposed
// verbatim (ADR-0030's own rationale: insulates the rest of Atlantis
// from Slang's own, undocumented, unversioned JSON shape). One instance
// describes exactly one compiled shader STAGE (one entry point) -- a
// full material's worth of reflection (vertex + fragment) is two
// separate ReflectionMetadata values, loaded separately.
struct ReflectionMetadata {
  static constexpr int kCurrentSchemaVersion = 1;

  int schemaVersion = kCurrentSchemaVersion;
  std::string entryPointName;  // always "vertexMain"/"fragmentMain" etc. -- the SLANG
                                // source function name, NOT the emitted SPIR-V
                                // OpEntryPoint name, which is always "main"
  ShaderStage stage = ShaderStage::Vertex;
  std::vector<DescriptorBinding> descriptorBindings;  // only bindings this entry point's own
                                                        // bindings[].used == true -- module-level-
                                                        // but-unused parameters are filtered out
                                                        // here, never carried into this struct
  std::vector<PushConstantRange> pushConstantRanges;
  std::vector<VertexInputAttribute> vertexInputAttributes;  // empty for a non-vertex stage
  std::vector<std::uint32_t> varyingOutputLocations;  // vertex stage only -- for the
                                                        // supplementary cross-stage check
  std::vector<std::uint32_t> varyingInputLocations;    // fragment stage only
  std::string sdkProvenance;  // e.g. "1.4.357.0 / slang-standard-module-2026.13.1" --
                               // see version_provenance.h; opaque to every consumer except
                               // diagnostics/logging, never parsed back
};

[[nodiscard]] bool operator==(const ReflectionMetadata& lhs, const ReflectionMetadata& rhs);

}  // namespace atlantis::shader_system
