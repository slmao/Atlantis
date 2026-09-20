# Spec: RHI Index-Type Parameterization (uint16/uint32)

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-20
- **Related Plan(s):** [Plan 0039](../plans/0039-rhi-index-type-parameterization.md)
  (Approved 2026-09-20 with the Joint Human Review recorded in its header;
  linked 2026-09-20). This Spec and
  **Implementation still awaits its own, separate Joint Human Review** of
  Spec + Plan together, per AGENTS.md's own workflow — this Approval
  authorizes drafting Plan 0039 only.
- **Approval:** slmao, 2026-09-20 (chat confirmation, no reviewing PR —
  authorizes drafting Plan 0039; Implementation itself still awaits its own,
  separate Joint Human Review of Spec + Plan together). The same review ruled
  all five open questions; see Risks & Open Questions below.
- **Related ADR(s):**
  [ADR-0086](../adr/0086-rhi-index-type-public-expression-and-backward-compatibility.md)
  (`Accepted`) — the RHI public-API shape for index type, its
  backward-compatibility strategy, and the Vulkan mapping;
  [ADR-0087](../adr/0087-asset-system-index-width-representation-and-artifact-version-dispatch.md)
  (`Accepted`) — the Asset System's own CPU-side index-width representation
  and `loadStaticMeshAsset()`'s schema-version dispatch. Both drafted
  alongside this Spec and accepted 2026-09-20 alongside this Spec's own
  Approval.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

This Spec implements [Spec 0036](0036-bistro-parity-roadmap.md) workflow ①b.
Atlantis can already *produce* `.amesh` schema-version-5 artifacts with
`uint32_t` indices (the glTF importer, Plan 0037 D2) but cannot *draw* them:
the RHI has no index-type concept at all, and the Vulkan Backend binds
`VK_INDEX_TYPE_UINT16` unconditionally at exactly one line. This Spec
parameterizes index width along the whole real chain — RHI public API →
Vulkan Backend → Renderer `createMesh()` → Asset System
`loadStaticMeshAsset()` → Runtime `scene_load.cpp` — so a v5 artifact
renders, while the existing `uint16_t` path stays behaviourally and
byte-level unchanged.

## Motivation / Problem Statement

[ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D2, executed
by Plan 0037, gives importer-produced meshes `uint32_t` indices because 3 of
Bistro's 551 primitives exceed the 65,535-vertex ceiling of `.amesh` schema 4
(the largest, `bistro_mesh_246_0`, has 127,104 vertices). Plan 0037 shipped
551 v5 artifacts that decode, validate and cross-check correctly, and its own
end-to-end test states in its header comment that it deliberately stops at the
Asset System decoders because nothing downstream can consume them
(`tests/tools/gltf_importer/bistro_end_to_end_tests.cpp:30-33`).

That gap was ruled, not worked around: Plan 0037 Ruling 5 (2026-09-19) left
the uniform-v5 decision in place and recorded the render-side work as Spec
0036 workflow ①b, a **hard** dependency of workflow ⑦ (the Bistro finale),
which "cannot render Bistro without it"
(`docs/specs/0036-bistro-parity-roadmap.md:305-307`). Nothing else in the
engine needs `uint32_t` indices today, and nothing else will unblock ⑦.

## Goals

- A `.amesh` schema-5 artifact loads through the existing public
  `loadStaticMeshAsset()` entry point and renders correctly through the
  existing Renderer/RenderGraph/Vulkan path.
- The `uint16_t` path is unchanged: same GPU bytes, same draw calls, same
  pixels. Every existing golden staying green is the standing proof.
- The index width is expressed once, in a place where a caller cannot
  silently get it wrong, rather than threaded as an untyped width through
  several call sites.
- Existing call sites of the three affected public APIs
  (`rhi::Device::createBuffer()`, `renderer::createMesh()`,
  `asset_system::loadStaticMeshAsset()`) stay source-compatible; no
  opportunistic rewrite of the ~20 `createMesh()` and ~24
  `BufferPurpose::Index` sites this Spec does not otherwise need to touch.

## Non-Goals

- **Rendering the Bistro scene itself.** That is workflow ⑦, which also needs
  ①c and ②–⑥. This Spec's real-data evidence stops at drawing individual
  imported meshes.
- **Changing the `.amesh` format.** Schema 4 and schema 5 are both already
  defined and shipped (`mesh_artifact.h:35`, `:75`); this Spec adds no schema
  version and changes no byte layout.
- **A `uint32_t` cooking path.** `atlantis_asset_cooker` keeps producing v4
  only; the glTF importer stays the sole v5 producer (Plan 0037 D2). The
  `.mesh.txt` authoring grammar keeps its `uint16_t` index domain
  (`mesh_source.h:16`, `:39`).
- **Index-buffer memory-residency changes.** Index buffers stay host-visible
  and host-coherent, as ADR-0023 fixed for every Buffer purpose; no staging or
  device-local upload path is introduced here.
- **Other index-binding features** — primitive restart, an index-buffer offset
  parameter, 8-bit indices (`VK_EXT_index_type_uint8`), or indirect draws.
  None is needed by workflow ①b and none is scaffolded for later.
- **Mip-chain passthrough (workflow ①c)** and every other Spec 0036 workflow.

## Requirements

### Functional

1. **RHI expresses index width in its public API**, backend-independently,
   with `uint16` as the default so that an existing index-buffer creation site
   that says nothing about width keeps its current meaning. The exact shape is
   ADR-0086's decision; this Spec's recommendation and reasoning are in
   Proposed Design below.
2. **The Vulkan Backend maps that expression to `VK_INDEX_TYPE_UINT16` /
   `VK_INDEX_TYPE_UINT32`** at its one binding site
   (`vulkan_command_list.cpp:387`), and nowhere else acquires an index-width
   concept.
3. **Renderer can build a `Mesh` from `uint32_t` indices.** `renderer::Mesh`
   itself gains no index-width state — it owns two Buffers and a count
   (`mesh.h:16-35`), and the width belongs to the index Buffer.
4. **`loadStaticMeshAsset()` accepts both schema 4 and schema 5**, deciding
   from the artifact's own `schema_version` header field rather than from a
   caller-supplied hint, a filename, or a manifest entry. The two decoders
   stay separate and unmodified, as Plan 0037 D2 requires.
5. **`StaticMeshAssetData` carries its own index width** in an
   Asset-System-owned form. It may not name an RHI type: ADR-0043's Decision
   forbids it and `tests/asset_system/module_boundary_tests.cpp:22-26`
   enforces it automatically. The composition root translates.
6. **Runtime's scene-load path selects the right width per mesh**
   (`scene_load.cpp:102-117`), so a scene whose dependency manifest mixes v4
   and v5 meshes loads and draws correctly.
7. **The index-value ceiling is stated and respected.** The Device is created
   with no `VkPhysicalDeviceFeatures` (`vulkan_device.cpp:1836-1842`), so
   `fullDrawIndexUint32` is disabled and `maxDrawIndexedIndexValue` is only
   guaranteed to be at least 2^24−1. This Spec's answer to that is Open
   Question O1.

### Non-functional

- **Performance:** no per-draw cost beyond the existing single bind call; the
  width resolves to a `switch` at `bindIndexBuffer()` time. No extra CPU copy
  or index conversion on either path.
- **Memory:** a `uint16_t` mesh's index buffer stays 2 bytes per index. No
  path widens v4 indices to 32 bits — for Bistro-scale scenes that would be a
  real cost for no benefit, and it is explicitly rejected in ADR-0087.
- **Portability (within the Vulkan-only Phase 1 constraint):**
  `VK_INDEX_TYPE_UINT32` is core Vulkan 1.0 and requires no extension and no
  optional feature on either primary platform (Windows, Android). The
  uint16/uint32 distinction is a standard backend-independent concept, so the
  RHI surface stays backend-neutral per AGENTS.md's Phase 1 constraint.
- **Other:** no new dependency; no build-system change beyond whatever new
  test/golden targets the Plan adds.

## Pre-drafting Investigation (required conclusions, cited against real files)

Every hardcoded `uint16`/`VK_INDEX_TYPE_UINT16` site on the real call chain,
found by following that chain end to end rather than by grepping alone.

### Investigation 1 — Asset System

- `static_mesh_asset_data.h:18` (constructor), `:22` (`indices()` accessor),
  `:34` (member) — the CPU-side mesh type is
  `std::vector<std::uint16_t>`-typed at all three points. This is the type the
  composition root hands to `createMesh()`.
- `load.cpp:50` calls `decodeMeshArtifact()`, which rejects every schema
  version but 4 (`mesh_artifact.cpp:114-115`); `load.cpp:63` derives
  `indexCount` from that vector; `load.cpp:82` constructs the
  `StaticMeshAssetData`. **Loader-level v5 support does not exist today** — a
  v5 artifact fails at `load.cpp:51` with `ArtifactDecodeFailed`, and that is
  squarely in this workflow's scope.
- The v5 half of the format is already public and already tested:
  `mesh_artifact.h:75` (`kMeshArtifactSchemaVersionU32 = 5`), `:77-82`
  (`DecodedMeshArtifactU32`), `:84-95` (the two encoders and the decoder),
  with the version gate at `mesh_artifact.cpp:293-294`. Nothing in the format
  layer needs to change.
- `module_boundary_tests.cpp:22-26` forbids any `atlantis/rhi/` include
  anywhere under `src/asset_system/`, enumerated at test-run time. This is why
  Requirement 5 exists in the shape it does.

### Investigation 2 — Renderer

- `mesh.h:47-52` — `createMesh(Device&, VertexInputLayout, const void*
  vertexData, std::size_t, const std::uint16_t* indices, std::uint32_t
  indexCount)`. The index pointer's type is the only `uint16` in Renderer's
  public surface.
- `mesh.cpp:31` — `indexCount * sizeof(std::uint16_t)` is the only place the
  index-buffer byte size is computed; `mesh.cpp:33` creates the Index-purpose
  Buffer; `mesh.cpp:38` memcpys into it.
- `mesh.h:16-35` — `Mesh` holds `vertexBuffer_`, `indexBuffer_`,
  `indexCount_` and nothing else, and its header comment fixes it as
  "Constructed once; never re-uploaded or mutated". No index-width state
  belongs here.
- `renderer.cpp:80`, `:106`, `:116`, `:264` bind index buffers;
  `renderer.cpp:82`, `:109`, `:246`, `:266` call `drawIndexed()`. Renderer
  never names an index width at any of these eight sites, and under this
  Spec's recommendation it still never will.

### Investigation 3 — RHI (the public shape this Spec must choose)

- `types.h:101-107` — `BufferPurpose { Vertex, Index, Uniform, Readback,
  Staging }`; `types.h:150-153` — `BufferCreateParams { purpose, sizeBytes }`,
  with `operator==` at `src/rhi/src/types.cpp:13-15`.
- `buffer.h:17-35` — `Buffer` exposes exactly `purpose()`, `sizeBytes()`,
  `mappedData()`. Its header comment (`buffer.h:9-10`) states the existing
  precedent this Spec leans on: a Buffer is **"fixed to one of three purposes
  at creation (ADR-0023)"**.
- `command_list.h:60-64` — `bindVertexBuffer(Buffer&)` /
  `bindIndexBuffer(Buffer&)`, both pure virtual, with a purpose precondition
  stated as a programmer error (`ATLANTIS_CHECK`), not a recoverable one;
  `command_list.h:78-80` — `drawIndexed(std::uint32_t indexCount)`, which
  counts indices and therefore needs no change at all.
- Three shapes are available — a new `IndexType` enum fixed at Buffer
  creation, the same enum passed at bind time, or a width parameter/template.
  The recommendation and the reasoning are in Proposed Design; the decision is
  ADR-0086's.

### Investigation 4 — Vulkan Backend

- `vulkan_command_list.cpp:384-388` — `bindIndexBuffer()`: the
  `VK_INDEX_TYPE_UINT16` literal here is **the only `VK_INDEX_TYPE` token in
  the entire repository** (verified by a whole-tree search). This single line
  is the backend's whole index-width surface.
- `vulkan_device.cpp:806-808` — `createBuffer()` maps `BufferPurpose::Index`
  to `VK_BUFFER_USAGE_INDEX_BUFFER_BIT`. The usage flag is width-independent,
  and Vulkan has no separate index-buffer *view* object, so there is no second
  site to change.
- `vulkan_buffer.h:25-51` — `VulkanBuffer` stores `purpose_` and `sizeBytes_`
  alongside its `VkBuffer`/`VkDeviceMemory`; a create-time index type would be
  stored exactly the same way and read back by `vulkan_command_list.cpp:387`.
- `vulkan_device.cpp:1836-1842` — `VkDeviceCreateInfo` sets `pNext` to a
  dynamic-rendering feature struct and **never sets `pEnabledFeatures`**.
  `fullDrawIndexUint32` is therefore `VK_FALSE`, and the Vulkan specification
  then guarantees `VkPhysicalDeviceLimits::maxDrawIndexedIndexValue` only to
  be at least 2^24−1 (16,777,215). Bistro's largest imported primitive has
  127,104 vertices — 131× of headroom — but the limit is real and must be
  stated rather than assumed away (Open Question O1).

### Investigation 5 — Runtime

- `scene_load.cpp:102-105` loads each distinct mesh dependency through
  `loadStaticMeshAsset()`; `scene_load.cpp:111-114` hands
  `meshAssetData.indices().data()` and `.size()` straight to `createMesh()`.
  This is the single composition-root line where a v5 artifact must pick the
  other width. The scene dependency manifest records paths and AssetIds only —
  it records no schema version, which is why Requirement 4 puts the decision in
  the loader.
- `runtime_application.cpp:824-835` — the output-transform pass's own
  fullscreen-triangle index buffer is a hand-built `std::uint16_t[3]` created
  directly via `createBuffer()`, not via `createMesh()`. It is an index buffer
  that no mesh owns, and it must keep working untouched — the strongest
  practical argument for a defaulted index type rather than a required
  parameter.

### Investigation 6 — Existing dependency surface (blast radius of an API change)

Measured, because the cost of each candidate shape is mostly this number:

| Surface | Sites | Where |
|---|---|---|
| `createMesh(...)` calls | 20, in 13 files | 1 in `src/runtime/src/scene_load.cpp`, 2 in `examples/`, 17 across 10 test files (`minimal_cube_fixture.cpp`, `textured_quad_fixture.cpp`, `world_scene_fixture.cpp`, `world_scene_loaded_fixture.cpp`, `shadow_gpu_tests.cpp`, `sky_background_gpu_tests.cpp`, `pbr_render_gpu_tests.cpp`, `descriptor_pool_growth_gpu_tests.cpp`, `headless_rendering_gpu_tests.cpp`, `minimal_renderer_gpu_tests.cpp`) |
| `bindIndexBuffer(...)` calls | 12 | 4 in `renderer.cpp`, 8 in GPU tests |
| `rhi::CommandList` implementations | 2 | `vulkan_command_list.h:68`, `tests/render_graph/fake_command_list.h:322` |
| `rhi::Buffer` implementations | 2 | `vulkan_buffer.h:25`, `tests/render_graph/fake_command_list.h:195` (`FakeBuffer`, constructed ~20× in `tests/renderer/renderer_ownership_tests.cpp`) |
| `createBuffer({.purpose = BufferPurpose::Index, ...})` sites | 24 | 2 in `src/`, 2 in `examples/`, 20 in tests |
| `loadStaticMeshAsset(...)` calls | 14 | 1 in `src/runtime/`, 13 in tests/fixtures |

The recommendation in Proposed Design changes **none** of these six rows' call
sites except the single `scene_load.cpp:111` line and the two `Buffer`
implementations (which gain a new pure virtual). Every other row stays
source-compatible.

## Proposed Design

The recommended shape, with the reasoning ADR-0086 and ADR-0087 record
formally.

**1. RHI — index type fixed at Buffer creation, defaulted to `Uint16`.**

```
enum class IndexType { Uint16, Uint32 };          // rhi/types.h
struct BufferCreateParams {
  BufferPurpose purpose = BufferPurpose::Vertex;
  std::size_t   sizeBytes = 0;
  IndexType     indexType = IndexType::Uint16;    // meaningful only for BufferPurpose::Index
};
class Buffer { ... virtual IndexType indexType() const = 0; };  // rhi/buffer.h
```

Recommended over a bind-time parameter for four reasons:

- **Precedent.** `BufferPurpose` is already fixed at creation and read back
  from the Buffer (`buffer.h:9-10`, `:21`). The index width is the same *kind*
  of fact — a property of the bytes written into the buffer, decided when they
  are written, and a Buffer's contents are never mutated after construction
  (`mesh.h:13-14`).
- **Unrepresentable mismatch.** With a bind-time parameter the same buffer
  could be bound with the wrong width at any of the 12 call sites. Fixing the
  type at creation makes that state impossible rather than merely checked.
- **Blast radius.** A defaulted struct field leaves all 24 index-buffer
  creation sites source-compatible and leaves `CommandList`'s pure-virtual
  surface — and therefore both its implementations, the fake included, and all
  12 bind sites — untouched.
- **A virtual function's default argument is a trap.** The bind-time variant
  would need `bindIndexBuffer(Buffer&, IndexType = IndexType::Uint16)` to keep
  its call sites; default arguments on virtual functions are bound statically
  to the declared type, so an override could silently present a different
  default.

Honest costs, recorded in ADR-0086: a non-Index Buffer carries a meaningless
field; `Buffer::indexType()` needs a purpose precondition; adding a pure
virtual to `rhi::Buffer` touches its two implementations; and
`operator==(BufferCreateParams)` (`src/rhi/src/types.cpp:13-15`) must compare
the new field.

**2. Vulkan Backend.** `VulkanBuffer` stores the type; `bindIndexBuffer()`
(`vulkan_command_list.cpp:387`) switches on `buffer.indexType()`. One literal
becomes one two-case switch. Nothing else in the backend changes.

**3. Renderer.** A second `createMesh()` overload taking `const std::uint32_t*
indices`, sharing the existing body and passing `IndexType::Uint32` in the
index `BufferCreateParams`. The existing overload keeps its exact signature, so
all 20 existing calls compile unchanged, and `Mesh` is untouched.

**4. Asset System.** Its own enum, because it may not name `rhi::IndexType`:

```
enum class MeshIndexType { Uint16, Uint32 };      // static_mesh_asset_data.h
```

`StaticMeshAssetData` gains `indexType()` and an `indices32()` accessor
alongside the existing `indices()`. Each typed accessor carries an
`ATLANTIS_CHECK` precondition on `indexType()` — always evaluated in Debug and
Release (`assert.h:35-37`), matching how `vertexCount()` already treats a zero
stride and how `bindIndexBuffer()` already treats a wrong purpose. The point is
that a v5 asset reaching a caller written only for v4 aborts loudly instead of
drawing an empty or garbage mesh.

`loadStaticMeshAsset()` keeps its signature and dispatches on the artifact's
own `schema_version`, via a small public `peekMeshArtifactSchemaVersion()`
added next to the two decoders. Neither decoder is modified, so Plan 0037 D2's
"separate code paths, not one unified reader" survives intact — the dispatch
sits above both, in the loader, which is the one place that must handle both.

**5. Runtime.** `scene_load.cpp:111` branches on `meshAssetData.indexType()`
and calls the matching overload. The fullscreen-triangle index buffer
(`runtime_application.cpp:824-835`) is not touched and keeps the default
`Uint16`.

## Architectural Impact

**Yes** — two decisions, in two modules that are forbidden to share a type.

- **RHI public API (index-type expression) and its Renderer entry point** —
  [ADR-0086](../adr/0086-rhi-index-type-public-expression-and-backward-compatibility.md).
  It also records the Vulkan mapping and the `maxDrawIndexedIndexValue`
  position. The Renderer `createMesh()` overload is folded into this ADR rather
  than given a third: it is the direct CPU-side consumer of the RHI decision,
  it names `rhi::IndexType` itself, and it raises no separate ownership or
  boundary question (`Mesh`, and therefore ADR-0022's ownership model, is
  untouched).
- **Asset System CPU-side index-width representation and the v4/v5 loader
  dispatch** —
  [ADR-0087](../adr/0087-asset-system-index-width-representation-and-artifact-version-dispatch.md).
  This is a genuinely separate decision, not a restatement: ADR-0043's Decision
  forbids Asset System from naming any RHI type, so the two modules *cannot*
  share one enum, and the question of how a single loader entry point serves
  two schema versions has no RHI content at all.

No module boundary moves, no dependency is added, and no threading or
memory-ownership model changes.

## Alternatives Considered

- **Index type as a `bindIndexBuffer()` parameter.** The shape Vulkan itself
  uses. Rejected as the primary design for the four reasons in Proposed Design
  — chiefly that it makes a buffer/width mismatch representable at 12 call
  sites and forces a pure-virtual change on `CommandList`. Recorded in full in
  ADR-0086.
- **Two buffer purposes, `Index16` and `Index32`.** Rejected: it conflates two
  orthogonal axes, turns `bindIndexBuffer()`'s single-purpose precondition into
  a two-value test, and would make `BufferPurpose` grow multiplicatively with
  any future per-purpose attribute.
- **Widen every index to `uint32_t` on load; delete the `uint16_t` path.**
  Rejected: it doubles index memory for every existing asset and for the 548
  Bistro meshes that do not need it, it changes the bytes the existing path
  puts on the GPU (so "the u16 path is unchanged" would stop being true), and
  it would break the existing tests that assert specific `uint16_t` index
  values (`tests/asset_system/textured_quad_mesh_tests.cpp`).
- **A second loader entry point, `loadStaticMeshAssetU32()`.** Rejected: the
  Runtime cannot know which to call — the scene dependency manifest records
  paths and AssetIds, never a schema version — so the dispatch would have to be
  reimplemented by every composition root. Mirroring Plan 0037 D2's separate
  encode/decode siblings at the *loader* level would move a decision the
  artifact header already answers onto the caller.
- **Making `StaticMeshAssetData::indices()` return a byte span plus a width.**
  Rejected as the recommendation, though it is the most honest shape: it breaks
  all 14 `loadStaticMeshAsset()` consumers for no correctness gain over the
  `ATLANTIS_CHECK`-guarded typed accessors, and this Spec's Goals rule out
  churning call sites it does not need to touch. Recorded as the runner-up in
  ADR-0087.
- **Templating `createMesh()` on the index type.** Rejected: it buys nothing
  over an overload, and it would push the index type into a header-visible
  template parameter that `Mesh` would then be tempted to carry.

## Testing & Verification Plan

Satisfies Spec 0036's own contract row for this workflow
(`docs/specs/0036-bistro-parity-roadmap.md:793`): GPU-independent tests over
index-type selection; a GPU test drawing a >65,535-vertex `uint32_t` mesh;
Validation Layers clean. Per
[docs/process/testing-strategy.md](../process/testing-strategy.md).

**T1 — GPU-independent unit tests** (Requirements 1, 4, 5, 6)

- `tests/rhi/`: `IndexType` defaults to `Uint16` in `BufferCreateParams`;
  `operator==` distinguishes two otherwise-identical params that differ only in
  `indexType`.
- `tests/asset_system/`: `loadStaticMeshAsset()` loads a v4 artifact with
  `indexType() == Uint16` and unchanged `indices()` values, and a v5 artifact
  with `indexType() == Uint32` and correct `indices32()` values; the
  wrong-width accessor trips its `ATLANTIS_CHECK` (the existing
  `setFailureHandler()` mechanism, `assert.h:21-25`, makes this testable); a v5
  artifact whose metadata sidecar disagrees still yields
  `MetadataArtifactMismatch`.
- `tests/renderer/`: the `uint32_t` `createMesh()` overload requests an
  `Uint32` index Buffer of `indexCount * 4` bytes, checked against the existing
  `FakeBuffer`-based recording fixture.

**T2 — Equivalence golden (GPU): index width alone changes nothing visible**
(Goals 1-2). One authored `ParsedMeshSource` is encoded twice, by
`encodeMeshArtifact()` and `encodeMeshArtifactU32()` — the two take the *same*
`ParsedMeshSource` and the *same* tangents (`mesh_artifact.h:61`, `:84`), so the
vertex bytes are identical by construction and only the index width differs.
Both artifacts plus their metadata sidecars are written to a temp directory,
each is rendered through the existing `setUpMinimalCubeFixtureFromAsset()`
composition root (`tests/image_regression/fixture/minimal_cube_fixture.h:79-92`),
and the two frames are compared with `compareBuffers()` at ADR-0042's
non-configurable zero tolerance: `maxChannelDiff == 0`,
`outOfToleranceCount == 0`. **No new golden PNG is needed for this test** — the
comparison is between two live frames, the precedent being
`tests/image_regression/lighting_demo_gpu_tests.cpp:430`.

**T3 — Large-mesh golden (GPU): a mesh that cannot exist at 16 bits**
(Requirements 2, 3, 7). A deterministic, closed-form generated grid with more
than 65,535 vertices (e.g. 258×258 = 66,564 vertices, 132,098 triangles),
encoded as a v5 artifact by the test's own call to
`encodeMeshArtifactU32FromIndices()` — no cooker path, no committed multi-MB
asset — and rendered through the same fixture. Evidence:

- a new golden PNG in the Initial-baseline category (ADR-0042), produced by a
  new `golden_generator` main that shares the one grid generator with the test,
  so the golden is reproducible rather than blessed by eye;
- a targeted non-degeneracy assertion that geometry drawn from indices
  **above** 65,535 actually lands on screen — a silent 16-bit truncation wraps
  index 65,536 to 0, which this assertion, not the golden alone, is designed to
  name.

**T4 — Vulkan Validation Layers clean.** Both new GPU tests run on the existing
fixture, which already creates its Device with `enableValidationLayers = true`
(`tests/image_regression/fixture/minimal_cube_fixture.cpp:509-510`). A wrong
`VkIndexType` for a buffer's contents is exactly what the layers flag.

**T5 — Regression (the "u16 path unchanged" proof).** Full Windows Debug and
Release builds and the complete `ctest` suite in both configurations; every
existing golden green; the asset-cooker determinism and byte-identity tests
green (they prove no cooked `.amesh` byte moved); Android `assembleDebug`
green, per the Plan 0034/0035 dual-platform pattern.

**T6 — Real-data proof (opt-in, `content` label).** Import Bistro, load and
draw `bistro_mesh_246_0` (127,104 vertices, the largest real v5 mesh), and
assert a non-degenerate frame. `SKIP()`ed when the content is absent, matching
`tests/tools/gltf_importer/bistro_end_to_end_tests.cpp:97-99`. This is the test
that closes Plan 0037 Ruling 5's own disclosure, and the Plan should also amend
that test's `30-33` header comment once the gap is gone.

## Risks & Open Questions

**All five open questions were ruled by Human Review on 2026-09-20 (chat
confirmation), alongside this Spec's own Approval.** Each ruling below is
binding on Plan 0039; the reasoning that led to it stays in the two ADRs.

- **O1 — `fullDrawIndexUint32` and `maxDrawIndexedIndexValue`.** The Device
  enables no features today (`vulkan_device.cpp:1836-1842`), so the guaranteed
  ceiling on a 32-bit index *value* is 2^24−1, not 2^32−1.
  **Ruled 2026-09-20: do not enable `fullDrawIndexUint32`, and do not add a
  runtime limit query.** The engine relies on the specification's guaranteed
  2^24−1 floor, and a mesh carrying an index value at or above 2^24 is
  rejected with a **named error** by the producing or loading layer, never
  bound and hoped for. Bistro's largest primitive is 127,104 vertices, 131×
  inside the limit; requiring an optional device feature would add a
  device-selection failure mode for range nothing needs. Raising the ceiling
  later is a new decision and therefore a new ADR (ADR-0086 Decision item 6).
- **O2 — `Buffer::indexType()` on a non-Index Buffer.**
  **Ruled 2026-09-20: `ATLANTIS_CHECK(purpose() == BufferPurpose::Index)`**,
  matching `bindIndexBuffer()`'s own precondition style. Silently returning
  `Uint16` would make a meaningless value look meaningful, and
  `ATLANTIS_CHECK` is evaluated in Release as well as Debug
  (`assert.h:35-37`).
- **O3 — adding a pure virtual to `rhi::Buffer`** breaks any implementation
  outside this repository. There are exactly two in-tree (`vulkan_buffer.h:25`,
  `fake_command_list.h:195`), and Atlantis ships no external RHI implementer.
  **Ruled 2026-09-20: accepted as a disclosed cost, inherited not mitigated** —
  the two in-tree implementations are updated, no compatibility shim or
  non-pure default implementation is added, and the PR discloses the change to
  the `rhi::Buffer` interface.
- **O4 — Android Validation-Layer gap.**
  **Ruled 2026-09-20: inherited and disclosed, not re-litigated here.** The
  dual-platform verification carries the emulator's Validation-Layer
  limitation forward from Plan 0034/0035 under the same disclosure wording;
  Android evidence is a build-and-run check, not a second golden.
- **O5 — should the scene dependency manifest record the index width?**
  **Ruled 2026-09-20: no.** The artifact header is the single authority, and a
  manifest field would be a second source of truth needing its own mismatch
  error and its own test. The manifest schema is unchanged by this Spec.
- **R1 — an accidental regression in the `uint16_t` path** would show up as
  moved golden pixels. The defaulted-at-creation shape is what keeps that risk
  low: no existing line changes meaning. T5 is the gate.
- **R2 — the T3 golden is generated from test code.** If the grid generator is
  ever edited, the golden must be regenerated. Mitigated by making the
  generator a single shared function used by both the test and the
  `golden_generator` main, and by keeping the grid closed-form and
  parameter-free.

## Out of Scope / Future Work

- **Workflow ⑦ (the Bistro finale)** — unblocked by this Spec on the index axis
  only; ①c and ②–⑥ remain.
- **Workflow ①c (mip-chain passthrough)** — the other hard ⑦ dependency (Plan
  0037 Ruling 6).
- **Device-local index/vertex buffers and a staging upload path** — ADR-0023's
  "every Buffer is host-visible" round has not been reopened, and Bistro-scale
  content will eventually force that conversation. Not here.
- **Primitive restart, 8-bit indices, index-buffer offsets, indirect draws** —
  each a separate RHI surface, none needed by ①b.
- **A `uint32_t` cooker path and a `uint32_t` `.mesh.txt` grammar** — only
  worth doing if a non-importer producer of large meshes ever appears.
