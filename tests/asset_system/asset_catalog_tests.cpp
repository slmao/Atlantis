#include <atlantis/asset_system/asset_catalog.h>

#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/material_metadata.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_metadata.h>
#include <atlantis/asset_system/texture_metadata.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace atlantis::asset_system;

namespace {

namespace fs = std::filesystem;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_asset_catalog_tests" /
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

[[nodiscard]] AssetGuid guidOf(std::string_view key) {
  return deriveAssetGuid(parseAssetGuid("00470047-0047-4047-8047-004700470047").value(), key);
}

void writeText(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

[[nodiscard]] std::string sidecarText(CatalogAssetType type, const AssetGuid& guid) {
  switch (type) {
    case CatalogAssetType::Mesh: {
      AssetMetadata metadata;
      metadata.assetGuid = guid;
      metadata.assetId = assetKey(guid);
      metadata.sourceLogicalPath = "meshes/m.mesh.txt";
      metadata.importerVersion = "test/1";
      metadata.assetType = "static_mesh";
      metadata.vertexCount = 3;
      metadata.indexCount = 3;
      metadata.vertexStrideBytes = kMeshArtifactVertexStrideBytes;
      return serializeAssetMetadata(metadata);
    }
    case CatalogAssetType::Texture: {
      TextureMetadata metadata;
      metadata.assetGuid = guid;
      metadata.assetId = assetKey(guid);
      metadata.sourceLogicalPath = "textures/t.png";
      metadata.width = 4;
      metadata.height = 4;
      metadata.channelsInFile = 4;
      return serializeTextureMetadata(metadata);
    }
    case CatalogAssetType::Material: {
      MaterialMetadata metadata;
      metadata.assetGuid = guid;
      metadata.assetId = assetKey(guid);
      metadata.sourceLogicalPath = "materials/m.material.txt";
      metadata.textureAsset = 1;
      return serializeMaterialMetadata(metadata);
    }
    case CatalogAssetType::Scene: {
      SceneMetadata metadata;
      metadata.assetGuid = guid;
      metadata.schemaVersion = kSceneArtifactSchemaVersion;
      metadata.nodeCount = 1;
      return serializeSceneMetadata(metadata);
    }
    case CatalogAssetType::Environment:
    case CatalogAssetType::GltfImport:
      break;
  }
  return {};
}

// A record for assets:<key> whose artifact and real sidecar sit under
// `dir`; its sidecar is written here, carrying the record's own GUID.
[[nodiscard]] AssetCatalogRecord makeRecord(const fs::path& dir, CatalogAssetType type, const std::string& key,
                                            std::vector<AssetGuid> dependencies = {}) {
  AssetCatalogRecord record;
  record.guid = guidOf(key);
  record.assetId = assetKey(record.guid);
  record.type = type;
  record.source = CatalogSourceId{CatalogRoot::Assets, key, ""};
  record.artifact = (dir / (key + ".bin")).generic_string();
  record.metadata = (dir / (key + ".meta.txt")).generic_string();
  record.artifactSchema = 1;
  record.tool = "test/1";
  record.dependencies = std::move(dependencies);
  writeText(record.metadata, sidecarText(type, record.guid));
  return record;
}

[[nodiscard]] std::string writeFragment(const fs::path& path, std::vector<AssetCatalogRecord> records) {
  writeText(path, serializeAssetCatalog(std::move(records)));
  return path.string();
}

[[nodiscard]] AssetCatalogSource catalogSourceOf(const std::vector<CatalogSourceEntry>& entries) {
  auto parsed = parseAssetCatalogSource(serializeAssetCatalogSource(entries));
  REQUIRE(parsed.isOk());
  return std::move(parsed.value());
}

// A small valid build: scene -> {mesh, material}, material -> texture, each
// declared and in the catalog source, one fragment per record.
struct Build {
  fs::path dir;
  std::vector<AssetCatalogRecord> records;
  std::vector<CatalogSourceEntry> sourceEntries;
  std::vector<AssetDeclaration> declarations;

  explicit Build(const fs::path& root) : dir(root) {
    const AssetCatalogRecord texture = makeRecord(dir, CatalogAssetType::Texture, "t");
    const AssetCatalogRecord material = makeRecord(dir, CatalogAssetType::Material, "mat", {texture.guid});
    const AssetCatalogRecord mesh = makeRecord(dir, CatalogAssetType::Mesh, "mesh");
    const AssetCatalogRecord scene =
        makeRecord(dir, CatalogAssetType::Scene, "scene", {mesh.guid, material.guid});
    records = {texture, material, mesh, scene};
    for (const AssetCatalogRecord& record : records) {
      sourceEntries.push_back(CatalogSourceEntry{record.guid, record.type, CatalogRoot::Assets, record.source.path});
      declarations.push_back(AssetDeclaration{record.type, CatalogRoot::Assets, record.source.path});
    }
  }

  [[nodiscard]] AssetCatalogRecord& byKey(const std::string& key) {
    for (AssetCatalogRecord& record : records) {
      if (record.source.path == key) return record;
    }
    FAIL("no record " << key);
    return records.front();
  }

  // Writes one fragment per record and assembles with `source`.
  [[nodiscard]] atlantis::Result<AssembledAssetCatalog, AssetCatalogAssemblyFailure> assemble(
      const AssetCatalogSource& source, std::vector<AssetCatalogClosureRequest> closures = {}) {
    AssetCatalogAssemblyRequest request;
    request.catalogSource = &source;
    request.declarations = declarations;
    for (const AssetCatalogRecord& record : records) {
      request.fragmentPaths.push_back(
          writeFragment(dir / "fragments" / (toString(record.guid) + ".catalog.txt"), {record}));
    }
    request.outPath = (dir / "asset_catalog.txt").string();
    request.closures = std::move(closures);
    return assembleAssetCatalog(request);
  }

  [[nodiscard]] atlantis::Result<AssembledAssetCatalog, AssetCatalogAssemblyFailure> assemble() {
    const AssetCatalogSource source = catalogSourceOf(sourceEntries);
    return assemble(source);
  }
};

void requireFailure(const atlantis::Result<AssembledAssetCatalog, AssetCatalogAssemblyFailure>& result,
                    AssetCatalogAssemblyError expected) {
  REQUIRE(result.isErr());
  INFO("subject: " << result.error().subject);
  CHECK(toString(result.error().error) == toString(expected));
  CHECK_FALSE(result.error().subject.empty());
}

}  // namespace

TEST_CASE("A catalog record round-trips through its text form", "[asset_system][asset_catalog]") {
  AssetCatalogRecord record;
  record.guid = guidOf("scene");
  record.assetId = assetKey(record.guid);
  record.type = CatalogAssetType::Scene;
  record.source = CatalogSourceId{CatalogRoot::Content, "bistro/bistro.gltf", "scene"};
  record.artifact = "C:/build dir/bistro/cooked/bistro.ascene";
  record.metadata = "C:/build dir/bistro/cooked/bistro.ascene.meta.txt";
  record.artifactSchema = 7;
  record.sourceSchema = 7;
  record.tool = "atlantis-asset-cooker/1";
  record.dependencies = {guidOf("b"), guidOf("a"), guidOf("b")};

  const std::string text = serializeAssetCatalog({record});
  const auto parsed = parseAssetCatalogRecords(text);
  REQUIRE(parsed.isOk());
  REQUIRE(parsed.value().size() == 1);
  const AssetCatalogRecord& back = parsed.value()[0];
  CHECK(back.guid == record.guid);
  CHECK(back.assetId == record.assetId);
  CHECK(back.type == CatalogAssetType::Scene);
  CHECK(toString(back.source) == "content:bistro/bistro.gltf#scene");
  CHECK(back.artifact == record.artifact);
  CHECK(back.metadata == record.metadata);
  CHECK(back.artifactSchema == 7);
  CHECK(back.sourceSchema == std::optional<std::uint32_t>(7));
  CHECK(back.tool == record.tool);
  // Serialization sorts and de-duplicates the dependencies.
  std::vector<AssetGuid> expected = {guidOf("a"), guidOf("b")};
  std::sort(expected.begin(), expected.end());
  CHECK(back.dependencies == expected);
  CHECK(serializeAssetCatalog(parsed.value()) == text);
}

TEST_CASE("A record with no source schema and no dependencies writes none and deps=0",
          "[asset_system][asset_catalog]") {
  AssetCatalogRecord record;
  record.guid = guidOf("t");
  record.assetId = assetKey(record.guid);
  record.type = CatalogAssetType::Texture;
  record.source = CatalogSourceId{CatalogRoot::Assets, "textures/t.png", ""};
  record.artifact = "assets/t.atex";
  record.metadata = "assets/t.atex.meta.txt";
  record.artifactSchema = 3;
  record.tool = "atlantis-asset-cooker/1";
  CHECK(formatAssetCatalogRecord(record) ==
        "record: guid=" + toString(record.guid) + " asset_id=" + toHexString(record.assetId) +
            " type=texture source=assets:textures/t.png artifact=assets/t.atex metadata=assets/t.atex.meta.txt "
            "artifact_schema=3 source_schema=none tool=atlantis-asset-cooker/1 deps=0");
}

TEST_CASE("serializeAssetCatalog orders records by GUID", "[asset_system][asset_catalog]") {
  TempDirGuard dir("order");
  const Build build(dir.path);
  const auto parsed = parseAssetCatalogRecords(serializeAssetCatalog(build.records));
  REQUIRE(parsed.isOk());
  REQUIRE(parsed.value().size() == 4);
  for (std::size_t i = 1; i < parsed.value().size(); ++i) CHECK(parsed.value()[i - 1].guid < parsed.value()[i].guid);
}

TEST_CASE("parseAssetCatalogRecords rejects a malformed catalog", "[asset_system][asset_catalog]") {
  AssetCatalogRecord record;
  record.guid = guidOf("t");
  record.assetId = assetKey(record.guid);
  record.type = CatalogAssetType::Texture;
  record.source = CatalogSourceId{CatalogRoot::Assets, "textures/t.png", ""};
  record.artifact = "t.atex";
  record.metadata = "t.atex.meta.txt";
  record.artifactSchema = 3;
  record.tool = "x/1";
  const std::string line = formatAssetCatalogRecord(record);
  const auto withLine = [](const std::string& recordLine) {
    return "atlantis_asset_catalog_version: 1\nrecord_count: 1\n" + recordLine + "\n";
  };
  const auto replaced = [&line](std::string_view from, std::string_view to) {
    std::string text = line;
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
  };

  CHECK(parseAssetCatalogRecords(withLine(line)).isOk());
  CHECK(parseAssetCatalogRecords("atlantis_asset_catalog_version: 2\nrecord_count: 0\n").error() ==
        AssetCatalogParseError::UnknownVersion);
  CHECK(parseAssetCatalogRecords("atlantis_asset_catalog_version: 1\nrecord_count: 2\n" + line + "\n").error() ==
        AssetCatalogParseError::RecordCountMismatch);
  CHECK(parseAssetCatalogRecords(withLine(replaced("type=texture", "type=gltf_import"))).error() ==
        AssetCatalogParseError::MalformedRecord);
  CHECK(parseAssetCatalogRecords(withLine(replaced(" asset_id=" + toHexString(record.assetId), " asset_id=12"))).error() ==
        AssetCatalogParseError::MalformedRecord);
  CHECK(parseAssetCatalogRecords(withLine(replaced("assets:textures/t.png", "assets:textures\\t.png"))).error() ==
        AssetCatalogParseError::MalformedRecord);
  CHECK(parseAssetCatalogRecords(withLine(replaced("textures/t.png", "textures/t.png#"))).error() ==
        AssetCatalogParseError::MalformedRecord);
  CHECK(parseAssetCatalogRecords(withLine(replaced("deps=0", "deps=1"))).error() ==
        AssetCatalogParseError::MalformedRecord);
  CHECK(parseAssetCatalogRecords(withLine(replaced(" tool=x/1", " tool="))).error() ==
        AssetCatalogParseError::MalformedRecord);

  const AssetGuid lowDep = std::min(guidOf("a"), guidOf("b"));
  const AssetGuid highDep = std::max(guidOf("a"), guidOf("b"));
  CHECK(parseAssetCatalogRecords(
            withLine(replaced("deps=0", "deps=2 " + toString(lowDep) + " " + toString(highDep))))
            .isOk());
  CHECK(parseAssetCatalogRecords(
            withLine(replaced("deps=0", "deps=2 " + toString(highDep) + " " + toString(lowDep))))
            .error() == AssetCatalogParseError::Unsorted);
  CHECK(parseAssetCatalogRecords(
            withLine(replaced("deps=0", "deps=2 " + toString(lowDep) + " " + toString(lowDep))))
            .error() == AssetCatalogParseError::Unsorted);

  AssetCatalogRecord other = record;
  other.guid = guidOf("u");
  other.assetId = assetKey(other.guid);
  const AssetCatalogRecord& low = record.guid < other.guid ? record : other;
  const AssetCatalogRecord& high = record.guid < other.guid ? other : record;
  CHECK(parseAssetCatalogRecords("atlantis_asset_catalog_version: 1\nrecord_count: 2\n" +
                                 formatAssetCatalogRecord(high) + "\n" + formatAssetCatalogRecord(low) + "\n")
            .error() == AssetCatalogParseError::Unsorted);
}

TEST_CASE("parseAssetDeclarations reads type, root and path per line", "[asset_system][asset_catalog]") {
  const auto declarations = parseAssetDeclarations("mesh\tassets\tmeshes/a.mesh.txt\r\n\ngltf_import\tcontent\tb/b.gltf\n");
  REQUIRE(declarations.has_value());
  REQUIRE(declarations->size() == 2);
  CHECK((*declarations)[0].type == CatalogAssetType::Mesh);
  CHECK((*declarations)[1].root == CatalogRoot::Content);
  CHECK((*declarations)[1].path == "b/b.gltf");
  CHECK_FALSE(parseAssetDeclarations("mesh\tassets\n").has_value());
  CHECK_FALSE(parseAssetDeclarations("blob\tassets\tx\n").has_value());
}

TEST_CASE("Assembly merges fragments into one GUID-ordered catalog with relative locations",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("assemble");
  Build build(dir.path);
  const auto result = build.assemble();
  REQUIRE(result.isOk());
  CHECK(result.value().recordCount == 4);
  CHECK(result.value().undeclaredSourceEntries == 0);

  const auto parsed = parseAssetCatalogRecords(result.value().catalogText);
  REQUIRE(parsed.isOk());
  REQUIRE(parsed.value().size() == 4);
  for (const AssetCatalogRecord& record : parsed.value()) {
    INFO(record.artifact);
    CHECK(record.artifact == record.source.path + ".bin");
    CHECK(record.metadata == record.source.path + ".meta.txt");
  }
  CHECK(result.value().catalogText == serializeAssetCatalog(parsed.value()));
}

TEST_CASE("A catalog-source entry no declaration names is counted, not an error",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("content_gated");
  Build build(dir.path);
  std::vector<CatalogSourceEntry> entries = build.sourceEntries;
  entries.push_back(CatalogSourceEntry{guidOf("bistro"), CatalogAssetType::GltfImport, CatalogRoot::Content,
                                       "bistro/bistro.gltf"});
  const AssetCatalogSource source = catalogSourceOf(entries);
  const auto result = build.assemble(source);
  REQUIRE(result.isOk());
  CHECK(result.value().undeclaredSourceEntries == 1);
}

TEST_CASE("A closure catalog holds exactly the scene's transitive set, located from its own directory",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("closure");
  Build build(dir.path / "data");
  build.records.push_back(makeRecord(build.dir, CatalogAssetType::Texture, "unrelated"));
  build.sourceEntries.push_back(
      CatalogSourceEntry{build.records.back().guid, CatalogAssetType::Texture, CatalogRoot::Assets, "unrelated"});
  build.declarations.push_back(AssetDeclaration{CatalogAssetType::Texture, CatalogRoot::Assets, "unrelated"});
  const AssetCatalogSource source = catalogSourceOf(build.sourceEntries);
  const AssetGuid scene = build.byKey("scene").guid;

  const auto result =
      build.assemble(source, {AssetCatalogClosureRequest{scene, (dir.path / "closure.catalog.txt").string()}});
  REQUIRE(result.isOk());
  REQUIRE(result.value().closureTexts.size() == 1);
  const auto closure = parseAssetCatalogRecords(result.value().closureTexts[0]);
  REQUIRE(closure.isOk());
  std::vector<std::string> keys;
  for (const AssetCatalogRecord& record : closure.value()) {
    keys.push_back(record.source.path);
    CHECK(record.artifact == "data/" + record.source.path + ".bin");
  }
  std::sort(keys.begin(), keys.end());
  CHECK(keys == std::vector<std::string>{"mat", "mesh", "scene", "t"});
  CHECK(result.value().recordCount == 5);
}

TEST_CASE("Assembly fails with FragmentUnreadable for a missing fragment", "[asset_system][asset_catalog]") {
  TempDirGuard dir("fragment_unreadable");
  Build build(dir.path);
  const AssetCatalogSource source = catalogSourceOf(build.sourceEntries);
  AssetCatalogAssemblyRequest request;
  request.catalogSource = &source;
  request.fragmentPaths = {(dir.path / "missing.catalog.txt").string()};
  request.outPath = (dir.path / "asset_catalog.txt").string();
  requireFailure(assembleAssetCatalog(request), AssetCatalogAssemblyError::FragmentUnreadable);
}

TEST_CASE("Assembly fails with MalformedFragment for unparseable text or a relative location",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("malformed_fragment");
  Build build(dir.path);
  const AssetCatalogSource source = catalogSourceOf(build.sourceEntries);
  AssetCatalogAssemblyRequest request;
  request.catalogSource = &source;
  request.outPath = (dir.path / "asset_catalog.txt").string();

  writeText(dir.path / "bad.catalog.txt", "not a catalog\n");
  request.fragmentPaths = {(dir.path / "bad.catalog.txt").string()};
  requireFailure(assembleAssetCatalog(request), AssetCatalogAssemblyError::MalformedFragment);

  AssetCatalogRecord relative = build.byKey("t");
  relative.artifact = "t.bin";
  request.fragmentPaths = {writeFragment(dir.path / "relative.catalog.txt", {relative})};
  requireFailure(assembleAssetCatalog(request), AssetCatalogAssemblyError::MalformedFragment);
}

TEST_CASE("Assembly fails with DuplicateGuid when two fragments carry one GUID", "[asset_system][asset_catalog]") {
  TempDirGuard dir("duplicate_guid");
  Build build(dir.path);
  build.records.push_back(build.byKey("mesh"));
  requireFailure(build.assemble(), AssetCatalogAssemblyError::DuplicateGuid);
}

TEST_CASE("The key check reports DuplicateAssetId for an injected shared key", "[asset_system][asset_catalog]") {
  const std::vector<detail::AssetKeyEntry> entries = {{7, "a"}, {8, "b"}, {7, "c"}};
  const auto result = detail::checkAssetKeys(entries);
  REQUIRE(result.isErr());
  CHECK(result.error().error == AssetCatalogAssemblyError::DuplicateAssetId);
  CHECK(result.error().subject == "a and c");
  const std::vector<detail::AssetKeyEntry> distinct = {{7, "a"}, {8, "b"}};
  CHECK(detail::checkAssetKeys(distinct).isOk());
}

TEST_CASE("The key check reports ZeroAssetId for an injected zero key", "[asset_system][asset_catalog]") {
  const std::vector<detail::AssetKeyEntry> entries = {{7, "a"}, {0, "b"}};
  const auto result = detail::checkAssetKeys(entries);
  REQUIRE(result.isErr());
  CHECK(result.error().error == AssetCatalogAssemblyError::ZeroAssetId);
  CHECK(result.error().subject == "b");
}

TEST_CASE("Assembly fails with AssetIdMismatch when asset_id is not the GUID's key", "[asset_system][asset_catalog]") {
  TempDirGuard dir("asset_id_mismatch");
  Build build(dir.path);
  build.byKey("mesh").assetId ^= 1;
  requireFailure(build.assemble(), AssetCatalogAssemblyError::AssetIdMismatch);
}

TEST_CASE("Assembly fails with DanglingDependency for a dependency with no record", "[asset_system][asset_catalog]") {
  TempDirGuard dir("dangling");
  Build build(dir.path);
  build.byKey("scene").dependencies.push_back(guidOf("nowhere"));
  requireFailure(build.assemble(), AssetCatalogAssemblyError::DanglingDependency);
}

TEST_CASE("Assembly fails with DependencyTypeMismatch outside scene->mesh/material and material->texture",
          "[asset_system][asset_catalog]") {
  {
    TempDirGuard dir("dependency_type_scene");
    Build build(dir.path);
    build.byKey("scene").dependencies.push_back(build.byKey("t").guid);
    requireFailure(build.assemble(), AssetCatalogAssemblyError::DependencyTypeMismatch);
  }
  {
    TempDirGuard dir("dependency_type_mesh");
    Build build(dir.path);
    build.byKey("mesh").dependencies.push_back(build.byKey("t").guid);
    requireFailure(build.assemble(), AssetCatalogAssemblyError::DependencyTypeMismatch);
  }
}

TEST_CASE("Assembly fails with DeclarationNotInCatalog for a declaration without a source entry",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("declaration_missing");
  Build build(dir.path);
  build.declarations.push_back(AssetDeclaration{CatalogAssetType::Mesh, CatalogRoot::Assets, "meshes/new.mesh.txt"});
  requireFailure(build.assemble(), AssetCatalogAssemblyError::DeclarationNotInCatalog);
}

TEST_CASE("Assembly fails with DeclarationTypeMismatch when a declaration's type differs from its entry",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("declaration_type");
  Build build(dir.path);
  build.declarations[0].type = CatalogAssetType::Environment;
  requireFailure(build.assemble(), AssetCatalogAssemblyError::DeclarationTypeMismatch);
}

TEST_CASE("Assembly fails with SidecarGuidMismatch when the sidecar records another GUID",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("sidecar_guid");
  Build build(dir.path);
  const AssetCatalogRecord& mesh = build.byKey("mesh");
  writeText(mesh.metadata, sidecarText(CatalogAssetType::Mesh, guidOf("someone else")));
  requireFailure(build.assemble(), AssetCatalogAssemblyError::SidecarGuidMismatch);
}

TEST_CASE("Assembly fails with LocationEscapesCatalog for a location outside the catalog's directory",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("escapes");
  Build build(dir.path / "inner");
  AssetCatalogRecord& texture = build.byKey("t");
  const fs::path outside = dir.path / "t.outside.bin";
  texture.artifact = outside.generic_string();
  requireFailure(build.assemble(), AssetCatalogAssemblyError::LocationEscapesCatalog);
}

TEST_CASE("Assembly fails with UnknownClosureScene for a closure GUID that is not a scene record",
          "[asset_system][asset_catalog]") {
  TempDirGuard dir("closure_unknown");
  Build build(dir.path);
  const AssetCatalogSource source = catalogSourceOf(build.sourceEntries);
  const std::string out = (dir.path / "closure.catalog.txt").string();
  requireFailure(build.assemble(source, {AssetCatalogClosureRequest{guidOf("absent"), out}}),
                 AssetCatalogAssemblyError::UnknownClosureScene);
  requireFailure(build.assemble(source, {AssetCatalogClosureRequest{build.byKey("mat").guid, out}}),
                 AssetCatalogAssemblyError::UnknownClosureScene);
}
