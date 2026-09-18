# Spec: glTF 2.0 Importer

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-16
- **Related Plan(s):** [Plan 0037](../plans/0037-gltf-importer.md)
  (`Approved`). Its Joint Human Review (slmao, 2026-09-19) covered this
  Spec and that Plan together and authorized Implementation.
- **Approval:** slmao, 2026-09-17 (chat confirmation, no reviewing PR —
  authorizes drafting Plan 0037; Implementation itself still awaits its
  own, separate Joint Human Review of Spec + Plan together).
- **Related ADR(s):** [ADR-0082](../adr/0082-gltf-parser-dependency-selection.md)
  (`Accepted`) — parser dependency; [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
  (`Accepted`) — format mapping; [ADR-0084](../adr/0084-gltf-importer-tools-subsystem-boundary.md)
  (`Accepted`) — Tools subsystem boundary. All three drafted alongside
  this Spec, per [Spec 0036](0036-bistro-parity-roadmap.md) workflow
  ①'s own named ADR obligations, which this Spec's own Architectural
  Impact section treats as a contract to discharge, not restate. All
  three accepted 2026-09-17, alongside this Spec's own Approval.
  **Revision (2026-09-16):** [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
  D4 and this Spec's own vendoring/Risks sections were revised following
  a Human Review ruling on texture format (CPU decode rejected; native
  block-compressed support carried by the new
  [Spec 0038](0038-block-compressed-textures.md)) and a repository-level
  large-content policy — see this Spec's own Investigation 4/Risks
  sections and ADR-0083 D4 for the current, ruled state; nothing in this
  revision note restates content already correct in the body below.
  **Correction (2026-09-19, post-Approval, reviewed in this branch's
  document PR):** Investigation 4's licence wording is corrected to a
  two-line attribution (MIT repository + CC-BY 4.0 original scene) —
  Plan 0037 Ruling 1.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

Implements [Spec 0036](0036-bistro-parity-roadmap.md) (Bistro Parity
Roadmap)'s workflow ① — a new `Atlantis Tools` subsystem
(`src/tools/gltf_importer/`) that parses a glTF 2.0 scene (core spec
plus the specific extension subset the recommended Bistro source
actually uses) and produces Atlantis's own asset-pipeline input: cooked
`.amesh` artifacts directly for mesh data, and generated `.material.txt`/
`.scene.txt` authoring source fed through the existing, unmodified
`atlantis_asset_cooker` for materials and scene graph. This directly
and explicitly revisits [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
own "no glTF/Assimp dependency" line, per that ADR's own stated
reopening condition ("once a real multi-mesh, multi-material authoring
workflow exists") — now satisfied by Bistro.

This Spec's own pre-drafting investigation downloaded and directly
measured the real, recommended Bistro glTF asset (not a hypothetical
one) — every quantitative figure below is drawn from that measurement,
cited inline, not estimated from precedent alone.

## Motivation / Problem Statement

Spec 0036 named workflow ① as this roadmap's highest-leverage, highest-
risk (XL-sized) workflow, and explicitly deferred every one of its
architectural decisions — format mapping, dependency choice, Tools
module boundary — to this Spec. Two of Spec 0036's own investigation
gaps are real and disclosed: it never opened `bistro.gltf` to check
which `KHR_*` extensions it actually uses, and it never measured the
asset's own real scale. Both are exactly the kind of "unknowns that
materially change the design" AGENTS.md's Golden Rule requires be
resolved by real investigation before Implementation, not discovered
mid-Plan. This Spec closes both gaps and makes the three ADR decisions
Spec 0036 named.

## Goals

- Parse a glTF 2.0 scene — core spec (`meshes`, `materials`, `textures`/
  `images`/`samplers`, `nodes`/`scenes`, `accessors`/`bufferViews`/
  `buffers`) plus the three extensions the real, recommended Bistro
  source actually uses (`KHR_materials_pbrSpecularGlossiness`,
  `KHR_materials_transmission`, `MSFT_texture_dds`) — into Atlantis's
  own asset-pipeline input, per [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md).
- Also parse `KHR_lights_punctual` (point/directional/spot light nodes)
  even though the recommended Bistro source itself defines none —
  Spec 0036's own workflow ① scope names "point light sources" as
  in-scope import content, and a real future glTF asset (not
  necessarily Bistro) may carry this extension; parsing it now, even
  unexercised against Bistro's own real data, avoids a second importer
  pass later. **Disclosed test gap:** this path cannot be exercised
  end-to-end against the real Bistro asset this Spec's own Testing
  Plan uses, since that asset defines no lights (Investigation 1
  below) — covered by a synthetic/hand-authored glTF fixture instead
  (Testing & Verification Plan).
- Produce, for a successfully-imported scene: cooked `.amesh` mesh
  artifacts (directly, [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
  D1), `.material.txt` authoring source (converted per D3), `.scene.txt`
  authoring source (per D5/D6), and texture artifacts (per D4, as native
  block-compressed artifacts once [Spec 0038](0038-block-compressed-textures.md)
  lands — a hard dependency for this one milestone only, ruled
  2026-09-15).
- Every recoverable error follows this repository's own established
  `Result`/error-enum contract (AGENTS.md Error handling) — a malformed
  glTF file, an unsupported extension actually encountered, an
  out-of-range value, or a mesh exceeding this Spec's own scale limits
  is a named, distinct error value, never a silent skip, a thrown
  exception, or a partial/corrupt artifact write.
- Adopt `cgltf` as the parsing dependency, narrowing
  [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  "no third-party glTF parser" line exactly as that ADR's own
  Alternatives Considered anticipated — [ADR-0082](../adr/0082-gltf-parser-dependency-selection.md).

## Non-Goals

- **glTF animation, skinning, and morph-target import.** Not part of
  Spec 0036's own workflow ① scope. Confirmed moot against the real
  recommended Bistro source specifically: direct inspection found **0
  `animations`, 0 `skins`** in `bistro.gltf` — this Non-Goal costs
  nothing against this Spec's own real test asset, though it remains a
  hard boundary for any *other* future glTF content this importer might
  later face.
- **Write-back / export.** This is an import-only tool; no glTF-
  authoring or round-trip-export capability is built.
- **Camera-node import.** Confirmed moot against the real recommended
  Bistro source: direct inspection found **0 `cameras`** in
  `bistro.gltf`. If a future glTF asset carries a camera node, this
  importer does not attempt to map it to an Atlantis scene camera node
  — out of scope until a real need is shown.
- **Non-triangle primitive modes** (glTF `LINES`/`LINE_STRIP`/`POINTS`/
  `TRIANGLE_STRIP`/`TRIANGLE_FAN`). Confirmed moot against the real
  Bistro source: all 551 of its own primitives use mode `4`
  (`TRIANGLES`) — the only mode this importer supports; any other mode
  encountered in a future asset is a named import error, not a silent
  skip or an attempted triangulation.
- **`KHR_materials_pbrSpecularGlossiness`/`KHR_materials_transmission`
  as first-class Atlantis BRDF features.** Both convert into existing
  `MaterialKind`s at import time ([ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
  D3) — neither becomes a new native shading model; real transmission/
  refraction remains excluded, unrevisited since Spec 0035's own
  Non-Goals.
- **CPU-side decoding of block-compressed (DDS/BC) textures to an
  uncompressed format.** Considered and **rejected by Human Review
  (2026-09-15)** on quantified-cost grounds
  ([ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D4)
  — native GPU block-compressed texture support, carried by
  [Spec 0038](0038-block-compressed-textures.md), is the path instead.
- **A general-purpose, format-agnostic 3D-interchange import framework.**
  This importer is scoped to glTF 2.0 plus the three named extensions —
  not Assimp-style multi-format support, not forward-compatible with
  glTF extensions this Spec's own investigation did not encounter.

## Requirements

### Functional

1. **Parsing.** Parse a glTF 2.0 `.gltf` (JSON) + external `.bin` +
   external image file set (the real shape the recommended source
   ships in — Investigation 1) via `cgltf` ([ADR-0082](../adr/0082-gltf-parser-dependency-selection.md)).
   `.glb` (single-file binary container) support is **not required**
   by this Spec (the recommended source does not ship this way) but is
   not precluded if `cgltf`'s own API makes it free.
2. **Mesh mapping.** Every glTF primitive with `POSITION` (required),
   optionally `NORMAL`/`TEXCOORD_0`/`TANGENT`/`COLOR_0`, mode
   `TRIANGLES`, indexed, maps to one cooked `.amesh` artifact per
   [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
   D1/D2/D7/D8. A primitive violating any of these (non-`TRIANGLES`
   mode, missing `POSITION`, non-indexed) is a named import error, not
   a best-effort partial import.
3. **Material mapping.** `KHR_materials_pbrSpecularGlossiness` and core
   metallic-roughness materials both convert to Atlantis's existing
   `MaterialAssetData` ([ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
   D3); `KHR_materials_transmission` materials import with the named
   `alphaMode: BLEND` approximation, D3. A material referencing an
   extension outside this Spec's own three named ones is a distinct,
   named import error (fail fast, not a silent default-material
   substitution) — matching this Spec's own "confirmed extension set
   only" scope discipline.
4. **Scene-graph mapping.** glTF node hierarchy flattens to Atlantis's
   existing flat `.scene.txt` node list with explicit `parent=`
   references ([ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
   D5); `KHR_lights_punctual` nodes map to `.scene.txt` light nodes
   (Goals above).
5. **Scale limits.** No hard-coded row/entry-count ceiling is imposed
   beyond what `.amesh`'s own D2 `uint32_t` index width and the
   existing `.scene.txt`/`.material.txt` grammars already support —
   this Spec's own Investigation 1/2 confirmed the real recommended
   Bistro source's own scale (551 meshes, 254 materials, 5,908 nodes,
   1.75M triangles) fits within those formats' own existing/widened
   capacity. A future, larger glTF asset exceeding any accessor's own
   glTF-spec-defined limits (e.g. `uint32_t` accessor `count`) is the
   underlying spec's own ceiling, not a new one this Spec introduces.
6. **Cooker integration.** Generated `.material.txt`/`.scene.txt` source
   is handed to the existing, **unmodified** `atlantis_asset_cooker`
   ([ADR-0084](../adr/0084-gltf-importer-tools-subsystem-boundary.md))
   — this Spec introduces no change to that tool's own existing
   behavior, argument grammar, or supported source formats.
7. **Error handling.** Every recoverable failure (malformed JSON,
   missing referenced buffer/image file, an accessor referencing an
   out-of-range buffer view, an encountered-but-unsupported extension,
   a non-`TRIANGLES` primitive, a primitive missing `POSITION`) is a
   distinct, named enumerator in a new `GltfImportError` (or
   equivalently-named) result type, returned via this repository's own
   `atlantis::Result<T, ErrorEnum>` convention (AGENTS.md) — never an
   exception, never a partial artifact write left on disk.

### Non-functional

- **Performance:** Import is an **offline, load-time-only** operation
  (matching `atlantis_asset_cooker`'s own existing execution model) —
  no runtime/frame-budget constraint applies. A real wall-clock budget
  for importing the full recommended Bistro source is **not measured by
  this Spec** (Risks below) — Plan-stage/Implementation-stage
  measurement against the real `cgltf`-based implementation, not
  estimated here.
- **Memory:** The single largest, most consequential finding of this
  Spec's own investigation — see Investigation 1 (texture volume) and
  [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D4.
  The ~8-9 GB decompressed-texture footprint a naive CPU-decode path
  would have produced was the real, quantified basis for rejecting that
  path (ruled 2026-09-15); the real runtime memory footprint under
  native GPU block-compressed import (~2.18 GB DDS, no CPU-side
  inflation) is [Spec 0038](0038-block-compressed-textures.md)'s own
  concern to size precisely, not restated here. Mesh data's own
  binary-artifact footprint is a comparatively modest, real ~120 MB
  (Investigation 2).
- **Portability (Vulkan-only Phase 1):** No portability-relevant change
  — this is an offline, host-side (Windows development machine) tool,
  matching `atlantis_asset_cooker`'s own existing execution model
  exactly; it produces the same cross-platform artifact formats every
  existing asset already uses (consumed identically on Windows and
  Android, per Spec 0034's own established asset-delivery path).
- **Other — content licensing:** See Investigation 4 below (license
  re-confirmation at implementation time, per Spec 0036's own stated
  requirement).

## Pre-drafting Investigation (required conclusions, cited against real
measurement)

### Investigation 1 — Real Bistro asset measurement

Downloaded the recommended source (`NVIDIA-RTX/RTXDI-Assets`'
`bistro/bistro.gltf` + `bistro.bin`, via a real `git clone`/`git lfs
pull`, MIT licensed — Investigation 4) and directly parsed/measured it
(not estimated):

| Metric | Value |
|---|---|
| `extensionsUsed` | `KHR_materials_pbrSpecularGlossiness`, `KHR_materials_transmission`, `MSFT_texture_dds` (no others; `extensionsRequired` empty) |
| Meshes / primitives | 551 (1 primitive per mesh) |
| Primitive modes | 100% `TRIANGLES` (mode 4) |
| Total triangles | 1,753,630 |
| Total vertices (summed across primitives, not deduplicated) | 1,738,262 |
| Largest single primitive | 126,990 vertices — **exceeds `uint16_t`'s 65,535 ceiling**; 3 of 551 primitives do |
| Vertex attributes present | `POSITION`, `NORMAL`, `TEXCOORD_0`, `TANGENT` — **no `COLOR_0`** |
| Materials | 254 total — 234 use `KHR_materials_pbrSpecularGlossiness`, 18 use `KHR_materials_transmission` |
| `alphaMode` | 231 `OPAQUE`, 20 `MASK`, 3 `BLEND` |
| Double-sided materials | 34 |
| Materials with non-zero `emissiveFactor` | 21 |
| Textures / images | 343 textures, 686 images (343 usable `.png`/`.jpg` **references** + 343 `.dds` — see below), 1 sampler |
| Nodes | 5,908 total; 2,909 reference a mesh; 1 scene root; max hierarchy depth 7 |
| Cameras / skins / animations | 0 / 0 / 0 |
| `KHR_lights_punctual` | **Not present** — 0 lights defined anywhere in this file |

**A real, previously-unconfirmed finding, closing Spec 0036's own
disclosed gap:** the 343 `.png`/`.jpg` image URIs the glTF JSON itself
references **do not exist as real files anywhere in the
`NVIDIA-RTX/RTXDI-Assets` repository** — confirmed via a real `git lfs
ls-files` listing against the actual cloned repository. Only the 390
real `.dds` files are physically present, **totaling ~2.18 GB**
(`git lfs ls-files -s`, summed), confirmed `BC7_UNORM_SRGB`-compressed
via direct DDS header parsing of sampled files, resolutions observed
from 512×512 to 4096×4096 in the samples inspected (not every file
individually measured). This is the real, quantified basis for
[ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D4's
own ruling (2026-09-15): native GPU block-compressed texture support,
carried by [Spec 0038](0038-block-compressed-textures.md).

Also confirmed: Filament's own bistro glTF (Spec 0036's own citation of
`google/filament` Issue #3248) is a **separate, internally-modified**
file ("we simply added light sources and emissive properties") — the
zero-lights finding above is real and specific to the *recommended*
`NVIDIA-RTX/RTXDI-Assets` source this Spec targets, not evidence that
Bistro itself can never carry lights.

### Investigation 2 — ASCII intermediate format feasibility

Quantified in full in
[ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D1: an
estimated ~282 MB of generated `.mesh.txt`-equivalent text vs. an
estimated ~120 MB cooked-binary-artifact equivalent, a ~2.4× cost with
no offsetting benefit (the ASCII source format's own stated purpose —
human authorability/diffability — does not apply to programmatically-
converted, 1.7-million-vertex data). **Conclusion, adopted as
[ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D1:**
mesh data bypasses the ASCII authoring-source stage entirely, emitting
cooked `.amesh` artifacts directly; materials (254 entries) and scene
nodes (5,908 entries) stay small enough at this real scale to go
through the existing ASCII-source-then-cook path unchanged (well under
1 MB each, comparable in kind, not scale, to existing precedent).

### Investigation 3 — Parser dependency candidates

Full candidate matrix and recommendation in
[ADR-0082](../adr/0082-gltf-parser-dependency-selection.md):
**`cgltf`** recommended (MIT, single-header, confirmed native typed
support for all three extensions this Spec's own real test asset uses
two of, plus `KHR_lights_punctual`) over `tinygltf` v3 (MIT, 3-file
distribution, more recently active, extension-struct coverage not
independently verified this round) and a hand-written parser (rejected,
disproportionate effort for a problem `cgltf` already solves at
MIT-license cost).

### Investigation 4 — License re-confirmation and provenance strategy

**Re-confirmed at this Spec's own drafting time** (Spec 0036 required
this re-check at implementation time, not assumed to still hold):
`NVIDIA-RTX/RTXDI-Assets` remains **MIT licensed** (re-verified directly
via GitHub's own license API against the live repository), **not
archived, not disabled**, last pushed 2026-03-10 — no material change
since Spec 0036's own original finding.

**Provenance strategy — a single, scene-level provenance file, not a
per-asset sidecar.** Spec 0036's own Risks section flagged this as an
open scaling question (per-texture sidecars, at Spec 0035's own
established granularity, would mean 390+ individual attribution files).
**Resolved here:** unlike Spec 0035's own CC0 texture set (each texture
independently sourced from a different provider, each needing its own
distinct attribution), the entire Bistro asset tree — every mesh,
material, and texture this importer touches — comes from **one single
source repository** (`NVIDIA-RTX/RTXDI-Assets`), with **one two-line
attribution**. The repository is licensed MIT; the original scene is
Amazon Lumberyard Bistro (NVIDIA ORCA), licensed CC-BY 4.0. (Corrected
2026-09-19, Joint Human Review of Plan 0037, Ruling 1. The original text
said "one single license, one single copyright holder … MIT", which
omitted the CC-BY 4.0 upstream that PR #165's own
`assets/textures/paris_stringlights_diff.provenance.txt` already records.)
One scene-level provenance file carrying both attribution lines
(matching this repository's own existing `<name>_source.provenance.txt`
naming precedent, scoped to the whole imported Bistro asset tree rather
than one file) is sufficient and avoids 390+ files of pure duplication.
**Plan-stage detail, not fixed here:** the exact file name/location.

**Vendoring strategy — reversed from Spec 0036's own original
mitigation, by explicit human ruling (2026-09-15), on real measured
volume.** Spec 0036's own Risks section named "vendor a copy + provenance
sidecar at import time" as the mitigation for a live GitHub repository
not being a durable dependency — a reasonable default *before* this
Spec's own Investigation 1 actually measured the asset. Now measured:
the full recommended Bistro source is **~2.28 GB** (2.18 GB DDS + 96 MB
`.bin` + 4.7 MB `.gltf`) — committing this into `git` (even via Git LFS,
which the upstream source itself uses) would make every clone of this
repository pull multiple gigabytes of content the overwhelming majority
of contributors never need to build, run tests, or review a PR. This is
a real, disclosed **deviation** from Spec 0036's own named mitigation,
justified by evidence that mitigation did not anticipate, not a quiet
reversal.

**Resolved here as a general, repository-level content policy** (stated
in reusable terms, since the same shape will recur for any future large
external asset, not only Bistro):

- **Any single content collection whose total real, measured size
  exceeds 50 MB is not committed to `git`** — neither directly nor via
  Git LFS. Instead, a **pinned, SHA256-verified, one-time fetch script**
  (naming convention: `fetch_<name>.ps1`, documenting its own source
  URL(s) in a header comment, matching this repository's own existing
  `generate_ibl_studio_source.ps1`-style throwaway-script precedent but
  **committed**, not throwaway, since it is the only record of how to
  reproduce the content) downloads the real content into a **`content/`
  directory added to `.gitignore`** at the point this policy first
  applies. The fetch script verifies each downloaded file's SHA256
  against a hash pinned in the script itself — the same "pinned by hash,
  not by mutable tag" discipline this repository's own `stb`
  `FetchContent_Declare` (`cmake/AtlantisStb.cmake`) already established
  for a source dependency, applied here to authored content instead.
  Bistro (~2.28 GB) is the first real case this policy applies to.
- **Provenance and license files are always committed to `git`**,
  regardless of the content-volume rule above — a `.gitignore`d
  `content/` directory holds only the large, reproducible-by-fetch-
  script bytes; the human-readable record of *what* was fetched, from
  *where*, under *what* license (Investigation 4's own single
  scene-level provenance file, this policy's own general form) is small,
  reviewable-in-a-PR text that belongs in version control exactly like
  every other provenance sidecar this repository already commits.
- **Tests that depend on this externally-fetched content `SKIP` (not
  `FAIL`) when it is absent**, printing the exact fetch-script
  invocation needed to obtain it — this is the mechanism that keeps this
  repository's own "clone → build → full `ctest` green" contract
  (CLAUDE.md's own documented build/test commands) true for a
  contributor who has not run `fetch_bistro.ps1`: they see a skipped
  test with clear instructions, never a red, unexplained failure. This
  is a real, new test-classification concept this repository's own
  existing test suites have not needed before (every prior GPU-
  independent/GPU-required split has assumed all referenced content is
  always present) — Plan-stage detail for exactly how a Catch2
  `SKIP`/tag mechanism expresses this, not fixed here.
- **Small assets are unaffected by this policy and continue exactly as
  before**: CC0 texture sets (Spec 0035's own precedent, each well under
  the 50 MB threshold), golden images and their sidecars, and any
  existing checked-in binary (e.g. the Validation Layer `.so`, Plan 0034)
  stay committed to `git` exactly as today. **Golden images are never
  subject to this policy, unconditionally** — ADR-0042's own golden-
  image-testing contract requires every golden PNG/sidecar be committed
  alongside the code it verifies, and nothing about this content policy
  changes, narrows, or creates an exception to that requirement.

## Proposed Design

High-level shape only — concrete file layouts, function signatures, and
algorithms are Plan-stage detail per AGENTS.md's own documentation-home
table (implementation-ready detail lives in the Plan and the PR, not the
Spec).

A new `atlantis_gltf_importer` executable + `atlantis_gltf_importer_lib`
static library ([ADR-0084](../adr/0084-gltf-importer-tools-subsystem-boundary.md)),
parsing via `cgltf` ([ADR-0082](../adr/0082-gltf-parser-dependency-selection.md)).
Two output paths, per
[ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D1:
mesh primitives cook directly to `.amesh` artifacts inside the
importer itself; materials and scene nodes generate `.material.txt`/
`.scene.txt` authoring source, then flow through the existing,
unmodified `atlantis_asset_cooker` — the same two-stage pipeline every
hand-authored asset already uses for those two asset types. Texture
handling ([ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
D4) emits native block-compressed texture artifacts, consuming the new
`SampledTextureFormat` value(s) [Spec 0038](0038-block-compressed-textures.md)
adds — a hard dependency on that Spec landing first (ruled 2026-09-15),
not an open design question.

## Architectural Impact

**Yes — this Spec discharges every ADR obligation Spec 0036's own
workflow ① definition named**, each recorded in its own separate,
independently-evolvable ADR (matching this repository's own established
one-ADR-per-decision discipline, ADR-0043/0044/0045's own precedent):

- [ADR-0082](../adr/0082-gltf-parser-dependency-selection.md) — new
  third-party dependency (`cgltf`), narrowing ADR-0045.
- [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) —
  format-mapping decisions (mesh/material/texture/scene-graph/
  coordinate-system), including a real runtime-artifact schema
  widening (D2) and one question (D4, texture format) that was
  escalated rather than silently decided, then ruled by Human Review
  (2026-09-15) — native block-compressed support, not CPU decode.
- [ADR-0084](../adr/0084-gltf-importer-tools-subsystem-boundary.md) —
  new Tools subsystem module boundary and dependency surface.

**One decision this Spec's own three ADRs deliberately do not make**,
named explicitly rather than smuggled through as a side effect of a
Tools-scoped ADR: the actual RHI/Vulkan Backend design for native GPU
block-compressed texture support (new `SampledTextureFormat` value(s),
`VkFormat` mapping, upload/validation-path changes) — a real capability
change outside this Spec's own Tools-only scope, correctly carried by
its own independent [Spec 0038](0038-block-compressed-textures.md)
rather than folded in here. This Spec's own texture-import Requirement
(2/6) is a **hard dependency** on that Spec, recorded in
[Spec 0036](0036-bistro-parity-roadmap.md)'s own amended workflow DAG as
workflow ⓪ gating this workflow's texture milestone specifically.

## Alternatives Considered

Each of the three ADRs above states its own rejected alternatives
inline (candidate matrix in ADR-0082; per-decision rejected alternative
in each of ADR-0083's D1-D8; folding into `atlantis_asset_cooker` in
ADR-0084) rather than restating them here, matching this Spec's own
"link ADR rationale... rather than adding duplicate... lists"
instruction.

## Testing & Verification Plan

Per AGENTS.md's Testing requirements and
[docs/process/testing-strategy.md](../process/testing-strategy.md),
mapped against this Spec's own Requirements:

- **GPU-independent parsing/mapping unit tests**, mirroring
  `tests/tools/asset_cooker/`'s own existing precedent exactly — one
  test class per Requirement 2-7 decision point (a synthetic, small,
  hand-authored `.gltf` fixture per case: missing `COLOR_0`, a
  non-`TRIANGLES` primitive rejected, an out-of-range accessor
  rejected, a `KHR_materials_pbrSpecularGlossiness` material converting
  to the expected metallic-roughness values, a `uint32_t`-widened mesh
  round-tripping correctly, a synthetic `KHR_lights_punctual` node
  mapping to a `.scene.txt` light node — Goals' own disclosed test gap
  against the real Bistro asset, covered here instead).
- **At least one real, end-to-end cook-and-load test against the actual
  recommended Bistro asset** (Requirement 1/6) — imports the real
  `bistro.gltf`, runs the generated material/scene source through the
  real `atlantis_asset_cooker`, and confirms the resulting artifacts
  load successfully through Asset System's own existing loader path.
  This test's own real texture-handling scope is gated on
  [Spec 0038](0038-block-compressed-textures.md) landing first (D4,
  ruled 2026-09-15) — Plan-stage detail once that dependency resolves,
  potentially scoped to a representative texture subset (per the
  content policy above) rather than the full ~2.18 GB.
- Vulkan Validation Layers clean is **not applicable** to this Spec's
  own scope — this is an offline, CPU-only tool; no GPU-touching code
  path is introduced.
- No image-regression golden is required by this Spec alone — a real
  rendered Bistro scene golden is workflow ⑦'s own job (Spec 0036),
  once workflows ②-⑥ also exist to light/shade/composite the imported
  geometry meaningfully.

## Risks & Open Questions

Each open item below is stated as **ruled** (a human decision already
landed this item, recorded where) or **still open** (genuinely
undecided, deferred to Plan/Implementation stage) — not left ambiguous
between the two.

- **[ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
  D4 (texture format) — ruled 2026-09-15.** CPU-decode rejected on
  quantified-cost grounds (~8-9 GB decompressed, infeasible); native GPU
  block-compressed texture support is the path, carried by
  [Spec 0038](0038-block-compressed-textures.md) as its own independent
  Spec, not decided inside this one. **Consequence, not fully resolved:**
  this workflow's own texture-import milestone is now a **hard
  dependency on Spec 0038 landing first** — mesh/material/scene-graph
  mapping (Requirements 2-3-4, D1/D2/D3/D5/D6/D7/D8) are unblocked and
  may proceed independently.
- **Real import wall-clock time against the full 1.75M-triangle,
  254-material, 5,908-node asset is unmeasured — still open.** This
  Spec's own Non-functional section names this a real gap, not a
  claimed bound.
- **`cgltf`'s own real parsing behavior against the actual `bistro.gltf`
  file is unverified by this Spec — still open, ruled how it gets
  closed.** ADR-0082's own disclosed gap (confirmed only via direct
  source-code inspection of `cgltf.h`'s own struct definitions, not by
  compiling and running it) is **not** left as a generic "Plan-stage
  detail" — it is Plan 0037's own **first-milestone smoke test gate**,
  matching Plan 0034 Milestone 1's own precedent ("NDK/CMake build-
  infrastructure smoke test... confirm, in isolation, [the toolchain
  works] before any Android-specific code is written," Spec 0034): the
  very first thing Plan 0037's own Implementation does is compile
  `cgltf` against this repository's own build and successfully parse
  the real `bistro.gltf` file's top-level structure (mesh/material/node
  counts matching Investigation 1's own measured figures) — every
  later milestone in Plan 0037 depends on this gate passing first,
  exactly the way every later Android milestone in Plan 0034 depended
  on its own Milestone 1.
- **`MSFT_texture_dds` extraction via `cgltf`'s generic extension
  pass-through is unverified — still open.** `tinygltf` v3 is the named
  fallback if this proves awkward (ADR-0082). Relevant to
  [Spec 0038](0038-block-compressed-textures.md)'s own cooker-side work
  once that Spec's RHI-side capability exists, not blocking this Spec's
  own smoke-test gate above (which only needs mesh/material/node
  top-level structure, not texture bytes).
- **D2's `uint32_t`-index runtime-artifact schema-version number is
  unassigned — still open.** Plan-stage detail, not fixed by this Spec
  or its ADRs.
- **D6 (glTF right-handed vs. Atlantis's own handedness) — still open,
  ruled how it gets closed.** Not left as "assumed correct until a
  visible defect appears" — Plan 0037 must explicitly confirm glTF's
  own right-handed convention against Atlantis's **own existing,
  already-established coordinate-convention/math contract** (this
  codebase's own `Mat4`/transform-composition code and any
  already-recorded handedness convention it embodies) as a named
  verification step, not merely wait for a rendering artifact to reveal
  a mismatch.
- **D3's specular-glossiness → metallic-roughness conversion formula —
  still open, ruled how it gets closed.** Named in kind (the
  well-known dielectric-proximity/`1 - glossiness` approach), not
  pinned to an exact published reference in this Spec or ADR-0083;
  Plan 0037 must cite the specific formula/reference actually
  implemented (matching ADR-0067's own established "cite the real math"
  discipline for BRDF equations, applied here to a conversion formula
  instead), not merely restate "well-known conversion."
- **Bistro's own real download/vendoring size (~2.28 GB total: ~2.18 GB
  DDS + 96 MB `.bin` + 4.7 MB `.gltf`) is a real repository-storage
  question** this Spec does not resolve — whether the full asset is
  vendored, a representative subset, or fetched-and-cached outside
  version control at Plan-stage build time is left open, disclosed
  rather than assumed.

## Out of Scope / Future Work

- Everything in Non-Goals.
- The RHI/Vulkan Backend-side design for native block-compressed texture
  support — carried entirely by [Spec 0038](0038-block-compressed-textures.md),
  this Spec's own hard dependency, not restated here.
- Workflows ②-⑦ of Spec 0036's own roadmap (and now workflow ⓪) — this
  Spec implements workflow ① only.
- A general-purpose glTF *export* path, or support for glTF extensions
  beyond the three this Spec's own real investigation found in use.
