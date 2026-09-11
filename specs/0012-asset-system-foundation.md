# Spec: Asset System Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Created:** 2026-08-18
- **Historical scope:** Two independent, agent-performed review rounds
  (2026-08-18) preceded Human Review — see
  [PR #55](https://github.com/slmao/Atlantis/pull/55) for the full
  revision history. Round 1 applied direct mechanical fixes (Asset-ID
  deterministic-but-not-rename-durable terminology; a path-normalization
  contract requirement; a hash-collision fail-fast requirement; a
  runtime-artifact byte-order disclosure; a composition-root
  clarification). Round 2, at the human maintainer's direction, split the
  original combined module-boundary ADR into
  [ADR-0043](../adr/0043-asset-system-module-boundary.md) (module boundary)
  and
  [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format / versioning / dependency policy), and completed the
  path-derived Asset ID's full implementable contract in
  [ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md).
- **Human Review Approval (2026-08-19):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`). **This approval explicitly accepts:**
  1. **A new, tenth top-level module, Atlantis Asset System**, depending
     on Atlantis Core alone (ADR-0043).
  2. **A path-derived, deterministic, explicitly non-rename-durable Asset
     ID**, with the logical-path normalization rules, ASCII-only Phase 1
     character set, 64-bit FNV-1a hash, serialization, and error semantics
     fixed in ADR-0044.
  3. **Hand-rolled, versioned, dependency-free authoring/metadata/
     runtime-artifact formats** (ADR-0045).
  4. **CMake `DEPENDS`-based re-import triggering, with no derived-data
     cache** built by this spec.
  5. **A static position/color mesh as the first and only asset type**,
     verified against Spec 0011's already-committed golden.
  6. **No new third-party dependency** — no glTF/Assimp, UUID, hashing, or
     JSON/YAML/TOML library.
  7. **No Runtime dependency, and no Android, hot-reload, asynchronous
     streaming, virtual file system, or general serialization framework**
     in this spec's scope.

  **Three corrections were directed and applied before this approval:**
  1. **Dependency correction.** Asset System depends on Atlantis Core
     only. The importer/cooker and the runtime artifact loader produce and
     consume a strict CPU-side `StaticMeshAssetData`; the composition root
     (a test, an example, or a future Runtime) owns the GPU handoff,
     resolving the `VertexInputLayout` through Shader System's public
     `rhi_integration` surface and calling the existing, unmodified
     `atlantis::renderer::createMesh()` itself. Tools depends on Asset
     System, never the reverse. No Renderer-integration submodule is
     introduced; if implementation proves one unavoidable, that is a new
     architectural question to raise, not resolve silently. All language
     implying Asset System itself creates GPU meshes or names RHI/Renderer
     types was removed.
  2. **Byte-order correction.** The runtime artifact's on-disk format is
     **unconditionally little-endian** — a fixed property of the format,
     not of the writing host. Both currently-supported targets (x86-64
     Windows, ARM/AArch64 Android ABI) are natively little-endian, so a
     conforming implementation needs no byte swapping in practice; a
     future big-endian host would be required to swap, not permitted to
     write host-endian bytes. The rule governs the Asset ID, header
     fields, floats, and indices alike.
  3. **Collision-detection correction.** The collision guarantee is scoped
     to what is implementable without a global asset database: within the
     asset set declared to a single importer/validator invocation, Asset
     ID collisions and case-only-differing logical paths must each be
     detected and returned as a distinct `Err`. This spec makes **no**
     claim of repository-global collision detection across independent
     build invocations. The Plan must arrange a validation step whose
     declared input set covers every asset the repository declares; a
     future asset registry/database spec must re-establish global
     uniqueness on its own terms.

  All three ADRs move to `Accepted` alongside this approval. This approval
  authorizes drafting a Plan; it does not itself authorize Implementation.
- **Related Plan(s):**
  [plans/0012-asset-system-foundation.md](../plans/0012-asset-system-foundation.md)
  (`Approved`).
- **Related ADR(s):**
  [ADR-0043](../adr/0043-asset-system-module-boundary.md) (module boundary
  and dependency boundary),
  [ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md)
  (asset identity, provenance, import/re-import methodology),
  [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format, versioning, third-party dependency policy) — all three
  `Accepted` 2026-08-19.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  Batch 4 PR (pending). Original scope and obligations retained; the
  round-by-round independent-review narration is preserved in PR #55
  history.

## Summary

This spec proposes Atlantis's first Asset System: a foundation for
turning checked-in, human-authored **authoring source** into a versioned,
deterministic **runtime artifact**, loaded into CPU-side data that a
composition root can hand to existing Renderer code — with no scene
graph, no asset database, no package system, and no change to any
existing Renderer/RHI/Vulkan public API. The module depends on Atlantis
Core alone and never touches a GPU type; the GPU handoff belongs to
whichever test, example, or future Runtime composes the frame. It fixes
asset identity, a metadata schema, the authoring/runtime separation
ADR-0035 requires every future data-model spec to address explicitly, and
a deterministic, dependency-tracked import pipeline — proven by one real,
minimal closed loop: importing the exact cube mesh
`examples/headless_rendering_demo` already hand-authors, and rendering it
through the existing, unmodified `Renderer` → RenderGraph → RHI → Vulkan
Backend stack to byte-identical output.

## Motivation / Problem Statement

Every mesh Atlantis renders today is a hand-authored C++ constant.
[specs/0007-minimal-renderer.md](0007-minimal-renderer.md)'s Non-Goals
fixed this deliberately. Every example and test built since
(`minimal_renderer_demo`, `headless_rendering_demo`,
`tests/image_regression/fixture/minimal_cube_fixture.cpp`) duplicates the
identical `kCubeVertices`/`kCubeIndices` arrays byte-for-byte, by design.
There is no way to author mesh content outside a C++ translation unit, no
stable way to name a piece of content independent of its source file's
path, and no versioned, checked boundary between what a human authors and
what the GPU consumes.

[specs/README.md](README.md)'s Candidate Spec Backlog names this gap as
"Asset System Foundation," depending only on Spec 0007 (`Approved`,
implemented) — satisfied. It does **not** list Runtime as a dependency
(see "Why this does not wait for Runtime").

[specs/0009-long-term-engine-architecture-alignment.md](0009-long-term-engine-architecture-alignment.md)
and its `Accepted` ADRs bind several things this spec must do explicitly
rather than default into: ADR-0032 requires every future Spec that
introduces or restructures a top-level module to decide that module's
position in both architectural views explicitly; ADR-0034 names Asset as
one of the future Specs whose externally-stable public boundary must be
expressed through schema/identity/protocol concepts, never internal C++
layout; ADR-0035 requires this spec to explicitly address whether its
authoring-facing and runtime-execution representations are the same
structure or two related-but-distinct ones, and if distinct, name the
transformation step.

**A note on Candidate numbering:** Spec 0009 and ADR-0033/0035 were
written while Headless Rendering and Image Regression Testing still
occupied Candidate Order 2 and 3; both have since become Specs 0010/0011,
shifting every later entry up by two. What those documents call "Candidate
4 (Asset System Foundation)" is the same working item now at Candidate
Order 2 — the identity is unchanged, only its numeric position.

### Why this does not wait for Runtime

Three independent, already-reviewed sources converge on "not blocked":
(1) the Candidate Backlog's own "Depends On" column names only Spec 0007;
(2) Spec 0009's Roadmap Impact discusses Asset System only in relation to
Serialization/Stable Identity ("plausibly benefits... but is not strictly
blocked"), never Runtime; (3) a real, already-shipped precedent — Atlantis
Shader System (Spec 0008) is a complete
authoring-source → build-time-compiled-artifact → versioned-schema →
runtime-consumption pipeline, built and shipped entirely without Atlantis
Runtime existing. `docs/architecture/module_boundaries.md` confirms
Runtime is structurally a leaf even once it exists ("**Depended on by:**
nothing (it's the executable)") — a future Runtime will consume Asset
System's runtime-loading API the same way it consumes Renderer's, not the
other way around.

This spec's minimal closed loop is, like Spec 0003's and Spec 0010's
verification compositions, **explicitly not a preview of Runtime** — the
same disclosed-non-preview framing ADR-0033's own Context establishes for
this repository's prior non-shipping compositions.

## Goals

- Fix **asset identity**: what names a piece of authoring content
  deterministically, and what does *not* survive an edit/rename — stated
  as an explicit, disclosed property.
- Fix a **metadata schema**: what provenance/version information every
  imported asset records, in a strict, versioned, dependency-free format.
- Fix the **authoring source / runtime artifact separation** ADR-0035
  requires: name whether they are the same representation or two distinct
  ones, and if distinct, name the transformation step.
- Fix **deterministic import**: the same authoring source, imported twice,
  produces byte-identical runtime artifact output — verified empirically,
  per this project's precedent (ADR-0031, ADR-0042's Context).
- Fix **dependency tracking and re-import triggering** — when a re-import
  must happen and how it is detected — without inventing a bespoke
  derived-data cache/database unless analysis shows one is actually needed
  at this scope.
- Decide **Asset System's ownership** within Atlantis's nine-module
  structure (ADR-0032) — new top-level module, or hosted by existing
  modules — as an explicit Human Review decision.
- Fix the **dependency boundary** between Asset System and Tools, Core,
  Renderer, Shader System, and a future Runtime.
- Deliver **one real, minimal, verifiable asset-consumption closed loop**.
- State the principle that a runtime artifact this pipeline produces is
  usable in principle by both Atlantis's current Windows environment and a
  future Android target sharing the same Vulkan Backend — **without
  implementing, designing, or verifying anything Android-specific**.
- Fix Phase 1 baseline non-functional properties: single-threaded, RAII
  ownership, explicit `Result`/error types (never exceptions on any path a
  runtime consumer calls), and no global mutable asset database or
  registry.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Runtime Host, World/ECS, Editor, hot-reload, or asynchronous
  streaming of any kind.** The import pipeline is a synchronous,
  offline/build-time or explicitly-invoked step, never a live-reload or
  background-streaming system.
- **A package system, virtual file system, remote/network asset source,
  or any UGC capability.** Every authoring source is a checked-in file at
  a fixed, known repository path.
- **Android, iOS, or Linux implementation of any kind.** This spec states
  a *principle* that its runtime artifact format is platform-agnostic in a
  way that does not preclude Android reuse later — it implements, builds,
  and verifies the Windows path only. Linux is not a target platform for
  Atlantis at all.
- **A general-purpose serialization framework.** The metadata sidecar and
  runtime artifact formats are narrow, hand-rolled, scoped to exactly the
  fields this spec's one asset type needs.
- **GPU-driven rendering, bindless resources, or any texture-system
  extension.** In particular, no general shader-read/sampled `Texture`
  type or `Sampler` type is added to RHI (verified absent today —
  `texture.h`'s `Texture` interface exposes only `DepthFormat format()`;
  no `Sampler` type exists anywhere in `src/`); no multi-draw, indirect,
  or instanced draw capability. See "Why the first asset type is a mesh,
  not a texture."
- **Any change to an existing Renderer, RHI, or Vulkan Backend public
  API.** Asset System does not depend on those modules; the composition
  root feeds this spec's CPU-side output into
  `atlantis::renderer::createMesh()`'s **existing, unmodified** signature
  — no new parameter, no new overload, no new public type. If this spec's
  design work had found a genuine need to change one of those APIs, it
  would stop and raise that explicitly — it did not.
- **An Asset System ↔ RHI/Renderer integration submodule** of the kind
  Shader System needed (`Atlantis::ShaderSystemRhiIntegration`). Asset
  System's output is plain CPU data `createMesh()` already accepts
  unchanged (ADR-0043).
- **A rename-stable, sidecar-backed GUID identity scheme.** Named as
  explicit future work, not designed now.
- **A general, content-hash-keyed derived-data cache/database.** This
  spec's own scope (one asset type, one build target, one developer
  machine at a time) is deliberately proven not to need one.
- **Any second or third asset type beyond the one this spec's closed loop
  proves** (a static triangle mesh with per-vertex position and color,
  matching the existing `minimal_mesh` shader's vertex layout).
- **`glTF`, `Assimp`, or any external 3D interchange format parser**
  (ADR-0045).
- **Multi-threaded import, a job/task system, or any importer
  parallelism** (ADR-0004's single-logical-thread baseline, unchanged).

## Requirements

### Functional

**Asset identity**

- Every importable authoring source is identified by a deterministic
  **Asset ID** — a value other content can reference without embedding a
  raw file path, computed the same way every time for the same input.
  This spec fixes the identity *scheme* (ADR-0044); the exact bit width
  and encoding are ADR-0044's Decision. **Determinism holds across
  repeated imports and across unrelated edits elsewhere in the source tree
  — it does not, by itself, mean the identifier survives a rename or move
  of its own source file.** This spec deliberately avoids calling the
  scheme "stable" without that qualifier.
- The chosen scheme's limitations (identity does **not** survive a source
  file rename/move) are stated explicitly, as a disclosed Phase 1 scope
  boundary.
- **The path this scheme hashes is normalized by one fixed,
  platform-invariant rule, fixed in full as ADR-0044's Decision** — a
  fixed asset-source root with a `/`-separated logical path relative to
  it; absolute paths and Windows drive prefixes rejected outright;
  `.`/`..` segments lexically resolved with any root-escaping `..`
  rejected; case preserved and hashed case-sensitively; an ASCII-only
  character set for Phase 1 (see Risks & Open Questions item 7 for the
  full contract) — so that Windows and a future Android cook path compute
  the *same* Asset ID for the *same* logical authoring source. This is
  required by, not separate from, the Windows-now/Android-later
  artifact-sharing principle below.
- **A hash collision (two distinct source paths producing the same Asset
  ID) is possible in principle**, since the hash (FNV-1a, 64-bit) is
  non-cryptographic and the space of possible paths is unbounded, though
  practically negligible at this project's realistic scale. **Within the
  set of assets declared to a single importer/validator invocation**, an
  Asset ID collision, and a pair of logical paths differing only by case,
  must each be detected and reported as a distinct `Err` — never silently
  merged, never resolved by whichever entry the filesystem or build system
  supplied first.
- **This spec does not claim repository-global collision detection.** With
  no asset registry/database and no derived-data cache, nothing observes
  assets never presented together in one invocation. The Plan must arrange
  a validation step whose declared input set covers every asset the
  repository declares (CMake organization a Plan-stage decision); a future
  asset registry/database spec inherits the obligation to re-establish
  global uniqueness (ADR-0044).
- **Asset ID is a distinct concept from a cache/rebuild key.** An Asset ID
  names *what* a piece of content is, deterministically; a cache key
  determines *when* a re-import must happen, and legitimately changes
  whenever source content, importer version, or any dependency changes —
  conflating the two is a real, disclosed risk this spec's design avoids
  (ADR-0044).

**Metadata schema**

- Every imported asset has an accompanying **metadata record** stating, at
  minimum: its Asset ID; its source-authoring-file's identity (path,
  relative to a fixed asset-source root); the importer's own
  version/identity; a `schema_version` field; and per-asset-type fields
  (vertex/index counts, the vertex layout it was imported against).
- The metadata format is a **strict, versioned, dependency-free flat text
  format** — the exact encoding is ADR-0044's Decision. Bounded the same
  way ADR-0042's own "Sidecar encoding and parsing" bounded the
  image-regression sidecar: no new parsing/serialization dependency
  without its own Spec/ADR/Human Review; a stable, fixed field order; an
  explicit version marker. This spec's metadata parser is **new,
  independent code** — it must not depend on, link against, or copy
  private implementation from `tests/image_regression/support/provenance.*`;
  the two sidecar formats may share a *pattern* (flat, versioned,
  anchored-prefix-parsed text), never a code dependency.

**Authoring source / runtime artifact separation (ADR-0035 compliance)**

- This spec **explicitly separates** authoring-facing from
  runtime-execution representation for its one asset type — they are not
  the same data structure. The authoring source is a small,
  human-readable, hand-rolled text format a human can author and diff in
  an ordinary PR; the runtime artifact is a small, versioned binary format
  that loads into the CPU-side `StaticMeshAssetData` structure (raw vertex
  bytes plus a `std::uint16_t` index array and their counts) — a shape
  chosen because a composition root can hand it to the existing,
  unmodified `atlantis::renderer::createMesh()` with no conversion step,
  though Asset System itself never makes that call.
- The **transformation step** connecting them is a deterministic
  **importer/cooker**: a build-time-or-explicitly-invoked tool (exact
  invocation model — build-integrated like Shader System's `slangc` step,
  or standalone like Image Regression's golden generator — is ADR-0044's
  Decision) that reads one authoring source file and produces one runtime
  artifact plus its metadata sidecar.
- This spec does **not** claim its transformation step is structurally
  equivalent to whatever a future World/ECS Spec's authoring→runtime bake
  turns out to need — per ADR-0035's Context, that Spec's multi-object
  dependency graph, editor round-tripping, and incremental-update needs
  are not solved or assumed solved here.

**Deterministic import, dependency tracking, and re-import triggering**

- Importing the same authoring source twice, with the same importer
  version, produces **byte-identical** runtime artifact output — proven
  empirically as part of this spec's Testing & Verification Plan.
- Re-import is triggered whenever the authoring source file's content
  changes, or the importer's own version/implementation changes — the
  exact mechanism (a real derived-data cache vs. reusing CMake's own
  incremental-build dependency tracking) is ADR-0044's Decision.
- **No asset is ever silently regenerated by an ordinary build/test run in
  a way that changes a checked-in file without a human noticing** —
  mirroring ADR-0042's own "never a silent regeneration step that a CI job
  runs and commits automatically" rule.

**Module ownership and dependency boundaries**

- This spec fixes Asset System's position in Atlantis's nine-module
  source-ownership structure — new tenth top-level module, or hosted by
  existing Tools/Core/Runtime boundaries — as an explicit Human Review
  decision (ADR-0043).
- Whatever the outcome, the following boundaries hold, verified against
  the real dependency structure of every module named:
  - **Depends on Atlantis Core only** (`Result<T,E>`, logging, assertions)
    plus the C++ standard library. Asset System does **not** depend on
    RHI, Renderer, Vulkan Backend, RenderGraph, Shader System, Platform,
    Runtime, or Tools; no GPU, windowing, graphics-API, or OS-process
    concept appears in its public surface or implementation.
  - **Produces and consumes CPU-side data, never GPU resources.** The
    importer/cooker writes, and the runtime artifact loader reads, a
    strict CPU-side static-mesh data structure (working name
    `StaticMeshAssetData`; exact naming/layout a Plan-stage detail)
    carrying vertex bytes, the index array, and interpretation metadata.
    Asset System never creates a `renderer::Mesh`, never calls
    `renderer::createMesh()`, never constructs an `rhi::Buffer`, and never
    names an `rhi::VertexInputLayout` (ADR-0043).
  - **The composition root owns the GPU handoff.** A test, an example, or
    eventually a future Atlantis Runtime calls Asset System's loader for
    `StaticMeshAssetData`, separately resolves the `VertexInputLayout`
    through Atlantis Shader System's public `rhi_integration` surface
    (`atlantis::shader_system::rhi_integration::toVertexInputLayout()` —
    never Shader System's private JSON parser or implementation), and
    passes both into the existing, unmodified
    `atlantis::renderer::createMesh()`. This is exactly what
    `headless_rendering_demo` and `minimal_cube_fixture.*` already do with
    hand-authored arrays; this spec replaces the *source* of that CPU
    data.
  - **No Renderer-integration submodule is introduced.** If implementation
    ever proves this unavoidable, that is a real architectural change to
    raise explicitly (ADR-0043).
  - **Renderer, RHI, Vulkan Backend, RenderGraph, and Shader System gain
    no new dependency, in either direction.**
  - **Atlantis Tools** hosts the importer/cooker's command-line entry
    point — matching `module_boundaries.md`'s `PROPOSED` description of
    Tools' responsibilities as including "asset processing," and Shader
    System's precedent of a Tools-hosted CLI invoking a separate module's
    library code. **The dependency runs Tools → Asset System only.** Asset
    System's runtime-loading code lives in the Asset System module itself,
    not under Tools (ADR-0043).
  - **No dependency on a future Atlantis Runtime module.**

**First asset-consumption closed loop**

- This spec's implementation must import one real, checked-in authoring
  source file — the same cube geometry
  `examples/headless_rendering_demo`'s `kCubeVertices`/`kCubeIndices`
  already hand-authors — through its own importer/cooker, and load the
  resulting runtime artifact into `StaticMeshAssetData` through a new
  runtime-loading API. **The composition root** (a test or example) then
  feeds that CPU data into the **existing, unmodified**
  `atlantis::renderer::createMesh()` call — Asset System itself makes no
  such call.
- The loaded, imported mesh, rendered through the existing, unmodified
  `Renderer` → RenderGraph → RHI → Vulkan Backend stack with the same
  fixed camera/material this project's reused fixture already uses, must
  produce **pixel-identical output** to the existing hand-authored fixture
  — verified using Atlantis's own, already-`Approved` Image Regression
  Testing harness (Spec 0011), not a new bespoke comparison mechanism.

**Windows-now, Android-later artifact-sharing principle**

- This spec's runtime artifact format is designed to carry no
  Windows-specific byte order, alignment, or path assumption beyond what
  Vulkan's own buffer/vertex-format conventions already require
  identically on both of Atlantis's primary target platforms — stated as
  a design **principle** future Android work can rely on, not a claim that
  this spec builds, tests, or verifies anything on Android. No Android
  build target, CMake toolchain file, or NDK dependency is added.

### Non-functional

- **Performance:** not a goal beyond "does not stall or leak
  unnecessarily." Import-time cost is not evaluated at this spec's
  one-fixture scale.
- **Memory:** no new allocation strategy. Imported/loaded asset data is
  ordinary heap memory, owned by whichever caller requested the load
  (matching ADR-0015's own precedent for RHI resources). No streaming,
  paging, or partial-load design.
- **Portability (within the Vulkan-only Phase 1 constraint):** Windows
  only for this spec's build/test/verification; the runtime artifact
  *format* is designed, as a stated principle, not to preclude a future
  Android cook/consume path.
- **Threading:** single-threaded (ADR-0004). The importer/cooker runs on
  one thread; the runtime loader is called from the same single logical
  thread every other RHI/Renderer call assumes. No thread-safety contract
  beyond "not thread-safe" is claimed.
- **Ownership:** RAII throughout — every owning type has one clear owner,
  released deterministically on destruction. No manual, caller-remembered
  cleanup step.
- **Error handling:** recoverable runtime errors (a missing authoring
  source file, a malformed metadata sidecar, an importer producing invalid
  output, a runtime artifact that fails to parse) use
  `atlantis::Result<T, E>` — never an exception — on every path a runtime
  consumer (anything other than the offline importer/cooker tool itself)
  calls. Whether the offline tool's own internal implementation may use
  exceptions is left to the Plan, per AGENTS.md's "offline tooling... is
  left to that module's own spec" allowance — no bearing on any
  runtime-consumed API.
- **No global mutable asset database.** Every loaded asset is owned by an
  explicit, caller-held value/handle — never a global, static, or
  singleton registry a caller looks up by ID without being handed
  ownership or a borrowed reference. This satisfies AGENTS.md's "No global
  mutable engine-state singletons" rule and is consistent with ADR-0033's
  future "Runtime is the sole authoritative owner of engine *world* state"
  principle — an in-memory loaded-asset cache, if the Plan needs one, is
  caller-owned and explicitly passed, never a process-wide singleton, and
  never framed as *world state*.

## Proposed Design

### Pipeline shape

```
=== Atlantis Asset System (new module; depends on Core ONLY) ===========
Authoring source (checked in, human-readable, hand-rolled text format)
  |
  v
Importer/cooker library  <-- invoked by a Tools-hosted CLI
  (deterministic; reads exactly one source file per invocation)
  |
  +--> Runtime artifact (versioned binary, little-endian: header +
  |     raw vertex bytes + std::uint16_t index array)
  |
  +--> Metadata sidecar (versioned flat text: Asset ID, source identity,
        importer version, per-asset-type fields)
        |
        v
Runtime artifact loader (Result-returning; no exceptions)
  |
  v
StaticMeshAssetData  <-- CPU-side data. Asset System's OUTPUT BOUNDARY.
  |                      No RHI/Renderer type crosses this line.
=== end Asset System ===================================================
  |
  v
Composition root (a test, an example, or eventually a future Runtime) --
  owns the GPU handoff; separately resolves VertexInputLayout via
  shader_system::rhi_integration::toVertexInputLayout()
  |
  v
atlantis::renderer::createMesh()  <-- EXISTING, UNMODIFIED public API
  |
  v
Renderer -> RenderGraph -> RHI -> Vulkan Backend (all EXISTING, unmodified)
  |
  v
Rendered frame, compared against the existing hand-authored fixture's
own already-committed golden (Spec 0011, tests/image_regression/)
```

### Why the first asset type is a mesh, not a texture

RHI's current `Texture` type (`src/rhi/include/atlantis/rhi/texture.h`)
exposes exactly one accessor beyond `extent()`: `virtual DepthFormat
format() const`. There is no general shader-read/sampled texture type, no
`Sampler` type, and no descriptor-binding surface for a texture resource
anywhere in `src/` today — confirmed by direct inspection.
[specs/0007-minimal-renderer.md](0007-minimal-renderer.md)'s Non-Goals
fixed this deliberately, and no spec since has revisited it. Choosing a
texture would require adding real, new RHI/Renderer public API surface —
exactly what this spec's Non-Goals rule out unless raised as its own
explicit architectural question first. A static mesh needs **zero** new
RHI or Renderer API: raw vertex bytes plus a `std::uint16_t` index array
is exactly what `atlantis::renderer::createMesh()`'s existing signature
already accepts.

### Module boundary (see ADR-0043)

**A new, tenth top-level module, `Atlantis Asset System`**, mirroring
Shader System's precedent almost exactly — an authoring format, a
Tools-hosted CLI, and a small, Core-only library other code links
against. The reasoning is structural: `module_boundaries.md`'s text
states, for both candidate hosts, "**Depended on by:** nothing" — both of
Atlantis's only two not-yet-`Accepted` candidate hosts (Tools, Runtime)
are structural **leaves** that nothing may link against as a library, and
this spec's runtime-loading half must be linkable by a future consumer.
Core is scoped to "non-graphics utilities" — Asset System's logic is
non-graphics but is a substantial, independently-versioned subsystem with
its own authoring format, artifact schema, identity scheme, and CLI, not a
small shared utility; folding it into Core would make Core the owner of an
entire asset pipeline every other module transitively inherits. Shader
System itself was not folded into Tools' "asset processing" language for
exactly this reason: a module with a real, versioned, runtime-consumed
public schema earns its own top-level position. Under this decision,
Renderer, RHI, Vulkan Backend, RenderGraph, and Shader System are entirely
unchanged in both directions. The alternative (hosting the importer/cooker
under Tools and finding another home for the runtime-loading half) is
named in Alternatives Considered with its own cost. Human Review accepted
the new-module decision on 2026-08-19.

## Architectural Impact

Three new architectural decisions, each recorded as its own ADR — all
three `Accepted` 2026-08-19 alongside this spec's Human Review Approval.
The module-boundary decision and the data-format/dependency decision were
originally drafted as one combined ADR and split into two at the human
maintainer's request, since the two are independently evolvable (ADR-0043
Revision History).

- [ADR-0043](../adr/0043-asset-system-module-boundary.md) — Asset System
  Module Boundary. Covers: new top-level module versus existing-module
  combination; Asset System's **Core-only** dependency; the CPU-side
  `StaticMeshAssetData` output boundary; the composition root's ownership
  of the GPU handoff; the one-directional Tools → Asset System edge.
- [ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md)
  — Asset System Identity, Provenance, and Import Methodology. Covers: the
  Asset ID scheme and its disclosed limitations, its full implementable
  contract (asset-source-root-relative logical path definition;
  separator/absolute-path/`.`/`..`/empty-segment/Windows-drive-prefix
  handling; case-sensitivity; ASCII-only Phase 1 character set; concrete
  hash algorithm FNV-1a 64-bit, byte serialization, and collision-detection
  rule — see Risks item 7); the metadata schema's exact field semantics;
  the deterministic-import and re-import-triggering mechanism (this spec's
  recommendation: no bespoke cache, reuse CMake's existing incremental-build
  dependency tracking); the golden-update-style human-review discipline for
  any checked-in imported artifact.
- [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  — Asset System Data Format, Versioning, and Third-Party Dependency
  Policy. Covers: the authoring-source, runtime-artifact, and
  metadata-sidecar wire-encoding formats and their mandatory
  `schema_version` fields; and whether any new third-party dependency (a
  model-format parser, a hashing library, a serialization library) is
  justified (this spec's recommendation: no).

**ADR-0035 compliance, stated explicitly:** this spec's authoring-facing
representation (a hand-rolled text mesh source) and its runtime-execution
representation (a versioned binary vertex/index artifact) are **two
distinct data structures**, connected by an explicit importer/cooker
transformation step. This spec does not claim its transformation step
generalizes to a future World/ECS Spec's structurally different bake
problem.

**ADR-0034 compliance:** this spec's externally-stable public boundary is
its Asset ID scheme and versioned metadata/artifact-format schema — never
the importer's internal C++ parsing/cooking types, none of which is
exposed publicly.

**ADR-0032 compliance:** this spec places Asset System, `Accepted` as a
new module, in the "Runtime Services" boundary of the conceptual
five-layer view (per
[engine_architecture.md](../docs/architecture/engine_architecture.md)'s
illustrative, non-binding mapping) and as a new, tenth top-level module in
the authoritative source-ownership view, **depending on Atlantis Core
alone**, and depended on by nothing yet.

**ADR-0033 is not implicated by this spec.** ADR-0033 governs
authoritative *world state* and Client access to it. This spec's loaded
assets are immutable, versioned, content-addressed-by-identity data, not
mutable world state; nothing in this spec's design claims or requires
exclusive/authoritative ownership of anything a future Runtime would need
to arbitrate.

**No change to any existing `Accepted` ADR or `Approved` Spec.** In
particular `atlantis::renderer::createMesh()`, RHI's
`Buffer`/`VertexInputLayout` types, and RenderGraph/Vulkan Backend's
public surfaces are all consumed exactly as they exist today.

## Alternatives Considered

- **Host both the importer/cooker and the runtime-loading library under
  Atlantis Tools**, matching `module_boundaries.md`'s draft language.
  Rejected as this spec's recommendation (though named as a real Human
  Review option, per ADR-0043): Tools is "depended on by: nothing," which
  structurally forecloses a future Renderer-adjacent consumer linking
  against Tools-hosted runtime-loading code without first reopening Tools'
  boundary.
- **Fold runtime loading directly into Renderer.** Rejected: Spec 0007's
  Non-Goals explicitly excluded "a scene graph, ECS, asset system, or a
  model/mesh loader" from Renderer's scope.
- **Wait for a future Runtime Host Spec or Serialization/Stable Identity
  Spec.** Rejected — see "Why this does not wait for Runtime."
- **Adopt `glTF` (via `cgltf`, `tinygltf`, or Assimp) as the authoring
  source format.** Rejected as this spec's default recommendation, not
  ruled out for a future spec with a real multi-mesh/multi-material
  workflow: this spec's one-fixture scope needs none of glTF's
  scene-graph/material/animation/skinning surface, and adopting a
  ~20,000+ line parser (or Assimp) to read eight hand-known vertices would
  be exactly the kind of dependency this project's discipline (ADR-0006,
  ADR-0041) requires justifying against a real, present need. See ADR-0045
  for the full license/build/maintenance analysis.
- **A random-GUID, sidecar-backed identity scheme.** Named as real future
  work, not adopted now: it requires a durable place to persist the
  assigned GUID (a `.meta`-style sidecar, itself new authoring-adjacent
  state to keep consistent under rename/move/delete), with no concrete
  second-asset-type or multi-author-workflow need yet to justify it. See
  ADR-0044.
- **A content-hash-derived identity scheme** (hashing the authoring
  source's own bytes). Rejected as an *identity* scheme specifically —
  identity would change on every content edit. This spec recommends a
  content-hash-based **cache key** as the right tool for cache
  invalidation, a distinct question from identity.
- **A bespoke, content-hash-keyed derived-data cache/database.** Rejected
  at this spec's scope: with one asset type and one build target, CMake's
  own `add_custom_command()`/`DEPENDS` mechanism (`Accepted` precedent,
  ADR-0029) already provides correct invalidation with zero new code. A
  real shared cache becomes justified once a second cook target (a future
  Android import path) or a shared build farm creates genuine need.

## Testing & Verification Plan

States what this spec's future Plan/Implementation must verify — this Spec
document introduces no code.

- **Unit tests (GPU-independent):**
  - The importer/cooker, run twice against the same authoring source with
    the same version, produces byte-identical runtime-artifact and
    metadata-sidecar output (ADR-0031/ADR-0042's empirical-determinism
    discipline).
  - Metadata-sidecar parsing: a well-formed sidecar parses correctly and
    round-trips through its own serializer byte-for-byte; each
    strict-parsing failure mode (wrong field count, wrong field name at a
    position, unknown `schema_version`, malformed value) returns a
    distinct, expected `Err`.
  - Runtime-artifact loading: a well-formed artifact loads into a
    correctly-populated `StaticMeshAssetData`; a malformed/truncated
    artifact is rejected with a distinct `Err`, never a crash or a silent,
    partially-populated result. These tests link Asset System and Core
    only — no `Device`, no GPU, no RHI/Renderer target required, a direct
    testable consequence of the Core-only module boundary.
  - Runtime-artifact byte order: an artifact written on the
    (little-endian) development host is byte-for-byte identical to a
    fixed, checked-in expected-bytes vector, confirming the format's
    unconditional little-endian contract rather than whatever the host
    happened to emit.
  - Asset ID computation: deterministic for a fixed source path;
    documented, tested behavior for the disclosed rename/move limitation
    (an Asset ID changing after a rename is expected, tested behavior —
    not a bug); each rejected path form (absolute path, Windows drive
    prefix, root-escaping `..`, non-ASCII byte) returns its own distinct
    `Err`.
  - Collision detection, **scoped to one invocation's declared asset
    set**: a deliberately constructed Asset ID collision, and a
    case-only-differing pair of logical paths, are each detected within a
    single importer/validator invocation and rejected with a distinct
    `Err`, never silently merged. No test asserts repository-global
    collision detection.
- **GPU-required tests (Windows/Vulkan, `gpu`-labeled):**
  - The full closed loop: import the checked-in cube authoring source,
    load the resulting runtime artifact, construct a `Mesh` via the
    existing, unmodified `atlantis::renderer::createMesh()`, render it
    through the existing, unmodified stack with the same fixed
    camera/material this project's fixture already uses, and compare the
    result against the **already-committed**
    `tests/image_regression/goldens/minimal_cube/` golden using Atlantis's
    existing `atlantis::image_regression::compareBuffers()` — not a new
    bespoke mechanism. **Zero** channel difference is the same confirmed,
    evidence-backed standard Spec 0011/ADR-0042 established for this
    fixture and reference GPU/driver. **Composition root for this test:**
    the same test/example-owned
    `Device`/`Presentation`(-or-`OffscreenTarget`)/`Renderer` construction
    sequence `minimal_cube_fixture.*` already owns, reused and extended
    with an asset-load step in place of the hand-authored vertex/index
    arrays — this spec's verification writes no new Device-construction or
    frame-orchestration code. Per ADR-0033's Context, explicitly disclosed
    as reused verification/test composition, not a preview of a future
    Runtime.
  - Vulkan Validation Layers clean throughout.
- **Manual/local verification:** a human or agent runs the full
  GPU-independent and GPU-required suites against real Windows/Vulkan
  hardware and records pass/fail (no CI-enforced gate exists yet, per
  `ci-strategy.md`).
- **Not applicable / explicitly out of scope for this spec's
  verification:** any second asset type; any Android/iOS build or run; any
  cross-vendor GPU verification.

## Risks & Open Questions

**Key options requiring explicit Human Review** — recommendation given for
each, none treated as silently decided:

1. **Is Asset System a new top-level (tenth) module, or hosted by a
   combination of existing Tools/Runtime/Core boundaries?**
   Recommendation: a new module (Tools and Runtime are both currently,
   explicitly, "depended on by nothing"; Core is scoped to non-graphics
   utilities). ADR-0043.
2. **Asset ID scheme: random GUID (with a sidecar to persist it), content
   hash, path-derived, or another scheme?** Recommendation: a
   deterministic, **path-derived** ID for Phase 1, with the "identity does
   not survive a source rename/move" limitation explicitly disclosed and
   accepted, and a content-hash-based (not identity-based) cache key used
   separately for invalidation. Random-GUID-with-sidecar named as real,
   deferred future work. ADR-0044.
3. **The exact metadata and runtime-artifact formats, and their
   versioning strategy.** Recommendation: both hand-rolled,
   dependency-free, versioned formats — a flat text sidecar (the same
   *pattern*, not the same code, as ADR-0042's image-regression sidecar)
   for metadata, and a small, versioned binary layout for the runtime
   vertex/index artifact. ADR-0045 (wire encoding and versioning),
   ADR-0044 (the metadata sidecar's field semantics).
4. **Is a derived-data cache needed, and if so, what does its cache key
   include?** Recommendation: no bespoke cache now — CMake's existing
   incremental-build dependency tracking is reused as this spec's entire
   "cache" mechanism, with a content-hash-based cache key (source bytes +
   importer version + dependency versions) named as the well-understood
   mechanism a future spec would adopt if a real multi-target need arises.
   ADR-0044.
5. **The first supported asset type and its acceptance closed loop.**
   Recommendation: a static triangle mesh (position + per-vertex color,
   matching the existing `minimal_mesh` shader's vertex layout), importing
   the exact cube geometry `examples/headless_rendering_demo` already
   hand-authors, verified via Spec 0011's image-regression comparator
   against the already-committed golden.
6. **Is any new third-party dependency (a model-format parser, a hashing
   library, a serialization library) justified?** Recommendation: no —
   every format and identity scheme is achievable with the C++ standard
   library alone (matching ADR-0042's hand-rolled sidecar precedent).
   `glTF`/`Assimp`, a UUID-generation library, and a JSON/YAML/TOML parser
   are each named explicitly with their own analysis in ADR-0045.
7. **Given path-derived identity (item 2), what is the Asset ID's full
   implementable contract?** Human Review must separately accept: the
   asset-source-root-relative logical path definition; separator,
   absolute-path, `.`/`..`, empty-segment, and Windows-drive-prefix
   handling; case-sensitivity semantics; whether full Unicode or a
   restricted character set is supported; and the concrete hash algorithm,
   bit width, byte serialization, and collision-detection rule.
   Recommendation, fixed in full in ADR-0044's Decision: a fixed
   asset-source root with a `/`-separated logical path relative to it;
   absolute paths and Windows drive prefixes rejected outright (never
   normalized); `.`/`..` lexically resolved with any root-escaping `..`
   rejected; case preserved and hashed case-sensitively (never folded); an
   ASCII-only character set for Phase 1 (a disclosed scope-narrowing
   choice that sidesteps Unicode normalization-form divergence, not an
   oversight); FNV-1a, 64-bit, applied to the normalized path's bytes;
   unconditionally little-endian 8-byte serialization in binary contexts
   and a fixed-width lowercase hex string in the metadata sidecar's text;
   and a fail-fast, distinct `Err` on any collision detected **within one
   importer/validator invocation's own declared asset set**. See ADR-0044's
   own "Why Windows and a future Android cook path are guaranteed to
   compute the identical Asset ID" for why each rule is load-bearing.

**Other risks:**

- **Path-derived identity's rename/move fragility is a real, disclosed UX
  cost**, not a hidden one. Mitigation: explicitly documented as a known
  Phase 1 limitation; a future GUID-based-identity spec remains free to
  supersede this scheme once a real workflow need exists.
- **Reusing CMake's own incremental-build mechanism as "the cache" ties
  Asset System's invalidation correctness to CMake's own
  dependency-tracking correctness** — the same accepted trade-off Shader
  System's ADR-0029 already lives with. Mitigation: none beyond that
  precedent; revisit if it proves insufficient once a second asset type or
  cook target exists.
- **Recommending a new top-level module sets a real precedent** for how
  future Candidate Backlog items might argue for their own module status.
  Mitigation: this spec's reasoning is structural (Tools/Runtime's
  "depended on by nothing" boundary), not a general "give everything its
  own module" argument — a future Spec still has to make its own case, per
  ADR-0032.
- **This spec's metadata/artifact format choices, once implemented, become
  a real compatibility surface** other content could depend on before a
  second asset type or a real versioning/migration need has exercised
  them. Mitigation: `schema_version` fields are fixed as mandatory from
  the first implementation, per ADR-0034's "stable boundary" principle; no
  migration *mechanism* is built now, matching ADR-0034's own explicit "no
  migration mechanism this ADR itself provides" disclosure.

## Out of Scope / Future Work

- **Rename-stable, GUID-with-sidecar identity** — real future work, once a
  concrete multi-author or frequent-rename workflow justifies its added
  bookkeeping cost over this spec's path-derived default.
- **Full Unicode support for authoring-source paths** — this spec
  restricts Phase 1 logical paths to an ASCII-only character set
  specifically to sidestep Unicode normalization-form divergence across
  Windows, Android, and git; real future work once a genuine need for
  non-ASCII asset paths exists.
- **A real, shared derived-data cache/database** — future work once a
  second cook target or a shared build farm creates genuine need beyond
  what CMake's incremental-build tracking already provides.
- **A second or later asset type** (textures — blocked on a future RHI
  Spec adding a general sampled `Texture`/`Sampler` capability first;
  materials-as-data; audio; anything else) — each a future spec's own
  scope.
- **`glTF`/`Assimp` (or any other external interchange format) import** —
  future work if and when a real multi-mesh/multi-material authoring
  workflow makes this project's one-fixture, hand-rolled format
  insufficient.
- **Android cook-target implementation and verification** — this spec
  states the artifact-format *principle* only; a future spec implements
  and verifies it once Android Platform (Candidate Order 1) lands.
- **Hot-reload, live-asset-watching, or any incremental in-process
  re-import.** This spec's importer/cooker is a one-shot,
  explicitly-invoked (or build-time-integrated) tool.
- **Asset streaming, paging, or any partial/progressive load.**
- **A world/scene-level asset reference or dependency graph** (an asset
  referencing another asset by ID) — this spec's one asset type has no
  cross-asset reference to resolve; a future spec with a real multi-asset
  dependency designs that graph when it exists to design against.
