#pragma once

#include <cstddef>
#include <type_traits>

namespace atlantis::renderer {

// Plan 0023 Milestone 3 (ADR-0067 D-3): the exact, real-Slang-and-MSVC-
// confirmed 96-byte push-constant layout for MaterialPushConstantLayout::
// PbrDirectLit (Milestone 5). objectToWorld keeps ObjectToWorldOnly's
// existing offset/size (0, 64 bytes) unchanged -- only PbrDirectLit's own
// Pipeline gets the wider range; the three existing 64-byte call sites
// (material_realization.cpp:176,301,324) are unaffected. Owned by this
// module (not Runtime) because Renderer::drawFrame() (Milestone 5) is the
// sole assembler and pushConstant() caller for this exact byte layout --
// mirrors CameraWorldPositionData's own explicit-tail-pad,
// static_assert-pinned style (scene_extraction.h), applied here to the
// module that actually writes these bytes.
struct alignas(16) PbrPushConstants {
  float objectToWorld[16] = {};    // offset 0,  64 bytes -- unchanged
  float baseColorFactor[4] = {};   // offset 64, 16 bytes
  float metallicFactor = 0.0f;     // offset 80,  4 bytes
  float roughnessFactor = 0.0f;    // offset 84,  4 bytes
  float _pad[2] = {0.0f, 0.0f};    // offset 88,  8 bytes, explicit -- not compiler-implicit
};

static_assert(std::is_standard_layout_v<PbrPushConstants>);
static_assert(alignof(PbrPushConstants) == 16);
static_assert(offsetof(PbrPushConstants, objectToWorld) == 0);
static_assert(offsetof(PbrPushConstants, baseColorFactor) == 64);
static_assert(offsetof(PbrPushConstants, metallicFactor) == 80);
static_assert(offsetof(PbrPushConstants, roughnessFactor) == 84);
static_assert(offsetof(PbrPushConstants, _pad) == 88);
static_assert(sizeof(PbrPushConstants) == 96);

}  // namespace atlantis::renderer
