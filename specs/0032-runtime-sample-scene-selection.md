# Spec: Runtime Sample Scene Selection

- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-09
- **Related Plan(s):** None yet — Spec/ADR only this round
- **Related ADR(s):** [ADR-0076: Runtime Sample Scene Selection Boundary](../adr/0076-runtime-sample-scene-selection-boundary.md) (`Proposed`); amends [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md) via that ADR's own new [Proposed Amendment — 2026-09-09](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md#proposed-amendment--2026-09-09) section

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

## Requirements

### Functional

1. **`atlantis_runtime`'s entry point becomes `int main(int argc, char**
   argv)`** (from today's `int main()`, which silently ignores any
   argv it is given). Argument parsing and scene-name-to-`BootstrapConfig`
   mapping happen in a new, private helper — not inline sprawl in
   `main.cpp`, and not part of `RuntimeApplication`/`BootstrapConfig`'s
   own public API.
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
6. **CMake cooks all three scenes and their dependencies under
   `ATLANTIS_BUILD_TESTS=OFF`.** `atlantis_runtime`'s own
   `add_dependencies()` list gains the two new scene targets
   (`${ATLANTIS_ibl_material_demo_scene_TARGET}`,
   `${ATLANTIS_pbr_normal_map_demo_scene_TARGET}`) alongside the
   existing `${ATLANTIS_integrated_showcase_demo_scene_TARGET}` —
   `atlantis_add_scene_asset()`'s own macro already wires each scene
   target's dependency on its declared `MESH_DEPENDENCIES`/
   `MATERIAL_DEPENDENCIES`/`TEXTURE_DEPENDENCIES` internally, so this
   is sufficient; no other CMake file needs a change. `assets/CMakeLists.txt`'s
   own unconditional `add_subdirectory(assets)` (before the
   `ATLANTIS_BUILD_TESTS` gate) already means every asset declaration
   exists regardless of that flag — only `atlantis_runtime`'s own
   build-graph edge to the two new scene targets was missing.
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
  scene, same environment, same window, same 10 goldens' own rendering
  path unaffected (goldens are produced by `tests/image_regression/`'s
  own fixtures, never by `atlantis_runtime` itself, so this Spec
  cannot touch them regardless).

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

**Startup layer placement.** A new, small header/source pair —
`atlantis::runtime::parseCommandLine()` (returns a closed
result: proceed-with-this-scene, print-and-exit-0, or
print-error-and-exit-1) and a `sceneName → BootstrapConfig-overrides`
mapping function — is added to the *existing* `atlantis_runtime_host`
static library (new source files, not a new CMake target): `ADR-0047`
already frames that library as "exists solely for testability... not a
new public dependency surface any other top-level module may
consume," which is exactly the right home for this new, still-private,
now-testable logic — no third CMake target, no amendment needed for
the "two CMake targets" shape of ADR-0047's own Decision. `main.cpp`
calls this before `createRuntimeApplication()`; a print-and-exit result
returns directly from `main()`, never reaching window/device creation.
`RuntimeApplication`/`BootstrapConfig` themselves gain no new field, no
new method, and no knowledge that a CLI exists.

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
it, subject to this round's own Human Review. [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)'s
own Decision text names item 8 directly as the boundary on
`atlantis_runtime`'s own "minimal argument handling" — since that
boundary is moving, ADR-0047 gets a short, cross-referencing Proposed
Amendment (not a rewrite of its own "two CMake targets" Decision, which
this Spec's design deliberately does not touch — see Proposed Design's
own "Startup layer placement" above). See
[ADR-0076](../adr/0076-runtime-sample-scene-selection-boundary.md)
(`Proposed`) for the full decision record.

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
- **A new, third CMake target dedicated to the CLI layer** (e.g.
  `atlantis_runtime_cli`): rejected — `atlantis_runtime_host` already
  exists for precisely "private, testable, no external consumer" code;
  a third target would need its own ADR-0047 Amendment for the "two
  CMake targets" Decision itself, real additional CMake surface this
  Spec's own narrow scope does not need.

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
- **Real-window verification, one scene per process.** This
  codebase's own Windows Platform lifecycle is a real, already-
  documented per-process singleton (double-initialization/double-
  shutdown are assertion failures, `windows_platform.cpp`) — so the
  three scenes are verified via three *separate* real
  `atlantis_runtime.exe --scene <name>` process launches (never three
  scenes cycled inside one process), each confirming: successful
  window creation, a real frame renders, Vulkan Validation Layers stay
  clean throughout, and a clean shutdown (`RuntimeExitReason::Success`,
  exit `0`). The existing manual-verification precedent (Spec 0010's
  own registry entry: interactive windowed regression "performed and
  observed directly by a human verifier") is the model this Spec's own
  Plan should follow for this exact reason.
- **No new golden.** Rendering content, algorithms, and every existing
  golden stay untouched — this Spec only adds a startup-time selection
  path onto already-verified content; the existing per-scene
  image-regression goldens (`ibl_material_demo`/`pbr_normal_map_demo`/
  `integrated_showcase_demo`, all three already committed) remain the
  authoritative, unaffected coverage of each scene's own rendered
  output.

## Risks & Open Questions

- Exact `ATLANTIS_RUNTIME_*` compile-definition macro names for the two
  new scene triples, and the exact `cli.h`/`cli.cpp` function
  signatures/return-type shape, are Plan-stage detail, not fixed here
  (matching Spec 0013 item 8's own established precedent for this
  class of decision).
- Whether each scene also gets a distinct window title
  (`BootstrapConfig::applicationName`) is a small, optional Plan-stage
  enhancement, not required by this Spec's own Goals.
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
