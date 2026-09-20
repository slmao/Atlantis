# ADR 0086: RHI Index-Type Public Expression and Backward-Compatibility Strategy

- **Status:** Proposed
- **Date:** 2026-09-20
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0039](../specs/0039-rhi-index-type-parameterization.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

The RHI has no index-width concept. `BufferPurpose::Index`
(`src/rhi/include/atlantis/rhi/types.h:101-107`) says a buffer holds indices
but not how wide they are, `BufferCreateParams` carries only a purpose and a
size (`types.h:150-153`), `Buffer` exposes only `purpose()`, `sizeBytes()` and
`mappedData()` (`src/rhi/include/atlantis/rhi/buffer.h:17-35`), and
`CommandList::bindIndexBuffer(Buffer&)` (`command_list.h:64`) takes nothing
else. The width is therefore decided in exactly one place, by the backend, as
a literal: `VK_INDEX_TYPE_UINT16` at
`src/vulkan_backend/src/vulkan_command_list.cpp:387` — the only
`VK_INDEX_TYPE` token in the repository.

This was correct while every mesh was `uint16_t`. It stopped being correct
when [ADR-0083](0083-gltf-to-atlantis-asset-format-mapping.md) D2 gave
importer-produced `.amesh` artifacts `uint32_t` indices, because 3 of Bistro's
551 primitives exceed 65,535 vertices. Those 551 artifacts exist and validate
today; none of them can be drawn. Spec 0036 workflow ①b exists to close that,
and it is a hard dependency of workflow ⑦.

The forces that make the choice non-obvious:

- **Vulkan's own shape argues for bind time.** `vkCmdBindIndexBuffer` takes
  the index type as a per-bind argument, not a buffer property. A design that
  mirrors the backend is usually the safe default for an RHI, and departing
  from it needs a reason.
- **The existing call surface argues for creation time.** There are 24
  `createBuffer({.purpose = BufferPurpose::Index, ...})` sites, 12
  `bindIndexBuffer()` call sites, two `CommandList` implementations
  (`vulkan_command_list.h:68`, `tests/render_graph/fake_command_list.h:322`)
  and two `Buffer` implementations (`vulkan_buffer.h:25`,
  `fake_command_list.h:195`). Not all of these are meshes: the
  output-transform pass builds a bare `std::uint16_t[3]` index buffer directly
  through `createBuffer()` (`src/runtime/src/runtime_application.cpp:824-835`),
  as do a dozen image-regression fixtures.
- **Renderer must expose the width too**, because `createMesh()` takes
  `const std::uint16_t*` (`src/renderer/include/atlantis/renderer/mesh.h:51`)
  and has 20 call sites across `src/`, `examples/` and tests.
- **An optional Vulkan feature is in play.** `VkDeviceCreateInfo` never sets
  `pEnabledFeatures` (`src/vulkan_backend/src/vulkan_device.cpp:1836-1842`), so
  `fullDrawIndexUint32` is `VK_FALSE` and `maxDrawIndexedIndexValue` is only
  guaranteed to be at least 2^24−1 rather than 2^32−1. A decision that ignores
  this would be quietly promising range the engine has not asked for.
- **AGENTS.md's Phase 1 constraint** requires the RHI interface to stay
  backend-independent while Vulkan remains the only backend, so the chosen
  expression may not be a `VkIndexType` in disguise.

## Decision

**The index type is a property of an index Buffer, fixed when that Buffer is
created, defaulted to 16-bit, and read back by the backend at bind time.**

1. **RHI gains a backend-independent `enum class IndexType { Uint16, Uint32 };`**
   in `rhi/types.h`, alongside the existing `BufferPurpose`.
2. **`BufferCreateParams` gains `IndexType indexType = IndexType::Uint16;`**.
   The field is meaningful only when `purpose == BufferPurpose::Index` and is
   ignored for every other purpose. `operator==(BufferCreateParams)`
   (`src/rhi/src/types.cpp:13-15`) compares it.
3. **`rhi::Buffer` gains `[[nodiscard]] virtual IndexType indexType() const = 0;`**,
   with the precondition `purpose() == BufferPurpose::Index`, enforced by
   `ATLANTIS_CHECK` in each implementation — the same "programmer error, not a
   recoverable outcome" treatment `bindIndexBuffer()` already gives a wrong
   purpose (`command_list.h:60-62`).
4. **`CommandList::bindIndexBuffer(Buffer&)` keeps its exact signature.** The
   Vulkan Backend reads `buffer.indexType()` and maps it at
   `vulkan_command_list.cpp:387`: `Uint16` → `VK_INDEX_TYPE_UINT16`, `Uint32` →
   `VK_INDEX_TYPE_UINT32`. `drawIndexed(std::uint32_t indexCount)` counts
   indices and is unchanged.
5. **Renderer gains a second `createMesh()` overload** taking
   `const std::uint32_t* indices`, sharing the existing body and passing
   `IndexType::Uint32` when it creates the index Buffer. The `uint16_t`
   overload keeps its exact signature. **`renderer::Mesh` gains no
   index-width state** — it continues to own two Buffers and a count
   (`mesh.h:16-35`), and the width lives in the index Buffer it already owns.
   ADR-0022's ownership model is untouched.
6. **Index *values* must be below 2^24.** `fullDrawIndexUint32` is
   deliberately **not** enabled, so the engine relies only on the
   specification's guaranteed `maxDrawIndexedIndexValue` floor of 2^24−1
   (16,777,215). A mesh with a larger index value is rejected by the producing
   or loading layer with a named error, not bound and hoped for. Raising this
   ceiling later means enabling the feature and requiring it in
   physical-device selection, which is a new decision and therefore a new ADR.

This ADR covers the RHI surface, its Vulkan mapping, and the Renderer entry
point that feeds it. The Asset System's own CPU-side index-width
representation is a separate decision, recorded in
[ADR-0087](0087-asset-system-index-width-representation-and-artifact-version-dispatch.md),
because ADR-0043's Decision forbids Asset System from naming any RHI type.

## Consequences

### Positive

- **A buffer/width mismatch becomes unrepresentable.** The bytes and the width
  are decided in the same call. With a bind-time parameter, any of 12 bind
  sites could pass the wrong one against a correct buffer, and only the
  Validation Layers or a garbage frame would say so.
- **It matches the type's existing grain.** A Buffer is already "fixed to one
  of three purposes at creation" (`buffer.h:9-10`, ADR-0023) and, once
  constructed, is never re-uploaded or mutated (`mesh.h:13-14`). Index width is
  the same kind of fact as purpose, and is now stated the same way.
- **Backward compatibility is total at the call sites.** A defaulted struct
  field leaves all 24 index-buffer creation sites, all 12 bind sites, both
  `CommandList` implementations and all 20 `createMesh()` calls compiling and
  behaving exactly as before. The `uint16_t` path's GPU bytes and draw calls do
  not change, which is what makes "every existing golden stays green" a real
  proof rather than a hope.
- **The backend stays a mapping layer.** One literal becomes one two-case
  switch; the backend acquires no policy.
- **It sidesteps a real C++ hazard.** Keeping the bind-time signature
  unchanged avoids `bindIndexBuffer(Buffer&, IndexType = IndexType::Uint16)`,
  whose default argument would be bound statically to the declared type and
  could differ per override.

### Negative / Trade-offs

- **`BufferCreateParams` carries a field that is meaningless for four of its
  five purposes.** A Vertex or Uniform buffer now has an `indexType` that
  nothing reads. The alternative — a purpose-specific params union or
  per-purpose factory functions — is a much larger change to a type that is
  otherwise working.
- **`Buffer::indexType()` is a partial function.** Calling it on a non-Index
  Buffer aborts. That is deliberate (a meaningless value would be worse than a
  loud stop) but it is a sharp edge in a small public interface.
- **Adding a pure virtual to `rhi::Buffer` breaks every implementer.** In-tree
  that is two files; anyone maintaining an out-of-tree RHI implementation would
  have to follow. Atlantis ships none.
- **It departs from Vulkan's own shape**, so a future backend whose native API
  genuinely wants a per-bind type would carry the value on its buffer object
  and pass it at bind time. That is a small, local adaptation, but it is real.
- **A buffer whose index width must change requires a new buffer.** No caller
  does this today, and re-uploading a Buffer is already outside ADR-0023's
  model.
- **The 2^24−1 ceiling is a real limit the engine now owns.** It is 131× above
  Bistro's largest primitive, but it is a number that has to be stated, tested
  against, and revisited if content ever grows past it.

## Alternatives Considered

- **Index type as a `bindIndexBuffer(Buffer&, IndexType)` parameter.** The
  shape Vulkan itself uses, and the most obvious candidate. Lost on three
  counts: it makes a buffer/width mismatch representable at 12 call sites; it
  changes a pure virtual, so both implementations, the render-graph fake and
  every bind site move, and any future `CommandList` implementer inherits a
  wider contract; and preserving source compatibility would require a default
  argument on a virtual function, which is statically bound and therefore
  unsafe. Its genuine advantage — one buffer reusable at two widths — has no
  caller today and no plausible one, since a buffer's bytes are written once.
- **Two purposes, `BufferPurpose::Index16` and `Index32`.** Rejected: it
  conflates two orthogonal axes in one enum, turns `bindIndexBuffer()`'s
  single-purpose precondition into a two-value test, and sets a precedent under
  which `BufferPurpose` grows multiplicatively with every future per-purpose
  attribute.
- **A byte-width integer (`std::uint32_t indexStrideBytes = 2`) instead of an
  enum.** Rejected: it admits values Vulkan cannot express (3, 8, 0) and moves
  validation from the type system to a runtime check, for no gain over a
  two-value enum.
- **Templating `createMesh()` and the Buffer creation on the index type.**
  Rejected: an overload achieves the same with no header-visible template
  parameter, and a template would invite `Mesh<IndexType>`, splitting a type
  that deliberately has no index-width state.
- **Widening every index to 32 bits and deleting the 16-bit path.** Rejected:
  it doubles index memory for every existing asset and for the 548 Bistro
  meshes that do not need it, and it changes the bytes the current path puts on
  the GPU, which would forfeit the strongest available regression proof.
- **Enabling `fullDrawIndexUint32` so the full 2^32−1 index range is
  guaranteed.** Rejected for now: it adds a required optional feature to
  physical-device selection — a new way for device creation to fail, including
  on Android — in exchange for range no Atlantis content is within two orders
  of magnitude of needing. Recorded as Spec 0039's Open Question O1 so review
  can overrule it; doing so later is a small, additive change.
