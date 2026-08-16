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
  CMake `FetchContent` pinned to a tagged release (e.g. Catch2, see
  [ADR-0007](0007-test-framework.md)), versus large platform SDKs
  installed externally (Vulkan SDK, Android NDK/SDK). It explicitly
  reserves "future dependencies that don't fit the small pinned source
  dependency via `FetchContent` model" for their own ADR — an image
  codec is exactly such a new category, not decided by ADR-0006 itself.

## Decision

- Golden images are stored as **PNG**, 8-bit-per-channel RGBA,
  losslessly encoding exactly the bytes an `OffscreenTarget` readback
  buffer already produces for whichever `atlantis::rhi::Format` the
  capturing scene used — no color-space conversion, no re-quantization,
  no lossy compression.
- PNG encode/decode is implemented using **`stb_image_write.h`** (encode)
  and **`stb_image.h`** (decode) from the `nothings/stb` project
  (public-domain / MIT dual-licensed, single-header, no transitive
  dependencies), fetched via CMake `FetchContent` pinned to a tagged
  commit — the same acquisition pattern ADR-0006/ADR-0007 already
  established for Catch2, not vendored into the repository and not
  installed as a system package.
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
