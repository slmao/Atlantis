#pragma once

#include <filesystem>
#include <variant>

#include <atlantis/result.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system {

enum class ReflectionLoadError {
  FileNotFound,
  FileReadFailed,
  MalformedJson,
  UnsupportedSchemaVersion,  // schemaVersion field present but > kCurrentSchemaVersion
  MissingRequiredField,      // schemaVersion/entryPointName/stage absent
};

enum class ReflectionSaveError {
  FileWriteFailed,
};

// Loads and validates an Atlantis-schema reflection JSON file (NOT
// Slang's own raw -reflection-json output -- see slang_json_transform.h
// for that). Called at build time by atlantis_shader_compiler (to
// re-verify what it just wrote) and at runtime by
// ShaderSystemRhiIntegration. Not thread-safe; caller-thread-only
// (ADR-0004) -- each call performs a fresh, uncached file read, per
// ADR-0030's "Shader System does not cache, retain, or watch the file"
// rule.
[[nodiscard]] atlantis::Result<ReflectionMetadata, ReflectionLoadError> loadReflectionMetadata(
    const std::filesystem::path& jsonPath);

// Writes metadata as the Atlantis-schema JSON this module's own loader
// above can read back. Called only by atlantis_shader_compiler at build
// time -- no runtime code path in this Plan's scope ever writes this
// file.
//
// Deviation from Plan 0008 (mechanical, no architectural effect): the
// Plan's own Section 2 text writes this return type as
// `Result<void, ReflectionSaveError>`, but atlantis::Result<T, E>
// (src/core/include/atlantis/result.h) stores its state in a
// std::variant<T, E>, and std::variant cannot hold `void` as an
// alternative. atlantis::rhi::Device::waitIdle() already established
// this repository's own resolution for exactly this shape --
// Result<std::monostate, SubmitError> -- so this signature follows that
// existing precedent instead of literally instantiating Result<void, ...>.
[[nodiscard]] atlantis::Result<std::monostate, ReflectionSaveError> saveReflectionMetadata(
    const ReflectionMetadata& metadata, const std::filesystem::path& jsonPath);

}  // namespace atlantis::shader_system
