#include <atlantis/runtime/scene_manifest.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/logical_path.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace atlantis::runtime {

namespace {

using atlantis::asset_system::AssetId;
using atlantis::asset_system::computeAssetId;
using atlantis::asset_system::normalizeLogicalPath;
using atlantis::asset_system::parseAssetMetadata;

// Duplicated from mesh_source.cpp's own identical helper rather than
// shared, matching that file's own file-local, not-exported precedent
// -- and confirmed necessary here specifically, not merely by
// precedent: Step 5's own scene_asset_cmake_declaration_tests.cpp
// empirically confirmed file(GENERATE) writes this toolchain's native
// \r\n line ending on Windows, not \n.
[[nodiscard]] std::vector<std::string_view> splitLines(std::string_view text) {
  if (text.empty()) return {};

  std::string_view body = text;
  if (body.back() == '\n') body.remove_suffix(1);

  std::vector<std::string_view> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    if (i == body.size() || body[i] == '\n') {
      std::string_view line = body.substr(start, i - start);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      lines.push_back(line);
      start = i + 1;
    }
  }
  return lines;
}

struct ParsedManifestLine {
  std::string logicalPath;
  std::string artifactPath;
  std::string metadataPath;
};

[[nodiscard]] bool splitManifestLine(std::string_view line, ParsedManifestLine& out) {
  const std::size_t firstTab = line.find('\t');
  if (firstTab == std::string_view::npos) return false;
  const std::size_t secondTab = line.find('\t', firstTab + 1);
  if (secondTab == std::string_view::npos) return false;
  if (line.find('\t', secondTab + 1) != std::string_view::npos) return false;  // more than two tabs

  const std::string_view logicalPath = line.substr(0, firstTab);
  const std::string_view artifactPath = line.substr(firstTab + 1, secondTab - firstTab - 1);
  const std::string_view metadataPath = line.substr(secondTab + 1);
  if (logicalPath.empty() || artifactPath.empty() || metadataPath.empty()) return false;

  out.logicalPath = std::string(logicalPath);
  out.artifactPath = std::string(artifactPath);
  out.metadataPath = std::string(metadataPath);
  return true;
}

[[nodiscard]] bool readFileText(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) return false;
  out = buffer.str();
  return true;
}

}  // namespace

namespace detail {

atlantis::Result<std::monostate, SceneManifestError> checkForDuplicatesAndCollisions(
    const std::vector<ManifestEntryForCollisionCheck>& entries) {
  using ResultT = atlantis::Result<std::monostate, SceneManifestError>;

  // Step 3: duplicate logical path -- sort-and-adjacency-check.
  {
    std::vector<std::string> logicalPaths;
    logicalPaths.reserve(entries.size());
    for (const auto& entry : entries) logicalPaths.push_back(entry.normalizedLogicalPath);
    std::sort(logicalPaths.begin(), logicalPaths.end());
    for (std::size_t i = 1; i < logicalPaths.size(); ++i) {
      if (logicalPaths[i] == logicalPaths[i - 1]) return ResultT::Err(SceneManifestError::DuplicateLogicalPath);
    }
  }

  // Step 4: AssetId collision -- a different logical path producing
  // the same AssetId.
  {
    std::vector<const ManifestEntryForCollisionCheck*> byAssetId;
    byAssetId.reserve(entries.size());
    for (const auto& entry : entries) byAssetId.push_back(&entry);
    std::sort(byAssetId.begin(), byAssetId.end(), [](const auto* a, const auto* b) { return a->assetId < b->assetId; });
    for (std::size_t i = 1; i < byAssetId.size(); ++i) {
      if (byAssetId[i]->assetId == byAssetId[i - 1]->assetId &&
          byAssetId[i]->normalizedLogicalPath != byAssetId[i - 1]->normalizedLogicalPath) {
        return ResultT::Err(SceneManifestError::AssetIdCollision);
      }
    }
  }

  return ResultT::Ok(std::monostate{});
}

}  // namespace detail

const SceneDependencyResolver::Entry* SceneDependencyResolver::find(atlantis::asset_system::AssetId id) const noexcept {
  const auto it = std::lower_bound(entries.begin(), entries.end(), id,
                                    [](const auto& entry, AssetId value) { return entry.first < value; });
  if (it == entries.end() || it->first != id) return nullptr;
  return &it->second;
}

atlantis::Result<SceneDependencyResolver, SceneManifestError> loadSceneDependencyManifest(
    const std::string& manifestPath) {
  using ResultT = atlantis::Result<SceneDependencyResolver, SceneManifestError>;

  // Step 1: read + split; a line with not exactly two tabs, or an
  // empty field, is MalformedEntry.
  std::string text;
  if (!readFileText(manifestPath, text)) return ResultT::Err(SceneManifestError::ManifestUnreadable);

  std::vector<ParsedManifestLine> parsedLines;
  for (std::string_view line : splitLines(text)) {
    ParsedManifestLine parsed;
    if (!splitManifestLine(line, parsed)) return ResultT::Err(SceneManifestError::MalformedEntry);
    parsedLines.push_back(std::move(parsed));
  }

  // Step 2: normalizeLogicalPath() + computeAssetId() over each
  // entry's own logical-path field -- a LogicalPathError here also
  // folds into MalformedEntry.
  struct ResolvedEntry {
    std::string normalizedLogicalPath;
    AssetId assetId = 0;
    std::string artifactPath;
    std::string metadataPath;
  };
  std::vector<ResolvedEntry> resolved;
  resolved.reserve(parsedLines.size());
  for (const ParsedManifestLine& line : parsedLines) {
    const auto normalizedResult = normalizeLogicalPath(line.logicalPath);
    if (normalizedResult.isErr()) return ResultT::Err(SceneManifestError::MalformedEntry);
    const std::string& normalizedLogicalPath = normalizedResult.value();
    resolved.push_back(ResolvedEntry{normalizedLogicalPath, computeAssetId(normalizedLogicalPath), line.artifactPath,
                                      line.metadataPath});
  }

  // Steps 3-4: duplicate logical path, then AssetId collision --
  // delegated to detail::checkForDuplicatesAndCollisions() (see
  // scene_manifest.h's own note on why this is a separate, testable
  // seam).
  {
    std::vector<detail::ManifestEntryForCollisionCheck> forCheck;
    forCheck.reserve(resolved.size());
    for (const ResolvedEntry& entry : resolved) {
      forCheck.push_back(detail::ManifestEntryForCollisionCheck{entry.assetId, entry.normalizedLogicalPath});
    }
    const auto checkResult = detail::checkForDuplicatesAndCollisions(forCheck);
    if (checkResult.isErr()) return ResultT::Err(checkResult.error());
  }

  // Step 5: metadata cross-check -- each entry's own metadata sidecar
  // must record the exact AssetId this manifest's own logical-path
  // field computes.
  for (const ResolvedEntry& entry : resolved) {
    std::string metadataText;
    if (!readFileText(entry.metadataPath, metadataText)) {
      return ResultT::Err(SceneManifestError::MetadataArtifactMismatch);
    }
    const auto metadataResult = parseAssetMetadata(metadataText);
    if (metadataResult.isErr() || metadataResult.value().assetId != entry.assetId) {
      return ResultT::Err(SceneManifestError::MetadataArtifactMismatch);
    }
  }

  // Step 6: build the AssetId-sorted resolver -- a point-lookup
  // structure only, never iterated end-to-end for load-order purposes
  // (see scene_manifest.h's own note).
  SceneDependencyResolver resolver;
  resolver.entries.reserve(resolved.size());
  for (const ResolvedEntry& entry : resolved) {
    resolver.entries.emplace_back(entry.assetId,
                                   SceneDependencyResolver::Entry{entry.artifactPath, entry.metadataPath});
  }
  std::sort(resolver.entries.begin(), resolver.entries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  return ResultT::Ok(std::move(resolver));
}

const char* toString(SceneManifestError error) noexcept {
  switch (error) {  // no default -- matching init_error.cpp's own established idiom
    case SceneManifestError::ManifestUnreadable:
      return "ManifestUnreadable";
    case SceneManifestError::MalformedEntry:
      return "MalformedEntry";
    case SceneManifestError::DuplicateLogicalPath:
      return "DuplicateLogicalPath";
    case SceneManifestError::AssetIdCollision:
      return "AssetIdCollision";
    case SceneManifestError::MetadataArtifactMismatch:
      return "MetadataArtifactMismatch";
  }
  ATLANTIS_CHECK_MSG(false, "toString(SceneManifestError): unhandled enumerator");
  return "(unrecognized SceneManifestError)";
}

}  // namespace atlantis::runtime
