# ADR 0041: Image Regression Testing — Golden Image Data Format and Codec Dependency

- **Status:** Proposed
- **Date:** 2026-08-16
- **Deciders:** _Pending Human Review_
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
- **Decode channel contract.** Every `stbi_load`-family call in this
  project's code passes `desired_channels = 4` explicitly. Per
  `stb_image.h`'s own documented contract, a non-zero `desired_channels`
  forces the decoded output to that many channels regardless of the
  source PNG's own encoded channel count — this guarantees a golden PNG
  is always read back as a 4-channel (RGBA) buffer with a fixed, known
  layout, independent of exactly how the encoder chose to write it.
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
