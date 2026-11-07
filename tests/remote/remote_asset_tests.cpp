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
  std::optional<ca::RebuildJob> job;
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
  ca::RebuildStartResult rebuild(const as::AssetGuid& asset) override {
    if (job) return ca::RebuildStartResult::Err(ca::RebuildError::Busy);
    if (asset != guid()) return ca::RebuildStartResult::Err(ca::RebuildError::UnknownAsset);
    job = ca::RebuildJob{{as::deriveAssetGuid(asset, "job")}, asset, ca::RebuildState::Running, {}, {}};
    return ca::RebuildStartResult::Ok(*job);
  }
  ca::RebuildStatusResult rebuildStatus(std::optional<ca::RebuildId> id) const override {
    if (id && (!job || job->id != *id)) return ca::RebuildStatusResult::Err(ca::RebuildError::UnknownJob);
    return ca::RebuildStatusResult::Ok({{}, job});
  }
};
struct Borrow final : atlantis::connection::AssetConnection {
  explicit Borrow(Assets& a) : a_(a) {}
  auto listAssets() const -> atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError> override { return a_.listAssets(); }
  auto getAsset(const as::AssetGuid& id) const -> atlantis::Result<ca::AssetDetails, ca::AssetError> override { return a_.getAsset(id); }
  ca::RebuildStartResult rebuild(const as::AssetGuid& asset) override { return a_.rebuild(asset); }
  ca::RebuildStatusResult rebuildStatus(std::optional<ca::RebuildId> id) const override { return a_.rebuildStatus(id); }
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

TEST_CASE("asset rebuild: typed receipts and status use the same shared provider", "[remote][assets][asset-rebuild]") {
  using namespace atlantis::remote::test;
  ServedWorld served(cookAndDecodeScene(kSceneSource));
  Assets assets;
  served.server->serveAssets([&] { return std::make_unique<Borrow>(assets); });
  auto a = served.attach(), b = served.attach();
  REQUIRE(a->assets().rebuildStatus({}).isOk());
  CHECK_FALSE(a->assets().rebuildStatus({}).value().job);
  CHECK(a->assets().rebuild(fixtureSceneGuid()).error() == ca::RebuildError::UnknownAsset);
  auto started = a->assets().rebuild(guid());
  REQUIRE(started.isOk());
  CHECK(b->assets().rebuild(guid()).error() == ca::RebuildError::Busy);
  CHECK(b->assets().rebuildStatus(started.value().id).value().job == started.value());
  assets.job->state = ca::RebuildState::Failed;
  assets.job->failure = ca::RebuildFailure{ca::RebuildStage::Pipeline, ca::RebuildFailureCode::BuildFailed, "test", 19};
  const auto terminal = b->assets().rebuildStatus(started.value().id);
  REQUIRE(terminal.isOk());
  CHECK(terminal.value().job == assets.job);
  CHECK_FALSE(b->failure());
  CHECK(b->assets().listAssets().isOk());
  auto encoded = codec::encode(*assets.job);
  encoded.set("generation", codec::encodeAsset(guid()));
  CHECK_FALSE(codec::decodeRebuildJob(encoded));
  CHECK_FALSE(codec::decodeRebuildError(Value::string("UnknownJob"), true));
}
