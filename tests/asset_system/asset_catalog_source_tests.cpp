// Plan 0047 M2 (P6; ADR-0098 D1): the catalog source grammar, every parse
// error, lookup, and serialization.

#include <catch2/catch_test_macros.hpp>

#include <atlantis/asset_system/asset_catalog_source.h>

#include <string>
#include <vector>

using atlantis::asset_system::AssetCatalogSource;
using atlantis::asset_system::CatalogAssetType;
using atlantis::asset_system::CatalogRoot;
using atlantis::asset_system::CatalogSourceEntry;
using atlantis::asset_system::CatalogSourceParseError;
using atlantis::asset_system::parseAssetCatalogSource;
using atlantis::asset_system::parseAssetGuid;
using atlantis::asset_system::serializeAssetCatalogSource;

namespace {

constexpr const char* kGuidA = "01234567-89ab-4def-8123-456789abcdef";
constexpr const char* kGuidB = "11234567-89ab-4def-8123-456789abcdef";
constexpr const char* kGuidC = "21234567-89ab-4def-8123-456789abcdef";

[[nodiscard]] std::string entry(const char* guid, const char* type, const char* root, const char* path) {
  return std::string("asset: guid=") + guid + " type=" + type + " root=" + root + " path=" + path;
}

[[nodiscard]] std::string source(const std::vector<std::string>& entries) {
  std::string text = "atlantis_asset_catalog_source_version: 1\nentry_count: " + std::to_string(entries.size()) + "\n";
  for (const std::string& line : entries) text += line + "\n";
  return text;
}

[[nodiscard]] CatalogSourceParseError errorOf(const std::string& text) {
  const auto parsed = parseAssetCatalogSource(text);
  REQUIRE(parsed.isErr());
  return parsed.error();
}

const std::vector<std::string> kValidEntries = {
    entry(kGuidA, "mesh", "assets", "meshes/minimal_cube.mesh.txt"),
    entry(kGuidB, "material", "assets", "materials/red.material.txt"),
    entry(kGuidC, "gltf_import", "content", "bistro/bistro.gltf"),
};

}  // namespace

TEST_CASE("A valid catalog source parses with every field", "[asset_catalog_source]") {
  // Catalog order is by root token, then path: "materials/…" < "meshes/…".
  const auto parsed = parseAssetCatalogSource(source({kValidEntries[1], kValidEntries[0], kValidEntries[2]}));
  REQUIRE(parsed.isOk());
  const std::vector<CatalogSourceEntry>& entries = parsed.value().entries();
  REQUIRE(entries.size() == 3);
  CHECK(entries[0].type == CatalogAssetType::Material);
  CHECK(entries[0].path == "materials/red.material.txt");
  CHECK(entries[1].guid == parseAssetGuid(kGuidA).value());
  CHECK(entries[1].type == CatalogAssetType::Mesh);
  CHECK(entries[2].type == CatalogAssetType::GltfImport);
  CHECK(entries[2].root == CatalogRoot::Content);
}

TEST_CASE("An empty catalog source is valid", "[asset_catalog_source]") {
  const auto parsed = parseAssetCatalogSource(source({}));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().entries().empty());
}

TEST_CASE("A CRLF catalog source parses like its LF form", "[asset_catalog_source]") {
  std::string crlf;
  for (char c : source({kValidEntries[1], kValidEntries[0]})) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  const auto parsed = parseAssetCatalogSource(crlf);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().entries().size() == 2);
}

TEST_CASE("Lookup finds entries by root and path and by GUID", "[asset_catalog_source]") {
  const auto parsed = parseAssetCatalogSource(source({kValidEntries[1], kValidEntries[0], kValidEntries[2]}));
  REQUIRE(parsed.isOk());
  const AssetCatalogSource& catalog = parsed.value();

  const CatalogSourceEntry* byPath = catalog.find(CatalogRoot::Assets, "meshes/minimal_cube.mesh.txt");
  REQUIRE(byPath != nullptr);
  CHECK(byPath->guid == parseAssetGuid(kGuidA).value());

  const CatalogSourceEntry* byGuid = catalog.find(parseAssetGuid(kGuidC).value());
  REQUIRE(byGuid != nullptr);
  CHECK(byGuid->path == "bistro/bistro.gltf");

  CHECK(catalog.find(CatalogRoot::Content, "meshes/minimal_cube.mesh.txt") == nullptr);
  CHECK(catalog.find(CatalogRoot::Assets, "meshes/missing.mesh.txt") == nullptr);
  CHECK(catalog.find(parseAssetGuid("31234567-89ab-4def-8123-456789abcdef").value()) == nullptr);
}

TEST_CASE("Serialization sorts into catalog order and round-trips", "[asset_catalog_source]") {
  const auto parsed = parseAssetCatalogSource(source({kValidEntries[1], kValidEntries[0], kValidEntries[2]}));
  REQUIRE(parsed.isOk());
  std::vector<CatalogSourceEntry> shuffled = {parsed.value().entries()[2], parsed.value().entries()[0],
                                              parsed.value().entries()[1]};
  const std::string text = serializeAssetCatalogSource(shuffled);
  CHECK(text == source({kValidEntries[1], kValidEntries[0], kValidEntries[2]}));
  CHECK(parseAssetCatalogSource(text).isOk());
}

TEST_CASE("Each catalog source defect reports its own error", "[asset_catalog_source]") {
  CHECK(errorOf("") == CatalogSourceParseError::UnknownVersion);
  CHECK(errorOf("atlantis_asset_catalog_source_version: 2\nentry_count: 0\n") ==
        CatalogSourceParseError::UnknownVersion);

  CHECK(errorOf("atlantis_asset_catalog_source_version: 1\n") == CatalogSourceParseError::EntryCountMismatch);
  CHECK(errorOf("atlantis_asset_catalog_source_version: 1\nentry_count: x\n") ==
        CatalogSourceParseError::EntryCountMismatch);
  CHECK(errorOf("atlantis_asset_catalog_source_version: 1\nentry_count: 2\n" + kValidEntries[0] + "\n") ==
        CatalogSourceParseError::EntryCountMismatch);

  CHECK(errorOf(source({"asset: type=mesh guid=" + std::string(kGuidA) + " root=assets path=a.mesh.txt"})) ==
        CatalogSourceParseError::MalformedEntry);
  CHECK(errorOf(source({entry(kGuidA, "mesh", "assets", "a.mesh.txt") + " extra=1"})) ==
        CatalogSourceParseError::MalformedEntry);
  CHECK(errorOf(source({entry("not-a-guid", "mesh", "assets", "a.mesh.txt")})) ==
        CatalogSourceParseError::MalformedEntry);
  CHECK(errorOf(source({""})) == CatalogSourceParseError::MalformedEntry);

  CHECK(errorOf(source({entry("00000000-0000-0000-0000-000000000000", "mesh", "assets", "a.mesh.txt")})) ==
        CatalogSourceParseError::NilGuid);
  CHECK(errorOf(source({entry(kGuidA, "shader", "assets", "a.slang")})) == CatalogSourceParseError::UnknownType);
  CHECK(errorOf(source({entry(kGuidA, "mesh", "build", "a.mesh.txt")})) == CatalogSourceParseError::UnknownRoot);
  CHECK(errorOf(source({entry(kGuidA, "mesh", "assets", "meshes/../a.mesh.txt")})) ==
        CatalogSourceParseError::NonNormalPath);
  CHECK(errorOf(source({entry(kGuidA, "mesh", "assets", "meshes\\a.mesh.txt")})) ==
        CatalogSourceParseError::NonNormalPath);
  CHECK(errorOf(source({entry(kGuidA, "mesh", "assets", "/a.mesh.txt")})) == CatalogSourceParseError::NonNormalPath);

  CHECK(errorOf(source({kValidEntries[0], kValidEntries[1]})) == CatalogSourceParseError::Unsorted);
  CHECK(errorOf(source({kValidEntries[2], kValidEntries[0]})) == CatalogSourceParseError::Unsorted);
  CHECK(errorOf(source({entry(kGuidA, "mesh", "assets", "a.mesh.txt"), entry(kGuidB, "mesh", "assets", "b.mesh.txt"),
                        entry(kGuidA, "mesh", "assets", "c.mesh.txt")})) == CatalogSourceParseError::DuplicateGuid);
  CHECK(errorOf(source({entry(kGuidA, "mesh", "assets", "a.mesh.txt"),
                        entry(kGuidB, "texture", "assets", "a.mesh.txt")})) == CatalogSourceParseError::DuplicatePath);
}

TEST_CASE("The same path under different roots is two entries", "[asset_catalog_source]") {
  const auto parsed = parseAssetCatalogSource(source(
      {entry(kGuidA, "texture", "assets", "bistro/x.dds"), entry(kGuidB, "texture", "content", "bistro/x.dds")}));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().find(CatalogRoot::Content, "bistro/x.dds")->guid == parseAssetGuid(kGuidB).value());
}
