#include "remote_fixture.h"
#include "codec.h"



namespace {
namespace as = atlantis::asset_system;
namespace ca = atlantis::connection::assets;
namespace codec = atlantis::remote::codec;
using Value = atlantis::connection::json::Value;
as::AssetGuid guid() { return as::parseAssetGuid("00810081-0081-4081-8081-008100810081").value(); }
as::AssetGuid materialGuid() { return as::deriveAssetGuid(guid(), "material"); }
ca::MeshPreview preview() {
  // Contract tests only need the signature and complete IHDR; a full PNG is
  // exercised through the Runtime's real render path.
  return {guid(), 256, 256, {137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',
                             0,0,1,0,0,0,1,0,8,6,0,0,0,0,0,0,0}};
}
ca::MeshMaterialPreview pairPreview() {
  return {guid(), materialGuid(), ca::kPreviewDimension, ca::kPreviewDimension, preview().png};
}
struct Assets final : atlantis::connection::AssetConnection {
  ca::AssetDetails detail{guid(), ca::AssetType::Mesh, "content:test/file.gltf#mesh/0", UINT32_MAX, std::nullopt, "unit/1", {}};
  bool unavailable = false;
  bool unordered = false;
  std::optional<ca::PreviewError> previewError;
  ca::MeshPreview image = preview();
  std::optional<ca::MeshMaterialPreviewError> pairError;
  ca::MeshMaterialPreview pairImage = pairPreview();
  std::optional<ca::RebuildJob> job;
  std::optional<ca::ImportJob> imported;
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
  ca::MeshPreviewResult previewMesh(const as::AssetGuid& id) override {
    if (id != detail.guid) return ca::MeshPreviewResult::Err(ca::PreviewError::UnknownAsset);
    if (previewError) return ca::MeshPreviewResult::Err(*previewError);
    return ca::MeshPreviewResult::Ok(image);
  }
  ca::MeshMaterialPreviewResult previewMeshMaterial(const as::AssetGuid& mesh,
                                                     const as::AssetGuid& material) override {
    using R = ca::MeshMaterialPreviewResult;
    using E = ca::MeshMaterialPreviewError;
    if (mesh != guid()) return R::Err(E::UnknownMesh);
    if (material != materialGuid()) return R::Err(E::UnknownMaterial);
    if (pairError) return R::Err(*pairError);
    return R::Ok(pairImage);
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
  ca::ImportStartResult importAsset(std::string_view name, ca::ImportMode mode) override {
    imported = ca::ImportJob{{as::deriveAssetGuid(guid(), "import")}, std::string(name), mode,
                            ca::RebuildState::Running, {}, {}, {}};
    return ca::ImportStartResult::Ok(*imported);
  }
  ca::ImportStatusResult importStatus(std::optional<ca::ImportId> id) const override {
    if (id && (!imported || imported->id != *id)) return ca::ImportStatusResult::Err(ca::ImportError::UnknownJob);
    return ca::ImportStatusResult::Ok({{}, imported});
  }
};
struct Borrow final : atlantis::connection::AssetConnection {
  explicit Borrow(Assets& a) : a_(a) {}
  auto listAssets() const -> atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError> override { return a_.listAssets(); }
  auto getAsset(const as::AssetGuid& id) const -> atlantis::Result<ca::AssetDetails, ca::AssetError> override { return a_.getAsset(id); }
  ca::MeshPreviewResult previewMesh(const as::AssetGuid& id) override { return a_.previewMesh(id); }
  ca::MeshMaterialPreviewResult previewMeshMaterial(const as::AssetGuid& mesh,
                                                     const as::AssetGuid& material) override {
    return a_.previewMeshMaterial(mesh, material);
  }
  ca::RebuildStartResult rebuild(const as::AssetGuid& asset) override { return a_.rebuild(asset); }
  ca::RebuildStatusResult rebuildStatus(std::optional<ca::RebuildId> id) const override { return a_.rebuildStatus(id); }
  ca::ImportStartResult importAsset(std::string_view name, ca::ImportMode mode) override {
    return a_.importAsset(name, mode);
  }
  ca::ImportStatusResult importStatus(std::optional<ca::ImportId> id) const override {
    return a_.importStatus(id);
  }
  Assets& a_;
};
}

TEST_CASE("asset preview codec: fixed metadata, canonical PNG and inclusive decoded size", "[remote][assets][asset-preview]") {
  const auto image = preview();
  const auto payload = ca::encodeMeshPreview(image, guid());
  REQUIRE(payload);
  CHECK(payload->find("pngBase64")->asString() == "iVBORw0KGgoAAAANSUhEUgAAAQAAAAEACAYAAAAAAAAA");
  CHECK(ca::decodeMeshPreview(*payload, guid()) == image);
  CHECK_FALSE(ca::decodeMeshPreview(*payload, as::deriveAssetGuid(guid(), "wrong-guid")));
  auto bad = *payload;
  bad.set("mimeType", Value::string("image/jpeg"));
  CHECK_FALSE(ca::decodeMeshPreview(bad, guid()));
  bad = *payload;
  bad.set("width", Value::number(std::uint64_t{255}));
  CHECK_FALSE(ca::decodeMeshPreview(bad, guid()));
  bad = *payload;
  bad.set("pngBase64", Value::string("iVBORw0KGgoAAAANSUhEUgAAAQAAAAIACAYAAAAAAAAA")); // IHDR height 512.
  CHECK_FALSE(ca::decodeMeshPreview(bad, guid()));
  bad.set("pngBase64", Value::string("iVBORw0KGgoAAAAMSUhEUgAAAQAAAAEACAYAAAAAAAAA")); // IHDR length 12.
  CHECK_FALSE(ca::decodeMeshPreview(bad, guid()));
  bad.set("pngBase64", Value::string("iVBORw0KGgoAAAANSUhEUgAAAQAAAAEACAYAAAAAAAA "));
  CHECK_FALSE(ca::decodeMeshPreview(bad, guid()));

  auto largest = image;
  largest.png.resize(ca::kPreviewPngMaxBytes);
  auto bound = ca::encodeMeshPreview(largest, guid());
  REQUIRE(bound);
  auto base64 = bound->find("pngBase64")->asString();
  REQUIRE(base64.size() == 699052);
  CHECK(ca::decodeMeshPreview(*bound, guid()) == largest);
  REQUIRE(base64.ends_with("A="));
  base64[base64.size() - 2] = 'B'; // Unused tail bits would give an alternate spelling.
  bound->set("pngBase64", Value::string(base64));
  CHECK_FALSE(ca::decodeMeshPreview(*bound, guid()));
  base64[base64.size() - 2] = 'A';
  base64.back() = 'A'; // Same encoded length, one byte over the decoded cap.
  bound->set("pngBase64", Value::string(std::move(base64)));
  CHECK_FALSE(ca::decodeMeshPreview(*bound, guid()));
}

TEST_CASE("asset preview: shared native provider and typed refusals keep discovery usable", "[remote][assets][asset-preview]") {
  using namespace atlantis::remote::test;
  ServedWorld served(cookAndDecodeScene(kSceneSource));
  auto absent = served.attach();
  CHECK(absent->assets().previewMesh(guid()).error() == ca::PreviewError::Unsupported);
  CHECK_FALSE(absent->failure());

  Assets assets;
  served.server->serveAssets([&] { return std::make_unique<Borrow>(assets); });
  auto session = served.attach();
  auto got = session->assets().previewMesh(guid());
  REQUIRE(got.isOk());
  CHECK(got.value() == assets.image);
  CHECK(session->assets().previewMesh(fixtureSceneGuid()).error() == ca::PreviewError::UnknownAsset);
  for (auto error : {ca::PreviewError::Unsupported, ca::PreviewError::Unavailable, ca::PreviewError::Busy,
                     ca::PreviewError::CatalogUnavailable, ca::PreviewError::NotMesh, ca::PreviewError::TooLarge,
                     ca::PreviewError::InvalidMesh, ca::PreviewError::RenderFailed}) {
    assets.previewError = error;
    CHECK(session->assets().previewMesh(guid()).error() == error);
    CHECK_FALSE(session->failure());
  }
  assets.previewError.reset();
  assets.image.png.resize(ca::kPreviewPngMaxBytes + 1);
  CHECK(session->assets().previewMesh(guid()).error() == ca::PreviewError::RenderFailed);
  CHECK_FALSE(session->failure());
  CHECK(got.value() == preview()); // The retained DTO owns its bytes.
  CHECK(session->assets().listAssets().isOk());
  CHECK(session->assets().getAsset(guid()).value() == assets.detail);
}

TEST_CASE("asset pair preview codec: role correlation and bounded PNG", "[remote][assets][asset-preview]") {
  const auto image = pairPreview();
  auto payload = ca::encodeMeshMaterialPreview(image, guid(), materialGuid());
  REQUIRE(payload);
  CHECK(ca::decodeMeshMaterialPreview(*payload, guid(), materialGuid()) == image);
  CHECK_FALSE(ca::encodeMeshMaterialPreview(image, materialGuid(), guid()));
  CHECK_FALSE(ca::decodeMeshMaterialPreview(*payload, materialGuid(), guid()));

  for (const auto* key : {"meshGuid", "materialGuid"}) {
    CAPTURE(key);
    auto mismatched = *payload;
    mismatched.set(key, codec::encodeAsset(as::deriveAssetGuid(guid(), "wrong")));
    CHECK_FALSE(ca::decodeMeshMaterialPreview(mismatched, guid(), materialGuid()));
  }
  auto duplicate = payload->asObject();
  duplicate.emplace_back("materialGuid", codec::encodeAsset(guid()));
  CHECK_FALSE(ca::decodeMeshMaterialPreview(Value::object(std::move(duplicate)), guid(), materialGuid()));
  auto bad = *payload;
  bad.set("mimeType", Value::string("image/jpeg"));
  CHECK_FALSE(ca::decodeMeshMaterialPreview(bad, guid(), materialGuid()));
  bad = *payload;
  bad.set("pngBase64", Value::string("iVBORw0KGgoAAAANSUhEUgAAAQAAAAIACAYAAAAAAAAA"));
  CHECK_FALSE(ca::decodeMeshMaterialPreview(bad, guid(), materialGuid()));

  auto largest = image;
  largest.png.resize(ca::kPreviewPngMaxBytes);
  auto bound = ca::encodeMeshMaterialPreview(largest, guid(), materialGuid());
  REQUIRE(bound);
  CHECK(ca::decodeMeshMaterialPreview(*bound, guid(), materialGuid()) == largest);
  auto encoded = bound->find("pngBase64")->asString();
  REQUIRE(encoded.size() == 699052);
  REQUIRE(encoded.ends_with("A="));
  encoded[encoded.size() - 2] = 'B';
  bound->set("pngBase64", Value::string(encoded));
  CHECK_FALSE(ca::decodeMeshMaterialPreview(*bound, guid(), materialGuid()));
  encoded[encoded.size() - 2] = 'A';
  encoded.back() = 'A';
  bound->set("pngBase64", Value::string(std::move(encoded)));
  CHECK_FALSE(ca::decodeMeshMaterialPreview(*bound, guid(), materialGuid()));
}

TEST_CASE("asset pair preview: shared provider keeps asset discovery and geometry usable", "[remote][assets][asset-preview]") {
  using namespace atlantis::remote::test;
  using E = ca::MeshMaterialPreviewError;
  ServedWorld served(cookAndDecodeScene(kSceneSource));
  auto absent = served.attach();
  CHECK(absent->assets().previewMeshMaterial(guid(), materialGuid()).error() == E::Unsupported);
  CHECK_FALSE(absent->failure());

  Assets assets;
  served.server->serveAssets([&] { return std::make_unique<Borrow>(assets); });
  auto session = served.attach();
  auto got = session->assets().previewMeshMaterial(guid(), materialGuid());
  REQUIRE(got.isOk());
  CHECK(got.value() == assets.pairImage);
  CHECK(session->assets().previewMeshMaterial(fixtureSceneGuid(), materialGuid()).error() == E::UnknownMesh);
  CHECK(session->assets().previewMeshMaterial(guid(), fixtureSceneGuid()).error() == E::UnknownMaterial);
  for (auto error : {E::Unsupported, E::Unavailable, E::Busy, E::CatalogUnavailable, E::NotMesh, E::NotMaterial,
                     E::InvalidMesh, E::InvalidMaterial, E::UnsupportedMaterial, E::DependencyUnavailable,
                     E::IncompatiblePair, E::TooLarge, E::RenderFailed}) {
    CAPTURE(ca::toString(error));
    assets.pairError = error;
    CHECK(session->assets().previewMeshMaterial(guid(), materialGuid()).error() == error);
    CHECK_FALSE(session->failure());
  }
  assets.pairError.reset();
  assets.pairImage.materialGuid = fixtureSceneGuid();
  CHECK(session->assets().previewMeshMaterial(guid(), materialGuid()).error() == E::RenderFailed);
  CHECK_FALSE(session->failure());
  CHECK(got.value() == pairPreview());
  CHECK(session->assets().previewMesh(guid()).value() == preview());
  CHECK(session->assets().listAssets().isOk());
  CHECK(session->assets().getAsset(guid()).value() == assets.detail);
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

TEST_CASE("asset import: committed failure remains observable on a separate session", "[remote][assets][asset-import]") {
  using namespace atlantis::remote::test;
  ServedWorld served(cookAndDecodeScene(kSceneSource));
  Assets assets;
  served.server->serveAssets([&] { return std::make_unique<Borrow>(assets); });
  auto a = served.attach(), b = served.attach();
  const auto accepted = a->assets().importAsset("sample", ca::ImportMode::Register);
  REQUIRE(accepted.isOk());
  const auto id = accepted.value().id;
  CHECK(b->assets().importStatus(id).value().job == accepted.value());
  CHECK(b->assets().importStatus(ca::ImportId{guid()}).error() == ca::ImportError::UnknownJob);
  assets.imported->state = ca::RebuildState::Failed;
  assets.imported->failure = ca::ImportFailure{ca::ImportStage::Publication, ca::ImportFailureCode::PublishFailed,
      "test", {}, ca::RegistrationState::Committed, guid()};
  const auto terminal = b->assets().importStatus(id);
  REQUIRE(terminal.isOk());
  CHECK(terminal.value().job == assets.imported);
  auto raw = codec::encode(*assets.imported);
  raw.set("generation", codec::encodeAsset(guid()));
  CHECK_FALSE(codec::decodeImportJob(raw));
  CHECK_FALSE(b->failure());
  CHECK(b->assets().listAssets().isOk());
}

TEST_CASE("managed material codec: closed Edit arms and distinct terminal outcomes", "[remote][assets][asset-material]") {
  ca::MaterialValues values{guid(), {0.125f, 0.25f, 1.0f}, 0.0f, 0.5f};
  auto edit = ca::MaterialSave{ca::EditMaterial{materialGuid(), {}, values, values}};
  auto wire = ca::encodeMaterialSave(edit);
  REQUIRE(ca::decodeMaterialSave(wire));
  CHECK(std::get<ca::EditMaterial>(*ca::decodeMaterialSave(wire)).expected == values);
  auto duplicate = wire.asObject();
  duplicate.emplace_back("activeGeneration", Value{});
  CHECK_FALSE(ca::decodeMaterialSave(Value::object(std::move(duplicate))));
  auto missing = wire.asObject();
  std::erase_if(missing, [](const auto& field) { return field.first == "activeGeneration"; });
  CHECK_FALSE(ca::decodeMaterialSave(Value::object(std::move(missing))));
  ca::MaterialJob job{{guid()}, ca::materialTarget(edit), ca::RebuildState::Failed, {}, {},
      ca::MaterialFailure{ca::MaterialStage::Publication, ca::MaterialFailureCode::PublishFailed,
          "committed", {}, ca::RegistrationState::Committed, materialGuid()}};
  CHECK(ca::decodeMaterialJob(ca::encodeMaterialJob(job)) == job);
  job.failure->material = guid();
  CHECK_FALSE(ca::decodeMaterialJob(ca::encodeMaterialJob(job)));
  job.failure->sourceState = ca::RegistrationState::Unknown;
  job.failure->material.reset();
  const ca::MaterialStatus repair{{}, job, true};
  CHECK(ca::decodeMaterialStatus(ca::encodeMaterialStatus(repair)) == repair);
}
