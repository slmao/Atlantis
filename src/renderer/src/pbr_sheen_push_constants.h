#pragma once

#include <cstddef>

namespace atlantis::renderer {

// Plan 0035 Milestone 3 (ADR-0081 D-3/D-4): PbrSheen's own, independent
// push-constant layout -- mirrors PbrClearcoatPushConstants's own shape
// (pbr_clearcoat_push_constants.h) through roughnessFactor, then departs
// from it: sheenColor is a vec3, which both Slang's own push-constant
// block layout rules and this struct's own explicit padding below place
// at a 16-byte-aligned offset (96, not 88) -- an 8-byte gap objectively
// required by sheenColor's own vec3 alignment, not a copy-paste
// leftover. This exact layout is confirmed by a real Slang reflection
// (atlantis_shader_compiler's own pbr-sheen-ibl/pbr-sheen-ibl-normal-map
// contract validation, compile_and_validate.cpp) AND by this struct's
// own static_asserts below -- ADR-0067 D-3's required double
// confirmation, not assumed.
//
// Total: 112 bytes, comfortably under Vulkan's 128-byte guaranteed
// minimum (16 bytes of headroom) -- ADR-0081's own flagged "likely
// tipping point" risk gate is confirmed NOT triggered by the real,
// measured layout.
struct alignas(16) PbrSheenPushConstants {
  float objectToWorld[16] = {};     // offset 0,   64 bytes
  float baseColorFactor[4] = {};    // offset 64,  16 bytes
  float metallicFactor = 0.0f;      // offset 80,   4 bytes
  float roughnessFactor = 0.0f;     // offset 84,   4 bytes
  float _padding0[2] = {};          // offset 88,   8 bytes (aligns sheenColor to its own 16-byte boundary)
  float sheenColor[3] = {};         // offset 96,  12 bytes
  float sheenRoughness = 0.0f;      // offset 108,  4 bytes
  // Plan 0041 Milestone 2 (Spec 0041 R5, ADR-0089 Decision 4): appended
  // after every existing field (none moves); an explicit tail pad (now
  // alphaCutoff, below) made sizeof a provable sum (Plan 0041 P3).
  // PbrSheen reaches exactly Vulkan's guaranteed 128-byte
  // maxPushConstantsSize here (Spec 0041 ruling O4): the next vector
  // parameter on this kind forces ADR-0081's per-material uniform buffer.
  float emissiveFactor[3] = {};     // offset 112, 12 bytes
  // Plan 0042 Milestone 2 (Spec 0042 R6, ADR-0090 Decision 4): the
  // explicit tail pad Plan 0041 left is now alphaCutoff -- same offset,
  // same size, so no struct grows. 0 unless the material is Mask.
  float alphaCutoff = 0.0f;         // offset 124,  4 bytes
};

static_assert(sizeof(PbrSheenPushConstants) == 128);
static_assert(offsetof(PbrSheenPushConstants, objectToWorld) == 0);
static_assert(offsetof(PbrSheenPushConstants, baseColorFactor) == 64);
static_assert(offsetof(PbrSheenPushConstants, metallicFactor) == 80);
static_assert(offsetof(PbrSheenPushConstants, roughnessFactor) == 84);
static_assert(offsetof(PbrSheenPushConstants, sheenColor) == 96);
static_assert(offsetof(PbrSheenPushConstants, sheenRoughness) == 108);
static_assert(offsetof(PbrSheenPushConstants, emissiveFactor) == 112);
static_assert(offsetof(PbrSheenPushConstants, alphaCutoff) == 124);

}  // namespace atlantis::renderer
