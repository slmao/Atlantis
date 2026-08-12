# Spec: Shader System Foundation

- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; pending Human Review.
- **Created:** 2026-08-13
- **Related Plan(s):** None yet — a plan may be drafted once this spec
  reaches `Approved`, per [AGENTS.md](../AGENTS.md); not before, and not
  as part of this spec's own PR.
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0007](../adr/0007-test-framework.md),
  [ADR-0010](../adr/0010-cmake-structure.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md),
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md),
  and [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  (all `Accepted`). See **Architectural Impact** below — four new
  decisions are identified and drafted alongside this spec:
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)
  (Phase 1 shader source language and compiler),
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)
  (build-time compilation boundary and Tools integration),
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  (reflection strategy, metadata ownership, and the RHI/Pipeline
  boundary), and
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
  (artifact location, versioning, and reproducibility) — all four
  `Proposed`, pending the same Human Review this spec itself is pending.

## Summary

This spec introduces `Atlantis Shader System` (`src/shader_system/`) as a
real module for the first time: a build-time pipeline that compiles GLSL
shader source to SPIR-V via `glslc`, reflects the compiled bytecode's
descriptor bindings, push-constant ranges, vertex-input attributes, and
cross-stage interface compatibility via SPIRV-Reflect, and emits both as
ordinary, non-checked-in build artifacts consumed by whichever code
constructs an RHI `Pipeline`. It replaces
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
temporary, human-run, checked-in-bytecode path — established explicitly
to avoid deciding Shader System's shape under Spec 0007's own
implementation pressure — with the real thing that ADR named as its own
migration boundary. It does **not** change RHI's `Pipeline`/
`CommandList`/`Device::createPipeline()` contract, does not introduce
runtime shader compilation or hot-reload, and does not implement the
migration of Spec 0007's existing checked-in shaders itself (that is
explicit follow-up work for this spec's future implementation Plan).

## Motivation / Problem Statement

[specs/README.md](README.md)'s Candidate Spec Backlog lists Shader System
Foundation as the very next candidate after Minimal Renderer, with Spec
0007 (`Approved`, implemented) as its sole named dependency — satisfied.
[docs/project-blueprint.md](../docs/project-blueprint.md)'s Milestone 5
names this spec's problem domain explicitly: "Phase 1 shader source
language choice; SPIR-V compilation; reflection (bindings, push-constant
layout); pipeline layout construction; cache/debug artifact handling,"
and states plainly that "Spec 0007 deliberately did not resolve any of
this... precisely so this milestone's own Spec is the one that decides
it."

Three concrete gaps stand between "a fixed, hand-authored `.spv` pair
checked into the repository" and "a real shader system," none of which
any existing `Accepted` ADR resolves:

- **No shader source language or compiler is chosen for Atlantis as a
  project.** [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  explicitly declined to record which language/compiler produced Spec
  0007's checked-in bytecode ("Whatever language the human author used...
  is not recorded, enforced, or built upon by any Atlantis code"). This
  spec is the first to make that choice a reviewed, recorded decision
  ([ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)).
- **No automated compilation exists.** Every `.spv` byte in this
  repository today was produced by a human manually running `glslc` and
  committing the result — real, accepted friction
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  own stated trade-off) that does not scale past one fixed material and
  leaves editing a shader source file with no automatic, build-integrated
  path to updated bytecode
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
- **No reflection exists**, so `Pipeline` creation's vertex-input/binding/
  push-constant layout is matched to its shader bytecode only "by
  convention and by the human author's own care" — an explicitly-named,
  explicitly-accepted-as-temporary gap
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md))
  this spec closes with real, automated extraction and a cross-stage
  compatibility check
  ([ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)).

A fourth, narrower gap follows directly from the first three: once
compilation is automated, compiled artifacts are build products, not
checked-in source — and no prior spec in this line has had to decide
where a generated binary artifact lives, how a consumer finds it without
a hardcoded path, or what "reproducible" means for something no longer
committed to git
([ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)).

## Goals

- Choose, as an explicit reviewed architecture decision, Phase 1's shader
  source language and compiler toolchain
  ([ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)).
- Introduce `Atlantis Shader System` (`src/shader_system/`) as a real
  module with a reviewed public API (compile, reflect), module boundary
  (depends only on Core), and build-time-only invocation contract
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
- Give `Atlantis Tools` its first real content: a small command-line
  executable wrapping Shader System's compile/reflect library, invoked by
  CMake at build time with correct incremental-rebuild dependency
  tracking
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
- Decide reflection scope, the reflection library, the reflection
  metadata's form and ownership, and the exact (deliberately unchanged)
  seam by which that metadata feeds RHI's existing
  `Device::createPipeline()` contract
  ([ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)).
- Fix where compiled artifacts live, how consumers locate them without a
  hardcoded developer-machine path, Debug/Release and multi-config
  generator behavior, and what reproducibility means for a
  non-checked-in binary artifact
  ([ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)).
- Fix the explicit migration boundary superseding
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  checked-in-bytecode mechanism for `shaders/minimal_renderer/`'s
  existing shaders, scoped as follow-up work for this spec's future
  implementation Plan, not performed by this spec itself.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Runtime shader compilation.** Shader System's compile function is
  only ever called by the build-time Tools CLI
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
  No Atlantis executable compiles a shader while running.
- **Shader hot-reload.** No file-watcher, no runtime recompilation path,
  no live-pipeline-swap mechanism of any kind.
- **A shader cache service.** Beyond CMake's own build-tree incremental
  dependency tracking (which requires no new Atlantis-side caching
  concept), no content-hash-keyed cache, no cross-machine/distributed
  cache, and no runtime shader cache are introduced.
- **A material graph, node-based shader authoring, or any visual shader
  tooling.** GLSL text source only.
- **A permutation/variant explosion framework.** Each `.glsl` file
  compiles to exactly one `.spv` artifact this round; no `#define`-driven
  variant matrix, no shader keyword system.
- **A pipeline cache architecture** (`VkPipelineCache` persistence,
  pipeline-object deduplication/reuse across `Material` instances). Out
  of this spec's scope; `Pipeline`'s own ownership model
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md))
  is unchanged.
- **An asset database or general serialization/schema platform.** The
  reflection JSON schema
  ([ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  is fixed and narrow, versioned by a single integer field, not a general
  schema-migration framework.
- **Editor integration.** No tool UI, no live-shader-preview surface.
- **D3D12/DXIL, Metal/MSL, WebGPU/WGSL.** Phase 1 targets Vulkan/SPIR-V
  exclusively; no cross-compilation, no multi-target shader architecture,
  per [AGENTS.md](../AGENTS.md) and
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
- **The Android implementation itself.** This spec's Tools CLI runs as a
  host build tool and its output artifact format does not preclude a
  future Android build consuming the same SPIR-V — but no Android
  Platform/Vulkan Backend work is performed or unblocked by this spec,
  consistent with [specs/README.md](README.md) Section B's own
  dependency ordering (Android Platform remains its own, separate,
  not-yet-specced candidate).
- **Headless rendering or image regression testing.** Unrelated to this
  spec's scope; both remain their own, separately-specced candidates.
- **Bindless resources, GPU-driven rendering, or neural shading.** Not
  designed, not scaffolded, per [AGENTS.md](../AGENTS.md).
- **Runtime (the module), ECS, or any scene system.** This spec's
  verification is build/tool-level and a minimal GPU-integration check
  against the existing Minimal Renderer path — no new runtime executable
  or composition beyond what Spec 0007 already established.
- **A general serialization/schema platform** beyond the fixed reflection
  JSON schema this spec defines.
- **A second Renderer or a second graphics backend.** Vulkan/SPIR-V only,
  unchanged from every prior spec in this line.
- **Modifying RHI's `Pipeline`/`CommandList`/`Device::createPipeline()`
  public contract.** [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)'s
  design is not reopened; Shader System's output is consumed by whichever
  code already constructs `PipelineCreateParams`, not by a new RHI-level
  artifact type — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
- **Implementing the migration of `shaders/minimal_renderer/`'s checked-in
  `.spv` files and README note, or changing
  `examples/minimal_renderer_demo`/the GPU test's `Material` construction
  call site.** Fixed as a required future-Plan follow-up by
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md),
  not performed by this spec.
- **Writing any source, test, shader, or build-configuration file.** This
  spec (and its accompanying `Proposed` ADRs) is a design document only —
  no code, CMake target, or shader file is added by this round.

## Requirements

### Functional

**`Atlantis Shader System` module**

- New module `src/shader_system/`, target `atlantis_shader_system`, alias
  `Atlantis::ShaderSystem`, depending only on `Atlantis::Core` — see
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md),
  realizing the dependency edge
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
  already anticipated but never implemented.
- Exposes a *compile* operation (GLSL source path + shader stage → SPIR-V
  bytes, via a `glslc` subprocess invocation) and a *reflect* operation
  (SPIR-V bytes → reflection metadata, via SPIRV-Reflect), each returning
  `atlantis::Result<T, E>` — no exception is thrown across this module's
  public API. Exact type/function names are a Plan-stage detail.
- Exposes a reflection-metadata loader (JSON sidecar file path →
  `atlantis::Result<ReflectionMetadata, ReflectionLoadError>`) and a
  mapping helper turning a loaded `ReflectionMetadata` (plus its
  corresponding `.spv` bytes) into RHI's existing
  `VertexInputLayout`/`pushConstantSizeBytes` shapes — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
- References no RHI, RenderGraph, Renderer, Vulkan Backend, or Atlantis
  Platform type anywhere in its public headers — its only structural
  awareness of RHI is producing plain data (`VertexInputLayout`-shaped
  values) matching RHI's own already-`Accepted` struct shapes, not a
  dependency on the RHI target itself. (A Plan-stage decision may choose
  to express the mapping helper's output as RHI's own `VertexInputLayout`
  type directly, which would add a `PRIVATE` or `PUBLIC` dependency on
  `Atlantis::RHI` scoped exactly to that one helper function — this spec
  fixes that the *data shape* matches RHI's contract, and leaves the
  exact header/dependency-direction mechanics as an implementation
  detail that must not let RHI depend back on Shader System.)

**`Atlantis Tools` shader compiler CLI**

- New executable target (e.g. `atlantis_shaderc`, exact name a Plan-stage
  detail) under `src/tools/`, realizing
  [module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-tools)'s
  existing "shader precompilation CLI" responsibility for the first time.
  Depends on `Atlantis::ShaderSystem` and `Atlantis::Core`.
- Takes a GLSL source file path and shader stage as command-line
  arguments, invokes Shader System's compile-then-reflect operations in
  sequence, and writes the resulting `.spv` and reflection JSON sidecar
  to a caller-specified output directory. Exits non-zero, with `glslc`'s
  own diagnostic surfaced verbatim, on any compile/reflect failure.
- No shader compiler or reflection logic is duplicated in this
  executable's own code — it is a thin CLI wrapper around Shader System's
  library functions, per
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).

**Build-time compilation and CMake integration**

- Every shader source file to be compiled is declared to CMake via
  `add_custom_command()`, with the source `.glsl` file as `DEPENDS`, the
  Tools CLI executable as the `COMMAND`, and the `.spv`/reflection-JSON
  pair as `OUTPUT` — giving correct incremental rebuild behavior with no
  custom staleness-tracking code, per
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
- CMake locates `glslc` via `find_program()`, sourced from the Vulkan SDK
  already required by the Vulkan Backend
  ([ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)).
  A missing `glslc` fails CMake **configure** with an explicit,
  human-readable `FATAL_ERROR` naming the missing component — never a
  silently-skipped shader target, never a build-time-only failure for
  what is really a configure-time precondition.
- Compiled artifacts are written to a single, configuration-independent
  build-tree directory
  (`${CMAKE_BINARY_DIR}/shaders/<relative-path>/...`, exact
  interpolation a Plan-stage detail) — not nested under a
  `$<CONFIG>`-specific subdirectory, since SPIR-V bytecode does not vary
  by C++ build configuration, per
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).
- No shader compiler or reflection code is invoked by any
  `Atlantis::Core`/`Atlantis::RHI`/`Atlantis::VulkanBackend`/
  `Atlantis::RenderGraph`/`Atlantis::Renderer` CMake target or source
  file — verifiable by inspection/grep, mirroring the existing
  `Vk*`-type/Vulkan-header boundary verification pattern this codebase
  already uses for other module boundaries.

**Reflection**

- For each compiled shader stage, reflection extracts: descriptor
  bindings (set/binding/type/stage), push-constant ranges (offset/size/
  stage), vertex-input attributes (vertex stage only: location/name/
  format), and the stage's entry-point name — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  for the full, deliberately narrow scope (no sampler reflection, no
  specialization constants, no compute `local_size`).
- A cross-stage interface compatibility check (vertex `Output` interface
  locations must be a superset of fragment `Input` interface locations,
  by location index) runs as part of the Tools CLI's reflect step for a
  compiled shader pair, failing the build with a diagnostic naming the
  offending location and both source files on mismatch.
- Reflection is performed via **SPIRV-Reflect**, a new third-party
  dependency of `Atlantis::ShaderSystem` only, acquired via CMake
  `FetchContent` pinned to a tagged release, per
  [ADR-0006](../adr/0006-dependency-management.md)'s existing "small,
  source-buildable development dependency" category — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  for the full library-choice rationale and Alternatives Considered.
- Reflection metadata is written as a small, versioned JSON sidecar file
  (one `"schemaVersion"` integer field, checked by Shader System's own
  loader), one per compiled shader stage, alongside the `.spv` file —
  never a generated C++ header, never a binary/opaque serialized format.
  Shader System owns the schema and provides the only supported loader;
  no other module hand-parses this JSON directly.

**RHI/Pipeline boundary — unchanged**

- `Device::createPipeline(PipelineCreateParams)`
  (`src/rhi/include/atlantis/rhi/device.h`,
  `src/rhi/include/atlantis/rhi/types.h`) is not modified by this spec.
  It continues to accept raw `ShaderStageBytecode` (opaque SPIR-V words)
  and an explicit `VertexInputLayout`/`pushConstantSizeBytes` value —
  RHI still performs no parsing, validation, or reflection of SPIR-V, and
  `Atlantis::RHI` gains no new dependency on `Atlantis::ShaderSystem`.
- Reflection metadata is consumed one layer above RHI, by whichever code
  constructs `PipelineCreateParams` (Shader System's own mapping helper,
  called by a future `Material`-construction call site — not designed by
  this spec) — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
- `Atlantis::VulkanBackend`'s `createPipeline()` implementation is
  unchanged and gains no dependency on Shader System or reflection.

**Artifact location, versioning, and migration**

- Compiled `.spv` bytecode and its JSON reflection sidecar are ordinary,
  non-checked-in build products (analogous to `.obj`/`.lib` files), not
  committed to version control by this spec's implementation. `.gitignore`
  is extended to exclude the build-tree shader output directory.
- Every compiled artifact records the exact `glslc`/shaderc/glslang/
  SPIRV-Tools version string used to produce it, in its reflection
  JSON's own metadata — self-describing provenance, superseding
  [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
  manual, per-directory plain-text compiler-version note. See
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
  for the full reproducibility model (same-version determinism; no
  cross-version bit-for-bit guarantee).
- `shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl` become Shader
  System build inputs (unchanged as GLSL source); the checked-in
  `.spv` files and the README's compiler-version note are retired **as
  explicit follow-up work for this spec's future implementation Plan**,
  not by this spec's own PR — see
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)'s
  Migration section for the full contract, including that no two
  parallel, simultaneously-authoritative shader-sourcing mechanisms may
  persist once that migration lands.

**Phase 1 single-threaded orchestration and error handling**

- The Tools CLI is a single-threaded, single-invocation process — it
  compiles/reflects exactly one shader stage per process invocation and
  exits; CMake build parallelism may run multiple such processes
  concurrently across different shader files, which is ordinary build-
  system parallelism, not a thread-safety contract on any Atlantis type.
  No job/task system, thread pool, or parallel-compilation scheduler is
  introduced.
- Shader System's library API (compile, reflect, load metadata) uses
  `atlantis::Result<T, E>` throughout, matching every other Phase 1
  module's convention — [AGENTS.md](../AGENTS.md) leaves offline
  tooling's exception policy open to this module's own spec; this spec
  chooses to stay exception-free for consistency, per
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
- Every new public type/function this spec introduces documents its
  thread-safety contract at its public API, per
  [ADR-0004](../adr/0004-phase1-threading-baseline.md)'s existing
  convention.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" for the Tools CLI's own execution, and "does not
  meaningfully slow down an incremental build when no shader source
  changed" for CMake's dependency-tracking behavior — no compile-time
  micro-benchmark target.
- **Memory:** no new GPU memory allocation strategy question — this spec
  introduces no new RHI resource type, no `vkAllocateMemory` call.
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  is unchanged and unreopened.
- **Portability (within the Vulkan-only Phase 1 constraint):** the Tools
  CLI is a host build tool; its compiled SPIR-V output is
  backend-portable across Windows and (future) Android by construction —
  the same artifact, compiled once on the host, is consumed by whichever
  target device's Vulkan Backend later loads it. This spec does not
  implement or verify Android consumption; it verifies only that nothing
  in its own design assumes a Windows-only artifact path or a
  Windows-only build-tree layout.
- **Other:** two new dependencies this round: SPIRV-Reflect (new,
  `FetchContent`-acquired third-party library, per
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md));
  `glslc` (not a new dependency in [AGENTS.md](../AGENTS.md)'s sense — a
  new *use* of a component already shipped by the Vulkan SDK
  [ADR-0006](../adr/0006-dependency-management.md) already requires, per
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)).
  Unit tests use the existing Catch2 v3 framework
  ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundaries (realizing, not moving, existing ones)

Realizes exactly the dependency edges
[module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
already anticipated for Shader System (depends on Core only; depended on
by Vulkan Backend/RHI "for pipeline construction, exact seam TBD" — this
spec resolves that seam as "not at all; consumed one layer above RHI,"
see below — and by Tools for offline compilation).

```
Build time (CMake, once per shader source file, incremental):
  add_custom_command(
    OUTPUT   <build-tree>/shaders/.../foo.vert.spv
             <build-tree>/shaders/.../foo.vert.refl.json
    COMMAND  atlantis_shaderc --stage=vertex
             --input=<source-tree>/shaders/.../foo.vert.glsl
             --output-dir=<build-tree>/shaders/.../
    DEPENDS  <source-tree>/shaders/.../foo.vert.glsl
             atlantis_shaderc  # (transitively: the tool itself)
  )

atlantis_shaderc (Tools CLI), one process per invocation:
  ShaderSystem::compile(sourcePath, stage) -> Result<SpirvBytes, CompileError>
    -- invokes glslc as a subprocess; glslc's own diagnostics surfaced
       verbatim on failure --
  ShaderSystem::reflect(spirvBytes) -> Result<ReflectionMetadata, ReflectError>
    -- SPIRV-Reflect extracts bindings/push-constants/vertex-inputs/
       entry-point/interface variables --
  write foo.vert.spv, foo.vert.refl.json to --output-dir

Later, at whatever point a caller constructs PipelineCreateParams
(a future Renderer-level Material-construction call site, not designed
by this spec):
  ShaderSystem::loadReflectionMetadata(jsonPath) -> Result<ReflectionMetadata, ...>
  ShaderSystem::toVertexInputLayout(metadata) -> rhi::VertexInputLayout
  ShaderSystem::toPushConstantSize(metadata) -> std::size_t
  -- caller loads the corresponding .spv bytes directly (unchanged from
     ADR-0027's existing "load bytes into memory" step) --
  Device::createPipeline({ vertexShader, fragmentShader,
                            vertexInputLayout, colorFormat, depthFormat,
                            pushConstantSizeBytes })
  -- Device::createPipeline()'s own contract, unchanged from ADR-0025 --
```

### Source language and compiler

See
[ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)
for the full decision: GLSL, compiled by `glslc` sourced from the
existing Vulkan SDK requirement, as a build tool only.

### Build-time compilation boundary

See
[ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)
for the full decision: Shader System as a library, Tools as its CLI
wrapper, CMake `add_custom_command()` integration, configure-time failure
on a missing compiler, build-time failure with surfaced diagnostics on a
compile error, single-threaded per-invocation Tools CLI, exception-free
library API.

### Reflection strategy and RHI boundary

See
[ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
for the full decision: reflection scope, SPIRV-Reflect as the reflection
library, JSON sidecar metadata form and Shader-System-owned schema, and
`Device::createPipeline()`'s unchanged consumer contract.

### Artifact location and reproducibility

See
[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
for the full decision: configuration-independent build-tree output
location, no checked-in binary artifacts, self-describing compiler-
version provenance, and the explicit migration boundary superseding
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
checked-in-bytecode mechanism.

### Threading

Unchanged from every prior spec in this line: single logical frame
thread for anything render-path-related, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md) — not applicable to
this spec's own build-time-only scope in the first place, since no
Shader System code runs on the render thread. The Tools CLI is a
short-lived, single-threaded build-time process, not part of the frame
loop at all.

### Error handling

- Recoverable runtime errors (compile failure, reflect failure, metadata
  load failure) use `atlantis::Result<T, E>`, consistent with every
  prior spec's convention and this spec's own explicit choice not to
  introduce exceptions
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
- A missing `glslc` toolchain is a CMake **configure**-time
  `FATAL_ERROR`, never a build-time or runtime failure.
- A GLSL syntax/compile error is a **build**-time failure at the specific
  `add_custom_command()` step, with `glslc`'s own diagnostic (which
  already names the offending source file and line) surfaced verbatim.
- A cross-stage interface mismatch (reflected vertex `Output` locations
  not covering fragment `Input` locations) is a **build**-time failure at
  the Tools CLI's reflect step, with a diagnostic naming the offending
  location and both source files.
- Every subprocess exit code (`glslc`'s own process exit status) is
  checked; a non-zero exit is never silently treated as success.

## Architectural Impact

This spec introduces architecture across four distinct, independently-
reviewable decisions, filed as four new `Proposed` ADRs — none decided by
this spec's prose alone:

1. **Phase 1 shader source language and compiler toolchain** — GLSL,
   compiled by `glslc` sourced from the existing Vulkan SDK requirement,
   as a build tool never linked or invoked at runtime. Filed as
   [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
2. **Build-time compilation boundary and Tools integration** — Shader
   System as a Core-only-dependent library; Atlantis Tools' first real
   content (a CLI wrapper); CMake `add_custom_command()` integration with
   correct incremental dependency tracking; configure-time vs. build-time
   failure modes; exception-free error handling. Filed as
   [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
3. **Reflection strategy, metadata ownership, and the RHI/Pipeline
   boundary** — reflection scope; SPIRV-Reflect as a new third-party
   dependency; JSON sidecar metadata form and Shader-System-owned schema;
   the explicit decision to leave `Device::createPipeline()`'s existing
   contract unchanged, consuming reflection metadata one layer above RHI.
   Filed as
   [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
4. **Artifact location, versioning, and reproducibility** — build-tree,
   configuration-independent output location; no checked-in binary
   artifacts; self-describing compiler-version provenance; the explicit
   migration boundary superseding
   [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
   mechanism for `shaders/minimal_renderer/`, scoped as future-Plan
   follow-up. Filed as
   [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the four new ADRs above — each new ADR
references and extends the existing ones (particularly ADR-0001,
ADR-0006, ADR-0007, ADR-0015, ADR-0023, ADR-0025, ADR-0027) without
altering them. **[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
remains `Accepted` and is explicitly not rewritten** — this spec's ADRs
supersede its *mechanism* going forward, per that ADR's own anticipated
Migration Boundary, not its historical correctness for Spec 0007's own
moment. Architectural Impact was not "None" — a new module
(`Atlantis Shader System`), a new build-time executable target (Tools'
CLI), a new third-party dependency (SPIRV-Reflect), and a new generated-
artifact category (build-tree, non-checked-in shader bytecode/metadata)
are each exactly what [AGENTS.md](../AGENTS.md)'s "What counts as
significant" section requires the full Spec → Plan → Human Review path
for. **This spec's approval is not itself an authorization to
implement** — a Plan may be drafted per [AGENTS.md](../AGENTS.md) only
once this spec's own PR has merged into `main`, and that future Plan
must still pass its own Human Review before any code, CMake target, or
shader file is written.

## Alternatives Considered

- **Split this spec into two or more smaller specs** (e.g. "compilation"
  separately from "reflection"). Considered, and rejected for this round
  for the same reason Spec 0007's own Alternatives Considered rejected
  splitting its six ADRs into separate specs: the decisions are genuinely
  interdependent (reflection's metadata form has no real validation
  target without a compilation pipeline that actually produces SPIR-V to
  reflect; the artifact-location decision has no real shape without both
  compilation and reflection each producing an artifact that needs a
  home) — filing four separate ADRs already gives Human Review the
  ability to accept, reject, or send back any one decision independently,
  without needing four separate spec documents to do it.
- **Defer reflection to a later, second Shader System spec**, shipping
  compilation alone first. Rejected: `Device::createPipeline()`'s
  existing contract already requires *some* `VertexInputLayout` source;
  shipping compilation without reflection would leave
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  "matched by convention, not validated" gap fully open even after
  automating compilation — a partial fix that leaves the more
  error-prone half of the original problem unaddressed. Also, Spec 0007's
  own two shaders are simple enough that reflecting them now, alongside
  their newly-automated compilation, is a small, low-risk addition to
  this round rather than a genuinely separable body of work.
- **Choose HLSL instead of GLSL.** See
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)'s
  own Alternatives Considered — rejected primarily because it would
  require rewriting Spec 0007's already-checked-in GLSL source for no
  Phase 1 functional gain, and HLSL's binding idioms are oriented toward
  D3D-first authoring Phase 1 has no use for.
- **Link `shaderc`'s library directly, or vendor a pinned compiler
  binary, instead of shelling out to the Vulkan-SDK-provided `glslc.exe`
  as a subprocess.** See
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)/
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)'s
  own Alternatives Considered — rejected as adding real new dependency-
  management surface for a build-time-only need the existing SDK
  requirement already serves.
- **SPIRV-Cross instead of SPIRV-Reflect, or hand-rolled SPIR-V parsing.**
  See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  own Alternatives Considered — rejected as a heavier dependency for a
  narrower need, respectively as reinventing a well-understood, already-
  solved parsing problem.
- **Continue checking in compiled `.spv` bytes, now generated by the
  build rather than by a human, committing the build's own output back
  into the repository.** See
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)'s
  own Alternatives Considered — rejected per the same "no silent,
  automatically-regenerated, committed binary" norm
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)'s
  golden-image rule already establishes.
- **Widen `Device::createPipeline()`'s contract to accept a
  Shader-System-defined bundled artifact type**, rather than leaving RHI
  unchanged and consuming reflection metadata one layer above it. See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  own Alternatives Considered — rejected for this round as introducing an
  `Atlantis::RHI` → `Atlantis::ShaderSystem` dependency edge
  [module_boundaries.md](../docs/architecture/module_boundaries.md) does
  not currently require and this spec has no concrete need to force.
- **Perform the `shaders/minimal_renderer/` migration as part of this
  spec's own implementation**, rather than deferring it to a future Plan.
  Rejected: this spec is explicitly scoped as a spec/ADR-only round (see
  Non-Goals) — mirroring Spec 0007's own Human Review Approval pattern of
  separating "spec/ADR approval" from "implementation authorization" as
  two distinct gates, per [AGENTS.md](../AGENTS.md).

## Testing & Verification Plan

*(This section describes what a future implementation Plan must verify —
consistent with this spec's own scope, no test, tool, or build
configuration is written by this round.)*

- **Build/tool integration tests** (once implemented):
  - Editing a `.glsl` source file and re-running `cmake --build` triggers
    exactly the affected shader's recompilation (and no others) — verified
    by observing CMake's own build output / file modification timestamps
    across an incremental build.
  - Removing/renaming `glslc` from the located Vulkan SDK path causes
    CMake **configure** to fail with an explicit, readable error — not a
    build-time or runtime failure, and not a silent skip.
  - An intentionally-broken GLSL source file (a syntax error) causes the
    **build** to fail at that shader's specific compile step, with
    `glslc`'s own diagnostic visible in the build log, naming the source
    file and line.
  - An intentionally-mismatched vertex/fragment pair (fragment stage
    reading an interface location the vertex stage does not write) causes
    the **build** to fail at the reflect step, with a diagnostic naming
    the mismatched location and both source files.
  - Debug and Release configurations (and, if the test environment is a
    multi-config generator, both configurations of the same build tree)
    both build successfully, sharing one compiled shader artifact set
    without redundant recompilation.
  - No developer-machine absolute path appears in any generated CMake
    cache entry, generated header, or test/demo source file that consumes
    a compiled shader artifact — verified by inspection/grep across a
    fresh build tree.
  - A full clean build followed by a second, no-op incremental build
    recompiles zero shaders on the second build (confirming CMake's
    `DEPENDS`-based staleness tracking is correctly wired, not
    over-triggering).
- **GPU-independent unit tests** (Catch2, no Vulkan device required):
  - Shader System's reflection-metadata loader correctly parses a fixture
    reflection JSON file, rejects one with a `"schemaVersion"` newer than
    the loader supports, and rejects malformed JSON — all via
    `Result::Err`, no exception thrown.
  - Shader System's `toVertexInputLayout()`/`toPushConstantSize()`
    mapping helpers correctly transform a fixture `ReflectionMetadata`
    value into RHI's existing `VertexInputLayout`/size shapes, matching a
    hand-computed expected value.
  - Shader System's compile/reflect functions handle a missing input
    file, a `glslc` subprocess-launch failure (simulated via a
    Plan-stage-decided fixture/mocking strategy), and a non-zero `glslc`
    exit code, all via `Result::Err`.
- **GPU integration tests (Windows/Vulkan):**
  - `shaders/minimal_renderer/`'s two shader files, compiled through this
    spec's new pipeline instead of loaded from checked-in bytecode,
    successfully back a real `Device::createPipeline()` call, with
    Vulkan Validation Layers reporting zero warnings/errors — this is the
    concrete regression test proving the new pipeline produces output
    RHI/Vulkan Backend can actually consume, not merely bytes that
    superficially resemble SPIR-V.
  - The resulting `Pipeline`, used to draw Spec 0007's existing minimal
    mesh (via `examples/minimal_renderer_demo`'s or an equivalent
    verification composition's existing draw path, updated per this
    spec's migration follow-up), produces the same visible, correctly-
    shaded, correctly depth-ordered output Spec 0007 already verified —
    confirming the migration changes *how* the shader artifact is sourced
    without changing *what* is rendered.
- **Headless integration tests / image regression tests:** not
  applicable — unchanged from every prior spec's equivalent flag, gated
  on headless rendering per
  [testing-strategy.md](../docs/process/testing-strategy.md)'s sequencing
  note.
- **Vulkan Validation Layers:** mandatory and must run clean for the GPU
  integration test above, per [AGENTS.md](../AGENTS.md) and
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- **Manual verification:** a developer deletes their entire build tree,
  reconfigures, and builds from clean, confirming: `glslc` is correctly
  located without any manually-set path beyond a normally-installed
  Vulkan SDK; `shaders/minimal_renderer/`'s shaders compile without
  error; `examples/minimal_renderer_demo` (or its equivalent, post-
  migration) runs and visibly renders the same mesh Spec 0007's own
  manual verification already confirmed.

## Risks & Open Questions

- **Exact Tools CLI argument shape, exact JSON schema field names, exact
  build-tree path interpolation syntax, and exact CMake target/property
  names** are all left to the Plan — this spec fixes behavior and
  boundaries, not exact spellings, consistent with how Spec 0007's own
  Risks & Open Questions treated equivalent naming-level detail.
- **Exact mechanism for expressing a "shader pair" to the Tools CLI's
  cross-stage compatibility check** (a third CLI invocation reflecting
  both already-compiled stages together, vs. a single invocation taking
  both source files, vs. a separate `link`-equivalent step) is left to
  the Plan — this spec fixes that the check exists and its exact
  location-matching semantics, not its exact invocation shape.
- **Whether the RHI-shaped mapping helper
  (`toVertexInputLayout()`/`toPushConstantSize()`) lives inside
  `Atlantis::ShaderSystem` itself (taking on a `PRIVATE`/`PUBLIC`
  dependency on `Atlantis::RHI`'s header for the `VertexInputLayout`
  struct shape) or in a separate, thin glue module/target** is left to
  the Plan — this spec fixes that RHI never depends back on Shader
  System, not which of Shader System's own two plausible internal shapes
  the Plan picks.
- **Exact SPIRV-Reflect pinned version, license-file inclusion mechanics,
  and `FetchContent_Declare()` details** are left to the Plan, per
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
- **Whether CI (once it exists, per
  [docs/process/ci-strategy.md](../docs/process/ci-strategy.md)) needs its
  own explicit Vulkan SDK / `glslc` provisioning step distinct from
  whatever it already needs for the Vulkan Backend** is a real open
  question this spec does not resolve — likely "no, the same SDK
  installation already serves both," but not verified against an actual
  CI image by this spec, since no CI pipeline exists yet for either
  purpose.
- **Whether a future spec revisiting cross-stage interface compatibility
  checking will need type/format-level checking** (not just location-
  index matching, per
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  own stated narrow scope) is left open — not needed for this spec's own
  two-shader scope, where Vulkan Validation Layers remain the backstop
  for any type mismatch this spec's own check does not catch.
- **Recommended/tested Vulkan SDK version range for `glslc` reproducibility
  guidance** (exact version numbers, and where that guidance is
  documented — likely a revised `shaders/README.md`) is left to the Plan.

## Out of Scope / Future Work

The migration of `shaders/minimal_renderer/`'s checked-in `.spv` files and
README compiler-version note, and the corresponding update to
`examples/minimal_renderer_demo`'s/the GPU test's `Material` construction
call site, are **required follow-up work for this spec's future
implementation Plan**, not performed by this spec itself — see
[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)'s
Migration section for the full contract.

Android Platform and Vulkan presentation, headless rendering, and image
regression testing all remain later, separately-specced candidates per
[docs/project-blueprint.md](../docs/project-blueprint.md) and
[specs/README.md](README.md) Section B, not advanced or unblocked by this
spec beyond confirming (by inspection, not implementation) that Shader
System's host-tool/portable-artifact design does not preclude a future
Android consumer.

A future spec introducing a second material, a texture, lighting, or
multiple shader pairs sharing a common uniform/descriptor layout is
expected to be the first real consumer that might motivate widening
`Device::createPipeline()`'s contract (e.g. RHI accepting a bundled
Shader-System artifact type directly) — not decided or designed by this
spec, per
[ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).

A future spec may also revisit this spec's reproducibility model (e.g. a
fully pinned/vendored compiler toolchain) if cross-machine/CI
reproducibility becomes a concrete, measured problem — not designed or
motivated by a concrete need in this spec's own scope, per
[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).

Runtime shader compilation and hot-reload remain explicitly future,
not-yet-justified work, per this spec's own Non-Goals and
[ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
