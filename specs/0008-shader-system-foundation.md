# Spec: Shader System Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Created:** 2026-08-13
- **Historical scope:** Originally drafted around GLSL + `glslc` +
  SPIRV-Reflect; re-based on **Slang** (Phase 1's shader language, its
  compiler infrastructure, and its Vulkan/SPIR-V compile-and-reflect
  foundation) on 2026-08-14 following explicit human direction, then
  checked against the real toolchain (see Validation Evidence). Nothing in
  the Non-Goals, the spec/ADR-only scope boundary, or the relationship to
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  changed across those revisions — only the technical design underneath.
- **Related Plan(s):** [plans/0008-shader-system-foundation.md](../plans/0008-shader-system-foundation.md)
  (`Approved`).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0007](../adr/0007-test-framework.md),
  [ADR-0010](../adr/0010-cmake-structure.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md),
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md),
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md),
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  (all `Accepted`, none reopened or modified). Four new decisions are
  filed alongside and are `Accepted` with this spec:
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)
  (Phase 1 shader source language and compiler — Slang, Vulkan/SPIR-V
  only, `slangc` from the Vulkan SDK),
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)
  (build-time compilation boundary, the CLI-vs-library decision, Tools
  integration),
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  (reflection strategy via Slang's own `-reflection-json`, metadata
  ownership, the RHI/Pipeline boundary), and
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
  (artifact location, versioning, reproducibility, migration boundary
  superseding ADR-0027).
- **Human Review Approval (2026-08-14):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), following a joint architecture review
  of this spec and ADR-0028–0031 across its revision history and a
  decision memo enumerating candidates HR-0008-01 through HR-0008-13. Two
  points requiring a human choice are resolved as part of this approval:

  1. **SPIR-V compatibility baseline: Option A (compatibility-first).**
     The Vulkan Backend's physical-device selection floor stays
     `VK_API_VERSION_1_0` (`src/vulkan_backend/src/vulkan_device.cpp`,
     unchanged); shader artifacts target **SPIR-V 1.0** via `slangc
     -profile spirv_1_0`; Slang's "experimental" support-tier disclosure
     for that path is accepted and stays recorded;
     [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)
     is **not** reopened. **`spirv-val --target-env vulkan1.0` is promoted
     from recommendation to a mandatory build-time verification step** —
     every emitted artifact must pass it, and a missing `spirv-val` must
     fail **CMake configure**, on the same footing as a missing `slangc`.
     Option B (SPIR-V 1.3, raising the device floor to Vulkan 1.1,
     reopening ADR-0024) is **rejected for this round**, retained in each
     ADR's Alternatives Considered as a live future option.
  2. **Slang `E50011` ("SPIR-V version too old") warning: Policy S
     (precise suppression).** Atlantis Tools' `slangc` invocation passes
     `-warnings-disable 50011` — suppressing exactly this one
     target-maturity warning and no other — with the reason recorded
     permanently in ADR-0028. Suppression reduces no verification
     coverage: `spirv-val` (now mandatory) and future Vulkan Validation
     Layers remain the substantive checks. Policy K (leave `E50011`
     unsuppressed) is recorded rejected in ADR-0028.
  3. **HR-0008-01 through HR-0008-13, as enumerated in the decision memo,
     are all approved as drafted** — covering: Slang as both Phase 1
     shader language and compiler infrastructure; `slangc` from the
     existing Vulkan SDK with no new dependency-acquisition mechanism; the
     CLI-subprocess (not library-link) architecture; Slang's own
     `-reflection-json` replacing the originally-proposed SPIRV-Reflect
     dependency; descriptor reflection scoped to validation of the
     existing fixed RHI/Vulkan-Backend descriptor contract only; the
     `"main"` SPIR-V entry-point compatibility policy; the RHI-integration
     target as a secondary target inside the Shader System module (not a
     new top-level module); the version-bound reflection-JSON parsing
     policy; the configuration-independent shader-artifact model with
     shader debug-info out of Phase 1 scope; ADR-0027's migration boundary.
     No `AGENTS.md` or `docs/architecture/module_boundaries.md` change is
     approved or implied.
  4. **Plan 0008 is authorized to be drafted only once this spec's PR has
     merged into `main`.** Implementation remains unauthorized — that
     future Plan must still pass its own (or a joint Spec+Plan) Human
     Review before any source, test, shader, or build-configuration file
     for this spec's scope is written.

  Following this approval, ADR-0028–0031 each move to `Accepted` and this
  spec moves to `Approved`. This checkbox-level approval is not itself an
  authorization to implement — this spec's Acceptance criteria describe
  properties a future implementation must satisfy, not ones already
  verified.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #150](https://github.com/slmao/Atlantis/pull/150) Batch 3. Original
  scope and obligations retained; the original GLSL-based draft, the
  Slang re-evaluation narration, and the full HR-0008 decision-memo
  walkthrough are preserved in
  [PR #35](https://github.com/slmao/Atlantis/pull/35)/[PR #36](https://github.com/slmao/Atlantis/pull/36)
  history.

## Validation Evidence (2026-08-14)

The Slang-based design was initially written from documentation alone,
then checked against the actual toolchain: single-machine, single-Slang-
version observations (Vulkan SDK `1.4.357.0`, Slang `2026.13.1`, Windows
host, `SPIRV-Tools v2026.3`), gathered in a scratch directory, no
experiment file checked in, no Atlantis target built, no GPU workload run.
Local observations are labelled as such and never escalated into vendor
guarantees. The findings that remain **normative constraints** (fixed in
the referenced ADRs, and relied on by Requirements and Risks below):

1. **SPIR-V 1.0 compilation via `-profile spirv_1_0` succeeds** (exit 0;
   `spirv-dis` reports `; Version: 1.0`), emitting one warning
   `warning[E50011]: SPIR-V version too old` (suppressible with
   `-warnings-disable 50011`, byte-identical output).
2. **The default (no `-profile`) emits SPIR-V 1.5**, which needs a Vulkan
   1.2 device — relying on the default would silently raise Atlantis's
   device-compatibility floor. **`-capability spirv_1_0` does not select
   the output version** — only `-profile` does. Both are fixed as
   mandatory requirements in ADR-0028.
3. **`-reflection-json` produces JSON successfully** for every invocation.
4. **Reflection field availability** (full classification in ADR-0030):
   entry-point name, stage, descriptor binding index, descriptor set
   (`"space"`), resource type, push-constant offset/size, vertex-input
   index and element type were all present. Two parsing hazards:
   **descriptor set appears as `"space"` and is omitted entirely when the
   set is 0** (a parser must treat its absence as set 0);
   **user-declared vertex inputs carry no `semanticName`** (only the
   system-value output `SV_Position` did).
5. **Vertex-input location is not a labelled JSON field** — the JSON
   reports `{"kind": "varyingInput", "index": N}`; disassembly showed
   `OpDecorate ... Location N` matching those indices, but the source had
   already pinned them with explicit `[[vk::location(N)]]`, so this is not
   an independent guarantee. The design treats the explicit authoring
   attribute as authoritative and the JSON index as a cross-check.
6. **Push-constant reflection was correct here** (`offset: 0`, `size: 64`);
   the known upstream concern
   ([issue #5676](https://github.com/shader-slang/slang/issues/5676)) did
   not reproduce for this shape — one passing observation, not a
   guarantee.
7. **Entry-point naming confirmed in both directions.** Without
   `-fvk-use-entrypoint-name`, the emitted `OpEntryPoint` name is always
   `"main"`; with the flag, it is the source name. The default is
   compatible with the Vulkan Backend's existing hard-coded `pName =
   "main"`, so **Atlantis Tools' CLI must never pass
   `-fvk-use-entrypoint-name`**.
8. **Reflection granularity is per invocation, not per module.** The
   top-level `"parameters"` array lists all module-scope parameters in
   both stages' output (including a stage's unused parameters);
   per-entry-point usage is available via `entryPoints[].bindings[].used`.
9. **`spirv-val --target-env vulkan1.0` passed (exit 0)** for both the
   vertex and fragment artifacts.
10. **`spirv-dis` confirmed** `OpEntryPoint`, `Location`, `DescriptorSet`,
    `Binding`, and push-constant `Block`/`Offset` decorations, including
    `DescriptorSet 2` / `Binding 3` on a non-zero-set probe.
11. **Local determinism observed** — two identical compiles produced
    SHA-256-identical `.spv` **and** reflection JSON. Single-machine,
    single-version only, not a Slang reproducibility guarantee.

**What this evidence did not establish:** whether Slang *guarantees*
`varyingInput` index equals SPIR-V `Location` independent of explicit
`[[vk::location(N)]]`; whether the reflection JSON's shape is stable
across Slang releases (**no official schema or versioning guarantee was
found** — hence ADR-0030's version-bound parsing policy); any runtime/GPU
behavior (Validation Layers remain the mandatory GPU-path gate a future
implementation must exercise).

## Summary

This spec introduces `Atlantis Shader System` (`src/shader_system/`) as a
real module for the first time: a build-time pipeline compiling Slang
shader source to SPIR-V and reflecting it, via `slangc` (Slang's official
CLI compiler, sourced from the Vulkan SDK), invoked as a subprocess by a
new Atlantis Tools CLI executable. Shader System owns a small,
Atlantis-versioned reflection JSON schema (populated from Slang's own
`-reflection-json` output), a loader for it, and — via a separate,
narrowly-scoped adapter target — a mapping into RHI's existing
`Device::createPipeline()` parameter shapes. It replaces
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
temporary, human-run, checked-in-bytecode path with the real thing that
ADR named as its own migration boundary. It does **not** change RHI's
`Pipeline`/`CommandList`/`Device::createPipeline()` contract, does not
introduce runtime shader compilation or hot-reload, and does not itself
migrate Spec 0007's existing checked-in GLSL shaders to Slang (explicit
follow-up work for this spec's implementation Plan).

## Motivation / Problem Statement

[specs/README.md](README.md)'s Candidate Spec Backlog lists Shader System
Foundation as the next candidate after Minimal Renderer, with Spec 0007
(`Approved`, implemented) as its sole named dependency — satisfied.
[docs/project-blueprint.md](../docs/project-blueprint.md)'s Milestone 5
names the problem domain: "Phase 1 shader source language choice; SPIR-V
compilation; reflection (bindings, push-constant layout); pipeline layout
construction; cache/debug artifact handling," and states Spec 0007
deliberately did not resolve any of this so this milestone's Spec would.

Four concrete gaps, none resolved by any existing `Accepted` ADR:

- **No shader source language or compiler is chosen for Atlantis as a
  project.** ADR-0027 explicitly declined to record which
  language/compiler produced Spec 0007's checked-in bytecode. This spec
  makes that a reviewed decision, and chooses Slang (ADR-0028).
- **No automated compilation exists.** Every `.spv` byte today was
  produced by a human running `glslc` manually — accepted friction
  (ADR-0027) that does not scale and leaves editing a shader source with
  no build-integrated path to updated bytecode (ADR-0029).
- **No reflection exists**, so `Pipeline` creation's
  vertex-input/binding/push-constant layout is matched to its bytecode
  only by convention and author care (ADR-0027). This spec closes that,
  for the fields reflection can genuinely own, with automated extraction
  plus a compiler-enforced cross-stage compatibility guarantee (ADR-0030).
- **No prior spec decided where a generated binary artifact lives**, how a
  consumer finds it without a hardcoded path, or what "reproducible" means
  for something no longer committed (ADR-0031).

## Goals

- Choose, as an explicit reviewed decision backed by official
  Slang/Khronos/LunarG documentation, Phase 1's shader source language and
  compiler toolchain (ADR-0028).
- Introduce `Atlantis Shader System` (`src/shader_system/`) as a real
  module with a reviewed, RHI-independent public API (a reflection schema,
  its loader, a command-line-construction helper), depending only on Core.
- Resolve, with a documented comparison, whether Shader System links
  Slang's compiler library or Atlantis Tools launches Slang's CLI — and
  fix the resulting module split between Shader System and Tools
  (ADR-0029).
- Decide reflection scope, remove the need for any new reflection library
  (Slang's own `-reflection-json` suffices), fix the reflection metadata's
  schema and ownership, fix exactly which data (shader-reflected vs.
  Mesh-schema-owned vs. attachment-format-sourced) populates each field of
  RHI's existing `VertexInputLayout`, and fix the module-dependency-
  direction seam — including a new, explicitly-named adapter target, not
  left to Plan (ADR-0030).
- Fix where compiled artifacts live, how consumers locate them without a
  hardcoded developer-machine path, Debug/Release and multi-config
  generator behavior, and what reproducibility means for a non-checked-in
  binary artifact, anchored on officially-verifiable provenance (the
  resolved Vulkan SDK version), not an unverified CLI flag assumption
  (ADR-0031).
- Fix the explicit migration boundary superseding ADR-0027's
  checked-in-GLSL-bytecode mechanism for `shaders/minimal_renderer/`'s
  existing shaders, including their migration to Slang source, scoped as
  follow-up work for this spec's implementation Plan.
- Where official Slang material reveals a genuine, unresolved tension with
  Atlantis's existing architecture (the SPIR-V version/device-
  compatibility-floor question), state it explicitly as a Human Review
  question rather than silently resolving it.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Runtime shader compilation.** Slang's compile/reflect invocation only
  ever happens via the build-time Tools CLI (ADR-0029). No Atlantis
  executable compiles a shader while running.
- **Shader hot-reload.** No file-watcher, no runtime recompilation, no
  live-pipeline-swap mechanism.
- **A shader cache service.** Beyond CMake's own build-tree incremental
  dependency tracking: no content-hash-keyed cache, no cross-machine/
  distributed cache, no runtime shader cache.
- **A material graph, node-based shader authoring, or visual shader
  tooling.** Slang text source only.
- **A permutation/variant explosion framework.** Each logical shader
  compiles to a small, fixed set of `.spv` artifacts this round (one per
  stage).
- **A pipeline cache architecture** (`VkPipelineCache` persistence,
  pipeline-object deduplication/reuse). `Pipeline`'s ownership model
  (ADR-0022) is unchanged.
- **An asset database or general serialization/schema platform.** The
  reflection JSON schema (ADR-0030) is fixed, narrow, Atlantis-owned,
  versioned by a single integer field.
- **Editor integration.** No tool UI, no live-shader-preview surface.
- **Shader debug-information generation, source-level shader debugging,
  per-configuration optimization levels, any Debug/Release shader
  permutation system.** Debug and Release C++ configurations use
  **identical** shader compilation flags — which is why a single
  configuration-independent artifact is coherent (ADR-0031). A future spec
  introducing configuration-dependent shader flags must re-examine both
  the artifact output-path model and the incremental-build dependency
  model together.
- **D3D12/DXIL, Metal/MSL, WebGPU/WGSL, CUDA, or any Slang target beyond
  Vulkan/SPIR-V.** Phase 1 configures and invokes exactly one:
  Vulkan/SPIR-V (AGENTS.md, ADR-0028). Choosing a multi-target-capable
  compiler does not authorize using more than one of its targets.
- **Building Slang from source, vendoring Slang's compiler library, or
  linking Slang's compiler API into any Atlantis target.** Slang is
  consumed exclusively as the prebuilt `slangc` binary from an
  externally-installed Vulkan SDK, invoked as a subprocess
  (ADR-0028/0029).
- **The Android implementation itself.** The Tools CLI runs as a host
  build tool; its output does not preclude a future Android build
  consuming the same SPIR-V, but no Android Platform/Vulkan Backend work
  is performed or unblocked.
- **Headless rendering or image regression testing.** Separately-specced
  candidates.
- **Bindless resources, GPU-driven rendering, or neural shading.** Not
  designed, not scaffolded (AGENTS.md) — even though Slang has documented
  neural-shading-adjacent capabilities elsewhere.
- **Runtime (the module), ECS, or any scene system.** Verification is
  build/tool-level plus a minimal GPU-integration check against the
  existing Minimal Renderer path.
- **A second Renderer or a second graphics backend.** Vulkan/SPIR-V only.
- **Modifying RHI's `Pipeline`/`CommandList`/`Device::createPipeline()`
  public contract.** ADR-0025's design is not reopened; Shader System's
  output is consumed by a separate, explicitly-named adapter target
  (ADR-0030).
- **Implementing the migration of `shaders/minimal_renderer/`'s checked-in
  GLSL source and `.spv` files to Slang, or changing
  `examples/minimal_renderer_demo`/the GPU test's `Material` construction
  call site.** Fixed as a required future-Plan follow-up by ADR-0031.
- **Writing any source, test, shader, or build-configuration file, or
  downloading/installing Slang.** This spec and its ADRs are a design
  document only.

## Requirements

### Functional

**`Atlantis Shader System` module**

- New module `src/shader_system/`, target `atlantis_shader_system`, alias
  `Atlantis::ShaderSystem`, depending only on `Atlantis::Core` (ADR-0029),
  realizing the dependency edge
  [module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
  already anticipated but never implemented.
- Exposes exactly: (1) a reflection-metadata schema type
  (`ReflectionMetadata`, Atlantis-owned, RHI-independent); (2) a loader
  (`Result<ReflectionMetadata, ReflectionLoadError>` from a file path);
  (3) a pure, data-only helper that, given a shader source path, an
  entry-point name, and an output directory, returns the exact `slangc`
  argument list and expected output paths. **Shader System never spawns a
  process, never touches an OS-process API, and never links Slang's
  compiler library.** Exact type/function names are a Plan-stage detail.
- References no RHI, RenderGraph, Renderer, Vulkan Backend, or Atlantis
  Platform type anywhere in its public headers — verifiable by
  inspection/grep.

**`Atlantis Tools` shader compiler CLI**

- New executable target (e.g. `atlantis_shader_compiler`) under
  `src/tools/`, realizing
  [module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-tools)'s
  existing "shader precompilation CLI" responsibility for the first time.
  Depends on `Atlantis::ShaderSystem` and `Atlantis::Core`.
- Takes a Slang source file path and entry-point name(s) as CLI arguments,
  calls Shader System's command-line-construction helper, **spawns
  `slangc` as a subprocess itself** (including its `-reflection-json`
  invocation), checks its exit code, transforms Slang's raw reflection
  JSON into Shader System's Atlantis-owned schema, and writes the
  resulting `.spv` and reflection JSON sidecar to a caller-specified
  output directory. Exits non-zero, with `slangc`'s diagnostic surfaced
  verbatim, on any compile/reflect failure. **This is the only place in
  the whole Atlantis codebase that spawns `slangc` or any other shader
  compiler process** (ADR-0029).

**Build-time compilation and CMake integration**

- Every shader source file is declared to CMake via `add_custom_command()`
  — source `.slang` file as `DEPENDS`, Atlantis Tools' CLI as `COMMAND`,
  the `.spv`/reflection-JSON pair as `OUTPUT` — giving correct incremental
  rebuild with no custom staleness-tracking code.
- CMake locates `slangc` via `find_program()`, sourced from the Vulkan SDK
  already required by the Vulkan Backend (ADR-0006, ADR-0028) — confirmed
  bundled since SDK 1.3.296.0, present in 1.4.357.0. A missing `slangc`
  fails CMake **configure** with an explicit `FATAL_ERROR` naming the
  missing component — never a silently-skipped shader target, never a
  build-time-only failure for a configure-time precondition.
- Compiled artifacts are written to a single, configuration-independent
  build-tree directory (`${CMAKE_BINARY_DIR}/shaders/<relative-path>/...`)
  — not nested under a `$<CONFIG>`-specific subdirectory (ADR-0031).
- No shader compiler or reflection code is invoked by any
  `Atlantis::Core`/`RHI`/`VulkanBackend`/`RenderGraph`/`Renderer` CMake
  target or source file — verifiable by inspection/grep.

**Shader source authoring convention**

- Every logical shader (a vertex+fragment pair backing one `Material`) is
  authored as **one Slang module** whose vertex and fragment entry points
  **share one common, explicitly-declared `struct` type for the vertex-
  to-fragment varying interface** (ADR-0030), to obtain Slang's own
  compile-time cross-stage type-checking as the primary interface-
  compatibility guarantee.
- Every vertex-input parameter carries an explicit `[[vk::location(X)]]`
  attribute; every explicitly-bound resource carries an explicit
  `[[vk::binding(binding, set)]]` attribute — mirroring the current
  checked-in GLSL shaders' explicit `layout(...)` discipline, so
  locations/bindings are deterministic and directly knowable from source.
  This is load-bearing: the reflection JSON reports a `varyingInput`
  index, not a field labelled as a SPIR-V `Location`, so the explicit
  attribute — not the reflected index — is what makes the location
  authoritative.

**Entry-point naming policy — fixed here, not left to the Plan**

Four names must not be conflated; Phase 1 fixes each:

| Concept | Phase 1 value |
|---|---|
| Slang source function name | meaningful, e.g. `vertexMain`/`fragmentMain` |
| `slangc -entry` argument | the same source name |
| SPIR-V `OpEntryPoint` name in the emitted module | **`"main"`** |
| `VkPipelineShaderStageCreateInfo::pName` | **`"main"`** (existing Vulkan Backend code, unchanged) |

- **Atlantis Tools' CLI must never pass `-fvk-use-entrypoint-name`.**
  Slang's documented default, with that flag absent, renames the selected
  entry point to `"main"` in the SPIR-V output (verified in both
  directions — see Validation Evidence).
- This keeps the Vulkan Backend's existing hard-coded `pName = "main"`
  (`src/vulkan_backend/src/vulkan_device.cpp`) working with **no RHI or
  Vulkan Backend change at all**, while still allowing meaningful source
  names.
- The reflection JSON reports the **source** name (`"vertexMain"`), not
  the emitted SPIR-V name; consumers must not treat them as the same
  string.
- **A future spec wanting source entry-point names preserved in SPIR-V
  must first change the RHI/Vulkan Backend contract** — enabling this flag
  is explicitly **not** a decision a future Plan may make on its own.

**Reflection**

- For each compiled shader stage, reflection extracts: descriptor bindings
  (set/binding/type), push-constant ranges (offset/size), vertex-input
  attributes (vertex stage only: index/element type — **not** stride or
  byte offset), the stage's entry-point name, and shader stage. See
  ADR-0030 for each field's evidence tier and the two parsing hazards
  (`"space"` omitted when the descriptor set is 0; user vertex inputs
  carry no `semanticName`).
- **Descriptor reflection is consumed for validation only, never for
  pipeline-layout construction this round.** `PipelineCreateParams`
  (`src/rhi/include/atlantis/rhi/types.h`) has no descriptor field, and
  the Vulkan Backend hard-codes its single binding (set 0, binding 0,
  uniform buffer, vertex stage). Shader System encodes that same
  expectation as a fixed expected contract and **compares** reflected
  descriptor data against it; any deviation is a recoverable mapping error
  that fails the **build** with a readable diagnostic. Its value is moving
  a today-silent shader/backend binding mismatch from "maybe caught by
  Validation Layers at draw time" to "always caught at build time" — with
  **zero** RHI change. Reflection-driven descriptor layout construction is
  explicitly future work requiring ADR-0025 to be reopened (ADR-0030's
  "Descriptor reflection: validation only" section).
- A supplementary cross-stage interface compatibility check (vertex
  `Output` interface locations must be a superset of fragment `Input`
  interface locations, by location index) runs as part of the Tools CLI's
  reflect step, closing the narrow gap between "the shared source module
  type-checked" and "the two separately-emitted SPIR-V artifacts still
  agree" — supplementing, not substituting for, Slang's own compile-time
  guarantee.
- Reflection is obtained via **`slangc -reflection-json`** — an official
  Slang CLI capability, verified working, requiring **no new dependency
  acquisition mechanism**. Shader System transforms Slang's raw reflection
  JSON into its own small, Atlantis-owned, versioned JSON schema (one
  `"schemaVersion"` integer field), never re-exposing Slang's raw JSON
  shape as Atlantis's own contract (ADR-0030). Shader System owns this
  schema and provides the only supported loader.

**Vertex input layout — split authority, stated explicitly**

- `VertexAttribute::location` and `VertexAttribute::format` are sourced
  from Shader System reflection.
- `VertexAttribute::offsetBytes` and `VertexInputLayout::strideBytes` are
  sourced from Mesh/vertex-schema (Atlantis/Renderer-side C++) — **never
  from shader reflection**, because a host-side interleaved vertex
  buffer's byte layout is not a concept any shader source declares or
  could declare.
- `PipelineCreateParams::colorFormat`/`depthFormat` are sourced from
  `Presentation::metadata().format`/the Vulkan Backend's fixed depth
  format, exactly as ADR-0025 already fixed — Shader System has zero
  involvement.
- See ADR-0030's authority table for the complete, field-by-field
  mapping.

**RHI/Pipeline boundary — decided explicitly**

- `Device::createPipeline(PipelineCreateParams)`
  (`src/rhi/include/atlantis/rhi/device.h`, `.../types.h`) is not modified
  by this spec. **`Atlantis::RHI` never depends on `Atlantis::ShaderSystem`,
  in any form.**
- **`Atlantis::ShaderSystem`'s own core library target references no RHI
  type in any public header**, and remains fully usable and testable with
  no knowledge that RHI exists.
- **The RHI-shape mapping lives in a secondary integration target inside
  the Atlantis Shader System module — not a new top-level module.**
  [AGENTS.md](../AGENTS.md)'s nine-module list is unchanged, and neither
  `AGENTS.md` nor
  [module_boundaries.md](../docs/architecture/module_boundaries.md) is
  edited. This target establishes no independent subsystem ownership, owns
  no GPU resource, no `Pipeline`, no compiler process; its sole job is
  combining Shader-System-owned metadata with consumer-supplied Mesh/
  vertex-schema data and validating the result into existing RHI value
  types. Concrete target/alias naming is a Plan-stage detail (ADR-0030).
- `Atlantis::VulkanBackend`'s `createPipeline()` implementation is
  unchanged and gains no dependency on Shader System, this integration
  target, or reflection.
- **No Slang type appears in any public header** of Core, RHI,
  RenderGraph, Renderer, or Shader System itself — Slang is only ever a
  subprocess.

**Artifact location, versioning, and migration**

- Compiled `.spv` bytecode and its Atlantis-schema reflection JSON sidecar
  are ordinary, non-checked-in build products; `.gitignore` is extended to
  exclude the build-tree shader output directory.
- Every compiled artifact records the resolved Vulkan SDK version (the
  officially-confirmed provenance anchor, per ADR-0031 — used instead of
  an unverified `slangc --version` assumption) in its reflection JSON's
  metadata — self-describing provenance, superseding
  [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
  manual compiler-version note.
- `shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl` are migrated to
  Slang source (following the shared-varying-struct authoring convention);
  the checked-in `.spv` files, the original GLSL source, and the README's
  compiler-version note are retired **as explicit follow-up work for this
  spec's implementation Plan**, not by this spec's PR — see ADR-0031's
  Migration section, including that no two parallel, simultaneously-
  authoritative shader-sourcing mechanisms may persist once that migration
  lands.

**Phase 1 single-threaded orchestration and error handling**

- The Tools CLI is a single-threaded, single-invocation process —
  compiles/reflects exactly one shader stage per process invocation and
  exits; CMake build parallelism running multiple such processes
  concurrently across different shader files is ordinary build-system
  parallelism, not a thread-safety contract on any Atlantis type. No
  job/task system, thread pool, or parallel-compilation scheduler.
- Shader System's library API uses `atlantis::Result<T, E>` throughout.
  The RHI-integration target does the same, returning `Result<...,
  MappingError>` on a genuine reflection/Mesh-schema mismatch or a
  reflected descriptor shape deviating from the fixed expected contract.
- Every new public type/function documents its thread-safety contract at
  its public API, per ADR-0004's convention.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" for the Tools CLI's execution, and "does not meaningfully
  slow an incremental build when no shader source changed" for CMake's
  dependency-tracking. Slang's subprocess-per-invocation model is a
  deliberate trade-off at Phase 1's shader count (ADR-0029).
- **Memory:** no new GPU memory allocation strategy question — no new RHI
  resource type, no `vkAllocateMemory` call. ADR-0023 unchanged.
- **Portability (within Vulkan-only Phase 1):** the Tools CLI is a host
  build tool; its SPIR-V output is backend-portable across Windows and
  (future) Android by construction. This spec does not implement or verify
  Android consumption; it verifies only that nothing in its design assumes
  a Windows-only artifact path or build-tree layout.
- **Dependency posture, stated precisely:**
  - **No new independent dependency acquisition mechanism** — no
    `FetchContent` entry, no package manager, no from-source build, no
    separate installer. (SPIRV-Reflect, in the original draft, is removed
    entirely.)
  - **`slangc` is nevertheless a newly required build tool** — a machine
    whose Vulkan SDK lacks it could no longer build shader-consuming
    targets.
  - **`spirv-val`** is likewise a newly required build tool — confirmed
    present in the same SDK `Bin` directory as `slangc`.
  - Both inherit their availability from the supported Vulkan SDK
    installation ADR-0006 already requires (ADR-0028).
  - **Missing-tool failure stage is fixed:** absence of either tool fails
    at **CMake configure time** with an explicit `FATAL_ERROR`.
  - Unit tests use the existing Catch2 v3 framework (ADR-0007).

## Proposed Design

### Module boundaries (realizing, not moving, existing ones)

Realizes exactly the dependency edges
[module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
already anticipated: Shader System depends on Core only; is depended on by
Vulkan Backend/RHI "for pipeline construction" — this spec resolves that
seam as "not at all, directly; a separate, narrow adapter target bridges
the two" (ADR-0030) — and by Tools for offline compilation.

Build-time and later flow (concrete C++ type/function names are a
Plan-stage detail; the ADRs fix behavior):

- **Build time (CMake, once per shader stage, incremental):**
  `add_custom_command()` with `OUTPUT` = the `.spv` + `.refl.json` pair,
  `COMMAND` = `atlantis_shader_compiler --entry=<name> --input=<source
  .slang> --output-dir=<build-tree dir>`, `DEPENDS` = the `.slang` source
  and the tool target itself.
- **`atlantis_shader_compiler` (Tools CLI), one process per invocation:**
  calls Shader System's `buildSlangcCommandLine()` helper (pure data —
  argv list + expected output paths); spawns `slangc <argv>` as a
  subprocess (Tools' responsibility, never Shader System's) which compiles
  to SPIR-V *and* emits its own `-reflection-json`; checks `slangc`'s exit
  code and surfaces its stderr verbatim on failure; calls Shader System's
  `transformSlangReflection()` to re-project Slang's raw JSON into
  Atlantis's own narrow, versioned schema; writes the `.spv` and
  `.refl.json` to `--output-dir`.
- **Later, wherever a caller constructs `PipelineCreateParams`** (a future
  Renderer-level `Material`-construction call site, not designed by this
  spec), via the Shader System module's RHI-integration target:
  `loadReflectionMetadata(jsonPath)` → `ReflectionMetadata`;
  `toVertexInputLayout(metadata, meshStrideOffsetTable)` combines
  reflection-owned (`location`, `format`) with Mesh-schema-owned (`stride`,
  `offsets`), cross-validates, does not invent;
  `toPushConstantSize(metadata)`;
  `validateDescriptorContract(metadata)` (validation only, drives
  nothing); the caller loads the corresponding `.spv` bytes directly; then
  `Device::createPipeline({...})` — its own contract unchanged from
  ADR-0025, with `colorFormat`/`depthFormat` sourced from
  `Presentation::metadata()`/the Vulkan Backend's fixed depth format, not
  from Shader System.

### Source language and compiler

See [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md):
Slang, targeting Vulkan/SPIR-V only, compiled by `slangc` sourced from the
existing Vulkan SDK requirement.

### Build-time compilation boundary and the CLI-vs-library decision

See
[ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md),
including the axis-by-axis comparison Human Review verified: Shader System
as a process-free library; Atlantis Tools as the only `slangc`-spawning
executable; CMake `add_custom_command()` integration; configure-time
failure on a missing compiler; build-time failure with surfaced
diagnostics on a compile error; single-threaded per-invocation Tools CLI;
exception-free library API.

### Reflection strategy and RHI boundary

See
[ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md):
reflection scope; Slang's own `-reflection-json` as the sole reflection
source (no new dependency); Atlantis's own transformed JSON schema; the
field-by-field `VertexInputLayout` authority table (reflection vs.
Mesh-schema vs. attachment-format); the shared-varying-struct authoring
convention and its supplementary location-index check; the
descriptor-reflection validation-only consumption scope; and the
secondary integration target inside the Shader System module that alone
bridges Shader System and RHI.

### Artifact location and reproducibility

See
[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md):
configuration-independent build-tree output location, no checked-in binary
artifacts, Vulkan-SDK-version-anchored provenance, and the explicit
migration boundary superseding ADR-0027's checked-in-GLSL-bytecode
mechanism.

### Threading

Single logical frame thread for anything render-path-related, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md) — not applicable to
this spec's build-time-only scope, since no Shader System code runs on the
render thread. The Tools CLI is a short-lived, single-threaded build-time
process, not part of the frame loop.

### Error handling

- Recoverable runtime errors (reflection-JSON transform failure, metadata
  load failure, RHI-adapter mapping mismatch) use `atlantis::Result<T,
  E>`.
- A missing `slangc` toolchain is a CMake **configure**-time
  `FATAL_ERROR`, never a build-time or runtime failure.
- A Slang compile error is a **build**-time failure at the specific
  `add_custom_command()` step, with `slangc`'s diagnostic surfaced
  verbatim.
- A cross-stage interface mismatch (the supplementary location-index
  check) is a **build**-time failure at the Tools CLI's reflect step.
- Every subprocess exit code (`slangc`'s own process exit status, checked
  by the Tools CLI, never by Shader System) is checked; a non-zero exit is
  never silently treated as success.

## Architectural Impact

Architecture across four distinct, independently-reviewable decisions,
filed as four new ADRs (`Accepted` alongside this spec) — none decided by
this spec's prose alone:

1. **Phase 1 shader source language and compiler toolchain** — Slang,
   Vulkan/SPIR-V only, `slangc` from the existing Vulkan SDK, a build tool
   never linked or invoked at runtime; the SPIR-V-version/device-
   compatibility-floor tension disclosed and flagged for Human Review.
   [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
2. **Build-time compilation boundary, the CLI-vs-library decision, Tools
   integration** — Shader System as a Core-only-dependent, process-free
   library; Atlantis Tools' first real content (the only `slangc`-spawning
   CLI); an evidence-based comparison resolving compiler-library-vs-CLI;
   CMake `add_custom_command()` integration; configure-time vs. build-time
   failure modes; exception-free error handling.
   [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
3. **Reflection strategy, metadata ownership, the RHI/Pipeline boundary**
   — reflection scope via Slang's own `-reflection-json` (no new
   dependency); Atlantis's own transformed JSON schema; the field-by-field
   `VertexInputLayout` authority split; the shared-varying-struct
   cross-stage authoring convention; the validation-only scope of
   descriptor reflection; the Shader-System-internal integration target
   resolving the RHI dependency-direction question without adding a
   top-level module.
   [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
4. **Artifact location, versioning, reproducibility** — build-tree,
   configuration-independent output location; no checked-in binary
   artifacts; Vulkan-SDK-version-anchored provenance; the explicit
   migration boundary superseding ADR-0027's mechanism for
   `shaders/minimal_renderer/`, including its GLSL-to-Slang source
   migration, scoped as future-Plan follow-up.
   [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified. **ADR-0027 remains `Accepted` and is explicitly not rewritten**
— this spec's ADRs supersede its *mechanism* going forward, per that
ADR's own anticipated Migration Boundary, not its historical correctness
for Spec 0007's moment. A new module (`Atlantis Shader System`), a new
build-time executable target (Tools' CLI), a new explicitly-named adapter
target (a secondary integration target within the Shader System module,
not a new top-level module), and a new generated-artifact category are
each what [AGENTS.md](../AGENTS.md)'s "What counts as significant" section
requires the full Spec → Plan → Human Review path for. **This spec's
approval is not itself an authorization to implement** — a Plan may be
drafted only once this spec's PR has merged into `main`, and that Plan
must still pass its own Human Review before any code, CMake target, or
shader file is written.

## Alternatives Considered

- **Continue with the original GLSL + `glslc` + SPIRV-Reflect design.**
  Superseded following explicit human direction to re-evaluate around
  Slang — Slang's Khronos governance, Vulkan-tier official support, own
  reflection-JSON CLI output (eliminating a would-be new dependency), and
  shared-module cross-stage type-checking each represent a genuine
  capability improvement, at the real, disclosed cost of a GLSL-to-Slang
  migration and an unresolved SPIR-V-version/compatibility-floor question.
- **Split this spec into two or more smaller specs.** Rejected: the four
  decisions are genuinely interdependent.
- **Defer reflection to a later Shader System spec**, shipping
  compilation alone first. Rejected — `slangc -reflection-json` reflection
  comes essentially "for free" alongside compilation in the same
  subprocess invocation.
- **Choose HLSL instead of Slang, or continue with GLSL.** See ADR-0028's
  Alternatives Considered.
- **Link Slang's compiler library, or vendor/build Slang from source,
  instead of invoking `slangc.exe` as a subprocess.** See ADR-0029's
  Context and Alternatives Considered.
- **Introduce SPIRV-Reflect (or SPIRV-Cross, or hand-rolled SPIR-V
  parsing) despite Slang's own reflection capability.** See ADR-0030 —
  rejected as pure duplication.
- **Continue checking in compiled `.spv` bytes, now generated by the
  build rather than a human.** See ADR-0031's Alternatives Considered.
- **Widen `Device::createPipeline()`'s contract, or fold the RHI-mapping
  helper directly into `Atlantis::ShaderSystem` itself**, rather than a
  separate integration target. See ADR-0030 — rejected because either
  would create an unwanted dependency edge the adapter-target design
  avoids.
- **Derive vertex stride/offset from reflection by assuming a fixed
  packing convention.** See ADR-0030 — rejected as an unsupported claim.
- **Perform the `shaders/minimal_renderer/` migration as part of this
  spec's implementation.** Rejected — this spec remains a spec/ADR-only
  round.

## Testing & Verification Plan

*(Describes what a future implementation Plan must verify — no test, tool,
or build configuration is written by this round.)*

- **Build/tool integration tests** (once implemented):
  - Editing a `.slang` source file and re-running `cmake --build`
    triggers exactly the affected shader's recompilation (and no others).
  - Removing/renaming `slangc` from the located Vulkan SDK path causes
    CMake **configure** to fail with an explicit, readable error.
  - An intentionally-broken Slang source file causes the **build** to fail
    at that shader's compile step, with `slangc`'s diagnostic in the build
    log.
  - An intentionally-mismatched vertex/fragment varying-interface struct
    (a field renamed or retyped in only one entry point) causes a **Slang
    compile error** — verifying the primary, compiler-enforced cross-stage
    guarantee fires, not merely the supplementary Atlantis-side check.
  - **The emitted SPIR-V version is asserted to be exactly the intended
    one** — a direct regression test for the two version-selection hazards
    (no-`-profile` default emitting SPIR-V 1.5; `-capability` not
    selecting the version).
  - **Every emitted artifact passes `spirv-val --target-env vulkan1.0`**,
    and a deliberately-invalid module is confirmed to make the build fail.
  - **The emitted `OpEntryPoint` name is asserted to be `"main"`**,
    guarding the entry-point compatibility contract against a future
    `-fvk-use-entrypoint-name` addition without a matching Vulkan Backend
    `pName` change.
  - **A shader whose `[[vk::binding(...)]]` deviates from the fixed
    expected descriptor contract fails the build** with a readable
    diagnostic.
  - **Push-constant offset and size reported by reflection are asserted
    against expected values**, per the issue-#5676 caveat: one passing
    observation is not a standing guarantee.
  - Debug and Release configurations (and both configurations of a
    multi-config generator's build tree) both build successfully, sharing
    one compiled shader artifact set.
  - No developer-machine absolute path appears in any generated CMake
    cache entry, generated header, or test/demo source consuming a
    compiled shader artifact.
  - A full clean build followed by a no-op incremental build recompiles
    zero shaders.
- **GPU-independent unit tests** (Catch2, no Vulkan device):
  - Shader System's reflection-metadata loader parses a fixture
    Atlantis-schema reflection JSON, rejects one with a `"schemaVersion"`
    newer than the loader supports, and rejects malformed JSON — all via
    `Result::Err`, no exception.
  - Shader System's Slang-raw-JSON-to-Atlantis-schema transformation
    correctly re-projects a fixture Slang `-reflection-json` output.
  - The RHI-integration target's `toVertexInputLayout()`/
    `toPushConstantSize()` correctly combine a fixture `ReflectionMetadata`
    with a fixture Mesh-schema stride/offset table, and return
    `Result::Err` when the two fixtures' attribute counts/locations
    disagree.
  - Shader System's command-line-construction helper produces the expected
    `slangc` argument list and output paths for a fixture input.
- **GPU integration tests (Windows/Vulkan):**
  - `shaders/minimal_renderer/`'s Slang-migrated shader, compiled through
    this spec's pipeline instead of loaded from checked-in GLSL-derived
    bytecode, successfully backs a real `Device::createPipeline()` call,
    with Vulkan Validation Layers reporting zero warnings/errors.
  - This includes verifying, on real hardware/driver, whichever SPIR-V
    version target ADR-0028's Human-Review-confirmed resolution settles on
    — for SPIR-V 1.0, the concrete test that Slang's "experimental" tier
    does not produce invalid or Validation-Layers-rejected output for
    Atlantis's own shader source.
  - The resulting `Pipeline`, drawing Spec 0007's minimal mesh, produces
    the same visible, correctly-shaded, correctly depth-ordered output
    Spec 0007 already verified.
- **Headless integration tests / image regression tests:** not applicable.
- **Vulkan Validation Layers:** mandatory and must run clean for the GPU
  integration test.
- **Manual verification:** a developer deletes their build tree,
  reconfigures, and builds from clean, confirming `slangc` is located
  without any manually-set path beyond a normally-installed Vulkan SDK;
  the migrated Slang shader compiles; `examples/minimal_renderer_demo` (or
  its post-migration equivalent) runs and visibly renders the same mesh
  Spec 0007's manual verification confirmed.

## Risks & Open Questions

- **SPIR-V version target vs. device-compatibility floor** — **resolved by
  Human Review (2026-08-14): Option A, Vulkan 1.0 / SPIR-V 1.0, with
  `spirv-val --target-env vulkan1.0` mandatory.** Four axes must not be
  conflated: the Vulkan *instance* `apiVersion` request (decided by
  ADR-0024); the *physical device* selection floor, currently
  `VK_API_VERSION_1_0` and never raised by any Accepted ADR; the SPIR-V
  binary version of an emitted module; and `slangc`'s
  `-target`/`-profile`/`-capability` flags. **ADR-0024 decides nothing
  about the SPIR-V binary version.** Per the Vulkan specification, a
  Vulkan 1.0 implementation must support only SPIR-V 1.0; SPIR-V 1.3 first
  becomes mandatory at Vulkan 1.1, and no extension bridges SPIR-V 1.3
  onto a Vulkan 1.0 device — **there is no third option** obtaining
  Slang's "stable" tier while keeping a Vulkan 1.0 device floor. Option A
  (`-profile spirv_1_0`, verified: compiles, disassembles as `Version:
  1.0`, passes `spirv-val --target-env vulkan1.0`) was selected; `spirv-val`
  is now a mandatory build-time check, which reduces but does not
  eliminate the risk and does not convert Slang's experimental path into a
  stable one. Option B (SPIR-V 1.3, raising the physical-device floor to
  `VK_API_VERSION_1_1`) is rejected for this round, retained as a live
  future alternative requiring its own compatibility-focused Human Review.
- **Whether to suppress the `E50011` warning** — **resolved: Policy S,
  precise suppression** via `-warnings-disable 50011` (byte-identical
  output), reason recorded permanently in ADR-0028, with `spirv-val` and
  future Validation Layers retained as the substantive verification
  layers. Policy K (leave unsuppressed) recorded rejected in ADR-0028.
- **Exact Tools CLI argument shape, exact Atlantis reflection-JSON schema
  field names, exact build-tree path interpolation syntax, and exact
  CMake target/property names** are left to the Plan — this spec fixes
  behavior and boundaries, not exact spellings.
- **Exact file layout for the Slang-migrated `shaders/minimal_renderer/`
  shader** (one `.slang` file with two entry points, vs. a shared
  `import`-ed interface module plus two per-stage files) is left to the
  Plan, provided the shared-varying-struct authoring convention (ADR-0030)
  is honored.
- **Whether `slangc` exposes its own `-v`/`--version` flag** — **resolved
  by direct observation: it does not** (`slangc --version` →
  `error[E00017]`). Provenance is anchored on the resolved Vulkan SDK
  version plus the SDK's `slang-standard-module-<version>` directory name
  (ADR-0031).
- **The disclosed Slang push-constant reporting caveat**
  ([issue #5676](https://github.com/shader-slang/slang/issues/5676))
  **did not reproduce** for this spec's shader shape — one passing
  observation, not a guarantee: a future implementation must keep a
  regression test asserting push-constant offset/size against the real
  shader.
- **The reflection JSON has no official schema or versioning guarantee.**
  The parser is bound to the Slang version pinned by the supported Vulkan
  SDK, ignores unknown fields, fails hard on missing required fields, and
  **must be re-verified whenever the supported SDK/Slang version changes**
  (ADR-0030). Atlantis does not control this schema.
- **Whether CI (once it exists) needs its own explicit Vulkan SDK
  provisioning step distinct from the Vulkan Backend's** — likely "no, the
  same SDK installation serves both," but not verified against an actual
  CI image.
- **Recommended/tested Vulkan SDK version range for reproducibility
  guidance** (exact version numbers, and where it is documented) is left
  to the Plan.

## Out of Scope / Future Work

The migration of `shaders/minimal_renderer/`'s checked-in GLSL source and
`.spv` files to Slang, and the corresponding update to
`examples/minimal_renderer_demo`'s/the GPU test's `Material` construction
call site, are **required follow-up work for this spec's implementation
Plan** — see ADR-0031's Migration section.

Android Platform and Vulkan presentation, headless rendering, and image
regression testing all remain later, separately-specced candidates, not
advanced or unblocked by this spec beyond confirming (by inspection) that
Shader System's host-tool/portable-artifact design does not preclude a
future Android consumer.

A future spec introducing a second material, a texture, lighting, or
multiple shader pairs sharing a common uniform/descriptor layout is
expected to be the first real consumer that might motivate widening
`Device::createPipeline()`'s contract — not decided or designed by this
spec (ADR-0030).

A future spec may revisit this spec's reproducibility model (e.g. a fully
pinned/vendored Slang toolchain) if cross-machine/CI reproducibility
becomes a concrete, measured problem (ADR-0031); may need to revisit the
SPIR-V version target if Slang's "experimental" SPIR-V 1.0–1.2 tier
surfaces real problems against Atlantis's actual shaders and hardware; and
runtime shader compilation and hot-reload remain explicitly future,
not-yet-justified work (ADR-0029).
