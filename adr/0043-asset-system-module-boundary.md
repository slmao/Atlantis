# ADR 0043: Asset System — Module Boundary and Dependency Boundary

- **Status:** Proposed
- **Date:** 2026-08-18
- **Deciders:** &lt;pending Human Review&gt;
- **Related Spec:** [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)
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
own runtime-loading half must be linkable by a future Renderer-adjacent
consumer (an example, a test, eventually a future Runtime itself) —
neither existing candidate host satisfies that without first reopening
its own boundary, which
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

- **Depends on:** Atlantis Core (`Result<T,E>`, logging, assertions) and
  Atlantis RHI (`Buffer` creation via `atlantis::renderer::createMesh()`'s
  existing, unmodified surface). May depend on Atlantis Shader System's
  public `rhi_integration` surface
  (`atlantis::shader_system::rhi_integration::toVertexInputLayout()`) to
  resolve the `VertexInputLayout` its one supported asset type is cooked
  against — never Shader System's own private JSON parser or private
  implementation.
- **Depended on by:** nothing yet, within this ADR's own scope — its
  consumers (examples, tests, and eventually a future Atlantis Runtime)
  sit outside Asset System itself, the same way Renderer's own consumers
  do today.
- **Atlantis Tools** hosts the importer/cooker's own command-line entry
  point (`atlantis_asset_cooker`, exact name a Plan-stage detail),
  mirroring `atlantis_shader_compiler`'s own precedent: a Tools-hosted
  CLI invoking a separate module's own library
  (`Atlantis::AssetSystem`) to do the real work, never containing that
  logic itself. This keeps Tools' own existing, real scope (currently:
  shader-compiler content only) additive, without requiring Tools itself
  to become linkable.
- **Renderer, RHI, and Vulkan Backend gain no new dependency.** Asset
  System depends on their existing public output types
  (`atlantis::renderer::Mesh`, `atlantis::rhi::VertexInputLayout`); none
  of the three is modified to depend back on Asset System, and none of
  their existing public APIs is changed by this decision.
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
- Renderer, RHI, and Vulkan Backend remain entirely untouched — zero
  risk to any already-`Accepted`/implemented public API.
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
- **Keep the module-boundary and data-format/dependency decisions in one
  combined ADR**, as originally drafted. Reconsidered and rejected at
  Human Review's own request: the two decisions are independently
  evolvable (see Revision History and Consequences), and this project's
  own precedent (ADR-0032–0037, six separate ADRs from one spec) favors
  one ADR per decision over a combined one, once a spec produces more
  than a single architectural decision.
