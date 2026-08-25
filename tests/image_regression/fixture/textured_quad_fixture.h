#pragma once

#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>

#include "../support/pixel_diff.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace atlantis::image_regression {

// Spec 0016 / Plan 0016 Milestone 9 (D11): the textured fixture -- two
// 1x2-triangle quads, side by side, each sampling its own SampledTexture
// (one Rgba8Unorm, one Rgba8Srgb, both cooked from the same checkerboard
// source PNG) through a shared Sampler.
//
// Member declaration order is load-bearing (Spec 0016/D3): sampledTextureUnorm/
// sampledTextureSrgb/sampler are declared BEFORE materialUnorm/materialSrgb,
// so C++'s reverse-declaration-order destruction destroys both Materials
// FIRST, before the SampledTexture/Sampler instances they borrow --
// structurally guaranteeing the ownership/destruction-order contract
// Material's own header documents, not merely following it by
// convention. No staging Buffer is a member here -- Spec 0016/D5a's own
// three-case lifecycle keeps each one function-local to
// renderTexturedQuadFrame(), created fresh and destroyed via ordinary
// RAII/early-return on every call, matching that section's own explicit
// "safe to destroy immediately, unconditionally" analysis for every
// failure path.
struct TexturedQuadFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  std::unique_ptr<atlantis::rhi::SampledTexture> sampledTextureUnorm;
  std::unique_ptr<atlantis::rhi::SampledTexture> sampledTextureSrgb;
  std::unique_ptr<atlantis::rhi::Sampler> sampler;
  std::optional<atlantis::renderer::Material> materialUnorm;
  std::optional<atlantis::renderer::Material> materialSrgb;
  std::optional<atlantis::renderer::Mesh> meshLeft;
  std::optional<atlantis::renderer::Mesh> meshRight;
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;

  // Decoded once at setup time (atlantis::asset_system::loadTextureAsset()'s
  // own CPU-side result, Milestone 6), kept here so every
  // renderTexturedQuadFrame() call has the bytes on hand to fill a fresh
  // staging Buffer -- never re-read from disk per call.
  std::vector<std::uint8_t> unormPixelBytes;
  std::vector<std::uint8_t> srgbPixelBytes;
};

inline constexpr std::uint32_t kTexturedQuadExtentPixels = 512;
inline constexpr atlantis::rhi::Format kTexturedQuadColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class TexturedQuadSetupError {
  DeviceCreationFailed,
  ShaderLoadFailed,
  AssetLoadFailed,
  ResourceCreationFailed,
};

// Constructs every long-lived resource once: device, both SampledTextures
// (loaded via atlantis::asset_system::loadTextureAsset() against the two
// cooked artifacts atlantis_add_texture_asset() produces, Milestone 6/7),
// the shared Sampler (Filter::Nearest, AddressMode::ClampToEdge -- keeps
// the checkerboard's own block edges crisp), both Materials, both
// Meshes, the camera Buffer, depth Texture, OffscreenTarget, and readback
// Buffer. Must be called with the process's current working directory
// set to a location where "shaders/textured_quad.{vert,frag}.spv"
// resolves, matching every other fixture's own established convention.
//
// Plan 0017 Milestone 3: meshLeft/meshRight are now loaded via
// atlantis::asset_system::loadStaticMeshAsset() against the
// textured_quad_left/textured_quad_right cooked artifacts
// (leftMeshArtifactPath/leftMeshMetadataPath,
// rightMeshArtifactPath/rightMeshMetadataPath) -- there is no longer a
// hand-authored vertex/UV fallback of any kind.
[[nodiscard]] atlantis::Result<TexturedQuadFixture, TexturedQuadSetupError> setUpTexturedQuadFixture(
    const char* unormArtifactPath, const char* unormMetadataPath, const char* srgbArtifactPath,
    const char* srgbMetadataPath, const char* leftMeshArtifactPath, const char* leftMeshMetadataPath,
    const char* rightMeshArtifactPath, const char* rightMeshMetadataPath);

enum class TexturedQuadRenderError {
  AcquireFailed,
  CommandListCreationFailed,
  StagingBufferCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// Spec 0016/D11's own explicit seven-step combined submission: two
// texture uploads (buildTextureUploadPass(), D4), two draws (one per
// quad/Material/SampledTexture, via Renderer::drawFrame()), and one
// readback -- all recorded into the SAME CommandList, against the SAME
// acquired RenderTarget, submitted via EXACTLY ONE Device::submit()
// call. Two fresh staging Buffers are created for this call only,
// destroyed via ordinary RAII/early-return before returning -- D5a's own
// three-case contract, not special-cased here (every failure path is
// already safe via plain C++ scope exit, see this file's own top
// comment). May be called more than once against the same
// TexturedQuadFixture (OffscreenTarget's own repeated-cycle contract,
// ADR-0038) -- each call independently re-uploads both textures, since
// ResourceState tracking is local to one execute() call, never persisted
// across calls (render_graph::execute()'s own documented contract).
[[nodiscard]] atlantis::Result<PixelBuffer, TexturedQuadRenderError> renderTexturedQuadFrame(
    TexturedQuadFixture& fixture);

// Spec 0016/D11's own "Proof the RenderTarget is genuinely used" test
// requirement: a second, minimal readback exercising only steps 4-6 of
// the seven-step list above (acquire, clear via beginRendering() with no
// draw items, copy, submit, waitIdle) against a freshly-cleared,
// undrawn target -- the known baseline renderTexturedQuadFrame()'s own
// captured pixels must differ from, in both quads' own screen regions.
[[nodiscard]] atlantis::Result<PixelBuffer, TexturedQuadRenderError> renderTexturedQuadBaselineFrame(
    TexturedQuadFixture& fixture);

}  // namespace atlantis::image_regression
