# Plan: Shader System Foundation

- **Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Human Review Approval (2026-08-15):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), at commit `30fecd7` on
  [PR #35](https://github.com/slmao/Atlantis/pull/35), following a joint
  review of `Approved` Spec 0008, `Accepted` ADR-0028–0031, and this Plan
  in full (including two rounds of AI-assisted read-only Plan
  Human-Review-readiness audits). All PHR-0008 decisions §13 records
  (PHR-0008-02, -03, -04, -05, -07, -14, -15) are approved as drafted,
  together with every other design choice in §1–§12. This approval:
  1. Changes this Plan's Status from `Draft` to **`Approved / Ready for
     Implementation`**.
  2. Authorizes creating an Implementation branch **once this approval
     record has merged into `main`** — strictly following this Plan's
     module boundaries, CMake target/dependency graph, public API surface,
     JSON parsing scope, `CreateProcessW` process model, artifact
     publish-transaction model, migration steps, and testing plan.
  3. Requires that any architectural deviation discovered during
     Implementation — in particular anything §12 lists as a stop condition
     — return to Spec/ADR review; it may not be resolved unilaterally.
     §12's separately-listed "not blockers" (exact names, exact CLI flags,
     other Plan-stage mechanical details) remain Implementation's to
     resolve directly.
  4. Does **not** authorize merging any pull request on the human's
     behalf.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #150](https://github.com/slmao/Atlantis/pull/150) Batch 3. Original
  scope, ordered work, and verification retained. Candidate C++ headers,
  class bodies, and CMake fragments drafted here (§2–§7) are summarised to
  their contracts and preserved in
  [PR #35](https://github.com/slmao/Atlantis/pull/35)/[PR #36](https://github.com/slmao/Atlantis/pull/36)
  history; the multi-round Plan-review narration is likewise not repeated
  here. Every C++ signature, JSON grammar detail, CLI flag list, CMake
  target name, and file path in §1–§9 was a **Plan-stage candidate** for
  Human Review at drafting time and is now the approved basis for
  implementation — Spec 0008 and ADR-0028–0031 fix *behavior*; this Plan
  proposed the concrete C++. §12 separately lists the handful of points
  needing an explicit human choice.

## Objective

Turn Spec 0008's approved contract — a build-time Slang → SPIR-V compile/
reflect pipeline, replacing
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
checked-in-bytecode bootstrap — into an ordered, reviewable implementation
plan: `Atlantis Shader System` as a real module, `Atlantis Tools`' first
real content (a `slangc`-driving CLI), and the migration of
`shaders/minimal_renderer/` off its checked-in GLSL/`.spv` pair onto this
new pipeline.

## Approval Baseline (what this Plan builds on, unchanged)

- **Spec 0008** — `Approved`, Human Review 2026-08-14.
- **ADR-0028–0031** — all `Accepted`, alongside Spec 0008's approval.
- **ADR-0024, ADR-0025, ADR-0027** — `Accepted`, unmodified by Spec 0008
  and not reopened by this Plan.
- **HR-0008-01 through HR-0008-13** — all approved as drafted.
- **SPIR-V compatibility baseline: Option A** — Vulkan physical-device
  selection floor stays `VK_API_VERSION_1_0`
  (`src/vulkan_backend/src/vulkan_device.cpp`, unchanged by this Plan);
  shader artifacts target SPIR-V 1.0 via `slangc -profile spirv_1_0`.
- **`E50011` warning: Policy S** — precisely suppressed via
  `-warnings-disable 50011`, reason recorded in
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md).
- **`spirv-val --target-env vulkan1.0` is mandatory** — every emitted
  shader artifact must pass it at build time; a missing `spirv-val` tool
  fails CMake **configure**, on the same footing as a missing `slangc`.
- Implementation begins only after this Plan's own Human Review, and only
  from a branch cut from `main` after this Plan's PR has merged.

## Authoritative Sources

Read in full before this Plan was drafted: [AGENTS.md](../../AGENTS.md);
[specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md);
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)–[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md);
[plans/0007-minimal-renderer.md](0007-minimal-renderer.md) (house style
and precedent — its `Buffer`/`Texture`/`Pipeline`/descriptor-binding
design is what this Plan's descriptor-contract validation checks against);
[docs/architecture/module_boundaries.md](../architecture/module_boundaries.md);
[docs/process/testing-strategy.md](../process/testing-strategy.md),
[definition-of-done.md](../process/definition-of-done.md),
[git-workflow.md](../process/git-workflow.md);
[.github/PULL_REQUEST_TEMPLATE.md](../../.github/PULL_REQUEST_TEMPLATE.md);
the root `CMakeLists.txt`, `cmake/CompilerWarnings.cmake`,
`cmake/AtlantisDependencies.cmake`;
`src/core/include/atlantis/{result,log,assert}.h`;
`src/rhi/include/atlantis/rhi/{types,device,pipeline,command_list}.h`;
`src/renderer/include/atlantis/renderer/{mesh,material,draw_item,renderer}.h`;
`src/vulkan_backend/src/vulkan_device.cpp` (the `createPipeline()` body,
including its hard-coded `pName = "main"` and fixed descriptor-binding
layout); `examples/minimal_renderer_demo/{main.cpp,CMakeLists.txt}`;
`tests/vulkan_backend/{CMakeLists.txt,minimal_renderer_gpu_tests.cpp}`;
`shaders/minimal_renderer/{minimal_mesh.vert.glsl,minimal_mesh.frag.glsl,README.md}`.

## Critical Architectural Boundaries (preserved, not re-decided here)

- **`Atlantis::RHI` never depends on `Atlantis::ShaderSystem`, in any
  form.** `Device::createPipeline(PipelineCreateParams)` is not touched —
  no new field, no new overload.
- **`Atlantis::ShaderSystem`'s core library never depends on RHI, never
  links Slang, and never touches an OS-process API.** Only a second,
  explicitly-named, Shader-System-*internal* target
  (`atlantis_shader_system_rhi_integration`, §6) depends on both
  `Atlantis::ShaderSystem` and `Atlantis::RHI`; only Atlantis Tools'
  `atlantis_shader_compiler` executable ever spawns `slangc`/`spirv-val`
  (ADR-0029, ADR-0030).
- **No new top-level module.** The RHI-integration target is declared
  inside `src/shader_system/`'s own `CMakeLists.txt` — no new
  `add_subdirectory()` entry sibling to `src/shader_system` in the root
  `CMakeLists.txt`, and `AGENTS.md`'s nine-module list is not touched.
- **No new third-party dependency.** `slangc`/`spirv-val` are external
  build tools from the already-required Vulkan SDK (ADR-0006, ADR-0028) —
  never linked, never `FetchContent`'d. The reflection-JSON parser this
  Plan introduces is a small, hand-rolled, internal parser (§3) — not a
  new JSON library dependency.
- **Descriptor reflection validates the existing fixed contract; it does
  not drive general pipeline-layout construction** (ADR-0030). The
  descriptor layout `vulkan_device.cpp`'s `createPipeline()` hard-codes
  today (one `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` binding, index 0, vertex
  stage only) is not changed by this Plan.
- **SPIR-V `OpEntryPoint` name is always `"main"`** — Atlantis Tools never
  passes `-fvk-use-entrypoint-name` — matching the Vulkan Backend's
  existing hard-coded `pName = "main"`, unchanged.
- **Vertex-buffer stride and per-attribute byte offset remain Mesh/
  vertex-schema-owned** (host-side C++), never derived from shader
  reflection (ADR-0030).
- **Single Phase 1 logical frame thread; nothing this Plan introduces runs
  on it.** Shader compilation is build-time-only (ADR-0004, ADR-0029);
  `atlantis_shader_compiler` is a short-lived, single-threaded,
  single-invocation build-time process.
- **`ATLANTIS_CHECK`/`ATLANTIS_ASSERT` for programmer errors,
  `Result<T,E>` for recoverable errors, no exceptions** in
  `Atlantis::ShaderSystem`'s and its RHI-integration target's public API
  (ADR-0029). Atlantis Tools' `main()` uses ordinary process/exit-code
  handling, not exceptions.
- **No `Vk*` type, no Slang type, and no OS-process type crosses into any
  public header of `Atlantis::ShaderSystem` or
  `Atlantis::ShaderSystemRhiIntegration`.**
- **The Vulkan Backend's minimum supported API version is not raised**
  (ADR-0028's Option A). `spirv-val --target-env vulkan1.0` is mandatory
  (ADR-0031).
- **No runtime shader compilation, no hot-reload.**

## Non-Goals (confirmed matching Spec 0008)

Runtime shader compilation; hot-reload; a shader cache service; a material
graph or node-based authoring; a permutation/variant framework; a
`VkPipelineCache` persistence architecture; an asset database; editor
integration; any Slang target other than Vulkan/SPIR-V; building Slang
from source or linking its compiler library; the Android implementation
itself; headless rendering; image regression testing;
bindless/GPU-driven/neural-shading; Runtime, ECS, or a scene system; a
general serialization/schema platform; a second Renderer or graphics
backend; modifying RHI's `Pipeline`/`CommandList`/`Device::createPipeline()`
contract; a general descriptor-set/pipeline-layout construction system.
This Plan does not add a third-party dependency, does not touch `AGENTS.md`
or `docs/architecture/module_boundaries.md`, and does not reopen any
`Accepted` ADR's conclusions.

---

## 1. Module and CMake Target Boundaries

**Two new modules** (`src/shader_system/`, `src/tools/shader_compiler/`),
plus two new test directories. No third-party dependency.

### Files to Create

- **`src/shader_system/include/atlantis/shader_system/`** public headers:
  `reflection_metadata.h` (`ShaderStage`, `DescriptorType`,
  `DescriptorBinding`, `PushConstantRange`, `VertexInputAttribute`,
  `VertexAttributeType`, `ReflectionMetadata`); `reflection_loader.h`
  (`loadReflectionMetadata()`, `saveReflectionMetadata()`,
  `ReflectionLoadError`, `ReflectionSaveError`); `slang_json_transform.h`
  (`transformSlangReflectionJson()`, `TransformError` — public because
  Tools links this target and calls it directly); `descriptor_contract.h`
  (`minimalRendererExpectedDescriptorContract()`,
  `validateDescriptorContract()`, `ContractMismatchError`);
  `command_line.h` (`SlangCompileRequest`, `buildSlangcArgv()`,
  `buildSpirvValArgv()`); `version_provenance.h` (`describeSdkProvenance()`
  — ADR-0031's Vulkan-SDK-version anchor).
- **`src/shader_system/src/`** private implementation: `json_value.h`
  (minimal DOM), `json_parser.h/.cpp` (`parseJson()` — strict, narrow
  grammar, §3), and the `.cpp` for each public header above.
- **`src/shader_system/CMakeLists.txt`** — declares BOTH
  `atlantis_shader_system` AND `atlantis_shader_system_rhi_integration`
  (§6, no separate `add_subdirectory()`); ALSO defines
  `atlantis_add_slang_shader_pair()` (§7, no separate `.cmake` file).
- **`src/shader_system/rhi_integration/`** —
  `include/.../rhi_integration/vertex_input_mapping.h`
  (`MeshVertexAttributeSchema`, `toVertexInputLayout()`,
  `toPushConstantSize()`, `MappingError`) and its `src/` `.cpp`.
- **`src/tools/shader_compiler/`** — `main.cpp` (CLI entry point);
  `process_launch.h/.cpp` (Windows-only `CreateProcessW` wrapper, §4);
  `compile_and_validate.h/.cpp` (orchestrates compile → reflect →
  transform → validate contract → `spirv-val` → publish, §5);
  `CMakeLists.txt`.
- **`tests/shader_system/`** — `json_parser_tests.cpp`,
  `reflection_metadata_tests.cpp`, `slang_json_transform_tests.cpp`
  (fixture-based), `descriptor_contract_tests.cpp`,
  `command_line_tests.cpp`, `version_provenance_tests.cpp`,
  `rhi_integration/vertex_input_mapping_tests.cpp` (links RHI),
  `CMakeLists.txt`. All GPU-independent.
- **`tests/tools/shader_compiler/`** — `process_launch_tests.cpp`
  (GPU-independent, argv/quoting logic), `toolchain_integration_tests.cpp`
  (`tool`-labeled, §9 — invokes the real `slangc`/`spirv-val`, no GPU
  device), `CMakeLists.txt`.
- **`shaders/minimal_renderer/minimal_mesh.slang`** — replaces the two
  `.glsl` files (§8).

### Files to Modify

- **`CMakeLists.txt` (root)** — add `find_program(...)` guards for
  `slangc`/`spirv-val` (§7, configure-time `FATAL_ERROR` if missing);
  `add_subdirectory()` for `src/shader_system`,
  `src/tools/shader_compiler`, and `shaders/minimal_renderer` (§8, which
  **must** come after `add_subdirectory(src/shader_system)` since
  `atlantis_add_slang_shader_pair()` is defined there); and, under
  `ATLANTIS_BUILD_TESTS`, `tests/shader_system` and
  `tests/tools/shader_compiler`.
- **`examples/minimal_renderer_demo/main.cpp`** —
  `loadSpirvFile("shaders/...")` retargeted to the build-tree artifact via
  a Shader-System-exported path (§8); `PipelineCreateParams::vertexInputLayout`
  built via `ShaderSystemRhiIntegration::toVertexInputLayout()`.
- **`examples/minimal_renderer_demo/CMakeLists.txt`** and
  **`tests/vulkan_backend/CMakeLists.txt`** — `POST_BUILD` copy source
  switches from `shaders/minimal_renderer/*.spv` to the build-tree shader
  output directory (§7/§8).
- **`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`** — same
  call-site change as the demo.
- **`shaders/minimal_renderer/README.md`** — rewritten: Slang source
  note, build-tree artifact location, retirement of the manual `glslc`
  regeneration instructions (§8).

### Files to Delete (as the final step of §8's migration, not before)

`shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl` and
`shaders/minimal_renderer/minimal_mesh.{vert,frag}.spv`.

### Files/Directories This Plan Does Not Touch

`AGENTS.md`; `docs/architecture/module_boundaries.md`;
`docs/project-blueprint.md`; `src/rhi/**`; `src/render_graph/**`;
`src/renderer/include/atlantis/renderer/**` (the `Mesh`/`Material`/
`Renderer` public API — only the demo's/test's own call sites that
*construct* `PipelineCreateParams` change); `src/vulkan_backend/**`;
`src/platform/**`; `.gitignore` beyond the one addition §7 lists; any
`plans/000{1-7}*.md`, `adr/00{01-27}*.md`. No file under `src/runtime/` or
any Android/iOS path. Any implementation-time discovery that a file
outside this list needs touching is a deviation to call out explicitly in
the implementation PR.

### Target/Dependency Graph

- **`atlantis_shader_system`** (STATIC lib, `Atlantis::ShaderSystem`) —
  `PUBLIC Atlantis::Core` only; no RHI, no Vulkan, no Slang, no OS-process
  API anywhere in its sources or public headers.
- **`atlantis_shader_system_rhi_integration`** (STATIC lib,
  `Atlantis::ShaderSystemRhiIntegration`) — `PUBLIC Atlantis::ShaderSystem`
  and `PUBLIC Atlantis::RHI` (both PUBLIC because its own public header
  returns `atlantis::rhi::VertexInputLayout` built from an
  `atlantis::shader_system::ReflectionMetadata` input). The **only**
  target in the codebase with a compile-time dependency on both.
- **`atlantis_shader_compiler`** (executable, no alias — matching the
  demo's executable-target-gets-no-alias precedent) — `PRIVATE
  Atlantis::ShaderSystem`, `PRIVATE Atlantis::Core`; links neither RHI nor
  the RHI-integration target. Owns its own `process_launch.{h,cpp}`
  (Windows-only, private to this target).
- **`Atlantis::RHI`** — UNCHANGED. Never gains a dependency on
  `Atlantis::ShaderSystem`, in any form.

Whichever of the demo / GPU test constructs `PipelineCreateParams` (never
`Renderer` itself, per ADR-0022) gains a `PRIVATE` link to
`Atlantis::ShaderSystemRhiIntegration`, in addition to its existing links.
`src/renderer/`'s own `CMakeLists.txt` is **not modified**.

---

## 2. Shader System Core — Public Types

Header `reflection_metadata.h` defines plain value types (all
`atlantis::shader_system`):

- **`VertexAttributeType`** — `Float3` only this round (mirrors
  `atlantis::rhi::VertexAttributeFormat`'s own narrowness). A reflected
  attribute whose Slang type does not map here is
  `TransformError::UnsupportedVertexAttributeType`, never silently
  coerced.
- **`ShaderStage`** — `Vertex`, `Fragment`.
- **`DescriptorType`** — `UniformBuffer` only (ADR-0030's narrow scope).
- **`DescriptorBinding`** — `{ set, binding, type, stage }`. `set`/`binding`
  are read from Slang's own `descriptorTableSlot` binding kind's
  `"space"`/`"index"` fields. This is a positive, `[JSON-verified]` rule:
  a `[[vk::binding(3, 2)]]` probe emitted `{"kind":
  "descriptorTableSlot", "space": 2, "index": 3}` (cross-checked against
  disassembled `DescriptorSet 2` / `Binding 3`); a `[[vk::binding(0, 0)]]`
  resource emitted no `"space"` key. `slang_json_transform.cpp` therefore
  parses **any** set value the JSON reports (0 or otherwise) into this
  field — it does **not** fail closed on a nonzero set. Rejecting a
  nonzero set is a separate, later step:
  `minimalRendererExpectedDescriptorContract()` accepts only set 0 /
  binding 0, so a nonzero-set shader parses into a valid
  `ReflectionMetadata` and then fails `validateDescriptorContract()` with
  a specific `ContractMismatchError` (§5). Parsing capability and contract
  acceptance are deliberately two independently-testable layers.
- **`PushConstantRange`** — `{ offsetBytes, sizeBytes, stage }`.
- **`VertexInputAttribute`** — `{ location, type }`. `location`/`type` are
  Shader-System-reflected (from the shader's explicit `[[vk::location(X)]]`
  and Slang type, ADR-0030). `offsetBytes`/`strideBytes` are deliberately
  **absent** — no shader reflection tool can derive a host-side
  interleaved vertex-buffer layout; those live on the caller-supplied
  `MeshVertexAttributeSchema` (§6), combined with this type only when
  `toVertexInputLayout()` runs.
- **`ReflectionMetadata`** — the single Atlantis-owned, versioned schema
  this module reads and writes, populated FROM Slang's raw
  `-reflection-json` output by `slang_json_transform.cpp`, never Slang's
  raw JSON re-exposed verbatim (ADR-0030). One instance describes exactly
  one compiled shader **stage** (one entry point) — a full material's
  worth is two separate `ReflectionMetadata` values. Fields:
  `kCurrentSchemaVersion` (static, `= 1`); `schemaVersion`;
  `entryPointName` (the **Slang source** function name, e.g.
  `"vertexMain"`, NOT the emitted SPIR-V `OpEntryPoint` name, which is
  always `"main"`); `stage`; `descriptorBindings` (only this entry point's
  own `bindings[].used == true` — module-level-but-unused parameters are
  filtered out, never carried in); `pushConstantRanges`;
  `vertexInputAttributes` (empty for a non-vertex stage);
  `varyingOutputLocations` (vertex stage only — for the supplementary
  cross-stage check, §5 step 12); `varyingInputLocations` (fragment stage
  only); `sdkProvenance` (opaque string, diagnostics/logging only, never
  parsed back). Plus an `operator==`.

Header `reflection_loader.h`: `loadReflectionMetadata(jsonPath)` →
`Result<ReflectionMetadata, ReflectionLoadError>` (`FileNotFound`,
`FileReadFailed`, `MalformedJson`, `UnsupportedSchemaVersion` [field
present but > `kCurrentSchemaVersion`], `MissingRequiredField`). Loads and
validates an **Atlantis-schema** file (not Slang's raw output). Called at
build time by `atlantis_shader_compiler` (to re-verify what it just wrote,
§5 step 14) and at program-startup by `ShaderSystemRhiIntegration` (§6).
Not thread-safe, caller-thread-only (ADR-0004); each call is a fresh,
uncached file read (ADR-0030's "Shader System does not cache, retain, or
watch the file"). `saveReflectionMetadata(metadata, jsonPath)` →
`Result<void, ReflectionSaveError>` (`FileWriteFailed`) — called only by
`atlantis_shader_compiler` at build time.

Header `slang_json_transform.h`:
`transformSlangReflectionJson(slangRawJsonPath, requestedEntryPointName,
stage, sdkProvenance)` → `Result<ReflectionMetadata, TransformError>`
(`FileNotFound`, `FileReadFailed`, `MalformedJson`, `UnexpectedStructure`
[parses as JSON but not the expected shape — a real "Slang JSON shape
changed" signal], `UnsupportedVertexAttributeType`, `EntryPointNotFound`).
Reads Slang's raw `-reflection-json` output (external, undocumented,
unversioned) and re-projects the ONE named entry point's data into
`ReflectionMetadata`. Called only by `atlantis_shader_compiler` at build
time, immediately after a `slangc` invocation succeeds.

Header `descriptor_contract.h`:
`minimalRendererExpectedDescriptorContract()` → `std::vector<DescriptorBinding>`
— the fixed, expected contract this round's Minimal Renderer shaders must
match, a single Atlantis-Tools-owned constant hand-kept in sync with
`vulkan_device.cpp`'s hard-coded `createPipeline()` binding layout (a
stated, accepted single-source-of-truth risk — §5, PHR-0008-07 — not a
solved problem). `validateDescriptorContract(metadata, expected)` →
`Result<void, ContractMismatchError>` (`BindingCountMismatch`,
`BindingNotFound`, `DescriptorTypeMismatch`, `StageMismatch`,
`UnexpectedExtraBinding`) — validation only (ADR-0030), never constructs
or returns anything RHI-shaped. Called by `atlantis_shader_compiler` at
build time (§5 step 7).

Header `command_line.h`: `SlangShaderStageArg` (`Vertex`, `Fragment`);
`SlangCompileRequest` (`{ sourcePath, entryPointName, stage,
spirvOutputPath, reflectionJsonOutputPath }` — pure data, describes ONE
`slangc` invocation; no process is spawned by this type).
`buildSlangcArgv(slangcExecutablePath, request)` → `std::vector<std::string>`
— `argv[0]` is the `slangc` path itself; fixes, as tested,
non-Plan-revisable facts: `-profile spirv_1_0` (Option A, mandatory — NOT
`-capability`, which does not select the output version), `-warnings-disable
50011` (Policy S, mandatory), **no** `-fvk-use-entrypoint-name` (never
passed), `-target spirv`, `-stage <vertex|fragment>`, `-entry
<entryPointName>`, `-o <spirvOutputPath>`, `-reflection-json
<reflectionJsonOutputPath>`. `buildSpirvValArgv(spirvValExecutablePath,
spirvPath)` → argv for `spirv-val --target-env vulkan1.0 <spirvPath>`
(ADR-0031, mandatory; no flag beyond `--target-env` by default).

Header `version_provenance.h`:
`describeSdkProvenance(slangcExecutablePath)` → `std::optional<std::string>`
— ADR-0031's provenance anchor: no confirmed `slangc --version` flag
exists, so provenance is read from a `slang-standard-module-<version>`
directory sibling to `slangc.exe` (observed `slang-standard-module-2026.13.1`
on the reference SDK). Returns `std::nullopt` if that directory is not
found — best-effort, not a hard build requirement; its absence does not
fail the build (contrast `slangc`/`spirv-val` themselves, which do).

**Ownership/thread-safety:** all types above are plain value types or free
functions; none owns a file/process/Slang/RHI handle across a call
boundary; none is thread-safe for concurrent access and none needs to be
(every call site is a single-threaded build-time process or a
single-threaded startup path).

## 3. JSON Parsing — Hand-Rolled, Narrow, No New Dependency

**Why a hand-rolled parser, not a library — not a free Plan-stage
choice.** Spec 0008's Non-functional Requirements state "zero new
third-party dependencies"; [AGENTS.md](../../AGENTS.md) requires any new
dependency (a JSON library included) to go through its own Spec/ADR
review, and this Plan has no authority to introduce one. A small,
internal, narrowly-scoped JSON parser private to `atlantis_shader_system`
is the only Plan-authorized option — not a general-purpose Core JSON
facility.

**Grammar scope — sufficient for both Slang's raw JSON and Atlantis's own
schema, nothing more:**

- Full JSON value model: object, array, string, number,
  `true`/`false`/`null` — not reduced, because Slang's raw reflection JSON
  uses all of these.
- **String escapes:** the standard JSON escape set (`\"`, `\\`, `\/`,
  `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX`), including `\uXXXX`
  surrogate-pair handling; a malformed/unexpected surrogate pair is a
  parse error, never undefined behavior or silent truncation.
- **Numbers:** parsed as `double` internally (every numeric field in this
  module's schema is a small integer); a number outside `double`'s
  exactly-representable-integer range is not specially guarded beyond
  ordinary `double` parsing.
- **Malformed input:** any structurally invalid JSON (unterminated
  string, unexpected token, trailing garbage, duplicate object keys —
  "last one wins", matching the JSON spec's permissive stance) is
  `Result::Err(...MalformedJson)`, never a partially-parsed value or a
  crash.
- **Unknown fields (both directions):** a JSON key
  `slang_json_transform.cpp` does not explicitly look for is silently
  ignored (Slang's raw JSON is not fully modeled); an unrecognized
  top-level key in Atlantis's own schema is likewise ignored (so a future
  schema version can add a field without breaking an older reader within
  the same `schemaVersion` — an incompatible change bumps
  `kCurrentSchemaVersion` instead).
- **Missing required fields:** `reflection_loader.cpp` treats
  `schemaVersion`, `entryPointName`, and `stage` as required
  (`ReflectionLoadError::MissingRequiredField`);
  `descriptorBindings`/`pushConstantRanges`/`vertexInputAttributes`/the
  two varying-location arrays default to empty when absent (a legitimate
  case). `slang_json_transform.cpp`'s reading of Slang's raw JSON is more
  defensive: any structural assumption that does not hold (e.g.
  `"entryPoints"` missing or not an array) is
  `TransformError::UnexpectedStructure`.

**Descriptor-set parsing rule** (Section 2's comment restates this, not a
second source of truth): if the reflected binding object has a `"space"`
key, `set` = its value parsed as an unsigned integer; else `set` = 0.
`binding` = the `"index"` value parsed the same way. A `[JSON-verified]`
rule (ADR-0030), directly observed against real `slangc` output on both
the absent-`"space"` and explicit-`"space"` cases and cross-checked
against disassembled SPIR-V. `slang_json_transform.cpp` parses a nonzero
`set` **successfully** — it does not fail closed. A malformed
`"space"`/`"index"` value (not an integer, negative, or outside
`std::uint32_t`) is a resource-limit parse failure, not a
nonzero-set-specific case.

**Contract acceptance is a separate, later, independently-testable
layer.** `minimalRendererExpectedDescriptorContract()` is fixed this round
to exactly `{set: 0, binding: 0}` — a shader reflecting `{set: 2, binding:
3}` parses into a valid `ReflectionMetadata`, then fails
`validateDescriptorContract()` with `BindingNotFound` and/or
`UnexpectedExtraBinding` (§5). This split lets a future material with a
real nonzero-set need extend only the expected-contract value (or a
material-specific one, §5's `--expected-contract=` mechanism) without
touching the parser.

**Fixture-based coverage** (per Spec 0008's Validation Evidence and
ADR-0030's `[JSON-verified]` findings): a fixture matching the real
sample JSON (set 0, no explicit `"space"`) parses to `set = 0`; a positive
fixture for a nonzero descriptor set reusing ADR-0030's recorded static
fixture `{"kind": "descriptorTableSlot", "space": 2, "index": 3}` parses
to `{set: 2, binding: 3}` (a parser-level pass), and a second, separate
test feeds that same `ReflectionMetadata` into
`validateDescriptorContract()` against the `{set: 0, binding: 0}`-only
contract and asserts the specific `ContractMismatchError` (a
contract-level expected-to-fail case) — both halves required; a
malformed-`"space"`/`"index"`-value negative fixture → a resource-limit
parse failure, kept distinct; a fixture for the
module-level-`"parameters"`-vs-entry-point-level-`"bindings"`-with-`"used"`
combination rule (a binding is included only when the entry point's own
`"bindings"` entry has `"used": true` or an equivalent truthy marker —
exact field-name confirmation deferred to Implementation against a fresh
real sample); a push-constant fixture explicitly cross-checked against
[issue #5676](https://github.com/shader-slang/slang/issues/5676)'s
disclosed caveat (§5/§9 require a real-SDK regression test asserting
reflected push-constant offset/size against the shader's declared
layout).

**Resource limits** — conservative, fixed constants private to
`json_parser.cpp`, not a Core-wide configuration surface; exceeding any is
a recoverable parse error (never a crash or silent truncation):

| Limit | Value | Rationale |
|---|---|---|
| Maximum input size | 16 MiB | Real reflection JSON is a few KB; rejects a pathological/corrupted input before parsing. |
| Maximum nesting depth (object/array) | 64 | Real Slang JSON nests a handful of levels; bounds the parser's recursion against stack overflow. |
| Maximum string length | 64 KiB | Reflection field values are short identifiers/paths. |
| Maximum array/object element count (per array/object) | 4096 | Real data has at most a handful of parameters/bindings/attributes. |

**Integer parsing:** every numeric field in this module's schema is
`std::uint32_t`; a JSON number that is negative, non-integer, or exceeds
`std::uint32_t`'s range is a parse failure for that field — never
silently truncated, rounded, or wrapped.

**Duplicate object keys — "last one wins"**, a deliberate, reviewed choice
matching the JSON spec's own permissive stance and mainstream parsers.
Implementation does not need to revisit this unless Human Review asks for
reject-on-duplicate.

**SDK/Slang-version-upgrade handling:** `slang_json_transform.cpp`'s
parser is not validated against, or assumed compatible with, any
Slang/Vulkan-SDK version other than the one this Plan's fixtures were
captured against (1.4.357.0 / Slang 2026.13.1). A future SDK upgrade
requires re-running the fixture capture and re-verifying
`slang_json_transform_tests.cpp` against the new sample **before** that
upgrade is adopted — recorded as a maintenance note in that test file's
header, not enforced by any runtime check (Slang exposes no schema-version
marker).

**Not generalized into a Core JSON facility** — `json_value.h`/
`json_parser.h` stay `src/shader_system/src/`-private, never installed
under `include/`.

## 4. Tools CLI — Process Execution

`process_launch.h` (private to `atlantis_shader_compiler`) declares
`launchProcess(executablePath, arguments)` →
`Result<ProcessOutput, ProcessLaunchError>` (Windows-only,
`CreateProcessW`; blocking; runs the child to completion; not
thread-safe, caller-thread-only). `ProcessOutput` = `{ std::string
diagnostics; std::int32_t exitCode }` — a single combined capture.
`ProcessLaunchError`: `ExecutableNotFound`, `DiagnosticFileCreationFailed`,
`LaunchFailed` (`CreateProcessW` returned FALSE; `GetLastError()` text
folded into the error's diagnostic), `WaitFailed`, `ExitCodeQueryFailed`,
`DiagnosticFileReadFailed`. This is a Tools-internal detail — never an
`Atlantis::Platform` API, never a `ShaderSystem` type.

Fully specified against the official `CreateProcessW` documentation:

- **Executable resolution — `lpApplicationName`, never `NULL`.**
  `executablePath` (already an absolute path from CMake's `find_program()`
  at configure time, §7 — `launchProcess()` performs no `PATH` search) is
  passed as `lpApplicationName`, not left `NULL` with the name embedded in
  `lpCommandLine`'s first token — avoiding the documented
  space-in-path-runs-a-different-executable hazard. Before calling
  `CreateProcessW`, `launchProcess()` checks
  `std::filesystem::exists(executablePath)` and returns
  `ExecutableNotFound` directly if it does not, keeping the "missing tool"
  case cheaply testable and scoping `LaunchFailed` to genuine OS-level
  failures.
- **Command-line construction — a mutable, owned buffer, never a string
  literal.** The docs are explicit that `CreateProcessW` can modify
  `lpCommandLine` and it must not be read-only memory. `launchProcess()`
  builds it into a `std::wstring` with its own storage and passes that
  string's non-`const` `data()`; the buffer's lifetime covers the entire
  `CreateProcessW` / `WaitForSingleObject` / `GetExitCodeProcess`
  sequence. Quoting follows the standard Windows/MSVC C-runtime
  argument-quoting convention (the one `CommandLineToArgvW()` consumes):
  wrap an argument in `"` when it contains whitespace/`"`/ambiguity;
  escape embedded `"` as `\"`; double a run of `N` backslashes
  immediately preceding a `"` to `2N`; leave other backslash runs
  untouched. Implemented **exactly once**, inside `process_launch.cpp`,
  never re-derived at any call site — `command_line.cpp` (§2) only
  produces a plain `std::vector<std::string>` argv with no quoting logic.
- **Diagnostic capture — a single temporary file, not pipes.** Two
  anonymous pipes with sequential reads is a well-known deadlock hazard
  (avoidable only with a second reader thread or overlapped I/O — a
  concurrency mechanism this Plan has no other reason to introduce).
  Instead, `launchProcess()` creates one per-invocation-unique temporary
  file (via `CreateFileW`, §7's per-invocation temp-path scheme reused)
  with `SECURITY_ATTRIBUTES` marking **only this one handle** inheritable;
  both `STARTUPINFOW::hStdOutput` and `hStdError` point at this same
  handle, so the child's stdout and stderr interleave into one file in
  chronological write order.
- **`STARTUPINFOW::dwFlags` must explicitly include
  `STARTF_USESTDHANDLES`** — without it, `hStdOutput`/`hStdError` are
  ignored and the child falls back to the parent's console buffer,
  silently defeating the capture design (only symptom: an empty
  `diagnostics` and a failing `process_launch_tests.cpp` happy-path).
  `si.dwFlags |= STARTF_USESTDHANDLES;` is set explicitly, and the same
  docs require the handles be inheritable with `bInheritHandles = TRUE`
  (already satisfied).
- **`hStdInput` — once `STARTF_USESTDHANDLES` is set, must be an explicit,
  valid, inheritable handle.** `launchProcess()` opens a second,
  separate inheritable `GENERIC_READ` handle to the Windows `NUL` device
  and assigns it to `hStdInput` — `slangc`/`spirv-val` never read stdin,
  so NUL (immediate EOF) is the documented-safe choice, never the
  parent's console input. Closed by the same RAII guard as the diagnostic
  file handle.
- `CreateProcessW` is called with `bInheritHandles = TRUE` and
  `dwCreationFlags = 0`. The parent closes its own copy of the diagnostic
  file handle immediately after `CreateProcessW` returns; after
  `WaitForSingleObject()` confirms exit, it reopens the temp file for
  reading, reads its full contents into `ProcessOutput::diagnostics` as
  raw bytes interpreted as UTF-8 (a non-UTF-8 sequence is tolerated as
  opaque bytes for logging — never parsed structurally). The temp file is
  deleted after being read, on every code path, by a small RAII guard
  inside `process_launch.cpp`.
- **`PROCESS_INFORMATION` handle lifetime.** Both `hProcess` and
  `hThread` are owned by a private RAII guard for the duration of the
  wait/exit-code-query sequence, guaranteeing `CloseHandle()` on every
  exit path.
- **Working directory and environment — both inherited, not overridden.**
  `lpCurrentDirectory` and `lpEnvironment` are both `NULL` — the child
  inherits the caller's current directory and full environment block
  (including `VULKAN_SDK`, `PATH`) unchanged. `atlantis_shader_compiler`
  never depends on its own working directory for anything (every path
  arrives as an explicit CLI argument, §7).
- **Exit code retrieval.** After `WaitForSingleObject(hProcess, INFINITE)`
  returns `WAIT_OBJECT_0`, `GetExitCodeProcess()` fills
  `ProcessOutput::exitCode`. A non-zero exit code is **not** a
  `ProcessLaunchError` — `launchProcess()` returns `Ok(ProcessOutput{...})`
  for any exit code; interpreting a non-zero exit as a compile/validate
  failure is `compile_and_validate.cpp`'s responsibility (§5), keeping
  `launchProcess()` a narrow "run this process and report what happened"
  primitive.
- **Timeout/cancellation — explicitly out of scope, honestly bounded.**
  `WaitForSingleObject()` uses `INFINITE` — no timeout. The single-file
  diagnostic design structurally eliminates the pipe-buffer-deadlock risk,
  but does not eliminate a `slangc`/`spirv-val` invocation that itself
  hangs, which would hang the whole build indefinitely with no automatic
  recovery this round — an accepted Phase 1 limitation (§9/§13), stated
  plainly.

Atomic, all-or-nothing artifact publication is specified in §7's
stamp-based transaction model, built on top of this process-execution
primitive.

## 5. Reflection and Contract-Validation Algorithm

Implemented in `src/tools/shader_compiler/compile_and_validate.h/.cpp`,
invoked once per `(source .slang file, entry point)` pair — once per
compiled shader **stage**, matching RHI's unchanged
two-separate-`ShaderStageBytecode`-blobs contract (ADR-0025):

1. Resolve `slangc`/`spirv-val` executable paths (passed as CLI args,
   §1's `--slangc-path`/`--spirv-val-path` — Tools does not re-run
   `find_program()`; CMake does that once at configure time, §7).
2. Build a `SlangCompileRequest` (§2); call `buildSlangcArgv()`.
3. `launchProcess(slangcPath, argv)` → `ProcessOutput`. Non-zero exit →
   `compile_and_validate.cpp` exits non-zero, surfacing `slangc`'s
   captured diagnostics verbatim; no artifact published (§7's stamp never
   created).
4. Slang emits BOTH the SPIR-V bytes and its raw reflection JSON to
   per-invocation-unique temp paths (`-o` and `-reflection-json` in the
   same `slangc` call).
5. `transformSlangReflectionJson(tempRawJsonPath, entryPointName, stage,
   sdkProvenance)` → `ReflectionMetadata`. `TransformError` → exit
   non-zero; no artifact published.
6. (Folded into step 5, per §3): entries in Slang's raw `"parameters"`
   not marked `used == true` for THIS entry point are filtered out before
   they reach the returned `descriptorBindings`/`pushConstantRanges`.
7. If this stage is Vertex or Fragment (both are, this round) AND this is
   Minimal Renderer's own material (identified by a
   `--expected-contract=minimal-renderer` CLI flag — a Plan-stage
   placeholder for "which fixed contract applies", since this round has
   exactly one): `validateDescriptorContract(metadata,
   minimalRendererExpectedDescriptorContract())`; a `ContractMismatchError`
   → exit non-zero, no artifact published. Step 5's transform already
   parsed a nonzero descriptor set correctly if the shader declares one —
   it is exactly THIS step, not the parser, that rejects it (a `{set: 2,
   binding: 3}` shader fails here with `BindingNotFound`/
   `UnexpectedExtraBinding`, not a transform-time error).
8. Push-constant validation: `metadata.pushConstantRanges` must contain
   exactly the range(s) this material's fixed expectation names (this
   round: one range, vertex stage, size == `sizeof(float) * 16`) — same
   "compare against a fixed, hand-specified expectation" pattern as step
   7. Mismatch → exit non-zero.
9. Vertex-stage-only: every `vertexInputAttributes[i].location` must be
   explicit and unique within this reflection — a narrow, redundant sanity
   check (Slang's compiler already rejects two `[[vk::location]]`
   collisions at step 3).
10. (Runtime-only step, NOT performed here): cross-validation against the
    caller's own Mesh-schema stride/offset table happens later, inside
    `ShaderSystemRhiIntegration`, wherever a demo/test constructs a
    `VertexInputLayout` (§6) — Tools has no visibility into that C++ code
    at build time.
11. (Same non-build-time note): step 10's C++ Mesh schema is unavailable
    here — omitted from this build-time algorithm entirely.
12. Supplementary cross-stage interface check — ONLY when BOTH stages of a
    material have been compiled in this same Tools invocation sequence
    (§1's CLI takes a `--pair-with=<path>` flag naming the other stage's
    already-written reflection JSON, a Plan-stage detail): vertex
    `varyingOutputLocations` must be a superset of fragment
    `varyingInputLocations`, by location index. Mismatch → exit non-zero.
    Supplements, does not substitute for, Slang's own primary compile-time
    guarantee (the shared varying-interface struct convention, §8), per
    ADR-0030.
13. `spirv-val --target-env vulkan1.0 <temp .spv path>` (mandatory,
    ADR-0031). Non-zero exit → exit non-zero; no artifact published; its
    captured diagnostics surfaced verbatim.
14. Only once every step above has succeeded, for BOTH stages: run §7's
    publish transaction (write final `ReflectionMetadata` via
    `saveReflectionMetadata()` to a temp path; publish all four final
    artifact files; write the stamp last, only after all four are safely
    in place). Any failure during publication is handled by §7's own
    cleanup rules.
15. Exit 0 only after the stamp has actually been written.

**Genuinely build-time vs. genuinely runtime** (Spec 0008's Risk item on
this exact question): steps 1–15 are **entirely build-time** — a
descriptor-contract or push-constant mismatch fails the **build**, not a
later program run. What is **not**, and structurally cannot be,
build-time: the final combination of reflected `location`/`type` data with
a caller's own C++ Mesh-schema `stride`/`offset` table (§6) — Tools has no
access to that C++ code. This is the direct, unavoidable consequence of
ADR-0030's "vertex stride is host-C++-owned, never shader-owned"
decision, restated honestly.

## 6. RHI Integration Target

`rhi_integration/vertex_input_mapping.h` (`atlantis::shader_system::rhi_integration`):

- **`MeshVertexAttributeSchema`** — caller-supplied, Mesh/vertex-schema-
  owned `{ location, offsetBytes }` — exactly the two fields
  `VertexInputAttribute` omits. One entry per attribute, matched to
  `ReflectionMetadata::vertexInputAttributes` by `location` (not array
  position).
- **`MappingError`** — `AttributeCountMismatch` (reflected count != schema
  entry count), `LocationNotFoundInSchema` (a reflected location has no
  matching schema entry), `UnsupportedVertexAttributeType` (mirrors
  `TransformError`'s case, re-checked defensively since this metadata may
  have been loaded from disk independently of the compile that produced
  it).
- **`toVertexInputLayout(vertexMetadata, schema, strideBytes)`** →
  `Result<atlantis::rhi::VertexInputLayout, MappingError>` — combines
  reflected `{location, type}` with caller-supplied `{location,
  offsetBytes}`, cross-validates (every reflected location must have a
  matching schema entry, and vice versa; ADR-0030's "cross-validate,
  never silently accept a mismatch"). `strideBytes` is a direct,
  un-cross-validated pass-through — there is no reflected value to
  cross-validate it against.
- **`toPushConstantSize(metadata)`** → `std::size_t` — sums
  `metadata.pushConstantRanges` (this round: expected exactly one range)
  into the single `PipelineCreateParams::pushConstantSizeBytes`. Returns 0
  (not an error) if there are no push-constant ranges.

**Attachment format (`PipelineCreateParams::colorFormat`/`depthFormat`) is
not this target's responsibility** — per Spec 0008's authority table
(`Presentation::metadata().format`/the Vulkan Backend's fixed depth
format, per ADR-0025, unchanged); the demo/test call sites (§8) continue
sourcing those two fields as they do today.

**Runtime error handling:** a `MappingError` here (called once, at
`Material` construction, never per-frame) is a program-startup-time
failure, consistent with how `createMaterial()` failure is already
handled in `examples/minimal_renderer_demo/main.cpp` (log and exit, §8
preserves this). It is **not** a "build failed" outcome — by the time this
code runs the build already succeeded (§5's step 14 already validated the
descriptor contract and push-constant layout at build time); a
`MappingError` here can only mean the demo's/test's own C++
`MeshVertexAttributeSchema` table is wrong (e.g. a location typo), which
no build-time check can catch.

## 7. Artifact Location and CMake Pipeline

**Output location** (ADR-0031): `${CMAKE_BINARY_DIR}/shaders/<relative-
path-mirroring-source>/`, single, configuration-independent — for Minimal
Renderer, `${CMAKE_BINARY_DIR}/shaders/minimal_renderer/minimal_mesh.vert.spv`
and `.../minimal_mesh.vert.refl.json` (and the `.frag.` equivalents).

**Configure-time tool discovery** (root `CMakeLists.txt`, after the
existing `find_package(Vulkan REQUIRED)`): `find_program()` for
`ATLANTIS_SLANGC_EXECUTABLE` (NAMES `slangc`, HINTS `$ENV{VULKAN_SDK}/Bin`)
and `ATLANTIS_SPIRV_VAL_EXECUTABLE` (NAMES `spirv-val`, same HINTS). Each,
if not found, is a `message(FATAL_ERROR ...)` naming the missing component
and that it ships with the Vulkan SDK (`slangc` confirmed present since
SDK 1.3.296.0; `spirv-val` a MANDATORY build-time step per ADR-0031, not
optional). Both resolved paths are passed to `atlantis_shader_compiler`
invocations as `--slangc-path=...`/`--spirv-val-path=...` CLI arguments,
not baked into the tool's source — keeping the tool testable without a
real Vulkan SDK for its own pure-logic unit tests (§9).

**Per-shader-pair custom-command chain — stamp-based, all-or-nothing
transaction.** A CMake function `atlantis_add_slang_shader_pair(NAME ...
SOURCE ... VERTEX_ENTRY ... FRAGMENT_ENTRY ... OUTPUT_DIR ...
EXPECTED_CONTRACT ...)`, **defined directly inside
`src/shader_system/CMakeLists.txt`** — no separate `.cmake` file, no
per-module `cmake/` subdirectory (a pattern with no precedent here), and
not placed in the root `cmake/` directory either (it has exactly one
consumer this round). CMake functions defined in a processed
`CMakeLists.txt` remain callable by any directory processed later in the
same configure run, so `shaders/minimal_renderer/CMakeLists.txt` (§8) can
call it **provided the root `CMakeLists.txt`'s
`add_subdirectory(src/shader_system)` precedes its
`add_subdirectory(shaders/minimal_renderer)`** — this ordering is a §11
Verification Checklist item.

The function's `add_custom_command()` declares:

- **`OUTPUT` = a single stamp file `${OUTPUT_DIR}/${NAME}.stamp`, not the
  four real artifacts — the load-bearing design decision.** CMake's
  dependency graph only ever asks "does `${NAME}.stamp` exist and is it
  newer than `DEPENDS`". `${NAME}_shaders` (the `add_custom_target(...
  ALL DEPENDS "${stamp}")` every consumer depends on transitively) depends
  only on the stamp. The four real files (`${NAME}.vert.spv`,
  `${NAME}.vert.refl.json`, `${NAME}.frag.spv`, `${NAME}.frag.refl.json`)
  are declared as `BYPRODUCTS` — per the official CMake docs, "files the
  command is expected to produce but whose modification time may or may
  not be newer than the dependencies" — so Ninja/Makefile generators know
  they exist (for dependency bookkeeping and `clean`) without treating
  their individual timestamps as the staleness signal.
- **`DEPENDS ${SOURCE}`** — editing `minimal_mesh.slang` makes the stamp
  stale, triggering exactly this one re-compilation next build.
- **`DEPENDS atlantis_shader_compiler`** (the target) — CMake rebuilds
  the Tools executable first if its sources changed.
- The single `atlantis_shader_compiler` invocation compiles **both**
  stages internally (§5's step 12 cross-stage check needs both results in
  one process invocation). The function also sets
  `ATLANTIS_${NAME}_SHADER_OUTPUT_DIR` via `PARENT_SCOPE`.

**Publish transaction** (implemented inside `compile_and_validate.cpp`,
referenced by §5's step 14):

- **14a.** Delete any pre-existing stamp at `${stamp}` first, defensively.
- **14b.** Compile/reflect/validate both stages ENTIRELY into a
  per-invocation-unique temporary directory
  `"${OUTPUT_DIR}/.tmp-${NAME}-<pid>-<counter or high-res clock>/"` —
  unique per invocation so parallel builds of DIFFERENT shader pairs
  never collide (a single NAME's own custom command is never invoked twice
  concurrently — CMake serializes a given `OUTPUT`-producing command
  against itself). Nothing under `OUTPUT_DIR`'s final paths is touched
  during this step.
- **14c.** Only once ALL of: both `slangc` compiles (step 3), both
  transforms (step 5), the descriptor-contract check (step 7), the
  push-constant check (step 8), the cross-stage check (step 12), and both
  `spirv-val` runs (step 13) have succeeded for BOTH stages — proceed;
  otherwise skip to 14f and exit non-zero.
- **14d.** Publish: for each of the four final artifact paths,
  `std::filesystem::rename()` the temp-directory file to its final path
  (same-volume rename, atomic per-file). If any single rename fails
  partway, stop immediately, proceed to 14f, exit non-zero — the stamp is
  never written.
- **14e.** Only after all four renames succeeded: write the stamp itself
  via the same temp-then-rename pattern (content is a plain text record of
  `sdkProvenance`, never parsed back by any Atlantis code — the stamp's
  mere *existence* is what CMake's dependency tracking checks).
- **14f.** Cleanup (every exit path): remove the entire per-invocation
  temp directory. On a failure path reached from 14d (a partial publish),
  additionally best-effort-remove any of the four final-path files this
  invocation renamed before failing — "best effort" because if this
  removal itself fails, the process still exits non-zero and still has
  not written a stamp, so the guarantee below still holds.

**Why a consumer can never observe a half-updated pair:** CMake's own
build-order guarantee means no consumer runs concurrently with, or before,
a still-in-progress or failed publish — by the time any consumer runs the
stamp already exists, which by 14e's ordering is only possible if all four
real files were successfully published first. **Why the next build always
retries a failure:** a failed run never writes the stamp, so CMake always
considers this `OUTPUT` missing/stale next build and re-runs the full
command; a partially-published file left by a best-effort cleanup failure
does not change this, since the stamp's absence (not the four files'
state) governs the rerun, and a rerun's own 14d overwrites stale partials.

**Multi-config generator support boundary — a fixed Phase 1 policy, not
left to Implementation-time discovery.** This Plan's design already avoids
the documented anti-pattern (the stamp `OUTPUT` is declared by exactly one
`add_custom_command()`, consumed by exactly one `add_custom_target()` —
never duplicated across independent targets). The fixed supported
workflow:

- **Exactly one, configuration-independent shader-artifact producer
  exists per binary tree**; every configuration's consumer targets depend
  on that same, single producer.
- **Ordinary parallel compilation *within* one configuration** (e.g.
  MSBuild `/m` parallelizing `.cpp` files of the same configuration) is
  unaffected and remains safe.
- **Building different configurations of the *same* binary tree must be
  sequential, not concurrent, within Phase 1** — e.g. `cmake --build .
  --config Debug` completing before `--config Release` begins. Safe under
  CMake's own ordinary staleness tracking: the second configuration's
  build checks the same configuration-independent stamp `OUTPUT`, finds it
  up to date, and skips re-running the command — the same "second build is
  a no-op" behavior every other `add_custom_command()` relies on.
- **Two independent build processes concurrently building different
  configurations of the *same* binary tree is explicitly an unsupported
  Phase 1 workflow.** A contributor wanting concurrent Debug/Release
  builds must use two separate binary trees (`-B build-debug` / `-B
  build-release`).
- **§9/§10/§11 each gain an explicit sequential-build regression test** —
  building Debug then Release (and, separately, Release then Debug) in the
  same binary tree, confirming the second configuration's build does not
  redundantly recompile the shader pair and does not corrupt or
  regenerate its artifacts.
- **If Implementation observes a conflicting-rule symptom under the
  *supported* (sequential-only) workflow, this is a Human Review Blocker**
  (§12/§13): Implementation must stop and return to Plan/ADR review rather
  than silently switch to per-configuration authoritative artifacts
  (conflicting with `Accepted` ADR-0031) or invent a cross-process
  locking mechanism. This Plan does not claim CMake provides cross-process
  serialization no official documentation confirms.

**Consumer artifact discovery — no hardcoded absolute path.** Every
consuming CMake target (the demo, the GPU test) references the shader
output directory via `ATLANTIS_${NAME}_SHADER_OUTPUT_DIR` (set by
`atlantis_add_slang_shader_pair()` via `PARENT_SCOPE`) — never a
hand-typed literal (ADR-0031's rule) — and adds an explicit
`add_dependencies(<consumer target> ${NAME}_shaders)` so CMake's
build-order guarantee applies to it. The demo's/test's existing
`POST_BUILD` `copy_if_different` step is retargeted from
`shaders/minimal_renderer/*.spv` (source tree, checked-in) to this
build-tree variable (generated, not checked in) — the copy mechanism
(copy next to the consumer's executable) is unchanged, only its source
path.

**`.gitignore`:** one line added, `/build*/shaders/` (or the exact
build-tree shader output pattern matched to this repository's existing
`.gitignore` conventions — a Plan-stage mechanical detail).

**Debug/Release share one artifact set** (`OUTPUT_DIR` has no `$<CONFIG>`
component) because shader compilation reads no `CMAKE_BUILD_TYPE`/`$<CONFIG>`
value and produces no configuration-dependent bytes (ADR-0029/0031).

## 8. Minimal Renderer Migration

One atomic sequence of implementation steps (§9's implementation order
sequences it across several reviewable steps), executed only after §1–§7's
new modules are implemented and independently tested:

1. **Add `shaders/minimal_renderer/minimal_mesh.slang`** — a single Slang
   module with both `vertexMain`/`fragmentMain` entry points sharing one
   explicitly-declared varying-interface `struct` (ADR-0030's authoring
   convention), functionally equivalent to today's
   `minimal_mesh.{vert,frag}.glsl` pair (camera uniform at
   `[[vk::binding(0,0)]]`, push-constant object-to-world matrix,
   position/color vertex inputs at explicit `[[vk::location(0/1)]]`, unlit
   per-vertex-color output) — content informed by, but not required to
   byte-match, Spec 0008's Validation Evidence experiment shader.
2. **Add `shaders/minimal_renderer/CMakeLists.txt`**, calling
   `atlantis_add_slang_shader_pair()` (§7) with
   `EXPECTED_CONTRACT=minimal-renderer`; wired from the root
   `CMakeLists.txt` (a new `add_subdirectory(shaders/minimal_renderer)`
   entry — Plan-stage mechanical detail).
3. **Update `examples/minimal_renderer_demo/main.cpp`**: `loadSpirvFile()`
   calls now target the build-tree-copied `.spv` files (path unchanged in
   *shape* — the `POST_BUILD` copy still lands them at
   `shaders/minimal_mesh.{vert,frag}.spv` relative to the executable);
   `PipelineCreateParams::vertexInputLayout` is now built via
   `ShaderSystemRhiIntegration::toVertexInputLayout()` (loading each
   stage's `ReflectionMetadata` via `loadReflectionMetadata()` against the
   copied `.refl.json` files) instead of the existing hand-written
   `minimalMeshVertexLayout()` literal — which is **deleted**, not kept as
   a parallel/fallback path.
4. **Update `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`**: the
   identical call-site change as step 3.
5. **Update both `CMakeLists.txt` files' `POST_BUILD` copy commands** to
   copy from the new build-tree source directory instead of
   `shaders/minimal_renderer/*.spv`.
6. **Delete** `shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl` and
   `.spv`, and rewrite `shaders/minimal_renderer/README.md` to describe
   the new Slang source + build-tree-artifact model, retiring the manual
   `glslc` regeneration instructions (ADR-0027's migration boundary, now
   exercised).
7. **No two parallel, simultaneously-authoritative shader-sourcing
   mechanisms exist after this step** — confirmed by inspection (no
   remaining reference to a checked-in `.spv` anywhere in
   `examples/`/`tests/`), part of §11's Verification Checklist.

**What this migration does not do** (Spec 0008 Non-Goals, restated):
introduce a second material, a texture, lighting, or any change to the
mesh geometry/camera behavior — the visual output is expected pixel-for-
pixel unchanged from Spec 0007's verified output, because the shader's
logic is unchanged, only its sourcing mechanism.

## 9. Testing Plan

### GPU-independent (`ctest -LE gpu` and no `tool` label)

- **`json_parser_tests.cpp`:** valid-JSON round-trip
  (object/array/string/number/bool/null nesting); string escapes including
  `\uXXXX`; malformed input (unterminated string, trailing garbage,
  duplicate keys); numbers at typical small-integer values; the four
  resource limits (§3) each individually exercised (an input exceeding
  each → the matching parse error, not a crash or hang).
- **`reflection_metadata_tests.cpp`:** `loadReflectionMetadata()`/
  `saveReflectionMetadata()` round-trip on a fixture; missing required
  field → `MissingRequiredField`; unknown extra field → ignored, not an
  error; `schemaVersion` newer than `kCurrentSchemaVersion` →
  `UnsupportedSchemaVersion`.
- **`slang_json_transform_tests.cpp`:** the real captured Spec 0008
  Validation Evidence sample transforms to the expected
  `ReflectionMetadata` exactly; the module-level-`"parameters"`-vs-entry-
  point-`"bindings"`-`"used"`-filtering fixture; the set-0-implicit
  fixture (absent `"space"` → `set = 0`); **the nonzero-set positive
  fixture** — the exact `{"kind": "descriptorTableSlot", "space": 2,
  "index": 3}` shape ADR-0030 recorded — transforms *successfully* to
  `{set: 2, binding: 3}` (a parser-level pass, not a failure case); a
  malformed-`"space"`/`"index"`-value negative fixture → a resource-limit
  parse failure, kept distinct; a push-constant fixture explicitly
  cross-checked for offset/size correctness (the issue-#5676 regression
  case); malformed/unexpected-structure fixtures → the matching
  `TransformError` variant, never a crash or partial result.
- **`descriptor_contract_tests.cpp`:** exact-match fixture (`{set: 0,
  binding: 0}`) → `Ok`; **the nonzero-set fixture from
  `slang_json_transform_tests.cpp`, fed into `validateDescriptorContract()`
  against `minimalRendererExpectedDescriptorContract()`, asserts the
  specific `ContractMismatchError`** (`BindingNotFound`/
  `UnexpectedExtraBinding`) — the second half of the parser-vs-contract
  split; every other `ContractMismatchError` variant individually
  exercised (wrong count, wrong type, wrong stage).
- **`command_line_tests.cpp`:** `buildSlangcArgv()`'s output contains
  `-profile spirv_1_0`, `-warnings-disable 50011`, and **never**
  `-fvk-use-entrypoint-name`, for every fixture; `buildSpirvValArgv()`'s
  output contains `--target-env vulkan1.0`; a source/output path
  containing a space produces a correctly-quotable argv entry (not a
  pre-quoted string).
- **`version_provenance_tests.cpp`:** a fixture directory with a
  `slang-standard-module-<version>` sibling → that version string
  returned; its absence → `std::nullopt`, not an error.
- **`rhi_integration/vertex_input_mapping_tests.cpp`** (links
  `Atlantis::RHI`, no Vulkan device): matching-location fixture → correct
  `VertexInputLayout`; a schema entry with no matching reflected location
  → `LocationNotFoundInSchema`; a reflected location with no matching
  schema entry → `AttributeCountMismatch`/`LocationNotFoundInSchema`;
  `toPushConstantSize()` sums correctly, including the zero-ranges case.
- **`process_launch_tests.cpp`:** the argv-vector-to-Windows-command-line-
  string quoting algorithm (§4) exercised as a pure function against every
  documented edge case (embedded space, embedded double quote, trailing
  backslash before the closing quote, empty-string argument, a path under
  a directory whose name contains a space) — asserting the exact expected
  quoted output, not merely "does not crash". A genuinely-nonexistent
  executable path → `ExecutableNotFound` (a `std::filesystem::exists()`
  check before `CreateProcessW`). A real, trivial always-present Windows
  executable (e.g. `cmd.exe /c exit 0` / `exit 3`) exercises
  `launchProcess()`'s full happy path end-to-end — successful launch,
  correct `exitCode` for both zero and nonzero, and `diagnostics`
  capturing text written to both stdout and stderr (confirming the
  single-file combined-capture design). Stays GPU-independent, no `tool`
  label — `cmd.exe` is a standard Windows component, not an SDK tool.

### Tool integration (`ctest -L tool` — a new label; needs the real Vulkan-SDK `slangc`/`spirv-val`, but no GPU/Vulkan device)

`tests/tools/shader_compiler/toolchain_integration_tests.cpp`:

- A real `slangc` compile of a small fixture `.slang` file with `-profile
  spirv_1_0` succeeds, and the resulting `.spv`'s `Version: 1.0` header is
  present (via `spirv-dis` or byte-inspection — a Plan-stage choice).
- Missing `-profile` (simulating an accidental regression to `slangc`'s
  default) demonstrably emits a **different** SPIR-V version — the
  concrete regression test for ADR-0028's "default is 1.5" finding.
- A real `spirv-val --target-env vulkan1.0` run against that compiled
  artifact exits 0.
- `-warnings-disable 50011` demonstrably suppresses `E50011`'s stderr on
  a fixture that would otherwise emit it, while producing a byte-identical
  `.spv` to the unsuppressed run (a standing regression check for Spec
  0008's local observation).
- `-fvk-use-entrypoint-name` is never present in any argv this test
  inspects across every code path reaching `buildSlangcArgv()`.
- A deliberately-invalid `.slang` fixture fails the real `slangc`
  invocation with a non-zero exit code, and `compile_and_validate.cpp`
  propagates that failure — **no stamp file is created, and none of the
  four final artifact paths exist afterward** (§7's step 14c
  short-circuits before any temp-directory content is renamed).
- **Partial-publish recovery**: a fixture that fails specifically during
  §7's step 14d (simulated by an injectable failure point — a Plan-stage
  test-harness detail) leaves **no** stamp file, and a subsequent ordinary
  re-run (failure removed) succeeds and produces a complete, correct
  four-file set plus a stamp — the concrete regression test for §7's "next
  build always retries a failure".
- `atlantis_shader_compiler`, run twice on identical input/flags, produces
  byte-identical `.spv` and reflection-JSON output — the standing
  regression test for Spec 0008's local (not vendor-guaranteed)
  determinism observation.
- Incremental rebuild: touching the `.slang` source and re-running `cmake
  --build` recompiles exactly the affected shader pair; a no-op second
  build recompiles nothing (the stamp untouched — verified by
  build-log/timestamp inspection).
- **Sequential Debug→Release and Release→Debug** (§7's multi-config
  support boundary): building Debug to completion, then Release in the
  *same* binary tree (and the reverse order) — the second configuration's
  build does not re-invoke `atlantis_shader_compiler` for an unchanged
  shader pair, and both configurations' consumers successfully locate and
  load the one shared artifact set. Run manually/via script during
  Implementation (§10's gate for this step); a standing CTest case is
  preferred if practical.
- Missing `slangc`/`spirv-val` (simulated by pointing
  `--slangc-path`/`--spirv-val-path` at a nonexistent file) fails
  `atlantis_shader_compiler` itself cleanly — the Tools executable's own
  defensive handling, distinct from the CMake-configure-time
  `find_program()`/`FATAL_ERROR` check (§7), which no test can exercise
  directly.

### GPU-required (`ctest -L gpu`, unchanged label, extended coverage)

- `minimal_renderer_gpu_tests.cpp` (post-migration): the Slang-compiled,
  build-tree `minimal_mesh.{vert,frag}.spv`/`.refl.json` successfully back
  a real `Device::createPipeline()` call; Vulkan Validation Layers report
  zero warnings/errors; a multi-draw-item frame (unchanged from Spec
  0007's case) still produces correct per-item transforms. **This test is
  also this Plan's designated regression test for the
  `vulkan_device.cpp`-hard-coded-layout-vs-`minimalRendererExpectedDescriptorContract()`
  duplication risk (§2's disclosed, accepted single-source-of-truth
  gap):** because it exercises a real `VkPipeline` built from the Vulkan
  Backend's actual, unchanged hard-coded binding layout using a shader
  Shader System has already build-time-validated against its own separate,
  hand-kept-in-sync copy of that same layout, any future drift surfaces
  here as a real Vulkan Validation Layers error/warning or a
  pipeline-creation failure — not silently. This does not eliminate the
  duplication (§13's PHR-0008-07); it is the concrete existing test that
  would catch it.
- **Manual verification** (`examples/minimal_renderer_demo`,
  post-migration): a visible, correctly-shaded, correctly depth-ordered
  mesh matching Spec 0007's verified output, confirming the migration
  changed *how* the shader artifact is sourced without changing *what* is
  rendered; resize/minimize/restore/close behavior unchanged; Debug and
  Release builds both exercised.

### Explicitly not automated (stated, not silently assumed covered)

- **Two independent, concurrently-running build processes building
  different configurations of the same binary tree** — an explicitly
  *unsupported* Phase 1 workflow (§7), not merely untested; no reliable
  portable way to force it deterministically. Contributors use separate
  binary trees.
- A real Slang/Vulkan-SDK version genuinely different from the one this
  Plan's fixtures were captured against (§3's SDK-upgrade re-verification
  requirement) — by construction cannot be exercised until such an upgrade
  happens.

---

## 10. Implementation Order

Each step is independently buildable/testable and gated on the previous
step's own tests passing, per [AGENTS.md](../../AGENTS.md)'s "build and run
tests after every implementation step" rule:

1. **CMake tool discovery + module skeletons.** `find_program()` guards
   (§7); empty `src/shader_system/` and `src/tools/shader_compiler/`
   targets (headers/stubs only) that link and build clean under `/W4 /WX`.
   **Gate:** clean configure/build; a deliberately-broken `VULKAN_SDK`
   reproduces the §7 `FATAL_ERROR` messages.
2. **JSON parser + `ReflectionMetadata`/loader.** §3's grammar; §2's
   types; `reflection_loader.cpp`. **Gate:** `json_parser_tests.cpp` and
   `reflection_metadata_tests.cpp` green.
3. **`slang_json_transform.cpp` against captured fixtures** (static JSON
   files checked into `tests/shader_system/fixtures/`, a new directory —
   no real `slangc` invocation yet). **Gate:**
   `slang_json_transform_tests.cpp` green, including the issue-#5676
   push-constant regression fixture.
4. **`descriptor_contract.cpp` + `command_line.cpp` +
   `version_provenance.cpp`.** **Gate:** their own unit tests green.
5. **`process_launch.cpp` (Tools)**, implementing §4's full
   `CreateProcessW` design. **Gate:** `process_launch_tests.cpp` green,
   including the `cmd.exe`-based end-to-end happy-path — the first step
   exercising a real `CreateProcessW` call, still needing no Vulkan SDK.
6. **`compile_and_validate.cpp` (Tools) wired to a real
   `slangc`/`spirv-val`**, implementing §7's full publish-transaction
   algorithm (14a–14f). First step that needs the real Vulkan SDK. **Gate:**
   `toolchain_integration_tests.cpp` green against a small throwaway
   fixture `.slang` file — including partial-publish-recovery and the
   determinism regression check.
7. **`atlantis_add_slang_shader_pair()` + end-to-end build-tree
   pipeline**, still against the throwaway fixture. **Gate:** a clean
   build produces the stamp plus the expected `.spv`/`.refl.json` pair (as
   `BYPRODUCTS`) at the expected path; incremental-rebuild and
   no-op-rebuild behavior verified; **the sequential Debug→Release and
   Release→Debug regression test (§9) is run and recorded here** — a
   required gate per §7's fixed supported-workflow boundary. A
   conflicting-rule symptom under this sequential single-process test
   means Implementation stops and returns to Plan/ADR review (§12/§13),
   not a silent switch to per-configuration artifacts.
8. **`ShaderSystemRhiIntegration` (`vertex_input_mapping.cpp`).** **Gate:**
   `vertex_input_mapping_tests.cpp` green.
9. **Minimal Renderer migration (§8), in its own seven sub-steps.**
   **Gate:** `minimal_renderer_gpu_tests.cpp` green on real hardware,
   Validation Layers clean; manual demo verification performed and
   recorded (visual output, resize, minimize/restore, close, Debug and
   Release); no remaining reference to the checked-in `.spv`/`.glsl` pair.
10. **Full-suite verification and PR write-up.** Every GPU-independent,
    `tool`-labeled, and `gpu`-labeled test green on Debug and Release;
    §11's Verification Checklist walked item by item in the implementation
    PR's description; any deviation from this Plan called out explicitly.

---

## 11. Verification Checklist

- [ ] `Atlantis::RHI`'s public headers are byte-for-byte unchanged by this
      Plan's implementation.
- [ ] No `Vk*` type, no Slang type (`Slang::ComPtr`, `slang::*`), and no
      Windows process-handle type (`HANDLE`, `PROCESS_INFORMATION`)
      appears in any public header of `Atlantis::ShaderSystem` or
      `Atlantis::ShaderSystemRhiIntegration`.
- [ ] `atlantis_shader_system`'s `CMakeLists.txt` links only
      `Atlantis::Core`.
- [ ] `atlantis_shader_system_rhi_integration` is the *only* target in the
      repository depending on both `Atlantis::ShaderSystem` and
      `Atlantis::RHI`.
- [ ] No `add_subdirectory()` for a Shader-System-RHI-integration-named
      directory appears in the root `CMakeLists.txt` — it is declared
      inside `src/shader_system/CMakeLists.txt` only.
- [ ] `AGENTS.md` and `docs/architecture/module_boundaries.md` are
      byte-for-byte unchanged.
- [ ] No `FetchContent_Declare()`/new `find_package()` for any JSON
      library appears anywhere in the diff.
- [ ] `buildSlangcArgv()`'s output for every test fixture contains
      `-profile spirv_1_0`, contains `-warnings-disable 50011`, and never
      contains `-fvk-use-entrypoint-name`.
- [ ] `spirv-val --target-env vulkan1.0` is invoked, and its exit code
      checked, for every emitted `.spv`.
- [ ] A missing `slangc` or `spirv-val` fails CMake **configure**
      (`FATAL_ERROR`), not build or runtime.
- [ ] `vulkan_device.cpp`'s `pName = "main"` and its hard-coded
      descriptor-binding layout are byte-for-byte unchanged.
- [ ] `descriptorContract` validation runs at **build** time (inside
      `atlantis_shader_compiler`), not deferred to program startup or
      per-frame — `validateDescriptorContract()` is called nowhere in
      `src/shader_system/rhi_integration/` or any demo/test per-frame code
      path.
- [ ] No file under `shaders/minimal_renderer/` is a checked-in `.spv` or
      `.glsl` file after §8's migration completes.
- [ ] `examples/minimal_renderer_demo` and
      `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` load shader
      artifacts exclusively from the build-tree location §7 defines — no
      remaining reference to `shaders/minimal_renderer/*.spv` as a source
      path.
- [ ] Every `VkResult`-adjacent failure this Plan's new code surfaces (via
      `launchProcess()`'s captured exit codes, `slangc`/`spirv-val`'s
      stderr) is checked and propagated — no discarded exit code.
- [ ] Debug and Release builds both succeed, sharing one shader artifact
      set with no redundant recompilation, **built sequentially in the
      same binary tree** — the concurrent-cross-config case remains
      explicitly unsupported and is not verified.
- [ ] A shader pair's `OUTPUT` is a single stamp file; `.spv`/reflection-
      JSON are declared `BYPRODUCTS`.
- [ ] A failed compile/reflect/validate run leaves no stamp file and no
      partially-published final artifact set behind — verified by the
      partial-publish-recovery test (§9).
- [ ] `slang_json_transform.cpp` parses a nonzero descriptor `"space"`
      value successfully (never fails closed on it), and
      `validateDescriptorContract()` separately rejects it against Minimal
      Renderer's `{set: 0, binding: 0}`-only expected contract — both
      halves verified by `slang_json_transform_tests.cpp`/
      `descriptor_contract_tests.cpp`.
- [ ] `launchProcess()` passes the resolved executable path via
      `lpApplicationName` (never `NULL`), builds `lpCommandLine` into an
      owned, mutable `std::wstring` buffer (never a string literal or a
      `const`-sourced pointer), and captures combined stdout+stderr via a
      single temporary file (never two pipes).
- [ ] `STARTUPINFOW::dwFlags` includes `STARTF_USESTDHANDLES` before every
      `CreateProcessW` call in `process_launch.cpp`, and `hStdInput` is
      set to an explicit, inheritable NUL-device handle (never left unset)
      — verified by `process_launch_tests.cpp`'s `cmd.exe`-based
      happy-path observing non-empty, correctly-captured `diagnostics`.
- [ ] `json_parser.cpp` enforces its four documented resource limits
      (input size, nesting depth, string length, element count).
- [ ] `ctest -LE gpu` (excluding both `gpu` and `tool` labels) passes
      with no Vulkan SDK or GPU present beyond what the existing
      GPU-independent suite already needs.
- [ ] `ctest -L tool` passes on a machine with the Vulkan SDK installed
      but no GPU device.
- [ ] `ctest -L gpu` passes on real hardware, Vulkan Validation Layers
      clean throughout.
- [ ] `git diff --check` clean; no `.claude/` or unrelated file in the
      implementation diff.
- [ ] Every item in Spec 0008's own Testing & Verification Plan and
      Acceptance-shaped Requirements is traceable to a specific test or
      manual-verification step above (cross-checked against
      [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)
      directly during the implementation PR's write-up).

## 12. Human Review Blockers and Deviation Rules

**Stop and return to Human Review (do not resolve unilaterally during
Implementation) if any of the following is discovered:**

- Linking Slang's compiler library becomes necessary for any reason —
  reopens ADR-0029's CLI-vs-library decision.
- `-reflection-json`'s real field shapes, on further real-world exercise
  beyond Spec 0008's single captured sample, meaningfully disagree with
  §2/§3's assumed structure in a way that cannot be resolved by extending
  `slang_json_transform.cpp`'s narrow, additive field-handling alone (i.e.
  it would require *removing* or *reinterpreting* an already-relied-upon
  field, not just adding a new one).
- Any change to `Device::createPipeline()`, `PipelineCreateParams`, or any
  other RHI public header is found necessary — reopens
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
- Any change to `vulkan_device.cpp`'s fixed descriptor-binding layout is
  found necessary (e.g. a real material needs a second binding) — out of
  this Plan's scope entirely.
- Preserving the source Slang entry-point name (rather than the default
  rename-to-`"main"`) is found necessary — requires a new Spec/ADR
  changing RHI's/Vulkan Backend's `pName` contract, never a Plan- or
  Implementation-level flag flip to `-fvk-use-entrypoint-name`.
- Raising the Vulkan Backend's physical-device selection floor above
  `VK_API_VERSION_1_0` is found necessary — reopens
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md) and
  ADR-0028's Option A decision; Option B remains available as a future,
  separately-reviewed choice, never adopted silently here.
- `spirv-val` is found unable to meaningfully validate Slang's SPIR-V 1.0
  output for this Plan's shaders (e.g. a false pass on a genuinely broken
  artifact, or it cannot be made to target `vulkan1.0` correctly) —
  undermines the risk-mitigation basis for Option A.
- `slangc` or `spirv-val` are found to not be reliably provided by the
  supported Vulkan SDK on some real development/CI machine — reopens
  ADR-0028's/ADR-0031's dependency-acquisition model.
- Any new top-level module, or any dependency edge not already drawn in
  §1's target graph (in particular, any `Atlantis::RHI` →
  `Atlantis::ShaderSystem` edge, or any `Atlantis::ShaderSystem` →
  `Atlantis::RHI` edge on the *core* library), is found necessary.
- The Windows-only `process_launch.cpp` implementation is found to
  meaningfully block or complicate a concrete, near-term Android-build
  host-tooling need (not merely a hypothetical future one).
- The **sequential** Debug→Release/Release→Debug regression test (§9,
  gated at §10 step 7) demonstrates a genuine conflicting-rule symptom
  under §7's *supported* (single-process, sequential-build) workflow — as
  distinct from the concurrent-cross-config case, which is already
  explicitly out of Phase 1's supported scope and requires no escalation.
  A sequential-build failure would call into question whether ADR-0031's
  "configuration-independent artifact" decision remains viable at all;
  Implementation must stop rather than silently switch to
  per-configuration artifacts or invent a new concurrency primitive.

**Not blockers — Implementation may resolve these directly, calling the
deviation out in the implementation PR:** exact file names, exact
type/function names, exact JSON field-name spellings (once confirmed
against a real, freshly-captured Slang sample), exact CMake
variable/property names, the exact `--expected-contract=` CLI-flag
mechanism (§5) if a cleaner alternative emerges, and any other detail this
Plan's own candidate-status disclaimer flags as non-binding.

---

## 13. Plan-Stage Design Decisions for Human Review

Each is a genuine Plan-stage design choice — none changes an `Approved`
Spec's or `Accepted` ADR's conclusions. Numbered to match this Plan's
prior review history; gaps (PHR-0008-01, -06, -08 through -13) correspond
to decisions confirmed in that prior round and not reopened here.

- **PHR-0008-02 — Nonzero descriptor-set parsing and rejection.**
  `slang_json_transform.cpp` parses ANY reflected descriptor `"space"`
  value (0 or otherwise) successfully into `ReflectionMetadata` (a
  `[JSON-verified]` capability, ADR-0030, not a guess). Separately,
  `validateDescriptorContract()` rejects any set/binding pair outside
  Minimal Renderer's fixed `{set: 0, binding: 0}` expectation. Parser
  capability and contract acceptance are two independent,
  independently-tested layers (§2, §3, §9).
- **PHR-0008-03 — `CreateProcessW` model.** Resolved executable path via
  `lpApplicationName` (never `NULL`); a mutable, owned `std::wstring`
  `lpCommandLine` buffer; a single temporary file (not two pipes) for
  combined stdout+stderr capture, with narrowly-scoped handle inheritance
  and full `PROCESS_INFORMATION`/file-handle RAII (§4).
- **PHR-0008-04 — Stamp-based artifact-pair transaction.** A single,
  configuration-independent stamp file is the sole CMake `OUTPUT` driving
  rebuild/staleness decisions; the four real artifacts are `BYPRODUCTS`,
  published via temp-directory-then-rename only after every validation
  step succeeds, with the stamp written strictly last and only after all
  four real files are already safely in place (§7).
- **PHR-0008-05 — Multi-config support boundary.** Exactly one,
  configuration-independent producer per binary tree; sequential (not
  concurrent) Debug/Release builds within one binary tree are supported
  and must be verified (§9/§10); two independent, concurrently-running
  build processes targeting different configurations of the *same* binary
  tree are an explicitly unsupported Phase 1 workflow (§7).
- **PHR-0008-07 — Descriptor-contract duplication risk and its regression
  backstop.** `vulkan_device.cpp`'s hard-coded binding layout and
  `minimalRendererExpectedDescriptorContract()` remain two, hand-kept-in-
  sync copies — this Plan does not eliminate that duplication (doing so
  would require an RHI API change outside this Plan's scope).
  `minimal_renderer_gpu_tests.cpp` (§9) is the designated regression
  backstop: a real `VkPipeline` built from the Vulkan Backend's actual
  layout, using a Shader-System-validated shader, would surface drift as a
  real Vulkan Validation Layers error or pipeline-creation failure — not
  silently. A general single-source-of-truth descriptor system is
  explicitly future work requiring its own Spec/ADR.
- **PHR-0008-14 — JSON parser resource limits.** Fixed, conservative,
  non-configurable constants (16 MiB input, 64-level nesting, 64 KiB
  strings, 4096 array/object elements — §3) bound the hand-rolled parser's
  worst-case behavior against malformed/adversarial input, without
  becoming a Core-wide configuration surface.
- **PHR-0008-15 — CMake helper placement.**
  `atlantis_add_slang_shader_pair()` is defined directly inside
  `src/shader_system/CMakeLists.txt` — no new per-module `cmake/`
  subdirectory, and not the repository's root `cmake/` directory either,
  since it has exactly one consumer this round (§7). Human Review may
  prefer the root-`cmake/` placement if a second consumer is anticipated
  sooner — either choice is mechanical.

---

## Sequencing & Dependencies

This Plan depends on nothing beyond what Spec 0008/ADR-0028–0031 already
established as `Accepted`. §1–§4 (module skeletons, JSON parser,
process-launch) can be implemented and tested with zero dependency on a
real Vulkan SDK. §5–§7 (the real `slangc`/`spirv-val` integration and
CMake pipeline) require the Vulkan SDK on the implementation machine. §8
(Minimal Renderer migration) must follow §1–§7 completely — explicitly
sequenced last, per Spec 0008's Non-Goals.

## Rollback Plan

Each numbered step in §10 is its own reviewable unit; a problem discovered
after step N merges is reverted by reverting that step's commit(s) —
steps 1–8 do not modify any existing, previously-shipped file (Minimal
Renderer's migration, step 9, is the only step touching already-shipped
files, and is the last step). If step 9 is reverted after landing,
`shaders/minimal_renderer/`'s GLSL/`.spv` pair and the demo's/test's prior
call sites are restored from version control — the new Shader System
module itself (§1–§8) is not required to be reverted just because its
first real consumer was rolled back.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan: the "Image regression tests" item is N/A
(headless rendering does not exist yet); the "Headless integration tests"
item is likewise N/A. Every other item applies as written, including the
`tool`-labeled test category this Plan introduces as a genuine,
first-class CTest label alongside the existing `gpu` label.
