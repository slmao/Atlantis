# Plan: Image Regression Testing Foundation

- **Spec:** [specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md) (`Approved`)
- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Human Review Approval (2026-08-17):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), following **one independent, read-only
  Plan Review round** (agent-performed) conducted against this Plan's
  drafted text and the real shipped implementation
  (`cmake/AtlantisDependencies.cmake`,
  `src/vulkan_backend/src/instance_api_version.cpp`,
  `examples/headless_rendering_demo/`, `tests/vulkan_backend/`) and one
  independent, read-only Human Review — see
  [PR #51](https://github.com/slmao/Atlantis/pull/51) for the full
  revision history.

  The **Plan Review round** found and directly fixed: a data-accuracy bug
  in the sidecar example (`vulkan_requested_instance_api_version` had
  incorrectly copied the loader's own reported version — corrected against
  `instance_api_version.cpp`'s actual `decideRequestedInstanceApiVersion()`
  body, which requests exactly `1.3.0` or `1.0.0`, never the loader's
  version); an ADR-0042-required actual/diff failure-artifact-writing step
  that was cited but never specified (added
  `computeDiffVisualization()`/`writeFailureArtifacts()`); underspecified
  sidecar delimiter/escaping/CRLF/version-format rules (fully specified —
  anchored-prefix field matching, no embedded newlines, `\n`/`\r\n` read
  tolerance, `^[0-9]+\.[0-9]+\.[0-9]+$` version validation); a golden
  generator that could have silently treated a failed `git` invocation as
  "clean tree" (fixed to a three-way launch-failure/nonzero-exit/dirty-tree
  split, plus a `git ls-files --error-unmatch` tracking safeguard for
  `current_environment.sidecar.txt`); and an ambiguous
  "deliberately-caught regression" proof (tightened to require a real
  fixture source-code change, a real rebuild, and a real re-run through
  the actual render path — explicitly excluding in-memory buffer
  corruption, moved out of the permanent `TEST_CASE` list into its own
  one-time procedure). All four originally-flagged open design questions
  were resolved directly in that round (golden/environment-file/failure-
  artifact path resolution via `target_compile_definitions()` string
  constants computed once at CMake configure time from
  `CMAKE_SOURCE_DIR`/`CMAKE_BINARY_DIR`, independent of the invoking
  process's working directory; `png_codec_tests.cpp`'s non-RGBA8 fixtures
  generated at test run time into `std::filesystem::temp_directory_path()`
  and removed at teardown, no binary fixture checked in;
  `current_environment.sidecar.txt`'s path resolution via the same
  compile-time-constant mechanism; and no distinct CI/test-category label
  for image-regression GPU tests — the existing plain `"gpu"` `LABELS`
  property continues to be reused).

  The **Human Review** cross-checked the revised Plan against Approved
  Spec 0011, Accepted ADR-0041/ADR-0042, and the actual current
  repository state — re-verifying the `stb`
  commit/hash/license/`ATLANTIS_BUILD_TESTS` scoping, the
  `decideRequestedInstanceApiVersion()` correction, the `atlantis::rhi::Format`
  enumerator set the sidecar validates against, the reused
  `examples/headless_rendering_demo` fixture's exact identifiers
  (`kCubeVertices`, `kCubeIndices`, `kExtentPixels`, `kColorFormat`,
  `kCycleCount`), the shader-artifact-copying
  `target_compile_definitions()`/`WORKING_DIRECTORY` split, and ADR-0006's
  "release tag or commit hash" pinning language — and found no remaining
  Must Fix or Should Fix issue, plus one non-blocking observation: an
  incidental `Vk*`-shaped substring inside an explanatory *comment* in
  `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`
  (`VkSwapchainKHR`/`VkSurfaceKHR`, naming what that file deliberately
  does *not* use) will also match the Explicit Prohibitions checklist's
  `git grep -rn "Vk[A-Z]"` command textually — Implementation should read
  that checklist item as scoped to real code references, not explanatory
  comments.

  **This approval covers:** the full implementation scope after the Plan
  Review round — Sections 1–7 in full; the Step 1–7 sequencing (including
  Step 5's bundling of the GPU-independent and GPU-required test
  additions); the disposition of all four originally-flagged open design
  questions; and all verification gates in "Verification Checklist"
  (GPU-independent and GPU-required test layers in both Debug and Release,
  Vulkan Validation Layers clean throughout, the
  deliberate-regression-caught proof — a real fixture source-code change,
  a real rebuild, a real re-run, reverted before the Implementation PR
  opens — and the recorded manual/local verification gate). Human Review
  is a distinct gate: with Spec 0011 `Approved` and this Plan `Approved`,
  that joint gate is satisfied. **Implementation may now begin against
  this Plan, strictly as written**, with any real-world deviation called
  out explicitly in the Implementation PR; this approval record does not
  authorize skipping the Verification step this Plan's own checklist
  defines, and does not authorize merging any PR on the human's behalf.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #150](https://github.com/slmao/Atlantis/pull/150) Batch 3. Original
  scope, ordered work, and verification retained. Candidate C++ headers,
  struct bodies, and CMake fragments drafted here (Sections 1–5) are
  summarised to their contracts and preserved in
  [PR #51](https://github.com/slmao/Atlantis/pull/51) history. Every
  concrete type/function name, file split, and struct layout in Sections
  1–7 was a **candidate** for Plan Review at drafting time and is now the
  approved basis for implementation — Spec 0011 and ADR-0041/ADR-0042 fix
  *behavior*; this Plan proposed the concrete C++.

## Objective

Implement Spec 0011's approved design: a golden-image comparison harness
built entirely on Spec 0010's unmodified `Renderer` → RenderGraph → RHI →
Vulkan Backend → `OffscreenTarget`/readback path
([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)–[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)),
adding PNG golden storage/provenance
([ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)),
a strict per-pixel comparison algorithm, a dedicated golden validity
check, provenance-mismatch detection, and a standalone golden regeneration
tool
([ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md))
— entirely new code under `tests/image_regression/`, touching no RHI,
RenderGraph, Renderer, or Vulkan Backend public API.

## Authoritative Sources

Read in full before implementing any step:
[specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md)
(`Approved`); [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
(`Accepted` — PNG format, `stb_image`/`stb_image_write` dependency, the
full usage contract); [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
(`Accepted` — comparison algorithm (channel tolerance 0, failing-pixel
budget 0), the golden validity check, source-revision provenance,
provenance-mismatch behavior, golden regeneration's locked architecture,
golden-update-reason categories, the bounded sidecar-encoding contract,
the `tests/image_regression/` test-ownership boundary);
[ADR-0006](../adr/0006-dependency-management.md) (`Accepted` —
dependency-acquisition policy; **verified by reading
`cmake/AtlantisDependencies.cmake` directly:** this repository's actual
`FetchContent` mechanism for Catch2 uses `URL`/`URL_HASH` (a plain HTTPS
tarball GET + SHA-256 pin), not `GIT_REPOSITORY`/`GIT_TAG` — this
environment resets git's smart-HTTP clone protocol, while a plain HTTPS
GET succeeds; this Plan follows the same proven mechanism for `stb`, with
`URL` pointed at a specific commit's archive as the pin);
[docs/process/testing-strategy.md](../process/testing-strategy.md),
[definition-of-done.md](../process/definition-of-done.md); the
existing implementation cited per step:
`cmake/AtlantisDependencies.cmake`, `CMakeLists.txt` (root),
`examples/headless_rendering_demo/{main.cpp,CMakeLists.txt}`,
`tests/vulkan_backend/{headless_rendering_gpu_tests.cpp,CMakeLists.txt}`,
`src/rhi/include/atlantis/rhi/{types,device,offscreen_target,command_list,buffer}.h`,
`src/renderer/include/atlantis/renderer/renderer.h`,
`src/tools/shader_compiler/` (process-launching precedent, referenced for
the golden generator's own `git` subprocess calls — not linked against).

## Critical Architectural Boundaries (preserved, not re-decided here)

None of the following is open for reinterpretation during this Plan or its
Implementation; a step that appears to require reopening one must stop and
escalate (see Human Review / Plan Review Blockers):

- **No RHI, RenderGraph, Renderer, or Vulkan Backend public API changes,
  anywhere, for any reason.** Every new type/function this Plan introduces
  is confined to `tests/image_regression/`.
- **No `Vk*` type, no Vulkan header, referenced anywhere under
  `tests/image_regression/`.** Every GPU-touching operation goes through
  Atlantis's own existing public API
  (`atlantis::rhi::Device`/`OffscreenTarget`/`CommandList`/`Buffer`,
  `atlantis::vulkan_backend::createDevice()`,
  `atlantis::renderer::Renderer`) — the same discipline
  `tests/vulkan_backend/headless_rendering_gpu_tests.cpp` already follows.
- **No new top-level module.** `tests/image_regression/` is a test-suite
  area parallel to `tests/core/`, `tests/rhi/`, `tests/render_graph/`,
  `tests/renderer/`, `tests/vulkan_backend/` — not an addition to
  [AGENTS.md](../../AGENTS.md)'s module list.
- **Channel tolerance = 0, failing-pixel budget = 0 — not configurable at
  a call site, not a runtime parameter.** Baked in as named constants
  internal to the comparison function (Section 2.1), per ADR-0042 — no API
  accepts a caller-supplied override.
- **No second image-codec or serialization/parsing dependency beyond
  `stb_image`/`stb_image_write`.** The sidecar format (Section 2.3) is
  hand-rolled, dependency-free, using only the C++ standard library.
- **No reuse of `src/shader_system/`'s private JSON parser, and no
  dependency of `tests/image_regression/` on any other module's private
  implementation.**
- **Golden regeneration is never reachable from an ordinary `ctest -L
  gpu` run.** The golden generator (Section 3) is a plain
  `add_executable()`, never passed to `catch_discover_tests()`, never a
  registered CTest test.
- **No golden file is written by any code path this Plan adds except the
  golden generator's own explicit, human-invoked write.** The GPU-required
  comparison test (Section 5.2) only ever reads
  `tests/image_regression/goldens/`.

## Non-Goals (confirmed matching Spec 0011)

Not implemented by this Plan, in any form: automated CI-enforced gating
(no `.github/workflows/*.yml`, no CI runner provisioning); Android, iOS,
or Linux support/build configuration of any kind; per-GPU-vendor golden
sets or any cross-vendor stability claim; percentage-based or perceptual
(SSIM) tolerance; automatic/silent golden acceptance or rebaseline; any
new scene beyond the one reused `examples/headless_rendering_demo` cube
fixture; any change to `Renderer`'s output or any rendering feature; a
general image-loading/asset-pipeline capability (the `stb` dependency
stays scoped to `tests/image_regression/`'s own targets).

---

## 1. `stb` Dependency Integration

### 1.1 `cmake/AtlantisDependencies.cmake` (modify)

Append, after the existing `FetchContent_Declare(Catch2 URL ... URL_HASH
...)` block, a `FetchContent_Declare(stb ...)` using the same `URL`/
`URL_HASH` mechanism (not `GIT_TAG`) this file already uses for Catch2:
`URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz`,
`URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515`,
`DOWNLOAD_EXTRACT_TIMESTAMP TRUE`; then `FetchContent_MakeAvailable(stb)`.
`stb` ships no `CMakeLists.txt` of its own (it is a pair of single-header
libraries), so wrap its fetched source directory in a plain `INTERFACE`
target: `add_library(stb INTERFACE)`,
`target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})`,
`add_library(Stb::Stb ALIAS stb)` — matching this repository's
`<Vendor>::<Lib>` alias style.

- **Commit/hash provenance, verified:** `2c980bb59875b0d32144a71867fbdebb2f77cd20`
  is `nothings/stb`'s actual `master`-branch HEAD as of this Plan's
  drafting (`stb_image.h` v2.30, `stb_image_write.h` v1.16), confirmed via
  `gh api repos/nothings/stb/commits/master`. The `URL_HASH` was computed
  by downloading this exact archive URL and running `sha256sum` against it
  directly (not copied from a second-hand source); the archive's contents
  were independently confirmed (via `tar -tzf`) to contain `stb_image.h`,
  `stb_image_write.h`, and `LICENSE` at the expected top-level path.
- **License, verified against the `LICENSE` file's actual content:** dual
  MIT / Unlicense (public-domain-equivalent), user's choice — matching
  ADR-0041's "License and attribution" analysis. `FetchContent` fetches
  the header files themselves, including each file's own embedded license
  footer, so no separate `NOTICE`/attribution file is added; none is
  required by either license option.
- **`ATLANTIS_BUILD_TESTS` scoping, confirmed by reading root
  `CMakeLists.txt`:** `include(cmake/AtlantisDependencies.cmake)` is
  called only inside that file's own `if(ATLANTIS_BUILD_TESTS)` block, so
  this new `stb` pair — like the existing Catch2 one right above it — is
  fetched only when `-DATLANTIS_BUILD_TESTS=ON` (the default). A configure
  with `-DATLANTIS_BUILD_TESTS=OFF` fetches neither Catch2 nor `stb`. This
  step adds no new gating logic; it inherits the existing boundary.
- **Dependency order:** first — every other step depends on `Stb::Stb`
  existing. A successful `cmake -S . -B build` is this step's pass/fail
  signal (a hash mismatch fails configure immediately and loudly).
- **Rollback:** revert this file alone; nothing references `Stb::Stb`
  until Section 2.4.

---

## 2. GPU-Independent Support Library

New static library `Atlantis::ImageRegressionSupport` (CMake target
`atlantis_image_regression_support`), under
`tests/image_regression/support/` — depends only on `Atlantis::Core` and
`Stb::Stb`; no RHI, RenderGraph, Renderer, or Vulkan Backend dependency
anywhere in this section. This is
[testing-strategy.md](../process/testing-strategy.md)'s
"GPU-independent... exercised against synthetic in-memory buffers... no
Vulkan device" layer.

### 2.1 New: `support/pixel_diff.h` / `.cpp`

(`atlantis::image_regression` namespace.)

- **`PixelBuffer`** — `{ std::uint32_t width = 0; std::uint32_t height =
  0; std::vector<std::uint8_t> rgba8; }` — tightly packed RGBA8, `width *
  height * 4` bytes (the exact layout `OffscreenTarget`'s readback
  `Buffer` and a decoded PNG both already share; ADR-0040 / ADR-0041's "no
  vertical flip").
- **`ComparisonReport`** — `{ bool passed = false; std::uint32_t
  maxChannelDiff = 0; double meanAbsoluteDiff = 0.0; std::uint64_t
  outOfToleranceCount = 0; double outOfTolerancePercentage = 0.0; }`.
- **`inline constexpr std::uint8_t kChannelTolerance = 0;` and `inline
  constexpr std::uint64_t kFailingPixelBudget = 0;`** — ADR-0042: not
  configurable, no function below accepts a caller-supplied override.
- **`ComparisonReport compareBuffers(const PixelBuffer& actual, const
  PixelBuffer& golden)`** — preconditions: `actual`/`golden` widths and
  heights match (format/extent mismatch is the caller's job to check
  first, via `golden_validity.h`); this function asserts matching shape
  (`ATLANTIS_CHECK`, not a `Result` — a shape mismatch reaching it is a
  caller precondition violation). Body: single pass over every
  pixel/channel, `std::abs(int(actual) - int(golden))`; a pixel is "out of
  tolerance" if **any** channel's diff exceeds `kChannelTolerance`
  (currently 0 → any nonzero diff); `passed = (outOfToleranceCount <=
  kFailingPixelBudget)` (currently 0 → `passed` iff `outOfToleranceCount
  == 0`).
- **`inline constexpr int kDiffAmplificationFactor = 16;` and `PixelBuffer
  computeDiffVisualization(const PixelBuffer& actual, const PixelBuffer&
  golden)`** — ADR-0042's "Failure output" contract: a per-pixel absolute
  difference visualization, `output pixel = min(255, diff *
  kDiffAmplificationFactor)` per channel; an all-black diff image (every
  pixel exactly `0,0,0,255`) is the visual signal "no difference here".
  Pure function — writes nothing to disk itself (see 2.4's
  `writeFailureArtifacts()`). Same preconditions/shape-assertion as
  `compareBuffers()`.

### 2.2 New: `support/png_codec.h` / `.cpp`

- **`png_codec.cpp` is the ONE translation unit in this repository that
  defines `STB_IMAGE_IMPLEMENTATION` and `STB_IMAGE_WRITE_IMPLEMENTATION`**
  (and includes `<stb_image.h>`/`<stb_image_write.h>`). No other file may
  define either.
- **`enum class PngDecodeError { FileNotFound, DecodeFailed,
  ChannelCountMismatch, UnsupportedBitDepth };`** and **`enum class
  PngEncodeError { WriteFailed };`**.
- **`DecodedPng`** — `{ PixelBuffer pixels; int channelsInFile = 0; bool
  is16Bit = false; }` — `channelsInFile` is `stb`'s own `channels_in_file`
  out-parameter (the file's real as-encoded channel count, independent of
  the forced 4-channel *output buffer* — ADR-0041's "why forcing 4
  channels is not enough"). Both diagnostic fields are already validated
  by the time this struct is returned `Ok` — callers do not need to
  re-check them.
- **`Result<DecodedPng, PngDecodeError> decodePng(const
  std::filesystem::path& path)`** — decodes with `desired_channels = 4`
  (always); additionally reads `stb`'s `channels_in_file` out-parameter
  and calls `stbi_is_16_bit_from_memory()` (or the `_from_file` variant);
  returns `Err(ChannelCountMismatch)` if `channels_in_file != 4`, or
  `Err(UnsupportedBitDepth)` if 16-bit — these checks run even though
  decode "succeeded" per `stb`'s return value, since a forced-4-channel
  decode of a real-3-channel file is exactly the silent-masking failure
  mode ADR-0041 requires catching.
- **`Result<std::monostate, PngEncodeError> encodePng(const
  std::filesystem::path& path, const PixelBuffer& pixels)`** — never calls
  `stbi_flip_vertically_on_write()`; writes `pixels.rgba8` exactly as
  given, row 0 first (ADR-0041's row-order contract).
  `Result<std::monostate, E>` for the success case matches the
  established Core precedent (`Device::waitIdle()`, Spec 0008's
  `atlantis_shader_compiler_lib`) for a `Result` carrying no value on
  success.
- **Dependency order:** after 1.1 (`Stb::Stb`), 2.1 (`PixelBuffer`).

### 2.3 New: `support/provenance.h` / `.cpp`

The sidecar's exact flat format, fixed by this step — **exactly 13 lines,
exactly this field order, one field per line, `^<field_name>: <value>$` (a
single space after the colon; no leading/trailing whitespace in
`<value>`), UTF-8**:

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

- **`vulkan_requested_instance_api_version`** is `1.3.0` in this example —
  verified against `src/vulkan_backend/src/instance_api_version.cpp`'s
  actual `decideRequestedInstanceApiVersion()`: this codebase's Vulkan
  Backend requests **exactly** `VK_API_VERSION_1_3` when the loader
  reports `>= 1.3` (true for `1.4.357`), or exactly `VK_API_VERSION_1_0`
  otherwise — never the loader's own version, never the physical device's.
  Its real value is one of exactly two closed-set possibilities
  (`"1.3.0"` or `"1.0.0"`), **entirely derived from the loader version by
  this documented rule — not independently observable via `vulkaninfo`**,
  unlike every other field. Section 3.6's human-facing template repeats
  this derivation rule explicitly.
- **Encoding and parsing rules** (closing every ambiguity a plain "key:
  value" description would leave open):
  - **Delimiter:** each line is matched by an **anchored prefix**,
    `line.rfind(expectedFieldName + ": ", 0) == 0` — never by scanning for
    the first/any `:`. This is why `capture_date`'s ISO 8601 value (two
    colons) is never ambiguous: the parser already knows, from the line's
    fixed position, which field name to expect and strips that exact known
    prefix.
  - **No embedded newlines, ever, in any field value — this format has no
    escaping mechanism for one.** Every one of the 13 fields' real values
    is single-line by nature; a future field needing one would require a
    `schema_version` bump and a new escaping rule, decided through its own
    Plan/Spec revision.
  - **Line-ending tolerance:** the parser accepts **either** `\n` or
    `\r\n` (splits on `\n`, then strips one trailing `\r` per line if
    present) — deliberately more lenient than the format's canonical
    `\n`-only *write* form (`serializeGoldenProvenance()`/
    `serializeEnvironmentProvenance()` always emit `\n`). Reading-side
    tolerance exists because `current_environment.sidecar.txt` (3.6/3.7)
    is git-ignored — never protected by repository line-ending
    normalization — and a human editing it with a plain Windows text
    editor could save it with `\r\n` without intending a format deviation.
  - **`schema_version`** is always line 1; any value other than the
    literal `1` there rejects the file outright as an unrecognized/future
    schema (`Err(UnknownSchemaVersion)`).
  - **The three Vulkan version fields are format-validated, not read as
    opaque strings:** each of `vulkan_requested_instance_api_version`/
    `vulkan_physical_device_api_version` must match `^[0-9]+\.[0-9]+\.[0-9]+$`
    exactly (decimal, no leading zeros, no fourth "variant" component —
    Phase 1 has never observed a nonzero Vulkan "variant" field).
    `vulkan_loader_api_version` accepts that same pattern **or** the
    literal token `unavailable`. A value failing this check is
    `Err(MalformedValue)`. This closes a real, otherwise-silent risk: a
    same-hardware comparison reporting a false `PROVENANCE MISMATCH`
    purely from inconsistent formatting (e.g. a stray leading zero)
    between how the golden generator writes a value and how a human
    transcribes another.
- **`Provenance`** — `{ captureDate; sourceRevision; gpuVendor; gpuModel;
  driverVersion; osBuild; vulkanLoaderApiVersion (dotted string, or
  "unavailable"); vulkanRequestedInstanceApiVersion (dotted string);
  vulkanPhysicalDeviceApiVersion (dotted string); std::uint32_t
  extentWidth; std::uint32_t extentHeight; format (matches an
  `atlantis::rhi::Format` enumerator name) }`.
- **`EnvironmentProvenance`** — the narrower schema
  `current_environment.sidecar.txt` uses (Section 3.2): the same 8
  hardware/environment-identity fields (`gpuVendor`, `gpuModel`,
  `driverVersion`, `osBuild`, and the three Vulkan version fields) plus
  `schema_version`, no `capture_date`/`source_revision`/`extent`/`format`
  (those describe a specific *capture*, not the machine itself).
- **`enum class ProvenanceParseError { WrongLineCount, UnknownSchemaVersion,
  FieldNameMismatch, MalformedValue };`**.
- **`Result<Provenance, ProvenanceParseError>
  parseGoldenProvenance(const std::string&)`**, **`std::string
  serializeGoldenProvenance(const Provenance&)`**, and the
  `parseEnvironmentProvenance()`/`serializeEnvironmentProvenance()` pair.
  Parsing is **strict**: `Err` on any deviation — wrong line count, a line
  not matching its expected field name at that exact position, an empty
  value, a non-numeric `extent_width`/`extent_height`, or an unrecognized
  `format` value (must be a known string set: `"Unknown"`, `"Bgra8Unorm"`,
  `"Bgra8Srgb"`, `"Rgba8Unorm"`, `"Rgba8Srgb"`, matching `types.h`'s
  `Format` enum by name — compared by name, this file has no RHI
  dependency).
- **`ProvenanceFieldDiff`** — `{ fieldName; goldenValue; currentValue; }`.
- **`std::vector<ProvenanceFieldDiff> compareProvenanceEnvironment(const
  Provenance& golden, const EnvironmentProvenance& current)`** — compares
  golden's 7 hardware/environment fields (`gpuVendor` through
  `vulkanPhysicalDeviceApiVersion`) against `current` — never
  `captureDate`/`sourceRevision`/`extent`/`format`, which describe the
  capture event. Empty return == full match.

### 2.4 New: `support/golden_validity.h` / `.cpp`

- **`enum class GoldenValidityError { MissingPngFile, MissingSidecarFile,
  PngDecodeFailed, ChannelCountMismatch, UnsupportedBitDepth,
  SidecarMalformed, SidecarFormatExtentMismatch };`** (each wrapping the
  corresponding `PngDecodeError`/`ProvenanceParseError` where applicable).
- **`ValidatedGolden`** — `{ PixelBuffer pixels; Provenance provenance;
  }`.
- **`Result<ValidatedGolden, GoldenValidityError> loadAndValidateGolden(const
  std::filesystem::path& pngPath, const std::filesystem::path&
  sidecarPath)`** — the four-step check, in this exact order, matching
  ADR-0042 verbatim: (1) both `pngPath` and `sidecarPath` exist as files
  and the PNG decodes (`Err(MissingPngFile)`/`Err(MissingSidecarFile)`/
  `Err(PngDecodeFailed)`, each distinct); (2) decoded properties
  (`channelsInFile == 4`, not 16-bit) satisfy the RGBA8 contract; (3) the
  sidecar's own recorded format/extent matches the PNG's actual decoded
  width/height/format; (4) the sidecar found alongside `pngPath` is
  structurally the one for it (same basename stem, per Section 2.6's
  naming convention). Returns `Err` at the first failing step — never
  partially populates `ValidatedGolden` on a failure path.
- **`enum class ArtifactWriteError { ActualPngWriteFailed, DiffPngWriteFailed
  };`**.
- **`Result<std::monostate, ArtifactWriteError> writeFailureArtifacts(const
  std::filesystem::path& outputDir, const std::string& goldenSlug, const
  PixelBuffer& actual, const PixelBuffer& golden)`** — ADR-0042's "Failure
  output" contract, the disk-writing half. Writes
  `<outputDir>/<goldenSlug>_actual.png` and
  `<outputDir>/<goldenSlug>_diff.png`, creating `outputDir` if it does not
  exist. `goldenSlug` scopes the two output filenames by golden name so a
  future multi-scene run cannot have one scene's failure artifacts
  overwrite another's. Always overwrites any pre-existing file at either
  path — transient diagnostic artifacts, never protected by the "never
  overwrite a golden" rule (which applies only to
  `tests/image_regression/goldens/`).
- **PNG intrinsic/ancillary metadata (`gAMA`/`sRGB`/`iCCP`/`cHRM`, any
  other chunk) is neither read nor validated anywhere in this pipeline** —
  a deliberate scope boundary. `decodePng()` uses only `stb_image`'s basic
  RGBA pixel-decode path; a golden PNG's raw, decoded pixel bytes are the
  sole source of truth (ADR-0041's "no color-profile interpretation on
  either side").
- **Dependency order:** after 2.1, 2.2, 2.3.

### 2.5 New: `support/CMakeLists.txt`

`add_library(atlantis_image_regression_support STATIC pixel_diff.cpp
png_codec.cpp provenance.cpp golden_validity.cpp)`;
`target_include_directories(... PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`;
`target_link_libraries(... PUBLIC Atlantis::Core PRIVATE Stb::Stb
atlantis_compiler_warnings)`; `add_library(Atlantis::ImageRegressionSupport
ALIAS atlantis_image_regression_support)`. `Stb::Stb` is `PRIVATE`: only
`png_codec.cpp` includes the `stb` headers; no consumer needs `stb`'s
include directory on its path, matching ADR-0041's "linked only into
test-support targets" scoping down to the file level.

### 2.6 New: `tests/image_regression/CMakeLists.txt`

Top-level file for this test area; this step adds only
`add_subdirectory(support)`. (Sections 3, 5 append more.)

### 2.7 Root `CMakeLists.txt` (modify)

Inside the existing `if(ATLANTIS_BUILD_TESTS)` block, add
`add_subdirectory(tests/image_regression)`, positioned after the existing
`tests/vulkan_backend` line.

---

## 3. Fixture Library and Golden Generator Tool

### 3.1 New: `fixture/minimal_cube_fixture.h` / `.cpp`

Input: `examples/headless_rendering_demo/main.cpp`, read in full — its own
`kCubeVertices`, `kCubeIndices`, `kExtentPixels` (512), `kColorFormat`
(`Rgba8Unorm`), `lookAt()`/`perspective()` math, and shader-loading path
(`shaders/minimal_mesh.{vert,frag}.spv` + `.refl.json`), **copied
byte-for-byte** — this Plan's fixture must produce pixel-identical output
to the fixture the calibration evidence cited in ADR-0042's Context was
captured against, or that evidence no longer applies. This duplication
(not a shared cross-example library — Plan 0010 Section 7.1's own
precedent) is deliberate and load-bearing.

- **`MinimalCubeFixture`** — `{ std::unique_ptr<atlantis::rhi::Device>
  device; std::optional<atlantis::renderer::Mesh> mesh;
  std::optional<atlantis::renderer::Material> material;
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer; }`.
- **`inline constexpr std::uint32_t kFixtureExtentPixels = 512;` and
  `inline constexpr atlantis::rhi::Format kFixtureColorFormat =
  atlantis::rhi::Format::Rgba8Unorm;`**.
- **`enum class FixtureSetupError { DeviceCreationFailed, ShaderLoadFailed,
  ResourceCreationFailed };`**.
- **`Result<MinimalCubeFixture, FixtureSetupError>
  setUpMinimalCubeFixture()`** — constructs every long-lived resource once
  (mirrors `headless_rendering_demo`'s own setup sequence). Must be called
  with the process's current working directory set to a location where
  `"shaders/minimal_mesh.{vert,frag}.spv"` resolves (the same
  relative-path convention every prior demo/GPU-test uses — see 3.4/3.5's
  `WORKING_DIRECTORY` wiring).
- **`enum class FixtureRenderError { AcquireFailed, CommandListCreationFailed,
  SubmitFailed, WaitIdleFailed };`**.
- **`Result<PixelBuffer, FixtureRenderError> renderOneFrame(MinimalCubeFixture&
  fixture)`** — one full acquire → draw → copy → submit → `waitIdle()`
  cycle (Spec 0010's flow, unchanged), returning the readback buffer's
  contents as a `PixelBuffer`. May be called more than once against the
  same `MinimalCubeFixture` (`OffscreenTarget`'s repeated-cycle contract,
  ADR-0038) — each call independent. Its body mirrors
  `headless_rendering_demo/main.cpp`'s per-cycle loop body exactly (same
  fixed `lookAt()`/`perspective()` values, no per-call variation,
  `Renderer::drawFrame(..., ResourceState::TransferSource)`, the
  caller-built copy-pass graph, `submit()`, `waitIdle()`, read
  `readbackBuffer->mappedData()` into a returned `PixelBuffer`), factored
  into a reusable function instead of inlined in a `main()` loop.
- No GPU-independent test double is introduced for this — it is GPU-
  required setup/render code, exercised for real only by 3.3/5.2 (matching
  Spec 0010's precedent that headless composition code is verified via
  real GPU tests).

### 3.2 New: `fixture/CMakeLists.txt`

`add_library(atlantis_image_regression_fixture STATIC
minimal_cube_fixture.cpp)`;
`target_include_directories(... PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`;
`target_link_libraries(... PUBLIC Atlantis::RHI Atlantis::Renderer
Atlantis::ImageRegressionSupport PRIVATE Atlantis::VulkanBackend
Atlantis::RenderGraph Atlantis::ShaderSystem
Atlantis::ShaderSystemRhiIntegration atlantis_compiler_warnings)`;
`add_library(Atlantis::ImageRegressionFixture ALIAS ...)`.
`Atlantis::VulkanBackend` stays `PRIVATE`: consumers construct a `Device`
via `atlantis::vulkan_backend::createDevice()` (a free-function call, not
a re-exported type) — matching `examples/headless_rendering_demo`'s
dependency shape.

### 3.3 New: `golden_generator/main.cpp`

Input: ADR-0042's "Golden regeneration" and "Source revision, precisely"
Decision text; `src/tools/shader_compiler/`'s `CreateProcessW`-based
launcher as prior art for "this project already shells out to an external
process from a dev tool" — not linked against; a new, much smaller
implementation (two fixed literal command strings, no argv
construction/escaping complexity).

**Invocation contract:** `atlantis_image_regression_golden_generator.exe
<golden-name>` — one required positional argument, e.g.
`minimal_cube/minimal_cube_512x512_rgba8unorm` (no `.png` extension — the
tool appends `.png`/`.sidecar.txt` itself), always resolved as
`<ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR>/<golden-name>.png`, where
`ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR` is a `target_compile_definitions()`
string literal (Section 3.4) equal to
`${CMAKE_SOURCE_DIR}/tests/image_regression/goldens` — resolution
identical regardless of the invoking shell's working directory. No other
flags. On any argument-count mismatch, print usage and exit `2`.

**Sequence:**

1. Shell out to `git status --porcelain` (via `_popen`, MSVC/Windows CRT —
   no new dependency; a lighter-weight API than `CreateProcessW`,
   sufficient for two fixed, literal, non-user-influenced command
   strings). **Three distinct outcomes, never conflated:** `_popen()`
   returns `nullptr` (process failed to launch, e.g. `git` not on `PATH`)
   → refuse, print "failed to invoke git — confirm it is installed and on
   PATH", exit `1` (**a failed launch is never silently treated as "no
   output, tree is clean"** — exactly the dangerous false-negative this
   split prevents); the pipe opens but `_pclose()` indicates `git` exited
   non-zero (e.g. outside a git repository) → refuse, print the exit code
   and any captured stderr, exit `1`; `git` exits zero with **non-empty**
   stdout → refuse, print "working tree is not clean; commit or stash
   changes before regenerating a golden", exit `1`. Only a zero exit
   **and** empty stdout counts as "clean". This is the "require clean
   source revision" enforcement point, and the mechanical enforcement of
   ADR-0042's same-PR commit-ordering rule (Section 4): the tool cannot
   run against a rendering change that exists only in the working tree.
2. `git rev-parse HEAD` (same mechanism, same three-way split — a launch
   failure or nonzero exit is likewise a hard `exit 1`, never an
   empty-string fallback); its trimmed stdout becomes
   `Provenance::sourceRevision`.
3. `git ls-files --error-unmatch
   tests/image_regression/current_environment.sidecar.txt` (same
   mechanism; exit code alone matters). A **zero** exit means the file is
   tracked by git despite the `.gitignore` entry (3.7) → refuse, print
   "current_environment.sidecar.txt must never be committed; found tracked
   in git — run `git rm --cached ...` before proceeding", exit `1`. A
   **non-zero** exit (the expected case) means the file is not tracked;
   proceed.
4. Read `ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE` (a
   `target_compile_definitions()` string literal, Section 3.4, equal to
   `${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt`
   — the same absolute-path mechanism, never a directory search) via
   `parseEnvironmentProvenance()` (2.3). Missing or malformed → refuse,
   print the expected path and format, exit `1` — the tool never
   fabricates a plausible-looking but unverified environment record.
5. `setUpMinimalCubeFixture()` + `renderOneFrame()` (3.1). Any `Err` →
   print the error, exit `1`.
6. Build the full `Provenance` (2.3): `captureDate` = current UTC time
   (`std::chrono::system_clock::now()`, ISO 8601), the
   `sourceRevision`/environment fields from steps 2/4, `extentWidth`/
   `extentHeight` = 512, `format` = `"Rgba8Unorm"`.
7. If the target `.png` already exists, decode its current sidecar (if
   present) and print an old-vs-new provenance summary to stdout (every
   field that differs, named) — **visibility, not a second confirmation
   gate**; the tool proceeds to overwrite regardless, its own deliberate,
   separate-binary invocation already being the safety boundary ADR-0042
   requires (no interactive prompt, so the tool remains scriptable).
8. `encodePng(...)` (2.2) to `<golden-name>.png`, then write the sidecar
   text (`serializeGoldenProvenance()`, 2.3) to
   `<golden-name>.sidecar.txt` (same directory, same stem). Either write
   failing → print the error, exit `1`.
9. Print a success summary (path, provenance) to stdout, exit `0`.

Every exit path calls `Device::waitIdle()` before any RAII-owned Vulkan
resource in `MinimalCubeFixture` is destroyed (matching
`examples/headless_rendering_demo`'s own teardown discipline) — including
every early-exit path above, none of which construct a `MinimalCubeFixture`
at all and so have nothing to wait on. Not automated (this tool is not
CTest-registered); exercised manually once in Section 4.

### 3.4 New: `golden_generator/CMakeLists.txt`

`add_executable(atlantis_image_regression_golden_generator main.cpp)`;
`target_link_libraries(... PRIVATE Atlantis::ImageRegressionFixture
Atlantis::ImageRegressionSupport atlantis_compiler_warnings)`;
`target_compile_definitions(... PRIVATE
ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt")`
— absolute, configure-time-computed paths, immune to whatever directory
this tool is invoked from, never a CWD-relative path, never a directory
search (the same two definitions are applied to
`atlantis_image_regression_gpu_tests` in 5.4, so both consumers resolve
identically). Plus `add_dependencies(... minimal_mesh_shaders)` and a
POST_BUILD `copy_if_different` of `minimal_mesh.{vert,frag}.spv` +
`.refl.json` from `${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}` into
`$<TARGET_FILE_DIR:...>/shaders/` — shader loading keeps this repository's
existing `WORKING_DIRECTORY` + relative-path convention unchanged (see
5.4's note on why these two path strategies deliberately differ).
**Deliberately no `catch_discover_tests()` call, no `LABELS "gpu"`
property, no `add_test()` of any kind** — the concrete enforcement of
"never registered with CTest". **Deliberately no `run_...` convenience
custom target** (this tool requires a per-invocation positional argument;
a parameterless target would need editing every use — a minor convenience
loss, accepted so no easy memorable target name invites casual/accidental
invocation). Lands in the same changeset as 3.3 (`main.cpp` reads both
compile-definition macros).

### 3.5 `tests/image_regression/CMakeLists.txt` (modify)

Append `add_subdirectory(fixture)` and `add_subdirectory(golden_generator)`.

### 3.6 New: `current_environment.sidecar.txt.example`

A checked-in **example/template** file (`.example` suffix, not the real,
machine-specific file the tooling reads — see 3.7), 8 lines, exactly the
`EnvironmentProvenance` field order (matching `parseEnvironmentProvenance()`'s
strict, anchored-prefix, position-checked parsing — Section 2.3):

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

A short header comment (a `.example` file, not itself parsed) explains,
**field by field, exactly where each value comes from** — not merely "run
`vulkaninfo --summary`", since one field is not directly observable
there: copy this file to `current_environment.sidecar.txt` in the same
directory (git-ignored, 3.7) — once per development/CI machine, not once
per invocation; run `vulkaninfo --summary`;
`gpu_vendor`/`gpu_model`/`driver_version` from that device's
`vendorID`/`deviceName`/`driverInfo`; `os_build` from `winver` or Windows
Settings' "About" page; `vulkan_loader_api_version` from `vulkaninfo`'s
"Vulkan Instance Version" line (or `unavailable` for a pre-1.1 loader with
no such line); `vulkan_physical_device_api_version` from that device's
`apiVersion` line; **`vulkan_requested_instance_api_version` is not read
from `vulkaninfo` at all — it is derived from `vulkan_loader_api_version`
by a fixed, two-outcome rule this codebase's Vulkan Backend hardcodes**
(verified against `decideRequestedInstanceApiVersion()`): write `1.3.0` if
the loader version is `>= 1.3.0` (true for essentially every currently-
shipping Windows Vulkan driver), otherwise `1.0.0` — **never copy the
loader's own version into this field.**

### 3.7 `.gitignore` (modify)

Append, in a new clearly-commented section:
`tests/image_regression/current_environment.sidecar.txt` (machine-local
Vulkan/GPU environment provenance — inherently specific to the machine it
was filled in on; would be actively wrong if committed and read on any
other machine; see `current_environment.sidecar.txt.example` for the
template and setup instructions).

---

## 4. First Committed Golden (operational, not a code step)

Input: Section 3's completed, built
`atlantis_image_regression_golden_generator`, run on the reference
Windows/Vulkan machine, with `current_environment.sidecar.txt` (3.6/3.7)
already populated with that machine's real, `vulkaninfo`-sourced values.

Run `atlantis_image_regression_golden_generator.exe
minimal_cube/minimal_cube_512x512_rgba8unorm` from any directory (the
golden-name argument resolves against the compile-time-injected
`ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR`, never the invoking shell's
working directory — Section 3.3), against a **clean working tree at the
commit that lands Sections 1–3** (per ADR-0042's same-PR ordering rule,
mechanically enforced by the tool's `git status --porcelain` check —
Sections 1–3's code is committed first, this golden is captured against
that already-existing commit, then the golden PNG + sidecar are added via
a **separate, subsequent commit** — never the same commit as Sections
1–3's code, and the tool physically refuses to run otherwise). The
resulting PNG + sidecar are reviewed by a human as this scene's first
"this is what correct output looks like today" baseline (the
golden-update-reason category is **not** "rendering change" /
"reference-environment change" / "approved rebaseline" — it is the
harness's own bootstrap, stated as such in the commit/PR description) and
committed. Requires real Windows/Vulkan hardware — not exercised by any
automated step. Rollback: delete the golden PNG + sidecar; no other code
depends on their *content* (only their *existence*, for 5.2 to have
something to compare against).

---

## 5. Automated Tests

### 5.1 New: GPU-independent test files under `tests/image_regression/`

Four new Catch2 files, tag `[image_regression]`, mirroring
`tests/vulkan_backend/`'s one-file-per-concern granularity, linking
`Atlantis::ImageRegressionSupport` + `Catch2::Catch2WithMain`:

- **`pixel_diff_tests.cpp`** — two identical buffers pass; a single
  differing pixel anywhere fails (confirming `kFailingPixelBudget == 0` is
  genuine, not merely small); a known, constructed set of differing pixels
  produces the expected
  `maxChannelDiff`/`meanAbsoluteDiff`/`outOfToleranceCount`/
  `outOfTolerancePercentage`.
- **`png_codec_tests.cpp`** — encode-then-decode round-trips a synthetic
  buffer byte-for-byte (confirms ADR-0041's no-added-quantization claim).
  The deliberately-non-RGBA8 cases (3-channel; 16-bit) are **generated
  programmatically at test run time, written to
  `std::filesystem::temp_directory_path()`, and removed again once the
  `TEST_CASE` finishes** (a `SECTION`-scoped RAII guard or Catch2 test
  teardown — never a file checked into this repository): a direct
  `stbi_write_png(..., comp = 3, ...)` for the 3-channel case
  (`stb_image_write`'s API accepts `comp` other than 4; `encodePng()` just
  never exercises anything but `comp = 4` in normal code), and a minimal,
  valid, hand-constructed 16-bit grayscale PNG byte sequence embedded as a
  `constexpr` byte array in the test file for the bit-depth case (needs no
  PNG *writer* support for 16-bit, only `decodePng()`'s *reader* path to
  reject it). Decoding either returns the expected
  `Err(ChannelCountMismatch)`/`Err(UnsupportedBitDepth)`. No new binary
  file is added to this repository.
- **`provenance_tests.cpp`** — a well-formed golden sidecar parses
  correctly and round-trips through `serializeGoldenProvenance()`
  byte-for-byte; each of wrong line count / wrong field name at a position
  / unknown `schema_version` / malformed numeric field returns the
  expected `Err`; `vulkan_loader_api_version: unavailable` parses
  successfully as the one modeled exception;
  `compareProvenanceEnvironment()` returns empty for a fully-matching pair
  and names every differing field (not just the first) for a constructed
  mismatch.
- **`golden_validity_tests.cpp`** — each of the four validity-check steps'
  failure mode, independently: missing PNG; missing sidecar (PNG present,
  `.sidecar.txt` absent); a real (test-fixture) 3-channel PNG; a 16-bit
  PNG; a sidecar whose recorded `extent_width`/`format` deliberately
  disagrees with its paired PNG's actual decoded properties — each
  produces the specific, distinct `GoldenValidityError` variant, never a
  generic catch-all; plus `writeFailureArtifacts()` producing two
  correctly-named, decodable PNG files in a constructed temporary output
  directory.

### 5.2 New: `tests/image_regression/image_regression_gpu_tests.cpp`

Input: Section 3 (fixture + support libraries), Section 4 (a real,
committed golden must already exist);
`tests/vulkan_backend/headless_rendering_gpu_tests.cpp`'s
`ScopedFailureHandler`-free, plain-`REQUIRE()`-based structure as the
pattern (this file asserts on `Result`/comparison outcomes, not on
`ATLANTIS_CHECK` firing). Every golden/environment-file/failure-artifact
path comes from the three `target_compile_definitions()` macros Section
5.4 injects — `ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR`,
`ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE`,
`ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR` — never a path relative to the
process's working directory. Catch2 `TEST_CASE`s, tag
`[image_regression][gpu]`, device created with `enableValidationLayers =
true`:

1. A full `setUpMinimalCubeFixture()` → `renderOneFrame()` →
   `loadAndValidateGolden()` → `compareBuffers()` cycle against the real,
   committed `minimal_cube` golden passes (`ComparisonReport::passed ==
   true`), Vulkan Validation Layers clean. At this `TEST_CASE`'s start,
   before anything else, removes any pre-existing
   `<ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR>/minimal_cube_512x512_rgba8unorm_{actual,diff}.png`
   left from a previous failing run — a stale diagnostic artifact from an
   already-fixed problem must never linger and be mistaken for a current
   one. On failure, calls `writeFailureArtifacts()` (2.4) before the
   `REQUIRE` that reports the failure, so the artifacts exist on disk by
   the time a human reads the test output.
2. The same cycle repeated 3 times against the same fixture (matching
   `examples/headless_rendering_demo`'s own `kCycleCount`) produces
   byte-for-byte identical `PixelBuffer`s each time — the automated,
   permanent form of the determinism verification the calibration evidence
   (ADR-0042's Context) already performed once, ad hoc and uncommitted.
3. Running `loadAndValidateGolden()` against a golden name this Plan never
   commits a golden for (e.g. `"nonexistent_scene/nonexistent"`) returns
   `Err(MissingPngFile)` — reported as a distinct Catch2 failure message
   prefixed `INVALID GOLDEN:`, not a crash, not a silent `SUCCEED()`.
4. **Provenance-mismatch handling:** read
   `ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE` (`SKIP()` with an
   explanatory message if the file is absent — this case exists to test
   the mismatch-reporting path itself, not the core comparison, and
   cannot run meaningfully without a populated environment file);
   construct an `EnvironmentProvenance` with one field deliberately
   altered from what the golden's sidecar records; confirm
   `compareProvenanceEnvironment()` returns exactly that one field as a
   `ProvenanceFieldDiff`, and confirm (by convention in this test file,
   not a shared helper) that logging this mismatch via `WARN(...)` does
   **not** cause the `TEST_CASE` to fail — only `compareBuffers()`'s own
   `passed` value drives the test's pass/fail `REQUIRE`.

**Not a `TEST_CASE` in this file — a one-time, manual verification
procedure, performed once during Implementation and recorded in the
Implementation PR, then reverted (see Section 6's checklist item):** proof
that a deliberately introduced *rendering* regression is actually caught.
This must be a **real source-code change to `minimal_cube_fixture.cpp`
(e.g. its clear color or one vertex's position), followed by a real
rebuild of `atlantis_image_regression_gpu_tests`, followed by a real
re-run** — `renderOneFrame()` must actually produce different
GPU-rendered pixels through the real render path, which
`loadAndValidateGolden()` + `compareBuffers()` then genuinely catch.
**Directly corrupting an in-memory `PixelBuffer` in test code before
calling `compareBuffers()` does not satisfy this requirement** — that
would prove only that `compareBuffers()`'s arithmetic works on constructed
inputs (already covered by 5.1's `pixel_diff_tests.cpp`), not that the
*rendering path itself*, end to end, produces a detectably different image
when the scene actually changes. The fixture change is reverted
immediately after this procedure, before the Implementation PR opens.

### 5.3 `tests/image_regression/CMakeLists.txt` (modify) — GPU-independent test executable

Defined directly in `tests/image_regression/CMakeLists.txt` (not
`support/CMakeLists.txt`), matching every other `tests/<module>/CMakeLists.txt`
precedent: `add_executable(atlantis_image_regression_tests
pixel_diff_tests.cpp png_codec_tests.cpp provenance_tests.cpp
golden_validity_tests.cpp)`; `target_link_libraries(... PRIVATE
Atlantis::ImageRegressionSupport Catch2::Catch2WithMain
atlantis_compiler_warnings)`;
`catch_discover_tests(atlantis_image_regression_tests DISCOVERY_MODE
PRE_TEST)`. No `LABELS "gpu"` anywhere in this block — entirely
GPU-independent (matching `tests/render_graph/CMakeLists.txt`).

### 5.4 `tests/image_regression/CMakeLists.txt` (modify) — GPU-required test executable

Append `add_executable(atlantis_image_regression_gpu_tests
image_regression_gpu_tests.cpp)`; `target_link_libraries(... PRIVATE
Atlantis::ImageRegressionFixture Atlantis::ImageRegressionSupport
Catch2::Catch2WithMain atlantis_compiler_warnings)`;
`catch_discover_tests(... DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "gpu"
WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_image_regression_gpu_tests>")`;
`target_compile_definitions(... PRIVATE
ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/tests/image_regression/goldens"
ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE="${CMAKE_SOURCE_DIR}/tests/image_regression/current_environment.sidecar.txt"
ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR="${CMAKE_BINARY_DIR}/image_regression_failures")`
— absolute, configure-time paths for everything this executable's test
code needs to find that is NOT the shader pair (goldens, the machine-local
environment file, and where to write failure diagnostics); none depends on
`WORKING_DIRECTORY`, which stays scoped to shader loading only (every prior
demo/GPU-test resolves its shader pair relative to its own build output
directory — deliberately not extended to the three paths above, which must
resolve identically regardless of which directory `ctest` or a human
invokes this executable from). Plus `add_dependencies(...
minimal_mesh_shaders)` and the same POST_BUILD shader-copy as 3.4.
`ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR`
(`${CMAKE_BINARY_DIR}/image_regression_failures`) is created on demand by
`writeFailureArtifacts()` (2.4, `std::filesystem::create_directories()`)
the first time a comparison actually fails — not created unconditionally,
so an all-passing run leaves no empty directory behind.

---

## 6. Verification

- **Debug and Release builds**, clean, no new compiler warning
  (`atlantis_compiler_warnings` applied to every new target).
- **GPU-independent suite** (`ctest -LE gpu`), Debug and Release: 5.1's
  new cases pass; every pre-existing GPU-independent test elsewhere
  continues to pass unmodified (this Plan touches no existing test file).
- **GPU-required suite** (`ctest -L gpu`), Debug and Release, on real
  Windows/Vulkan hardware, with `current_environment.sidecar.txt`
  populated: 5.2's new cases pass; every pre-existing GPU-required test
  elsewhere continues to pass unmodified.
- **Vulkan Validation Layers clean** throughout every GPU-touching run,
  both configs.
- **Deliberate-regression-caught proof** (5.2's final unnumbered
  paragraph — not a permanent `TEST_CASE`): performed once, manually,
  during Implementation, as a **real source change, real rebuild, real
  re-run** — never by corrupting an in-memory buffer — temporarily alter
  `minimal_cube_fixture.cpp`'s clear color (or one vertex position),
  rebuild `atlantis_image_regression_gpu_tests`, re-run it, confirm item 1
  now fails with a nonzero `outOfToleranceCount` and a written
  `_actual.png`/`_diff.png` pair under
  `ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR`, then **revert the temporary
  change** before the Implementation PR is opened. Evidence (the
  before/after `ComparisonReport` values, and confirmation the fixture
  change was reverted) recorded in the Implementation PR description.
- **Local/manual gate, recorded:** a human or agent runs `ctest -L gpu`
  (and, at least once, `atlantis_image_regression_golden_generator`
  itself, per Section 4) against real hardware and records:
  hardware/driver/Vulkan version used (matching Spec 0010's disclosure
  format), pass/fail per test, and confirmation the deliberate-regression
  check above was performed and reverted — this is Spec 0011's own real,
  working gate (its Non-Goals explicitly exclude automated CI
  enforcement).
- **`git diff --check` clean** on every commit.
- **Explicit Prohibitions checklist** (below) fully checked.

---

## 7. Documentation and Registry Post-Implementation Updates

Deferred to the Implementation PR itself (not this Plan, not a separate
PR) — matching Spec 0010's precedent:

- `specs/README.md`: Spec 0011's row — Implementation column updated from
  "Not started" to a description of what actually shipped
  (`tests/image_regression/` structure, PR link(s), verification summary —
  GPU-independent/GPU-required pass counts, Debug/Release, Validation
  Layers clean, deliberate-regression-caught confirmation), mirroring Spec
  0010's row's level of detail.
- `docs/project-blueprint.md`: Milestone 8 (Image Regression Testing) —
  updated only if Implementation confirms this is in scope for the
  Implementation PR, matching Spec 0010's "explicit Implementation-time
  decision, not pre-committed here" precedent.
- `docs/architecture/*.md`: updated only if Implementation reveals a
  genuine as-built architecture fact these documents' content should
  reflect — not assumed necessary (this Plan adds no new module or
  architecture boundary).

---

## Explicit Prohibitions (grep/code-review checklist)

Every item below must hold, verifiable by inspection, before this Plan's
Implementation is considered complete:

- [ ] `git grep -rn "Vk[A-Z]" tests/image_regression/` returns nothing in
      real code references (an incidental `Vk*`-shaped substring inside an
      explanatory *comment* naming what a file deliberately does not use
      is not a violation — see this Plan's Human Review Approval).
- [ ] `git grep -rn "#include <vulkan" tests/image_regression/` returns
      nothing.
- [ ] No file under `src/rhi/`, `src/render_graph/`, `src/renderer/`, or
      `src/vulkan_backend/` is modified — `git diff --stat` against this
      Plan's base commit shows only files under `tests/image_regression/`,
      `cmake/`, `.gitignore`, and root `CMakeLists.txt`.
- [ ] `git grep -rn "STB_IMAGE_IMPLEMENTATION\|STB_IMAGE_WRITE_IMPLEMENTATION"
      tests/image_regression/` returns **exactly one match each**, both in
      `support/png_codec.cpp`.
- [ ] `git grep -rn "stbi_set_flip_vertically_on_load\|stbi_flip_vertically_on_write"
      tests/image_regression/` returns nothing.
- [ ] `git grep -rn "#include.*shader_system/src\|shader_system_rhi_integration/src"
      tests/image_regression/` returns nothing — every Shader System
      symbol used comes from that module's public headers only.
- [ ] `git grep -rn "kChannelTolerance\|kFailingPixelBudget"
      tests/image_regression/` shows both constants defined exactly once
      (`pixel_diff.h`), value `0`, referenced only from within
      `pixel_diff.cpp`.
- [ ] No `add_test(` or `catch_discover_tests(` call anywhere references
      `atlantis_image_regression_golden_generator`.
- [ ] `tests/image_regression/current_environment.sidecar.txt` (the real,
      filled-in file, not the `.example` template) does not appear in `git
      status --porcelain` output after being created locally.
- [ ] No `find_package`/`FetchContent_Declare` call for any dependency
      beyond `stb` is added anywhere this Plan touches.
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
      `current_environment.sidecar.txt` is not tracked before reading it —
      Section 3.3 step 3's safeguard is actually present.
- [ ] No new binary file (image or otherwise) is added under
      `tests/image_regression/` beyond the one golden PNG + sidecar pair
      Section 4 commits — `png_codec_tests.cpp`'s non-RGBA8 test fixtures
      are generated at run time into
      `std::filesystem::temp_directory_path()`, never checked in.
- [ ] `git diff --check` clean on every commit.

## Build Integration

- `cmake/AtlantisDependencies.cmake`: `stb`'s
  `FetchContent_Declare`/`FetchContent_MakeAvailable`/`Stb::Stb` wrapper
  (Section 1.1).
- `CMakeLists.txt` (root): one new line,
  `add_subdirectory(tests/image_regression)`, inside the existing
  `if(ATLANTIS_BUILD_TESTS)` block (Section 2.7).
- `tests/image_regression/CMakeLists.txt` (new): `add_subdirectory()`
  calls for `support/`, `fixture/`, `golden_generator/`, plus the two
  test executables defined directly in this file (Sections 2.6, 3.5, 5.3,
  5.4).
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

Unlike Plan 0010, this Plan touches no already-implemented abstract
interface — every type and function is brand new, so no step forces
splitting a pure-virtual declaration from its concrete overrides across a
step boundary. Sequencing is close to linear:

1. **Step 1 — `stb` dependency (Section 1):**
   `cmake/AtlantisDependencies.cmake` only. Ends compilable (a successful
   CMake configure); nothing yet consumes `Stb::Stb`.
2. **Step 2 — GPU-independent support library (Sections 2.1–2.7):**
   `pixel_diff`, `png_codec`, `provenance`, `golden_validity`, their
   shared `CMakeLists.txt`, `tests/image_regression/CMakeLists.txt`'s
   first line, and the root `CMakeLists.txt` wiring. Ends compilable;
   `Atlantis::ImageRegressionSupport` builds as a library with no consumer
   yet.
3. **Step 3 — Fixture library and golden generator (Sections 3.1–3.7):**
   depends on Step 2. Ends compilable; the golden generator tool exists
   and can be run manually, but no golden has been captured yet.
4. **Step 4 — First committed golden (Section 4):** depends on Step 3,
   requires real hardware. Not a compilation step, but it must complete
   before Step 5's GPU-required test can be written meaningfully.
5. **Step 5 — Automated tests (Sections 5.1–5.4):** 5.1
   (GPU-independent) depends only on Step 2 and could technically land
   before Step 3/4; 5.2/5.4 (GPU-required) depend on Step 3 (fixture) and
   Step 4 (a real golden to compare against). Grouped into one step here
   because splitting the GPU-independent and GPU-required test additions
   across separate Implementation-PR boundaries would leave `ctest -L gpu`
   referencing a source file (`image_regression_gpu_tests.cpp`) that does
   not exist yet for no compilation benefit — both land together. Ends
   compilable; `ctest -LE gpu` passes immediately; `ctest -L gpu` passes
   on real hardware (Step 6's job to actually run and record).
6. **Step 6 — Verification (Section 6):** depends on Step 5 in full.
7. **Step 7 — Documentation (Section 7):** depends on Step 6.

A single Implementation PR landing Steps 1–7 together (matching Spec
0010's own single-PR-per-spec precedent) is this Plan's expected shape;
splitting into multiple PRs is a Plan-Review-confirmable choice — unlike
Plan 0010, no step here is a large, non-subdividable bundle, so a narrower
split (e.g. Steps 1–2 as one PR, 3–7 as a second) is a genuinely
available option if Plan Review prefers it.

## Files / Modules Touched (expected)

**New:** `tests/image_regression/CMakeLists.txt`,
`tests/image_regression/support/{CMakeLists.txt,pixel_diff.{h,cpp},png_codec.{h,cpp},provenance.{h,cpp},golden_validity.{h,cpp}}`,
`tests/image_regression/fixture/{CMakeLists.txt,minimal_cube_fixture.{h,cpp}}`,
`tests/image_regression/golden_generator/{CMakeLists.txt,main.cpp}`,
`tests/image_regression/{pixel_diff_tests.cpp,png_codec_tests.cpp,provenance_tests.cpp,golden_validity_tests.cpp,image_regression_gpu_tests.cpp}`,
`tests/image_regression/current_environment.sidecar.txt.example`,
`tests/image_regression/goldens/minimal_cube/{minimal_cube_512x512_rgba8unorm.png,minimal_cube_512x512_rgba8unorm.sidecar.txt}`
(Section 4, a separate commit from every other file in this list, per
ADR-0042's same-PR ordering rule).

**Modified:** `cmake/AtlantisDependencies.cmake`, `CMakeLists.txt` (root),
`.gitignore`, `specs/README.md` (post-implementation, Section 7).

**Explicitly not touched:** any file under `src/rhi/`, `src/render_graph/`,
`src/renderer/`, `src/vulkan_backend/`, `src/shader_system/`,
`src/platform/`, `src/tools/`, `shaders/`; any existing `examples/*` or
`tests/{core,platform,rhi,vulkan_backend,render_graph,renderer,shader_system,tools}/*`
file — this Plan's fixture (Section 3.1) is a deliberate, disclosed
duplication of `examples/headless_rendering_demo/main.cpp`'s fixture
values, not a shared-code refactor of that file, which therefore needs no
edit.

If Implementation touches a file not listed here, that is a deviation to
call out explicitly in the Implementation PR.

## Verification Checklist

- [ ] Unit tests (GPU-independent, `ctest -LE gpu`, Debug **and**
      Release): Section 5.1's new cases pass, no new warning introduced,
      every pre-existing GPU-independent test elsewhere unaffected.
- [ ] Headless integration tests (GPU-required, `ctest -L gpu`, Debug
      **and** Release): Section 5.2's new cases pass on real
      Vulkan-capable hardware, with `current_environment.sidecar.txt`
      populated.
- [ ] Image regression tests: **this Plan is what makes this test layer
      exist for the first time** — Section 5.2's own coverage is the
      answer, not a future deferral.
- [ ] Golden validity check exercised for all four failure modes
      independently (Section 5.1's `golden_validity_tests.cpp`).
- [ ] Provenance-mismatch diagnostic confirmed separate from pass/fail
      (Section 5.2 item 4) — a mismatch never changes a `TEST_CASE`'s own
      `REQUIRE`-driven outcome.
- [ ] Deliberate rendering regression confirmed caught (Section 6),
      performed once manually, reverted before the Implementation PR opens
      — evidence (before/after `ComparisonReport` values) recorded in the
      Implementation PR description.
- [ ] Vulkan Validation Layers clean: for every GPU-touching test and the
      golden generator's own real run (Section 4), in both Debug and
      Release — zero warnings, zero errors.
- [ ] Manual/local verification record (Section 6) — hardware/driver/
      Vulkan version, pass/fail per test, deliberate-regression
      confirmation — written into the Implementation PR.
- [ ] Explicit Prohibitions checklist (above) fully checked.
- [ ] `git diff --check` clean on every commit.

## Rollback Plan

Steps 1–7 are each independently revertible in reverse order (7 → 1)
without touching an earlier, already-verified step — `git revert` of the
Implementation PR's commit(s) in reverse-chronological order restores the
pre-Plan state exactly, since every file this Plan touches is either brand
new (Steps 1–5, safe to delete outright) or a small, additive,
easily-reversible edit to an existing file
(`cmake/AtlantisDependencies.cmake`'s appended block, root `CMakeLists.txt`'s
one new `add_subdirectory()` line, `.gitignore`'s one new entry) — no
existing test, example, or `src/` file's own behavior is changed by any
step. Two narrower rollback points:

- If the problem is isolated to the golden generator or the first
  committed golden (Sections 3.3–3.7, 4): revert only those
  files/commits; Sections 1–2's support library and Section 5.1's
  GPU-independent tests remain valid and unaffected (they test the
  comparison algorithm against synthetic buffers, not the real golden).
- If the problem is isolated to `stb`/PNG codec specifically (Sections 1,
  2.2): revert those two sections; every other section's own code fails to
  compile once `png_codec.h`'s API disappears — **not** an
  independently-revertible narrower point on its own (Section 2.2 is a
  true dependency root for everything downstream), listed here only to
  name where the fault would actually originate if PNG encode/decode
  itself is the problem.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan:

- "Image regression tests added/updated if rendered output changed" —
  **N/A for this Plan's own diff**: this Plan adds the image-regression
  *harness itself*; it does not change any existing rendered output.
- "CI green" — **N/A, no CI pipeline exists** (Spec 0011's Non-Goals); the
  Verification Checklist's manual/local gate is this Plan's actual
  Definition-of-Done gate, per Spec 0011's explicit design.

## Human Review / Plan Review Blockers

**No architectural gap requiring a return to Spec/ADR was found while
producing or revising this Plan.** Every decision below (and every fix in
the Plan Review round) is an implementation-shape or data-accuracy
correction within the boundaries Spec 0011 and ADR-0041/ADR-0042 already
fixed — none requires a new public API, module boundary, dependency beyond
`stb`, or a change to Spec 0011/ADR-0041/ADR-0042's own approved
methodology.

**Resolved during the Plan Review round, no longer open:**

- Golden/sidecar/environment-file path resolution — repository-root-relative
  vs. build-output-relative (formerly Blockers 1 and 3) — **resolved**:
  none of the three is resolved against the invoking process's working
  directory at all. `ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR`,
  `ATLANTIS_IMAGE_REGRESSION_ENVIRONMENT_FILE`, and
  `ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR` are `target_compile_definitions()`
  string constants, computed once at CMake configure time from
  `CMAKE_SOURCE_DIR`/`CMAKE_BINARY_DIR` (Sections 3.4, 5.4) and applied
  identically to the golden generator and the GPU test — no implicit
  directory search, no dependency on `WORKING_DIRECTORY`, which stays
  scoped to shader loading only.
- `png_codec_tests.cpp`'s 3-channel/16-bit test PNG fixtures —
  **resolved**: generated programmatically at test run time into
  `std::filesystem::temp_directory_path()` and removed at teardown; no
  binary test fixture checked in (Section 5.1).
- Whether a distinct CI/test-category label for image-regression GPU tests
  is needed — **resolved**: no. This Plan continues to reuse the existing
  plain `"gpu"` `LABELS` property (Section 5.4). Spec
  0006/0007/0010's own open question about whether a *more granular* label
  might someday be useful remains theirs to resolve, not force-closed
  here.
- `vulkan_requested_instance_api_version`'s example value — **found and
  corrected during the Plan Review round**: an earlier draft copied the
  *loader's* reported version (`1.4.357`) into this field. Verified
  against `src/vulkan_backend/src/instance_api_version.cpp`'s actual
  `decideRequestedInstanceApiVersion()` body: this codebase's Vulkan
  Backend requests exactly `1.3.0` (loader supports `>= 1.3`) or exactly
  `1.0.0` — a closed, two-outcome, loader-version-derived value.
  Corrected in Sections 2.3 and 3.6, with an explicit derivation rule.

**Non-blocking, disclosed limitations carried into Implementation:**

- The golden generator's `git status --porcelain`/`git rev-parse
  HEAD`/`git ls-files` subprocess calls (Section 3.3) assume `git` is on
  the invoking process's `PATH` — true for every development/CI
  environment this project has used (the entire Spec → Plan →
  Implementation workflow depends on `git`). A failure to even *launch*
  `git` is no longer silently treated as "no output, tree is clean" —
  Section 3.3 enumerates launch-failure, nonzero-exit, and dirty-tree as
  three distinct, never-conflated outcomes.
- `EnvironmentProvenance`'s values are human-transcribed (from `vulkaninfo
  --summary` or equivalent, per Section 3.6's field-by-field mapping —
  with one field, the requested instance version, instead *derived* by a
  documented rule), not machine-verified against the actual running
  process's real Vulkan instance/device — a disclosed, accepted gap given
  RHI's public API exposes no query for this data and this Plan does not
  add one. A future spec wanting an automated, self-verifying provenance
  capture would need its own RHI API addition.
- This format's sidecar parser has no escaping mechanism for a field value
  containing an embedded newline (Section 2.3) — not a gap for any of this
  Plan's own 13+8 fields, all single-line by nature; a future field
  needing one would require its own `schema_version` bump and escaping
  rule.
- This Plan's `ComparisonReport`/`GoldenValidityError` types are scoped to
  exactly this Plan's one covered scene (`minimal_cube`); no multi-scene
  registry/lookup mechanism is designed, since Spec 0011 itself covers
  exactly one scene.
