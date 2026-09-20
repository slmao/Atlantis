# Plan: RHI Index-Type Parameterization (uint16/uint32)

- **Spec:** [Spec 0039: RHI Index-Type Parameterization (uint16/uint32)](../specs/0039-rhi-index-type-parameterization.md)
  (`Approved`, 2026-09-20) —
  [ADR-0086](../adr/0086-rhi-index-type-public-expression-and-backward-compatibility.md)
  (`Accepted`),
  [ADR-0087](../adr/0087-asset-system-index-width-representation-and-artifact-version-dispatch.md)
  (`Accepted`)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending — this Plan and Spec 0039 must be reviewed
  together and Implementation explicitly authorized before Milestone 1 begins.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0039 in full — Spec 0036 workflow ①b — so a `.amesh` schema-5
artifact loads through `loadStaticMeshAsset()` and renders through the existing
Renderer/RenderGraph/Vulkan path, with the `uint16_t` path byte-identical. Spec
0039's rulings O1–O5 (2026-09-20) are binding on this Plan and are not reopened
here; each is cited where it lands.

## Pre-drafting reading (cited, not restated)

Spec 0039's Investigations 1–6 were re-walked at Plan granularity against
`origin/main` at `c22a97c`. Everything the Spec found still holds; the four
corrections below are recount corrections only — no conclusion changes.

**1. `rhi/types.h` — where `IndexType` lands.** `BufferPurpose` is declared at
`src/rhi/include/atlantis/rhi/types.h:101-107`, `AddressMode` at `:145-148`,
`BufferCreateParams` at `:150-153` with its free `operator==` declared at `:155`
and defined at `src/rhi/src/types.cpp:13-15`. `IndexType` goes next to
`BufferPurpose`; `BufferCreateParams` gains a third, defaulted member and
`operator==` a third conjunct (ADR-0086 Decision items 1-2).

**2. `rhi::Buffer` and its two in-tree implementations.** The interface is
`src/rhi/include/atlantis/rhi/buffer.h:17-35` — three pure virtuals, the
purpose-fixed-at-creation contract stated at `:9-10`. Implementations:
- `src/vulkan_backend/src/vulkan_buffer.h:25-51` — members at `:45-50`,
  inline accessors at `:36-38`; constructor declared `:27-28` and defined
  `src/vulkan_backend/src/vulkan_buffer.cpp:5-12`. The single construction site
  is `src/vulkan_backend/src/vulkan_device.cpp:870-871`
  (`std::make_unique<VulkanBuffer>(..., params.purpose, params.sizeBytes)`),
  inside `VulkanDevice::createBuffer()` (`:797-872`), whose purpose→usage switch
  is at `:802-818`.
- `tests/render_graph/fake_command_list.h:195-209` (`FakeBuffer`, namespace
  `atlantis::render_graph::test`) — constructor `:197-199`, accessors
  `:201-203`. It is reused by `tests/renderer/renderer_ownership_tests.cpp`
  (`#include "fake_command_list.h"` at `:19`, `using` at `:35`), which
  constructs an Index-purpose `FakeBuffer` **11 times**. Giving the constructor
  a defaulted third parameter keeps all 11 unchanged.

**3. The Vulkan bind switch.** `VulkanCommandList::bindIndexBuffer()` at
`src/vulkan_backend/src/vulkan_command_list.cpp:384-388`; the purpose
`ATLANTIS_CHECK` is `:385` and the literal is `:387`. Re-confirmed by a
whole-tree search: this is still the only `VK_INDEX_TYPE` token in the
repository. Declaration at `src/vulkan_backend/src/vulkan_command_list.h:68`.

**4. `loadStaticMeshAsset()` dispatch, `indices32()`, and the 2^24 gate.**
- `src/asset_system/src/load.cpp:40-83`: decode at `:50`, error map at `:51`,
  count derivation at `:59-63`, the artifact/metadata cross-check at `:65-68`,
  the sidecar self-consistency check at `:77-79`, construction at `:81-82`.
- `src/asset_system/include/atlantis/asset_system/static_mesh_asset_data.h:16-36`
  and `src/asset_system/src/static_mesh_asset_data.cpp:7-18`. **Correction
  worth pinning:** `indexCount()` (`.cpp:16-18`) returns `indices_.size()` and
  is **not** one of the guarded accessors — it must report whichever vector is
  populated, for both widths.
- `AssetLoadError` is `src/asset_system/include/atlantis/asset_system/errors.h:76-82`.
  A whole-tree search finds **no exhaustive switch and no `toString()` over
  `AssetLoadError`** anywhere in `src/`, `tests/` or `examples/`, so adding an
  enumerator breaks no build under `/W4 /WX /w14062`.
- **The O1 gate is O(1), not a scan.** `decodeMeshArtifactU32()` already
  rejects any index `>= vertexCount` at
  `src/asset_system/src/mesh_artifact.cpp:373`, and only rejects
  `vertexCount == 0` (`:307`) — there is no upper bound. So `vertexCount <=
  2^24` is exactly equivalent to "every index value <= 2^24−1", and needs no
  per-index pass. See Plan-stage decision P2.
- `peekMeshArtifactSchemaVersion()` has everything it needs already public:
  `kMeshArtifactHeaderSizeBytes` (`mesh_artifact.h:52`), the two version
  constants (`:35`, `:75`); the two decoders' own header preambles are
  `mesh_artifact.cpp:105-115` and `:283-294`.

**5. The composition root.** `src/runtime/src/scene_load.cpp:111-114` is the
one `createMesh()` call that must branch; it sits inside the per-mesh loop
`:101-119`, after the load at `:102-103` and the `device != nullptr`
`ATLANTIS_CHECK_MSG` at `:109-110`. `src/runtime/src/runtime_application.cpp:824-835`
builds the output-transform fullscreen-triangle index buffer directly and is
**not** touched (it keeps the `Uint16` default).

**6. Renderer.** `createMesh()` declared
`src/renderer/include/atlantis/renderer/mesh.h:47-52`, defined
`src/renderer/src/mesh.cpp:11-41`; the index size computation is `:31`, the
Index `createBuffer` `:32-33`, the memcpy `:38`. `Mesh` (`mesh.h:16-35`) and
`CreateMeshError` (`:37-40`) are the only other types in that header.

**7. Zero-impact confirmation for the bind surface (recounted).**

| Surface | Spec 0039 said | Recounted on `c22a97c` | Touched by this Plan |
|---|---|---|---|
| `bindIndexBuffer()` call expressions | 12 | **13** — `renderer.cpp:80,106,116,264`; `pbr_render_gpu_tests.cpp:1118`; `descriptor_set_revisit_gpu_tests.cpp:253,261,274`; `pipeline_depth_write_gpu_tests.cpp:254,261,268`; `shadow_map_render_gpu_tests.cpp:296,307` | **none** |
| `createMesh()` call expressions | 20 in 13 files | **17 in 13 files** (the Spec counted three `ATLANTIS_LOG_ERROR("createMesh() failed…")` strings) | **1** (`scene_load.cpp:111`) |
| `loadStaticMeshAsset()` call expressions | 14 | **19** — 1 in `src/runtime/`, 18 in tests/fixtures | **0 signature-forced**; 1 behavioural (`scene_load.cpp:102`) |
| `createBuffer({.purpose = …Index…})` sites | 24 | **26** — 2 in `src/`, 2 in `examples/`, 22 in tests | **1** (`mesh.cpp:33`) |
| `FakeBuffer(…Index…)` constructions | ~20 | **11**, all in `renderer_ownership_tests.cpp` | **none** |
| `rhi::Buffer` / `rhi::CommandList` implementations | 2 / 2 | 2 / 2, confirmed | **2 / 0** |

The recount strengthens rather than weakens ADR-0086's blast-radius argument:
the defaulted-at-creation shape leaves 13 + 16 + 18 + 25 + 11 = **83** existing
call sites untouched, and changes **two** (`scene_load.cpp:111`,
`mesh.cpp:31-33`) plus the two `Buffer` implementations.

## Plan-stage decisions (closing what the Spec/ADRs left to this Plan)

**P1 — `Buffer::indexType()` is pure virtual, not defaulted.** Per Spec 0039
ruling O3 (accepted, disclosed, not mitigated): no non-pure default
implementation, no shim. Both implementations get a real member.
`VulkanBuffer`'s constructor gains a sixth parameter; `FakeBuffer`'s gains a
third, **defaulted to `Uint16`** so its 11 existing constructions are
untouched. `VulkanDevice::createBuffer()` passes `params.indexType` through.

**P2 — the O1 gate lives in `loadStaticMeshAsset()`, as an O(1) vertex-count
check.** Ruling O1 names "the producing or loading layer"; this Plan picks the
loading layer, for three reasons: it is the gate every consumer passes
regardless of producer; the decoder's existing `index < vertexCount` invariant
(`mesh_artifact.cpp:373`) makes `vertexCount <= 2^24` an exact and free
equivalent of the real rule; and the importer is today's only producer but not
guaranteed to be tomorrow's. Concretely: a public
`kMaxDrawableVertexCount = 1u << 24` constant (Asset-System-owned, commented as
tracing to ADR-0086 Decision item 6 — a plain number, never an RHI type, so
ADR-0043 and `tests/asset_system/module_boundary_tests.cpp:22-26` are
satisfied), and a new `AssetLoadError` enumerator (working name
`IndexValueExceedsDrawableRange`). **Not** added: a second check in the glTF
importer — see Open point Q1.

**P3 — the v5 path reuses `loadStaticMeshAsset()`'s existing cross-checks
verbatim.** Asset id, vertex count, index count, vertex stride
(`load.cpp:65-68`) and the sidecar self-consistency check (`:77-79`) are
version-independent and apply to v5 unchanged. The `.amesh.meta.txt` schema is
not touched (it records no schema version, and Spec 0039 ruling O5's reasoning
applies to it identically).

**P4 — one Renderer overload, no shared private helper extracted.** The
`uint32_t` overload duplicates `mesh.cpp:11-41`'s ten-line body with two
changed tokens rather than introducing a `createMeshImpl(const void*,
std::size_t indexStrideBytes, …)`. Rationale: the `uint16_t` overload's body
must stay provably unchanged for the "u16 path byte-identical" claim, and a
shared helper makes that a reading exercise instead of a diff. If review
prefers the helper, it is a local change confined to `mesh.cpp`.

**P5 — the large-mesh grid is authored inside the existing fixture's camera
frustum.** `setUpMinimalCubeFixtureFromAsset()`
(`tests/image_regression/fixture/minimal_cube_fixture.h:79-92`) bakes its
camera, material, lighting and extent at setup; ADR-0042's calibration evidence
is tied to exactly those values. The grid is therefore authored in the minimal
cube's own coordinate range (a subdivided plane occupying the cube's own
screen-space footprint), and **no fixture parameter, camera or shader is
changed**. Normals are `(0,0,1)` and tangents `(1,0,0,1)` — trivially unit,
orthogonal and ±1-handed, so the v5 decoder's per-vertex battery
(`mesh_artifact.cpp:334-367`) passes by construction.

**P6 — vertices are coloured by index band.** Vertices with index `>= 65,536`
get a distinct vertex colour from those below. This is what turns the
large-mesh test from "something rendered" into a real 16-bit-truncation
detector: under truncation, index 65,536 wraps to 0 and those triangles
collapse to a corner, removing the high-band colour from the frame entirely.

## Milestones / Task Breakdown

Three milestones, each independently reviewable and each ending green. No
milestone leaves the tree unbuildable or a test red.

### Milestone 1 — RHI + Vulkan Backend (Spec Requirements 1, 2, 7)

1. `rhi/types.h`: add `enum class IndexType { Uint16, Uint32 };` beside
   `BufferPurpose`; add `IndexType indexType = IndexType::Uint16;` to
   `BufferCreateParams`, documented as meaningful only for
   `BufferPurpose::Index`. Extend `operator==` in `src/rhi/src/types.cpp:13-15`.
2. `rhi/buffer.h`: add `[[nodiscard]] virtual IndexType indexType() const = 0;`
   with the `purpose() == BufferPurpose::Index` precondition in its comment
   (ruling O2).
3. `VulkanBuffer`: new member + accessor with `ATLANTIS_CHECK`; constructor
   parameter; `VulkanDevice::createBuffer()` (`vulkan_device.cpp:870-871`)
   threads `params.indexType`. No change to the purpose→usage switch.
4. `FakeBuffer` (`tests/render_graph/fake_command_list.h:195-209`): same, with
   the constructor parameter defaulted to `Uint16`.
5. `VulkanCommandList::bindIndexBuffer()` (`vulkan_command_list.cpp:387`):
   replace the literal with a `switch (buffer.indexType())` over both
   enumerators — no `default:` label, so `/w14062` keeps it exhaustive.
6. GPU-independent tests in `tests/rhi/types_tests.cpp`: the default is
   `Uint16`; `operator==` separates two params differing only in `indexType`;
   the enum's two values round-trip through `BufferCreateParams`.

**Risk gate.** If adding the pure virtual breaks any implementation not named
in the re-read above, or if `/W4 /WX` flags the new switch or the widened
`operator==` anywhere, **stop and report** — a third `Buffer` implementation
would mean the re-read was wrong, which is an ADR-0086 fact, not a coding
problem. Also stop if the full existing `ctest` run is not green at the end of
this milestone: at this point nothing should behave differently at all, so any
red test is a real regression, not an expected one.

### Milestone 2 — Asset System (Spec Requirements 4, 5, 7)

1. `static_mesh_asset_data.h`: add `enum class MeshIndexType { Uint16, Uint32 };`
   and a second constructor taking `std::vector<std::uint32_t>`; add
   `indexType()` and `indices32()`; add the `ATLANTIS_CHECK` guard to
   `indices()` and `indices32()` (ruling O2's style, ADR-0087 Decision item 2).
   `indexCount()` reports the populated vector (re-read item 4).
2. `mesh_artifact.h`/`.cpp`: add `peekMeshArtifactSchemaVersion(bytes)`
   returning `Result<std::uint32_t, ArtifactDecodeError>` (`TooSmallForHeader`,
   `BadMagic`, else the version). **Neither existing decoder is edited** —
   Plan 0037 D2 and ADR-0087 Decision item 4.
3. `errors.h`: add the `AssetLoadError` enumerator from P2, with a comment
   citing ADR-0086 Decision item 6.
4. `load.cpp`: peek, then dispatch to `decodeMeshArtifact()` or
   `decodeMeshArtifactU32()`; apply P2's `vertexCount <= kMaxDrawableVertexCount`
   gate on the v5 branch; run the existing cross-checks on both branches (P3);
   construct the matching `StaticMeshAssetData`.
5. Tests in `tests/asset_system/load_tests.cpp` (and a new
   `mesh_artifact_peek_tests.cpp` if the peek's own cases read better
   separately): a v4 artifact loads with `Uint16` and byte-identical
   `indices()`; a v5 artifact loads with `Uint32` and correct `indices32()`; a
   v5 artifact whose sidecar disagrees still gives `MetadataArtifactMismatch`;
   an artifact with an unknown schema version gives `ArtifactDecodeFailed`; a
   v5 header declaring `vertexCount > 2^24` gives the new named error; each
   wrong-width accessor aborts, verified through
   `atlantis::assertions::setFailureHandler()`
   (`src/core/include/atlantis/assert.h:21-25`).

**Risk gate.** The over-range test must not need a real 2^24-vertex file — it
is constructed as a header-only rejection, so if the only way to exercise it is
to materialize a ~1 GB artifact, **stop and report**: that would mean the gate
was placed at the wrong layer and P2 needs rework. Also stop if any existing
`tests/asset_system/` test changes behaviour: every v4 assertion must pass
untouched.

### Milestone 3 — Renderer, Runtime, and the verification set (Requirements 3, 6; Spec T2-T6)

1. `renderer/mesh.h`/`mesh.cpp`: the `const std::uint32_t*` overload (P4).
2. `src/runtime/src/scene_load.cpp:111-114`: branch on
   `meshAssetData.indexType()` and call the matching overload. No other Runtime
   change.
3. `tests/renderer/`: GPU-independent check that the `uint32_t` overload
   requests an `Uint32` index Buffer of `indexCount * 4` bytes, using the
   existing `FakeBuffer`/fake-device recording path.
4. **Equivalence test (Spec T2), no new golden.** One authored
   `ParsedMeshSource`, encoded by both `encodeMeshArtifact()`
   (`mesh_artifact.h:61`) and `encodeMeshArtifactU32()` (`:84`) — the same
   source and the same tangents, so vertex bytes are identical by construction
   — written with `serializeAssetMetadata()` sidecars to a temp directory, each
   rendered through `setUpMinimalCubeFixtureFromAsset()`, then
   `compareBuffers()` must report `maxChannelDiff == 0` and
   `outOfToleranceCount == 0`. Precedent for a golden-free two-frame compare:
   `tests/image_regression/lighting_demo_gpu_tests.cpp:430`.
5. **Large-mesh golden (Spec T3).** A closed-form 258×258 grid — 66,564
   vertices, 66,049 quads, 132,098 triangles, 396,294 indices — generated by
   one shared function, encoded with `encodeMeshArtifactU32FromIndices()`
   (`mesh_artifact.h:90-92`), rendered through the same fixture (P5, P6).
   Assertions: the golden compare at ADR-0042's zero tolerance, **plus** a
   direct check that the high-index band's colour is present in the frame and
   occupies the expected screen region. The generator function is shared
   between the test and a new `golden_generator` main so the golden is
   reproducible; the `bc7_dual_quad` generator
   (`tests/image_regression/golden_generator/CMakeLists.txt:156-206`) is the
   wiring precedent.
6. **Golden capture, two commits.** The PNG + sidecar are added in their own
   separate, subsequent commit against a clean already-committed tree —
   ADR-0042's "Initial baseline bootstrap" amendment, item 4. The PR must carry
   that category's four evidence items (item 5): human visual inspection of the
   captured frame, a capture-compare cycle at zero channel difference, a real
   GPU run with Validation Layers clean, and a citation of the fixture's
   existing channel-tolerance calibration evidence.
7. **Real-data test (Spec T6), `content`-labelled.** Import Bistro, load and
   draw `bistro_mesh_246_0` (127,104 vertices), assert a non-degenerate frame,
   `SKIP()` when the content is absent. It needs both the importer library and
   the image-regression fixture, so it lives in its own small binary with
   `catch_discover_tests(... TEST_SPEC "[bistro]" PROPERTIES LABELS "content"
   SKIP_RETURN_CODE 4)` — the split-registration pattern at
   `tests/tools/gltf_importer/CMakeLists.txt:41-49`, and
   `ATLANTIS_BISTRO_CONTENT_DIR` from `:29-33`. Windows-only, because
   `atlantis_gltf_importer_lib` is registered under `if(NOT ANDROID)`.
8. **Close the Ruling 5 disclosure.** Update the header comment at
   `tests/tools/gltf_importer/bistro_end_to_end_tests.cpp:30-33`, which
   currently states that Runtime/RHI cannot draw v5 meshes "until Spec 0036
   workflow 1b parameterizes the index type".
9. **Dual-platform regression.** Windows Debug and Release full builds + full
   `ctest` in both; every existing golden green; cooker determinism/byte-identity
   green; Android `assembleDebug` green — check ASProxy first if the Gradle
   wrapper stalls, and fall back to the locally installed Gradle with
   `--offline` (the Plan 0037 Milestone 7 precedent).

**Risk gates.**
- **Equivalence not bit-exact.** If the two frames differ at all, **stop and
  report**. A non-zero difference means index width alone changed the raster
  result, which contradicts ADR-0086's premise — it is a design finding, not a
  tolerance to widen. ADR-0042's tolerances are non-configurable and must not
  be touched.
- **High-index band not provably on screen.** 66,564 − 65,536 = 1,028 vertices
  is about four of the grid's 258 rows, so the high-index band is a thin strip.
  If it cannot be asserted robustly at 512×512, **stop and report** with the
  measured strip height rather than silently enlarging the grid — the 66,564
  figure came from the human. See Open point Q2.
- **Any existing golden moves.** Stop and report; that is the "u16 path
  unchanged" claim failing, and it invalidates the milestone.
- **Validation Layers non-clean** on either new GPU test: stop and report.
- **Android build red:** diagnose ASProxy/Gradle first, per above; if the
  failure is genuinely in the native build, stop and report — none of this
  Plan's changes should reach Android's `.cxx` output beyond the RHI headers.

## Files / Modules Touched (expected)

**Created**
- `tests/image_regression/<name>_gpu_tests.cpp` (equivalence + large-mesh)
- `tests/image_regression/golden_generator/<name>_main.cpp`
- `tests/image_regression/goldens/<slug>/<slug>_512x512_rgba8unorm.png` + `.sidecar.txt` (own commit)
- a small `content`-labelled GPU test binary and its `CMakeLists.txt` entry (Milestone 3.7)
- possibly `tests/asset_system/mesh_artifact_peek_tests.cpp`

**Changed**
- `src/rhi/include/atlantis/rhi/types.h`, `src/rhi/include/atlantis/rhi/buffer.h`, `src/rhi/src/types.cpp`
- `src/vulkan_backend/src/vulkan_buffer.h`, `vulkan_buffer.cpp`, `vulkan_device.cpp` (one construction line), `vulkan_command_list.cpp` (one bind body)
- `src/renderer/include/atlantis/renderer/mesh.h`, `src/renderer/src/mesh.cpp`
- `src/asset_system/include/atlantis/asset_system/static_mesh_asset_data.h`, `errors.h`, `mesh_artifact.h`; `src/asset_system/src/static_mesh_asset_data.cpp`, `load.cpp`, `mesh_artifact.cpp` (append the peek only)
- `src/runtime/src/scene_load.cpp` (the one `createMesh()` call)
- `tests/render_graph/fake_command_list.h` (`FakeBuffer` only)
- `tests/rhi/types_tests.cpp`, `tests/asset_system/load_tests.cpp`, a `tests/renderer/` test
- `tests/image_regression/CMakeLists.txt`, `tests/image_regression/golden_generator/CMakeLists.txt`
- `tests/tools/gltf_importer/bistro_end_to_end_tests.cpp` (header comment only)

**Explicitly not touched**
- **The `.amesh` format.** No schema version added; `encodeMeshArtifact`,
  `decodeMeshArtifact`, `encodeMeshArtifactU32`, `decodeMeshArtifactU32` and
  `encodeMeshArtifactU32FromIndices` keep their exact bodies. Every existing
  cooked `.amesh` stays byte-identical, which the determinism tests prove.
- **The `uint16_t` GPU path's bytes.** `mesh.cpp`'s existing overload, the
  Index `BufferCreateParams` it builds, and the `VK_INDEX_TYPE_UINT16` it now
  resolves to via the default are unchanged in effect.
- **RenderGraph** — no pass, resource or execution change; only the `FakeBuffer`
  test double in its test directory moves.
- **Platform**, **World**, and **Runtime internals** — `scene_load.cpp`'s single
  `createMesh()` call is the whole Runtime change;
  `runtime_application.cpp:824-835`, the frame loop, lifecycle, manifests and
  `scene_extraction.cpp` are untouched.
- **`rhi::CommandList`** — no signature change, so both implementations and all
  13 bind call sites are untouched.
- **The asset cooker**, the `.mesh.txt` grammar, the `.amesh.meta.txt` schema,
  the scene dependency manifest (ruling O5), and the glTF importer's own code
  (only a test comment in its test directory changes).
- **`VkDeviceCreateInfo`** (`vulkan_device.cpp:1836-1842`) — `fullDrawIndexUint32`
  stays disabled, ruling O1.
- **ADR-0042's tolerances**, the golden comparison algorithm, and the sidecar
  contract.

## Sequencing & Dependencies

1 → 2 → 3, strictly. Milestone 2's `StaticMeshAssetData` has no RHI dependency
and could in principle precede 1, but Milestone 3 needs both, and running 1
first means the "nothing behaves differently yet" risk gate is available at the
cleanest point. Within Milestone 3: the overload and `scene_load` branch (3.1-3.3)
come before any GPU test; the equivalence test (3.4) before the large-mesh
golden (3.5-3.6), because it is the cheaper failure; the golden's two-commit
capture (3.6) after 3.5 is committed clean; the `content` test (3.7) and the
comment update (3.8) last; the dual-platform regression (3.9) closes the Plan.

External dependency: none. Spec 0037 has merged, so real v5 artifacts exist for
3.7, but every other test generates its own data.

## Verification Checklist

- [ ] **Unit tests (GPU-independent):** `IndexType` default and `operator==`
      (Spec T1/`tests/rhi/`); v4 and v5 both load with the right
      `indexType()`/accessor and the v4 values are unchanged; the
      schema-version peek's error cases; the over-range v5 header's named
      rejection (ruling O1); both wrong-width accessors abort via
      `setFailureHandler()`; the `uint32_t` `createMesh()` overload's buffer
      request (Spec T1).
- [ ] **Headless integration tests:** the existing `tests/runtime/scene_load_tests.cpp`
      suite stays green with the branched composition root.
- [ ] **Image regression tests:** v4-vs-v5 equivalence at
      `maxChannelDiff == 0` / `outOfToleranceCount == 0` (Spec T2); the
      66,564-vertex grid golden plus the high-index-band assertion (Spec T3);
      **every existing golden green** (Spec T5, the "u16 unchanged" proof).
- [ ] **Vulkan Validation Layers clean:** both new GPU tests and the full
      existing `gpu`-labelled set, Debug and Release (Spec T4).
- [ ] **Golden provenance:** the new golden added in its own commit with a
      complete sidecar, and the PR carries ADR-0042's four "Initial baseline
      bootstrap" evidence items.
- [ ] **Byte-identity:** asset-cooker determinism and re-cook byte-identity
      tests green — no `.amesh` byte moved.
- [ ] **Content-labelled:** real `bistro_mesh_246_0` renders non-degenerately;
      the test `SKIP()`s cleanly without the content.
- [ ] **Other:** Windows Debug + Release full builds and full `ctest` in both
      configurations; Android `assembleDebug` green; Plan 0037's Ruling 5
      disclosure comment updated; `/W4 /WX` clean including `/w14062` on the
      new switch.

## Open points (for Joint Human Review)

- **Q1 — should the glTF importer also reject at import time?** This Plan puts
  the O1 gate only in the loader (P2). An importer-side check would fail
  earlier and with a better message, at the cost of a second place holding the
  same constant. Recommendation: no, the loader is sufficient and is the layer
  every producer passes. Ruling O1 permits either.
- **Q2 — the 66,564-vertex grid's high-index band is ~4 of 258 rows.** The
  figure is the human's and is the honest minimum above the ceiling, but it
  makes the targeted assertion thin at 512×512. Recommendation: keep 66,564 and
  rely on P6's colour banding plus the golden; if implementation measures the
  band at fewer than ~4 pixels tall, the fallback is a larger grid (362×362 puts
  half the rows above the ceiling) — flagged rather than pre-decided.
- **Q3 — naming.** `MeshIndexType`/`IndexType` as two parallel enums,
  `indices32()`, `peekMeshArtifactSchemaVersion()`,
  `kMaxDrawableVertexCount`, `IndexValueExceedsDrawableRange` are working
  names; ADR-0086/0087 fix the shapes, not the spellings.
- **Q4 — drive-by observed, not fixed:** `mesh_tangent_generation.h:38` says
  "Lengfel-body" where ADR-0073's method is Lengyel's. Out of this Plan's
  scope; noted so it is not mistaken for a term of art.

## Rollback Plan

Each milestone is one revertable commit, and the order is chosen so that
reverting later ones leaves earlier ones harmless: reverting Milestone 3 leaves
an unused `uint32_t` `createMesh()` overload and an unused `indices32()`;
reverting Milestone 2 leaves an unused `IndexType`; reverting Milestone 1
restores the exact pre-Plan state. Nothing here changes persisted data — no
artifact format, no cooked bytes, no manifest — so a revert needs no re-cook
and no content migration. The new golden is deleted with its own commit. The
only externally visible consequence of a full revert is that v5 artifacts stop
being drawable, which is the status quo Spec 0036 workflow ①b exists to change.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan:

- The PR discloses the `rhi::Buffer` interface change (a new pure virtual) in
  its own line, per ruling O3.
- The PR carries ADR-0042's "Initial baseline bootstrap" category and all four
  of its evidence items for the new golden, in the two-commit order.
- The PR states the Android Validation-Layer disclosure inherited from Plan
  0034/0035, per ruling O4.
- The PR records the recount corrections in this Plan's re-read item 7 against
  Spec 0039's own table, so the Spec's numbers are not silently contradicted.
