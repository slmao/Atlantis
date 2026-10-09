// Plan 0057 M5 (Spec 0057 R4, R8, R10; rulings Q7, Q8, Q9; J1, J7): the
// Gameplay SDK against a real, windowed Runtime under fatal Validation
// Layers, over Plan 0055 M7's in-process loopback -- a RuntimeControlHost
// drives the frames, a real RemoteServer is polled after each, and the
// client's waits run them, exactly as `atlantis_runtime --listen` does:
//   - the North star: the example's beacon loop (P6) with the typed layer,
//     exact frame data per logic step, an image change by decoded pixels,
//     and the same per-step values on a second run whose frame numbers
//     differ;
//   - RemoteBatch and SequentialQueryBatch answer the same;
//   - read isolation: a second client of the same Runtime (its own InProcess
//     connection and the shared control, J1) stepping, resuming, or a step
//     still pending, mixes the leaves of a one-at-a-time read; paused with
//     none of those, the read is consistent;
//   - Bistro, content-gated: typed and reflective Light queries agree, and
//     6b63b12c goes 12 -> 24 exactly through the typed layer.

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/gameplay/generated/world.h>
#include <atlantis/gameplay/query_batch.h>
#include <atlantis/gameplay/transaction.h>
#include <atlantis/gameplay/world.h>
#include <atlantis/remote/remote_client.h>
#include <atlantis/remote/remote_server.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/runtime_control_host.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "png_codec.h"

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;

namespace {

namespace fs = std::filesystem;
namespace gp = atlantis::gameplay;
namespace w = atlantis::gameplay::world;
namespace access = atlantis::world::access;

// --- Copied from runtime_smoke_gpu_tests.cpp's anonymous namespace (the
// windowed smoke config, as main.cpp populates it), so that verified file
// stays untouched. --------------------------------------------------------------

// The ATLANTIS_RUNTIME_*_GUID definitions are generated from the committed
// catalog source at configure time (Plan 0047 P15).
[[nodiscard]] atlantis::asset_system::AssetGuid sceneGuidFromDefinition(const char* text) {
  auto parsed = atlantis::asset_system::parseAssetGuid(text);
  REQUIRE(parsed.isOk());
  return parsed.value();
}

// The windowed smoke config, as main.cpp populates it. Plan 0044 M2
// follow-up: shared by the bloom smoke TEST_CASE below, which swaps only
// the scene paths.
[[nodiscard]] BootstrapConfig buildSmokeConfig() {
  BootstrapConfig config;
  config.applicationName = "Atlantis Runtime GPU Smoke Test";
  config.vertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv";
  config.vertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json";
  config.fragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv";
  config.fragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.refl.json";
  config.assetArtifactPath = ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH;
  config.assetMetadataPath = ATLANTIS_RUNTIME_ASSET_METADATA_PATH;
  // Plan 0015 Section D11: the real, loaded scene path -- replaces the
  // former hardcoded six-entity validation scene.
  config.assetCatalogPath = ATLANTIS_ASSET_CATALOG_PATH;
  config.sceneAsset = sceneGuidFromDefinition(ATLANTIS_RUNTIME_SCENE_GUID);
  // Plan 0018 Section P10: mirrors main.cpp's own identical population
  // of the second, MaterialKind::UnlitTextured built-in shader pair.
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  // Plan 0019 Section P6/P11: mirrors main.cpp's own identical
  // population of the third, MaterialKind::LitTextured built-in shader
  // pair.
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  // Plan 0023 Milestone 5: mirrors main.cpp's own identical population
  // of the fourth, MaterialKind::PbrDirectLit built-in shader pair.
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  // Plan 0029 Section P15: mirrors main.cpp's own identical population
  // of the fifth, normal-map MaterialKind::PbrDirectLit built-in shader
  // pair -- unconditionally required.
  config.pbrDirectLitNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) + "/pbr_direct_lit_normal_map.vert.spv";
  config.pbrDirectLitNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.vert.refl.json";
  config.pbrDirectLitNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) + "/pbr_direct_lit_normal_map.frag.spv";
  config.pbrDirectLitNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.frag.refl.json";
  // Plan 0028 Milestone 2: mirrors main.cpp's own identical population --
  // the default scene now configures an environment (sky + IBL), which
  // this TEST_CASE's own config previously left unset.
  config.environmentArtifactPath = ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_RUNTIME_ENVIRONMENT_METADATA_PATH;
  config.pbrIblVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.refl.json";
  // Plan 0029 Section P15: mirrors main.cpp's own identical population
  // of the normal-map IBL PBR pair -- required in exactly the same
  // environment-configured case as pbrIbl above.
  config.pbrIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.refl.json";
  config.skyVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.frag.refl.json";
  // Plan 0024 Milestone 6/7: mirrors main.cpp's own identical
  // population of the two output-transform shader pairs.
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.refl.json";
  config.outputTransformSrgbVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.vert.spv";
  config.outputTransformSrgbVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.vert.refl.json";
  config.outputTransformSrgbFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.frag.spv";
  config.outputTransformSrgbFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.frag.refl.json";
  // Plan 0027 Milestone 8 (ADR-0072 D-1): the shadow-casting shader pair
  // -- unconditionally required (mirrors main.cpp's own identical
  // population).
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  // Plan 0044 Milestone 1 (P9): the three bloom shader pairs, as main.cpp
  // sets them -- so the Runtime's bloom Pipelines are created here, under
  // Validation Layers, even when the scene does not turn bloom on.
  config.bloomDownsampleVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.vert.spv";
  config.bloomDownsampleVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.vert.refl.json";
  config.bloomDownsampleFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.frag.spv";
  config.bloomDownsampleFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.frag.refl.json";
  config.bloomUpsampleVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.vert.spv";
  config.bloomUpsampleVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.vert.refl.json";
  config.bloomUpsampleFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.frag.spv";
  config.bloomUpsampleFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.frag.refl.json";
  config.bloomCompositeVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.vert.spv";
  config.bloomCompositeVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.vert.refl.json";
  config.bloomCompositeFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.frag.spv";
  config.bloomCompositeFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.frag.refl.json";
  config.enableValidationLayers = true;
  return config;
}

// --- End of the copy. ----------------------------------------------------------

// The example's QueryBatch adapter (examples/gameplay_demo/remote_batch.h),
// over the loopback session.
class RemoteBatch final : public gp::QueryBatch {
 public:
  explicit RemoteBatch(atlantis::remote::RemoteSession& session) : session_(session) {}
  std::vector<atlantis::Result<std::vector<atlantis::schema::TypeId>, access::AccessError>> listComponents(
      std::span<const atlantis::asset_system::EntityGuid> entities) override {
    return session_.listComponents(entities);
  }
  std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> getProperties(
      std::span<const access::PropertyAddress> addresses) override {
    return session_.getProperties(addresses);
  }

 private:
  atlantis::remote::RemoteSession& session_;
};

// One query at a time over the connection, calling `between(i)` before the
// i-th (i >= 1) -- where a test places another client's actions inside a
// multi-leaf read (Spec 0057 R10).
class InterleavedBatch final : public gp::QueryBatch {
 public:
  explicit InterleavedBatch(atlantis::connection::RuntimeConnection& connection) : connection_(connection) {}
  std::vector<atlantis::Result<std::vector<atlantis::schema::TypeId>, access::AccessError>> listComponents(
      std::span<const atlantis::asset_system::EntityGuid> entities) override {
    return gp::SequentialQueryBatch(connection_).listComponents(entities);
  }
  std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> getProperties(
      std::span<const access::PropertyAddress> addresses) override {
    std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> out;
    for (std::size_t i = 0; i < addresses.size(); ++i) {
      if (i > 0 && between) between(i);
      out.push_back(connection_.getProperty(addresses[i]));
    }
    return out;
  }
  std::function<void(std::size_t)> between;

 private:
  atlantis::connection::RuntimeConnection& connection_;
};

// A windowed RuntimeApplication served as `atlantis_runtime --listen` serves
// it (Plan 0055 M7's in-process loopback): a RuntimeControlHost drives its
// frames, a real RemoteServer on loopback is polled after each, and the
// client's waits run those frames. Client A is the SDK over the remote
// session; client B is a second client of the same Runtime -- its own
// InProcess connection and the shared control, as --listen serves every
// remote client (Plan 0057 J1).
struct Attached {
  explicit Attached(RuntimeApplication& application)
      : app(application), control(atlantis::runtime::RuntimeControlHost::forApplication(app), options()) {
    auto listening = atlantis::remote::RemoteServer::listen(
        0, atlantis::remote::generateToken(), app.sceneGuid(), [this] { return app.openConnection(); }, &control);
    REQUIRE(listening.isOk());
    server = std::move(listening.value());
    atlantis::remote::RemoteOptions remoteOptions;
    remoteOptions.whileWaiting = [this] { frame(); };
    auto connected = atlantis::remote::connectRemote(server->session(), remoteOptions);
    REQUIRE(connected.isOk());
    session = std::move(connected.value());
    clientB = app.openConnection();
  }
  ~Attached() {
    clientB.reset();
    session.reset();
  }
  Attached(const Attached&) = delete;
  Attached& operator=(const Attached&) = delete;

  static atlantis::runtime::RuntimeControlHost::Options options() {
    atlantis::runtime::RuntimeControlHost::Options o;
    o.recordDiagnostics = false;
    return o;
  }

  void frame() {
    control.beforeFrame();
    app.runFrame();
    control.afterFrame();
    server->poll();
    REQUIRE(app.shouldContinue());
  }

  // A stepped frame through the remote control (client A's step).
  [[nodiscard]] atlantis::connection::FrameReport step(std::optional<fs::path> image = std::nullopt) {
    atlantis::connection::StepRequest request;
    request.frames = 1;
    if (image.has_value()) request.imagePath = image->string();
    std::optional<atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>> result;
    session->control().step(request, [&](auto done) { result = std::move(done); });
    REQUIRE(result.has_value());
    REQUIRE(result->isOk());
    REQUIRE_FALSE(session->failure().has_value());
    return result->value();
  }

  RuntimeApplication& app;
  atlantis::runtime::RuntimeControlHost control;
  std::unique_ptr<atlantis::remote::RemoteServer> server;
  std::unique_ptr<atlantis::remote::RemoteSession> session;
  std::unique_ptr<atlantis::connection::RuntimeConnection> clientB;
};

constexpr float kPi = 3.14159265358979f;
constexpr int kSteps = 8;

[[nodiscard]] std::array<float, 4> positionAt(int k) {
  const float angle = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(kSteps);
  return {1.5f * std::cos(angle), 2.0f, 1.5f * std::sin(angle), 1.0f};
}

[[nodiscard]] float intensityAt(int k) {
  return 4.0f + 2.0f * std::sin(2.0f * kPi * static_cast<float>(k) / static_cast<float>(kSteps));
}

[[nodiscard]] const atlantis::connection::FramePointLight* pointLightAt(const atlantis::connection::FrameReport& report,
                                                                       const std::array<float, 4>& position) {
  for (const auto& light : report.data.pointLights) {
    if (light.position == std::array<float, 3>{position[0], position[1], position[2]}) return &light;
  }
  return nullptr;
}

const auto kBeacon = atlantis::asset_system::parseEntityGuid("57005700-0000-4000-8000-0000000000b1").value();
const auto kSun = atlantis::asset_system::parseEntityGuid("0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea").value();

// What one run of the North star observed, per logic step k.
struct NorthStarRun {
  std::vector<std::array<float, 3>> positions;
  std::vector<float> intensities;
  std::vector<std::uint64_t> frames;
};

// Plan 0057 P7: the example's loop (P6) as a test, in process -- typed
// layer over the loopback session; every frame's data exact.
[[nodiscard]] NorthStarRun runNorthStar(Attached& attached, const fs::path& dir, int idleFramesFirst) {
  for (int i = 0; i < idleFramesFirst; ++i) attached.frame();  // shifts the Runtime's frame numbers
  RemoteBatch batch(*attached.session);
  gp::World world(attached.session->connection(), &batch);
  REQUIRE(world.checkCompatible<w::Light>().isOk());
  REQUIRE(world.checkCompatible<w::WorldMatrix>().isOk());
  REQUIRE_FALSE(world.exists(kBeacon));

  // Find: exactly one Directional light, through a typed query.
  const auto lights = world.entitiesWith<w::Light, w::WorldMatrix>();
  REQUIRE(lights.isOk());
  REQUIRE(lights.value() == std::vector<atlantis::asset_system::EntityGuid>{kSun});
  CHECK(world.read<w::Light>(kSun).value().kind == w::LightKind::Directional);

  attached.session->control().pause();
  const fs::path baselineImage = dir / "baseline.png";
  const auto baseline = attached.step(baselineImage);
  CHECK(baseline.data.pointLights.empty());

  // Spawn in one typed transaction, the Light before the WorldMatrix.
  gp::Subscription events = world.subscribe(
      atlantis::connection::EventFilter{atlantis::connection::EventKindSet::all(), kBeacon, std::nullopt});
  gp::Transaction spawn;
  spawn.create(kBeacon)
      .add(kBeacon, w::Light{w::LightKind::Point, {1.0f, 0.6f, 0.2f}, intensityAt(0), 4.0f})
      .add(kBeacon, w::WorldMatrix{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, positionAt(0)});
  const auto spawned = world.submit(spawn);
  REQUIRE(spawned.isOk());
  const auto first = attached.step();
  gp::FailureLog failures;
  failures.absorb(world.drainFailures());
  REQUIRE_FALSE(failures.refusal(spawned.value()).has_value());
  REQUIRE(first.data.pointLights.size() == 1);
  const auto* born = pointLightAt(first, positionAt(0));
  REQUIRE(born != nullptr);
  CHECK(born->color == std::array<float, 3>{1.0f, 0.6f, 0.2f});
  CHECK(born->intensity == intensityAt(0));
  CHECK(born->range == 4.0f);
  CHECK(events.drain().value().size() == spawned.value().count);

  // The logic loop: one transaction and one stepped frame per k, exact.
  NorthStarRun run;
  for (int k = 1; k <= kSteps; ++k) {
    INFO("k = " << k);
    gp::Transaction move;
    move.set(kBeacon, w::fields::WorldMatrix.column3, positionAt(k)).set(kBeacon, w::fields::Light.intensity, intensityAt(k));
    REQUIRE(world.submit(move).isOk());
    const auto report = attached.step();
    const auto* light = pointLightAt(report, positionAt(k));
    REQUIRE(light != nullptr);
    CHECK(light->intensity == intensityAt(k));
    run.positions.push_back(light->position);
    run.intensities.push_back(light->intensity);
    run.frames.push_back(report.frame);
    const auto moved = events.drain().value();
    REQUIRE(moved.size() == 2);
    CHECK(world.decode(moved[1], w::fields::Light.intensity).value() == std::optional<float>{intensityAt(k)});
  }

  // A refused transaction: one failure by ticket, nothing applied.
  gp::Transaction bad;
  bad.set(kBeacon, w::fields::Light.intensity, 9.0f)
      .set(kBeacon, w::fields::Light.color, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
  const auto refused = world.submit(bad);
  REQUIRE(refused.isOk());
  const auto unchanged = attached.step();
  failures.absorb(world.drainFailures());
  REQUIRE(failures.refusal(refused.value()).has_value());
  CHECK(failures.refusal(refused.value())->error == access::AccessError::NonFiniteValue);
  REQUIRE(pointLightAt(unchanged, positionAt(kSteps)) != nullptr);
  CHECK(pointLightAt(unchanged, positionAt(kSteps))->intensity == intensityAt(kSteps));
  CHECK(events.drain().value().empty());

  // Capture: the image differs from the baseline, by decoded pixels (J7).
  const fs::path beaconImage = dir / "beacon.png";
  (void)attached.step(beaconImage);
  const auto before = atlantis::image_regression::decodePng(baselineImage);
  const auto after = atlantis::image_regression::decodePng(beaconImage);
  REQUIRE(before.isOk());
  REQUIRE(after.isOk());
  REQUIRE(before.value().pixels.width == after.value().pixels.width);
  REQUIRE(before.value().pixels.height == after.value().pixels.height);
  std::size_t changed = 0;
  for (std::size_t i = 0; i < before.value().pixels.rgba8.size(); ++i) {
    if (before.value().pixels.rgba8[i] != after.value().pixels.rgba8[i]) ++changed;
  }
  INFO("changed channel values: " << changed);
  CHECK(changed > 0);

  // Destroy: the original lights again.
  gp::Transaction destroy;
  destroy.destroy(kBeacon);
  REQUIRE(world.submit(destroy).isOk());
  const auto last = attached.step();
  CHECK(last.data.pointLights == baseline.data.pointLights);
  CHECK(last.data.directionalLights == baseline.data.directionalLights);
  CHECK(events.drain().value().back() == access::Event{access::EntityDestroyed{kBeacon}});
  attached.session->control().resume();
  return run;
}

[[nodiscard]] fs::path freshDir(std::string_view name) {
  const fs::path dir = fs::temp_directory_path() / "atlantis_gameplay_gpu" /
                       (std::string(name) + "_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  return dir;
}

}  // namespace

// One windowed application per test process (a second one cannot be created
// in the same process: Platform does not re-initialize, the Spec 0056 M6
// finding), so reproducibility is shown by running the whole loop twice on
// one Runtime -- the beacon is destroyed at the end of each run and created
// again by the next -- with idle frames before the second run.
TEST_CASE("Gameplay SDK North star in process: the beacon loop, exact per logic step, reproducible",
          "[runtime][gpu][gameplay_sdk][north_star]") {
  const fs::path dir = freshDir("north_star");
  NorthStarRun firstRun;
  NorthStarRun secondRun;
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    Attached attached(app);
    for (int i = 0; i < 3; ++i) attached.frame();
    fs::create_directories(dir / "0");
    fs::create_directories(dir / "1");
    firstRun = runNorthStar(attached, dir / "0", 0);
    // The second run first idles a few frames: its Runtime frame numbers
    // differ, its logic-step values must not (ruling Q7).
    secondRun = runNorthStar(attached, dir / "1", 7);
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
  CHECK(secondRun.positions == firstRun.positions);
  CHECK(secondRun.intensities == firstRun.intensities);
  CHECK(secondRun.frames != firstRun.frames);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("Gameplay SDK: RemoteBatch and SequentialQueryBatch answer the same", "[runtime][gpu][gameplay_sdk][batch]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    Attached attached(app);
    for (int i = 0; i < 3; ++i) attached.frame();
    RemoteBatch remote(*attached.session);
    gp::SequentialQueryBatch sequential(attached.session->connection());
    gp::World viaRemote(attached.session->connection(), &remote);
    gp::World viaSequential(attached.session->connection(), &sequential);
    CHECK(viaRemote.entitiesWith({"WorldMatrix"}).value() == viaSequential.entitiesWith({"WorldMatrix"}).value());
    CHECK(viaRemote.entitiesWith<w::Light>().value() == viaSequential.entitiesWith<w::Light>().value());
    const auto a = viaRemote.readComponent(kSun, "Light");
    const auto b = viaSequential.readComponent(kSun, "Light");
    REQUIRE(a.isOk());
    REQUIRE(b.isOk());
    CHECK(a.value().leaves == b.value().leaves);
    const auto entities = viaRemote.entities();
    const auto remoteComponents = remote.listComponents(entities);
    const auto sequentialComponents = sequential.listComponents(entities);
    REQUIRE(remoteComponents.size() == sequentialComponents.size());
    for (std::size_t i = 0; i < entities.size(); ++i) CHECK(remoteComponents[i].value() == sequentialComponents[i].value());
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

// The four scenarios run in order on one Runtime (one windowed application
// per test process), each with its own target values for B's edit.
TEST_CASE("Gameplay SDK read isolation: pausing alone does not isolate a multi-leaf read (R10)",
          "[runtime][gpu][gameplay_sdk][isolation]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    Attached attached(app);
    for (int i = 0; i < 3; ++i) attached.frame();
    InterleavedBatch interleaved(attached.session->connection());
    gp::World clientA(attached.session->connection(), &interleaved);
    gp::World clientB(*attached.clientB);
    attached.session->control().pause();

    const auto lightId = atlantis::schema::typeId("world::Light");
    const auto colorField = atlantis::schema::fieldId("world::Light", "color");
    const auto intensityField = atlantis::schema::fieldId("world::Light", "intensity");
    // Client B's edit: colour (leaf 2) and intensity (leaf 3), one transaction.
    const auto editByB = [&](std::array<float, 3> color, float intensity) {
      gp::Transaction tx;
      tx.set(access::PropertyAddress{kSun, lightId, colorField}, color)
          .set(access::PropertyAddress{kSun, lightId, intensityField}, intensity);
      REQUIRE(clientB.submit(tx).isOk());
    };
    const auto stepByB = [&](std::uint32_t frames) {
      attached.control.step(atlantis::connection::StepRequest{frames, std::nullopt}, [](auto) {});
    };
    // Apply anything pending and let any running step finish; the World is
    // then stable again (still paused).
    const auto settle = [&] {
      stepByB(1);
      for (int i = 0; i < 14; ++i) attached.frame();
    };
    // A's one-at-a-time read, with B acting just before the third leaf
    // (intensity) is asked for.
    const auto readWith = [&](std::function<void()> beforeThirdLeaf) {
      interleaved.between = [&](std::size_t i) {
        if (i == 2) beforeThirdLeaf();
      };
      const w::Light read = clientA.read<w::Light>(kSun).value();
      interleaved.between = nullptr;
      return read;
    };
    // Mixed: neither the World before nor after B's edit -- the colour leaf
    // from before, the intensity leaf from after.
    const auto isMixed = [](const w::Light& read, const w::Light& before, float nextIntensity) {
      return read.color == before.color && read.intensity == nextIntensity && before.intensity != nextIntensity;
    };

    {
      INFO("B steps between A's leaves");
      const w::Light before = clientA.read<w::Light>(kSun).value();
      const w::Light read = readWith([&] {
        editByB({0.9f, 0.9f, 0.9f}, 4.0f);
        stepByB(1);
      });
      CHECK(isMixed(read, before, 4.0f));
      settle();
    }
    {
      INFO("B resumes between A's leaves");
      const w::Light before = clientA.read<w::Light>(kSun).value();
      const w::Light read = readWith([&] {
        editByB({0.8f, 0.8f, 0.8f}, 5.0f);
        attached.control.resume();
      });
      CHECK(isMixed(read, before, 5.0f));
      attached.control.pause();
      settle();
    }
    {
      INFO("a step requested before A's read is still pending as it starts");
      const w::Light before = clientA.read<w::Light>(kSun).value();
      stepByB(10);  // pending: its frames run during A's waits, still pending at the third leaf
      const w::Light read = readWith([&] { editByB({0.7f, 0.7f, 0.7f}, 6.0f); });  // no step or resume by B
      CHECK(isMixed(read, before, 6.0f));
      settle();
    }
    {
      INFO("paused, no step or resume from any client, no pending step: one consistent component");
      const w::Light before = clientA.read<w::Light>(kSun).value();
      const w::Light read = readWith([&] { editByB({0.6f, 0.6f, 0.6f}, 7.0f); });  // submitted, but held
      CHECK(read.color == before.color);
      CHECK(read.intensity == before.intensity);
      CHECK(read.kind == before.kind);
      CHECK(read.range == before.range);
      settle();  // B's held edit applies now: a later read sees all of it
      const w::Light later = clientA.read<w::Light>(kSun).value();
      CHECK(later.color == std::array<float, 3>{0.6f, 0.6f, 0.6f});
      CHECK(later.intensity == 7.0f);
    }
    attached.session->control().resume();
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

TEST_CASE("Gameplay SDK on Bistro: typed and reflective Light queries agree, and 6b63b12c 12 -> 24 exactly",
          "[runtime][gpu][gameplay_sdk][bistro]") {
#if !defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  SKIP("Bistro content is not present (the content-gated build step was not declared)");
#else
  BootstrapConfig config = buildSmokeConfig();
  config.sceneAsset = atlantis::asset_system::deriveAssetGuid(
      sceneGuidFromDefinition(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID), "scene");
  config.environmentArtifactPath.clear();
  config.environmentMetadataPath.clear();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    Attached attached(app);
    for (int i = 0; i < 3; ++i) attached.frame();
    RemoteBatch batch(*attached.session);
    gp::World world(attached.session->connection(), &batch);
    const auto typed = world.entitiesWith<w::Light>();
    const auto reflective = world.entitiesWith({"Light"});
    REQUIRE(typed.isOk());
    REQUIRE(reflective.isOk());
    CHECK(typed.value() == reflective.value());
    CHECK(typed.value().size() == 60);

    const auto lamp = atlantis::asset_system::parseEntityGuid("6b63b12c-9cde-4ae2-8391-c0b4cefadb7d").value();
    CHECK(world.get(lamp, w::fields::Light.intensity).value() == 12.0f);
    const auto at = world.get(lamp, w::fields::WorldMatrix.column3).value();
    attached.session->control().pause();
    REQUIRE(world.set(lamp, w::fields::Light.intensity, 24.0f).isOk());
    const auto report = attached.step();
    const auto* light = pointLightAt(report, at);
    REQUIRE(light != nullptr);
    CHECK(light->intensity == 24.0f);  // exactly
    attached.session->control().resume();
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
#endif
}
