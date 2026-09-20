# ADR 0087: Asset System CPU-Side Index-Width Representation and Mesh-Artifact Version Dispatch

- **Status:** Proposed
- **Date:** 2026-09-20
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0039](../specs/0039-rhi-index-type-parameterization.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

The Asset System's CPU-side mesh type is 16-bit at every point:
`StaticMeshAssetData`'s constructor, its `indices()` accessor and its member
are all `std::vector<std::uint16_t>`
(`src/asset_system/include/atlantis/asset_system/static_mesh_asset_data.h:18`,
`:22`, `:34`). The single public loader,
`loadStaticMeshAsset(artifactPath, metadataPath)`
(`asset_system/load.h:19-20`), calls `decodeMeshArtifact()`
(`src/asset_system/src/load.cpp:50`), which rejects every `.amesh` schema
version but 4 (`src/asset_system/src/mesh_artifact.cpp:114-115`). A schema-5
artifact therefore fails today at `load.cpp:51` with `ArtifactDecodeFailed`.

The format layer is already ready. Plan 0037 D2 added schema version 5 — the
same 40-byte header and 60-byte vertex layout as version 4, with `uint32_t`
indices — as deliberately **separate** encode/decode siblings rather than a
unified reader, so that `encodeMeshArtifact()`/`decodeMeshArtifact()` stayed
byte-for-byte unchanged (`mesh_artifact.h:67-95`,
`mesh_artifact.cpp:293-294`). The glTF importer is the only producer, and 551
such artifacts exist.

Two constraints shape what can be done about it:

- **Asset System may not name an RHI type.** ADR-0043's Decision states it
  "never constructs an `atlantis::rhi::Buffer`, and never names an
  `atlantis::rhi::VertexInputLayout`", with Core-only dependencies, and
  `tests/asset_system/module_boundary_tests.cpp:22-26` enforces the ban on
  `atlantis/rhi/` includes by enumerating every file under `src/asset_system/`
  at test-run time. So the index width cannot simply be `rhi::IndexType`,
  however convenient that would be at the handoff.
- **The caller cannot know the schema version.** Runtime's scene-load path
  resolves each mesh dependency through a manifest of paths and AssetIds
  (`src/runtime/src/scene_load.cpp:75-83`) and then loads it
  (`scene_load.cpp:102-105`). The manifest records no schema version, and
  neither does the logical path. Only the artifact's own header knows.

There are also 14 `loadStaticMeshAsset()` call sites and one composition-root
line (`scene_load.cpp:111-114`) that reads `.indices().data()` and
`.indices().size()`, so the shape of the answer decides how much unrelated
code moves.

## Decision

**The Asset System keeps its own index-width vocabulary, exposes both widths
as separately typed, precondition-guarded accessors, and dispatches on the
artifact's own `schema_version` inside the single existing loader.**

1. **`enum class MeshIndexType { Uint16, Uint32 };`** is declared by the Asset
   System in `static_mesh_asset_data.h`. It is deliberately a parallel type to
   `rhi::IndexType` ([ADR-0086](0086-rhi-index-type-public-expression-and-backward-compatibility.md)),
   not a shared one; the composition root translates between them, exactly as
   it already translates `StaticMeshAssetData` into `createMesh()` arguments.
2. **`StaticMeshAssetData` gains `indexType()` and `indices32()`**, keeping
   `indices()` at its exact existing type and signature. Each typed accessor
   carries `ATLANTIS_CHECK` on `indexType()` — always evaluated in Debug and
   Release (`src/core/include/atlantis/assert.h:35-37`) — so reading the wrong
   width aborts with a message instead of returning an empty vector. This
   follows the type's own existing precedent: `vertexCount()` already treats a
   zero stride as a programmer error rather than a recoverable one
   (`static_mesh_asset_data.h:25-29`). Exactly one of the two index vectors is
   ever non-empty.
3. **`loadStaticMeshAsset()` keeps its signature and handles both versions.**
   It reads the artifact's `schema_version` and dispatches: version 4 to
   `decodeMeshArtifact()`, version 5 to `decodeMeshArtifactU32()`. Any other
   value is `ArtifactDecodeFailed`, exactly as today. All existing
   artifact/metadata cross-checks (asset id, vertex count, index count, vertex
   stride, and the sidecar's internal self-consistency, `load.cpp:59-79`) apply
   identically to both.
4. **Neither decoder changes.** The dispatch is performed by a new, small
   public helper, `peekMeshArtifactSchemaVersion(bytes)`, declared next to the
   two decoders in `mesh_artifact.h` and returning the same
   `ArtifactDecodeError` type for a buffer too small to hold a header or
   carrying a bad magic. Plan 0037 D2's "separate code paths, not one unified
   reader" holds: the two readers stay independent, and the choice between them
   is made above both.
5. **No path converts between widths.** A version-4 artifact yields
   `Uint16` data and a version-4-sized index buffer downstream; a version-5
   artifact yields `Uint32`. The Asset System never widens, narrows, or
   re-encodes indices.

## Consequences

### Positive

- **The module boundary holds without a new dependency.** ADR-0043's Core-only
  shape is preserved, and the automated boundary test needs no exception.
- **Existing callers do not move.** All 14 `loadStaticMeshAsset()` call sites
  and every `.indices()` use compile and behave unchanged; only the one
  composition-root line that must now choose a width
  (`scene_load.cpp:111-114`) changes. The Spec's "do not churn call sites this
  work does not need to touch" goal is met.
- **A v5 asset reaching v4-only code fails loudly.** The dangerous outcome — a
  caller that ignores `indexType()`, reads `indices()`, gets an empty vector,
  and silently draws nothing or draws garbage — is converted into an abort with
  a message, in Release as well as Debug.
- **The version question is answered once, where the answer lives.** The
  artifact header is the only thing that knows the schema version, and the
  loader is the only layer that sees the header and serves every caller. No
  manifest field, filename convention, or caller-supplied hint becomes a second
  source of truth that could disagree.
- **Memory cost is exactly what the content requires.** The 548 Bistro meshes
  under the 16-bit ceiling — and every existing cooked asset — keep 2-byte
  indices.

### Negative / Trade-offs

- **Two parallel enums for one concept.** `MeshIndexType` and `rhi::IndexType`
  will always have the same two values, and a translation must be written and
  maintained at each composition root. This is the price of ADR-0043's
  boundary, not a benefit of it.
- **A partial accessor pair.** `indices()` and `indices32()` are each valid
  only for one `indexType()`. The type is now correct-by-assertion rather than
  correct-by-construction — a `std::variant` or a byte span would be stronger,
  at the cost of every existing caller.
- **`StaticMeshAssetData` carries an always-empty vector.** One of the two
  index containers is unused per instance. The waste is a vector header, not
  data.
- **`loadStaticMeshAsset()` is no longer a thin pass-through.** It now contains
  a version decision, which is a small amount of policy in a function that
  previously had none.
- **A new public helper.** `peekMeshArtifactSchemaVersion()` widens the
  artifact module's public surface by one function, and it partially duplicates
  knowledge (magic and header size) that the decoders also hold — mitigated by
  its reusing the existing public `kMeshArtifactHeaderSizeBytes`
  (`mesh_artifact.h:52`).

## Alternatives Considered

- **Replace `indices()` with a raw index-byte span plus a width.** The most
  honest shape — it makes the width impossible to ignore rather than merely
  fatal to ignore. Rejected because it breaks all 14 `loadStaticMeshAsset()`
  consumers and every test that asserts specific `uint16_t` index values
  (`tests/asset_system/textured_quad_mesh_tests.cpp`) for no correctness gain
  over the guarded accessors. This remains the natural successor if a future
  round is already touching those call sites.
- **Always store `uint32_t`, widening v4 on load.** Simple and uniform, and
  the accessor stays single. Rejected: it doubles index memory for every
  existing asset, changes the bytes the existing path uploads to the GPU — so
  "the 16-bit path is unchanged" would stop being true and the
  every-golden-green regression proof would lose its meaning — and it breaks
  the tests that assert 16-bit index values.
- **A second entry point, `loadStaticMeshAssetU32()`, mirroring the separate
  encode/decode siblings.** Rejected: the caller cannot know which to call. The
  scene dependency manifest records paths and AssetIds, never a schema version,
  so every composition root would have to re-implement the header sniff that
  the loader can do once. Plan 0037 D2's separation is about keeping the two
  *readers* independent, which this decision preserves; it was never about
  pushing the version question onto callers.
- **Try `decodeMeshArtifact()` first and fall back to
  `decodeMeshArtifactU32()` on `UnknownSchemaVersion`.** Rejected: it makes an
  expected outcome flow through an error path, it re-parses the header twice,
  and a future third version would make the fallback chain the place where
  version policy accidentally lives.
- **Record the index width in the `.amesh.meta.txt` sidecar or in the scene
  dependency manifest.** Rejected: both would create a second source of truth
  for a fact the artifact header already states authoritatively, and each would
  need its own mismatch error and its own test. The sidecar's existing role is
  to be cross-checked against the artifact, not to describe its encoding.
- **Give the Asset System no index-width concept at all and have the
  composition root sniff the artifact itself.** Rejected outright: it would
  push format knowledge into Runtime and into every test fixture, inverting
  ADR-0043's whole point.
