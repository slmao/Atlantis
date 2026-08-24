#include <atlantis/rhi/types.h>

#include <cstddef>
#include <iterator>

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::AddressMode;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::ClearColorValue;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Filter;
using atlantis::rhi::Format;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::PresentationError;
using atlantis::rhi::ResourceState;
using atlantis::rhi::SampledTextureCreateParams;
using atlantis::rhi::SampledTextureFormat;
using atlantis::rhi::SamplerCreateParams;
using atlantis::rhi::SwapchainMetadata;

TEST_CASE("Extent2D defaults to zero", "[rhi][extent2d]") {
  const Extent2D extent;
  REQUIRE(extent.width == 0);
  REQUIRE(extent.height == 0);
}

TEST_CASE("Extent2D detects the zero state", "[rhi][extent2d]") {
  REQUIRE(Extent2D{}.isZero());
  REQUIRE(Extent2D{0, 0}.isZero());
  REQUIRE_FALSE(Extent2D{1, 0}.isZero());
  REQUIRE_FALSE(Extent2D{0, 1}.isZero());
  REQUIRE_FALSE(Extent2D{1920, 1080}.isZero());
}

TEST_CASE("Extent2D equality and inequality", "[rhi][extent2d]") {
  REQUIRE(Extent2D{1920, 1080} == Extent2D{1920, 1080});
  REQUIRE_FALSE(Extent2D{1920, 1080} == Extent2D{1280, 720});
  REQUIRE_FALSE(Extent2D{1920, 1080} == Extent2D{1080, 1920});
}

TEST_CASE("Format enumerators are all distinct and usable", "[rhi][format]") {
  const Format formats[] = {Format::Unknown, Format::Bgra8Unorm, Format::Bgra8Srgb, Format::Rgba8Unorm,
                             Format::Rgba8Srgb};
  for (std::size_t i = 0; i < std::size(formats); ++i) {
    for (std::size_t j = 0; j < std::size(formats); ++j) {
      REQUIRE((formats[i] == formats[j]) == (i == j));
    }
  }
}

TEST_CASE("SwapchainMetadata defaults to the pre-recreation state", "[rhi][swapchain_metadata]") {
  const SwapchainMetadata metadata;
  REQUIRE(metadata.imageCount == 0);
  REQUIRE(metadata.format == Format::Unknown);
  REQUIRE(metadata.extent.isZero());
}

TEST_CASE("SwapchainMetadata stores and returns image count, format, and extent", "[rhi][swapchain_metadata]") {
  SwapchainMetadata metadata;
  metadata.imageCount = 3;
  metadata.format = Format::Bgra8Unorm;
  metadata.extent = Extent2D{1920, 1080};

  REQUIRE(metadata.imageCount == 3);
  REQUIRE(metadata.format == Format::Bgra8Unorm);
  REQUIRE(metadata.extent == Extent2D{1920, 1080});
}

TEST_CASE("PresentationError enumerators construct and compare", "[rhi][presentation_error]") {
  const PresentationError errors[] = {PresentationError::SurfaceLost, PresentationError::SwapchainCreationFailed,
                                       PresentationError::DeviceLost, PresentationError::Unknown};
  for (std::size_t i = 0; i < std::size(errors); ++i) {
    for (std::size_t j = 0; j < std::size(errors); ++j) {
      REQUIRE((errors[i] == errors[j]) == (i == j));
    }
  }
}

TEST_CASE("ResourceState enumerators are all distinct and usable", "[rhi][resource_state]") {
  const ResourceState states[] = {ResourceState::Undefined, ResourceState::ColorAttachmentWrite,
                                   ResourceState::PresentSource, ResourceState::TransferSource};
  for (std::size_t i = 0; i < std::size(states); ++i) {
    for (std::size_t j = 0; j < std::size(states); ++j) {
      REQUIRE((states[i] == states[j]) == (i == j));
    }
  }
}

TEST_CASE("BufferPurpose enumerators are all distinct and usable", "[rhi][buffer_purpose]") {
  const BufferPurpose purposes[] = {BufferPurpose::Vertex, BufferPurpose::Index, BufferPurpose::Uniform,
                                     BufferPurpose::Readback};
  for (std::size_t i = 0; i < std::size(purposes); ++i) {
    for (std::size_t j = 0; j < std::size(purposes); ++j) {
      REQUIRE((purposes[i] == purposes[j]) == (i == j));
    }
  }
}

TEST_CASE("ClearColorValue defaults to opaque black", "[rhi][clear_color_value]") {
  const ClearColorValue color;
  REQUIRE(color.r == 0.0f);
  REQUIRE(color.g == 0.0f);
  REQUIRE(color.b == 0.0f);
  REQUIRE(color.a == 1.0f);
}

TEST_CASE("ClearColorValue equality and inequality", "[rhi][clear_color_value]") {
  REQUIRE(ClearColorValue{0.1f, 0.2f, 0.3f, 1.0f} == ClearColorValue{0.1f, 0.2f, 0.3f, 1.0f});
  REQUIRE_FALSE(ClearColorValue{0.1f, 0.2f, 0.3f, 1.0f} == ClearColorValue{0.9f, 0.2f, 0.3f, 1.0f});
  REQUIRE_FALSE(ClearColorValue{} == ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f});
}

TEST_CASE("OffscreenTargetCreateParams defaults to a real, usable format", "[rhi][offscreen_target_create_params]") {
  const OffscreenTargetCreateParams params;
  REQUIRE(params.extent.isZero());
  REQUIRE(params.format == Format::Rgba8Unorm);
}

TEST_CASE("OffscreenTargetCreateParams equality and inequality", "[rhi][offscreen_target_create_params]") {
  REQUIRE(OffscreenTargetCreateParams{Extent2D{512, 512}, Format::Rgba8Unorm} ==
          OffscreenTargetCreateParams{Extent2D{512, 512}, Format::Rgba8Unorm});
  REQUIRE_FALSE(OffscreenTargetCreateParams{Extent2D{512, 512}, Format::Rgba8Unorm} ==
                OffscreenTargetCreateParams{Extent2D{256, 256}, Format::Rgba8Unorm});
  REQUIRE_FALSE(OffscreenTargetCreateParams{Extent2D{512, 512}, Format::Rgba8Unorm} ==
                OffscreenTargetCreateParams{Extent2D{512, 512}, Format::Bgra8Unorm});
}

TEST_CASE("SampledTextureCreateParams defaults to a real, usable format", "[rhi][sampled_texture_create_params]") {
  const SampledTextureCreateParams params;
  REQUIRE(params.extent.isZero());
  REQUIRE(params.format == SampledTextureFormat::Rgba8Unorm);
}

TEST_CASE("SampledTextureCreateParams equality and inequality", "[rhi][sampled_texture_create_params]") {
  REQUIRE(SampledTextureCreateParams{Extent2D{64, 64}, SampledTextureFormat::Rgba8Unorm} ==
          SampledTextureCreateParams{Extent2D{64, 64}, SampledTextureFormat::Rgba8Unorm});
  REQUIRE_FALSE(SampledTextureCreateParams{Extent2D{64, 64}, SampledTextureFormat::Rgba8Unorm} ==
                SampledTextureCreateParams{Extent2D{32, 32}, SampledTextureFormat::Rgba8Unorm});
  REQUIRE_FALSE(SampledTextureCreateParams{Extent2D{64, 64}, SampledTextureFormat::Rgba8Unorm} ==
                SampledTextureCreateParams{Extent2D{64, 64}, SampledTextureFormat::Rgba8Srgb});
}

TEST_CASE("SamplerCreateParams defaults to a real, usable filter and address mode", "[rhi][sampler_create_params]") {
  const SamplerCreateParams params;
  REQUIRE(params.filter == Filter::Nearest);
  REQUIRE(params.addressMode == AddressMode::ClampToEdge);
}

TEST_CASE("SamplerCreateParams equality and inequality", "[rhi][sampler_create_params]") {
  REQUIRE(SamplerCreateParams{Filter::Linear, AddressMode::Repeat} ==
          SamplerCreateParams{Filter::Linear, AddressMode::Repeat});
  REQUIRE_FALSE(SamplerCreateParams{Filter::Linear, AddressMode::Repeat} ==
                SamplerCreateParams{Filter::Nearest, AddressMode::Repeat});
  REQUIRE_FALSE(SamplerCreateParams{Filter::Linear, AddressMode::Repeat} ==
                SamplerCreateParams{Filter::Linear, AddressMode::ClampToEdge});
}
