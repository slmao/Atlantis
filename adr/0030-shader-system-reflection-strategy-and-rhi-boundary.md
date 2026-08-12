# ADR 0030: Shader System — Reflection Strategy, Metadata Ownership, and the RHI/Pipeline Boundary

- **Status:** Proposed
- **Date:** 2026-08-13
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Context

[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md) fixed
that Spec 0007's `Pipeline` creation uses a **hand-specified**
`VertexInputLayout` (`src/rhi/include/atlantis/rhi/types.h`), matched to
its checked-in shader bytecode only "by convention and by the human
author's own care, not introspected or validated against the bytecode by
any Atlantis code" — and named reflection as explicitly out of that
spec's scope, deferred to Shader System's own future spec. That ADR's
Alternatives Considered rejected performing reflection at that time
specifically because "reflection strategy is explicitly a Shader System
design question" — this ADR is that design question's answer.

`Device::createPipeline(PipelineCreateParams)`
(`src/rhi/include/atlantis/rhi/device.h`,
`src/rhi/include/atlantis/rhi/types.h`) already fixes RHI's own consumer
contract, unchanged by Spec 0007:
`PipelineCreateParams::vertexShader`/`fragmentShader` are opaque
`ShaderStageBytecode` byte spans ("RHI does not parse, validate, or
reflect it"), and `PipelineCreateParams::vertexInputLayout` is a
`VertexInputLayout` value the caller constructs directly. RHI's own
public contract does not require a "shader artifact" abstraction at all —
it requires bytes and a layout struct, both plain data.

[module_boundaries.md](../docs/architecture/module_boundaries.md) names
Shader System's responsibility as "turning shader source into a
backend-consumable artifact (e.g. SPIR-V) plus reflection metadata
(bindings, push-constant layout) usable to build RHI pipeline objects,"
and separately flags "the exact seam TBD by its own spec" for how RHI/
Vulkan Backend consumes that metadata — this ADR fixes that seam.

## Decision

**Reflection is performed once, at build time, by Shader System's library
code (wrapped by the Tools CLI, per
[ADR-0029](0029-shader-system-build-time-compilation-boundary.md)),
using SPIRV-Reflect. Its output is a small, versioned JSON sidecar file
written next to each compiled `.spv` artifact. `Device::createPipeline()`'s
existing contract (raw SPIR-V bytes + a hand-specified
`VertexInputLayout`) is unchanged by this spec — reflection metadata is
consumed one layer above RHI, by whichever code constructs
`PipelineCreateParams`, not inside RHI or Vulkan Backend.**

### Reflection scope

Reflection extracts exactly the following from each compiled SPIR-V
module, per shader stage:

- **Descriptor bindings**: set/binding index, descriptor type (this
  round's shaders use exactly one: uniform buffer), and the shader stage(s)
  that reference it.
- **Push-constant ranges**: offset, size, and the shader stage(s) that
  reference the range.
- **Vertex input attributes** (vertex stage only): location, name, and
  SPIR-V's own reported format/component count for each input variable.
- **Stage entry point** (SPIR-V modules may contain more than one; this
  round's compiled artifacts each contain exactly one, and reflection
  records its name explicitly rather than assuming `"main"`).
- **Stage interface variables** (`Input`/`Output` storage class
  variables) at each stage's boundary, sufficient for the cross-stage
  compatibility check described below.

This is deliberately the same, narrow scope
[module_boundaries.md](../docs/architecture/module_boundaries.md) already
named ("bindings, push-constant layout") plus vertex input and entry
point, which `Device::createPipeline()`'s existing parameter shape
(`VertexInputLayout`, `pushConstantSizeBytes`) already requires a source
for. **No sampler/combined-image-sampler reflection, no specialization-
constant reflection, and no compute-shader (`local_size`) reflection** —
none of Phase 1's shaders use any of these; adding their reflection now
would be speculative.

### Cross-stage interface compatibility

The Tools CLI's reflect step, after reflecting both a vertex and fragment
stage compiled from the same logical shader (a Plan-stage detail decides
exactly how a "shader pair" is expressed to the CLI), checks that the
vertex stage's `Output` interface locations are a superset of the
fragment stage's `Input` interface locations, matching by location index
(SPIR-V's own linking rule). A mismatch is a build failure at the reflect
step, with a diagnostic naming the offending location and both shader
source files — the first form of automated cross-check
[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
"caught, if at all, by Vulkan Validation Layers at pipeline-creation or
draw time" gap did not have.

### Reflection library: SPIRV-Reflect (new dependency)

- **SPIRV-Reflect** (Khronos-adjacent, MIT-licensed, single-header/
  small-source-tree C library purpose-built for exactly this: extracting
  descriptor-binding, push-constant, and I/O-interface metadata from
  compiled SPIR-V) is adopted as a new third-party dependency of
  `Atlantis::ShaderSystem` only.
- Acquired via CMake `FetchContent`, pinned to a specific tagged release
  (`GIT_TAG`, never a floating branch), per
  [ADR-0006](0006-dependency-management.md)'s existing "small,
  source-buildable development dependency" category — SPIRV-Reflect is
  exactly that category: it is a small C library with no further
  transitive dependencies, built from source per build tree, the same
  acquisition story Catch2 already uses ([ADR-0007](0007-test-framework.md)).
  This is **not** a rejection of ADR-0006's Vulkan-SDK-is-external-not-
  FetchContent rule — SPIRV-Reflect is a small library Atlantis compiles
  itself, not a platform SDK/toolchain Atlantis locates on the host.
- **This dependency is private to `Atlantis::ShaderSystem`'s
  implementation.** No public Shader System header exposes an
  SPIRV-Reflect type; Shader System's own reflection-result types are
  plain Atlantis-owned structs (see "Metadata form," below). No other
  module (RHI, Vulkan Backend, Renderer, Tools' own public surface beyond
  the CLI's own `main()`) links against or includes SPIRV-Reflect
  directly.
- **Exact pinned version, license text inclusion, and
  `FetchContent_Declare()` details are a Plan-stage detail** — this ADR
  fixes the library choice and its acquisition category, not the specific
  tag.

### Metadata form and ownership

- **A small, versioned JSON file, one per compiled shader stage** (e.g.
  `minimal_mesh.vert.refl.json` alongside `minimal_mesh.vert.spv` — exact
  naming a Plan-stage detail), written by the Tools CLI at the same time
  as the `.spv` file, into the same build-tree output location
  ([ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)).
  Not a generated C++ header (avoids coupling shader recompilation to a
  second, code-generation build step feeding back into C++ compilation)
  and not a binary/serialized format (JSON is human-readable for
  debugging and needs no schema-versioning machinery beyond a single
  top-level `"schemaVersion"` integer field this ADR fixes must exist,
  bumped whenever the JSON's own shape changes incompatibly).
- **Shader System owns the schema and provides a loader.** A public
  Shader System type (e.g. `atlantis::shader_system::ReflectionMetadata`,
  exact naming a Plan-stage detail) is the *only* supported way any other
  Atlantis code reads this JSON — no other module hand-parses the JSON
  file directly. Shader System's loader validates `"schemaVersion"` and
  returns `atlantis::Result<ReflectionMetadata, ReflectionLoadError>` (or
  equivalent), matching
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)'s
  exception-free convention.
- **This is not a general asset/serialization platform.** The schema is
  fixed, narrow, and scoped exactly to what "Reflection scope" above
  lists — no generic reflection-metadata-for-anything schema, no
  versioned-migration framework beyond the single schema-version integer
  check, no asset-database concept. A future spec needing a broader
  metadata/asset model is not authorized or unblocked by this decision.
- **Metadata lifetime**: loaded once, at whatever point the caller needs
  it to build `PipelineCreateParams` (see below); Shader System does not
  cache, retain, or watch the file — each load is a fresh file read.

### RHI/Pipeline boundary: unchanged

- **`Device::createPipeline(PipelineCreateParams)`'s existing contract
  (`src/rhi/include/atlantis/rhi/device.h`,
  `src/rhi/include/atlantis/rhi/types.h`) is not modified by this spec.**
  It continues to accept raw `ShaderStageBytecode` (opaque SPIR-V words)
  and an explicit `VertexInputLayout`/`pushConstantSizeBytes` the caller
  supplies. RHI still does not parse, validate, or reflect SPIR-V, and
  still references no Shader System type in any public header —
  `Atlantis::RHI` gains **no** new dependency on `Atlantis::ShaderSystem`.
- **Reflection metadata is consumed one layer above RHI**, by whichever
  code constructs `PipelineCreateParams` — in this spec's own scope, that
  is Shader System's own small helper responsible for turning a loaded
  `ReflectionMetadata` plus the corresponding `.spv` bytes into a
  `VertexInputLayout`/`pushConstantSizeBytes` pair matching RHI's existing
  struct shapes (a pure data-mapping function, not a new RHI capability).
  A future Renderer-level `Material`-construction call site (not
  designed by this spec — see Spec 0008's own Non-Goals) is expected to
  call this helper instead of hand-writing a `VertexInputLayout` literal,
  replacing [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  "matched by convention" mechanism with a metadata-driven one, without
  RHI itself changing shape.
- **`Atlantis::Vulkan Backend` gains no dependency on Shader System or
  reflection.** Vulkan Backend's `createPipeline()` implementation
  continues to consume exactly the same `PipelineCreateParams` it does
  today — it has no awareness that a `VertexInputLayout` value was
  constructed from reflection metadata rather than hand-written literal.
- **This decision is a deliberate scope minimization**, not an oversight:
  it keeps this spec's architectural footprint on RHI at zero, avoiding
  reopening [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)'s
  already-`Accepted` `Pipeline`/`CommandList` design. A future spec
  (e.g. one introducing a second material, needing pipeline-layout
  caching, or wanting RHI itself to accept a "compiled shader artifact"
  type) may choose to widen this seam — not decided or foreclosed here.

## Consequences

### Positive

- Closes [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  named gap ("no reflection is performed... a mismatch... is a
  shader-authoring error, caught if at all by Validation Layers") with a
  real, automated, build-time cross-check between hand-authored/generated
  layout and the shader's actual interface.
- Descriptor bindings, push-constant ranges, and vertex-input attributes
  are all available from the same JSON payload, with no separate
  query/introspection API needed per consumer.
- Zero RHI-surface churn: `Device::createPipeline()`,
  `PipelineCreateParams`, `VertexInputLayout` are all unchanged, so
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)'s
  already-reviewed and `Accepted` design is not reopened by this spec.
- SPIRV-Reflect is purpose-built for exactly this extraction task
  (unlike SPIRV-Cross, which is primarily a cross-compiler/decompiler and
  a heavier dependency for a reflection-only need), keeping the new
  dependency's scope narrow and its maintenance surface small.
- A fixed, narrow JSON schema with a single version-check field is
  simple enough to not need a general schema/migration framework, while
  still being forward-compatible if the schema needs a field added later.

### Negative / Trade-offs

- SPIRV-Reflect is a genuinely new third-party dependency — real
  provenance/licensing/version-pin bookkeeping (mitigated by
  `FetchContent`'s `GIT_TAG` pin and MIT license compatibility, but not
  eliminated).
- A JSON sidecar file is a second artifact per shader stage (beyond the
  `.spv` bytes) that must stay present and in sync with its `.spv`
  sibling — mitigated by both being produced atomically by the same
  Tools CLI invocation (see
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)), but
  a manually-deleted or hand-edited sidecar file (outside the normal
  build) is not detected/re-derived automatically beyond the next full
  rebuild.
- The cross-stage interface compatibility check is intentionally narrow
  (location-index matching only) — it does not check type/format
  compatibility between a vertex output and a fragment input at the same
  location (e.g. a `vec3` output feeding a `vec4` input), which SPIR-V
  linking itself, and Vulkan Validation Layers, still catch independently
  at pipeline-creation time; this spec does not attempt to fully replace
  Validation Layers as the type-correctness backstop.
- Keeping `Device::createPipeline()` unchanged means every future
  material still requires *some* call site to construct
  `PipelineCreateParams` by hand, now via Shader System's mapping helper
  rather than a hand-written literal — an improvement over
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  status quo, but not a fully automatic "shader in, `Pipeline` out" API;
  a future spec may choose to fold that helper's responsibility further
  into RHI or Renderer if a concrete need justifies widening the seam.

## Alternatives Considered

- **SPIRV-Cross**, instead of SPIRV-Reflect. Rejected: SPIRV-Cross is
  primarily designed for cross-compiling SPIR-V into GLSL/HLSL/MSL source
  (relevant to a future multi-backend phase, not Phase 1's Vulkan-only
  scope) and is a substantially larger dependency surface for a
  reflection-only need; SPIRV-Reflect is purpose-built for exactly the
  binding/push-constant/interface extraction this ADR needs and nothing
  more.
- **Hand-rolled SPIR-V parsing** (reading the SPIR-V binary format
  directly, extracting `OpDecorate`/`OpVariable`/`OpEntryPoint`
  instructions without a third-party library). Rejected: avoids a new
  dependency, but SPIR-V's binary format and its many decoration/type
  interactions are exactly the kind of well-trodden, easy-to-get-subtly-
  wrong parsing problem a maintained, widely-used library like
  SPIRV-Reflect already solves correctly — reinventing it duplicates real
  engineering effort for a well-understood, already-solved problem, with
  ongoing maintenance cost as SPIR-V itself evolves.
- **Checked-in hand-authored reflection metadata** (a human writes the
  JSON/struct by hand, mirroring [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  own hand-specified-layout approach, just formalized as a checked-in
  file). Rejected: this does not close
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  named gap at all — a hand-written reflection file can drift out of sync
  with its shader exactly as easily as a hand-written `VertexInputLayout`
  literal can; automated extraction from the compiled bytecode is the
  entire point of "reflection."
- **Code-generation into a C++ header** (reflection emits a generated
  `.h` file with `constexpr` binding/layout constants), rather than a
  JSON sidecar loaded at whatever point a caller needs it. Rejected:
  couples shader recompilation to regenerating and recompiling C++ code
  that includes the generated header, adding real build-graph complexity
  (the generated header must exist before any translation unit that
  includes it can compile) for a benefit (avoiding a runtime file read
  and JSON parse) this spec's own performance bar
  ("does not stall... unnecessarily," not a micro-benchmark target) does
  not require.
- **Have `Device::createPipeline()` itself accept a Shader-System-defined
  artifact type (bytes + metadata bundled) instead of raw bytes plus a
  hand/metadata-derived `VertexInputLayout`.** Rejected for this round:
  this would make `Atlantis::RHI` depend on `Atlantis::ShaderSystem`,
  which [module_boundaries.md](../docs/architecture/module_boundaries.md)
  does not currently require and this spec has no concrete need to
  introduce — RHI's existing bytes-plus-struct contract already serves
  this spec's mapping-helper design without any RHI-side change; widening
  RHI's contract is left to a future spec if a concrete need for it
  appears.
