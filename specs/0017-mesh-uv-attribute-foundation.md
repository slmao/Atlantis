# Spec: Mesh UV Attribute Foundation

- **Status:** In Review
- **Author:** slmao
- **Created:** 2026-08-25
- **Related Plan(s):** None yet — this Spec is not implementation-ready
  until Human Review approves it and a Plan is drafted against it (see
  [AGENTS.md](../AGENTS.md)'s Golden Rule).
- **Related ADR(s):**
  [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  (`Proposed`) — the one new architectural decision this Spec's own
  Architectural Impact identifies. See that ADR's own Related ADR(s)
  field for how it relates to
  [ADR-0043](../adr/0043-asset-system-module-boundary.md)–[ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  and
  [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)–[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md).

## Summary

[Spec 0016](0016-texture-sampler-foundation.md) built real RHI
`SampledTexture`/`Sampler` types, a RenderGraph sampled-resource
binding kind, and a `Material` that can optionally sample one fixed
texture — and proved all of it on real GPU hardware. But the UV0
attribute that proof depends on is entirely hand-authored inside one
test fixture (`textured_quad_fixture.cpp`); no Asset-System-sourced
static mesh anywhere in this codebase carries UV data, and Spec 0016's
own record names this an explicit, disclosed, blocking gap. This Spec
closes exactly that gap: it makes UV0 a real, mandatory attribute of
the one static mesh vertex layout Asset System already cooks, loads,
and hands to a composition root's `atlantis::renderer::createMesh()`
call — and proves, on real GPU hardware, that a genuinely
authoring-sourced, cooked, loaded mesh can be textured through Spec
0016's own already-built sampling pipeline, replacing (or standing
alongside) that pipeline's own hand-authored UV backdoor. It does not
build a material system, a scene-level texture binding, or any new
texture/lighting capability — those remain named, disclosed future
work.

## Motivation / Problem Statement

Direct inspection of current, real code confirms the gap precisely, not
by inference:

- `mesh_source.h`'s `MeshSourceVertex` is `{positionX, Y, Z, colorR, G,
  B}` — six floats, no UV field.
- `mesh_artifact.h` fixes the runtime artifact's vertex stride at a
  single global constant, `kMeshArtifactVertexStrideBytes = 24` (six
  32-bit floats) — every static mesh asset that exists, or could exist
  under today's cooker/loader, shares this one shape.
- [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  own already-`Accepted` Decision text states this explicitly: the
  formats are "scoped exactly to this Spec's one supported asset type
  (a static triangle mesh: per-vertex position and color,
  `std::uint16_t` indices)."
- `textured_quad_fixture.cpp` (Spec 0016's own real-GPU proof) draws two
  quads whose vertex data — `struct Vertex { float position[3]; float
  uv[2]; };`, four `constexpr` literals — is written directly in that
  test file's own C++ source. It never calls `cookStaticMesh()`,
  `loadStaticMeshAsset()`, or touches an `AssetId` of any kind for its
  own geometry.
- Spec 0016's own registry record (`specs/README.md`) discloses this
  explicitly as a named, blocking follow-up: "'Mesh UV Attribute
  Foundation' (real UV0 inside Asset System's own mesh pipeline)
  remains a named, immediate, blocking follow-up candidate for a future
  asset-sourced-textured-mesh claim."

Without this Spec, no future spec, demo, or Runtime-driven scene can
truthfully claim to render "a real, authored, textured mesh" — every
such claim today would still be, underneath, a hand-authored fixture
vertex array wearing Spec 0016's own real texture-sampling
infrastructure as a coat of paint. This Spec exists to make that claim
true for the first time, in the narrowest way the existing
architecture already supports.

## Goals

- Every Asset-System-sourced static mesh asset carries a UV0 attribute,
  end to end: authoring source → `cookStaticMesh()` → runtime artifact
  → `loadStaticMeshAsset()` → `StaticMeshAssetData` → a composition
  root's real GPU `Mesh` (`atlantis::renderer::createMesh()`).
- UV0 data is preserved losslessly and deterministically through every
  stage of that chain — the same finiteness/determinism/little-endian
  discipline every existing position/color float already receives,
  extended, not reinvented.
- A real, on-GPU, Vulkan-Validation-Layers-clean proof that a texture
  can be sampled using UV0 data that actually originated from a cooked,
  loaded Asset System mesh artifact — not a fixture-local vertex array
  — using Spec 0016's own already-built `SampledTexture`/`Sampler`/
  `Material`/RenderGraph-binding surface unmodified.
- Every existing mesh consumer (`minimal_cube`, `world_scene`) keeps
  rendering correctly, byte-for-byte where a golden already exists,
  after its own underlying mesh asset gains UV0 data its own shader
  does not read.

## Non-Goals

- **Material Asset & Scene Binding Foundation.** `World::Renderable`
  carries exactly one field, `meshAsset` (an `AssetId`) — confirmed by
  direct inspection of `src/world/include/atlantis/world/renderable.h`
  — no material, texture, or color-space reference of any kind. This
  Spec does **not** add one. A scene loaded through
  `World::fromValidatedSceneData()`/Runtime's own scene-driven path
  still resolves every `Renderable` to the one fixed, hardcoded,
  vertex-color `Material` Runtime already constructs — Runtime's own
  material selection is completely unchanged by this Spec. Making a
  Runtime-loaded scene automatically render a texture is a separate,
  disclosed, immediately-next candidate — see Out of Scope / Future
  Work.
- **PBR, a material graph, lighting, shadowing, or post-processing of
  any kind.** Unchanged from Spec 0016's own Non-Goals; this Spec adds
  no new shading model.
- **Tangent/bitangent attributes or normal mapping.** UV0 is the only
  new per-vertex attribute this Spec adds.
- **Multiple UV sets (UV1, UV2, ...).** Exactly one UV attribute per
  vertex.
- **Mipmap generation, texture compression, texture streaming, or a
  bindless descriptor system.** Unchanged from Spec 0016's own
  Non-Goals; this Spec touches no texture-format or binding-model
  decision at all.
- **A general, reusable N-attribute vertex-schema system.** This Spec
  adds exactly one new, fixed attribute to the one existing mesh vertex
  layout — it does not build a mechanism for declaring arbitrary future
  attribute combinations. See
  [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)'s
  own Alternatives Considered.
- **Any new third-party dependency or new top-level module.** UV0 is
  two more `float`s parsed/serialized with the exact same
  `std::from_chars`/explicit-shift-mask machinery every existing
  position/color float already uses.
- **Android, iOS, or Linux implementation.** Windows remains this
  Spec's own verified target, matching every prior Spec.
- **A distributable Asset Catalog or cross-session stable identity.**
  Unchanged, carried forward from Spec 0012/0015's own Non-Goals.

## Requirements

### Functional

- **FR1 — Authoring grammar.** Each `vertex:` line in the mesh
  authoring source format gains two trailing space-separated float
  tokens (`u v`), extending the existing exactly-6-field line to
  exactly 8. The grammar's own version marker line becomes
  `atlantis_static_mesh_source_version: 2`; `parseMeshSource()` rejects
  any other value with the existing `SourceParseError::UnknownSourceVersion`
  — no dual-version reader. See
  [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  Decision item 1.
- **FR2 — CPU authoring DTO.** `MeshSourceVertex` (`mesh_source.h`)
  gains `uvU`/`uvV` `float` fields, in that order, after the existing
  six position/color fields.
- **FR3 — Runtime artifact layout.** The runtime artifact's per-vertex
  byte layout becomes position xyz (offset 0) → color rgb (offset 12)
  → UV0 uv (offset 24), 32 bytes total.
  `kMeshArtifactVertexStrideBytes` becomes `32`;
  `kMeshArtifactSchemaVersion` becomes `2`. `decodeMeshArtifact()`
  rejects any other schema version or stride with the existing
  `ArtifactDecodeError::UnknownSchemaVersion`/`UnsupportedVertexStride`
  — no migration reader. Every UV float is serialized/deserialized with
  the exact same explicit shift/mask little-endian routine
  (`appendFloatLE`/`readFloatLE`) every existing float already uses.
- **FR4 — Value contract.** UV floats must be finite — the existing
  per-vertex `NonFiniteFloat` check (`parseMeshSource()`,
  `decodeMeshArtifact()`) extends from six floats to eight, unchanged
  in kind. No `[0, 1]` range check, clamp, or rejection is added at any
  authoring, cook, or decode layer.
- **FR5 — No new error enumerator.** `SourceParseError`,
  `ArtifactDecodeError`, `CookError`, and `AssetLoadError` gain no new
  case anywhere. Every rejection this Spec's own Implementation needs —
  malformed UV token, non-finite UV value, wrong field count, wrong
  schema/source version, wrong stride, truncated/oversized artifact —
  is already covered by an existing enumerator once each function's own
  loop bound or field-count check moves from 6 to 8. Confirmed by
  direct inspection of `parseMeshSource()`, `encodeMeshArtifact()`,
  `decodeMeshArtifact()`, `cookStaticMesh()`, and `loadStaticMeshAsset()`.
- **FR6 — `StaticMeshAssetData`/`loadStaticMeshAsset()`/`createMesh()`
  require zero source change.** Each is already vertex-layout-agnostic
  by construction (a byte buffer plus a runtime `vertexStrideBytes`
  field; `createMesh()`'s own `VertexInputLayout` parameter is already
  unread). Confirmed by direct inspection, not assumed; this Spec's own
  Implementation must not introduce a change to any of the three.
- **FR7 — Re-authoring of existing mesh sources.** Every
  currently-checked-in mesh authoring source file
  (`assets/meshes/minimal_cube.mesh.txt`, and any other real, authored
  `.mesh.txt` this repository has by Implementation time) is re-authored
  with real, deliberately-chosen UV values and re-cooked under the new
  grammar version — no file is left on the old 6-field grammar, and the
  cooker applies no silent `(0, 0)` default.
- **FR8 — Four composition-root call sites updated in lockstep.**
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_loaded_fixture.cpp`, and
  `src/runtime/src/runtime_application.cpp` each hardcode a local
  `struct Vertex { float position[3]; float color[3]; };` and
  `sizeof(Vertex)` as `VertexInputLayout::strideBytes` for an
  Asset-System-sourced mesh, independent of
  `StaticMeshAssetData::vertexStrideBytes()`'s own real, dynamic value.
  Each of these four `Vertex` structs gains a trailing `float uv[2];`
  field, matching the new 32-byte artifact layout exactly. None of
  their own shaders (`minimal_mesh.slang`) is changed — their own
  `MeshVertexAttributeSchema` stays exactly the two entries it already
  is (position@0, color@1); the new UV bytes are present in the vertex
  buffer but unreferenced by any pipeline attribute for these draws,
  which `toVertexInputLayout()`'s own existing cross-validation already
  permits (see FR9). Confirmed as the complete list by a repository-wide
  search for every hardcoded `struct Vertex`/`sizeof(Vertex)` call site,
  cross-checked against which ones actually consume
  `StaticMeshAssetData`/`loadStaticMeshAsset()` (five further hardcoded
  `Vertex` call sites exist in this repository —
  `textured_quad_fixture.cpp`,
  `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`,
  `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`,
  `examples/headless_rendering_demo/main.cpp`,
  `examples/minimal_renderer_demo/main.cpp` — none of these touches
  Asset System at all; their own hand-authored vertex data is
  completely unaffected by this Spec and must not be changed).
  `textured_quad_fixture.cpp` is a distinct, sixth case: not one of the
  four requiring a mechanical stride widening (it does not consume
  `StaticMeshAssetData` today), but the one file this Spec proposes to
  *convert* to Asset-System-sourced vertex data as part of FR9 — see
  Decision item 11. `src/runtime/src/runtime_application.cpp`'s own
  consumption is indirect — it builds the `VertexInputLayout` from its
  own local `Vertex` struct and passes it into `loadAndInstantiateScene()`
  (`scene_load.cpp`), which threads it through to every real,
  Asset-System-loaded mesh a scene references, confirmed by direct
  inspection of `runtime_application.cpp`'s own `loadAndInstantiateScene(config,
  device_.get(), vertexInputLayout_)` call site. `scene_load.cpp`/
  `scene_load.h` and `tests/runtime/scene_load_tests.cpp` were also
  checked directly: the former only receives an already-built
  `VertexInputLayout` as a pass-through parameter (no local `Vertex`
  struct of its own), and the latter's every call site passes a
  default-constructed, empty `VertexInputLayout{}` alongside a null
  `Device*` — exercising only failure/error paths that never touch a
  real vertex buffer — so neither needs any change for this Spec.
  `tests/shader_system/rhi_integration/vertex_input_mapping_tests.cpp`
  was likewise checked: it unit-tests `toVertexInputLayout()` itself
  with synthetic offsets/strides, independent of the real mesh artifact
  format, and also needs no change.
- **FR9 — Real GPU proof via an asset-sourced, UV-carrying mesh.** At
  least one real, checked-in mesh authoring source, cooked through the
  ordinary `cookStaticMesh()`/`atlantis_add_static_mesh_asset()` path
  and loaded through the ordinary `loadStaticMeshAsset()` path, is
  drawn with a real `SampledTexture`/`Sampler`-bound `Material`
  (Spec 0016's own, unmodified mechanism) and captured in a real,
  Vulkan-Validation-Layers-clean image-regression golden — proving the
  UV0 data driving the sampled color genuinely passed through the full
  authoring → cook → artifact → load → GPU chain, not a fixture-local
  backdoor. See "Decisions for Human Review" item 11 for the specific,
  recommended shape of this proof.

### Non-functional

- **Determinism:** cooking the same authoring source (now including UV
  values) twice produces byte-identical artifact/metadata output —
  extends the existing determinism guarantee/test pattern
  (`tests/tools/asset_cooker/cooker_determinism_tests.cpp`), no new
  mechanism.
- **Byte order:** the artifact's own unconditional little-endian
  contract ([ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md))
  is unchanged and applies identically to the two new UV floats.
- **Portability (within the Vulkan-only Phase 1 constraint):** no
  platform-specific code of any kind; Windows remains the verified
  target.
- **Regression safety:** `minimal_cube`'s and `world_scene`'s own
  existing image-regression goldens must continue to match with zero
  pixel difference after their own underlying mesh assets gain UV0 data
  their own shaders do not read — this is the concrete, testable proof
  that FR8's own "unreferenced bytes are harmless" claim is actually
  true, not merely architecturally plausible.

## Proposed Design

Extend the one, single static mesh vertex layout Asset System already
owns — position (3 floats) + color (3 floats) — with a third, mandatory
attribute, UV0 (2 floats), for a total of 8 floats / 32 bytes per
vertex. This is a pure width extension of an existing, already-generic
pipeline, not a new mechanism:

1. **Authoring source** (`mesh_source.h`/`.cpp`): version bump, 8-field
   `vertex:` line, `MeshSourceVertex` gains `uvU`/`uvV`.
2. **Runtime artifact** (`mesh_artifact.h`/`.cpp`): stride constant and
   schema version both bump; UV floats serialize with the existing
   float-encoding routine at the new trailing offset.
3. **CPU load** (`static_mesh_asset_data.h`, `load.cpp`): **no change**
   — both are already stride-agnostic.
4. **GPU handoff** (`atlantis::renderer::createMesh()`): **no change**
   — vertex layout is already opaque to this function.
5. **Composition-root vertex-layout construction**
   (`toVertexInputLayout()` call sites): the four Asset-System-sourced
   call sites gain a `uv[2]` field on their own local `Vertex` struct
   (widening `sizeof(Vertex)` to 32) but keep their own
   `MeshVertexAttributeSchema` unchanged — their own shaders neither
   declare nor need a UV input.
6. **The one new, real-GPU-textured, asset-sourced mesh** (see FR9 and
   Decisions for Human Review item 11): a composition root that, unlike
   the four above, **does** wire a UV-consuming schema/shader and a
   real `Material` with a bound `SampledTexture`/`Sampler`.

No new RHI, RenderGraph, Renderer, or Shader System type is introduced
anywhere — every GPU-facing type this Spec's own proof needs
(`VertexAttributeFormat::Float2`, `SampledTexture`, `Sampler`,
`Material`'s optional texture pair, the RenderGraph `sampledTexture`
bind-kind) already exists, `Accepted` and implemented, from Spec 0016.

## Decisions for Human Review

Twelve concrete decisions this Spec resolves, each with a recommended
answer grounded in direct inspection of current code (cited inline).
Human Review may accept, reject, or redirect any of them individually —
none is assumed settled by this Draft.

1. **Is UV0 mandatory, optional, or an explicit vertex-layout/version
   variant?** *Recommended: mandatory, one fixed layout, no variant.*
   `decodeMeshArtifact()` already checks vertex stride against one
   single global compile-time constant — there is no per-asset variable
   layout anywhere in this codebase to make UV0 "optional" against.
   Building that variability would be new, currently-unneeded
   architecture; see
   [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)'s
   own Alternatives Considered for the two variable-layout alternatives
   weighed and rejected.
2. **How are existing, no-UV mesh assets handled — migration, default,
   continued old-format support, or explicit rejection?** *Recommended:
   re-author with real UV values and re-cook; no implicit default, no
   dual-format support.* No mesh artifact is independently distributed
   or shipped separately from its own checked-in authoring source —
   every artifact is a deterministic build output, already re-cooked
   automatically whenever its source file or the cooker binary itself
   changes (the existing CMake custom command already depends on both).
   "Migration" therefore means editing
   `assets/meshes/minimal_cube.mesh.txt` (and any other real authored
   `.mesh.txt`) to add real UV columns and bump its own version line, not
   converting a binary artifact — there is no scenario in which a real
   version-1 *artifact* is ever loaded once this Spec's own
   Implementation lands, since the source that produces it will already
   have moved to version 2. A silent `(0, 0)` default is rejected as
   this Spec's own recommendation: it would make FR9's own proof
   impossible to build honestly and would leave every migrated mesh's UV
   data meaningless — but this remains **Human Review's own decision to
   make, not a foregone conclusion**; if Human Review instead directs a
   `(0, 0)` (or other) default for un-migrated sources, FR9's own proof
   mesh must still be authored with real, non-default UV values
   regardless. See
   [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
   Decision item 3.

   **A real, disclosed limitation of `minimal_cube.mesh.txt`'s own
   migration:** `assets/meshes/minimal_cube.mesh.txt` authors exactly 8
   vertices (one per cube corner) and 36 indices (12 triangles, 6 faces)
   — confirmed by direct inspection — meaning each corner vertex is
   *shared* across the 3 faces that meet at it. A shared vertex can carry
   exactly one UV value, not one per face; a cube's 6 faces cannot be
   given a correct, independent texture unwrap on this 8-vertex topology
   at all, regardless of what UV values are chosen. This Spec's own
   migration of `minimal_cube.mesh.txt` therefore assigns each of its 8
   vertices a real, deterministic, finite UV value (satisfying the new
   mandatory schema and this Spec's own "no implicit default" position)
   **that is not, and cannot be, a correct per-face texture unwrap** —
   `minimal_cube` continues to be drawn exclusively by its own existing,
   UV-blind `minimal_mesh.slang` material, exactly as today, and this
   Spec makes no claim that `minimal_cube` becomes texture-ready. See
   Decision item 11 below for why FR9's own real proof mesh must
   therefore be a quad (or another topology with no cross-face vertex
   sharing), never the existing cube.
3. **Does the mesh artifact format version bump? Is an old version
   read compatibly or rejected? Does a well-versioned source file that
   is still missing UV columns get its own distinct error?**
   *Recommended: bump both the authoring-source version (1 → 2) and the
   artifact schema version (1 → 2); reject any other value outright, no
   compatible/dual read; a version-2-labeled source whose `vertex:` line
   still has only 6 fields is a distinct, already-existing rejection
   (`CountMismatch`), never silently accepted as "UV omitted."*
   Two genuinely different malformed-input shapes exist, and this Spec
   recommends they stay genuinely distinct, not collapsed into one:
   (a) a source file whose version line still reads
   `atlantis_static_mesh_source_version: 1` — rejected by the existing
   `SourceParseError::UnknownSourceVersion`, exactly as
   `parseMeshSource()` already rejects any version string other than its
   own one supported value today; (b) a source file correctly labeled
   version 2 whose `vertex:` line was not actually updated to 8 fields
   (a human bumped the version marker but forgot the UV columns) —
   rejected by the existing `SourceParseError::CountMismatch`
   (`fields.size() != 8`), the same mechanism that already rejects a
   line with too few or too many fields today. Neither case is treated
   as "UV0 intentionally omitted, default to `(0, 0)`" — both are hard
   parse failures. This matches this format's own existing precedent
   exactly — `parseMeshSource()` already hard-rejects any authoring
   version line other than its own one supported string, and
   `decodeMeshArtifact()` already hard-rejects any schema version other
   than its own one supported constant. Every other Asset System format
   (texture, scene) works the same way; no format in this codebase has
   ever supported reading more than one schema version, and this Spec
   recommends none start now.
4. **What is the UV authoring syntax, CPU DTO shape, and exact artifact
   byte layout?** *Recommended:* authoring — `vertex: x y z r g b u v`
   (two trailing float tokens); CPU DTO — `MeshSourceVertex` gains
   `uvU`/`uvV` after the existing six fields; artifact — position (offset
   0) → color (offset 12) → UV0 (offset 24), 32 bytes/vertex,
   little-endian, `appendFloatLE`/`readFloatLE` unchanged. See FR1–FR3.
5. **Must UV values be finite? Is `[0, 1]` enforced, or can UV exceed
   it?** *Recommended: finite required (extends the existing
   `NonFiniteFloat` check); no range enforcement of any kind.*
   `atlantis::rhi::Sampler::addressMode()` (`Repeat`/`ClampToEdge`,
   already `Accepted` and implemented, Spec 0016/ADR-0055) already gives
   out-of-`[0, 1]` UV a real, well-defined GPU sampling meaning —
   clamping vertex data at cook/decode time would silently discard a
   legitimate `Repeat`-tiling author intent with no way to opt out, and
   would duplicate a decision RHI already owns.
6. **How does UV origin/V-direction correspond to stb's non-flipped PNG
   row order and the Vulkan sampling path?** *Recommended: adopt
   `textured_quad_fixture.cpp`'s own already-established convention
   verbatim — `v = 0` is the texture's first-decoded (top) row, `v = 1`
   is its last-decoded (bottom) row.* This is not a new decision; Spec
   0016 already made it and it already ships, unchanged, in production
   code. This Spec's own new, asset-sourced UV data must be authored
   consistent with that existing convention, not a new or inverted one
   — **but whether the existing golden/test suite has actually proven
   this convention *correct*, in an absolute sense, versus merely
   self-consistent, is a real, separate, disclosed limitation — see
   Decision item 11 below.** This item settles which convention to
   author to; item 11 settles what can and cannot be proven about it.
7. **What is the exact vertex stride/attribute-location/format
   contract, including its relationship to Shader reflection — and is
   "a shader can ignore a middle attribute" actually true at the real
   Vulkan pipeline level, or only asserted?** *Recommended:* stride is
   always the mesh's own real, artifact-derived 32 bytes; a shader
   declaring all three attributes uses position@0, color@1, UV0@2
   (declaration order, matching this codebase's own convention); a
   shader declaring only a subset uses whatever `location` numbers that
   shader itself assigns to its own inputs — `location` is per-shader,
   never a global registry (`textured_quad.slang`'s own `location(1)` is
   UV, not color; it is not required to declare a `location(1)` "color"
   input it does not use). **This is closed at the real Vulkan pipeline
   construction itself, confirmed by direct inspection of
   `Device::createPipeline()`
   (`src/vulkan_backend/src/vulkan_device.cpp`), not asserted from the
   RHI-level abstraction alone:** exactly one
   `VkVertexInputBindingDescription` is built (stride = the mesh's own
   real per-vertex byte size), and the `VkVertexInputAttributeDescription`
   array is built by iterating `VertexInputLayout::attributes` **only**
   — one entry per attribute the caller actually lists, each with its
   own independent `location`/`format`/`offset`. Nothing requires
   attributes to be contiguous or to cover every byte of `stride`; an
   offset with no corresponding attribute description is never read by
   that pipeline. This is what makes both `minimal_mesh.slang`'s own
   unused UV region (bytes 24–31) and a UV-only shader's own unused
   color region (bytes 12–23) safe — a real-code-verified fact, not a
   plausible-sounding claim. `VertexAttributeFormat::Float2` (already
   `Accepted`, Spec 0016) is the UV0 attribute's own RHI format. See
   FR8/FR9 and
   [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
   Decision item 6 for the full citation.
8. **What are the cooker/loader's independent error semantics for
   truncated, overflowing, non-finite, wrong-version, or corrupted
   input?** *Recommended: no new enumerator anywhere — every case is
   already covered.* Confirmed by direct inspection: `MalformedNumber`
   (bad token), `NonFiniteFloat` (extended to 8 floats),
   `CountMismatch`/`FieldOrderMismatch`/`MissingField`/`TrailingContent`
   (wrong field count/order/truncation, authoring side),
   `UnknownSourceVersion`/`UnknownSchemaVersion` (wrong version),
   `UnsupportedVertexStride`/`InconsistentOffsets`/`SizeMismatch`
   (corrupted/truncated artifact), `MetadataArtifactMismatch` (artifact
   vs. metadata disagreement) — all already exist and already generalize
   once loop bounds move from 6 to 8 fields/floats. See FR5.
9. **How is determinism, fixed little-endian byte order, and regression
   safety for existing mesh assets guaranteed?** *Recommended: no new
   mechanism — extend existing coverage.* Determinism: the existing
   real-CLI, byte-identical-two-run test pattern
   (`cooker_determinism_tests.cpp`) already generalizes. Byte order:
   [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
   own unconditional little-endian contract is unchanged. Regression
   safety: `minimal_cube`'s and `world_scene`'s own existing
   image-regression goldens must be re-verified at zero pixel
   difference after their own mesh assets gain unreferenced UV data —
   see the Testing & Verification Plan.
10. **How does the GPU proof demonstrate the sampled data genuinely
    originated from Asset System, not a hand-authored backdoor?**
    *Recommended: the proof's own mesh must be loaded via a real
    `loadStaticMeshAsset()` call against a real, `atlantis_add_static_mesh_asset()`-cooked
    artifact — never a `constexpr`/hardcoded C++ vertex array — and this
    must be stated and checked explicitly in the proof's own test
    assertions/code comments, not left implicit.* See FR9 and item 11
    below.
11. **Does this Spec reuse the existing `textured_quad` golden, or does
    it require a new/modified golden — and can the existing golden/test
    suite actually prove UV origin/V-direction correctness at all?**
    *Recommended, pending Implementation-time confirmation: replace
    `textured_quad_fixture.cpp`'s own hand-authored quad vertex data
    with a real, asset-sourced mesh (never the cube — see Decision item
    2's own disclosed cube-topology limitation) authored to the exact
    same position/UV float values already in use
    (`kLeftQuadVertices`/`kRightQuadVertices`), cooked and loaded through
    the ordinary Asset System path, keeping every other part of the
    fixture (two `Material`s, two `SampledTexture`s, the RenderGraph
    upload/draw sequence, the camera/projection identity matrices)
    unchanged.* Because `createMesh()` only `memcpy`s raw bytes and
    every position/UV literal already in use (`-0.9, -0.5, 0.0`, `0.0,
    1.0`, etc.) is exactly representable in IEEE-754 float32, authoring
    a `.mesh.txt` source with the identical literal values and letting
    it flow through `std::from_chars`/`appendFloatLE`/`readFloatLE`
    should reproduce the exact same GPU-visible vertex bytes, and
    therefore the exact same rendered pixels — **this is this Spec's own
    stated expectation, to be empirically confirmed during
    Implementation, not asserted as guaranteed.** The asset-sourced
    mesh's own authoring source additionally needs real (if
    render-irrelevant, since `textured_quad.slang` declares no color
    input) color values, since color remains part of the one shared
    vertex layout (Decision 1); `textured_quad_fixture.cpp`'s own
    `MeshVertexAttributeSchema` for this mesh stays two entries
    (position@0, UV0@1 — matching `textured_quad.slang`'s own reflected
    shape exactly), with the color bytes present but unreferenced,
    mirroring FR8's own already-established pattern. **If Implementation
    finds the golden cannot be reproduced byte-identically** (e.g. a
    real floating-point round-trip discrepancy discovered only by
    running the code), the golden is updated following
    [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
    own golden-update-reason rule, with the specific discrepancy
    disclosed in the Plan/Implementation PR — not silently absorbed.

    **A real, separate limitation, disclosed honestly rather than
    silently inherited: reusing the existing golden/checkerboard proves
    only self-consistency, not absolute UV-direction correctness.**
    `assets/textures/textured_quad_source_unorm.png`/`_srgb.png` is a
    small, regular checkerboard — under a vertical flip, a regular
    even-row checkerboard becomes its own color-inverted mirror, which
    is visually very hard for a human to distinguish from the
    un-flipped original at a glance (both simply "look like a
    checkerboard"). This Spec's own V38-equivalent human visual review
    of the *existing* `textured_quad` golden (recorded on
    [PR #80](https://github.com/slmao/Atlantis/pull/80)) confirmed
    exactly this and no more: "both checkerboard quads ... clearly
    visible, the color difference matches expected UNORM-vs-sRGB ...,
    no black frame or garbage data" — it never claimed, and could not
    have verified, that `v = 0` genuinely maps to the texture's
    first-decoded row rather than its last. No automated test in
    `textured_quad_gpu_tests.cpp` asserts an absolute expected color at
    a specific, known UV-mapped screen position either — every existing
    test checks self-consistency (matches the previously-captured
    golden, differs appropriately between color spaces, differs from an
    undrawn baseline), confirmed by reading every `TEST_CASE` in that
    file. **Reusing this same golden/texture for FR9's own proof
    inherits this same limitation: it would prove UV0 data genuinely
    reached the GPU from Asset System, but not that this Spec's own
    stated V-direction convention (Decision item 6 above) is itself
    correct, only that whatever convention the fixture already used
    remains unchanged.** This Spec's own recommendation is that this is
    an **acceptable, disclosed limitation** for FR9's own narrower claim
    — the convention itself is not new (it is adopted unchanged from
    Spec 0016's own already-shipped fixture, not invented here), and
    this Spec's own scope is proving asset-sourced provenance, not
    re-litigating a convention Spec 0016 already fixed. **Whether Human
    Review accepts this narrower claim, or instead directs a new,
    row-asymmetric texture and/or a new pixel-position-specific
    assertion to independently close the V-direction question for the
    first time, is itself submitted as part of this decision — this
    Spec does not silently assume the broader claim.**
12. **Given `Renderable` has no material/texture reference today, how
    does this Spec's own composition-root boundary hold together?**
    *Recommended: this Spec's FR9 proof is a direct, fixture-level
    composition root — the same pattern `textured_quad_fixture.cpp`
    already establishes — never a Runtime/`World`/`Renderable`-driven
    path.* `World::Renderable` carries only `meshAsset` (an `AssetId`,
    confirmed by direct inspection of `renderable.h`); Runtime's own
    scene-driven rendering path has no mechanism to select a textured
    `Material` for any `Renderable` and this Spec adds none. This Spec
    explicitly does not claim a Runtime-loaded scene can render a
    texture — only that a real, asset-sourced mesh, drawn by a
    fixture-level composition root exactly like Spec 0016's own proof
    already is, can carry and use real UV0 data. See Non-Goals and Out
    of Scope / Future Work.

## Architectural Impact

**Yes** — this Spec identifies one new architectural decision:
[ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
(`Proposed`), fixing the static mesh vertex layout's own UV0 attribute,
schema version, and sampling convention. It directly narrows an
already-`Accepted` sentence of
[ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
(which currently names the mesh formats as scoped to "position and
color" only) — an **Accepted Amendment to ADR-0045** recording this
widening is required once ADR-0058 itself reaches `Accepted`, following
this repository's own established amendment pattern (see ADR-0041's own
"Accepted Amendment — 2026-08-24" section for the precedent), not
performed by this Draft.

No other module boundary, public API, dependency, threading model, or
backend-abstraction contract changes:

- Asset System's own module boundary (`Atlantis::Core`-only dependency,
  no RHI/Renderer/RenderGraph/Shader System/`atlantis::world`,
  [ADR-0043](../adr/0043-asset-system-module-boundary.md)) is unchanged
  — UV0 is CPU-side data flowing through the exact same boundary
  position/color data already crosses.
- Asset ID/logical-path identity
  ([ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md))
  is unchanged — a vertex-layout change does not touch how a logical
  path becomes an Asset ID.
- RHI, RenderGraph, Renderer, and Shader System gain no new public type
  or API — every GPU-facing mechanism this Spec's own proof needs
  (`VertexAttributeFormat::Float2`, `SampledTexture`, `Sampler`,
  `Material`'s optional texture+sampler pair, the RenderGraph
  `sampledTexture` bind-kind, `toVertexInputLayout()`/
  `MeshVertexAttributeSchema`) already exists, `Accepted` and
  implemented, from Spec 0007/0008/0016.
- No new third-party dependency, no new top-level module.

## Alternatives Considered

See
[ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)'s
own Alternatives Considered for the vertex-layout-shape alternatives
(optional UV via a second mesh kind; a caller/data-driven stride; a
general N-attribute vertex-schema system; clamping UV at cook/decode
time) and why each was rejected in favor of one mandatory, fixed,
32-byte layout.

At the Spec level, one further alternative was considered and rejected:

- **Defer this Spec entirely and instead build Material Asset & Scene
  Binding Foundation first**, on the theory that UV0 alone is not
  useful without a way to bind a texture to a scene. Rejected: Spec
  0016's own already-`Accepted`, already-implemented texture-sampling
  infrastructure has no consumer at all without a real, asset-sourced
  UV0 attribute to feed it — this Spec is the narrower, lower-risk,
  immediately-actionable prerequisite Spec 0016's own record already
  names as the next blocking gap, while Material Asset & Scene Binding
  is a substantially larger, separate design (a new Scene Asset field,
  a new `Renderable` shape, a new Runtime material-resolution path) that
  deserves its own Spec and its own Human Review, not folded into this
  one's already-full scope. See Out of Scope / Future Work.

## Testing & Verification Plan

- **GPU-independent unit tests** (extending existing test files, no new
  test executable): `mesh_source_tests.cpp` (8-field parse/serialize
  round-trip, new version-line rejection, UV `NonFiniteFloat`/
  `MalformedNumber` cases), `mesh_artifact_tests.cpp` (32-byte
  encode/decode round-trip, new schema-version/stride rejection, UV
  `NonFiniteFloat` at decode time), `static_mesh_asset_data_tests.cpp`
  (confirms no change needed — existing tests continue to pass
  unmodified against the wider stride), and the cooker's own existing
  determinism test extended to cover a UV-bearing mesh source.
- **`/w14062` positive/negative build probe:** not applicable — this
  Spec adds no new enumerator to any switch (FR5); this is itself a
  verifiable, negative claim (grep confirms no new `case` is needed
  anywhere `SourceParseError`/`ArtifactDecodeError`/`CookError`/
  `AssetLoadError` are switched over).
- **Regression goldens:** `minimal_cube` and `world_scene`'s own
  existing image-regression goldens must match with **zero** pixel
  difference after their own underlying mesh assets are re-authored
  with UV0 data — the concrete, empirical proof that unreferenced
  vertex bytes are harmless to a shader that does not declare them.
- **New GPU proof (FR9):** a real, Vulkan-Validation-Layers-clean
  image-regression test/golden showing a texture sampled using UV0 data
  that demonstrably originated from a `loadStaticMeshAsset()`-loaded,
  `cookStaticMesh()`-cooked artifact — see Decisions for Human Review
  items 10–11 for the specific, recommended shape (reusing/replacing
  `textured_quad`'s own hand-authored vertices) and the disclosed
  fallback if byte-identical golden reuse does not hold up empirically.
- **Full build/test matrix**, matching every prior Spec's own
  established bar: fresh Debug and Release builds; `ctest -LE gpu` and
  `ctest -L gpu` both green, both configurations; Vulkan Validation
  Layers grepped clean (zero `VUID`/`Validation Error`/`Validation
  Warning`); module-boundary/CMake-link-graph scan reconfirming Asset
  System's own dependency surface is unchanged.

## Risks & Open Questions

- **The four-call-site migration (FR8) is a real, mechanical, but
  easy-to-miss consequence.** Missing one of `minimal_cube_fixture.cpp`,
  `world_scene_fixture.cpp`, `world_scene_loaded_fixture.cpp`, or
  `runtime_application.cpp` would silently corrupt that call site's own
  vertex fetch (a stride mismatch between the real 32-byte artifact data
  and a stale 24-byte `VertexInputLayout::strideBytes`) — likely
  visible as garbled or degenerate geometry, not a compile error or a
  clean `Result::Err`. The Plan that implements this Spec must treat
  all four as a single, atomic, all-four-or-none change, verified by
  the regression-golden requirement above, not by code review alone.
- **Golden byte-identity for the `textured_quad` reuse (item 11) is an
  expectation, not a guarantee**, until Implementation actually runs the
  code. If it does not hold, the fallback (a disclosed, ADR-0042-governed
  golden update) is already named above — this is not a blocking risk
  to approving this Spec, only a disclosed uncertainty about one
  specific Implementation-time outcome.
- **Whether FR9's proof reuses/replaces the existing `textured_quad`
  golden, or is a genuinely new, third golden, is itself part of what
  this Spec asks Human Review to decide** (item 11) — the Plan stage
  will need to commit to one concrete answer before Implementation
  begins, per this repository's own Spec → Plan → Human Review →
  Implementation sequencing.
- **This Spec does not, by itself, make any Runtime-loaded scene visibly
  textured.** This is stated repeatedly above (Non-Goals, Decisions for
  Human Review item 12) precisely because it is the single most likely
  misreading of this Spec's own scope — flagged here again as an
  explicit risk of scope creep during Implementation, not only as a
  documentation nicety.

## Out of Scope / Future Work

- **Material Asset & Scene Binding Foundation** — the next, disclosed,
  immediately-following candidate: a real material/texture reference on
  `World::Renderable` or an equivalent Scene Asset field, and a Runtime
  material-resolution path that is no longer hardcoded to one fixed,
  vertex-color `Material`. This Spec's own UV0 work is this future
  Spec's own real prerequisite — without it, there would be nothing for
  a scene-level texture binding to sample correctly.
- Multiple UV sets, tangent/bitangent attributes, normal mapping.
- A general, reusable N-attribute vertex-schema/descriptor system for
  meshes.
- Mipmap generation, texture compression, texture streaming, a bindless
  descriptor system, PBR, lighting, shadowing, and post-processing —
  all unchanged, carried forward from Spec 0016's own Non-Goals.
- Target-independent GPU submission (a `Device::submit()` path admitting
  no real `RenderTarget`) — unchanged, carried forward from Spec 0016.
- A distributable, cross-session Asset Catalog and rename-stable GUID
  identity — unchanged, carried forward from Spec 0012/0015.
- Android/iOS/Linux implementation.

## Readiness for Human Review (2026-08-25 self-review)

A centralized, evidence-driven self-review pass re-verified every claim
in this Draft directly against current, real code before this note was
written — not by re-reading this document's own prose. It resolved one
real governance gap and tightened or newly disclosed several findings;
none reversed this Spec's own core recommendation (a mandatory,
fixed, 32-byte vertex layout):

- **Closed:** [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  now carries its own "Proposed Amendment — 2026-08-25" section,
  explicitly `Proposed` (not `Accepted`), cross-referenced with
  [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  and this Spec, so all three reach Human Review together rather than
  leaving ADR-0045's own required narrowing as unrecorded future work.
- **Closed, with real proof, not assertion:** the vertex-layout/Shader-
  reflection closure (Decision item 7) is now grounded directly in
  `Device::createPipeline()`'s own real
  `VkVertexInputBindingDescription`/`VkVertexInputAttributeDescription`
  construction (`src/vulkan_backend/src/vulkan_device.cpp`) — confirmed
  that an attribute-less region of a vertex's own stride (whether
  trailing or interior) is never read by a pipeline whose own
  `VertexInputLayout` does not name it. This holds for both directions
  this Spec needs: a color-only shader ignoring a trailing UV region,
  and a UV-only shader ignoring an interior color region.
- **Newly disclosed, not previously stated:** `minimal_cube.mesh.txt`'s
  own 8-vertex, shared-corner topology cannot express a correct
  per-face UV unwrap under any UV values — its own migration can only
  ever carry deterministic, non-default placeholder UV data, never a
  real texture-ready unwrap, and it must never be proposed as a
  texture-sampling proof mesh (Decision items 2 and 11).
- **Newly disclosed, not previously stated:** the existing
  `textured_quad` golden and its own already-recorded V38 confirmation
  prove only render self-consistency and Unorm-vs-Srgb color
  difference — not the absolute correctness of the UV-origin/
  V-direction convention itself, since a regular checkerboard is
  visually near-symmetric under a vertical flip and no existing test
  asserts an absolute expected color at a known UV-mapped position.
  This is submitted as an explicit Human Review choice (Decision item
  11), not silently assumed either way.
- **Re-confirmed by a fresh, broader repository search:** exactly four
  composition-root call sites require a mechanical vertex-stride
  widening (FR8); the count and file list are unchanged from this
  Draft's own original claim, now additionally cross-checked against
  `scene_load.cpp`/`scene_load_tests.cpp`/`vertex_input_mapping_tests.cpp`,
  each confirmed unaffected for a distinct, stated reason rather than
  merely omitted.
- **No finding in this review pass overturned the core recommendation.**
  `decodeMeshArtifact()`'s own single-global-constant stride check, and
  the real Vulkan pipeline evidence above, both continue to support one
  mandatory, fixed vertex layout over a variable/optional one — no
  smaller alternative surfaced that still satisfies FR9's own real,
  asset-sourced GPU proof requirement.

**This Spec, [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md),
and ADR-0045's own new Proposed Amendment are, on this basis, ready for
a real, formal Human Review pass** — every decision this Draft asks
Human Review to make is now backed by direct code evidence or an
explicit, honest disclosure of what remains genuinely uncertain, rather
than an assumption. This self-review does not itself approve anything:
this Spec's own Status remains `In Review`, and both ADRs remain
`Proposed`, pending that actual Human Review.
