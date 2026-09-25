#include <atlantis/renderer/bloom.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>

// Plan 0044 Milestone 1 (Spec 0044 R2, P5): bloomLevelExtents() -- the
// D1..D6 extents (U1..U5 share D1..D5's), GPU-independent.

using atlantis::renderer::bloomLevelExtents;
using atlantis::renderer::kBloomLevelCount;
using atlantis::rhi::Extent2D;

namespace {

void checkExtents(Extent2D hdr, const std::array<Extent2D, kBloomLevelCount>& expected) {
  const auto actual = bloomLevelExtents(hdr);
  for (std::size_t level = 0; level < kBloomLevelCount; ++level) {
    INFO("hdr " << hdr.width << "x" << hdr.height << ", D" << (level + 1));
    CHECK(actual[level].width == expected[level].width);
    CHECK(actual[level].height == expected[level].height);
  }
}

}  // namespace

TEST_CASE("bloomLevelExtents(): six halvings from the HDR extent, the Plan 0044 tables",
          "[renderer][bloom]") {
  STATIC_REQUIRE(kBloomLevelCount == 6);
  checkExtents({512, 512}, {{{256, 256}, {128, 128}, {64, 64}, {32, 32}, {16, 16}, {8, 8}}});
  checkExtents({1920, 1080}, {{{960, 540}, {480, 270}, {240, 135}, {120, 67}, {60, 33}, {30, 16}}});
}

TEST_CASE("bloomLevelExtents(): odd sizes floor, and every level stays at least 1x1", "[renderer][bloom]") {
  checkExtents({1001, 7}, {{{500, 3}, {250, 1}, {125, 1}, {62, 1}, {31, 1}, {15, 1}}});
  checkExtents({1, 1}, {{{1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}}});
  checkExtents({3, 40}, {{{1, 20}, {1, 10}, {1, 5}, {1, 2}, {1, 1}, {1, 1}}});
}
