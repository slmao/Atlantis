#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include <vulkan/vulkan_core.h>

#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/texture.h>

#include "vulkan_descriptor_pool_growth.h"

// Concrete Vulkan implementation of atlantis::rhi::Device (ADR-0014). See
// vulkan_device.cpp for createDevice()'s full orchestration (instance
// creation, validation installation, physical device/queue-family
// selection, logical device/queue creation).
namespace atlantis::vulkan_backend::detail {

// Spec 0021/ADR-0064: one entry in VulkanDevice's own growable
// descriptor-pool set. Vulkan has no "query this pool's own maxSets"
// call, so this pairs each pool's own handle with the maxSets it was
// created with -- needed to compute the next generation's own size
// (descriptorPoolMaxSetsForGeneration()). Both members default to a
// safe "unused slot" value; every real read is bounded by
// VulkanDevice's own descriptorPoolCount_, so an untouched entry is
// never actually used.
struct DescriptorPoolEntry {
  VkDescriptorPool pool = VK_NULL_HANDLE;
  std::uint32_t maxSets = 0;
};

// Exclusively owns its VkInstance, the VkPhysicalDevice it selected, its
// VkDevice, the single VkQueue created from the selected combined
// graphics/present queue family, that queue family's index, (when
// validation is enabled) the explicit VkDebugUtilsMessengerEXT installed
// immediately after instance creation succeeds (see
// plans/0003-rhi-vulkan-windowed-foundation.md Section 6), and -- new in
// Plan 0006 -- a persistent VkCommandPool and a persistent submission
// VkFence used by createCommandList()/submit()/waitIdle() below. The
// render-finished semaphore submit() signals is NOT owned here -- found
// via GPU testing that a single Device-owned semaphore is not safe to
// reuse across frames (vkQueuePresentKHR's wait on it has no CPU-visible
// completion signal); VulkanPresentation owns one per swapchain image
// instead and threads the correct one through via RenderTarget (see
// VulkanPresentation's own header comment). No separate validation
// *state* beyond the messenger handle.
//
// Plan 0007: also owns the resolved dynamic-rendering entry-point pair
// (whichever of vkCmdBeginRendering/vkCmdEndRendering (core) or
// vkCmdBeginRenderingKHR/vkCmdEndRenderingKHR (extension) this Device's
// selected physical device resolved to, Section 8/ADR-0024) and --
// widened by Spec 0021/ADR-0064 -- a Device-owned, growable *set* of
// descriptor pools (a fixed-size std::array, never a std::vector: the
// Approved hard ceiling is a small, fixed, compile-time constant, so a
// std::array structurally cannot exceed it, and never risks the
// std::bad_alloc/leak-on-throw exposure a dynamically-growing container
// would on this render path). Starts with exactly one pool at
// construction (generation 0, maxSets = 4, unchanged from this
// codebase's own pre-Spec-0021 value); createPipeline()'s own
// allocateDescriptorSet() grows it -- one additional pool at a time,
// geometric doubling per the fixed kDescriptorPoolMaxSetsByGeneration
// table -- only on real, observed exhaustion, up to
// kMaxDescriptorPoolCount pools total (60 concurrent descriptor sets).
// Never exposed on this class's own public accessor surface beyond the
// narrow, Vulkan-Backend-internal accessors VulkanCommandList/
// VulkanPipeline need.
//
// Not copyable, not movable -- held exclusively behind
// std::unique_ptr<atlantis::rhi::Device> (ADR-0014); nothing in this
// Plan needs to relocate a VulkanDevice, so this simplifies destruction-
// order reasoning at no real cost. Not internally thread-safe;
// construction, use, and destruction all happen on the single Phase 1
// logical frame thread (ADR-0004). No global mutable state.
//
// Single frame-in-flight submission ownership (ADR-0020, Plan 0006
// Section 9): submit() takes ownership of the CommandList passed to it
// and retains it (plus submissionFence_) until the *next* submit() call,
// waitIdle(), or this destructor releases it -- a caller never manages a
// fence or decides when a CommandList is safe to destroy. Precondition,
// not enforced by the type system, confirmed by Human Review as an
// accepted design constraint: a *windowed* caller must call
// VulkanPresentation::present() for a successful submit() before calling
// submit() again -- submit() followed directly by application exit
// remains legal (waitIdle() drains it). See
// plans/0006-rhi-render-graph-frame-execution-foundation.md. A *headless*
// caller (Spec 0010/ADR-0038) never constructs a VulkanPresentation and
// therefore never calls present() at all -- its own repeated-submit()
// safety comes entirely from submit()'s existing internal single-frame-
// in-flight fence-wait (waitAndReleaseRetainedSubmission() below), not
// from present().
//
// The accessor methods below exist solely for VulkanPresentation (build a
// VkSurfaceKHR, run the concrete-surface presentation-support check,
// vkQueuePresentKHR) and VulkanCommandList/VulkanDevice's own
// createCommandList()/submit() bodies -- never reach RHI's or Vulkan
// Backend's public surface, and are not a general GPU-handle escape
// hatch for a future RenderGraph, Renderer, or second backend.
class VulkanDevice final : public atlantis::rhi::Device {
 public:
  // destroyMessengerFn must be non-null whenever explicitMessenger is not
  // VK_NULL_HANDLE (validation enabled), and is otherwise never
  // dereferenced. Stored rather than re-resolved via vkGetInstanceProcAddr
  // at destruction time -- necessary destruction bookkeeping, not
  // additional diagnostics state. commandPool/submissionFence are both
  // already-created, valid handles this constructor takes ownership of
  // (created by createDevice() below, guard-protected until this
  // constructor succeeds -- same two-phase ownership-transfer pattern as
  // instance/device/messenger). No render-finished semaphore is owned
  // here -- found via GPU testing that a single Device-owned one is not
  // safe to reuse across frames; VulkanPresentation now owns one per
  // swapchain image instead (see that class's own header comment).
  VulkanDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
               std::uint32_t queueFamilyIndex, VkDebugUtilsMessengerEXT explicitMessenger,
               PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn, VkCommandPool commandPool,
               VkFence submissionFence, PFN_vkCmdBeginRenderingKHR cmdBeginRendering,
               PFN_vkCmdEndRenderingKHR cmdEndRendering, VkDescriptorPool descriptorPool);
  ~VulkanDevice() override;

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;
  VulkanDevice(VulkanDevice&&) = delete;
  VulkanDevice& operator=(VulkanDevice&&) = delete;

  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::CommandList>, atlantis::rhi::CommandListCreateError>
  createCommandList() override;
  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::SubmissionSignal>, atlantis::rhi::SubmitError> submit(
      std::unique_ptr<atlantis::rhi::CommandList> commandList, const atlantis::rhi::RenderTarget& target) override;
  [[nodiscard]] atlantis::Result<std::monostate, atlantis::rhi::SubmitError> waitIdle() override;

  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Buffer>, atlantis::rhi::BufferCreateError>
  createBuffer(const atlantis::rhi::BufferCreateParams& params) override;
  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Texture>, atlantis::rhi::TextureCreateError>
  createTexture(const atlantis::rhi::TextureCreateParams& params) override;
  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Pipeline>, atlantis::rhi::PipelineCreateError>
  createPipeline(const atlantis::rhi::PipelineCreateParams& params) override;
  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::OffscreenTarget>,
                                  atlantis::rhi::OffscreenTargetCreateError>
  createOffscreenTarget(const atlantis::rhi::OffscreenTargetCreateParams& params) override;

  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::SampledTexture>,
                                  atlantis::rhi::SampledTextureCreateError>
  createSampledTexture(const atlantis::rhi::SampledTextureCreateParams& params) override;
  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Sampler>, atlantis::rhi::SamplerCreateError>
  createSampler(const atlantis::rhi::SamplerCreateParams& params) override;

  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::HdrColorTarget>,
                                  atlantis::rhi::HdrColorTargetCreateError>
  createHdrColorTarget(const atlantis::rhi::HdrColorTargetCreateParams& params) override;

  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::ShadowMap>, atlantis::rhi::ShadowMapCreateError>
  createShadowMap(const atlantis::rhi::ShadowMapCreateParams& params) override;

  [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
  [[nodiscard]] VkDevice device() const noexcept { return device_; }
  [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
  [[nodiscard]] std::uint32_t queueFamilyIndex() const noexcept { return queueFamilyIndex_; }

  // Never exposed on rhi::Device's own interface -- narrow, Vulkan-
  // Backend-internal accessors for VulkanCommandList (beginRendering()/
  // endRendering(), Section 8/10) and VulkanPipeline (descriptor set
  // allocation, Section 10) only.
  [[nodiscard]] PFN_vkCmdBeginRenderingKHR cmdBeginRendering() const noexcept { return cmdBeginRendering_; }
  [[nodiscard]] PFN_vkCmdEndRenderingKHR cmdEndRendering() const noexcept { return cmdEndRendering_; }

  // Waits on submissionFence_ (if a submission is currently retained),
  // resets it, and releases retainedSubmission_ -- the shared first step
  // of submit(), waitIdle(), and the destructor. Returns Err only for a
  // genuine vkWaitForFences/vkResetFences failure; a no-op (Ok) if
  // nothing is currently retained.
  //
  // Public (not part of rhi::Device's interface -- a narrow, Vulkan-
  // Backend-internal accessor, same category as instance()/queue() above)
  // specifically so VulkanPresentation::acquireNextTarget() can call it
  // before vkAcquireNextImageKHR re-signals the persistent acquire-
  // complete semaphore that semaphore's own reuse is only valid once the
  // previous frame's vkQueueSubmit wait on it has fully retired on the
  // GPU -- which submissionFence_ signaling guarantees, but only once
  // this is called. Found via GPU testing (Plan 0006's own Human-Review-
  // approved single-frame-in-flight design assumed this was already
  // guaranteed by submit()'s own internal wait; it was not, since that
  // wait only ran before the *next* submit(), not before the *next*
  // acquire, which reuses the same semaphore first) --
  // VK_LAYER_KHRONOS_validation correctly rejected the resulting
  // "semaphore must not have any pending operations" reuse hazard.
  [[nodiscard]] atlantis::Result<std::monostate, atlantis::rhi::SubmitError> waitAndReleaseRetainedSubmission();

 private:
  // Spec 0021/ADR-0064: scans descriptorPools_[0, descriptorPoolCount_)
  // in creation order, allocating one VkDescriptorSet of the given
  // layout from the first pool with capacity -- naturally reusing
  // whatever capacity an earlier vkFreeDescriptorSets call already
  // returned to any of them. Grows (creates exactly one new pool, per
  // the fixed generation table, vulkan_descriptor_pool_growth.h) and
  // retries exactly once, only after every existing pool has failed for
  // a growth-eligible reason (isDescriptorPoolGrowthEligible()) and the
  // hard ceiling (kMaxDescriptorPoolCount) has not yet been reached. On
  // success, outDescriptorSet/outOriginPool are both set and this
  // returns std::nullopt; on any failure, neither out-param is touched
  // and this returns PipelineCreateError::DescriptorSetAllocationFailed
  // -- the only error this function can ever produce. Called only from
  // createPipeline().
  //
  // Termination, exact worst case: at most kMaxDescriptorPoolCount (4)
  // vkAllocateDescriptorSets calls total, never 5 -- either every
  // existing pool is already at the ceiling and all 4 are scanned with
  // no growth attempted (4 scans, 0 retries), or fewer than 4 exist, all
  // scanned (at most 3 scans) and exactly one retry follows a single
  // growth event (at most 3 + 1 = 4). These two cases are mutually
  // exclusive (Step 2's own ceiling check gates growth), never additive
  // -- growth and a full 4-pool scan cannot both contribute their own
  // maximum in the same call. At most one vkCreateDescriptorPool call.
  // No loop, no recursion -- always terminates.
  [[nodiscard]] std::optional<atlantis::rhi::PipelineCreateError> allocateDescriptorSet(
      VkDescriptorSetLayout layout, VkDescriptorSet& outDescriptorSet, VkDescriptorPool& outOriginPool);

  VkInstance instance_;
  VkPhysicalDevice physicalDevice_;
  VkDevice device_;
  VkQueue queue_;
  std::uint32_t queueFamilyIndex_;
  VkDebugUtilsMessengerEXT explicitMessenger_;  // VK_NULL_HANDLE when validation is disabled
  PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn_;  // meaningful only when explicitMessenger_ is set

  VkCommandPool commandPool_;
  VkFence submissionFence_;
  std::unique_ptr<atlantis::rhi::CommandList> retainedSubmission_;  // null until the first submit()
  bool hasRetainedSubmission_ = false;

  // Plan 0007 Section 8: resolved once at construction, from whichever of
  // the Core/Extension dynamic-rendering paths createDevice() selected --
  // never re-resolved, never both non-null (exactly one path is active
  // per Device instance).
  PFN_vkCmdBeginRenderingKHR cmdBeginRendering_;
  PFN_vkCmdEndRenderingKHR cmdEndRendering_;

  // Spec 0021/ADR-0064 (originally Plan 0007 Section 10's own single,
  // fixed-capacity pool): a fixed-size array, sized to the Approved
  // hard ceiling at compile time -- never a std::vector (a dynamically-
  // growing container would risk a std::bad_alloc/leak-on-throw
  // exposure on this render path; see Plan 0021's own Final Review
  // Round). Only indices [0, descriptorPoolCount_) are ever live; every
  // entry at or past that index holds DescriptorPoolEntry's own default
  // value and is never read, written past its own creation, or
  // destroyed. Backs exactly one VkDescriptorSet per VulkanPipeline
  // (allocated at that Pipeline's own construction, freed at its own
  // destruction, back to its own specific origin pool) -- never exposed
  // outside vulkan_pipeline.*/vulkan_device.*.
  std::array<DescriptorPoolEntry, kMaxDescriptorPoolCount> descriptorPools_{};
  std::size_t descriptorPoolCount_ = 0;
};

}  // namespace atlantis::vulkan_backend::detail
