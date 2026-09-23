#pragma once

#include <cstddef>

namespace atlantis::renderer {

// Plan 0035 Milestone 4 (ADR-0081 D-3/D-4): PbrAnisotropic's own,
// independent push-constant layout -- mirrors PbrClearcoatPushConstants's
// own shape (pbr_clearcoat_push_constants.h) exactly: two plain trailing
// scalars, unlike PbrSheenPushConstants's own vec3 (which needed 8
// bytes of alignment padding, pbr_sheen_push_constants.h). anisotropyFactor
// and anisotropyRotation pack tightly right after roughnessFactor
// (offset 88-96), no padding needed. This exact layout is confirmed by
// a real Slang reflection (atlantis_shader_compiler's own
// pbr-anisotropic-ibl/pbr-anisotropic-ibl-normal-map contract
// validation, compile_and_validate.cpp) AND by this struct's own
// static_asserts below -- ADR-0067 D-3's required double confirmation,
// not assumed.
//
// Total: 96 bytes, comfortably under Vulkan's 128-byte guaranteed
// minimum (32 bytes of headroom) -- the same as PbrClearcoatPushConstants.
struct alignas(16) PbrAnisotropicPushConstants {
  float objectToWorld[16] = {};    // offset 0,  64 bytes
  float baseColorFactor[4] = {};   // offset 64, 16 bytes
  float metallicFactor = 0.0f;     // offset 80,  4 bytes
  float roughnessFactor = 0.0f;    // offset 84,  4 bytes
  float anisotropyFactor = 0.0f;   // offset 88,  4 bytes
  float anisotropyRotation = 0.0f; // offset 92,  4 bytes
  // Plan 0041 Milestone 2 (Spec 0041 R5, ADR-0089 Decision 4): appended
  // after every existing field (none moves); an explicit tail pad (now
  // alphaCutoff, below) made sizeof a provable sum (Plan 0041 P3).
  float emissiveFactor[3] = {};    // offset 96, 12 bytes
  // Plan 0042 Milestone 2 (Spec 0042 R6, ADR-0090 Decision 4): the
  // explicit tail pad Plan 0041 left is now alphaCutoff -- same offset,
  // same size, so no struct grows. 0 unless the material is Mask.
  float alphaCutoff = 0.0f;        // offset 108, 4 bytes
};

static_assert(sizeof(PbrAnisotropicPushConstants) == 112);
static_assert(offsetof(PbrAnisotropicPushConstants, objectToWorld) == 0);
static_assert(offsetof(PbrAnisotropicPushConstants, baseColorFactor) == 64);
static_assert(offsetof(PbrAnisotropicPushConstants, metallicFactor) == 80);
static_assert(offsetof(PbrAnisotropicPushConstants, roughnessFactor) == 84);
static_assert(offsetof(PbrAnisotropicPushConstants, anisotropyFactor) == 88);
static_assert(offsetof(PbrAnisotropicPushConstants, anisotropyRotation) == 92);
static_assert(offsetof(PbrAnisotropicPushConstants, emissiveFactor) == 96);
static_assert(offsetof(PbrAnisotropicPushConstants, alphaCutoff) == 108);

}  // namespace atlantis::renderer
