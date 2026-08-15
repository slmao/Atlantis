#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace atlantis::shader_system {

// ADR-0031's provenance anchor: no confirmed slangc --version flag
// exists, so provenance is read from the Vulkan SDK's own directory
// structure instead -- specifically, a slang-standard-module-<version>
// directory sibling to slangc.exe (Spec 0008's Validation Evidence
// observed slang-standard-module-2026.13.1 on the reference SDK
// install). Returns std::nullopt if that directory is not found -- this
// is a best-effort provenance string, not a hard build requirement; its
// absence does not fail the build (contrast with slangc/spirv-val
// themselves, which do).
[[nodiscard]] std::optional<std::string> describeSdkProvenance(const std::filesystem::path& slangcExecutablePath);

}  // namespace atlantis::shader_system
