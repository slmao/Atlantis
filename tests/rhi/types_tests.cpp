#include <atlantis/rhi/types.h>

#include <cstddef>
#include <iterator>

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::PresentationError;
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
