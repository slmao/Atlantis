# ADR 0029: Shader System — Build-Time Compilation Boundary and Tools Integration

- **Status:** Proposed
- **Date:** 2026-08-13
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Context

[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md) fixed a
narrow, temporary rule for Spec 0007: no shader compiler is invoked by any
Atlantis build target, and a human runs `glslc` manually, checking the
resulting `.spv` bytes into the repository. That ADR's own Migration
Boundary names this as the exact gap a future Shader System spec is
expected to close: real, automated compilation replacing manual, checked-
in bytecode.

[ADR-0028](0028-shader-system-source-language-and-compiler.md) fixes
*what* compiles shader source (`glslc`, GLSL). This ADR fixes *when* and
*by what mechanism* that compiler runs, and draws the boundary
[AGENTS.md](../AGENTS.md) and
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
already require: Renderer and Vulkan Backend never invoke a shader
compiler; Shader System is an artifact producer, not a Renderer
dependency; Runtime does not compile shaders.

## Decision

**Shader compilation happens exclusively at build time, driven by CMake,
through a small Atlantis Tools command-line executable that wraps Shader
System's compile-and-reflect library code. No runtime compilation, no
hot-reload, and no Renderer- or Vulkan-Backend-invoked compilation exists
anywhere in this spec's scope.**

- **`Atlantis Shader System` (`src/shader_system/`) is a library**
  (`atlantis_shader_system`, alias `Atlantis::ShaderSystem`) exposing two
  operations as plain C++ functions/types, each returning
  `atlantis::Result<T, E>`: *compile* (GLSL source path + shader stage →
  SPIR-V bytes, invoking `glslc` as a subprocess) and *reflect* (SPIR-V
  bytes → the reflection metadata
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  defines). It depends only on `Atlantis::Core`, per
  [module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  existing, unchanged "Shader System depends on Core" rule. It does not
  depend on RHI, Renderer, RenderGraph, or Vulkan Backend.
- **`Atlantis Tools` (`src/tools/shaderc_cli/` or equivalent — exact path
  a Plan-stage detail) gets its first real content: a small command-line
  executable** (e.g. `atlantis_shaderc`) that takes a shader source path
  plus stage on the command line, calls Shader System's compile-then-
  reflect functions, and writes the two output files
  ([ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md))
  to a caller-specified output directory. This realizes
  [module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  existing "Tools: shader precompilation CLI" responsibility and "Tools
  depends on ... Shader System" dependency edge — neither invented by this
  ADR, only implemented for the first time.
- **CMake invokes this Tools executable via `add_custom_command()`, once
  per shader source file, with the shader source file listed as the
  command's `DEPENDS`.** This gives CMake's own incremental-build
  dependency tracking for free: editing a `.glsl` file makes its `.spv`/
  reflection outputs stale and triggers exactly the affected
  recompilation on the next build, the same mechanism CMake already uses
  for `.cpp` → `.obj` staleness. No custom file-watcher, no separate
  "shader build" step outside the normal `cmake --build` invocation.
- **Build ordering:** the Tools compiler executable must itself be built
  (as a host tool) before any `add_custom_command()` that invokes it can
  run; `add_custom_command()`'s implicit dependency on its own `COMMAND`
  target, plus an explicit `add_dependencies()` where needed, is
  sufficient — CMake already supports this "build a tool, then use it to
  generate other build products" pattern without special-casing. This
  Tools executable runs on the **host** build machine, never on the
  target device — relevant for a future Android build (see Portability,
  below): SPIR-V compilation and reflection always happen on the
  developer/CI host, the same artifact is later consumed by the Vulkan
  Backend running on whatever target device the artifact ships to.
- **No shader compiler, and no reflection code, is invoked by
  `Atlantis::Core`, `Atlantis::RHI`, `Atlantis::VulkanBackend`,
  `Atlantis::RenderGraph`, or `Atlantis::Renderer`'s own CMake targets or
  source files.** Only the Tools executable (and Shader System's library
  it wraps) ever calls `glslc` or performs reflection — this is the
  concrete, verifiable form of
  [module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  existing "Shader System is a producer of artifacts consumed lower in
  the stack, not a consumer of frame-level or platform-level concepts"
  rule and [AGENTS.md](../AGENTS.md)'s "Renderer does not fundamentally
  depend on ... Shader compilation" implication.
- **No runtime shader compilation and no hot-reload exist in this spec's
  scope**, matching [AGENTS.md](../AGENTS.md)'s Non-Goals instruction:
  Shader System's compile function is only ever called from the Tools CLI
  at build time; no Atlantis executable (Runtime-equivalent composition,
  `examples/minimal_renderer_demo`, or any future Runtime) calls it at
  process-run time. A future spec proposing hot-reload must justify it
  freshly and pass its own architecture review — not inherit authorization
  from this one.
- **Missing `glslc` (per
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)) fails
  CMake **configure**, not build, and not silently**: `find_program()`
  failing to locate `glslc` produces `message(FATAL_ERROR ...)` naming the
  Vulkan SDK component that is missing, before any target is configured —
  consistent with how a missing Vulkan SDK already fails Vulkan Backend's
  own configure step today. A compile-time failure (invalid GLSL syntax,
  a `glslc` diagnostic) fails the **build** at the specific
  `add_custom_command()` step for that shader, with `glslc`'s own
  stderr output (which already names the offending source file and line)
  surfaced verbatim by CMake/the build tool — no Atlantis-side parsing,
  reformatting, or suppression of that diagnostic.
- **Debug/Release and other multi-configuration generators
  (Visual Studio) share one compiled artifact set.** SPIR-V bytecode does
  not vary by C++ build configuration — nothing in Shader System's compile
  step reads `CMAKE_BUILD_TYPE`/`$<CONFIG>` or produces different bytes
  per configuration. Shader artifacts are compiled into a single,
  configuration-independent output location (see
  [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)
  for the exact location), compiled once regardless of how many
  configurations a multi-config generator's build tree contains, avoiding
  redundant recompilation across `Debug`/`Release`.
- **Error handling and threading:** Shader System's library API
  (compile/reflect) uses `atlantis::Result<T, E>`, matching every other
  Phase 1 module's convention — not exceptions. [AGENTS.md](../AGENTS.md)
  explicitly leaves offline tooling's exception policy open to this
  module's own spec; this ADR chooses **not** to introduce exceptions
  here, since no concrete need for them exists and staying consistent
  with Core/RHI/RenderGraph/Renderer's existing `Result<T, E>` convention
  avoids a second error-handling idiom in the codebase for no reason. The
  Tools CLI (`atlantis_shaderc`) is a single-threaded, single-invocation
  process — it compiles/reflects exactly one shader stage per process
  invocation, runs to completion, and exits with a non-zero status on any
  `Result::Err`; no job/task system, thread pool, or parallel-compilation
  scheduling is introduced by this spec. (CMake may still invoke multiple
  such single-threaded processes concurrently across different shader
  files via its own build-parallelism flags — that is ordinary build-
  system parallelism, not a thread-safety contract on any Atlantis type.)

## Consequences

### Positive

- Directly closes the gap [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)
  named as its own migration boundary: shader compilation becomes real,
  automated, and incremental, replacing a human manually running `glslc`
  and checking in the result.
- CMake's own dependency tracking (`add_custom_command` `DEPENDS`) gives
  correct incremental rebuilds for free — editing shader source correctly
  triggers exactly the affected recompilation, with no custom watcher.
- Keeps the "Renderer/Vulkan Backend never compile shaders" boundary
  structurally true, not just documented: no compiler invocation exists
  anywhere outside Shader System's library and Tools' CLI wrapper around
  it, verifiable by inspection/grep, mirroring how prior specs' Vulkan-
  header/`Vk*`-type boundaries are verified.
- The host-tool pattern (compile on the build host, consume the resulting
  artifact on any target device) requires no special-casing for a future
  Android build — the same CMake mechanism this ADR establishes for
  Windows carries over unchanged.

### Negative / Trade-offs

- A hard build-time dependency on a working `glslc` toolchain is now
  unavoidable for any target that includes a shader — a genuinely new
  build-environment requirement beyond what
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  checked-in-bytecode path needed (that path required nothing at build
  time at all). Accepted as the direct, necessary cost of real
  compilation; mitigated by the explicit, early, named configure-time
  failure rather than a late or confusing one.
- Building a host tool (the Tools CLI) as a build-time dependency of every
  shader-consuming target adds a small amount of extra build-graph
  complexity and a short one-time build cost (compiling
  `atlantis_shaderc` itself) that a purely checked-in-artifact scheme
  never had. Accepted: this is the standard, well-understood "build a
  code-generator tool, then run it" CMake pattern, not a novel risk.
- No parallel/distributed compilation scheme, shader-content caching
  across machines, or build-farm-aware artifact sharing is designed —
  each build tree independently compiles its own shaders, the same way
  every other Atlantis C++ source file is independently compiled per
  build tree today. Acceptable at Phase 1's shader count; a future spec
  can revisit if compile time becomes a real, measured problem.

## Alternatives Considered

- **Invoke `glslc` directly from a `add_custom_command()`, with no Tools
  CLI wrapper in between.** Rejected: this would leave reflection
  ([ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  with nowhere to run — SPIR-V reflection needs its own small program (or
  the same one) to load the compiled bytes and emit the reflection
  sidecar; wrapping both steps in one Tools executable keeps compile and
  reflect as one atomic, dependency-tracked build step per shader file,
  rather than two separately-orchestrated CMake custom commands that could
  drift out of sync.
- **A standalone offline CLI outside the Atlantis CMake build (a separate
  script invoked manually by a developer, as ADR-0027's path already
  was).** Rejected: this is exactly the manual, easy-to-forget,
  human-discipline-dependent path this ADR exists to replace; a build
  target that silently keeps working against a stale artifact because a
  developer forgot to re-run a separate script is a worse failure mode
  than the CMake-integrated path's automatic staleness detection.
- **Runtime compilation** (compiling `.glsl` to SPIR-V inside the Atlantis
  process at first use, optionally with an on-disk cache). Rejected per
  [AGENTS.md](../AGENTS.md)'s explicit instruction that Phase 1 does not
  implement runtime shader compilation or hot-reload unless a spec proves
  it necessary — no concrete Phase 1 consumer needs it, and it would make
  every shader-consuming Atlantis executable carry a shader-compiler
  runtime dependency it does not otherwise need.
- **Compile shaders as part of each consuming target's own CMake target**
  (e.g. `examples/minimal_renderer_demo`'s own `CMakeLists.txt` calling
  `glslc` inline), rather than a shared Tools executable. Rejected: this
  duplicates compile/reflect logic (or at least its invocation contract)
  across every consumer, and reintroduces per-consumer drift risk
  ([shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
  own "shared, as a single authoritative copy" note already establishes
  the precedent of one shared source of truth, not per-consumer
  duplication) — a single Tools executable, invoked identically by every
  consumer's CMake, keeps the compile/reflect contract in exactly one
  place.
