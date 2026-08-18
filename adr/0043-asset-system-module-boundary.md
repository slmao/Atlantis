# ADR 0043: Asset System — Module Boundary and Dependency Boundary

- **Status:** Accepted
- **Date:** 2026-08-18 (accepted 2026-08-19 — see Revision History)
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-19 as part of Spec 0012's Human Review Approval; see
  that spec's own Human Review Approval note for the full approval
  record and the three directed corrections this ADR carries.
- **Related Spec:** [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md) (`Approved`)
- **Related ADR(s):**
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format, versioning, and third-party dependency policy) —
  originally drafted as part of this same ADR, split out 2026-08-18 at
  Human Review's own request so each permanent decision has exactly one
  authoritative record, matching this project's own precedent of one
  ADR per independently-evolvable decision (ADR-0032–0037, drafted
  alongside Spec 0009). This ADR's own scope narrowed to the module-
  boundary decision alone as a result; see Revision History.
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)
  (identity, provenance, and import methodology) remains a separate,
  unaffected sibling.

## Revision History

- **2026-08-18 (original):** Combined the module-boundary decision with
  the data-format and third-party-dependency decisions in one ADR.
- **2026-08-18 (split):** Narrowed to the module-boundary decision only,
  at Human Review's own explicit request, on the grounds that the module-
  boundary question and the data-format/dependency question are
  independently evolvable — Human Review could reasonably accept one and
  revisit the other later, which a single combined ADR would make
  awkward. The data-format and dependency content that was here moved,
  unchanged in substance, to new
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md).
- **2026-08-19 (Human Review correction, then `Accepted`):** Human
  Review directed one correction to this ADR before accepting it: the
  original Decision had Asset System depending on **Atlantis RHI** (for
  `Buffer` creation) and optionally on Shader System's
  `rhi_integration` surface, which contradicted the module's own real
  job — Asset System transforms data and has no GPU concept of its own.
  The Decision now fixes a **Core-only** dependency, a CPU-side
  `StaticMeshAssetData` output boundary, and composition-root ownership
  of the GPU handoff, and explicitly forecloses an
  `AssetSystemRhiIntegration` submodule absent a demonstrated need
  raised as its own architectural question. This ADR then moved to
  `Accepted`. See
  [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)'s
  own Human Review Approval note.

## Context

Atlantis's nine top-level modules are named in
[AGENTS.md](../AGENTS.md) and detailed (status: `PROPOSED — pending
spec/ADR approval`) in
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md):
Core, Platform, RHI, Vulkan Backend, RenderGraph, Renderer, Shader
System, Runtime, Tools.
[ADR-0032](0032-conceptual-architecture-layers-versus-source-module-ownership.md)
(`Accepted`) requires that "every future Spec that introduces or
restructures a top-level module ... decides that module's real position
in both [the conceptual-layer and module-ownership] views explicitly, as
part of its own Architectural Impact section" — this ADR is that
decision for Asset System.

`module_boundaries.md`'s own current, `PROPOSED` text already assigns
"asset processing" as one of Atlantis Tools' Responsibilities, and
separately describes both Atlantis Tools ("**Depended on by:** nothing —
no runtime module ever depends on Tools") and Atlantis Runtime
("**Depended on by:** nothing (it's the executable)") as structural
leaves — nothing may link against either as a library. Asset System's
own runtime-loading half must be linkable by a future consumer (an
example, a test, eventually a future Runtime itself) — neither existing
candidate host satisfies that without first reopening its own boundary,
which
[specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)
is not scoped to do unilaterally.

A real, already-`Accepted`, already-shipped precedent for exactly this
shape exists in this repository: Atlantis Shader System (Spec 0008,
implemented via
[PR #36](https://github.com/slmao/Atlantis/pull/36)) is a real top-level
module with a Tools-hosted CLI (`atlantis_shader_compiler`) producing a
build-time artifact, and its own runtime-consumed library
(`Atlantis::ShaderSystem`, `Atlantis::ShaderSystemRhiIntegration`) other
modules link against directly — not folded into Tools despite Tools'
own draft language already covering "shader precompilation CLI" too.
This module split is itself fixed by
[ADR-0029](0029-shader-system-build-time-compilation-boundary.md)
(`Accepted`), which independently confirms — by real, shipped
`CMakeLists.txt` dependency edges, not only by `module_boundaries.md`'s
own `PROPOSED` text — that Atlantis Tools' CLI target depends on
`Atlantis::ShaderSystem`, and nothing in the repository depends back on
Tools. The "Tools is a leaf" structural claim this ADR relies on is
therefore grounded in an already-`Accepted` decision and its own real
build graph, not solely in the not-yet-`Accepted` draft language
`module_boundaries.md` also happens to state.

## Decision

**Asset System becomes a new, tenth top-level module: Atlantis Asset
System.** It is placed in the conceptual five-layer view (per
[ADR-0032](0032-conceptual-architecture-layers-versus-source-module-ownership.md))
alongside Render, in the "Runtime Services"/"Authoritative Runtime"
boundary — a non-binding, illustrative position only, per ADR-0032's own
terms. In the authoritative nine-module (now ten-module) source-ownership
view:

- **Depends on:** **Atlantis Core only** (`Result<T,E>`, logging,
  assertions) plus the C++ standard library. Asset System does **not**
  depend on Atlantis RHI, Renderer, Vulkan Backend, RenderGraph, Shader
  System, Platform, Runtime, or Tools — no GPU, windowing, graphics-API,
  or OS-process concept appears anywhere in its public surface or its
  implementation. This is the same narrow, Core-only shape
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)
  already fixed for `Atlantis::ShaderSystem`'s own base library, and for
  the same reason: a module whose real job is data transformation should
  not force every consumer to inherit a graphics dependency it may not
  need.
- **What Asset System produces and consumes is CPU-side data, not GPU
  resources.** The importer/cooker writes, and the runtime artifact
  loader reads, a strict CPU-side static-mesh data structure (working
  name `StaticMeshAssetData`; exact naming and layout a Plan-stage
  detail) carrying the vertex bytes, the index array, and the counts
  and per-asset-type metadata needed to interpret them. **Asset System
  never creates an `atlantis::renderer::Mesh`, never calls
  `atlantis::renderer::createMesh()`, never constructs an
  `atlantis::rhi::Buffer`, and never names an
  `atlantis::rhi::VertexInputLayout`** — every one of those is a GPU-side
  concept owned by modules Asset System does not depend on.
- **Depended on by:** nothing yet, within this ADR's own scope — its
  consumers (examples, tests, and eventually a future Atlantis Runtime)
  sit outside Asset System itself, the same way Renderer's own consumers
  do today.
- **The composition root — a test, an example, or eventually a future
  Atlantis Runtime — owns the GPU handoff.** It calls Asset System's own
  loader to obtain `StaticMeshAssetData`, separately resolves the
  `VertexInputLayout` through Atlantis Shader System's own public
  `rhi_integration` surface
  (`atlantis::shader_system::rhi_integration::toVertexInputLayout()` —
  never Shader System's private JSON parser or private implementation),
  and passes both into the **existing, unmodified**
  `atlantis::renderer::createMesh()`. This is exactly what
  `examples/headless_rendering_demo` and
  `tests/image_regression/fixture/minimal_cube_fixture.*` already do
  today with their own hand-authored vertex/index arrays; this Spec
  replaces the *source* of that CPU data, not the composition that
  consumes it.
- **No Renderer-integration submodule is introduced.** Unlike Shader
  System — which genuinely needed
  `Atlantis::ShaderSystemRhiIntegration` because its own reflection
  metadata must be *translated into* an `atlantis::rhi::VertexInputLayout`
  — Asset System's own output is plain CPU bytes and counts that
  `createMesh()` already accepts directly, with no translation layer in
  between. If a Plan or Implementation ever finds a concrete case where
  this genuinely cannot be avoided, that is a real architectural change:
  stop and raise it as its own explicit question rather than adding an
  integration submodule silently.
- **Atlantis Tools** hosts the importer/cooker's own command-line entry
  point (`atlantis_asset_cooker`, exact name a Plan-stage detail),
  mirroring `atlantis_shader_compiler`'s own precedent: a Tools-hosted
  CLI invoking a separate module's own library
  (`Atlantis::AssetSystem`) to do the real work, never containing that
  logic itself. **The dependency runs Tools → Asset System only; Asset
  System never depends on Tools.** This keeps Tools' own existing, real
  scope (currently: shader-compiler content only) additive, without
  requiring Tools itself to become linkable.
- **Renderer, RHI, Vulkan Backend, RenderGraph, and Shader System gain
  no new dependency, in either direction.** None of them is modified to
  depend on Asset System, none of their existing public APIs is changed
  by this decision, and Asset System does not depend on any of them.
- **No dependency on a future Atlantis Runtime module**, which does not
  yet exist as a real module (Candidate Order 3 in
  [specs/README.md](../specs/README.md) Section B, spec not yet
  drafted).

`docs/architecture/module_boundaries.md`'s own current draft language
("asset processing" under Tools' Responsibilities) is not `Accepted`
today and is not amended by this ADR itself — a future Plan reconciling
that document with this decision (removing the generic "asset
processing" phrase from Tools' own description, or narrowing it to "the
importer/cooker CLI entry point only") is left to whatever Plan
implements this Spec, per the same precedent
[ADR-0032](0032-conceptual-architecture-layers-versus-source-module-ownership.md)
itself already established for not touching that document.

This ADR fixes the module-boundary/dependency-graph question only. The
authoring-source, runtime-artifact, and metadata-sidecar data formats,
and whether any new third-party dependency is justified, are
[ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
own, separately-reviewable decisions — a module could in principle be
approved here while that format/dependency decision is still under
review, or vice versa, without either approval being incoherent on its
own.

## Consequences

### Positive

- Gives Asset System the same, already-proven module shape Shader
  System successfully used: an authoring format, a Tools-hosted CLI, and
  a runtime-consumed library — a repeatable pattern, not a one-off
  design.
- Renderer, RHI, Vulkan Backend, RenderGraph, and Shader System remain
  entirely untouched — zero risk to any already-`Accepted`/implemented
  public API, in either dependency direction.
- A Core-only dependency makes Asset System's own logic trivially
  unit-testable with no Vulkan device, no window, and no GPU present —
  the same practical benefit
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)'s own
  Core-only Shader System library already demonstrates, and a strictly
  narrower dependency surface than an RHI-dependent design would have.
- Keeping this decision separate from
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  means a future amendment to the data-format/dependency decision (e.g.
  adopting `cgltf` once a real multi-mesh workflow exists) does not
  require reopening or re-dating this module-boundary decision, and vice
  versa.

### Negative / Trade-offs

- A new, tenth top-level module is a real, permanent structural
  commitment — once `Atlantis::AssetSystem` exists as a module other
  code links against, folding it back into an existing module later
  would be a breaking, disruptive change, not a free refactor.
- `docs/architecture/module_boundaries.md`'s own current "asset
  processing" language under Tools becomes stale the moment this ADR is
  `Accepted`, until a future Plan reconciles it — a known, disclosed
  documentation-lag cost, matching the same kind of gap
  [ADR-0032](0032-conceptual-architecture-layers-versus-source-module-ownership.md)
  already accepted for that same document.
- Maintaining two sibling ADRs (this one and ADR-0045) for what began as
  one Asset System foundation decision is a small, real bookkeeping cost
  — mitigated by each one's own Related ADR(s) field cross-referencing
  the other, so a reader starting from either finds the other.

## Alternatives Considered

- **Host both the importer/cooker and the runtime-loading library under
  Atlantis Tools**, matching `module_boundaries.md`'s own current draft
  language. Rejected: `module_boundaries.md`'s own text states Tools is
  "depended on by: nothing — no runtime module ever depends on Tools,"
  which structurally forecloses a future Renderer-adjacent consumer
  linking against Tools-hosted runtime-loading code without first
  reopening Tools' own boundary — corroborated, not merely asserted by a
  not-yet-`Accepted` draft, by `Accepted` ADR-0029's own real, shipped
  Tools/Shader-System dependency edge (see Context). Reopening that
  boundary now, as a side effect of this Spec, would itself be a larger
  architectural change than a foundation-scoped Asset System Spec should
  make unilaterally.
- **Host runtime loading under Atlantis Runtime, once it exists.**
  Rejected: Runtime does not exist as a real module yet (Candidate Order
  3, spec not yet drafted), and `module_boundaries.md`'s own description
  states Runtime, too, is "depended on by: nothing (it's the
  executable)" — a leaf, not a library, by design. Waiting for Runtime
  to exist and then re-deciding this question would also contradict this
  Spec's own finding (see
  [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)'s
  "Why this does not wait for Runtime") that Asset System is not blocked
  on Runtime.
- **Fold runtime loading into Renderer directly.** Rejected:
  [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)'s
  own Non-Goals explicitly excluded "a scene graph, ECS, asset system, or
  a model/mesh loader" from Renderer's scope; this ADR does not reopen
  that settled boundary.
- **Have Asset System depend on RHI/Renderer and return a ready-made
  `atlantis::renderer::Mesh` directly**, rather than CPU-side
  `StaticMeshAssetData` a composition root converts. Rejected at Human
  Review (2026-08-19): it would force every Asset System consumer —
  including pure, GPU-free unit tests of the importer's own parsing and
  identity logic — to link RHI and Renderer and stand up a `Device` for
  no reason, and it would make Asset System's own module boundary
  strictly wider than its real job (data transformation) requires. The
  composition root already exists in every case that needs GPU upload
  (`examples/headless_rendering_demo`,
  `tests/image_regression/fixture/minimal_cube_fixture.*`, eventually a
  future Runtime), and already owns exactly this kind of wiring today —
  so routing the GPU handoff through it costs nothing that is not
  already being paid.
- **Add an `Atlantis::AssetSystemRhiIntegration` submodule**, mirroring
  `Atlantis::ShaderSystemRhiIntegration`. Rejected: Shader System needed
  that submodule because its reflection metadata must be *translated
  into* an `atlantis::rhi::VertexInputLayout` — a real, non-trivial
  mapping with one correct owner. Asset System's own output is plain CPU
  vertex/index bytes and counts that `createMesh()`'s existing signature
  already accepts unchanged, so there is nothing for such a submodule to
  translate. Adding one anyway would create the exact RHI dependency
  this ADR's own Decision exists to avoid.
- **Keep the module-boundary and data-format/dependency decisions in one
  combined ADR**, as originally drafted. Reconsidered and rejected at
  Human Review's own request: the two decisions are independently
  evolvable (see Revision History and Consequences), and this project's
  own precedent (ADR-0032–0037, six separate ADRs from one spec) favors
  one ADR per decision over a combined one, once a spec produces more
  than a single architectural decision.
