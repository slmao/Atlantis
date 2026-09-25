#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/command_list.h>

#include <array>
#include <cstddef>
#include <unordered_map>

// Concrete Vulkan implementation of atlantis::rhi::CommandList
// (ADR-0020). See vulkan_device.cpp for where this is constructed
// (VulkanDevice::createCommandList()).
namespace atlantis::vulkan_backend::detail {

class VulkanSampledTexture;
class VulkanSampler;
class VulkanHdrColorTarget;
class VulkanShadowMap;

// Owns its VkCommandBuffer's allocation from device/commandPool (frees it
// in its destructor via vkFreeCommandBuffers) but does not own device or
// commandPool themselves -- both must outlive this object (caller-
// enforced; VulkanDevice's own single-retained-submission state machine
// is what guarantees this in practice, see VulkanDevice's header
// comment). transitionResource()/clearColor()/beginRendering()/
// copyRenderTargetToBuffer()'s rhi::RenderTarget& argument may be either
// of this module's two concrete RenderTarget implementations
// (VulkanRenderTarget, VulkanOffscreenRenderTarget, Spec 0010/ADR-0038)
// -- each resolves it via a checked pointer-form dynamic_cast to the
// shared private VulkanRenderTargetAccess interface, never a static_cast
// (only Vulkan Backend ever constructs a RenderTarget in Phase 1,
// ADR-0001's single-backend constraint, but which of the two concrete
// types is no longer assumable at compile time). Caller-owned only while
// being recorded into; ownership transfers to VulkanDevice at submit() --
// a caller never destroys one it has submitted. Not copyable, not
// movable, not thread-safe.
class VulkanCommandList final : public atlantis::rhi::CommandList {
 public:
  // cmdBeginRendering/cmdEndRendering: VulkanDevice's own resolved
  // dynamic-rendering entry points (Section 8/10 -- whichever of the
  // Core/Extension path that Device selected), borrowed for this
  // CommandList's whole lifetime; VulkanDevice must outlive it (same
  // caller-enforced tier as device/commandPool below).
  VulkanCommandList(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer,
                     PFN_vkCmdBeginRenderingKHR cmdBeginRendering, PFN_vkCmdEndRenderingKHR cmdEndRendering);
  ~VulkanCommandList() override;

  VulkanCommandList(const VulkanCommandList&) = delete;
  VulkanCommandList& operator=(const VulkanCommandList&) = delete;
  VulkanCommandList(VulkanCommandList&&) = delete;
  VulkanCommandList& operator=(VulkanCommandList&&) = delete;

  void transitionResource(atlantis::rhi::RenderTarget& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override;
  void transitionResource(atlantis::rhi::Texture& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override;
  void transitionResource(atlantis::rhi::SampledTexture& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override;
  void clearColor(atlantis::rhi::RenderTarget& target, atlantis::rhi::ClearColorValue color) override;

  void beginRendering(atlantis::rhi::RenderTarget& color, atlantis::rhi::Texture* depth,
                       atlantis::rhi::ClearColorValue colorClear, float depthClear) override;
  void endRendering() override;

  void bindPipeline(atlantis::rhi::Pipeline& pipeline) override;
  void bindVertexBuffer(atlantis::rhi::Buffer& buffer) override;
  void bindIndexBuffer(atlantis::rhi::Buffer& buffer) override;
  void bindUniformBuffer(atlantis::rhi::Buffer& buffer) override;
  // Renderer calls this immediately after bindUniformBuffer() for a
  // textured Material's draw items (Spec 0016/D3) -- see this method's
  // own .cpp body comment for why that order is safe. const&, matching
  // CommandList's own interface declaration -- see that header's own
  // comment for why (only const-qualified accessors are ever read here).
  void bindTexture(std::uint32_t binding, const atlantis::rhi::SampledTexture& texture,
                   const atlantis::rhi::Sampler& sampler) override;
  void pushConstant(const void* data, std::size_t sizeBytes) override;
  void drawIndexed(std::uint32_t indexCount) override;
  void copyRenderTargetToBuffer(atlantis::rhi::RenderTarget& source, atlantis::rhi::Buffer& destination) override;
  void copyBufferToTexture(atlantis::rhi::Buffer& source, atlantis::rhi::SampledTexture& destination) override;
  void copyBufferToTexture(atlantis::rhi::Buffer& source, atlantis::rhi::SampledTexture& destination,
                           std::span<const atlantis::rhi::SampledTextureUploadRegion> regions) override;

  // Plan 0024 Milestone 2 (ADR-0068 D-1/D-3): three new/overloaded
  // methods for HdrColorTarget -- a fourth transitionResource()
  // overload, a second beginRendering() overload (used only for the
  // geometry pass writing into an HdrColorTarget), and a second
  // bindTexture() overload (used only by the output-transform pass to
  // sample it, at binding 0 -- the output-transform descriptor
  // contract's own sole binding, unlike the material contracts' own
  // binding 1, ADR-0068 D-10).
  void transitionResource(atlantis::rhi::HdrColorTarget& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override;
  void beginRendering(atlantis::rhi::HdrColorTarget& color, atlantis::rhi::Texture* depth,
                       atlantis::rhi::ClearColorValue colorClear, float depthClear) override;
  void bindTexture(std::uint32_t binding, const atlantis::rhi::HdrColorTarget& texture,
                   const atlantis::rhi::Sampler& sampler) override;

  // Plan 0027 Milestone 2 (ADR-0072 D-4): three new/overloaded methods
  // for ShadowMap -- a fifth transitionResource() overload, a genuinely
  // depth-only beginRendering() overload (no color attachment at all,
  // unlike every overload above), and a third bindTexture() overload
  // (memoized like the SampledTexture one -- the shadow-map binding is
  // re-issued once per PbrDirectLit/pbr_ibl DrawItem, not once per
  // frame like the HdrColorTarget overload above).
  void transitionResource(atlantis::rhi::ShadowMap& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override;
  void beginRendering(atlantis::rhi::ShadowMap& depth, float depthClear) override;
  void bindTexture(std::uint32_t binding, const atlantis::rhi::ShadowMap& texture,
                   const atlantis::rhi::Sampler& sampler) override;

  // Exists solely for VulkanDevice::submit() (vkEndCommandBuffer,
  // vkQueueSubmit) -- never reached from RHI's public surface.
  [[nodiscard]] VkCommandBuffer commandBuffer() const noexcept { return commandBuffer_; }

 private:
  VkDevice device_;
  VkCommandPool commandPool_;
  VkCommandBuffer commandBuffer_;
  PFN_vkCmdBeginRenderingKHR cmdBeginRendering_;
  PFN_vkCmdEndRenderingKHR cmdEndRendering_;

  // Set by bindPipeline(), read by bindUniformBuffer()/pushConstant() --
  // the currently-bound Pipeline's own VkPipelineLayout/VkDescriptorSet
  // (Section 10). Non-owning; null until the first bindPipeline() call in
  // this recording. A programmer error (ATLANTIS_CHECK) to call
  // bindUniformBuffer()/pushConstant() before any bindPipeline().
  VkPipelineLayout boundPipelineLayout_ = VK_NULL_HANDLE;
  VkDescriptorSet boundDescriptorSet_ = VK_NULL_HANDLE;
  std::uint32_t boundSampledTextureFirstBinding_ = 0;
  std::uint32_t boundSampledTextureBindingCount_ = 0;

  // Implementation-forced addition, discovered by Plan 0007 Section 15's
  // own multi-DrawItem GPU test: Vulkan invalidates a command buffer if
  // vkUpdateDescriptorSets() is called again on a VkDescriptorSet that
  // was already vkCmdBindDescriptorSets()'d earlier in the *same*
  // not-yet-submitted command buffer recording (its layout has no
  // VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, per Section 10's fixed,
  // minimal design). The real, load-bearing invariant this memo must
  // maintain is: **a given VkDescriptorSet's binding N may be written by
  // vkUpdateDescriptorSets() at most once per recording, no matter how
  // many other sets are bound in between** -- not the narrower "the two
  // most recent calls happened to touch the same set back-to-back"
  // reading a single last-touched scalar/slot can only approximate.
  //
  // Found the hard way, 2026-09-14 (Plan 0035 Milestone 5): the original
  // implementation here tracked only the SINGLE most-recently-touched
  // (VkDescriptorSet, resource) pair per binding, on the unstated
  // assumption that multiple DrawItems sharing one Material are always
  // drawn back-to-back. Plan 0035's own ~28-sphere showcase scene was
  // the first real draw order to violate that assumption -- 28 pedestal
  // nodes sharing one Material, each interleaved with a *different*
  // sphere Material's DrawItem in between -- so every revisit of the
  // shared pedestal set found the memo already overwritten by the
  // intervening sphere set, concluded (wrongly) that the pedestal set
  // had never been written this recording, and re-issued
  // vkUpdateDescriptorSets() on a set already bound earlier in this same
  // recording: VUID-vkCmdBindDescriptorSets-commandBuffer-recording,
  // observed as a real Validation Layer FATAL. Keying the memo by the
  // VkDescriptorSet itself (below), not merely by "was it the last one
  // touched," is what actually keeps this invariant regardless of draw
  // order/interleaving -- a real correctness fix, not a cache-eviction
  // tuning knob.
  //
  // Still per-recording only (not per-frame, not cross-CommandList,
  // never persisted): every VulkanCommandList instance owns exactly one
  // VkCommandBuffer allocation for its entire lifetime (VulkanDevice::
  // createCommandList() always calls vkAllocateCommandBuffers() fresh,
  // never vkResetCommandBuffer() on a reused instance -- see that
  // function's own body), so this member's own default-empty
  // construction already *is* "clear all memos at the start of every
  // recording" -- no separate reset step exists or is needed. Holds no
  // GPU resource, still never the general resource cache Section 10/
  // ADR-0025 deliberately avoids -- it remembers state already written
  // into a set this CommandList itself owns for this recording, nothing
  // shared or outliving it. bindUniformBuffer() still skips only the
  // exact redundant vkUpdateDescriptorSets() call, never the
  // vkCmdBindDescriptorSets() call itself (always re-issued, matching
  // Section 10's own stated "re-binds every draw item regardless, for
  // simplicity" design).
  std::unordered_map<VkDescriptorSet, VkBuffer> uniformBufferMemo_;

  // Same memo pattern and the same 2026-09-14 fix as uniformBufferMemo_
  // above, for bindTexture() (Spec 0016/D3) -- a textured Material
  // shared by multiple DrawItems calls bindTexture() again for every
  // item, each with byte-identical VkDescriptorImageInfo contents (the
  // one shared SampledTexture/Sampler pair), and the same non-
  // consecutive-revisit hazard applies per binding. Pointer identity
  // only, matching the buffer memo's own VkBuffer-handle-identity
  // comparison; no pointer here is ever dereferenced.
  struct TextureBindingMemo {
    const VulkanSampledTexture* texture = nullptr;
    // Plan 0027 Milestone 2 (ADR-0072 D-4/D-7): a binding index is always
    // exclusively one resource kind for any given Pipeline's own
    // descriptor layout (the shadow-map slot never coincides with a
    // SampledTexture binding), so this parallel field shares the same
    // per-binding memo slot rather than needing a second array.
    const VulkanShadowMap* shadowMap = nullptr;
    const VulkanSampler* sampler = nullptr;
  };
  // Plan 0027 Milestone 6 (ADR-0072 D-7): widened from 4 to 5 --
  // pbr_ibl's own new shadow-map slot is binding 4, which fails
  // ATLANTIS_CHECK(binding < kMaxTextureBindingSlots) in bindTexture()
  // at the old size. ADR-0072 D-7's own Accepted Amendment, 2026-09-06
  // (Plan 0029 Section P10): widened again to 6 -- pbr_ibl_normal_map's
  // own new normal-map slot is binding 5. ADR-0096 (Plan 0046 Milestone 1,
  // the ADR-0072 D-7 precedent): widened again to 7 -- pbr_ibl_normal_map's
  // emissive-texture slot is binding 6.
  static constexpr std::size_t kMaxTextureBindingSlots = 7;
  // Outer key: the VkDescriptorSet itself (2026-09-14 fix, see
  // uniformBufferMemo_'s own comment above for the full reasoning) --
  // default-constructed to an all-nullptr array of kMaxTextureBindingSlots
  // memo slots the first time any given set is touched this recording.
  std::unordered_map<VkDescriptorSet, std::array<TextureBindingMemo, kMaxTextureBindingSlots>>
      textureDescriptorMemos_;
};

}  // namespace atlantis::vulkan_backend::detail
