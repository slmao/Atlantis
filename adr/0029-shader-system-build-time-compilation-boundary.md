# ADR 0029: Shader System — Build-Time Compilation Boundary, CLI-vs-Library Decision, and Tools Integration

- **Status:** Proposed
- **Date:** 2026-08-13 (revised 2026-08-14 — see Revision History)
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Revision History

- **2026-08-13 (original):** Proposed a build-time Tools CLI wrapping a
  Shader System library that itself shelled out to `glslc`.
- **2026-08-14 (revised):** Superseded by this version, following the
  same human-directed re-evaluation toward Slang described in
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)'s
  Revision History. This revision also **narrows and firms up the module
  split** between `Atlantis::ShaderSystem` and Atlantis Tools' CLI: the
  original version left Shader System's library itself responsible for
  invoking the compiler subprocess; this revision moves subprocess
  invocation entirely into Tools, keeping Shader System's public API
  free of any OS-process concept — an explicit, reviewed boundary
  decision, not left open for a future Plan to choose between two
  architecturally different shapes.

## Context

[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md) fixed a
narrow, temporary rule for Spec 0007: no shader compiler is invoked by
any Atlantis build target, and a human runs `glslc` manually, checking
the resulting `.spv` bytes into the repository. That ADR's own Migration
Boundary names this as the exact gap a future Shader System spec is
expected to close: real, automated compilation replacing manual,
checked-in bytecode.

[ADR-0028](0028-shader-system-source-language-and-compiler.md) fixes
*what* compiles shader source (Slang, targeting Vulkan/SPIR-V, sourced as
a prebuilt binary from the Vulkan SDK). This ADR fixes *when*, *by what
mechanism*, and — the question this revision resolves explicitly rather
than deferring — **whether Shader System links Slang's compiler library
directly, or Atlantis Tools launches Slang's `slangc` CLI as a
subprocess.** It also draws the boundary [AGENTS.md](../AGENTS.md) and
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
already require: Renderer and Vulkan Backend never invoke a shader
compiler; Shader System is an artifact producer, not a Renderer
dependency; Runtime does not compile shaders.

### The compiler-library-vs-CLI question, decided with evidence

Slang officially ships two consumption paths: "an offline compiler (a
binary for multiple operating systems) and a library for runtime
compilation"
([docs.vulkan.org — Slang Shading Language in Vulkan](https://docs.vulkan.org/guide/latest/slang.html)).
The library path (`Session`/`Module`/`ComponentType`/`EntryPoint`,
COM-style reference-counted objects managed via `Slang::ComPtr<>`,
diagnostics returned via a `diagnostics` blob parameter rather than
exceptions, `SlangResult` return codes) is documented in Slang's
Compilation API tutorial
([docs.shader-slang.org — Using the Slang Compilation API](https://docs.shader-slang.org/en/latest/compilation-api.html)).
The CLI path (`slangc`) is documented in the Command Line Reference
([shader-slang/slang `docs/command-line-slangc-reference.md`](https://github.com/shader-slang/slang/blob/master/docs/command-line-slangc-reference.md)),
which confirms `slangc` accepts `-target spirv`, `-profile`/`-capability`
flags selecting SPIR-V version (`spirv_1_{0..5}`), a repeatable `-entry`
flag ("Multiple `-entry` options may be used in a single invocation"),
an `-o <path>` output flag, and — critically —
**`-reflection-json <path>`, documented as "Emit reflection data in
JSON format to a file"** (same source). This single CLI flag already
produces exactly the structured reflection output
[ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)
needs, with no library linkage required.

Weighed against each of the axes this decision must account for:

| Axis | Library (link `slang`) | CLI (`slangc` subprocess) |
|---|---|---|
| Errors/diagnostics | A `diagnostics` blob object, parsed in C++ | `slangc`'s own stderr text, already human/build-log-readable, no parsing needed |
| Structured reflection | Requires walking `ShaderReflection`/`VariableLayoutReflection` objects in C++ | `-reflection-json` already emits it as a file — zero reflection-object-walking code in Atlantis |
| Unit testing | Requires mocking COM-style interfaces, or running real (slow, environment-dependent) Slang API calls in tests | The subprocess boundary is trivially fakeable (a fixture that returns canned exit codes/files); the *consumer* of the resulting JSON/`.spv` is what Shader System's own unit tests exercise, with no Slang object involved at all |
| Process-launch overhead | None (in-process call) | One process launch per invocation; officially, "long-lived sessions" are recommended for cache efficiency across *many* compiles in one process ([docs.shader-slang.org — Compilation API](https://docs.shader-slang.org/en/latest/compilation-api.html)) — a benefit only realized by staying in-process across many shaders, which Phase 1's shader count (a small, fixed handful) does not need |
| Version discovery | Requires querying the linked library's own version at build/link time | The resolved Vulkan SDK's own version is already known at `find_program()` time (see [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)) |
| Tool/deployment | Must redistribute `slang-compiler.dll`/`libslang-compiler.so` alongside any executable that links it, per Slang's own build docs ("downstream users... distributing their products as binaries should... redistribute the Slang libraries they linked against," [shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md)) — real even for a build-time-only tool, since the Tools executable itself becomes a "product" carrying that obligation in every build tree | `slangc.exe` and its own dependent DLLs are already correctly co-located by the Vulkan SDK installer at `%VULKAN_SDK%\Bin\`; nothing new for Atlantis's own build output to carry |
| Incremental builds | No difference — either way, CMake's own `add_custom_command()` staleness tracking governs when compilation reruns | No difference |
| Cross-platform/host builds | Requires either building Slang from source (a real commitment: recursive submodule clone, CMake ≥ 3.26, C++17, [shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md)) on every host platform, or relying on a prebuilt release whose `find_package`-readiness for downloaded (not self-built) binaries was still an open upstream request as of November 2024 ([issue #5649](https://github.com/shader-slang/slang/issues/5649)) | `slangc.exe` ships prebuilt, already tested by LunarG across every Vulkan-SDK-supported host platform (Windows, Linux, macOS) — no from-source Slang build needed anywhere in Atlantis's own build graph |
| Ownership/lifetime/threading | Atlantis's own C++ code must correctly manage `Session`/`Module`/`ComponentType` COM-style object lifetimes, with no official thread-safety guarantee documented | No Slang object lifetime crosses into Atlantis's own code at all — a subprocess's entire lifetime is the OS process lifetime, already a well-understood, already-used pattern (Vulkan SDK tool invocation) in this codebase's build system |

**Every axis favors the CLI path for Phase 1's actual need** (a small,
fixed handful of shaders, compiled at build time, not thousands of
shaders needing session-reuse compile-time optimization). This is not a
close call decided by convention — the evidence above is why this ADR
does not leave the choice to a future Plan.

## Decision

**Shader compilation and reflection happen exclusively at build time, by
Atlantis Tools' CLI executable invoking `slangc` as a subprocess. Shader
System is a library that owns the reflection-metadata *schema*, its
loader, and the RHI-shape mapping helper — it never links Slang, never
spawns a process, and never touches an OS process API.**

### Module split — explicit, not deferred to Plan

- **`Atlantis Shader System` (`src/shader_system/`) is a library**
  (`atlantis_shader_system`, alias `Atlantis::ShaderSystem`), depending
  only on `Atlantis::Core`, per
  [module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  existing, unchanged "Shader System depends on Core" rule. Its public
  API is exactly:
  - **A reflection-metadata schema** — Atlantis's own, narrow, fixed
    JSON shape (see
    [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)),
    populated *from* Slang's own `-reflection-json` output by a
    transformation step this library implements, not Slang's raw JSON
    schema re-exposed verbatim (insulating the rest of Atlantis from
    Slang's own JSON shape evolving across Slang releases — Slang's
    reflection JSON output is not documented anywhere as a stable,
    versioned public schema in its own right).
  - **A loader** for that schema (file path → `atlantis::Result<ReflectionMetadata,
    ReflectionLoadError>`).
  - **A pure, data-only command-line-construction helper** — given a
    shader source path, an entry-point name, and an output directory,
    returns the exact `slangc` argument list (`std::vector<std::string>`
    or equivalent) Tools' CLI should invoke, and the exact output file
    names/paths it should expect — kept in Shader System so this naming/
    flag *contract* has exactly one owner, even though Shader System
    itself never executes that argument list.
  - **No compile()/reflect() function that spawns a process.** Shader
    System's public API never touches `CreateProcess`/`fork`+`exec`,
    `std::system`, or any OS-process type — this is what keeps it a
    small, portable, Core-only-dependent library, unit-testable with no
    real Slang installation required for anything except the transform-
    step's own input-fixture tests.
- **`Atlantis Tools` (`src/tools/shader_compiler_cli/` or equivalent —
  exact path a Plan-stage detail) gets its first real content: a small
  command-line executable** (e.g. `atlantis_shader_compiler`, exact name
  a Plan-stage detail) that takes a shader source path and entry-point
  name(s) on the command line, calls Shader System's command-line-
  construction helper to build the `slangc` argument list, **spawns
  `slangc` as a subprocess itself**, checks its exit code, calls Shader
  System's reflection-transformation step on the resulting Slang JSON
  output, and writes the final `.spv` and Atlantis-schema reflection JSON
  to the caller-specified output directory. Depends on
  `Atlantis::ShaderSystem` and `Atlantis::Core`. This realizes
  [module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  existing "Tools: shader precompilation CLI" responsibility and "Tools
  depends on ... Shader System" dependency edge for the first time.
- **This split is deliberate**, not incidental: it keeps `Atlantis::RHI`,
  `Atlantis::Renderer`, `Atlantis::RenderGraph`, and
  `Atlantis::VulkanBackend` — none of which need or want an OS-process
  dependency — able to depend on `Atlantis::ShaderSystem`'s reflection
  types/loader (for the RHI-mapping seam, see
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  **without pulling in process-spawning code they never call.** Only
  Atlantis Tools' CLI executable — a build-time-only, never-shipped
  program — ever spawns `slangc`.

### CMake integration

- CMake locates `slangc` via `find_program()`, sourced from the Vulkan
  SDK already required by the Vulkan Backend
  ([ADR-0006](0006-dependency-management.md),
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)) — the
  same `%VULKAN_SDK%\Bin\` (or platform-equivalent) location `glslc` was
  already found at, confirmed to also contain Slang as of the currently-
  installed SDK version
  ([Vulkan SDK 1.4.357.0 release notes](https://vulkan.lunarg.com/doc/view/latest/windows/release_notes.html)).
  A missing `slangc` fails CMake **configure**, not build, and not
  silently: `find_program()` failing produces `message(FATAL_ERROR ...)`
  naming the missing Vulkan SDK component, before any shader-consuming
  target is configured.
- Every shader source file to be compiled is declared to CMake via
  `add_custom_command()`, with the source `.slang` file as `DEPENDS`,
  Atlantis Tools' CLI executable as the `COMMAND`, and the `.spv`/
  Atlantis-schema-reflection-JSON pair as `OUTPUT` — giving correct
  incremental rebuild behavior with no custom staleness-tracking code.
- **Build ordering:** Atlantis Tools' CLI executable must itself be built
  (as a host tool) before any `add_custom_command()` that invokes it can
  run — CMake's existing "build a tool, then use it to generate other
  build products" support (via the custom command's implicit dependency
  on its own `COMMAND` target, plus explicit `add_dependencies()` where
  needed) already handles this. This host tool, and `slangc` itself, run
  on the **host** build machine, never on the target device — relevant
  for a future Android build: SPIR-V compilation and reflection always
  happen on the developer/CI host; the Vulkan Backend running on whatever
  target device the artifact ships to only ever consumes the resulting
  `.spv` bytes, never invokes Slang itself. This matches Slang's own
  README, which lists Android among its supported *build/host* platforms
  ([shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md))
  — not evidence this spec needs to build Slang for Android itself, only
  that Slang's own toolchain does not preclude a future Android-hosted
  build if that ever became relevant, which Atlantis does not need for
  Phase 1's host-only compilation model.
- **No shader compiler, and no reflection code, is invoked by
  `Atlantis::Core`, `Atlantis::RHI`, `Atlantis::VulkanBackend`,
  `Atlantis::RenderGraph`, or `Atlantis::Renderer`'s own CMake targets or
  source files.** Only Atlantis Tools' CLI ever spawns `slangc` —
  verifiable by inspection/grep, mirroring the existing `Vk*`-type/
  Vulkan-header boundary verification pattern this codebase already uses
  for other module boundaries.
- **Debug/Release and other multi-configuration generators
  (Visual Studio) share one compiled artifact set.** SPIR-V bytecode does
  not vary by C++ build configuration; Slang compilation reads no
  `CMAKE_BUILD_TYPE`/`$<CONFIG>` value and produces no configuration-
  dependent bytes. Artifacts are compiled once into a single,
  configuration-independent output location (see
  [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)).

### Error handling and threading

- **Missing `slangc`** fails CMake configure with an explicit
  `FATAL_ERROR`.
- **A Slang compile error** (syntax error, type error) fails the
  **build** at the specific `add_custom_command()` step for that shader,
  with `slangc`'s own diagnostic output (already naming the offending
  source file/line, per Slang's own diagnostics model) surfaced verbatim
  by the build system — no Atlantis-side parsing, reformatting, or
  suppression.
- **Atlantis Tools' CLI checks `slangc`'s subprocess exit code
  explicitly** — a non-zero exit is never silently treated as success,
  and the CLI itself exits non-zero, propagating the failure to CMake.
- **Shader System's own library API (schema loader, mapping helper,
  command-line-construction helper) uses `atlantis::Result<T, E>`
  throughout, matching every other Phase 1 module's convention** — not
  exceptions. [AGENTS.md](../AGENTS.md) explicitly leaves offline
  tooling's exception policy open to this module's own spec; this ADR
  chooses to stay exception-free for consistency with Core/RHI/
  RenderGraph/Renderer, since no concrete need for exceptions exists in
  Shader System's own narrow, data-transformation-shaped API.
- **Atlantis Tools' CLI executable itself** (`main()`, its subprocess-
  spawning code) is ordinary build-tool code using the host OS's process
  APIs directly (`CreateProcess` on Windows) and standard exit-code
  propagation — not part of any render-path exception-free contract,
  since it is offline tooling per [AGENTS.md](../AGENTS.md)'s own
  carve-out, and does not itself need to introduce C++ exceptions to do
  its job (subprocess launch/wait/exit-code-check is naturally expressed
  without them).
- **Single-threaded, single-invocation Tools CLI process**: it
  compiles/reflects exactly one shader (one `slangc` invocation) per
  process invocation, runs to completion, and exits. CMake build
  parallelism may run multiple such processes concurrently across
  different shader files — ordinary build-system parallelism, not a
  thread-safety contract on any Atlantis type. No job/task system, thread
  pool, or parallel-compilation scheduler is introduced. Per
  [ADR-0004](0004-phase1-threading-baseline.md), this is offline,
  build-time-only tooling, not part of Phase 1's single-logical-frame-
  thread render path.
- **No runtime shader compilation and no hot-reload exist in this spec's
  scope.** Shader System's schema/loader/mapping API is never called by
  any Atlantis executable at process-run time in a way that triggers
  compilation — only Tools' CLI, at build time, ever produces a new
  `.spv`/reflection-JSON pair. A future spec proposing hot-reload must
  justify it freshly and pass its own architecture review.

## Consequences

### Positive

- Directly closes the gap [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)
  named as its own migration boundary, with a compiler that also
  provides first-class reflection JSON output
  ([ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)),
  eliminating what would otherwise have been a separate third-party
  reflection-library dependency.
- The CLI-vs-library decision is settled with an explicit, evidence-based
  comparison table rather than left for implementation to discover — a
  future Plan cannot silently choose the architecturally heavier
  (library-linking) path.
- Keeping Shader System free of any OS-process dependency means RHI-
  adjacent consumers of its reflection types
  ([ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  never inherit a process-spawning dependency they do not need — a
  cleaner, narrower dependency graph than the original version's design,
  where Shader System's own library invoked the compiler itself.
- CMake's own dependency tracking (`add_custom_command` `DEPENDS`) gives
  correct incremental rebuilds for free.
- The host-tool pattern (compile on the build host, consume the resulting
  artifact on any target device) requires no special-casing for a future
  Android build.

### Negative / Trade-offs

- A hard build-time dependency on a working `slangc` toolchain is now
  unavoidable for any target that includes a shader — mitigated by the
  explicit, early, named configure-time failure, and by `slangc` already
  being present in the same Vulkan SDK install `glslc` previously
  required.
- Subprocess-per-shader compilation forgoes the compile-time efficiency
  Slang's own docs attribute to long-lived, reused `Session` objects
  across many compiles in one process
  ([docs.shader-slang.org — Compilation API](https://docs.shader-slang.org/en/latest/compilation-api.html)) —
  accepted as immaterial at Phase 1's shader count (a small, fixed
  handful); a future spec facing a much larger shader count, where
  compile-time throughput becomes a measured problem, may need to revisit
  this trade-off and adopt the library-linked path this ADR rejects for
  now — not designed or scaffolded here.
- Building a host tool (Atlantis Tools' CLI) as a build-time dependency
  of every shader-consuming target adds a small amount of extra
  build-graph complexity and a short one-time build cost, the same
  accepted trade-off the original GLSL-based version of this ADR already
  had.
- No parallel/distributed compilation scheme, shader-content caching
  across machines, or build-farm-aware artifact sharing is designed.

## Alternatives Considered

- **Link Slang's compiler library directly into Shader System or
  Atlantis Tools**, rather than invoking `slangc` as a subprocess. Fully
  evaluated in Context's comparison table above and rejected on every
  axis for Phase 1's actual scale — the strongest reasons being avoiding
  a redistribution obligation for `slang-compiler.dll`
  ([shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md))
  that the CLI path never incurs, and avoiding a from-source Slang build
  or a still-uncertain prebuilt-release `find_package` story
  ([issue #5649](https://github.com/shader-slang/slang/issues/5649)).
- **Have Shader System's own library spawn the `slangc` subprocess
  itself**, rather than moving that responsibility into Atlantis Tools'
  CLI (this ADR's own original 2026-08-13 shape). Rejected on revision:
  keeping Shader System's public API entirely free of any OS-process
  concept is a strictly narrower, more portable, easier-to-unit-test
  contract, and correctly reflects that "spawn a subprocess" is a
  build-tool concern belonging to Atlantis Tools, not a library concern
  belonging to a module every RHI-adjacent consumer might otherwise
  transitively depend on.
- **A standalone offline CLI outside the Atlantis CMake build** (a
  separate script invoked manually by a developer, as
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s path
  already was). Rejected: exactly the manual, easy-to-forget path this
  ADR exists to replace.
- **Runtime compilation.** Rejected per [AGENTS.md](../AGENTS.md)'s
  explicit instruction that Phase 1 does not implement runtime shader
  compilation or hot-reload unless a spec proves it necessary.
- **Compile shaders as part of each consuming target's own CMake target**
  rather than a shared Tools executable. Rejected: duplicates the
  compile/reflect invocation contract across every consumer; a single
  Tools executable, invoked identically by every consumer's CMake, keeps
  it in exactly one place.
