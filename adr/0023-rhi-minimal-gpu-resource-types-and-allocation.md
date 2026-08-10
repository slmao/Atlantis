# ADR 0023: RHI Minimal GPU Resource Types (Buffer, Texture) and Direct Allocation Strategy

- **Status:** Accepted
- **Date:** 2026-08-11
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-11; see
  [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)'s
  Human Review Approval note for the full approval record this ADR's
  Decision is part of, including the explicit confirmation that this
  round's `Buffer`s are host-visible/host-coherent and individually,
  directly allocated (no pooling, no VMA).
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md) (`Approved`)

## Context

RHI's public surface today ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
exposes exactly one resource type, `RenderTarget`, and explicitly
introduces no `Buffer`/`Texture`/`Sampler` type. Spec 0007's acceptance
target — a real, visible mesh with depth testing — cannot be built
without a vertex buffer, an index buffer, a uniform (camera) buffer, and a
depth attachment image. This is exactly the future consumer
[ADR-0015](0015-vulkan-memory-allocation-deferred.md) named as its own
resolution trigger: *"whichever future spec introduces `Buffer`/`Texture`
resource creation must resolve this question via its own ADR... before
any `vkAllocateMemory` call or VMA-equivalent allocation call is written
anywhere in the codebase."* This ADR is that resolution.

[resource_lifetime.md](../docs/architecture/resource_lifetime.md) also
left one open question this spec must now answer: "Whether RHI resource
handles are move-only... or support explicit shared ownership... is
exactly the kind of API-shape decision this document defers to the RHI
spec/ADR."

Two forces bound this decision: [AGENTS.md](../AGENTS.md) explicitly
forbids adopting VMA or a hand-rolled suballocator "for later" without a
concrete need, and this spec's own scope (a handful of fixed-purpose
buffers/textures per frame, single frame-in-flight) does not, by itself,
create a concrete need for pooling or suballocation — only for *some*
`VkDeviceMemory` allocation to exist at all.

## Decision

**RHI gains exactly two new public resource types, `Buffer` and
`Texture`, and nothing more this round.**

- **`Buffer`** — an RHI interface representing a GPU-visible linear memory
  region, created for one of three fixed purposes this round: vertex data,
  index data, or a small host-updatable uniform block (camera
  view/projection). No general "any buffer, any usage flags" API — the
  concrete purpose is fixed at creation time (a `BufferPurpose`-shaped enum
  or equivalent, left to the Plan), matching this spec's own
  minimal-and-honest scope rather than a general resource system. **All
  three purposes use host-visible, host-coherent memory this round** —
  including vertex and index data, not only the uniform buffer. This is a
  deliberate simplification, not an oversight: a device-local vertex/index
  buffer would need a staging buffer plus an upload copy command (and the
  synchronization to know that copy has completed before the first draw
  reads it), which is real additional complexity this spec's own mesh —
  small, uploaded exactly once at `Mesh` construction, never re-uploaded —
  does not need. A future spec with a meaningfully larger mesh, or a real
  performance motivation, is expected to introduce a staging/upload path
  and move vertex/index data to device-local memory; this spec does not
  design that path. Each `Buffer`'s backing memory is mapped exactly once,
  for its whole lifetime, at creation — never repeatedly mapped/unmapped
  per write — since host-coherent memory needs no explicit flush/
  invalidate call between a CPU write and a subsequent GPU read that is
  otherwise correctly ordered (e.g. by this spec's own acquire-time drain
  guarantee, see Requirements below). Exact alignment/offset requirements
  (e.g. respecting `VkPhysicalDeviceLimits::minUniformBufferOffsetAlignment`
  for the uniform buffer) are a Plan-stage implementation detail this ADR
  does not enumerate, but must be respected regardless of which concrete
  values the Plan chooses.
- **`Texture`** — an RHI interface representing a GPU image used, this
  round, exclusively as a depth attachment. No sampled/shader-read usage,
  no mipmaps, no texture streaming, no general format table beyond
  whatever single depth format this round selects. A general, sampled
  `Texture` (material color maps, etc.) is explicitly future work — this
  decision does not attempt to design that surface now, only the
  depth-attachment case this spec's acceptance target actually needs.
- **Both are move-only, single-owner RAII types**, held exclusively behind
  an owning smart pointer (`std::unique_ptr<Buffer>`/
  `std::unique_ptr<Texture>`), mirroring `RenderTarget`'s own existing
  ownership shape ([ADR-0014](0014-rhi-device-presentation-construction-boundary.md),
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)).
  This resolves [resource_lifetime.md](../docs/architecture/resource_lifetime.md)'s
  open question: **move-only, not shared-ownership**, consistent with
  [ADR-0003](0003-resource-rendertarget-ownership-model.md)'s "explicit
  ownership... if a resource needs to be shared, the caller decides that
  and holds shared ownership explicitly" — this round has no concrete
  cross-owner sharing need (see
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)),
  so no shared-ownership wrapper is introduced to serve one.
- **Creation and destruction are `Device` operations**: `Device` gains
  `createBuffer(BufferCreateParams)` and `createTexture(TextureCreateParams)`,
  each returning `atlantis::Result<std::unique_ptr<Buffer|Texture>,
  ResourceCreateError>` (or an equivalent error type — exact naming left
  to the Plan). Both parameter structs are expressed in RHI-level terms
  (size/purpose for `Buffer`; extent/format/usage for `Texture`) — never a
  `Vk*` type, same rule as every existing RHI creation-parameter struct.
  Destruction is ordinary RAII: destroying the owning `unique_ptr` releases
  the underlying Vulkan object and its backing memory. No pooling, no
  reuse-across-instances, no resource variant beyond what a caller
  explicitly constructs and explicitly destroys.
- **No hidden cache of any kind** — `Device` does not deduplicate, key, or
  retain a reference to any `Buffer`/`Texture` it creates beyond returning
  the owning handle to the caller. This directly extends
  [ADR-0003](0003-resource-rendertarget-ownership-model.md)'s existing
  "no hidden global resource cache or implicit refcounted pool inside RHI
  itself in Phase 1" rule to these two new types, rather than leaving it
  ambiguous whether it still applies.

**GPU memory allocation strategy — direct, unpooled, Vulkan-Backend-
private, with an explicit migration boundary.** This resolves
[ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s named blocker:

- Each `Buffer`/`Texture` the Vulkan Backend creates gets **its own,
  individual `vkAllocateMemory` call and its own, individual
  `vkFreeMemory` call at destruction** — no suballocation, no shared
  `VkDeviceMemory` block backing more than one resource, no pooling of any
  kind. This is a deliberate continuation of
  [ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s own explicit
  carve-out ("a single direct `vkAllocateMemory`/`vkFreeMemory` pair, with
  no pooling or suballocation policy, is not what this ADR defers"),
  extended from Spec 0003's at-most-incidental use to this spec's several
  fixed-purpose resources — still no general, reusable allocation
  *strategy* is adopted, only a fixed, narrow, per-resource policy.
- **This is a stated temporary policy, strictly confined to the Vulkan
  Backend's private implementation of `createBuffer()`/`createTexture()`
  — never exposed, referenced, or assumed by any RHI public interface, any
  Renderer code, or any test outside Vulkan Backend's own unit tests.** No
  RHI or Renderer method signature is shaped around "one allocation per
  resource" as an assumption a future suballocator could not satisfy —
  callers see only `Buffer`/`Texture` handles, never a memory-block
  concept.
- **Migration boundary:** this policy is expected to be replaced — not
  amended, but superseded by a new ADR — once either (a) a future spec
  needs enough concurrent GPU resources that per-resource
  `vkAllocateMemory` calls risk hitting a driver's low
  `maxMemoryAllocationCount` limit, or (b) a real performance need
  motivates suballocation. That future ADR decides VMA vs. a hand-rolled
  suballocator, exactly as [ADR-0015](0015-vulkan-memory-allocation-deferred.md)
  already anticipated; nothing in this round's implementation may be
  written in a way that presumes which choice that future ADR makes.
- **This is a genuinely narrow claim, not an abstract one — this spec's
  own scope creates, at most, a handful of individual GPU allocations
  total**: one vertex `Buffer`, one index `Buffer`, one uniform `Buffer`,
  and one depth `Texture` per instance the verification composition
  constructs (plus one more depth `Texture` allocation on each interactive
  resize, since the old one is destroyed and a new one created — see
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)).
  This is nowhere near a typical driver's `maxMemoryAllocationCount` floor
  (commonly 4096 or higher) — the migration boundary above exists for
  when a *future* spec's resource count changes this picture, not because
  this spec's own scope is already near that limit.
- The uniform (camera) buffer's memory-write-timing safety argument (why
  a direct, unsynchronized CPU write into host-coherent memory is safe
  once per frame) is tied to the single-frame-in-flight baseline's
  existing acquire-time drain, per
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md);
  elaborated in
  [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)'s
  own Proposed Design, not repeated here.

**No `Sampler` type, no general resource-format table, and no
resource-lifetime/versioning/aliasing model is introduced.** These remain
exactly as out of scope as [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
and Spec 0005 already left them.

## Consequences

### Positive

- Resolves [ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s named
  implementation blocker explicitly, via its own ADR, exactly as that
  decision required — not silently reused past its stated scope.
- Answers [resource_lifetime.md](../docs/architecture/resource_lifetime.md)'s
  open move-only-vs-shared question with the option consistent with every
  existing RHI resource type (`RenderTarget`) and with
  [ADR-0003](0003-resource-rendertarget-ownership-model.md)'s existing
  no-hidden-sharing principle, keeping the whole RHI resource family
  uniform in shape.
- A narrow, explicitly-temporary allocation policy, confined to Vulkan
  Backend's private implementation, means a future suballocator adoption
  changes zero lines outside that module — no RHI/Renderer signature is
  coupled to "one allocation per resource."

### Negative / Trade-offs

- Direct per-resource `vkAllocateMemory` is real, avoidable overhead
  (allocation call cost, potential driver allocation-count pressure) this
  round accepts deliberately rather than optimizes — a future spec must
  revisit this once a concrete need (see Migration boundary above)
  appears; this is not a hidden cost, it is a stated one.
- `Buffer`/`Texture` being move-only, single-owner types means this
  round's own draw path (potentially drawing the same `Mesh` more than
  once per frame) works only via borrowed-reference reuse
  ([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)),
  not genuine multi-owner sharing — accepted, since no concrete case needs
  more.
- Fixing `Buffer`'s purpose at creation time (vertex/index/uniform, not a
  general usage-flags API) means a future spec wanting a more general
  buffer (e.g. a storage buffer for compute) must extend this type rather
  than being served by it unchanged — accepted as the cost of staying
  minimal and honest about this round's actual needs.

## Alternatives Considered

- **Adopt VMA now**, since it is the de facto standard and this spec is a
  natural, low-risk point to adopt it (a real consumer finally exists).
  Rejected: still adds a new third-party dependency
  ([AGENTS.md](../AGENTS.md) requires its own spec/ADR review for any new
  dependency) to serve a workload — a handful of fixed-size, fixed-purpose
  allocations per frame — that a direct allocation policy already serves
  correctly; adopting a general allocator ahead of a concrete pooling/
  suballocation need is exactly the "scaffold for later" pattern
  [AGENTS.md](../AGENTS.md) asks agents not to do.
- **Write a minimal hand-rolled suballocator now**, scoped just to this
  spec's fixed set of resource kinds. Rejected for the same reason as VMA,
  plus it is strictly more implementation and testing effort than a direct
  allocation policy for a need (allocation-count pressure, fragmentation)
  that does not yet exist at this spec's resource count.
- **Give `Buffer`/`Texture` a shared-ownership (reference-counted) handle
  type from the start**, anticipating a future asset system's needs.
  Rejected: no concrete consumer in this spec's own scope needs cross-
  owner sharing (see
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Alternatives Considered for the same reasoning applied at the Renderer
  level); inventing the wrapper type now, with no real case to validate its
  shape against, risks getting it wrong in a way a real future consumer
  would have to unwind.
- **A general `Buffer` type with caller-specified Vulkan usage flags**,
  rather than a fixed, RHI-level `BufferPurpose` enum. Rejected: this
  reintroduces exactly the "no `Vk*` type in RHI's public surface" leak
  [ADR-0001](0001-rhi-backend-independence.md) exists to prevent, for a
  round that only ever needs three fixed purposes.
