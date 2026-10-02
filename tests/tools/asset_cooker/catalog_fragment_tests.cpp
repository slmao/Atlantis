// Plan 0047 M4 (P12; ADR-0098 D2): every cook writes a one-record catalog
// fragment beside its artifact -- absolute locations, the artifact's own
// schema, the P12 source schema and the GUIDs its source references.

#include <cook_command.h>
#include "test_catalog_source.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace atlantis::asset_system;
using atlantis::tools::asset_cooker::AssetKind;
using atlantis::tools::asset_cooker::CookArgumentSource;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::parseCookArguments;
using atlantis::tools::asset_cooker::runCookCommand;
using atlantis::tools::asset_cooker::test::testCatalogGuid;
using atlantis::tools::asset_cooker::test::writeCatalogSourceCovering;

namespace fs = std::filesystem;

namespace {

constexpr const char* kTriangle =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

// A scratch asset root holding one source of every hand-authored kind, the
// scene and material referencing the others by their catalog GUIDs.
class Scratch {
 public:
  Scratch() : root_(fs::temp_directory_path() / "atlantis_catalog_fragment_tests") {
    fs::remove_all(root_);
    write("assets/meshes/tri.mesh.txt", kTriangle);
    fs::create_directories(path("assets/textures"));
    fs::copy_file(fs::path(ATLANTIS_ASSET_COOKER_TEST_FIXTURES_DIR) / "tiny_rgba.png",
                  path("assets/textures/t.png"));
    write("assets/materials/m.material.txt",
          "atlantis_material_source_version: 10\n"
          "kind: unlit_textured\n"
          "texture: " + toString(testCatalogGuid("textures/t.png")) + "\n"
          "filter: linear\n"
          "address_mode: repeat\n");
    write("assets/scenes/s.scene.txt",
          "atlantis_scene_source_version: 7\n"
          "node_count: 2\n"
          "active_camera: none\n"
          "node: node_id=1 guid=00470047-0000-4000-8000-000000000001 parent=none position=0.0 0.0 0.0 "
          "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" + toString(testCatalogGuid("meshes/tri.mesh.txt")) +
              " material=" + toString(testCatalogGuid("materials/m.material.txt")) + "\n"
          "node: node_id=2 guid=00470047-0000-4000-8000-000000000002 parent=none position=1.0 0.0 0.0 "
          "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" + toString(testCatalogGuid("meshes/tri.mesh.txt")) +
              "\n");
    writeHdr("assets/environments/e.hdr");
    catalog_ = writeCatalogSourceCovering(path("assets"), path("asset_catalog.txt")).string();
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;

  [[nodiscard]] fs::path path(const std::string& relative) const { return root_ / relative; }

  [[nodiscard]] CookCommandRequest request(AssetKind kind, const std::string& source,
                                           const std::string& stampName = "") const {
    CookCommandRequest request;
    request.kind = kind;
    request.sourcePath = path("assets/" + source).string();
    request.assetRoot = path("assets").string();
    request.outputDir = path("out").string();
    if (!stampName.empty()) request.stampPath = path("out/" + stampName + ".stamp").string();
    request.catalogSourcePath = catalog_;
    return request;
  }

 private:
  void write(const std::string& relative, const std::string& text) const {
    fs::create_directories(path(relative).parent_path());
    std::ofstream(path(relative), std::ios::binary) << text;
  }

  // A flat (uncompressed) 4x2 Radiance HDR -- the smallest 2:1 source the
  // environment cook accepts.
  void writeHdr(const std::string& relative) const {
    std::string bytes = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    for (int i = 0; i < 8; ++i) bytes += std::string("\x80\x80\x80\x81", 4);
    write(relative, bytes);
  }

  fs::path root_;
  std::string catalog_;
};

[[nodiscard]] int runQuietly(const CookCommandRequest& request) {
  std::ostringstream captured;
  std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());
  const int exitCode = runCookCommand(request);
  std::cerr.rdbuf(previous);
  INFO(captured.str());
  return exitCode;
}

[[nodiscard]] AssetCatalogRecord onlyRecord(const fs::path& fragmentPath) {
  std::ifstream in(fragmentPath, std::ios::binary);
  REQUIRE(in.is_open());
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto records = parseAssetCatalogRecords(text);
  REQUIRE(records.isOk());
  REQUIRE(records.value().size() == 1);
  return records.value()[0];
}

[[nodiscard]] std::string absoluteLocation(const fs::path& path) {
  return fs::absolute(path).lexically_normal().generic_string();
}

[[nodiscard]] std::vector<AssetGuid> sorted(std::vector<AssetGuid> guids) {
  std::sort(guids.begin(), guids.end());
  return guids;
}

}  // namespace

TEST_CASE("Every cook writes a one-record fragment beside its artifact", "[asset_cooker][catalog]") {
  const Scratch scratch;

  REQUIRE(runQuietly(scratch.request(AssetKind::StaticMesh, "meshes/tri.mesh.txt")) == 0);
  const AssetCatalogRecord mesh = onlyRecord(scratch.path("out/meshes/tri.amesh.catalog.txt"));
  CHECK(mesh.guid == testCatalogGuid("meshes/tri.mesh.txt"));
  CHECK(mesh.assetId == assetKey(mesh.guid));
  CHECK(mesh.type == CatalogAssetType::Mesh);
  CHECK(toString(mesh.source) == "assets:meshes/tri.mesh.txt");
  CHECK(mesh.artifact == absoluteLocation(scratch.path("out/meshes/tri.amesh")));
  CHECK(mesh.metadata == absoluteLocation(scratch.path("out/meshes/tri.amesh.meta.txt")));
  CHECK(mesh.artifactSchema == 4);
  CHECK(mesh.sourceSchema == std::optional<std::uint32_t>(3));
  CHECK(mesh.tool == "atlantis-asset-cooker/1");
  CHECK(mesh.dependencies.empty());

  CookCommandRequest textureRequest = scratch.request(AssetKind::Texture, "textures/t.png", "t");
  textureRequest.colorSpace = "unorm";
  REQUIRE(runQuietly(textureRequest) == 0);
  const AssetCatalogRecord texture = onlyRecord(scratch.path("out/t.atex.catalog.txt"));
  CHECK(texture.type == CatalogAssetType::Texture);
  CHECK(toString(texture.source) == "assets:textures/t.png");
  CHECK(texture.artifact == absoluteLocation(scratch.path("out/t.atex")));
  CHECK(texture.artifactSchema == 3);
  CHECK_FALSE(texture.sourceSchema.has_value());
  CHECK(texture.dependencies.empty());

  REQUIRE(runQuietly(scratch.request(AssetKind::Material, "materials/m.material.txt")) == 0);
  const AssetCatalogRecord material = onlyRecord(scratch.path("out/materials/m.amaterial.catalog.txt"));
  CHECK(material.type == CatalogAssetType::Material);
  CHECK(material.artifactSchema == 9);
  CHECK(material.sourceSchema == std::optional<std::uint32_t>(10));
  CHECK(material.dependencies == std::vector<AssetGuid>{testCatalogGuid("textures/t.png")});

  REQUIRE(runQuietly(scratch.request(AssetKind::Scene, "scenes/s.scene.txt")) == 0);
  const AssetCatalogRecord scene = onlyRecord(scratch.path("out/scenes/s.ascene.catalog.txt"));
  CHECK(scene.type == CatalogAssetType::Scene);
  CHECK(scene.artifactSchema == 7);
  CHECK(scene.sourceSchema == std::optional<std::uint32_t>(7));
  // Both nodes name the mesh; it is listed once.
  CHECK(scene.dependencies ==
        sorted({testCatalogGuid("meshes/tri.mesh.txt"), testCatalogGuid("materials/m.material.txt")}));

  REQUIRE(runQuietly(scratch.request(AssetKind::Environment, "environments/e.hdr", "e")) == 0);
  const AssetCatalogRecord environment = onlyRecord(scratch.path("out/e.aenv.catalog.txt"));
  CHECK(environment.type == CatalogAssetType::Environment);
  CHECK(environment.artifact == absoluteLocation(scratch.path("out/e.aenv")));
  CHECK_FALSE(environment.sourceSchema.has_value());
}

TEST_CASE("A cook-manifest-line cook names the importer's --catalog-id as its source",
          "[asset_cooker][catalog]") {
  const Scratch scratch;
  CookCommandRequest request = scratch.request(AssetKind::StaticMesh, "meshes/tri.mesh.txt");
  request.catalogSourcePath.clear();
  request.guid = "0047aaaa-0000-4000-8000-000000000001";
  request.catalogId = "content:street/input.gltf#mesh/0/0";
  REQUIRE(runQuietly(request) == 0);
  const AssetCatalogRecord record = onlyRecord(scratch.path("out/meshes/tri.amesh.catalog.txt"));
  CHECK(toString(record.guid) == request.guid);
  CHECK(record.source.root == CatalogRoot::Content);
  CHECK(record.source.path == "street/input.gltf");
  CHECK(record.source.subKey == "mesh/0/0");
}

TEST_CASE("--catalog-id= is accepted only on a cook-manifest line, where it is required",
          "[asset_cooker][catalog]") {
  const std::vector<std::string> base = {"--kind=mesh", "--source=a.mesh.txt", "--asset-root=r", "--output-dir=o"};
  const auto parses = [&base](std::vector<std::string> extra, CookArgumentSource source) {
    std::vector<std::string> args = base;
    args.insert(args.end(), extra.begin(), extra.end());
    CookCommandRequest request;
    std::ostringstream err;
    return parseCookArguments(args, request, err, source);
  };
  const std::string guid = "--guid=0047aaaa-0000-4000-8000-000000000001";
  const std::string id = "--catalog-id=content:street/input.gltf#mesh/0/0";

  CHECK(parses({guid, id}, CookArgumentSource::CookManifestLine));
  CHECK_FALSE(parses({guid}, CookArgumentSource::CookManifestLine));
  CHECK_FALSE(parses({guid, "--catalog-id=street/input.gltf"}, CookArgumentSource::CookManifestLine));
  CHECK_FALSE(parses({guid, "--catalog-id=content:street/input.gltf#"}, CookArgumentSource::CookManifestLine));
  CHECK_FALSE(parses({"--catalog-source=c.txt", id}, CookArgumentSource::CommandLine));
  CHECK_FALSE(parses({"--kind=lookup", "--catalog-source=c.txt", "--source=assets:a", id},
                     CookArgumentSource::CommandLine));
}
