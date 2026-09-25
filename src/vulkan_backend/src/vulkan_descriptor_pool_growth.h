#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Spec 0021 D5/D6, ADR-0064: the descriptor-pool growth strategy's own
// fixed capacity table. Pure, GPU-independent, deterministic -- safe to
// unit-test with literal integer inputs and no real Vulkan call,
// matching vulkan_result.h's own established "pure classification"
// precedent. A fixed table, not a computed doubling function: this
// locks every real pool's own maxSets to the literal, Approved
// sequence (4, 8, 16, 32, 64, 128, 256 -- seven values since ADR-0064's
// Accepted Amendment of 2026-09-26), structurally incapable of producing
// an eighth-generation or otherwise unapproved value.
namespace atlantis::vulkan_backend::detail {

// The total number of pools VulkanDevice's own growable set may ever
// contain (Spec 0021 D6) -- a leak/defect safety net, never a
// content-scaling ceiling. ADR-0064 Accepted Amendment 2026-09-26 (Plan
// 0046 M3): widened 4 -> 7, since Bistro's ~270 Pipelines (one set each)
// had made the 60-set ceiling exactly that.
inline constexpr std::size_t kMaxDescriptorPoolCount = 7;

// Index 0 is the initial pool (createDevice()'s own one-time creation);
// index 1 is the first growth generation; and so on. Geometric doubling
// from 4 (Spec 0021 D5) -- written here as the four literal, Approved
// values themselves, not derived at runtime, so this table alone is the
// single, complete, exact contract. Summing all seven gives the real,
// current hard ceiling on concurrent descriptor sets:
// 4+8+16+32+64+128+256 = 508 (was the first four, 60, until the ADR-0064
// amendment of 2026-09-26).
inline constexpr std::array<std::uint32_t, kMaxDescriptorPoolCount> kDescriptorPoolMaxSetsByGeneration = {
    4, 8, 16, 32, 64, 128, 256};

// kDescriptorPoolMaxSetsByGeneration[generationIndex]. generationIndex
// must be < kMaxDescriptorPoolCount -- a violated precondition here is
// a programmer error (every real call site is already gated by
// VulkanDevice's own ceiling check before this is ever called), not a
// recoverable runtime condition, so it is an ATLANTIS_CHECK, matching
// AGENTS.md's own "programmer errors are assertions, not error
// returns" rule -- never a std::optional or an out-of-range value
// silently substituted.
[[nodiscard]] std::uint32_t descriptorPoolMaxSetsForGeneration(std::size_t generationIndex);

}  // namespace atlantis::vulkan_backend::detail
