# ADR 0060: Scene Material Binding and Runtime Transactional Resource Publish

- **Status:** Accepted
- **Date:** 2026-08-26
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-27 as part of Spec 0018's Human Review Approval
- **Related Spec:** [specs/0018-material-asset-scene-binding-foundation.md](../specs/0018-material-asset-scene-binding-foundation.md) (`Approved`)
- **Acceptance Record (2026-08-27):** Accepted by Human Review as part
  of [specs/0018-material-asset-scene-binding-foundation.md](../specs/0018-material-asset-scene-binding-foundation.md)'s
  own Human Review Approval (2026-08-27), following two centralized
  final-review rounds — the first found and corrected a real
  architectural gap in the original deferred-GPU-realization design
  (Decision item 6); the second found and corrected the format-change
  rebuild's own atomicity (Decision item 9) and verified the
  `waitIdle()`-then-`present()` ordering against the real Vulkan call
  chain rather than asserting it — see that Spec's own approval note
  for the specific Decision items this ADR corresponds to. This record
  does not change this ADR's own Decision, Consequences, or
  Alternatives Considered below.
- **Related ADR(s):**
  [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  (World/Scene module boundary and World-to-Renderer extraction — this
  ADR extends the exact mesh-resolution pattern ADR-0051 already
  establishes to a second asset kind, without changing either ADR's
  own module-boundary decision),
  [ADR-0052](0052-scene-asset-module-boundary-and-ownership.md)–[ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
  (Scene Asset module boundary, data format, and transactional
  instantiation contract — this ADR widens the data format ADR-0053
  governs and extends the transactional contract ADR-0054 governs, in
  kind, not in structure),
  [ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
  (Material Asset's own module boundary and format — this ADR consumes
  that asset type; it does not redefine it).

## Context

[ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
gives Asset System a Material asset type. On its own, that asset type
has no consumer: nothing in the Scene Asset format, `World`, or Runtime
today can name, resolve, load, or draw with one. Three concrete,
evidence-based facts fix the shape of the problem this ADR solves:

- `atlantis::asset_system::DecodedRenderable` and
  `atlantis::world::Renderable` are each, today, exactly `{ AssetId
  meshAsset = 0; }` — confirmed by reading both files in full. Neither
  has a material or texture field of any kind, and the scene binary
  artifact's own per-node record is a fixed 72 bytes with no spare
  capacity — a genuine schema version bump is required to add a
  material reference, not a reinterpretation of existing bytes.
- Runtime's own real (non-test-fixture) `runFrame()` builds exactly one
  `Material` for the entire process, lazily, and binds it to every
  `DrawItem` regardless of entity: `item.material = &*material_;`
  unconditionally, confirmed directly in
  `runtime_application.cpp:398-420`. There is no per-entity material
  selection mechanism today, and no `SampledTexture`/`Sampler`
  construction anywhere in Runtime's own production code.
- Runtime's scene-loading pipeline
  (`RuntimeApplication::initializeSteps()` → `loadAndInstantiateScene()`)
  already has a real, working, tested shape for exactly this kind of
  problem — resolve a scene's declared `AssetId` references against a
  build-tree-private manifest, load each exactly once (deduplicated by
  `AssetId`, via `meshResourceMap_`, an `std::unordered_map<AssetId,
  Mesh>`), instantiate `World` infallibly via
  `fromValidatedSceneData()`, then publish `world_`/`meshResourceMap_`
  together as one atomic step, proven `noexcept` at compile time by two
  `static_assert`s rather than argued in prose.
- **`initializeSteps()` runs entirely before any real windowed
  `RenderTarget` exists.** Confirmed directly: `presentation_` is
  constructed only in response to the first `platform::SurfaceCreated`
  event, observed inside `runFrame()`'s own event loop
  (`runtime_application.cpp:246-260`) — which is only ever called after
  `initializeSteps()` has already returned and published `world_`. This
  is exactly why today's single `material_` is *not* built during
  `initializeSteps()` either — its own first construction is deferred
  to `runFrame()`'s own format-change check
  (`runtime_application.cpp:293-313`), the first point a real,
  format-known `RenderTarget` exists.
- **`Device::submit()` has no target-independent overload.** Its real
  signature (`device.h:53-54`) is
  `submit(std::unique_ptr<CommandList> commandList, const RenderTarget&
  target)` — `target` is a non-null reference, not a pointer. A one-time
  CPU→GPU texture upload is, by Spec 0016's own already-`Accepted`
  design, mandatorily a RenderGraph-recorded operation submitted
  against a real `RenderTarget` (ADR-0056) — it structurally cannot
  happen anywhere `initializeSteps()` runs. `Renderer::drawFrame()`'s
  own internal `RenderGraphBuilder` (`renderer.cpp:20-54`) never itself
  declares a sampled texture as a tracked resource — `cmd.bindTexture()`
  is a raw call requiring the texture to already be `ShaderRead` by
  whatever means the caller separately arranged.

Given the above, mesh resolution/loading and material/texture
resolution/loading share the same CPU-only shape and can share the same
Phase 1 transactional mechanism — but GPU *realization* of a material
(the actual `SampledTexture`/`Sampler`/`Pipeline`/`Material`
construction and upload) cannot join that same transactional step; it
must happen later, per frame, once a real `RenderTarget` exists. The
real design question this ADR settles: how does a scene node reference
an optional material, how does that reference survive version-bumped
serialization without breaking every existing scene asset's own
rendering, how does Runtime resolve/load a material's CPU data at
scene-load time while deferring its GPU realization to the correct
later point without a general Asset Catalog or a hand-rolled rollback
mechanism, and how does the existing atomic-publish/ownership-order
contract extend to cover both the CPU-side data and the later,
per-frame-realized GPU resources.

## Decision

**`Renderable` (both `DecodedRenderable` and `world::Renderable`) gains
an optional material `AssetId` reference. The Scene Asset format bumps
version to carry it, with no dual-version reader. Runtime's existing
per-scene manifest and CPU-side atomic-publish mechanism extend, in
kind, to resolve and load materials and their own texture dependencies
alongside meshes — but the resulting *GPU* resources
(`SampledTexture`/`Sampler`/`Material`) cannot be constructed at that
same point, because no real windowed `RenderTarget` exists yet at scene-
load time and a one-time texture upload mandatorily requires one (Spec
0016's own already-`Accepted` constraint). Realization therefore splits
into two phases — CPU-only resolution/loading at scene-load time, then
deferred, per-frame GPU realization once a real `RenderTarget` exists —
with a hardcoded fallback to today's single Material whenever a node
names no material asset, so every currently-authored scene keeps
rendering exactly as it does today.**

1. **`Renderable` widening:**
   ```cpp
   // atlantis::asset_system::DecodedRenderable
   struct DecodedRenderable {
     atlantis::asset_system::AssetId meshAsset = 0;
     std::optional<atlantis::asset_system::AssetId> materialAsset;
   };

   // atlantis::world::Renderable
   struct Renderable {
     atlantis::asset_system::AssetId meshAsset = 0;
     std::optional<atlantis::asset_system::AssetId> materialAsset;
   };
   ```
   `std::nullopt` means "no material scene binding for this node" —
   the same optionality convention `Camera`/`activeCameraIndex`/
   `parentNodeId` already use throughout this exact pipeline, never a
   sentinel value. `fromValidatedSceneData()` copies
   `n.renderable->materialAsset` into `Renderable::materialAsset` with
   the same trivial, infallible field-copy shape it already uses for
   `meshAsset` (`scene_instantiation.cpp:30-32`'s own pattern, widened
   by one field) — `atlantis::world::World`'s own module boundary,
   dependency closure (`Atlantis::Core` + `Atlantis::AssetSystem`, for
   `AssetId` only), and construction of zero Renderer/RHI types all
   remain exactly as [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)
   already establishes.
2. **Scene authoring grammar:** `atlantis_scene_source_version: 1 → 2`.
   A node may optionally carry `material=<logical path>`, resolved to
   an `AssetId` by `cookScene()` the same way `mesh=<logical path>`
   already is (`cookScene()`'s own existing `normalizeLogicalPath()` +
   `computeAssetId()` step, applied to one more optional token). A
   version-1 source is rejected outright with the existing
   `SceneSourceParseError::UnknownSourceVersion` — no dual-version
   reader, matching [ADR-0058](0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)'s
   own precedent for the mesh format exactly.
3. **Scene binary artifact:** schema version bump; the per-node record
   widens to carry a `has_material` flag and a `material_asset_id`
   (little-endian `u64`), encoded/decoded with the exact same explicit
   shift/mask discipline the existing `has_renderable`/`mesh_asset_id`
   pair already uses. A version-1 artifact is rejected outright.
4. **Migration — explicit, no silent visual change:** every
   currently-checked-in scene source (`world_scene.scene.txt`) and
   every embedded scene-source-literal test string is re-authored only
   to the version-2 grammar's own version line — no existing node gains
   a `material=` token. Their own cooked artifacts change (new schema
   version, wider per-node record, `has_material = 0` throughout); their
   own rendered pixels do not, guaranteed by Runtime's own fallback
   (item 8, below) and verified directly by the existing
   `minimal_cube`/`world_scene` goldens staying byte-for-byte unchanged
   on disk throughout Implementation.
5. **Per-scene manifest extension, not a catalog:**
   `atlantis_add_scene_asset()` gains a `MATERIAL_DEPENDENCIES`
   argument, mirroring the existing `MESH_DEPENDENCIES` mechanism
   exactly; each entry must already be declared via the new
   `atlantis_add_material_asset()`
   ([ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)),
   itself taking a `TEXTURE_DEPENDENCIES` argument. At CMake configure
   time, a material dependency's own already-known texture-dependency
   artifact/metadata paths are pulled transitively into the **same,
   unchanged, three-column** manifest format
   (`logicalPath\tartifactPath\tmetadataPath`) the mesh-only manifest
   already uses — no new column, no schema change, no new file. The
   manifest remains exactly what it is today: generated fresh per
   scene at CMake generate time, listing only that one scene's own
   declared dependency closure, never a portable part of any artifact,
   never queryable from outside that one scene's own build step.
6. **Runtime resolution splits into two phases — corrected during this
   ADR's own centralized final review, not the single-phase extension
   originally drafted here.** Confirmed directly: `loadAndInstantiateScene()`
   runs entirely inside `RuntimeApplication::initializeSteps()`, before
   any real windowed `RenderTarget` has ever been acquired (`Presentation`
   itself is not constructed until the first `platform::SurfaceCreated`
   event, observed only inside `runFrame()`'s own event loop, which runs
   after `initializeSteps()` has already returned). `Device::submit()`'s
   own real signature (`submit(std::unique_ptr<CommandList>, const
   RenderTarget& target)`) takes a non-null `RenderTarget&` — there is no
   target-independent overload. A one-time texture upload is,
   by Spec 0016's own already-`Accepted` constraint, mandatorily a
   RenderGraph-recorded operation against a real `RenderTarget`'s own
   `CommandList` submission — it cannot happen during
   `initializeSteps()`. The two phases:

   - **Phase 1 (CPU-only, inside `loadAndInstantiateScene()`, unchanged
     in kind from today's mesh handling):** collect distinct mesh
     *and* material `AssetId`s in first-reference order; resolve every
     one against the same manifest resolver *before* any loading
     begins — an unresolvable material `AssetId` fails the whole scene
     load with its own `RuntimeInitError` sub-code, exactly like an
     unresolvable mesh `AssetId` does today; load meshes and their real
     GPU `Mesh` buffers (unchanged — `Buffer` creation needs no
     `RenderTarget`); load each distinct material via
     `loadMaterialAsset()` and each distinct material's own texture via
     `loadTextureAsset()` (deduplicated by texture `AssetId`), both
     returning **CPU-only DTOs** — no `SampledTexture`, `Sampler`,
     `Pipeline`, or `Material` is constructed in this phase.
   - **Phase 2 (deferred GPU realization, inside `runFrame()`, at the
     exact point `Material` already rebuilds today):** `runFrame()`'s
     existing format-change block (`runtime_application.cpp:295-313`)
     is the only place in Runtime's own real code where a real,
     acquired `RenderTarget` and a known `colorFormat` coexist for the
     first time. On every frame, for each distinct material referenced
     by a currently-renderable entity that is not yet a key in
     `materialResourceMap_`, attempt realization as one local,
     all-or-nothing sequence (`Sampler`, `SampledTexture`, a staging
     `Buffer` with the already-loaded pixel bytes copied in, and a
     `Pipeline`/`Material` against the frame's own current
     `colorFormat` — none of these four calls needs a `RenderTarget`).
     Any failure destroys everything created so far via ordinary RAII
     (nothing submitted yet, safe to abandon unconditionally) and
     leaves that material pending, retried next frame — logged, never
     fatal. Every material whose realization attempt succeeded this
     frame gets its own texture-upload RenderGraph pass recorded into
     the **same `CommandList`** this frame's real draw graph will also
     use, recorded *before* `Renderer::drawFrame()`'s own call — the
     recording order alone (both graphs in one `VkCommandBuffer`)
     is what makes the upload's own barrier complete before the draw's
     `cmd.bindTexture()` call executes; `Renderer::drawFrame()`'s own
     internal graph (confirmed directly, `renderer.cpp:20-54`) never
     itself tracks the sampled texture as a resource — `cmd.bindTexture()`
     is a raw call requiring the texture to already be `ShaderRead`.
     This same frame's own `DrawItem` list is built using the candidate
     bundle directly — a just-realized entity's `item.material` points
     at the local candidate `Material`, not yet a map entry — so
     realization is visible to drawing the very frame it succeeds, not
     deferred to the next one. Safe by construction:
     `Renderer::drawFrame()`'s own command recording reads
     `item.material` synchronously at record time and embeds only the
     real Vulkan handle values into the command buffer, never the C++
     pointer itself; moving the candidate's own wrapper objects into the
     resource maps afterward (below) changes their C++-object address,
     not the handle values already recorded, and no `DrawItem` outlives
     the one `drawFrame()` call that consumed it.
     Exactly one `Device::submit()` per frame covers both the upload(s)
     and the draw together. Immediately after that `submit()` succeeds
     and before `present()`, `device_->waitIdle()` is called **only on
     a frame where at least one material was newly realized** — this is
     what makes every staging `Buffer` created that frame safe to
     destroy immediately afterward.

     **The `waitIdle()`-then-`present()` ordering is verified against
     the real Vulkan call chain, not merely asserted as "should be
     fine":** `VulkanDevice::submit()` (`vulkan_device.cpp:510-566`)
     calls `vkQueueSubmit(queue_, 1, &submitInfo, submissionFence_)`,
     where `submitInfo.pSignalSemaphores` names the target's own
     `renderFinishedSemaphore` — the GPU signals both `submissionFence_`
     and this semaphore together, on completion of the exact same
     submitted work. `submit()` returns a `VulkanSubmissionSignal`
     wrapping that same semaphore (`vulkan_device.cpp:565`).
     `VulkanDevice::waitIdle()` (`vulkan_device.cpp:568-585`) calls
     `waitAndReleaseRetainedSubmission()`, which itself calls
     `vkWaitForFences(device_, 1, &submissionFence_, VK_TRUE,
     UINT64_MAX)` (`vulkan_device.cpp:465`) — blocking until the exact
     submission that signals both the fence and the semaphore has
     completed on the GPU — then additionally calls
     `vkDeviceWaitIdle(device_)` (`vulkan_device.cpp:580`) as a second,
     stronger guarantee. By the time `waitIdle()` returns `Ok`, the
     `renderFinishedSemaphore` this frame's `submit()` named has
     therefore already been signaled by the GPU. `VulkanPresentation::present()`
     (`vulkan_presentation.cpp:603-638`) then builds a `VkPresentInfoKHR`
     with `pWaitSemaphores` set to that exact same semaphore
     (`vulkan_presentation.cpp:610-616`) and calls `vkQueuePresentKHR()`.
     Per the Vulkan specification's own binary-semaphore wait contract
     (`VkPresentInfoKHR::pWaitSemaphores`), a wait operation is valid
     against a semaphore that is *already* signaled at the time the
     wait is submitted — a signal, once produced, unconditionally
     satisfies exactly one subsequent wait, whether that wait arrives
     before or after the signal — so `vkQueuePresentKHR` simply proceeds
     without blocking; this is not a special case the specification
     carves out, it is the ordinary semantics of a binary semaphore's
     one signal, one wait pairing, which this call chain preserves
     exactly (one `vkQueueSubmit` signal, one subsequent `vkQueuePresentKHR`
     wait, regardless of the intervening `waitIdle()`, which touches
     only the fence- and `vkDeviceWaitIdle`-based CPU-side wait
     machinery — entirely orthogonal state from the semaphore's own
     GPU-side signal, which `waitIdle()` neither consumes nor
     interacts with). No real Vulkan or RHI-level contract is violated
     by this ordering; if a future finding contradicts this, that
     finding must cite the specific spec clause or real validation
     failure it rests on, not a general suspicion of the sequence.
     Only after `waitIdle()` returns `Ok` does the realized
     `SampledTexture`/`Sampler`/`Material` move into
     `textureResourceMap_`/`samplerResourceMap_`/`materialResourceMap_` —
     the one and only point a material becomes visible to any draw. A
     `submit()` failure on a realization frame is treated with the same
     severity `runFrame()`'s own existing plain-draw `submit()` failure
     already has (`classifySubmitError()` + `lifecycle_.markFailed()`)
     — nothing from a rejected `CommandList` executed, so every
     locally-created object for that frame's attempts is destroyed via
     ordinary RAII, unconditionally safe. An entity whose
     `materialAsset` is present but not yet realized is skipped for
     that frame's `DrawItem` list — the same recoverable, per-entity
     pattern `resolveMeshAsset()`'s own failure path already uses —
     never silently substituted with the built-in fallback `Material`
     (reserved exclusively for `materialAsset == std::nullopt`, item 8
     below) and never scene-load-fatal (that severity is reserved for a
     Phase 1 resolve/load failure).
7. **Atomic publish stays a Phase 1, CPU-only concept:**
   `SceneLoadOutcome` widens to carry the new mesh resource map
   (unchanged) plus two new **CPU-only** `AssetId`-keyed maps — loaded
   `MaterialAssetData`/`TextureAssetData` — alongside `world`. Each new
   map gets its own `static_assert(std::is_nothrow_move_constructible_v<...>)`
   or `is_nothrow_move_assignable_v<...>` at the exact same call site
   the existing two already occupy (`runtime_application.cpp:83-89`'s
   own pattern), proving the widened Phase 1 publish step is genuinely
   atomic by compile-time construction — no catch/rollback is
   introduced, matching [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)'s
   own existing "compute everything fallibly first, publish only
   infallible moves at the end" structure exactly. **No GPU
   material/texture/sampler resource map is part of this atomic
   publish** — those three maps are populated incrementally, per
   distinct material, by Phase 2's own per-frame realization loop (item
   6 above), never as one all-or-nothing scene-wide step; this is a
   deliberate, disclosed difference from mesh's own all-loaded-at-once
   Phase 1 shape, required because Phase 2 cannot even begin until a
   real `RenderTarget` exists, which Phase 1's own transactional
   boundary does not and cannot wait for.
8. **Per-entity binding with fallback:** `runFrame()`'s `DrawItem`
   extraction reads `Renderable::materialAsset`; when present *and*
   already realized (a key in `materialResourceMap_`), binds
   `item.material` to that entry; when absent, falls back to Runtime's
   own existing single, hardcoded `Material` — the exact mechanism that
   keeps every currently-authored, unmodified-by-this-Spec scene
   rendering unchanged. When present but not yet realized, see item 6's
   own per-entity skip — never conflated with the absent-fallback case.
9. **GPU ownership/destruction order, and atomic, all-or-nothing
   format-change rebuild — corrected during this ADR's own final
   review from a per-entry-independent design that was wrong.**
   `SampledTexture`/`Sampler` instances must outlive every `Material`
   that borrows them, and both must be destroyed before `Device` —
   `Material`'s own already-documented contract, already demonstrated
   structurally by `textured_quad_fixture.cpp` (declaring its own
   textures/sampler *before* its own materials, so C++'s reverse-
   declaration-order destruction destroys materials first).
   `RuntimeApplication`'s member declaration order places its new
   `textureResourceMap_`/`samplerResourceMap_` before its new
   `materialResourceMap_`, for the identical reason; `device_` remains
   declared first, per the existing pattern, so it outlives everything
   this ADR adds; `shutdown()`'s own existing ordered `.reset()`/`.clear()`
   sequence widens to clear the three new maps in the same order.

   Because `PipelineCreateParams::colorFormat` is baked into a
   `Pipeline` at creation time, drawing with a `Pipeline` built for one
   swapchain format against a `RenderTarget` of a *different* format is
   a genuine mismatched-attachment-format condition (a real Vulkan
   Validation Layers violation), not a merely-suboptimal frame.
   Confirmed directly: today's single-`material_` rebuild
   (`runtime_application.cpp:295-313`) does not actually guard against
   this on its own failure path — if `createMaterial()` fails during a
   format change, the code keeps the *old*-format `material_` and falls
   through, two lines later, to draw with it against `*target` (already
   the *new* format). This is a latent, real gap in Spec 0013's own
   existing code, found during this review — not an accepted design to
   extend. Folding the single fallback `Material` into the *same*
   atomic contract as the new per-material map (required regardless, to
   answer "what does an entity with `materialAsset == nullopt` draw
   with mid-rebuild") is what surfaces and closes this gap, as a
   direct, disclosed, in-scope consequence of unification.

   **Corrected contract:** `SampledTexture`/`Sampler` never rebuild on
   a format change (a texture's own format is independent of the
   swapchain's `colorFormat` — confirmed directly,
   `SampledTextureCreateParams`/`SamplerCreateParams` name no
   swapchain-format field — so every rebuild below reuses them
   unchanged, needing no new upload, `CommandList`, or `submit()`). On
   format-change detection, Runtime builds a complete *candidate*
   batch first — a new fallback `Material` plus a new `Pipeline`/
   `Material` for **every** existing `materialResourceMap_` entry,
   each a synchronous `createMaterial()` call needing only the new
   `colorFormat` and that entry's own already-uploaded
   `SampledTexture`/`Sampler` — entirely in local, RAII-owned state.
   Only if **all** of these candidates succeed does Runtime swap
   `material_` and the whole `materialResourceMap_` in together, in one
   step, updating `lastSeenFormat_` only then (old objects destroyed
   only after their replacements already exist, matching today's
   single-Material create-before-destroy ordering, generalized to the
   batch). If **any one** candidate fails, the entire candidate batch
   is discarded via ordinary RAII (nothing was ever submitted — safe to
   abandon unconditionally); the existing `material_`/
   `materialResourceMap_` are left completely untouched, still RAII-
   alive for the old format, but **not used to draw this frame** — the
   frame draws an empty `DrawItem` list instead (still correctly clears
   the target to the new format, since no mismatched `Pipeline` is ever
   bound), retried next frame with the same non-fatal severity today's
   `createMaterial()`-failure path already uses (logged, never
   `lifecycle_.markFailed()`). First-time realization of a brand-new
   material (item 6 above) stays a separate, incremental path that adds
   one map entry without touching any other — it is never folded into
   this atomic rebuild, and a format-change rebuild never attempts to
   realize a material that has not yet had its own first, independent
   realization succeed.
10. **Deduplication via `AssetId`-keyed maps, no global registry:**
    `materialResourceMap_`/`textureResourceMap_`/`samplerResourceMap_`
    are each a fresh `std::unordered_map<AssetId, T>`, populated
    incrementally across frames by Phase 2's own per-frame realization
    loop (item 6) — not rebuilt within one function call the way
    `meshResourceMap_` is populated within `loadAndInstantiateScene()`,
    but identical in every other respect: scoped to the current scene's
    own lifetime (from scene load until the next scene load or
    `shutdown()`), never a persistent, cross-load, or cross-session
    store. Multiple entities referencing the same material or
    underlying texture dedup for free — "pending realization" (item 6's
    own per-frame set-difference computation, recomputed fresh each
    frame, never a separately persisted queue) never attempts a
    material already present as a map key — with no new caching design
    and no global mutable registry of any kind.

## Consequences

### Positive

- Every currently-authored scene asset keeps rendering byte-for-byte
  identically — proven by the existing `minimal_cube`/`world_scene`
  goldens, not merely argued.
- Runtime's own already-audited, `static_assert`-proven atomic-publish
  mechanism extends in structure, not in kind, for the *CPU-side*
  material/texture data — no new transactional concept, no new
  failure-recovery design for Phase 1, just more members published by
  the same proven pattern.
- Phase 2's own GPU realization needs no new failure-classification
  machinery either: because a realization frame's upload and draw share
  one `Device::submit()` call, a `DeviceLost`/submit failure on that
  exact frame flows through `runFrame()`'s own existing
  `classifySubmitError()` path unmodified — there is no separate
  "upload failed" error path to design or test.
- Deduplication is free: reusing the exact `AssetId`-keyed-map shape
  `meshResourceMap_` already uses means no new caching logic, no new
  invalidation question, and no risk of the kind of global mutable
  state this codebase's own conventions reject.
- `World`'s own module boundary and dependency closure are completely
  unchanged in kind — one more optional `AssetId` field on an existing
  component, using a dependency (`AssetId`) `World` already has for
  exactly this reason.
- Unifying the fallback `Material` into the same atomic candidate-batch
  contract as the new per-material map surfaced and closed a real,
  latent gap in Spec 0013's own existing single-`Material` rebuild code
  (drawing with an old-format `Pipeline` against a new-format
  `RenderTarget` on a rebuild failure) — a genuine correctness
  improvement to already-shipped code, found only because this ADR
  required looking at that exact path closely enough to generalize it.

### Negative / Trade-offs

- A format-change rebuild is now genuinely all-or-nothing: if even one
  of potentially many materials fails to rebuild against the new
  format, **no** material draws that frame (an empty `DrawItem` list,
  not a partially-correct one) — a real, accepted trade-off preferring
  a uniformly blank frame over a partially-wrong one, and a behavioral
  change from today's single-Material code's own prior (latent-buggy)
  "keep drawing with the old value" fallthrough.

- The scene format's version bump forces a repository-wide sweep of
  every embedded scene-source-literal test string, mirroring the exact
  risk Spec 0017's own Plan Review found for mesh sources — a
  hardcoded assertion unrelated to the literal string itself could
  hide from a grep-only inventory; the Plan must require running the
  full test suite, not trusting a static search alone.
- Resolution is genuinely two-phase, not one — a real, added conceptual
  surface future maintainers must understand: CPU-side data can be
  "loaded" (Phase 1, scene-load time) without yet being "realized"
  (Phase 2, per-frame, GPU-side) for potentially several frames after a
  scene loads, and an entity referencing a not-yet-realized material is
  silently skipped from that frame's draw (logged, not fatal) until
  realization succeeds.
- A one-time CPU stall (`waitIdle()`) is paid on every frame that
  realizes at least one new distinct material — accepted as a
  correctness-first, simplest-first choice for this foundation round;
  avoiding it (relying on the next frame's own implicit
  single-frame-in-flight wait to reclaim a staging buffer instead) is
  legitimate, unattempted future optimization.
- Every distinct material `AssetId` a scene load resolves gets its own
  real `Pipeline`/`Material`/`SampledTexture`/`Sampler` — no caching or
  reuse across distinct `AssetId`s sharing a `MaterialKind`, an
  explicit, accepted limitation of this round (Spec 0018's own
  Non-Goals).
- Runtime's `RuntimeApplication` gains three more members
  (material/texture/sampler resource maps) whose declaration order is
  now load-bearing for correctness (item 9) — a real, if
  already-precedented, source of "this must not be reordered casually"
  fragility future changes to this struct must respect.

## Alternatives Considered

- **Make `materialAsset` a required field**, with every existing scene
  asset migrated to name a real (possibly placeholder) material.
  Rejected: forces an unrelated migration burden on every existing
  scene for no functional reason, and removes the natural fallback
  path (item 8) that keeps today's rendering unchanged — the entire
  reason this ADR can make the strong "no silent visual change"
  guarantee it does.
- **A fourth manifest column recording each entry's own asset kind**
  (mesh/material/texture), to let the resolver disambiguate.
  Rejected: Runtime already knows which `AssetId`s are mesh references
  and which are material references directly from the scene artifact's
  own decoded structure (two disjoint fields on `DecodedRenderable`) —
  a manifest-level kind tag would be redundant data solving a
  disambiguation problem that does not exist.
- **A separate, dedicated manifest file for material/texture
  dependencies**, rather than folding them into the existing scene
  manifest. Rejected: would double the number of build-tree-private
  files Runtime must read per scene load for no benefit — the existing
  three-column format already generalizes to any `AssetId`-keyed
  dependency without a schema change, and one file per scene is
  simpler for both the CMake-side generator and the Runtime-side reader
  to reason about than two.
- **Skip the atomic-publish extension for material/texture *CPU data*;
  publish meshes/`world_` atomically as today, then resolve/load
  materials separately, best-effort, after.** Rejected — and worth
  distinguishing precisely from the two-phase GPU-realization split
  this ADR *does* adopt (item 6): that split is a structural
  consequence of `Device::submit()` requiring a real `RenderTarget`
  that provably does not exist at scene-load time, not a relaxed
  guarantee. This rejected alternative is different — it would relax
  Phase 1's own CPU-side resolve/load step, letting a material fail to
  resolve or load *without* failing the whole scene load the way an
  unresolvable mesh reference already does. Rejected: would reintroduce
  exactly the "a `Renderable` could reference a mesh that loaded but a
  material that didn't, at the CPU level, for no structural reason"
  partial-scene risk [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
  was written to eliminate for meshes; there is no principled reason a
  material's own *CPU-side* resolve/load step deserves weaker
  guarantees than a mesh's, since nothing about CPU-side loading is
  RenderTarget-dependent. GPU realization is deferred because it must
  be; CPU resolve/load atomicity is not relaxed, because it need not
  be.
- **Defer staging-buffer destruction by one frame instead of calling
  `waitIdle()` on a realization frame**, relying on `Device::submit()`'s
  own documented "internally waits on ... any previously-retained
  submission before accepting this one" single-frame-in-flight
  behavior to guarantee a prior frame's upload has completed by the
  time its own staging buffer is freed. Rejected for this round, not
  ruled out permanently: it avoids the one-time CPU stall this ADR
  accepts, but requires `RuntimeApplication` to track "staging buffers
  awaiting the next frame's own implicit wait" as new, real lifetime
  state with its own edge cases (what if the *next* frame itself never
  runs, e.g. the window closes immediately after realization) — real,
  legitimate optimization work this foundation round does not need,
  named explicitly in Spec 0018's own Non-Goals rather than half-built.
