# Spec: Runtime Sample Scene Selection

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-09
- **Related Plan(s):** None yet — Plan 0032 may be drafted only once
  [PR #139](https://github.com/slmao/Atlantis/pull/139) itself has
  merged to `main`, not before
- **Related ADR(s):** [ADR-0076: Runtime Sample Scene Selection Boundary](../adr/0076-runtime-sample-scene-selection-boundary.md) (`Accepted`); amends [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md) via that ADR's own [Accepted Amendment — 2026-09-09](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md#accepted-amendment--2026-09-09) section (`Accepted`, approved independently of ADR-0076 and this Spec) — see this Spec's own [Human Review Approval — 2026-09-10](#human-review-approval--2026-09-10) below
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #155](https://github.com/slmao/Atlantis/pull/155) Batch 8. Original scope and obligations retained. Drafting
  history in [PR #139](https://github.com/slmao/Atlantis/pull/139).

## Summary

`atlantis_runtime.exe` gains a closed, whitelisted `--scene <name>`
startup flag selecting among three already-shipping sample scenes
(`integrated_showcase_demo` — today's unconditional default,
`ibl_material_demo`, `pbr_normal_map_demo`), plus `--list-scenes` and
`--help`. Selection happens once, entirely before window/Vulkan-device
creation, inside the executable's own private startup layer; no new
public scene-management API, no arbitrary path loading, no
command-line library dependency.

## Motivation / Problem Statement

`atlantis_runtime` today calls `int main()` (no `argc`/`argv` at all)
and unconditionally builds one `BootstrapConfig` pointing at
`integrated_showcase_demo_scene` — the *only* way to see any other
sample scene through the real windowed Runtime is to build and run a
different, separate executable (an `examples/` demo or an
`image_regression` fixture), none of which is the actual product
entry point. Two more real, already-shipping, already-golden-tested
scenes (`ibl_material_demo`, `pbr_normal_map_demo`) exist purely as
test/example content today. This Spec lets a human pick among the
three directly from the one real product binary, for manual
inspection — nothing about the scenes' own content, rendering, or test
coverage changes.

## Goals

- `--scene <name>` selects one of exactly three whitelisted sample
  scenes; omitting it keeps today's exact default
  (`integrated_showcase_demo`).
- `--list-scenes` prints the whitelist and exits `0`; `--help` prints
  usage and exits `0`.
- Every scene's own complete, real `BootstrapConfig` mapping (scene
  artifact/metadata/manifest triple, environment) is fixed by this
  Spec, verified against the real, already-shipping CMake asset
  declarations and image-regression fixtures — never assumed.
- Selection, parsing, and validation happen before any window or
  Vulkan device is created.
- `ATLANTIS_BUILD_TESTS=OFF` still cooks all three scenes and their
  dependencies.

## Non-Goals

- No arbitrary/user-supplied scene file path — the three names are the
  entire surface; no new "load any path" capability.
- No new public scene-management system, no runtime scene switching
  (once started, the process keeps the scene it started with — exiting
  and relaunching with a different `--scene` is the only way to
  change), no config file, no environment-variable surface.
- No new rendering feature, no data/content change to any of the three
  scenes, no golden-image change, no exposure-default change (Spec
  0031 stays as shipped), no Spec 0030 shadow-bias work (stays
  deferred).
- No third-party CLI/argument-parsing library — a plain, hand-rolled
  parser, matching `src/tools/asset_cooker/main.cpp`'s own existing
  `--flag[=value]` convention in spirit (loop over `argv`, explicit
  duplicate/unknown detection, usage-to-`stderr`-then-exit-1 on error).
- No change to `RuntimeApplication`'s or `BootstrapConfig`'s own public
  shape — both are consumed exactly as today.
- No per-scene window title — `BootstrapConfig::applicationName` stays
  fixed, exactly as today, regardless of which scene is selected.

## Requirements

### Functional

1. **`atlantis_runtime`'s entry point becomes `int main(int argc, char**
   argv)`** (from today's `int main()`, which silently ignores any
   argv it is given). Argument parsing and scene-name-to-`BootstrapConfig`
   mapping happen in a new, private `cli.h`/`cli.cpp` pair — outside
   `atlantis_runtime_host`'s own public `include/` directory, not part
   of that library at all, and not part of `RuntimeApplication`/
   `BootstrapConfig`'s own public API. The same `cli.cpp` source file
   is compiled into both `atlantis_runtime` (the executable) and the
   existing GPU-independent `atlantis_runtime_tests` target — a shared
   source file across two executable targets, not a new library, not a
   new CMake target. `main.cpp` itself owns reading `argv`, printing
   every diagnostic/usage message, and returning the process exit code;
   the helper functions are pure (parse in, structured result out —
   see Proposed Design), doing no I/O and calling neither `std::exit`
   nor any print function themselves, which is what makes them directly
   assertable in a GPU-independent test.
2. **Closed scene-name whitelist, exactly three values:**
   `integrated_showcase_demo`, `ibl_material_demo`,
   `pbr_normal_map_demo` — matching each real, already-declared
   `atlantis_add_scene_asset(NAME <name>_scene ...)` in
   `assets/CMakeLists.txt` exactly (minus the `_scene` suffix). No
   other value is ever accepted.
3. **`--scene <name>`** (two tokens, space-separated) selects one
   whitelisted scene. Omitted → `integrated_showcase_demo` (today's
   exact, unconditional behavior — zero change for a no-argument
   launch). `--list-scenes` prints the three names (one per line) to
   `stdout` and exits `0` — no window, no device, no scene load.
   `--help` prints a short usage synopsis (including the whitelist) to
   `stdout` and exits `0`.
4. **Error handling, before any window/device work:** an unrecognized
   `--scene` value, a `--scene` with no following value, an unknown
   flag, or `--scene` supplied more than once (even with the same
   value — a duplicate is still rejected, not silently accepted) each
   print a usage/diagnostic message to `stderr` and exit non-zero,
   reusing `RuntimeExitReason::InitializationFailed`'s own existing
   exit code (`1`) — no new `RuntimeExitReason` enumerator.
   `--list-scenes`/`--help` combined with any other flag (including
   each other) is a conflicting-argument error under this same rule,
   not a silent "last flag wins."
5. **Real, per-scene `BootstrapConfig` mapping — verified, not
   assumed** (see the Proposed Design table): each of the three names
   maps to its own real `sceneArtifactPath`/`sceneMetadataPath`/
   `sceneDependencyManifestPath` triple (the `ATLANTIS_<name>_scene_*`
   CMake variables `atlantis_add_scene_asset()` already exports for
   each). `environmentArtifactPath`/`environmentMetadataPath` stay
   fixed at `ibl_studio` for all three — confirmed identical for all
   three scenes' own real, already-shipping image-regression
   fixture/golden-generator configuration (not assumed from the scene
   path alone). Every other `BootstrapConfig` field (all built-in
   shader-pair paths, `enableValidationLayers`, the legacy, already-
   unreferenced `assetArtifactPath`/`assetMetadataPath` pair) stays
   identical across all three, exactly as it is unconditionally today.
   The real, absolute paths are supplied by `main.cpp` (via
   `atlantis_runtime`'s own CMake compile definitions) as a small
   name→paths table passed *into* the parsing helper as a parameter —
   the helper itself never reads a build-path macro, an environment
   variable, or any other process state beyond the `argv` it is given,
   which is exactly what lets a test call it with an injected,
   fixture-only table (Testing & Verification Plan below).
6. **CMake scope — two files, both real edits, neither a repository-
   wide cleanup:**
   - `src/runtime/CMakeLists.txt` (production): `cli.cpp` is added to
     `atlantis_runtime`'s own source list; the two new scene path
     triples are added as `target_compile_definitions()` on
     `atlantis_runtime`; `atlantis_runtime`'s own `add_dependencies()`
     list gains the two new scene targets
     (`${ATLANTIS_ibl_material_demo_scene_TARGET}`,
     `${ATLANTIS_pbr_normal_map_demo_scene_TARGET}`) alongside the
     existing `${ATLANTIS_integrated_showcase_demo_scene_TARGET}`.
     `atlantis_add_scene_asset()`'s own macro already wires each scene
     target's dependency on its declared `MESH_DEPENDENCIES`/
     `MATERIAL_DEPENDENCIES`/`TEXTURE_DEPENDENCIES` internally, so
     adding the two scene-target edges is sufficient to reach every
     transitive mesh/material/texture dependency —
     `assets/CMakeLists.txt`'s own unconditional
     `add_subdirectory(assets)` (before the `ATLANTIS_BUILD_TESTS`
     gate) already means every asset *declaration* exists regardless
     of that flag; only `atlantis_runtime`'s own build-graph *edge* to
     the two new scene targets was missing.
   - `tests/runtime/CMakeLists.txt` (test): the same `cli.cpp` (a
     relative-path source reference into `src/runtime/`, mirroring
     this codebase's own established "test compiles a module's private
     source file directly" precedent — e.g. `json_parser_tests.cpp`'s
     own inclusion of `src/shader_system/src/json_parser.h`) is added
     to `atlantis_runtime_tests`' own (GPU-independent) source list,
     alongside a new `cli_tests.cpp`. No change to
     `atlantis_runtime_gpu_tests`, `tests/image_regression/`, or any
     other file — this Spec's own CMake surface is exactly these two
     files.
7. **No working-directory dependence.** Every path threaded into
   `BootstrapConfig` is a CMake-injected, absolute, build-tree compile
   definition — the exact, already-established pattern every existing
   `ATLANTIS_RUNTIME_*` macro in `main.cpp` already follows; the CLI
   layer never resolves a path relative to the process's own current
   directory.

### Non-functional

- **Performance:** parsing three or four `argv` tokens once at startup;
  immaterial.
- **Memory:** one new small, private header/source pair; no new
  runtime allocation pattern.
- **Portability (Vulkan-only Phase 1):** Windows-only for now, matching
  this executable's own current scope; nothing about the CLI surface
  is Windows-specific (`argc`/`argv` is standard C++), so no portability
  regression for a future Android entry point (which would not use
  this `argv`-based path at all — Android has no process command line
  in this sense — and is out of this Spec's own scope regardless).
- **Other — compatibility:** a no-argument launch is byte-for-byte
  identical in behavior to today's `atlantis_runtime.exe` — same
  scene, same environment, same window. Goldens are produced by
  `tests/image_regression/`'s own fixtures, not by `atlantis_runtime`,
  so this Spec's own changes have no code path that reaches them —
  the Testing & Verification Plan below still requires re-confirming
  all 10 existing goldens unchanged as a real, executed check, not
  asserted as structurally impossible to affect.

## Proposed Design

**CLI surface** (hand-rolled, no library — mirrors
`src/tools/asset_cooker/main.cpp`'s own established parsing style):

```
atlantis_runtime [--scene <name>]
atlantis_runtime --list-scenes
atlantis_runtime --help

<name> is one of: integrated_showcase_demo, ibl_material_demo, pbr_normal_map_demo
```

**Scene name → `BootstrapConfig` mapping** (every path already exists
as a real CMake variable; only the compile-definition macro names
threading them into `main.cpp` are new — exact names are a Plan-stage
detail, matching Spec 0013's own "exact field names are Plan's own
freedom" precedent):

| `--scene` value | `sceneArtifactPath`/`sceneMetadataPath`/`sceneDependencyManifestPath` source | `environmentArtifactPath`/`environmentMetadataPath` |
|---|---|---|
| *(omitted)* / `integrated_showcase_demo` | `ATLANTIS_integrated_showcase_demo_scene_*` (today's existing, unchanged mapping) | `ATLANTIS_ibl_studio_*` (today's existing, unchanged mapping) |
| `ibl_material_demo` | `ATLANTIS_ibl_material_demo_scene_*` | `ATLANTIS_ibl_studio_*` (same as above — confirmed via `tests/image_regression/CMakeLists.txt`'s/`golden_generator/CMakeLists.txt`'s own real `ATLANTIS_IBL_DEMO_ENVIRONMENT_ARTIFACT_PATH` definition) |
| `pbr_normal_map_demo` | `ATLANTIS_pbr_normal_map_demo_scene_*` | `ATLANTIS_ibl_studio_*` (same — confirmed via the real `ATLANTIS_PBR_NORMAL_MAP_DEMO_ENVIRONMENT_ARTIFACT_PATH` definition, also `ibl_studio`) |

Every other `BootstrapConfig` field (all ten built-in shader-pair path
groups, `enableValidationLayers`) is populated identically for all
three, exactly as `main.cpp` already does today — none of them vary by
scene in the real, current shader/pipeline architecture (Spec 0031's
own investigation already established every shader pair is loaded
unconditionally, regardless of scene content).

**Startup layer placement.** A new, small, executable-private
`src/runtime/cli.h`/`src/runtime/cli.cpp` pair — *not* under
`src/runtime/include/atlantis/runtime/` (`atlantis_runtime_host`'s own
public directory), *not* compiled into `atlantis_runtime_host` at all
— holds a pure parsing function and a pure name→config lookup
function, both free of I/O and process state beyond their own
parameters:

```cpp
// src/runtime/cli.h -- private, not installed, not part of
// Atlantis::RuntimeHost's own public surface.
struct SceneBootstrapPaths {
  std::string sceneArtifactPath;
  std::string sceneMetadataPath;
  std::string sceneDependencyManifestPath;
};
struct SceneWhitelistEntry {
  std::string_view name;
  SceneBootstrapPaths paths;
};

enum class CommandLineOutcome { RunScene, PrintUsageAndExit, PrintErrorAndExit };

struct CommandLineResult {
  CommandLineOutcome outcome;
  std::string message;                            // usage text or error text; empty for RunScene
  std::optional<SceneBootstrapPaths> selectedScene;  // populated only for RunScene
};

[[nodiscard]] CommandLineResult parseCommandLine(int argc, char** argv,
                                                  std::span<const SceneWhitelistEntry> whitelist);
```

`main.cpp` builds `whitelist` from its own three real, absolute,
CMake-injected path triples (`ATLANTIS_RUNTIME_*` compile definitions
— exact macro names remain a Plan-stage detail, per Spec 0013 item 8's
own established precedent for this class of decision), calls
`parseCommandLine(argc, argv, whitelist)`, then itself does all the
I/O: `PrintUsageAndExit` → print `.message` to `stdout`, return
`toProcessExitCode(RuntimeExitReason::Success)`; `PrintErrorAndExit` →
print `.message` to `stderr`, return
`toProcessExitCode(RuntimeExitReason::InitializationFailed)`;
`RunScene` → copy `.selectedScene`'s three paths into the real
`BootstrapConfig` (built exactly as today otherwise) and proceed to
`createRuntimeApplication()`. Neither print-and-exit path ever reaches
window/device creation. `RuntimeApplication`/`BootstrapConfig`
themselves gain no new field, no new method, and no knowledge that a
CLI exists — `BootstrapConfig`'s own existing "populated by the
caller from CMake-injected compile definitions, no command-line
parsing inside Atlantis::RuntimeHost's own composition logic" contract
stays literally, plainly true, with no reinterpretation needed: the
new parsing code is not inside `Atlantis::RuntimeHost` at all.

Keeping `cli.h`/`cli.cpp` out of `atlantis_runtime_host` (rather than
adding it as new source files to that existing library, the
alternative this Spec's own drafting first considered) keeps that
library's own scope exactly as ADR-0047 already fixed it — Runtime
Host composition logic (object model, initialization, per-frame
orchestration, shutdown) — with no new public header its *other* real
consumers (`tests/image_regression/fixture/`,
`tests/runtime/`'s own GPU-required target, `tests/image_regression/
golden_generator/`) would see even though none of them ever parses
`argv` or needs scene-name-to-path mapping; they each already build a
`BootstrapConfig` directly, in their own code. `cli.cpp`'s own reuse
between `atlantis_runtime` and `atlantis_runtime_tests` is a shared
*source file* compiled twice, matching this codebase's own established
"test compiles a module's private source file directly" pattern — not
a new library, not a new CMake target, and not a change to how many
targets currently link `Atlantis::RuntimeHost` (that count is
unaffected either way, and is not "two" — see ADR-0076's own Context).

## Architectural Impact

**Yes** — this widens Runtime's own configuration boundary, a decision
[Spec 0013](0013-runtime-host-foundation.md)'s own already-approved
"Decisions Requiring Human Review item 8" fixed as: *"everything this
spec's own bootstrap needs is fixed at build/composition time... zero
required command-line... surface... A trivial, optional window-title/
size command-line override may be added at Plan stage without being
architecturally significant."* A closed, three-way, whitelisted
`--scene`/`--list-scenes`/`--help` surface is more than that
pre-approved "trivial" carve-out — it selects among discrete,
build-time-cooked *configurations*, not a cosmetic override — so this
Spec does not silently exceed item 8's own scope; it explicitly widens
it, subject to Human Review. [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)'s
own Decision text names item 8 directly as the boundary on
`atlantis_runtime`'s own "minimal argument handling" — since that
boundary is moving, ADR-0047 gets a short, cross-referencing Amendment
(not a rewrite of its own "two CMake targets" Decision, which this
Spec's design deliberately does not touch — see Proposed Design's own
"Startup layer placement" above), now [Accepted](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md#accepted-amendment--2026-09-09)
independently of this Spec. See
[ADR-0076](../adr/0076-runtime-sample-scene-selection-boundary.md)
(`Accepted`) for the full decision record — see this Spec's own
[Human Review Approval — 2026-09-10](#human-review-approval--2026-09-10)
below for the exact, separate scope each of the three approvals covers.

This Spec was checked against every other `Accepted` ADR whose scope it
touches (ADR-0046's own resource-destruction-order Decision explicitly
anticipates "every future spec that extends Runtime's bootstrap scene"
and is unaffected by which of the three scenes is chosen; ADR-0033's
Client-authority boundary is not exercised by this Spec, matching
ADR-0046's own precedent finding for the same question) and found no
other conflict.

## Alternatives Considered

- **A general scene-management/registry system** (arbitrary scene
  registration, plugin-style discovery): rejected — explicitly out of
  this Spec's own Non-Goals; three fixed, whitelisted names cooked at
  build time is the entire, deliberately narrow surface this round
  needs.
- **An arbitrary `--scene-file <path>` accepting any cooked artifact
  path on disk**: rejected — this would let `atlantis_runtime` load
  content never declared to CMake (no dependency-cooking guarantee, no
  `ATLANTIS_BUILD_TESTS=OFF` coverage, no fixed whitelist to validate
  against) and reopens exactly the "arbitrary path loading" surface
  this Spec's own Non-Goals rule out.
- **A `--scene=<name>` single-token form** (matching
  `atlantis_asset_cooker`'s own `--flag=value` convention exactly):
  considered for stylistic consistency with the existing Tools
  precedent, but this Spec fixes the two-token `--scene <name>` form
  instead, matching the form already used in this round's own
  directing instructions and common CLI convention for a value-bearing
  flag; both are equally simple, hand-rolled, and dependency-free, so
  this is a naming-convention choice, not an architectural one.
- **Putting the CLI parser directly in `main.cpp`, inline**: rejected —
  Requirement 7 (parsing itself needs GPU-independent test coverage,
  Testing & Verification Plan below) requires a linkable, testable
  unit; folding it into `main.cpp` (the executable-only translation
  unit) would make it untestable without a real process launch,
  exactly the class of gap ADR-0047 already exists to close for
  Runtime's other composition logic.
- **Adding `cli.h`/`cli.cpp` as new source files inside
  `atlantis_runtime_host`** (this Spec's own first-drafted design):
  rejected — see Proposed Design's own "Startup layer placement" for
  the full reasoning (keeps that library's real, existing consumers
  free of an unrelated public header none of them need).
- **A new, third CMake target dedicated to the CLI layer** (e.g.
  `atlantis_runtime_cli`): rejected — reusing the same `cli.cpp` source
  file directly in the two already-existing executable targets
  (`atlantis_runtime`, `atlantis_runtime_tests`) already gives full
  testability with zero new CMake targets; a third target would need
  its own ADR-0047 Amendment for the "two CMake targets" Decision
  itself, real additional CMake surface this Spec's own narrow scope
  does not need.

## Testing & Verification Plan

- **GPU-independent**: new tests for `parseCommandLine()`/the
  name-mapping function — every whitelisted name maps to its own
  correct scene-artifact/metadata/manifest triple and the shared
  environment; no-argument input resolves to
  `integrated_showcase_demo` byte-for-byte identically to today's
  hardcoded default; `--list-scenes`/`--help` each report "print and
  exit 0"; an unrecognized name, a missing `--scene` value, a
  duplicated `--scene`, an unknown flag, and `--list-scenes`/`--help`
  combined with another flag each report "print-error and exit
  non-zero" (`RuntimeExitReason::InitializationFailed`'s own code).
- **Real-process verification of `--help`/`--list-scenes`/invalid
  arguments.** Each is launched as a real, separate
  `atlantis_runtime.exe` process (not merely the GPU-independent
  `parseCommandLine()` unit test above) confirming: the real `stdout`/
  `stderr` output and real process exit code match the GPU-independent
  result: `--help`/`--list-scenes` exit `0`; an unrecognized `--scene`
  value, a missing `--scene` value, a duplicated `--scene`, an unknown
  flag, and `--list-scenes`/`--help` combined with another flag each
  exit non-zero; and, for every one of these, Runtime itself never
  started — no window is ever created and no device is ever touched
  (confirmed by the absence of any post-`createRuntimeApplication()`
  log line, and by the process exiting without ever presenting a
  window).
- **Real-window verification, one scene per process, from more than
  one working directory.** This codebase's own Windows Platform
  lifecycle is a real, already-documented per-process singleton
  (double-initialization/double-shutdown are assertion failures,
  `windows_platform.cpp`) — so the three scenes are verified via three
  *separate* real `atlantis_runtime.exe --scene <name>` process
  launches (never three scenes cycled inside one process), each
  confirming: successful window creation, a real frame renders, Vulkan
  Validation Layers stay clean throughout, and a clean shutdown
  (`RuntimeExitReason::Success`, exit `0`). At least one of the three
  launches (and the no-argument default) is additionally run from a
  working directory other than the build output directory, to confirm
  Requirement 7's own "no working-directory dependence" claim against
  a real process, not only against the source of the paths
  (CMake-injected compile definitions). The existing manual-
  verification precedent (Spec 0010's own registry entry: interactive
  windowed regression "performed and observed directly by a human
  verifier") is the model this Spec's own Plan should follow for this
  exact reason.
- **All 10 existing goldens re-confirmed unchanged, no new golden.**
  This Spec adds no rendering-path code, so no golden is expected to
  change — but that expectation is verified by an actual, executed
  capture-compare run of all 10 existing goldens as part of this
  Spec's own Implementation, not asserted as structurally impossible
  to affect.

## Risks & Open Questions

- Exact `ATLANTIS_RUNTIME_*` compile-definition macro names for the two
  new scene triples, and the exact `cli.h`/`cli.cpp` function
  signatures/return-type shape, are Plan-stage detail, not fixed here
  (matching Spec 0013 item 8's own established precedent for this
  class of decision).
- `BootstrapConfig::assetArtifactPath`/`assetMetadataPath` (the
  `minimal_cube` bootstrap-mesh pair) were confirmed, while
  investigating this Spec, to be dead — never read anywhere in
  `runtime_application.cpp` today. This Spec does not touch them
  (unrelated cleanup, out of scope); flagged here for a possible
  separate, future spawn_task.

## Out of Scope / Future Work

- Runtime scene *switching* while the process is running (this Spec
  fixes the scene once, at startup, for the process's own lifetime).
- A fourth+ scene, or any scene not already cooked/golden-tested today.
- Android/iOS entry points (this Spec's own CLI surface is a Windows
  `argv` concern only, per Non-functional above).

## Human Review Approval — 2026-09-10

**Status: Approved.** Recorded against
[PR #139](https://github.com/slmao/Atlantis/pull/139). Human Review's
own words: *"我分别认可 Spec 0032、ADR-0076，以及 ADR-0047 的 2026-09-09
Proposed Amendment"* ("I separately approve Spec 0032, ADR-0076, and
ADR-0047's 2026-09-09 Proposed Amendment").

This approval covers this Spec's complete Requirements and scope as
corrected in this round — the executable-private `cli.h`/`cli.cpp`
pair (outside `atlantis_runtime_host`, shared as a compiled-twice
source file between `atlantis_runtime` and `atlantis_runtime_tests`,
not a new library or CMake target), pure-function parsing with the
caller-injected `SceneWhitelistEntry` table (Requirement 5), the three
fixed scene names, the established `--scene`/`--list-scenes`/`--help`
CLI and exit-code contract (Requirements 3-4), the two-file CMake scope
(Requirement 6: `src/runtime/CMakeLists.txt` and
`tests/runtime/CMakeLists.txt`), and the Testing & Verification Plan
(GPU-independent parsing tests, real-process `--help`/`--list-scenes`/
invalid-argument verification, one real windowed process per scene from
more than one working directory, all 10 existing goldens re-confirmed).
No Requirements/Proposed Design text is rewritten by this approval.
[ADR-0076](../adr/0076-runtime-sample-scene-selection-boundary.md)
and [ADR-0047's own Accepted Amendment](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md#accepted-amendment--2026-09-09)
are approved independently, each carrying its own Acceptance Record —
this is not a blanket approval of one implying the others.

**This approval authorizes drafting Plan 0032 only once
[PR #139](https://github.com/slmao/Atlantis/pull/139) itself has merged
to `main` — not before, and not Implementation, code, tests, assets, or
golden capture of any kind.**

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-10, approving this Spec in full, as corrected
this round, with no further change.
