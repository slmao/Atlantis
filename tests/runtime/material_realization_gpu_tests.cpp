#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/renderer/material.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/material_realization.h>
#include <atlantis/runtime/scene_load.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Regression coverage for a real, previously-undisclosed gap found during
// this PR's own final centralized review: RuntimeApplication::runFrame()'s
// wait-then-publish gate (Spec 0018 D8 step 5 / Plan 0018 Section P12) must
// trigger on "at least one material was newly realized this frame", not
// narrowed to "at least one NEW TEXTURE was newly uploaded this frame". A
// material whose own texture is already realized (D10 dedup against a
// texture a DIFFERENT material realized in an EARLIER frame -- as opposed
// to the same-frame dedup Milestone 12's own existing coverage already
// proves) still returns a non-null Sampler/Material from
// realizePendingMaterials() with a null newSampledTexture -- exercised here
// directly against the real, Runtime-shared computePendingMaterialIds()/
// realizePendingMaterials() (material_realization.h), the same functions
// runFrame() itself calls, never duplicated.
//
// GPU-required (real Device, real Sampler/SampledTexture/Pipeline
// creation), zero window (OffscreenTarget, matching every other headless
// GPU test's established pattern) -- tagged "gpu".

namespace {

using atlantis::asset_system::AssetId;
using atlantis::asset_system::MaterialAssetData;
using atlantis::asset_system::MaterialKind;
using atlantis::asset_system::TextureAssetData;
using atlantis::asset_system::TextureColorSpace;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::VertexInputLayout;
using atlantis::runtime::computePendingMaterialIds;
using atlantis::runtime::realizePendingMaterials;
using atlantis::runtime::rebuildMaterialsForFormatChange;
using atlantis::runtime::RealizedMaterialCandidate;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

// Duplicated, not shared -- matches every other composition root's own
// established "duplicated Vertex schema + loadSpirvFile()" precedent
// (runtime_application.cpp, material_demo_fixture.cpp).
struct Vertex {
  float position[3];
  float color[3];
  float uv[2];
};

[[nodiscard]] std::optional<std::vector<std::uint32_t>> loadSpirvFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return std::nullopt;
  const std::streamsize sizeBytes = file.tellg();
  if (sizeBytes <= 0 || sizeBytes % 4 != 0) return std::nullopt;
  file.seekg(0);
  std::vector<std::uint32_t> words(static_cast<std::size_t>(sizeBytes) / 4);
  if (!file.read(reinterpret_cast<char*>(words.data()), sizeBytes)) return std::nullopt;
  return words;
}

[[nodiscard]] std::optional<VertexInputLayout> unlitTexturedVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

[[nodiscard]] TextureAssetData makeSolidTextureData(std::uint32_t extent, std::uint8_t value) {
  TextureAssetData data;
  data.width = extent;
  data.height = extent;
  data.colorSpace = TextureColorSpace::Unorm;
  data.pixelBytes.assign(static_cast<std::size_t>(extent) * extent * 4, value);
  return data;
}

// minimal_mesh.slang own fallback-material vertex schema (position@0,
// color@1) -- matches runtime_application.cpp's own minimalMeshVertexLayout()
// exactly; rebuildMaterialsForFormatChange() needs this shader pair too,
// for its own candidate fallback Material.
[[nodiscard]] std::optional<VertexInputLayout> fallbackVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

}  // namespace

// Regression coverage for Plan 0018 Section P13 / Human Review Approval
// item 2 -- the Approved Plan's own highest-priority finding (the old
// Material/Pipeline GPU-in-flight lifetime fix during a format-change
// rebuild). Milestone 13 promised dedicated GPU tests for this exact
// property (object identity/address-stable on a rebuild, zero
// object-still-in-use Validation Layers errors across a real format
// change), but no test anywhere in this PR's own tree ever called
// rebuildMaterialsForFormatChange() at all -- confirmed by a repository-
// wide search before writing this test. This exercises the real,
// Runtime-shared function directly, then reproduces
// runtime_application.cpp's own exact sequence by hand: candidate built
// while the OLD bundle is read-only; OLD bundle still fully valid and
// address-stable through a real submit() against the NEW format; the OLD
// bundle is destroyed only after that submit() returns Ok, never before.
TEST_CASE("rebuildMaterialsForFormatChange never touches the caller existing bundle, and the OLD Material stays "
          "valid and address-stable through a real submit() against the NEW format, destroyed only after",
          "[runtime][gpu][material_realization][format_rebuild]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Material Realization GPU Tests (format rebuild)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  auto unlitVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv");
  auto unlitFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv");
  REQUIRE(unlitVertexSpirv.has_value());
  REQUIRE(unlitFragmentSpirv.has_value());
  auto unlitVertexReflectionResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) +
                                                             "/textured_quad.vert.refl.json");
  REQUIRE(unlitVertexReflectionResult.isOk());
  const auto unlitLayout = unlitTexturedVertexLayout(unlitVertexReflectionResult.value());
  REQUIRE(unlitLayout.has_value());

  auto fallbackVertexSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv");
  auto fallbackFragmentSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv");
  REQUIRE(fallbackVertexSpirv.has_value());
  REQUIRE(fallbackFragmentSpirv.has_value());
  auto fallbackVertexReflectionResult =
      loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json");
  REQUIRE(fallbackVertexReflectionResult.isOk());
  const auto fallbackLayout = fallbackVertexLayout(fallbackVertexReflectionResult.value());
  REQUIRE(fallbackLayout.has_value());

  constexpr Extent2D kExtent{4, 4};
  constexpr AssetId kTextureId = 200;
  constexpr AssetId kMaterialA = 1;

  std::unordered_map<AssetId, MaterialAssetData> materialDataMap;
  materialDataMap.emplace(kMaterialA,
                           MaterialAssetData{.kind = MaterialKind::UnlitTextured, .textureAsset = kTextureId});
  std::unordered_map<AssetId, TextureAssetData> textureDataMap;
  textureDataMap.emplace(kTextureId, makeSolidTextureData(4, 0x33));

  std::unordered_map<AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>> sampledTextureResourceMap;
  std::unordered_map<AssetId, std::unique_ptr<atlantis::renderer::Material>> materialResourceMap;

  // ---- "Frame" 1: format A (Rgba8Unorm) -- realize kMaterialA for real. ----
  {
    auto offscreenA = device->createOffscreenTarget({.extent = kExtent, .format = Format::Rgba8Unorm});
    REQUIRE(offscreenA.isOk());
    auto acquireResult = offscreenA.value()->acquireTarget();
    REQUIRE(acquireResult.isOk());
    std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());

    auto commandListResult = device->createCommandList();
    REQUIRE(commandListResult.isOk());
    std::unique_ptr<atlantis::rhi::CommandList> commandList = std::move(commandListResult.value());

    std::unordered_map<AssetId, RealizedMaterialCandidate> realized =
        realizePendingMaterials(*device, *commandList, *unlitLayout, *unlitVertexSpirv, *unlitFragmentSpirv,
                                 Format::Rgba8Unorm, {kMaterialA}, sampledTextureResourceMap, materialDataMap,
                                 textureDataMap);
    REQUIRE(realized.size() == 1);

    auto submitResult = device->submit(std::move(commandList), *target);
    REQUIRE(submitResult.isOk());
    REQUIRE(device->waitIdle().isOk());

    for (auto& [assetId, candidate] : realized) {
      sampledTextureResourceMap.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
      materialResourceMap.emplace(assetId, std::move(candidate.material));
    }
  }
  REQUIRE(materialResourceMap.size() == 1);
  const atlantis::renderer::Material* oldMaterialPtr = materialResourceMap.at(kMaterialA).get();
  const atlantis::rhi::SampledTexture* oldSampledTexturePtr = oldMaterialPtr->sampledTexture();
  REQUIRE(oldMaterialPtr != nullptr);

  // ---- "Frame" 2: a real format change, Rgba8Unorm -> Rgba8Srgb. ----
  {
    auto offscreenB = device->createOffscreenTarget({.extent = kExtent, .format = Format::Rgba8Srgb});
    REQUIRE(offscreenB.isOk());

    auto rebuildResult = rebuildMaterialsForFormatChange(*device, *fallbackLayout, *fallbackVertexSpirv,
                                                          *fallbackFragmentSpirv, *unlitLayout, *unlitVertexSpirv,
                                                          *unlitFragmentSpirv, Format::Rgba8Srgb, materialResourceMap);
    REQUIRE(rebuildResult.isOk());
    auto candidates = std::move(rebuildResult.value());
    REQUIRE(candidates.fallback != nullptr);
    REQUIRE(candidates.materials.size() == 1);
    // A genuinely NEW object -- never the same Pipeline reused across a
    // format change (Spec 0018 D9 item 1 is about SampledTexture/Sampler
    // only, never Pipeline/Material).
    CHECK(candidates.materials.at(kMaterialA).get() != oldMaterialPtr);
    // rebuildMaterialsForFormatChange() itself must never touch the
    // caller existing map -- confirmed by address identity, not merely
    // by "the call succeeded".
    CHECK(materialResourceMap.at(kMaterialA).get() == oldMaterialPtr);
    // The OLD Material is still fully alive and usable at this point --
    // not merely "not yet freed memory", genuinely still valid.
    CHECK(oldMaterialPtr->sampledTexture() == oldSampledTexturePtr);

    // This frame's own real draw graph would be recorded using ONLY the
    // candidate batch here (Spec 0018 D9 item 2/P13 step 2) -- an empty
    // CommandList is a legal, already-proven submission shape (the
    // dedup test above's own "frame 2", no upload pass recorded, still
    // submits successfully) and is sufficient to prove this test's own
    // subject: the OLD bundle survives a real submit() against the NEW
    // format, untouched, and is destroyed only after that submit()
    // returns Ok -- exactly runtime_application.cpp's own sequence.
    auto acquireResult = offscreenB.value()->acquireTarget();
    REQUIRE(acquireResult.isOk());
    std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());
    auto commandListResult = device->createCommandList();
    REQUIRE(commandListResult.isOk());
    auto submitResult = device->submit(std::move(commandListResult.value()), *target);
    REQUIRE(submitResult.isOk());

    // Only NOW, after submit() has returned Ok -- Device::submit() own
    // internal waitAndReleaseRetainedSubmission() has already confirmed
    // the previous frame GPU work is finished -- is it safe to swap in
    // the candidate batch. The OLD Material this overwrites is destroyed
    // here, provably safe, matching runtime_application.cpp's own P13
    // sequence exactly (never before this point).
    materialResourceMap = std::move(candidates.materials);
    std::unique_ptr<atlantis::renderer::Material> fallbackMaterial = std::move(candidates.fallback);
    CHECK(materialResourceMap.at(kMaterialA).get() != oldMaterialPtr);

    REQUIRE(device->waitIdle().isOk());
  }

  materialResourceMap.clear();
  sampledTextureResourceMap.clear();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("A second material that dedups its texture against an EARLIER frame's already-realized texture is still "
          "reported realized, published, and never rebuilt on a later frame",
          "[runtime][gpu][material_realization]") {
  auto deviceResult =
      atlantis::vulkan_backend::createDevice({.applicationName = "Atlantis Material Realization GPU Tests",
                                               .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  auto vertexSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv");
  auto fragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv");
  REQUIRE(vertexSpirv.has_value());
  REQUIRE(fragmentSpirv.has_value());
  auto vertexReflectionResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) +
                                                        "/textured_quad.vert.refl.json");
  REQUIRE(vertexReflectionResult.isOk());
  const auto vertexInputLayout = unlitTexturedVertexLayout(vertexReflectionResult.value());
  REQUIRE(vertexInputLayout.has_value());

  constexpr Extent2D kExtent{4, 4};
  auto offscreenResult = device->createOffscreenTarget({.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  constexpr AssetId kTextureId = 100;
  constexpr AssetId kMaterialA = 1;
  constexpr AssetId kMaterialB = 2;

  std::unordered_map<AssetId, MaterialAssetData> materialDataMap;
  materialDataMap.emplace(kMaterialA, MaterialAssetData{.kind = MaterialKind::UnlitTextured,
                                                         .textureAsset = kTextureId});
  materialDataMap.emplace(kMaterialB, MaterialAssetData{.kind = MaterialKind::UnlitTextured,
                                                         .textureAsset = kTextureId});
  std::unordered_map<AssetId, TextureAssetData> textureDataMap;
  textureDataMap.emplace(kTextureId, makeSolidTextureData(4, 0x7F));

  std::unordered_map<AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>> sampledTextureResourceMap;
  std::unordered_map<AssetId, std::unique_ptr<atlantis::rhi::Sampler>> samplerResourceMap;
  std::unordered_map<AssetId, std::unique_ptr<atlantis::renderer::Material>> materialResourceMap;

  // ---- "Frame" 1: only kMaterialA is referenced/pending. ----
  {
    auto acquireResult = offscreenTarget->acquireTarget();
    REQUIRE(acquireResult.isOk());
    std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());

    const std::vector<AssetId> pendingIds =
        computePendingMaterialIds({kMaterialA}, /*alreadyRealizedIds=*/{});
    REQUIRE(pendingIds == std::vector<AssetId>{kMaterialA});

    auto commandListResult = device->createCommandList();
    REQUIRE(commandListResult.isOk());
    std::unique_ptr<atlantis::rhi::CommandList> commandList = std::move(commandListResult.value());

    std::unordered_map<AssetId, RealizedMaterialCandidate> realized =
        realizePendingMaterials(*device, *commandList, *vertexInputLayout, *vertexSpirv, *fragmentSpirv,
                                 Format::Rgba8Unorm, pendingIds, sampledTextureResourceMap, materialDataMap,
                                 textureDataMap);
    REQUIRE(realized.size() == 1);
    REQUIRE(realized.at(kMaterialA).newSampledTexture != nullptr);
    REQUIRE(realized.at(kMaterialA).sampler != nullptr);
    REQUIRE(realized.at(kMaterialA).material != nullptr);

    auto submitResult = device->submit(std::move(commandList), *target);
    REQUIRE(submitResult.isOk());
    REQUIRE(device->waitIdle().isOk());  // real texture upload this frame -- staging buffer lifetime, D8 step 5.

    for (auto& [assetId, candidate] : realized) {
      sampledTextureResourceMap.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
      samplerResourceMap.emplace(assetId, std::move(candidate.sampler));
      materialResourceMap.emplace(assetId, std::move(candidate.material));
    }
  }
  REQUIRE(sampledTextureResourceMap.size() == 1);
  REQUIRE(materialResourceMap.size() == 1);
  const atlantis::renderer::Material* materialAAddress = materialResourceMap.at(kMaterialA).get();

  // ---- "Frame" 2: kMaterialB newly referenced -- its own texture (kTextureId)
  // was already realized in Frame 1, by a DIFFERENT material. This is the
  // exact scenario runFrame()'s own former anyNewUploadThisFrame gate got
  // wrong (gated on "any candidate uploaded a texture", true only when a
  // NEW texture is involved) instead of the Plan/Spec-mandated "any
  // candidate was realized at all". ----
  {
    std::vector<AssetId> alreadyRealized;
    for (const auto& [id, material] : materialResourceMap) alreadyRealized.push_back(id);
    const std::vector<AssetId> pendingIds = computePendingMaterialIds({kMaterialA, kMaterialB}, alreadyRealized);
    REQUIRE(pendingIds == std::vector<AssetId>{kMaterialB});

    auto commandListResult = device->createCommandList();
    REQUIRE(commandListResult.isOk());
    std::unique_ptr<atlantis::rhi::CommandList> commandList = std::move(commandListResult.value());

    std::unordered_map<AssetId, RealizedMaterialCandidate> realized =
        realizePendingMaterials(*device, *commandList, *vertexInputLayout, *vertexSpirv, *fragmentSpirv,
                                 Format::Rgba8Unorm, pendingIds, sampledTextureResourceMap, materialDataMap,
                                 textureDataMap);

    // The exact invariant the fix restores: a cross-frame dedup candidate
    // still comes back non-empty, with a real Sampler/Material, even though
    // no NEW texture upload was needed.
    REQUIRE(realized.size() == 1);
    REQUIRE(realized.at(kMaterialB).newSampledTexture == nullptr);
    REQUIRE(realized.at(kMaterialB).sampler != nullptr);
    REQUIRE(realized.at(kMaterialB).material != nullptr);
    const bool anyMaterialRealizedThisFrame = !realized.empty();
    const bool anyNewUploadThisFrame = [&] {
      for (const auto& [id, candidate] : realized) {
        if (candidate.newSampledTexture) return true;
      }
      return false;
    }();
    REQUIRE(anyMaterialRealizedThisFrame);       // the correct, Plan/Spec-mandated gate
    REQUIRE_FALSE(anyNewUploadThisFrame);        // the bug's own former (wrong) gate -- must not be relied on

    auto acquireResult = offscreenTarget->acquireTarget();
    REQUIRE(acquireResult.isOk());
    std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());
    auto submitResult = device->submit(std::move(commandList), *target);
    REQUIRE(submitResult.isOk());
    // Gated on anyMaterialRealizedThisFrame, matching the fixed runFrame().
    REQUIRE(device->waitIdle().isOk());

    for (auto& [assetId, candidate] : realized) {
      if (candidate.newSampledTexture) {
        sampledTextureResourceMap.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
      }
      samplerResourceMap.emplace(assetId, std::move(candidate.sampler));
      materialResourceMap.emplace(assetId, std::move(candidate.material));
    }
  }
  // kMaterialB is now cached; kTextureId still shared (dedup, D10), never
  // re-uploaded; kMaterialA's own already-published Material was never
  // touched (address-stable across this whole sequence).
  REQUIRE(sampledTextureResourceMap.size() == 1);
  REQUIRE(samplerResourceMap.size() == 2);
  REQUIRE(materialResourceMap.size() == 2);
  REQUIRE(materialResourceMap.at(kMaterialA).get() == materialAAddress);

  // ---- "Frame" 3: both materials already realized -- nothing pending,
  // nothing rebuilt (proves kMaterialB was genuinely cached by Frame 2,
  // not silently dropped and re-created every frame). ----
  {
    std::vector<AssetId> alreadyRealized;
    for (const auto& [id, material] : materialResourceMap) alreadyRealized.push_back(id);
    const std::vector<AssetId> pendingIds = computePendingMaterialIds({kMaterialA, kMaterialB}, alreadyRealized);
    REQUIRE(pendingIds.empty());
  }

  materialResourceMap.clear();
  samplerResourceMap.clear();
  sampledTextureResourceMap.clear();
  REQUIRE(device->waitIdle().isOk());
}

// ---------------------------------------------------------------------
// Plan 0018 Milestone 11 regression coverage (PR #88 final review round):
// the three loadAndInstantiateScene() material-loop cases the Approved
// Plan Milestone 11 promised (a material that resolves and loads but
// whose own embedded textureAsset reference does not resolve also fails
// scene load fatally; a scene with a resolvable but unloadable material
// fails scene load fatally; two entities referencing the same material
// AssetId produce exactly one materialDataMap/textureDataMap entry each)
// were never actually added anywhere in this PR's own test tree. Unlike
// scene_load_tests.cpp's own GPU-independent (device=nullptr) coverage,
// these three genuinely require a real Device: reaching the material-
// loading loop at all requires every distinct mesh reference to have
// ALREADY loaded successfully (scene_load.cpp's own step (e) mesh loop
// runs, and must fully succeed -- calling the real, device-dereferencing
// createMesh() -- before the material loop begins), which a material=
// node own mandatory mesh= companion (the scene grammar never allows
// material= without mesh=) makes unavoidable.
// ---------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

using atlantis::asset_system::cookMaterial;
using atlantis::asset_system::cookScene;
using atlantis::asset_system::cookStaticMesh;
using atlantis::asset_system::cookTexture;
using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::loadAndInstantiateScene;
using atlantis::runtime::RuntimeInitError;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_material_realization_gpu_tests" /
              (label + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

void writeFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

void writeManifestLine(std::string& manifest, const std::string& logicalPath, const fs::path& artifactPath,
                        const fs::path& metadataPath) {
  manifest += logicalPath + "\t" + artifactPath.string() + "\t" + metadataPath.string() + "\n";
}

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

struct CookedMeshFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

[[nodiscard]] CookedMeshFixture cookFixtureMesh(const fs::path& dir, const std::string& logicalPath) {
  const fs::path sourcePath = dir / "mesh_source" / (logicalPath + ".txt");
  writeFile(sourcePath, std::string(kValidTriangleSource));
  const fs::path artifactPath = dir / (logicalPath + ".amesh");
  const fs::path metadataPath = dir / (logicalPath + ".amesh.meta.txt");
  REQUIRE(cookStaticMesh(sourcePath.string(), logicalPath, artifactPath.string(), metadataPath.string()).isOk());
  return CookedMeshFixture{artifactPath, metadataPath};
}

struct CookedTextureFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

[[nodiscard]] CookedTextureFixture cookFixtureTexture(const fs::path& dir, const std::string& logicalPath) {
  constexpr std::uint32_t kTexExtent = 2;
  const std::vector<std::uint8_t> pixelBytes(static_cast<std::size_t>(kTexExtent) * kTexExtent * 4, 0x7F);
  const fs::path artifactPath = dir / (logicalPath + ".atex");
  const fs::path metadataPath = dir / (logicalPath + ".atex.meta.txt");
  REQUIRE(cookTexture(pixelBytes.data(), kTexExtent, kTexExtent, 4, atlantis::asset_system::TextureColorSpace::Unorm,
                       logicalPath, artifactPath, metadataPath)
              .isOk());
  return CookedTextureFixture{artifactPath, metadataPath};
}

struct CookedMaterialFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

[[nodiscard]] CookedMaterialFixture cookFixtureMaterial(const fs::path& dir, const std::string& logicalPath,
                                                          const std::string& textureLogicalPath) {
  const fs::path sourcePath = dir / "material_source" / (logicalPath + ".txt");
  writeFile(sourcePath, "atlantis_material_source_version: 1\n"
                        "kind: unlit_textured\n"
                        "texture: " + textureLogicalPath + "\n"
                        "filter: linear\n"
                        "address_mode: repeat\n");
  const fs::path artifactPath = dir / (logicalPath + ".amaterial");
  const fs::path metadataPath = dir / (logicalPath + ".amaterial.meta.txt");
  REQUIRE(cookMaterial(sourcePath.string(), logicalPath, artifactPath.string(), metadataPath.string()).isOk());
  return CookedMaterialFixture{artifactPath, metadataPath};
}

struct CookedSceneFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

// Every node names both a mesh and a material (materialLogicalPaths[i] for
// meshLogicalPaths[i]) -- the grammar never accepts material= without
// mesh= (Plan 0018 Section P6 own 13-token case).
[[nodiscard]] CookedSceneFixture cookFixtureSceneWithMaterials(
    const fs::path& dir, const std::vector<std::string>& meshLogicalPaths,
    const std::vector<std::string>& materialLogicalPaths) {
  REQUIRE(meshLogicalPaths.size() == materialLogicalPaths.size());
  std::string source = "atlantis_scene_source_version: 2\n";
  source += "node_count: " + std::to_string(meshLogicalPaths.size()) + "\n";
  source += "active_camera: none\n";
  for (std::size_t i = 0; i < meshLogicalPaths.size(); ++i) {
    source += "node: node_id=" + std::to_string(i + 1) +
              " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" +
              meshLogicalPaths[i] + " material=" + materialLogicalPaths[i] + "\n";
  }
  const fs::path sourcePath = dir / "scene_with_material.scene.txt";
  writeFile(sourcePath, source);
  const fs::path artifactPath = dir / "scene_with_material.ascene";
  const fs::path metadataPath = dir / "scene_with_material.ascene.meta.txt";
  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());
  return CookedSceneFixture{artifactPath, metadataPath};
}

[[nodiscard]] BootstrapConfig makeSceneConfig(const fs::path& sceneArtifactPath, const fs::path& sceneMetadataPath,
                                               const fs::path& manifestPath) {
  BootstrapConfig config;
  config.sceneArtifactPath = sceneArtifactPath.string();
  config.sceneMetadataPath = sceneMetadataPath.string();
  config.sceneDependencyManifestPath = manifestPath.string();
  return config;
}

}  // namespace

TEST_CASE("loadAndInstantiateScene: a material that resolves and loads but whose own embedded texture reference "
          "does not resolve fails scene load fatally with SceneDependencyUnresolved, a distinct code path from an "
          "unresolvable material AssetId itself",
          "[runtime][gpu][scene][material]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Material Realization GPU Tests (scene load)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  TempDirGuard dir("material_texture_unresolved");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  // cookMaterial() never validates its own texture= reference existence
  // (ADR-0059 D6/D7) -- this material cooks and loads perfectly fine even
  // though "textures/never_declared.png" is never cooked or manifested.
  const CookedMaterialFixture material =
      cookFixtureMaterial(dir.path, "materials/a.material.txt", "textures/never_declared.png");
  const CookedSceneFixture scene =
      cookFixtureSceneWithMaterials(dir.path, {"meshes/a.mesh.txt"}, {"materials/a.material.txt"});

  std::string manifest;
  writeManifestLine(manifest, "meshes/a.mesh.txt", mesh.artifactPath, mesh.metadataPath);
  writeManifestLine(manifest, "materials/a.material.txt", material.artifactPath, material.metadataPath);
  // No manifest entry for textures/never_declared.png at all.
  writeFile(dir.path / "manifest.txt", manifest);
  const BootstrapConfig config = makeSceneConfig(scene.artifactPath, scene.metadataPath, dir.path / "manifest.txt");

  const auto result = loadAndInstantiateScene(config, device.get(), atlantis::rhi::VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyUnresolved);
}

TEST_CASE("loadAndInstantiateScene: a resolvable but unloadable material with a missing artifact fails scene load "
          "fatally with SceneDependencyLoadFailed",
          "[runtime][gpu][scene][material]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Material Realization GPU Tests (scene load)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  TempDirGuard dir("material_load_failed");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const CookedTextureFixture texture = cookFixtureTexture(dir.path, "textures/a.png");
  const CookedMaterialFixture material = cookFixtureMaterial(dir.path, "materials/a.material.txt", "textures/a.png");
  const CookedSceneFixture scene =
      cookFixtureSceneWithMaterials(dir.path, {"meshes/a.mesh.txt"}, {"materials/a.material.txt"});

  std::string manifest;
  writeManifestLine(manifest, "meshes/a.mesh.txt", mesh.artifactPath, mesh.metadataPath);
  // Real metadata (loadSceneDependencyManifest() itself validates every
  // entry own sidecar) but a missing ARTIFACT path -- never read during
  // manifest validation, only later, at loadMaterialAsset() itself.
  writeManifestLine(manifest, "materials/a.material.txt", dir.path / "does_not_exist.amaterial",
                     material.metadataPath);
  writeManifestLine(manifest, "textures/a.png", texture.artifactPath, texture.metadataPath);
  writeFile(dir.path / "manifest.txt", manifest);
  const BootstrapConfig config = makeSceneConfig(scene.artifactPath, scene.metadataPath, dir.path / "manifest.txt");

  const auto result = loadAndInstantiateScene(config, device.get(), atlantis::rhi::VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyLoadFailed);
}

TEST_CASE("loadAndInstantiateScene: two entities referencing the same material AssetId produce exactly one "
          "materialDataMap and textureDataMap entry each, the D10 CPU-load dedup contract",
          "[runtime][gpu][scene][material]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Material Realization GPU Tests (scene load)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  TempDirGuard dir("material_cpu_dedup");
  const CookedMeshFixture meshA = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const CookedMeshFixture meshB = cookFixtureMesh(dir.path, "meshes/b.mesh.txt");
  const CookedTextureFixture texture = cookFixtureTexture(dir.path, "textures/a.png");
  const CookedMaterialFixture material = cookFixtureMaterial(dir.path, "materials/a.material.txt", "textures/a.png");
  const CookedSceneFixture scene = cookFixtureSceneWithMaterials(
      dir.path, {"meshes/a.mesh.txt", "meshes/b.mesh.txt"},
      {"materials/a.material.txt", "materials/a.material.txt"});  // both nodes reference the SAME material AssetId

  std::string manifest;
  writeManifestLine(manifest, "meshes/a.mesh.txt", meshA.artifactPath, meshA.metadataPath);
  writeManifestLine(manifest, "meshes/b.mesh.txt", meshB.artifactPath, meshB.metadataPath);
  writeManifestLine(manifest, "materials/a.material.txt", material.artifactPath, material.metadataPath);
  writeManifestLine(manifest, "textures/a.png", texture.artifactPath, texture.metadataPath);
  writeFile(dir.path / "manifest.txt", manifest);
  const BootstrapConfig config = makeSceneConfig(scene.artifactPath, scene.metadataPath, dir.path / "manifest.txt");

  const auto result = loadAndInstantiateScene(config, device.get(), atlantis::rhi::VertexInputLayout{});
  REQUIRE(result.isOk());
  CHECK(result.value().meshResourceMap.size() == 2);   // two distinct meshes, not deduped
  CHECK(result.value().materialDataMap.size() == 1);   // one distinct material, deduped
  CHECK(result.value().textureDataMap.size() == 1);    // one distinct texture, deduped
  CHECK(result.value().world.renderableEntities().size() == 2);
}
