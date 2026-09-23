#pragma once

#include <cstddef>
#include <type_traits>

namespace atlantis::renderer {

// Plan 0023 Milestone 3 (ADR-0067 D-3): the exact, real-Slang-and-MSVC-
// confirmed 96-byte push-constant layout for MaterialPushConstantLayout::
// PbrDirectLit (Milestone 5). objectToWorld keeps ObjectToWorldOnly's
// existing offset/size (0, 64 bytes) unchanged -- only PbrDirectLit's own
// Pipeline gets the wider range; the three existing 64-byte call sites
// (material_realization.cpp:176,301,324) are unaffected.
//
// Deliberately a PRIVATE header (src/renderer/src/, not
// src/renderer/include/atlantis/renderer/) -- found and corrected during
// Plan 0023's own final review: this struct carries no cross-module
// contract of its own (unlike CameraWorldPositionData, which multiple
// composition roots outside Runtime genuinely write into a shared
// buffer) -- renderer.cpp is its own sole real production consumer, and
// including it from a test target uses the same relative-path pattern
// tests/shader_system/json_parser_tests.cpp's own inclusion of
// src/shader_system/src/json_parser.h already establishes for a
// module's own private header. Exposing it under this module's public
// include/ would have silently widened Atlantis::Renderer's own public
// API surface with a type neither Spec 0023, Plan 0023, nor ADR-0067
// ever named a header location for -- narrowing it back to private,
// zero-functionality-change, is the conservative resolution.
struct alignas(16) PbrPushConstants {
  float objectToWorld[16] = {};    // offset 0,  64 bytes -- unchanged
  float baseColorFactor[4] = {};   // offset 64, 16 bytes
  float metallicFactor = 0.0f;     // offset 80,  4 bytes
  float roughnessFactor = 0.0f;    // offset 84,  4 bytes
  float _pad[2] = {0.0f, 0.0f};    // offset 88,  8 bytes, explicit -- not compiler-implicit
  // Plan 0041 Milestone 2 (Spec 0041 R5, ADR-0089 Decision 4): appended
  // after every existing field (none moves); an explicit tail pad (now
  // alphaCutoff, below) made sizeof a provable sum (Plan 0041 P3).
  float emissiveFactor[3] = {};    // offset 96, 12 bytes
  // Plan 0042 Milestone 2 (Spec 0042 R6, ADR-0090 Decision 4): the
  // explicit tail pad Plan 0041 left is now alphaCutoff -- same offset,
  // same size, so no struct grows. 0 unless the material is Mask.
  float alphaCutoff = 0.0f;        // offset 108, 4 bytes
};

static_assert(std::is_standard_layout_v<PbrPushConstants>);
static_assert(alignof(PbrPushConstants) == 16);
static_assert(offsetof(PbrPushConstants, objectToWorld) == 0);
static_assert(offsetof(PbrPushConstants, baseColorFactor) == 64);
static_assert(offsetof(PbrPushConstants, metallicFactor) == 80);
static_assert(offsetof(PbrPushConstants, roughnessFactor) == 84);
static_assert(offsetof(PbrPushConstants, _pad) == 88);
static_assert(offsetof(PbrPushConstants, emissiveFactor) == 96);
static_assert(offsetof(PbrPushConstants, alphaCutoff) == 108);
static_assert(sizeof(PbrPushConstants) == 112);

}  // namespace atlantis::renderer
