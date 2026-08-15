#include <atlantis/shader_system/version_provenance.h>

#include <system_error>

namespace atlantis::shader_system {

std::optional<std::string> describeSdkProvenance(const std::filesystem::path& slangcExecutablePath) {
  const std::filesystem::path binDir = slangcExecutablePath.parent_path();

  std::error_code walkError;
  std::string moduleDirName;
  bool found = false;
  for (const auto& entry : std::filesystem::directory_iterator(binDir, walkError)) {
    if (walkError) break;
    if (!entry.is_directory()) continue;
    const std::string name = entry.path().filename().string();
    if (name.rfind("slang-standard-module-", 0) == 0) {
      moduleDirName = name;
      found = true;
      break;
    }
  }
  if (!found) return std::nullopt;

  // Best-effort only (this function's own doc comment): the Vulkan SDK
  // version is read from Bin/'s own parent directory name (e.g.
  // "C:\VulkanSDK\1.4.357.0\Bin\slangc.exe" -> "1.4.357.0"). A
  // non-standard install layout simply yields a less informative, but
  // still harmless, provenance string -- this value is never parsed
  // back by any Atlantis code, only surfaced for diagnostics/logging.
  const std::string sdkVersion = binDir.parent_path().filename().string();
  return sdkVersion + " / " + moduleDirName;
}

}  // namespace atlantis::shader_system
