#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/types.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>
#include <string_view>

// Spec 0059 R2 / ADR-0115 D5: the RHI's instrumentation surface is identical in
// every build configuration (profiling off / on / on + Tracy). This file has no
// build-option conditional; the same committed numbers must hold in A, B and C,
// and profile_boundary_tests.cpp proves no public header tests those options.
// The numbers are for the 64-bit Windows target the tests run on.

namespace {

using namespace atlantis::rhi;

template <class Params>
void expectDebugNameIsLast(const char* what) {
  INFO(what);
  CHECK(offsetof(Params, debugName) + sizeof(std::string_view) == sizeof(Params));
}

}  // namespace

TEST_CASE("every creation-params struct ends with a debugName", "[rhi][instrumentation]") {
  expectDebugNameIsLast<BufferCreateParams>("BufferCreateParams");
  expectDebugNameIsLast<TextureCreateParams>("TextureCreateParams");
  expectDebugNameIsLast<SampledTextureCreateParams>("SampledTextureCreateParams");
  expectDebugNameIsLast<SamplerCreateParams>("SamplerCreateParams");
  expectDebugNameIsLast<OffscreenTargetCreateParams>("OffscreenTargetCreateParams");
  expectDebugNameIsLast<HdrColorTargetCreateParams>("HdrColorTargetCreateParams");
  expectDebugNameIsLast<ShadowMapCreateParams>("ShadowMapCreateParams");
  expectDebugNameIsLast<PipelineCreateParams>("PipelineCreateParams");
}

TEST_CASE("the creation-params layouts are the same in every build configuration", "[rhi][instrumentation]") {
  CHECK(sizeof(BufferCreateParams) == 40);
  CHECK(sizeof(TextureCreateParams) == 32);
  CHECK(sizeof(SampledTextureCreateParams) == 40);
  CHECK(sizeof(SamplerCreateParams) == 40);
  CHECK(sizeof(OffscreenTargetCreateParams) == 32);
  CHECK(sizeof(HdrColorTargetCreateParams) == 32);
  CHECK(sizeof(ShadowMapCreateParams) == 32);
  CHECK(sizeof(PipelineCreateParams) == 128);
}

TEST_CASE("a designated initializer written before debugName existed still compiles and leaves it empty",
          "[rhi][instrumentation]") {
  const BufferCreateParams buffer{.purpose = BufferPurpose::Uniform, .sizeBytes = 16};
  const TextureCreateParams texture{.extent = {4, 4}, .format = DepthFormat::D32Sfloat};
  CHECK(buffer.debugName.empty());
  CHECK(texture.debugName.empty());
}

TEST_CASE("the instrumentation virtuals are non-pure no-ops on CommandList", "[rhi][instrumentation]") {
  // A CommandList with every pre-existing virtual overridden and none of the
  // new ones must still be instantiable: they are not pure.
  struct Minimal final : CommandList {
    void transitionResource(RenderTarget&, ResourceState, ResourceState) override {}
    void clearColor(RenderTarget&, ClearColorValue) override {}
    void transitionResource(Texture&, ResourceState, ResourceState) override {}
    void beginRendering(RenderTarget&, Texture*, ClearColorValue, float) override {}
    void endRendering() override {}
    void bindPipeline(Pipeline&) override {}
    void bindVertexBuffer(Buffer&) override {}
    void bindIndexBuffer(Buffer&) override {}
    void bindUniformBuffer(Buffer&) override {}
    void pushConstant(const void*, std::size_t) override {}
    void drawIndexed(std::uint32_t) override {}
    void copyRenderTargetToBuffer(RenderTarget&, Buffer&) override {}
    void transitionResource(SampledTexture&, ResourceState, ResourceState) override {}
    void copyBufferToTexture(Buffer&, SampledTexture&) override {}
    void copyBufferToTexture(Buffer&, SampledTexture&, std::span<const SampledTextureUploadRegion>) override {}
    void bindTexture(std::uint32_t, const SampledTexture&, const Sampler&) override {}
    void transitionResource(HdrColorTarget&, ResourceState, ResourceState) override {}
    void beginRendering(HdrColorTarget&, Texture*, ClearColorValue, float) override {}
    void bindTexture(std::uint32_t, const HdrColorTarget&, const Sampler&) override {}
    void transitionResource(ShadowMap&, ResourceState, ResourceState) override {}
    void beginRendering(ShadowMap&, float) override {}
    void bindTexture(std::uint32_t, const ShadowMap&, const Sampler&) override {}
    void bindTexture(std::uint32_t, const RenderTarget&, const Sampler&) override {}
    void setScissor(Rect2D) override {}
    void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
  };
  Minimal list;
  list.beginRegion("x");
  list.endRegion();
  list.setDebugName("y");
  SUCCEED();
}
