# Plan: Runtime Sample Scene Selection

- **Spec:** [specs/0032-runtime-sample-scene-selection.md](../specs/0032-runtime-sample-scene-selection.md) (`Approved`)
- **Status:** Approved / **Implemented in [PR #141](https://github.com/slmao/Atlantis/pull/141); pending merge** (not yet merged — this Plan is not "done" until a human merges that PR)
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Related ADR(s):** [ADR-0076](../adr/0076-runtime-sample-scene-selection-boundary.md) (`Accepted`); [ADR-0047's own Accepted Amendment — 2026-09-09](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md#accepted-amendment--2026-09-09) (`Accepted`, boundary reference only)
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  Batch 8 PR (pending). Original scope, both Milestones, and the full
  Verification Checklist retained.

## Objective

Implement Spec 0032 exactly as approved: `atlantis_runtime` gains
`int main(int argc, char** argv)`, a private `src/runtime/cli.h`/
`cli.cpp` pair (outside `atlantis_runtime_host`, compiled directly into
both `atlantis_runtime` and `atlantis_runtime_tests`) that pure-function
parses `--scene <name>` / `--list-scenes` / `--help` against a
caller-injected, three-entry whitelist, and `src/runtime/CMakeLists.txt`
gains the two missing scene-target dependencies so
`ATLANTIS_BUILD_TESTS=OFF` cooks all three scenes. No-argument behavior
stays byte-for-byte identical to today; no golden changes.

## Milestones / Task Breakdown

Two atomic, strictly-ordered milestones. M1 is self-contained (new
files only, zero change to `atlantis_runtime`'s own production
behavior) and independently buildable/testable. M2 wires M1's layer
into `main.cpp` and `src/runtime/CMakeLists.txt`, changing production
behavior for the first time, and carries the full verification matrix.

### Milestone 1 — Private CLI layer + GPU-independent coverage

**New files:**

- `src/runtime/cli.h`
- `src/runtime/cli.cpp`
- `tests/runtime/cli_tests.cpp`

**Modified files:**

- `tests/runtime/CMakeLists.txt` — add `cli.cpp` (relative-path source
  reference into `src/runtime/`, mirroring
  `tests/shader_system/json_parser_tests.cpp`'s own `#include
  "../../src/shader_system/src/json_parser.h"` precedent, adapted: here
  the `.cpp` itself is recompiled as a second translation unit, not
  merely its header included against an already-linked library, because
  `cli.cpp` has no home library to link against — see Spec 0032
  Requirement 1 / ADR-0076 Decision 2) and `cli_tests.cpp` to
  `atlantis_runtime_tests`'s own source list. No other target in this
  file changes.

`atlantis_runtime`/`main.cpp`/`src/runtime/CMakeLists.txt` are
untouched in this milestone — the repository builds and every existing
test passes completely unmodified in behavior; only new tests are
added.

#### `src/runtime/cli.h` — fixed, final content

```cpp
#pragma once

// Private to atlantis_runtime's own startup layer. NOT under
// src/runtime/include/atlantis/runtime/ (Atlantis::RuntimeHost's own
// public directory), NOT compiled into atlantis_runtime_host --
// ADR-0076 Decision 2. The same cli.cpp this header declares is
// compiled directly into both atlantis_runtime and the existing
// GPU-independent atlantis_runtime_tests target (Spec 0032
// Requirement 1) -- a shared source file across two executable
// targets, not a library, not a new CMake target.

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace atlantis::runtime::cli {

// Owned copy of exactly the three BootstrapConfig fields this Spec's
// own CLI surface varies (Spec 0032 Requirement 5). Every other
// BootstrapConfig field (all shader-pair paths, environment paths,
// enableValidationLayers, applicationName) is untouched by this type
// or by parseCommandLine() below -- a structural guarantee, not
// something a test needs to separately assert.
struct SceneBootstrapPaths {
  std::string sceneArtifactPath;
  std::string sceneMetadataPath;
  std::string sceneDependencyManifestPath;
};

// One whitelist entry. `name` is a non-owning view: every real caller
// (main.cpp) constructs entries from string literals (the three fixed
// scene names have static storage duration), so `name` trivially
// outlives any parseCommandLine() call; `paths` is owned by the entry.
// Borrowed access never implies ownership transfer (AGENTS.md
// Ownership rules) -- the `whitelist` span parseCommandLine() takes
// below is valid only for the duration of that one call; every string
// parseCommandLine() returns is a fresh owned copy, never a view into
// argv or whitelist.
struct SceneWhitelistEntry {
  std::string_view name;
  SceneBootstrapPaths paths;
};

// The name selected when --scene is omitted -- matches today's
// unconditional atlantis_runtime default, byte-for-byte (Spec 0032
// Requirement 3). The caller's whitelist MUST contain an entry with
// this exact name; parseCommandLine() treats a missing default as a
// caller/programmer-error precondition violation (assert), never a
// runtime error -- that distinction matches AGENTS.md's Error
// handling rule ("programmer errors are assertions, not error
// returns"; malformed --scene *user* input, by contrast, is real
// untrusted input and always produces a PrintErrorAndExit result,
// never an assertion).
inline constexpr std::string_view kDefaultSceneName = "integrated_showcase_demo";

enum class CommandLineOutcome {
  RunScene,           // .selectedScene is populated; proceed to BootstrapConfig/createRuntimeApplication().
  PrintUsageAndExit,   // .message is --help or --list-scenes text; print to stdout, exit RuntimeExitReason::Success.
  PrintErrorAndExit,   // .message is a diagnostic + usage; print to stderr, exit RuntimeExitReason::InitializationFailed.
};

struct CommandLineResult {
  CommandLineOutcome outcome;
  std::string message;                               // empty when outcome == RunScene
  std::optional<SceneBootstrapPaths> selectedScene;    // populated only when outcome == RunScene
};

// Pure function: no I/O, no std::exit, no read of any CMake macro,
// environment variable, or other process state beyond argc/argv and
// whitelist (Spec 0032 Requirement 5) -- this is what lets a
// GPU-independent test call it with a fixture-only whitelist instead
// of real build paths. `whitelist` must contain exactly the three
// entries Spec 0032 Requirement 2 fixes (order does not affect lookup
// correctness, but fixes --list-scenes' own output order -- callers
// SHOULD use Requirement 2's stated order: integrated_showcase_demo,
// ibl_material_demo, pbr_normal_map_demo) and must include an entry
// named kDefaultSceneName. parseCommandLine() does not itself enforce
// whitelist size or name uniqueness beyond that one precondition --
// exactly the same caller-responsibility contract BootstrapConfig's
// own header comment already establishes for its caller-populated
// fields.
[[nodiscard]] CommandLineResult parseCommandLine(int argc, char** argv,
                                                  std::span<const SceneWhitelistEntry> whitelist);

}  // namespace atlantis::runtime::cli
```

#### `src/runtime/cli.cpp` — fixed algorithm (final; Implementation transcribes this directly, no remaining design choice)

Single left-to-right scan over `argv[1..argc)`, failing fast at the
first malformed token (deterministic: exactly one diagnostic per
invalid invocation, matching `src/tools/asset_cooker/main.cpp`'s own
"stop scanning is not required, but exactly one usage/diagnostic
message is printed" spirit — here fail-fast so the message always names
the *first* problem, never a later one silently masked).

```cpp
#include "cli.h"

#include <atlantis/assert.h>

namespace atlantis::runtime::cli {

namespace {

// Verbatim from Spec 0032's own already-approved "CLI surface" text
// (Proposed Design) -- both --help's own message and every error
// message's own trailing usage block reuse this one constant.
constexpr std::string_view kUsageText =
    "usage: atlantis_runtime [--scene <name>]\n"
    "       atlantis_runtime --list-scenes\n"
    "       atlantis_runtime --help\n"
    "\n"
    "<name> is one of: integrated_showcase_demo, ibl_material_demo, pbr_normal_map_demo\n";

[[nodiscard]] const SceneWhitelistEntry* findByName(std::span<const SceneWhitelistEntry> whitelist,
                                                      std::string_view name) {
  for (const auto& entry : whitelist) {
    if (entry.name == name) return &entry;
  }
  return nullptr;
}

// Every PrintErrorAndExit path shares one message shape: "atlantis_runtime: <diagnostic>\n\n<usage>".
[[nodiscard]] CommandLineResult makeError(std::string_view diagnostic) {
  CommandLineResult result;
  result.outcome = CommandLineOutcome::PrintErrorAndExit;
  result.message = std::string("atlantis_runtime: ").append(diagnostic).append("\n\n").append(kUsageText);
  return result;
}

}  // namespace

CommandLineResult parseCommandLine(int argc, char** argv, std::span<const SceneWhitelistEntry> whitelist) {
  int sceneCount = 0;
  int helpCount = 0;
  int listScenesCount = 0;
  std::string_view requestedSceneName;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--scene") {
      if (++sceneCount > 1) return makeError("--scene supplied more than once");
      if (i + 1 >= argc) return makeError("--scene requires a value");
      requestedSceneName = argv[++i];
    } else if (arg == "--help") {
      if (++helpCount > 1) return makeError("--help supplied more than once");
    } else if (arg == "--list-scenes") {
      if (++listScenesCount > 1) return makeError("--list-scenes supplied more than once");
    } else {
      // Covers an unknown flag, a bare positional argument, and the
      // explicitly-unsupported single-token "--scene=<name>" form
      // (Spec 0032 Non-Goals / Alternatives Considered) -- none of
      // these ever literally equal "--scene"/"--help"/"--list-scenes",
      // so all three fall into this one bucket by construction, with
      // no special-cased message.
      return makeError(std::string("unrecognized argument: ").append(arg));
    }
  }

  const int modesRequested = (sceneCount > 0 ? 1 : 0) + (helpCount > 0 ? 1 : 0) + (listScenesCount > 0 ? 1 : 0);
  if (modesRequested > 1) {
    return makeError("--help, --list-scenes, and --scene may not be combined");
  }

  if (helpCount > 0) {
    CommandLineResult result;
    result.outcome = CommandLineOutcome::PrintUsageAndExit;
    result.message = std::string(kUsageText);
    return result;
  }

  if (listScenesCount > 0) {
    CommandLineResult result;
    result.outcome = CommandLineOutcome::PrintUsageAndExit;
    std::string names;
    for (const auto& entry : whitelist) {
      names.append(entry.name).append("\n");
    }
    result.message = std::move(names);
    return result;
  }

  const std::string_view lookupName = sceneCount > 0 ? requestedSceneName : kDefaultSceneName;
  const SceneWhitelistEntry* matched = findByName(whitelist, lookupName);
  if (matched == nullptr) {
    if (sceneCount > 0) {
      return makeError(std::string("unrecognized --scene value: ").append(requestedSceneName));
    }
    // Caller-precondition violation (see cli.h's own kDefaultSceneName
    // comment): the injected whitelist is missing the default entry --
    // a programmer error, not user input. Matches exit_reason.cpp's
    // own ATLANTIS_CHECK_MSG + defensive-fallback-return pattern (the
    // installed assert handler is not guaranteed to terminate).
    ATLANTIS_CHECK_MSG(false, "parseCommandLine(): whitelist is missing the default scene entry");
    return makeError("internal error: default scene entry not found");
  }

  CommandLineResult result;
  result.outcome = CommandLineOutcome::RunScene;
  result.selectedScene = matched->paths;
  return result;
}

}  // namespace atlantis::runtime::cli
```

#### Full CLI behavior table (fixed; `cli_tests.cpp` has one `TEST_CASE`/`SECTION` per row)

All 23 rows are exercised purely in-process against a 3-entry fixture
whitelist (fake, distinguishable path strings per entry, e.g.
`"/fake/a/..."` / `"/fake/b/..."` / `"/fake/c/..."` — never real build
paths, satisfying Requirement 5's own injection contract) — no process
launch, no build-path macro.

| # | `argv` (after program name) | Outcome | `.message` (stdout row) / (stderr row) | Exit code (via `toProcessExitCode`) | Reaches `createRuntimeApplication()`? |
|---|---|---|---|---|---|
| 1 | *(none)* | `RunScene` | — | `Success` (0) on clean shutdown | Yes — default scene |
| 2 | `--scene integrated_showcase_demo` | `RunScene` | — | `Success` (0) | Yes — identical selection to #1 |
| 3 | `--scene ibl_material_demo` | `RunScene` | — | `Success` (0) | Yes |
| 4 | `--scene pbr_normal_map_demo` | `RunScene` | — | `Success` (0) | Yes |
| 5 | `--help` | `PrintUsageAndExit` | stdout: `kUsageText` | `Success` (0) | No |
| 6 | `--list-scenes` | `PrintUsageAndExit` | stdout: 3 names, one per line, whitelist order | `Success` (0) | No |
| 7 | `--scene bogus_name` | `PrintErrorAndExit` | stderr: `"unrecognized --scene value: bogus_name"` + usage | `InitializationFailed` (1) | No |
| 8 | `--scene` *(last token)* | `PrintErrorAndExit` | stderr: `"--scene requires a value"` + usage | `InitializationFailed` (1) | No |
| 9 | `--scene ibl_material_demo --scene ibl_material_demo` | `PrintErrorAndExit` | stderr: `"--scene supplied more than once"` + usage | `InitializationFailed` (1) | No |
| 10 | `--scene ibl_material_demo --scene pbr_normal_map_demo` | `PrintErrorAndExit` | stderr: same as #9 (duplicate flag, value irrelevant) | `InitializationFailed` (1) | No |
| 11 | `--help --help` | `PrintErrorAndExit` | stderr: `"--help supplied more than once"` + usage | `InitializationFailed` (1) | No |
| 12 | `--list-scenes --list-scenes` | `PrintErrorAndExit` | stderr: `"--list-scenes supplied more than once"` + usage | `InitializationFailed` (1) | No |
| 13 | `--foo` | `PrintErrorAndExit` | stderr: `"unrecognized argument: --foo"` + usage | `InitializationFailed` (1) | No |
| 14 | `--scene=ibl_material_demo` *(single token)* | `PrintErrorAndExit` | stderr: `"unrecognized argument: --scene=ibl_material_demo"` + usage | `InitializationFailed` (1) | No |
| 15 | `ibl_material_demo` *(bare positional)* | `PrintErrorAndExit` | stderr: `"unrecognized argument: ibl_material_demo"` + usage | `InitializationFailed` (1) | No |
| 16 | `--scene --help` | `PrintErrorAndExit` | stderr: `"unrecognized --scene value: --help"` + usage | `InitializationFailed` (1) | No |
| 17 | `--scene --list-scenes` | `PrintErrorAndExit` | stderr: `"unrecognized --scene value: --list-scenes"` + usage | `InitializationFailed` (1) | No |
| 18 | `--help --list-scenes` | `PrintErrorAndExit` | stderr: `"--help, --list-scenes, and --scene may not be combined"` + usage | `InitializationFailed` (1) | No |
| 19 | `--list-scenes --help` *(reverse of #18)* | `PrintErrorAndExit` | stderr: same as #18 | `InitializationFailed` (1) | No |
| 20 | `--help --scene ibl_material_demo` | `PrintErrorAndExit` | stderr: same as #18 | `InitializationFailed` (1) | No |
| 21 | `--scene ibl_material_demo --help` *(reverse of #20)* | `PrintErrorAndExit` | stderr: same as #18 | `InitializationFailed` (1) | No |
| 22 | `--list-scenes --scene ibl_material_demo` | `PrintErrorAndExit` | stderr: same as #18 | `InitializationFailed` (1) | No |
| 23 | `--scene ibl_material_demo --list-scenes` *(reverse of #22)* | `PrintErrorAndExit` | stderr: same as #18 | `InitializationFailed` (1) | No |

Rows 16–17 exercise `--scene`'s own unconditional "consume the very
next token as the value" behavior in `cli.cpp` above (`requestedSceneName
= argv[++i];`, no lookahead check on what that token looks like) — the
algorithm needs no special case for this: `"--help"`/`"--list-scenes"`
are simply not whitelist entry names, so `findByName()` fails exactly
as it would for any other unrecognized `--scene` value, and
`modesRequested` never exceeds 1 (the loop already advanced past both
tokens as one `--scene` occurrence). Confirmed consistent with the
fixed algorithm above by inspection, not a separate code path.

`cli_tests.cpp` additionally covers, per Spec 0032's own Testing &
Verification Plan:

- **Three-different-path mapping, no cross-contamination**: three
  independently-constructed fixture whitelists (distinct fake paths per
  entry), one `--scene <name>` call per whitelist per name, asserting
  `.selectedScene` equals *exactly* that whitelist's matching entry's
  `SceneBootstrapPaths` — never another entry's paths, never a partial
  mix.
- **Default-compatibility**: no-argument parse and `--scene
  integrated_showcase_demo` parse against the *same* whitelist produce
  byte-identical `.selectedScene`.
- **Structural "other `BootstrapConfig` fields preserved" guarantee**:
  `SceneBootstrapPaths` has exactly three fields by its own type
  definition (cli.h above) — `parseCommandLine()` cannot, by
  construction, populate or touch any other `BootstrapConfig` field.
  Milestone 2's own `main.cpp` diff additionally leaves every other
  `config.*` assignment line (all ten built-in shader-pair path groups,
  `environmentArtifactPath`/`environmentMetadataPath`,
  `enableValidationLayers`, `applicationName`) completely unchanged
  from today — a reviewable property of that Milestone's own diff, not
  a Catch2 assertion.
- **Missing-default-whitelist precondition** (`kDefaultSceneName`'s own
  documented contract in `cli.h`): a fixture whitelist with only two
  entries, neither named `kDefaultSceneName`, passed to a no-argument
  `parseCommandLine()` call. Verified via the same replaceable
  `atlantis::assertions::setFailureHandler()` mechanism
  `tests/core/assert_tests.cpp` establishes and
  `tests/runtime/scene_extraction_tests.cpp` already reuses for a
  Runtime-domain `ATLANTIS_CHECK_MSG` case (install a recording
  handler, call `parseCommandLine()`, restore the previous handler,
  assert exactly one recorded failure and that the function still
  returned its defensive `PrintErrorAndExit` fallback rather than
  crashing or dereferencing).

`tests/runtime/cli_tests.cpp` includes `cli.h` the same way
`tests/shader_system/json_parser_tests.cpp` includes its own module's
private header — a relative path from the test file's own directory:

```cpp
#include "../../src/runtime/cli.h"
```

### Milestone 2 — Production wiring + full verification matrix

**Modified files:**

- `src/runtime/main.cpp`
- `src/runtime/CMakeLists.txt`
- `specs/README.md` — not part of this Milestone; already updated by
  this Plan's own governance commit (see below), not Implementation.

No other file changes — matches Spec 0032 Requirement 6's fixed,
two-CMake-file scope exactly (`atlantis_runtime_gpu_tests`,
`tests/image_regression/`, `assets/CMakeLists.txt` all untouched; the
three scenes' own asset declarations already exist, unconditionally,
today).

#### `src/runtime/CMakeLists.txt` — fixed diff

Six new `target_compile_definitions()` entries (the two new scenes'
own real, already-exported CMake variables — Spec 0032's own Proposed
Design mapping table) alongside the three existing
`ATLANTIS_RUNTIME_SCENE_{ARTIFACT,METADATA,MANIFEST}_PATH` macros,
which are **left unrenamed** (they already represent
`integrated_showcase_demo`'s own triple today; renaming them for
symmetry alone would be unrelated refactoring — AGENTS.md rule 7 — with
no behavioral benefit, since Requirement 3's own "no-argument launch is
byte-for-byte identical" is a runtime-behavior property, not a
source-symbol-naming one):

```cmake
target_compile_definitions(atlantis_runtime PRIVATE
  # ... all existing entries, unchanged ...
  ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_ARTIFACT_PATH="${ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_METADATA_PATH="${ATLANTIS_ibl_material_demo_scene_METADATA_PATH}"
  ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_MANIFEST_PATH="${ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH}"
  ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_ARTIFACT_PATH="${ATLANTIS_pbr_normal_map_demo_scene_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_METADATA_PATH="${ATLANTIS_pbr_normal_map_demo_scene_METADATA_PATH}"
  ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_MANIFEST_PATH="${ATLANTIS_pbr_normal_map_demo_scene_MANIFEST_PATH}"
)
```

`cli.cpp` is added to the executable's own source list:

```cmake
add_executable(atlantis_runtime main.cpp cli.cpp)
```

`add_dependencies(atlantis_runtime ...)` gains exactly the two new
scene-level targets (closing the real, confirmed gap Spec 0032
Requirement 6 identifies — `atlantis_add_scene_asset()`'s own macro
already internally wires each scene target's `MESH_DEPENDENCIES`/
`MATERIAL_DEPENDENCIES`/`TEXTURE_DEPENDENCIES`, so no other target name
needs adding):

```cmake
add_dependencies(atlantis_runtime minimal_mesh_shaders textured_quad_shaders lit_textured_shaders
  pbr_direct_lit_shaders pbr_ibl_shaders output_transform_unorm_shaders output_transform_srgb_shaders sky_shaders
  shadow_cast_shaders pbr_direct_lit_normal_map_shaders pbr_ibl_normal_map_shaders
  ${ATLANTIS_minimal_cube_TARGET} ${ATLANTIS_integrated_showcase_demo_scene_TARGET} ${ATLANTIS_ground_plane_TARGET}
  ${ATLANTIS_ibl_studio_TARGET} ${ATLANTIS_ibl_material_demo_scene_TARGET} ${ATLANTIS_pbr_normal_map_demo_scene_TARGET})
```

#### `src/runtime/main.cpp` — fixed diff

Only the signature and the top of `main()` change; every existing
shader-path/environment assignment line (today's lines 29–113) is
**untouched** — reproduced here only where it changes:

```cpp
#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/runtime_application.h>

#include "cli.h"

#include <array>
#include <iostream>
#include <string>
#include <utility>

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;
using atlantis::runtime::toProcessExitCode;
using atlantis::runtime::cli::CommandLineOutcome;
using atlantis::runtime::cli::CommandLineResult;
using atlantis::runtime::cli::parseCommandLine;
using atlantis::runtime::cli::SceneBootstrapPaths;
using atlantis::runtime::cli::SceneWhitelistEntry;

int main(int argc, char** argv) {
  // Fixed order matches Spec 0032 Requirement 2 exactly -- also fixes
  // --list-scenes' own output order (cli.cpp prints whitelist order
  // verbatim). Real, absolute, CMake-injected paths only -- cli.h's
  // own parseCommandLine() never reads a macro itself (Requirement 5).
  const std::array<SceneWhitelistEntry, 3> whitelist{{
      {"integrated_showcase_demo",
       SceneBootstrapPaths{ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH, ATLANTIS_RUNTIME_SCENE_METADATA_PATH,
                            ATLANTIS_RUNTIME_SCENE_MANIFEST_PATH}},
      {"ibl_material_demo",
       SceneBootstrapPaths{ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_ARTIFACT_PATH,
                            ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_METADATA_PATH,
                            ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_MANIFEST_PATH}},
      {"pbr_normal_map_demo",
       SceneBootstrapPaths{ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_ARTIFACT_PATH,
                            ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_METADATA_PATH,
                            ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_MANIFEST_PATH}},
  }};

  const CommandLineResult cliResult = parseCommandLine(argc, argv, whitelist);
  if (cliResult.outcome == CommandLineOutcome::PrintUsageAndExit) {
    std::cout << cliResult.message;
    return toProcessExitCode(RuntimeExitReason::Success);
  }
  if (cliResult.outcome == CommandLineOutcome::PrintErrorAndExit) {
    std::cerr << cliResult.message;
    return toProcessExitCode(RuntimeExitReason::InitializationFailed);
  }

  atlantis::log::setMinLevel(atlantis::LogLevel::Info);
  ATLANTIS_LOG_INFO("Atlantis Runtime starting");

  BootstrapConfig config;
  config.applicationName = "Atlantis Runtime";
  // ... every shader-path/asset/environment assignment line below is
  // byte-for-byte unchanged from today ...
  config.sceneArtifactPath = cliResult.selectedScene->sceneArtifactPath;
  config.sceneMetadataPath = cliResult.selectedScene->sceneMetadataPath;
  config.sceneDependencyManifestPath = cliResult.selectedScene->sceneDependencyManifestPath;
  // ... unlitTextured*/litTextured*/pbrDirectLit*/environment*/pbrIbl*/
  // sky*/shadowCast*/outputTransform*/enableValidationLayers all
  // unchanged from today's main.cpp ...

  auto appResult = createRuntimeApplication(config);
  // ... rest of main() (createRuntimeApplication() error handling,
  // the shouldContinue()/runFrame() loop, shutdown()) entirely
  // unchanged from today ...
}
```

`atlantis::log::setMinLevel`/`ATLANTIS_LOG_INFO("Atlantis Runtime
starting")` move to *after* the CLI check (today they are the first two
statements) — deliberately, so `--help`/`--list-scenes`/every error
path prints nothing but its own `.message`, with zero log output,
before exiting; this is the fixed, concrete mechanism behind Spec
0032's own verification claim that these paths are silent /
Runtime-never-started (see Verification Checklist below).

The `config.sceneArtifactPath = cliResult.selectedScene->...` lines are
only reached when `cliResult.outcome == CommandLineOutcome::RunScene`,
which is the only outcome for which `.selectedScene` is populated
(`cli.h`'s own documented contract) — no null-optional-dereference
risk.

## Files / Modules Touched (expected)

- **New**: `src/runtime/cli.h`, `src/runtime/cli.cpp` (M1),
  `tests/runtime/cli_tests.cpp` (M1).
- **Modified**: `tests/runtime/CMakeLists.txt` (M1 — `atlantis_runtime_tests`'
  own source list only; zero change to `atlantis_runtime_gpu_tests`);
  `src/runtime/main.cpp`, `src/runtime/CMakeLists.txt` (M2).
- **Governance-only** (this Plan's own drafting commit, not a
  Milestone): `specs/README.md` — Plan 0032 link updated from "None
  yet" to this file.
- **Untouched, confirmed**: `src/runtime/include/atlantis/runtime/*`
  (no `BootstrapConfig`/`RuntimeApplication` field, method, or public
  API change — ADR-0076 Decision 3), `assets/CMakeLists.txt` (all
  three scenes already declared), `tests/image_regression/*`,
  `tests/runtime/runtime_smoke_gpu_tests.cpp` and every other
  `atlantis_runtime_gpu_tests` source (Requirement 6), every existing
  golden.

## Sequencing & Dependencies

M1 → M2, strictly: M2's `main.cpp` calls `atlantis::runtime::cli::parseCommandLine()`,
which requires M1's `cli.h`/`cli.cpp` to exist and already be covered
by `cli_tests.cpp` (a parsing bug is far cheaper to find via M1's
GPU-independent tests than after M2 wires it into the real executable).
M1 alone builds and passes every existing test unmodified, plus the new
`cli_tests.cpp` coverage. M1+M2 together build, cook all three scenes
under both `ATLANTIS_BUILD_TESTS` settings, and pass every existing
test unmodified in behavior (no-argument launch is byte-identical to
today).

## Verification Checklist

- [ ] **Unit tests (GPU-independent)**: `tests/runtime/cli_tests.cpp` —
      all 23 CLI-behavior-table rows, the three-different-whitelist
      mapping/no-cross-contamination cases, the missing-default-whitelist
      precondition case, and the default-compatibility case (all
      in-process, fixture whitelists only, no process launch,
      no build-path macro — Requirement 5).
- [ ] **Real-process verification — `--help`/`--list-scenes`/every
      invalid-argument row (table rows 5–23).** Run via a temporary,
      not-committed PowerShell script (no new product dependency, no new
      CMake target) that invokes the real, already-built
      `atlantis_runtime.exe` once per row, capturing stdout/stderr
      separately and the exit code, with a timeout so a hung process
      cannot block Verification:

      ```powershell
      function Invoke-AtlantisRuntimeCase([string]$exePath, [string[]]$cliArgs) {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $exePath
        $psi.Arguments = ($cliArgs -join ' ')
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.UseShellExecute = $false
        $p = [System.Diagnostics.Process]::Start($psi)
        $stdout = $p.StandardOutput.ReadToEnd()
        $stderr = $p.StandardError.ReadToEnd()
        if (-not $p.WaitForExit(5000)) { $p.Kill(); throw "timed out: $cliArgs" }
        [PSCustomObject]@{ ExitCode = $p.ExitCode; StdOut = $stdout; StdErr = $stderr }
      }
      ```

      Looped once per row 5–23 (each row's own `argv` tokens as
      `$cliArgs`), comparing `.ExitCode`/`.StdOut`/`.StdErr` against
      that row's fixed expectation (message text matched as a substring,
      exit code matched exactly). Recorded PASS/FAIL per row in the
      Implementation PR's own "Manual verification record." **Evidence
      that Runtime never started, in priority order**: (1) the reviewed
      `main.cpp` diff (Milestone 2 below) shows every `PrintUsageAndExit`/
      `PrintErrorAndExit` branch `return`s before `BootstrapConfig` is
      even constructed, let alone `createRuntimeApplication()` called —
      a structural property of the code, verified by review, not by
      execution; (2) the process's own real stdout/stderr/exit code
      match that row's expectation exactly (a mismatch here would itself
      be the primary failure signal); (3) the absence of `"Atlantis
      Runtime starting"` in stdout is supplementary corroboration only
      — never the sole evidence a row passed.
- [ ] **Real-window verification, one scene per process, from more than
      one working directory — manual, recorded.** This is the only step
      that needs a human: a real rendered window, judged by eye, has no
      automated substitute in this repository (see [Spec 0010's own
      precedent](../specs/README.md), "interactive windowed regression
      ... performed and observed directly by a human verifier"). Four
      launches total: `atlantis_runtime.exe` (no argument), `--scene
      integrated_showcase_demo`, `--scene ibl_material_demo`, `--scene
      pbr_normal_map_demo` — each its own separate process
      (`src/platform/src/windows/windows_platform.cpp`'s own
      `shutdown()` assertion — "called without a successful
      initialize(), or called twice" — and its own "re-initialization
      after shutdown() is unsupported in Phase 1 ... not designed, not
      guarded" comment are the real, cited justification for why three
      scenes are never cycled inside one process). At least one launch
      (plus the no-argument default) additionally runs from a working
      directory other than the build output directory. **Fixed
      observation/shutdown method, per launch**: a human verifier
      directly watches the real window appear with the expected scene
      content on screen (not merely a log line — an initialization log
      line is never treated as confirmation of a rendered frame),
      confirms Vulkan Validation Layers output is clean for that
      process's own console/log capture, then closes the window and
      confirms the process exits with `RuntimeExitReason::Success` (0).
      Recorded PASS/FAIL per launch/configuration in the Implementation
      PR's own "Manual verification record," mirroring PR #48's (Spec
      0010) exact format.
- [ ] **Image regression tests**: all 10 existing goldens re-confirmed
      byte-identical via a real, executed capture-compare run (`ctest -L
      gpu` under `tests/image_regression/`) — no new golden, no Spec
      0030 shadow-bias work revived.
- [ ] **Vulkan Validation Layers clean**: Debug and Release, for every
      real-window launch above and for `ctest -L gpu`'s own existing
      GPU test suite (unaffected by this Plan, re-run as a regression
      check).
- [ ] **Other**:
  - Fresh `ATLANTIS_BUILD_TESTS=OFF` configure + build of
    `atlantis_runtime` only: `ibl_material_demo_scene`/
    `pbr_normal_map_demo_scene`'s own artifact/metadata/manifest files
    exist on disk afterward (manual, recorded — matches Plan
    0017/0020's own established `ATLANTIS_BUILD_TESTS=OFF` verification
    precedent).
  - Debug and Release builds clean, no new compiler warnings
    (`/w14062` untouched — `cli.cpp` does not touch
    `atlantis_runtime_host`'s own enum-exhaustiveness-checked switches).
  - **Module-boundary re-check, matched to the real, fixed include
    forms above** (`main.cpp`/`cli.cpp` both use `#include "cli.h"`;
    `cli_tests.cpp` uses `#include "../../src/runtime/cli.h"` — a bare
    `grep -rn "runtime/cli.h"` would miss the first two entirely, since
    neither literally contains the substring `"runtime/"`):
    1. `grep -rn '"cli\.h"' src/ tests/` — expect exactly three matches:
       `src/runtime/main.cpp`, `src/runtime/cli.cpp`,
       `tests/runtime/cli_tests.cpp`. Any other match means `cli.h`
       leaked outside its intended two targets.
    2. `grep -n "cli\.cpp" src/runtime/CMakeLists.txt` — expect it to
       appear only on the `add_executable(atlantis_runtime ...)` line,
       never inside the `add_library(atlantis_runtime_host STATIC ...)`
       block above it.
    3. `grep -n "cli\.cpp" tests/runtime/CMakeLists.txt` — expect it to
       appear only on `atlantis_runtime_tests`'s own `add_executable(...)`
       line, never inside `atlantis_runtime_gpu_tests`'s.
  - `git diff --check` clean.
  - **Default-mapping correctness (replaces logging-based comparison —
    `runtime_application.cpp` does not log a full `BootstrapConfig` at
    any level; no such mechanism exists to diff against)**: (1) review
    the `main.cpp` diff itself and confirm only the three
    `config.sceneArtifactPath`/`sceneMetadataPath`/
    `sceneDependencyManifestPath` assignment lines changed from a
    literal macro to `cliResult.selectedScene->...`, with every other
    `config.*` line byte-identical to today's; (2) `cli_tests.cpp`'s own
    default-compatibility case (above) already proves no-argument and
    `--scene integrated_showcase_demo` select identical paths; (3)
    confirm the whitelist's `"integrated_showcase_demo"` entry in
    `main.cpp` is literally built from
    `ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH`/`_METADATA_PATH`/
    `_MANIFEST_PATH` — the exact three macros already driving today's
    unconditional default, unrenamed; (4) the real-window verification
    above already runs both the no-argument and the explicit
    `--scene integrated_showcase_demo` launches, confirming identical
    real rendered output. No new logging, public API, or configuration
    system is added for this check.

## Rollback Plan

Revert the Implementation PR's own commits on
`feature/0032-runtime-sample-scene-selection` (or the merge commit on
`main`, if already merged). Every change is additive/mechanical:
`cli.h`/`cli.cpp` are new, isolated files; `main.cpp`'s only behavioral
change for a no-argument launch is where the two log lines execute
(after, not before, the CLI check — no other statement changes for that
path); `src/runtime/CMakeLists.txt`'s new macros/dependencies are pure
additions. No data migration, no golden touched, no `BootstrapConfig`/
`RuntimeApplication` public-API change — a revert restores today's
exact prior behavior with no follow-up cleanup.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No delta beyond: this repository has no CI pipeline yet, so "CI green"
is reported as not applicable, exactly as every prior PR in this
repository already does. Real-process rows are verified by a
not-committed PowerShell script, run and recorded by whoever performs
Verification; real-window verification is the one genuinely manual,
human-observed step — see Verification Checklist above for both.

## Human Review Approval — 2026-09-10

**Status: Approved.** Recorded against
[PR #140](https://github.com/slmao/Atlantis/pull/140). Human Review's
own words: *"我认可 Plan 0032，并确认 Spec 0032 与 Plan 0032 的联合
Human Review 通过；批准记录随 PR #140 合并后可开始实现"* ("I approve
Plan 0032, and confirm the joint Human Review of Spec 0032 and Plan
0032 has passed; the approval record takes effect for starting
Implementation once PR #140 merges").

This is a **joint** approval of Spec 0032 (already `Approved`,
2026-09-10) together with this Plan — not a second, independent review
of the Spec's own design text, and not a reopening of it. This Plan's
own approval covers it in full, as drafted (including this round's own
corrections): the two strictly-ordered Milestones (M1: the private,
executable-scoped `cli.h`/`cli.cpp` pair, compiled as a shared source
file into both `atlantis_runtime` and `atlantis_runtime_tests`, with
its caller-injected whitelist and the full 23-row CLI behavior table;
M2: `main.cpp`/`src/runtime/CMakeLists.txt` production wiring, exactly
the two CMake files Requirement 6 fixes); the not-committed PowerShell
real-process verification script and the human-observed real-window
verification; and the byte-identical, no-new-golden gate on all 10
existing goldens — with no change to this Plan's own already-drafted
text.

**This approval authorizes starting Implementation only once
[PR #140](https://github.com/slmao/Atlantis/pull/140) itself has merged
to `main` — not before.**

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-10, jointly approving Spec 0032 and this
Plan, with no change to either's already-drafted text.
