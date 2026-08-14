# ADR 0030: Shader System — Reflection Strategy, Metadata Ownership, and the RHI/Pipeline Boundary

- **Status:** Accepted
- **Date:** 2026-08-13 (revised 2026-08-14 — see Revision History)
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-14; see
  [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)'s
  Human Review Approval note for the full approval record this ADR's
  Decision is part of, including the explicit confirmation that
  descriptor reflection is scoped to validation of the existing fixed
  RHI/Vulkan-Backend descriptor contract only (never general pipeline-
  layout construction), and that the RHI-integration target is a
  secondary target inside the Shader System module, not a new top-level
  module.
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Approved`)

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
- **2026-08-14 (second revision — evidence-based):** Revised again after
  an actual `slangc`/`spirv-dis`/`spirv-val` experiment was run against
  the installed Vulkan SDK (see
  [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)'s
  "Validation Evidence" section for the full record). Three claims in the
  previous revision were **not supported by the real reflection JSON** and
  are corrected here: (1) descriptor-binding reflection was described in
  a way that implied it feeds pipeline-layout construction, which the
  unchanged `PipelineCreateParams` cannot express — its consumption is
  now narrowed to **contract validation only**; (2) the reflection JSON
  reports a Slang-internal `varyingInput` **index**, not a field labelled
  as a SPIR-V `Location`, so the location-mapping rule is now stated as
  an explicitly-verified-by-disassembly correspondence rather than an
  assumed JSON guarantee; (3) the entry-point naming policy
  (`-fvk-use-entrypoint-name` must not be passed) is now fixed
  explicitly, having been verified in both directions. Every reflection
  field is now classified by how it was actually established.
- **2026-08-14 (third revision — Human Review approval):** The
  descriptor-reflection consumption scope (validation-only), the
  `"main"` entry-point compatibility policy, and the RHI-integration
  target's position as a Shader-System-internal secondary target were
  all reviewed and approved without change — see the Deciders field
  above and
  [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)'s
  Human Review Approval note. This ADR moves to `Accepted`.

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
secondary integration target inside the Shader System module — not
Shader System's own core library target, and not a new top-level module —
is the only place a Shader-System-to-RHI dependency exists.**

### Reflection scope, classified by how each field was established

Every field below is tagged with its evidence tier, per the experiment
recorded in
[specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)'s
"Validation Evidence" section. **`[JSON-verified]`** = observed in a real
`slangc -reflection-json` output on Slang 2026.13.1 (Vulkan SDK
1.4.357.0). **`[SPIRV-verified]`** = established only by disassembling
the emitted module with `spirv-dis`, not by a labelled JSON field.
**`[host-owned]`** = not obtainable from any shader reflection, by
construction.

- **Entry-point name** `[JSON-verified]` — `entryPoints[].name`. **This
  is the Slang *source* function name (e.g. `"vertexMain"`), which is
  deliberately *not* the name that ends up in the emitted SPIR-V** — see
  "Entry-point naming policy," below. Consumers must never assume these
  two strings are the same.
- **Shader stage** `[JSON-verified]` — `entryPoints[].stage` (observed
  values `"vertex"`, `"fragment"`).
- **Descriptor binding index** `[JSON-verified]` —
  `binding: {"kind": "descriptorTableSlot", "index": N}`.
- **Descriptor set index** `[JSON-verified, with a parsing hazard]` — the
  same object's `"space": N` field. **`"space"` is omitted entirely when
  the set index is 0**, as confirmed by a dedicated probe (a
  `[[vk::binding(3, 2)]]` resource emitted `{"kind":
  "descriptorTableSlot", "space": 2, "index": 3}`, while a
  `[[vk::binding(0, 0)]]` resource emitted no `"space"` key at all). A
  parser **must** treat an absent `"space"` as set 0 rather than as
  missing data.
- **Resource type** `[JSON-verified]` — `type.kind` (observed:
  `"constantBuffer"`, with a nested `elementType` struct describing its
  members), sufficient to distinguish this round's only descriptor type
  (uniform buffer).
- **Push-constant range offset/size** `[JSON-verified]` — the parameter
  carries `binding: {"kind": "pushConstantBuffer", "index": 0}` at the
  container level, and its `elementVarLayout.binding` carries
  `{"kind": "uniform", "offset": 0, "size": 64}` for this round's single
  4x4 matrix. **The known upstream push-constant reflection concern
  ([shader-slang/slang issue #5676](https://github.com/shader-slang/slang/issues/5676))
  did not reproduce for this shader shape** — recorded as a single
  passing observation, explicitly *not* generalized into a guarantee;
  see Consequences.
- **Per-entry-point usage flag** `[JSON-verified]` —
  `entryPoints[].bindings[].used` (observed `1` for the camera uniform in
  the vertex stage, `0` in the fragment stage). Noted because Slang's own
  Reflection API guide states the base reflection API "*intentionally*
  does not provide" usage information; the CLI JSON evidently does expose
  it. Atlantis does **not** depend on this field this round — recorded
  only so a future spec knows it exists.
- **Vertex input attribute index** `[JSON-verified]` +
  **its correspondence to the SPIR-V `Location`** `[SPIRV-verified]` —
  the JSON reports `binding: {"kind": "varyingInput", "index": N}`. It
  does **not** contain any field named or documented as a SPIR-V
  `Location`. Disassembly of the same module confirmed
  `OpDecorate %input_position Location 0` / `%input_color Location 1`,
  matching the JSON's `varyingInput` indices 0 and 1 — **but the shader
  source had already pinned those values with explicit
  `[[vk::location(0)]]`/`[[vk::location(1)]]` attributes**, so this
  observation cannot distinguish "Slang guarantees varyingInput index ==
  Location" from "both simply reflect what the author explicitly wrote."
  Atlantis therefore relies on the *authoring convention* (explicit
  `[[vk::location(N)]]` on every vertex input, mandatory per this ADR)
  as the authority, treating the JSON index as a cross-check of what the
  author declared — never as an independently-derived location.
- **Vertex input element type** `[JSON-verified]` — `type: {"kind":
  "vector", "elementCount": 3, "elementType": {"kind": "scalar",
  "scalarType": "float32"}}`, sufficient to map to
  `VertexAttributeFormat::Float3`
  (`src/rhi/include/atlantis/rhi/types.h`). Any reflected type that does
  not map onto that enum's currently-single value **must** produce an
  explicit mapping error, never a silent fallback — see "Vertex input
  layout," below.
- **Vertex input *semantic name*** — **not available for ordinary vertex
  inputs.** The JSON emitted `semanticName` only for the system-value
  output `SV_Position`; user-declared vertex input fields carried a
  `varyingInput` binding and no `semanticName`. This corrects an earlier
  assumption in this ADR's own previous revision, which had inferred from
  Slang's Reflection API guide that `getSemanticName()`-style data would
  be the vertex-input identity mechanism.
- **Vertex-buffer stride and per-attribute byte offset** `[host-owned]` —
  absent from the reflection output by construction, and correctly so:
  these describe how *host* code packs an interleaved vertex buffer, which
  no shader source of any language declares. (The `elementStride` keys
  that do appear in the JSON belong to *uniform-buffer* layout, not
  vertex-buffer layout, and must not be confused for it.)

**No sampler/combined-image-sampler reflection, no specialization-constant
reflection, and no compute-shader (`local_size`) reflection** — none of
Phase 1's shaders use any of these.

### Reflection output granularity (per invocation, not per module)

Confirmed by compiling the vertex and fragment entry points of the *same*
`.slang` module in two separate `slangc` invocations:

- The top-level `"parameters"` array is **module-scope**: it listed
  *both* the camera uniform and the push constant in **both** stages'
  JSON, including in the fragment stage's output where the camera uniform
  is unused.
- The `"entryPoints"` array contained **exactly the one entry point named
  by that invocation's `-entry` flag** — not every entry point in the
  module.

Consequently a consumer must read per-stage facts from
`entryPoints[0]`, and must **not** assume the top-level `"parameters"`
array describes only what the compiled stage actually uses.

### Atlantis's own reflection JSON schema — not Slang's raw output

- The Tools CLI invokes `slangc -reflection-json <slang-json-path>` to
  get **Slang's own** reflection JSON, then calls a Shader-System-owned
  transformation function that reads exactly the fields "Reflection
  scope" above lists and re-emits them as **Atlantis's own, narrow, fixed
  JSON schema**, versioned by a single top-level `"schemaVersion"`
  integer field this ADR fixes must exist.
- **This transformation step exists specifically to avoid coupling the
  rest of Atlantis to Slang's own JSON shape.** **No official schema
  document, versioning field, or cross-release stability guarantee for
  `-reflection-json`'s output was found** — the CLI reference documents
  the flag's existence and purpose, but no accompanying schema contract.
  The observed payload also carries no version field of its own. Slang
  could therefore change its raw reflection JSON's field names or nesting
  across releases without breaking any documented promise. Atlantis's own
  schema, by contrast, is Shader-System-owned and versioned, and only
  this one transformation function needs to change if Slang's own JSON
  shape ever does.
- **Slang-JSON parsing policy, fixed here rather than left to Plan** —
  because Atlantis cannot rely on an unversioned external schema:
  - **The parser is bound to the Slang version shipped by the supported
    Vulkan SDK**, not to Slang generally. This is workable precisely
    because [ADR-0028](0028-shader-system-source-language-and-compiler.md)
    sources `slangc` from the SDK, which pins one Slang release per SDK
    version (SDK 1.4.357.0 ships Slang 2026.13.1, per the standard-module
    directory name observed alongside `slangc.exe`).
  - **Unknown/unrecognized fields are ignored**, not rejected — Slang
    adding a field must not break an Atlantis build.
  - **Missing *required* fields are a hard failure**
    (`Result::Err`), never defaulted or guessed. The one deliberate
    exception is the descriptor-set `"space"` key, whose absence is
    *specified* to mean set 0 (see "Reflection scope," above) — this is a
    known, evidence-backed encoding, not a silent default.
  - **An SDK/Slang version change must trigger re-verification** of the
    reflection fixtures and the parser, as a required step in whatever
    future change raises the supported SDK version. Atlantis does not
    control this schema and must not pretend otherwise.
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

### Descriptor reflection: validation only, never layout construction

**This section resolves a genuine contradiction in this ADR's previous
revision.** `PipelineCreateParams`
(`src/rhi/include/atlantis/rhi/types.h`) has **no descriptor set,
binding, or descriptor-type field at all**, and the Vulkan Backend's
`createPipeline()` hard-codes its single descriptor binding
(`src/vulkan_backend/src/vulkan_device.cpp` — binding 0,
`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, vertex stage only). Reflected
descriptor data therefore has **nowhere to go** in the unchanged RHI
contract. Claiming both "we reflect descriptor bindings" and "RHI is
unchanged" without saying what the reflected data is *for* was
incoherent. The resolution:

- **The one and only descriptor shape Phase 1 permits** is exactly what
  the Vulkan Backend already hard-codes: **descriptor set 0, binding 0, a
  uniform buffer, referenced by the vertex stage**. This expectation is
  *defined by the Vulkan Backend's existing, already-`Accepted`
  implementation* ([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)),
  not invented by Shader System.
- **Shader System encodes that same expectation as a fixed, constant
  expected-contract value**, and the RHI-adapter helper **compares** the
  reflected descriptor data against it.
- **On mismatch** — a different set, a different binding index, a
  different descriptor type, or more than one descriptor — the adapter
  returns a **recoverable `Result::Err` (a mapping/validation error, the
  same tier as every other adapter mismatch)**, which Atlantis Tools'
  CLI turns into a non-zero exit and therefore a **build-time failure
  with a readable diagnostic**. It never silently proceeds, and never
  attempts to reconfigure anything.
- **Reflected descriptor data is never used to *construct* a pipeline
  layout, a descriptor set layout, or any RHI parameter this round.** It
  is consumed exclusively as the input to the equality check above.
- **Why this is still worth doing, rather than skipping descriptor
  reflection entirely:** today, a shader author can change a
  `[[vk::binding(...)]]` in shader source and get a *silent* mismatch
  against the Backend's hard-coded binding — caught, if at all, only by
  Vulkan Validation Layers at draw time on a machine with a GPU, which is
  exactly the fragile "matched by convention and by the human author's
  own care" gap
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)
  accepted as temporary. This check moves that failure to build time, on
  every machine, with a diagnostic naming the offending binding. That is
  a real, concrete safety improvement obtained without touching RHI at
  all.
- **This is explicitly not a step toward a general descriptor system.**
  A future spec that wants reflection to genuinely *drive* pipeline-layout
  construction must extend RHI's public surface, which means reopening
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  and re-reviewing `PipelineCreateParams` — explicitly out of scope here,
  and explicitly not pre-authorized by this ADR.

### Entry-point naming policy

Four distinct names must not be conflated. This policy fixes each:

| Concept | Phase 1 value | Fixed by |
|---|---|---|
| Slang source function name | meaningful, e.g. `vertexMain`/`fragmentMain` | shader author |
| `slangc -entry` argument | the same source name | Atlantis Tools CLI invocation |
| SPIR-V `OpEntryPoint` name in the emitted module | **`"main"`** | Slang's *default* renaming behavior |
| `VkPipelineShaderStageCreateInfo::pName` | **`"main"`** | existing Vulkan Backend code, unchanged |

- **Atlantis Tools' CLI must never pass `-fvk-use-entrypoint-name`.**
  Slang's CLI reference documents that flag as "Uses the entrypoint name
  from the source instead of 'main' in the spirv output" — i.e. the
  *default*, with the flag absent, is to emit `"main"`.
- **Verified in both directions on the installed toolchain.** Compiling
  `vertexMain` without the flag produced
  `OpEntryPoint Vertex %vertexMain "main"`; compiling the same entry point
  *with* the flag produced `OpEntryPoint Vertex %vertexMain "vertexMain"`.
  The fragment stage behaved identically (`OpEntryPoint Fragment
  %fragmentMain "main"`). Documented behavior and observed behavior agree.
- **Consequence:** meaningful source names cost nothing, and the Vulkan
  Backend's existing hard-coded `pName = "main"` keeps working with **no
  RHI or Backend change whatsoever**.
- **The reflection JSON reports the *source* name** (`"vertexMain"`),
  not the emitted SPIR-V name. Any future consumer that wants the actual
  `OpEntryPoint` string must not read it from the JSON's
  `entryPoints[].name`.
- **A future spec wanting to preserve source entry-point names in SPIR-V
  must change the RHI/Vulkan Backend contract first** (since `pName` is
  currently a hard-coded literal). Enabling this flag is therefore
  **not** a decision a future Plan may make on its own.

### Vertex input layout: split authority, stated explicitly

`VertexInputLayout` (`src/rhi/include/atlantis/rhi/types.h`) has two
kinds of fields, and this ADR fixes exactly which party owns which —
**no field's authority is left ambiguous or implicitly "whoever gets
there first":**

| `VertexInputLayout` field | Authority | Why |
|---|---|---|
| `VertexAttribute::location` | **Shader source's explicit `[[vk::location(N)]]` attribute**, surfaced through reflection's `varyingInput` index and cross-checkable against the emitted `Location` decoration | The shader source is the sole authority on which location an input occupies. Note the reflection JSON exposes this as a `varyingInput` **index**, not as a field labelled `Location` — the two were observed to agree, but only on a module whose source had already pinned the values explicitly. Atlantis therefore treats the mandatory `[[vk::location(N)]]` authoring convention as the authority and the JSON index as a cross-check, never as an independently-derived location. |
| `VertexAttribute::format` | **Shader System reflection** (from the JSON's per-attribute `type` object) | The shader source's declared type is the sole authority on what data shape the shader expects at that location. Any reflected type not mapping onto `VertexAttributeFormat`'s currently-single `Float3` value is an explicit mapping error, never a silent fallback. |
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
- **A secondary integration target *inside the Atlantis Shader System
  module* performs the RHI-shape mapping** — not Shader System's own core
  library target, and **not a new top-level module.** This is a
  deliberate, explicitly-scoped positioning:
  - **It is not a tenth top-level module.**
    [AGENTS.md](../AGENTS.md)'s module list (Core, Platform, RHI, Vulkan
    Backend, RenderGraph, Renderer, Shader System, Runtime, Tools) is
    **unchanged by this ADR**, and neither
    [AGENTS.md](../AGENTS.md) nor
    [module_boundaries.md](../docs/architecture/module_boundaries.md) is
    edited by this spec. This target is an internal build-structure
    detail of the *Atlantis Shader System* module — the same way a module
    may have more than one CMake target without that making each target a
    subsystem.
  - **It establishes no independent long-term subsystem ownership.**
    Shader System owns it; it has no separate architectural charter, no
    separate roadmap position, and no separate entry in any module
    registry.
  - **Its responsibility is exactly one thing:** combine
    Shader-System-owned reflection metadata with consumer-supplied
    Mesh/vertex-schema data, validate the two against each other and
    against the fixed descriptor contract, and produce existing RHI
    value types. It owns **no GPU resource, no `Pipeline`, and no shader
    compiler process.**
  - **Dependency shape:** the core Shader System target depends only on
    `Atlantis::Core` and must remain fully usable with no knowledge that
    RHI exists; this secondary target may depend on both the core Shader
    System target and `Atlantis::RHI`. `Atlantis::RHI` still depends on
    neither. The concrete target/alias naming is a Plan-stage detail and
    is deliberately not fixed here, precisely so the name cannot be read
    as announcing a new top-level module.
  - **No Slang type appears in any public header** of Core, RHI,
    RenderGraph, or Renderer — nor, indeed, in Shader System's own public
    headers, since Slang is only ever a subprocess
    ([ADR-0029](0029-shader-system-build-time-compilation-boundary.md)).
  - Its function: given a loaded `ShaderSystem::ReflectionMetadata`
    (vertex stage) and a caller-supplied Mesh-schema stride/offset table
    (see "Vertex input layout," above), returns a populated
    `atlantis::rhi::VertexInputLayout`; given a loaded
    `ShaderSystem::ReflectionMetadata` (either stage), returns
    `pushConstantSizeBytes`; and validates the reflected descriptor data
    against the fixed expected descriptor contract (see "Descriptor
    reflection: validation only," above). All of these return
    `atlantis::Result<..., MappingError>` on a genuine mismatch, not a
    silently-accepted best-effort guess.
  - **Called by whichever future Renderer-level `Material`-construction
    call site builds `PipelineCreateParams`** — not designed by this
    spec (see Non-Goals in
    [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)).
    That call site links this secondary target, which transitively brings
    in both the Shader System core target and `Atlantis::RHI`.
- **`Atlantis::VulkanBackend` gains no dependency on Shader System,
  the RHI-mapping target, or reflection of any kind.** Its `createPipeline()`
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
  secondary integration target inside the Shader System module, with
  Shader System's own core target kept RHI-independent and the top-level
  module list unchanged.
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
  Shader System's RHI-mapping target rather than a hand-written literal —
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
  transitively depend on `Atlantis::RHI` — the narrow, secondary
  integration target avoids that coupling entirely.
- **Derive vertex stride/offset from reflection by assuming a fixed,
  conventional packing rule** (e.g. "attributes are always tightly packed
  in declaration order"). Rejected: this is exactly the kind of silent,
  unstated assumption the user's own review explicitly warned against —
  no official Slang (or any shader reflection tool's) material supports
  deriving a host-side interleaved-buffer stride from shader source
  alone, and asserting otherwise would misrepresent what reflection can
  actually guarantee.
