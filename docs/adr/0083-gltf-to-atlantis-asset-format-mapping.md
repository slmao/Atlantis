# ADR 0083: glTF-to-Atlantis Asset Format Mapping

- **Status:** Accepted
- **Date:** 2026-09-16 (accepted 2026-09-17)
- **Deciders:** slmao — Human Review, chat confirmation, no reviewing
  PR, alongside [Spec 0037](../specs/0037-gltf-importer.md)'s own
  Approval (2026-09-17)
- **Related Spec:** [Spec 0037: glTF 2.0 Importer](../specs/0037-gltf-importer.md) (`Approved`)
- **Related ADR(s):** [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (`Accepted`) — this ADR's own mesh/material/texture artifact formats
  are the target this decision maps into, unmodified in shape except
  where explicitly widened below. [ADR-0082](0082-gltf-parser-dependency-selection.md)
  (`Accepted`) — the parser this mapping consumes. [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Accepted`) — the `MaterialAssetData` metallic-roughness contract
  this decision converts *into*. [ADR-0073](0073-static-mesh-tangent-attribute-generation-and-schema.md)
  (`Accepted`) — the tangent-generation contract this decision must
  reconcile with glTF's own pre-computed `TANGENT` attribute (below).
  [Spec 0038: Block-Compressed Texture Support](../specs/0038-block-compressed-textures.md)
  (`Approved`) — D4 below records the human ruling (2026-09-15) that
  native GPU block-compressed texture support is a real RHI/Vulkan
  Backend capability this ADR's own Tools-only scope must not decide,
  and is instead carried by that independent Spec; this workflow's own
  texture milestone (D4) is a hard dependency on Spec 0038 landing
  first.

## Context

Spec 0036's own ADR obligation for workflow ① names, as one surface: "
format-mapping decisions — mesh attribute layout, material model mapping
..., texture/sampler mapping, scene-graph/node-hierarchy mapping,
coordinate-system convention." This ADR is that decision, grounded in
real measurement against the actual recommended Bistro source
(`NVIDIA-RTX/RTXDI-Assets`' `bistro/bistro.gltf` — Spec 0037's own
Investigation 1), not assumption. All figures below are Spec 0037's own
Investigation 1/2 findings, cited here rather than re-derived.

## Decision

### D1 — Mesh data bypasses the ASCII authoring-source format; the
importer emits cooked `.amesh` artifacts directly

**Quantified basis** (Spec 0037 Investigation 2): Bistro's own real
scale is 1,738,262 vertices and 1,753,630 triangles across 551 meshes.
Extrapolating the existing hand-authored `.mesh.txt` authoring-source
format's own real per-line text overhead (measured directly against
`assets/meshes/pbr_pedestal.mesh.txt`/`pbr_dimpled_sphere.mesh.txt`, ADR-0045
as amended) to this scale yields an estimated **~282 MB** of generated
intermediate ASCII source text (≈140 bytes/vertex line, ≈30 bytes/index
line, conservative for the longer float precision a real FBX2glTF-
sourced conversion carries versus this codebase's own hand-typed,
round-number synthetic meshes) — versus an estimated **~120 MB** for
the equivalent cooked binary artifact (ADR-0045's own 60-bytes/vertex
runtime stride, `uint32_t` indices per D2 below): **the ASCII
intermediate path costs roughly 2.4× the binary artifact's own size,
for zero benefit** — the entire stated purpose of the ASCII authoring-
source format (ADR-0045's own Decision: "a small, human-readable, flat
text format a human can author and diff in an ordinary PR") does not
apply to programmatically-converted, 1.7-million-vertex mesh data no
human will ever hand-author or usefully diff.

**Decision:** the glTF importer's own mesh path writes cooked `.amesh`
runtime artifacts (ADR-0045's own binary format, unmodified in byte
layout except D2 below) **directly**, never generating an intermediate
`.mesh.txt` source file. This is a deliberate, narrow exception to this
project's own "asset source is always hand-authorable text, the cooker
always produces the binary artifact from it" pipeline shape — disclosed
explicitly, not silently — justified by the quantified cost above and by
`.mesh.txt`'s own stated purpose never having applied to converted,
non-hand-authored content in the first place.

**Materials and scene-graph nodes are not affected by D1** — see D5/D6
below; both stay small enough at Bistro's own real scale (254 materials,
5,908 nodes) to go through the existing ASCII-source-then-cook path
unchanged, preserving human-diffability where it still has real value
(a material's own scalar parameters, a node's own transform, are
genuinely worth a human being able to read in a PR diff; 1.7M raw vertex
floats are not).

### D2 — Runtime artifact index width: `uint32_t` for imported meshes,
ADR-0045's own `uint16_t` unchanged for hand-authored meshes

**Quantified basis:** ADR-0045's own runtime artifact format fixes
`std::uint16_t` indices (a 65,535-vertex ceiling per mesh). Direct
inspection of `bistro.gltf`'s own accessor data found **3 of 551
primitives exceed this ceiling** — the largest carries 126,990 vertices,
nearly double the `uint16_t` limit.

**Decision:** the glTF-importer-produced `.amesh` artifact variant uses
`std::uint32_t` indices. This is **not** a silent widening of ADR-0045's
own existing format — every hand-authored mesh continues to use
`uint16_t` indices exactly as ADR-0045 fixed it, unchanged. The
`schema_version` field ADR-0045's own Decision already mandates is the
existing, correct mechanism to distinguish the two: a new schema version
value marks "position + color + UV0 + normal + tangent, `uint32_t`
indices," distinct from the existing "...,`uint16_t` indices" version.
This mirrors ADR-0045's own three prior amendments' own established
pattern (each attribute addition got its own version bump, never a
silent reinterpretation of an existing version's byte layout) — applied
here to index width instead of vertex attributes, the same discipline.
**Plan-stage detail, not fixed here:** the exact new version number and
whether the loader path is a single unified reader branching on stored
index width, or two distinct code paths, is left to Spec 0037's own
Plan.

**Alternative rejected:** splitting the 3 oversized primitives into
multiple `uint16_t`-indexed sub-meshes at import time, avoiding any
runtime-format change at all. Rejected as this ADR's own default: mesh
splitting is real, nontrivial new algorithmic work (a
vertex-remapping/index-rewriting pass) to avoid a comparatively small,
precedented, mechanical format-version bump — not disproportionate the
way adopting a whole new dependency might be, but strictly more new
logic for a worse outcome (extra draw calls, more `DrawItem`s) than
simply widening the index type. Not ruled out as a future micro-
optimization if `uint32_t` indices prove to have a real, measured
performance cost this ADR did not anticipate.

### D3 — Material model mapping: `KHR_materials_pbrSpecularGlossiness`
converts to Atlantis's existing metallic-roughness `MaterialAssetData`

**Quantified basis:** 234 of Bistro's 254 materials (92%) use
`KHR_materials_pbrSpecularGlossiness` — `diffuseFactor` (RGBA),
`specularFactor` (RGB), `glossinessFactor` (scalar), plus
`diffuseTexture`/`specularGlossinessTexture` — not core glTF's
`baseColorFactor`/`metallicFactor`/`roughnessFactor` at all. Atlantis's
own material schema (ADR-0066) is metallic-roughness-only; no
specular-glossiness `MaterialKind` exists or is proposed.

**Decision:** the importer performs a **specular-glossiness → metallic-
roughness conversion** at import time, using the well-known, published
conversion (the same approach the official
`KHR_materials_pbrSpecularGlossiness` extension spec and Khronos's own
reference converter document — derive an approximate `metallicFactor`
from the specular color's own dielectric-vs-metal proximity, and
`roughnessFactor = 1 - glossinessFactor`). This is a **real,
disclosed, lossy, approximate** conversion, not an exact round-trip —
the two BRDF parameterizations are not bijective. **Plan-stage detail,
not fixed here:** the exact conversion formula/reference cited, and
whether the conversion happens once at import/cook time (baked into the
`.amat`/`.material.txt`) or is deferred, are left to Spec 0037's own
Plan; this ADR fixes only that the conversion happens at import, not at
runtime, and that no new `MaterialKind` is introduced for it.

`KHR_materials_transmission` (18 of 254 materials — real glass) has
**no corresponding Atlantis feature at all** — Spec 0035's own Non-Goals
already excluded transmission/refraction, unrevisited by this ADR or
Spec 0036. **Decision:** materials carrying this extension import as
`PbrDirectLit` (or `PbrClearcoat`, if otherwise eligible) with
`alphaMode: BLEND` (workflow ④'s own transparency mechanism, Spec 0036)
as the closest available approximation — `transmissionFactor` is
recorded in the material's own provenance/comment but has no
functional effect until (if ever) a future spec adds real transmission.
This is the same "documented, disclosed compromise, not a silent
feature loss" discipline this project's own Non-Goals sections already
use throughout.

### D4 — Texture format: **native GPU block-compressed texture support,
carried by independent Spec 0038** — CPU decode rejected, ruling
recorded 2026-09-15

**Quantified basis — the single largest, most consequential finding of
this ADR's own investigation:** `bistro.gltf`'s own `images[]` array
references 343 `.png`/`.jpg` "fallback" paths for clients without
`MSFT_texture_dds` support — **these files do not exist anywhere in the
`NVIDIA-RTX/RTXDI-Assets` repository.** Only the 390 real `.dds` files
are physically present, confirmed via `git lfs ls-files` against a real
clone: **totaling ~2.18 GB**, `BC7_UNORM_SRGB`-compressed (confirmed via
direct DDS header parsing of sampled files), resolutions ranging at
least 512×512 to 4096×4096 in the samples inspected. `stb_image`
(ADR-0041's own existing texture-decode dependency) **cannot decode
DDS/BC-compressed data** — it has never supported block-compression
formats.

This ADR's own original draft named two real paths without choosing
between them (CPU-side BC7 decode into the existing `Rgba8Unorm`/
`Rgba8Srgb` pipeline vs. a new native GPU block-compressed
`SampledTextureFormat`) and escalated the choice to Human Review, since
either resolution reaches outside this ADR's own Tools-only scope.
**Human Review ruled directly on 2026-09-15:**

**Ruling: CPU-side decode is rejected outright** — the quantified basis
above (BC7's own 4:1 compression ratio against ~2.18 GB of real,
measured DDS source data) means decoding to raw `Rgba8Unorm`/`Rgba8Srgb`
would produce **on the order of 8-9 GB of uncompressed texture bytes**
for Bistro's own texture set alone — an infeasible VRAM/RAM footprint on
either target platform (Windows dev hardware, and far more acutely
Android's own real memory ceiling), not a viable "simple, contained"
option once actually sized against real data, regardless of it staying
inside the Tools module boundary. **Native GPU block-compressed texture
support is the only viable path** and is carried by its own independent
Spec — [Spec 0038: Block-Compressed Texture Support](../specs/0038-block-compressed-textures.md)
— rather than folded into this workflow's own Tools-scoped ADR, exactly
because it is a real RHI/Vulkan Backend capability change
(`SampledTextureFormat` extension, `VkFormat` mapping, upload/
validation-path changes) that AGENTS.md's own module-boundary rules
reserve for its own, separately-reviewed decision — this ADR's own
original reasoning for *not* deciding it unilaterally stands unchanged;
what changed is that the choice between the two named paths is now
made (native BC support, not CPU decode), while the actual RHI/Vulkan
Backend design remains entirely Spec 0038's own, not restated or
duplicated here.

**Consequence for this workflow: a hard dependency.** Workflow ①'s own
texture-import milestone cannot land before Spec 0038's own native
block-compressed `SampledTextureFormat`/cooker path exists — this ADR's
own D1-D3/D5-D8 decisions (mesh, material, scene-graph mapping) are
**not** blocked by this dependency and may proceed independently; only
the texture-artifact-emission half of this workflow is gated. Recorded
in [Spec 0036](../specs/0036-bistro-parity-roadmap.md)'s own amended
workflow list as workflow ⓪, a hard DAG prerequisite to this workflow's
own texture milestone specifically (not to mesh/material/scene-graph
mapping).

### D5 — Scene-graph/node-hierarchy mapping goes through the existing
`.scene.txt` authoring-source format, unchanged in shape

**Quantified basis:** `bistro.gltf` has 5,908 total nodes (2,909
mesh-referencing), 1 scene root, maximum hierarchy depth 7 — Spec 0037's
own Investigation 2 estimate for a `.scene.txt`-equivalent text
representation at this scale is well under 1 MB, comparable in *kind*
(not scale) to `pbr_materials_showcase.scene.txt`'s own existing
61-line precedent — no format-capacity concern the way D1's mesh data
carries.

**Decision:** the importer flattens glTF's own node hierarchy (parent/
child transforms) into Atlantis's existing flat `.scene.txt` node list
with explicit `parent=` references, matching that format's own existing
grammar unchanged — no new scene-graph concept, no hierarchical
authoring-source nesting introduced. World-space transform composition
happens the same way it already does for every existing hand-authored
scene (`World::updateTransforms()`'s own existing parent/child walk,
unmodified).

### D6 — Coordinate-system convention

Confirmed by direct inspection of this codebase's own existing scene
content (every `.scene.txt` file's own node `position`/camera height
values) that **Atlantis is already Y-up**, matching glTF's own Y-up
convention by specification — no axis-remapping transform is needed on
import for the up-axis. **Handedness (glTF is right-handed) is not
independently re-confirmed by this ADR** against Atlantis's own existing
convention — a real, disclosed gap left to Spec 0037's own Plan to
verify against real rendered output (a visible mirroring/winding-order
defect would be immediately obvious in a real test render, making this
a cheap thing to verify at Plan/Implementation time rather than requiring
further Spec-stage archaeology).

### D7 — Missing `COLOR_0` vertex attribute

**Quantified basis:** `bistro.gltf`'s own mesh primitives carry
`POSITION`, `NORMAL`, `TEXCOORD_0`, and `TANGENT` — **no `COLOR_0`**.
Atlantis's own mandatory static-mesh vertex layout (ADR-0045, as
amended) requires per-vertex color.

**Decision:** the importer synthesizes a default vertex color of
`(1, 1, 1, 1)` for every imported vertex when `COLOR_0` is absent —
matching glTF's own specification-defined default for a missing
`COLOR_0` accessor (white, full alpha), so this is not an Atlantis-
specific invention, it is the format's own documented fallback,
reused.

### D8 — Pre-computed `TANGENT` attribute vs. ADR-0073's own
cooker-generated-tangent precedent

**Quantified basis:** `bistro.gltf`'s own primitives already carry a
real `TANGENT` accessor. ADR-0073 (Accepted, amending ADR-0045)
established that tangent is **exclusively cooker-generated** for every
existing hand-authored mesh — the authoring source format has no
tangent field at all, by design.

**Decision:** the glTF-sourced `TANGENT` data is **discarded**; the
existing cooker's own tangent-generation algorithm (ADR-0073) runs
unchanged against the imported position/normal/UV data, exactly as it
already does for every hand-authored mesh. This keeps exactly one
tangent-generation code path in this codebase, rather than two (one
that trusts an external file's own tangents, one that computes them) —
a real, deliberate simplicity choice, disclosed as discarding real,
already-correct upstream data rather than silently claimed as free.
**Risk, named, not resolved here:** the cooker's own regenerated
tangents may visibly differ from Blender/FBX2glTF's own original
tangent basis, particularly at UV seams — Spec 0037's own Testing
section requires this be checked against real rendered output, not
assumed equivalent.

## Consequences

### Positive

- D1/D2 keep the existing hand-authored `.mesh.txt`/`uint16_t` path
  completely untouched for every existing asset — this is purely
  additive new capability, not a migration.
- D3/D7/D8 each reuse an existing Atlantis mechanism (metallic-
  roughness materials, glTF's own documented default, ADR-0073's own
  tangent generator) rather than inventing a new one, keeping this
  Spec's own new-surface footprint smaller than it could have been.
- D4's own escalation (rather than a unilateral Tools-subsystem
  decision) meant the real, quantified CPU-decode cost (~8-9 GB) surfaced
  and was rejected *before* any implementation effort was sunk into it —
  the module-boundary discipline AGENTS.md's Golden Rule exists to
  protect did its job here, not merely in the abstract.

### Negative / Trade-offs

- D1 is a real, disclosed exception to this project's own "source is
  always hand-authorable text" pipeline shape.
- D2 introduces a second runtime-artifact index-width variant,
  real bookkeeping/loader-branching cost, however precedented in shape.
- D3's specular-glossiness conversion remains a real, unresolved-risk
  item this ADR names rather than eliminates. D4 is no longer open (see
  above) but is now a real, hard cross-Spec dependency — this workflow's
  own texture milestone cannot land until Spec 0038 does.
- D8 discards real, already-computed upstream tangent data.

## Alternatives Considered

Each Decision item above states its own rejected alternative inline
(intermediate-source-text generation for D1; mesh-splitting for D2; a
new specular-glossiness `MaterialKind` for D3; CPU-side BC7 decode for
D4, rejected by Human Review ruling 2026-09-15 on quantified-cost
grounds) rather than restating them in a separate section, since each
alternative's own rejection is the direct, inseparable basis for that
item's own recommendation.

## Risks & Open Questions

- **D4 is resolved** (native block-compressed support via Spec 0038,
  CPU decode rejected) but introduces a real, hard cross-Spec
  dependency in its place — this workflow's own texture milestone is
  blocked on that independent Spec landing first (see D4 above).
- D3's conversion formula is named in kind, not pinned to an exact
  published reference — Plan-stage detail.
- D6's handedness question is unverified against real evidence — Plan-
  stage detail, cheap to catch via a real test render.
