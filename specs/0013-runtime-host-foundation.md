# Spec: Runtime Host Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Created:** 2026-08-20
- **Historical scope:** Two independent self-review rounds (2026-08-20)
  preceded Human Review — see
  [PR #61](https://github.com/slmao/Atlantis/pull/61) for the full
  revision history. Round 1 confirmed the central architectural question
  (whether Runtime can be built without changing any existing module's
  public API) has a verifiable answer — **no new public API is required**
  — checked directly against every composed module's current headers, and
  fixed one internal inconsistency (`module_boundaries.md`'s `PROPOSED`
  Runtime section lists RenderGraph as a direct Runtime dependency, but no
  existing composition root includes any `atlantis/render_graph/*.h`
  header — `Renderer::drawFrame()` already fully encapsulates RenderGraph
  construction/compilation/execution internally). Round 2 found and fixed
  one substantive drafting error and three mechanical ones, all corrected
  before this spec's status changed:
  - **Substantive:** the Testing & Verification Plan originally claimed
    this spec's windowed bootstrap composition could be automatically
    pixel-compared against Spec 0011's `minimal_cube` golden via
    `atlantis::image_regression::compareBuffers()`. This is false as a
    matter of real, current interface capability: swapchain-backed
    `RenderTarget`s are created with `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
    VK_IMAGE_USAGE_TRANSFER_DST_BIT` only
    (`src/vulkan_backend/src/vulkan_presentation.cpp`), never
    `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`, which
    `CommandList::copyRenderTargetToBuffer()`'s own `vkCmdCopyImageToBuffer`
    call requires of its source image. Corrected to a three-layer
    verification model (see Testing & Verification Plan).
  - **Mechanical:** the presentation/error-state table (and Decisions
    item 6) omitted `PresentationError::SwapchainCreationFailed`, a real,
    reachable outcome of `acquireNextTarget()`'s internal
    `recreateIfNeeded()` call — fixed to cover the enum's full four-value
    set. Added an explicit note on why Initialization steps 3–6's failure
    teardown never calls `Device::waitIdle()`. Corrected a stale
    `specs/README.md` cross-reference (Spec 0012's row still read "does not
    wait for Atlantis Runtime (Candidate Order 2 below, not yet specced)").
- **Human Review Approval (2026-08-20):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`). This approval explicitly accepts:
  1. The `atlantis_runtime_host` private static library plus a thin
     `atlantis_runtime` Windows executable split (ADR-0047) — the private
     library exists solely for entry-point reuse and GPU-independent
     testability, not a public dependency surface any other top-level
     module may consume.
  2. Runtime, as composition root, depending on and selecting
     `Atlantis::VulkanBackend` directly — without any `Vk*` type crossing
     into any other module's public surface or into the GPU-independent
     lifecycle/state-machine test boundary.
  3. ADR-0046's complete initialization order, object-ownership model,
     per-frame execution order, `Device::waitIdle()` usage, and
     reverse-order destruction contract, as fixed in Requirements.
  4. A pure `RuntimeLifecycleState` lifecycle/decision state-machine
     boundary for GPU-independent testing — no general dependency
     injection, service locator, or fake-engine-interface set.
  5. A bootstrap scene combining only the existing cooked `minimal_cube`
     asset, the existing `minimal_mesh` shader, a camera, and one fixed
     `Material` — no World/ECS/Scene abstraction.
  6. The revised, complete `Presentation`/`acquireNextTarget()`/`submit()`
     error classification — including
     `PresentationError::SwapchainCreationFailed` — and its
     recoverable/unrecoverable handling.
  7. Windows windowed mode only for this round — no headless Runtime,
     server mode, or Android implementation.
  8. A minimal configuration boundary — no general application
     configuration system designed or implied.
  9. Every existing example/demo retained, unchanged, as its own spec's
     disclosed verification composition.
  10. No Client API, Editor IPC, remote transport, or command/query/event
      protocol in this round — ADR-0033's Runtime-authority principle
      acknowledged but not exercised beyond local, single-process
      ownership.
  11. No change to any existing public API of Platform, RHI, Vulkan
      Backend, Renderer, Shader System, or Asset System.
  12. The revised three-layer verification model: a Runtime GPU smoke test
      covering real windowed acquire/draw/submit/present and Vulkan
      Validation Layers; the existing, unmodified headless image-regression
      suite as the continuing automated pixel-level regression gate;
      manual, by-eye comparison of Runtime's visible window against the
      existing `minimal_cube` golden PNG; and no addition of
      `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` to the swapchain or any other
      windowed-readback capability.

  [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md)
  and
  [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)
  both move to `Accepted` alongside this approval. **This approval
  authorizes drafting Plan 0013; it does not itself authorize
  Implementation.**
- **Related Plan(s):**
  [plans/0013-runtime-host-foundation.md](../plans/0013-runtime-host-foundation.md)
  (`Approved / Ready for Implementation`, joint Spec + Plan Human Review
  recorded 2026-08-21, with a 2026-08-21 Human Review Amendment correcting
  D3/V3's enum-exhaustiveness mechanism). Implementation is authorized
  only once [PR #61](https://github.com/slmao/Atlantis/pull/61) has merged
  into `main`.
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md)–[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md),
  [ADR-0009](../adr/0009-assertion.md),
  [ADR-0011](../adr/0011-native-window-handle-representation.md)–[ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md),
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md),
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)–[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md),
  [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md)–[ADR-0037](../adr/0037-long-term-device-backend-extensibility-without-phase1-scaffolding.md),
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)–[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md),
  [ADR-0043](../adr/0043-asset-system-module-boundary.md)–[ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (all `Accepted`; none reopened or modified). New:
  [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md)
  (Runtime composition, object ownership, frame lifecycle) and
  [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)
  (Runtime Host executable/library structure and test boundary) — both
  `Accepted` alongside this spec's Human Review Approval.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  Batch 4 PR (pending). Original scope, obligations, the fixed
  initialization/per-frame/shutdown ordering, and the error-state table
  retained; the round-by-round self-review narration is preserved in PR
  #61 history.

## Summary

This spec introduces `Atlantis Runtime` as a real module for the first
time: a genuine, verifiable Windows executable — `atlantis_runtime` — that
replaces every existing demo's role as the project's de facto composition
root with the actual thing `AGENTS.md`'s module list has always named but
never built. It composes Atlantis Platform, the Vulkan Backend/RHI,
Atlantis Renderer, Atlantis Shader System, and Atlantis Asset System —
every one **exactly as already `Accepted`/implemented, with zero public
API change** — into one fixed, single-threaded startup → frame loop →
shutdown lifecycle that loads the already-cooked `minimal_cube` asset and
the already-compiled `minimal_mesh` shader, and displays a real window
showing that mesh. It fixes object ownership and init/frame/destruction
ordering, a small internal `atlantis_runtime_host` library boundary for
testability (not a new public dependency surface), and how initialization
failure, recoverable presentation states, and unrecoverable device errors
each map to a distinct outcome and exit code. It does not touch World/ECS,
an Editor/Client protocol, headless mode, or any new rendering capability.

## Motivation / Problem Statement

`AGENTS.md`'s ten-module list has named **Atlantis Runtime** from the
start. Every other module in that list — Core, Platform (Windows), RHI,
Vulkan Backend, RenderGraph, Renderer, Shader System, Asset System (Spec
0012), and Tools — is now `Accepted`/`Approved` and implemented. Runtime
alone remains, per
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)'s
`PROPOSED` description, a description of an *intended shape*, not an
implemented thing.

What stands in for it today is a series of explicitly disclosed,
non-shipping verification compositions: `examples/rhi_vulkan_demo`,
`examples/frame_execution_demo`, `examples/minimal_renderer_demo`,
`examples/headless_rendering_demo`. Every one carries the same disclaimer
([specs/0003-rhi-vulkan-windowed-foundation.md](0003-rhi-vulkan-windowed-foundation.md)'s
words: "this spec's own verification uses a minimal, non-shipping
composition... It is not, and must not be mistaken for, Runtime itself.").
None of them is the actual product entry point a user launches, and none
consumes Asset-System-sourced content while presenting to a real window
(the one composition that does load an Asset-System-sourced mesh,
`minimal_cube_fixture.cpp`'s `setUpMinimalCubeFixtureFromAsset()`, renders
to an **offscreen** target, never a window).

[specs/README.md](README.md)'s Candidate Spec Backlog names this gap as
"Runtime Host and Composition Root," depending on Spec 0002 (Platform),
0003 (RHI), 0005 (RenderGraph Foundation), 0007 (Minimal Renderer) — all
`Approved` and implemented; this spec's analysis finds Runtime
additionally, necessarily depends on Spec 0008 (Shader System) and Spec
0012 (Asset System), both also `Approved`/implemented, since a real
Runtime cannot draw anything without a compiled shader and cannot claim to
be "the real thing" while still hand-authoring its mesh data in C++.

[ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)
(`Accepted`, Spec 0009) already commits Atlantis to a long-term principle
— Runtime is the sole authoritative owner of engine world state, and every
external module accesses it as a Client — but explicitly leaves "whether
Runtime is built as a linked library, a statically-linked executable, or
some other packaging shape" and "Runtime's internal threading/concurrency
model" as Out of Scope. This is that spec. It exercises ADR-0033's
principle only to the minimal extent a single, real Runtime process with
no second Client actually needs — it does not design a Client API, an IPC
protocol, or a Query/Command/Event surface.

### Why this is buildable without changing any existing public API

**Every capability this spec's bootstrap scene needs is already exposed,
unchanged, by an existing `Accepted` public API**, proven end to end by
two existing, independently-verified compositions:

- `examples/minimal_renderer_demo/main.cpp` already proves the complete
  windowed lifecycle: `platform::initialize()` →
  `vulkan_backend::createDevice()` → (on the first `SurfaceCreated` event)
  `vulkan_backend::createPresentation()` → a per-frame `acquireNextTarget()`
  → format/extent-change handling → `Renderer::drawFrame()` → `submit()` →
  `present()` loop, with `notifyResized()`, minimize/restore, close, and
  clean shutdown — using a **hand-authored** mesh and a **checked-in,
  build-tree-loaded** shader pair.
- `minimal_cube_fixture.cpp`'s `setUpMinimalCubeFixtureFromAsset()`
  already proves that `atlantis::asset_system::loadStaticMeshAsset()`'s
  output (`StaticMeshAssetData`) feeds `atlantis::renderer::createMesh()`'s
  existing, unmodified signature with no conversion step (Plan 0012 Step 6,
  GPU-verified against Spec 0011's golden with **zero** channel
  difference) — using an **offscreen** target, not a window.

No existing composition combines both halves — an Asset-System-sourced
mesh, presented to a real window. This spec is the first to do that, but
combining them requires **new orchestration code only**, not a new
capability: `Renderer::drawFrame()`'s own `finalColorState` parameter
already distinguishes exactly this case
(`atlantis::rhi::ResourceState::PresentSource` for a windowed caller vs.
`TransferSource` for a headless one), and every other call is used exactly
as its existing signature allows. If this spec's implementation later
discovers a real gap, that is to be raised as its own architectural
question, not patched around silently — this self-review found none.

## Goals

- Introduce **`Atlantis Runtime`** as a real Windows executable,
  `atlantis_runtime`, that is the project's actual composition root, not
  another disclosed, non-shipping demo.
- Compose Atlantis Platform (Windows), the Vulkan Backend/RHI, Atlantis
  Renderer, Atlantis Shader System, and Atlantis Asset System into one
  fixed startup → frame loop → shutdown lifecycle, with **zero change** to
  any of their existing public APIs.
- Fix, precisely and completely, object ownership, initialization order,
  per-frame call order, and reverse-order destruction.
- Load the already-cooked `minimal_cube` Asset System runtime artifact and
  the already-compiled `minimal_mesh` Slang shader pair, and display a
  real window showing that mesh.
- Fix the single-threaded startup, frame-loop, resize, minimize/restore,
  close, and normal-exit contract as a complete, testable state machine.
- Fix, exhaustively, how initialization failure, each recoverable
  presentation state, and each unrecoverable device/presentation error is
  handled and which exit-code category it maps to.
- Realize ADR-0033's Runtime-authority principle to the extent this spec's
  single-process, no-second-Client scope requires — without designing an
  Editor/Client transport, protocol, or API surface ahead of a real
  second consumer.
- Preserve every existing public API and dependency direction of Renderer,
  RHI, Platform, Asset System, and Shader System exactly as `Accepted` —
  and if implementation work found a genuine gap, raise that explicitly.
- Deliver a Runtime executable other Specs can build directly on —
  World/ECS, Tool/Editor Connection Protocol, and every later Candidate
  Backlog item that names Runtime as a dependency — without this spec
  designing any of them.

## Non-Goals

Explicitly excluded:

- **Android, iOS, or Linux.** Builds, tests, and verifies Windows only.
  Notes (Architectural Impact) that its internal library boundary does not
  preclude a future Android entry point reusing it, but does not design
  one. Linux is not a target platform.
- **World/ECS, a scene graph, scene serialization, or stable entity
  GUIDs.** The bootstrap scene is exactly one hardcoded `DrawItem`; no
  entity, component, or scene-description format. ADR-0033's future
  "authoritative world state" remains entirely unimplemented — this spec's
  bootstrap state (one mesh, one material, one camera) is not framed as
  *world state*.
- **An Editor/Runtime IPC protocol, a Tool/Editor connection protocol, a
  Gameplay SDK, or any general Query/Command/Event/Snapshot API surface.**
  No process boundary, no wire protocol, no second Client.
- **A headless Runtime mode or a server mode.** Windowed-only. Headless
  rendering already has its own complete closed loop (Spec 0010/0011)
  serving its own consumers.
- **PBR, lighting, shadows, texturing, post-processing, or any new
  rendering capability.** One material — the same fixed-vertex-color
  `minimal_mesh` material every existing windowed demo uses.
- **Hot-reload of any kind, asynchronous asset streaming, a Job System, or
  multi-threaded frame orchestration.** ADR-0004's
  single-logical-frame-thread baseline is unchanged.
- **A plugin system, a general dependency-injection or service-locator
  framework, or a general application framework.** Runtime's internal
  testability boundary is a small, fixed, concrete composition.
- **Any change to Renderer's, RHI's, Vulkan Backend's, Platform's, Asset
  System's, or Shader System's existing public API**, unless
  implementation work finds a genuine, disclosed need. None is currently
  identified.
- **A general application configuration system, a settings file format, or
  command-line argument parsing beyond what Decisions item 8 explicitly
  allows as optional, minimal, Plan-stage detail.**
- **Frame pacing, a profiler, or a general timing/telemetry system.**
- **Rewriting, removing, or absorbing any existing example.** All remain,
  unchanged, as their own specs' disclosed verification compositions.

## Requirements

### Functional

**Module boundary**

- New module `src/runtime/`, realizing the already-named `Atlantis
  Runtime` slot in [AGENTS.md](../AGENTS.md)'s ten-module list. Depends
  on: `Atlantis::Core`, `Atlantis::Platform`, `Atlantis::RHI`,
  `Atlantis::VulkanBackend`, `Atlantis::Renderer`, `Atlantis::ShaderSystem`,
  `Atlantis::ShaderSystemRhiIntegration`, and `Atlantis::AssetSystem`.
  **Not** a direct dependency on `Atlantis::RenderGraph` — confirmed by
  direct inspection that no existing windowed or asset-sourced composition
  root includes any `atlantis/render_graph/*.h` header;
  `Renderer::drawFrame()` already fully owns RenderGraph construction,
  compilation, and execution internally. This is a corrected, narrower
  dependency list than
  [module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  current `PROPOSED` Runtime section states — see Architectural Impact.
- Per module_boundaries.md's `PROPOSED` Runtime section, Runtime is **the
  only module permitted to depend on Atlantis Platform** — unchanged by
  this spec, and satisfied: no other module gains a Platform dependency
  here.
- No `Vk*` type, and no `#include <vulkan/...>`, appears anywhere in
  Runtime's own source — Runtime calls only
  `vulkan_backend::createDevice()`/`createPresentation()`'s existing,
  already-backend-agnostic-typed signatures, exactly as every existing
  composition root does.

**Executable/library structure**

- Two CMake targets realize the module (see ADR-0047):
  - `atlantis_runtime_host` (static library), alias `Atlantis::RuntimeHost`
    — owns the actual composition logic: object construction/ownership,
    the initialization sequence, the per-frame orchestration, resize/
    lifecycle event handling, and reverse-order teardown. Contains **no**
    OS entry point (`main`/`WinMain`).
  - `atlantis_runtime` (executable) — a thin, per-OS entry point (Windows
    `WinMain`/`main`, exact form a Plan-stage detail) that constructs and
    drives `Atlantis::RuntimeHost`'s composition type and returns its exit
    code. No composition logic beyond argument handling explicitly allowed
    by Decisions item 8.
  - **`Atlantis::RuntimeHost` is not a new public dependency surface.** No
    other top-level module may depend on it; it exists solely so this
    spec's GPU-independent lifecycle/state-machine tests can exercise real
    composition logic without spawning a process or a window — mirroring
    Shader System's already-`Accepted` precedent of factoring
    `atlantis_shader_compiler`'s process-execution logic into a private
    `atlantis_shader_compiler_lib`. An internal testability seam, not an
    exercise of ADR-0033's future Client-boundary concept.

**Object model and ownership**

- A single composition type inside `Atlantis::RuntimeHost` (exact name a
  Plan-stage detail — this spec's prose calls it "the Runtime Host
  object") owns, directly or via `std::unique_ptr`/`std::optional` exactly
  as every existing composition root does, in this order: a `Platform`
  initialization handle (Platform itself is free functions, not an owned
  object); a `std::unique_ptr<atlantis::rhi::Device>`; a
  `std::unique_ptr<atlantis::rhi::Presentation>` (constructed lazily, on
  the first observed `SurfaceCreated` event); an
  `atlantis::renderer::Mesh` (constructed once, from the loaded Asset
  System data, at startup — never re-created); an
  `atlantis::renderer::Material` (constructed once at startup against the
  swapchain's initial format, and **rebuilt** — not mutated — whenever
  `Presentation::metadata().format` changes, per Spec 0007's `Accepted`
  caller-owned format-change contract); a
  `std::unique_ptr<atlantis::rhi::Buffer>` camera uniform buffer
  (constructed once); a `std::unique_ptr<atlantis::rhi::Texture>` depth
  buffer (constructed once at the initial extent, **recreated** whenever
  the acquired `RenderTarget`'s extent changes); and an
  `atlantis::renderer::Renderer` (the same stateless,
  default-constructed, zero-owned-resource type every existing windowed
  composition uses).
- **No global mutable engine-state singleton anywhere in Runtime.** The
  Runtime Host object is the sole owner of every resource it constructs;
  nothing is process-global or static beyond Atlantis Core's own
  already-stated logging exception.
- **Composition, not inheritance, DI, or a service registry.** The Runtime
  Host object's constructor/initialization function takes no abstract
  service interface and performs no runtime service lookup — it calls
  `platform::initialize()`, `vulkan_backend::createDevice()`, Asset
  System's and Shader System's loaders, and
  `Device::createBuffer()`/`createTexture()`/`createCommandList()`
  directly, by name, exactly as every existing composition root does.

**Initialization order — fixed, not left to the Plan**

1. `platform::initialize()`. On `Err`, log and exit with the
   initialization-failure exit-code category — no further step runs.
2. Load the `minimal_mesh` vertex and fragment SPIR-V + reflection JSON
   (via `loadReflectionMetadata()` and plain file reads, exactly as
   `minimal_renderer_demo` does), from a fixed, CMake-injected, **absolute
   build-tree path** (see Build Integration — **not** a
   working-directory-relative path, unlike `minimal_renderer_demo`'s
   documented "must be launched from its own build output directory"
   limitation, which this spec's "a real Runtime executable, not another
   demo" goal does not want to inherit). Resolve the `VertexInputLayout`
   via `shader_system::rhi_integration::toVertexInputLayout()`. On any
   failure, log and exit with the initialization-failure category;
   `platform::shutdown()` runs first.
3. `vulkan_backend::createDevice()`. On `Err`, log, run
   `platform::shutdown()`, exit with the initialization-failure category.
4. Load the `minimal_cube` Asset System runtime artifact and metadata
   (`asset_system::loadStaticMeshAsset()`) from a fixed, CMake-injected,
   absolute build-tree path. On `Err`, log, tear down the `Device`, run
   `platform::shutdown()`, exit with the initialization-failure category.
5. `renderer::createMesh()` from the loaded `StaticMeshAssetData`'s
   `vertexBytes()`/`indices()`, exactly as
   `setUpMinimalCubeFixtureFromAsset()` does. On `Err`, same
   teardown-and-exit path as step 4.
6. `Device::createBuffer()` for the camera uniform buffer. On `Err`, same
   teardown-and-exit path (now also releasing the `Mesh`).
7. `renderer::createMaterial()` — see Bootstrap Sequencing Detail for how
   this is resolved without a "guess the format" step (step 7 is
   **removed** from the fixed initialization order; `Material`
   construction happens for the first time inside the frame loop).
8. Enter the frame loop. The first loop iteration's event processing
   delivers `SurfaceCreated`, at which point
   `vulkan_backend::createPresentation()` runs and `Presentation`'s real
   `metadata().format` becomes known for the first time.

**Why steps 3–6's own failure teardown never calls `Device::waitIdle()`.**
None of steps 3–6 ever reaches `Device::createCommandList()`/`submit()` —
the first submission of any kind happens only inside the frame loop.
`waitIdle()`'s contract ("blocks until every submission this Device has
made has finished executing") therefore has nothing to wait for; omitting
it here matches `examples/minimal_renderer_demo/main.cpp`'s own identical
early-failure teardown (its `createMesh()`-failure path calls
`device.reset()` directly, with no `waitIdle()` call). This is
deliberately narrower than the general Shutdown sequence, which calls
`waitIdle()` unconditionally because it is reachable from the frame loop,
where a submission may genuinely be outstanding.

**Bootstrap Sequencing Detail — resolving Material's initial format.**
`Material`/`Pipeline` creation needs a concrete color format, but no
`Presentation` — and therefore no real swapchain format — exists until the
first `SurfaceCreated` event is observed inside the frame loop. This spec
resolves this the same way Spec 0007's format-change contract already
generalizes: **Material is not constructed at step 7 at all — it is
deferred to the frame loop's own existing format-change check** (`!lastSeenFormat.has_value()
|| currentFormat != *lastSeenFormat` — true, and therefore triggering
first-time creation, on the very first frame a `RenderTarget` is
successfully acquired). This spec's implementation must not invent a
placeholder/guessed format for an earlier, artificial "construct Material
at startup" step; doing so would risk immediately triggering the
format-change rebuild path on frame one for no reason.

**Per-frame order — fixed, not left to the Plan**

Every iteration while `!platform::shouldQuit()`:

1. `platform::processEvents()`, handling each event exactly as
   `minimal_renderer_demo` does: `SurfaceCreated` → construct
   `Presentation` (an unexpected second `SurfaceCreated` while one already
   exists is a logged, unrecoverable-category failure); `WindowResize` →
   `Presentation::notifyResized()` (a no-op if `Presentation` does not
   exist yet); `WindowCloseRequested` → set a close-requested flag for
   this iteration; `SurfaceDestroyed` → logged (an unexpected occurrence
   while `Presentation` still exists is an unrecoverable-category
   failure); `Quit`, `FocusGained`, `FocusLost`, `ApplicationPause`,
   `ApplicationResume` → logged only, no state change.
2. If a `Presentation` exists and this iteration is not already closing:
   `acquireNextTarget()`. See the outcome table below.
3. On a successfully acquired `RenderTarget`: the format-change check
   (rebuild `Material` if `Presentation::metadata().format` changed,
   including the very first frame); the extent-change check (recreate the
   depth `Texture` if the acquired target's extent changed, including the
   first frame); write this frame's camera view/projection into the camera
   uniform `Buffer`'s mapped memory; build the one fixed `DrawItem`;
   `Device::createCommandList()`; `Renderer::drawFrame(...,
   atlantis::rhi::ResourceState::PresentSource)`; `Device::submit()`;
   `Presentation::present()`.
4. If this iteration's close-requested flag is set, or any step above
   returned an unrecoverable-category `Err`: proceed to Shutdown instead
   of continuing the loop.

**Presentation and error-state handling — fixed exhaustively, not left to
the Plan.** Every outcome `acquireNextTarget()`, `present()`, `submit()`,
and `waitIdle()` can actually return (per their real, current signatures)
is classified into exactly one of three categories:

| Outcome | Category | Runtime's response |
|---|---|---|
| `acquireNextTarget()` → `Ok(nullptr)` (zero extent, or an internally-deferred out-of-date swapchain) | Recoverable, silent | Skip this frame's render entirely; no Vulkan call is made; loop continues. Identical to every existing windowed demo's minimize handling. |
| Out-of-date/suboptimal swapchain | Recoverable, silent | Already handled internally by `recreateIfNeeded()`/`acquireNextTarget()`'s existing contract (ADR-0016/ADR-0019) — Runtime performs no explicit handling. |
| `Presentation::metadata().format` differs from last observed | Recoverable, Runtime-visible | Rebuild `Material` (Spec 0007's existing caller-owned contract); on `createMaterial()` failure, keep the existing `Material` and retry next frame — matching `minimal_renderer_demo`'s already-verified retry behavior, not a new bounded-retry policy. |
| Acquired target's extent differs from last observed | Recoverable, Runtime-visible | Recreate the depth `Texture`; on `createTexture()` failure, keep the existing `Texture` and retry next frame — same retry pattern. |
| `WindowCloseRequested` observed, or `Quit` observed with `shouldQuit()` true | Normal exit | Proceed to Shutdown; exit code Success. |
| `acquireNextTarget()` → `Err(PresentationError::...)` (any variant — including `SwapchainCreationFailed`, surfaced from its internal `recreateIfNeeded()` call, per `PresentationError`'s full four-value enum) | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `present()` → `Err(PresentationError::...)` (any variant) | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `Device::submit()` → `Err(SubmitError::QueueSubmitFailed \| DeviceLost)` | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `Device::createCommandList()` → `Err` | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `Device::waitIdle()` → `Err` (during Shutdown) | Unrecoverable, already shutting down | Log only — Shutdown's teardown continues regardless; no further recovery available. |
| A second `SurfaceCreated` while `Presentation` already exists, or `SurfaceDestroyed` while it does | Unrecoverable (programmer/environment invariant violated) | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |

**Mid-frame close.** Because `processEvents()` always runs, and is always
fully drained, before that same iteration's `acquireNextTarget()` call,
there is no code path in which a `RenderTarget` is acquired and then
abandoned mid-frame because a close request arrived in between. The one
genuine "work was in flight when shutdown began" case
(`submit()`-then-`present()`-fails, or the process needing to exit before
`present()` runs) is handled the same way `minimal_renderer_demo` handles
it: `Device::waitIdle()` unconditionally, before any further resource
destruction, drains it (ADR-0019). No new concurrency-safe abort mechanism
is introduced.

**Shutdown / reverse-order destruction — fixed, not left to the Plan**

1. `Device::waitIdle()` (if `Device` exists) — drains any outstanding
   submission, including a `submit()`-then-exit sequence.
2. Destroy, in this exact order: `Material` (and its `Pipeline`); depth
   `Texture`; camera `Buffer`; `Mesh`; `Presentation` (if it exists);
   `Device`.
3. `platform::shutdown()`.
4. Drain any final `SurfaceDestroyed`/`Quit` events via one last
   `platform::processEvents()` call (logged only), matching
   `minimal_renderer_demo`'s existing post-loop drain.
5. Return the exit code fixed by Error Handling below.

This ordering is a direct, unmodified continuation of the
lifetime-precondition rule every prior RHI-consuming spec establishes
(ADR-0003, ADR-0019: every `Buffer`/`Texture`/`Pipeline`/`Mesh`/`Material`
a `Device` backed is destroyed before that `Device`) — this spec fixes the
concrete order for Runtime's own resource set, it does not invent a new
ownership rule.

**Bootstrap scene**

- Exactly one `DrawItem`: the `Mesh` loaded from the `minimal_cube` Asset
  System artifact (already `Approved`/implemented, Plan 0012), the current
  `Material` (built from the `minimal_mesh` Slang shader pair, already
  `Approved`/implemented, Spec 0008), and a fixed object-to-world
  transform (the identity matrix — a Plan may choose to reuse
  `minimal_renderer_demo`'s orbiting-camera behavior instead of a static
  one, since either is already-proven prior art and neither is an
  architectural decision).
- No second mesh, no second material, no scene file, and no World/ECS
  representation.
- This is the same geometry and shader Spec 0011/Plan 0012's `minimal_cube`
  golden was captured from — chosen so a human verifier comparing
  Runtime's visible window against that already-published golden has a
  direct reference, and so the *offscreen* path this spec's GPU-required
  tests actually exercise is the same content the existing headless
  image-regression suite already covers pixel-for-pixel. This spec's
  windowed path is **not** automatically, pixel-comparably verified
  against that golden — see Testing & Verification Plan.

**Build integration**

- Shader and asset artifact paths are supplied to `atlantis_runtime` via
  `target_compile_definitions()` as absolute, configuration-independent,
  build-tree paths — mirroring `tests/image_regression`'s
  `ATLANTIS_ASSET_ARTIFACT_DIR` pattern (exact macro names a Plan-stage
  detail) — **not** a working-directory-relative path. This reuses the
  exact `atlantis_add_static_mesh_asset()`-vended
  `ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH}` and
  `atlantis_add_slang_shader_pair()`-vended
  `ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR` CMake variables both already
  export today.
- `atlantis_runtime` depends (CMake `add_dependencies()`) on the
  `minimal_cube` asset-cook target and the `minimal_mesh`
  shader-compile targets, exactly as `tests/image_regression`'s own
  CMakeLists.txt already does.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" — no frame-pacing target; Runtime's frame loop runs as
  fast as its own `acquireNextTarget()`/`present()` calls allow, exactly
  like every existing windowed demo.
- **Memory:** no new GPU memory allocation strategy — Runtime allocates
  nothing beyond what
  `Device::createBuffer()`/`createTexture()`/`renderer::createMesh()`/
  `createMaterial()`'s existing, unchanged allocation policies provide
  (ADR-0023, unmodified).
- **Portability (within the Vulkan-only Phase 1 constraint):** implemented
  and verified on Windows only. Runtime's internal library/executable
  split (ADR-0047) is designed not to preclude a future Android entry
  point reusing `Atlantis::RuntimeHost`'s composition logic — a stated
  principle, not something this spec builds, tests, or verifies.
- **Threading:** single-threaded (ADR-0004) — `platform::initialize()`/
  `processEvents()`, `Device`/`Presentation` construction and every
  per-frame RHI call, all on the one thread that owns the Windows Platform
  message pump. No thread, job/task system, or lock-free structure.
- **Ownership:** RAII throughout — every resource Runtime owns is released
  deterministically via the fixed reverse-order destruction sequence; no
  manual, caller-remembered cleanup step.
- **Error handling:** recoverable runtime errors use `atlantis::Result<T,
  E>` throughout — Runtime introduces no exception anywhere in its own
  frame-loop-adjacent code. Whether the thin `atlantis_runtime`
  executable's own OS-entry-point boilerplate (argument parsing, if any)
  may use exceptions is a Plan-stage detail with no bearing on any
  Result-based API this spec fixes.

## Proposed Design

### Runtime authority and the Client question

ADR-0033 commits Atlantis to Runtime eventually being the sole
authoritative owner of engine world state, accessed by every external
module (including a future Editor) as a symmetric Client. This spec's
Runtime process has exactly one implicit "Client" today — the OS/user,
through the window — and zero second process, Editor, or automation
consumer to design a real access boundary against. Consistent with
ADR-0033's own Alternatives Considered ("deciding the mechanism now...
would be exactly the kind of premature, uncontrolled architectural
decision AGENTS.md's Golden Rule prohibits"), this spec does not introduce
any query/command/event type, IPC mechanism, or process boundary. It
satisfies ADR-0033's principle only trivially: nothing outside Runtime
observes or mutates its bootstrap state at all in this spec's scope, so
there is, yet, no access pattern to get wrong.

### Module boundary diagram

```
atlantis_runtime (thin Windows executable; WinMain/main only)
  -> constructs and drives Atlantis::RuntimeHost's composition object

Atlantis::RuntimeHost (private library; testable composition logic)
  -> Atlantis::Platform          (window, events, NativeWindowHandle)
  -> Atlantis::VulkanBackend     (createDevice / createPresentation)
  -> Atlantis::RHI               (Device / Presentation / Buffer / Texture
                                   / CommandList -- all backend-agnostic
                                   types the calls above already return)
  -> Atlantis::Renderer          (createMesh / createMaterial / drawFrame
                                   -- owns RenderGraph construction/
                                   compilation/execution internally)
  -> Atlantis::ShaderSystem +
     Atlantis::ShaderSystemRhiIntegration
                                  (loadReflectionMetadata /
                                   toVertexInputLayout, for the
                                   minimal_mesh shader pair)
  -> Atlantis::AssetSystem       (loadStaticMeshAsset, for the
                                   minimal_cube runtime artifact)
  -> Atlantis::Core              (Result<T,E>, logging, assertions)

No dependency on Atlantis::RenderGraph directly. No Vk* type anywhere in
src/runtime/.
```

## Architectural Impact

Architecture recorded in two new ADRs, both `Accepted` alongside this
spec's Human Review Approval:

1. **Runtime composition, object ownership, and frame lifecycle** — the
   complete object model, initialization order, per-frame order, resize/
   presentation-error taxonomy, and reverse-order destruction fixed under
   Requirements; the bootstrap-scene selection principle; and the scope to
   which ADR-0033's Runtime-authority principle is exercised (trivially —
   no Client API).
   [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md).
2. **Runtime Host executable/library structure and test boundary** — the
   `atlantis_runtime_host` library / thin `atlantis_runtime` executable
   split, why it exists (testability only, mirroring
   `atlantis_shader_compiler_lib`), why it is explicitly not a new public
   dependency surface, Runtime's direct dependency on
   `Atlantis::VulkanBackend` (matching every existing composition root's
   precedent), and the GPU-independent-testable lifecycle/state-machine
   boundary that does not require a general DI/service-locator framework.
   [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified.

**No new public API.** Confirmed by direct inspection:
`Atlantis::Platform`, `Atlantis::RHI`, `Atlantis::VulkanBackend`,
`Atlantis::Renderer`, `Atlantis::ShaderSystem`/`ShaderSystemRhiIntegration`,
and `Atlantis::AssetSystem` are consumed exactly as they exist today, with
zero modification to any public header, type, or function signature.

**`docs/architecture/module_boundaries.md` correction, deferred.** That
document's `PROPOSED` Runtime section lists RenderGraph as a direct
Runtime dependency; this spec's code-verified dependency list omits it.
Per this repository's established pattern (Spec 0012's Tools-description
narrowing), reconciling that document's prose with this spec's `Approved`
conclusion is deferred to a future Plan/docs-sync — not performed by this
spec, and not a blocker, since `module_boundaries.md` is not `Accepted`.

**ADR-0032 compliance.** `Atlantis Runtime` is already named among
AGENTS.md's ten top-level modules — this spec does not add a module, it
makes the ninth of those ten a real, implemented one for the first time.

**ADR-0033 / ADR-0034 / ADR-0035 compliance.** ADR-0033: this spec
exercises the principle only trivially (no Client, no access-category
API). ADR-0034: not meaningfully implicated — this spec introduces no
externally-consumed schema, identity, or protocol surface;
`Atlantis::RuntimeHost` is explicitly not a public dependency any other
module may consume. ADR-0035: not implicated — this spec introduces no new
authoring-facing or runtime-execution data representation, it consumes
Asset System's and Shader System's already-ADR-0035-compliant output as
produced.

**Registry update (specs/README.md).** "Runtime Host and Composition
Root," formerly Candidate Order 2, is promoted to Section A as Spec 0013.
Its "Depends On" column is corrected to add Spec 0008 and Spec 0012 —
both `Approved`/implemented and load-bearing for the bootstrap scene; Spec
0005's dependency is retained (Renderer's dependency on RenderGraph is
what this spec transitively relies on). Android Platform and Vulkan
Presentation (Candidate Order 1) is explicitly **not** reordered,
reprioritized, or reinterpreted. World/ECS Foundation (renumbered
Candidate Order 2 by this same registry update, a mechanical index
correction) continues to depend on this spec and is not drafted here.

## Decisions Requiring Human Review

**All ten items were accepted as recommended by the 2026-08-20 Human
Review Approval** — retained here as the permanent record of each
recommendation and its reasoning. None was decided unilaterally.

1. **Pure executable, or an internal Runtime Host library plus a thin
   Windows executable?** The library-plus-thin-executable split
   (`atlantis_runtime_host` / `atlantis_runtime`), for testability only —
   **explicitly not** a new public service layer; no other top-level
   module may depend on `Atlantis::RuntimeHost`. Mirrors
   `atlantis_shader_compiler_lib` exactly.
2. **Runtime's dependency on a concrete Vulkan Backend, and where backend
   selection lives.** `Atlantis::RuntimeHost` depends on
   `Atlantis::VulkanBackend` directly and calls
   `vulkan_backend::createDevice()`/`createPresentation()` exactly as
   every existing composition root does — the same pattern AGENTS.md's
   Vulkan-header-visibility rule already permits for a composition root.
   No backend-selection abstraction, factory registry, or capability-tier
   system — Phase 1 has exactly one backend.
3. **Complete object ownership, initialization order, per-frame order, and
   reverse-order destruction.** Fixed in full under Requirements. Nothing
   left open for the Plan to invent.
4. **A testable lifecycle/state-machine boundary, without a general
   DI/service-locator framework.** Yes — a small, pure, GPU-independent
   `RuntimeLifecycleState` transition type inside `Atlantis::RuntimeHost`
   (states at minimum: `Uninitialized`, `Initializing`, `Running`,
   `ShuttingDown`, `ShutDown`, `Failed`; legal-transition checking via
   `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`) is unit-testable with no GPU, no
   Platform window, no Device. The real orchestration function is **not**
   made generically mockable — it is verified by GPU-required and manual
   tests, matching how every prior spec's composition root has always been
   verified. No DI container, service-locator, or plugin registry.
5. **How the bootstrap scene selects its mesh, shader, camera, and
   material.** The already-existing, already-`Approved`, already-cooked
   `minimal_cube` asset and already-compiled `minimal_mesh` shader, a
   fixed or orbiting camera (either is already-proven prior art), one
   hardcoded `DrawItem`. No scene file format, no World/ECS preview, no
   second asset.
6. **Resize, zero extent, out-of-date/suboptimal, surface loss, device
   loss, and mid-frame close.** Fixed exhaustively under "Presentation and
   error-state handling," using only outcomes `Presentation`/`Device`'s
   real, current signatures can produce (`PresentationError`'s full
   four-value enum — `SurfaceLost`, `SwapchainCreationFailed`,
   `DeviceLost`, `Unknown` — and `SubmitError::{QueueSubmitFailed,
   DeviceLost}`, confirmed live-mapped from real `VkResult`s in
   `src/vulkan_backend/src/vulkan_result.cpp`). No new RHI capability is
   needed or proposed.
7. **Windowed-only, no headless Runtime mode.** Confirmed — see Non-Goals.
   Headless already has its own complete closed loop (Spec 0010/0011); the
   library/executable split does not foreclose a future spec adding one by
   substituting an `OffscreenTarget` for `Presentation`.
8. **Runtime's configuration boundary.** Everything this spec's bootstrap
   needs is fixed at build/composition time (window title, initial size,
   the one bootstrap asset/shader path set via CMake compile definitions,
   the existing `enableValidationLayers` policy) — **zero** required
   command-line or config-file surface. A trivial, optional
   window-title/size command-line override may be added at Plan stage
   without being architecturally significant; a real configuration *file*
   format, hot-reloadable settings, or general framework is fully out of
   scope.
9. **Whether existing demos are retired, refactored, or kept.** Kept,
   entirely unchanged — this spec adds a new, separate, real product entry
   point alongside them, not a replacement.
10. **Whether Runtime authority needs any Client API in this round.** No —
    no IPC, no Editor protocol, no remote client, no general
    Query/Command/Event/Snapshot surface; ADR-0033's principle is
    acknowledged and trivially satisfied.

## Alternatives Considered

- **A pure executable with no internal library boundary at all.**
  Rejected: it would leave the entire lifecycle/state-machine untestable
  without a real GPU and a real window, unlike every other substantial
  piece of logic this codebase already unit-tests, and would break the
  `atlantis_shader_compiler_lib` precedent.
- **Route Vulkan Backend selection through a new RHI-level
  factory/registry abstraction.** Rejected: Phase 1 has exactly one
  backend; inventing a selection mechanism now would be speculative
  abstraction with no second backend to validate it against.
- **Design a minimal Editor/Client query API now.** Rejected: no real
  second Client exists to design against yet, and ADR-0033's Alternatives
  Considered already rejected deciding this mechanism ahead of a concrete
  consumer.
- **Add a headless Runtime mode in this same spec.** Rejected for this
  round: no product need exists yet, and Spec 0010/0011's headless closed
  loop already serves its own consumers.
- **Invent a general application configuration/settings system.** Rejected:
  no second configuration ever needs to exist yet.
- **Retire or fold the existing demos into Runtime.** Rejected: each
  remains its own spec's disclosed verification composition with its own
  recorded verification history; retiring any of them is unrelated cleanup
  `AGENTS.md`'s "do not introduce unrelated refactoring" rule advises
  against doing opportunistically.

## Testing & Verification Plan

- **Unit tests (GPU-independent), `tests/runtime/`, linking
  `Atlantis::RuntimeHost` and `Atlantis::Core` only — no `Device`, no GPU,
  no real window required:**
  - `RuntimeLifecycleState`'s own transition table: every legal
    transition succeeds; every illegal transition (`Running` →
    `Initializing`, a frame request while `ShuttingDown`, a second
    `initialize()` while already `Running`) is rejected as a programmer
    error (`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`).
  - Idempotent shutdown: invoking the shutdown path from each reachable
    state (`Initializing` having partially failed, `Running`, already
    `ShuttingDown`) reaches `ShutDown` exactly once, with no double-free
    and no repeated `platform::shutdown()` call — exercised against the
    pure state type.
  - The presentation/error-outcome classification table as a pure mapping
    function: each of `PresentationError`'s and `SubmitError`'s real
    enumerators (confirmed exhaustive against `src/rhi/include/atlantis/rhi/types.h`)
    maps to exactly the category (recoverable-silent,
    recoverable-Runtime-visible, unrecoverable) this spec's table fixes,
    and every unrecoverable category maps to the same exit-code bucket.
  - Exit-code-category mapping: `Success`/`InitializationFailed`/
    `UnrecoverableRuntimeError` (or equivalently named, Plan-stage detail)
    each map to a distinct, fixed process exit code.
  - **Honest scope limit, stated explicitly:** real resource
    construction/teardown ordering (the actual sequence of destruction
    calls against real objects) is **not** exercised by these
    GPU-independent tests — it inherently requires a real `Device`, and is
    instead verified by the GPU-required tests and Vulkan Validation
    Layers below, matching how every prior spec in this line has verified
    its composition root's real teardown correctness.
- **GPU-required tests (Windows/Vulkan, `gpu`-labeled):**
  - **Correction to an earlier drafting error:** this spec's bootstrap
    composition does **not** automatically compare its windowed output
    against Spec 0011's `minimal_cube` golden via `compareBuffers()`, and
    no such test is added. Verified directly against
    `src/rhi/include/atlantis/rhi/render_target.h` and
    `src/vulkan_backend/src/vulkan_presentation.cpp`: a swapchain-backed
    `RenderTarget` is created with `imageUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`
    only — **no** `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`.
    `CommandList::copyRenderTargetToBuffer()` (the only existing
    GPU-to-CPU readback path, ADR-0040) issues `vkCmdCopyImageToBuffer`,
    which requires `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` on its source image;
    calling it against a windowed `RenderTarget` would violate that
    requirement and fail Vulkan Validation Layers, even though the
    type-level `dynamic_cast<VulkanRenderTargetAccess*>` check inside it
    does not reject a windowed target. There is therefore **no existing,
    unmodified-public-API path to read back and pixel-compare a real
    windowed frame**, and this spec does not add one — doing so would mean
    changing the Vulkan Backend's own internal swapchain `imageUsage`
    flags (itself contingent on `VkSurfaceCapabilitiesKHR::supportedUsageFlags`
    actually offering `TRANSFER_SRC_BIT`, not guaranteed on every present
    mode/compositor), which is exactly the kind of change this spec's
    Non-Goals require raising as an explicit architectural question —
    not raised here, since no real need has forced it.
  - **What is verified instead, split across three complementary layers,
    none claiming what the others do not:**
    1. A **Runtime GPU smoke test** constructs the full bootstrap
       composition, creates a real window, and confirms
       `acquireNextTarget()` → the format/extent-change paths →
       `Renderer::drawFrame()` → `submit()` → `present()` all succeed at
       least once with Vulkan Validation Layers reporting zero
       warnings/errors — a mechanical correctness check, not a
       pixel-content assertion.
    2. The **existing, unmodified headless image-regression suite**
       (`tests/image_regression/`, Spec 0011/0012, offscreen-only) remains
       the actual pixel-level rendering-output regression gate for this
       exact geometry/shader/camera combination — untouched, still
       passing.
    3. **Manual verification** (below) is the evidence that Runtime's own
       *visible window* shows the correct mesh — a human comparing what
       the window displays against the same already-published
       `minimal_cube` golden PNG, by eye, not an automated diff.
  - Device-loss and surface-loss code paths are verified by code
    inspection against `src/vulkan_backend/src/vulkan_result.cpp`'s real,
    live `VK_ERROR_DEVICE_LOST`-to-`PresentationError::DeviceLost`/
    `SubmitError::DeviceLost` mapping — genuinely triggering a real device
    loss is not assumed available, matching Spec 0007's own precedent for
    its hard-to-trigger format-change case.
- **Manual verification (Windows, real window, real GPU):**
  - A visible window shows the `minimal_cube` mesh, correctly shaded — a
    human verifier compares it by eye against the same already-published
    `minimal_cube` golden PNG, the only comparison this spec's
    verification makes against that golden.
  - Interactive resize continues to show the mesh correctly, with the
    depth `Texture` recreated at the new extent and `Material`/`Pipeline`
    **not** recreated for an extent-only change (dynamic viewport/scissor,
    unchanged from Spec 0007).
  - Minimizing produces no crash, no busy-spin, and no Vulkan call while
    minimized; restoring resumes correct rendering.
  - Closing the window (title-bar close, and — if feasible — a deliberate
    mid-sequence close immediately after a resize/minimize) exits cleanly
    with exit code Success, no Validation Layer warning or error at any
    point including shutdown, and no leaked
    `CommandList`/`Buffer`/`Texture`/`Pipeline`/`Mesh`/`Material`.
  - A deliberately induced initialization failure (a temporarily
    corrupted/missing bootstrap asset/shader path) is confirmed to produce
    the initialization-failure exit-code category with a clear, logged
    reason — not a crash, not a silent hang.
- **Regression, unchanged:** every existing GPU-independent test suite,
  every existing `gpu`-labeled test suite, every existing
  headless/image-regression test, and every existing Asset System/Shader
  System test suite continues to pass, on both Debug and Release — this
  spec adds a new module and a new test directory, and touches no existing
  test, example, CMake target, asset, shader, or golden file.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of this spec's implementation.
- **No global mutable engine state; reverse-order resource release:**
  verified by inspection (no `static`/global owning pointer anywhere in
  `src/runtime/`) and by the manual verification's clean-shutdown/no-leak
  observation.
- **No golden added or modified**, per Non-Goals and ADR-0042 — this
  spec's implementation never writes to `tests/image_regression/goldens/`,
  and no test this spec adds performs an automated pixel comparison of any
  kind. The existing `minimal_cube` golden remains exactly as Spec
  0011/Plan 0012 left it, exercised only by the existing, unmodified
  headless suite.

## Risks & Open Questions

- **Whether a deferred `Material` construction (Bootstrap Sequencing
  Detail) is genuinely equivalent to the format-change path Spec 0007
  already verified, or introduces a subtly different first-frame timing.**
  This spec's analysis concludes it is the same code path exercised for
  the same reason (`lastSeenFormat` starting unset), but this has not been
  run against real hardware yet — flagged for the Plan/Implementation
  stage to confirm empirically.
- **Exact `RuntimeLifecycleState` enumerator set and transition table** is
  left to the Plan — this spec fixes the state-machine's existence,
  GPU-independent testability, and the states named as "at minimum," not
  its exact C++ shape.
- **Exact exit-code integer values** are left to the Plan — this spec
  fixes the three required, distinct categories and which outcome maps to
  which, not their literal numeric values.
- **Whether a fixed or orbiting camera** is used for the bootstrap scene
  is left to the Plan — both are already-proven prior art and neither is
  an architectural decision.
- **Whether `docs/architecture/module_boundaries.md`'s Runtime section
  should be corrected in the same PR that implements this spec, or as a
  separate docs-sync PR** — this spec identifies the correction but does
  not perform it (matching Spec 0009/0012's judgment on similar
  cross-cutting docs corrections).
- **Whether Runtime's eventual real Client-boundary exercise (a future
  Tool/Editor Connection Protocol spec) will find this spec's composition
  shape genuinely reusable, or will need to restructure
  `Atlantis::RuntimeHost`'s internals** — explicitly not answerable now,
  named honestly as a real, deferred risk (matching ADR-0033's own
  Negative/Trade-offs).

## Out of Scope / Future Work

Android Platform and Vulkan Presentation (Candidate Order 1, unimplemented,
unaffected by this spec); World/ECS Foundation (Candidate Order 2, depends
on this spec once `Approved`/implemented); Serialization and Stable
Identity; a Tool/Editor Connection Protocol (the first real exercise of
[ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s Client
model); a Gameplay SDK; a headless Runtime mode; hot-reload of any kind;
async asset streaming; a VFS; glTF/Assimp import; a derived-data cache —
all remain later, separately-specced work, per
[docs/project-blueprint.md](../docs/project-blueprint.md) and this
document's Non-Goals, and none is advanced, designed, or unblocked by this
spec beyond providing the real Runtime executable each of them will
eventually depend on.
