# ADR 0041: Image Regression Testing — Golden Image Data Format and Codec Dependency

- **Status:** Accepted
- **Date:** 2026-08-16
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-16; see
  [specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md)'s
  Human Review Approval note for the full, two-round independent-review
  record this ADR's Decision (including the corrected commit-pin wording
  and the full `stb` usage contract) is part of.
- **Related Spec:** [specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md)

## Context

- Spec 0010 (`Approved`, implemented via [PR #48](https://github.com/slmao/Atlantis/pull/48))
  gives Atlantis a working headless render-and-readback pipeline that
  produces a tightly-packed 8-bit-per-channel RGBA pixel buffer
  (`atlantis::rhi::Format::Rgba8Unorm` in the shipped
  `examples/headless_rendering_demo`; the RHI `Format` enum also defines
  `Bgra8Unorm`/`Bgra8Srgb`/`Rgba8Srgb`, all 8-bit-per-channel) in
  host-visible memory, readable via `Buffer::mappedData()`.
- Image Regression Testing (Spec 0011) needs to persist a
  human-reviewed reference ("golden") copy of that buffer in the git
  repository, and reload it later for comparison against a freshly
  captured buffer.
- [AGENTS.md](../AGENTS.md)'s Golden Rule requires any new dependency to
  go through Spec → ADR → Human Review, and explicitly forbids silently
  introducing a new library ("Anything that adds a new dependency
  (library, tool, SDK)" is significant).
- At the existing demo's fixed 512×512 resolution, one uncompressed
  captured buffer is 1,048,576 bytes (512 × 512 × 4). A Phase 1 image
  regression suite is expected to accumulate more than one golden image
  over time as more scenes/passes gain coverage, so storing goldens
  uncompressed in git has a real, compounding cost.
- No image codec of any kind exists anywhere in this repository today —
  confirmed by searching this repository for `png`/`stb_image`/`libpng`
  outside this ADR's and Spec 0011's own drafts.
- [ADR-0006](0006-dependency-management.md) (`Accepted`) established a
  two-tier dependency model: small, source-buildable dependencies via
  CMake `FetchContent`, each "pinned to a specific tagged release
  (`GIT_TAG` set to a release tag or commit hash, never a floating
  branch)" (ADR-0006's own Decision text — a commit hash is an
  explicitly permitted pin, not only a tag), versus large platform SDKs
  installed externally (Vulkan SDK, Android NDK/SDK). It explicitly
  reserves "future dependencies that don't fit the small pinned source
  dependency via `FetchContent` model" for their own ADR — an image
  codec is exactly such a new category, not decided by ADR-0006 itself.
- **The `nothings/stb` GitHub repository has no git tags and no GitHub
  Releases** — verified directly against the repository (`git ls-remote
  --tags`/the GitHub Releases API both return empty as of this ADR's
  drafting). Unlike Catch2 (ADR-0007), which is pinned to an actual
  tagged release, `stb` can only be pinned to a specific commit hash on
  its `master` branch. This is a real difference from ADR-0006/ADR-0007's
  own precedent, not an oversight — see Decision below.

## Decision

- Golden images are stored as **PNG**, 8-bit-per-channel RGBA,
  losslessly encoding exactly the bytes an `OffscreenTarget` readback
  buffer already produces for whichever `atlantis::rhi::Format` the
  capturing scene used — no color-space conversion, no re-quantization,
  no lossy compression.
- PNG encode/decode is implemented using **`stb_image_write.h`** (encode)
  and **`stb_image.h`** (decode) from the `nothings/stb` project
  (public-domain / MIT dual-licensed, single-header, no transitive
  dependencies), fetched via CMake `FetchContent` **pinned to a
  specific, full 40-character commit hash on `stb`'s `master` branch —
  never a tag (none exist), never a floating branch reference.** This is
  the same `FetchContent` mechanism ADR-0006/ADR-0007 already established
  for Catch2, differing only in what `GIT_TAG` is set to (a commit hash
  here, a release tag for Catch2) — a difference ADR-0006's own Decision
  text explicitly anticipates and permits. Not vendored into the
  repository, not installed as a system package. The exact commit hash
  pinned is a Plan-stage detail (this ADR fixes the pinning *mechanism*,
  not the specific hash, which should be the tip of `master` at the time
  the Plan is implemented).
- This dependency is linked **only into the test-support targets under
  `tests/image_regression/`** defined by
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md) —
  no `src/` module gains a dependency on it, and no public RHI/
  Renderer/RenderGraph API is touched by this decision. This is a
  testing-infrastructure dependency, not an engine/runtime one.
- Golden PNGs live in the repository as ordinary versioned binary files
  (exact path and update workflow: see ADR-0042) — no external
  binary-asset storage system (Git LFS, a cloud bucket, etc.) is
  introduced by this decision.

### stb usage contract

Verified directly against `stb_image.h` (v2.30, 2024-05-31) and
`stb_image_write.h` (v1.16) at
[github.com/nothings/stb](https://github.com/nothings/stb), not assumed:

- **Implementation macros.** `STB_IMAGE_IMPLEMENTATION` and
  `STB_IMAGE_WRITE_IMPLEMENTATION` are each `#define`d in **exactly one**
  dedicated `.cpp` translation unit within `tests/image_regression/`'s
  own test-support target (exact file name left to the Plan) —
  immediately before the corresponding `#include`. Defining either macro
  in more than one translation unit is an ODR violation (duplicate
  symbols at link time); every other translation unit that needs the
  decode/encode API `#include`s the header **without** the macro
  defined, exactly as both headers' own build instructions require.
- **Decode channel contract, and why forcing 4 channels is not enough
  on its own.** Every `stbi_load`-family call in this project's code
  passes `desired_channels = 4` explicitly. Per `stb_image.h`'s own
  documented contract, a non-zero `desired_channels` forces the decoded
  *output buffer* to that many channels regardless of the source PNG's
  own encoded channel count — this guarantees a golden PNG is always
  read back as a 4-channel (RGBA) buffer with a fixed, known layout, no
  matter how the file was actually encoded. **On its own, this would
  silently mask a golden PNG that was never really RGBA** (e.g. one
  accidentally re-saved as RGB-only or grayscale by an external tool,
  losing its real alpha data) — `stb_image`'s forced-channel expansion
  fills in a synthetic alpha rather than erroring. This project's code
  therefore **also inspects the decoder's own `channels_in_file`
  out-parameter** (`stb_image.h`'s own documented contract: "*channels_in_file
  has the number of components that _would_ have been output" had
  `desired_channels` been 0 — i.e. the file's real, as-encoded channel
  count, independent of the forced-4-channel output buffer) and requires
  it to equal 4, and separately calls `stb_image.h`'s own
  `stbi_is_16_bit`-family query and requires it to report `false` (this
  project's contract is 8 bits per channel only, never 16). Either check
  failing is a **malformed-golden** hard failure — see
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "Golden validity check" for the full, ordered validation this
  feeds into.
- **No vertical flip, ever, on either side.** This project's code never
  calls `stbi_set_flip_vertically_on_load()`,
  `stbi_set_flip_vertically_on_load_thread()`, or
  `stbi_flip_vertically_on_write()`. Both libraries' un-flipped default
  ("first pixel top-left" on decode; first input row is the PNG's top
  row on encode, per each header's own documentation) is used
  unconditionally. This default already matches the row order
  `VulkanCommandList::copyRenderTargetToBuffer()` produces (row 0 of the
  readback buffer is the color image's row 0, i.e. the framebuffer's top
  row under this codebase's standard viewport convention;
  `VkBufferImageCopy::bufferRowLength = 0` — tightly packed, no
  additional transform, per ADR-0040) — no flip call is needed on either
  the write or the read path for the actual/golden byte layouts to
  agree.
- **Never rely on either flip setter's global state, because it is
  never called.** Noted for completeness, not because this project uses
  it: `stbi_set_flip_vertically_on_load_thread()` is thread-local (since
  v2.24), but `stbi_flip_vertically_on_write()` has no thread-local
  variant at all — it mutates a plain, process-wide `static int`. Since
  this project never calls any of the three flip setters (previous
  bullet), this asymmetry has no practical effect here; it is recorded
  so a future contributor does not introduce a flip call without first
  re-reading this constraint.
- **No PNG color-profile metadata is written or interpreted.**
  `stbi_write_png()` writes no `gAMA`/`sRGB`/`iCCP`/`cHRM` chunk of any
  kind (verified against `stb_image_write.h`'s own source — no such
  chunk-writing code exists in it); comparison in ADR-0042 operates on
  raw pixel bytes only, with no color-profile interpretation on either
  side.
- **Scope, restated:** the `STB_IMAGE_IMPLEMENTATION`/
  `STB_IMAGE_WRITE_IMPLEMENTATION` translation unit and every call site
  above exist only inside `tests/image_regression/`'s own test-support
  targets (ADR-0042) — never in `src/`, never linked into any shipping
  example or the engine's own libraries.
- **License and attribution.** Verified directly against the `LICENSE`
  file at the root of `github.com/nothings/stb`: dual-licensed, the
  user's choice of MIT or an Unlicense-equivalent public-domain
  dedication, both permissive and requiring no NOTICE-file propagation
  or separate attribution document. `FetchContent` fetches the header
  files themselves (including their own embedded license text at the
  end of each file), so no additional attribution mechanism is
  introduced or required by this decision.
- **Offline/air-gapped builds.** This dependency inherits
  [ADR-0006](0006-dependency-management.md)'s existing, already-disclosed
  `FetchContent` limitation: the first configure that needs to fetch it
  requires network access; an offline/air-gapped build needs a
  pre-populated `FetchContent` cache or vendored copy, which neither
  ADR-0006 nor this ADR addresses. Restated explicitly here because this
  is a second `FetchContent` dependency stacking on top of Catch2, not
  merely a reference to an already-accepted cost.

## Consequences

### Positive

- Lossless: no additional quantization/compression artifact is
  introduced between a captured buffer and its stored golden — a
  comparison mismatch is always a real content difference, never a
  codec artifact.
- Matches `OffscreenTarget`'s own byte layout directly (RGBA,
  8 bits/channel); no format-conversion code is needed beyond PNG's own
  color-type selection.
- `stb_image`/`stb_image_write` is an extremely small (a few thousand
  lines, single header each), widely-used-in-shipped-engines,
  permissively-licensed dependency with a minimal build footprint (no
  transitive dependencies, no separate build step beyond compiling one
  translation unit) — consistent in spirit and weight with
  ADR-0006/ADR-0007's existing `FetchContent` precedent.
- PNG is a ubiquitous, tool-supported format — any off-the-shelf image
  viewer or diff tool can open a golden, or a failure's actual/diff
  artifact, without any Atlantis-specific tooling.

### Negative / Trade-offs

- A new third-party dependency, however small — a first for this
  category (no codec of any kind existed before this decision); expands
  what `FetchContent`/a future CI job must fetch and build.
- PNG compression costs CPU time on every capture — negligible at
  512×512, not evaluated at larger resolutions since none are in this
  spec's scope.
- Binary golden files still bloat git history on update (PNG
  compression reduces, but does not eliminate, this — a changed golden
  is still a new binary blob per commit); accepted as a known, monitored
  cost, not solved by this ADR.
- `stb_image`'s decoder is not a validating/hardened parser against
  adversarial input; acceptable here because golden and actual images
  are exclusively produced by this project's own pipeline or committed
  via reviewed PRs, never loaded from an untrusted source.

## Alternatives Considered

- **Raw/PPM, no codec dependency.** Rejected as the primary format:
  avoids a new dependency entirely, but PPM is uncompressed (same
  storage cost as the raw buffer, just a different header) and does not
  solve the repository-bloat concern; also has no viewer/tooling support
  comparable to PNG's. Recorded here as the fallback if Human Review
  rejects the `stb` dependency (see Spec 0011's own Risks & Open
  Questions) — this ADR would then be revised or superseded, not
  silently reworked.
- **libpng (+ zlib).** Rejected: a heavier, autotools/CMake-based C
  library with its own transitive zlib dependency; substantially more
  build-system surface than this narrow "write/read one RGBA8 PNG" use
  case needs, for no capability benefit over `stb` here.
- **JPEG** (via `stb_image`/`stb_image_write`'s own JPEG support, or any
  other codec). Rejected: lossy compression reintroduces exactly the
  "codec artifact vs. real regression" ambiguity a golden-image
  comparison must avoid.
- **An external, invoked-as-a-subprocess tool** (e.g. ImageMagick,
  `pngcrush`, `optipng`). Rejected: adds a non-reproducible,
  environment-dependent external tool dependency (must be separately
  installed, versioned, and discovered) instead of a small, pinned,
  in-tree-buildable one — worse on every axis ADR-0006 already used to
  reject vcpkg/Conan for this project's current scale.
- **A cloud/external binary asset store (Git LFS, S3, etc.) for
  goldens.** Rejected as out of scope for this decision: introduces its
  own operational/access-control/CI-credential surface with no current
  justification at this project's still-small golden-image count;
  revisit only if repository bloat becomes a real, measured problem.

## Proposed Amendment — 2026-08-23

**Status: Proposed, pending the same Human Review as
[Spec 0016](../specs/0016-texture-sampler-foundation.md) and
[ADR-0055](0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)–[ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).
Everything above this section is this ADR's own original, unmodified
`Accepted` Decision — this amendment does not alter, narrow, or
reinterpret any of it; it adds one new, additional linkage point to the
"Scope, restated" bullet above, under the constraints below.**

### Context for this amendment

Spec 0016 (Texture & Sampler Foundation) needs a build-time texture
cooker that decodes an authored PNG into a runtime pixel artifact. This
ADR's own "Scope, restated" bullet above currently states, without
qualification, that `stb_image`/`stb_image_write` usage exists "only
inside `tests/image_regression/`'s own test-support targets ... never
in `src/`, never linked into any shipping example or the engine's own
libraries." A texture cooker inside `src/tools/asset_cooker` is exactly
the kind of linkage that sentence forbids as written — reusing `stb` for
it cannot proceed under a mere forward-looking mention in Spec 0016's
own ADR-0057; this ADR's own stated boundary must be amended directly,
by name, here.

**A second, real, previously undiscovered gap this amendment also
closes:** `cmake/AtlantisDependencies.cmake` — the file whose
`FetchContent_Declare(stb ...)`/`FetchContent_MakeAvailable(stb)` calls
(lines 33–39) are this ADR's own implementation — is included from the
root `CMakeLists.txt` **only** inside the `if(ATLANTIS_BUILD_TESTS)`
block (`CMakeLists.txt:95-97`). `add_subdirectory(src/tools/asset_cooker)`
(`CMakeLists.txt:60`) runs unconditionally, **before** that block, at
configure time. Under today's `CMakeLists.txt` ordering, `Stb::Stb`
therefore does not exist as a target yet at the point
`src/tools/asset_cooker/CMakeLists.txt` would need to
`target_link_libraries(... Stb::Stb)` — regardless of
`ATLANTIS_BUILD_TESTS`'s value. Simply adding a `target_link_libraries`
call to the cooker's own `CMakeLists.txt`, as Spec 0016/ADR-0057
originally described without checking this, would be a hard CMake
configure-time error ("target Stb::Stb not found"), not a working
change. This amendment's own Decision below fixes the ordering, not
only the scope sentence.

### Decision

- **`stb_image`/`stb_image_write` usage is extended from
  image-regression test-only to offline Asset Cooker use, additionally
  and only.** `Stb::Stb` is linked `PRIVATE` into exactly one additional
  target: `atlantis_asset_cooker_lib` (the library backing the
  `atlantis_asset_cooker` Tools executable, `src/tools/asset_cooker/`).
  This ADR's own "Scope, restated" bullet is amended, in effect, to read:
  *"... exist only inside `tests/image_regression/`'s own test-support
  targets and `src/tools/asset_cooker`'s own cooker library — never
  anywhere else in `src/`, never linked into any shipping example, and
  never linked into any of the engine's own runtime libraries."*
- **Never linked into Runtime, `Atlantis::AssetSystem`'s own runtime
  library, `Atlantis::Renderer`, or any GPU-facing module.**
  Specifically and by name: `Atlantis::AssetSystem`'s own
  `atlantis_asset_system` runtime library (the one linked by
  `Atlantis::World`, Runtime, and every runtime-loading call site) does
  **not** gain a `Stb::Stb` dependency — only the separate,
  Tools-only `atlantis_asset_cooker_lib` does, matching the existing,
  already-`Accepted` separation between `atlantis_asset_system` (runtime
  library) and `atlantis_asset_cooker` (offline Tools executable) this
  repository already draws for the mesh and scene cookers. RHI, Vulkan
  Backend, RenderGraph, Renderer, and Shader System gain no dependency
  on `stb` of any kind, directly or transitively — `Stb::Stb` remains an
  `INTERFACE` target with no dependency edge into any of them.
- **`ATLANTIS_BUILD_TESTS=OFF` must not remove the cooker's own access
  to `stb`.** `cmake/AtlantisDependencies.cmake`'s `stb`
  `FetchContent_Declare`/`FetchContent_MakeAvailable`/`Stb::Stb`
  `INTERFACE`-target block (lines 28–47) is moved out of that
  test-only-included file into a new, separate CMake module (exact file
  name a Plan-level detail, e.g. `cmake/AtlantisStb.cmake`), included
  unconditionally and early from the root `CMakeLists.txt` — before
  `add_subdirectory(src/tools/asset_cooker)` (`CMakeLists.txt:60`) and
  independent of the `if(ATLANTIS_BUILD_TESTS)` block. Catch2's own
  `FetchContent` declaration remains exactly where it is today, inside
  `cmake/AtlantisDependencies.cmake`, still gated by
  `ATLANTIS_BUILD_TESTS` — this amendment moves only `stb`'s declaration,
  and does not change Catch2's own test-only scope in any way.
  `tests/image_regression/`'s own two existing `Stb::Stb` consumers are
  unaffected: the target still exists, under the same name, by the time
  `ATLANTIS_BUILD_TESTS=ON` processes their own subdirectories — this is
  a relocation of *where* `Stb::Stb` is declared, not a second,
  duplicate declaration of it (CMake's `include_guard(GLOBAL)`, already
  present in the moved block, continues to prevent a double-inclusion
  problem if both the cooker and a test subdirectory transitively
  `include()` the new module).
- **One implementation-macro translation unit each, unchanged
  principle, one new instance.** The existing rule ("`STB_IMAGE_IMPLEMENTATION`
  and `STB_IMAGE_WRITE_IMPLEMENTATION` are each `#define`d in exactly one
  dedicated `.cpp` translation unit," this ADR's own "stb usage contract"
  above) now applies **per linking target**, not merely once
  repository-wide: `tests/image_regression/support/png_codec.cpp`
  remains the one TU defining both macros for the test-support target;
  the new texture cooker gains its **own**, second, independent TU
  (inside `src/tools/asset_cooker/`) defining `STB_IMAGE_IMPLEMENTATION`
  only (the cooker decodes authoring images; it does not need
  `stb_image_write`'s encode path, which stays test-only). Two separate
  binaries each defining the macro once, in their own TU, is not an ODR
  violation — the existing rule's own "exactly one... within
  `tests/image_regression/`'s own test-support target" wording is
  narrowed to "within its own linking target," which is what the
  original rule already meant in a single-consumer world and is now
  stated explicitly because a second consumer exists.
- **License, offline-build/network, and maintenance boundary: unchanged,
  explicitly re-confirmed, not re-litigated.** This ADR's own existing
  "License and attribution" and "Offline/air-gapped builds" disclosures
  (above) apply identically to this second linkage — same dual MIT/
  Unlicense-equivalent license, same no-NOTICE-file requirement, same
  pinned-commit-hash `FetchContent` mechanism, same first-configure
  network-access requirement (now also gating a configure that never
  builds any test target, since the relocated module is unconditional).
  No new version is pinned by this amendment; the cooker links the
  identical pinned commit hash `tests/image_regression/` already uses —
  one dependency, one pin, two consumers. A future upgrade of that pinned
  commit (a Plan-level, not architectural, change) updates both
  consumers together, from the one relocated declaration — this
  amendment does not introduce a second, independently-versioned copy
  to keep in sync.
- **Warnings**: the cooker's own new `stb_image` TU builds under this
  project's existing `atlantis_compiler_warnings` target exactly as
  `png_codec.cpp` already does today — no new warning-suppression
  mechanism, no relaxed warning level, introduced by this amendment.

### Consequences of this amendment

#### Positive

- Closes a real, disclosed gap between Spec 0016/ADR-0057's own original
  text (which merely said a future amendment "would be needed") and this
  ADR's own actual, binding scope statement — the two documents no
  longer disagree.
- Fixes a genuine CMake configure-order defect this amendment's own
  research found before it could reach an implementing Plan and fail
  there instead.
- Preserves this ADR's own original `Accepted` Decision, Consequences,
  and Alternatives Considered untouched — a future reader can still see
  exactly what was decided 2026-08-16 and why, unmodified.

#### Negative / Trade-offs

- `stb_image`'s own decoder is, for the first time, part of a build-time
  Tools executable's input-handling surface, not only a test-support
  target's — mitigated exactly as this ADR's own existing "Negative /
  Trade-offs" already states for the test-only case: authoring images are
  developer-supplied, trusted input, never loaded from an untrusted or
  network source, matching every other cooker's own existing trust model
  in this repository.
- Relocating `stb`'s `FetchContent` declaration out of
  `cmake/AtlantisDependencies.cmake` is a real, if small, build-system
  restructuring — a Plan implementing Spec 0016 must perform it
  correctly (new file, updated `CMakeLists.txt` include order,
  `include_guard(GLOBAL)` preserved) or the configure-order defect above
  persists.

### Alternatives Considered (for this amendment)

- **Decode PNG at Runtime load time instead of at cook time**, avoiding
  any need to extend `stb`'s linkage into Tools at all. Rejected outright
  — directly contradicts Spec 0016's own explicit "Runtime never parses
  PNG/JPEG" requirement and this repository's authoring/runtime
  separation principle (ADR-0035); would also require linking `stb` into
  a runtime-linked target, a strictly worse boundary than the one this
  amendment actually proposes.
- **Use a platform/OS-provided image codec** (e.g. Windows Imaging
  Component / WIC) for the cooker instead of `stb_image`. Rejected: a
  Windows-only API contradicts this project's own cross-platform
  (Windows/Android/future iOS) target set stated in AGENTS.md — a cooker
  dependency should not be harder to build on Android's own host
  tooling than the small, portable library already in use.
- **`libpng` (+ zlib)** for the cooker. Rejected for the same reason this
  ADR's own original "Alternatives Considered" already rejected it for
  golden encoding: heavier, autotools/CMake-based, its own transitive
  zlib dependency, no capability benefit over `stb` for this narrow
  "decode one RGBA-ish PNG" use case.
- **Vendor or fetch a second, independent copy of `stb` scoped only to
  the cooker**, rather than widening the existing `Stb::Stb` target's own
  linkage. Rejected as needless duplication — two independently pinned
  copies of the same header-only library would need to be kept in sync
  by hand, with no benefit over one shared, relocated declaration two
  targets both depend on.
