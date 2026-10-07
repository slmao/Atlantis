#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/renderer/ui_overlay.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>


// Plan 0056 M3 (P4, P5; ADR-0108 D3, J2, J6): the RHI's sampleable offscreen
// target, scissor and ranged indexed draw, and the Renderer's UI overlay pass,
// on a real device with validation layers on (a validation message is fatal),
// headless: zero windows, read back exactly. Every case carries the "gpu"
// CTest label (tests/vulkan_backend/CMakeLists.txt).

namespace {

using atlantis::renderer::UiDrawCommand;
using atlantis::renderer::UiDrawList;
using atlantis::renderer::UiOverlayResources;
using atlantis::renderer::UiTexture;
using atlantis::renderer::UiVertex;
using atlantis::rhi::Buffer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::ClearColorValue;
using atlantis::rhi::CommandList;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::OffscreenTarget;
using atlantis::rhi::Pipeline;
using atlantis::rhi::Rect2D;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::ResourceState;
using atlantis::rhi::SampledTexture;
using atlantis::rhi::Sampler;

[[nodiscard]] std::vector<std::uint32_t> loadSpirv(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  REQUIRE(file.is_open());
  const std::streamsize sizeBytes = file.tellg();
  REQUIRE(sizeBytes > 0);
  REQUIRE(sizeBytes % 4 == 0);
  file.seekg(0);
  std::vector<std::uint32_t> words(static_cast<std::size_t>(sizeBytes) / 4);
  REQUIRE(file.read(reinterpret_cast<char*>(words.data()), sizeBytes));
  return words;
}

[[nodiscard]] std::unique_ptr<Device> makeDevice(const char* name) {
  auto device = atlantis::vulkan_backend::createDevice(
      atlantis::vulkan_backend::DeviceCreateParams{.applicationName = name, .enableValidationLayers = true});
  REQUIRE(device.isOk());
  return std::move(device.value());
}

[[nodiscard]] bool isSrgb(Format format) { return format == Format::Rgba8Srgb || format == Format::Bgra8Srgb; }
[[nodiscard]] bool isBgra(Format format) { return format == Format::Bgra8Unorm || format == Format::Bgra8Srgb; }

[[nodiscard]] std::unique_ptr<Pipeline> makeOverlayPipeline(Device& device, Format format) {
  static const std::vector<std::uint32_t> vertex = loadSpirv("shaders/editor_ui.vert.spv");
  static const std::vector<std::uint32_t> fragment = loadSpirv("shaders/editor_ui.frag.spv");
  auto pipeline = device.createPipeline(
      {.vertexShader = {.spirvWords = vertex.data(), .wordCount = vertex.size()},
       .fragmentShader = {.spirvWords = fragment.data(), .wordCount = fragment.size()},
       .vertexInputLayout = atlantis::renderer::uiVertexInputLayout(),
       .colorFormat = format,
       .pushConstantSizeBytes = atlantis::renderer::kUiOverlayPushConstantSizeBytes,
       .sampledTextureBindingCount = atlantis::renderer::kUiOverlaySampledTextureBindingCount,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false,
       .colorBlendMode = atlantis::rhi::ColorBlendMode::AlphaBlend});
  REQUIRE(pipeline.isOk());
  return std::move(pipeline.value());
}

[[nodiscard]] std::unique_ptr<OffscreenTarget> makeTarget(Device& device, Extent2D extent, Format format,
                                                          bool sampled) {
  auto target = device.createOffscreenTarget({.extent = extent, .format = format, .sampled = sampled});
  REQUIRE(target.isOk());
  return std::move(target.value());
}

[[nodiscard]] std::unique_ptr<RenderTarget> acquire(OffscreenTarget& target) {
  auto acquired = target.acquireTarget();
  REQUIRE(acquired.isOk());
  return std::move(acquired.value());
}

[[nodiscard]] std::unique_ptr<CommandList> begin(Device& device) {
  auto commandList = device.createCommandList();
  REQUIRE(commandList.isOk());
  return std::move(commandList.value());
}

void submitAndWait(Device& device, std::unique_ptr<CommandList> commandList, const RenderTarget& target) {
  REQUIRE(device.submit(std::move(commandList), target).isOk());
  REQUIRE(device.waitIdle().isOk());
}

// One pass clearing `target` to `color` (a draw pass with no draws), leaving it
// in `finalState`.
void recordClear(CommandList& commandList, RenderTarget& target, ClearColorValue color, ResourceState finalState) {
  atlantis::render_graph::RenderGraphBuilder builder;
  const auto resource = builder.declareResource("clear");
  const auto pass = builder.declarePass("clear");
  builder.writes(pass, resource, ResourceState::ColorAttachmentOutput);
  builder.setExecute(pass, [](CommandList&) {});
  auto compiled = builder.compile();
  REQUIRE(compiled.isOk());
  atlantis::render_graph::execute(
      compiled.value(),
      {{.resource = compiled.value().resourceAt(0), .target = &target, .colorClear = color, .finalState = finalState}},
      commandList);
}

// Records the copy of `target` (already in TransferSource) into `readback`.
void recordReadback(CommandList& commandList, RenderTarget& target, Buffer& readback) {
  atlantis::render_graph::RenderGraphBuilder builder;
  const auto resource = builder.declareResource("readback");
  const auto pass = builder.declarePass("readback");
  builder.writes(pass, resource, ResourceState::TransferSource);
  builder.setExecute(pass, [&target, &readback](CommandList& cmd) { cmd.copyRenderTargetToBuffer(target, readback); });
  auto compiled = builder.compile();
  REQUIRE(compiled.isOk());
  atlantis::render_graph::execute(compiled.value(),
                                  {{.resource = compiled.value().resourceAt(0),
                                    .target = &target,
                                    .incomingState = ResourceState::TransferSource,
                                    .finalState = std::nullopt}},
                                  commandList);
}

[[nodiscard]] std::unique_ptr<Buffer> makeBuffer(Device& device, BufferPurpose purpose, std::size_t bytes,
                                                 atlantis::rhi::IndexType indexType = atlantis::rhi::IndexType::Uint16) {
  auto buffer = device.createBuffer({.purpose = purpose, .sizeBytes = bytes, .indexType = indexType});
  REQUIRE(buffer.isOk());
  return std::move(buffer.value());
}

// A 2x2 opaque white RGBA8 font atlas stand-in, uploaded and left in ShaderRead.
[[nodiscard]] std::unique_ptr<SampledTexture> makeWhiteAtlas(Device& device, const RenderTarget& submitTarget) {
  auto texture = device.createSampledTexture({.extent = {2, 2}});
  REQUIRE(texture.isOk());
  auto staging = makeBuffer(device, BufferPurpose::Staging, 16);
  std::memset(staging->mappedData(), 0xFF, 16);
  auto commandList = begin(device);
  atlantis::render_graph::RenderGraphBuilder builder;
  const auto resource = builder.declareResource("atlas-upload");
  const auto pass = builder.declarePass("atlas-upload");
  builder.writes(pass, resource, ResourceState::TransferDestination);
  SampledTexture& destination = *texture.value();
  builder.setExecute(pass, [&staging, &destination](CommandList& cmd) { cmd.copyBufferToTexture(*staging, destination); });
  auto compiled = builder.compile();
  REQUIRE(compiled.isOk());
  atlantis::render_graph::execute(compiled.value(),
                                  {{.resource = compiled.value().resourceAt(0),
                                    .sampledTexture = &destination,
                                    .finalState = ResourceState::ShaderRead}},
                                  *commandList);
  submitAndWait(device, std::move(commandList), submitTarget);
  return std::move(texture.value());
}

[[nodiscard]] std::unique_ptr<Sampler> makeSampler(Device& device) {
  auto sampler = device.createSampler({});  // nearest, clamp: texel centres come back exact
  REQUIRE(sampler.isOk());
  return std::move(sampler.value());
}

struct Rgba {
  std::uint8_t r = 0, g = 0, b = 0, a = 0;
  friend bool operator==(const Rgba&, const Rgba&) = default;
};

[[nodiscard]] Rgba pixelAt(Buffer& readback, Extent2D extent, Format format, std::uint32_t x, std::uint32_t y) {
  const auto* bytes = static_cast<const std::uint8_t*>(readback.mappedData());
  const std::size_t at = (static_cast<std::size_t>(y) * extent.width + x) * 4;
  if (isBgra(format)) return Rgba{bytes[at + 2], bytes[at + 1], bytes[at], bytes[at + 3]};
  return Rgba{bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]};
}

void quad(UiDrawList& list, float x0, float y0, float x1, float y1, std::array<float, 4> color) {
  list.vertices.push_back({x0, y0, 0.0f, 0.0f, color[0], color[1], color[2], color[3]});
  list.vertices.push_back({x1, y0, 1.0f, 0.0f, color[0], color[1], color[2], color[3]});
  list.vertices.push_back({x1, y1, 1.0f, 1.0f, color[0], color[1], color[2], color[3]});
  list.vertices.push_back({x0, y1, 0.0f, 1.0f, color[0], color[1], color[2], color[3]});
}

constexpr float kGray = 128.0f / 255.0f;
// The Viewport's colour, as exact 8-bit UNORM values (51, 102, 153).
constexpr ClearColorValue kViewportColor{51.0f / 255.0f, 102.0f / 255.0f, 153.0f / 255.0f, 1.0f};

}  // namespace

TEST_CASE("bindTexture() refuses a RenderTarget that is not a sampled offscreen target",
          "[vulkan_backend][gpu][ui_overlay]") {
  auto device = makeDevice("Atlantis UI overlay GPU tests (refusal)");
  auto plain = makeTarget(*device, {4, 4}, Format::Rgba8Unorm, /*sampled=*/false);
  auto plainTarget = acquire(*plain);
  auto pipeline = makeOverlayPipeline(*device, Format::Rgba8Unorm);
  auto sampler = makeSampler(*device);
  std::vector<std::string> failures;
  const auto previous = atlantis::assertions::setFailureHandler(
      [&failures](const atlantis::AssertFailureInfo& info) { failures.emplace_back(info.message); });
  {
    auto commandList = begin(*device);
    commandList->bindPipeline(*pipeline);
    commandList->bindTexture(atlantis::renderer::kUiOverlayViewportBinding, *plainTarget, *sampler);
  }  // never submitted: nothing was recorded for the refused binding
  atlantis::assertions::setFailureHandler(previous);
  REQUIRE(failures.size() == 1);
  CHECK(failures[0].find("not a sampled offscreen target") != std::string::npos);
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("a sampled offscreen target is drawn into, then sampled by the overlay pass, and passes through exactly",
          "[vulkan_backend][gpu][ui_overlay]") {
  const Format format = GENERATE(Format::Rgba8Unorm, Format::Rgba8Srgb, Format::Bgra8Unorm, Format::Bgra8Srgb);
  INFO("format " << static_cast<int>(format));
  constexpr Extent2D kExtent{16, 8};
  auto device = makeDevice("Atlantis UI overlay GPU tests (pass-through)");

  // The source, sampled; and a reference with the same format and clear, read
  // back directly -- its bytes are what the format stores for that colour.
  auto source = makeTarget(*device, kExtent, format, /*sampled=*/true);
  auto sourceTarget = acquire(*source);
  auto reference = makeTarget(*device, kExtent, format, /*sampled=*/false);
  auto referenceTarget = acquire(*reference);
  auto output = makeTarget(*device, kExtent, format, /*sampled=*/false);
  auto outputTarget = acquire(*output);
  auto atlas = makeWhiteAtlas(*device, *outputTarget);
  auto sampler = makeSampler(*device);
  auto pipeline = makeOverlayPipeline(*device, format);

  UiDrawList list;
  list.display = kExtent;
  quad(list, 0.0f, 0.0f, 16.0f, 8.0f, {1.0f, 1.0f, 1.0f, 1.0f});
  list.indices = {0, 1, 2, 0, 2, 3};
  list.commands.push_back(UiDrawCommand{0, 6, 0, Rect2D{0, 0, kExtent}, UiTexture::Viewport});
  auto vertices = makeBuffer(*device, BufferPurpose::Vertex, atlantis::renderer::uiVertexBytes(list));
  auto indices = makeBuffer(*device, BufferPurpose::Index, atlantis::renderer::uiIndexBytes(list),
                            atlantis::rhi::IndexType::Uint32);
  const std::size_t bytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4;
  auto referenceReadback = makeBuffer(*device, BufferPurpose::Readback, bytes);
  auto outputReadback = makeBuffer(*device, BufferPurpose::Readback, bytes);

  auto commandList = begin(*device);
  recordClear(*commandList, *sourceTarget, kViewportColor, ResourceState::ShaderRead);
  recordClear(*commandList, *referenceTarget, kViewportColor, ResourceState::TransferSource);
  recordReadback(*commandList, *referenceTarget, *referenceReadback);
  const UiOverlayResources resources{*pipeline, *vertices, *indices, *atlas, *sampler, *sourceTarget, *sampler,
                                     isSrgb(format)};
  atlantis::renderer::Renderer{}.drawOverlay(*commandList, *outputTarget, ResourceState::TransferSource, list,
                                             resources);
  recordReadback(*commandList, *outputTarget, *outputReadback);
  submitAndWait(*device, std::move(commandList), *outputTarget);

  for (std::uint32_t y = 0; y < kExtent.height; ++y) {
    for (std::uint32_t x = 0; x < kExtent.width; ++x) {
      const Rgba expected = pixelAt(*referenceReadback, kExtent, format, x, y);
      const Rgba actual = pixelAt(*outputReadback, kExtent, format, x, y);
      INFO("pixel " << x << "," << y);
      REQUIRE(actual.r == expected.r);
      REQUIRE(actual.g == expected.g);
      REQUIRE(actual.b == expected.b);
    }
  }
  if (!isSrgb(format)) CHECK(pixelAt(*outputReadback, kExtent, format, 3, 3) == Rgba{51, 102, 153, 255});
}

TEST_CASE("the overlay pass draws ranged, scissored commands from both texture slots, sRGB and UNORM",
          "[vulkan_backend][gpu][ui_overlay]") {
  const Format format = GENERATE(Format::Rgba8Unorm, Format::Bgra8Srgb);
  INFO("format " << static_cast<int>(format));
  constexpr Extent2D kExtent{64, 32};
  constexpr Extent2D kViewportExtent{32, 32};
  auto device = makeDevice("Atlantis UI overlay GPU tests (commands)");

  auto viewport = makeTarget(*device, kViewportExtent, format, /*sampled=*/true);
  auto viewportTarget = acquire(*viewport);
  auto reference = makeTarget(*device, kViewportExtent, format, /*sampled=*/false);
  auto referenceTarget = acquire(*reference);
  auto output = makeTarget(*device, kExtent, format, /*sampled=*/false);
  auto outputTarget = acquire(*output);
  auto atlas = makeWhiteAtlas(*device, *outputTarget);
  auto sampler = makeSampler(*device);
  auto pipeline = makeOverlayPipeline(*device, format);

  // Two quads sharing one index range: the second command re-reads indices
  // 0..5 from firstIndex 6 with vertexOffset 4. The first (red over the white
  // atlas, then gray) is scissored to (4,4)-(24,24) and (4,26)-(24,30); the
  // second (the Viewport) has a clip reaching past the target, which is
  // clamped.
  UiDrawList list;
  list.display = kExtent;
  quad(list, 0.0f, 0.0f, 32.0f, 24.0f, {1.0f, 0.0f, 0.0f, 1.0f});
  quad(list, 32.0f, 0.0f, 64.0f, 32.0f, {1.0f, 1.0f, 1.0f, 1.0f});
  quad(list, 0.0f, 24.0f, 32.0f, 32.0f, {kGray, kGray, kGray, 1.0f});
  list.indices = {0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3};
  list.commands.push_back(UiDrawCommand{0, 6, 0, Rect2D{4, 4, {20, 20}}, UiTexture::FontAtlas});
  list.commands.push_back(UiDrawCommand{6, 6, 4, Rect2D{32, -8, {64, 100}}, UiTexture::Viewport});
  list.commands.push_back(UiDrawCommand{0, 6, 8, Rect2D{4, 26, {20, 4}}, UiTexture::FontAtlas});
  auto vertices = makeBuffer(*device, BufferPurpose::Vertex, atlantis::renderer::uiVertexBytes(list));
  auto indices = makeBuffer(*device, BufferPurpose::Index, atlantis::renderer::uiIndexBytes(list),
                            atlantis::rhi::IndexType::Uint32);
  auto referenceReadback =
      makeBuffer(*device, BufferPurpose::Readback, static_cast<std::size_t>(kViewportExtent.width) * kViewportExtent.height * 4);
  auto outputReadback =
      makeBuffer(*device, BufferPurpose::Readback, static_cast<std::size_t>(kExtent.width) * kExtent.height * 4);

  auto commandList = begin(*device);
  recordClear(*commandList, *viewportTarget, kViewportColor, ResourceState::ShaderRead);
  recordClear(*commandList, *referenceTarget, kViewportColor, ResourceState::TransferSource);
  recordReadback(*commandList, *referenceTarget, *referenceReadback);
  const UiOverlayResources resources{*pipeline, *vertices, *indices, *atlas, *sampler, *viewportTarget, *sampler,
                                     isSrgb(format)};
  atlantis::renderer::Renderer{}.drawOverlay(*commandList, *outputTarget, ResourceState::TransferSource, list,
                                             resources);
  recordReadback(*commandList, *outputTarget, *outputReadback);
  submitAndWait(*device, std::move(commandList), *outputTarget);

  const auto at = [&](std::uint32_t x, std::uint32_t y) { return pixelAt(*outputReadback, kExtent, format, x, y); };
  const Rgba black{0, 0, 0, 255};
  const Rgba red{255, 0, 0, 255};
  // Inside the first scissor: red; outside it (still inside the quad): the clear.
  CHECK(at(4, 4) == red);
  CHECK(at(23, 23) == red);
  CHECK(at(3, 10) == black);
  CHECK(at(10, 3) == black);
  CHECK(at(24, 10) == black);
  CHECK(at(10, 24) == black);
  // The Viewport quad, its clip clamped to the target: the Viewport's own
  // texels, unchanged.
  const Rgba viewportTexel = pixelAt(*referenceReadback, kViewportExtent, format, 0, 0);
  CHECK(at(32, 0) == viewportTexel);
  CHECK(at(63, 31) == viewportTexel);
  CHECK(at(48, 16) == viewportTexel);
  // The gray quad: an sRGB-authored 128 is stored as 128 on either target --
  // on the sRGB one only because the shader linearized it before the store's
  // encode (unlinearized it would be about 188).
  const Rgba gray = at(10, 28);
  CHECK(gray == Rgba{128, 128, 128, 255});
  CHECK(at(10, 25) == black);  // outside the third scissor
}
