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
  **License, verified against that `LICENSE` file's actual content, not
  assumed from reputation:** dual MIT / Unlicense (public-domain-
  equivalent), user's choice — matching
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)'s
  own "License and attribution" analysis exactly. `FetchContent` fetches
  the header files themselves, including each file's own embedded
  license footer, so no separate `NOTICE`/attribution file is added by
  this step — none is required by either license option.
  **`ATLANTIS_BUILD_TESTS` scoping, confirmed by reading root
  `CMakeLists.txt` directly:** `include(cmake/AtlantisDependencies.cmake)`
  is called only inside that file's own `if(ATLANTIS_BUILD_TESTS)` block
  (immediately after `enable_testing()`), so this new `stb`
  `FetchContent_Declare`/`FetchContent_MakeAvailable` pair — like the
  existing Catch2 one right above it — is fetched only when
  `-DATLANTIS_BUILD_TESTS=ON` (the default). A configure with
  `-DATLANTIS_BUILD_TESTS=OFF` fetches neither Catch2 nor `stb`. This
  step adds no new gating logic of its own; it inherits the existing
  boundary by living in the same, already-conditionally-included file.
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

  // ADR-0042's own "Failure output" contract: a per-pixel absolute
  // difference visualization, amplified for visibility. Same
  // preconditions/shape-assertion as compareBuffers() above. Output
  // pixel = min(255, diff * kDiffAmplificationFactor) per channel --
  // an all-black diff image (every pixel exactly 0,0,0,255) is the
  // visual signal "no difference here." Pure function -- writes
  // nothing to disk itself; see golden_validity.h's
  // writeFailureArtifacts() (2.4) for the disk-writing step.
  inline constexpr int kDiffAmplificationFactor = 16;
  [[nodiscard]] PixelBuffer computeDiffVisualization(const PixelBuffer& actual, const PixelBuffer& golden);

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
  vulkan_requested_instance_api_version: 1.3.0
  vulkan_physical_device_api_version: 1.4.335
  extent_width: 512
  extent_height: 512
  format: Rgba8Unorm
  ```
  **`vulkan_requested_instance_api_version` corrected during this Plan
  Review round, from an earlier draft's incorrect `1.4.357` (the
  *loader's* version, wrongly copied into this field) to `1.3.0` —
  verified directly against
  `src/vulkan_backend/src/instance_api_version.cpp`'s actual
  `decideRequestedInstanceApiVersion()` body: this codebase's Vulkan
  Backend requests **exactly** `VK_API_VERSION_1_3` when the loader
  reports `>= 1.3` (true for `1.4.357`), or exactly
  `VK_API_VERSION_1_0` otherwise — never the loader's own version, and
  never the physical device's. This field's real value is one of
  exactly two closed-set possibilities (`"1.3.0"` or `"1.0.0"`),
  **entirely derived from the loader version by this documented rule —
  not independently observable via `vulkaninfo` or any other external
  tool**, unlike every other field in this format. Section 3.6's own
  human-facing template repeats this derivation rule explicitly, so a
  human filling in a new machine's environment file does not need to
  read Vulkan Backend source to get it right.

  Exactly 13 lines, exactly this field order, one field per line,
  `^<field_name>: <value>$` (a **single space** after the colon; no
  leading/trailing whitespace in `<value>`), UTF-8, encoded and parsed
  with the following precise rules — closing every ambiguity a plain
  "key: value" description would otherwise leave open:
  - **Delimiter, precisely:** each line is matched by an **anchored
    prefix**, `line.rfind(expectedFieldName + ": ", 0) == 0` (C++
    `std::string::rfind` with position `0`, i.e. "starts with"), never
    by scanning the line for the first/any `:` character. This is why
    `capture_date`'s own ISO 8601 value (which itself contains two
    colons, e.g. `2026-08-17T00:00:00Z`) is never ambiguous: the parser
    already knows, from the line's fixed position, which field name to
    expect there, and simply strips that exact, known prefix — it never
    searches for a delimiter at all.
  - **No embedded newlines, ever, in any field value — this format has
    no escaping mechanism for one.** Every one of the 13 fields' real
    values (dates, hashes, hardware names, dotted version numbers,
    decimal integers, the fixed `Format` enumerator-name set) is
    single-line by its own nature; if a future field's legitimate value
    ever needed one, that would require a `schema_version` bump and a
    new escaping rule, decided through its own Plan/Spec revision — not
    silently supported now.
  - **Line-ending tolerance:** the parser accepts **either** `\n` or
    `\r\n` as a line terminator (splits on `\n`, then strips one
    trailing `\r` from each resulting line if present) — this is
    deliberately more lenient than the format's own canonical `\n`-only
    *write* form (`serializeGoldenProvenance()`/
    `serializeEnvironmentProvenance()` always emit `\n`, never `\r\n`).
    Reading-side tolerance exists specifically because
    `current_environment.sidecar.txt` (3.6/3.7) is git-ignored — never
    protected by any repository-level line-ending normalization — and a
    human editing it with a plain Windows text editor could easily save
    it with `\r\n` without intending any format deviation; this is not
    a case a strict parser should reject.
  - **`schema_version`** is always line 1; a parser that finds any value
    other than the literal `1` there rejects the file outright as an
    unrecognized/future schema (`Err(UnknownSchemaVersion)`), rather
    than guessing at what a different version might mean.
  - **The three Vulkan version fields are format-validated, not merely
    read as opaque strings:** each of
    `vulkan_requested_instance_api_version`/
    `vulkan_physical_device_api_version` must match
    `^[0-9]+\.[0-9]+\.[0-9]+$` exactly (decimal, no leading zeros, no
    fourth "variant" component — matching `vulkaninfo --summary`'s own
    display convention, which this codebase's own disclosed hardware
    records, e.g. ADR-0042's Context, already follow; Phase 1 has never
    observed a nonzero Vulkan "variant" field, and this format does not
    attempt to represent one). `vulkan_loader_api_version` accepts that
    same pattern **or** the literal token `unavailable` — no other
    value is accepted for any of the three. A value failing this check
    is `Err(MalformedValue)`. This closes a real, otherwise-silent risk:
    without validation, a same-hardware comparison could report a false
    `PROVENANCE MISMATCH` purely from inconsistent formatting (e.g. a
    stray leading zero) between how the golden generator writes a value
    and how a human transcribes another, rather than from any real
    environment difference.
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
  // verbatim: (1) both pngPath and sidecarPath exist as files, and the
  // PNG decodes (Err(MissingPngFile)/Err(MissingSidecarFile)/
  // Err(PngDecodeFailed), each distinct); (2) decoded properties
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

  enum class ArtifactWriteError { ActualPngWriteFailed, DiffPngWriteFailed };

  // ADR-0042's own "Failure output" contract, the disk-writing half
  // (computeDiffVisualization(), 2.1, is the pure pixel-math half).
  // Writes <outputDir>/<goldenSlug>_actual.png and
  // <outputDir>/<goldenSlug>_diff.png, creating outputDir if it does
  // not exist. goldenSlug scopes the two output filenames by golden
  // name (e.g. "minimal_cube_512x512_rgba8unorm") so a future
  // multi-scene run cannot have one scene's failure artifacts
  // overwrite another's. Always overwrites any pre-existing file at
  // either path -- these are transient diagnostic artifacts, never
  // protected by the "never overwrite a golden" rule, which applies
  // only to tests/image_regression/goldens/ (ADR-0042's own golden-
  // regeneration boundary, unaffected by this function).
  [[nodiscard]] atlantis::Result<std::monostate, ArtifactWriteError> writeFailureArtifacts(
      const std::filesystem::path& outputDir, const std::string& goldenSlug, const PixelBuffer& actual,
      const PixelBuffer& golden);

  }  // namespace atlantis::image_regression
  ```
  **PNG intrinsic/ancillary metadata (`gAMA`/`sRGB`/`iCCP`/`cHRM`, or
  any other chunk) is neither read nor validated anywhere in this
  pipeline** — a deliberate scope boundary, not an oversight.
  `decodePng()` (2.2) uses only `stb_image`'s basic RGBA pixel-decode
  path, which does not expose ancillary-chunk presence at all; a golden
  PNG's raw, decoded pixel bytes are the sole source of truth for both
  the validity check above and the comparison itself, matching
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)'s
  own "no color-profile interpretation on either side" decision exactly
  — this pipeline compares stored bytes correctly regardless of whether
  a stray ancillary chunk is present, it simply never inspects or warns
  about one.
- **Dependency order:** after 2.1, 2.2, 2.3.
- **Tests after this step:** see 5.1 (each of the four validity-check
  steps' failure mode exercised independently against constructed test
  fixtures — a missing file, a real 3-channel PNG, a 16-bit PNG, a
  sidecar with a deliberately wrong extent — plus `writeFailureArtifacts()`
  producing two correctly-named, decodable PNG files in a constructed
  temporary output directory).
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
- **Output — invocation contract, fixed by this step, resolved
  Plan-Review Blocker 1/3 (see below): every path this tool touches is
  either an absolute path computed at CMake configure time and injected
  as a compiler-definition string constant, or built from one — never
  resolved against the process's current working directory, and never
  found via directory search:**
  ```
  atlantis_image_regression_golden_generator.exe <golden-name>
  ```
  One required positional argument, e.g. `minimal_cube/minimal_cube_512x512_rgba8unorm`
  (no `.png` extension — the tool appends `.png`/`.sidecar.txt` itself),
  always resolved as `<ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR>/<golden-name>.png`
  — `ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR` is a `target_compile_definitions()`
  string literal (Section 3.4) equal to
  `${CMAKE_SOURCE_DIR}/tests/image_regression/goldens`, so this
  resolution is identical regardless of the invoking shell's own working
  directory. No other flags. On any argument-count mismatch, prints
  usage and exits `2`.

  **Sequence:**
  1. Shell out to `git status --porcelain` (via `_popen`, MSVC/Windows
     CRT — no new dependency, the same tier of "already-available OS/
     compiler facility" `CreateProcessW` is in `shader_compiler`'s own
     precedent, just a lighter-weight API sufficient for this simpler
     case: two fixed, literal, non-user-influenced command strings, no
     injection surface). **Three distinct outcomes, never conflated:**
     - `_popen()` itself returns `nullptr` (process failed to launch —
       e.g. `git` is not on `PATH`) → refuse, print "failed to invoke
       git — confirm it is installed and on PATH", exit `1`. **A failed
       launch is never silently treated as "no output, tree is clean"**
       — that would be exactly the dangerous false-negative this
       three-way split exists to prevent.
     - The pipe opens but `_pclose()`'s return value indicates `git`
       itself exited non-zero (e.g. invoked outside any git repository)
       → refuse, print `git status --porcelain` exit code and any
       captured stderr, exit `1`.
     - `git` exits zero with **non-empty** stdout → refuse, print
       "working tree is not clean; commit or stash changes before
       regenerating a golden", exit `1`.
     Only a zero exit **and** empty stdout counts as "clean," and only
     then does the tool proceed. **This is the "require clean source
     revision" enforcement point, and — not merely incidentally — the
     mechanical enforcement of ADR-0042's same-PR commit-ordering rule**
     (Section 4): the tool physically cannot run against a rendering
     change that exists only in the working tree, uncommitted: it must
     already be committed first, or this step refuses.
  2. Shell out to `git rev-parse HEAD` (same mechanism, same three-way
     launch-failure/nonzero-exit/success split as step 1 — a launch
     failure or nonzero exit here is likewise a hard `exit 1`, never an
     empty-string fallback for `sourceRevision`); its trimmed stdout
     becomes `Provenance::sourceRevision`.
  3. Run `git ls-files --error-unmatch tests/image_regression/current_environment.sidecar.txt`
     (same mechanism; exit code alone matters, output is discarded). A
     **zero** exit means the file is tracked by git despite the
     `.gitignore` entry (3.7) — e.g. force-added at some point in the
     past — and this step refuses: print "current_environment.sidecar.txt
     must never be committed; found tracked in git — run `git rm
     --cached tests/image_regression/current_environment.sidecar.txt`
     before proceeding", exit `1`. A **non-zero** exit (the expected,
     normal case) means the file is not tracked, and this step proceeds.
  4. Read `ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE` (a
     `target_compile_definitions()` string literal, Section 3.4, equal
     to `${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt`
     — the same absolute-path mechanism the golden-name argument above
     uses, never a search across candidate directories) via
     `parseEnvironmentProvenance()` (2.3). Missing or malformed →
     refuse, print the expected path and format, exit `1` — the tool
     never fabricates a plausible-looking but unverified environment
     record.
  5. `setUpMinimalCubeFixture()` + `renderOneFrame()` (3.1). Any `Err` →
     print the error, exit `1`.
  6. Build the full `Provenance` (2.3): `captureDate` = current UTC time
     (`<chrono>`, `std::chrono::system_clock::now()`, ISO 8601), the
     `sourceRevision`/environment fields from steps 2/4, `extentWidth`/
     `extentHeight` = 512, `format` = `"Rgba8Unorm"`.
  7. If the target `.png` already exists, decode its current sidecar
     (if present) and print an old-vs-new provenance summary to stdout
     (every field that differs, named) — **visibility, not a second
     confirmation gate**; the tool proceeds to overwrite regardless, its
     own deliberate, separate-binary invocation already being the
     safety boundary ADR-0042 requires (no interactive prompt, so the
     tool remains scriptable).
  8. `encodePng(...)` (2.2) to `<golden-name>.png`, then write the
     sidecar text (`serializeGoldenProvenance()`, 2.3) to
     `<golden-name>.sidecar.txt` (same directory, same stem). Either
     write failing → print the error, exit `1`.
  9. Print a success summary (path, provenance) to stdout, exit `0`.

  Every exit path calls `Device::waitIdle()` before any RAII-owned
  Vulkan resource in `MinimalCubeFixture` is destroyed (matching
  `examples/headless_rendering_demo`'s own established teardown
  discipline) — including every early-exit path above (argument
  mismatch, git failures of any kind, dirty tree, tracked-environment-
  file, missing/malformed environment file), none of which ever
  construct a `MinimalCubeFixture` at all and so have nothing to wait
  on.
- **Dependency order:** after 3.1, 3.2, 2.5 (2.2/2.3's `encodePng()`/
  `serializeGoldenProvenance()`); lands in the same changeset as 3.4
  (the two compile-definition macros this file's own code reads).
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

  # Resolves Plan-Review Blocker 1/3: absolute, configure-time-computed
  # paths, immune to whatever directory this tool happens to be invoked
  # from -- never a CWD-relative path, never a directory search. The
  # same two definitions are applied to atlantis_image_regression_gpu_tests
  # (Section 5.4), so both consumers resolve identically.
  target_compile_definitions(atlantis_image_regression_golden_generator PRIVATE
    ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
    ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
  )

  # Same build-tree Shader-System-produced artifact set every prior
  # headless/windowed demo/test copies -- this tool renders the
  # identical minimal_mesh shader pair (Section 3.1). Shader loading
  # (unlike the two paths above) keeps this repository's existing,
  # already-established WORKING_DIRECTORY + relative-path convention
  # unchanged -- see Section 5.4's own note on why these two path
  # strategies deliberately differ.
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
- **Dependency order:** lands in the same changeset as 3.3 — `main.cpp`
  reads both compile-definition macros this file defines.
- **Stop condition / rollback:** revert this file alone; 3.3 fails to
  compile without it (same-changeset pair, not independently revertible
  once both exist).

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
  not the real, machine-specific file the tooling reads — see 3.7), 8
  lines, exactly this field order (matching `parseEnvironmentProvenance()`'s
  own strict, anchored-prefix, position-checked parsing — Section 2.3
  — the same rules as the golden sidecar format, just this narrower
  7-field-plus-schema-version set):
  ```
  schema_version: 1
  gpu_vendor: Intel
  gpu_model: Intel(R) Arc(TM) B370 GPU
  driver_version: 101.8509
  os_build: Windows 11 Home, Build 26200
  vulkan_loader_api_version: 1.4.357
  vulkan_requested_instance_api_version: 1.3.0
  vulkan_physical_device_api_version: 1.4.335
  ```
  A short header comment (as a `.example` file, not itself parsed, plain
  Markdown-adjacent prose is fine) explains, **field by field, exactly
  where each value comes from** — not merely "run `vulkaninfo
  --summary`," since one field is not directly observable there:
  - Copy this file to `current_environment.sidecar.txt` in the same
    directory (git-ignored, Section 3.7) — once per development/CI
    machine that will run `atlantis_image_regression_gpu_tests` or the
    golden generator, not once per invocation.
  - Run `vulkaninfo --summary` on that machine.
  - `gpu_vendor`/`gpu_model`/`driver_version`: from that device's own
    `vendorID`/`deviceName`/`driverInfo` lines.
  - `os_build`: from `winver` or Windows Settings' own "About" page —
    not part of `vulkaninfo`'s own output.
  - `vulkan_loader_api_version`: `vulkaninfo`'s own top-level "Vulkan
    Instance Version" line — or the literal token `unavailable` if
    `vulkaninfo` itself reports a pre-1.1 loader with no such line at
    all.
  - `vulkan_physical_device_api_version`: that device's own `apiVersion`
    line.
  - **`vulkan_requested_instance_api_version` is not read from
    `vulkaninfo` at all — it is derived from the `vulkan_loader_api_version`
    value directly above by a fixed, two-outcome rule this codebase's
    own Vulkan Backend already hardcodes** (verified against
    `src/vulkan_backend/src/instance_api_version.cpp`'s
    `decideRequestedInstanceApiVersion()`): write `1.3.0` if the loader
    version is `>= 1.3.0` (true for essentially every currently-shipping
    Windows Vulkan driver), otherwise write `1.0.0`. **Never copy the
    loader's own version into this field** — that was an error in an
    earlier draft of this plan, corrected in Section 2.3's own example
    after being checked against the real implementation.
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
  atlantis_image_regression_golden_generator.exe minimal_cube/minimal_cube_512x512_rgba8unorm
  ```
  from any directory (the golden-name argument resolves against the
  compile-time-injected `ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR`, never
  the invoking shell's own working directory — Section 3.3), against a
  **clean working tree at the commit that lands Sections 1–3** (per
  ADR-0042's own same-PR ordering rule, and mechanically enforced by
  the tool's own `git status --porcelain` check, Section 3.3 step 1:
  Sections 1–3's code is committed first, this golden is captured
  against that already-existing commit, then the golden PNG + sidecar
  are added via a **separate, subsequent commit** — never the same
  commit as Sections 1–3's own code, and the tool physically refuses to
  run otherwise). The resulting PNG + sidecar are reviewed by a human as
  this scene's first "this is what correct output looks like today"
  baseline (the golden-update-reason category is **not** "rendering
  change" / "reference-environment change" / "approved rebaseline" — it
  is the harness's own bootstrap, stated as such in the commit/PR
  description) and committed.
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
    quantization claim). The deliberately-non-RGBA8 cases (3-channel;
    16-bit) are **generated programmatically at test run time, written
    to `std::filesystem::temp_directory_path()`, and removed again once
    the `TEST_CASE` finishes** (`std::filesystem::remove()` in a
    `SECTION`-scoped RAII guard, or Catch2's own test teardown — never
    a file checked into this repository): a direct
    `stbi_write_png(..., comp = 3, ...)` call for the 3-channel case
    (`stb_image_write`'s own API accepts a `comp` other than 4, this
    plan's own `encodePng()` just never exercises anything but `comp =
    4` in its own normal, non-test code path), and a minimal, valid,
    hand-constructed 16-bit grayscale PNG byte sequence for the
    bit-depth case (small enough — a handful of bytes — to embed as a
    `constexpr` byte array in the test file itself, needing no PNG
    *writer* support for 16-bit output at all, only `decodePng()`'s own
    *reader* path to reject it). Decoding either returns the expected
    `Err(ChannelCountMismatch)`/`Err(UnsupportedBitDepth)`. No new
    binary file is added to this repository for this purpose.
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
    steps' failure mode, independently: missing PNG; missing sidecar
    (PNG present, `.sidecar.txt` absent); a real (test-fixture)
    3-channel PNG; a 16-bit PNG; a sidecar whose recorded
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
- **Output:** every golden/environment-file/failure-artifact path this
  file's code uses comes from the same three
  `target_compile_definitions()` macros Section 5.4 injects into this
  target — `ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR`,
  `ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE`,
  `ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR` — never a path relative to this
  process's own working directory. Catch2 `TEST_CASE`s, tag
  `[image_regression][gpu]`, covering — precisely, per Spec 0011's own
  Testing & Verification Plan bullets for this layer:
  1. A full `setUpMinimalCubeFixture()` → `renderOneFrame()` →
     `loadAndValidateGolden()` → `compareBuffers()` cycle against the
     real, committed `minimal_cube` golden
     (`<ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR>/minimal_cube/minimal_cube_512x512_rgba8unorm.png`)
     passes (`ComparisonReport::passed == true`), Vulkan Validation
     Layers clean (this test's own device is created with
     `enableValidationLayers = true`, matching every prior GPU test in
     this repository). At this `TEST_CASE`'s own start, before anything
     else, removes any pre-existing
     `<ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR>/minimal_cube_512x512_rgba8unorm_{actual,diff}.png`
     left over from a previous failing run — a stale diagnostic
     artifact from an already-fixed problem must never linger and be
     mistaken for a current one. On failure, calls
     `writeFailureArtifacts()` (2.4) before the `REQUIRE` that reports
     the failure, so the artifacts exist on disk by the time a human
     reads the test output.
  2. The same cycle repeated 3 times against the same fixture (matching
     `examples/headless_rendering_demo`'s own `kCycleCount`) produces
     byte-for-byte identical `PixelBuffer`s each time — the automated,
     permanent form of the determinism verification this plan's
     calibration evidence (ADR-0042's own Context) already performed
     once, ad hoc and uncommitted, during Spec 0011's own review.
  3. Running `loadAndValidateGolden()` against a golden name this plan
     never commits a golden for (e.g. `"nonexistent_scene/nonexistent"`)
     returns `Err(MissingPngFile)` — reported as a distinct Catch2
     failure message prefixed `INVALID GOLDEN:`, not a crash, not a
     silent `SUCCEED()`.
  4. **Provenance-mismatch handling:** read
     `ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE` (skip this
     `TEST_CASE` — Catch2 `SKIP()` — with an explanatory message if the
     file is absent, since this specific case exists to test the
     mismatch-reporting path itself, not the core comparison, and
     cannot run meaningfully without a populated environment file);
     construct an `EnvironmentProvenance` with one field deliberately
     altered from what the golden's own sidecar records; confirm
     `compareProvenanceEnvironment()` returns exactly that one field as
     a `ProvenanceFieldDiff`, and confirm (by convention established in
     this test file, not by a shared helper) that logging this mismatch
     via `WARN(...)` does **not** cause the `TEST_CASE` to fail — only
     `compareBuffers()`'s own `passed` value drives the test's pass/fail
     `REQUIRE`.

  **Not a `TEST_CASE` in this file — a one-time, manual verification
  procedure, performed once during Implementation and recorded in the
  Implementation PR, then reverted (see Section 6's own checklist
  item):** proof that a deliberately introduced *rendering* regression
  is actually caught. This must be a **real source-code change to
  `minimal_cube_fixture.cpp` (e.g. its clear color or one vertex's
  position), followed by a real rebuild of
  `atlantis_image_regression_gpu_tests`, followed by a real re-run** —
  `renderOneFrame()` must actually produce different GPU-rendered pixels
  through the real render path, which `loadAndValidateGolden()` +
  `compareBuffers()` then genuinely catch. **Directly corrupting an
  in-memory `PixelBuffer` in test code before calling `compareBuffers()`
  does not satisfy this requirement** — that would prove only that
  `compareBuffers()`'s own arithmetic works on constructed inputs
  (already covered by 5.1's own `pixel_diff_tests.cpp`), not that the
  *rendering path itself*, end to end, produces a detectably different
  image when the scene actually changes. The fixture change is reverted
  immediately after this procedure, before the Implementation PR opens
  — matching Spec 0011's own established precedent for this exact kind
  of one-off, never-committed verification instrumentation (its
  empirical-calibration work, ADR-0042's Context).
- **Dependency order:** after 3.1–3.5 (fixture + golden generator's own
  libraries), 5.4 (the three compile-definition macros this file's own
  code reads), Section 4 (a real golden must exist on disk for item 1 to
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

  # Resolved Plan-Review Blocker 1/3 (see Section 3.4's identical
  # block, applied here to this target too): absolute, configure-time
  # paths for everything this executable's own test code needs to find
  # that is NOT the shader pair -- goldens, the machine-local
  # environment file, and where to write failure diagnostics. None of
  # the three depends on WORKING_DIRECTORY.
  target_compile_definitions(atlantis_image_regression_gpu_tests PRIVATE
    ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
    ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
    ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR="${CMAKE_BINARY_DIR}/image_regression_failures"
  )

  # Same build-tree shader artifact set as the golden generator
  # (Section 3.4) -- this executable renders the identical fixture.
  # WORKING_DIRECTORY above stays scoped to shader loading only,
  # matching this repository's existing, already-established
  # convention (every prior demo/GPU-test resolves its shader pair
  # relative to its own build output directory) -- deliberately not
  # extended to the three paths above, which use the
  # target_compile_definitions() mechanism instead precisely because
  # they must resolve identically regardless of which directory ctest
  # (or a human) happens to invoke this executable from.
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
  `ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR` (`${CMAKE_BINARY_DIR}/image_regression_failures`)
  is created on demand by `writeFailureArtifacts()` (2.4,
  `std::filesystem::create_directories()`) the first time a comparison
  actually fails — not created unconditionally by this build step, so
  an all-passing run leaves no empty directory behind either.
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
- **Deliberate-regression-caught proof** (5.2's own final, unnumbered
  paragraph — not a permanent `TEST_CASE`): performed once, manually,
  during this Plan's own Implementation, as a **real source change,
  real rebuild, real re-run** — never by corrupting an in-memory buffer
  in test code — temporarily alter `minimal_cube_fixture.cpp`'s clear
  color (or one vertex position), rebuild
  `atlantis_image_regression_gpu_tests`, re-run it, confirm item 1 now
  fails with a nonzero `outOfToleranceCount` and a written
  `_actual.png`/`_diff.png` pair under
  `ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR`, then **revert the temporary
  change** (matching this project's own established pattern for this
  exact kind of one-off, never-committed verification instrumentation —
  see Spec 0011's own calibration precedent) before the Implementation
  PR is opened. Evidence (the before/after `ComparisonReport` values,
  and confirmation the fixture change was reverted) recorded in the
  Implementation PR description.
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
- [ ] `git grep -rn "ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR\|ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE\|ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR"
      tests/image_regression/*.cmake tests/image_regression/CMakeLists.txt
      tests/image_regression/*/CMakeLists.txt` shows all three macros
      defined exactly where Sections 3.4/5.4 specify, and
      `image_regression_gpu_tests.cpp`/`golden_generator/main.cpp` use
      only these macros (never a literal
      `"tests/image_regression/goldens"`-shaped string, never a
      `std::filesystem::current_path()` call) to locate goldens, the
      environment file, or the failure-artifact output directory.
- [ ] `golden_generator/main.cpp` contains a `git ls-files
      --error-unmatch`-based (or equivalent) check confirming
      `current_environment.sidecar.txt` is not tracked before reading
      it — Section 3.3 step 3's safeguard is actually present, not
      merely described.
- [ ] No new binary file (image or otherwise) is added under
      `tests/image_regression/` beyond the one golden PNG + sidecar
      pair Section 4 commits — `png_codec_tests.cpp`'s own non-RGBA8
      test fixtures are generated at run time into
      `std::filesystem::temp_directory_path()`, never checked in
      (Section 5.1).
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
producing or revising this plan.** Every decision below (and every fix
in this Plan Review round) is an implementation-shape or data-accuracy
correction within the boundaries Spec 0011 and ADR-0041/ADR-0042
already fixed — none requires a new public API, module boundary,
dependency beyond `stb`, or a change to Spec 0011/ADR-0041/ADR-0042's
own approved methodology.

**Resolved during this Plan Review round, no longer open:**

- ~~Golden/sidecar/environment-file path resolution — repository-root-
  relative vs. build-output-relative~~ (formerly Blockers 1 and 3) —
  **resolved**: none of the three is resolved against the invoking
  process's working directory at all. `ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR`,
  `ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE`, and
  `ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR` are `target_compile_definitions()`
  string constants, computed once at CMake configure time from
  `CMAKE_SOURCE_DIR`/`CMAKE_BINARY_DIR` (Sections 3.4, 5.4) and applied
  identically to the golden generator and the GPU test — no implicit
  directory search, no dependency on `WORKING_DIRECTORY`, which stays
  scoped to shader loading only (the one path category every prior
  demo/GPU-test already resolves that way, left unchanged).
- ~~`png_codec_tests.cpp`'s 3-channel/16-bit test PNG fixtures~~ —
  **resolved**: generated programmatically at test run time into
  `std::filesystem::temp_directory_path()` and removed at teardown; no
  binary test fixture is checked into this repository (Section 5.1).
- ~~Whether a distinct CI/test-category label for image-regression GPU
  tests is needed~~ — **resolved**: no. This plan continues to reuse the
  existing plain `"gpu"` `LABELS` property (Section 5.4) — the same
  label Spec 0006/Spec 0010's own GPU tests already use — introducing no
  new CTest label. Spec 0006/0007/0010's own open question about
  whether a *more granular* label might someday be useful remains
  theirs to resolve, not force-closed here, but this plan itself makes
  no new label.
- ~~`vulkan_requested_instance_api_version`'s example value~~ — **found
  and corrected during this Plan Review round**: an earlier draft
  copied the *loader's* reported version (`1.4.357`) into this field.
  Verified against `src/vulkan_backend/src/instance_api_version.cpp`'s
  actual `decideRequestedInstanceApiVersion()` body: this codebase's
  Vulkan Backend requests exactly `1.3.0` (when the loader supports
  `>= 1.3`) or exactly `1.0.0` (otherwise) — a closed, two-outcome,
  loader-version-derived value, never equal to the loader's own version
  in this example's case. Corrected in Sections 2.3 and 3.6, with an
  explicit derivation rule so a human filling in a new machine's
  environment file does not need to read Vulkan Backend source to get
  it right.

**Non-blocking, disclosed limitations carried into Implementation:**

- The golden generator's `git status --porcelain`/`git rev-parse HEAD`/
  `git ls-files` subprocess calls (Section 3.3) assume `git` is on the
  invoking process's `PATH` — true for every development/CI environment
  this project has used so far (the entire Spec → Plan → Implementation
  workflow itself depends on `git`), not a new assumption this plan
  introduces. Unlike an earlier draft of this plan, a failure to even
  *launch* `git` is no longer silently treated as "no output, tree is
  clean" — Section 3.3 now enumerates launch-failure, nonzero-exit, and
  dirty-tree as three distinct, never-conflated outcomes.
- `EnvironmentProvenance`'s values are human-transcribed (from
  `vulkaninfo --summary` or equivalent, per Section 3.6's now-explicit,
  field-by-field mapping — with one field, the requested instance
  version, instead *derived* by a documented rule rather than
  transcribed at all), not machine-verified against the actual running
  process's real Vulkan instance/device — a disclosed, accepted gap
  given RHI's public API exposes no query for this data and this plan
  does not add one (Critical Architectural Boundaries). A future spec
  wanting an automated, self-verifying provenance capture would need
  its own RHI API addition, decided through its own Spec/ADR, not
  implied or pre-designed here.
- This format's sidecar parser has no escaping mechanism for a field
  value containing an embedded newline (Section 2.3) — not a gap for
  any of this plan's own 13+8 fields, all single-line by nature; a
  future field that genuinely needed one would require its own
  `schema_version` bump and escaping rule, decided through a future
  Plan/Spec revision, not silently supported now.
- This plan's `ComparisonReport`/`GoldenValidityError` types are
  scoped to exactly this plan's one covered scene (`minimal_cube`); no
  multi-scene registry/lookup mechanism is designed, since Spec 0011
  itself covers exactly one scene (Non-Goals: "Additional covered
  scenes beyond the one reused fixture are each a future spec's or
  Plan's own scope").
