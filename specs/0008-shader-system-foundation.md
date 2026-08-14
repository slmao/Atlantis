# Spec: Shader System Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved by human review — see Human Review Approval below.
- **Created:** 2026-08-13
- **Revised:** 2026-08-14 — re-based on Slang instead of GLSL + `glslc` +
  SPIRV-Reflect, following explicit human direction. See Revision Note
  below. Further revised the same day with hands-on toolchain evidence —
  see Validation Evidence below.
- **Related Plan(s):** [plans/0008-shader-system-foundation.md](../plans/0008-shader-system-foundation.md)
  (`Draft`, pending its own Human Review — drafting a Plan against this
  `Approved` spec is authorized per [AGENTS.md](../AGENTS.md); drafting it
  is not itself an implementation authorization, see that Plan's own
  Status).
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
  and [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  (all `Accepted`, none reopened or modified by this spec). See
  **Architectural Impact** below — four new decisions are identified and
  drafted alongside this spec:
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)
  (Phase 1 shader source language and compiler — Slang),
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)
  (build-time compilation boundary, the CLI-vs-library decision, and
  Tools integration),
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  (reflection strategy, metadata ownership, and the RHI/Pipeline
  boundary), and
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
  (artifact location, versioning, and reproducibility) — all four
  `Accepted` alongside this spec's own approval below.
- **Human Review Approval (2026-08-14):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-14, following a joint
  architecture review of this spec and
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)–[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
  conducted across this spec's own revision history (an initial
  GLSL-based draft; a Slang-based re-evaluation; a hands-on toolchain
  validation pass; and an independent, AI-assisted Human-Review-readiness
  audit — see each document's own Revision History for what each round
  raised and resolved before this approval) and a dedicated decision
  memo enumerating candidates HR-0008-01 through HR-0008-13. Two points
  were left open through that entire process specifically because they
  required a human choice rather than a technical recommendation, and are
  resolved explicitly, in full, as part of this approval:

  1. **SPIR-V compatibility baseline: Option A (compatibility-first)
     approved.** The Vulkan Backend's physical-device selection floor
     remains `VK_API_VERSION_1_0`
     (`src/vulkan_backend/src/vulkan_device.cpp`, unchanged); shader
     artifacts target **SPIR-V 1.0** via `slangc -profile spirv_1_0`;
     Slang's own "experimental" support-tier disclosure for that path is
     accepted and stays recorded, not minimized; and
     [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)
     is **not** reopened. **`spirv-val --target-env vulkan1.0` is
     promoted from a recommendation to a mandatory build-time
     verification step**, alongside this decision: every emitted shader
     artifact must pass it, and a missing `spirv-val` tool must fail
     **CMake configure**, on the same footing as a missing `slangc` — see
     [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)
     and
     [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
     for the updated Decision text this approval confirms. Option B
     (SPIR-V 1.3, raising the physical-device floor to Vulkan 1.1,
     reopening ADR-0024) is confirmed **rejected** for this round —
     retained in each ADR's Alternatives Considered as a live option a
     future compatibility-focused Human Review could still choose, not
     foreclosed permanently.
  2. **Slang `E50011` ("SPIR-V version too old") warning: Policy S
     (precise suppression) approved.** Atlantis Tools' CLI invocation of
     `slangc` passes `-warnings-disable 50011` — suppressing exactly this
     one, known, Slang-target-maturity warning and no other — with the
     reason (Slang's own SPIR-V 1.0 "experimental" disclosure) recorded
     permanently in
     [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md),
     not silently dropped. Suppression does not reduce verification
     coverage: `spirv-val` (now mandatory, per point 1) and, once a GPU
     path exists, Vulkan Validation Layers remain the substantive checks;
     suppressing `E50011` only keeps the build free of a known, already
     zero-information-content repeated warning, consistent with
     [docs/process/definition-of-done.md](../docs/process/definition-of-done.md)'s
     "builds cleanly with no new warnings" bar.
  3. **HR-0008-01 through HR-0008-13, as enumerated in the Human Review
     decision memo prepared for this PR, are all approved as drafted** —
     covering, among others: Slang as both Phase 1 shader language and
     compiler infrastructure; `slangc` sourced from the existing Vulkan
     SDK with no new dependency-acquisition mechanism; the CLI-subprocess
     (not library-link) architecture; Slang's own `-reflection-json`
     replacing the originally-proposed SPIRV-Reflect dependency;
     descriptor reflection scoped to validation of the existing fixed
     RHI/Vulkan-Backend descriptor contract only, never general
     pipeline-layout construction; the `"main"` SPIR-V entry-point
     compatibility policy; the RHI-integration target's position as a
     secondary target inside the Shader System module rather than a new
     top-level module; the version-bound (not self-designed-stable)
     reflection-JSON parsing policy; the configuration-independent
     shader-artifact model with shader debug-info explicitly out of
     Phase 1 scope; and ADR-0027's migration/retirement boundary. No
     `AGENTS.md` or `docs/architecture/module_boundaries.md` change is
     approved or implied by any of these.
  4. **Plan 0008 is authorized to be drafted, but only once this spec's
     own PR has merged into `main`** — not before, and not as part of the
     same branch/PR. **Implementation remains unauthorized.** Per
     [AGENTS.md](../AGENTS.md), drafting Plan 0008 does not itself
     authorize writing code; that future Plan must still pass its own (or
     a joint Spec+Plan) Human Review, per the same Spec → Plan → Human
     Review → Implementation → Verification → PR → Merge path every prior
     spec in this line has followed, before any source, test, shader, or
     build-configuration file for this spec's scope is written.

  Following this approval: ADR-0028 through ADR-0031 each move to
  `Accepted` (see each ADR's own header) and this spec moves to
  `Approved`. This checkbox-level approval is not itself an authorization
  to implement — see point 4 above and this spec's own Acceptance
  criteria throughout, which describe properties a future implementation
  must satisfy, not ones already verified.

## Revision Note (2026-08-14)

This spec's original 2026-08-13 draft proposed GLSL, compiled by
`glslc`, reflected via a new SPIRV-Reflect dependency. Following explicit
human direction to re-evaluate the whole design around **Slang** — as
Phase 1's shader language, its compiler infrastructure, and its
Vulkan/SPIR-V compilation-and-reflection foundation — this revision
replaces that design throughout, based on official Slang/Khronos/LunarG
research (cited at each point of use in
[ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)–[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)).
The most significant outcomes of that research:

- **SPIRV-Reflect is dropped entirely** — Slang's own `slangc
  -reflection-json` CLI output covers this spec's full reflection scope,
  so no second reflection library is introduced. This spec introduces
  **no new dependency acquisition mechanism** (no `FetchContent` entry,
  no package manager, no separate installer), while being explicit that
  `slangc` is nevertheless a **newly required build tool** inherited from
  the already-required Vulkan SDK installation.
- **Slang's own compiler (`slangc`) is sourced from the same Vulkan SDK
  installation `glslc` was already sourced from** — confirmed bundled
  since Vulkan SDK 1.3.296.0, and present in the exact SDK version
  (1.4.357.0) this repository's development environment already has
  installed. No new acquisition mechanism is introduced.
- **The compiler-library-vs-CLI question is resolved explicitly, with a
  documented comparison**: Atlantis Tools' CLI invokes `slangc` as a
  subprocess; Shader System never links Slang's compiler library.
- **The Shader-System-to-RHI dependency direction is resolved
  explicitly**: `Atlantis::RHI` never depends on `Atlantis::ShaderSystem`;
  Shader System's own core target references no RHI type in any public
  header; the RHI-shape mapping lives in a **secondary integration target
  inside the Shader System module — not a new top-level module**, leaving
  [AGENTS.md](../AGENTS.md)'s nine-module list unchanged.
- **Vertex-buffer stride and per-attribute offset are explicitly stated
  as Mesh/vertex-schema-owned, never reflected** — no shader reflection
  tool of any kind, Slang included, can derive a host-side interleaved
  vertex-buffer layout from shader source alone, and this spec does not
  claim otherwise.
- **A real, disclosed, unresolved tension is flagged rather than silently
  decided**: Slang's own documentation states its SPIR-V 1.3+ emission is
  "stable" while SPIR-V 1.0–1.2 emission is "experimental." Preserving
  the Vulkan Backend's current, unraised `VK_API_VERSION_1_0` minimum
  means targeting the experimental tier. This spec recommends accepting
  that, with the risk disclosed, over silently raising Atlantis's
  device-compatibility floor — but states this explicitly as a Risk
  requiring Human Review confirmation, not a foregone conclusion. See
  Risks & Open Questions.

A second revision the same day added the **Validation Evidence** section
below, after the design was checked against the real toolchain. That
evidence **corrected** three claims this Revision Note originally made
from documentation alone — see that section's own "Results" and "What
this evidence did not establish" subsections.

Nothing in this spec's Non-Goals, overall scope boundary (spec/ADR-only,
no Plan, no implementation), or relationship to
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
changes from the original draft — only the technical design underneath
that scope changes.

## Validation Evidence (2026-08-14)

The Slang-based design above was initially written from documentation
alone. It has since been checked against the actual toolchain. This
section records what was run, what was observed, and — importantly —
which earlier claims the evidence **contradicted**, so that a reviewer
can see the design was corrected by the results rather than filtered
through them.

**Scope and limits of this evidence.** These are single-machine
observations on one Slang version, gathered in a scratch directory
outside the repository. No experiment file is checked in, no Atlantis
target was built, and no GPU workload was run. Local observations are
labelled as such and are **never** escalated into vendor guarantees.

### Environment

| Item | Value |
|---|---|
| Vulkan SDK | `1.4.357.0` (`%VULKAN_SDK%`) |
| `slangc.exe` | present in the SDK's `Bin` directory |
| `spirv-val.exe` / `spirv-dis.exe` | present in the same directory; both report `SPIRV-Tools v2026.3` |
| Slang version | **`slangc` has no version flag** — `slangc --version` returns `error[E00017]: unknown command-line option '--version'`. Version identified instead from the SDK's `Bin\slang-standard-module-2026.13.1` directory. |
| Host | Windows |

### What was compiled

A minimal `.slang` module (scratch directory only) containing one vertex
and one fragment entry point sharing a single varying-interface `struct`,
with explicit `[[vk::location(0)]]`/`[[vk::location(1)]]` vertex inputs,
an explicit `[[vk::binding(0, 0)]]` uniform buffer, and a
`[[vk::push_constant]]` 4x4 matrix. A second, smaller probe module used
`[[vk::binding(3, 2)]]` specifically to test non-zero descriptor-set
reporting.

Representative command (fragment stage identical but for `-entry`/
`-stage`/output names):

```
slangc -target spirv -profile spirv_1_0 -entry vertexMain -stage vertex \
       -o out.vert.spv -reflection-json out.vert.refl.json minimal_mesh.slang
```

### Results

1. **SPIR-V 1.0 compilation succeeds.** Exit code 0; `spirv-dis` reports
   `; Version: 1.0`. It emits one warning: `warning[E50011]: SPIR-V
   version too old`. `-profile spirv_1_3` produces no such warning. The
   warning is suppressible (`-warnings-disable 50011`) with byte-identical
   output, but this spec does not silently adopt suppression — see Risks.
2. **Two version-selection findings that materially changed the design:**
   - **The default, with no `-profile`, emits SPIR-V 1.5** — which
     requires a Vulkan 1.2 device. Relying on the default would have
     silently raised Atlantis's device-compatibility floor.
   - **`-capability spirv_1_0` does not select the output version** — a
     compile passing only that flag still emitted SPIR-V 1.5. **Only
     `-profile` selects the version.**
   Both are now fixed as mandatory requirements in
   [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
3. **`-reflection-json` produces JSON successfully** for every
   invocation.
4. **Reflection field availability** (full classification in
   [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)):
   entry-point name, stage, descriptor binding index, descriptor set
   (`"space"`), resource type, push-constant offset/size, vertex-input
   index, and vertex-input element type were **all present**. Two
   corrections to earlier assumptions:
   - **Descriptor set appears as `"space"`, and is omitted entirely when
     the set is 0.** A parser must treat its absence as set 0.
   - **User-declared vertex inputs carry no `semanticName`.** Only the
     system-value output `SV_Position` did. The earlier draft's
     assumption that semantic name/index would identify vertex inputs was
     wrong.
5. **Vertex-input location is not a labelled JSON field.** The JSON
   reports `{"kind": "varyingInput", "index": N}`. Disassembly of the
   same module showed `OpDecorate %input_position Location 0` /
   `%input_color Location 1`, matching those indices — **but the source
   had already pinned them with explicit `[[vk::location(N)]]`**, so this
   observation cannot prove an independent guarantee. The design
   consequently treats the explicit authoring attribute as authoritative
   and the JSON index as a cross-check.
6. **Push-constant reflection was correct here** (`offset: 0`,
   `size: 64`); the known upstream concern
   ([issue #5676](https://github.com/shader-slang/slang/issues/5676)) did
   not reproduce for this shape. Recorded as one passing observation, not
   a guarantee.
7. **Entry-point naming confirmed in both directions.** Without
   `-fvk-use-entrypoint-name`: `OpEntryPoint Vertex %vertexMain "main"`
   (and `OpEntryPoint Fragment %fragmentMain "main"`). With the flag:
   `"vertexMain"`. Documented behavior and observed behavior agree, and
   the default is compatible with the Vulkan Backend's existing
   hard-coded `pName = "main"`.
8. **Reflection granularity is per invocation, not per module.** The
   top-level `"parameters"` array listed *all* module-scope parameters in
   both stages' output — including the camera uniform in the fragment
   stage, where it is unused. The `"entryPoints"` array contained only the
   entry point named by `-entry`. Per-entry-point usage is separately
   available via `entryPoints[].bindings[].used` (observed `1` for the
   vertex stage, `0` for the fragment stage).
9. **`spirv-val --target-env vulkan1.0` passed (exit 0)** for both the
   vertex and fragment artifacts.
10. **`spirv-dis` confirmed** `OpEntryPoint`, `Location`,
    `DescriptorSet`, `Binding`, and push-constant `Block`/`Offset`
    decorations, including `DescriptorSet 2` / `Binding 3` on the
    non-zero-set probe.
11. **Local determinism observed**: two identical compiles produced
    SHA-256-identical `.spv` **and** SHA-256-identical reflection JSON.
    **Single-machine, single-version observation only — not a Slang
    reproducibility guarantee**, and not treated as one anywhere in this
    spec.

### What this evidence did not establish

- Whether Slang *guarantees* `varyingInput` index equals SPIR-V
  `Location` independently of explicit `[[vk::location(N)]]` attributes.
- Whether the reflection JSON's shape is stable across Slang releases —
  **no official schema or versioning guarantee was found**, which is
  precisely why
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  fixes a version-bound parsing policy.
- Any runtime/GPU behavior. No Vulkan device was created and no
  Validation Layer run was performed; Validation Layers remain the
  mandatory GPU-path gate that only a future implementation can exercise.

## Summary

This spec introduces `Atlantis Shader System` (`src/shader_system/`) as a
real module for the first time: a build-time pipeline that compiles Slang
shader source to SPIR-V and reflects it, via `slangc` (Slang's own
official command-line compiler, sourced from the Vulkan SDK), invoked as
a subprocess by a new Atlantis Tools CLI executable. Shader System owns a
small, Atlantis-versioned reflection JSON schema (populated from Slang's
own `-reflection-json` output), a loader for it, and — via a separate,
narrowly-scoped adapter target — a mapping into RHI's existing
`Device::createPipeline()` parameter shapes. It replaces
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
temporary, human-run, checked-in-bytecode path — established explicitly
to avoid deciding Shader System's shape under Spec 0007's own
implementation pressure — with the real thing that ADR named as its own
migration boundary. It does **not** change RHI's `Pipeline`/
`CommandList`/`Device::createPipeline()` contract, does not introduce
runtime shader compilation or hot-reload, and does not implement the
migration of Spec 0007's existing checked-in GLSL shaders to Slang itself
(that is explicit follow-up work for this spec's future implementation
Plan).

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

Four concrete gaps stand between "a fixed, hand-authored `.spv` pair
checked into the repository" and "a real shader system," none of which
any existing `Accepted` ADR resolves:

- **No shader source language or compiler is chosen for Atlantis as a
  project.** [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  explicitly declined to record which language/compiler produced Spec
  0007's checked-in bytecode. This spec is the first to make that choice
  a reviewed, recorded decision, and — after the redirection this
  revision reflects — chooses Slang
  ([ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)).
- **No automated compilation exists.** Every `.spv` byte in this
  repository today was produced by a human manually running `glslc` and
  committing the result — real, accepted friction
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  own stated trade-off) that does not scale past one fixed material and
  leaves editing a shader source file with no automatic, build-integrated
  path to updated bytecode
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
- **No reflection exists**, so `Pipeline` creation's vertex-input/
  binding/push-constant layout is matched to its shader bytecode only
  "by convention and by the human author's own care" — an explicitly-
  named, explicitly-accepted-as-temporary gap
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md))
  this spec closes, for the fields reflection can genuinely own, with
  real, automated extraction plus a compiler-enforced cross-stage
  compatibility guarantee
  ([ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)).
- **No prior spec has decided where a generated binary artifact lives**,
  how a consumer finds it without a hardcoded path, or what
  "reproducible" means for something no longer committed to git
  ([ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)).

## Goals

- Choose, as an explicit reviewed architecture decision backed by
  official Slang/Khronos/LunarG documentation, Phase 1's shader source
  language and compiler toolchain
  ([ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)).
- Introduce `Atlantis Shader System` (`src/shader_system/`) as a real
  module with a reviewed, RHI-independent public API (a reflection
  schema, its loader, and a command-line-construction helper), depending
  only on Core.
- Resolve, explicitly and with a documented comparison, whether Shader
  System links Slang's compiler library or Atlantis Tools launches
  Slang's CLI — and fix the resulting module split between Shader System
  and Tools
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
- Decide reflection scope, remove the need for any new reflection
  library (Slang's own `-reflection-json` suffices), fix the reflection
  metadata's schema and ownership, fix exactly which data (shader-
  reflected vs. Mesh-schema-owned vs. attachment-format-sourced)
  populates each field of RHI's existing `VertexInputLayout`, and fix the
  exact module-dependency-direction seam by which reflection metadata
  feeds RHI's existing `Device::createPipeline()` contract — including a
  new, explicitly-named adapter target, not left to Plan
  ([ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)).
- Fix where compiled artifacts live, how consumers locate them without a
  hardcoded developer-machine path, Debug/Release and multi-config
  generator behavior, and what reproducibility means for a
  non-checked-in binary artifact, anchored on officially-verifiable
  provenance (the resolved Vulkan SDK version), not an unverified CLI
  flag assumption
  ([ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)).
- Fix the explicit migration boundary superseding
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  checked-in-GLSL-bytecode mechanism for `shaders/minimal_renderer/`'s
  existing shaders, including their migration to Slang source, scoped as
  follow-up work for this spec's future implementation Plan, not
  performed by this spec itself.
- Where official Slang material reveals a genuine, unresolved tension
  with Atlantis's existing architecture (the SPIR-V version/device-
  compatibility-floor question), state it explicitly as a Human Review
  question rather than silently resolving it either direction.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Runtime shader compilation.** Slang's compile/reflect invocation only
  ever happens via the build-time Tools CLI
  ([ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)).
  No Atlantis executable compiles a shader while running.
- **Shader hot-reload.** No file-watcher, no runtime recompilation path,
  no live-pipeline-swap mechanism of any kind.
- **A shader cache service.** Beyond CMake's own build-tree incremental
  dependency tracking, no content-hash-keyed cache, no cross-machine/
  distributed cache, and no runtime shader cache are introduced.
- **A material graph, node-based shader authoring, or any visual shader
  tooling.** Slang text source only.
- **A permutation/variant explosion framework.** Each logical shader
  compiles to a small, fixed set of `.spv` artifacts this round (one per
  stage); no `#define`/specialization-driven variant matrix.
- **A pipeline cache architecture** (`VkPipelineCache` persistence,
  pipeline-object deduplication/reuse across `Material` instances). Out
  of this spec's scope; `Pipeline`'s own ownership model
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md))
  is unchanged.
- **An asset database or general serialization/schema platform.** The
  reflection JSON schema
  ([ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  is fixed, narrow, and Atlantis-owned, versioned by a single integer
  field, not a general schema-migration framework.
- **Editor integration.** No tool UI, no live-shader-preview surface.
- **Shader debug-information generation, source-level shader debugging
  (e.g. RenderDoc shader source views), per-configuration optimization
  levels, and any Debug/Release shader permutation system.** Debug and
  Release C++ configurations use **identical** shader compilation flags,
  which is exactly why a single configuration-independent artifact is
  coherent rather than a shortcut — see
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).
  If a future spec introduces configuration-dependent shader flags, both
  the artifact output-path model and the incremental-build dependency
  model must be re-examined together.
- **D3D12/DXIL, Metal/MSL, WebGPU/WGSL, CUDA, or any Slang target beyond
  Vulkan/SPIR-V.** Slang itself officially supports several of these
  targets, but Phase 1 configures and invokes exactly one:
  Vulkan/SPIR-V. No cross-compilation, no multi-target shader
  architecture, per [AGENTS.md](../AGENTS.md) and
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
  Choosing a multi-target-capable compiler does not, by itself,
  authorize using more than one of its targets.
- **Building Slang from source, vendoring Slang's compiler library, or
  linking Slang's compiler API into any Atlantis target.** Slang's
  compiler is consumed exclusively as a prebuilt `slangc` binary from an
  externally-installed Vulkan SDK, invoked as a subprocess — see
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)/[ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
- **The Android implementation itself.** This spec's Tools CLI runs as a
  host build tool and its output artifact format does not preclude a
  future Android build consuming the same SPIR-V — but no Android
  Platform/Vulkan Backend work is performed or unblocked by this spec.
- **Headless rendering or image regression testing.** Unrelated to this
  spec's scope; both remain their own, separately-specced candidates.
- **Bindless resources, GPU-driven rendering, or neural shading.** Not
  designed, not scaffolded, per [AGENTS.md](../AGENTS.md) — notably, this
  exclusion applies even though Slang itself has documented neural-
  shading-adjacent capabilities elsewhere in its own ecosystem; this spec
  does not evaluate, use, or scaffold for any of that.
- **Runtime (the module), ECS, or any scene system.** This spec's
  verification is build/tool-level and a minimal GPU-integration check
  against the existing Minimal Renderer path — no new runtime executable
  or composition beyond what Spec 0007 already established.
- **A second Renderer or a second graphics backend.** Vulkan/SPIR-V only,
  unchanged from every prior spec in this line.
- **Modifying RHI's `Pipeline`/`CommandList`/`Device::createPipeline()`
  public contract.** [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)'s
  design is not reopened; Shader System's output is consumed by a
  separate, explicitly-named adapter target, not by a new RHI-level
  artifact type — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
- **Implementing the migration of `shaders/minimal_renderer/`'s checked-in
  GLSL source and `.spv` files to Slang, or changing
  `examples/minimal_renderer_demo`/the GPU test's `Material` construction
  call site.** Fixed as a required future-Plan follow-up by
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md),
  not performed by this spec.
- **Writing any source, test, shader, or build-configuration file, or
  downloading/installing Slang.** This spec (and its accompanying
  `Proposed` ADRs) is a design document only — no code, CMake target,
  shader file, or dependency is added by this round.

## Requirements

### Functional

**`Atlantis Shader System` module**

- New module `src/shader_system/`, target `atlantis_shader_system`, alias
  `Atlantis::ShaderSystem`, depending only on `Atlantis::Core` — see
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md),
  realizing the dependency edge
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
  already anticipated but never implemented.
- Exposes exactly: (1) a reflection-metadata schema type
  (`ReflectionMetadata`, Atlantis-owned, RHI-independent), (2) a loader
  (`Result<ReflectionMetadata, ReflectionLoadError>` from a file path),
  and (3) a pure, data-only helper that, given a shader source path, an
  entry-point name, and an output directory, returns the exact `slangc`
  argument list and expected output paths a caller should use. **Shader
  System never spawns a process, never touches an OS-process API, and
  never links Slang's compiler library.** Exact type/function names are a
  Plan-stage detail.
- References no RHI, RenderGraph, Renderer, Vulkan Backend, or Atlantis
  Platform type anywhere in its public headers — verifiable by
  inspection/grep, the same pattern this codebase already uses for other
  module-boundary verification.

**`Atlantis Tools` shader compiler CLI**

- New executable target (e.g. `atlantis_shader_compiler`, exact name a
  Plan-stage detail) under `src/tools/`, realizing
  [module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-tools)'s
  existing "shader precompilation CLI" responsibility for the first time.
  Depends on `Atlantis::ShaderSystem` and `Atlantis::Core`.
- Takes a Slang source file path and entry-point name(s) as command-line
  arguments, calls Shader System's command-line-construction helper,
  **spawns `slangc` as a subprocess itself** (including its
  `-reflection-json` invocation), checks its exit code, transforms
  Slang's own reflection JSON into Shader System's Atlantis-owned schema,
  and writes the resulting `.spv` and reflection JSON sidecar to a
  caller-specified output directory. Exits non-zero, with `slangc`'s own
  diagnostic surfaced verbatim, on any compile/reflect failure. **This is
  the only place in the whole Atlantis codebase that spawns `slangc` or
  any other shader compiler process** — see
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).

**Build-time compilation and CMake integration**

- Every shader source file is declared to CMake via
  `add_custom_command()`, with the source `.slang` file as `DEPENDS`,
  Atlantis Tools' CLI executable as the `COMMAND`, and the `.spv`/
  reflection-JSON pair as `OUTPUT` — giving correct incremental rebuild
  behavior with no custom staleness-tracking code.
- CMake locates `slangc` via `find_program()`, sourced from the Vulkan
  SDK already required by the Vulkan Backend
  ([ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)) —
  confirmed to bundle Slang since SDK 1.3.296.0 and present in the exact
  SDK version (1.4.357.0) already installed in this repository's
  development environment. A missing `slangc` fails CMake **configure**
  with an explicit, human-readable `FATAL_ERROR` naming the missing
  component — never a silently-skipped shader target, never a
  build-time-only failure for what is really a configure-time
  precondition.
- Compiled artifacts are written to a single, configuration-independent
  build-tree directory
  (`${CMAKE_BINARY_DIR}/shaders/<relative-path>/...`, exact
  interpolation a Plan-stage detail) — not nested under a
  `$<CONFIG>`-specific subdirectory, per
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).
- No shader compiler or reflection code is invoked by any
  `Atlantis::Core`/`Atlantis::RHI`/`Atlantis::VulkanBackend`/
  `Atlantis::RenderGraph`/`Atlantis::Renderer` CMake target or source
  file — verifiable by inspection/grep.

**Shader source authoring convention**

- Every logical shader (a vertex+fragment pair backing one `Material`) is
  authored as **one Slang module** whose vertex and fragment entry points
  **share one common, explicitly-declared `struct` type for the vertex-
  to-fragment varying interface** — the idiomatic Slang pattern this
  spec's authoring convention mandates, per
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md),
  to obtain Slang's own compile-time cross-stage type-checking as the
  primary interface-compatibility guarantee.
- Every vertex-input parameter carries an explicit `[[vk::location(X)]]`
  attribute; every explicitly-bound resource carries an explicit
  `[[vk::binding(binding, set)]]` attribute — mirroring the explicit
  `layout(location=...)`/`layout(binding=...)` discipline the current
  checked-in GLSL shaders already use, so locations/bindings are
  deterministic and directly knowable from source, not left to Slang's
  own default declaration-order assignment. This is load-bearing, not
  stylistic: the reflection JSON reports a `varyingInput` index rather
  than a field labelled as a SPIR-V `Location`, so the explicit
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
  entry point to `"main"` in the SPIR-V output — verified in both
  directions on the installed toolchain (see Validation Evidence).
- This keeps the Vulkan Backend's existing hard-coded `pName = "main"`
  (`src/vulkan_backend/src/vulkan_device.cpp`) working with **no RHI or
  Vulkan Backend change at all**, while still allowing meaningful source
  names.
- The reflection JSON reports the **source** name (`"vertexMain"`), not
  the emitted SPIR-V name; consumers must not treat them as the same
  string.
- **A future spec wanting source entry-point names preserved in SPIR-V
  must first change the RHI/Vulkan Backend contract** (`pName` is
  currently a hard-coded literal). Enabling this flag is therefore
  explicitly **not** a decision a future Plan may make on its own.

**Reflection**

- For each compiled shader stage, reflection extracts: descriptor
  bindings (set/binding/type), push-constant ranges (offset/size),
  vertex-input attributes (vertex stage only: index/element type —
  **not** stride or byte offset, see below), the stage's entry-point
  name, and shader stage. Every one of these was observed in a real
  `slangc -reflection-json` output (see Validation Evidence above); see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  for each field's evidence tier and the two parsing hazards
  (`"space"` omitted when the descriptor set is 0; user vertex inputs
  carry no `semanticName`).
- **Descriptor reflection is consumed for validation only, never for
  pipeline-layout construction this round.** `PipelineCreateParams`
  (`src/rhi/include/atlantis/rhi/types.h`) has no descriptor field, and
  the Vulkan Backend hard-codes its single binding
  (`src/vulkan_backend/src/vulkan_device.cpp`: set 0, binding 0, uniform
  buffer, vertex stage). Shader System encodes that same expectation as a
  fixed expected contract and **compares** reflected descriptor data
  against it; any deviation (different set, binding, type, or count) is a
  recoverable mapping error that fails the **build** with a readable
  diagnostic. Its value is moving a today-silent shader/backend binding
  mismatch from "maybe caught by Validation Layers at draw time on a GPU
  machine" to "always caught at build time on every machine" — obtained
  with **zero** RHI change. Reflection-driven descriptor layout
  construction is explicitly future work requiring
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  to be reopened. See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  "Descriptor reflection: validation only" section.
- A supplementary cross-stage interface compatibility check (vertex
  `Output` interface locations must be a superset of fragment `Input`
  interface locations, by location index) runs as part of the Tools
  CLI's reflect step, closing the narrow gap between "the shared source
  module type-checked" and "the two separately-emitted SPIR-V artifacts
  (required by RHI's unchanged two-blob contract) still agree" — this
  supplements, and does not duplicate or substitute for, Slang's own
  compile-time guarantee (the primary check).
- Reflection is obtained via **`slangc -reflection-json`** — an official
  Slang CLI capability, verified working on the installed SDK, requiring
  **no new dependency acquisition mechanism** (it is the same `slangc`
  already required for compilation).
  Shader System transforms Slang's own reflection JSON into its own
  small, Atlantis-owned, versioned JSON schema (one `"schemaVersion"`
  integer field), never re-exposing Slang's raw JSON shape as Atlantis's
  own contract — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  for why. Shader System owns this schema and provides the only
  supported loader.

**Vertex input layout — split authority, stated explicitly**

- `VertexAttribute::location` and `VertexAttribute::format` are sourced
  from Shader System reflection.
- `VertexAttribute::offsetBytes` and `VertexInputLayout::strideBytes` are
  sourced from Mesh/vertex-schema (Atlantis/Renderer-side C++) — **never
  from shader reflection**, because a host-side interleaved vertex
  buffer's byte layout is not a concept any shader source (Slang or
  otherwise) declares or could declare. No claim to the contrary is made
  anywhere in this spec or its ADRs.
- `PipelineCreateParams::colorFormat`/`depthFormat` are sourced from
  `Presentation::metadata().format`/the Vulkan Backend's fixed depth
  format, exactly as [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  already fixed — Shader System has zero involvement in this value.
- See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  authority table for the complete, field-by-field mapping.

**RHI/Pipeline boundary — decided explicitly**

- `Device::createPipeline(PipelineCreateParams)`
  (`src/rhi/include/atlantis/rhi/device.h`,
  `src/rhi/include/atlantis/rhi/types.h`) is not modified by this spec.
  **`Atlantis::RHI` never depends on `Atlantis::ShaderSystem`, in any
  form.**
- **`Atlantis::ShaderSystem`'s own core library target references no RHI
  type in any public header**, and must remain fully usable and testable
  with no knowledge that RHI exists.
- **The RHI-shape mapping lives in a secondary integration target inside
  the Atlantis Shader System module — not a new top-level module.**
  [AGENTS.md](../AGENTS.md)'s nine-module list is **unchanged by this
  spec**, and neither `AGENTS.md` nor
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
  is edited. This target establishes no independent subsystem ownership,
  owns no GPU resource, no `Pipeline`, and no compiler process; its sole
  job is combining Shader-System-owned metadata with consumer-supplied
  Mesh/vertex-schema data and validating the result into existing RHI
  value types. Concrete target/alias naming is a Plan-stage detail,
  deliberately not fixed here so the name cannot be read as announcing a
  tenth module. See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
- `Atlantis::VulkanBackend`'s `createPipeline()` implementation is
  unchanged and gains no dependency on Shader System, this integration
  target, or reflection of any kind.
- **No Slang type appears in any public header** of Core, RHI,
  RenderGraph, Renderer, or Shader System itself — Slang is only ever a
  subprocess.

**Artifact location, versioning, and migration**

- Compiled `.spv` bytecode and its Atlantis-schema reflection JSON
  sidecar are ordinary, non-checked-in build products, not committed to
  version control by this spec's implementation. `.gitignore` is
  extended to exclude the build-tree shader output directory.
- Every compiled artifact records the resolved Vulkan SDK version (the
  officially-confirmed provenance anchor — see
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
  for why this is used instead of an unverified `slangc --version`
  assumption) in its reflection JSON's own metadata — self-describing
  provenance, superseding
  [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
  manual, per-directory plain-text compiler-version note.
- `shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl` are migrated
  to Slang source (exact file layout a Plan-stage detail, but following
  this spec's own shared-varying-struct authoring convention above); the
  checked-in `.spv` files, the original GLSL source, and the README's
  compiler-version note are retired **as explicit follow-up work for this
  spec's future implementation Plan**, not by this spec's own PR — see
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
- Shader System's library API (the schema, its loader, the command-line-
  construction helper) uses `atlantis::Result<T, E>` throughout, matching
  every other Phase 1 module's convention. The Shader System module's
  RHI-integration target does the same, returning
  `Result<..., MappingError>` on a genuine reflection/Mesh-schema
  mismatch, or on a reflected descriptor shape that deviates from the
  fixed expected descriptor contract, rather than silently accepting
  either.
- Every new public type/function this spec introduces documents its
  thread-safety contract at its public API, per
  [ADR-0004](../adr/0004-phase1-threading-baseline.md)'s existing
  convention.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" for the Tools CLI's own execution, and "does not
  meaningfully slow down an incremental build when no shader source
  changed" for CMake's dependency-tracking behavior — no compile-time
  micro-benchmark target. Slang's own subprocess-per-invocation model
  (rather than a long-lived, session-reused in-process compiler) is a
  deliberate trade-off at Phase 1's shader count, per
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
- **Memory:** no new GPU memory allocation strategy question — this spec
  introduces no new RHI resource type, no `vkAllocateMemory` call.
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  is unchanged and unreopened.
- **Portability (within the Vulkan-only Phase 1 constraint):** the Tools
  CLI is a host build tool; its compiled SPIR-V output is
  backend-portable across Windows and (future) Android by construction.
  This spec does not implement or verify Android consumption; it
  verifies only that nothing in its own design assumes a Windows-only
  artifact path or build-tree layout. Slang's own official platform
  support (Windows, Linux, macOS, WebAssembly, Android as *host* build
  platforms, per
  [shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md))
  is not itself evidence Atlantis needs an Android host build in Phase
  1 — it only confirms Slang's own toolchain does not structurally
  preclude one later.
- **Other — dependency posture, stated precisely rather than as "zero
  dependencies":**
  - **No new independent dependency acquisition mechanism is
    introduced.** No `FetchContent` entry, no package manager, no
    from-source build, no separate installer. (SPIRV-Reflect, proposed in
    this spec's original 2026-08-13 draft, is removed entirely.)
  - **`slangc` is nevertheless a newly required build tool** and a new
    component the Atlantis build graph depends on. A machine whose Vulkan
    SDK installation lacks it could no longer build shader-consuming
    targets.
  - **`spirv-val`**, if
    [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)'s
    recommended static validation is adopted, is likewise a newly
    required build tool — confirmed present in the same SDK `Bin`
    directory as `slangc`.
  - **Both inherit their availability from the supported Vulkan SDK
    installation** [ADR-0006](../adr/0006-dependency-management.md)
    already requires, per
    [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
  - **Missing-tool failure stage is fixed:** absence of either tool fails
    at **CMake configure time** with an explicit `FATAL_ERROR`, never a
    silently-skipped target and never a late build-time surprise.
  Unit tests use the existing Catch2 v3 framework
  ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundaries (realizing, not moving, existing ones)

Realizes exactly the dependency edges
[module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
already anticipated for Shader System (depends on Core only; depended on
by Vulkan Backend/RHI "for pipeline construction, exact seam TBD" — this
spec resolves that seam as "not at all, directly; a separate, narrow
adapter target bridges the two," see
[ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md) —
and by Tools for offline compilation).

```
Build time (CMake, once per shader stage, incremental):
  add_custom_command(
    OUTPUT   <build-tree>/shaders/.../minimal_mesh.vert.spv
             <build-tree>/shaders/.../minimal_mesh.vert.refl.json
    COMMAND  atlantis_shader_compiler --entry=vertexMain
             --input=<source-tree>/shaders/.../minimal_mesh.slang
             --output-dir=<build-tree>/shaders/.../
    DEPENDS  <source-tree>/shaders/.../minimal_mesh.slang
             atlantis_shader_compiler  # (transitively: the tool itself)
  )

atlantis_shader_compiler (Tools CLI), one process per invocation:
  ShaderSystem::buildSlangcCommandLine(sourcePath, entryPoint, outDir)
    -> argv list, expected output paths
    -- pure data, Shader System's own helper --
  spawn `slangc <argv>` as a subprocess  -- Tools' own responsibility,
                                             never Shader System's --
    -- slangc compiles to SPIR-V AND emits its own -reflection-json --
  check slangc's exit code; surface its stderr verbatim on failure
  ShaderSystem::transformSlangReflection(slangJsonPath)
    -> Result<AtlantisReflectionJson, TransformError>
    -- re-projects Slang's raw JSON into Atlantis's own narrow,
       versioned schema --
  write minimal_mesh.vert.spv, minimal_mesh.vert.refl.json to --output-dir

Later, at whatever point a caller constructs PipelineCreateParams
(a future Renderer-level Material-construction call site, not designed
by this spec), via the Shader System module's RHI-integration target:
  ShaderSystem::loadReflectionMetadata(jsonPath) -> Result<ReflectionMetadata, ...>
  ShaderSystemRhiIntegration::toVertexInputLayout(metadata, meshStrideOffsetTable)
    -> Result<rhi::VertexInputLayout, MappingError>
    -- combines reflection-owned (location, format) with Mesh-schema-
       owned (stride, offsets); cross-validates, does not invent --
  ShaderSystemRhiIntegration::toPushConstantSize(metadata) -> std::size_t
  ShaderSystemRhiIntegration::validateDescriptorContract(metadata)
    -> Result<void, MappingError>   // validation only; drives nothing
  -- caller loads the corresponding .spv bytes directly --
  Device::createPipeline({ vertexShader, fragmentShader,
                            vertexInputLayout, colorFormat, depthFormat,
                            pushConstantSizeBytes })
  -- Device::createPipeline()'s own contract, unchanged from ADR-0025;
     colorFormat/depthFormat sourced from Presentation::metadata()/the
     Vulkan Backend's fixed depth format, not from Shader System --
```

### Source language and compiler

See
[ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)
for the full decision: Slang, targeting Vulkan/SPIR-V only, compiled by
`slangc` sourced from the existing Vulkan SDK requirement.

### Build-time compilation boundary and the CLI-vs-library decision

See
[ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)
for the full decision, including the axis-by-axis comparison this spec's
Human Review should verify: Shader System as a process-free library,
Atlantis Tools as the only `slangc`-spawning executable, CMake
`add_custom_command()` integration, configure-time failure on a missing
compiler, build-time failure with surfaced diagnostics on a compile
error, single-threaded per-invocation Tools CLI, exception-free library
API.

### Reflection strategy and RHI boundary

See
[ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)
for the full decision: reflection scope; Slang's own `-reflection-json`
as the sole reflection source (no new dependency); Atlantis's own
transformed JSON schema; the field-by-field `VertexInputLayout` authority
table (reflection vs. Mesh-schema vs. attachment-format); the shared-
varying-struct authoring convention and its supplementary location-index
check; the descriptor-reflection validation-only consumption scope; and
the secondary integration target inside the Shader System module that
alone bridges Shader System and RHI.

### Artifact location and reproducibility

See
[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)
for the full decision: configuration-independent build-tree output
location, no checked-in binary artifacts, Vulkan-SDK-version-anchored
provenance, and the explicit migration boundary superseding
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
checked-in-GLSL-bytecode mechanism.

### Threading

Unchanged from every prior spec in this line: single logical frame
thread for anything render-path-related, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md) — not applicable to
this spec's own build-time-only scope, since no Shader System code runs
on the render thread. The Tools CLI is a short-lived, single-threaded
build-time process, not part of the frame loop at all.

### Error handling

- Recoverable runtime errors (reflection-JSON transform failure, metadata
  load failure, RHI-adapter mapping mismatch) use `atlantis::Result<T,
  E>`, consistent with every prior spec's convention.
- A missing `slangc` toolchain is a CMake **configure**-time
  `FATAL_ERROR`, never a build-time or runtime failure.
- A Slang compile error is a **build**-time failure at the specific
  `add_custom_command()` step, with `slangc`'s own diagnostic surfaced
  verbatim.
- A cross-stage interface mismatch (the supplementary location-index
  check) is a **build**-time failure at the Tools CLI's reflect step.
- Every subprocess exit code (`slangc`'s own process exit status,
  checked by the Tools CLI, never by Shader System) is checked; a
  non-zero exit is never silently treated as success.

## Architectural Impact

This spec introduces architecture across four distinct, independently-
reviewable decisions, filed as four new `Proposed` ADRs — none decided by
this spec's prose alone:

1. **Phase 1 shader source language and compiler toolchain** — Slang,
   targeting Vulkan/SPIR-V only, compiled by `slangc` sourced from the
   existing Vulkan SDK requirement, as a build tool never linked or
   invoked at runtime; the SPIR-V-version/device-compatibility-floor
   tension disclosed and flagged for Human Review. Filed as
   [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
2. **Build-time compilation boundary, the CLI-vs-library decision, and
   Tools integration** — Shader System as a Core-only-dependent,
   process-free library; Atlantis Tools' first real content (the only
   `slangc`-spawning CLI); an explicit, evidence-based comparison
   resolving compiler-library-vs-CLI; CMake `add_custom_command()`
   integration; configure-time vs. build-time failure modes;
   exception-free error handling. Filed as
   [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
3. **Reflection strategy, metadata ownership, and the RHI/Pipeline
   boundary** — reflection scope via Slang's own `-reflection-json` (no
   new dependency); Atlantis's own transformed JSON schema; the explicit,
   field-by-field `VertexInputLayout` authority split between reflection
   and Mesh-schema; the shared-varying-struct cross-stage authoring
   convention; the validation-only scope of descriptor reflection; and
   the Shader-System-internal integration target resolving the RHI
   dependency-direction question without adding a top-level module. Filed as
   [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
4. **Artifact location, versioning, and reproducibility** — build-tree,
   configuration-independent output location; no checked-in binary
   artifacts; Vulkan-SDK-version-anchored provenance (not an unverified
   CLI flag); the explicit migration boundary superseding
   [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
   mechanism for `shaders/minimal_renderer/`, including its GLSL-to-Slang
   source migration, scoped as future-Plan follow-up. Filed as
   [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the four new ADRs above. **[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
remains `Accepted` and is explicitly not rewritten** — this spec's ADRs
supersede its *mechanism* going forward, per that ADR's own anticipated
Migration Boundary, not its historical correctness for Spec 0007's own
moment. Architectural Impact was not "None" — a new module
(`Atlantis Shader System`), a new build-time executable target (Tools'
CLI), a new explicitly-named adapter target
(a secondary integration target within the existing Shader System
module, not a new top-level module), and a new generated-artifact
category (build-tree, non-checked-in shader bytecode/metadata) are each
exactly what [AGENTS.md](../AGENTS.md)'s "What counts as significant"
section requires the full Spec → Plan → Human Review path for. **This
spec's approval is not itself an authorization to implement** — a Plan
may be drafted per [AGENTS.md](../AGENTS.md) only once this spec's own PR
has merged into `main`, and that future Plan must still pass its own
Human Review before any code, CMake target, or shader file is written.

## Alternatives Considered

- **Continue with the original GLSL + `glslc` + SPIRV-Reflect design**
  this spec's own 2026-08-13 draft proposed. Superseded following
  explicit human direction to re-evaluate around Slang — see this spec's
  own Revision Note and each ADR's own Revision History for the full,
  evidence-based reasoning. Not repeated here in full; the short version
  is that Slang's Khronos governance, Vulkan-tier official support, own
  reflection-JSON CLI output (eliminating a would-be new dependency), and
  shared-module cross-stage type-checking each represent a genuine
  capability improvement over the superseded design, at the real,
  disclosed cost of a GLSL-to-Slang migration and an unresolved SPIR-V-
  version/compatibility-floor question.
- **Split this spec into two or more smaller specs.** Still rejected, for
  the same reason as the original draft: the four decisions are
  genuinely interdependent.
- **Defer reflection to a later, second Shader System spec**, shipping
  compilation alone first. Still rejected — now for an even stronger
  reason than the original draft's: `slangc -reflection-json` reflection
  comes essentially "for free" alongside compilation in the same
  subprocess invocation, so deferring it would not even reduce this
  spec's own implementation surface meaningfully.
- **Choose HLSL instead of Slang**, or continue with GLSL rather than
  moving to Slang. See
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)'s
  own Alternatives Considered.
- **Link Slang's compiler library, or vendor/build Slang from source,
  instead of invoking the Vulkan-SDK-provided `slangc.exe` as a
  subprocess.** See
  [ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md)'s
  own Context (the full axis-by-axis comparison) and Alternatives
  Considered.
- **Introduce SPIRV-Reflect (or SPIRV-Cross, or hand-rolled SPIR-V
  parsing) despite Slang's own reflection capability.** See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  own Alternatives Considered — rejected as pure duplication of
  capability Slang's compiler already provides.
- **Continue checking in compiled `.spv` bytes, now generated by the
  build rather than by a human.** See
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)'s
  own Alternatives Considered.
- **Widen `Device::createPipeline()`'s contract, or fold the RHI-mapping
  helper directly into `Atlantis::ShaderSystem` itself**, rather than
  introducing a separate integration target inside the Shader System
  module. See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  own Alternatives Considered — rejected because either would create an
  unwanted dependency edge (`RHI` → `ShaderSystem`, or every reflection
  consumer → `RHI`) the adapter-target design avoids.
- **Derive vertex stride/offset from reflection by assuming a fixed
  packing convention.** See
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  own Alternatives Considered — rejected as an unsupported claim no
  official material backs.
- **Perform the `shaders/minimal_renderer/` migration as part of this
  spec's own implementation**, rather than deferring it to a future Plan.
  Still rejected — this spec remains explicitly scoped as a spec/ADR-only
  round.

## Testing & Verification Plan

*(This section describes what a future implementation Plan must verify —
consistent with this spec's own scope, no test, tool, or build
configuration is written by this round.)*

- **Build/tool integration tests** (once implemented):
  - Editing a `.slang` source file and re-running `cmake --build`
    triggers exactly the affected shader's recompilation (and no
    others).
  - Removing/renaming `slangc` from the located Vulkan SDK path causes
    CMake **configure** to fail with an explicit, readable error.
  - An intentionally-broken Slang source file (a syntax/type error)
    causes the **build** to fail at that shader's specific compile step,
    with `slangc`'s own diagnostic visible in the build log.
  - An intentionally-mismatched vertex/fragment varying-interface struct
    (a field renamed or retyped in only one of the two entry points)
    causes a **Slang compile error** — verifying the primary, compiler-
    enforced cross-stage guarantee actually fires, not merely the
    supplementary Atlantis-side location check.
  - **The emitted SPIR-V version is asserted to be exactly the intended
    one**, not merely assumed from the flags passed. This is a direct
    regression test for the two version-selection hazards recorded in
    Validation Evidence (the no-`-profile` default emitting SPIR-V 1.5,
    and `-capability` not selecting the version at all): a silent
    reversion to either would otherwise raise Atlantis's device floor
    undetected.
  - **Every emitted artifact passes `spirv-val --target-env vulkan1.0`**
    (if [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)'s
    recommended validation step is adopted), and a deliberately-invalid
    module is confirmed to make the build fail rather than pass silently.
  - **The emitted `OpEntryPoint` name is asserted to be `"main"`**,
    guarding the entry-point compatibility contract against a future
    change that adds `-fvk-use-entrypoint-name` without also changing the
    Vulkan Backend's hard-coded `pName`.
  - **A shader whose `[[vk::binding(...)]]` deviates from the fixed
    expected descriptor contract fails the build** with a readable
    diagnostic — the concrete regression test for the descriptor
    validation-only mechanism.
  - **Push-constant offset and size reported by reflection are asserted
    against expected values**, per the issue-#5676 caveat recorded in
    Risks: one passing observation is not a standing guarantee.
  - Debug and Release configurations (and, if the test environment is a
    multi-config generator, both configurations of the same build tree)
    both build successfully, sharing one compiled shader artifact set.
  - No developer-machine absolute path appears in any generated CMake
    cache entry, generated header, or test/demo source file that
    consumes a compiled shader artifact.
  - A full clean build followed by a second, no-op incremental build
    recompiles zero shaders.
- **GPU-independent unit tests** (Catch2, no Vulkan device required):
  - Shader System's reflection-metadata loader correctly parses a
    fixture Atlantis-schema reflection JSON file, rejects one with a
    `"schemaVersion"` newer than the loader supports, and rejects
    malformed JSON — all via `Result::Err`, no exception thrown.
  - Shader System's Slang-raw-JSON-to-Atlantis-schema transformation
    function correctly re-projects a fixture Slang `-reflection-json`
    output into the expected Atlantis-schema value.
  - The Shader System RHI-integration target's `toVertexInputLayout()`/
    `toPushConstantSize()` correctly combine a fixture
    `ReflectionMetadata` with a fixture Mesh-schema stride/offset table
    into the expected `VertexInputLayout`, and correctly return
    `Result::Err` when the two fixtures' attribute counts/locations
    disagree (the cross-validation case).
  - Shader System's command-line-construction helper produces the
    expected `slangc` argument list and expected output paths for a
    fixture input.
- **GPU integration tests (Windows/Vulkan):**
  - `shaders/minimal_renderer/`'s Slang-migrated shader, compiled through
    this spec's new pipeline instead of loaded from checked-in GLSL-
    derived bytecode, successfully backs a real `Device::createPipeline()`
    call, with Vulkan Validation Layers reporting zero warnings/errors.
  - This includes verifying, on real hardware/driver, whichever SPIR-V
    version target [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)'s
    Human-Review-confirmed resolution settles on — if SPIR-V 1.0 is
    confirmed, this is the concrete test that Slang's disclosed
    "experimental" tier for that version does not, in practice, produce
    invalid or Validation-Layers-rejected output for Atlantis's own
    actual shader source.
  - The resulting `Pipeline`, used to draw Spec 0007's existing minimal
    mesh, produces the same visible, correctly-shaded, correctly
    depth-ordered output Spec 0007 already verified.
- **Headless integration tests / image regression tests:** not
  applicable — unchanged from every prior spec's equivalent flag.
- **Vulkan Validation Layers:** mandatory and must run clean for the GPU
  integration test above.
- **Manual verification:** a developer deletes their entire build tree,
  reconfigures, and builds from clean, confirming: `slangc` is correctly
  located without any manually-set path beyond a normally-installed
  Vulkan SDK; `shaders/minimal_renderer/`'s migrated Slang shader
  compiles without error; `examples/minimal_renderer_demo` (or its
  equivalent, post-migration) runs and visibly renders the same mesh
  Spec 0007's own manual verification already confirmed.

## Risks & Open Questions

- ~~**SPIR-V version target vs. device-compatibility floor**~~ —
  **resolved by Human Review (2026-08-14): Option A, Vulkan 1.0 / SPIR-V
  1.0, with `spirv-val --target-env vulkan1.0` mandatory.** Slang's own
  documentation states SPIR-V 1.3+ emission is "stable" while SPIR-V
  1.0–1.2 emission is "experimental"
  ([docs.shader-slang.org — SPIR-V-Specific Functionalities](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/a2-01-spirv-target-specific.html)).
  **Four axes must not be conflated** (an earlier draft of this spec
  blurred them): the Vulkan *instance* `apiVersion` request (decided by
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md));
  the *physical device* selection floor, currently `VK_API_VERSION_1_0`
  in `src/vulkan_backend/src/vulkan_device.cpp` and never raised by any
  Accepted ADR; the **SPIR-V binary version** of an emitted module; and
  `slangc`'s own `-target`/`-profile`/`-capability` flags.
  **ADR-0024 decides nothing about the SPIR-V binary version** — it does
  not mention SPIR-V at all. This spec is the first to touch that axis.

  Per the Vulkan specification, a Vulkan 1.0 implementation must support
  **only** SPIR-V 1.0, while SPIR-V 1.3 first becomes mandatory at
  **Vulkan 1.1**
  ([`appendices/spirvenv.adoc`](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/appendices/spirvenv.adoc)).
  No extension bridges SPIR-V 1.3 onto a Vulkan 1.0 device
  (`VK_KHR_spirv_1_4` bridges Vulkan **1.1**-to-SPIR-V 1.4, so it does
  not help). **There is no third option that obtains Slang's "stable"
  tier while keeping a Vulkan 1.0 device floor** — this spec does not
  claim one exists.

  Two concrete options were presented to Human Review, which did not pick
  one unilaterally:
  1. **Option A — Target SPIR-V 1.0 via `-profile spirv_1_0`**
     (this spec's stated recommendation in
     [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)),
     preserving the Vulkan 1.0 physical-device floor and
     [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)'s
     explicit prior decision not to narrow device compatibility — at the
     cost of relying on a tier Slang's own docs call experimental, and of
     the `E50011: SPIR-V version too old` warning this path emits.
     **Verified working**: the artifact compiles, disassembles as
     `Version: 1.0`, and passes `spirv-val --target-env vulkan1.0`.
     **`spirv-val` is now a mandatory build-time check**
     ([ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md)),
     which reduces but **does not eliminate** the risk, and does **not**
     convert Slang's experimental path into a stable one.
  2. **Option B — Target SPIR-V 1.3** (Slang's "stable" tier), which
     requires raising the **physical-device** floor from
     `VK_API_VERSION_1_0` to `VK_API_VERSION_1_1` — a real narrowing of
     the device population Atlantis serves. This would require its own
     compatibility-focused Human Review covering
     [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)
     (whose Human Review twice declined to narrow device compatibility)
     and [AGENTS.md](../AGENTS.md)'s Windows/Android platform commitment.
     This spec has no authority to make that change and does not reopen
     ADR-0024.

  **Human Review approval (2026-08-14) selected Option A**, promoted
  `spirv-val` from recommended to mandatory as part of that choice, and
  explicitly declined Option B for this round (retained as a live future
  alternative, not foreclosed) — see this spec's own Human Review
  Approval note above and
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)'s
  updated Decision.
- ~~**Whether to suppress the `E50011` warning**~~ — **resolved by Human
  Review (2026-08-14): Policy S, precise suppression.** The warning is
  suppressible via `-warnings-disable 50011` with byte-identical output.
  Human Review approved suppressing exactly this one, known,
  Slang-target-maturity warning, with the reason (Slang's own SPIR-V 1.0
  "experimental" disclosure) recorded permanently in
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)
  rather than left to an implicit Plan-stage default, and with `spirv-val`
  (now mandatory) and future Vulkan Validation Layers retained as the
  substantive verification layers this suppression does not weaken —
  consistent with, not in tension with,
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md)'s
  "builds cleanly with no new warnings" item. The rejected alternative
  (Policy K — leave `E50011` unsuppressed on every SPIR-V 1.0 compile) is
  recorded in [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)'s
  Alternatives Considered.
- **Exact Tools CLI argument shape, exact Atlantis reflection-JSON schema
  field names, exact build-tree path interpolation syntax, and exact
  CMake target/property names** are left to the Plan — this spec fixes
  behavior and boundaries, not exact spellings.
- **Exact file layout for the Slang-migrated `shaders/minimal_renderer/`
  shader** (one `.slang` file with two entry points, vs. a shared
  `import`-ed interface module plus two small per-stage files) is left to
  the Plan, provided the shared-varying-struct authoring convention
  ([ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  is honored.
- ~~**Whether `slangc` exposes its own `-v`/`--version` flag**~~ —
  **resolved by direct observation: it does not.** `slangc --version`
  returns `error[E00017]: unknown command-line option '--version'`.
  Provenance is therefore anchored on the resolved Vulkan SDK version
  plus the SDK's `slang-standard-module-<version>` directory name, per
  [ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).
- **The disclosed Slang reflection push-constant reporting caveat**
  ([shader-slang/slang issue #5676](https://github.com/shader-slang/slang/issues/5676))
  **did not reproduce** for this spec's own shader shape — push-constant
  offset and size were reported correctly (see Validation Evidence). This
  is **one passing observation, not a guarantee**: a future implementation
  must keep a regression test asserting push-constant offset/size against
  the real shader, and must not treat this single result as closing the
  issue.
- **The reflection JSON has no official schema or versioning guarantee.**
  None was found in any official Slang material, and the payload carries
  no version field of its own. The parser is therefore bound to the Slang
  version pinned by the supported Vulkan SDK, ignores unknown fields,
  fails hard on missing required fields, and **must be re-verified
  whenever the supported SDK/Slang version changes** — see
  [ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).
  Atlantis does not control this schema.
- **Whether CI (once it exists) needs its own explicit Vulkan SDK
  provisioning step distinct from whatever it already needs for the
  Vulkan Backend** is a real open question this spec does not resolve —
  likely "no, the same SDK installation already serves both, since Slang
  is bundled in it," but not verified against an actual CI image.
- **Recommended/tested Vulkan SDK version range for reproducibility
  guidance** (exact version numbers, and where that guidance is
  documented) is left to the Plan.

## Out of Scope / Future Work

The migration of `shaders/minimal_renderer/`'s checked-in GLSL source and
`.spv` files to Slang, and the corresponding update to
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
`Device::createPipeline()`'s contract — not decided or designed by this
spec, per
[ADR-0030](../adr/0030-shader-system-reflection-strategy-and-rhi-boundary.md).

A future spec may also revisit this spec's reproducibility model (e.g. a
fully pinned/vendored Slang toolchain) if cross-machine/CI
reproducibility becomes a concrete, measured problem, per
[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md).

A future spec may need to revisit the SPIR-V version target decided by
the Human Review flagged in Risks & Open Questions, if Slang's own
"experimental" SPIR-V 1.0–1.2 tier surfaces real problems once exercised
against Atlantis's actual shaders and hardware.

Runtime shader compilation and hot-reload remain explicitly future,
not-yet-justified work, per this spec's own Non-Goals and
[ADR-0029](../adr/0029-shader-system-build-time-compilation-boundary.md).
