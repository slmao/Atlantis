#pragma once

#include <atlantis/asset_system/load.h>

#include <fstream>
#include <optional>
#include <string>

namespace atlantis::asset_system::detail {

// Measure and read the same opened handle. Probe EOF after an exact read,
// so a file growing beyond the cap cannot be silently accepted truncated.
template <typename ByteContainer>
[[nodiscard]] std::optional<PreviewArtifactLoadError> readPreviewFile(
    const std::string& path, std::size_t cap, ByteContainer& out) {
  using E = PreviewArtifactLoadError;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return E::Invalid;
  const auto size = static_cast<std::streamoff>(file.tellg());
  if (size < 0) return E::Invalid;
  if (size > static_cast<std::streamoff>(cap)) return E::TooLarge;
  file.seekg(0);
  if (!file) return E::Invalid;
  out.resize(static_cast<std::size_t>(size));
  if (size != 0 && !file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size))) {
    return E::Invalid;
  }
  if (file.peek() != std::char_traits<char>::eof()) {
    return out.size() == cap ? E::TooLarge : E::Invalid;
  }
  return file.eof() && !file.bad() ? std::nullopt : std::optional{E::Invalid};
}

}  // namespace atlantis::asset_system::detail
