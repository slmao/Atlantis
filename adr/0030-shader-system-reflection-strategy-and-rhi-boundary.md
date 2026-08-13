# ADR 0030: Shader System — Reflection Strategy, Metadata Ownership, and the RHI/Pipeline Boundary

- **Status:** Proposed
- **Date:** 2026-08-13 (revised 2026-08-14 — see Revision History)
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Revision History

- **2026-08-13 (original):** Proposed SPIRV-Reflect as a new,
  `FetchContent`-acquired third-party dependency for reflection, with a
  bespoke Atlantis JSON schema, and left "whether Shader System depends
  on RHI" as a Plan-stage detail.
- **2026-08-14 (revised):** Superseded by this version, following the
  same human-directed re-evaluation toward Slang described in
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)'s
  Revision History. **SPIRV-Reflect is removed entirely** — Slang's own
  `slangc -reflection-json` output covers this spec's full reflection
  scope, so no second reflection library is introduced (see Context,
  below, for exactly what evidence supports this). This revision also
  **firms up the RHI dependency direction and the vertex-stride
  authority question explicitly**, rather than leaving either to a
  future Plan, per explicit human direction.

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
`VertexInputLayout` value (`strideBytes` plus a list of
`VertexAttribute{location, offsetBytes, format}`) the caller constructs
directly. RHI's own public contract does not require a "shader artifact"
abstraction at all — it requires bytes and a layout struct, both plain
data.

### What official Slang reflection material establishes

- **`slangc -reflection-json <path>`** is an official, documented CLI
  flag: "Emit reflection data in JSON format to a file"
  ([shader-slang/slang `docs/command-line-slangc-reference.md`](https://github.com/shader-slang/slang/blob/master/docs/command-line-slangc-reference.md)).
  This alone already produces the structured reflection payload this ADR
  needs — no C++ reflection-object-walking code, and no separate
  reflection library, is required to obtain it.
- **Reflection scope, confirmed against Slang's Reflection API guide**
  (the CLI's JSON output mirrors the same underlying reflection data
  model this guide documents): descriptor bindings are queryable via
  `getBindingSpace()`-family accessors, covering "Vulkan/SPIR-V
  descriptor set... or a WebGPU/WGSL binding group"; push constants are
  covered because "a `uniform` parameter defined in the parameter list of
  an entrypoint function is translated to a push constant in SPIR-V, if
  the type of the parameter is ordinary data" by default, or explicitly
  via `[vk::push_constant]`; entry-point parameters are exposed as
  `VariableReflection`s; shader stage is exposed via `getStage()`;
  "varying" (stage-boundary) parameters are reflected with semantic
  name/index via `getSemanticName()`/`getSemanticIndex()`
  ([docs.shader-slang.org — Using the Reflection API](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/09-reflection.html)).
- **Explicit binding/location control, matching this codebase's existing
  authoring discipline.** Slang exposes `[[vk::binding(binding, set)]]`
  to fix a resource's descriptor set/binding explicitly, and
  `[[vk::location(X)]]` to fix "the number of the vertex attribute from
  which input values are taken" explicitly
  ([shader-slang.org — SPIR-V-Specific Functionalities](http://shader-slang.org/slang/user-guide/spirv-target-specific)).
  This is a direct, idiomatic analog of the `layout(binding=...)`/
  `layout(location=...)` discipline the current checked-in GLSL shaders
  already use — Atlantis's authoring convention (see Decision, below)
  carries that same explicitness forward into Slang, rather than relying
  on Slang's own default declaration-order location/binding assignment.
- **What reflection explicitly does *not* cover, stated plainly by
  Slang's own documentation**: "Slang's base reflection API
  *intentionally* does not provide information about which shader
  parameters are or are not used by a program" (a separate
  `getEntryPointMetadata()` query exists for that, not needed by this
  spec's scope) — and, more importantly for this ADR's own Decision,
  **no vertex-buffer *stride* concept appears anywhere in the reflection
  documentation** ([docs.shader-slang.org — Using the Reflection API](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/09-reflection.html)).
  This is expected, not a gap in Slang specifically: a vertex buffer's
  interleaved byte stride is a property of how the *host application*
  packs its own vertex data in memory, which no shader source of any
  language (GLSL, HLSL, or Slang) declares or could declare — a vertex
  shader's `in`/entry-point-parameter declarations describe per-attribute
  *location* and *type* only, never how multiple attributes are
  interleaved into one buffer by whatever code fills that buffer. **No
  reflection tool, Slang included, can derive `VertexInputLayout::strideBytes`
  from shader source alone** — see Decision, below, for exactly who is
  responsible for it instead.
- **A real, disclosed reflection-maturity caveat exists**: an open Slang
  issue reports push constants not always being reported correctly by
  the reflection API in at least one case
  ([shader-slang/slang issue #5676](https://github.com/shader-slang/slang/issues/5676)).
  This is noted honestly as a real risk, not hidden — see Consequences,
  below — but does not, by itself, outweigh the evidence favoring Slang's
  reflection over introducing a second, unrelated reflection library
  (SPIRV-Reflect) whose own SPIR-V-parsing code Slang's compiler has
  already effectively subsumed for Atlantis's actual shader source.

## Decision

**Reflection is performed by `slangc -reflection-json`, invoked by
Atlantis Tools' CLI in the same subprocess invocation that compiles a
shader stage to SPIR-V (see
[ADR-0029](0029-shader-system-build-time-compilation-boundary.md)). No
second reflection library (SPIRV-Reflect or otherwise) is introduced.
Shader System transforms Slang's own reflection JSON into a small,
Atlantis-owned, versioned JSON schema; RHI's `Device::createPipeline()`
contract is unchanged; vertex-buffer stride and byte offsets remain an
explicitly Mesh/vertex-schema-owned value, never a reflected one; and a
new, narrowly-scoped RHI-adapter target — not `Atlantis::ShaderSystem`
itself — is the only place a Shader-System-to-RHI dependency exists.**

### Reflection scope

Reflection extracts exactly the following from each compiled shader
stage's Slang reflection output, per stage:

- **Descriptor bindings**: set/binding index, descriptor type (this
  round's shaders use exactly one: uniform buffer), and the shader
  stage(s) that reference it — sourced from Slang's binding-space
  reflection, per Context above.
- **Push-constant ranges**: offset, size, and the shader stage(s) that
  reference the range — sourced from Slang's default ordinary-uniform-
  parameter-to-push-constant mapping, or an explicit `[vk::push_constant]`
  annotation.
- **Vertex input attributes** (vertex stage only): **location** (from
  the shader's own explicit `[[vk::location(X)]]` attribute, per
  Atlantis's mandated authoring convention below) and **format**
  (derived from the Slang type of each vertex-stage entry-point
  parameter/struct field — e.g. a `float3` field maps to
  `VertexAttributeFormat::Float3`, mirroring
  `src/rhi/include/atlantis/rhi/types.h`'s existing enum). **Neither
  stride nor per-attribute byte offset within an interleaved buffer is
  part of this data** — see "Vertex input layout: split authority,"
  below.
- **Stage entry point name** and **shader stage** (`getStage()`).
- **Stage interface (varying) variables**, sufficient for the
  supplementary compatibility check described under "Cross-stage
  interface validation," below.

This is deliberately the same, narrow scope
[module_boundaries.md](../docs/architecture/module_boundaries.md) already
named ("bindings, push-constant layout") plus vertex input and entry
point — exactly what `Device::createPipeline()`'s existing parameter
shape (`VertexInputLayout`, `pushConstantSizeBytes`) already requires a
source for. **No sampler/combined-image-sampler reflection, no
specialization-constant reflection, and no compute-shader
(`local_size`) reflection** — none of Phase 1's shaders use any of these.

### Atlantis's own reflection JSON schema — not Slang's raw output

- The Tools CLI invokes `slangc -reflection-json <slang-json-path>` to
  get **Slang's own** reflection JSON, then calls a Shader-System-owned
  transformation function that reads exactly the fields "Reflection
  scope" above lists and re-emits them as **Atlantis's own, narrow, fixed
  JSON schema**, versioned by a single top-level `"schemaVersion"`
  integer field this ADR fixes must exist.
- **This transformation step exists specifically to avoid coupling the
  rest of Atlantis to Slang's own JSON shape**, which is not documented
  anywhere as a stable, versioned public contract in its own right (only
  the CLI flag that produces it is documented) — Slang could change its
  raw reflection JSON's field names/nesting across releases without
  breaking any documented compatibility promise; Atlantis's own schema,
  by contrast, is Shader-System-owned and versioned, and only this one
  transformation function needs to change if Slang's own JSON shape ever
  does.
- **Shader System owns this schema and provides the only supported
  loader.** No other module hand-parses either the Slang-raw JSON or the
  Atlantis-schema JSON directly. The loader validates `"schemaVersion"`
  and returns `atlantis::Result<ReflectionMetadata, ReflectionLoadError>`.
- **This is not a general asset/serialization platform.** The schema is
  fixed, narrow, and scoped exactly to "Reflection scope" above — no
  generic reflection-metadata-for-anything schema, no versioned-migration
  framework beyond the single schema-version integer check.
- **Metadata lifetime**: loaded once, at whatever point the caller needs
  it; Shader System does not cache, retain, or watch the file.

### Vertex input layout: split authority, stated explicitly

`VertexInputLayout` (`src/rhi/include/atlantis/rhi/types.h`) has two
kinds of fields, and this ADR fixes exactly which party owns which —
**no field's authority is left ambiguous or implicitly "whoever gets
there first":**

| `VertexInputLayout` field | Authority | Why |
|---|---|---|
| `VertexAttribute::location` | **Shader System reflection** (from Slang's `[[vk::location(X)]]`-derived reflection) | The shader source is the sole authority on which location index an input variable occupies — no other source could know this. |
| `VertexAttribute::format` | **Shader System reflection** (from Slang's per-attribute type info) | The shader source's declared type is the sole authority on what data shape the shader expects at that location. |
| `VertexAttribute::offsetBytes` | **Mesh/vertex-schema (Atlantis/Renderer-side, C++)** | This is the byte offset of one attribute *within a single interleaved vertex buffer record* — a property of how `Mesh`'s own vertex data is packed in memory, which no shader source declares or could declare (see Context, above). |
| `VertexInputLayout::strideBytes` | **Mesh/vertex-schema (Atlantis/Renderer-side, C++)** | Same reasoning: the total byte size of one interleaved vertex record is a host-side packing decision, not a shader-source-expressible concept, in Slang or any other shading language. |
| `PipelineCreateParams::colorFormat`/`depthFormat` | **Neither shader reflection nor Mesh schema** — sourced from `Presentation::metadata().format` / the Vulkan Backend's fixed depth-format choice, exactly as [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md) already fixed, entirely unrelated to Shader System | Attachment format is a swapchain/render-target property, not a shader or mesh property; Shader System has zero involvement in this value. |

- **Shader System's RHI-adapter helper (see below) never invents
  `offsetBytes`/`strideBytes` from reflection alone — it requires both a
  loaded `ReflectionMetadata` *and* a caller-supplied, Mesh-schema-sourced
  stride/offset table as separate inputs**, and its own job is limited to
  (a) combining the two into one `VertexInputLayout` value and (b)
  **cross-validating** that the attribute count and `location`s the
  Mesh-schema table names actually appear in the reflected metadata (a
  build/runtime-time check that the two independently-authored sources —
  shader source and Mesh vertex-schema code — have not drifted apart),
  never silently accepting a mismatch.
- This directly answers, and closes, the risk
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)
  originally accepted ("matched by convention and by the human author's
  own care, not introspected or validated") for the fields reflection
  *can* own (location, format), while being explicit and honest that the
  fields reflection *cannot* own (stride, offset) remain exactly as
  convention-dependent as before, on the Mesh/vertex-schema side alone —
  this ADR does not, and cannot, claim reflection eliminates that risk
  entirely.

### Cross-stage interface validation

- **Primary guarantee: Slang's own compiler type-checking, obtained by
  an explicit Atlantis authoring convention.** Atlantis's shader source
  organization convention (fixed by this spec, not left to Plan) requires
  every logical shader (a vertex+fragment pair backing one `Material`) to
  be authored as **one Slang module** (one `.slang` file, or one file
  that `import`s a second) whose vertex and fragment entry points **share
  one common, explicitly-declared `struct` type for the vertex-to-
  fragment varying interface** — the idiomatic Slang pattern of returning
  that struct from the vertex entry point and taking it as a parameter to
  the fragment entry point. Under this convention, an interface mismatch
  (wrong field, wrong type, missing field) is an ordinary Slang **compile
  error**, caught by Slang's own type system before SPIR-V emission or
  reflection ever runs — a strictly stronger guarantee than GLSL's
  original location-index-only convention could offer, because it is
  full structural type-checking, not a numeric-index heuristic.
- **Supplementary, narrower Atlantis-side check — closing a specific,
  named gap, not duplicating Slang's own guarantee.** Because RHI's
  unchanged `Device::createPipeline()` contract still requires **two
  separately-supplied SPIR-V byte blobs** (`vertexShader`/`fragmentShader`,
  per [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)),
  Atlantis Tools' CLI compiles/reflects each entry point into its own
  separate `.spv`/reflection-JSON output (see
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)),
  even when both entry points were authored in one type-checked module.
  This means the single-module type-checking guarantee applies to the
  *shared source*, but the two *independently emitted* SPIR-V artifacts
  are a second, separate step where — in principle, however unlikely —
  a compiler bug or an unusual per-entry-point compilation flag could
  cause the two independently-emitted binaries to diverge from what the
  shared source guaranteed. The Tools CLI's reflect step therefore
  additionally checks that the vertex stage's reflected varying-`Output`
  locations are a superset of the fragment stage's reflected varying-
  `Input` locations, by location index, failing the build with a
  diagnostic naming the offending location and both source files on
  mismatch. **This check is a narrow, low-probability-gap closer for the
  two-separate-artifacts consequence of RHI's unchanged contract — not a
  restatement of, or a substitute for, Slang's own compile-time
  guarantee**, which remains the primary, load-bearing check.

### RHI/Pipeline boundary — decided explicitly, not deferred

- **`Device::createPipeline(PipelineCreateParams)`'s existing contract
  (`src/rhi/include/atlantis/rhi/device.h`,
  `src/rhi/include/atlantis/rhi/types.h`) is not modified by this spec.**
  It continues to accept raw `ShaderStageBytecode` and an explicit
  `VertexInputLayout`/`pushConstantSizeBytes`. RHI still does not parse,
  validate, or reflect SPIR-V. **`Atlantis::RHI` never depends on
  `Atlantis::ShaderSystem`, in any form — not `PUBLIC`, not `PRIVATE`.
  This is a firm rule, not a default pending Plan confirmation.**
- **`Atlantis::ShaderSystem`'s own public headers reference no RHI type.**
  Its `ReflectionMetadata` type and every enum it defines (descriptor
  type, vertex-attribute-format-equivalent, stage) are Shader-System-
  owned plain data, entirely independent of `atlantis::rhi::*` types —
  Shader System is fully usable, testable, and meaningful with zero
  knowledge that RHI exists.
- **A new, explicitly-named, narrowly-scoped adapter target performs the
  RHI-shape mapping** — not `Atlantis::ShaderSystem` itself. This target
  (e.g. `atlantis_shader_system_rhi_adapter`, alias
  `Atlantis::ShaderSystemRhiAdapter`, exact name a Plan-stage detail)
  depends on **`PUBLIC Atlantis::ShaderSystem`** and **`PUBLIC
  Atlantis::RHI`** — `PUBLIC` on both because its own public header
  returns `atlantis::rhi::VertexInputLayout`/`std::size_t` (push-constant
  size) values built from a `ShaderSystem::ReflectionMetadata` input, so
  any consumer of this adapter's header necessarily sees both types. This
  is a real, explicit `Atlantis::ShaderSystemRhiAdapter` → `Atlantis::RHI`
  dependency edge — **not** an `Atlantis::RHI` → `Atlantis::ShaderSystem`
  edge, and not a form of "Shader System depends on RHI" in the sense
  [module_boundaries.md](../docs/architecture/module_boundaries.md)
  would need to be amended for: `Atlantis::ShaderSystem` itself, the
  library every other potential consumer of reflection metadata would
  actually link, remains exactly as RHI-independent as stated above. Only
  this one, narrow, explicitly-named adapter target — whose entire reason
  to exist is bridging the two — carries the dependency.
  - Its function: given a loaded `ShaderSystem::ReflectionMetadata`
    (vertex stage) and a caller-supplied Mesh-schema stride/offset table
    (see "Vertex input layout," above), returns a populated
    `atlantis::rhi::VertexInputLayout`; given a loaded
    `ShaderSystem::ReflectionMetadata` (either stage), returns
    `pushConstantSizeBytes`. Both cross-validate against the reflected
    data per "Vertex input layout," above, returning
    `atlantis::Result<..., MappingError>` on a genuine mismatch, not a
    silently-accepted best-effort guess.
  - **Called by whichever future Renderer-level `Material`-construction
    call site builds `PipelineCreateParams`** — not designed by this
    spec (see Non-Goals in
    [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)).
    That call site links `Atlantis::ShaderSystemRhiAdapter` (which
    transitively brings in both `Atlantis::ShaderSystem` and
    `Atlantis::RHI`); it does not need to link `Atlantis::ShaderSystem`
    directly.
- **`Atlantis::VulkanBackend` gains no dependency on Shader System,
  the RHI adapter, or reflection of any kind.** Its `createPipeline()`
  implementation continues to consume exactly the same
  `PipelineCreateParams` it does today.
- **This decision is a deliberate scope minimization**, not an
  oversight: it keeps this spec's architectural footprint on RHI at
  zero, avoiding reopening
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)'s
  already-`Accepted` `Pipeline`/`CommandList` design, while still giving
  the dependency-direction question (a real architectural decision the
  user explicitly required be resolved here, not deferred) a firm,
  reviewable answer: **RHI depends on nothing new; a new, narrow adapter
  target depends on both Shader System and RHI; Shader System itself
  depends on neither RHI nor the adapter.**

## Consequences

### Positive

- **No new third-party dependency.** Removing SPIRV-Reflect entirely
  (superseded by Slang's own `-reflection-json`) means this spec
  introduces zero new `FetchContent`-acquired libraries — a strictly
  smaller dependency footprint than this ADR's original version.
- Closes [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  named gap for the fields reflection genuinely can own (location,
  format, bindings, push-constant layout), with a real, automated,
  build-time source, while being explicit about the fields it cannot
  (stride, offset) — an honest, not overstated, closure of that gap.
- Zero RHI-surface churn: `Device::createPipeline()`, `PipelineCreateParams`,
  `VertexInputLayout` are all unchanged, so
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)'s
  already-reviewed and `Accepted` design is not reopened.
- The module-dependency-direction question the original version of this
  ADR left to Plan is now a firm, explicit, reviewable decision: a named
  adapter target, `PUBLIC` on both sides, with `Atlantis::ShaderSystem`
  itself kept RHI-independent.
- Slang's shared-struct cross-stage authoring convention gives a
  genuinely stronger primary compatibility guarantee (full type-checking)
  than the original GLSL-based design's location-index-only check ever
  could, with the Atlantis-side check now correctly scoped as a narrow
  supplement rather than the sole guarantee.

### Negative / Trade-offs

- **The disclosed Slang reflection-maturity caveat**
  ([issue #5676](https://github.com/shader-slang/slang/issues/5676))
  is a real, if narrow, risk this ADR accepts rather than hides — a
  future implementation must verify push-constant reflection behaves
  correctly against Atlantis's own actual shader source during Plan-
  stage/implementation verification, not merely assume the documentation
  above is sufficient.
- **Atlantis's own reflection JSON schema requires a real transformation
  step** (Slang-raw JSON → Atlantis schema) that a "just re-expose
  Slang's own JSON" design would not have needed — accepted deliberately
  to avoid coupling the rest of Atlantis to an undocumented, unversioned
  external JSON shape, at the cost of one small, additional, testable
  transformation function to write and maintain.
- **Vertex stride/offset remain exactly as convention-dependent as
  before** — this ADR does not, and honestly cannot, claim to have
  solved that half of [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  original risk; only the reflectable half (location, format, bindings,
  push-constant layout) is closed.
- The supplementary cross-stage location check only fires against two
  independently-emitted SPIR-V artifacts derived from what was originally
  one type-checked module — it cannot catch an error the shared-module
  authoring convention itself would already have rejected at compile
  time, and is only a safety net for the narrower "did two separate
  emissions of a validated module diverge" case.
- Keeping `Device::createPipeline()` unchanged still requires *some* call
  site to construct `PipelineCreateParams`, now via
  `Atlantis::ShaderSystemRhiAdapter` rather than a hand-written literal —
  an improvement over
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  status quo, but not a fully automatic "shader in, `Pipeline` out" API.

## Alternatives Considered

- **SPIRV-Reflect**, as this ADR's own original version proposed.
  Rejected on revision: Slang's own `-reflection-json` already covers
  this spec's full reflection scope with zero new dependency
  acquisition, licensing, or build-integration surface — introducing a
  second, unrelated reflection library to re-derive information Slang's
  own compiler already emits would be pure duplication with no offsetting
  benefit.
- **SPIRV-Cross.** Same rejection reasoning as SPIRV-Reflect, and (as
  this ADR's original version already noted) it is primarily a
  cross-compilation tool, an even worse fit once Slang itself already
  supplies reflection.
- **Hand-rolled SPIR-V parsing.** Same rejection reasoning as before —
  and doubly unnecessary now that Slang's own compiler, which already
  must fully understand the SPIR-V it emits, exposes that understanding
  directly via `-reflection-json`.
- **Re-expose Slang's own raw reflection JSON as Atlantis's contract**,
  skipping the transformation step into Atlantis's own schema. Rejected:
  couples the rest of Atlantis to an external tool's undocumented JSON
  shape with no stated compatibility guarantee across Slang releases —
  Atlantis's own narrow, versioned schema is a small, cheap insulation
  layer against that risk.
- **Have `Device::createPipeline()` itself accept a Shader-System-defined
  artifact type (bytes + metadata bundled) instead of raw bytes plus an
  adapter-constructed `VertexInputLayout`.** Rejected for this round, for
  the same reason as this ADR's original version: no concrete need to
  make `Atlantis::RHI` depend on `Atlantis::ShaderSystem` (or vice versa)
  exists yet; the adapter-target design gives the same practical
  convenience (a caller does not hand-write `VertexInputLayout` literals)
  without touching RHI's own dependency graph at all.
- **Fold the RHI-shape mapping directly into `Atlantis::ShaderSystem`
  itself** (this ADR's own original version's unresolved lean). Rejected
  on revision, per explicit human direction that module dependency
  direction is an architectural decision this ADR must fix, not leave to
  Plan: doing so would make every consumer of Shader System's reflection
  types (including any future consumer with no interest in RHI at all)
  transitively depend on `Atlantis::RHI` — the narrow adapter target
  avoids that coupling entirely.
- **Derive vertex stride/offset from reflection by assuming a fixed,
  conventional packing rule** (e.g. "attributes are always tightly packed
  in declaration order"). Rejected: this is exactly the kind of silent,
  unstated assumption the user's own review explicitly warned against —
  no official Slang (or any shader reflection tool's) material supports
  deriving a host-side interleaved-buffer stride from shader source
  alone, and asserting otherwise would misrepresent what reflection can
  actually guarantee.
