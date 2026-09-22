#include <atlantis/asset_system/load.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/asset_system/mesh_tangent_generation.h>
#include <atlantis/assert.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

// Per-process tag: catch_discover_tests runs each TEST_CASE in its own
// process under ctest -j, and the counter below restarts at 0 in each.
const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_system_load_tests" /
              (label + "_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

// Cooks a real, valid triangle asset into dir, returning
// {artifactPath, metadataPath}.
[[nodiscard]] std::pair<std::string, std::string> cookValidTriangle(const fs::path& dir) {
  const fs::path sourcePath = dir / "triangle.mesh.txt";
  {
    std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
    source << kValidTriangleSource;
  }
  const fs::path artifactPath = dir / "triangle.amesh";
  const fs::path metadataPath = dir / "triangle.amesh.meta.txt";
  const auto result =
      cookStaticMesh(sourcePath.string(), "triangle.mesh.txt", artifactPath.string(), metadataPath.string());
  REQUIRE(result.isOk());
  return {artifactPath.string(), metadataPath.string()};
}

void writeFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

// Spec 0039/ADR-0087: the same authored triangle as above, written as a
// schema-5 (.amesh v5, uint32 index) artifact instead of a schema-4 one
// -- encodeMeshArtifactU32() takes the very same ParsedMeshSource and
// tangents the cooker feeds encodeMeshArtifact(), so the two artifacts
// differ in exactly the index width and the version field. The importer
// is production's only v5 producer; this test writes one directly rather
// than depending on the Tools subsystem.
[[nodiscard]] std::pair<std::string, std::string> writeValidTriangleV5(const fs::path& dir) {
  const auto parsed = parseMeshSource(std::string(kValidTriangleSource));
  REQUIRE(parsed.isOk());
  const auto tangents = generateTangents(parsed.value());
  REQUIRE(tangents.isOk());

  const auto logicalPath = normalizeLogicalPath("triangle_v5.mesh.txt");
  REQUIRE(logicalPath.isOk());
  const AssetId assetId = computeAssetId(logicalPath.value());

  const std::vector<std::byte> artifactBytes = encodeMeshArtifactU32(assetId, parsed.value(), tangents.value());

  AssetMetadata metadata;
  metadata.assetId = assetId;
  metadata.sourceLogicalPath = logicalPath.value();
  metadata.importerVersion = "test";
  metadata.assetType = "static_mesh";
  metadata.vertexCount = static_cast<std::uint32_t>(parsed.value().vertices.size());
  metadata.indexCount = static_cast<std::uint32_t>(parsed.value().indices.size());
  metadata.vertexStrideBytes = kMeshArtifactVertexStrideBytes;

  const fs::path artifactPath = dir / "triangle_v5.amesh";
  const fs::path metadataPath = dir / "triangle_v5.amesh.meta.txt";
  {
    std::ofstream out(artifactPath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(artifactBytes.data()),
              static_cast<std::streamsize>(artifactBytes.size()));
  }
  writeFile(metadataPath, serializeAssetMetadata(metadata));
  return {artifactPath.string(), metadataPath.string()};
}

[[nodiscard]] std::vector<char> readAllBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeAllBytes(const std::string& path, const std::vector<char>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Overwrites the little-endian uint32 at byteOffset -- the header fields
// mesh_artifact.h documents: schema_version at 8, vertex_count at 24.
void patchHeaderU32(std::vector<char>& bytes, std::size_t byteOffset, std::uint32_t value) {
  REQUIRE(bytes.size() >= byteOffset + 4);
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[byteOffset + i] = static_cast<char>((value >> (8 * i)) & 0xFFU);
  }
}

// Runs body with a recording assert handler installed, and returns the
// messages ATLANTIS_CHECK/ATLANTIS_CHECK_MSG reported. The default
// handler aborts; this one does not, which is what makes a precondition
// violation testable at all (assert.h's own setFailureHandler contract,
// the pattern tests/core/assert_tests.cpp established).
template <typename Body>
[[nodiscard]] std::vector<std::string> recordedAssertMessages(Body&& body) {
  std::vector<std::string> messages;
  auto previous = atlantis::assertions::setFailureHandler(
      [&messages](const atlantis::AssertFailureInfo& info) { messages.emplace_back(info.message); });
  body();
  atlantis::assertions::setFailureHandler(std::move(previous));
  return messages;
}

}  // namespace

TEST_CASE("loadStaticMeshAsset loads a well-formed artifact/metadata pair", "[asset_system]") {
  TempDirGuard dir("success");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().vertexCount() == 3);
  CHECK(result.value().indexCount() == 3);
  CHECK(result.value().vertexStrideBytes() == 60);
}

TEST_CASE("loadStaticMeshAsset fails when the artifact file does not exist", "[asset_system]") {
  TempDirGuard dir("missing_artifact");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  const auto result = loadStaticMeshAsset((dir.path / "does_not_exist.amesh").string(), metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::ArtifactFileUnreadable);
}

TEST_CASE("loadStaticMeshAsset fails when the metadata file does not exist", "[asset_system]") {
  TempDirGuard dir("missing_metadata");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  const auto result = loadStaticMeshAsset(artifactPath, (dir.path / "does_not_exist.meta.txt").string());
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataFileUnreadable);
}

TEST_CASE("loadStaticMeshAsset fails when the artifact fails to decode", "[asset_system]") {
  TempDirGuard dir("bad_artifact");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  writeFile(artifactPath, "not a valid artifact");

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::ArtifactDecodeFailed);
}

TEST_CASE("loadStaticMeshAsset fails when the metadata fails to parse", "[asset_system]") {
  TempDirGuard dir("bad_metadata");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  writeFile(metadataPath, "not valid metadata\n");

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataParseFailed);
}

TEST_CASE("loadStaticMeshAsset detects a deliberate artifact/metadata mismatch", "[asset_system]") {
  TempDirGuard dir("mismatch");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  // Cook a second, different asset and swap in its metadata -- same
  // valid format on both sides, but the recorded fields (asset_id,
  // vertex_count) now disagree with the artifact's own header.
  const fs::path otherSourcePath = dir.path / "other.mesh.txt";
  writeFile(otherSourcePath,
            "atlantis_static_mesh_source_version: 3\n"
            "vertex_count: 4\n"
            "index_count: 6\n"
            "vertex: 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
            "vertex: 1.0 0.0 0.0 0.0 0.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
            "vertex: 1.0 1.0 0.0 0.0 0.0 0.0 1.0 1.0 0.577350269 0.577350269 0.577350269\n"
            "vertex: 0.0 1.0 0.0 0.0 0.0 0.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
            "index: 0 1 2\n"
            "index: 2 3 0\n");
  const fs::path otherArtifactPath = dir.path / "other.amesh";
  const fs::path otherMetadataPath = dir.path / "other.amesh.meta.txt";
  const auto otherResult = cookStaticMesh(otherSourcePath.string(), "other.mesh.txt", otherArtifactPath.string(),
                                           otherMetadataPath.string());
  REQUIRE(otherResult.isOk());

  fs::copy_file(otherMetadataPath, metadataPath, fs::copy_options::overwrite_existing);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataArtifactMismatch);
}

TEST_CASE(
    "loadStaticMeshAsset detects a metadata file whose own recorded asset_id and source_logical_path disagree "
    "with each other, even when asset_id still matches the artifact",
    "[asset_system]") {
  TempDirGuard dir("self_inconsistent_metadata");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  // Individually well-formed and passes the artifact-vs-metadata check
  // above (asset_id/counts still agree with the artifact's own header)
  // -- but source_logical_path no longer hashes to that same asset_id,
  // an internal contradiction within the metadata file itself that the
  // artifact-vs-metadata check alone cannot see.
  std::string metadataText;
  {
    std::ifstream in(metadataPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    metadataText = buffer.str();
  }
  const std::string oldLine = "source_logical_path: triangle.mesh.txt";
  const std::string newLine = "source_logical_path: some/other/path.mesh.txt";
  const auto pos = metadataText.find(oldLine);
  REQUIRE(pos != std::string::npos);
  metadataText.replace(pos, oldLine.size(), newLine);
  writeFile(metadataPath, metadataText);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataArtifactMismatch);
}

// ---------------------------------------------------------------------
// Spec 0039 / ADR-0087: schema-version dispatch, the width-guarded
// accessors, and the drawable-index-range gate (ruling O1).
// ---------------------------------------------------------------------

TEST_CASE("loadStaticMeshAsset reports Uint16 and unchanged indices for a schema-4 artifact", "[asset_system]") {
  TempDirGuard dir("v4_index_type");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().indexType() == MeshIndexType::Uint16);
  CHECK(result.value().indices() == std::vector<std::uint16_t>{0, 1, 2});
  CHECK(result.value().indexCount() == 3);
  CHECK(result.value().vertexCount() == 3);
}

TEST_CASE("loadStaticMeshAsset loads a schema-5 artifact and reports Uint32", "[asset_system]") {
  TempDirGuard dir("v5_index_type");
  const auto [artifactPath, metadataPath] = writeValidTriangleV5(dir.path);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());
  CHECK(result.value().indexType() == MeshIndexType::Uint32);
  CHECK(result.value().indices32() == std::vector<std::uint32_t>{0, 1, 2});
  CHECK(result.value().indexCount() == 3);
  CHECK(result.value().vertexCount() == 3);
  CHECK(result.value().vertexStrideBytes() == kMeshArtifactVertexStrideBytes);
}

TEST_CASE("A schema-4 and a schema-5 encoding of one source differ only in index width", "[asset_system]") {
  TempDirGuard dir("v4_v5_same_vertices");
  const auto [v4Artifact, v4Metadata] = cookValidTriangle(dir.path);
  const auto [v5Artifact, v5Metadata] = writeValidTriangleV5(dir.path);

  const auto v4 = loadStaticMeshAsset(v4Artifact, v4Metadata);
  const auto v5 = loadStaticMeshAsset(v5Artifact, v5Metadata);
  REQUIRE(v4.isOk());
  REQUIRE(v5.isOk());

  // The claim Spec 0039's equivalence golden rests on, asserted here at
  // the byte level before any GPU is involved.
  CHECK(v4.value().vertexBytes() == v5.value().vertexBytes());
  CHECK(v4.value().vertexStrideBytes() == v5.value().vertexStrideBytes());
  REQUIRE(v4.value().indices().size() == v5.value().indices32().size());
  for (std::size_t i = 0; i < v4.value().indices().size(); ++i) {
    CHECK(static_cast<std::uint32_t>(v4.value().indices()[i]) == v5.value().indices32()[i]);
  }
}

TEST_CASE("loadStaticMeshAsset still detects a metadata mismatch on the schema-5 path", "[asset_system]") {
  TempDirGuard dir("v5_metadata_mismatch");
  const auto [artifactPath, metadataPath] = writeValidTriangleV5(dir.path);

  std::string metadataText;
  {
    std::ifstream in(metadataPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    metadataText = buffer.str();
  }
  const std::string oldLine = "index_count: 3";
  const auto pos = metadataText.find(oldLine);
  REQUIRE(pos != std::string::npos);
  metadataText.replace(pos, oldLine.size(), "index_count: 6");
  writeFile(metadataPath, metadataText);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::MetadataArtifactMismatch);
}

TEST_CASE("loadStaticMeshAsset rejects an unknown schema version as ArtifactDecodeFailed", "[asset_system]") {
  TempDirGuard dir("unknown_schema_version");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);

  // Version 6 is neither 4 nor 5, so the dispatch must fall through to
  // the schema-4 decoder, which rejects it exactly as it always has.
  std::vector<char> bytes = readAllBytes(artifactPath);
  patchHeaderU32(bytes, 8, 6);
  writeAllBytes(artifactPath, bytes);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::ArtifactDecodeFailed);
}

TEST_CASE("loadStaticMeshAsset rejects a schema-5 header whose vertex count exceeds the drawable index range",
          "[asset_system]") {
  TempDirGuard dir("drawable_range");
  const auto [artifactPath, metadataPath] = writeValidTriangleV5(dir.path);

  // The in-range artifact loads, so the gate does not fire on ordinary
  // content.
  REQUIRE(loadStaticMeshAsset(artifactPath, metadataPath).isOk());

  // Spec 0039 ruling O1 / Plan 0039 P2: the gate reads vertex_count out
  // of the 40-byte header and fires BEFORE the decode, which is what
  // makes it provable without materializing the ~1 GB artifact such a
  // vertex count really implies. The rest of the file stays a valid
  // 3-vertex triangle; a gate placed after the decode would report this
  // as a generic decode failure instead.
  std::vector<char> bytes = readAllBytes(artifactPath);
  patchHeaderU32(bytes, 24, kMaxDrawableVertexCount + 1U);
  writeAllBytes(artifactPath, bytes);

  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetLoadError::IndexValueExceedsDrawableRange);
}

TEST_CASE("The drawable-range bound admits a mesh of exactly kMaxDrawableVertexCount vertices", "[asset_system]") {
  // A mesh of exactly kMaxDrawableVertexCount vertices has a largest
  // possible index of kMaxDrawableIndexValue, which is precisely the
  // ceiling the Vulkan specification guarantees without
  // fullDrawIndexUint32 -- so it is legal, and the bound must not be
  // off by one against it (ADR-0086 Decision item 6).
  CHECK(kMaxDrawableIndexValue == 16777215U);
  CHECK(kMaxDrawableVertexCount == kMaxDrawableIndexValue + 1U);

  TempDirGuard dir("drawable_range_boundary");
  const auto [artifactPath, metadataPath] = writeValidTriangleV5(dir.path);
  std::vector<char> bytes = readAllBytes(artifactPath);
  patchHeaderU32(bytes, 24, kMaxDrawableVertexCount);
  writeAllBytes(artifactPath, bytes);

  // Passes the range gate, then fails later on the real content -- the
  // point being that it is NOT rejected as out of range.
  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isErr());
  CHECK(result.error() != AssetLoadError::IndexValueExceedsDrawableRange);
}

TEST_CASE("StaticMeshAssetData::indices() on a schema-5 asset trips its precondition", "[asset_system]") {
  TempDirGuard dir("v5_wrong_accessor");
  const auto [artifactPath, metadataPath] = writeValidTriangleV5(dir.path);
  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());

  const auto messages = recordedAssertMessages([&result] { (void)result.value().indices(); });
  REQUIRE(messages.size() == 1);
  CHECK(messages[0].find("indices32()") != std::string::npos);
}

TEST_CASE("StaticMeshAssetData::indices32() on a schema-4 asset trips its precondition", "[asset_system]") {
  TempDirGuard dir("v4_wrong_accessor");
  const auto [artifactPath, metadataPath] = cookValidTriangle(dir.path);
  const auto result = loadStaticMeshAsset(artifactPath, metadataPath);
  REQUIRE(result.isOk());

  const auto messages = recordedAssertMessages([&result] { (void)result.value().indices32(); });
  REQUIRE(messages.size() == 1);
  CHECK(messages[0].find("indices()") != std::string::npos);
}

TEST_CASE("peekMeshArtifactHeader reports each schema and rejects a malformed prefix", "[asset_system]") {
  TempDirGuard dir("peek");
  const auto [v4Artifact, v4Metadata] = cookValidTriangle(dir.path);
  const auto [v5Artifact, v5Metadata] = writeValidTriangleV5(dir.path);
  (void)v4Metadata;
  (void)v5Metadata;

  const auto asBytes = [](const std::vector<char>& raw) {
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) bytes[i] = static_cast<std::byte>(raw[i]);
    return bytes;
  };

  const auto v4 = peekMeshArtifactHeader(asBytes(readAllBytes(v4Artifact)));
  REQUIRE(v4.isOk());
  CHECK(v4.value().schemaVersion == kMeshArtifactSchemaVersion);
  CHECK(v4.value().vertexCount == 3);

  const auto v5 = peekMeshArtifactHeader(asBytes(readAllBytes(v5Artifact)));
  REQUIRE(v5.isOk());
  CHECK(v5.value().schemaVersion == kMeshArtifactSchemaVersionU32);
  CHECK(v5.value().vertexCount == 3);

  const auto tooSmall = peekMeshArtifactHeader(std::vector<std::byte>(8));
  REQUIRE(tooSmall.isErr());
  CHECK(tooSmall.error() == ArtifactDecodeError::TooSmallForHeader);

  std::vector<std::byte> badMagic = asBytes(readAllBytes(v4Artifact));
  badMagic[0] = static_cast<std::byte>('X');
  const auto rejected = peekMeshArtifactHeader(badMagic);
  REQUIRE(rejected.isErr());
  CHECK(rejected.error() == ArtifactDecodeError::BadMagic);
}
