# ADR 0043: Asset System — Module Boundary and Data Format/Dependency

- **Status:** Proposed
- **Date:** 2026-08-18
- **Deciders:** &lt;pending Human Review&gt;
- **Related Spec:** [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)

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

Separately, Asset System's authoring source and runtime artifact both
need a concrete data format. No parsing, hashing, serialization, or
3D-model-interchange dependency exists anywhere in `src/` today —
confirmed by direct inspection (`src/core/include/atlantis/` contains
only `assert.h`, `log.h`, `result.h`; no hash utility of any kind exists
in this repository). [ADR-0006](0006-dependency-management.md) requires
any dependency that does not fit the "small, pinned `FetchContent`
source dependency" model to get its own ADR — this is that ADR for
Asset System's own data formats.

## Decision

### Module boundary

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

### Data formats — hand-rolled, dependency-free

**Both the authoring source format and the runtime artifact format are
new, hand-rolled, dependency-free formats scoped exactly to this Spec's
one supported asset type (a static triangle mesh: per-vertex position
and color, `std::uint16_t` indices) — no third-party parsing, model-
interchange, hashing, or serialization dependency is introduced.**

- **Authoring source format:** a small, human-readable, flat text
  format a human can author and diff in an ordinary PR — a fixed,
  documented `key: value`/list structure sufficient to describe a
  vertex list and an index list, in the same spirit as (not the same
  code as, and not sharing an implementation with)
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own hand-rolled sidecar format. Exact field layout is a Plan-stage
  detail, fixed by Requirements in
  [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md).
- **Runtime artifact format:** a small, versioned binary layout — a
  fixed header (magic bytes, `schema_version`, vertex count, index
  count) followed by raw vertex bytes matching a `VertexInputLayout`,
  followed by raw `std::uint16_t` index bytes — loaded directly into the
  exact shape `atlantis::renderer::createMesh()` already expects, with
  no transformation at load time beyond a byte-count/header-consistency
  check. Not a general-purpose binary serialization format; specific to
  this one asset type's own two flat arrays. **Byte order:** all
  multi-byte fields (header integers, vertex floats, index values) are
  written and read in the host's native byte order — little-endian on
  every currently-relevant Atlantis target (x86-64 Windows development,
  ARM/AArch64 Android) — with no explicit endianness marker or
  conversion step, matching how `atlantis::renderer::createMesh()`
  already consumes raw bytes today. No big-endian target exists or is
  planned for Atlantis; a future one would require this format to add an
  explicit endianness marker first — a disclosed limitation, not
  designed against now.
- **Metadata sidecar format:** a strict, versioned flat text format
  (Asset ID, source-file identity, importer version, per-asset-type
  fields, `schema_version`), implemented as new, independent parsing
  code — never a dependency on, or shared implementation with,
  `tests/image_regression/support/provenance.*` (test-only code, outside
  any `src/` module's dependency surface).
- Both formats carry a **mandatory `schema_version` field from their
  first implementation**, satisfying
  [ADR-0034](0034-stable-public-boundary-versus-internal-cpp-layout.md)'s
  own "stable public boundary" requirement — no migration *mechanism* is
  built now (matching ADR-0034's own explicit disclosure that it
  supplies no versioning scheme or migration mechanism itself), only the
  version marker a future migration would need to gate on.

### No new third-party dependency

No `glTF`/`Assimp`/other 3D-interchange parser, no UUID-generation
library, no hashing library, and no JSON/YAML/TOML parser is added by
this ADR. Every format and mechanism this Spec's own scope needs is
achievable with the C++ standard library alone — see Alternatives
Considered for the specific alternatives weighed and why each is
deferred, not adopted, at this Spec's scope.

## Consequences

### Positive

- Gives Asset System the same, already-proven module shape Shader
  System successfully used: an authoring format, a Tools-hosted CLI, and
  a runtime-consumed library — a repeatable pattern, not a one-off
  design.
- Renderer, RHI, and Vulkan Backend remain entirely untouched — zero
  risk to any already-`Accepted`/implemented public API.
- Zero new third-party dependency means zero new license, build, or
  long-term-maintenance surface to manage — consistent with this
  project's own demonstrated default (ADR-0041's `stb` decision is the
  one exception in this repository's history, justified there by PNG's
  own real external-interchange need; no such need exists for this
  Spec's own internal-only formats).
- A mandatory `schema_version` field from day one avoids the same
  retrofit risk a missing version marker would otherwise create once a
  second asset type or a real compatibility need arises.

### Negative / Trade-offs

- A new, tenth top-level module is a real, permanent structural
  commitment — once `Atlantis::AssetSystem` exists as a module other
  code links against, folding it back into an existing module later
  would be a breaking, disruptive change, not a free refactor.
- Two new hand-rolled data formats (authoring source, runtime artifact)
  are two more formats this project must maintain and document, instead
  of reusing an established, widely-supported interchange format a new
  contributor might already know.
- `docs/architecture/module_boundaries.md`'s own current "asset
  processing" language under Tools becomes stale the moment this ADR is
  `Accepted`, until a future Plan reconciles it — a known, disclosed
  documentation-lag cost, matching the same kind of gap
  [ADR-0032](0032-conceptual-architecture-layers-versus-source-module-ownership.md)
  already accepted for that same document.

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
  boundary now, as a side
  effect of this Spec, would itself be a larger architectural change
  than a foundation-scoped Asset System Spec should make unilaterally.
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
- **Adopt `glTF`** (via `cgltf`, a small, single-header, MIT-licensed C
  parser, or the larger `tinygltf`/Assimp) **as the authoring source
  format.** Considered in real depth, not dismissed reflexively:
  `cgltf` specifically would be a small, permissively-licensed,
  `FetchContent`-compatible dependency in the same weight class as
  `stb` (ADR-0041's own precedent). Rejected as this Spec's *default*
  regardless: this Spec's own scope is exactly one hand-known, eight-
  vertex cube — glTF's own scene-graph, material, animation, texture,
  and skinning surface (the overwhelming majority of what any glTF
  parser exists to read) has no consumer in this Spec's scope at all.
  Adopting a general interchange-format parser to read eight vertices is
  exactly the "dependency justified by a future, anticipated need rather
  than a present, real one" pattern
  [ADR-0006](0006-dependency-management.md)'s own dependency discipline
  warns against. Revisit once a real multi-mesh, multi-material
  authoring workflow exists to justify it — at that point, `cgltf`'s
  small size, permissive (MIT) license, and single-header/`FetchContent`
  compatibility make it a strong future candidate, not a rejected one.
- **A UUID-generation library** (e.g. `stduuid`, `boost::uuid`) for a
  random-GUID identity scheme. Rejected alongside the identity-scheme
  decision itself (see
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)):
  this Spec's own recommended path-derived identity scheme needs no
  random-number-based UUID generation at all, so the dependency question
  does not arise under that recommendation; it would arise again if
  Human Review instead directs a random-GUID scheme, at which point this
  ADR would need its own revision.
- **A general hashing library** (e.g. `xxHash`, `wyhash`) for the
  content-hash-based cache key
  ([ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)
  recommends one). Rejected for this Spec's own scope: a hand-rolled,
  well-known, public-domain-equivalent algorithm (e.g. FNV-1a, a few
  lines of C++) is sufficient for a non-cryptographic, single-build-tree
  cache key at this Spec's one-fixture scale, matching this project's
  own existing precedent of hand-rolling small algorithms (CRC32/Adler32
  for `tests/image_regression/png_codec_tests.cpp`'s own hand-assembled
  PNG test fixture) rather than reaching for a dependency. Revisit if a
  future spec's real performance/collision-resistance requirements
  outgrow a hand-rolled hash.
- **A JSON/YAML/TOML parser** for the authoring source and/or metadata
  formats. Rejected for the same reason
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  already rejected one for the image-regression sidecar: every field
  this Spec's own formats need is a small, flat, non-nested set of
  scalar values with no legitimate need for a general markup language's
  nesting/escaping/Unicode-handling machinery.
