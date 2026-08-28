# ADR 0058: Static Mesh Vertex Layout — UV0 Attribute, Schema Version, and Sampling Convention

- **Status:** Accepted
- **Date:** 2026-08-25
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-25 as part of Spec 0017's Human Review Approval
- **Related Spec:** [specs/0017-mesh-uv-attribute-foundation.md](../specs/0017-mesh-uv-attribute-foundation.md) (`Approved`)
- **Acceptance Record (2026-08-25):** Accepted by Human Review as part
  of [specs/0017-mesh-uv-attribute-foundation.md](../specs/0017-mesh-uv-attribute-foundation.md)'s
  own Human Review Approval (2026-08-25) — see that Spec's own approval
  note for the specific Decision items this ADR corresponds to. This
  record does not change this ADR's own Decision, Consequences, or
  Alternatives Considered below.
- **Related ADR(s):**
  [ADR-0043](0043-asset-system-module-boundary.md) (Asset System module
  boundary — unaffected; this ADR changes the mesh vertex *layout*, not
  the module's own dependency surface or CPU/GPU boundary),
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)
  (Asset ID/logical-path identity — unaffected; a vertex-layout change
  does not touch how a logical path becomes an Asset ID),
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format, versioning, and dependency policy — **directly
  narrowed**: its own already-`Accepted` Decision text names the
  runtime artifact/authoring source formats as scoped to "a static
  triangle mesh: per-vertex position and color" — this ADR widens that
  to "position, color, and UV0"; ADR-0045's own "Accepted Amendment —
  2026-08-25" section, accepted in the same Human Review pass as this
  ADR, records that widening, matching this repository's own
  established amendment pattern — see ADR-0041's own "Accepted
  Amendment — 2026-08-24" section for the precedent this follows),
  [ADR-0055](0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)–[ADR-0056](0056-texture-upload-resource-state-and-descriptor-binding.md)
  (RHI `SampledTexture`/`Sampler`, `VertexAttributeFormat::Float2` — the
  already-`Accepted`, already-implemented infrastructure this ADR's own
  GPU proof reuses unmodified; this ADR adds no new RHI type or public
  API).

## Context

[Spec 0016](../specs/0016-texture-sampler-foundation.md) delivered real
`SampledTexture`/`Sampler` RHI types, a RenderGraph sampled-resource
binding kind, a one-time CPU→GPU upload path, and `Material`'s optional
texture+sampler pair — all proven end to end on real GPU hardware. But
the UV0 attribute that proof depends on is entirely
`tests/image_regression/fixture/textured_quad_fixture.cpp`'s own
hand-authored, in-fixture vertex data (`struct Vertex { float
position[3]; float uv[2]; };`, four constant `Vertex` literals) — no
Asset-System-sourced static mesh anywhere in this codebase carries UV
data. Spec 0016's own registry record names this gap explicitly: "Mesh
UV Attribute Foundation (real UV0 inside Asset System's own mesh
pipeline, required before any future asset-sourced-textured-mesh
claim) remains a named, immediate, blocking follow-up candidate."

The static mesh vertex shape is fixed, end to end, by real, current
code — confirmed by direct inspection, not assumption:

- `mesh_source.h`'s `MeshSourceVertex` is exactly `{positionX, Y, Z,
  colorR, G, B}` — six `float` fields, no more.
- `mesh_source.cpp`'s `parseMeshSource()` hard-requires exactly 6
  space-separated tokens per `vertex:` line (`fields.size() != 6` is a
  parse error) and hard-matches the authoring grammar's own version
  line (`atlantis_static_mesh_source_version: 1`) as an exact string,
  not merely a numeric check — any other text on that line is
  `UnknownSourceVersion`.
- `mesh_artifact.h` fixes `kMeshArtifactVertexStrideBytes = 24` (six
  32-bit floats) as **one single, global, compile-time constant** —
  `decodeMeshArtifact()` rejects any artifact whose header declares a
  different stride with `ArtifactDecodeError::UnsupportedVertexStride`.
  There is no per-asset variable vertex layout anywhere in this
  codebase today; every static mesh asset that exists or could exist
  shares the one shape this constant fixes.
- [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  own already-`Accepted` Decision text states this in so many words:
  "scoped exactly to this Spec's one supported asset type (a static
  triangle mesh: **per-vertex position and color**, `std::uint16_t`
  indices)."
- `StaticMeshAssetData` (the CPU-side output type `loadStaticMeshAsset()`
  returns) is already layout-agnostic — a tightly-packed byte buffer
  plus a runtime `vertexStrideBytes` field, not a hardcoded struct. It
  requires no change at all to carry a wider per-vertex layout.
- `atlantis::renderer::createMesh()` never inspects vertex layout either
  — its own `VertexInputLayout` parameter is explicitly unread
  (`static_cast<void>(layout);`, "Reserved, not currently read"); it
  only `memcpy`s whatever bytes and byte count it is given into a GPU
  buffer. The real vertex-attribute-to-shader-location binding happens
  one layer up, at `Device::createPipeline()` time, via
  `PipelineCreateParams::vertexInputLayout` — built by each composition
  root's own call to `toVertexInputLayout()`
  (`shader_system/rhi_integration`, Spec 0008/0016), which
  cross-validates a caller-supplied `MeshVertexAttributeSchema` (a
  `{location, offsetBytes}` list) against the real shader's own
  reflected `vertexInputAttributes` — matching by `location` and
  requiring equal *counts*, never inspecting total vertex stride versus
  attribute-referenced bytes. A shader that does not declare a given
  attribute (e.g. `minimal_mesh.slang`'s `VertexInput { position;
  color; }`, no UV input) is therefore unaffected by extra, unreferenced
  bytes elsewhere in that same vertex's own stride — confirmed directly
  against `minimal_mesh.slang`/`textured_quad.slang`'s own real source
  (`shaders/minimal_renderer/`, `shaders/textured_quad/`) and the
  `toVertexInputLayout()` implementation itself
  (`vertex_input_mapping.cpp`).
- Every composition root that already builds a `VertexInputLayout` for
  an Asset-System-sourced mesh does so via its own hardcoded, local
  `struct Vertex { float position[3]; float color[3]; };` and
  `sizeof(Vertex)` passed as `strideBytes` — **not** derived from
  `StaticMeshAssetData::vertexStrideBytes()` at runtime. Four such call
  sites exist today:
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_loaded_fixture.cpp`, and
  `src/runtime/src/runtime_application.cpp`. Widening the artifact's own
  per-vertex byte layout therefore requires each of these four files'
  own local `Vertex` struct to widen in lockstep (a real, disclosed,
  mechanical consequence — not a hidden one), even though none of their
  own shaders (`minimal_mesh.slang`) declares or consumes a UV input.
- `atlantis::rhi::Sampler::addressMode()` (`AddressMode::Repeat` /
  `ClampToEdge`, Spec 0016/ADR-0055) already gives out-of-`[0,1]` UV
  sampling a real, well-defined GPU meaning — vertex-data-level UV
  clamping would duplicate, and could silently conflict with, that
  existing sampler-level contract.
- `tests/image_regression/fixture/textured_quad_fixture.cpp`'s own
  comment already states the UV-origin convention this codebase uses:
  "v=0 at the top row, v=1 at the bottom row, matching the texture
  artifact's own 'row 0 = first-decoded row (top), no vertical flip'
  contract." **This convention already ships in production code, but
  its own absolute correctness has not actually been independently
  verified** — the existing `textured_quad` golden and its own human
  visual review (recorded on
  [PR #80](https://github.com/slmao/Atlantis/pull/80)) only confirm a
  non-degenerate, self-consistent checkerboard render; no test asserts
  an absolute expected color at a specific, known UV-mapped screen
  position, and a regular, even-row checkerboard is visually
  near-indistinguishable from its own vertically-flipped, color-inverted
  form to a human reviewer. See
  [Spec 0017](../specs/0017-mesh-uv-attribute-foundation.md)'s own
  Decision item 11 for the full disclosure and the explicit Human
  Review choice this raises.

Given all of the above, the real design question this ADR settles is
narrow: does UV0 become a **mandatory** part of the one, single, shared
static-mesh vertex layout (matching every existing precedent — one
fixed shape, no per-asset variability, exactly how position/color
already work), or does this Spec build a **new, currently-nonexistent**
per-asset variable-layout/variant mechanism to make UV0 optional? The
second path is real, uncontracted new architecture this codebase has
never needed before; the first path is a direct, minimal extension of
the one mechanism that already exists.

## Decision

**UV0 becomes a mandatory third attribute of the one, single static
mesh vertex layout — position (3 floats) + color (3 floats) + UV0 (2
floats), 8 floats / 32 bytes per vertex, replacing the current 6-float
/ 24-byte layout. No optional/variant vertex-layout mechanism is
introduced.**

1. **Authoring grammar:** each `vertex:` line gains two trailing
   space-separated float tokens (`x y z r g b u v`, extending the
   existing exactly-6-field line to exactly 8) —
   `MeshSourceVertex` gains `uvU`/`uvV` fields. The grammar's own
   version marker (`atlantis_static_mesh_source_version: 1`) becomes
   `atlantis_static_mesh_source_version: 2`; a source file declaring
   any other value is rejected with the existing
   `SourceParseError::UnknownSourceVersion` — no dual-version reader,
   matching this format's own established single-supported-version
   precedent (the runtime artifact and metadata sidecar formats already
   work the same way).
2. **Runtime artifact layout:** per-vertex bytes become position xyz
   (offset 0), color rgb (offset 12), UV0 uv (offset 24) — 32 bytes
   total. `kMeshArtifactVertexStrideBytes` becomes `32`;
   `kMeshArtifactSchemaVersion` becomes `2`. An artifact whose header
   declares schema version 1 or any stride other than 32 is rejected
   with the existing `ArtifactDecodeError::UnknownSchemaVersion` /
   `UnsupportedVertexStride` — no migration reader. Every multi-byte
   field (including the two new UV floats) is serialized with the
   exact same explicit shift/mask little-endian routine
   (`appendFloatLE`/`readFloatLE`) every existing float already uses —
   no new serialization primitive, matching
   [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
   own unconditional little-endian contract verbatim.
3. **No migration mechanism, no back-compat reader, no silent default.**
   There is no independently-distributed copy of a mesh artifact
   anywhere — every artifact is a build output, deterministically
   regenerated from its own checked-in authoring source at build time.
   "Migrating an existing mesh asset" therefore means re-authoring its
   `.mesh.txt` source file with real UV columns and re-cooking it, not
   converting a shipped binary. Every currently-authored mesh source
   file in this repository is re-authored with explicit UV values as
   part of this Spec's own Implementation — none is left on the old
   6-field grammar, and none is silently defaulted to `(0, 0)` UV by
   the cooker itself.
4. **Value contract:** UV floats must be finite — the exact same
   per-vertex finiteness check `parseMeshSource()`/`decodeMeshArtifact()`
   already apply to all six position/color floats (`NonFiniteFloat`)
   extends, unchanged in kind, to cover eight floats instead of six.
   No `[0, 1]` range check, clamp, or rejection is added at the
   authoring, cook, or decode layer — `Sampler::addressMode()`
   (`Repeat`/`ClampToEdge`) is the existing, correct layer for
   out-of-range UV sampling behavior; duplicating or second-guessing it
   in vertex data is explicitly rejected (see Alternatives Considered).
5. **UV origin/convention:** `v = 0` corresponds to the sampled
   texture's first-decoded (top) row; `v = 1` corresponds to its
   last-decoded (bottom) row — adopting, verbatim, the exact convention
   `textured_quad_fixture.cpp` already established and already ships in
   production code. This ADR invents no new convention; it extends the
   existing one to asset-sourced authoring. **This ADR does not claim
   that convention's own absolute correctness has been independently
   verified** — see Context above and
   [Spec 0017](../specs/0017-mesh-uv-attribute-foundation.md)'s own
   Decision item 11 for the disclosed limitation and the Human Review
   choice it raises.
6. **Attribute location convention — closed at the real Vulkan pipeline
   level, not merely asserted:** `location` numbering is per-shader, not
   a global registry any code enforces — each shader's own
   `[[vk::location(N)]]` declarations are matched only against that same
   shader's own real reflected input list, by `toVertexInputLayout()`
   (Spec 0016/0008), which requires the caller's `MeshVertexAttributeSchema`
   to have exactly the shader's own reflected attribute *count*, matched
   by `location`. A shader declaring all three attributes uses
   `location(0)` = position, `location(1)` = color, `location(2)` = UV0,
   matching this codebase's own established declaration-order
   convention. A shader declaring only a subset uses whatever `location`
   numbers *that shader itself* assigns to *its own* inputs —
   `minimal_mesh.slang` (`position@0, color@1`, no UV input) and
   `textured_quad.slang` (`position@0, uv@1`, no color input, so its own
   `location(1)` is UV, not color) are both already real, shipped
   examples of this, confirmed by direct inspection of both `.slang`
   files' own `VertexInput` structs.

   This is not merely a C++-level schema-matching claim — it is closed
   at the real Vulkan pipeline construction itself
   (`src/vulkan_backend/src/vulkan_device.cpp`,
   `Device::createPipeline()`): exactly **one**
   `VkVertexInputBindingDescription` is built, with `stride =
   params.vertexInputLayout.strideBytes` (the mesh's own real, full
   per-vertex byte size); the `VkVertexInputAttributeDescription` array
   is built by iterating `params.vertexInputLayout.attributes`
   **only** — one entry per attribute the caller's `VertexInputLayout`
   actually lists, each with its own independent `location`, `format`,
   and `offset` within that one binding. Vulkan's own vertex-fetch stage
   reads only the byte ranges named by an attribute description that
   exists; nothing in `VkPipelineVertexInputStateCreateInfo` requires
   attributes to be contiguous, requires every byte of `stride` to be
   covered by some attribute, or requires attribute order to follow
   offset order. A "middle" or "trailing" region of a vertex's own
   stride with no corresponding `VkVertexInputAttributeDescription`
   entry is therefore not read by that pipeline at all — confirmed by
   this real construction code, not inferred from the RHI-level
   abstraction alone. This is exactly what makes both
   `minimal_mesh.slang`'s own unused UV region (offset 24–31 of a
   32-byte stride) and a UV-only shader's own unused color region
   (offset 12–23) safe: each pipeline's own attribute list simply never
   names that offset.
7. **No new error enumerator anywhere.** `SourceParseError`,
   `ArtifactDecodeError`, `CookError`, and `AssetLoadError`'s existing
   categories — `MalformedNumber`, `NonFiniteFloat`, `CountMismatch`,
   `UnknownSourceVersion`/`UnknownSchemaVersion`,
   `UnsupportedVertexStride`, `MetadataArtifactMismatch`, and so on —
   already generalize correctly once each function's own loop bound or
   field-count check moves from 6 to 8; confirmed by direct inspection
   of every function this Spec's own Implementation touches. No new
   `TruncatedInput`/`Overflow`/`CorruptUv`-shaped enumerator is
   introduced anywhere.

## Consequences

### Positive

- Exactly one vertex layout to reason about, build, and test — no new
  per-asset schema-selection mechanism, no branching artifact reader,
  no new public API surface anywhere in Asset System, RHI, RenderGraph,
  or Renderer.
- The GPU proof this ADR's own Spec requires reuses 100% of Spec 0016's
  already-`Accepted`, already-implemented `SampledTexture`/`Sampler`/
  RenderGraph-binding/`Material` surface untouched — this ADR adds zero
  new RHI or RenderGraph type.
- Near-zero new error surface: every rejection path this Spec's own
  Implementation needs already exists under an existing enumerator.
- `StaticMeshAssetData`, `loadStaticMeshAsset()`, and
  `atlantis::renderer::createMesh()` all require **zero source change**
  — each was already layout-agnostic by construction, confirmed by
  direct inspection, not merely assumed.

### Negative / Trade-offs

- Every static mesh asset — including ones with no texture and no
  intention of ever having one — now carries 8 bytes per vertex of
  UV0 data it may never read. This is a real, permanent per-vertex
  size cost, accepted here in exchange for not building a
  per-asset-variable-layout mechanism this codebase has no other need
  for yet.
- Four existing composition-root call sites
  (`minimal_cube_fixture.cpp`, `world_scene_fixture.cpp`,
  `world_scene_loaded_fixture.cpp`, `runtime_application.cpp`) each
  hardcode a local `Vertex` struct/`sizeof(Vertex)` for
  `VertexInputLayout::strideBytes`, independent of
  `StaticMeshAssetData::vertexStrideBytes()`'s own real, dynamic value.
  Each must be mechanically widened in lockstep with this ADR's own
  vertex-layout change, or silently corrupt every vertex fetch for the
  meshes it draws. This Spec's own Implementation must touch, test, and
  disclose all four; none may be missed.
- This ADR narrows an already-`Accepted` sentence of
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md).
  An Accepted Amendment to that ADR, recording the widened vertex
  shape, is required once this ADR itself reaches `Accepted` — a real,
  disclosed follow-up action, not performed by this document.

## Alternatives Considered

- **Optional UV via a second, distinct mesh asset kind or authoring
  grammar variant** (e.g. `static_mesh` vs. `static_mesh_textured`).
  Rejected: this would require a real, new per-asset vertex-schema
  selection mechanism — nothing in Asset System, the cooker CLI, or the
  loader has ever needed to pick between two mesh shapes before, and
  building that machinery for exactly two shapes is disproportionate,
  speculative scope this Spec's own Non-Goals explicitly exclude (see
  Spec 0017's own "general vertex-schema system" Non-Goal). It would
  also split the single, simple mesh-format guarantee
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  was written to keep.
- **A caller/data-driven vertex stride** — `decodeMeshArtifact()` trusts
  whatever stride the artifact header declares, rather than checking it
  against one compile-time constant. Rejected for the same reason: this
  is real new flexibility nothing in this codebase currently exercises
  or needs, and it would let a single, malformed or unexpected artifact
  silently drive an arbitrary vertex byte layout no schema validates.
- **A general, reusable N-attribute vertex-schema/descriptor system**
  (any caller-declared combination of position/color/UV/normal/tangent/
  etc., per mesh). Rejected as exactly the "no speculative abstraction"
  pattern [AGENTS.md](../AGENTS.md) warns against — no second concrete
  attribute combination exists anywhere in this codebase to design
  against yet; `VertexAttribute`/`VertexInputLayout`/
  `MeshVertexAttributeSchema` (Spec 0007/0008/0016) already provide the
  general RHI-level mechanism a future such system would sit on top of,
  without this ADR needing to build the mesh-authoring side of it now.
- **Clamp UV to `[0, 1]` at cook or decode time.** Rejected:
  `Sampler::addressMode()` (`Repeat`/`ClampToEdge`, already `Accepted`
  and implemented, Spec 0016/ADR-0055) is the correct, existing layer
  for this; clamping vertex data would silently discard a legitimate
  `Repeat`-tiling author intent with no way to opt back out, and would
  duplicate a decision RHI already owns.
- **Default UV to `(0, 0)` for every vertex of every currently-authored
  mesh, rather than re-authoring real values.** Rejected: a silent,
  uniform default would make this ADR's own required GPU proof (a real,
  asset-sourced, UV-varying textured mesh) impossible to build from any
  existing authored source, and would leave every migrated mesh's own
  UV data meaningless rather than deliberately authored — see Spec
  0017's own migration decision for the full disclosure.

## Proposed Amendment — 2026-08-29

**Status: Proposed.** Drafted 2026-08-29 alongside
[Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md) and
[ADR-0063](0063-static-mesh-normal-attribute-schema-version-and-convention.md),
awaiting Human Review in the same pass as that Spec's own approval.
Everything above this section remains this ADR's own original,
unmodified `Accepted` Decision (this ADR's own top-level `Status:
Accepted` above is unchanged and unaffected by this proposed amendment)
— this amendment does not alter, narrow, or reinterpret any of it as
originally written; it proposes narrowing this ADR's own "the one,
single static mesh vertex layout is exactly position + color + UV0"
statement under the Decision below, pending Human Review.

**Related:**
[Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)
(`In Review`),
[ADR-0063](0063-static-mesh-normal-attribute-schema-version-and-convention.md)
(`Proposed`) — this Amendment and ADR-0063 cross-reference each other,
mirroring this ADR's own existing relationship with ADR-0045: ADR-0063
makes the actual normal-attribute vertex-layout decision; this
Amendment records the corresponding, narrower change to this ADR's own
already-`Accepted` "one, single vertex layout" Decision, so the two
documents do not duplicate one another's content. Both await Human
Review together, in the same pass as Spec 0020's own approval.

### Context for this amendment

This ADR's own `Accepted` Decision (above, unmodified) states "UV0
becomes a mandatory third attribute of the one, single static mesh
vertex layout — position (3 floats) + color (3 floats) + UV0 (2
floats), 8 floats / 32 bytes per vertex" — a closed statement that, by
naming exactly three attributes and a fixed 32-byte size, implicitly
fixes the vertex layout's own attribute count. Spec 0020 widens the one
static mesh vertex layout to add a fourth, mandatory attribute,
object-space normal — a real, direct narrowing of that closed statement,
not a compatible extension it already permitted. Per this repository's
own established practice (an `Accepted` ADR's original Decision text is
never silently rewritten; a scope change gets its own, separately-dated,
clearly-labeled amendment — this ADR's own Decision text above already
follows this same discipline relative to ADR-0045's own prior scope
statement), this section is drafted alongside Spec 0020 and ADR-0063, so
Human Review can evaluate the normal-attribute decision and its own
required consequence to this ADR in one pass.

### Proposed Decision

Pending Human Review, this ADR's own Decision (above) would be amended,
in effect, to read:

- **Normal becomes a mandatory fourth attribute of the one, single
  static mesh vertex layout — position (3 floats) + color (3 floats) +
  UV0 (2 floats) + normal (3 floats), 11 floats / 44 bytes per vertex,
  replacing the current 8-float / 32-byte layout.** No optional/variant
  vertex-layout mechanism is introduced — this ADR's own "No
  optional/variant vertex-layout mechanism is introduced" sentence
  (Decision, above) continues to hold, now covering four attributes
  instead of three.
- **Authoring grammar:** each `vertex:` line gains three trailing
  space-separated float tokens (`nx ny nz`, extending the existing
  8-field line to 11) — `MeshSourceVertex` gains `normalX`/`normalY`/
  `normalZ` fields. The grammar's own version marker becomes
  `atlantis_static_mesh_source_version: 3`; a source file declaring any
  other value is rejected with the existing
  `SourceParseError::UnknownSourceVersion` — no dual-version reader,
  identical in kind to this ADR's own original version-1-to-2 rule.
- **Runtime artifact layout:** per-vertex bytes become position xyz
  (offset 0), color rgb (offset 12), UV0 uv (offset 24), normal xyz
  (offset 32) — 44 bytes total. `kMeshArtifactVertexStrideBytes`
  becomes `44`; `kMeshArtifactSchemaVersion` becomes `3`. An artifact
  whose header declares schema version 1 or 2, or any stride other than
  44, is rejected with the existing `ArtifactDecodeError::UnknownSchemaVersion`
  / `UnsupportedVertexStride` — no migration reader, identical in kind
  to this ADR's own original rule.
- **No migration mechanism, no back-compat reader, no silent default**
  — identical in kind to this ADR's own Decision item 3, above. Every
  currently-authored mesh source file is re-authored with a real,
  disclosed normal value as part of Spec 0020's own Implementation —
  none is left on the old 8-field grammar, none silently defaulted to
  `(0, 0, 0)`.
- **Value contract:** normal floats must be finite (this ADR's own
  Decision item 4's existing finiteness check, extended in kind from
  eight floats to eleven) **and, unlike position/color/UV0, must also
  fall within `[0.99, 1.01]` of unit length** — a new numeric
  requirement this ADR's own original Decision item 4 did not need for
  position/color/UV0 (which carry no comparable "correct magnitude"
  concept), checked independently at cook time and decode time via a
  new pair of enumerators, `SourceParseError::NonUnitNormal` /
  `ArtifactDecodeError::NonUnitNormal`. Neither layer ever rewrites,
  rescales, or auto-normalizes an authored normal.
- **Attribute location convention** (this ADR's own Decision item 6,
  above) continues unchanged in kind — a shader declaring a normal
  input assigns it whatever `location` number that shader itself
  chooses, matched only against that same shader's own reflected input
  list, exactly as position/color/UV0 already work; no shader in this
  codebase declares one yet (Spec 0020's own Pre-draft verification).
- **One new error-enumerator pair, `NonUnitNormal`** (parse-time and
  decode-time) — the one exception to this ADR's own Decision item 7
  ("No new error enumerator anywhere"), since no existing enumerator
  covers a "correct magnitude" check; every other new rejection path
  (wrong field count, wrong version, wrong stride) continues to reuse
  an existing enumerator, unchanged in kind.

### Consequences of this proposed amendment

No change to this ADR's own UV0-origin/sampling-convention Decision
(item 5, above), attribute-location-convention Decision (item 6), or
Vulkan-pipeline-level closure argument — this Amendment, if accepted,
narrows the vertex-layout attribute-count sentence and value-contract
sentence only. The negative/trade-off this ADR's own Decision above
already accepted (four existing composition-root call sites must widen
their own local `Vertex` struct in lockstep) recurs for this Spec's own
freshly, independently re-enumerated list (six real touch points, not
the four this ADR's own 2026-08-25 drafting named — see
[Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s own
Pre-draft verification for the full, current list and the two files
this ADR's own list predates).
