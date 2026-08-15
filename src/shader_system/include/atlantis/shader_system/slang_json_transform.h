#pragma once

#include <filesystem>
#include <string>

#include <atlantis/result.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system {

enum class TransformError {
  FileNotFound,
  FileReadFailed,
  MalformedJson,                   // Slang's own JSON does not even parse as JSON
  UnexpectedStructure,              // parses as JSON, but not in the shape this module
                                     // expects (e.g. "entryPoints" missing/not an array) --
                                     // a real Slang-JSON-shape-changed signal
  UnsupportedVertexAttributeType,  // a vertex-stage attribute's Slang type has no
                                    // VertexAttributeType mapping (only Float3 exists this round)
  EntryPointNotFound,              // requestedEntryPointName not present in Slang's own
                                    // "entryPoints" array
};

// Reads Slang's own raw -reflection-json output (an external,
// undocumented, unversioned format -- ADR-0030's own rationale for this
// transform step existing at all) and re-projects the ONE named entry
// point's data into this module's own ReflectionMetadata schema. Called
// only by atlantis_shader_compiler, at build time, immediately after a
// slangc invocation succeeds.
[[nodiscard]] atlantis::Result<ReflectionMetadata, TransformError> transformSlangReflectionJson(
    const std::filesystem::path& slangRawJsonPath, const std::string& requestedEntryPointName, ShaderStage stage,
    const std::string& sdkProvenance);

}  // namespace atlantis::shader_system
