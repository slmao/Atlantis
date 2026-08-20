# Spec: Runtime Host Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved by human review — see Human Review Approval below.
- **Created:** 2026-08-20
- **Human Review Approval (2026-08-20):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-20, following the two independent
  review rounds recorded below (Round 2 found and fixed one substantive
  and three mechanical drafting errors before this approval, all
  corrected on this same branch — no further correction was directed by
  Human Review itself). This approval explicitly accepts:

  1. The `atlantis_runtime_host` private static library plus a thin
     `atlantis_runtime` Windows executable split (ADR-0047) — the private
     library exists solely for entry-point reuse and GPU-independent
     testability, and is not a public dependency surface any other
     top-level module may consume.
  2. Runtime, as composition root, depending on and selecting
     `Atlantis::VulkanBackend` directly — without any `Vk*` type crossing
     into any other module's public surface or into the GPU-independent
     lifecycle/state-machine test boundary.
  3. ADR-0046's complete initialization order, object-ownership model,
     per-frame execution order, `Device::waitIdle()` usage, and
     reverse-order destruction contract, as fixed in this spec's own
     Requirements.
  4. A pure `RuntimeLifecycleState` lifecycle/decision state-machine
     boundary for GPU-independent testing — no general dependency
     injection, service locator, or fake-engine-interface set is
     introduced.
  5. A bootstrap scene combining only the existing cooked `minimal_cube`
     asset, the existing `minimal_mesh` shader, a camera, and one fixed
     `Material` — no World/ECS/Scene abstraction of any kind.
  6. The revised, complete `Presentation`/`acquireNextTarget()`/`submit()`
     error classification — including `PresentationError::SwapchainCreationFailed`
     — and its recoverable/unrecoverable handling, per Round 2's own fix.
  7. Windows windowed mode only for this round — no headless Runtime,
     server mode, or Android implementation.
  8. A minimal configuration boundary — no general application
     configuration system designed or implied.
  9. Every existing example/demo retained, unchanged, as its own spec's
     disclosed verification composition — no removal or large-scale
     refactor.
  10. No Client API, Editor IPC, remote transport, or command/query/event
      protocol in this round — [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
      Runtime-authority principle is acknowledged but not exercised beyond
      local, single-process ownership.
  11. No change to any existing public API of Platform, RHI, Vulkan
      Backend, Renderer, Shader System, or Asset System.
  12. The revised three-layer verification model from Round 2: a Runtime
      GPU smoke test covering real windowed acquire/draw/submit/present
      and Vulkan Validation Layers; the existing, unmodified headless
      image-regression suite as the continuing automated pixel-level
      regression gate; manual, by-eye comparison of Runtime's visible
      window against the existing `minimal_cube` golden PNG; and no
      addition of `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` to the swapchain or
      any other windowed-readback capability for this spec.

  [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md)
  and [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)
  both move to `Accepted` alongside this approval. **This approval
  authorizes drafting Plan 0013 against this spec, per
  [AGENTS.md](../AGENTS.md); it does not itself authorize Implementation**
  — that future Plan must still pass its own Human Review, per the same
  Spec → Plan → Human Review → Implementation → Verification → PR →
  Merge path every prior spec in this line has followed.
- **Independent Review — Round 1 (2026-08-20):** Self-review performed
  during drafting, against `origin/main`'s actual, current public headers
  (not historical summaries) for every module this spec composes:
  `src/platform/include/atlantis/platform/platform.h`,
  `src/rhi/include/atlantis/rhi/{device,presentation,types}.h`,
  `src/vulkan_backend/include/atlantis/vulkan_backend/vulkan_backend.h`,
  `src/renderer/include/atlantis/renderer/{renderer,mesh,material,draw_item}.h`,
  `src/shader_system/include/atlantis/shader_system/reflection_loader.h`,
  `src/shader_system/rhi_integration/include/.../vertex_input_mapping.h`,
  `src/asset_system/include/atlantis/asset_system/{load,static_mesh_asset_data}.h`,
  and the actual composition-root code in `examples/minimal_renderer_demo/main.cpp`
  and `tests/image_regression/fixture/minimal_cube_fixture.cpp`. This
  round confirmed the central architectural question this spec's own
  instructions raised — whether Runtime can be built at all without
  changing any existing module's public API — has a definite, verifiable
  answer (**no new public API is required**; see Architectural Impact's
  "No new public API" finding) and fixed one internal inconsistency found
  during drafting: `docs/architecture/module_boundaries.md`'s own
  (`PROPOSED`, not `Accepted`) Runtime section lists RenderGraph as a
  direct Runtime dependency, but no existing composition root
  (`examples/minimal_renderer_demo`,
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`) includes any
  `atlantis/render_graph/*.h` header for its windowed/mesh-drawing path —
  `Renderer::drawFrame()` already fully encapsulates RenderGraph
  construction, compilation, and execution internally (`src/renderer/include/atlantis/renderer/renderer.h`).
  This spec's own dependency list (Requirements, "Module boundary") does
  not name RenderGraph as a direct Runtime dependency; see Architectural
  Impact's own note on this for the reasoning and the corresponding,
  explicitly-deferred `module_boundaries.md` correction.
- **Independent Review — Round 2 (2026-08-20):** Centralized review of
  this spec's actual PR content against real code — the RHI/Vulkan
  Backend/Renderer/Platform/Shader/Asset System headers and
  implementations, `examples/minimal_renderer_demo`, Spec 0011/0012's own
  test compositions, and [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)
  — checking specifically for claimed capabilities the current interfaces
  cannot actually provide. Found and fixed one substantive drafting error
  and three smaller ones, all corrected directly on this branch; no
  architectural blocker was found.
  - **Substantive:** the Testing & Verification Plan originally claimed
    this spec's own windowed bootstrap composition could be automatically
    pixel-compared against Spec 0011's `minimal_cube` golden via
    `atlantis::image_regression::compareBuffers()`. This is false as a
    matter of real, current interface capability, not merely undesigned:
    swapchain-backed `RenderTarget`s are created with
    `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`
    only (`src/vulkan_backend/src/vulkan_presentation.cpp`), never
    `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`, which `CommandList::copyRenderTargetToBuffer()`'s
    own `vkCmdCopyImageToBuffer` call requires of its source image — the
    only existing GPU-to-CPU readback path (ADR-0040) cannot legally be
    used against a windowed target, and doing so would fail Vulkan
    Validation Layers. Corrected: Testing & Verification Plan's
    "GPU-required tests" now specifies a three-layer verification model
    instead (a Runtime GPU smoke test checking the real acquire/draw/
    submit/present pipeline runs Validation-Layers-clean with no pixel
    assertion; the existing, unmodified headless image-regression suite
    as the actual pixel-level rendering-output regression gate; manual,
    by-eye comparison against the existing golden PNG as the evidence for
    Runtime's own visible window) — see that section's own inline
    correction note for the full evidence trail. Bootstrap Scene's and
    the Acceptance-adjacent "No golden added or modified" bullet's
    wording were corrected to match.
  - **Mechanical:** the presentation/error-state outcome table (and the
    matching Decisions Requiring Human Review item 6) omitted
    `PresentationError::SwapchainCreationFailed` — a real, reachable
    outcome of `acquireNextTarget()`'s own internal `recreateIfNeeded()`
    call, confirmed against `src/rhi/include/atlantis/rhi/types.h`'s
    actual four-value enum — from what it claimed was an exhaustive
    classification. Fixed to cover the enum's full value set.
  - **Mechanical:** added an explicit note explaining why Initialization
    order steps 3–6's own failure teardown never calls
    `Device::waitIdle()` (no submission can have occurred before the
    frame loop begins), distinguishing it from the general Shutdown
    sequence's unconditional call — matching, not contradicting,
    `examples/minimal_renderer_demo`'s own identical early-failure
    teardown behavior.
  - **Mechanical, in `specs/README.md`:** Spec 0012's own registry row
    still read "does not wait for Atlantis Runtime (Candidate Order 2
    below, not yet specced)" — stale after this spec's own prior
    registry update promoted Runtime out of Candidate Order 2 and gave it
    a real spec number. Corrected to reference Spec 0013 directly.
  - **Confirmed clean, no change needed:** the Windows `WM_CLOSE`/
    `WM_DESTROY`/`SurfaceCreated` event-timing model this spec's per-frame
    and shutdown ordering depends on, checked directly against
    `src/platform/src/windows/windows_platform.cpp`'s actual `windowProc`
    and `shutdown()` implementation — `WindowCloseRequested` never itself
    triggers `SurfaceDestroyed`/`shouldQuit()`; only `shutdown()`'s own
    `DestroyWindow()` call does, synchronously, exactly as this spec's Mid-
    frame close and Shutdown sections already assumed. ADR-0046/ADR-0047's
    own division of responsibility (composition/ownership/lifecycle vs.
    executable/library structure and the Vulkan Backend dependency) has no
    overlapping claim between the two documents. The registry's Candidate
    Backlog renumbering and World/ECS's own corrected dependency entry
    were re-checked and found internally consistent, apart from the one
    stale cross-reference fixed above.
- **Related Plan(s):** None yet — a plan may now be drafted against this
  `Approved` spec, per [AGENTS.md](../AGENTS.md); Plan 0013 has not been
  drafted by this document, and may only be drafted once this spec's own
  PR has merged into `main` (see Human Review Approval above).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md)–[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md),
  [ADR-0009](../adr/0009-assertion.md),
  [ADR-0011](../adr/0011-native-window-handle-representation.md)–[ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md),
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md),
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)–[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md),
  [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md)–[ADR-0037](../adr/0037-long-term-device-backend-extensibility-without-phase1-scaffolding.md),
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)–[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md),
  and
  [ADR-0043](../adr/0043-asset-system-module-boundary.md)–[ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (all `Accepted`; none reopened or modified — see Motivation and
  Architectural Impact for which are load-bearing here).
  [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md)
  (Runtime composition, object ownership, and frame lifecycle) and
  [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)
  (Runtime Host executable/library structure and test boundary) — both
  `Accepted` alongside this spec's own Human Review Approval above.

## Summary

This spec introduces `Atlantis Runtime` as a real module for the first
time: a genuine, verifiable Windows executable — `atlantis_runtime` — that
replaces every existing demo's role as the project's de facto composition
root with the actual thing `AGENTS.md`'s own module list has always named
but never built. It composes Atlantis Platform, the Vulkan Backend/RHI,
Atlantis Renderer, Atlantis Shader System, and Atlantis Asset System —
every one of them **exactly as already `Accepted`/implemented, with zero
public API change** — into one fixed, single-threaded startup → frame loop
→ shutdown lifecycle that loads the already-cooked `minimal_cube` asset and
the already-compiled `minimal_mesh` shader, and displays a real window
showing that mesh. It fixes object ownership and init/frame/destruction
ordering, a small internal `atlantis_runtime_host` library boundary for
testability (not a new public dependency surface any other module may use),
and how initialization failure, recoverable presentation states, and
unrecoverable device errors each map to a distinct outcome and exit code.
It does not touch World/ECS, an Editor/Client protocol, headless mode, or
any new rendering capability — see Non-Goals.

## Motivation / Problem Statement

`AGENTS.md`'s own ten-module list has named **Atlantis Runtime** from the
start. Every other module in that list — Core, Platform (Windows), RHI,
Vulkan Backend, RenderGraph, Renderer, Shader System, Asset System (Spec
0012, `Approved`, implemented), and Tools (hosting the real
`atlantis_shader_compiler`/`atlantis_asset_cooker` CLIs) — is now real:
each is `Accepted`/`Approved` and implemented. Runtime alone remains, per
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)'s
own `PROPOSED` (not `Accepted`) description, "largely private... the
composition root, not a library other modules link against" — a
description of an *intended shape*, not an implemented thing.

What stands in for it today is a series of explicitly disclosed,
non-shipping verification compositions: `examples/rhi_vulkan_demo`,
`examples/frame_execution_demo`, `examples/minimal_renderer_demo`, and
`examples/headless_rendering_demo`. Every one of them carries, in its own
source or its own spec, the same disclaimer —
[specs/0003-rhi-vulkan-windowed-foundation.md](0003-rhi-vulkan-windowed-foundation.md)'s
own words: "this spec's own verification uses a minimal, non-shipping
composition... It is not, and must not be mistaken for, Runtime itself."
`examples/minimal_renderer_demo/main.cpp` repeats the same disclaimer
verbatim in its own top-of-file comment. These programs are real, tested,
and valuable — but none of them is the actual product entry point a user
launches to see Atlantis render something, and none of them consumes
Asset-System-sourced content while presenting to a real window (the one
composition that does load an Asset-System-sourced mesh,
`tests/image_regression/fixture/minimal_cube_fixture.cpp`'s
`setUpMinimalCubeFixtureFromAsset()`, renders to an **offscreen** target
for image-regression comparison, never a window).

[specs/README.md](README.md)'s Candidate Spec Backlog names this gap
directly, as "Runtime Host and Composition Root," Candidate Order 2,
depending on Spec 0002 (Platform), Spec 0003 (RHI), Spec 0005 (RenderGraph
Foundation), and Spec 0007 (Minimal Renderer) — all four `Approved` and
implemented; this spec's own analysis (see Requirements) finds Runtime
additionally, necessarily depends on Spec 0008 (Shader System Foundation)
and Spec 0012 (Asset System Foundation), both also `Approved` and
implemented, since a real Runtime cannot draw anything without a compiled
shader and cannot claim to be "the real thing" while still hand-authoring
its mesh data in C++ the way every demo before it has. See Architectural
Impact for the corresponding registry update.

[ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)
(`Accepted`, Spec 0009) already commits Atlantis to a long-term principle
— Runtime is the sole authoritative owner of engine world state, and every
external module accesses it as a Client — but explicitly leaves "whether
Runtime is built as a linked library, a statically-linked executable, or
some other packaging shape" and "Runtime's own internal threading/
concurrency model" as Out of Scope, deferred to Runtime's own future spec.
This is that spec. It exercises ADR-0033's principle only to the minimal
extent a single, real Runtime process with no second Client yet actually
needs (see Non-Goals) — it does not design a Client API, an IPC protocol,
or a Query/Command/Event surface, since none of those has a real second
consumer yet to design against, and inventing one now would be exactly the
premature abstraction `AGENTS.md`'s Golden Rule and ADR-0033's own
Alternatives Considered warn against.

### Why this is buildable without changing any existing public API

This spec's own instructions required checking, directly against real
code, whether Runtime can actually be composed from what already exists,
and to raise an explicit architectural question rather than force a draft
if not. The answer is unambiguous: **every capability this spec's bootstrap
scene needs is already exposed, unchanged, by an existing `Accepted`
public API**, proven end to end by two existing, independently-verified
compositions:

- `examples/minimal_renderer_demo/main.cpp` already proves the complete
  windowed lifecycle this spec needs — `platform::initialize()` →
  `vulkan_backend::createDevice()` → (on the first `SurfaceCreated` event)
  `vulkan_backend::createPresentation()` → a per-frame `acquireNextTarget()`
  → format/extent-change handling → `Renderer::drawFrame()` → `submit()` →
  `present()` loop, with `notifyResized()`, minimize/restore (zero-extent
  skip), `WindowCloseRequested`, and clean shutdown all already
  implemented and manually verified (Spec 0007's own Testing &
  Verification Plan) — using a **hand-authored** mesh and a **checked-in,
  build-tree-loaded** shader pair.
- `tests/image_regression/fixture/minimal_cube_fixture.cpp`'s
  `setUpMinimalCubeFixtureFromAsset()` already proves that
  `atlantis::asset_system::loadStaticMeshAsset()`'s output
  (`StaticMeshAssetData`) feeds `atlantis::renderer::createMesh()`'s
  existing, unmodified signature with no conversion step (Plan 0012 Step
  6, GPU-verified against Spec 0011's own golden with **zero** channel
  difference) — using an **offscreen** target, not a window.

No existing composition combines both halves — an Asset-System-sourced
mesh, presented to a real window. This spec is the first to do that. But
combining them requires **new orchestration code only**, not a new
capability: `Renderer::drawFrame()`'s own `finalColorState` parameter
already distinguishes exactly this case
(`atlantis::rhi::ResourceState::PresentSource` for a windowed caller vs.
`TransferSource` for a headless one — stated directly in
`renderer.h`'s own doc comment), and every other call in the composition
(`loadStaticMeshAsset()`, `createMesh()`, `createDevice()`,
`createPresentation()`, `acquireNextTarget()`, `present()`) is used by this
spec exactly as its existing signature already allows. If this spec's own
implementation later discovers a real gap — a capability neither existing
composition actually proves — Requirements below states explicitly that
this is to be raised as its own architectural question, not patched
around silently; this self-review found none.

## Goals

- Introduce **`Atlantis Runtime`** — already named among
  [AGENTS.md](../AGENTS.md)'s ten top-level modules, but until this spec
  the last of the original nine (pre-Asset-System) to remain unimplemented
  — as a real Windows executable, `atlantis_runtime`, that is the
  project's actual composition root, not another disclosed, non-shipping
  demo.
- Compose Atlantis Platform (Windows), the Vulkan Backend/RHI, Atlantis
  Renderer, Atlantis Shader System, and Atlantis Asset System into one
  fixed startup → frame loop → shutdown lifecycle, with **zero change** to
  any of their existing public APIs (see "Why this is buildable" above).
- Fix, precisely and completely, object ownership, initialization order,
  per-frame call order, and reverse-order destruction — see Proposed
  Design's Object Model and Lifecycle Sequencing.
- Load the already-cooked `minimal_cube` Asset System runtime artifact and
  the already-compiled `minimal_mesh` Slang shader pair, and display a
  real window showing that mesh — a genuine bootstrap scene, not a new
  scene format or a hand-authored fallback.
- Fix the single-threaded startup, frame-loop, resize, minimize/restore,
  close, and normal-exit contract as a complete, testable state machine.
- Fix, exhaustively, how initialization failure, each recoverable
  presentation state, and each unrecoverable device/presentation error is
  handled and which exit-code category it maps to.
- Realize [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
  Runtime-authority principle to the extent this spec's own single-process,
  no-second-Client scope actually requires — without designing an
  Editor/Client transport, protocol, or API surface ahead of a real second
  consumer.
- Preserve every existing public API and dependency direction of Renderer,
  RHI, Platform, Asset System, and Shader System exactly as `Accepted` —
  and if this spec's own implementation work were to find a genuine gap
  forcing a change, raise that explicitly as its own architectural
  question rather than resolve it silently (see "Why this is buildable"
  above; none was found).
- Deliver a Runtime executable other Specs can build directly on —
  World/ECS, Tool/Editor Connection Protocol, and every later Candidate
  Backlog item that names Runtime as a dependency — without this spec
  itself designing any of them.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Android, iOS, or Linux.** This spec builds, tests, and verifies
  Windows only. It does not add Android NDK build configuration, does not
  implement an `android_main`-equivalent entry point, and does not design
  one beyond noting (Architectural Impact) that its internal library
  boundary does not preclude a future Android entry point reusing it.
  Linux is not a target platform for Atlantis at all, per
  [AGENTS.md](../AGENTS.md).
- **World/ECS, a scene graph, scene serialization, or stable entity
  GUIDs.** This spec's bootstrap scene is exactly one hardcoded
  `DrawItem`; no entity, component, or scene-description format of any
  kind is introduced. [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
  future "authoritative world state" remains entirely unimplemented — this
  spec's own bootstrap state (one mesh, one material, one camera) is not
  framed as *world state* in that ADR's sense.
- **An Editor/Runtime IPC protocol, a Tool/Editor connection protocol, a
  Gameplay SDK, or any general Query/Command/Event/Snapshot API surface.**
  See "Runtime authority and the Client question" under Proposed Design
  and Decisions Requiring Human Review, item 10. No process boundary, no
  wire protocol, no second Client of any kind exists or is designed by
  this spec.
- **A headless Runtime mode or a server mode.** This spec's Runtime is
  windowed-only. Headless rendering already has its own complete,
  independently verified, test-only closed loop (Spec 0010/0011,
  `examples/headless_rendering_demo`, `tests/image_regression/`) serving
  its own real consumers; no product need for a headless *Runtime* entry
  point exists yet to justify designing one now. See Decisions Requiring
  Human Review, item 7.
- **PBR, lighting, shadows, texturing, post-processing, or any new
  rendering capability.** This spec's one material is the same
  fixed-vertex-color `minimal_mesh` material every existing windowed demo
  already uses — Renderer's own existing single-material, single-mesh
  scope (Spec 0007) is unchanged and unreopened.
- **Hot-reload of any kind** (shader, asset, or pipeline), **asynchronous
  asset streaming, a Job System, or multi-threaded frame orchestration.**
  Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged and
  unreopened.
- **A plugin system, a general dependency-injection or service-locator
  framework, or a general application framework.** Runtime's own internal
  testability boundary (Architectural Impact, ADR-0047) is a small, fixed,
  concrete composition — not a registry, not an abstract service
  interface set, and not a mechanism any other module could plug into.
- **Any change to Renderer's, RHI's, Vulkan Backend's, Atlantis Platform's,
  Atlantis Asset System's, or Atlantis Shader System's existing public
  API**, unless this spec's own implementation work finds a genuine,
  disclosed need — see "Why this is buildable" above. None is currently
  identified.
- **A general application configuration system, a settings file format,
  or command-line argument parsing beyond what Decisions Requiring Human
  Review item 8 explicitly allows as optional, minimal, Plan-stage detail.**
- **Frame pacing, a profiler, or a general timing/telemetry system.**
  Platform's existing monotonic-clock scope
  ([specs/0002-platform-foundation.md](0002-platform-foundation.md)) is
  unchanged; this spec's frame loop runs as fast as `processEvents()`/
  `acquireNextTarget()` allow, exactly as every existing windowed demo's
  loop already does.
- **Rewriting, removing, or absorbing any existing example.**
  `examples/foundation_demo`, `examples/platform_demo`,
  `examples/rhi_vulkan_demo`, `examples/frame_execution_demo`,
  `examples/minimal_renderer_demo`, and `examples/headless_rendering_demo`
  all remain, unchanged, as their own specs' own disclosed verification
  compositions. See Decisions Requiring Human Review, item 9.

## Requirements

### Functional

**Module boundary**

- New module `src/runtime/`, realizing the already-named `Atlantis
  Runtime` slot in [AGENTS.md](../AGENTS.md)'s ten-module list. Depends
  on: `Atlantis::Core`,
  `Atlantis::Platform`, `Atlantis::RHI`, `Atlantis::VulkanBackend`,
  `Atlantis::Renderer`, `Atlantis::ShaderSystem`,
  `Atlantis::ShaderSystemRhiIntegration`, and `Atlantis::AssetSystem`.
  **Not** a direct dependency on `Atlantis::RenderGraph` — confirmed by
  direct inspection that no existing windowed or asset-sourced composition
  root includes any `atlantis/render_graph/*.h` header;
  `Renderer::drawFrame()` already fully owns RenderGraph construction,
  compilation, and execution internally. This is a corrected, narrower
  dependency list than
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  own current, `PROPOSED` (not `Accepted`) Runtime section states — see
  Architectural Impact.
- Per
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  own (`PROPOSED`, not yet `Accepted`) Runtime section, Runtime is **the
  only module permitted to depend on Atlantis Platform** — consistent
  with, though not verbatim stated by, [AGENTS.md](../AGENTS.md)'s own
  broader principle that Platform-specific code stays outside every other
  module. Unchanged by this spec, and satisfied: no other module gains a
  Platform dependency here.
- No `Vk*` type, and no `#include <vulkan/...>`, appears anywhere in
  Runtime's own source — Runtime calls only `vulkan_backend::createDevice()`/
  `createPresentation()`'s existing, already-backend-agnostic-typed
  signatures, exactly as every existing composition root already does;
  it does not itself touch the Vulkan Backend's private WSI boundary or
  any `Vk*` symbol.

**Executable/library structure**

- Two CMake targets realize the module (see Architectural Impact,
  ADR-0047, for the full decision and its justification):
  - `atlantis_runtime_host` (static library), alias `Atlantis::RuntimeHost`
    — owns the actual composition logic: object construction/ownership,
    the initialization sequence, the per-frame orchestration, resize/
    lifecycle event handling, and reverse-order teardown. Contains **no**
    OS entry point (`main`/`WinMain`).
  - `atlantis_runtime` (executable) — a thin, per-OS entry point (Windows
    `WinMain`/`main`, exact form a Plan-stage detail) that constructs and
    drives `Atlantis::RuntimeHost`'s composition type and returns its
    exit code. Contains no composition logic of its own beyond argument
    handling explicitly allowed by Decisions Requiring Human Review item 8.
  - **`Atlantis::RuntimeHost` is not a new public dependency surface.** No
    other top-level module may depend on it; it exists solely so this
    spec's GPU-independent lifecycle/state-machine tests (Testing &
    Verification Plan) can exercise real composition logic without
    spawning a process or a window — mirroring Atlantis Shader System's
    already-`Accepted` precedent of factoring
    `atlantis_shader_compiler`'s own process-execution logic into a
    private `atlantis_shader_compiler_lib` for exactly this reason (Spec
    0008, [PR #36](https://github.com/slmao/Atlantis/pull/36)). This is
    an internal testability seam, not an exercise of
    [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
    future Client-boundary concept — no query/command/event type, and no
    process/IPC boundary, is introduced by this split.

**Object model and ownership**

- A single composition type inside `Atlantis::RuntimeHost` (exact name a
  Plan-stage detail — this spec's own prose below calls it "the Runtime
  Host object") owns, directly or via `std::unique_ptr`/`std::optional`
  exactly as every existing composition root already does, in this order:
  a `Platform` initialization handle (Platform itself is free functions,
  not an owned object, per its own existing API — see
  `src/platform/include/atlantis/platform/platform.h`); a
  `std::unique_ptr<atlantis::rhi::Device>`; a
  `std::unique_ptr<atlantis::rhi::Presentation>` (constructed lazily, on
  the first observed `SurfaceCreated` event — exactly as
  `minimal_renderer_demo` already does, since `Presentation` needs a
  `NativeWindowHandle` that does not exist before that event); an
  `atlantis::renderer::Mesh` (constructed once, from the loaded Asset
  System data, at startup — never re-created for the life of the
  process); an `atlantis::renderer::Material` (constructed once at
  startup against the swapchain's initial format, and **rebuilt** — not
  mutated — whenever `Presentation::metadata().format` changes, per Spec
  0007's own already-`Accepted` caller-owned format-change contract); a
  `std::unique_ptr<atlantis::rhi::Buffer>` camera uniform buffer
  (constructed once); a `std::unique_ptr<atlantis::rhi::Texture>` depth
  buffer (constructed once at the initial extent, and **recreated** —
  same pattern — whenever the acquired `RenderTarget`'s extent changes);
  and an `atlantis::renderer::Renderer` (the same stateless,
  default-constructed, zero-owned-resource type every existing windowed
  composition already uses).
- **No global mutable engine-state singleton anywhere in Runtime,** per
  [AGENTS.md](../AGENTS.md)'s existing rule. The Runtime Host object is
  the sole owner of every resource it constructs; nothing is
  process-global or static beyond Atlantis Core's own already-stated
  logging exception.
- **Composition, not inheritance, DI, or a service registry.** The Runtime
  Host object's constructor/initialization function takes no abstract
  service interface and performs no runtime service lookup — it calls
  `platform::initialize()`, `vulkan_backend::createDevice()`, Asset
  System's and Shader System's loaders, and `Device::createBuffer()`/
  `createTexture()`/`createCommandList()` directly, by name, exactly as
  every existing composition root already does. See Decisions Requiring
  Human Review item 4 and ADR-0047.

**Initialization order — fixed, not left to the Plan**

1. `platform::initialize()`. On `Err`, log and exit with the
   initialization-failure exit-code category (see Error Handling below) —
   no further step runs.
2. Load the `minimal_mesh` vertex and fragment SPIR-V + reflection JSON
   (via Atlantis Shader System's existing `loadReflectionMetadata()` and
   plain file reads, exactly as `minimal_renderer_demo` already does),
   from a fixed, CMake-injected, absolute build-tree path (see Build
   Integration — **not** a working-directory-relative path, unlike
   `minimal_renderer_demo`'s own documented "must be launched from its own
   build output directory" limitation, which this spec's own "a real
   Runtime executable, not another demo" goal does not want to inherit).
   Resolve the `VertexInputLayout` via
   `shader_system::rhi_integration::toVertexInputLayout()`. On any
   failure, log and exit with the initialization-failure exit-code
   category; `platform::shutdown()` runs first.
3. `vulkan_backend::createDevice()`. On `Err`, log, run
   `platform::shutdown()`, and exit with the initialization-failure
   exit-code category.
4. Load the `minimal_cube` Asset System runtime artifact and metadata
   (`asset_system::loadStaticMeshAsset()`) from a fixed, CMake-injected,
   absolute build-tree path (see Build Integration). On `Err`, log, tear
   down the `Device` (nothing else has been constructed yet), run
   `platform::shutdown()`, and exit with the initialization-failure
   exit-code category.
5. `renderer::createMesh()` from the loaded `StaticMeshAssetData`'s
   `vertexBytes()`/`indices()`, exactly as
   `setUpMinimalCubeFixtureFromAsset()` already does. On `Err`, same
   teardown-and-exit path as step 4.
6. `Device::createBuffer()` for the camera uniform buffer. On `Err`, same
   teardown-and-exit path (now also releasing the `Mesh`).
7. `renderer::createMaterial()`, against a swapchain format this spec's
   own bootstrap has not observed yet — see Bootstrap Sequencing Detail
   below for exactly how this is resolved without inventing a "guess the
   format" step.
8. Enter the frame loop (below). The first loop iteration's event
   processing is what actually delivers `SurfaceCreated`, at which point
   `vulkan_backend::createPresentation()` runs and `Presentation`'s real
   `metadata().format` becomes known for the first time — see Bootstrap
   Sequencing Detail.

**Why steps 3–6's own failure teardown never calls `Device::waitIdle()`.**
None of steps 3–6 ever reaches `Device::createCommandList()`/`submit()` —
the first submission of any kind happens only inside the frame loop
(Per-frame order, below). `waitIdle()`'s own contract ("blocks until every
submission this Device has made has finished executing") therefore has
nothing to wait for at any of these steps; omitting it here is not a gap,
it matches `examples/minimal_renderer_demo/main.cpp`'s own identical
early-failure teardown (e.g. its `createMesh()`-failure path calls
`device.reset()` directly, with no `waitIdle()` call). This is deliberately
narrower than the general Shutdown sequence below, which calls
`waitIdle()` unconditionally because it is reachable from the frame loop,
where a submission may genuinely be outstanding.

**Bootstrap Sequencing Detail — resolving Material's initial format**

`Material`/`Pipeline` creation needs a concrete color format
(`PipelineCreateParams::colorFormat`), but no `Presentation` — and
therefore no real swapchain format — exists until the first
`SurfaceCreated` event is observed inside the frame loop, which happens
*after* step 7 above in a naive reading. This spec resolves this the same
way Spec 0007's own format-change contract already generalizes: **Material
is not constructed at step 7 at all — it is deferred to the frame loop's
own existing format-change check**, exactly the code path
`minimal_renderer_demo` already runs on every frame (`!lastSeenFormat.has_value()
|| currentFormat != *lastSeenFormat` — true, and therefore triggering
first-time creation, on the very first frame a `RenderTarget` is
successfully acquired). Step 7 above is therefore **removed** from the
fixed initialization order; `Material` construction happens for the first
time inside the frame loop, on the first frame with a real acquired
target, using the exact mechanism Spec 0007 already fixed — not a new
mechanism. This spec's implementation must not invent a placeholder/guessed
format for an earlier, artificial "construct Material at startup" step;
doing so would risk immediately triggering the format-change rebuild path
on frame one for no reason.

**Per-frame order — fixed, not left to the Plan**

Every iteration of the frame loop, while `!platform::shouldQuit()`:

1. `platform::processEvents()`, handling each event exactly as
   `minimal_renderer_demo` already does: `SurfaceCreated` → construct
   `Presentation` (an unexpected second `SurfaceCreated` while one already
   exists is a logged, unrecoverable-category failure — Windows only ever
   emits this once per process, per Spec 0002's own Ownership and
   Lifetime table); `WindowResize` → `Presentation::notifyResized()` (a
   no-op if `Presentation` does not exist yet); `WindowCloseRequested` →
   set a close-requested flag for this iteration; `SurfaceDestroyed` →
   logged (an unexpected occurrence while `Presentation` still exists is
   an unrecoverable-category failure, matching Windows' own
   Ownership-and-Lifetime expectation that Windows does not tear down and
   recreate its native window mid-process); `Quit`, `FocusGained`,
   `FocusLost`, `ApplicationPause`, `ApplicationResume` → logged only, no
   state change (`ApplicationPause`/`Resume` are not emitted on Windows
   per Spec 0002's own Events section, but the handling code accepts them
   uniformly rather than special-casing their absence).
2. If a `Presentation` exists and this iteration is not already closing:
   `acquireNextTarget()`. See "Presentation and error-state handling"
   below for the complete outcome table.
3. On a successfully acquired `RenderTarget`: the format-change check
   (rebuild `Material` if `Presentation::metadata().format` changed,
   including — see Bootstrap Sequencing Detail — the very first frame);
   the extent-change check (recreate the depth `Texture` if the acquired
   target's extent changed, including the first frame); write this
   frame's camera view/projection into the camera uniform `Buffer`'s
   mapped memory; build the one fixed `DrawItem` (the bootstrap `Mesh` +
   the current `Material` + a fixed object-to-world transform — see
   Bootstrap Scene below); `Device::createCommandList()`;
   `Renderer::drawFrame(..., atlantis::rhi::ResourceState::PresentSource)`;
   `Device::submit()`; `Presentation::present()`.
4. If this iteration's close-requested flag is set, or any step above
   returned an unrecoverable-category `Err` (see below): proceed to
   Shutdown, below, instead of continuing the loop.

**Presentation and error-state handling — fixed exhaustively, not left to
the Plan**

Every outcome `acquireNextTarget()`, `present()`, `submit()`, and
`waitIdle()` can actually return (per their real, current signatures —
`src/rhi/include/atlantis/rhi/{presentation,device,types}.h`) is
classified into exactly one of three categories:

| Outcome | Category | Runtime's response |
|---|---|---|
| `acquireNextTarget()` → `Ok(nullptr)` (zero extent, or an out-of-date swapchain internally deferred to next call) | Recoverable, silent | Skip this frame's render entirely; no Vulkan call is made; loop continues. Identical to every existing windowed demo's own minimize handling. |
| Out-of-date/suboptimal swapchain | Recoverable, silent | Already handled internally by `recreateIfNeeded()`/`acquireNextTarget()`'s own existing contract (ADR-0016/ADR-0019) — Runtime performs no explicit handling of this case at all. |
| `Presentation::metadata().format` differs from last observed | Recoverable, Runtime-visible | Rebuild `Material` (Spec 0007's existing caller-owned contract); on `createMaterial()` failure, keep the existing `Material` and retry next frame — matching `minimal_renderer_demo`'s own already-verified retry behavior exactly, not a new bounded-retry policy. |
| Acquired target's extent differs from last observed | Recoverable, Runtime-visible | Recreate the depth `Texture`; on `createTexture()` failure, keep the existing `Texture` and retry next frame — same retry pattern as above. |
| `WindowCloseRequested` observed, or `Quit` observed with `shouldQuit()` true | Normal exit | Proceed to Shutdown; exit code Success (see Error Handling). |
| `acquireNextTarget()` → `Err(PresentationError::...)` (any variant — including `SwapchainCreationFailed`, surfaced from its own internal `recreateIfNeeded()` call, per `PresentationError`'s full four-value enum) | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `present()` → `Err(PresentationError::...)` (any variant) | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `Device::submit()` → `Err(SubmitError::QueueSubmitFailed \| DeviceLost)` | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `Device::createCommandList()` → `Err` | Unrecoverable | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |
| `Device::waitIdle()` → `Err` (during Shutdown) | Unrecoverable, already shutting down | Log only — Shutdown's own teardown continues regardless, matching every existing demo's own unconditional teardown-on-failure path; there is no further recovery action available. |
| A second `SurfaceCreated` while `Presentation` already exists, or `SurfaceDestroyed` while it does | Unrecoverable (programmer/environment invariant violated) | Log; proceed to Shutdown; exit code UnrecoverableRuntimeError. |

**Mid-frame close.** Because `processEvents()` always runs, and is always
fully drained, before that same iteration's `acquireNextTarget()` call
(per the per-frame order above), there is no code path in which a
`RenderTarget` is acquired and then abandoned mid-frame because a close
request arrived in between — a close request observed during event
processing skips that iteration's entire render block (per the outcome
table's "Normal exit" row) before any acquire happens. The one genuine
"work was in flight when shutdown began" case the existing codebase
already documents — a `submit()` that succeeded followed immediately by
`present()` failing, or by the process needing to exit before `present()`
runs at all — is handled the same way `minimal_renderer_demo` already
handles it: `Device::waitIdle()` unconditionally, before any further
resource destruction, drains it (ADR-0019). This spec introduces no new
concurrency-safe abort mechanism; the existing single-threaded, fully
event-drained-before-render sequencing already makes one unnecessary.

**Shutdown / reverse-order destruction — fixed, not left to the Plan**

1. `Device::waitIdle()` (if `Device` exists) — drains any outstanding
   submission, including a `submit()`-then-exit sequence.
2. Destroy, in this exact order: `Material` (and its `Pipeline`); depth
   `Texture`; camera `Buffer`; `Mesh`; `Presentation` (if it exists);
   `Device`.
3. `platform::shutdown()`.
4. Drain any final `SurfaceDestroyed`/`Quit` events via one last
   `platform::processEvents()` call (logged only), matching
   `minimal_renderer_demo`'s own existing post-loop drain.
5. Return the exit code fixed by Error Handling below.

This ordering is a direct, unmodified continuation of the exact
lifetime-precondition rule every prior RHI-consuming spec already
establishes (ADR-0003, ADR-0019: every `Buffer`/`Texture`/`Pipeline`/`Mesh`/
`Material` a `Device` backed is destroyed before that `Device`) — this
spec fixes the concrete order for Runtime's own specific resource set, it
does not invent a new ownership rule.

**Bootstrap scene**

- Exactly one `DrawItem`: the `Mesh` loaded from the `minimal_cube` Asset
  System artifact (`assets/meshes/minimal_cube.mesh.txt`, already
  `Approved`/implemented, Plan 0012 Step 4b/6), the current `Material`
  (built from the `minimal_mesh` Slang shader pair, already
  `Approved`/implemented, Spec 0008), and a fixed object-to-world
  transform (the identity matrix — no per-frame animation is required by
  this spec; a Plan may choose to reuse `minimal_renderer_demo`'s own
  orbiting-camera behavior instead of a static one, since either is
  already-proven prior art and neither is an architectural decision).
- No second mesh, no second material, no scene file, and no World/ECS
  representation of any kind — see Non-Goals.
- This is the same geometry and shader Spec 0011/Plan 0012's own
  `minimal_cube` golden was captured from — chosen so a human verifier
  comparing Runtime's own visible window against that already-published
  golden image has a direct, meaningful reference, and so the *offscreen*
  path this spec's own GPU-required tests actually exercise (see Testing
  & Verification Plan) is the same content the existing headless
  image-regression suite already covers pixel-for-pixel. This spec's own
  windowed path is **not** automatically, pixel-comparably verified
  against that golden — see Testing & Verification Plan's own correction
  of an earlier drafting error on exactly this point.

**Build integration**

- Shader and asset artifact paths are supplied to `atlantis_runtime` via
  `target_compile_definitions()` as absolute, configuration-independent,
  build-tree paths — mirroring `tests/image_regression`'s own already-
  established `ATLANTIS_ASSET_ARTIFACT_DIR` pattern (exact macro names a
  Plan-stage detail) — **not** a working-directory-relative path like
  `minimal_renderer_demo`'s own documented limitation. This is a genuine,
  small improvement within existing CMake capability, not a new build
  mechanism: it reuses the exact `atlantis_add_static_mesh_asset()`-
  vended `ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH}` and
  `atlantis_add_slang_shader_pair()`-vended
  `ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR` CMake variables both already
  export today.
- `atlantis_runtime` depends (CMake `add_dependencies()`) on the
  `minimal_cube` asset-cook target and the `minimal_mesh` shader-compile
  targets, exactly as `tests/image_regression`'s own CMakeLists.txt
  already does for the same two artifacts.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" — the same bar every prior spec in this line has set. No
  frame-pacing target; Runtime's frame loop runs as fast as its own
  `acquireNextTarget()`/`present()` calls allow, exactly like every
  existing windowed demo.
- **Memory:** no new GPU memory allocation strategy — Runtime allocates
  nothing beyond what `Device::createBuffer()`/`createTexture()`/
  `renderer::createMesh()`/`createMaterial()`'s existing, unchanged
  allocation policies already provide (ADR-0023, unmodified).
- **Portability (within the Vulkan-only Phase 1 constraint):** implemented
  and verified on Windows only. Runtime's internal library/executable
  split (ADR-0047) is designed not to preclude a future Android entry
  point reusing `Atlantis::RuntimeHost`'s composition logic behind its own
  native entry point — a stated principle for a future spec to build
  against, not something this spec builds, tests, or verifies.
- **Threading:** single-threaded, matching
  [ADR-0004](../adr/0004-phase1-threading-baseline.md)'s existing Phase 1
  baseline exactly — `platform::initialize()`/`processEvents()`, `Device`/
  `Presentation` construction and every per-frame RHI call, all happen on
  the one thread that owns the Windows Platform message pump. No thread,
  job/task system, or lock-free structure is introduced.
- **Ownership:** RAII throughout, per [AGENTS.md](../AGENTS.md)'s existing
  rule — every resource Runtime owns is released deterministically via
  the fixed reverse-order destruction sequence above; no manual,
  caller-remembered cleanup step beyond what that sequence already fixes.
- **Error handling:** recoverable runtime errors use `atlantis::Result<T,
  E>` throughout, matching every existing module's convention — Runtime
  introduces no exception anywhere in its own frame-loop-adjacent code.
  Whether the thin `atlantis_runtime` executable's own OS-entry-point
  boilerplate (argument parsing, if any — see Decisions Requiring Human
  Review item 8) may use exceptions is a Plan-stage detail with no bearing
  on any Result-based API this spec fixes.

## Proposed Design

### Runtime authority and the Client question

[ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md) commits
Atlantis to Runtime eventually being the sole authoritative owner of
engine world state, accessed by every external module (including a future
Editor) as a symmetric Client. This spec's own Runtime process has exactly
one implicit "Client" today — the OS/user, through the window itself — and
zero second process, Editor, or automation consumer to design a real
access boundary against. Consistent with ADR-0033's own Alternatives
Considered ("deciding the mechanism now... would be exactly the kind of
premature, uncontrolled architectural decision AGENTS.md's Golden Rule
prohibits"), this spec does not introduce any query/command/event type, IPC
mechanism, or process boundary. It satisfies ADR-0033's principle only
trivially: nothing outside Runtime observes or mutates its bootstrap
state at all in this spec's own scope, so there is, yet, no access pattern
to get wrong. See Decisions Requiring Human Review, item 10.

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

No dependency on Atlantis::RenderGraph directly (see Requirements'
"Module boundary"). No Vk* type anywhere in src/runtime/.
```

## Architectural Impact

This spec introduces architecture recorded in two new ADRs, both
`Accepted` alongside this spec's own Human Review Approval — required to
reach `Accepted` before this spec could move from `In Review` to
`Approved`, per [AGENTS.md](../AGENTS.md):

1. **Runtime composition, object ownership, and frame lifecycle** — the
   complete object model, initialization order, per-frame order, resize/
   presentation-error taxonomy, and reverse-order destruction fixed under
   Requirements above; the bootstrap-scene selection principle; and the
   scope to which [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
   Runtime-authority principle is exercised by this spec (trivially — no
   Client API). Filed as
   [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md).
2. **Runtime Host executable/library structure and test boundary** — the
   `atlantis_runtime_host` library / thin `atlantis_runtime` executable
   split, why it exists (testability only, mirroring Shader System's
   already-`Accepted` `atlantis_shader_compiler_lib` precedent), why it is
   explicitly not a new public dependency surface, Runtime's direct
   dependency on `Atlantis::VulkanBackend` (matching every existing
   composition root's own precedent, not a new backend-selection
   mechanism), and the GPU-independent-testable lifecycle/state-machine
   boundary (Testing & Verification Plan) that does not require a
   general DI/service-locator framework. Filed as
   [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the two new ADRs above — each new ADR
references and builds on the existing ones (particularly ADR-0001–0005,
ADR-0014, ADR-0016, ADR-0019–ADR-0027, ADR-0029–ADR-0031, ADR-0033,
ADR-0043) without altering them.

**No new public API.** Confirmed by direct inspection (see "Why this is
buildable" above, and the Independent Review note): `Atlantis::Platform`,
`Atlantis::RHI`, `Atlantis::VulkanBackend`, `Atlantis::Renderer`,
`Atlantis::ShaderSystem`/`ShaderSystemRhiIntegration`, and
`Atlantis::AssetSystem` are consumed by this spec's design exactly as they
exist today, with zero modification to any public header, public type, or
public function signature in any of them.

**`docs/architecture/module_boundaries.md` correction, deferred.** That
document's own Runtime section (`PROPOSED`, not `Accepted`) currently
lists RenderGraph as a direct Runtime dependency. This spec's own,
code-verified dependency list (Requirements' "Module boundary") omits it,
for the reason stated there. Per this repository's own established
pattern (e.g. Spec 0012's Tools-description narrowing), reconciling that
document's prose with this spec's own `Approved` conclusion is deferred to
a future Plan/docs-sync — not performed by this spec itself, and not a
blocker to this spec's own approval, since `module_boundaries.md` is not
`Accepted` and grants no approval on its own.

**ADR-0032 compliance.** `Atlantis Runtime` is already named among
[AGENTS.md](../AGENTS.md)'s ten top-level modules (Core, Platform, RHI,
Vulkan Backend, RenderGraph, Renderer, Shader System, Asset System,
Runtime, Tools) — this spec does not add a module to that list, it makes
the ninth of those ten a real, implemented one for the first time, the
last of the original nine (pre-Asset-System) named modules to reach that
state. This spec places `Atlantis Runtime`, once `Accepted` as a real
module (ADR-0046/ADR-0047), in that same, unchanged ten-module
authoritative source-ownership view, depending on Core, Platform, RHI,
Vulkan Backend, Renderer, Shader System (both targets), and Asset System,
and depended on by nothing (it is the executable) except its own tests. In the
conceptual five-layer view, it sits where
[specs/0009-long-term-engine-architecture-alignment.md](0009-long-term-engine-architecture-alignment.md)'s
own illustrative mapping already anticipated: "the not-yet-implemented
Runtime and Tools modules will eventually span Authoritative Runtime and
the SDK/Client boundary once their own Specs place them there" — this
spec's own scope stays within Authoritative Runtime only (a single
bootstrap scene, no SDK/Client surface at all yet; see "Runtime authority
and the Client question" above).

**ADR-0033 compliance.** Stated explicitly above under "Runtime authority
and the Client question" — this spec exercises ADR-0033's principle only
trivially (no Client, no access-category API), and does not design,
implement, or preview any Editor/Client mechanism ADR-0033 itself left Out
of Scope.

**ADR-0034 compliance.** Not meaningfully implicated: this spec introduces
no externally-consumed schema, identity, or protocol surface at all — its
only new public-ish surface, `Atlantis::RuntimeHost`, is explicitly not a
public dependency any other module may consume (see ADR-0047), so there is
no external boundary here for ADR-0034's stable-boundary principle to
apply to yet.

**ADR-0035 compliance.** Not implicated. This spec introduces no new
authoring-facing or runtime-execution data representation of its own — it
consumes Asset System's and Shader System's already-ADR-0035-compliant
output exactly as produced.

**Registry update (specs/README.md).** "Runtime Host and Composition
Root," formerly Candidate Order 2 in the Candidate Spec Backlog, has been
promoted to Section A as Spec 0013 alongside this spec's own drafting —
per that registry's own backlog-maintenance rule ("when a candidate item
gets a real Spec drafted, assign it a formal spec number at that point and
move/replace its row into Section A"), promotion happens once a real spec
is drafted, not gated on this spec later reaching `Approved`. Its
"Depends On" column is corrected from "Spec 0002 (Platform), Spec 0003
(RHI), Spec 0005 (RenderGraph Foundation), Spec 0007 (Minimal Renderer)"
to add Spec 0008
(Shader System Foundation) and Spec 0012 (Asset System Foundation) — both
`Approved`/implemented, and both load-bearing for this spec's own
bootstrap scene, per Requirements above; Spec 0005's own dependency is
retained even though Runtime does not depend on `Atlantis::RenderGraph`
directly, since Renderer's own dependency on it is what this spec
transitively relies on. Android Platform and Vulkan Presentation
(Candidate Order 1) is explicitly **not** reordered, reprioritized, or
reinterpreted by this spec — it remains `Candidate`, unimplemented, at its
own unchanged backlog position; drafting this spec ahead of it is, as with
Spec 0012 before it, an explicit human-directed reprioritization, not a
finding that Android's own scope or dependencies changed. World/ECS
Foundation (now Candidate Order 2, renumbered by this same registry
update from its prior Candidate Order 3 as a mechanical index correction
— see Section B's own updated ordering note) continues to depend on this
spec and is not drafted here.

## Decisions Requiring Human Review

**All ten items below were accepted as recommended by the 2026-08-20
Human Review Approval recorded at the top of this document** — retained
here as the permanent record of each recommendation and its reasoning,
per this repository's established practice (e.g. Spec 0008's own
pre-approval decision list). None was decided unilaterally by this spec;
each was confirmed explicitly rather than assumed.

1. **Pure executable, or an internal Runtime Host library plus a thin
   Windows executable?** *Recommendation:* the library-plus-thin-
   executable split (`atlantis_runtime_host` / `atlantis_runtime`), for
   testability only — see Requirements' "Executable/library structure" and
   ADR-0047. **Explicitly not** a new public service layer: no other
   top-level module may depend on `Atlantis::RuntimeHost`; it is not
   listed as a dependency of anything in
   `docs/architecture/module_boundaries.md`'s eventual corrected Runtime
   entry beyond `atlantis_runtime` itself and `tests/runtime/`. This
   mirrors Shader System's own already-`Accepted`
   `atlantis_shader_compiler_lib` precedent exactly.
2. **Runtime's dependency on a concrete Vulkan Backend, and where backend
   selection lives.** *Recommendation:* `Atlantis::RuntimeHost` depends on
   `Atlantis::VulkanBackend` directly and calls
   `vulkan_backend::createDevice()`/`createPresentation()` exactly as
   every existing composition root already does — this is not new
   coupling, it is the same pattern `AGENTS.md`'s own Vulkan-header-
   visibility rule already permits for a composition root (as opposed to
   Renderer/RenderGraph/Platform/generic-RHI's own public surface, which
   this spec's Runtime never touches with a `Vk*` type). No backend-
   selection abstraction, factory registry, or capability-tier system is
   introduced — Phase 1 has exactly one backend, and inventing a selection
   mechanism for a choice that does not exist would be the speculative
   abstraction `AGENTS.md` warns against.
3. **Complete object ownership, initialization order, per-frame order,
   and reverse-order destruction.** *Recommendation:* fixed in full under
   Requirements above (Object Model and Ownership; Initialization order;
   Bootstrap Sequencing Detail; Per-frame order; Presentation and
   error-state handling; Shutdown). Nothing here is left open for the
   Plan to invent.
4. **A testable lifecycle/state-machine boundary, without a general DI/
   service-locator framework.** *Recommendation:* yes — a small, pure,
   GPU-independent `RuntimeLifecycleState` transition type inside
   `Atlantis::RuntimeHost` (states at minimum: `Uninitialized`,
   `Initializing`, `Running`, `ShuttingDown`, `ShutDown`, `Failed`; legal-
   transition checking via `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`, per
   existing convention) is unit-testable with no GPU, no Platform window,
   and no Device — exactly analogous to how RenderGraph's own dependency/
   compilation logic (Spec 0005) is unit-tested without a GPU today. The
   real orchestration function that drives real Platform/Device/
   Presentation calls is **not** made generically mockable — it is
   verified by the GPU-required and manual tests instead (Testing &
   Verification Plan), matching how every prior spec's own composition-
   root orchestration has always been verified. No DI container,
   service-locator, or plugin registry is introduced anywhere.
5. **How the bootstrap scene selects its mesh, shader, camera, and
   material.** *Recommendation:* fixed under Requirements' "Bootstrap
   scene" — the already-existing, already-`Approved`, already-cooked
   `minimal_cube` asset and already-compiled `minimal_mesh` shader, a
   fixed or orbiting camera (either is already-proven prior art; exact
   choice a Plan-stage detail), one hardcoded `DrawItem`. No scene file
   format, no World/ECS preview, no second asset.
6. **Resize, zero extent, out-of-date/suboptimal, surface loss, device
   loss, and mid-frame close.** *Recommendation:* fixed exhaustively under
   Requirements' "Presentation and error-state handling," using only
   outcomes `Presentation`/`Device`'s real, current signatures can
   actually produce (`PresentationError`'s full four-value enum —
   `SurfaceLost`, `SwapchainCreationFailed`, `DeviceLost`, `Unknown` —
   and `SubmitError::{QueueSubmitFailed, DeviceLost}`, confirmed
   live-mapped from real `VkResult`s in
   `src/vulkan_backend/src/vulkan_result.cpp`, not merely declared and
   unused) — no new RHI capability is needed or proposed to express any of
   these outcomes.
7. **Windowed-only, no headless Runtime mode.** *Recommendation:*
   confirmed — see Non-Goals. Headless already has its own complete,
   independently useful closed loop (Spec 0010/0011) with real consumers
   of its own; no product need for a headless Runtime exists yet to
   justify designing one now, and this spec's own library/executable
   split (item 1) does not foreclose a future spec adding one later by
   substituting an `OffscreenTarget` for `Presentation` inside
   `Atlantis::RuntimeHost`'s existing composition shape — not designed or
   implemented here.
8. **Runtime's configuration boundary.** *Recommendation:* everything
   this spec's own bootstrap needs is fixed at build/composition time
   (window title, initial size, the one bootstrap asset/shader path set
   via CMake compile definitions, the existing `enableValidationLayers`
   Debug-always-on/Release-opt-in policy, unchanged) — **zero** required
   command-line or config-file surface for this first Runtime. A trivial,
   optional window-title/size command-line override may be added at Plan
   stage without being architecturally significant (the same level of
   freedom prior specs already leave to Plan for exact field names/
   values); a real configuration *file* format, hot-reloadable settings,
   or general configuration framework is fully out of scope, not merely
   deferred — see Non-Goals.
9. **Whether existing demos are retired, refactored, or kept.**
   *Recommendation:* kept, entirely unchanged — see Non-Goals. Each
   remains its own spec's own disclosed, non-shipping verification
   composition; this spec adds a new, separate, real product entry point
   alongside them, not a replacement.
10. **Whether Runtime authority needs any Client API in this round.**
    *Recommendation:* no — see "Runtime authority and the Client question"
    under Proposed Design. No IPC, no Editor protocol, no remote client,
    and no general Query/Command/Event/Snapshot surface of any kind is
    designed or implemented; [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
    principle is acknowledged and trivially satisfied (nothing external
    observes or mutates Runtime's state yet), not exercised in earnest.

## Alternatives Considered

- **A pure executable with no internal library boundary at all.**
  Rejected as this spec's own recommendation (Decisions Requiring Human
  Review item 1): it would leave the entire lifecycle/state-machine
  untestable without a real GPU and a real window, unlike every other
  substantial piece of logic this codebase already unit-tests, and would
  break the precedent Shader System's own `atlantis_shader_compiler_lib`
  split already established for exactly this reason.
- **Route Vulkan Backend selection through a new RHI-level factory/
  registry abstraction**, anticipating a second backend. Rejected: Phase 1
  has exactly one backend (`AGENTS.md`); every existing composition root
  already calls `vulkan_backend::createDevice()` directly, and Runtime
  doing the same is not new coupling this spec introduces — inventing a
  selection mechanism now would be speculative abstraction with no second
  backend to validate it against.
- **Design a minimal Editor/Client query API now**, so a future Tool/
  Editor Connection Protocol spec has less to add later. Rejected: no real
  second Client exists to design against yet, and ADR-0033's own
  Alternatives Considered already rejected deciding this mechanism ahead
  of a concrete consumer — this spec does not reopen that judgment.
- **Add a headless Runtime mode in this same spec**, since
  `Atlantis::RuntimeHost`'s own object model could plausibly support
  substituting `OffscreenTarget` for `Presentation`. Rejected for this
  round: no product need exists yet, and Spec 0010/0011's own headless
  closed loop already fully serves its own real consumers — see Non-Goals
  and Decisions Requiring Human Review item 7.
- **Invent a general application configuration/settings system**, so this
  spec's bootstrap parameters (window size, asset paths) are not
  hardcoded. Rejected: no second configuration ever needs to exist yet —
  exactly the kind of premature general framework `AGENTS.md` and this
  spec's own Non-Goals warn against; a Plan-stage, minimal, optional
  command-line override remains available without this.
- **Retire or fold the existing demos into Runtime**, since Runtime is now
  "the real thing" they were never meant to be. Rejected: each existing
  demo remains its own spec's own disclosed verification composition with
  its own already-recorded verification history; retiring any of them is
  unrelated cleanup this spec's own scope does not require and
  `AGENTS.md`'s "do not introduce unrelated refactoring" rule advises
  against doing opportunistically.

## Testing & Verification Plan

- **Unit tests (GPU-independent), `tests/runtime/`, linking
  `Atlantis::RuntimeHost` and `Atlantis::Core` only — no `Device`, no GPU,
  and no real window required to run:**
  - `RuntimeLifecycleState`'s own transition table: every legal
    transition succeeds; every illegal transition (e.g. `Running` →
    `Initializing`, a frame request while `ShuttingDown`, a second
    `initialize()` while already `Running`) is rejected as a programmer
    error (`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`), per existing convention.
  - Idempotent shutdown: invoking the shutdown path from each reachable
    state (`Initializing` having partially failed, `Running`, already
    `ShuttingDown`) reaches `ShutDown` exactly once, with no double-free
    and no repeated `platform::shutdown()` call — exercised against the
    pure state type, not a real `Device`/`Platform`.
  - The presentation/error-outcome classification table (Requirements'
    "Presentation and error-state handling") as a pure mapping function:
    each of `PresentationError`'s and `SubmitError`'s real enumerators
    (confirmed exhaustive against `src/rhi/include/atlantis/rhi/types.h`)
    maps to exactly the category (recoverable-silent, recoverable-
    Runtime-visible, unrecoverable) this spec's own table fixes, and
    every unrecoverable category maps to the same exit-code bucket.
  - Exit-code-category mapping: `Success`/`InitializationFailed`/
    `UnrecoverableRuntimeError` (or equivalently named, Plan-stage detail)
    each map to a distinct, fixed process exit code.
  - **Honest scope limit, stated explicitly, not silently overclaimed:**
    real resource construction/teardown ordering (the actual sequence of
    `Device`/`Presentation`/`Mesh`/`Material`/`Buffer`/`Texture`
    destruction calls against real objects) is **not** exercised by these
    GPU-independent tests — it inherently requires a real `Device`, and is
    instead verified by the GPU-required tests and Vulkan Validation
    Layers below, matching how every prior spec in this line has always
    verified its own composition root's real teardown correctness (none
    unit-tests it either).
- **GPU-required tests (Windows/Vulkan, `gpu`-labeled):**
  - **Correction to an earlier drafting error, found during independent
    review:** this spec's own bootstrap composition does **not**
    automatically compare its windowed output against Spec 0011's
    `minimal_cube` golden via `atlantis::image_regression::compareBuffers()`,
    and no such test is added. Verified directly against
    `src/rhi/include/atlantis/rhi/render_target.h` and
    `src/vulkan_backend/src/vulkan_presentation.cpp`: a swapchain-backed
    `RenderTarget` is created with `imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    | VK_IMAGE_USAGE_TRANSFER_DST_BIT` only — **no**
    `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`. `CommandList::copyRenderTargetToBuffer()`
    (the only existing GPU-to-CPU readback path, ADR-0040) issues
    `vkCmdCopyImageToBuffer`, which requires `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`
    on its source image; calling it against a windowed `RenderTarget` would
    violate that requirement and fail Vulkan Validation Layers, even though
    the type-level `dynamic_cast<VulkanRenderTargetAccess*>` check inside
    it (`src/vulkan_backend/src/vulkan_command_list.cpp`) does not itself
    reject a windowed target — `VulkanRenderTarget` and
    `VulkanOffscreenRenderTarget` both implement that private interface.
    There is therefore **no existing, unmodified-public-API path to read
    back and pixel-compare a real windowed frame**, and this spec does not
    add one — doing so would mean changing the Vulkan Backend's own
    internal swapchain `imageUsage` flags (itself contingent on
    `VkSurfaceCapabilitiesKHR::supportedUsageFlags` actually offering
    `TRANSFER_SRC_BIT`, not guaranteed on every present mode/compositor),
    which is exactly the kind of implementation change this spec's own
    Non-Goals require raising as an explicit architectural question rather
    than adding silently — not raised here, since no real need has forced
    it (see below for what this spec verifies instead).
  - **What is verified instead, split across three complementary layers,
    none of which claims what the others do not:**
    1. A **Runtime GPU smoke test** constructs the full bootstrap
       composition, creates a real window, and confirms `acquireNextTarget()`
       → the format/extent-change paths → `Renderer::drawFrame()` →
       `submit()` → `present()` all succeed at least once with Vulkan
       Validation Layers reporting zero warnings/errors — a mechanical
       correctness check (the real pipeline runs end to end, cleanly), not
       a pixel-content assertion of any kind.
    2. The **existing, unmodified headless image-regression suite**
       (`tests/image_regression/`, Spec 0011/0012, offscreen-only) remains
       the actual pixel-level rendering-output regression gate for this
       exact geometry/shader/camera combination — untouched, still passing,
       already covering the content Runtime's own bootstrap scene reuses.
    3. **Manual verification** (below) is the evidence that Runtime's own
       *visible window* shows the correct mesh — a human comparing what
       the window displays against the same already-published
       `minimal_cube` golden PNG, by eye, not an automated diff.
  - Device-loss and surface-loss code paths are verified by code
    inspection against `src/vulkan_backend/src/vulkan_result.cpp`'s real,
    live `VK_ERROR_DEVICE_LOST`-to-`PresentationError::DeviceLost`/
    `SubmitError::DeviceLost` mapping — genuinely triggering a real device
    loss on the test machine is not assumed available, matching Spec
    0007's own precedent for its own hard-to-trigger format-change case
    (stated as inspection-only if the environment cannot genuinely trigger
    it, never claimed as fully exercised otherwise).
- **Manual verification (Windows, real window, real GPU):**
  - A visible window shows the `minimal_cube` mesh, correctly shaded,
    matching every existing windowed demo's own already-verified visual
    bar — a human verifier compares it by eye against the same
    already-published `minimal_cube` golden PNG
    (`tests/image_regression/goldens/minimal_cube/`), the only comparison
    this spec's own verification makes against that golden (see Testing &
    Verification Plan's "GPU-required tests" note above).
  - Interactive resize continues to show the mesh correctly, with the
    depth `Texture` recreated at the new extent and `Material`/`Pipeline`
    **not** recreated for an extent-only change (dynamic viewport/
    scissor, unchanged from Spec 0007).
  - Minimizing produces no crash, no busy-spin, and no Vulkan call while
    minimized; restoring resumes correct rendering with no special
    recovery step visible to the user.
  - Closing the window (title-bar close, and — if feasible on the test
    machine — a deliberate mid-sequence close immediately after a
    resize/minimize) exits cleanly with exit code Success, no Validation
    Layer warning or error at any point including shutdown, and no leaked
    `CommandList`/`Buffer`/`Texture`/`Pipeline`/`Mesh`/`Material`.
  - A deliberately induced initialization failure (e.g., a temporarily
    corrupted or missing bootstrap asset/shader path) is confirmed to
    produce the initialization-failure exit-code category with a clear,
    logged reason — not a crash, not a silent hang.
- **Regression, unchanged:** every existing GPU-independent test suite,
  every existing `gpu`-labeled test suite, every existing headless/image-
  regression test, and every existing Asset System/Shader System test
  suite continues to pass, on both Debug and Release, exactly as before
  this spec's implementation — this spec adds a new module and a new test
  directory, and touches no existing test, example, CMake target, asset,
  shader, or golden file.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of this spec's implementation, per
  [AGENTS.md](../AGENTS.md) and
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- **No global mutable engine state; reverse-order resource release:**
  verified by inspection (no `static`/global owning pointer anywhere in
  `src/runtime/`, matching every other module's own equivalent Acceptance
  Criterion) and by the manual verification's own clean-shutdown/no-leak
  observation above.
- **No golden added or modified**, per this spec's own Non-Goals and
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  — this spec's implementation never writes to
  `tests/image_regression/goldens/`, and no test this spec adds performs
  an automated pixel comparison of any kind (see the "GPU-required tests"
  correction above for why). The existing `minimal_cube` golden remains
  exactly as Spec 0011/Plan 0012 left it, exercised only by the existing,
  unmodified headless suite.

## Risks & Open Questions

- **Whether a deferred `Material` construction (Bootstrap Sequencing
  Detail) is genuinely equivalent to the format-change path Spec 0007
  already verified, or introduces a subtly different first-frame timing
  this spec's implementation must re-verify.** This spec's own analysis
  concludes it is the same code path exercised for the same reason
  (`lastSeenFormat` starting unset), but this has not been run against
  real hardware yet — flagged for the Plan/Implementation stage to
  confirm empirically, not assumed.
- **Exact `RuntimeLifecycleState` enumerator set and transition table** is
  left to the Plan — this spec fixes the state-machine's existence,
  GPU-independent testability, and the states named as "at minimum" under
  Decisions Requiring Human Review item 4, not its exact C++ shape.
- **Exact exit-code integer values** are left to the Plan — this spec
  fixes the three required, distinct categories (Success,
  InitializationFailed, UnrecoverableRuntimeError) and which outcome maps
  to which, not their literal numeric values.
- **Whether a fixed or orbiting camera** is used for the bootstrap scene
  is left to the Plan — both are already-proven prior art (the fixture
  uses a fixed camera; `minimal_renderer_demo` uses an orbiting one) and
  neither is an architectural decision.
- **Whether `docs/architecture/module_boundaries.md`'s Runtime section
  should be corrected in the same PR that implements this spec, or as a
  separate docs-sync PR** — this spec's own Architectural Impact
  identifies the correction but does not itself perform it (that document
  is out of this spec's own file scope, matching the same judgment Spec
  0009/0012 already applied to similar cross-cutting docs corrections);
  left to the Plan or a follow-up docs PR to decide the mechanics of.
- **Whether Runtime's eventual real Client-boundary exercise (a future
  Tool/Editor Connection Protocol spec) will find this spec's own
  composition shape genuinely reusable, or will need to restructure
  `Atlantis::RuntimeHost`'s internals** is explicitly not answerable now —
  named honestly as a real, deferred risk (matching ADR-0033's own
  Negative/Trade-offs), not something this spec claims to have preempted.

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
document's own Non-Goals above, and none is advanced, designed, or
unblocked by this spec beyond providing the real Runtime executable each
of them will eventually depend on.
