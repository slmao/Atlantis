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
boundary, not a placeholder. **Extended by Spec 0015 (Scene Asset &
Serialization Foundation, `Approved`), ADR-0052–ADR-0054 (all
`Accepted`) — implemented and merged via
[PR #74](https://github.com/slmao/Atlantis/pull/74) (2026-08-23); the
paragraphs below already describe the extended boundary.**

**Responsibilities:** turns checked-in, human-authored authoring source
into a deterministic, versioned runtime artifact plus a metadata
sidecar, and loads that artifact back into CPU-side data. Two asset
types are supported: `atlantis::asset_system::StaticMeshAssetData`
(Spec 0012) and a scene graph
(`atlantis::asset_system::ValidatedSceneData` — node hierarchy,
Transform/Camera/Renderable-shaped DTOs that never name a
`atlantis::world::` type, ADR-0053; Spec 0015). Never constructs a GPU
resource itself.

**Depends on:** Core only. No RHI, Renderer, RenderGraph, Shader System,
Vulkan Backend, Platform, Tools, or World dependency — verified by an
include-scanning test (`tests/asset_system/module_boundary_tests.cpp`),
not merely stated.

**Depended on by:** Tools (the `atlantis_asset_cooker` CLI entry point)
and, outside this module, any composition root that loads an asset and
then itself constructs GPU resources from the CPU data returned —
`tests/image_regression/fixture/` and Atlantis Runtime (which loads a
real scene asset, not a single hardcoded mesh — see the Atlantis
Runtime section below) and `atlantis::world::fromValidatedSceneData()`
(Atlantis World, for the CPU-side scene graph only — World still
depends on Asset System only narrowly, for `AssetId`, per
ADR-0048/ADR-0053, never the reverse). Asset System itself is never
depended on by Renderer, RHI, RenderGraph, or Vulkan Backend, and gains
no dependency from any of them in the other direction either.

**Forbidden:** no RHI, Renderer, RenderGraph, Shader System, Vulkan
Backend, Platform, Tools, or World header anywhere in this module's own
source. No construction of `atlantis::rhi::Buffer` or any other GPU
resource — that is exclusively the composition root's job, using the
module's existing, unmodified `atlantis::renderer::createMesh()`.

**Ownership:** each loaded `StaticMeshAssetData`/decoded
`ValidatedSceneData` is an explicit, caller-held value — never a
global, static, or singleton registry. No in-process asset cache or
database of any kind exists in this module.

**Public/private boundary:** public surface is path normalization,
Asset ID computation, the cooker/loader functions, and each data
format's own parse/serialize functions. The cooker's atomic-write
mechanism and the CMake stamp/`BYPRODUCTS` integration are internal to
the build graph, not part of the runtime-consumed public API.

**Textures (Spec 0016, implemented and merged):** a third asset type,
following the same cook/artifact/metadata-sidecar/load shape mesh and
scene already established --
`texture_types.h`/`texture_artifact.h`/`texture_metadata.h`/
`cook_texture.h`/`load_texture.h`. `cookTexture()` takes already-decoded
pixel bytes and never calls `stbi_load()` itself -- the PNG decode call
lives only in the Tools cooker's own `runCookTextureMode()`
(`src/tools/asset_cooker/`), the sole place this module's own runtime
library boundary is crossed by `stb`; `atlantis_asset_system`'s own
`target_link_libraries` closure never reaches `Stb::Stb`, confirmed via
CMake's own dependency graph, not header-grep alone. `TextureColorSpace`
is this module's own independent enum, never `atlantis::rhi::SampledTextureFormat`
-- the RHI dependency boundary this section states above is unaffected;
a composition root outside Asset System (the fixture) is the only place
that translates one to the other. `cookTexture()` normalizes its own
logical path exactly like `cookStaticMesh()`, so every cooked texture
asset has its own unique, normalized logical path and Asset ID -- a
post-merge Human Review Correction ([PR #79](https://github.com/slmao/Atlantis/pull/79),
[PR #80](https://github.com/slmao/Atlantis/pull/80)) fixed an initial
implementation that let two color-space variants of the same texture
silently share one Asset ID.

**Mesh UV0 (Spec 0017, implemented and merged):** the static mesh
authoring/artifact format bumped to schema version 2 -- every vertex is
now position(3)+color(3)+UV0(2), a fixed 32-byte stride at byte offsets
0/12/24, mandatory for every vertex, no optional/variant layout
([ADR-0058](../../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)).
Version 1 (24-byte, position+color-only) is rejected outright by both
`parseMeshSource()` and `decodeMeshArtifact()` -- no dual-version
reader, no compatibility migrator. UV is never clamped or flipped by
this module; a caller-supplied vertex-input schema decides which byte
offsets a given shader pipeline actually reads (proven directly against
`Device::createPipeline()`'s own real `VkVertexInputAttributeDescription`
construction, which only ever names an attribute the caller's schema
lists). `minimal_cube`'s own 8-vertex, shared-corner topology carries a
disclosed `(0.0, 0.0)` placeholder UV0 per vertex -- a schema-migration
necessity only, never claimed as a per-face texture unwrap. Two
independent quad mesh assets (`textured_quad_left`, `textured_quad_right`)
are now cooked through this same pipeline and consumed by the textured
image-regression fixture via the real `loadStaticMeshAsset()` path, in
place of that fixture's own former hand-authored vertex/UV arrays.

**Material (Spec 0018, implemented and merged), a fourth asset type,
still Core-only:** a small, versioned DTO — a closed `MaterialKind` enum
(one enumerator this round, `UnlitTextured`), a texture Asset ID, and
`Sampler` parameters (`Filter`/`AddressMode`) mirroring
`atlantis::rhi::SamplerCreateParams` exactly
([ADR-0059](../../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)).
`MaterialAssetData` names no RHI type and no shader path/identifier —
shader identity is a closed, Runtime-private mapping from `MaterialKind`
to a fixed, already-compiled built-in shader pair, never a Shader Asset
Catalog. The 32-byte material artifact embeds no self-Asset-ID (the
texture artifact's own precedent); its embedded texture Asset ID is
value-level only, never existence-checked here — an unresolvable
reference is exclusively a Runtime-side error at scene-load time.
Alongside this, the scene authoring/artifact format bumped to schema
version 2: a node's `Renderable` slot gained an *optional* material
Asset ID beside its existing mandatory mesh Asset ID (the per-node
record widened from 72 to 84 bytes); version 1 is rejected outright, no
dual-version reader
([ADR-0060](../../adr/0060-scene-material-binding-and-runtime-transactional-resource-publish.md)).
The per-scene manifest (Spec 0015) gained `MATERIAL_DEPENDENCIES`/
`TEXTURE_DEPENDENCIES` CMake arguments, still the same, unwidened
three-column, build-tree-private, scene-scoped file — never a fourth
"kind" column, since Runtime already knows which Asset ID is which kind
from the scene's own decoded structure. `World::Renderable` widened the
same way, in kind, as `DecodedRenderable` — one more plain, optional
`AssetId` field, no change to `World`'s own dependency closure or
construction of zero Renderer/RHI types.

**Mesh Normal (Spec 0020, implemented and merged):** the static mesh
authoring/artifact format bumped to schema version 3 -- every vertex is
now position(3)+color(3)+UV0(2)+normal(3), a fixed 44-byte stride at
byte offsets 0/12/24/32, mandatory for every vertex, no optional/
variant layout, four named `constexpr` offset constants in
`mesh_artifact.h`
([ADR-0063](../../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)).
Versions 1 and 2 (24-byte position+color-only, 32-byte pre-normal) are
both rejected outright by both `parseMeshSource()` and
`decodeMeshArtifact()` -- no dual-version reader, no compatibility
migrator. A shared `atlantis::asset_system::detail` numeric pair
independently re-derives each normal's own double-precision
length-squared and checks it against a fixed, inclusive `[0.9801,
1.0201]` tolerance at *both* cook and load time -- the decoder never
trusts the cooker's own already-performed check. The cooker never
auto-generates a normal from position and never normalizes an
author-supplied value; an out-of-tolerance normal is a hard cook-time/
decode-time error (`NonUnitNormal`), not a silent correction.
`minimal_cube`'s own eight corners each carry a real, smooth
(vertex-averaged), sign-matched normal; both `textured_quad` meshes
carry a uniform `(0, 0, 1)`, independently verified by hand
cross-product over both triangles of each quad. This is a normal *data
contract* only (authoring → cook → artifact → load) -- no shader in
this codebase reads the new attribute, and no rendered output changes
as a result; it exists solely as Lighting Foundation's own named, hard
prerequisite (Spec 0019 D1).

**Lighting Foundation (Spec 0019, implemented and merged via
[PR #96](https://github.com/slmao/Atlantis/pull/96)):** `atlantis::world::World`
gains a third optional per-entity component, `Light` (Directional or
Point — a flat DTO, no direction/position of its own, both re-derived
from the owning entity's own current world matrix), mirroring
`Camera`/`Renderable`'s own existing shape exactly
([ADR-0061](../../adr/0061-world-light-component-and-scene-lighting-binding-boundary.md)).
The Scene Asset format gains an optional, structurally capped light
node (one Directional, up to four Point) at both the source-grammar and
artifact layers, schema version bumped 2 → 3 the same way Material's
own addition (Spec 0018) bumped it 1 → 2; `MaterialKind` gains a second
enumerator, `LitTextured`. Runtime computes a fixed-size,
`alignas(16)`, 176-byte `FrameLightingData` array exactly once per
session — the first frame `World::updateTransforms()` runs — and
publishes it through the *existing* camera uniform `Buffer` (widened
from 128 to 304 bytes), never a second buffer or a new RHI/Renderer
public API; the one real RHI-internal change is the existing uniform
binding's own Vulkan `stageFlags` widening from vertex-only to
vertex-and-fragment (one value, one line,
[ADR-0062](../../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)).
**Disclosed, real, pre-existing limitation found and not fixed by this
Spec: no runtime light-update capability of any kind** — a
`World::setLight()` call after that one-time capture changed `World`'s
own CPU state only, never reflected in a rendered frame without a full
scene reload. **Fixed by Dynamic Frame Uniform Updates Foundation,
below.** A new `lit_textured` shader
pair applies exact Lambertian diffuse shading against the real,
Spec-0020-sourced per-vertex normal, gated by a conformal-transform
check that affects only `LitTextured`-bound entities, never the unlit
path. Shadows, PBR, image-based lighting, and post-processing are all
explicitly out of this Spec's own scope (its own Non-Goals) — none of
them exist anywhere in this codebase as a result of this work.
**Disclosed, real, pre-existing limitation found during this Spec's own
final review** (Plan 0018-introduced, unrelated to lighting): the Vulkan
Backend's own `VkDescriptorPool` (`VulkanDevice`, Device-global, fixed
`maxSets = 4`) was sized for Plan 0007's own original "exactly one
Material" assumption, never revisited when Plan 0018 introduced
arbitrary-N-materials support — a real, currently-supported two-
distinct-material color-format change exceeded it, reproduced by a real
GPU regression test. **Fixed by Descriptor Pool Capacity Foundation,
immediately below.**

**Descriptor Pool Capacity Foundation (Spec 0021, implemented and
merged via [PR #100](https://github.com/slmao/Atlantis/pull/100)):**
`VulkanDevice`'s own single, fixed-capacity `VkDescriptorPool` is
replaced by a private, growable set — a fixed
`std::array<DescriptorPoolEntry, 4>` plus an explicit live count, never
a `std::vector` (this render path must stay exception-free; a
dynamically-growing container risks `std::bad_alloc`/a leak-on-throw
window a fixed array cannot,
[ADR-0064](../../adr/0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)).
One shared helper creates both the initial pool (generation 0,
`maxSets = 4`) and every later-grown pool (generations 1-3: `maxSets =
8, 16, 32`, geometric doubling), so they cannot drift apart.
Allocation scans every existing pool in creation order, reusing freed
capacity, before ever growing — only `VK_ERROR_OUT_OF_POOL_MEMORY`/
`VK_ERROR_FRAGMENTED_POOL` are growth-eligible; `VK_ERROR_DEVICE_LOST`/
host-or-device out-of-memory fail immediately, no growth attempted. A
fixed, Plan-time-approved hard ceiling (4 pools, 60 concurrent
descriptor sets total) stops growth from masking a genuine leak as
unbounded allocation. Zero RHI/Renderer/Material public API change;
`VulkanPipeline` needed no modification — its own pre-existing single
`descriptorPool_` field already expressed "the specific pool my one
descriptor set belongs to," only the value threaded into it changed.
**The four-pool/60-concurrent-descriptor-set hard ceiling is a real,
deliberate, disclosed limit, not bindless rendering, not descriptor
indexing, and not unbounded material support** — a future scene
genuinely needing more remains its own, dedicated future finding.

**Dynamic Frame Uniform Updates Foundation (Spec 0022, corrected design,
implemented and merged via [PR #106](https://github.com/slmao/Atlantis/pull/106)):**
closes Lighting Foundation's own disclosed "no runtime light-update
capability" limitation above. `RuntimeApplication`'s own one-time-capture
guard (`lightingDataCaptured_`) is removed; the complete 176-byte
`FrameLightingData` is re-extracted from `World`'s live state and
republished every successful frame, at the exact call point already safe
without any new synchronization primitive or RHI API — this Spec's own
first draft proposed adding one, believing a real Camera/Lighting
write-timing race existed on the windowed path; a pre-drafting Plan
governance gate found that belief false (`VulkanPresentation::acquireNextTarget()`
already contains an unconditional, pre-existing drain, added during Plan
0006's own post-implementation GPU testing for an unrelated hazard, that
already closes it), and the Spec was corrected to this narrower design
before approval. `World::setLight()`, a Light's own local/parent
`Transform`, and Light entity creation/removal are all reflected on the
next successful frame — `World::updateTransforms()` already refreshed
every entity's own cached world matrix unconditionally, every frame,
before this point; only the guard around reading the result was removed.
The Camera(128-byte)/Lighting(176-byte) = 304-byte shared Buffer layout,
the existing uniform binding's own stage visibility, and every other
Spec 0019 decision are unchanged. Zero RHI/Renderer/Material public API
change; zero new synchronization primitive.
[ADR-0065](../../adr/0065-explicit-pre-write-submission-drain-for-frame-uniform-safety.md),
which would have recorded the originally-proposed new RHI method, is
`Rejected` — its own Decision was never implemented and must not be read
as a current architectural decision.

**Extension points:** a rename-stable GUID identity scheme and a real
derived-data cache are each named, explicitly out-of-scope future work
in Spec 0012/Spec 0015 — not designed or scaffolded here. A
distributable, cross-session Asset Catalog/Registry is likewise
explicitly deferred (Spec 0015's own Non-Goals) — the scene dependency
manifest Spec 0015 adds is build-tree-private, never a portable part of
any artifact.

---

## Atlantis World

**Status: Approved, implemented** (Spec 0014, ADR-0048–ADR-0051, all
`Accepted`) — this section states the real, built boundary.

**Responsibilities:** Atlantis's in-memory, multi-entity scene. `World`
is a slot map: entity lifecycle (`createEntity()`/`destroyEntity()`, an
index+generation `EntityId` handle formally overflow-safe via permanent
slot retirement at the generation counter's maximum value), a
mandatory `Transform` plus two optional components (`Camera`,
`Renderable`) directly on each entity's own record — not a generic,
type-erased ECS registry — an atomic parent/child hierarchy with cycle
prevention and cascading destroy, and an explicit `updateTransforms()`
producing each entity's own world matrix via a fully iterative
(non-recursive) traversal. `World` never mutates itself automatically;
every state change is caller-driven.

**Depends on:** Core, and, narrowly, Asset System (for the `AssetId`
type only, named in `Renderable`'s own two public fields — a mandatory
`meshAsset` and, since Spec 0018, an *optional* `materialAsset`). No
RHI, Renderer, RenderGraph, Shader System, Vulkan Backend, Platform,
Runtime, or Tools dependency in either direction — verified by an
include-scanning test (`tests/world/module_boundary_tests.cpp`), not
merely stated.

**Depended on by:** Runtime (the sole real `World` instance's owner) and
`tests/image_regression/`'s own headless fixture (an independent,
duplicated scene-construction/extraction copy, not a shared dependency
edge — see Runtime's own section below).

**Ownership:** `World` owns every entity and component outright; no
reference or pointer into its own internal storage ever crosses its
public API — every accessor returns by value. `EntityId` is a strictly
borrowed, non-owning handle: it carries a private, non-owning reference
to its own issuing `World` instance's heap-allocated, address-stable
identity token (never a writable public field, never serialized,
persisted, or used across a process boundary); a handle used against a
different, live `World` instance is rejected with `WorldError::WrongWorld`,
never silently misapplied to an unrelated entity. `World` itself is
move-constructible, not copyable, not move-assignable; a moved-from
`World` guarantees only that it remains destructible or may be
move-constructed from again.

**Public/private boundary:** public surface is the entity/component
accessor API, `updateTransforms()`/`getWorldMatrix()`, the public value
types (`EntityId`, `Transform`, `Camera`, `Renderable`, `WorldError`),
and, since Spec 0015, `fromValidatedSceneData()`
(`scene_instantiation.h`, deliberately not a `World` member function) —
two-pass, deterministic, genuinely infallible instantiation from Asset
System's own `ValidatedSceneData`, never persisting a scene-local node
index as `EntityId`. The slot map's own internal representation
(`Slot`, the opaque identity-token type) is private to `world.cpp`,
never declared in any public header.

**Extension points:** a rename/save-durable identity scheme and a
Tool/Editor protocol are each named, explicitly out-of-scope future
work in Spec 0014 — not designed or scaffolded here. Scene
serialization itself is Spec 0015's own scope (AssetSystem-owned, not
World-owned — `fromValidatedSceneData()` above is World's own, single,
narrow consumption point of it).

---

## Atlantis Runtime

**Status: Approved, implemented** (Spec 0013, ADR-0046/ADR-0047, all
`Accepted`; extended by Spec 0014, ADR-0048–ADR-0051, all `Accepted`) —
this section states the real, built boundary for the Windows windowed
path; Android/iOS remain architectural, not implemented (see Extension
points). **Further extended by Spec 0015 (`Approved`), ADR-0052–
ADR-0054 (all `Accepted`) — implemented and merged via
[PR #74](https://github.com/slmao/Atlantis/pull/74) (2026-08-23); the
scene-loading description below already reflects that PR's own code.
Further extended again by Spec 0018 (`Approved`), ADR-0059/ADR-0060
(both `Accepted`) — implemented and merged via
[PR #88](https://github.com/slmao/Atlantis/pull/88) (2026-08-28); the
Responsibilities/Ownership descriptions below already reflect that PR's
own code.**

**Responsibilities:** the actual composition root. A private
`atlantis_runtime_host` static library (`Atlantis::RuntimeHost`) owns
the object model, initialization sequence, per-frame orchestration, and
shutdown; a thin `atlantis_runtime` Windows executable contains only the
`main()` entry point that constructs and drives it. Owns a Platform
session for the OS being built (Windows Platform; Android Platform not
implemented), creates the RHI `Device` and (on the first `SurfaceCreated`
event) `Presentation` via the Vulkan Backend, loads both the
`minimal_mesh` and (Spec 0018) the `MaterialKind::UnlitTextured`
built-in Shader-System-compiled shader pairs, and reads a real scene
asset's own dependency manifest, decodes its `ValidatedSceneData`,
resolves and loads every distinct mesh it references (in ascending
first-reference order, never `AssetId`-sorted order — a keyed
`meshResourceMap_`, not a single hardcoded asset) — since Spec 0018,
also resolves and loads every distinct material an entity's
`Renderable` names (and each material's own referenced texture,
deduplicated by texture `AssetId`) into two further CPU-only maps, all
published atomically together — and instantiates the one real `World`
instance via `atlantis::world::fromValidatedSceneData()` — replacing
the former fixed, hardcoded six-entity validation scene Spec 0014
shipped. Each frame: calls `World::updateTransforms()`, extracts the
active camera's view/projection matrices, computes which referenced
materials are not yet GPU-realized and realizes them (Spec 0018 — a
new, independently-testable `material_realization.h`/`.cpp` module;
each realized material's own texture-upload RenderGraph pass is
recorded into the same `CommandList` the draw graph below also uses,
before it), and builds one `DrawItem` per renderable entity via a
Runtime-private adapter (`scene_extraction.h`/`.cpp` — eye/forward-only
camera extraction, never a right/up column, so it stays correct under a
sheared hierarchy; per-entity mesh `AssetId` resolution against
`meshResourceMap_`'s own key set — a keyed-map membership check,
replacing the project's own former single hardcoded asset comparison;
per-entity material resolution, since Spec 0018, through a priority
chain — a pending format-rebuild candidate, then a material realized
this same frame, then the persistent map — falling back to a single
hardcoded `Material` only when `materialAsset` is absent, and skipping
the entity for this frame only, never substituting the fallback, when
it is present but not yet realized), acquires a `RenderTarget` from
`Presentation`, hands the `DrawItem`s to `Renderer`, submits (one
`CommandList`, one `submit()` covering any upload passes plus the draw),
then presents. On a color-format change, Spec 0018 rebuilds a complete
*candidate* batch (the fallback `Material` plus every existing
per-material entry) without touching the live bundle, records this
frame's own draw graph against the candidate only, and swaps the
candidate in only after this frame's own `submit()` returns `Ok` —
never destroying the old bundle while GPU work that could still
reference it (the previous frame's) might not yet have finished, and
never mixing an old-format `Pipeline` with a new-format `RenderTarget`
on a rebuild failure (an unconditional empty `DrawItem` list for that
one frame instead). Also responds to
Platform-delivered lifecycle events;
Android-specific lifecycle handling (surface destroyed/recreated, app
paused/resumed) remains TBD, see Open Questions in
[threading.md](threading.md).

**Depends on:** Atlantis Platform, RHI (Device + Presentation), Renderer,
Shader System (both targets), Asset System, World, Core. **Not**
RenderGraph directly — `Renderer::drawFrame()` already owns RenderGraph
construction/compilation/execution internally, confirmed by inspection
that no `atlantis/render_graph/*.h` header is included anywhere under
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
convention. `Device`, `Presentation`, a keyed map of every loaded
`Mesh` (`meshResourceMap_`, since Spec 0015 occupying the single `Mesh`
member's own former declaration slot, so the guarantee below is
unchanged, not newly invented), the camera `Buffer`, the depth
`Texture`, and (since Spec 0018) four material-related members in place
of the former single `Material` — a texture-`AssetId`-keyed
`SampledTexture` map, a material-`AssetId`-keyed `Sampler` map, a
material-`AssetId`-keyed `Material` map, and the single fallback
`Material` — are owned in that reverse order below it, each held as a
`std::unique_ptr<T>` map *value* so a borrowed raw pointer into any of
them is address-stable regardless of map rehash/move; the declaration
order keeps every `SampledTexture`/`Sampler` a `Material` might borrow
destroyed strictly after that `Material`, matching `fallback Material →
per-material Materials → Samplers → SampledTextures → Texture → Buffer
→ Mesh(es) → Presentation → Device → PlatformSession`/window as the
fixed destruction sequence.
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
