# Spec: Asset System Foundation

- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Created:** 2026-08-18
- **Independent Review (2026-08-18):** Agent-performed, read-only-then-
  mechanical-fix review — not Human Review — conducted against this
  spec's and both ADRs' actual drafted text, cross-checked against
  `origin/main`, `AGENTS.md`, the Spec/ADR templates, Spec 0008/0009,
  ADR-0029/0032–0035, `docs/architecture/module_boundaries.md`, and
  direct source inspection
  (`src/renderer/include/atlantis/renderer/mesh.h`,
  `src/rhi/include/atlantis/rhi/texture.h`,
  `examples/headless_rendering_demo`). Found no blocking architectural
  issue; applied direct, disclosed mechanical fixes — terminology
  precision for the Asset ID's deterministic-but-not-rename-durable
  scope, a path-normalization-contract requirement, an Asset ID
  hash-collision fail-fast requirement, a runtime-artifact byte-order
  disclosure, a composition-root clarification for this spec's own
  closed-loop verification, and an added ADR-0029 citation strengthening
  ADR-0043's module-boundary evidence beyond the not-yet-`Accepted`
  `module_boundaries.md` draft alone — see
  [PR #55](https://github.com/slmao/Atlantis/pull/55) for the full
  revision history. This spec's own six Human-Review-required decisions
  (Risks & Open Questions) remain open, undecided by this review, and
  are now ready for the human maintainer's own formal Human Review and
  `Approved` decision. Both ADRs remain `Proposed` pending that same
  Human Review.
- **Related Plan(s):** none yet — a Plan may be drafted once this Spec
  and its ADRs are `Approved`/`Accepted`, per [AGENTS.md](../AGENTS.md).
- **Related ADR(s):**
  [ADR-0043](../adr/0043-asset-system-module-boundary-and-data-format-dependency.md)
  (module boundary, data format, dependency policy) and
  [ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md)
  (asset identity, provenance, deterministic import and cache
  methodology) — both `Proposed`. See Architectural Impact.

## Summary

This spec proposes Atlantis's first Asset System: a foundation for
turning checked-in, human-authored **authoring source** into a
versioned, deterministic **runtime artifact** that existing RHI/Renderer
code can load and consume — with no scene graph, no asset database, no
package system, and no change to any existing Renderer/RHI/Vulkan
public API. It fixes asset identity, a metadata schema, the authoring/
runtime separation ADR-0035 requires every future data-model spec to
address explicitly, and a deterministic, dependency-tracked import
pipeline — proven by one real, minimal, verifiable closed loop: importing
the exact cube mesh `examples/headless_rendering_demo` already
hand-authors, and rendering it through the existing, unmodified
`Renderer` → RenderGraph → RHI → Vulkan Backend stack to byte-identical
output.

## Motivation / Problem Statement

Every mesh Atlantis renders today is a hand-authored C++ constant.
[specs/0007-minimal-renderer.md](0007-minimal-renderer.md)'s own
Non-Goals fixed this deliberately: "This spec's mesh data is a small,
fixed, hand-authored set of vertices/indices ... constructed directly in
C++ or loaded from a trivial, fixed-format fixture file this spec's own
verification composition owns — not a general asset pipeline." Every
example and test built since (`examples/minimal_renderer_demo`,
`examples/headless_rendering_demo`,
`tests/image_regression/fixture/minimal_cube_fixture.cpp`) duplicates
the identical `kCubeVertices`/`kCubeIndices` arrays byte-for-byte,
by design — see `tests/image_regression/fixture/minimal_cube_fixture.cpp`'s
own comment: "duplicated, not shared ... Plan 0010 Section 7.1's own
default." There is no way, today, to author mesh content outside a C++
translation unit, no stable way to name/reference a piece of content
independent of its source file's path, and no versioned, checked
boundary between what a human authors and what the GPU actually
consumes.

[specs/README.md](README.md)'s Candidate Spec Backlog (Section B) names
this gap directly, as the current Candidate Order 2, "Asset System
Foundation": **"Depends On: Spec 0007 (Minimal Renderer) — `Approved`,
implemented, for real asset consumers"** and **"Intended Outcome: Asset
GUID/metadata model, authoring-to-runtime conversion boundary."** Notably,
this existing, already-reviewed backlog entry does **not** list Runtime
(Candidate Order 3, not yet specced) as a dependency — see "Why this
does not wait for Runtime" below for why this spec agrees with, and does
not reopen, that existing dependency judgment.

[specs/0009-long-term-engine-architecture-alignment.md](0009-long-term-engine-architecture-alignment.md)
(`Approved`) and its six `Accepted` ADRs (ADR-0032–0037) already bind
several things this spec must do, explicitly, rather than silently
default into an answer:

- [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md)
  requires that "every future Spec that introduces or restructures a
  top-level module ... decides that module's real position in both
  [the conceptual-layer and module-ownership] views explicitly, as part
  of its own Architectural Impact section" — this spec does not get to
  leave Asset System's module ownership implicit.
- [ADR-0034](../adr/0034-stable-public-boundary-versus-internal-cpp-layout.md)
  names Asset explicitly as one of the future Specs whose externally-
  stable public boundary must be "expressed through schema, identity,
  and protocol concepts — never through direct exposure of internal C++
  type layout."
- [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)
  requires, in its own words, that "a future ... Asset System Spec
  (Candidate 4 [now Candidate 2, see below]) must ... explicitly address,
  in [its] own Architectural Impact section, whether [its]
  authoring-facing representation and [its] runtime-execution
  representation are the same data structure or two related-but-distinct
  ones — and if distinct, name the transformation step." Silence is
  explicitly stated to be unacceptable once this ADR is `Accepted` — it
  is.

**A note on Candidate numbering:** Spec 0009 and ADR-0033/0035 were
written while Headless Rendering and Image Regression Testing still
occupied Candidate Order 2 and 3; both have since been promoted to
Section A as Specs 0010/0011 (`Approved`, implemented), which shifted
every later backlog entry up by two positions. What those documents call
"Candidate 4 (Asset System Foundation)" is the same working item this
spec's own registry update (see Architectural Impact) finds at the
current Candidate Order 2 — the identity of the item is unchanged, only
its numeric position in a list that shrinks as items get promoted.

### Why this does not wait for Runtime

This spec's own instructions required checking, explicitly, whether
Asset System can form a reasonable boundary before Atlantis Runtime (the
module) is implemented, and to object with an alternative ordering
rather than force a draft if not. Three independent, already-reviewed
sources converge on the same answer — **no, it is not blocked**:

1. **The Candidate Backlog's own "Depends On" column** for Asset System
   Foundation names only Spec 0007 (Minimal Renderer, `Approved`,
   implemented) — not Runtime (Candidate Order 3, `Candidate`, not yet
   specced).
2. **Spec 0009's own Roadmap Impact section**, drafted specifically to
   surface exactly this kind of sequencing risk, discusses Asset System
   only in relation to Serialization/Stable Identity (Candidate Order 5,
   "plausibly benefits ... but is not described as strictly blocked by
   it") — it never names Runtime as a prerequisite for Asset System.
3. **A real, already-`Accepted`, already-shipped precedent exists in
   this exact repository**: Atlantis Shader System (Spec 0008,
   `Approved`, implemented via
   [PR #36](https://github.com/slmao/Atlantis/pull/36)) is a complete
   authoring-source → build-time-compiled-artifact → versioned-schema →
   runtime-consumption pipeline, built and shipped entirely without
   Atlantis Runtime existing. `docs/architecture/module_boundaries.md`
   confirms Runtime is structurally a leaf even once it exists — "**Depended
   on by:** nothing (it's the executable)" — meaning nothing has ever
   needed Runtime to exist in order to consume a versioned, Tools-produced
   runtime artifact, and nothing will: a future Runtime will itself
   consume Asset System's own runtime-loading API the same way it will
   consume Renderer's, not the other way around.

This spec's own minimal closed loop (see Requirements/Testing &
Verification Plan) is, like Spec 0003's and Spec 0010's own verification
compositions before it, **explicitly not a preview of Runtime** — the
same disclosed-non-preview framing
[ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s own
Context already establishes for this repository's prior non-shipping
compositions.

## Goals

- Fix **asset identity**: what names a piece of authoring content
  deterministically, and what does *not* survive an edit/rename — stated
  as an explicit, disclosed property, not left implicit.
- Fix a **metadata schema**: what provenance/version information every
  imported asset records, in a strict, versioned, dependency-free format.
- Fix the **authoring source / runtime artifact separation** ADR-0035
  requires this spec to address explicitly: name whether they are the
  same representation or two distinct ones, and if distinct, name the
  transformation step connecting them.
- Fix **deterministic import**: the same authoring source, imported
  twice, produces byte-identical runtime artifact output — verified
  empirically, not assumed, per this project's own established
  precedent ([ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md),
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own Context).
- Fix **dependency tracking and re-import triggering** (the underlying
  problem informally called "cache invalidation" in asset-pipeline
  design, though this spec's own mechanism is not a cache — see
  Requirements): when a re-import must happen, and how that is detected
  — without inventing a bespoke derived-data cache/database unless this
  spec's own analysis shows one is actually needed at this scope (see
  Risks & Open Questions).
- Decide **Asset System's ownership** within Atlantis's existing
  nine-module structure, per ADR-0032's own requirement — new top-level
  module, or hosted by a combination of existing modules — as an
  explicit Human Review decision, not a silent default.
- Fix the **dependency boundary** between Asset System and Tools, Core,
  Renderer, Shader System, and a future Runtime.
- Deliver **one real, minimal, verifiable asset-consumption closed
  loop** — narrow enough to build and verify now, load-bearing enough to
  prove the pipeline actually works end to end, not merely on paper.
- State the principle that a runtime artifact this spec's pipeline
  produces is usable, in principle, by both Atlantis's current Windows
  development environment and a future Android target sharing the same
  Vulkan Backend — **without implementing, designing, or verifying
  anything Android-specific**.
- Fix Phase 1 baseline non-functional properties for this module,
  explicitly: single-threaded, RAII ownership, explicit `Result`/error
  types (never exceptions on any path a runtime consumer calls), and no
  global mutable asset database or registry.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Runtime Host, World/ECS, Editor, hot-reload, or asynchronous
  streaming of any kind.** This spec's import pipeline is a synchronous,
  offline/build-time or explicitly-invoked step, never a live-reload or
  background-streaming system. No Runtime module, World/ECS model, or
  Editor tool is designed, implied, or previewed.
- **A package system, virtual file system, remote/network asset source,
  or any UGC (user-generated content) capability.** Every authoring
  source this spec's scope covers is a checked-in file at a fixed,
  known repository path — never a package archive, a mounted virtual
  path, a URL, or untrusted third-party content.
- **Android, iOS, or Linux implementation of any kind.** This spec
  states a *principle* that its runtime artifact format is platform-
  agnostic in a way that does not preclude Android reuse later (see
  Requirements) — it implements, builds, and verifies the Windows path
  only. Linux is not a target platform for Atlantis at all, per
  [AGENTS.md](../AGENTS.md).
- **A general-purpose serialization framework.** This spec's metadata
  sidecar and runtime artifact formats are narrow, hand-rolled, and
  scoped to exactly the fields this spec's own one asset type needs —
  not a reusable, general (de)serialization library any other module
  could adopt for unrelated data.
- **GPU-driven rendering, bindless resources, or any texture-system
  extension.** In particular, this spec does **not** add a general,
  shader-read/sampled `Texture` type or a `Sampler` type to RHI —
  verified absent today (`src/rhi/include/atlantis/rhi/texture.h`'s
  `Texture` interface exposes only `DepthFormat format()`; no `Sampler`
  type exists anywhere in `src/`) — nor does it add any multi-draw,
  indirect, or instanced draw capability. See "Why the first asset type
  is a mesh, not a texture" under Proposed Design.
- **Any change to an existing Renderer, RHI, or Vulkan Backend public
  API.** This spec's runtime loader is a new consumer of
  `atlantis::renderer::createMesh()`'s **existing, unmodified** public
  signature (`src/renderer/include/atlantis/renderer/mesh.h`) — it adds
  no new parameter, no new overload with different semantics, and no
  new public type to any of those three modules. If this spec's own
  design work had found a genuine need to change one of those APIs, this
  spec would stop and raise that explicitly as its own architectural
  question rather than fold it in silently — it did not find such a
  need (see Requirements).
- **A rename-stable, sidecar-backed GUID identity scheme.** See
  Architectural Impact/Risks & Open Questions — this spec recommends a
  narrower, disclosed-limitation identity scheme for its own Phase 1
  scope and names GUID+sidecar as explicit future work, not something it
  designs now.
- **A general, content-hash-keyed derived-data cache/database.** See
  Risks & Open Questions — this spec's own scope (one asset type, one
  build target, one developer machine at a time) is deliberately proven
  not to need one; a future spec introducing a second asset type, a
  second cook target (e.g. Android), or a shared build farm may need to
  revisit this.
- **Any second or third asset type beyond the one this spec's closed
  loop proves** (a static triangle mesh with per-vertex position and
  color, matching the existing `minimal_mesh` shader's vertex layout).
  Textures, materials-as-data, audio, or any other asset kind are each a
  future spec's own scope, once a real consumer needs them.
- **`glTF`, `Assimp`, or any external 3D interchange format parser.**
  See Architectural Impact/ADR-0043 — this spec's authoring source
  format is a minimal, hand-rolled, dependency-free format scoped to
  exactly the geometry its one asset type needs, not a general
  model-import capability.
- **Multi-threaded import, a job/task system, or any importer
  parallelism.** Matches [ADR-0004](../adr/0004-phase1-threading-baseline.md)'s
  existing Phase 1 single-logical-thread baseline, unchanged and
  unreopened by this spec.

## Requirements

### Functional

**Asset identity**

- Every importable authoring source is identified by a deterministic
  **Asset ID** — a value other content (and, later, other assets) can
  reference without embedding a raw file path, computed the same way
  every time for the same input. This spec fixes the identity *scheme*
  (see Architectural Impact/ADR-0044); the exact bit width and encoding
  are ADR-0044's own Decision, not left further open past this spec's
  approval. **This determinism holds across repeated imports and across
  unrelated edits elsewhere in the source tree — it does not, by itself,
  mean the identifier survives a rename or move of its own source file;
  see the next bullet.** This spec deliberately avoids calling the
  scheme "stable" without that qualifier, since the chosen scheme is
  narrower than full rename/move-durable identity (see ADR-0044's own
  Context, which names and distinguishes the two).
- The chosen scheme's limitations (in particular: identity does **not**
  survive a source file rename/move) are stated explicitly, as a
  disclosed Phase 1 scope boundary — never silently assumed to be
  stronger than it is.
- **The path this scheme hashes is normalized by one fixed,
  platform-invariant rule** — case sensitivity, path separator (`/` vs.
  `\`), `.`/`..` segment resolution, the fixed asset-source-root anchor,
  and Unicode normalization form are all fixed to a single canonical
  rule (exact rule a Plan-stage detail, per ADR-0044) so that Windows
  and a future Android cook path compute the *same* Asset ID for the
  *same* logical authoring source. This is required by, not separate
  from, this spec's own Windows-now, Android-later artifact-sharing
  principle below — an unspecified or platform-dependent normalization
  rule would silently break that principle.
- **A hash collision (two distinct source paths producing the same
  Asset ID) is possible in principle**, since the hash is
  non-cryptographic and the space of possible source paths is unbounded.
  The importer must detect this if it ever occurs and fail with a
  distinct `Err` rather than silently associating either path's data
  with the shared ID — the same fail-fast-on-invalid-input discipline
  this spec's own Error handling requirements apply everywhere else (see
  ADR-0044).
- **Asset ID is a distinct concept from a cache/rebuild key.** An Asset
  ID names *what* a piece of content is, deterministically; a cache key
  (see "Deterministic import" below) determines *when* a re-import must
  happen, and legitimately changes whenever the source content, the
  importer's own version, or any dependency changes — conflating the two
  is a real, disclosed risk this spec's design explicitly avoids (see
  ADR-0044).

**Metadata schema**

- Every imported asset has an accompanying **metadata record** stating,
  at minimum: its Asset ID; its source-authoring-file's identity
  (path, relative to a fixed asset-source root); the importer's own
  version/identity that produced this artifact; a `schema_version`
  field controlling the metadata format itself; and whatever per-asset-
  type fields the one supported asset type needs (e.g., vertex/index
  counts, the vertex layout it was imported against).
- The metadata format is a **strict, versioned, dependency-free flat
  text format** — the exact encoding is ADR-0044's own Decision. Bounded
  the same way [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "Sidecar encoding and parsing" bounded the image-regression
  sidecar: no new parsing/serialization dependency without its own
  Spec/ADR/Human Review; a stable, fixed field order; an explicit
  version marker. This spec's own metadata parser is **new,
  independent code** — it must not depend on, link against, or copy
  private implementation from `tests/image_regression/support/provenance.*`,
  which is test-only code outside any `src/` module's dependency
  surface; the two sidecar formats may share a *pattern* (flat,
  versioned, anchored-prefix-parsed text), never a code dependency.

**Authoring source / runtime artifact separation (ADR-0035 compliance)**

- This spec **explicitly separates** authoring-facing representation
  from runtime-execution representation for its one supported asset
  type — they are not the same data structure. The authoring source is
  a small, human-readable, hand-rolled text format a human can author
  and diff in an ordinary PR; the runtime artifact is a small,
  versioned binary format laid out to be loaded directly into the exact
  buffer shape `atlantis::renderer::createMesh()` already expects (raw
  vertex bytes matching a `VertexInputLayout`, plus a `std::uint16_t`
  index array — see "Why the first asset type is a mesh, not a texture"
  under Proposed Design for why this exact shape was chosen).
- The **transformation step** connecting them is a deterministic
  **importer/cooker**: a build-time-or-explicitly-invoked tool (exact
  invocation model — build-integrated like Shader System's `slangc`
  step, or a standalone tool like Image Regression's golden generator —
  is ADR-0044's own Decision) that reads one authoring source file and
  produces one runtime artifact plus its metadata sidecar.
- This spec does **not** claim its own transformation step is
  structurally equivalent to whatever a future World/ECS Spec's own
  authoring→runtime bake turns out to need — per ADR-0035's own Context,
  that Spec's own multi-object dependency graph, editor round-tripping,
  and incremental-update needs are not solved or assumed solved here.

**Deterministic import, dependency tracking, and re-import triggering**

- Importing the same authoring source twice, with the same importer
  version, produces **byte-identical** runtime artifact output — proven
  empirically as part of this spec's own Testing & Verification Plan,
  not merely asserted.
- Re-import is triggered whenever the authoring source file's own
  content changes, or the importer's own version/implementation changes
  — the exact mechanism (a real derived-data cache, versus reusing
  CMake's own existing incremental-build dependency tracking) is
  ADR-0044's own Decision; this spec's own recommendation and reasoning
  are in Architectural Impact.
- **No asset is ever silently regenerated by an ordinary build/test
  run in a way that changes a checked-in file without a human noticing**
  — mirroring [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "never a silent regeneration step that a CI job runs and commits
  automatically" rule, applied here to imported assets the same way it
  already applies to golden images.

**Module ownership and dependency boundaries**

- This spec fixes Asset System's position in Atlantis's nine-module
  source-ownership structure — either as a new, tenth top-level module,
  or as a combination of responsibilities hosted by existing Tools/
  Core/Runtime boundaries — as an explicit Human Review decision (see
  Architectural Impact/ADR-0043).
- Whatever the outcome, the following boundaries hold, verified against
  the current, real dependency structure of every module named:
  - **Depends on RHI** (to construct GPU-consumable `Buffer`s via the
    existing `atlantis::renderer::createMesh()` path) **and Core**
    (`Result<T,E>`, logging, assertions) — both already-`Accepted`,
    already-implemented, stable dependencies, matching how every other
    RHI-consuming module already depends on them.
  - **May depend on Atlantis Shader System's public
    `rhi_integration` surface** (`atlantis::shader_system::rhi_integration`,
    e.g. `toVertexInputLayout()`) to resolve which `VertexInputLayout` an
    imported mesh must be cooked against — the same public surface
    `examples/headless_rendering_demo` already consumes, never Shader
    System's own private JSON parser or private implementation.
  - **Renderer, RHI, and Vulkan Backend gain no new dependency on Asset
    System.** Asset System depends on their existing public output
    types (`Mesh`, `VertexInputLayout`); none of them is modified to
    depend back on Asset System, matching the existing, unidirectional
    dependency shape every other consumer of RHI/Renderer already
    follows.
  - **Atlantis Tools** hosts, at minimum, the importer/cooker's own
    command-line entry point — matching `docs/architecture/module_boundaries.md`'s
    own (`PROPOSED`, not yet `Accepted`) description of Tools'
    responsibilities as including "asset processing," and matching
    Shader System's own precedent of a Tools-hosted CLI
    (`atlantis_shader_compiler`) invoking a separate module's own
    library code. Whether Asset System's own *runtime-loading* code (the
    half a future Renderer-adjacent consumer links against) also lives
    under Tools, or under a new dedicated module, is exactly ADR-0043's
    own question — see Architectural Impact for why this spec's own
    analysis finds this structurally significant, not a naming
    preference.
  - **No dependency on a future Atlantis Runtime module.** Nothing in
    this spec's own scope requires Runtime to exist — see "Why this does
    not wait for Runtime" above.

**First asset-consumption closed loop**

- This spec's implementation must import one real, checked-in authoring
  source file — the same cube geometry
  `examples/headless_rendering_demo`'s `kCubeVertices`/`kCubeIndices`
  already hand-authors — through its own importer/cooker, load the
  resulting runtime artifact through a new runtime-loading API, and feed
  it into the **existing, unmodified**
  `atlantis::renderer::createMesh()` call this project's own examples
  already use.
- The loaded, imported mesh, rendered through the existing, unmodified
  `Renderer` → RenderGraph → RHI → Vulkan Backend stack with the same
  fixed camera/material this project's own reused fixture already uses,
  must produce **pixel-identical output** to the existing hand-authored
  fixture — verified using Atlantis's own, already-`Approved`,
  already-implemented Image Regression Testing harness (Spec 0011),
  not a new, bespoke comparison mechanism.

**Windows-now, Android-later artifact-sharing principle**

- This spec's runtime artifact format is designed to carry no
  Windows-specific byte order, alignment, or path assumption beyond
  what Vulkan's own buffer/vertex-format conventions already require
  identically on both of Atlantis's primary target platforms (Windows
  and Android, both consuming the same Vulkan Backend, per
  [AGENTS.md](../AGENTS.md)) — stated as a design **principle** future
  Android work can rely on, not a claim that this spec builds, tests, or
  verifies anything on Android. No Android build target, CMake toolchain
  file, or NDK dependency is added by this spec.

### Non-functional

- **Performance:** not a goal beyond "does not stall or leak
  unnecessarily." Import-time cost is not evaluated as a performance
  concern at this spec's one-fixture scale; no benchmark or performance
  claim is made or required.
- **Memory:** no new allocation strategy. Imported/loaded asset data is
  ordinary heap memory, owned by whichever caller requested the load,
  consistent with Phase 1's existing "no general allocator, no pooling"
  posture (matching [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s
  own precedent for RHI resources). No streaming, paging, or partial-load
  design.
- **Portability (within the Vulkan-only Phase 1 constraint):** Windows
  only for this spec's own build/test/verification; the runtime artifact
  *format* is designed, as a stated principle, not to preclude a future
  Android cook/consume path — see Requirements above.
- **Threading:** single-threaded, matching
  [ADR-0004](../adr/0004-phase1-threading-baseline.md)'s existing Phase
  1 baseline. The importer/cooker runs on one thread; the runtime loader
  is called from the same single logical thread every other RHI/Renderer
  call in this project already assumes. No thread-safety contract beyond
  "not thread-safe" is claimed for any type this spec introduces.
- **Ownership:** RAII throughout — every owning type (an in-memory
  loaded asset, an open file handle during import) has one clear owner,
  released deterministically on destruction, matching
  [AGENTS.md](../AGENTS.md)'s existing "RAII by default" rule. No
  manual, caller-remembered cleanup step.
- **Error handling:** recoverable runtime errors (a missing authoring
  source file, a malformed metadata sidecar, an importer producing
  invalid output, a runtime artifact that fails to parse) use
  `atlantis::Result<T, E>` — never an exception — on every path a
  runtime consumer (i.e. anything other than the offline importer/cooker
  tool itself) calls, matching this project's own established
  render-path convention
  ([AGENTS.md](../AGENTS.md) Error handling). Whether the offline
  importer/cooker tool's own internal implementation may use exceptions
  is left to this spec's own Plan, per
  [AGENTS.md](../AGENTS.md)'s explicit "offline tooling ... is left to
  that module's own spec" allowance — this spec does not decide it,
  since it has no bearing on any runtime-consumed API.
- **No global mutable asset database.** Every loaded asset is owned by
  an explicit, caller-held value/handle — never a global, static, or
  singleton registry a caller looks up by ID without having been handed
  ownership or a borrowed reference first. This directly satisfies
  [AGENTS.md](../AGENTS.md)'s existing "No global mutable engine-state
  singletons" rule and is consistent with, and does not conflict with,
  [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
  future "Runtime is the sole authoritative owner of engine *world*
  state" principle — an in-memory loaded-asset cache, if this spec's own
  Plan needs one at all for its one fixture, is caller-owned and
  explicitly passed, never a process-wide singleton, and never framed as
  *world state* (a future World/ECS Spec's own concept, not this one's).

## Proposed Design

### Pipeline shape

```
Authoring source (checked in, human-readable, hand-rolled text format)
  |
  v
Importer/cooker (Atlantis Tools-hosted CLI; deterministic;
                  reads exactly one source file per invocation)
  |
  +--> Runtime artifact (versioned binary: raw vertex bytes matching a
  |     VertexInputLayout, plus a std::uint16_t index array)
  |
  +--> Metadata sidecar (versioned flat text: Asset ID, source identity,
        importer version, per-asset-type fields)
        |
        v
Runtime loader (new library code; Result-returning; no exceptions)
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

RHI's current `Texture` type
(`src/rhi/include/atlantis/rhi/texture.h`) exposes exactly one accessor
beyond `extent()`: `[[nodiscard]] virtual DepthFormat format() const`.
There is no general, shader-read/sampled texture type, no `Sampler`
type, and no descriptor-binding surface for a texture resource anywhere
in `src/` today — confirmed by direct inspection, not assumed.
[specs/0007-minimal-renderer.md](0007-minimal-renderer.md)'s own
Non-Goals fixed this deliberately ("a general `Sampler` type ... or a
sampled/shader-read `Texture`"), and no spec since has revisited it.
Choosing a texture as this spec's first supported asset type would
therefore require adding real, new RHI/Renderer public API surface —
exactly the kind of change this spec's own Non-Goals rule out unless
raised as its own explicit architectural question first. A static mesh,
by contrast, needs **zero** new RHI or Renderer API: raw vertex bytes
plus a `std::uint16_t` index array is exactly what
`atlantis::renderer::createMesh()`'s existing, unmodified signature
already accepts
(`src/renderer/include/atlantis/renderer/mesh.h`). This is why mesh
geometry, not a texture, is this spec's one supported asset type.

### Module boundary (recommended, not decided here — see ADR-0043)

This spec's own recommendation, offered for Human Review to confirm or
redirect: **a new, tenth top-level module, `Atlantis Asset System`**,
mirroring Shader System's own precedent almost exactly — an authoring
format, a Tools-hosted CLI producing a versioned runtime artifact, and a
small runtime-loading library other modules link against. The reasoning
is structural, not stylistic:

- `docs/architecture/module_boundaries.md`'s own text states, for both
  candidate hosts: **Tools** — "**Depended on by:** nothing — no runtime
  module ever depends on Tools"; **Runtime** — "**Depended on by:**
  nothing (it's the executable)." Both of Atlantis's only two
  not-yet-`Accepted` candidate hosts are, by their own current
  description, structural **leaves** — nothing may link against either
  of them as a library. This spec's own runtime-loading half must be
  linkable by a future Renderer-adjacent consumer (an example, a test,
  eventually a future Runtime) — a requirement neither Tools nor Runtime
  can satisfy without first changing *their own* module boundary, which
  would itself be a larger, separate architectural change than this
  spec is scoped to make.
- **Core** is explicitly scoped to "non-graphics utilities" — this
  spec's runtime loader has a real, direct dependency on RHI (`Buffer`
  creation) that does not fit Core's own existing boundary.
- Shader System itself was not folded into Tools' own already-drafted
  (`PROPOSED`, not `Accepted`) "asset processing" language for exactly
  this reason: a module with a real, versioned, runtime-consumed public
  schema earns its own top-level position. Asset System has the same
  shape.
- Under this recommendation, **Renderer, RHI, and Vulkan Backend are
  entirely unchanged** — Asset System depends on their existing public
  output types; they gain no new dependency and no new public API.

The alternative — hosting the importer/cooker under Tools (consistent
with `module_boundaries.md`'s own current draft language) and finding
some other home for the runtime-loading half — is named honestly in
Alternatives Considered, with its own real cost (it requires either
amending Tools' or Runtime's own "depended on by nothing" boundary, or
routing runtime consumption through Renderer itself, reopening Spec
0007's own settled boundary). Whichever way Human Review decides, this
spec's own Plan implements exactly that decision — this Proposed Design
section states a recommendation, not a fait accompli.

## Architectural Impact

This spec introduces two new architectural decisions, each drafted as
its own `Proposed` ADR:

- [ADR-0043](../adr/0043-asset-system-module-boundary-and-data-format-dependency.md) —
  Asset System Module Boundary and Data Format/Dependency. Covers: new
  top-level module versus existing-module combination (see Proposed
  Design above); the authoring-source and runtime-artifact data formats;
  and whether any new third-party dependency (a model-format parser, a
  hashing library, a serialization library) is justified — this spec's
  own recommendation is no, see ADR-0043's own Alternatives Considered.
- [ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md) —
  Asset System Identity, Provenance, and Import Methodology. Covers:
  the Asset ID scheme and its disclosed limitations; the metadata
  schema's exact fields and encoding; the deterministic-import and
  cache-invalidation mechanism (and whether a bespoke derived-data cache
  is justified at this spec's scope — this spec's own recommendation is
  no, reusing CMake's existing incremental-build dependency tracking
  instead); and the golden-update-style human-review discipline for any
  checked-in imported artifact.

**ADR-0035 compliance, stated explicitly as this ADR requires:** this
spec's authoring-facing representation (a hand-rolled text mesh source)
and its runtime-execution representation (a versioned binary vertex/
index artifact) are **two distinct data structures**, connected by an
explicit importer/cooker transformation step — see "Authoring source /
runtime artifact separation" under Requirements and the pipeline diagram
under Proposed Design. This spec does not claim its own transformation
step generalizes to a future World/ECS Spec's own, structurally
different bake problem (see ADR-0035's own Context, quoted under
Requirements).

**ADR-0034 compliance:** this spec's externally-stable public boundary
is its Asset ID scheme and its versioned metadata/artifact-format
schema — never the importer's own internal C++ parsing/cooking types,
none of which is exposed publicly.

**ADR-0032 compliance:** this spec places Asset System, if approved as a
new module (ADR-0043's own recommendation), in the "Runtime Services"/
"Authoritative Runtime" boundary of the conceptual five-layer view
(alongside Render, per
[docs/architecture/engine_architecture.md](../docs/architecture/engine_architecture.md)'s
own illustrative mapping) and as a new, tenth top-level module in the
authoritative nine-module source-ownership view, depending on Core and
RHI, optionally on Shader System's public `rhi_integration` surface, and
depended on by nothing yet (its own consumers — examples, tests, a
future Runtime — are all outside this spec's own module).

**ADR-0033 is not implicated by this spec.** ADR-0033 governs
authoritative *world state* and Client access to it — a future World/
ECS Spec's own concept. This spec's loaded assets are immutable,
versioned, content-addressed-by-identity data, not mutable world state;
nothing in this spec's design claims or requires exclusive/authoritative
ownership of anything a future Runtime would itself need to arbitrate.

**No change to any existing `Accepted` ADR or `Approved` Spec.** In
particular: `atlantis::renderer::createMesh()`
(`src/renderer/include/atlantis/renderer/mesh.h`), RHI's `Buffer`/
`VertexInputLayout` types, and RenderGraph/Vulkan Backend's existing
public surfaces are all consumed exactly as they exist today, with zero
modification.

## Alternatives Considered

- **Host both the importer/cooker and the runtime-loading library under
  Atlantis Tools**, matching `module_boundaries.md`'s own current draft
  language ("asset processing" under Tools' Responsibilities). Rejected
  as this spec's own recommendation (though named as a real Human
  Review option, per ADR-0043): `module_boundaries.md`'s own text states
  Tools is "depended on by: nothing — no runtime module ever depends on
  Tools," which structurally forecloses a future Renderer-adjacent
  consumer linking against Tools-hosted runtime-loading code without
  first reopening Tools' own boundary — a larger change than this spec
  is scoped to make unilaterally.
- **Fold runtime loading directly into Renderer.** Rejected:
  [specs/0007-minimal-renderer.md](0007-minimal-renderer.md)'s own
  Non-Goals explicitly excluded "a scene graph, ECS, asset system, or a
  model/mesh loader" from Renderer's scope; Renderer's existing,
  `Accepted` boundary is "depends only on Core, RHI, RenderGraph" — this
  spec does not propose reopening that settled boundary.
- **Wait for a future Runtime Host Spec (Candidate Order 3) or a future
  Serialization and Stable Identity Spec (Candidate Order 5) before
  drafting Asset System.** Rejected — see "Why this does not wait for
  Runtime" under Motivation. Spec 0009's own Roadmap Impact already
  weighed the Serialization dependency specifically and found Asset
  System "plausibly benefits ... but is not ... strictly blocked."
- **Adopt `glTF` (via `cgltf`, `tinygltf`, or Assimp) as the authoring
  source format for this spec's one mesh asset type.** Rejected as this
  spec's own default recommendation, not ruled out for a future spec
  with a real multi-mesh/multi-material authoring workflow to justify
  it: this spec's own one-fixture, one-mesh scope needs none of glTF's
  scene-graph, material, animation, or skinning surface, and adopting a
  ~20,000+ line third-party parser (or the substantially larger Assimp)
  to read eight hand-known vertices would be exactly the kind of
  dependency this project's own established discipline
  ([ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md))
  requires justifying against a real, present need — not a future,
  anticipated one. See ADR-0043's own Alternatives Considered for the
  full license/build/maintenance analysis Human Review requires before
  any dependency decision.
- **A random-GUID, sidecar-backed identity scheme**, matching how Unity/
  Unreal-style engines commonly assign stable identity independent of a
  source file's path. Named as real future work (see Out of Scope), not
  adopted now: it requires a durable place to persist the assigned GUID
  once generated (a `.meta`-style sidecar, itself a new piece of
  authoring-adjacent state to keep consistent under rename/move/delete),
  which is a real engineering commitment with no concrete second-asset-
  type or multi-author-workflow need yet to justify it at this spec's
  one-fixture scope. See ADR-0044's own Alternatives Considered.
- **A content-hash-derived identity scheme** (hashing the authoring
  source's own bytes to produce the Asset ID). Rejected as an *identity*
  scheme specifically, because identity would then change on every
  content edit — breaking the property that other content should be
  able to reference "this same asset" stably across a routine edit. This
  spec instead recommends a content-hash-based **cache key** (combining
  source content, importer version, and dependency versions) as the
  right tool for cache invalidation specifically — a distinct question
  from identity. See Requirements' own "Asset ID is a distinct concept
  from a cache/rebuild key" and ADR-0044.
- **A bespoke, content-hash-keyed derived-data cache/database**,
  matching how a mature engine's asset pipeline (e.g. Unreal's Derived
  Data Cache) typically works. Rejected at this spec's scope: with one
  asset type and one build target, CMake's own existing
  `add_custom_command()`/`DEPENDS`-based incremental-rebuild mechanism
  (already `Accepted` precedent, per
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md))
  already provides correct invalidation with zero new code. A real,
  shared derived-data cache becomes justified once a second cook target
  (e.g. a future Android import path needing different output from the
  same source) or a shared build farm creates a genuine need a single
  build tree's own dependency tracking cannot serve — named as future
  work, not designed now.

## Testing & Verification Plan

This section states what this spec's future Plan/Implementation must
verify — this Spec document itself introduces no code.

- **Unit tests (GPU-independent):**
  - The importer/cooker, run twice against the same authoring source
    with the same version, produces byte-identical runtime-artifact and
    metadata-sidecar output — the same empirical-determinism discipline
    established by [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
    (Shader System) and
    [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
    (Image Regression Testing).
  - Metadata-sidecar parsing: a well-formed sidecar parses correctly and
    round-trips through its own serializer byte-for-byte; each strict-
    parsing failure mode (wrong field count, wrong field name at a
    position, unknown `schema_version`, malformed value) returns a
    distinct, expected `Err`.
  - Runtime-artifact loading: a well-formed artifact loads into the
    exact `VertexInputLayout`-compatible buffer shape
    `atlantis::renderer::createMesh()` expects; a malformed/truncated
    artifact is rejected with a distinct `Err`, never a crash or a
    silent, partially-populated result.
  - Asset ID computation: deterministic for a fixed source path;
    documented, tested behavior for the disclosed rename/move limitation
    (an Asset ID changing after a rename is expected, tested behavior
    under this spec's own recommended scheme — not a bug); a
    deliberately constructed hash collision (two distinct source paths
    forced to share one Asset ID) is detected and rejected with a
    distinct `Err`, never silently merged (see Requirements).
- **GPU-required tests (Windows/Vulkan, `gpu`-labeled):**
  - The full closed loop: import the checked-in cube authoring source,
    load the resulting runtime artifact, construct a `Mesh` via the
    existing, unmodified `atlantis::renderer::createMesh()`, render it
    through the existing, unmodified `Renderer` → RenderGraph → RHI →
    Vulkan Backend stack with the same fixed camera/material this
    project's fixture already uses, and compare the result against the
    **already-committed** `tests/image_regression/goldens/minimal_cube/`
    golden (Spec 0011) using Atlantis's own existing image-regression
    comparator (`atlantis::image_regression::compareBuffers()`) — not a
    new, bespoke pixel-comparison mechanism. **Zero** channel difference
    is the same confirmed, evidence-backed standard Spec 0011/ADR-0042
    already established for this exact fixture and reference GPU/driver.
    **Composition root for this test:** the same test/example-owned
    `Device`/`Presentation`(-or-`OffscreenTarget`)/`Renderer`
    construction sequence
    `tests/image_regression/fixture/minimal_cube_fixture.*` already owns
    is reused and extended with an asset-load step in place of that
    fixture's own hand-authored vertex/index arrays — this spec's own
    verification writes no new Device-construction or frame-orchestration
    code. Per [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
    own Context, this is explicitly disclosed as reused verification/test
    composition, not a preview of a future Atlantis Runtime's own
    composition-root architecture — the same disclosed-non-preview
    framing this project's prior non-shipping compositions (Spec 0003,
    Spec 0010, Spec 0011) already established, cited under Motivation's
    own "Why this does not wait for Runtime" above.
  - Vulkan Validation Layers clean throughout, matching every prior GPU-
    touching test in this project.
- **Manual/local verification:** a human or agent runs the full
  GPU-independent and GPU-required suites against real Windows/Vulkan
  hardware and records pass/fail, matching every prior spec's own
  "Manual verification record" precedent (no CI-enforced gate exists in
  this repository yet, per
  [docs/process/ci-strategy.md](../docs/process/ci-strategy.md)).
- **Not applicable / explicitly out of scope for this spec's own
  verification:** any second asset type; any Android/iOS build or run;
  any cross-vendor GPU verification (matching every prior spec's own
  disclosed single-GPU-vendor limitation).

## Risks & Open Questions

**Key options requiring explicit Human Review** — this spec's own
analysis gives a recommendation for each, but does not treat any of
these as silently decided:

1. **Is Asset System a new top-level (tenth) module, or hosted by a
   combination of existing Tools/Runtime/Core boundaries?** This spec's
   own recommendation: a new module, for the structural reasons stated
   under Proposed Design and Alternatives Considered (Tools and Runtime
   are both currently, explicitly, "depended on by nothing"; Core is
   scoped to non-graphics utilities). See ADR-0043.
2. **Asset ID scheme: random GUID (with a sidecar to persist it),
   content hash, path-derived, or another scheme?** This spec's own
   recommendation: a deterministic, **path-derived** ID for this spec's
   own Phase 1 scope, with the "identity does not survive a source
   rename/move" limitation explicitly disclosed and accepted, and a
   content-hash-based (not identity-based) cache key used separately for
   invalidation. Random-GUID-with-sidecar is named as real, deferred
   future work once a real multi-author editing workflow exists to
   justify its added bookkeeping cost. See ADR-0044.
3. **The exact metadata and runtime-artifact formats, and their
   versioning strategy.** This spec's own recommendation: both are
   hand-rolled, dependency-free, versioned formats — a flat text sidecar
   (the same *pattern*, not the same code, as ADR-0042's own
   image-regression sidecar) for metadata, and a small, versioned binary
   layout for the runtime vertex/index artifact. See ADR-0043/ADR-0044.
4. **Is a derived-data cache needed, and if so, what does its cache key
   include?** This spec's own recommendation: no bespoke cache is built
   now — CMake's own existing incremental-build dependency tracking is
   reused as this spec's entire "cache" mechanism, with a content-hash-
   based cache key (source bytes + importer version + dependency
   versions) named as the well-understood mechanism a future spec would
   adopt if a real, multi-target need for a shared cache arises. See
   ADR-0044.
5. **The first supported asset type and its acceptance closed loop.**
   This spec's own recommendation: a static triangle mesh (position +
   per-vertex color, matching the existing `minimal_mesh` shader's
   vertex layout), importing the exact cube geometry
   `examples/headless_rendering_demo` already hand-authors, verified via
   Spec 0011's own image-regression comparator against the already-
   committed golden for that exact fixture. See Proposed Design's "Why
   the first asset type is a mesh, not a texture" and Testing &
   Verification Plan.
6. **Is any new third-party dependency (a model-format parser, a
   hashing library, a serialization library) justified?** This spec's
   own recommendation: no — every format and identity scheme this spec
   proposes is achievable with the C++ standard library alone, matching
   this project's own demonstrated default (see ADR-0042's own hand-
   rolled sidecar precedent). `glTF`/`Assimp`, a UUID-generation
   library, and a JSON/YAML/TOML parser are each named explicitly, with
   their own license/build/alternatives/maintenance analysis, in
   ADR-0043's own Alternatives Considered — none is silently adopted by
   this spec.

**Other risks:**

- **Path-derived identity's rename/move fragility is a real, disclosed
  UX cost**, not a hidden one — a future author renaming the one
  checked-in authoring source file this spec's closed loop uses would
  change its Asset ID. Mitigation: explicitly documented as a known
  Phase 1 limitation; a future spec introducing GUID-based identity
  remains free to supersede this scheme once a real workflow need exists
  (see Out of Scope).
- **Reusing CMake's own incremental-build mechanism as "the cache"
  ties Asset System's own invalidation correctness to CMake's own
  dependency-tracking correctness**, the same accepted trade-off Shader
  System's own [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)
  already lives with. Mitigation: none beyond what Shader System's own
  precedent already establishes as workable; revisit if this proves
  insufficient once a second asset type or cook target exists.
- **Recommending a new top-level module sets a real precedent** for how
  future Candidate Backlog items (World/ECS, Package System) might
  argue for their own module status. Mitigation: this spec's own
  reasoning is structural (Tools/Runtime's own "depended on by nothing"
  boundary), not a general "give everything its own module" argument —
  a future Spec proposing a new module still has to make its own case,
  per ADR-0032's own requirement that every future module-introducing
  Spec decide this explicitly, not inherit a precedent automatically.
- **This spec's own metadata/artifact format choices, once implemented,
  become a real compatibility surface** other content could start
  depending on before a second asset type or a real versioning/migration
  need has ever exercised them. Mitigation: `schema_version` fields are
  fixed as mandatory from this spec's first implementation, per
  ADR-0034's own "stable boundary" principle, so that future format
  evolution has a version to gate on — no migration *mechanism* is built
  now, matching ADR-0034's own explicit "no migration mechanism this ADR
  itself provides" disclosure.

## Out of Scope / Future Work

- **Rename-stable, GUID-with-sidecar identity** — real future work,
  once a concrete multi-author or frequent-rename workflow justifies its
  added bookkeeping cost over this spec's own path-derived default.
- **A real, shared derived-data cache/database** — future work once a
  second cook target (e.g. a future Android-specific cook path) or a
  shared build farm creates genuine need beyond what CMake's own
  incremental-build tracking already provides.
- **A second or later asset type** (textures — blocked on a future RHI
  Spec adding a general sampled `Texture`/`Sampler` capability first;
  materials-as-data; audio; anything else) — each a future spec's own
  scope, once a real consumer needs it.
- **`glTF`/`Assimp` (or any other external interchange format) import**
  — future work if and when a real multi-mesh/multi-material authoring
  workflow makes this project's own one-fixture, hand-rolled format
  insufficient.
- **Android cook-target implementation and verification** — this spec
  states the artifact-format *principle* only (see Requirements); a
  future spec implements and verifies it once Android Platform
  (Candidate Order 1) itself lands.
- **Hot-reload, live-asset-watching, or any incremental in-process
  re-import.** This spec's importer/cooker is a one-shot, explicitly-
  invoked (or build-time-integrated) tool, never a running/watching
  process.
- **Asset streaming, paging, or any partial/progressive load.**
- **A world/scene-level asset reference or dependency graph** (an asset
  referencing another asset by ID) — this spec's own one asset type has
  no cross-asset reference to resolve; a future spec with a real
  multi-asset dependency (e.g. a material referencing a texture)
  designs that graph when it exists to design against.
