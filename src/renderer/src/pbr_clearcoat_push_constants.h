#pragma once

#include <cstddef>
#include <type_traits>

namespace atlantis::renderer {

// Plan 0035 Milestone 2 (ADR-0081 Decision item 2): the exact, real-
// Slang-and-MSVC-confirmed push-constant layout for
// MaterialPushConstantLayout::PbrClearcoat -- ADR-0081's own decision
// that each new BRDF kind (clearcoat, sheen, anisotropy) gets its own
// independent push-constant struct, starting from the same 96-byte
// objectToWorld/baseColorFactor/metallicFactor/roughnessFactor base
// PbrPushConstants (pbr_push_constants.h) already establishes, rather
// than widening that existing, nearly-exhausted 96-byte struct in
// place. clearcoatFactor/clearcoatRoughness append the base with
// exactly 8 more bytes -- the resulting 96-byte total (64+16+4+4+4+4)
// is ALREADY a multiple of 16 with no compiler-implicit padding and no
// explicit padding field needed (unlike PbrPushConstants's own 88-real-
// byte total, which needed 8 explicit trailing bytes to reach 96) --
// confirmed by the static_asserts below, not assumed. 96 of Vulkan's
// guaranteed-minimum 128-byte push-constant budget -- 32 bytes of
// headroom, identical to PbrDirectLit's own budget, since this is an
// independent struct, not a widening of that one (ADR-0081's own
// Context table: this is comfortably inside the guarantee, unlike a
// naive combined clearcoat+sheen+anisotropy struct would have been).
//
// Deliberately a PRIVATE header (src/renderer/src/, not
// src/renderer/include/atlantis/renderer/), mirroring
// pbr_push_constants.h's own identical "no cross-module contract of its
// own" reasoning -- renderer.cpp is its own sole real production
// consumer.
struct alignas(16) PbrClearcoatPushConstants {
  float objectToWorld[16] = {};     // offset 0,  64 bytes
  float baseColorFactor[4] = {};    // offset 64, 16 bytes
  float metallicFactor = 0.0f;      // offset 80,  4 bytes
  float roughnessFactor = 0.0f;     // offset 84,  4 bytes
  float clearcoatFactor = 0.0f;     // offset 88,  4 bytes
  float clearcoatRoughness = 0.0f;  // offset 92,  4 bytes
  // Plan 0041 Milestone 2 (Spec 0041 R5, ADR-0089 Decision 4): appended
  // after every existing field (none moves); an explicit tail pad (now
  // alphaCutoff, below) made sizeof a provable sum (Plan 0041 P3).
  float emissiveFactor[3] = {};     // offset 96, 12 bytes
  // Plan 0042 Milestone 2 (Spec 0042 R6, ADR-0090 Decision 4): the
  // explicit tail pad Plan 0041 left is now alphaCutoff -- same offset,
  // same size, so no struct grows. 0 unless the material is Mask.
  float alphaCutoff = 0.0f;         // offset 108, 4 bytes
};

static_assert(std::is_standard_layout_v<PbrClearcoatPushConstants>);
static_assert(alignof(PbrClearcoatPushConstants) == 16);
static_assert(offsetof(PbrClearcoatPushConstants, objectToWorld) == 0);
static_assert(offsetof(PbrClearcoatPushConstants, baseColorFactor) == 64);
static_assert(offsetof(PbrClearcoatPushConstants, metallicFactor) == 80);
static_assert(offsetof(PbrClearcoatPushConstants, roughnessFactor) == 84);
static_assert(offsetof(PbrClearcoatPushConstants, clearcoatFactor) == 88);
static_assert(offsetof(PbrClearcoatPushConstants, clearcoatRoughness) == 92);
static_assert(offsetof(PbrClearcoatPushConstants, emissiveFactor) == 96);
static_assert(offsetof(PbrClearcoatPushConstants, alphaCutoff) == 108);
static_assert(sizeof(PbrClearcoatPushConstants) == 112);

}  // namespace atlantis::renderer
