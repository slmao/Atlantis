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
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/material_realization.h>
#include <atlantis/runtime/scene_load.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "../../src/vulkan_backend/src/vulkan_descriptor_pool_growth.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
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
using atlantis::runtime::RealizedMaterialCandidate;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

// Duplicated, not shared -- matches every other composition root's own
// established "duplicated Vertex schema + loadSpirvFile()" precedent
// (runtime_application.cpp, material_demo_fixture.cpp). Plan 0019
// Section P6: normal appended (matching the real mesh v3 44-byte
// stride) so litTexturedVertexLayout() below has a real offset to name
// -- this file never actually uploads real vertex data through these
// layouts (it tests Material/Pipeline construction, not drawing).
struct Vertex {
  float position[3];
  float color[3];
  float uv[2];
  float normal[3];
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

// Plan 0019 Section P6: realizePendingMaterials()'s own widened
// signature requires a real litTextured* trio at every call site, even
// here, where no test in this file ever realizes a
// MaterialKind::LitTextured material.
[[nodiscard]] std::optional<VertexInputLayout> litTexturedVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0023 Milestone 5: realizePendingMaterials()'s own further-
// widened signature requires a real pbrDirectLit* trio at every call
// site, even here, where no test in this file ever realizes a
// MaterialKind::PbrDirectLit material. Byte-identical schema to
// litTexturedVertexLayout() above (pbr_direct_lit.slang's own vertex
// input matches lit_textured.slang's exactly, Milestone 4).
[[nodiscard]] std::optional<VertexInputLayout> pbrDirectLitVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
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
// exactly.
[[nodiscard]] std::optional<VertexInputLayout> fallbackVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

[[nodiscard]] std::optional<VertexInputLayout> outputTransformVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(float) * 2);
  if (result.isErr()) return std::nullopt;
  return result.value();
}

}  // namespace

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

  auto litVertexSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv");
  auto litFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv");
  REQUIRE(litVertexSpirv.has_value());
  REQUIRE(litFragmentSpirv.has_value());
  auto litVertexReflectionResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) +
                                                           "/lit_textured.vert.refl.json");
  REQUIRE(litVertexReflectionResult.isOk());
  const auto litLayout = litTexturedVertexLayout(litVertexReflectionResult.value());
  REQUIRE(litLayout.has_value());

  // Plan 0023 Milestone 5: the fourth, MaterialKind::PbrDirectLit
  // built-in shader pair -- material_realization.h's own further-widened
  // realizePendingMaterials() call needs a real pbrDirectLit* trio too,
  // even where this TEST_CASE never realizes one.
  auto pbrVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv");
  auto pbrFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv");
  REQUIRE(pbrVertexSpirv.has_value());
  REQUIRE(pbrFragmentSpirv.has_value());
  auto pbrVertexReflectionResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) +
                                                           "/pbr_direct_lit.vert.refl.json");
  REQUIRE(pbrVertexReflectionResult.isOk());
  const auto pbrLayout = pbrDirectLitVertexLayout(pbrVertexReflectionResult.value());
  REQUIRE(pbrLayout.has_value());

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
                                 *litLayout, *litVertexSpirv, *litFragmentSpirv, *pbrLayout, *pbrVertexSpirv, *pbrFragmentSpirv, pendingIds,
                                 sampledTextureResourceMap, materialDataMap, textureDataMap);
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
                                 *litLayout, *litVertexSpirv, *litFragmentSpirv, *pbrLayout, *pbrVertexSpirv, *pbrFragmentSpirv, pendingIds,
                                 sampledTextureResourceMap, materialDataMap, textureDataMap);

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
  writeFile(sourcePath, "atlantis_material_source_version: 2\n"
                        "kind: unlit_textured\n"
                        "texture: " + textureLogicalPath + "\n"
                        "filter: linear\n"
                        "address_mode: repeat\n");
  const fs::path artifactPath = dir / (logicalPath + ".amaterial");
  const fs::path metadataPath = dir / (logicalPath + ".amaterial.meta.txt");
  REQUIRE(cookMaterial(sourcePath.string(), logicalPath, artifactPath.string(), metadataPath.string()).isOk());
  return CookedMaterialFixture{artifactPath, metadataPath};
}

// Plan 0023 Milestone 7 (ADR-0066 item 6): mirrors cookFixtureMaterial()
// above exactly, except kind: pbr_direct_lit and the full 8-line PBR
// grammar (Milestone 1) -- needed by the new PbrBaseColorTextureNotSrgb
// rejection test below, which requires a PbrDirectLit-kind material
// whose own resolved texture is deliberately Unorm, not Srgb.
[[nodiscard]] CookedMaterialFixture cookFixturePbrMaterial(const fs::path& dir, const std::string& logicalPath,
                                                            const std::string& textureLogicalPath) {
  const fs::path sourcePath = dir / "material_source" / (logicalPath + ".txt");
  writeFile(sourcePath, "atlantis_material_source_version: 2\n"
                        "kind: pbr_direct_lit\n"
                        "texture: " + textureLogicalPath + "\n"
                        "filter: linear\n"
                        "address_mode: repeat\n"
                        "base_color_factor: 1.0 1.0 1.0 1.0\n"
                        "metallic_factor: 1.0\n"
                        "roughness_factor: 0.5\n");
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
  std::string source = "atlantis_scene_source_version: 3\n";
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

TEST_CASE("loadAndInstantiateScene: a PbrDirectLit material whose own resolved base-color texture is Unorm, not "
          "Srgb, fails scene load fatally with PbrBaseColorTextureNotSrgb (ADR-0066 item 6)",
          "[runtime][gpu][scene][material][pbr]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Material Realization GPU Tests (PBR sRGB rejection)",
       .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  TempDirGuard dir("pbr_base_color_not_srgb");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  // cookFixtureTexture() cooks an Unorm texture (its own fixed, existing
  // convention) -- deliberately the wrong color space for a
  // PbrDirectLit material's own base-color texture (ADR-0066 item 6).
  const CookedTextureFixture texture = cookFixtureTexture(dir.path, "textures/a.png");
  const CookedMaterialFixture material =
      cookFixturePbrMaterial(dir.path, "materials/a.material.txt", "textures/a.png");
  const CookedSceneFixture scene =
      cookFixtureSceneWithMaterials(dir.path, {"meshes/a.mesh.txt"}, {"materials/a.material.txt"});

  std::string manifest;
  writeManifestLine(manifest, "meshes/a.mesh.txt", mesh.artifactPath, mesh.metadataPath);
  writeManifestLine(manifest, "materials/a.material.txt", material.artifactPath, material.metadataPath);
  writeManifestLine(manifest, "textures/a.png", texture.artifactPath, texture.metadataPath);
  writeFile(dir.path / "manifest.txt", manifest);
  const BootstrapConfig config = makeSceneConfig(scene.artifactPath, scene.metadataPath, dir.path / "manifest.txt");

  const auto result = loadAndInstantiateScene(config, device.get(), atlantis::rhi::VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::PbrBaseColorTextureNotSrgb);
}

// ---------------------------------------------------------------------
// A real, pre-existing, PRE-Plan-0019 architectural ceiling -- FIXED by
// Spec 0021/ADR-0062/Plan 0021 (Descriptor Pool Capacity Foundation).
// Originally found and disclosed, not fixed, during Spec 0019's own
// final centralized review (see plans/0019-lighting-foundation.md's own
// "Implementation Status Update" for that original finding).
//
// Root cause, as originally traced: VulkanDevice's own VkDescriptorPool
// was a single, Device-global, fixed-capacity pool -- maxSets = 4 --
// created exactly once and never resized, derived from Plan 0007
// Section 10's own now-stale "exactly one Material exists in steady
// state" assumption.
//
// Fixed by Spec 0021/ADR-0064: VulkanDevice now owns a growable set of
// descriptor pools (a fixed std::array<DescriptorPoolEntry, 4>, never a
// std::vector), scanning every existing pool in creation order before
// growing (geometric doubling: 4, 8, 16, 32 -- 60 concurrent descriptor
// sets total) on real, observed VK_ERROR_OUT_OF_POOL_MEMORY/
// VK_ERROR_FRAGMENTED_POOL exhaustion. No RHI/Renderer/Material public
// API changed.
//
// Plan 0024 Milestone 6 (correction, ADR-0068 D-4): this TEST_CASE
// used to have a real-shape "Part 2" reconstructing the OLD 2*(N+1)
// format-change-window concurrent-descriptor-set scenario via
// rebuildMaterialsForFormatChange() -- retired along with that function
// (material_realization.h/.cpp's own comment there has the full
// reasoning): every geometry Pipeline now targets the fixed
// HdrFormat::Rgba16Float unconditionally and never participates in
// format-change rebuild at all, so there is no longer a "2*(N+1)
// concurrent old+new material batch" scenario to reconstruct. Only the
// low-level, kind-independent growth-mechanics probe below survives --
// see material_realization_gpu_tests.cpp's own new N=6 TEST_CASE
// (Milestone 8) for the real re-confirmation of D-4's own N+2/N+3
// formula against this exact growable pool mechanism.
// Plan 0026 Milestone 6: this TEST_CASE models the NO-ENVIRONMENT case --
// no sky Pipeline is ever created in this scenario, so its own N+2/N+3
// derivation remains exactly correct as written. See the sibling
// "N=6 HDR pipeline descriptor-set peak is exactly N+4 with an
// environment/sky Pipeline present" TEST_CASE below for the
// environment-enabled re-derivation.
TEST_CASE("N=6 HDR pipeline descriptor-set peak is exactly N+3 and succeeds against the real 60-set ceiling",
          "[runtime][gpu][material_realization][descriptor_pool_growth][hdr]") {
  using atlantis::vulkan_backend::detail::kDescriptorPoolMaxSetsByGeneration;

  constexpr std::size_t kMaterialPipelineCount = 6;
  constexpr std::size_t kFallbackPipelineCount = 1;
  constexpr std::size_t kSteadyOutputTransformPipelineCount = 1;
  constexpr std::size_t kTransientOutputTransformPipelineCount = 1;
  constexpr std::size_t kExpectedSteadySetCount = kMaterialPipelineCount + kFallbackPipelineCount +
                                                   kSteadyOutputTransformPipelineCount;
  constexpr std::size_t kExpectedPeakSetCount =
      kExpectedSteadySetCount + kTransientOutputTransformPipelineCount;
  static_assert(kExpectedSteadySetCount == 8);  // N + 2
  static_assert(kExpectedPeakSetCount == 9);    // N + 3

  const std::size_t totalDescriptorSetCapacity =
      std::accumulate(kDescriptorPoolMaxSetsByGeneration.begin(), kDescriptorPoolMaxSetsByGeneration.end(),
                      std::size_t{0});
  REQUIRE(totalDescriptorSetCapacity == 60);
  REQUIRE(kExpectedPeakSetCount < totalDescriptorSetCapacity);

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis N=6 HDR Descriptor Pool GPU Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  const auto fallbackVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv");
  const auto fallbackFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv");
  auto fallbackReflection =
      loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json");
  REQUIRE(fallbackVertexSpirv.has_value());
  REQUIRE(fallbackFragmentSpirv.has_value());
  REQUIRE(fallbackReflection.isOk());
  const auto fallbackLayout = fallbackVertexLayout(fallbackReflection.value());
  REQUIRE(fallbackLayout.has_value());

  auto fallbackPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = fallbackVertexSpirv->data(), .wordCount = fallbackVertexSpirv->size()},
       .fragmentShader = {.spirvWords = fallbackFragmentSpirv->data(), .wordCount = fallbackFragmentSpirv->size()},
       .vertexInputLayout = *fallbackLayout,
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = atlantis::rhi::DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16});
  REQUIRE(fallbackPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> fallbackPipeline = std::move(fallbackPipelineResult.value());
  REQUIRE(fallbackPipeline != nullptr);

  const auto materialVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv");
  const auto materialFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv");
  auto materialReflection = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) +
                                                    "/textured_quad.vert.refl.json");
  REQUIRE(materialVertexSpirv.has_value());
  REQUIRE(materialFragmentSpirv.has_value());
  REQUIRE(materialReflection.isOk());
  const auto materialLayout = unlitTexturedVertexLayout(materialReflection.value());
  REQUIRE(materialLayout.has_value());

  std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> materialPipelines;
  materialPipelines.reserve(kMaterialPipelineCount);
  for (std::size_t i = 0; i < kMaterialPipelineCount; ++i) {
    auto materialPipelineResult = device->createPipeline(
        {.vertexShader = {.spirvWords = materialVertexSpirv->data(), .wordCount = materialVertexSpirv->size()},
         .fragmentShader = {.spirvWords = materialFragmentSpirv->data(),
                             .wordCount = materialFragmentSpirv->size()},
         .vertexInputLayout = *materialLayout,
         .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
         .depthFormat = atlantis::rhi::DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16,
         .sampledTextureBindingCount = 1});
    REQUIRE(materialPipelineResult.isOk());
    materialPipelines.push_back(std::move(materialPipelineResult.value()));
  }
  REQUIRE(materialPipelines.size() == kMaterialPipelineCount);

  const auto makeOutputTransformPipeline = [&](const char* shaderDirectory, const char* shaderName,
                                                Format finalFormat) -> std::unique_ptr<atlantis::rhi::Pipeline> {
    const std::string shaderPrefix = std::string(shaderDirectory) + "/" + shaderName;
    const auto vertexSpirv = loadSpirvFile(shaderPrefix + ".vert.spv");
    const auto fragmentSpirv = loadSpirvFile(shaderPrefix + ".frag.spv");
    auto reflection = loadReflectionMetadata(shaderPrefix + ".vert.refl.json");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());
    REQUIRE(reflection.isOk());
    const auto layout = outputTransformVertexLayout(reflection.value());
    REQUIRE(layout.has_value());
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
         .vertexInputLayout = *layout,
         .colorFormat = finalFormat,
         .sampledTextureBindingCount = 1,
         .hasCameraUniformBinding = false,
         .hasDepthAttachment = false});
    REQUIRE(result.isOk());
    return std::move(result.value());
  };

  std::unique_ptr<atlantis::rhi::Pipeline> steadyOutputTransformPipeline = makeOutputTransformPipeline(
      ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR, "output_transform_unorm", Format::Rgba8Unorm);
  REQUIRE(steadyOutputTransformPipeline != nullptr);
  REQUIRE(1 + materialPipelines.size() + 1 == kExpectedSteadySetCount);

  // The second output-transform Pipeline is the prepared-but-not-yet-
  // swapped format-change candidate. All geometry Pipelines remain
  // alive and fixed at Rgba16Float; only these old/new output sets
  // coexist, producing the real N+3 peak.
  std::unique_ptr<atlantis::rhi::Pipeline> transientOutputTransformPipeline = makeOutputTransformPipeline(
      ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR, "output_transform_srgb", Format::Rgba8Srgb);
  REQUIRE(transientOutputTransformPipeline != nullptr);
  REQUIRE(1 + materialPipelines.size() + 2 == kExpectedPeakSetCount);
  REQUIRE(device->waitIdle().isOk());
}

// Plan 0026 Milestone 6 (ADR-0071): re-derives the same N=6 scenario with
// one more fixed, never-rebuilt synthetic Pipeline standing in for the
// sky Pipeline (created once at startup alongside fallbackMaterial_,
// runtime_application.cpp Step 4d -- never participates in format-change
// rebuild, exactly like fallbackMaterial_ itself) -- proving the real,
// environment-enabled steady-state/peak formula against the same real
// 60-set ceiling.
TEST_CASE("N=6 HDR pipeline descriptor-set peak is exactly N+4 with an environment/sky Pipeline present",
          "[runtime][gpu][material_realization][descriptor_pool_growth][hdr][sky]") {
  using atlantis::vulkan_backend::detail::kDescriptorPoolMaxSetsByGeneration;

  constexpr std::size_t kMaterialPipelineCount = 6;
  constexpr std::size_t kFallbackPipelineCount = 1;
  constexpr std::size_t kSkyPipelineCount = 1;
  constexpr std::size_t kSteadyOutputTransformPipelineCount = 1;
  constexpr std::size_t kTransientOutputTransformPipelineCount = 1;
  constexpr std::size_t kExpectedSteadySetCount =
      kMaterialPipelineCount + kFallbackPipelineCount + kSkyPipelineCount + kSteadyOutputTransformPipelineCount;
  constexpr std::size_t kExpectedPeakSetCount = kExpectedSteadySetCount + kTransientOutputTransformPipelineCount;
  static_assert(kExpectedSteadySetCount == 9);  // N + 3
  static_assert(kExpectedPeakSetCount == 10);   // N + 4

  const std::size_t totalDescriptorSetCapacity =
      std::accumulate(kDescriptorPoolMaxSetsByGeneration.begin(), kDescriptorPoolMaxSetsByGeneration.end(),
                      std::size_t{0});
  REQUIRE(totalDescriptorSetCapacity == 60);
  REQUIRE(kExpectedPeakSetCount < totalDescriptorSetCapacity);

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis N=6 HDR+Sky Descriptor Pool GPU Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  const auto fallbackVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv");
  const auto fallbackFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv");
  auto fallbackReflection =
      loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json");
  REQUIRE(fallbackVertexSpirv.has_value());
  REQUIRE(fallbackFragmentSpirv.has_value());
  REQUIRE(fallbackReflection.isOk());
  const auto fallbackLayout = fallbackVertexLayout(fallbackReflection.value());
  REQUIRE(fallbackLayout.has_value());

  auto fallbackPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = fallbackVertexSpirv->data(), .wordCount = fallbackVertexSpirv->size()},
       .fragmentShader = {.spirvWords = fallbackFragmentSpirv->data(), .wordCount = fallbackFragmentSpirv->size()},
       .vertexInputLayout = *fallbackLayout,
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = atlantis::rhi::DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16});
  REQUIRE(fallbackPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> fallbackPipeline = std::move(fallbackPipelineResult.value());
  REQUIRE(fallbackPipeline != nullptr);

  const auto materialVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv");
  const auto materialFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv");
  auto materialReflection = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) +
                                                    "/textured_quad.vert.refl.json");
  REQUIRE(materialVertexSpirv.has_value());
  REQUIRE(materialFragmentSpirv.has_value());
  REQUIRE(materialReflection.isOk());
  const auto materialLayout = unlitTexturedVertexLayout(materialReflection.value());
  REQUIRE(materialLayout.has_value());

  std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> materialPipelines;
  materialPipelines.reserve(kMaterialPipelineCount);
  for (std::size_t i = 0; i < kMaterialPipelineCount; ++i) {
    auto materialPipelineResult = device->createPipeline(
        {.vertexShader = {.spirvWords = materialVertexSpirv->data(), .wordCount = materialVertexSpirv->size()},
         .fragmentShader = {.spirvWords = materialFragmentSpirv->data(),
                             .wordCount = materialFragmentSpirv->size()},
         .vertexInputLayout = *materialLayout,
         .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
         .depthFormat = atlantis::rhi::DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16,
         .sampledTextureBindingCount = 1});
    REQUIRE(materialPipelineResult.isOk());
    materialPipelines.push_back(std::move(materialPipelineResult.value()));
  }
  REQUIRE(materialPipelines.size() == kMaterialPipelineCount);

  // The sky Pipeline (ADR-0071 P3): sampledTextureBindingCount = 1 (the
  // environment cubemap), depthWriteEnabled = false -- its own real
  // shape, reusing the fixed fullscreen-triangle vertex schema like the
  // output-transform Pipelines below.
  const auto skyVertexSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.spv");
  const auto skyFragmentSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.frag.spv");
  auto skyReflection = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.refl.json");
  REQUIRE(skyVertexSpirv.has_value());
  REQUIRE(skyFragmentSpirv.has_value());
  REQUIRE(skyReflection.isOk());
  const auto skyLayout = outputTransformVertexLayout(skyReflection.value());
  REQUIRE(skyLayout.has_value());

  auto skyPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = skyVertexSpirv->data(), .wordCount = skyVertexSpirv->size()},
       .fragmentShader = {.spirvWords = skyFragmentSpirv->data(), .wordCount = skyFragmentSpirv->size()},
       .vertexInputLayout = *skyLayout,
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = atlantis::rhi::DepthFormat::D32Sfloat,
       .sampledTextureBindingCount = 1,
       .hasDepthAttachment = true,
       .depthWriteEnabled = false});
  REQUIRE(skyPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> skyPipeline = std::move(skyPipelineResult.value());
  REQUIRE(skyPipeline != nullptr);

  const auto makeOutputTransformPipeline = [&](const char* shaderDirectory, const char* shaderName,
                                                Format finalFormat) -> std::unique_ptr<atlantis::rhi::Pipeline> {
    const std::string shaderPrefix = std::string(shaderDirectory) + "/" + shaderName;
    const auto vertexSpirv = loadSpirvFile(shaderPrefix + ".vert.spv");
    const auto fragmentSpirv = loadSpirvFile(shaderPrefix + ".frag.spv");
    auto reflection = loadReflectionMetadata(shaderPrefix + ".vert.refl.json");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());
    REQUIRE(reflection.isOk());
    const auto layout = outputTransformVertexLayout(reflection.value());
    REQUIRE(layout.has_value());
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
         .vertexInputLayout = *layout,
         .colorFormat = finalFormat,
         .sampledTextureBindingCount = 1,
         .hasCameraUniformBinding = false,
         .hasDepthAttachment = false});
    REQUIRE(result.isOk());
    return std::move(result.value());
  };

  std::unique_ptr<atlantis::rhi::Pipeline> steadyOutputTransformPipeline = makeOutputTransformPipeline(
      ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR, "output_transform_unorm", Format::Rgba8Unorm);
  REQUIRE(steadyOutputTransformPipeline != nullptr);
  REQUIRE(1 + materialPipelines.size() + 1 + 1 == kExpectedSteadySetCount);

  // The second output-transform Pipeline is the prepared-but-not-yet-
  // swapped format-change candidate -- the sky Pipeline above is never
  // rebuilt by this event (P5), so only the output-transform old/new
  // pair coexists, producing the real N+4 peak.
  std::unique_ptr<atlantis::rhi::Pipeline> transientOutputTransformPipeline = makeOutputTransformPipeline(
      ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR, "output_transform_srgb", Format::Rgba8Srgb);
  REQUIRE(transientOutputTransformPipeline != nullptr);
  REQUIRE(1 + materialPipelines.size() + 1 + 2 == kExpectedPeakSetCount);
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("A real Vulkan Device's own descriptor pool grows past its own historical maxSets = 4 ceiling "
          "(Spec 0021/ADR-0064/Plan 0021 fix -- formerly a documented known limitation since Plan 0018)",
          "[runtime][gpu][material_realization][descriptor_pool_growth]") {
  // The low-level, kind-independent probe -- confirms growth actually
  // engages past the historical maxSets = 4 ceiling, entirely
  // independent of MaterialKind, Material, or format-change machinery.
  // 7 Pipelines: the first 4 exhaust generation-0's own capacity; the
  // 5th-7th only succeed if a second pool (generation 1, maxSets = 8)
  // was actually created and used -- proving growth engaged, not merely
  // that the historical ceiling stopped being hit by coincidence.
  {
    auto deviceResult = atlantis::vulkan_backend::createDevice(
        {.applicationName = "Atlantis Descriptor Pool Growth Probe", .enableValidationLayers = true});
    REQUIRE(deviceResult.isOk());
    std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

    auto vertexSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv");
    auto fragmentSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());
    auto vertexReflectionResult =
        loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json");
    REQUIRE(vertexReflectionResult.isOk());
    const auto layout = fallbackVertexLayout(vertexReflectionResult.value());
    REQUIRE(layout.has_value());

    std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> pipelines;
    for (int i = 0; i < 7; ++i) {
      auto result = device->createPipeline(
          {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
           .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
           .vertexInputLayout = *layout,
           .colorFormat = atlantis::rhi::Format::Rgba8Unorm,
           .depthFormat = atlantis::rhi::DepthFormat::D32Sfloat,
           .pushConstantSizeBytes = sizeof(float) * 16});
      // Every one of the 7 must succeed -- the 5th-7th only can if
      // growth (a second pool) actually happened.
      REQUIRE(result.isOk());
      pipelines.push_back(std::move(result.value()));
    }
    REQUIRE(device->waitIdle().isOk());
  }
}
