# ADR 0027: Temporary Pre-Compiled SPIR-V Shader Artifact Sourcing, Pending Shader System

- **Status:** Proposed
- **Date:** 2026-08-11
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)

## Context

[ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
fixes that `Device::createPipeline()` consumes raw SPIR-V bytecode. Spec
0007 needs *some* SPIR-V bytes to actually exist for its minimal
material's vertex and fragment shaders, but Shader System — the module
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
already names as the future owner of "shader authoring/compilation/
reflection — turning shader source into a backend-consumable artifact"
— has no spec, no chosen shader source language, and is explicitly out of
this spec's Non-Goals (per
[AGENTS.md](../AGENTS.md)'s Phase 1 sequencing, Shader System is a later
candidate than Minimal Renderer,
[specs/README.md](../specs/README.md) Section B).

Left unaddressed, this is exactly the kind of gap that quietly resolves
itself the wrong way: whichever ad hoc shader-sourcing choice this spec's
implementation makes first — a build-time `glslc`/`dxc` invocation folded
into CMake, an embedded compiler dependency, a caching scheme — risks
becoming Shader System's de facto starting shape without ever having been
reviewed as one, and without Shader System's own future spec ever having
had a real choice about shader source language, reflection strategy, or
caching architecture. This ADR exists specifically to draw that line
explicitly, per this spec's own explicit instruction: this temporary path
"must not evolve into a hidden Shader System" and "must not decide shader
source language, reflection, or caching architecture ahead of time."

## Decision

**Shader bytecode for this spec's minimal material is pre-compiled
offline, by a human, outside any build step this spec introduces, and
checked into the repository as static `.spv` files under a narrowly-
scoped, clearly-labeled test/demo asset directory** — the exact path
(e.g. under `examples/`'s new Minimal Renderer demo, or a dedicated
`tests/assets/shaders/` directory) is a Plan-stage detail; this ADR fixes
the *sourcing model*, not the path.

- **No shader compiler (`glslc`, `dxc`, or any equivalent) is invoked by
  any CMake target, build script, or Atlantis Core/Renderer/Tools code
  this spec introduces.** A human developer runs a compiler manually,
  once, during development, to produce each `.spv` file this spec needs,
  the same way this repository already treats, e.g., a hand-authored test
  fixture — the `.spv` files are committed source artifacts, not build
  output.
- **No reflection is performed on the SPIR-V bytecode, by RHI, Vulkan
  Backend, or Renderer.** Vertex input layout, binding/uniform layout, and
  push-constant layout are all **hand-specified in C++**, alongside the
  `.spv` bytes, by whichever code calls `Device::createPipeline()`
  ([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
  — matched to the shader source by convention and by the human author's
  own care, not introspected or validated against the bytecode by any
  Atlantis code. A mismatch between the hand-specified layout and the
  actual shader is a shader-authoring error, caught (if at all) by Vulkan
  Validation Layers at pipeline-creation or draw time, not by any
  Atlantis-side reflection this spec does not build.
- **No shader source language decision is made by this spec.** Whatever
  language the human author used to produce the `.spv` files (GLSL, HLSL,
  or otherwise) is not recorded, enforced, or built upon by any Atlantis
  code — this spec's C++ surface only ever sees the compiled bytes.
- **No shader caching, hot-reload, or shader-variant/permutation
  mechanism is introduced.** Each `.spv` file is loaded once, read
  directly into memory, and handed to `Device::createPipeline()` — no
  cache keyed by content hash, no file-watcher, no runtime recompilation
  path of any kind.
- **This path is explicitly temporary and explicitly bounded.** It exists
  solely to satisfy this spec's own minimal-material acceptance target
  (see [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)).
  It is not exposed as a general-purpose "load a shader by name" API for
  future rendering features to build on, and is not extended by this spec
  beyond the exact vertex/fragment pair its own material needs.
- **Migration boundary:** once a future Shader System spec exists and is
  implemented, it becomes the sole producer of SPIR-V bytecode and
  reflection metadata feeding `Device::createPipeline()` for every future
  material — at that point, this spec's checked-in `.spv` files and
  hand-specified layout become either (a) inputs Shader System itself
  compiles from equivalent source, superseding this path entirely, or (b)
  a narrow, explicitly-labeled legacy fixture retained only if a test
  still needs a minimal, Shader-System-independent pipeline-creation
  check. Which of the two is a decision for that future spec to make, not
  this one — this ADR does not pre-commit to either outcome, only states
  that `Device::createPipeline()`'s own consumer contract (compiled bytes
  + hand-specified layout in) is designed to remain a valid low-level
  entry point Shader System can sit on top of, not a shape this spec
  expects to be discarded.

## Consequences

### Positive

- Unblocks this spec's own acceptance target (a real, visible, textured-
  by-vertex-color-or-similar mesh) without deciding, or even implicitly
  leaning toward, any Shader System architecture question — source
  language, reflection strategy, caching, hot-reload all remain fully
  open for that future spec.
- Keeps the boundary between "this spec's temporary shader-sourcing
  convenience" and "Shader System" bright-line clear and reviewable: no
  compiler dependency, no reflection code, no cache — anyone auditing this
  spec's diff can confirm the boundary was not crossed by inspection.
- `Device::createPipeline()`'s bytes-plus-hand-specified-layout contract
  is a genuinely useful low-level API on its own merits (many engines keep
  exactly this as their lowest-level pipeline-creation entry point even
  after a shader system exists above it) — so this temporary path costs
  nothing to keep as a permanent low-level capability, even after Shader
  System lands.

### Negative / Trade-offs

- Hand-specifying vertex-input/binding layout in C++, matched to shader
  source only by author discipline, is exactly the kind of error-prone,
  easy-to-desync pattern a real reflection system exists to eliminate —
  accepted deliberately, as a small, fixed, single-material cost, not a
  general practice this spec recommends for anything beyond its own
  narrow scope.
- Requiring a human to manually invoke a shader compiler and check in the
  result is real developer friction versus an automated build step — an
  intentional trade-off: automating it now would require deciding
  Shader-System-shaped questions (which compiler, which flags, where
  intermediate artifacts live) this spec is explicitly not authorized to
  decide.
- The checked-in `.spv` files' provenance (which compiler, which flags,
  which source) is not tracked by any Atlantis tooling — a real, accepted
  gap for this narrow, temporary path; a future Shader System is expected
  to replace this with a real, tracked build pipeline.

## Alternatives Considered

- **Invoke `glslc`/`dxc` as a CMake build step for this spec's shaders.**
  Rejected: this is a real Shader-System-shaped decision (compiler choice,
  invocation strategy, build-system integration) made under this spec's
  narrow pressure rather than reviewed on its own terms — exactly the
  "quietly becomes the first path" failure mode this ADR exists to
  prevent, per this spec's own explicit instruction not to let this
  temporary path decide Shader System's shape.
- **Embed a shader-compiler library (e.g. `shaderc`) as a new runtime or
  build-time dependency**, to compile shader source at build or first-run
  time. Rejected for the same reason as the CMake-step alternative, plus
  it adds a new third-party dependency ([AGENTS.md](../AGENTS.md) requires
  its own spec/ADR review for that) to serve a need (runtime/build-time
  shader compilation) no approved spec has actually validated yet.
- **Perform SPIR-V reflection** (via `spirv-cross` or hand-rolled
  SPIR-V parsing) to derive vertex-input/binding layout automatically,
  rather than hand-specifying it. Rejected: reflection strategy is
  explicitly a Shader System design question
  ([module_boundaries.md](../docs/architecture/module_boundaries.md)
  already names "reflection metadata (bindings, push-constant layout)" as
  that module's responsibility); deciding it here, for one fixed
  vertex/fragment pair, would either constrain or duplicate that future
  spec's own decision.
- **Store shader source (not compiled bytecode) in the repository, and
  compile it at Atlantis process startup.** Rejected: this is a strictly
  larger commitment than a build-time compile step (a shader compiler
  becomes a runtime dependency of every Atlantis executable, not just the
  build), for the same "quietly becomes Shader System" reason already
  rejected above.
