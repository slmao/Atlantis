#include "remote_fixture.h"
#include "codec.h"



namespace {
namespace as = atlantis::asset_system;
namespace ca = atlantis::connection::assets;
namespace codec = atlantis::remote::codec;
using Value = atlantis::connection::json::Value;
as::AssetGuid guid() { return as::parseAssetGuid("00810081-0081-4081-8081-008100810081").value(); }
struct Assets final : atlantis::connection::AssetConnection {
  ca::AssetDetails detail{guid(), ca::AssetType::Mesh, "content:test/file.gltf#mesh/0", UINT32_MAX, std::nullopt, "unit/1", {}};
  bool unavailable = false;
  bool unordered = false;
  auto listAssets() const -> atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError> override {
    using R = atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError>;
    if (unavailable) return R::Err(ca::AssetError::CatalogUnavailable);
    std::vector<ca::AssetSummary> list{{detail.guid, detail.type, detail.source}};
    if (unordered) list.push_back(list[0]);
    return R::Ok(std::move(list));
  }
  auto getAsset(const as::AssetGuid& id) const -> atlantis::Result<ca::AssetDetails, ca::AssetError> override {
    using R = atlantis::Result<ca::AssetDetails, ca::AssetError>;
    if (unavailable) return R::Err(ca::AssetError::CatalogUnavailable);
    return id == detail.guid ? R::Ok(detail) : R::Err(ca::AssetError::UnknownAsset);
  }
};
struct Borrow final : atlantis::connection::AssetConnection {
  explicit Borrow(Assets& a) : a_(a) {}
  auto listAssets() const -> atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError> override { return a_.listAssets(); }
  auto getAsset(const as::AssetGuid& id) const -> atlantis::Result<ca::AssetDetails, ca::AssetError> override { return a_.getAsset(id); }
  Assets& a_;
};
}
TEST_CASE("assets: typed codecs reject malformed details but tolerate extra members and future schema", "[remote][assets]") {
  Assets assets;
  auto encoded = codec::encode(assets.detail);
  encoded.set("extra", Value::boolean(true));
  CHECK(codec::decodeAssetDetails(encoded) == assets.detail);
  for (auto key : {"guid", "type", "source", "artifactSchema", "tool", "dependencies"}) {
    auto bad = encoded; bad.set(key, Value()); CHECK_FALSE(codec::decodeAssetDetails(bad));
  }
  auto nil = encoded; nil.set("guid", Value::string("00000000-0000-0000-0000-000000000000")); CHECK_FALSE(codec::decodeAssetDetails(nil));
  auto bad = encoded; bad.set("artifactSchema", Value::number(std::uint64_t{UINT32_MAX} + 1));
  CHECK_FALSE(codec::decodeAssetDetails(bad));
  bad = encoded; bad.set("type", Value::string("gltf_import")); CHECK_FALSE(codec::decodeAssetDetails(bad));
  bad = encoded; bad.set("dependencies", Value::array({codec::encodeAsset(guid()), codec::encodeAsset(guid())}));
  CHECK_FALSE(codec::decodeAssetDetails(bad));
  auto missing = codec::encode(ca::AssetSummary{guid(), ca::AssetType::Mesh, "assets:mesh.txt"});
  missing.set("artifactSchema", Value::number(std::uint64_t{0}));
  missing.set("tool", Value::string("test/1")); missing.set("dependencies", Value::array());
  CHECK_FALSE(codec::decodeAssetDetails(missing));
  missing.set("sourceSchema", Value::number(std::uint64_t{UINT32_MAX}));
  REQUIRE(codec::decodeAssetDetails(missing)); CHECK(codec::decodeAssetDetails(missing)->artifactSchema == 0);
  missing.set("sourceSchema", Value::number(std::int64_t{-1})); CHECK_FALSE(codec::decodeAssetDetails(missing));
}
TEST_CASE("assets: native clients share queries and in-band refusals preserve their session", "[remote][assets]") {
  using namespace atlantis::remote::test;
  ServedWorld served(cookAndDecodeScene(kSceneSource)); Assets assets;
  served.server->serveAssets([&] { return std::make_unique<Borrow>(assets); });
  auto a = served.attach(); auto b = served.attach();
  auto& sdk = a->assets();
  CHECK(sdk.listAssets().value() == b->assets().listAssets().value());
  CHECK(sdk.getAsset(guid()).value() == assets.detail);
  CHECK(sdk.getAsset(fixtureSceneGuid()).error() == ca::AssetError::UnknownAsset);
  assets.unavailable = true;
  CHECK(sdk.listAssets().error() == ca::AssetError::CatalogUnavailable);
  CHECK_FALSE(a->failure()); CHECK_FALSE(a->connection().listEntities().empty());
  assets.unavailable = false; assets.unordered = true;
  CHECK(sdk.listAssets().isErr()); CHECK(a->failure().has_value());
}
TEST_CASE("assets: absent provider and output overflow fail without empty success", "[remote][assets]") {
  using namespace atlantis::remote::test;
  ServedWorld absent(cookAndDecodeScene(kSceneSource)); auto old = absent.attach();
  CHECK_FALSE(old->failure()); CHECK(old->assets().listAssets().isErr()); CHECK(old->failure().has_value());
  atlantis::remote::ServerLimits limits; limits.maxPendingWriteBytes = 64 * 1024;
  ServedWorld served(cookAndDecodeScene(kSceneSource), limits); Assets assets;
  served.server->serveAssets([&] { return std::make_unique<Borrow>(assets); });
  auto a = served.attach(); assets.detail.source = std::string(70 * 1024, 'x');
  CHECK(a->assets().listAssets().isErr()); CHECK(a->failure().has_value());
  assets.detail.source = "assets:mesh.txt"; auto b = served.attach();
  CHECK(b->assets().listAssets().isOk());
}
