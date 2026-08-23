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
   happens to need." **Which of the two a given texture uses is a
   mandatory, explicit `colorSpace` cooker parameter — never inferred
   from the source PNG.** `stb_image` applies no gamma/color-profile
   interpretation of its own; the cooker reads no `gAMA`/`sRGB`/`iCCP`/
   `cHRM` chunk (matching ADR-0041's own already-established "no
   color-profile interpretation on either side" discipline, extended
   here from goldens to authored textures). Sampling behavior differs
   by format at the hardware level, stated explicitly: an `Rgba8Srgb`
   `SampledTexture` is linearized by Vulkan's own fixed-function texture
   unit (the sRGB EOTF) before a fragment shader ever sees a sampled
   value; `Rgba8Unorm` is not. **This is real GPU hardware behavior, not
   a form of tonemapping, and cannot be proven by a CPU-only artifact
   round-trip test** — a second review round corrected an earlier
   version of this Decision that tried to exercise `Rgba8Srgb` with only
   a GPU-independent cook/decode unit test (which proves the artifact's
   own format tag and bytes round-trip correctly, never that Vulkan's
   own hardware sampling actually linearizes). Spec 0016's own one
   GPU-required fixture instead cooks the *same* source image twice —
   once per format — and samples both in the same golden (two quads, two
   `Material`s, one shared `Sampler`), so that any visible difference
   between them in the captured, human-confirmed golden is exactly
   Vulkan's own real hardware sRGB decode, not an artifact of cooking,
   uploading, or comparison methodology; the GPU-independent round-trip
   test remains valuable only as evidence the artifact layer itself is
   correct, never as a substitute for the GPU evidence above. Golden
   comparison in either case
   happens on the final, rendered RGBA8 color-attachment bytes — never a
   direct comparison against the source PNG or the artifact's own stored
   texel values.
2. **A new texture cooker**, `cookTexture(sourceImagePath,
   logicalPathInput, colorSpace, artifactOutputPath, metadataOutputPath)
   -> Result<monostate, TextureCookError>` (exact name a Plan-level
   detail), following `cookStaticMesh()`/`cookScene()`'s established
   pattern exactly: normalize logical path → decode the authoring image
   via `stb_image` with `desired_channels = 4` forced (matching
   ADR-0041's own established `stb_image` convention exactly; unlike a
   golden, an authored texture may legitimately be a real RGB/grayscale
   source, so a `channels_in_file != 4` source is **not** itself a cook
   error — the source's own real `channels_in_file` is instead recorded
   in the metadata sidecar for provenance) → validate against a
   defensive maximum dimension (Plan-level detail, e.g. 8192×8192,
   chosen so `maxDimension × maxDimension × 4` stays comfortably within
   a `uint32_t` pixel-data-size header field), checked **before**
   `width × height × 4` is computed for any allocation, that computation
   itself performed in 64-bit arithmetic first so a corrupted/adversarial
   value cannot wrap a 32-bit multiplication into a small, falsely-valid
   size → `computeAssetId()` → encode a magic-prefixed, versioned,
   little-endian artifact (header: schema version, width, height,
   `SampledTextureFormat`, mip count [fixed at 1, checked equal to 1 at
   decode time], pixel-data offset/size; body: tightly-packed row-major
   RGBA8 bytes — `width × 4` bytes per row, no padding, matching
   `VkBufferImageCopy::bufferRowLength = 0`'s own existing convention,
   ADR-0040 — first row matching the authoring image's own first-decoded
   row, matching ADR-0041's own "no vertical flip, ever" convention)
   plus a text metadata sidecar → atomic dual-file write, reusing
   `writeBytesAtomically()`/`writeTextAtomically()` unchanged. An
   unreadable/malformed source image is a distinct `TextureCookError`
   enumerator (e.g. `SourceImageDecodeFailed`); a corrupted/truncated
   artifact at decode time (bad magic, unknown schema version, an
   inconsistent pixel-data size, a dimension exceeding the maximum, a
   mip count other than 1, truncated pixel data) is each a distinct
   `TextureArtifactDecodeError`/`TextureLoadError` enumerator, mirroring
   `mesh_artifact.h`'s own already-shipped defense-in-depth decode
   discipline exactly. Exposed via a new mode of the existing
   `atlantis_asset_cooker` Tools executable, dispatched by `AssetKind`
   exactly as the mesh/scene modes already are (`cook_command.cpp:208-217`).
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
   `atlantis_asset_cooker_lib` (Tools) only — never into
   `Atlantis::AssetSystem`'s own runtime library, `src/renderer`,
   `src/runtime`, or any other runtime-linked target. This widens
   ADR-0041's own explicit "never in `src/`, never linked into any
   shipping example or engine library" boundary statement. **ADR-0041
   now carries its own "Proposed Amendment — 2026-08-23" section**
   recording this widening, its own per-target single-implementation-TU
   rule, and a real, previously-undiscovered CMake configure-ordering
   defect this review round found (`cmake/AtlantisDependencies.cmake`,
   where `stb`'s `FetchContent` declaration lives, is included only
   inside `if(ATLANTIS_BUILD_TESTS)`, **after**
   `add_subdirectory(src/tools/asset_cooker)` already runs at
   `CMakeLists.txt:60` — `Stb::Stb` would not exist as a target at the
   point the cooker needs it, regardless of `ATLANTIS_BUILD_TESTS`'s
   value, without also relocating that declaration into a new,
   unconditionally-included module) — ADR-0041's own original `Accepted`
   Decision/Consequences/Alternatives Considered are left completely
   unmodified by that amendment. This ADR does not stand approved on its
   own without that amendment also being accepted, by the same Human
   Review pass; see Spec 0016's own Human Review Decision item 10.
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
  a companion Proposed Amendment to an already-`Accepted` ADR (ADR-0041),
  adding a real, disclosed cross-document approval dependency Human
  Review must handle explicitly, not incidentally.
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
- The explicit, mandatory `colorSpace` cooker parameter puts the burden
  of choosing correctly on the human author (or a Plan-level authoring
  tool default) — this ADR introduces no automatic detection, so an
  author who passes the wrong `colorSpace` for a given source image gets
  a cooked artifact that decodes and uploads successfully but samples
  with the wrong linearization; no validation catches this, since it is
  an authoring-intent question, not a data-integrity one.

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
- **Infer `colorSpace` from the source PNG's own `gAMA`/`sRGB` chunk**,
  rather than requiring an explicit cooker parameter. Rejected — `stb_image`
  does not read these chunks in this codebase's existing usage, and
  adding that interpretation would be new, undisclosed decoder behavior
  beyond ADR-0041's own established "raw bytes only, no color-profile
  interpretation" contract; an explicit, author-supplied parameter is
  also simply more predictable than trusting arbitrary third-party
  authoring-tool metadata.
- **Compute `width × height × 4` in 32-bit arithmetic before the
  maximum-dimension check**, matching a naive reading of "check the
  dimensions, then compute the size." Rejected — a crafted or corrupted
  header could wrap that multiplication into a small, falsely-valid
  size before the check ever runs, defeating the check it's meant to
  gate; 64-bit arithmetic first closes this.
- **(Withdrawn, second review round.)** Exercise `Rgba8Srgb` with only a
  GPU-independent cook/decode round-trip unit test, using `Rgba8Unorm`
  for the one GPU-required fixture. This was an earlier version of this
  ADR's own Decision 1 and is withdrawn: it proves the artifact format
  tag and bytes round-trip correctly, never that Vulkan's own hardware
  sampling actually linearizes on sample, which is the entire point of
  claiming `Rgba8Srgb` support — see this ADR's own Decision 1 for the
  corrected, dual-format-in-one-golden design.
