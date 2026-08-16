# Plan: Image Regression Testing Foundation

- **Spec:** [specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md) (`Approved`)
- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human direction.

## Objective

Implement Spec 0011's approved design: a golden-image comparison harness
built entirely on Spec 0010's unmodified `Renderer` → RenderGraph → RHI →
Vulkan Backend → `OffscreenTarget`/readback path
([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)–[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)),
adding PNG golden storage/provenance
([ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)),
a strict per-pixel comparison algorithm, a dedicated golden validity
check, provenance-mismatch detection, and a standalone golden
regeneration tool
([ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md))
— entirely new code under `tests/image_regression/`, touching no RHI,
RenderGraph, Renderer, or Vulkan Backend public API.

## Authoritative Sources

Read in full before implementing any step below:

- [specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md)
  (`Approved`) — the governing spec; every step below cites the exact
  Requirements bullet(s) it implements.
- [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  (`Accepted`) — PNG format, `stb_image`/`stb_image_write` dependency,
  full usage contract (implementation macros, forced-channel decode +
  `channels_in_file` check, no-flip contract, license/offline-build
  implications).
- [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  (`Accepted`) — comparison algorithm (channel tolerance 0,
  failing-pixel budget 0), the golden validity check, source-revision
  provenance, provenance-mismatch behavior, golden regeneration's
  locked architecture, golden-update-reason categories, the bounded
  sidecar-encoding contract, and the `tests/image_regression/`
  test-ownership boundary.
- [ADR-0006](../adr/0006-dependency-management.md) (`Accepted`) —
  dependency-acquisition policy. **Note, verified by reading
  `cmake/AtlantisDependencies.cmake` directly, not assumed:** this
  repository's actual `FetchContent` mechanism for Catch2 uses
  `URL`/`URL_HASH` (a plain HTTPS tarball GET + SHA-256 pin), not
  `GIT_REPOSITORY`/`GIT_TAG` — the file's own comment states this
  environment's network resets git's smart-HTTP clone protocol
  consistently, while a plain HTTPS GET succeeds. This plan follows the
  same, already-proven mechanism for `stb` (Section 1), not the
  `GIT_TAG` form ADR-0006's prose literally describes — `URL` pointed at
  a specific commit's archive is the same pin, expressed the way this
  project's build actually performs it.
- [docs/process/testing-strategy.md](../docs/process/testing-strategy.md),
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- Existing implementation, read in full or in the specific parts cited
  per step below:
  `cmake/AtlantisDependencies.cmake`,
  `CMakeLists.txt` (root),
  `examples/headless_rendering_demo/{main.cpp,CMakeLists.txt}`,
  `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`,
  `tests/vulkan_backend/CMakeLists.txt`,
  `src/rhi/include/atlantis/rhi/{types,device,offscreen_target,command_list,buffer}.h`,
  `src/renderer/include/atlantis/renderer/renderer.h`,
  `src/tools/shader_compiler/` (process-launching precedent, referenced
  for the golden generator's own `git` subprocess calls — not linked
  against; a new, independent, much smaller implementation, see Section
  3.3).

## Critical Architectural Boundaries (preserved, not re-decided here)

Restated for Plan Review's benefit — none of the following is open for
reinterpretation during this Plan or its Implementation; a step below
that appears to require reopening one of these must stop and escalate
(see Human Review / Plan Review Blockers):

- **No RHI, RenderGraph, Renderer, or Vulkan Backend public API
  changes, anywhere, for any reason.** Every new type/function this
  plan introduces is confined to `tests/image_regression/`.
- **No `Vk*` type, no Vulkan header, referenced anywhere under
  `tests/image_regression/`.** Every GPU-touching operation goes
  through Atlantis's own existing public API
  (`atlantis::rhi::Device`/`OffscreenTarget`/`CommandList`/`Buffer`,
  `atlantis::vulkan_backend::createDevice()`,
  `atlantis::renderer::Renderer`) — the same discipline
  `tests/vulkan_backend/headless_rendering_gpu_tests.cpp` already
  follows (verified: it references no `Vk*` type directly).
- **No new top-level module.** `tests/image_regression/` is a test-suite
  area, parallel to `tests/core/`, `tests/rhi/`, `tests/render_graph/`,
  `tests/renderer/`, `tests/vulkan_backend/` — not an addition to
  [AGENTS.md](../AGENTS.md)'s module list.
- **Channel tolerance = 0, failing-pixel budget = 0 — not configurable
  at a call site, not a runtime parameter.** Baked in as named
  constants internal to the comparison function (Section 2.1), per
  ADR-0042's own confirmed-not-placeholder decision — no API accepts a
  caller-supplied tolerance/budget override.
- **No second image-codec or serialization/parsing dependency beyond
  `stb_image`/`stb_image_write`.** The sidecar format (Section 2.3) is
  hand-rolled, dependency-free, using only the C++ standard library.
- **No reuse of `src/shader_system/`'s private JSON parser, and no
  dependency of `tests/image_regression/` on any other module's private
  implementation.** Confirmed by this plan's own file list (Section
  "Files / Modules Touched") never referencing
  `src/shader_system/src/*` or any other module's private headers.
- **Golden regeneration is never reachable from an ordinary `ctest -L
  gpu` run.** The golden generator (Section 3) is a plain
  `add_executable()`, never passed to `catch_discover_tests()`, never a
  registered CTest test.
- **No golden file is written by any code path this plan adds except
  the golden generator's own explicit, human-invoked write.** The
  GPU-required comparison test (Section 5.2) only ever reads
  `tests/image_regression/goldens/`.

## Non-Goals (confirmed matching Spec 0011)

Not implemented by this plan, in any form: automated CI-enforced
gating (no `.github/workflows/*.yml`, no CI runner provisioning);
Android, iOS, or Linux support/build configuration of any kind;
per-GPU-vendor golden sets or any cross-vendor stability claim;
percentage-based or perceptual (SSIM) tolerance; automatic/silent golden
acceptance or rebaseline; any new scene beyond the one reused
`examples/headless_rendering_demo` cube fixture; any change to
`Renderer`'s output or any rendering feature; a general
image-loading/asset-pipeline capability (the `stb` dependency stays
scoped to `tests/image_regression/`'s own targets).

## Candidate-API Status

Every concrete type/function name, file split, and struct layout below
is a **candidate** — a reasonable, precedent-consistent proposal for
Plan Review to confirm or redirect, not a re-opening of anything Spec
0011/ADR-0041/ADR-0042 already fixed at the conceptual level. Where this
plan found more than one genuinely different, defensible implementation
shape, it says so explicitly and defers the choice to Plan Review (see
"Design choices flagged for Plan Review" under Human Review / Plan
Review Blockers) — it does not pick silently.

---

## 1. `stb` Dependency Integration

**Spec Requirement:** "Data format and dependencies." **ADR:**
[ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md).

### 1.1 `cmake/AtlantisDependencies.cmake` (modify)

- **Input:** the existing `FetchContent_Declare(Catch2 URL ... URL_HASH
  ...)` block as the exact pattern to follow.
- **Output:** append, after the existing Catch2 block:
  ```cmake
  # ADR-0041: stb (nothings/stb) has no git tags or GitHub Releases --
  # pinned to a specific, full commit hash's archive instead of a
  # tagged release, via the same URL/URL_HASH mechanism (not GIT_TAG)
  # this file already uses for Catch2, for the same network-reliability
  # reason documented above.
  FetchContent_Declare(
    stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(stb)

  # stb ships no CMakeLists.txt of its own (it is a pair of single-header
  # libraries, not a CMake project) -- wrap its fetched source directory
  # in a plain INTERFACE target, matching this repository's own
  # target-naming convention (Atlantis::* / <Vendor>::<Lib> alias style).
  add_library(stb INTERFACE)
  target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
  add_library(Stb::Stb ALIAS stb)
  ```
  **Commit/hash provenance, verified, not assumed:** `2c980bb59875b0d32144a71867fbdebb2f77cd20`
  is `nothings/stb`'s actual `master`-branch HEAD as of this plan's
  drafting (`stb_image.h` v2.30, `stb_image_write.h` v1.16), confirmed
  via `gh api repos/nothings/stb/commits/master`. The `URL_HASH` was
  computed by downloading this exact archive URL and running
  `sha256sum` against it directly (not copied from any second-hand
  source), and the archive's contents were independently confirmed (via
  `tar -tzf`) to contain `stb_image.h`, `stb_image_write.h`, and
  `LICENSE` at the expected `stb-2c980bb.../` top-level path.
- **Dependency order:** first — every other step depends on `Stb::Stb`
  existing.
- **Tests after this step:** none directly; a successful CMake
  configure (`cmake -S . -B build`) is this step's own pass/fail signal
  — a hash mismatch fails configure immediately and loudly, per
  `FetchContent`'s own built-in behavior.
- **Stop condition / rollback:** revert this file alone; nothing
  references `Stb::Stb` until Section 2.4.

---

## 2. GPU-Independent Support Library

**Spec Requirement:** "Comparison algorithm," "Golden image storage,
naming, and provenance" (the GPU-independent parts), "Test/module
ownership." **ADR:**
[ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md),
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).

New static library, `Atlantis::ImageRegressionSupport` (CMake target
`atlantis_image_regression_support`), under
`tests/image_regression/support/` — depends only on `Atlantis::Core` and
`Stb::Stb`; no RHI, RenderGraph, Renderer, or Vulkan Backend dependency
anywhere in this section. This is the layer
[testing-strategy.md](../docs/process/testing-strategy.md) calls
"GPU-independent... exercised against synthetic in-memory buffers... no
Vulkan device."

### 2.1 New: `tests/image_regression/support/pixel_diff.h` / `.cpp`

- **Input:** ADR-0042's "Comparison algorithm" Decision text.
- **Output:**
  ```cpp
  namespace atlantis::image_regression {

  struct PixelBuffer {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Tightly packed RGBA8, width * height * 4 bytes -- the exact
    // layout OffscreenTarget's readback Buffer and a decoded PNG both
    // already share (ADR-0040 / ADR-0041's "no vertical flip" note).
    std::vector<std::uint8_t> rgba8;
  };

  struct ComparisonReport {
    bool passed = false;
    std::uint32_t maxChannelDiff = 0;
    double meanAbsoluteDiff = 0.0;
    std::uint64_t outOfToleranceCount = 0;
    double outOfTolerancePercentage = 0.0;
  };

  // ADR-0042: confirmed, not configurable -- no function below accepts
  // a caller-supplied tolerance or budget override.
  inline constexpr std::uint8_t kChannelTolerance = 0;
  inline constexpr std::uint64_t kFailingPixelBudget = 0;

  // Preconditions: actual.width == golden.width && actual.height ==
  // golden.height (format/extent mismatch is the caller's job to check
  // first -- see golden_validity.h; this function assumes matching
  // shape and asserts it, ATLANTIS_CHECK, not a Result -- a shape
  // mismatch reaching this function is a caller precondition
  // violation, not a recoverable comparison outcome).
  [[nodiscard]] ComparisonReport compareBuffers(const PixelBuffer& actual, const PixelBuffer& golden);

  }  // namespace atlantis::image_regression
  ```
  `compareBuffers()`'s body: single pass over every pixel/channel,
  `std::abs(int(actual) - int(golden))`; a pixel is "out of tolerance"
  if **any** channel's diff exceeds `kChannelTolerance` (currently 0, so
  any nonzero diff); `passed = (outOfToleranceCount <=
  kFailingPixelBudget)` (currently 0, so `passed` iff
  `outOfToleranceCount == 0`).
- **Dependency order:** none beyond `<cstdint>`/`<vector>`.
- **Tests after this step:** see 5.1.
- **Stop condition / rollback:** revert this file alone.

### 2.2 New: `tests/image_regression/support/png_codec.h` / `.cpp`

- **Input:** [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)'s
  full "stb usage contract" — implementation-macro single-translation-
  unit rule, forced 4-channel decode plus `channels_in_file`/bit-depth
  metadata check, no vertical-flip calls, `Stb::Stb`.
- **Output:**
  ```cpp
  // png_codec.cpp -- the ONE translation unit in this repository that
  // defines these two macros. No other file may define either.
  #define STB_IMAGE_IMPLEMENTATION
  #define STB_IMAGE_WRITE_IMPLEMENTATION
  #include <stb_image.h>
  #include <stb_image_write.h>
  ```
  ```cpp
  // png_codec.h
  namespace atlantis::image_regression {

  enum class PngDecodeError { FileNotFound, DecodeFailed, ChannelCountMismatch, UnsupportedBitDepth };
  enum class PngEncodeError { WriteFailed };

  struct DecodedPng {
    PixelBuffer pixels;
    // stb's own channels_in_file out-parameter -- the file's real,
    // as-encoded channel count, independent of the forced 4-channel
    // *output buffer* (ADR-0041's own "why forcing 4 channels is not
    // enough on its own"). Already validated == 4 and 8-bit by the time
    // this struct is returned Ok -- see decodePng()'s own contract
    // below; callers do not need to re-check these two fields, they
    // exist for diagnostic/logging use only.
    int channelsInFile = 0;
    bool is16Bit = false;
  };

  // Decodes path with desired_channels = 4 (always). Additionally reads
  // stb's own channels_in_file out-parameter and calls
  // stbi_is_16_bit_from_memory() (or the _from_file variant); returns
  // Err(ChannelCountMismatch) if channels_in_file != 4, or
  // Err(UnsupportedBitDepth) if 16-bit -- these two checks run even
  // though decode itself "succeeded" per stb's own return value, since
  // a forced-4-channel decode of a real-3-channel file is exactly the
  // silent-masking failure mode ADR-0041 requires catching.
  [[nodiscard]] atlantis::Result<DecodedPng, PngDecodeError> decodePng(const std::filesystem::path& path);

  // Never calls stbi_flip_vertically_on_write() -- writes pixels.rgba8
  // exactly as given, row 0 first (ADR-0041's own row-order contract).
  [[nodiscard]] atlantis::Result<std::monostate, PngEncodeError> encodePng(const std::filesystem::path& path,
                                                                            const PixelBuffer& pixels);

  }  // namespace atlantis::image_regression
  ```
  `Result<std::monostate, E>` for `encodePng()`'s success case — matches
  the established Core precedent (`Device::waitIdle()`, Spec 0008's
  `atlantis_shader_compiler_lib`) for a `Result` that carries no value on
  success.
- **Dependency order:** after 1.1 (`Stb::Stb`), 2.1 (`PixelBuffer`).
- **Tests after this step:** see 5.1 (round-trip, forced-channel-mismatch,
  16-bit-rejection cases).
- **Stop condition / rollback:** revert this file alone; nothing else
  compiles against it until 2.5/2.6 (this library's own CMakeLists.txt)
  and 5.1 (its tests) land — safe to revert independently before those.

### 2.3 New: `tests/image_regression/support/provenance.h` / `.cpp`

- **Input:** ADR-0042's "Source revision, precisely," "Vulkan version
  fields, three, separate, never concatenated," "Behavior when the
  current environment does not match a golden's recorded provenance,"
  and "Sidecar encoding and parsing" Decision text.
- **Output — the sidecar's exact flat format, fixed by this step (not
  left further open):**
  ```
  schema_version: 1
  capture_date: 2026-08-17T00:00:00Z
  source_revision: 217db1a30c0934c66afa1dfbba8fdbfbe60fea67
  gpu_vendor: Intel
  gpu_model: Intel(R) Arc(TM) B370 GPU
  driver_version: 101.8509
  os_build: Windows 11 Home, Build 26200
  vulkan_loader_api_version: 1.4.357
  vulkan_requested_instance_api_version: 1.4.357
  vulkan_physical_device_api_version: 1.4.335
  extent_width: 512
  extent_height: 512
  format: Rgba8Unorm
  ```
  Exactly 13 lines, exactly this field order, one field per line,
  `^<field_name>: <value>$` (single space after the colon, no leading/
  trailing whitespace in `<value>`), UTF-8, `\n` line endings, a single
  trailing newline. `schema_version` is always line 1 and always `1`
  for this plan's own format — a parser that finds any other value on
  line 1 rejects the file outright as an unrecognized/future schema,
  rather than guessing. **`vulkan_loader_api_version`'s value is either
  a dotted `major.minor.patch` string or the literal token
  `unavailable`** — the one, sole, explicitly-modeled exception to
  "every value is a plain string/number," covering the genuine
  pre-Vulkan-1.1-loader case ADR-0042 requires accounting for.
  ```cpp
  namespace atlantis::image_regression {

  struct Provenance {
    std::string captureDate;
    std::string sourceRevision;
    std::string gpuVendor;
    std::string gpuModel;
    std::string driverVersion;
    std::string osBuild;
    std::string vulkanLoaderApiVersion;              // dotted string, or "unavailable"
    std::string vulkanRequestedInstanceApiVersion;    // dotted string
    std::string vulkanPhysicalDeviceApiVersion;       // dotted string
    std::uint32_t extentWidth = 0;
    std::uint32_t extentHeight = 0;
    std::string format;                               // matches an atlantis::rhi::Format enumerator name
  };

  // The narrower schema tests/image_regression/current_environment.sidecar.txt
  // uses (Section 3.2) -- same 8 hardware/environment-identity fields,
  // no capture_date/source_revision/extent/format (those describe a
  // specific *capture*, not the machine itself).
  struct EnvironmentProvenance {
    std::string gpuVendor;
    std::string gpuModel;
    std::string driverVersion;
    std::string osBuild;
    std::string vulkanLoaderApiVersion;
    std::string vulkanRequestedInstanceApiVersion;
    std::string vulkanPhysicalDeviceApiVersion;
  };

  enum class ProvenanceParseError { WrongLineCount, UnknownSchemaVersion, FieldNameMismatch, MalformedValue };

  [[nodiscard]] atlantis::Result<Provenance, ProvenanceParseError> parseGoldenProvenance(const std::string& sidecarText);
  [[nodiscard]] std::string serializeGoldenProvenance(const Provenance& provenance);

  [[nodiscard]] atlantis::Result<EnvironmentProvenance, ProvenanceParseError>
  parseEnvironmentProvenance(const std::string& sidecarText);
  [[nodiscard]] std::string serializeEnvironmentProvenance(const EnvironmentProvenance& provenance);

  struct ProvenanceFieldDiff {
    std::string fieldName;
    std::string goldenValue;
    std::string currentValue;
  };

  // Compares golden's 7 hardware/environment fields (gpuVendor through
  // vulkanPhysicalDeviceApiVersion) against current -- never
  // captureDate/sourceRevision/extent/format, which describe the
  // capture event, not the environment. Empty return == full match.
  [[nodiscard]] std::vector<ProvenanceFieldDiff> compareProvenanceEnvironment(const Provenance& golden,
                                                                               const EnvironmentProvenance& current);

  }  // namespace atlantis::image_regression
  ```
  Parsing is **strict**: `parseGoldenProvenance()`/`parseEnvironmentProvenance()`
  return `Err` on any deviation — wrong line count, a line not matching
  its expected field name at that exact position, an empty value, a
  non-numeric `extent_width`/`extent_height`, or an unrecognized
  `format` value (must be a real `atlantis::rhi::Format` enumerator
  name — the string form, compared by name, not parsed as an enum
  directly, since this file has no RHI dependency; validated as a known
  string set: `"Unknown"`, `"Bgra8Unorm"`, `"Bgra8Srgb"`,
  `"Rgba8Unorm"`, `"Rgba8Srgb"`, matching `types.h`'s `Format` enum by
  name).
- **Dependency order:** after 2.1 (uses no `PixelBuffer` type directly,
  but lives in the same library and is conventionally grouped after it).
- **Tests after this step:** see 5.1.
- **Stop condition / rollback:** revert this file alone.

### 2.4 New: `tests/image_regression/support/golden_validity.h` / `.cpp`

- **Input:** ADR-0042's "Golden validity check" Decision text (the four
  ordered steps); 2.2 (`decodePng()`), 2.3 (`Provenance`).
- **Output:**
  ```cpp
  namespace atlantis::image_regression {

  enum class GoldenValidityError {
    MissingPngFile,
    MissingSidecarFile,
    PngDecodeFailed,       // wraps PngDecodeError::DecodeFailed
    ChannelCountMismatch,  // wraps PngDecodeError::ChannelCountMismatch
    UnsupportedBitDepth,   // wraps PngDecodeError::UnsupportedBitDepth
    SidecarMalformed,      // wraps ProvenanceParseError
    SidecarFormatExtentMismatch,  // sidecar's recorded format/extent != PNG's own decoded properties
  };

  struct ValidatedGolden {
    PixelBuffer pixels;
    Provenance provenance;
  };

  // The four-step check, in this exact order, matching ADR-0042
  // verbatim: (1) PNG exists and decodes; (2) decoded properties
  // (channelsInFile == 4, not 16-bit) satisfy the RGBA8 contract; (3)
  // sidecar's own recorded format/extent matches the PNG's actual
  // decoded width/height/format; (4) the sidecar found alongside pngPath
  // is structurally the one for it (same basename stem, per Section
  // 2.6's naming convention -- a caller-supplied mismatched pair, e.g.
  // by constructing paths incorrectly, is what this step catches).
  // Returns Err at the first failing step -- never partially populates
  // ValidatedGolden on a failure path.
  [[nodiscard]] atlantis::Result<ValidatedGolden, GoldenValidityError> loadAndValidateGolden(
      const std::filesystem::path& pngPath, const std::filesystem::path& sidecarPath);

  }  // namespace atlantis::image_regression
  ```
- **Dependency order:** after 2.2, 2.3.
- **Tests after this step:** see 5.1 (each of the four steps' failure
  mode exercised independently against constructed test fixtures — a
  missing file, a real 3-channel PNG, a 16-bit PNG, a sidecar with a
  deliberately wrong extent).
- **Stop condition / rollback:** revert this file alone.

### 2.5 New: `tests/image_regression/support/CMakeLists.txt`

- **Output:**
  ```cmake
  add_library(atlantis_image_regression_support STATIC
    pixel_diff.cpp
    png_codec.cpp
    provenance.cpp
    golden_validity.cpp
  )

  target_include_directories(atlantis_image_regression_support
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  )

  target_link_libraries(atlantis_image_regression_support
    PUBLIC
      Atlantis::Core
    PRIVATE
      Stb::Stb
      atlantis_compiler_warnings
  )

  add_library(Atlantis::ImageRegressionSupport ALIAS atlantis_image_regression_support)
  ```
  `Stb::Stb` is `PRIVATE`: only `png_codec.cpp` includes `<stb_image.h>`/
  `<stb_image_write.h>`; no consumer of this library needs `stb`'s own
  include directory on its path, matching ADR-0041's "linked only into
  test-support targets" scoping down to the file level, not just the
  target level.
- **Dependency order:** after 2.1–2.4.
- **Stop condition / rollback:** revert this file; nothing links against
  `Atlantis::ImageRegressionSupport` until 2.6.

### 2.6 `tests/image_regression/CMakeLists.txt` (new)

- **Output:** top-level file for this test area; this step only adds:
  ```cmake
  add_subdirectory(support)
  ```
  (Sections 3, 5 append more to this same file — see those steps.)
- **Dependency order:** after 2.5.
- **Stop condition / rollback:** revert this file's one line, or the
  whole file if nothing else has landed yet.

### 2.7 Root `CMakeLists.txt` (modify)

- **Output:** inside the existing `if(ATLANTIS_BUILD_TESTS)` block, add
  `add_subdirectory(tests/image_regression)`, positioned after the
  existing `tests/vulkan_backend` line (alphabetical-ish grouping this
  file's own existing list roughly already follows, not a hard rule).
- **Dependency order:** after 2.6.
- **Tests after this step:** none directly; this is the wiring that
  makes Section 2's own future unit tests (5.1) discoverable at all.
- **Stop condition / rollback:** revert this one line; the rest of
  Section 2 still compiles as a standalone library, just not part of the
  default build graph.

---

## 3. Fixture Library and Golden Generator Tool

**Spec Requirement:** "Reproducibility," "Golden regeneration," "Golden
image storage, naming, and provenance" (the write path). **ADR:**
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).

### 3.1 New: `tests/image_regression/fixture/minimal_cube_fixture.h` / `.cpp`

- **Input:** `examples/headless_rendering_demo/main.cpp`, read in full —
  this file's own `kCubeVertices`, `kCubeIndices`, `kExtentPixels`
  (512), `kColorFormat` (`Rgba8Unorm`), `lookAt()`/`perspective()` math,
  and shader-loading path (`shaders/minimal_mesh.{vert,frag}.spv` +
  `.refl.json`), copied **byte-for-byte** — this plan's own fixture must
  produce pixel-identical output to the fixture the calibration evidence
  cited in ADR-0042's own Context was captured against, or that evidence
  no longer applies (see Critical Architectural Boundaries).
- **Output:** a small library providing everything both 3.3 (golden
  generator) and 5.2 (GPU-required comparison test) need to construct
  and render one frame:
  ```cpp
  namespace atlantis::image_regression {

  // Matches examples/headless_rendering_demo's own fixture exactly --
  // duplicated, not shared via a new cross-example library (Plan 0010
  // Section 7.1's own precedent: duplication is this project's default
  // over introducing a shared-fixture module neither prior example
  // needed). This duplication is deliberate and load-bearing: the
  // calibration evidence ADR-0042's Context cites was captured against
  // examples/headless_rendering_demo's exact vertex/camera/material
  // values, and this fixture must reproduce them exactly, not merely
  // "a similar cube."
  struct MinimalCubeFixture {
    std::unique_ptr<atlantis::rhi::Device> device;
    std::optional<atlantis::renderer::Mesh> mesh;
    std::optional<atlantis::renderer::Material> material;
    std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
    std::unique_ptr<atlantis::rhi::Texture> depthTexture;
    std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
    std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;
  };

  inline constexpr std::uint32_t kFixtureExtentPixels = 512;
  inline constexpr atlantis::rhi::Format kFixtureColorFormat = atlantis::rhi::Format::Rgba8Unorm;

  enum class FixtureSetupError { DeviceCreationFailed, ShaderLoadFailed, ResourceCreationFailed };

  // Constructs every long-lived resource once (mirrors
  // headless_rendering_demo's own setup sequence). Must be called with
  // the process's current working directory set to a location where
  // "shaders/minimal_mesh.{vert,frag}.spv" resolves (same relative-path
  // convention every prior demo/GPU-test uses) -- see 3.4/3.5's
  // WORKING_DIRECTORY wiring.
  [[nodiscard]] atlantis::Result<MinimalCubeFixture, FixtureSetupError> setUpMinimalCubeFixture();

  enum class FixtureRenderError { AcquireFailed, CommandListCreationFailed, SubmitFailed, WaitIdleFailed };

  // One full acquire -> draw -> copy -> submit -> waitIdle cycle
  // (Spec 0010's own flow, unchanged), returning the readback buffer's
  // contents as a PixelBuffer. May be called more than once against the
  // same MinimalCubeFixture (OffscreenTarget's own repeated-cycle
  // contract, ADR-0038) -- each call is independent.
  [[nodiscard]] atlantis::Result<PixelBuffer, FixtureRenderError> renderOneFrame(MinimalCubeFixture& fixture);

  }  // namespace atlantis::image_regression
  ```
  `renderOneFrame()`'s body mirrors
  `examples/headless_rendering_demo/main.cpp`'s per-cycle loop body
  exactly (acquire, write camera matrices — the same fixed `lookAt()`/
  `perspective()` values, no per-call variation — `Renderer::drawFrame(...,
  ResourceState::TransferSource)`, the caller-built copy-pass graph,
  `submit()`, `waitIdle()`, read `readbackBuffer->mappedData()` into a
  returned `PixelBuffer`), factored into a reusable function instead of
  being inlined in a `main()` loop.
- **Dependency order:** after Section 2 (uses `PixelBuffer` from 2.1).
- **Tests after this step:** none directly (this is GPU-required setup/
  render code, exercised for real only by 3.3/5.2 on real hardware); no
  GPU-independent test double is introduced for it — matching Spec
  0010's own precedent that headless composition code is verified via
  real GPU tests, not a `FakeCommandList`-style substitute (the fixture
  itself is not RenderGraph-execution logic; it is ordinary setup code).
- **Stop condition / rollback:** revert this file alone; nothing depends
  on it compiling until 3.2's CMakeLists.txt links it in.

### 3.2 New: `tests/image_regression/fixture/CMakeLists.txt`

- **Output:**
  ```cmake
  add_library(atlantis_image_regression_fixture STATIC
    minimal_cube_fixture.cpp
  )

  target_include_directories(atlantis_image_regression_fixture
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  )

  target_link_libraries(atlantis_image_regression_fixture
    PUBLIC
      Atlantis::RHI
      Atlantis::Renderer
      Atlantis::ImageRegressionSupport
    PRIVATE
      Atlantis::VulkanBackend
      Atlantis::RenderGraph
      Atlantis::ShaderSystem
      Atlantis::ShaderSystemRhiIntegration
      atlantis_compiler_warnings
  )

  add_library(Atlantis::ImageRegressionFixture ALIAS atlantis_image_regression_fixture)
  ```
  `Atlantis::VulkanBackend` stays `PRIVATE`: consumers construct a
  `Device` via `atlantis::vulkan_backend::createDevice()` (a free
  function call, not a type this library re-exports in its own public
  headers) — matching `examples/headless_rendering_demo`'s own dependency
  shape.
- **Dependency order:** after 3.1, 2.5.
- **Stop condition / rollback:** revert this file; nothing links against
  `Atlantis::ImageRegressionFixture` until 3.4/5.4.

### 3.3 New: `tests/image_regression/golden_generator/main.cpp`

- **Input:** ADR-0042's "Golden regeneration" and "Source revision,
  precisely" Decision text; `src/tools/shader_compiler/`'s own
  `CreateProcessW`-based launcher as prior art for "this project already
  shells out to an external process from a dev tool," not linked
  against — this is a new, much smaller, independent implementation
  (two fixed, literal command strings, no argv construction/escaping
  complexity `atlantis_shader_compiler_lib` needed for an arbitrary
  compiler invocation).
- **Output — invocation contract, fixed by this step:**
  ```
  atlantis_image_regression_golden_generator.exe <golden-png-path>
  ```
  One required positional argument: the golden PNG's path (e.g.
  `tests/image_regression/goldens/minimal_cube/minimal_cube_512x512_rgba8unorm.png`,
  resolved relative to the process's current working directory — run it
  from the repository root, or pass an absolute path). No other flags.
  On any argument-count mismatch, prints usage and exits `2`.

  **Sequence:**
  1. Shell out to `git status --porcelain` (via `_popen`, MSVC/Windows
     CRT — no new dependency, the same tier of "already-available OS/
     compiler facility" `CreateProcessW` is in `shader_compiler`'s own
     precedent, just a lighter-weight API sufficient for this simpler
     case: two fixed, literal, non-user-influenced command strings, no
     injection surface). Non-empty output → refuse, print "working tree
     is not clean; commit or stash changes before regenerating a golden"
     to stderr, exit `1`. **This is the "require clean source revision"
     enforcement point.**
  2. Shell out to `git rev-parse HEAD` (same mechanism); its trimmed
     stdout becomes `Provenance::sourceRevision`.
  3. Read `tests/image_regression/current_environment.sidecar.txt`
     (fixed, repository-root-relative path — Section 3.2 below defines
     this file's own format and why it is not checked in) via
     `parseEnvironmentProvenance()` (2.3). Missing or malformed → refuse,
     print the expected path and format, exit `1` — the tool never
     fabricates a plausible-looking but unverified environment record.
  4. `setUpMinimalCubeFixture()` + `renderOneFrame()` (3.1). Any `Err` →
     print the error, exit `1`.
  5. Build the full `Provenance` (2.3): `captureDate` = current UTC time
     (`<chrono>`, `std::chrono::system_clock::now()`, ISO 8601), the
     `sourceRevision`/environment fields from steps 1–3, `extentWidth`/
     `extentHeight` = 512, `format` = `"Rgba8Unorm"`.
  6. If `<golden-png-path>` already exists, decode its current sidecar
     (if present) and print an old-vs-new provenance summary to stdout
     (every field that differs, named) — **visibility, not a second
     confirmation gate**; the tool proceeds to overwrite regardless, its
     own deliberate, separate-binary invocation already being the
     safety boundary ADR-0042 requires (no interactive prompt, so the
     tool remains scriptable).
  7. `encodePng(pngPath, pixels)` (2.2), then write the sidecar text
     (`serializeGoldenProvenance()`, 2.3) to `<golden-png-path>` with
     its extension replaced by `.sidecar.txt` (e.g.
     `minimal_cube_512x512_rgba8unorm.sidecar.txt`, same directory, same
     stem). Either write failing → print the error, exit `1`.
  8. Print a success summary (path, provenance) to stdout, exit `0`.

  Every exit path calls `Device::waitIdle()` before any RAII-owned
  Vulkan resource in `MinimalCubeFixture` is destroyed (matching
  `examples/headless_rendering_demo`'s own established teardown
  discipline) — including the argument-mismatch/dirty-tree/missing-
  environment-file early-exit paths, which never construct a
  `MinimalCubeFixture` at all and so have nothing to wait on.
- **Dependency order:** after 3.1, 3.2, 2.5 (2.2/2.3's `encodePng()`/
  `serializeGoldenProvenance()`).
- **Tests after this step:** none automated (this tool is not
  CTest-registered — see Critical Architectural Boundaries); exercised
  manually once, for real, in Section 4.
- **Stop condition / rollback:** revert this file alone.

### 3.4 New: `tests/image_regression/golden_generator/CMakeLists.txt`

- **Output:**
  ```cmake
  add_executable(atlantis_image_regression_golden_generator main.cpp)

  target_link_libraries(atlantis_image_regression_golden_generator
    PRIVATE
      Atlantis::ImageRegressionFixture
      Atlantis::ImageRegressionSupport
      atlantis_compiler_warnings
  )

  # Same build-tree Shader-System-produced artifact set every prior
  # headless/windowed demo/test copies -- this tool renders the
  # identical minimal_mesh shader pair (Section 3.1).
  add_dependencies(atlantis_image_regression_golden_generator minimal_mesh_shaders)
  add_custom_command(TARGET atlantis_image_regression_golden_generator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
        "$<TARGET_FILE_DIR:atlantis_image_regression_golden_generator>/shaders/"
  )
  ```
  **Deliberately no `catch_discover_tests()` call, no `LABELS "gpu"`
  property, no `add_test()` of any kind in this file** — this is the
  concrete enforcement of "never registered with CTest." **Deliberately
  no `run_...` convenience custom target** (unlike every demo's own
  `run_X_demo` target): this tool requires a positional argument that
  varies per invocation, so a parameterless custom target would need
  editing on every use — a minor convenience loss, accepted in exchange
  for not adding a target whose easy, memorable name might invite
  casual/accidental invocation.
- **Dependency order:** after 3.3.
- **Stop condition / rollback:** revert this file alone.

### 3.5 `tests/image_regression/CMakeLists.txt` (modify)

- **Output:** append `add_subdirectory(fixture)` and
  `add_subdirectory(golden_generator)` to the file 2.6 started.
- **Dependency order:** after 3.2, 3.4.
- **Stop condition / rollback:** revert these two lines.

### 3.6 New: `tests/image_regression/current_environment.sidecar.txt.example`

- **Input:** `EnvironmentProvenance` (2.3); this repository's own
  established precedent for how GPU/driver/Vulkan-version facts are
  actually obtained today — **by a human running `vulkaninfo --summary`
  and reading off the values** (confirmed: this is exactly how every
  prior spec's own "Manual verification record"/disclosed-hardware
  section, including Spec 0010's, was produced — RHI's public API has
  no `deviceInfo()`/equivalent query, and this plan does not add one,
  per Critical Architectural Boundaries).
- **Output:** a checked-in **example/template** file (`.example` suffix,
  not the real, machine-specific file the tooling reads — see 3.7):
  ```
  vulkan_environment_provenance_schema: 1
  gpu_vendor: Intel
  gpu_model: Intel(R) Arc(TM) B370 GPU
  driver_version: 101.8509
  os_build: Windows 11 Home, Build 26200
  vulkan_loader_api_version: 1.4.357
  vulkan_requested_instance_api_version: 1.4.357
  vulkan_physical_device_api_version: 1.4.335
  ```
  A short header comment (as a `.example` file, not itself parsed, plain
  Markdown-adjacent prose is fine) explains: copy this file to
  `current_environment.sidecar.txt` in the same directory (git-ignored,
  Section 3.7), fill in this machine's own real values (`vulkaninfo
  --summary` on Windows), once per development/CI machine that will run
  `atlantis_image_regression_gpu_tests` or the golden generator — not
  once per invocation.
- **Dependency order:** none; documentation-shaped, no compilation
  dependency.
- **Stop condition / rollback:** revert this file alone.

### 3.7 `.gitignore` (modify)

- **Output:** append, in a new, clearly-commented section:
  ```
  # Machine-local Vulkan/GPU environment provenance (Section 3.6/3.7,
  # Plan 0011) -- inherently specific to the machine it was filled in
  # on; would be actively wrong if committed and read on any other
  # machine. See current_environment.sidecar.txt.example for the
  # template and setup instructions.
  tests/image_regression/current_environment.sidecar.txt
  ```
- **Dependency order:** independent; may land alongside 3.6.
- **Stop condition / rollback:** revert this one line.

---

## 4. First Committed Golden (operational, not a code step)

**Spec Requirement:** "The first golden for the reused
`examples/headless_rendering_demo` fixture must itself be captured once,
reviewed by a human... and committed through the same reviewed-PR-diff
workflow as any later update." **ADR:**
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).

- **Input:** Section 3's completed, built
  `atlantis_image_regression_golden_generator`, run on the reference
  Windows/Vulkan machine, with `current_environment.sidecar.txt` (3.6/
  3.7) already populated with that machine's real, `vulkaninfo`-sourced
  values.
- **Output:** run
  ```
  atlantis_image_regression_golden_generator.exe tests/image_regression/goldens/minimal_cube/minimal_cube_512x512_rgba8unorm.png
  ```
  from the repository root (or the tool's own build output directory,
  adjusting the path accordingly), against a **clean working tree at
  the commit that lands Sections 1–3** (per ADR-0042's own same-PR
  ordering rule: Sections 1–3's code is committed first, this golden is
  captured against that already-existing commit, then the golden PNG +
  sidecar are added via a **separate, subsequent commit** — never the
  same commit as Sections 1–3's own code). The resulting PNG + sidecar
  are reviewed by a human as this scene's first "this is what correct
  output looks like today" baseline (the golden-update-reason category
  is **not** "rendering change" / "reference-environment change" /
  "approved rebaseline" — it is the harness's own bootstrap, stated as
  such in the commit/PR description) and committed.
- **Dependency order:** after Section 3 lands and compiles; requires
  real Windows/Vulkan hardware — not exercised by any automated step.
- **Tests after this step:** Section 5.2's GPU-required comparison test
  can only be written and run meaningfully once this golden exists —
  see Sequencing & Dependencies.
- **Stop condition / rollback:** delete the golden PNG + sidecar files;
  no other code depends on their *content* (only their *existence*, for
  5.2 to have something to compare against).

---

## 5. Automated Tests

**Spec Requirement:** "Testing & Verification Plan" (Unit tests, GPU
integration tests). **ADR:** both.

### 5.1 New: GPU-independent test files under `tests/image_regression/`

- **Input:** Section 2 (`Atlantis::ImageRegressionSupport`); Spec 0011's
  own Testing & Verification Plan bullet list (Unit tests), verbatim.
- **Output:** four new Catch2 files, tag `[image_regression]`,
  mirroring `tests/vulkan_backend/`'s one-file-per-concern granularity:
  - `pixel_diff_tests.cpp` — two identical buffers pass; a single
    differing pixel anywhere fails (confirming
    `kFailingPixelBudget == 0` is genuine, not merely small); a
    known, constructed set of differing pixels produces the expected
    `maxChannelDiff`/`meanAbsoluteDiff`/`outOfToleranceCount`/
    `outOfTolerancePercentage`.
  - `png_codec_tests.cpp` — encode-then-decode round-trips a
    synthetic buffer byte-for-byte (confirms ADR-0041's no-added-
    quantization claim); decoding a deliberately-3-channel PNG (write
    one via a raw, minimal in-test PNG encoder call with `comp = 3`,
    or a small checked-in 3-channel test fixture PNG — Plan Review to
    confirm which) returns `Err(ChannelCountMismatch)`; decoding a
    16-bit PNG returns `Err(UnsupportedBitDepth)`.
  - `provenance_tests.cpp` — a well-formed golden sidecar parses
    correctly and round-trips through `serializeGoldenProvenance()`
    byte-for-byte; each of wrong line count / wrong field name at a
    position / unknown `schema_version` / malformed numeric field
    returns the expected `Err`; `vulkan_loader_api_version: unavailable`
    parses successfully as the one modeled exception;
    `compareProvenanceEnvironment()` returns empty for a fully-matching
    pair and names every differing field (not just the first) for a
    constructed mismatch.
  - `golden_validity_tests.cpp` — each of the four validity-check
    steps' failure mode, independently: missing PNG; a real (test-
    fixture) 3-channel PNG; a 16-bit PNG; a sidecar whose recorded
    `extent_width`/`format` deliberately disagrees with its paired
    PNG's actual decoded properties — each produces the specific,
    distinct `GoldenValidityError` variant ADR-0042's four steps name,
    never a generic catch-all.
- **Dependency order:** after Section 2 in full.
- **Tests after this step:** these files **are** the tests; run via
  `ctest -LE gpu` once wired into the build (5.3).
- **Stop condition / rollback:** revert these four files alone; no
  production code depends on them.

### 5.2 New: `tests/image_regression/image_regression_gpu_tests.cpp`

- **Input:** Section 3 (fixture + support libraries), Section 4 (a real,
  committed golden must already exist); Spec 0011's own Testing &
  Verification Plan bullet list (GPU integration tests), verbatim;
  `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`'s own
  `ScopedFailureHandler`-free, plain-`REQUIRE()`-based structure as the
  pattern to follow (this file asserts on `Result`/comparison outcomes,
  not on `ATLANTIS_CHECK` firing, so no `ScopedFailureHandler` is
  needed here).
- **Output:** Catch2 `TEST_CASE`s, tag `[image_regression][gpu]`,
  covering — precisely, per Spec 0011's own Testing & Verification Plan
  bullets for this layer:
  1. A full `setUpMinimalCubeFixture()` → `renderOneFrame()` →
     `loadAndValidateGolden()` → `compareBuffers()` cycle against the
     real, committed `minimal_cube` golden passes (`ComparisonReport::passed
     == true`), Vulkan Validation Layers clean (this test's own device
     is created with `enableValidationLayers = true`, matching every
     prior GPU test in this repository).
  2. The same cycle repeated 3 times against the same fixture (matching
     `examples/headless_rendering_demo`'s own `kCycleCount`) produces
     byte-for-byte identical `PixelBuffer`s each time — the automated,
     permanent form of the determinism verification this plan's
     calibration evidence (ADR-0042's own Context) already performed
     once, ad hoc and uncommitted, during Spec 0011's own review.
  3. **A deliberately introduced rendering regression is caught:** a
     temporary, in-test-only, reverted-before-merge change (e.g.
     `minimal_cube_fixture.cpp`'s clear color or one vertex's position,
     changed for this one manual verification run only — see Section
     6's own checklist item, not committed as part of this file) makes
     `compareBuffers()` return `passed == false` with a nonzero
     `outOfToleranceCount`; this proves the harness detects a real
     regression, not merely that it reports `PASS` against unchanged
     input.
  4. Running `loadAndValidateGolden()` against a path with no PNG (a
     scene slug this plan never commits a golden for) returns
     `Err(MissingPngFile)` — reported as a distinct Catch2 failure
     message prefixed `INVALID GOLDEN:`, not a crash, not a silent
     `SUCCEED()`.
  5. **Provenance-mismatch handling:** read
     `current_environment.sidecar.txt` (skip this `TEST_CASE` — Catch2
     `SKIP()` — with an explanatory message if the file is absent, since
     this specific case exists to test the mismatch-reporting path
     itself, not the core comparison, and cannot run meaningfully
     without a populated environment file); construct an
     `EnvironmentProvenance` with one field deliberately altered from
     what the golden's own sidecar records; confirm
     `compareProvenanceEnvironment()` returns exactly that one field as
     a `ProvenanceFieldDiff`, and confirm (by convention established in
     this test file, not by a shared helper) that logging this mismatch
     via `WARN(...)` does **not** cause the `TEST_CASE` to fail — only
     `compareBuffers()`'s own `passed` value drives the test's pass/fail
     `REQUIRE`.
- **Dependency order:** after 3.1–3.5 (fixture + golden generator's own
  libraries), Section 4 (a real golden must exist on disk for item 1 to
  have anything to load).
- **Tests after this step:** this *is* the test file; run via
  `ctest -L gpu` on real Windows/Vulkan hardware.
- **Stop condition / rollback:** revert this file alone.

### 5.3 `tests/image_regression/CMakeLists.txt` (modify) — GPU-independent test executable

- **Output:** the test executable is a sibling of the library it tests,
  defined directly in `tests/image_regression/CMakeLists.txt` (not
  `support/CMakeLists.txt`), matching every other
  `tests/<module>/CMakeLists.txt` precedent:
  ```cmake
  add_executable(atlantis_image_regression_tests
    pixel_diff_tests.cpp
    png_codec_tests.cpp
    provenance_tests.cpp
    golden_validity_tests.cpp
  )

  target_link_libraries(atlantis_image_regression_tests
    PRIVATE
      Atlantis::ImageRegressionSupport
      Catch2::Catch2WithMain
      atlantis_compiler_warnings
  )

  catch_discover_tests(atlantis_image_regression_tests DISCOVERY_MODE PRE_TEST)
  ```
  No `LABELS "gpu"` anywhere in this block — this executable is entirely
  GPU-independent, matching `tests/render_graph/CMakeLists.txt`'s own
  precedent.
- **Dependency order:** after 5.1, 2.5.
- **Stop condition / rollback:** revert this block; `support/CMakeLists.txt`
  itself is untouched by this step.

### 5.4 `tests/image_regression/CMakeLists.txt` (modify) — GPU-required test executable

- **Output:** append:
  ```cmake
  add_executable(atlantis_image_regression_gpu_tests
    image_regression_gpu_tests.cpp
  )

  target_link_libraries(atlantis_image_regression_gpu_tests
    PRIVATE
      Atlantis::ImageRegressionFixture
      Atlantis::ImageRegressionSupport
      Catch2::Catch2WithMain
      atlantis_compiler_warnings
  )

  catch_discover_tests(atlantis_image_regression_gpu_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "gpu"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>"
  )

  # Same build-tree shader artifact set as the golden generator
  # (Section 3.4) -- this executable renders the identical fixture.
  add_dependencies(atlantis_image_regression_gpu_tests minimal_mesh_shaders)
  add_custom_command(TARGET atlantis_image_regression_gpu_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.spv"
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.vert.refl.json"
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.spv"
        "${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}/minimal_mesh.frag.refl.json"
        "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>/shaders/"
  )
  ```
  **`WORKING_DIRECTORY` set on this executable, but the golden/sidecar
  paths 5.2's own test code opens are repository-root-relative
  (`tests/image_regression/goldens/...`), not build-output-relative —
  unlike the shader path.** This is a real, disclosed inconsistency
  with the shader-loading convention every prior demo/GPU-test uses (all
  of which resolve every relative path, shaders included, against their
  own build output directory). **Flagged for Plan Review, not silently
  picked** — see Human Review / Plan Review Blockers, "Golden/sidecar
  path resolution."
- **Dependency order:** after 5.2, 3.5, Section 4 (needs the real golden
  to exist for a meaningful first `ctest -L gpu` run, though the
  *target* itself compiles without it).
- **Tests after this step:** `ctest -L gpu` discovers and can run
  5.2's cases.
- **Stop condition / rollback:** revert this block alone.

---

## 6. Verification

**Spec Requirement:** "Testing & Verification Plan" (Vulkan Validation
Layers, Manual/local verification). **ADR:** both.

- **Debug and Release builds**, clean, no new compiler warning
  introduced (`atlantis_compiler_warnings` applied to every new target
  above, matching existing convention).
- **GPU-independent suite** (`ctest -LE gpu`), Debug and Release: 5.1's
  new cases pass; every pre-existing GPU-independent test elsewhere in
  the repository continues to pass unmodified (this plan touches no
  existing test file).
- **GPU-required suite** (`ctest -L gpu`), Debug and Release, on real
  Windows/Vulkan hardware, with `current_environment.sidecar.txt`
  populated: 5.2's new cases pass; every pre-existing GPU-required test
  elsewhere in the repository continues to pass unmodified.
- **Vulkan Validation Layers clean** throughout every GPU-touching run
  above — zero warnings, zero errors, in both configs.
- **Deliberate-regression-caught proof** (5.2 item 3): performed once,
  manually, during this Plan's own Implementation — temporarily alter
  `minimal_cube_fixture.cpp`'s clear color (or one vertex position),
  rebuild, re-run `atlantis_image_regression_gpu_tests`, confirm item 1
  now fails with a nonzero `outOfToleranceCount` and a written diff
  artifact, then **revert the temporary change** (matching this
  project's own established pattern for this exact kind of one-off,
  never-committed verification instrumentation — see Spec 0011's own
  calibration precedent) before the Implementation PR is opened.
- **Local/manual gate, recorded:** a human or agent runs
  `ctest -L gpu` (and, at least once, `atlantis_image_regression_golden_generator`
  itself, per Section 4) against real hardware and records: hardware/
  driver/Vulkan version used (matching Spec 0010's own disclosure
  format), pass/fail per test, and confirmation the deliberate-
  regression check above was performed and reverted — this is Spec
  0011's own real, working gate (its Non-Goals explicitly exclude
  automated CI enforcement).
- **`git diff --check` clean** on every commit.
- **Explicit Prohibitions checklist** (below) fully checked.

---

## 7. Documentation and Registry Post-Implementation Updates

Deferred to the Implementation PR itself (not this Plan, not a separate
PR) — matching Spec 0010's own precedent (its Implementation PR updated
`specs/README.md`'s Implementation column as part of landing, not as a
prerequisite to Plan approval):

- `specs/README.md`: Spec 0011's row — Implementation column updated
  from "Not started" to a description of what actually shipped
  (`tests/image_regression/` structure, PR link(s), verification
  summary — GPU-independent/GPU-required pass counts, Debug/Release,
  Validation Layers clean, deliberate-regression-caught confirmation),
  mirroring Spec 0010's own row's level of detail.
- `docs/project-blueprint.md`: Milestone 8 (Image Regression Testing) —
  updated only if Implementation confirms this is in scope for the
  Implementation PR, matching Spec 0010's own "left as an explicit
  Implementation-time decision, not pre-committed here" precedent.
- `docs/architecture/*.md`: updated only if Implementation reveals a
  genuine as-built architecture fact these documents' own content should
  reflect — not assumed necessary by this plan (this plan adds no new
  module or architecture boundary for these documents to describe).

---

## Explicit Prohibitions (grep/code-review checklist)

Every item below must hold, verifiable by inspection, before this
plan's Implementation is considered complete:

- [ ] `git grep -rn "Vk[A-Z]" tests/image_regression/` returns nothing —
      no `Vk*` type referenced anywhere in this plan's own new code.
- [ ] `git grep -rn "#include <vulkan" tests/image_regression/` returns
      nothing.
- [ ] No file under `src/rhi/`, `src/render_graph/`, `src/renderer/`,
      or `src/vulkan_backend/` is modified by this plan's Implementation
      — `git diff --stat` against this plan's own base commit shows only
      files under `tests/image_regression/`, `cmake/`, `.gitignore`, and
      root `CMakeLists.txt`.
- [ ] `git grep -rn "STB_IMAGE_IMPLEMENTATION\|STB_IMAGE_WRITE_IMPLEMENTATION"
      tests/image_regression/` returns **exactly one match each**,
      both in `support/png_codec.cpp`.
- [ ] `git grep -rn "stbi_set_flip_vertically_on_load\|stbi_flip_vertically_on_write"
      tests/image_regression/` returns nothing — this plan's code never
      calls any `stb` flip API.
- [ ] `git grep -rn "#include.*shader_system/src\|shader_system_rhi_integration/src"
      tests/image_regression/` returns nothing — every Shader System
      symbol this plan's code uses
      (`atlantis::shader_system::loadReflectionMetadata()`,
      `rhi_integration::toVertexInputLayout()`) comes from that module's
      public headers only, the same public surface
      `examples/headless_rendering_demo` already uses; nothing here
      includes a private Shader System header directly.
- [ ] `git grep -rn "kChannelTolerance\|kFailingPixelBudget"
      tests/image_regression/` shows both constants defined exactly once
      (`pixel_diff.h`), value `0`, and referenced only from within
      `pixel_diff.cpp` — no call site anywhere overrides or shadows them.
- [ ] No `add_test(` or `catch_discover_tests(` call anywhere references
      `atlantis_image_regression_golden_generator`.
- [ ] `tests/image_regression/current_environment.sidecar.txt` (the
      real, filled-in file, not the `.example` template) does not appear
      in `git status --porcelain` output after being created locally —
      confirms `.gitignore`'s new entry (3.7) actually takes effect.
- [ ] No `find_package`/`FetchContent_Declare` call for any dependency
      beyond `stb` is added anywhere this plan touches.
- [ ] `git diff --check` clean on every commit.

## Build Integration

- `cmake/AtlantisDependencies.cmake`: `stb`'s `FetchContent_Declare`/
  `FetchContent_MakeAvailable`/`Stb::Stb` wrapper (Section 1.1).
- `CMakeLists.txt` (root): one new line,
  `add_subdirectory(tests/image_regression)`, inside the existing
  `if(ATLANTIS_BUILD_TESTS)` block (Section 2.7).
- `tests/image_regression/CMakeLists.txt` (new): `add_subdirectory()`
  calls for `support/`, `fixture/`, `golden_generator/`, plus the two
  test executables (`atlantis_image_regression_tests`,
  `atlantis_image_regression_gpu_tests`) defined directly in this file
  (Sections 2.6, 3.5, 5.3, 5.4).
- `tests/image_regression/support/CMakeLists.txt` (new): the
  `atlantis_image_regression_support` static library (Section 2.5).
- `tests/image_regression/fixture/CMakeLists.txt` (new): the
  `atlantis_image_regression_fixture` static library (Section 3.2).
- `tests/image_regression/golden_generator/CMakeLists.txt` (new): the
  standalone, non-CTest-registered
  `atlantis_image_regression_golden_generator` executable (Section 3.4).
- `.gitignore` (modify): the machine-local
  `current_environment.sidecar.txt` exclusion (Section 3.7).

## Sequencing & Dependencies

Unlike Plan 0010, this plan touches no already-implemented abstract
interface — every type and function introduced here is brand new, so no
step forces splitting a pure-virtual declaration from its concrete
overrides across a step boundary. Sequencing is close to linear:

1. **Step 1 — `stb` dependency (Section 1):** `cmake/AtlantisDependencies.cmake`
   only. Ends compilable (a successful CMake configure); nothing yet
   consumes `Stb::Stb`.
2. **Step 2 — GPU-independent support library (Sections 2.1–2.7):**
   `pixel_diff`, `png_codec`, `provenance`, `golden_validity`, their
   shared `CMakeLists.txt`, `tests/image_regression/CMakeLists.txt`'s
   first line, and the root `CMakeLists.txt` wiring. Ends compilable;
   `Atlantis::ImageRegressionSupport` builds as a library with no
   consumer yet.
3. **Step 3 — Fixture library and golden generator (Sections 3.1–3.7):**
   depends on Step 2. Ends compilable; the golden generator tool exists
   and can be run manually, but no golden has been captured yet.
4. **Step 4 — First committed golden (Section 4):** depends on Step 3,
   requires real hardware. Not a compilation step — its own "ends
   compilable" criterion is trivially satisfied (no code changes), but
   it must complete before Step 5's GPU-required test can be written
   meaningfully.
5. **Step 5 — Automated tests (Sections 5.1–5.4):** 5.1 (GPU-independent)
   depends only on Step 2 and could technically land before Step 3/4;
   5.2/5.4 (GPU-required) depend on Step 3 (fixture) and Step 4 (a real
   golden to compare against). Grouped into one step here because
   splitting the GPU-independent and GPU-required test additions across
   separate Implementation-PR boundaries would leave `ctest -L gpu`
   referencing a source file (`image_regression_gpu_tests.cpp`) that
   does not exist yet for no compilation benefit — both land together.
   Ends compilable; `ctest -LE gpu` passes immediately; `ctest -L gpu`
   passes on real hardware (Step 6's job to actually run and record).
6. **Step 6 — Verification (Section 6):** depends on Step 5 in full.
7. **Step 7 — Documentation (Section 7):** depends on Step 6.

A single Implementation PR landing Steps 1–7 together (matching Spec
0010's own single-PR-per-spec precedent) is this plan's expected shape;
splitting into multiple PRs is a Plan-Review-confirmable choice — unlike
Plan 0010, no step here is a large, non-subdividable bundle, so a
narrower split (e.g., Steps 1–2 as one PR, 3–7 as a second) is a
genuinely available option if Plan Review prefers it.

## Files / Modules Touched (expected)

**New:**
`tests/image_regression/CMakeLists.txt`,
`tests/image_regression/support/{CMakeLists.txt,pixel_diff.{h,cpp},png_codec.{h,cpp},provenance.{h,cpp},golden_validity.{h,cpp}}`,
`tests/image_regression/fixture/{CMakeLists.txt,minimal_cube_fixture.{h,cpp}}`,
`tests/image_regression/golden_generator/{CMakeLists.txt,main.cpp}`,
`tests/image_regression/{pixel_diff_tests.cpp,png_codec_tests.cpp,provenance_tests.cpp,golden_validity_tests.cpp,image_regression_gpu_tests.cpp}`,
`tests/image_regression/current_environment.sidecar.txt.example`,
`tests/image_regression/goldens/minimal_cube/{minimal_cube_512x512_rgba8unorm.png,minimal_cube_512x512_rgba8unorm.sidecar.txt}`
(Section 4, a separate commit from every other file in this list, per
ADR-0042's same-PR ordering rule).

**Modified:**
`cmake/AtlantisDependencies.cmake`,
`CMakeLists.txt` (root),
`.gitignore`,
`specs/README.md` (post-implementation, Section 7).

**Explicitly not touched, confirmed by this plan's own research:**
any file under `src/rhi/`, `src/render_graph/`, `src/renderer/`,
`src/vulkan_backend/`, `src/shader_system/`, `src/platform/`,
`src/tools/`, `shaders/`; any existing `examples/*` or `tests/{core,
platform,rhi,vulkan_backend,render_graph,renderer,shader_system,
tools}/*` file — this plan's own fixture (Section 3.1) is a
deliberate, disclosed duplication of
`examples/headless_rendering_demo/main.cpp`'s fixture values, not a
shared-code refactor of that file, which therefore needs no edit.

If Implementation touches a file not listed here, that is a deviation to
call out explicitly in the Implementation PR, not to slip in silently
(per this plan's own template and AGENTS.md).

## Verification Checklist

- [ ] Unit tests (GPU-independent, `ctest -LE gpu`, Debug **and**
      Release): Section 5.1's new cases pass, no new warning introduced,
      every pre-existing GPU-independent test elsewhere unaffected.
- [ ] Headless integration tests (GPU-required, `ctest -L gpu`, Debug
      **and** Release): Section 5.2's new cases pass on real
      Vulkan-capable hardware, with `current_environment.sidecar.txt`
      populated.
- [ ] Image regression tests: **this plan is what makes this test layer
      exist for the first time** — Section 5.2's own coverage is the
      answer, not a future deferral (mirroring Plan 0010's identical
      framing for headless integration tests).
- [ ] Golden validity check exercised for all four failure modes
      independently (Section 5.1's `golden_validity_tests.cpp`).
- [ ] Provenance-mismatch diagnostic confirmed separate from pass/fail
      (Section 5.2 item 5) — a mismatch never changes a `TEST_CASE`'s
      own `REQUIRE`-driven outcome.
- [ ] Deliberate rendering regression confirmed caught (Section 6),
      performed once manually, reverted before the Implementation PR
      opens — evidence (before/after `ComparisonReport` values) recorded
      in the Implementation PR description.
- [ ] Vulkan Validation Layers clean: for every GPU-touching test and
      the golden generator's own real run (Section 4), in both Debug and
      Release — zero warnings, zero errors.
- [ ] Manual/local verification record (Section 6) — hardware/driver/
      Vulkan version, pass/fail per test, deliberate-regression
      confirmation — written into the Implementation PR.
- [ ] Explicit Prohibitions checklist (above) fully checked.
- [ ] `git diff --check` clean on every commit.

## Rollback Plan

Steps 1–7 (Sequencing & Dependencies) are each independently revertible
in reverse order (7 → 1) without touching an earlier, already-verified
step — `git revert` of the Implementation PR's commit(s) in
reverse-chronological order restores the pre-Plan state exactly, since
every file this plan touches is either brand new (Steps 1–5, safe to
delete outright) or a small, additive, easily-reversible edit to an
existing file (`cmake/AtlantisDependencies.cmake`'s appended block,
root `CMakeLists.txt`'s one new `add_subdirectory()` line, `.gitignore`'s
one new entry) — no existing test, example, or `src/` file's own
behavior is changed by any step here. Two narrower rollback points:

- If the problem is isolated to the golden generator or the first
  committed golden (Sections 3.3–3.7, 4): revert only those files/
  commits; Sections 1–2's support library and Section 5.1's
  GPU-independent tests remain valid and unaffected (they test the
  comparison algorithm against synthetic buffers, not the real golden).
- If the problem is isolated to `stb`/PNG codec specifically (Sections
  1, 2.2): revert those two sections; every other section's own code
  fails to compile once `png_codec.h`'s API disappears — this is
  **not** an independently-revertible narrower point on its own (Section
  2.2 is a true dependency root for everything downstream), listed here
  only to name where the fault would actually originate if PNG
  encode/decode itself is the problem, not to claim it can be reverted
  alone without also reverting Sections 2.3–7.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- "Image regression tests added/updated if rendered output changed" —
  **N/A for this plan's own diff**: this plan adds the image-regression
  *harness itself*; it does not change any existing rendered output.
- "CI green" — **N/A, no CI pipeline exists** (Spec 0011's own
  Non-Goals); the Verification Checklist's manual/local gate is this
  plan's actual Definition-of-Done gate, per Spec 0011's own explicit
  design.

## Human Review / Plan Review Blockers

**No architectural gap requiring a return to Spec/ADR was found while
producing this plan.** Every decision below is an implementation-shape
detail within the boundaries Spec 0011 and ADR-0041/ADR-0042 already
fixed — none requires a new public API, module boundary, dependency
beyond `stb`, or ownership/threading model.

**Design choices flagged for Plan Review; genuinely different,
defensible options exist; this plan does not pick silently:**

1. **Golden/sidecar path resolution — repository-root-relative vs.
   build-output-relative** (Section 5.4): `atlantis_image_regression_gpu_tests`
   sets `WORKING_DIRECTORY` to its own build output directory (matching
   every prior GPU-test/demo's shader-loading convention), but this
   plan's own test code opens golden/sidecar paths relative to the
   **repository root**, not that working directory — a real,
   deliberately disclosed inconsistency with the shader-path
   convention. Confirm this split (shaders build-output-relative,
   goldens repository-root-relative) is acceptable, or direct a single,
   uniform resolution strategy (e.g., an absolute path computed from a
   `CMAKE_SOURCE_DIR`-derived compile definition, matching how some
   projects inject a source-root constant — not currently done anywhere
   in this repository, so would be a small, new pattern).
2. **`png_codec_tests.cpp`'s 3-channel/16-bit test PNG fixtures**
   (Section 5.1): generate them programmatically at test time (via
   `stb_image_write`'s own `comp = 3`/16-bit write path, exercised
   read-only for test-fixture generation, not through the plan's own
   `encodePng()` which is RGBA8-only) vs. a small, checked-in binary
   test-fixture PNG pair under
   `tests/image_regression/support/test_fixtures/`. The former needs no
   new checked-in binary; the latter is more obviously "a real file,
   not synthesized," at the cost of two small binary files in the repo.
   This plan defaults to the programmatic option; confirm or redirect.
3. **`current_environment.sidecar.txt`'s exact discovery path from
   within the golden generator and the GPU test** (Section 3.3/5.2):
   this plan fixes it as `tests/image_regression/current_environment.sidecar.txt`,
   resolved relative to the process's current working directory — which
   means, per Blocker 1 above, the golden generator (run from the
   repository root per Section 4's own invocation example) and the
   GPU test (run from its own build output directory, per Section 5.4's
   `WORKING_DIRECTORY`) would need to resolve this **same** logical path
   two different ways unless a single, uniform resolution strategy is
   picked. This is the same underlying question as Blocker 1, not a
   second, independent one — flagged separately here only because it
   surfaces at two different call sites.
4. **Whether a distinct CI/test-category label for image-regression GPU
   tests is needed**, separate from the existing `gpu`-labeled pattern —
   Spec 0006/0007/0010 each flagged this as open and left it open; this
   plan does the same (Section 5.4 uses plain `"gpu"`) unless Plan
   Review wants it resolved now.

**Non-blocking, disclosed limitations carried into Implementation:**

- The golden generator's `git status --porcelain`/`git rev-parse HEAD`
  subprocess calls (Section 3.3) assume `git` is on the invoking
  process's `PATH` — true for every development/CI environment this
  project has used so far (the entire Spec → Plan → Implementation
  workflow itself depends on `git`), not a new assumption this plan
  introduces.
- `EnvironmentProvenance`'s values are human-transcribed (from
  `vulkaninfo --summary` or equivalent), not machine-verified against
  the actual running process's real Vulkan instance/device — a
  disclosed, accepted gap given RHI's public API exposes no query for
  this data and this plan does not add one (Critical Architectural
  Boundaries). A future spec wanting an automated, self-verifying
  provenance capture would need its own RHI API addition, decided
  through its own Spec/ADR, not implied or pre-designed here.
- This plan's `ComparisonReport`/`GoldenValidityError` types are
  scoped to exactly this plan's one covered scene (`minimal_cube`); no
  multi-scene registry/lookup mechanism is designed, since Spec 0011
  itself covers exactly one scene (Non-Goals: "Additional covered
  scenes beyond the one reused fixture are each a future spec's or
  Plan's own scope").
