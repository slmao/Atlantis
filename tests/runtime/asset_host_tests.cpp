#include "../../src/runtime/asset_host.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>

namespace {
namespace as = atlantis::asset_system;
namespace ac = atlantis::connection::assets;
struct CatalogFixture {
  std::filesystem::path dir = std::filesystem::temp_directory_path() / ("atlantis_assets_" + std::to_string(std::random_device{}()));
  std::filesystem::path path = dir / "catalog.txt";
  CatalogFixture() { std::filesystem::create_directories(dir); }
  ~CatalogFixture() { std::error_code ec; std::filesystem::remove(path, ec); std::filesystem::remove(dir, ec); }
  void write(std::vector<as::AssetCatalogRecord> records) const { std::ofstream(path, std::ios::binary) << as::serializeAssetCatalog(std::move(records)); }
};
using AssetHost = atlantis::runtime::AssetHost;
as::AssetGuid guid(std::string_view key) { return as::deriveAssetGuid(as::parseAssetGuid("00810081-0081-4081-8081-008100810081").value(), key); }
as::AssetCatalogRecord record(as::CatalogAssetType type, std::string key) {
  as::AssetCatalogRecord r;
  r.guid = guid(key); r.assetId = as::assetKey(r.guid); r.type = type;
  r.source = {as::CatalogRoot::Content, "test/scene.gltf", key};
  r.artifact = "asset.bin"; r.metadata = "asset.meta.txt"; r.artifactSchema = UINT32_MAX;
  r.sourceSchema = 8; r.tool = "unit/1";
  return r;
}
}
TEST_CASE("assets: first list freezes full catalog facts across connections", "[runtime][assets]") {
  CatalogFixture f;
  atlantis::runtime::AssetHost host(f.path.string());
  auto a = host.connect(); auto b = host.connect();
  std::vector<as::AssetCatalogRecord> records;
  for (auto t : {as::CatalogAssetType::Mesh, as::CatalogAssetType::Texture, as::CatalogAssetType::Material,
                 as::CatalogAssetType::Scene, as::CatalogAssetType::Environment}) records.push_back(record(t, std::string(as::toString(t))));
  records[2].dependencies = {records[1].guid}; records[0].sourceSchema.reset();
  f.write(records); // Construction/connect did not cache an absent file.
  auto list = a->listAssets(); REQUIRE(list.isOk()); REQUIRE(list.value().size() == 5);
  for (std::size_t i = 1; i < list.value().size(); ++i) CHECK(list.value()[i - 1].guid < list.value()[i].guid);
  for (const auto& r : records) {
    auto detail = b->getAsset(r.guid); REQUIRE(detail.isOk());
    CHECK(detail.value().source == as::toString(r.source));
    CHECK(detail.value().artifactSchema == r.artifactSchema);
    CHECK(detail.value().sourceSchema == r.sourceSchema);
    CHECK(detail.value().dependencies == r.dependencies);
    CHECK(detail.value().tool == r.tool);
  }
  f.write({});
  CHECK(host.connect()->listAssets().value() == list.value());
  auto copy = list.value(); copy[0].source = "changed client copy";
  CHECK(a->listAssets().value() != copy);
  atlantis::runtime::AssetHost fresh(f.path.string());
  REQUIRE(fresh.connect()->listAssets().isOk()); CHECK(fresh.connect()->listAssets().value().empty());
  CHECK(a->getAsset(guid("missing")).error() == ac::AssetError::UnknownAsset);
}
TEST_CASE("assets: unknown first get freezes ready; failed first load stays unavailable", "[runtime][assets]") {
  CatalogFixture f;
  atlantis::runtime::AssetHost failed(f.path.string());
  CHECK(failed.connect()->getAsset(guid("missing")).error() == ac::AssetError::CatalogUnavailable);
  auto r = record(as::CatalogAssetType::Mesh, "mesh"); f.write({r});
  CHECK(failed.connect()->listAssets().error() == ac::AssetError::CatalogUnavailable);
  atlantis::runtime::AssetHost ready(f.path.string());
  CHECK(ready.connect()->getAsset(guid("missing")).error() == ac::AssetError::UnknownAsset);
  f.write({}); CHECK(ready.connect()->listAssets().value().size() == 1);
  std::ofstream(f.path) << "bad catalog";
  atlantis::runtime::AssetHost malformed(f.path.string());
  CHECK(malformed.connect()->listAssets().error() == ac::AssetError::CatalogUnavailable);
}
TEST_CASE("assets: the assembled build catalog is projected completely without loading artifacts", "[runtime][assets]") {
  auto catalog = as::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(catalog.isOk());
  atlantis::runtime::AssetHost host(ATLANTIS_ASSET_CATALOG_PATH);
  auto connection = host.connect();
  auto list = connection->listAssets(); REQUIRE(list.isOk());
  REQUIRE(list.value().size() == catalog.value().records().size());
  for (const auto& r : catalog.value().records()) {
    auto d = connection->getAsset(r.guid); REQUIRE(d.isOk());
    CHECK(ac::toString(d.value().type) == as::toString(r.type));
    CHECK(d.value().source == as::toString(r.source));
    CHECK(d.value().artifactSchema == r.artifactSchema);
    CHECK(d.value().sourceSchema == r.sourceSchema);
    CHECK(d.value().tool == r.tool);
    CHECK(d.value().dependencies == r.dependencies);
    const auto s = std::find_if(list.value().begin(), list.value().end(), [&](const auto& v) { return v.guid == r.guid; });
    REQUIRE(s != list.value().end());
    CHECK(*s == ac::AssetSummary{d.value().guid, d.value().type, d.value().source});
  }
}

TEST_CASE("assets preview shares the first catalog snapshot across connections without rebuild", "[runtime][assets][preview]") {
  CatalogFixture f;
  const auto mesh = record(as::CatalogAssetType::Mesh, "mesh");
  const AssetHost::PreviewProvider preview{
      .available = [] { return true; },
      .render = [](const as::AssetGuid&, as::StaticMeshAssetData) {
        FAIL("an unavailable or frozen catalog must not reach the renderer");
        return ac::MeshPreviewResult::Err(ac::PreviewError::RenderFailed);
      }};
  AssetHost host(f.path.string(), nullptr, preview);
  auto client = host.connect();
  SECTION("list-first empty snapshot is not refreshed") {
    f.write({});
    REQUIRE(client->listAssets().isOk());
    f.write({mesh});
    CHECK(client->previewMesh(mesh.guid).error() == ac::PreviewError::UnknownAsset);
    client.reset();
    host.beginServiceInterval();
    CHECK(host.connect()->previewMesh(mesh.guid).error() == ac::PreviewError::UnknownAsset);
    AssetHost fresh(f.path.string(), nullptr, preview);
    CHECK(fresh.connect()->getAsset(mesh.guid).isOk());
    CHECK(fresh.connect()->previewMesh(mesh.guid).error() == ac::PreviewError::InvalidMesh);
  }
  SECTION("preview-first freezes an unavailable catalog across reconnect") {
    CHECK(client->previewMesh(mesh.guid).error() == ac::PreviewError::CatalogUnavailable);
    f.write({mesh});
    client.reset();
    host.beginServiceInterval();
    CHECK(host.connect()->previewMesh(mesh.guid).error() == ac::PreviewError::CatalogUnavailable);
    CHECK(host.connect()->listAssets().error() == ac::AssetError::CatalogUnavailable);
  }
  SECTION("preview-first freezes a readable catalog even when its mesh is unknown") {
    f.write({mesh});
    CHECK(client->previewMesh(guid("unknown")).error() == ac::PreviewError::UnknownAsset);
    f.write({});
    CHECK(host.connect()->getAsset(mesh.guid).isOk());
  }
}

TEST_CASE("assets preview admits one usable attempt across Remote and local connections", "[runtime][assets][preview]") {
  auto catalog = as::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(catalog.isOk());
  const auto& records = catalog.value().records();
  const auto mesh = std::find_if(records.begin(), records.end(), [](const auto& r) { return r.type == as::CatalogAssetType::Mesh; });
  const auto nonMesh = std::find_if(records.begin(), records.end(), [](const auto& r) { return r.type != as::CatalogAssetType::Mesh; });
  REQUIRE(mesh != records.end());
  REQUIRE(nonMesh != records.end());
  bool usable = false;
  int renders = 0;
  AssetHost host(ATLANTIS_ASSET_CATALOG_PATH, nullptr,
                 {.available = [&] { return usable; },
                  .render = [&](const as::AssetGuid& id, as::StaticMeshAssetData data) {
                    ++renders;
                    CHECK(id == mesh->guid);
                    CHECK(data.indexCount() > 0);
                    if (renders == 1) return ac::MeshPreviewResult::Err(ac::PreviewError::RenderFailed);
                    return ac::MeshPreviewResult::Ok({id, 256, 256, {}});
                  }});
  auto remoteA = host.connect();
  auto remoteB = host.connect();
  auto local = host.connect();
  AssetHost noProvider(ATLANTIS_ASSET_CATALOG_PATH);
  CHECK(noProvider.connect()->previewMesh(mesh->guid).error() == ac::PreviewError::Unsupported);
  CHECK(remoteA->previewMesh(mesh->guid).error() == ac::PreviewError::Unavailable);
  usable = true;  // unavailable attempt did not consume
  CHECK(remoteA->previewMesh(nonMesh->guid).error() == ac::PreviewError::NotMesh);
  CHECK(remoteB->previewMesh(mesh->guid).error() == ac::PreviewError::Busy);
  CHECK(local->previewMesh(mesh->guid).error() == ac::PreviewError::Busy);
  CHECK(renders == 0);
  remoteA.reset();  // disconnect does not reset the shared permit
  CHECK(host.connect()->previewMesh(mesh->guid).error() == ac::PreviewError::Busy);
  host.beginServiceInterval();
  CHECK(remoteB->previewMesh(mesh->guid).error() == ac::PreviewError::RenderFailed);
  CHECK(local->previewMesh(mesh->guid).error() == ac::PreviewError::Busy);
  host.beginServiceInterval();
  REQUIRE(local->previewMesh(mesh->guid).isOk());
  host.beginServiceInterval();
  REQUIRE(remoteB->previewMesh(mesh->guid).isOk());
  CHECK(renders == 3);
}
