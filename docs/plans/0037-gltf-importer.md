# Plan: glTF 2.0 Importer

- **Spec:** [Spec 0037: glTF 2.0 Importer](../specs/0037-gltf-importer.md)
  (`Approved`, 2026-09-17) — [ADR-0082](../adr/0082-gltf-parser-dependency-selection.md),
  [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md),
  [ADR-0084](../adr/0084-gltf-importer-tools-subsystem-boundary.md) (all `Accepted`)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending — Implementation must not start until a
  human reviews this Plan and Spec 0037 together and explicitly authorizes
  it. The Open Points section lists the items that review must rule on.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0037 in full (Spec 0036 workflow ①): a new
`atlantis_gltf_importer` tool (ADR-0084) that parses glTF 2.0 through `cgltf`
(ADR-0082). It emits `.amesh` artifacts directly, generates `.material.txt`/`.scene.txt` source for the
unmodified `atlantis_asset_cooker`, and routes `MSFT_texture_dds` BC7
textures through that same cooker's Spec 0038 DDS path (ADR-0083 D1–D8). It is
proven end to end against the real Bistro asset for data correctness only;
rendering Bistro belongs to workflow ⑦.

## Pre-drafting reading (cited, not restated)

Confirmed against the code at `origin/main` `8b09a24` (PR #165, Spec 0038, merged).

**Dependency precedent.** `cmake/AtlantisStb.cmake:20-34` pins a commit
archive with `URL` + `URL_HASH SHA256` instead of `GIT_TAG`. `:15-19` gives the
reason: git smart-HTTP clone resets in this environment. The fetched source is
then wrapped in an `INTERFACE` target plus a `Vendor::Lib` alias. Two further facts:

- ADR-0006 (Decision, bullet 1) requires pinning to a tag or commit hash.
- Catch2 follows the same URL pattern (`cmake/AtlantisDependencies.cmake:18-20`, v3.7.1).

cgltf `v1.15` is the latest release (2025-02-09). Its tag resolves to commit
`bbeb5b0b070ddacddac6852fb72143eb68454937`. The commit archive's SHA256 was
measured during drafting: `98b987d9a6b0443a830af5bcc019eb2e75d8f2d79988e0aec02db5c849c43ee4`.
In that `cgltf.h`:

- Typed support: `has_pbr_specular_glossiness` (:543), `has_transmission` (:545),
  `cgltf_light_type_spot` (:251), `cgltf_validate()` (:864).
- `cgltf_texture` has typed `basisu`/`webp` fields but **no DDS field**. Unrecognized
  extensions land in `cgltf_texture.extensions` (:417-418), which is the pass-through
  ADR-0082 names for `MSFT_texture_dds`.

**Existing pipeline.**

- *Cooker structure.* `atlantis_asset_cooker_lib` (STATIC) links AssetSystem/Core
  PUBLIC and Stb PRIVATE, with `/w14062` exhaustiveness scoped to that target
  (`src/tools/asset_cooker/CMakeLists.txt:12-47`). The exe is a thin `main.cpp` (`:49-57`).
- *Build wiring.* Tools are added from the **root** `CMakeLists.txt:96-98` inside
  `if(NOT ANDROID)`; `src/tools/CMakeLists.txt` does not exist. ADR-0084's "CMake
  integration" paragraph names that file, so this Plan follows the real precedent
  instead (a factual correction, not a design change).
- *Cooker CLI.* The cooker handles one file per invocation, selected by
  `--kind=mesh|scene|texture|material|environment` (`main.cpp:52-61`). Dispatch is in
  `cook_command.cpp:507-521`. `.dds` sources go to `runCookTextureDdsMode`
  (`:274`), where the DXGI format is the **sole color-space authority** and
  `--color-space` is ignored (`:269-273`).
- *DDS parsing.* `parseDdsBc7()` lives in `src/tools/asset_cooker/dds_parser.h:37`
  (namespace `atlantis::asset_cooker`), not in Asset System.
- *CMake asset functions* (`src/asset_system/CMakeLists.txt`):
  `atlantis_add_static_mesh_asset` (:77), `atlantis_add_scene_asset` (:137),
  `atlantis_add_texture_asset` (:253), `atlantis_add_material_asset` (:355). Each
  function wraps one cooker invocation per asset. **None is used by the importer**:
  Bistro content is optional and gitignored (Spec content policy), so it must never
  enter the default build graph.
- *`.amesh` format.* Schema version 4 has a 40-byte header, a 60-byte vertex stride,
  and `uint16_t` indices, all serialized by explicit shift/mask
  (`mesh_artifact.h:15-52`). `DecodedMeshArtifact::indices` is `uint16_t` (`:58`).
  `ParsedMeshSource::indices` is `uint16_t` (`mesh_source.h:39`), and
  `generateTangents()` takes a `ParsedMeshSource` (`mesh_tangent_generation.h:28-29`).
  The normal-length tolerance helper is `detail::isNormalLengthSquaredInTolerance`
  (`mesh_source.h:94`, range [0.9801, 1.0201]).
- *Material source grammar.* The version line is `atlantis_material_source_version: 6`
  (`material_source.cpp:11`), with fixed-order fields (`:12-40`). `texture:` is
  **mandatory** and there is no `none` token. The optional fields are
  `normal_map:`, `base_color_factor/metallic_factor/roughness_factor`, and the
  per-kind clearcoat/sheen/anisotropy fields (`material_source.h:22-69`).
  There is **no comment syntax**; unexpected content fails as `TrailingContent`.
- *No alpha/transparency schema.* `MaterialAssetData` has **no alpha-mode,
  emissive, occlusion, or double-sided field** (`material_types.h:65-106`), and
  `alphaMode` appears nowhere in `src/`. Spec 0036 ④ records that blending is hard-disabled today.
- *Scene source grammar.* `atlantis_scene_source_version: 4`
  (`scene_source.cpp:16`). Each node is one line carrying `node_id`, `parent=`,
  `position`, `rotation` (Euler radians), and `scale`, plus one of:
  `mesh=` [`material=`], `light=`, or a camera. See
  `assets/scenes/pbr_materials_showcase.scene.txt:4` for the syntax.
- *Scene light limits.* A light never shares a node with a mesh or camera
  (`scene_source.h:34-37`). `DecodedLightKind` is **`{Directional, Point}` only —
  no Spot** (`scene_types.h:56`). The scene is capped at 1 directional and 4 point
  lights (`TooManyLights`, `scene_source.h:59-63`).
- *Logical paths.* Mesh logical paths are strings such as `meshes/x.mesh.txt`.
  Neither `cook_scene.cpp` nor `cook_material.cpp` enforces an extension or checks
  that the file exists (grep: zero hits); resolution is value-level only.
- *Texture (Spec 0038) path.* `TextureDataLayout {Rgba8, Bc7}` (`texture_types.h:24-27`);
  `cookTextureBc7()` (`cook_texture.h:51-54`); `.atex` schema v2, 40-byte header,
  `kMaxTextureDimension = 8192` (`texture_artifact.h:40-42`). Only the base mip is
  kept (`dds_parser.h:19-21`). Plan 0038 Milestone 3 **explicitly deferred mip-chain
  passthrough "to Spec 0037's importer milestone"**
  (`docs/plans/0038-block-compressed-textures.md:114-116`). This Plan cannot absorb
  that deferral; see Open Point 6.
- *Render-side index width.* `VK_INDEX_TYPE_UINT16` is hardcoded at
  `vulkan_command_list.cpp:387`; the RHI has no index-type concept. Runtime uploads
  `meshAssetData.indices()` at `scene_load.cpp:112-113`.
- *Test precedent.* A Catch2 `SKIP()` for absent machine-local content already exists
  (`tests/image_regression/image_regression_gpu_tests.cpp:168`), and the pinned
  Catch2 is v3.7.1. The include-scan boundary test lives at
  `tests/asset_system/module_boundary_tests.cpp:48-60`.

**Bistro source (measured during drafting).**

- `NVIDIA-RTX/RTXDI-Assets` is still MIT-licensed and not archived. Its `main` is
  `1da7b749ef0e3fb606ab46dde8e5bfbacd078a02` (2026-03-10).
- At that commit, `bistro/` contains 393 blobs: 390 `.dds`, `bistro.bin`, `bistro.gltf`,
  and `bistro_without_paintings.gltf`.
- LFS pointers: `bistro.gltf` oid `e31aa48f…d788`, 4,740,530 B; `bistro.bin` oid
  `5fb11a1d…8faa`, 95,660,264 B.
- An anonymous `POST …/RTXDI-Assets.git/info/lfs/objects/batch` returned a signed
  download action for the gltf oid, so the fetch path described in Milestone 2 works
  with no git or git-lfs on the client.
- **Licence finding:** PR #165's own committed
  `assets/textures/paris_stringlights_diff.provenance.txt` records the upstream as
  "Amazon Lumberyard Bistro (NVIDIA ORCA, CC-BY 4.0)" repackaged under the
  repository's MIT licence. Spec 0037 Investigation 4 says "one single license".
  See Open Point 1.
- The same fixture shows a **`_diff` (base-color) DDS stored as `BC7_UNORM`, not
  `_SRGB`** (256×128). This contradicts the Spec's sampled "all `BC7_UNORM_SRGB`"
  finding and interacts with the cooker's DXGI-is-authority rule; see Open Point 4.

## Plan-stage decisions (closing the items the Spec/ADRs deferred here)

**D2 — index-width schema.** `.amesh` **schema version 5** keeps version 4's header
and 60-byte vertex layout byte-for-byte, but stores **`uint32_t` indices**. The
importer is the only producer. The two versions use **separate code paths**, not one
unified reader:

- `encodeMeshArtifact`/`decodeMeshArtifact`/`DecodedMeshArtifact` stay exactly as
  they are and keep rejecting any version other than 4.
- New siblings (`encodeMeshArtifactU32`/`decodeMeshArtifactU32`, final names left to
  the implementer) handle version 5.

Rationale: this leaves Runtime, `StaticMeshAssetData`, and the RHI's hardcoded
`UINT16` binding untouched, which this Plan's scope requires. The cost is disclosed:
**a v5 `.amesh` cannot be rendered until a separate RHI index-type decision lands**
(Open Point 5).

**D8 — one tangent generator, two index widths.** `generateTangents()` gets a
`uint32_t`-index overload. Both overloads share one algorithm body (Lengyel,
ADR-0073), with the existing `ParsedMeshSource` entry point delegating to it.
Requirement: every existing cooked `.amesh` stays byte-identical, which the
determinism tests and re-cook verify (Verification). The glTF `TANGENT` accessor is
read only for presence reporting and then discarded.

**D7 — missing `COLOR_0`.** Vertex color is `(1, 1, 1)`. Atlantis's layout is RGB
(`mesh_artifact.h:28-29`), so D7's alpha of 1 has no destination. When `COLOR_0` is
present, it is read through cgltf's normalized accessor unpacking (float, unorm8, or
unorm16; VEC3 or VEC4), and **its alpha is dropped**. That loss is recorded in the
import report.

**D6 — coordinate conventions: no conversion needed.** Every convention matches:

| | glTF 2.0 core spec | Atlantis |
|---|---|---|
| Handedness / up | right-handed, +Y up | right-handed, Y-up (ADR-0050:137-144) |
| Matrix convention | column-major, column-vector, local = T·R·S | column-vector, local = T·(R·(S·v)) (ADR-0050:128-131) |
| Front face | counter-clockwise | `VK_FRONT_FACE_COUNTER_CLOCKWISE`, cull `NONE` (`vulkan_device.cpp:1201-1202`) |

So **no axis remap, winding flip, or handedness conversion is applied**. Only the
representation changes:

- *Quaternions.* glTF rotation quaternions become Atlantis Euler angles
  `(pitch x, yaw y, roll z)` composed as `R = Ry·Rx·Rz` (ADR-0050:155,
  `transform.h:8-9`). This is the standard YXZ decomposition. At pitch = ±π/2
  (gimbal lock) the rule is deterministic: roll = 0.
- *`matrix` nodes.* These are decomposed into T/R/S. A shear or non-orthogonal
  residual beyond tolerance is a named error. A negative determinant is folded into a
  negative X scale.

The named verification step (Spec Risks, D6) is to recompose every node's local
matrix with the ADR-0050 formula and compare it to cgltf's `cgltf_node_transform_local`.
This runs for each synthetic fixture and for all 5,908 Bistro nodes (Milestones 5 and 6).
One disclosed consequence: culling is off, so `doubleSided` (34 materials) has no
visible effect today; single-sided back faces also render.

**D3 — specular-glossiness conversion, with a cited formula.** The reference
implementation is Khronos's own converter:
`KhronosGroup/glTF`, `extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness/examples/convert-between-workflows-bjs/js/babylon.pbrUtilities.js`,
`PbrUtilities.ConvertToMetallicRoughness`. The extension README's Appendix links to it
("Conversion between the two PBR material models"). Its inputs are the spec's BRDF
definitions: `c_diff = diffuse·(1 − max(specular))`, `F0 = specular`,
`α = (1 − glossiness)²`. It was read at commit `11136cfa`. Normative form:

```
ε = 1e-6;  d = 0.04 (dielectric F0);  s = 1 − max(spec.r, spec.g, spec.b)
P(c) = sqrt(0.299 r² + 0.587 g² + 0.114 b²)          // perceived brightness
metallic = P(spec) < d ? 0
         : clamp((−b + sqrt(b² − 4ac)) / 2a, 0, 1),
           a = d,  b = P(diffuse)·s/(1 − d) + P(spec) − 2d,  c = d − P(spec)
baseRGB  = clamp( lerp( diffuse·s/(1−d)/max(1−metallic, ε),
                        (spec − d·(1−metallic))/max(metallic, ε),
                        metallic² ) )
baseA    = diffuse.a;   roughness = 1 − glossiness
```

**Error disclosure:**

1. Khronos publishes **no error bound**. It states the two models are not equivalent,
   and KhronosGroup/glTF Issue #1903 documents different rendered results.
2. The solve is scalar (perceived brightness), so a chromatic F0 on partial metals
   loses hue.
3. The formula is **per-material on factors**. It cannot convert
   `specularGlossinessTexture` texels, because that would require decoding BC7,
   which D4 rejects.

Consequence: when a spec-gloss texture is present, the factors are texture
multipliers (default 1.0), and the formula then yields `metallic = 1` exactly. For
example, `spec = (1, 1, 1)` gives `b = 0.92`, `c = −0.96`, `D = 1`, so
`metallic = 0.08 / 0.08 = 1`. That would make every textured material a pure metal.
The Plan therefore applies the formula **only to materials without
`specularGlossinessTexture`**. For textured spec-gloss materials the default is a
dielectric fallback: `metallic = 0`, `baseColor = diffuseFactor`,
`roughness = 1 − glossinessFactor`, with the texture dropped and reported. This needs
explicit sign-off (Open Point 2). The conversion happens once, at import, and is
baked into `.material.txt` (ADR-0083 D3). Every material's kind is `pbr_direct_lit`:
the ADR's "PbrClearcoat if eligible" never applies, because no clearcoat extension is
in the scope.

**Properties with no v6 destination** are listed per material in a generated
`import_report.txt`:

- `alphaMode`: 231 OPAQUE, 20 MASK, 3 BLEND
- `alphaCutoff`
- `transmissionFactor`: 18 materials
- `emissiveFactor`/`emissiveTexture`: 21 materials
- `occlusionTexture`
- `doubleSided`
- `normalTexture.scale ≠ 1`
- `specularGlossinessTexture`

The report is needed because the `.material.txt` grammar has no comment syntax, so
ADR-0083 D3's "recorded in the material's own provenance/comment" cannot be done
in-file. It is a disclosed adaptation (Open Point 3).

**Textures.** Only textures referenced by an imported material are cooked:

- *`MSFT_texture_dds` present.* The importer reads `{"source": <image>}` from the
  pass-through JSON and hands the `.dds` file to the unmodified cooker
  (`--kind=texture`). The cooker's `parseDdsBc7` does the validation, so the importer
  duplicates no DDS parser and takes no dependency on `atlantis_asset_cooker_lib`.
  This keeps ADR-0084's dependency surface exact.
- *No DDS extension.* The core `source` image (PNG/JPG) goes through the cooker's stb
  path. Color space is set by usage: base color → `srgb`, normal → `unorm`.
- *Material with no base-color texture.* The v6 grammar's mandatory `texture:` line is
  satisfied by one importer-generated 4×4 solid-white BC7 DDS in the import output,
  cooked like any other texture (Open Point 7).
- *Sampler mapping.* `LINEAR*` → `linear`, `NEAREST*` → `nearest`,
  `REPEAT` → `repeat`, `CLAMP_TO_EDGE` → `clamp_to_edge`. `MIRRORED_REPEAT`, or
  differing wrapS/wrapT, is a named error.

**Scene graph (D5).** Only the default scene is imported (`scene`, else `scenes[0]`).
Its nodes are flattened depth-first. `node_id` = glTF node index + 1, and synthetic
nodes are numbered after the last glTF id, deterministically. Three cases produce a
synthetic identity-transform child:

- each primitive of a multi-primitive mesh (Bistro has none);
- a light on a node that also has a mesh or camera, because the grammar forbids
  co-presence.

The rest of the rules:

- Camera nodes import as plain transform nodes, per the Spec Non-Goal. The active
  camera is `none`.
- Skins, animations, and morph targets are ignored with a report entry. The node's
  static bind pose is imported.
- **`KHR_lights_punctual`** maps as follows:
  - directional → `light=directional`;
  - point → `light=point` with `range` (an undefined or infinite range is a rule for
    Open Point 8);
  - **spot → named error `UnsupportedLightKind`**, because Atlantis has no spot kind
    (Open Point 8).
  - The 1-directional/4-point cap is pre-checked as a named error rather than left to
    the cooker's generic failure.
  - Light direction (glTF: the node's −Z) is checked against Runtime's extraction
    convention by reading it in Milestone 5. Runtime is not modified.
- `extensionsRequired` naming anything outside {the three Spec extensions +
  `KHR_lights_punctual`} fails fast. An unsupported extension from `extensionsUsed`
  fails only when an imported object actually carries it (Spec Req 3/7).

**Missing `NORMAL`/`TEXCOORD_0`.** Spec Req 2 lists them as optional, but ADR-0083
gives a synthesis rule only for `COLOR_0`. glTF-mandated flat normals would need a
vertex-splitting pass. Missing UVs would make tangent generation fail
(`DegenerateTangentBasis`). Default: named error `MissingRequiredAttribute`. Bistro
carries both attributes on every primitive (Spec Investigation 1). This needs sign-off
(Open Point 9).

**Normals.** Every `NORMAL` must pass the existing ±1% tolerance
(`isNormalLengthSquaredInTolerance`). Failures are a named error; there is no silent
renormalization. This is the "归一化校验" (normalization check) requirement.

**Logical-path namespace.** All generated identities live under `<import-name>/`
(default: the glTF file stem, so `bistro/`). This namespace keeps a future second
import from colliding.

- Meshes: `bistro/meshes/m<mesh>_p<prim>`. Glyph-safe, index-based, and independent
  of glTF names, which are neither unique nor path-safe.
- Materials: `bistro/materials/<index>.material.txt`.
- Scene: `bistro/bistro.scene.txt`.
- Textures: `bistro/<upstream-relative .dds path>`. The cooker is run with
  `--asset-root=content/`, so the relative path it hashes is the same string the
  materials reference.

AssetId uniqueness across the whole set is checked by the cooker's existing
`--validate-set`.

**Output atomicity (Req 7).** The importer writes everything into a staging directory
beside the output and renames it into place only after every artifact and source has
been written. On any error it deletes the staging directory. No partial set is ever
visible.

**Hand-off to the cooker (Req 6, ADR-0084).** The importer library **does not spawn
processes**. Alongside its outputs it writes `cook_manifest.txt`, one cooker
invocation per line, in dependency order: textures, then materials, then the scene.
A driver runs each line against the unmodified `atlantis_asset_cooker` exe: a CMake
`-P` script for tests (Milestone 6), or a human by hand.

**Content policy mechanics (Spec Investigation 4).**

- *Locations.* The script is `tools/content/fetch_bistro.ps1`, which is "dev
  tooling" per `tools/README.md` and linked to this Plan. The content goes to
  `content/bistro/` (gitignored). The committed provenance lives at
  `tools/content/bistro_source.provenance.txt`, beside the upstream licence copy
  `tools/content/bistro_upstream_LICENSE.txt`.
- *Missing-content behaviour.* Bistro tests use Catch2 tag `[bistro]` and ctest label
  `content`. They `SKIP()` when content is absent, printing the exact fetch command,
  following the `image_regression_gpu_tests.cpp:168` precedent. The content location
  is CMake cache variable `ATLANTIS_BISTRO_CONTENT_DIR`, default
  `${CMAKE_SOURCE_DIR}/content/bistro`.

## Subsystem boundary (ADR-0084, made concrete)

- **`src/tools/gltf_importer/`**
  - Targets: `atlantis_gltf_importer_lib` (STATIC) and `atlantis_gltf_importer`
    (exe, argument handling only).
  - Links: `Atlantis::AssetSystem` and `Atlantis::Core` PUBLIC; `Cgltf::Cgltf` and
    `atlantis_compiler_warnings` PRIVATE; `/w14062` scoped to the lib.
  - `cgltf.h` is included only by the lib's `.cpp` files, never by its headers.
    Exactly one TU defines `CGLTF_IMPLEMENTATION`. That TU is exempt from `/W4 /WX`
    (third-party code), and this is the only warning exemption.
- **No dependency on** RHI, Renderer, RenderGraph, Shader System, Vulkan Backend,
  Platform, World, Runtime, or `atlantis_asset_cooker_lib`.
  - This is enforced by a new include-scan test mirroring
    `tests/asset_system/module_boundary_tests.cpp:48-60`.
  - That test also asserts that no importer public header includes `cgltf.h`.
- **Build gating.** Registered from the root `CMakeLists.txt` inside the existing
  `if(NOT ANDROID)` block (`:96-98`). `include(cmake/AtlantisCgltf.cmake)` sits in
  that same block, so Android never fetches cgltf.

**Error handling.** Recoverable failures go through `atlantis::Result<T, GltfImportError>`
(AGENTS.md Error handling). There are no exceptions; cgltf is C and throws nothing.
Every `cgltf_result` is checked. Programmer errors are assertions. A
`gltfImportErrorMessage()` no-default switch is protected by `/w14062`. The exact
enumerator list is an implementation detail; the **categories** are fixed here:

1. **Input/parse:** file not found, malformed JSON / invalid glTF, buffer or image URI
   unresolvable, data-URI decode failure, `cgltf_validate` failure (for example an
   out-of-range accessor or bufferView).
2. **Unsupported content:** required or encountered extension outside the scope,
   non-`TRIANGLES` mode, non-indexed primitive, missing `POSITION` (and
   `NORMAL`/`TEXCOORD_0` per Open Point 9), spot light, mirrored-repeat or
   mixed-wrap sampler, non-decomposable node matrix, texture with neither a DDS nor a
   core image source.
3. **Value validation:** non-finite value, normal out of tolerance, index ≥ vertex
   count, more than 1 directional or 4 point lights, invalid or colliding logical
   path, tangent-generation failure (carrying the wrapped `CookError`).
4. **Output I/O:** staging creation, write, or rename failure.

## Milestones / Task Breakdown

Every milestone ends with a Windows build plus the relevant tests green. Each **Risk
gate** means: stop and report, with no workaround.

1. **cgltf integration + real-file smoke gate** (Spec Risks: this is the
   first-milestone gate; the Plan 0034 Milestone 1 pattern, `0034:37-58`).
   - Add `cmake/AtlantisCgltf.cmake`, pinned to `bbeb5b0b…` with the SHA256 above,
     using the stb pattern. Add the `Cgltf::Cgltf` INTERFACE target, a
     `src/tools/gltf_importer/` skeleton (lib + exe + implementation TU), and the
     root `CMakeLists.txt` wiring.
   - Add a `[bistro]` smoke test that parses the real `bistro.gltf` and asserts:
     - counts: **551 meshes, 551 primitives (all mode 4), 254 materials, 5,908 nodes,
       1 scene, 0 cameras/skins/animations, 343 textures, 686 images, 1 sampler**;
     - extensions: `has_pbr_specular_glossiness` on **234**, `has_transmission` on
       **18**, `extensionsUsed` exactly the three names, no lights;
     - `cgltf_load_buffers` and `cgltf_validate` succeed;
     - every texture's `extensions[]` contains a parsable `MSFT_texture_dds.source`,
       which closes ADR-0082's second open risk early.
   - For this milestone only, the input files are fetched by hand at commit
     `1da7b749` and verified against the two LFS oids above. Milestone 2 generalizes
     this.
   - The same test prints a **census** for the PR description (not asserted): how many
     materials have a spec-gloss texture, have no diffuse texture, have non-default
     spec/gloss factors, or have an emissive texture; the sampler's filter and wrap;
     how many nodes use `matrix` versus TRS; negative-determinant nodes; index
     component types; primitives missing `NORMAL`/`TEXCOORD_0`/`TANGENT`.
     Open Points 2, 4, 7, and 9 are ruled on with these real numbers.
   - **Risk gate:** any count mismatch, parse or validate failure, or cgltf failing to
     compile cleanly under this repo's MSVC flags stops the work. A switch to the
     `tinygltf` v3 fallback (ADR-0082) is a human decision, not an agent one.
     **Human checkpoint:** the gate result and census are reported before
     Milestone 3 starts.

2. **Content fetch infrastructure + provenance.** The `fetch_bistro.ps1` contract:
   - *Environment.* Windows PowerShell 5.1 only, with no git or git-lfs dependency,
     using `Invoke-RestMethod`/`Invoke-WebRequest`/`Get-FileHash`. A header comment
     records the source repository, the pinned commit, and the manifest-generation
     procedure.
   - *Pinning.* The script embeds a **pinned manifest of 392 `(path, sha256 oid, size)`
     entries**: 390 DDS, `bistro.gltf`, and `bistro.bin`; `bistro_without_paintings.gltf`
     is deliberately excluded. It also pins the upstream `LICENSE` by SHA256 (a
     non-LFS file fetched raw at the pinned commit). The manifest is generated once
     from the LFS pointer files at the pinned commit (`GIT_LFS_SKIP_SMUDGE=1` clone,
     or the tree plus contents API).
   - *Download.* Batch API `download` requests go out in chunks of ≤100 objects. Each
     object streams to a `.partial` file and is renamed only after its SHA256 equals
     the oid and its size matches. Already-present files that verify are skipped, so
     the script is resumable and idempotent. Any mismatch deletes the partial file and
     exits non-zero; nothing is ever trusted by name.
   - *Repo changes.* Add a `/content/` rule to `.gitignore`, the committed provenance
     file and licence copy, and a `tools/README.md` update.
   - *Why this pinning scheme.* **Per-file oids are chosen over a whole-archive
     hash.** An LFS oid *is* the SHA256 of the content (Git LFS pointer spec), so
     per-file pinning is exact and free. By contrast, GitHub's zip/tarball of an LFS
     repository contains pointer files unless the upstream owner enables "include LFS
     objects in archives", which is outside our control. Archive bytes are also not
     guaranteed stable over time, and a single 2.28 GB download cannot be resumed or
     verified incrementally.
   - *Wiring and census.* Wire `ATLANTIS_BISTRO_CONTENT_DIR` and the
     `SKIP`/`content` label into `tests/tools/gltf_importer/`. Run a one-time DDS
     header census over all 390 files (DXGI format, dimensions, mip count), reported
     in the PR.
   - **Risk gate:** any file not `BC7_UNORM`/`BC7_UNORM_SRGB` (for example a BC5
     normal map), any dimension not a multiple of 4, or any dimension over 8192 stops
     the work. Spec 0038's cooker would reject it, and widening that is outside this
     Plan. The batch API also counts as a gate: if it proves unavailable or rate-limited
     in practice, report it; do not switch silently to a git-lfs dependency.

3. **Mesh slice** (D1/D2/D7/D8, Req 2/5/7).
   - Asset System: add `.amesh` schema 5 with separate encode and decode functions and
     the `uint32_t` tangent overload (per the decisions above), with tests alongside
     the existing `mesh_artifact`/tangent tests.
   - Importer: add the primitive → `.amesh` v5 path (the uniform v5 choice is an open
     point, below) and the `.amesh.meta.txt` sidecar via the existing
     `serializeAssetMetadata`. Include the error enum, the staging/atomic-commit
     writer, and the exe's argument grammar (`--input=`, `--output-dir=`, `--name=`,
     `--content-root=`).
   - Tests use tiny glTF files built from bytes inside each test (`data:` URI buffers,
     so there are no fixture binaries):
     - 8-bit, 16-bit, and 32-bit index inputs;
     - a mesh with more than 65,535 vertices that round-trips as v5;
     - missing `COLOR_0` produces white;
     - `COLOR_0` present as VEC4 unorm8 drops alpha;
     - non-`TRIANGLES`, non-indexed, missing `POSITION`, out-of-range index, and
       out-of-range accessor are each rejected with a distinct error;
     - an out-of-tolerance normal is rejected;
     - a `TANGENT` in the input is ignored in favour of regenerated tangents;
     - a failed import leaves no output directory;
     - two imports of the same input are byte-identical (determinism).
   - **Risk gate:** any change in an existing cooked `.amesh` byte stream stops the
     work, because the tangent refactor must be behaviour-neutral.

4. **Material + texture slice** (D3/D4, Req 3/6). Implement:
   - the spec-gloss → metallic-roughness conversion (factor path plus textured
     fallback), core metallic-roughness passthrough, and transmission → `pbr_direct_lit`
     with a report entry;
   - `.material.txt` v6 generation, round-trip checked through the existing
     `parseMaterialSource()`;
   - DDS/PNG texture resolution, the sampler mapping, the white fallback texture,
     `import_report.txt`, and the texture and material lines of `cook_manifest.txt`.
   - Unit tests:
     - conversion vectors: dielectric (`spec = 0.04` → `metallic = 0`); gold-like
       `spec = (1.0, 0.766, 0.336)` with black diffuse → `metallic ≈ 1`; a mid-case.
       Expected values are computed from the cited Khronos formula and noted beside
       each case.
     - the textured spec-gloss fallback;
     - an unsupported material extension rejected;
     - `MIRRORED_REPEAT` rejected;
     - a missing DDS file rejected;
     - generated source parses and cooks through the real `cookMaterial()`.
   - **Risk gate:** if `MSFT_texture_dds` is not reachable through cgltf's
     pass-through (Milestone 1 will already know), stop for the ADR-0082 fallback
     decision.

5. **Scene-graph slice** (D5/D6, Req 4). Implement:
   - hierarchy flattening, the quaternion → YXZ Euler conversion and `matrix`
     decomposition, synthetic child nodes, `KHR_lights_punctual`
     (directional/point mapped, spot rejected, cap pre-checked), and ignore-and-report
     for camera/skin/animation/morph;
   - `.scene.txt` v4 generation, round-trip checked through `parseSceneSource()`.
   - Unit tests:
     - Euler round-trip across all octants plus gimbal lock (recompose via ADR-0050
       and compare to the cgltf local matrix, 1e-5);
     - `matrix` decomposition, including a negative determinant, with shear rejected;
     - a 3-level hierarchy;
     - a synthetic `KHR_lights_punctual` fixture: 1 directional + 2 point imported,
       spot rejected, a 5th point rejected (the Spec's disclosed Bistro gap, covered
       here);
     - light direction.
   - Read-only confirmation step: cite the Runtime light-direction derivation and
     state the match with glTF's −Z in the PR.
   - **Risk gate:** if Runtime's directional-light convention is not the node's −Z,
     stop. The fix belongs to a Spec, not a silent sign flip.

6. **End-to-end on real Bistro + measurements** (Req 1/6, Spec Testing bullet 2,
   Spec open question on time/memory). A ctest `content`-label test drives a CMake
   `-P` script: import `content/bistro/bistro.gltf`, then run every
   `cook_manifest.txt` line through the real `atlantis_asset_cooker`, then run
   `--validate-set` over all declared logical paths. A `[bistro]` load-verification
   test then:
   - decodes all 551 v5 `.amesh` files, with summed triangles **= 1,753,630** and
     vertices **= 1,738,262**, and the largest primitive at 126,990 vertices;
   - loads all `.amaterial` files through `loadMaterialAsset`, and every referenced
     `.atex` through `loadTextureAsset` (layout `Bc7`);
   - decodes the `.ascene`, with node count = 5,908 + synthetic nodes;
   - checks that every scene mesh/material AssetId resolves to a produced artifact,
     and that every node's recomposed local matrix matches cgltf's.

   The **full** asset is used, not a subset. The test is skipped unless content is
   present, so it never taxes a default run. Wall-clock time for import and for cook
   (separately), peak working set, and output bytes are measured and reported in the
   PR; the Plan only fixes that they are measured.
   **Risk gate:** any real-data tangent failure (`DegenerateTangentBasis` or
   handedness conflict) or normal-tolerance failure stops the work. The possible
   resolutions (upstream tangents for those meshes, relaxing ADR-0073, a
   renormalization policy) each amend an Accepted ADR, which is a human decision. If
   end-to-end wall clock exceeds 30 minutes, report and propose a subset.

7. **Full regression, both platforms.**
   - Windows: Debug + Release full build, then full `ctest -C Debug` twice — once
     with content present, once absent (showing `[bistro]`/`content` as
     **Skipped, not Passed/Failed**).
   - Re-cook all checked-in assets and confirm the artifacts are byte-identical to
     `main`.
   - Android: the `assembleDebug` chain is green. This proves the `NOT ANDROID`
     gating: cgltf is neither fetched nor built, and there is no importer leakage.
   - **Risk gate:** any regression in an existing test, a changed existing artifact,
     or an Android configure that touches cgltf stops the work.

## Files / Modules Touched (expected)

- **New:**
  - `cmake/AtlantisCgltf.cmake`
  - `src/tools/gltf_importer/` (`CMakeLists.txt`, `main.cpp`, importer lib sources
    and headers, `cgltf_implementation` TU)
  - `tests/tools/gltf_importer/` (`CMakeLists.txt`, unit tests, `[bistro]` tests,
    end-to-end `-P` driver script, include-scan boundary test)
  - `tools/content/fetch_bistro.ps1`
  - `tools/content/bistro_source.provenance.txt`
  - `tools/content/bistro_upstream_LICENSE.txt`
- **Changed:**
  - `CMakeLists.txt` (root: cgltf include + importer subdirectory inside
    `if(NOT ANDROID)`; test subdirectory in the tests block)
  - `.gitignore` (`/content/`)
  - `tools/README.md`
  - Asset System, additive only:
    - `mesh_artifact.h`/`.cpp` (schema 5 functions)
    - `mesh_tangent_generation.h`/`.cpp` (`uint32_t` overload, shared body)
    - `errors.h`, only if v5 decoding needs a distinct `ArtifactDecodeError` value
  - Plus their existing tests in `tests/asset_system/`.
- **Not touched:**
  - the RHI public API and Vulkan Backend (including the `UINT16` index binding)
  - RenderGraph, Renderer, Shader System and all shaders
  - Platform, World, Runtime and `atlantis_runtime_host`
  - `atlantis_asset_cooker` / `_lib` (unmodified per Req 6, `dds_parser` included)
  - the `.material.txt` v6, `.scene.txt` v4, `.atex` v2 and `.amesh` v4 grammars and
    byte formats
  - the four `atlantis_add_*_asset` CMake functions and `assets/`
  - every golden image, `android/`, and all existing specs and ADRs

## Sequencing & Dependencies

- **Satisfied prerequisites.** Spec 0038 has landed (`8b09a24`), so Milestone 4's D4
  dependency is met; Spec 0036's DAG edge W0 → W1 is satisfied.
- **Milestone 1 is a hard root.** Nothing starts before its gate and human checkpoint.
- **After Milestone 1, two branches can proceed in parallel:**
  - Milestone 2 (script and content; no C++);
  - Milestone 3 (synthetic fixtures only; needs no content).
- **Middle chain.** Milestone 4 needs Milestone 3's skeleton (error enum,
  staging/writer, manifest). Milestone 5 needs Milestone 3's mesh logical paths and
  Milestone 4's material logical paths.
  - The conversion math in Milestone 4 and the Euler/decomposition math in Milestone 5
    are pure functions and can be written in parallel; only their integration is
    ordered.
- **Join and finish.** Milestone 6 is the join (2 + 3 + 4 + 5). Milestone 7 comes
  last.
- **Branching and PRs.** One `feature/0037-gltf-importer` branch, one commit or more
  per milestone, one PR (the Plan 0038 precedent). If the change becomes unreviewable
  as one PR, split after Milestone 3; the milestones are designed to allow that.

## Verification Checklist

- [ ] **Unit tests (Spec Testing bullet 1, Req 2–7):**
  - Milestone 3 cases: `COLOR_0` default, non-`TRIANGLES`/non-indexed/missing
    `POSITION`/out-of-range rejections, `uint32_t` round-trip;
  - Milestone 4: spec-gloss conversion vectors against the cited formula;
  - Milestone 5: `KHR_lights_punctual` synthetic fixture, D6 recomposition;
  - schema 5 encode/decode, and v4 decode rejecting v5 and the reverse;
  - importer include-scan boundary test;
  - all GPU-independent, in `ctest` without content.
- [ ] **Real-asset end-to-end (Spec Testing bullet 2, Req 1/6):** Milestone 6
  import → real cooker → `--validate-set` → load checks with the exact Investigation 1
  totals.
- [ ] **Milestone 1 smoke gate:** counts match Spec Investigation 1 exactly (Spec Risks).
- [ ] **D6 named verification:** local-matrix recomposition matches for fixtures and
  all Bistro nodes, not deferred to a render.
- [ ] **D3 citation:** the formula and reference are recorded in code at the
  conversion function (ADR-0067 inline-attribution style) and the tests cite the
  expected-value source.
- [ ] **Content policy:** fetch verifies every SHA256 (a tampered-byte negative test
  on one small file, run by hand and reported in the PR); `/content/` is ignored;
  `[bistro]` tests skip with instructions when content is absent.
- [ ] **Regression:** existing `.amesh`/`.amaterial`/`.ascene`/`.atex` artifacts are
  byte-identical after the tangent refactor; full `ctest` is green (Debug); Release
  builds.
- [ ] **Android:** `assembleDebug` is green; cgltf is absent from the Android
  configure.
- [ ] **Image regression tests:** N/A. There is no rendered-output change; the Bistro
  golden is workflow ⑦'s (Spec Testing bullet 4).
- [ ] **Vulkan Validation Layers clean:** N/A. The tool is offline and CPU-only
  (Spec Testing bullet 3). Milestone 7's full ctest still runs the existing GPU tests
  clean.
- [ ] **Measurements reported in the PR:** import time, cook time, peak memory, and
  artifact bytes (Spec Risks open question).

## Non-Goals (reaffirmed from the Spec, not extended)

- **No rendering of imported content.** Rendering Bistro belongs to workflow ⑦, and
  it is additionally blocked on Open Points 5 and 6.
- **No glTF import of animation, skinning, or morph targets.** When present, these
  are ignored and reported, never mapped.
- **No glTF write-back or export.**

Also excluded, per the Spec: camera mapping, non-triangle modes, CPU BC decode, new
`MaterialKind`s, transmission as a feature, and a general multi-format framework.

## Open Points (for Joint Human Review)

1. **Licence chain.** PR #165's own provenance records Bistro as CC-BY 4.0 (Amazon
   Lumberyard / NVIDIA ORCA), repackaged under the repository's MIT licence. This
   Plan's provenance file will carry both, including CC-BY attribution text.
   *Question:* does the Spec's "single license" statement need a follow-up correction?
2. **D3 for textured spec-gloss materials.** Applying the formula to factors alone
   makes them fully metallic (see the D3 decision above). *Proposed:* dielectric
   fallback with the texture dropped and reported. *Alternative:* a named error, which
   would block most of Bistro. The Milestone 1 census supplies the counts.
3. **Properties with no destination recorded in `import_report.txt`, not in the
   material file.** ADR-0083 D3 says "provenance/comment", but the v6 grammar has no
   comments. This also covers alphaMode for all 254 materials, not just the
   transmission ones. **Transparency cannot be expressed until a workflow ④ schema
   bump.**
4. **DDS colour space versus usage.** At least one `_diff` DDS is `BC7_UNORM`. Under
   Spec 0038's rule that the DXGI format is the sole authority, such base-color
   textures will sample as linear. The unmodified cooker offers no override.
   *Proposed:* report the mismatches (Milestone 2 census) and change nothing; any
   override is a Spec 0038 follow-up.
5. **uint32 meshes cannot render yet.** The RHI binds `UINT16` only
   (`vulkan_command_list.cpp:387`). Consuming schema 5 needs a new RHI index-type
   decision, which requires its own Spec and ADR.
   *Question:* register it as a new Spec 0036 roadmap gap ahead of ⑦, like ⓪ was.
   Sub-question: should the importer emit v4 for the 548 primitives that fit in
   `uint16_t`, and v5 only for the 3 oversized ones? *Proposed:* uniform v5, to keep
   one importer path.
6. **Mip-chain passthrough.** Plan 0038 (`:114-116`) deferred it to this importer.
   It needs `.atex` multi-mip plus a Runtime upload loop, both outside this Plan's
   Tools-only scope and red lines. *Proposed:* re-defer it explicitly to a follow-up
   Spec before ⑦. Base-mip-only 4096² textures will alias.
7. **Materials without a base-color texture.** *Proposed:* an importer-generated 4×4
   white BC7 DDS. *Alternative:* a named error.
8. **Lights.** Spec Goals name spot lights, but Atlantis has no spot kind.
   *Proposed:* named error. The intensity units (glTF lux/candela) against Atlantis's
   intensity, and point `range` when undefined, also need a rule. *Proposed:* pass the
   values through unchanged plus a report note. Bistro is unaffected (0 lights).
9. **Missing `NORMAL`/`TEXCOORD_0`.** The Spec says optional; the Plan proposes a
   named error (no synthesis rule in ADR-0083).

## Rollback Plan

The change is purely additive:

- a new tool, new tests, a new script, and new provenance files;
- `.amesh` schema 5 and the tangent overload added beside unchanged v4 paths.

To roll back, revert the PR. Existing artifacts and consumers never read v5, so no
data migration is needed. Fetched `content/` is outside git and can be deleted
locally.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- The Validation-Layer item is N/A for the new code; existing GPU tests still run clean.
- The Milestone 1 gate result and human checkpoint are recorded in the PR.
- Import/cook measurements are reported in the PR.
- Every Open Point ruling is reflected in the implementation and cited in the PR.
