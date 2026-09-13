#include "asset_extraction.h"

#include <atlantis/log.h>

#include <android/asset_manager.h>

#include <sys/stat.h>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <vector>

namespace atlantis::runtime::android_detail {

namespace {

// Creates every missing intermediate directory in filePath's own
// directory portion (everything before its last '/') -- POSIX mkdir()
// is not itself recursive, and relativePath may contain subdirectories
// (e.g. "shaders/minimal_renderer/..."). Ignores an individual mkdir()
// failure (most commonly EEXIST, since an earlier extraction may have
// already created a shared parent directory) -- any failure that
// actually matters surfaces as the subsequent file write failing
// visibly.
void makeDirectoriesFor(const std::string& filePath) {
  for (std::size_t i = 0; i < filePath.size(); ++i) {
    if (filePath[i] == '/' && i > 0) {
      const std::string parent = filePath.substr(0, i);
      ::mkdir(parent.c_str(), 0770);
    }
  }
}

[[nodiscard]] bool fileExistsWithSize(const std::string& path, long long expectedSize) {
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0) {
    return false;
  }
  return static_cast<long long>(info.st_size) == expectedSize;
}

}  // namespace

std::string extractAsset(AAssetManager* mgr, const std::string& internalDataPath, const std::string& relativePath) {
  const std::string destination = internalDataPath + "/" + relativePath;

  AAsset* asset = AAssetManager_open(mgr, relativePath.c_str(), AASSET_MODE_STREAMING);
  if (asset == nullptr) {
    ATLANTIS_LOG_ERROR("extractAsset(): failed to open packaged asset {}", relativePath);
    return {};
  }

  const off64_t length = AAsset_getLength64(asset);
  if (fileExistsWithSize(destination, static_cast<long long>(length))) {
    AAsset_close(asset);
    return destination;
  }

  std::vector<std::byte> buffer(static_cast<std::size_t>(length));
  std::size_t totalRead = 0;
  while (totalRead < buffer.size()) {
    const int readResult = AAsset_read(asset, buffer.data() + totalRead, buffer.size() - totalRead);
    if (readResult <= 0) {
      break;
    }
    totalRead += static_cast<std::size_t>(readResult);
  }
  AAsset_close(asset);

  if (totalRead != buffer.size()) {
    ATLANTIS_LOG_ERROR("extractAsset(): short read for packaged asset {} ({} of {} bytes)", relativePath, totalRead,
                        buffer.size());
    return {};
  }

  makeDirectoriesFor(destination);

  std::ofstream out(destination, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    ATLANTIS_LOG_ERROR("extractAsset(): failed to open destination file {}", destination);
    return {};
  }
  out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
  if (!out) {
    ATLANTIS_LOG_ERROR("extractAsset(): failed to write destination file {}", destination);
    return {};
  }

  return destination;
}

std::string extractSceneManifest(AAssetManager* mgr, const std::string& internalDataPath,
                                  const std::string& relativePath) {
  AAsset* asset = AAssetManager_open(mgr, relativePath.c_str(), AASSET_MODE_STREAMING);
  if (asset == nullptr) {
    ATLANTIS_LOG_ERROR("extractSceneManifest(): failed to open packaged manifest {}", relativePath);
    return {};
  }
  const off64_t length = AAsset_getLength64(asset);
  std::string content(static_cast<std::size_t>(length), '\0');
  std::size_t totalRead = 0;
  while (totalRead < content.size()) {
    const int readResult = AAsset_read(asset, content.data() + totalRead, content.size() - totalRead);
    if (readResult <= 0) {
      break;
    }
    totalRead += static_cast<std::size_t>(readResult);
  }
  AAsset_close(asset);
  if (totalRead != content.size()) {
    ATLANTIS_LOG_ERROR("extractSceneManifest(): short read for packaged manifest {}", relativePath);
    return {};
  }

  // scene_manifest.h's own documented format: one tab-separated
  // <logical path>\t<artifact path>\t<metadata path> triple per line.
  // Gradle's own packaging task (android/app/build.gradle) already
  // rewrote the latter two columns from absolute Windows build-tree
  // paths to paths relative to the packaged assets root -- this loop
  // extracts the file each of those now-relative columns names, then
  // rewrites the column to that extraction's own real, per-device
  // absolute path.
  std::ostringstream rewritten;
  std::istringstream lines(content);
  std::string line;
  bool ok = true;
  while (std::getline(lines, line)) {
    if (line.empty()) {
      continue;
    }
    const std::size_t firstTab = line.find('\t');
    const std::size_t secondTab = firstTab == std::string::npos ? std::string::npos : line.find('\t', firstTab + 1);
    if (firstTab == std::string::npos || secondTab == std::string::npos) {
      ATLANTIS_LOG_ERROR("extractSceneManifest(): malformed manifest line: {}", line);
      ok = false;
      continue;
    }
    const std::string logicalPath = line.substr(0, firstTab);
    const std::string artifactRelPath = line.substr(firstTab + 1, secondTab - firstTab - 1);
    const std::string metadataRelPath = line.substr(secondTab + 1);

    const std::string extractedArtifactPath = extractAsset(mgr, internalDataPath, artifactRelPath);
    const std::string extractedMetadataPath = extractAsset(mgr, internalDataPath, metadataRelPath);
    if (extractedArtifactPath.empty() || extractedMetadataPath.empty()) {
      ok = false;
      continue;
    }
    rewritten << logicalPath << '\t' << extractedArtifactPath << '\t' << extractedMetadataPath << '\n';
  }
  if (!ok) {
    return {};
  }

  const std::string destination = internalDataPath + "/" + relativePath;
  makeDirectoriesFor(destination);
  std::ofstream out(destination, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    ATLANTIS_LOG_ERROR("extractSceneManifest(): failed to open destination file {}", destination);
    return {};
  }
  out << rewritten.str();
  if (!out) {
    ATLANTIS_LOG_ERROR("extractSceneManifest(): failed to write destination file {}", destination);
    return {};
  }
  return destination;
}

}  // namespace atlantis::runtime::android_detail
