# ADR 0057: Texture Asset Format, Decoder Dependency, and Color Space Contract

- **Status:** Proposed
- **Date:** 2026-08-23
- **Deciders:** Pending Human Review (as part of Spec 0016)
- **Related Spec:** [specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md)

## Context

`Atlantis::AssetSystem` has an established, twice-proven cook/artifact/
metadata/loader convention (static mesh, Spec 0012; scene graph, Spec
0015): a magic-prefixed, versioned, unconditionally little-endian binary
artifact (`mesh_artifact.h`/`.cpp`, explicit shift/mask serialization,
never a struct memcpy), a separate, versioned, strict flat-text metadata
sidecar cross-validated against the artifact, atomic dual-file writes
(temp-file-then-rename, `cook.cpp:45-78`), and a loader that
independently re-validates every cook-time condition against the
artifact's own actual bytes rather than trusting a well-formed cooker
output (`load.cpp:40-83`).

No texture/image asset kind exists in this pattern today. The existing
swapchain/offscreen-shaped `Format` enum (`types.h:26-32`) is explicitly
not a general resource format — its own doc comment states: *"still not
a general resource-format system ... A future Buffer/Texture spec is
expected to introduce its own general format concept, quite possibly
superseding this enum's role rather than extending it in place"*
(`types.h:22-25`).

`stb_image`/`stb_image_write` are an existing, `Accepted` (ADR-0041),
pinned-commit `FetchContent` dependency (`Stb::Stb`), but linked
`PRIVATE` into exactly two `tests/image_regression/` targets only
(`tests/image_regression/support/CMakeLists.txt:12-18`,
`tests/image_regression/CMakeLists.txt:18-29`). `src/tools/asset_cooker`
links no image-decoding library today
(`src/tools/asset_cooker/CMakeLists.txt:21-27`). ADR-0041 states this
scope explicitly, not incidentally: *"the `STB_IMAGE_IMPLEMENTATION`/
`STB_IMAGE_WRITE_IMPLEMENTATION` translation unit and every call site
above exist only inside `tests/image_regression/`'s own test-support
targets (ADR-0042) — never in `src/`, never linked into any shipping
example or the engine's own libraries"* (`adr/0041-...md:156-160`).
ADR-0041's own licensing note (`adr/0041-...md:161-168`) already
confirms `stb`'s dual MIT/Unlicense-equivalent public-domain license as
permissive, requiring no separate `NOTICE`/`THIRD_PARTY` file entry; its
offline-build caveat (`adr/0041-...md:169-176`) already discloses that
`FetchContent` needs network access on first configure.

## Decision

1. **A new, independent `SampledTextureFormat` enum** — decoupled from
   the existing swapchain-shaped `Format` enum (whose own BGRA variants
   are meaningful only for a platform swapchain, not an authored
   texture). First two supported values: `Rgba8Unorm` (linear) and
   `Rgba8Srgb`, giving an explicit linear-vs-sRGB color-space contract
   from the outset — not deferred to "whichever the first real texture
   happens to need."
2. **A new texture cooker**, `cookTexture(sourceImagePath,
   logicalPathInput, colorSpace, artifactOutputPath, metadataOutputPath)
   -> Result<monostate, TextureCookError>` (exact name a Plan-level
   detail), following `cookStaticMesh()`/`cookScene()`'s established
   pattern exactly: normalize logical path → decode the authoring image
   via `stb_image` → validate (non-zero dimensions, within a defensive
   maximum, decoded channel data consistent with the declared
   `SampledTextureFormat`) → `computeAssetId()` → encode a magic-prefixed,
   versioned, little-endian artifact (header: schema version, width,
   height, `SampledTextureFormat`, mip count [fixed at 1], pixel-data
   offset/size; body: tightly-packed row-major RGBA8 bytes, first row
   matching the authoring image's own first-decoded row) plus a text
   metadata sidecar → atomic dual-file write, reusing
   `writeBytesAtomically()`/`writeTextAtomically()` unchanged. Exposed
   via a new mode of the existing `atlantis_asset_cooker` Tools
   executable, dispatched by `AssetKind` exactly as the mesh/scene modes
   already are (`cook_command.cpp:208-217`).
3. **A new texture loader**, `loadTextureAsset(artifactPath,
   metadataPath) -> Result<TextureAssetData, TextureLoadError>` (exact
   name a Plan-level detail), mirroring `loadStaticMeshAsset()`'s own
   cross-validation discipline: independently re-checks magic, schema
   version, and that the declared pixel-data size exactly equals
   `width × height × 4`; cross-checks the metadata sidecar against the
   artifact. Returns pure CPU-side `TextureAssetData` (width, height,
   `SampledTextureFormat`, owned pixel bytes) naming no RHI type — a
   composition root elsewhere passes the result into
   `Device::createSampledTexture()` plus
   [ADR-0056](0056-texture-upload-resource-state-and-descriptor-binding.md)'s
   own upload path, exactly as `loadStaticMeshAsset()`'s result already
   passes into `renderer::createMesh()`.
4. **`stb_image` is promoted from test-only to Tools use, disclosed
   explicitly.** `Stb::Stb` is additionally linked `PRIVATE` into
   `atlantis_asset_cooker` (Tools) only — never into
   `Atlantis::AssetSystem`'s own runtime library, `src/renderer`,
   `src/runtime`, or any other runtime-linked target. This widens
   ADR-0041's own explicit "never in `src/`, never linked into any
   shipping example or engine library" boundary statement and therefore
   requires ADR-0041's own future Human Review Amendment — **not made by
   this ADR**; ADR-0041's `Accepted` body remains untouched here. This
   ADR does not stand approved on its own without that companion
   amendment; see Spec 0016's own Human Review Decision item 10.
5. **Runtime never decodes an authoring image format.**
   `loadTextureAsset()` reads only the cooked, already-decoded
   pixel-byte artifact. `stb_image`'s own linkage never reaches
   `Atlantis::AssetSystem`'s runtime-loading translation units or any
   runtime-linked target — enforced by the same module-boundary
   include-scanning discipline every prior Spec's own tests already use,
   extended to check for `stb_image.h`/`stb_image_write.h` specifically.

## Consequences

### Positive

- Reuses an already-license-reviewed, already-vetted, small dependency
  rather than writing or vendoring a redundant PNG/JPEG decoder.
- The texture asset kind follows an already-twice-proven convention
  exactly, minimizing new format-design risk — the mesh and scene
  pipelines' own established defense-in-depth decode-time re-validation
  discipline transfers directly.
- The linear/sRGB contract is explicit and named from the first texture
  onward, rather than an implicit assumption a much later spec would
  have to retrofit.
- The `stb_image` boundary widening is narrow (Tools/cooker only) and
  fully disclosed, not a blanket "make it available everywhere" change.

### Negative / Trade-offs

- This ADR is not independently approvable — its own decision 4 requires
  a companion amendment to an already-`Accepted` ADR (ADR-0041), adding
  a real, disclosed cross-document approval dependency Human Review must
  handle explicitly, not incidentally.
- A defensive maximum texture dimension is a somewhat arbitrary,
  Plan-level choice (not derived from any queried real device limit) —
  acceptable for this Spec's own small-test-texture scope, but not a
  substitute for a future spec that needs to reason about real
  `VkPhysicalDeviceLimits::maxImageDimension2D`.
- `stb_image`'s own decode behavior (e.g., its specific handling of
  malformed/adversarial input images) becomes, for the first time, part
  of a build-time Tools executable's own attack surface, not only a
  test-support target's — mitigated by the fact that authoring images are
  developer-supplied, trusted input, not runtime/network-supplied data,
  matching every other cooker's own existing trust model for its own
  authoring source.

## Alternatives Considered

- **Reuse the existing swapchain-shaped `Format` enum for sampled-texture
  color space.** Rejected — see Spec 0016's own Human Review Decision
  item 3; `Format`'s own BGRA variants are meaningless for an authored
  texture, and its own doc comment already anticipates a distinct future
  format concept rather than in-place extension.
- **Write a hand-rolled PNG decoder instead of reusing `stb_image`.**
  Rejected — duplicates a small, already-permissively-licensed,
  already-in-repo library for no real benefit; this repository's own
  established restraint targets *new* general-parser dependencies, not
  reuse of an already-`Accepted` one.
- **Ship raw, undecoded authoring-image bytes as the "artifact," decoding
  at Runtime load time instead.** Rejected outright — directly
  contradicts this repository's own authoring/runtime data-separation
  principle ([ADR-0035](0035-authoring-runtime-data-separation-as-a-long-term-principle.md))
  and Spec 0016's own explicit "Runtime never parses PNG/JPEG"
  requirement.
- **Vendor a private copy of `stb_image` scoped only to Tools, rather
  than widening the existing `Stb::Stb` target's own linkage.** Rejected
  as needless duplication — one pinned-commit, one license review, one
  `FetchContent` declaration remains simpler to reason about than two
  independent copies of the same header-only library.
