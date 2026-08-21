# Module Boundaries

> **Status: PROPOSED — pending spec/ADR approval. Not as-built.** See the
> status note in [overview.md](overview.md) and
> [docs/architecture/README.md](README.md).
>
> **Revised 2026-08-02** for the Windows/Android (primary) + iOS (future)
> platform decision: adds **Atlantis Platform** as a module, and updates
> Renderer/RHI/Vulkan Backend/Runtime boundaries accordingly. Linux is not
> a target platform. See
> [ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

For a higher-level overview connecting these modules to Atlantis's
conceptual architecture and current build status, see
[engine_architecture.md](engine_architecture.md) — this document remains
the authoritative source for per-module responsibility, dependency, and
ownership detail.

Each module below is documented as: responsibilities, allowed/forbidden
dependencies, ownership, public/private boundary, and future extension
points. None of these modules exist yet — this is the boundary they must
be built against once each gets its own approved spec.

---

## Atlantis Core

**Responsibilities:** math (vectors, matrices, quaternions), containers,
memory allocators, logging, assertions, non-graphics/non-windowing
platform-independent utilities (e.g. time, minimal file I/O abstractions
where they don't require OS-specific windowing/lifecycle knowledge).

**Depends on:** nothing in Atlantis. Standard library and, where an
approved spec calls for it, a small number of foundation-level
third-party libraries (each such addition is itself a "new dependency"
under [AGENTS.md](../../AGENTS.md) and needs its own spec/ADR).

**Depended on by:** every other module.

**Ownership:** Core types are value types or explicitly-owned RAII types;
Core does not define any GPU, windowing, or OS-lifecycle concept and
therefore has no GPU/window/platform lifetime story.

**Public/private boundary:** everything Core exports is meant for reuse
across modules; there is no "internal-only" Core surface by design — if
something is Core-internal, it isn't in Core's public headers.

**Extension points:** allocator strategy, job/task system (if one is
introduced) are both explicitly *not decided here* — flagged as open in
[threading.md](threading.md).

---

## Atlantis Platform

**Responsibilities:** the per-OS windowing/surface/lifecycle abstraction.
Owns window/view creation and destruction, the OS event/message pump,
input event delivery, and app-lifecycle signaling (e.g. pause/resume,
surface created/destroyed). Exposes an **opaque native surface handle**
to its caller (Runtime) — nothing about that handle's internal shape is
visible outside Platform and whatever RHI/Vulkan Backend code consumes it.

**Concrete implementations (Phase 1 + future):**
- **Windows Platform** — Win32 window + message loop.
- **Android Platform** — Android NDK / `ANativeWindow`, Activity/Surface
  lifecycle callbacks (likely via `android_native_app_glue` or an
  equivalent — not decided by this document).
- **iOS Platform (future, not implemented)** — UIKit `UIView`/
  `CAMetalLayer`. Not started, not designed; named only to communicate
  direction.

**Depends on:** Core, and the target OS's native platform headers
(`Win32`, Android NDK, future UIKit) — each concrete implementation
depends only on *its own* OS's headers, never another OS's.

**Depended on by:** Runtime (owns a Platform instance for the OS being
built for). **Nothing else** — RHI, Vulkan Backend, RenderGraph, and
Renderer never depend on Atlantis Platform. See Forbidden, below.

**Forbidden:** no Vulkan header, no `Vk*` type, anywhere in Platform. No
graphics-API knowledge at all — Platform doesn't know Vulkan exists. No
dependency on RHI, RenderGraph, or Renderer.

**Ownership:** owns the native window/view/surface for its full lifetime,
including OS-driven destruction/recreation (e.g. Android backgrounding).
Runtime owns the Platform instance itself.

**Public/private boundary:** public surface is lifecycle (create/
destroy/pump-events), an opaque native-surface-handle accessor, and
lifecycle event delivery (e.g. "surface became invalid," "app paused").
OS-specific types (`HWND`, `ANativeWindow*`, ...) never cross this public
surface — see [resource_lifetime.md](resource_lifetime.md) for how
surface invalidity is expected to propagate.

**Extension points:** iOS Platform (future, not designed). Exact shape of
the opaque native-surface-handle type, and exact shape of lifecycle event
delivery (callback vs. polled-at-frame-boundary vs. queue) are **not
decided by this document** — see
[ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md) and
Open Questions in [threading.md](threading.md).

---

## Atlantis RHI

**Responsibilities:** the backend-agnostic Render Hardware Interface —
`Device`, command recording (`CommandList`/`CommandBuffer`), resources
(`Buffer`, `Texture`, `Sampler`), pipeline/pipeline-state objects,
`RenderTarget`, the `Presentation` abstraction, and synchronization
primitives (fence/semaphore-equivalents), all as opaque interfaces/handles.

**Depends on:** Core only.

**Depended on by:** Vulkan Backend (implements it), RenderGraph, Renderer
(both consume it, never the Vulkan Backend directly), Runtime (creates a
`Device` and a `Presentation` instance to wire the frame loop).

**Forbidden:** no `Vk*` type, no Vulkan header, anywhere in RHI's public
surface. No windowing-library type (GLFW/SDL) and **no Atlantis Platform
type** (Win32/Android NDK/future UIKit) anywhere in RHI — `Presentation`
creation accepts an opaque native-surface handle as a parameter, not a
`Platform` object. See
[ADR-0001](../../adr/0001-rhi-backend-independence.md).

**Ownership:** RHI defines the *interface* for resource/RenderTarget
lifetime; it does not itself decide caching/pooling policy. See
[resource_lifetime.md](resource_lifetime.md).

**Public/private boundary:** RHI's public surface is the interfaces
listed above. Anything backend-specific (Vulkan object handles, extension
usage, WSI surface-creation calls) is private to Vulkan Backend and never
appears in an RHI header.

**Extension points:** a second backend (a native Metal backend for future
iOS is the one named candidate — see
[overview.md](overview.md#windowed-vs-headless-the-shared-path-across-platforms))
implementing the same interfaces is the reason this boundary exists, but
is explicitly **out of scope for Phase 1** — do not add abstraction knobs
"for" a backend that isn't being built (per
[AGENTS.md](../../AGENTS.md) Phase 1 constraints).

---

## Atlantis Vulkan Backend

**Responsibilities:** the concrete, sole-for-Phase-1 implementation of
every RHI interface using Vulkan, targeting both Windows and Android.
Owns all `Vk*` objects, all Vulkan extension usage — including the
platform-specific WSI surface extensions (`VK_KHR_win32_surface`,
`VK_KHR_android_surface`) — and the `VkSwapchainKHR`-backed implementation
of RHI's `Presentation` interface.

**Depends on:** RHI (implements its interfaces), Core, the Vulkan SDK/
loader. **Does not depend on Atlantis Platform** — it receives the same
opaque native-surface handle RHI's `Presentation` interface defines
(produced by Platform, passed through Runtime) and internally dispatches
to the correct WSI extension based on which platform/handle variant it
was given.

**Depended on by:** Runtime (only to select/construct the backend at
startup — Runtime talks to it through RHI interfaces afterward, not
concretely). Nothing else depends on it; RenderGraph and Renderer never
reference it.

**Ownership:** owns the lifetime of every native Vulkan object it creates
in service of an RHI handle; when an RHI handle is destroyed, the backend
tears down the corresponding Vulkan object(s).

**Public/private boundary:** its public surface *is* RHI's interfaces
(via implementation) plus whatever narrow construction API Runtime needs
to stand up a `Device`/`Presentation` at startup. No `Vk*` type crosses
that construction API's return values — callers get back RHI handles.

**Extension points:** future iOS via MoltenVK would extend this module
with a third WSI path (`VK_MVK_ios_surface`/`VK_EXT_metal_surface`); not
designed, not implemented, not decided over the native-Metal-backend
alternative — see [ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

---

## Atlantis RenderGraph

**Responsibilities:** the central rendering abstraction — pass
declaration, resource read/write dependency tracking, automatic barrier
and resource-lifetime resolution, execution-order (topological) resolution
of a frame's passes. Per [AGENTS.md](../../AGENTS.md), this is not
optional infrastructure: no ad hoc direct-submission rendering path is
allowed to bypass it.

**Depends on:** RHI, Core.

**Depended on by:** Renderer.

**Forbidden:** no dependency on Vulkan Backend, Atlantis Platform, Win32,
Android NDK, GLFW/SDL, or Runtime.

**Ownership:** owns the per-frame graph data structure (passes + resource
dependency edges) for the duration of a frame's construction and
execution; does not own long-lived GPU resources — those are owned per
[resource_lifetime.md](resource_lifetime.md) and referenced into the graph.

**Public/private boundary:** public surface is pass-declaration and
resource-dependency APIs consumed by Renderer; graph-compilation/
scheduling internals (topological sort, barrier synthesis) are private.

**Extension points:** GPU-driven / data-dependent scheduling is a
named future phase (see [AGENTS.md](../../AGENTS.md)) — not designed here,
not to shape this module's Phase 1 shape beyond what an approved spec
calls for.

---

## Atlantis Renderer

**Responsibilities:** frame orchestration built on RenderGraph + RHI.
Receives a `RenderTarget` from its caller each frame and draws into it.

**Depends on:** RHI, RenderGraph, Core.

**Forbidden (explicit, per the task driving this document):** Win32, the
Android NDK, GLFW/SDL or any windowing/platform library, `VkSurfaceKHR`,
`VkSwapchainKHR`, any `Vk*` type, the Vulkan Backend module directly, and
the Atlantis Platform module directly. See
[ADR-0001](../../adr/0001-rhi-backend-independence.md),
[ADR-0002](../../adr/0002-presentation-rendertarget-unification.md), and
[ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

**Depended on by:** Runtime.

**Ownership:** does **not** own the `RenderTarget` it draws into — it
borrows a non-owning reference for the duration of a frame. It does not
create, resize, or destroy `RenderTarget`s, and has no way to observe
which OS or Platform implementation produced one. See
[resource_lifetime.md](resource_lifetime.md).

**Public/private boundary:** public surface is "given RHI handles, a
RenderGraph, and a RenderTarget, produce a frame." Scene representation,
material system, and any higher-level submission API are **not designed
yet** — no rendering features are implemented ahead of their own specs
(per [AGENTS.md](../../AGENTS.md)), so this document deliberately does not
invent one.

**Extension points:** scene/material submission API (future spec);
multi-`RenderTarget` / multi-viewport rendering (future spec).

---

## Atlantis Shader System

**Responsibilities:** shader authoring/compilation/reflection — turning
shader source into a backend-consumable artifact (e.g. SPIR-V) plus
reflection metadata (bindings, push-constant layout) usable to build RHI
pipeline objects.

**Depends on:** Core.

**Depended on by:** Vulkan Backend and/or RHI (for pipeline construction,
exact seam TBD by its own spec), Tools (offline shader compilation).

**Forbidden:** no dependency on Renderer or Atlantis Platform. Shader
System is a producer of artifacts consumed lower in the stack, not a
consumer of frame-level or platform-level concepts.

**Ownership:** owns compiled-artifact lifetime up until it is handed to
RHI/Vulkan Backend for pipeline creation, at which point pipeline-object
lifetime is RHI's concern.

**Public/private boundary:** public surface is "source/asset in, compiled
artifact + reflection metadata out." Compiler internals (parsing,
target-specific codegen) are private.

**Extension points:** shader language choice, hot-reload, and multiple
target profiles are **not decided here** — open questions for that
module's own spec.

---

## Atlantis Asset System

**Status: Approved, implemented and merged** (Spec 0012, ADR-0043/
ADR-0044/ADR-0045, all `Accepted`;
[PR #58](https://github.com/slmao/Atlantis/pull/58)) — the first module
in this document that is genuinely as-built rather than only a
`PROPOSED` draft description; this section states the real, shipped
boundary, not a placeholder.

**Responsibilities:** turns checked-in, human-authored authoring source
into a deterministic, versioned runtime artifact plus a metadata
sidecar, and loads that artifact back into CPU-side data
(`atlantis::asset_system::StaticMeshAssetData`, this Spec's one
supported asset type). Never constructs a GPU resource itself.

**Depends on:** Core only. No RHI, Renderer, RenderGraph, Shader System,
Vulkan Backend, Platform, or Tools dependency — verified by an
include-scanning test (`tests/asset_system/module_boundary_tests.cpp`),
not merely stated.

**Depended on by:** Tools (the `atlantis_asset_cooker` CLI entry point)
and, outside this module, any composition root that loads an asset and
then itself constructs a GPU `Mesh` from the CPU data returned —
currently `tests/image_regression/fixture/`, eventually a future
Atlantis Runtime. Asset System itself is never depended on by Renderer,
RHI, RenderGraph, or Vulkan Backend, and gains no dependency from any of
them in the other direction either.

**Forbidden:** no RHI, Renderer, RenderGraph, Shader System, Vulkan
Backend, Platform, or Tools header anywhere in this module's own source.
No construction of `atlantis::rhi::Buffer` or any other GPU resource —
that is exclusively the composition root's job, using the module's
existing, unmodified `atlantis::renderer::createMesh()`.

**Ownership:** each loaded `StaticMeshAssetData` is an explicit,
caller-held value — never a global, static, or singleton registry. No
in-process asset cache or database of any kind exists in this module.

**Public/private boundary:** public surface is path normalization,
Asset ID computation, the cooker/loader functions, and the three data
formats' own parse/serialize functions. The cooker's atomic-write
mechanism and the CMake stamp/`BYPRODUCTS` integration are internal to
the build graph, not part of the runtime-consumed public API.

**Extension points:** a second asset type (textures — blocked on a
future RHI Spec adding a general sampled `Texture`/`Sampler` capability
first), a rename-stable GUID identity scheme, and a real derived-data
cache are each named, explicitly out-of-scope future work in Spec 0012
— not designed or scaffolded here.

---

## Atlantis Runtime

**Status: Approved, implemented** (Spec 0013, ADR-0046/ADR-0047, all
`Accepted`) — this section states the real, built boundary for the
Windows windowed path; Android/iOS remain architectural, not
implemented (see Extension points).

**Responsibilities:** the actual composition root. A private
`atlantis_runtime_host` static library (`Atlantis::RuntimeHost`) owns
the object model, initialization sequence, per-frame orchestration, and
shutdown; a thin `atlantis_runtime` Windows executable contains only the
`main()` entry point that constructs and drives it. Owns a Platform
session for the OS being built (Windows Platform; Android Platform not
implemented), creates the RHI `Device` and (on the first `SurfaceCreated`
event) `Presentation` via the Vulkan Backend, loads the `minimal_cube`
Asset-System-sourced mesh and the `minimal_mesh` Shader-System-compiled
material, and each frame: acquires a `RenderTarget` from `Presentation`,
hands it to `Renderer`, then presents. Also responds to Platform-
delivered lifecycle events; Android-specific lifecycle handling (surface
destroyed/recreated, app paused/resumed) remains TBD, see Open Questions
in [threading.md](threading.md).

**Depends on:** Atlantis Platform, RHI (Device + Presentation), Renderer,
Shader System (both targets), Asset System, Core. **Not** RenderGraph
directly — `Renderer::drawFrame()` already owns RenderGraph construction/
compilation/execution internally, confirmed by inspection that no
`atlantis/render_graph/*.h` header is included anywhere under
`src/runtime/` (a correction to this section's own earlier, `PROPOSED`-
era text, which listed RenderGraph as a direct dependency before any
real Runtime code existed to check that claim against). This is the
**only** module permitted to depend on Atlantis Platform.

**Depended on by:** nothing (it's the executable) — and, per its own
design, `Atlantis::RuntimeHost` specifically is not to be depended on by
any *other* top-level module either, even though it is technically a
linkable library: it exists solely so `tests/runtime/`'s own GPU-
independent lifecycle/error-classification tests can exercise real
composition logic without a device or a window.

**Ownership:** a `PlatformSession` RAII guard (wrapping
`platform::initialize()`/`shutdown()`) is declared as the composition
object's *first* member, so it is destroyed *last* by ordinary C++
reverse-declaration-order destruction — a compiler-enforced guarantee
that the window outlives every GPU resource, not a hand-sequenced
convention. `Device`, `Presentation`, `Mesh`, the camera `Buffer`, the
depth `Texture`, and `Material` are owned in that reverse order below
it, matching `Material → Texture → Buffer → Mesh → Presentation → Device
→ PlatformSession`/window as the fixed destruction sequence.
`RenderTarget` instances acquired each frame are short-lived and scoped
to that frame — see [resource_lifetime.md](resource_lifetime.md).

**Public/private boundary:** `atlantis_runtime_host` is a private
composition library — not a public dependency surface any other module
may consume, despite being a real, linkable CMake target (see Depended
on by, above). `atlantis_runtime`'s own `main()` (Windows; Android
`android_main`/`ANativeActivity_onCreate` or equivalent remains
unimplemented) contains no composition logic of its own beyond building
a fixed configuration from build-tree paths and driving
`atlantis_runtime_host`'s own already-public API.

**Extension points:** the headless entry point (offscreen target
construction instead of Platform/Presentation) remains a future,
unimplemented Runtime-level (or Tools-level) concern that would reuse the
same Renderer/RenderGraph — see
[overview.md](overview.md#windowed-vs-headless-the-shared-path-across-platforms).
Not implemented by Spec 0013, which is windowed-only by explicit design.
Future iOS Runtime entry point: not designed.

---

## Atlantis Tools

**Responsibilities:** offline/developer tooling — the
`atlantis_asset_cooker` CLI entry point (Spec 0012, `Approved`,
implemented; the real cooking logic lives in Atlantis Asset System, not
here), shader
precompilation CLI, debug-capture glue (e.g. RenderDoc workflow per
[testing-strategy.md](../process/testing-strategy.md)). This narrows an
earlier, more generic "asset processing" phrase now that Spec 0012 has
fixed the actual boundary — Tools hosts the CLI entry point only.

**Depends on:** Core, Shader System, Atlantis Asset System, and other
modules as needed for what a given tool processes.

**Depended on by:** nothing — no runtime module ever depends on Tools.

**Ownership:** each tool owns its own process lifetime; no shared state
with Runtime.

**Public/private boundary:** each tool is its own executable/CLI surface;
no shared "Tools library" API is assumed here.

**Extension points:** left to individual tool specs as they're proposed.
Whether Tools runs on the same target platforms as the engine (Windows/
Android) or is desktop-only (Windows) tooling is not decided here.
