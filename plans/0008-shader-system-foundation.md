# Plan: Shader System Foundation

- **Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Approved`)
- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; pending Human Review.

## Objective

Turn Spec 0008's approved contract — a build-time Slang → SPIR-V compile/
reflect pipeline, replacing [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
checked-in-bytecode bootstrap — into an ordered, reviewable implementation
plan: `Atlantis Shader System` as a real module, `Atlantis Tools`' first
real content (a `slangc`-driving CLI), and the migration of
`shaders/minimal_renderer/` off its checked-in GLSL/`.spv` pair onto this
new pipeline. This Plan's C++ signatures, algorithms, CMake structure,
and file layout (§1–§9) are **candidates** — see "Candidate-API Status"
below — subject to their own Human Review before Implementation begins.

## Approval Baseline (what this Plan builds on, unchanged)

- **Spec 0008** — `Approved`, Human Review recorded 2026-08-14.
- **ADR-0028–0031** — all `Accepted`, alongside Spec 0008's approval.
- **ADR-0024, ADR-0025, ADR-0027** — `Accepted`, unmodified by Spec 0008
  and not reopened by this Plan.
- **HR-0008-01 through HR-0008-13** (the Human Review decision memo
  enumerated alongside Spec 0008's approval) — all approved as drafted.
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
- **This Plan is `Draft` and grants no implementation authorization by
  itself.** Per [AGENTS.md](../AGENTS.md), Implementation begins only
  after this Plan's own Human Review, and only from a branch cut from
  `main` after this Plan's PR has merged — never from this Plan's own
  branch (`plan/0008-shader-system-foundation`).

## Authoritative Sources

Read in full before this Plan was drafted: [AGENTS.md](../AGENTS.md);
[specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md);
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)–[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md);
[plans/0007-minimal-renderer.md](0007-minimal-renderer.md) (house style
and precedent — its `Buffer`/`Texture`/`Pipeline`/descriptor-binding
design is what this Plan's descriptor-contract validation checks
against, unchanged); [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md);
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md),
[definition-of-done.md](../docs/process/definition-of-done.md),
[git-workflow.md](../docs/process/git-workflow.md);
[.github/PULL_REQUEST_TEMPLATE.md](../.github/PULL_REQUEST_TEMPLATE.md);
the root `CMakeLists.txt`, `cmake/CompilerWarnings.cmake`,
`cmake/AtlantisDependencies.cmake`; `src/core/include/atlantis/{result,log,assert}.h`;
`src/rhi/include/atlantis/rhi/{types,device,pipeline,command_list}.h`;
`src/renderer/include/atlantis/renderer/{mesh,material,draw_item,renderer}.h`;
`src/vulkan_backend/src/vulkan_device.cpp` (the `createPipeline()` body,
including its hard-coded `pName = "main"` and fixed descriptor-binding
layout); `examples/minimal_renderer_demo/{main.cpp,CMakeLists.txt}`;
`tests/vulkan_backend/{CMakeLists.txt,minimal_renderer_gpu_tests.cpp}`;
`shaders/minimal_renderer/{minimal_mesh.vert.glsl,minimal_mesh.frag.glsl,README.md}`.

## Critical Architectural Boundaries (preserved, not re-decided here)

- **`Atlantis::RHI` never depends on `Atlantis::ShaderSystem`, in any
  form.** `Device::createPipeline(PipelineCreateParams)` is not touched
  by this Plan — no new field, no new overload.
- **`Atlantis::ShaderSystem`'s core library never depends on RHI, never
  links Slang, and never touches an OS-process API.** Only a second,
  explicitly-named, Shader-System-*internal* target
  (`atlantis_shader_system_rhi_integration`, §6) depends on both
  `Atlantis::ShaderSystem` and `Atlantis::RHI`; only Atlantis Tools'
  `atlantis_shader_compiler` executable ever spawns `slangc`/`spirv-val`
  (ADR-0029, ADR-0030).
- **No new top-level module.** The RHI-integration target is declared
  inside `src/shader_system/`'s own `CMakeLists.txt` — no new
  `add_subdirectory()` entry sibling to `src/shader_system` is added to
  the root `CMakeLists.txt` for it, and `AGENTS.md`'s nine-module list is
  not touched.
- **No new third-party dependency.** `slangc`/`spirv-val` are external
  build tools sourced from the already-required Vulkan SDK
  ([ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)) —
  never linked, never `FetchContent`'d. The reflection-JSON parser this
  Plan introduces is a small, hand-rolled, internal parser (§3) — not a
  new JSON library dependency (see §3's own rationale for why this is the
  only choice this Plan is authorized to make).
- **Descriptor reflection validates the existing fixed contract; it does
  not drive general pipeline-layout construction** (ADR-0030). The
  descriptor layout `vulkan_device.cpp`'s `createPipeline()` hard-codes
  today (one `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` binding, index 0, vertex
  stage only) is **not changed by this Plan** — Shader System's validator
  checks a compiled shader *against* that existing, unchanged contract.
- **SPIR-V `OpEntryPoint` name is always `"main"`** — Atlantis Tools never
  passes `-fvk-use-entrypoint-name` — matching the Vulkan Backend's
  existing hard-coded `VkPipelineShaderStageCreateInfo::pName = "main"`
  (`vulkan_device.cpp`), unchanged by this Plan.
- **Vertex-buffer stride and per-attribute byte offset remain Mesh/
  vertex-schema-owned** (host-side C++, e.g. `offsetof(Vertex, position)`
  in a demo/test), never derived from shader reflection (ADR-0030).
- **Single Phase 1 logical frame thread; nothing this Plan introduces
  runs on it.** Shader compilation is build-time-only
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md), ADR-0029);
  `atlantis_shader_compiler` is a short-lived, single-threaded,
  single-invocation build-time process.
- **`ATLANTIS_CHECK`/`ATLANTIS_ASSERT` for programmer errors,
  `Result<T,E>` for recoverable errors, no exceptions** in
  `Atlantis::ShaderSystem`'s and its RHI-integration target's public API
  (ADR-0029). Atlantis Tools' `main()` uses ordinary process/exit-code
  handling, not exceptions, per the same ADR.
- **No `Vk*` type, no Slang type, and no OS-process type crosses into any
  public header of `Atlantis::ShaderSystem` or
  `Atlantis::ShaderSystemRhiIntegration`.**
- **The Vulkan Backend's minimum supported API version is not raised**
  (ADR-0028's Option A). `spirv-val --target-env vulkan1.0` is mandatory
  (ADR-0031).
- **No runtime shader compilation, no hot-reload.**

## Non-Goals (confirmed matching Spec 0008)

Runtime shader compilation; hot-reload; a shader cache service; a
material graph or node-based authoring; a permutation/variant framework;
a `VkPipelineCache` persistence architecture; an asset database; editor
integration; any Slang target other than Vulkan/SPIR-V (D3D12, D3D11,
CUDA, Metal, WebGPU, CPU); building Slang from source or linking its
compiler library; the Android implementation itself; headless rendering;
image regression testing; bindless/GPU-driven/neural-shading; Runtime,
ECS, or a scene system; a general serialization/schema platform; a
second Renderer or graphics backend; modifying RHI's `Pipeline`/
`CommandList`/`Device::createPipeline()` contract; a general
descriptor-set/pipeline-layout construction system. This Plan does not
add a third-party dependency, does not touch `AGENTS.md` or
`docs/architecture/module_boundaries.md`, and does not reopen any
`Accepted` ADR's conclusions.

## Candidate-API Status

Every C++ signature, JSON grammar detail, CLI flag list, CMake target
name, and file path in §1–§9 is a **Plan-stage candidate**, per
[AGENTS.md](../AGENTS.md) and this repository's own precedent (Plan
0005's, 0006's, and 0007's identical disclaimer). Naming this explicitly
while this Plan is `Draft` is what makes it reviewable as a plan, not a
fait accompli. §10 ("Human Review Blockers") separately lists the
handful of points this Plan's own author judged as needing an explicit
human choice rather than a candidate default — those are flagged, not
silently decided, and are not part of this "ordinary candidate detail"
disclaimer.

---

## 1. Module and CMake Target Boundaries

**Two new modules** (`src/shader_system/`, `src/tools/shader_compiler/`),
plus two new test directories. No third-party dependency; no new root
CMake option beyond what's listed below.

### Files to Create

```
src/shader_system/include/atlantis/shader_system/reflection_metadata.h
                                            # ShaderStage, DescriptorType,
                                            # DescriptorBinding, PushConstantRange,
                                            # VertexInputAttribute, ReflectionMetadata
src/shader_system/include/atlantis/shader_system/reflection_loader.h
                                            # loadReflectionMetadata(), saveReflectionMetadata(),
                                            # ReflectionLoadError, ReflectionSaveError
src/shader_system/include/atlantis/shader_system/slang_json_transform.h
                                            # transformSlangReflectionJson(), TransformError
                                            # (public because Tools links this target and
                                            # calls it directly -- see AGENTS.md's own
                                            # "public/private" rule: everything a distinct
                                            # CMake target consumes is public, not smuggled
                                            # in via a private include path)
src/shader_system/include/atlantis/shader_system/descriptor_contract.h
                                            # DescriptorContract, validateDescriptorContract(),
                                            # ContractMismatchError
src/shader_system/include/atlantis/shader_system/command_line.h
                                            # SlangCompileRequest, buildSlangcArgv(),
                                            # buildSpirvValArgv()
src/shader_system/include/atlantis/shader_system/version_provenance.h
                                            # SdkProvenance, describeSdkProvenance()
                                            # (ADR-0031's Vulkan-SDK-version anchor)

src/shader_system/src/json_value.h          # JsonValue (private, minimal DOM)
src/shader_system/src/json_parser.h/.cpp    # parseJson() (private, strict, narrow grammar -- see §3)
src/shader_system/src/reflection_metadata.cpp
src/shader_system/src/reflection_loader.cpp
src/shader_system/src/slang_json_transform.cpp
src/shader_system/src/descriptor_contract.cpp
src/shader_system/src/command_line.cpp
src/shader_system/src/version_provenance.cpp
src/shader_system/CMakeLists.txt            # declares BOTH atlantis_shader_system AND
                                            # atlantis_shader_system_rhi_integration (§6) --
                                            # no separate add_subdirectory() for the latter;
                                            # ALSO defines atlantis_add_slang_shader_pair()
                                            # (§7) -- no separate .cmake file for it

src/shader_system/rhi_integration/include/atlantis/shader_system/rhi_integration/vertex_input_mapping.h
                                            # MeshVertexAttributeSchema, toVertexInputLayout(),
                                            # toPushConstantSize(), MappingError
src/shader_system/rhi_integration/src/vertex_input_mapping.cpp

src/tools/shader_compiler/main.cpp          # CLI entry point
src/tools/shader_compiler/process_launch.h/.cpp
                                            # Windows-only CreateProcessW wrapper (§4)
src/tools/shader_compiler/compile_and_validate.h/.cpp
                                            # orchestrates: compile -> reflect -> transform ->
                                            # validate contract -> spirv-val -> publish (§5)
src/tools/shader_compiler/CMakeLists.txt

tests/shader_system/json_parser_tests.cpp              # GPU-independent
tests/shader_system/reflection_metadata_tests.cpp       # GPU-independent
tests/shader_system/slang_json_transform_tests.cpp      # GPU-independent (fixture-based)
tests/shader_system/descriptor_contract_tests.cpp       # GPU-independent
tests/shader_system/command_line_tests.cpp               # GPU-independent
tests/shader_system/version_provenance_tests.cpp         # GPU-independent
tests/shader_system/rhi_integration/vertex_input_mapping_tests.cpp  # GPU-independent, links RHI
tests/shader_system/CMakeLists.txt

tests/tools/shader_compiler/process_launch_tests.cpp     # GPU-independent (argv/quoting logic only)
tests/tools/shader_compiler/toolchain_integration_tests.cpp
                                            # "tool"-labeled (§9): invokes the REAL slangc/
                                            # spirv-val from the installed Vulkan SDK; no GPU
                                            # device needed, but not GPU-independent either --
                                            # a third CTest label, see §9
tests/tools/shader_compiler/CMakeLists.txt

shaders/minimal_renderer/minimal_mesh.slang # NEW -- replaces the two .glsl files (§8)
```

### Files to Modify

```
CMakeLists.txt (root)                       # + find_program(...) guards for slangc/spirv-val
                                            #   (§7) -- configure-time FATAL_ERROR if missing
                                            # + add_subdirectory(src/shader_system)
                                            # + add_subdirectory(src/tools/shader_compiler)
                                            # + add_subdirectory(shaders/minimal_renderer)
                                            #   (§8) -- MUST come after
                                            #   add_subdirectory(src/shader_system) in this
                                            #   file's own ordering, since
                                            #   atlantis_add_slang_shader_pair() is defined
                                            #   there (§7) and this call site invokes it
                                            # + add_subdirectory(tests/shader_system) under
                                            #   ATLANTIS_BUILD_TESTS
                                            # + add_subdirectory(tests/tools/shader_compiler)
                                            #   under ATLANTIS_BUILD_TESTS

examples/minimal_renderer_demo/main.cpp     # loadSpirvFile("shaders/...") -> load build-tree
                                            # artifact via Shader-System-exported path (§8);
                                            # PipelineCreateParams::vertexInputLayout built via
                                            # ShaderSystemRhiIntegration::toVertexInputLayout()
examples/minimal_renderer_demo/CMakeLists.txt  # POST_BUILD copy source switches from
                                            # shaders/minimal_renderer/*.spv to the build-tree
                                            # shader output directory (§7/§8)

tests/vulkan_backend/minimal_renderer_gpu_tests.cpp  # same call-site change as the demo
tests/vulkan_backend/CMakeLists.txt         # same POST_BUILD copy-source change

shaders/minimal_renderer/README.md          # rewritten: Slang source note, build-tree
                                            # artifact location, retirement of the manual
                                            # glslc regeneration instructions (§8)
```

### Files to Delete (as the final step of §8's migration, not before)

```
shaders/minimal_renderer/minimal_mesh.vert.glsl
shaders/minimal_renderer/minimal_mesh.frag.glsl
shaders/minimal_renderer/minimal_mesh.vert.spv
shaders/minimal_renderer/minimal_mesh.frag.spv
```

### Files/Directories This Plan Does Not Touch

`AGENTS.md`; `docs/architecture/module_boundaries.md`;
`docs/project-blueprint.md`; `src/rhi/**`; `src/render_graph/**`;
`src/renderer/include/atlantis/renderer/**` (the `Mesh`/`Material`/
`Renderer` public API itself — only the demo's/test's own call sites that
*construct* `PipelineCreateParams` change, per the file list above);
`src/vulkan_backend/**`; `src/platform/**`; `.gitignore` beyond the one
addition §7 lists; any `plans/000{1-7}*.md`, `adr/00{01-27}*.md`. No file
under `src/runtime/` or any Android/iOS path is created or modified. Any
implementation-time discovery that a file outside this list needs
touching is a deviation to call out explicitly in the implementation PR
(per [AGENTS.md](../AGENTS.md)), not to fold in silently — this list is
the scope, not a starting point.

### Target/Dependency Graph

```
atlantis_shader_system                       (STATIC lib, Atlantis::ShaderSystem)
  PUBLIC  Atlantis::Core
  -- no RHI, no Vulkan, no Slang, no OS-process API anywhere in this
     target's sources or public headers --

atlantis_shader_system_rhi_integration        (STATIC lib, Atlantis::ShaderSystemRhiIntegration)
  PUBLIC  Atlantis::ShaderSystem
  PUBLIC  Atlantis::RHI
  -- PUBLIC on both because its own public header (vertex_input_mapping.h)
     returns atlantis::rhi::VertexInputLayout, built from an
     atlantis::shader_system::ReflectionMetadata input; any consumer of
     this header necessarily sees both types. This is the ONLY target
     in the whole codebase with a compile-time dependency on both
     Atlantis::ShaderSystem and Atlantis::RHI. --

atlantis_shader_compiler                      (executable, no alias -- matches
                                                atlantis_minimal_renderer_demo's own
                                                executable-target-gets-no-alias precedent)
  PRIVATE Atlantis::ShaderSystem
  PRIVATE Atlantis::Core
  -- links neither RHI nor the RHI-integration target: Tools never
     constructs a PipelineCreateParams or touches an RHI type. Owns its
     own process_launch.{h,cpp} (Windows-only, private to this target --
     never a Platform or ShaderSystem API). --

Atlantis::RHI                                 -- UNCHANGED. Never gains a dependency
                                                on Atlantis::ShaderSystem, in any form.
```

**Renderer's/the demo's/the GPU test's own link boundary is unchanged in
kind, extended in content**: whichever of them constructs
`PipelineCreateParams` (the demo, the GPU test — never `Renderer` itself,
per ADR-0022, unchanged) gains a `PRIVATE` link to
`Atlantis::ShaderSystemRhiIntegration`, in addition to their existing
links. `src/renderer/`'s own `CMakeLists.txt` is **not modified** — the
`Renderer` class itself never sees a Shader System type.

---

## 2. Shader System Core — Public Types

`src/shader_system/include/atlantis/shader_system/reflection_metadata.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace atlantis::shader_system {

// Mirrors atlantis::rhi::VertexAttributeFormat's own narrowness
// (Float3 only, this round) -- see reflection_metadata.cpp's mapping
// from Slang's scalar-type/element-count pair. A reflected attribute
// whose Slang type does not map to a value here is a transform-time
// error (TransformError::UnsupportedVertexAttributeType, see below),
// never silently coerced.
enum class VertexAttributeType {
  Float3,
};

enum class ShaderStage {
  Vertex,
  Fragment,
};

enum class DescriptorType {
  UniformBuffer,  // this round's only reflected resource kind (ADR-0030's narrow scope)
};

// [JSON-verified against a real Slang reflection sample, per Spec 0008's
// Validation Evidence and ADR-0030's own "Descriptor set index
// [JSON-verified, with a parsing hazard]" entry] set/binding is read
// from Slang's own "descriptorTableSlot" binding kind's "space"/"index"
// fields. This is a POSITIVE, evidence-backed rule, not a guess: a
// dedicated probe compiling a [[vk::binding(3, 2)]] resource emitted
// {"kind": "descriptorTableSlot", "space": 2, "index": 3}, confirmed
// against the same module's disassembled SPIR-V (`DescriptorSet 2` /
// `Binding 3`); a [[vk::binding(0, 0)]] resource emitted no "space" key
// at all. slang_json_transform.cpp therefore parses ANY set value the
// JSON reports (0 or otherwise) into this field -- it does not fail
// closed on a nonzero set, because there is nothing unknown about that
// shape. What DOES reject a nonzero set is a separate, later step:
// Minimal Renderer's own fixed expected contract
// (minimalRendererExpectedDescriptorContract(), descriptor_contract.h)
// only accepts set 0 / binding 0 -- a nonzero-set shader parses into a
// perfectly valid ReflectionMetadata and then fails
// validateDescriptorContract() with a real, specific
// ContractMismatchError (Section 5). Parsing capability and contract
// acceptance are deliberately two separate, independently-testable
// layers -- see Section 3's own restatement of this rule.
struct DescriptorBinding {
  std::uint32_t set = 0;
  std::uint32_t binding = 0;
  DescriptorType type = DescriptorType::UniformBuffer;
  ShaderStage stage = ShaderStage::Vertex;
};

struct PushConstantRange {
  std::uint32_t offsetBytes = 0;
  std::uint32_t sizeBytes = 0;
  ShaderStage stage = ShaderStage::Vertex;
};

// location/type are Shader-System-reflected (from the shader's own
// explicit [[vk::location(X)]] attribute and Slang type, ADR-0030).
// offsetBytes/strideBytes are deliberately ABSENT from this type -- no
// shader reflection tool can derive a host-side interleaved
// vertex-buffer layout from shader source (ADR-0030's own Decision).
// Those two values live on the *caller*-supplied
// MeshVertexAttributeSchema (rhi_integration/vertex_input_mapping.h,
// Section 6), combined with this type only at the point
// toVertexInputLayout() runs.
struct VertexInputAttribute {
  std::uint32_t location = 0;
  VertexAttributeType type = VertexAttributeType::Float3;
};

// The single, Atlantis-owned, versioned schema this whole module reads
// and writes -- populated FROM Slang's own raw -reflection-json output
// by slang_json_transform.cpp, never Slang's raw JSON re-exposed
// verbatim (ADR-0030's own rationale: insulates the rest of Atlantis
// from Slang's own, undocumented, unversioned JSON shape). One instance
// describes exactly one compiled shader STAGE (one entry point) -- this
// matches Spec 0008's own "reflection is per invocation" finding; a
// full material's worth of reflection (vertex + fragment) is two
// separate ReflectionMetadata values, loaded separately.
struct ReflectionMetadata {
  static constexpr int kCurrentSchemaVersion = 1;

  int schemaVersion = kCurrentSchemaVersion;
  std::string entryPointName;   // always "vertexMain"/"fragmentMain" etc. -- the SLANG
                                 // source function name, NOT the emitted SPIR-V
                                 // OpEntryPoint name, which is always "main" (Section 5)
  ShaderStage stage = ShaderStage::Vertex;
  std::vector<DescriptorBinding> descriptorBindings;   // only bindings this entry point's
                                                         // own bindings[].used == true (Section 5
                                                         // step 6) -- module-level-but-unused
                                                         // parameters are filtered out here,
                                                         // never carried into this struct
  std::vector<PushConstantRange> pushConstantRanges;
  std::vector<VertexInputAttribute> vertexInputAttributes;  // empty for a non-vertex stage
  std::vector<std::uint32_t> varyingOutputLocations;   // vertex stage only -- for the
                                                         // supplementary cross-stage check (Section 5 step 12)
  std::vector<std::uint32_t> varyingInputLocations;    // fragment stage only
  std::string sdkProvenance;    // e.g. "1.4.357.0 / slang-standard-module-2026.13.1" --
                                 // see version_provenance.h; opaque to every consumer except
                                 // diagnostics/logging, never parsed back
};

[[nodiscard]] bool operator==(const ReflectionMetadata& lhs, const ReflectionMetadata& rhs);

}  // namespace atlantis::shader_system
```

`src/shader_system/include/atlantis/shader_system/reflection_loader.h`:

```cpp
#pragma once

#include <filesystem>

#include <atlantis/result.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system {

enum class ReflectionLoadError {
  FileNotFound,
  FileReadFailed,
  MalformedJson,
  UnsupportedSchemaVersion,  // schemaVersion field present but > kCurrentSchemaVersion
  MissingRequiredField,      // any field Section 3's grammar marks required is absent
};

enum class ReflectionSaveError {
  FileWriteFailed,
};

// Loads and validates an Atlantis-schema reflection JSON file (NOT
// Slang's own raw -reflection-json output -- see
// slang_json_transform.h for that). Called at build time by
// atlantis_shader_compiler (to re-verify what it just wrote, Section 5
// step 14) and at runtime by ShaderSystemRhiIntegration (Section 6).
// Not thread-safe; caller-thread-only (ADR-0004) -- each call performs
// a fresh, uncached file read, per ADR-0030's "Shader System does not
// cache, retain, or watch the file" rule.
[[nodiscard]] atlantis::Result<ReflectionMetadata, ReflectionLoadError> loadReflectionMetadata(
    const std::filesystem::path& jsonPath);

// Writes metadata as the Atlantis-schema JSON this module's own loader
// above can read back. Called only by atlantis_shader_compiler at build
// time -- no runtime code path in this Plan's scope ever writes this
// file.
[[nodiscard]] atlantis::Result<void, ReflectionSaveError> saveReflectionMetadata(
    const ReflectionMetadata& metadata, const std::filesystem::path& jsonPath);

}  // namespace atlantis::shader_system
```

`src/shader_system/include/atlantis/shader_system/slang_json_transform.h`:

```cpp
#pragma once

#include <filesystem>

#include <atlantis/result.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system {

enum class TransformError {
  FileNotFound,
  FileReadFailed,
  MalformedJson,               // Slang's own JSON does not even parse as JSON
  UnexpectedStructure,         // parses as JSON, but not in the shape slang_json_transform.cpp
                                //  expects (e.g. "entryPoints" missing/not an array) --
                                //  a real Slang-JSON-shape-changed signal, see Section 3
  UnsupportedVertexAttributeType,  // a vertex-stage attribute's Slang type has no
                                    // VertexAttributeType mapping (only Float3 exists this round)
  EntryPointNotFound,          // requestedEntryPointName not present in Slang's own
                                // "entryPoints" array
};

// Reads Slang's own raw -reflection-json output (an external, undocumented,
// unversioned format -- ADR-0030's own rationale for this transform step
// existing at all) and re-projects the ONE named entry point's data into
// this module's own ReflectionMetadata schema. Called only by
// atlantis_shader_compiler, at build time, immediately after a slangc
// invocation succeeds.
[[nodiscard]] atlantis::Result<ReflectionMetadata, TransformError> transformSlangReflectionJson(
    const std::filesystem::path& slangRawJsonPath, const std::string& requestedEntryPointName,
    ShaderStage stage, const std::string& sdkProvenance);

}  // namespace atlantis::shader_system
```

`src/shader_system/include/atlantis/shader_system/descriptor_contract.h`:

```cpp
#pragma once

#include <vector>

#include <atlantis/result.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system {

// The fixed, expected descriptor contract this round's Minimal Renderer
// shaders must match -- a single, Atlantis-Tools-owned constant
// (defined in descriptor_contract.cpp, see its own file-level comment)
// hand-kept in sync with vulkan_device.cpp's own hard-coded
// createPipeline() binding layout. This is a stated, accepted
// single-source-of-truth risk (Section 5's own note), not a solved
// problem -- Spec 0008 scoped descriptor reflection to "validate a
// fixed contract," not "eliminate hand-authored duplication of it."
[[nodiscard]] std::vector<DescriptorBinding> minimalRendererExpectedDescriptorContract();

enum class ContractMismatchError {
  BindingCountMismatch,
  BindingNotFound,       // an expected {set, binding} pair is absent from the reflected shader
  DescriptorTypeMismatch,
  StageMismatch,
  UnexpectedExtraBinding,  // the shader declares a binding the expected contract does not
};

// Compares metadata's own descriptorBindings against `expected`,
// returning Result<void, ...> -- validation only (ADR-0030), never
// constructs or returns anything RHI-shaped. Called by
// atlantis_shader_compiler at build time (Section 5 step 7).
[[nodiscard]] atlantis::Result<void, ContractMismatchError> validateDescriptorContract(
    const ReflectionMetadata& metadata, const std::vector<DescriptorBinding>& expected);

}  // namespace atlantis::shader_system
```

`src/shader_system/include/atlantis/shader_system/command_line.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace atlantis::shader_system {

enum class SlangShaderStageArg { Vertex, Fragment };

// Pure data -- describes ONE slangc invocation compiling ONE entry point
// from ONE Slang source file. No process is spawned by this type or by
// buildSlangcArgv() below; see Section 4 for who does spawn it.
struct SlangCompileRequest {
  std::filesystem::path sourcePath;
  std::string entryPointName;       // e.g. "vertexMain"
  SlangShaderStageArg stage;
  std::filesystem::path spirvOutputPath;
  std::filesystem::path reflectionJsonOutputPath;  // Slang's own raw JSON, an
                                                     // intermediate file -- Section 5
};

// Returns the exact argv (argv[0] is the slangc executable path itself,
// matching how process_launch.h's launchProcess() consumes it) for the
// request above. Fixes, as tested, non-Plan-revisable facts (per Spec
// 0008 Approval / ADR-0028 Decision):
//  -profile spirv_1_0             (Option A, mandatory -- NOT -capability,
//                                   which experimentally does not select
//                                   the output version, ADR-0028)
//  -warnings-disable 50011        (Policy S, mandatory)
//  no -fvk-use-entrypoint-name    (never passed, ever -- Section 5)
//  -target spirv
//  -stage <vertex|fragment>
//  -entry <entryPointName>
//  -o <spirvOutputPath>
//  -reflection-json <reflectionJsonOutputPath>
[[nodiscard]] std::vector<std::string> buildSlangcArgv(const std::filesystem::path& slangcExecutablePath,
                                                         const SlangCompileRequest& request);

// argv for `spirv-val --target-env vulkan1.0 <spirvPath>` (ADR-0031,
// mandatory). No flag beyond --target-env is added by default.
[[nodiscard]] std::vector<std::string> buildSpirvValArgv(const std::filesystem::path& spirvValExecutablePath,
                                                           const std::filesystem::path& spirvPath);

}  // namespace atlantis::shader_system
```

`src/shader_system/include/atlantis/shader_system/version_provenance.h`:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace atlantis::shader_system {

// ADR-0031's provenance anchor: no confirmed slangc --version flag
// exists, so provenance is read from the Vulkan SDK's own directory
// structure instead -- specifically, a slang-standard-module-<version>
// directory sibling to slangc.exe (Spec 0008's Validation Evidence
// observed slang-standard-module-2026.13.1 on the reference SDK
// install). Returns std::nullopt if that directory is not found --
// this is a best-effort provenance string, not a hard build
// requirement; its absence does not fail the build (contrast with
// slangc/spirv-val themselves, which do).
[[nodiscard]] std::optional<std::string> describeSdkProvenance(const std::filesystem::path& slangcExecutablePath);

}  // namespace atlantis::shader_system
```

**Ownership/thread-safety summary for every type above:** all are plain
value types or free functions; no type owns a file handle, a process
handle, or a Slang/RHI object across a call boundary. None is
thread-safe for concurrent access from more than one thread — consistent
with every other Phase 1 Core-adjacent type (ADR-0004) — and none needs
to be, since every call site in this Plan's own scope is a single-
threaded, single-invocation build-time process or a single-threaded
program-startup path.

## 3. JSON Parsing — Hand-Rolled, Narrow, No New Dependency

**Why a hand-rolled parser, not a library — this is not a free Plan-stage
choice, it is what the Approved Spec already fixes.** Spec 0008's own
Non-functional Requirements state "zero new third-party dependencies"
(reaffirmed in its Revision Note: "This spec now introduces zero new
`FetchContent`-acquired third-party dependencies"). [AGENTS.md](../AGENTS.md)
requires any new dependency — including a JSON library — to go through
its own Spec/ADR review; this Plan has no authority to introduce one.
**A small, internal, narrowly-scoped JSON parser, private to
`atlantis_shader_system`'s implementation, is therefore the only
Plan-authorized option** — not a general-purpose Core JSON facility (that
would expand this module's public surface and Core's own scope beyond
what Spec 0008 asked for; a future spec with a real second JSON consumer
can decide whether to generalize it then).

**Grammar scope — sufficient for both Slang's raw JSON and Atlantis's own
schema, nothing more:**

- Full JSON value model: object, array, string, number, `true`/`false`/
  `null` — this is *not* optional/reduced, because Slang's own raw
  reflection JSON (which `slang_json_transform.cpp` must parse) uses all
  of these (see Spec 0008's Validation Evidence sample: nested objects,
  arrays of objects, string/number/bool leaf values).
- **String escapes:** the standard JSON escape set (`\"`, `\\`, `\/`,
  `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX`) is supported; `\uXXXX`
  surrogate-pair handling for characters outside the Basic Multilingual
  Plane is supported (Slang identifiers/paths are not expected to need
  this, but a malformed or unexpected surrogate pair must be a parse
  error, never undefined behavior or silent truncation).
- **Numbers:** parsed as `double` internally (this module never emits or
  consumes a JSON number requiring more precision than a `double` gives —
  every numeric field in `ReflectionMetadata`, above, is a small integer:
  offsets, sizes, counts, locations); a number outside `double`'s exactly-
  representable-integer range is not a case this module's own fields
  produce or need to round-trip exactly, and is not specially guarded
  against beyond ordinary `double` parsing.
- **Malformed input:** any structurally invalid JSON (unterminated
  string, unexpected token, trailing garbage after the top-level value,
  duplicate object keys — the last treated as "last one wins," matching
  the JSON specification's own permissive stance, not specially
  rejected) is `Result::Err(...MalformedJson)`, never a partially-parsed
  value or a crash.
- **Unknown fields (both directions):**
  - When `json_parser.cpp`'s output is walked by `slang_json_transform.cpp`
    to build a `ReflectionMetadata`: any JSON object key
    `slang_json_transform.cpp` does not explicitly look for is silently
    ignored — Slang's own raw JSON is not fully modeled, only the
    specific fields Section 2's `ReflectionMetadata` needs.
  - When `reflection_loader.cpp` reads back Atlantis's own schema
    (written by `saveReflectionMetadata()` moments earlier, or loaded
    much later at program-startup time by the RHI-integration target):
    an unrecognized top-level key is likewise ignored — this is what
    lets a future schema version add a field without breaking an older
    reader, within the same `schemaVersion` (a genuinely incompatible
    change bumps `kCurrentSchemaVersion` instead, per Section 2).
- **Missing required fields:** `reflection_loader.cpp`'s reader for
  Atlantis's own schema treats `schemaVersion`, `entryPointName`, and
  `stage` as required — their absence is
  `ReflectionLoadError::MissingRequiredField`. `descriptorBindings`/
  `pushConstantRanges`/`vertexInputAttributes`/the two varying-location
  arrays default to empty when absent (a shader with no descriptor
  bindings, e.g., is a legitimate, if unused-by-this-round's-material,
  case — an empty array is not an error). `slang_json_transform.cpp`'s
  reading of Slang's *raw* JSON is more defensive still: any structural
  assumption it makes that does not hold (e.g. `"entryPoints"` missing or
  not an array) is `TransformError::UnexpectedStructure`, treated as a
  real signal that Slang's own JSON shape has changed — see the SDK-
  upgrade handling below.

**Descriptor-set parsing rule — stated once, positively, here (Section 2's
own comment restates it, not a second, independent source of truth):**

```
if the reflected binding object has a "space" key:
    set = (value of "space", parsed as an unsigned integer per the
           resource-limit rules below)
else:
    set = 0
binding = (value of "index", parsed the same way)
```

This is a **[JSON-verified]** rule (ADR-0030), not a heuristic — both the
"absent means 0" case and the "present means that explicit value" case
were directly observed against real `slangc` output (a `[[vk::binding(0,
0)]]` resource emitting no `"space"` key; a `[[vk::binding(3, 2)]]`
resource emitting `"space": 2`), and the latter was independently
cross-checked against the same module's disassembled SPIR-V
(`DescriptorSet 2`/`Binding 3`). **`slang_json_transform.cpp` parses a
nonzero `set` successfully into `ReflectionMetadata` — it does not fail
closed on this shape.** A malformed `"space"`/`"index"` value (not an
integer, negative, or outside `std::uint32_t`'s range) is a resource-
limit-driven parse failure (below), not a nonzero-set-specific case.

**Contract acceptance is a separate, later, independently-testable
layer.** `minimalRendererExpectedDescriptorContract()`
(`descriptor_contract.h`) is fixed, this round, to exactly `{set: 0,
binding: 0}` — a shader reflecting `{set: 2, binding: 3}` parses into a
perfectly valid `ReflectionMetadata`, then fails
`validateDescriptorContract()` with `ContractMismatchError::BindingNotFound`
(the expected `{0, 0}` pair is absent) and/or
`ContractMismatchError::UnexpectedExtraBinding` (the shader declares a
pair the contract does not expect) — see Section 5. This split (parser
accepts and faithfully represents any set/binding the shader actually
uses; a *separate*, narrower contract check rejects anything Minimal
Renderer's own fixed material does not need) is what lets a future
material with a real nonzero-set need extend only
`minimalRendererExpectedDescriptorContract()`'s equivalent — or a
material-specific expected-contract value, Section 5's own
`--expected-contract=` mechanism — without touching the parser at all.

**Fixture-based coverage for known real-world shapes, per Spec 0008's own
Validation Evidence and ADR-0030's `[JSON-verified]` findings:**

- A fixture matching the actual sample JSON captured during Spec 0008's
  validation experiment (descriptor set 0, no explicit `"space"` field
  present) — `slang_json_transform.cpp` parses this to `set = 0`.
- **A positive fixture for a nonzero descriptor set**, reusing the exact
  JSON shape ADR-0030 already recorded as a static, checked-in test
  fixture — `{"kind": "descriptorTableSlot", "space": 2, "index": 3}` —
  requiring no new toolchain run: `slang_json_transform.cpp` parses this
  to `{set: 2, binding: 3}` in the resulting `ReflectionMetadata` (a
  parser-level pass); a *second*, separate test then feeds that same
  `ReflectionMetadata` into `validateDescriptorContract()` against
  Minimal Renderer's own `{set: 0, binding: 0}`-only expected contract
  and asserts the specific `ContractMismatchError` this mismatch
  produces (a contract-level, expected-to-fail case). Both halves of
  this fixture are required — a Plan that tested only one would leave
  the parser-vs-contract split (above) unverified.
- A malformed-`"space"`/`"index"`-value negative fixture (a string where
  a number is expected, a negative number, a number exceeding
  `std::uint32_t`) → a resource-limit parse failure (below), distinct
  from, and not conflated with, the ordinary nonzero-set positive case
  immediately above.
- A fixture for the module-level-`"parameters"`-vs-entry-point-level-
  `"bindings"`-with-`"used"` combination rule (Spec 0008's own finding:
  top-level `"parameters"` is module-scope even for parameters the
  compiled stage never references) — `slang_json_transform.cpp` only
  includes a binding in the resulting `ReflectionMetadata` when the
  corresponding entry in the entry point's own `"bindings"` array has
  `"used": true` (or an equivalent truthy marker — exact field-name
  confirmation deferred to Implementation's own fixture-driven
  development against a fresh real sample, not guessed here).
- A push-constant fixture, explicitly cross-checked against
  [issue #5676](https://github.com/shader-slang/slang/issues/5676)'s
  disclosed caveat — Section 5 and Section 9 both require a real-SDK
  regression test asserting the reflected push-constant offset/size
  matches the shader's own declared layout, not merely that *a* value
  was returned.

**Resource limits — conservative, fixed constants, private to this
module, not a Core-wide configuration surface:**

| Limit | Value | Rationale |
|---|---|---|
| Maximum input size | 16 MiB | Every real reflection JSON captured during Spec 0008's Validation Evidence was on the order of a few KB; this is a generous ceiling that rejects a pathological/corrupted input before parsing begins, not a realistic operating limit. |
| Maximum nesting depth (object/array) | 64 | Real Slang reflection JSON nests a handful of levels deep (parameter → type → elementType → ...); 64 is far beyond any legitimate shape and bounds the parser's own recursion so a malformed/adversarial input cannot cause unbounded recursion or a stack overflow. |
| Maximum string length | 64 KiB | Reflection field values are short identifiers/paths; generous relative to real data. |
| Maximum array/object element count (per array/object) | 4096 | Real data has, at most, a handful of parameters/bindings/attributes; generous relative to real data. |

Exceeding any limit above is a recoverable parse error
(`...MalformedJson`/`...UnexpectedStructure`, matching whichever caller
context is parsing — never a crash, never silent truncation). These
constants live in `json_parser.cpp`, are not exposed as a public,
runtime-configurable API, and are not proposed as a Core-wide facility —
consistent with this module's own "not generalized into a Core JSON
facility" rule, below.

**Integer parsing:** every numeric field this module's own schema
defines (`set`, `binding`, `offsetBytes`, `sizeBytes`, `location`, etc.)
is declared `std::uint32_t`; a JSON number that is negative, non-integer
(carries a fractional part), or exceeds `std::uint32_t`'s range is a
parse failure for that field — never silently truncated, rounded, or
wrapped.

**Duplicate object keys — "last one wins," a deliberate, reviewed choice,
not an oversight:** matches the JSON specification's own permissive
stance (it does not forbid duplicate keys or mandate a specific
resolution), and matches the behavior of essentially every mainstream
JSON parser Atlantis's implementers are likely already familiar with.
Flagged here explicitly, once, for Human Review awareness — Implementation
does not need to revisit this choice unless Human Review specifically
asks for stricter (reject-on-duplicate) behavior instead.

**SDK/Slang-version-upgrade handling:** `slang_json_transform.cpp`'s
parser is **not** validated against, or assumed compatible with, any
Slang/Vulkan-SDK version other than the one this Plan's own fixtures were
captured against (1.4.357.0 / Slang 2026.13.1, per Spec 0008's Validation
Evidence). A future SDK upgrade requires re-running the fixture capture
and re-verifying `slang_json_transform_tests.cpp` against the new
sample **before** that upgrade is adopted — this Plan does not attempt
version-range compatibility logic, matching ADR-0030's own "Atlantis does
not control Slang's output JSON schema" acknowledgment. This re-
verification requirement is recorded as a maintenance note in
`tests/shader_system/slang_json_transform_tests.cpp`'s own file header,
not enforced by any runtime check (there is nothing to check *against* —
Slang exposes no schema-version marker of its own).

**Not generalized into a Core JSON facility** — `json_value.h`/
`json_parser.h` stay `src/shader_system/src/`-private (never installed
under `include/`), reachable only by this module's own `.cpp` files.

## 4. Tools CLI — Process Execution

Fully specified against the official `CreateProcessW` documentation
([Microsoft Learn — CreateProcessW function](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)),
cited inline at each design point below — this section fixes the process
model completely enough that Implementation does not invent it on the
spot.

`src/tools/shader_compiler/process_launch.h` (private to this target):

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <atlantis/result.h>

namespace atlantis::tools::shader_compiler {

// A single combined capture -- see "Diagnostic capture model" below for
// why this is one string, not separate stdout/stderr fields.
struct ProcessOutput {
  std::string diagnostics;
  std::int32_t exitCode = 0;
};

enum class ProcessLaunchError {
  ExecutableNotFound,       // executablePath did not exist at launch time
                             // (checked before ever calling CreateProcessW --
                             // see "Executable resolution" below)
  DiagnosticFileCreationFailed,
  LaunchFailed,              // CreateProcessW itself returned FALSE;
                              // GetLastError() text folded into this
                              // error's own diagnostic, not a separate variant
  WaitFailed,                // WaitForSingleObject() returned WAIT_FAILED
  ExitCodeQueryFailed,       // GetExitCodeProcess() itself failed
  DiagnosticFileReadFailed,
};

// Windows-only (WIN32_LEAN_AND_MEAN, CreateProcessW). Tools-internal
// implementation detail -- never an Atlantis::Platform API, never a
// ShaderSystem type (see Critical Architectural Boundaries). Blocking;
// runs the child process to completion. Not thread-safe; caller-thread-
// only, matching this whole executable's single-threaded, single-
// invocation model (Section 1). See "Timeout/cancellation" below for
// this function's one explicitly accepted limitation.
[[nodiscard]] atlantis::Result<ProcessOutput, ProcessLaunchError> launchProcess(
    const std::filesystem::path& executablePath, const std::vector<std::string>& arguments);

}  // namespace atlantis::tools::shader_compiler
```

**Executable resolution — `lpApplicationName`, never `NULL`.**
`executablePath` (already resolved to an absolute path by CMake's own
`find_program()` at configure time, Section 7 — `launchProcess()` itself
performs no `PATH` search) is passed as `CreateProcessW`'s
`lpApplicationName` parameter, **not** left `NULL` with the executable
name embedded only in `lpCommandLine`'s first token. This is not a
stylistic preference — the official documentation's own Security Remarks
name the exact hazard this avoids: *"If the executable or path name has a
space in it, there is a risk that a different executable could be run
because of the way the function parses spaces"* — the documented example
shows `CreateProcess(NULL, "C:\Program Files\MyApp -L -S", ...)`
attempting to run `C:\Program.exe` if it exists, instead of `MyApp.exe`.
Passing a resolved path via `lpApplicationName` sidesteps that ambiguity
entirely, per the same documentation's own recommendation. Before ever
calling `CreateProcessW`, `launchProcess()` checks
`std::filesystem::exists(executablePath)` and returns
`ProcessLaunchError::ExecutableNotFound` directly if it does not — this
keeps the "missing tool" case cheaply testable without needing to parse
a `GetLastError()` code, and keeps `CreateProcessW`'s own failure path
(`LaunchFailed`) scoped to genuine OS-level launch failures.

**Command-line construction — a mutable, owned buffer, never a string
literal.** The official documentation is explicit: *"The Unicode version
of this function, CreateProcessW, can modify the contents of this
string. Therefore, this parameter cannot be a pointer to read-only
memory (such as a const variable or a literal string). If this parameter
is a constant string, the function may cause an access violation."*
`launchProcess()` therefore builds `lpCommandLine` into a **`std::wstring`
with its own storage** (never a `const wchar_t*` from a literal, and
never a `.c_str()` result taken from a `const` object elsewhere), and
passes that string's own non-`const` `data()` (C++17 guarantees
`std::wstring::data()` is contiguous, mutable, and null-terminated). This
buffer's lifetime covers the entire `CreateProcessW` call and the
subsequent `WaitForSingleObject()`/`GetExitCodeProcess()` sequence — it
is not freed or allowed to go out of scope until the child process has
been waited on. Quoting follows the standard Windows/MSVC C runtime
argument-quoting convention (the same one `CommandLineToArgvW()`
consumes): each argument containing whitespace, a double quote, or being
otherwise ambiguous is wrapped in `"`; embedded `"` characters are
escaped as `\"`; a run of `N` backslashes immediately preceding a `"`
(literal or the argument's own closing quote) is doubled to `2N`
backslashes; a run of backslashes not immediately followed by a `"` is
left untouched. This algorithm is implemented exactly once, inside
`process_launch.cpp`, never re-derived at any call site — `command_line.cpp`
(Section 2) only ever produces a plain `std::vector<std::string>` argv,
with no quoting logic of its own.

**Diagnostic capture model — a single temporary file, not pipes.**
Rejected: two anonymous pipes (one for `stdout`, one for `stderr`) with
sequential reads — a well-known deadlock hazard (the child can block
writing to a full pipe buffer while the parent is still blocked reading
the *other* stream first) that would require either a second reader
thread or overlapped I/O to avoid safely, either of which is a real-time/
concurrency mechanism this Plan has no other reason to introduce
(Non-Goals: no job/task system). **Adopted instead:** `launchProcess()`
creates one temporary file (unique per invocation — Section 7 gives the
exact per-invocation temp-path scheme this reuses) via `CreateFileW`,
with a `SECURITY_ATTRIBUTES` marking **only this one handle** inheritable
(`bInheritHandle = TRUE`); every other handle this process holds is left
at its default, non-inheritable state (Windows handles are non-
inheritable unless explicitly created or marked otherwise — the official
documentation's own `bInheritHandles` remarks confirm inheritance is
opt-in per handle, not process-wide once `bInheritHandles = TRUE` is
passed to a specific handle set), so no unrelated open handle leaks into
the child. Both `STARTUPINFOW::hStdOutput` and `hStdError` are set to
this **same** file handle; the child
process's stdout and stderr writes therefore interleave into one file in
their actual chronological write order (the OS serializes writes through
one underlying file handle), which is simpler and more useful for a
human reading a build-log diagnostic than two separately-ordered
captures would be — matching `ProcessOutput::diagnostics` being a single
field, not separate `stdOut`/`stdErr` fields as an earlier draft of this
Plan had.

**`STARTUPINFOW::dwFlags` must explicitly include `STARTF_USESTDHANDLES`
— stated here because it is easy to silently omit.** Per the official
documentation's own explicit wording for `hStdOutput`/`hStdError`: *"If
dwFlags specifies STARTF_USESTDHANDLES, this member is the standard
[output/error] handle for the process. **Otherwise, this member is
ignored** and the default for standard output is the console window's
buffer."* Setting `hStdOutput`/`hStdError` without also setting this
flag has **no effect whatsoever** — the child would silently fall back
to inheriting the parent's own console buffer, defeating this entire
diagnostic-capture design without any error or symptom other than an
empty `ProcessOutput::diagnostics` and a failing
`process_launch_tests.cpp` happy-path case (Section 9). `si.dwFlags |=
STARTF_USESTDHANDLES;` is therefore set explicitly, alongside
`hStdOutput`/`hStdError`, before `CreateProcessW` is called. The same
official documentation also requires, for this flag: *"the handles must
be inheritable and the function's bInheritHandles parameter must be set
to TRUE"* — already satisfied by the single-inheritable-handle design
above and by `bInheritHandles = TRUE` below.

**`hStdInput` — once `STARTF_USESTDHANDLES` is set, it is no longer
ignorable and must be an explicit, valid, inheritable handle**, per the
same documentation (*"If dwFlags specifies STARTF_USESTDHANDLES, this
member is the standard input handle for the process"* — the "default is
the keyboard buffer" fallback only applies when the flag is *not* set).
`launchProcess()` therefore opens a second, separate inheritable handle
to the Windows NUL device (`CreateFileW(L"NUL", GENERIC_READ, ...,
&inheritableSecurityAttributes, OPEN_EXISTING, ...)`) and assigns it to
`hStdInput` — `slangc`/`spirv-val` are one-shot CLI invocations that
never read standard input, so a NUL device (immediate EOF on any read)
is the correct, documented-safe choice, never the parent's own console
input. This handle is closed by the same RAII guard covering the
diagnostic file handle (both are opened, marked inheritable, and closed
on the same lifecycle), and it is opened non-writable (`GENERIC_READ`
only) since the child never writes to it. `CreateProcessW` is called
with `bInheritHandles = TRUE` (required for the child to actually
receive both inherited handles) and `dwCreationFlags = 0` (no console
allocated/detached — this Plan does not need one). The parent closes its own copy of the
diagnostic file's handle immediately after `CreateProcessW` returns
successfully (the child now holds its own inherited copy), then, after
`WaitForSingleObject()` confirms the child has exited, reopens the same
temporary file path for reading and reads its full contents into
`ProcessOutput::diagnostics` as raw bytes, interpreted as UTF-8 (a
non-UTF-8 byte sequence is tolerated as opaque bytes for logging/
diagnostic purposes only — this text is never parsed structurally by any
Atlantis code, only surfaced to a human or a build log). The temporary
diagnostic file is deleted after being read, on every code path
(success or failure) — a small RAII guard inside `process_launch.cpp`
(not part of this header's public surface) owns this cleanup, matching
the same "clean up on every exit path" discipline Section 7's artifact-
publish design uses.

**Handle lifetime (`PROCESS_INFORMATION`).** Per the official
documentation's own explicit requirement (*"Handles in
PROCESS_INFORMATION must be closed with CloseHandle when they are no
longer needed"*), both `hProcess` and `hThread` are owned by a small,
private RAII guard type inside `process_launch.cpp` for the duration of
the wait/exit-code-query sequence, guaranteeing `CloseHandle()` runs on
every exit path (including early-return error paths) — this guard is an
implementation detail, not part of this header's public API.

**Working directory and environment — both inherited, not overridden.**
`lpCurrentDirectory` and `lpEnvironment` are both passed as `NULL` — the
child inherits the calling `atlantis_shader_compiler` process's own
current directory and full environment block (including `VULKAN_SDK`,
`PATH`, and anything else the calling shell/CI environment already set)
unchanged. `atlantis_shader_compiler` itself never depends on its own
current working directory for anything (every path it needs — source,
output directory, `slangc`/`spirv-val` paths — arrives as an explicit
CLI argument, Section 7), so there is no reason to override either.

**Exit code retrieval.** After `WaitForSingleObject(hProcess, INFINITE)`
returns `WAIT_OBJECT_0`, `GetExitCodeProcess()` retrieves the child's
exit code into `ProcessOutput::exitCode` (`std::int32_t`, matching
`slangc`'s/`spirv-val`'s own observed exit-code range). A non-zero exit
code is not itself a `ProcessLaunchError` — `launchProcess()` returns
`Result::Ok(ProcessOutput{...})` for *any* exit code, successful or not;
interpreting a non-zero exit code as a compile/validate failure is
`compile_and_validate.cpp`'s own responsibility (Section 5), keeping
`launchProcess()` itself a narrow, reusable "run this process and report
what happened" primitive, not one that also encodes domain-specific
success/failure semantics for `slangc` specifically.

**Timeout/cancellation — explicitly out of scope, honestly bounded.**
`WaitForSingleObject()` is called with `INFINITE` — no timeout. The
diagnostic-capture redesign above **structurally eliminates the pipe-
buffer-deadlock risk** the original pipe-based design would have carried
(a single file has no fixed buffer that can fill and block a write), but
it does **not** eliminate a genuinely different risk this Plan still
accepts deliberately: a `slangc`/`spirv-val` invocation that itself hangs
(enters an infinite loop, waits on something that never completes) would
still hang the whole build indefinitely, with no automatic recovery this
round. This is stated plainly, not minimized — Section 9/13 record it as
an accepted Phase 1 limitation, not a silently assumed-safe corner.

**Atomic, all-or-nothing artifact publication.** How the compiled `.spv`/
reflection-metadata pair is published only after every validation step
succeeds — and how a partial or failed publish is prevented from ever
being observable by a consumer or a subsequent build — is specified in
full in Section 7's stamp-based transaction model, not here; this
section's own scope is the process-execution primitive that model is
built on top of.

## 5. Reflection and Contract-Validation Algorithm

Implemented in `src/tools/shader_compiler/compile_and_validate.h/.cpp`,
invoked once per `(source .slang file, entry point)` pair — i.e. once
per compiled shader **stage**, matching RHI's unchanged two-separate-
`ShaderStageBytecode`-blobs contract (ADR-0025, unchanged):

```
1.  Resolve slangc/spirv-val executable paths (passed in as CLI args,
    Section 1's --slangc-path/--spirv-val-path -- Tools itself does not
    re-run find_program(); CMake does that once, at configure time,
    Section 7, and passes the resolved paths down).
2.  Build a SlangCompileRequest (Section 2) for this invocation; call
    ShaderSystem::buildSlangcArgv().
3.  launchProcess(slangcPath, argv) -> ProcessOutput.
    - Non-zero exit code -> compile_and_validate.cpp exits non-zero,
      surfacing slangc's own captured diagnostics verbatim to the build
      log. No artifact is published (Section 7's stamp is never
      created) -- see Section 7 for the full transaction/cleanup model.
4.  Slang emits BOTH the SPIR-V bytes (to a per-invocation-unique temp
    path, Section 7) and its own raw reflection JSON (to a temp path) as
    part of step 3's single invocation (-o and -reflection-json in the
    same slangc call, Section 2's SlangCompileRequest already reflects
    this).
5.  ShaderSystem::transformSlangReflectionJson(tempRawJsonPath,
    entryPointName, stage, sdkProvenance) -> ReflectionMetadata.
    - TransformError -> exit non-zero; no artifact published.
6.  (Folded into step 5's own implementation, per Section 3's grammar
    note): entries in Slang's raw "parameters" not marked used == true
    for THIS entry point are filtered out before they ever reach the
    returned ReflectionMetadata's descriptorBindings/pushConstantRanges.
7.  If this stage is Vertex or Fragment (both are, this round) AND this
    material is Minimal Renderer's own (identified by a
    --expected-contract=minimal-renderer CLI flag, Section 1 -- a
    Plan-stage placeholder for "which fixed contract applies," since
    this round has exactly one):
      ShaderSystem::validateDescriptorContract(metadata,
        minimalRendererExpectedDescriptorContract())
      -> ContractMismatchError -> exit non-zero; no artifact published
         (Section 7's stamp is never created).
    Note (Section 2/3): step 5's transform already parses a nonzero
    descriptor set correctly into `metadata` if the shader actually
    declares one -- it is exactly THIS step, not the parser, that
    rejects it, because minimalRendererExpectedDescriptorContract()
    only accepts {set: 0, binding: 0} this round. A shader declaring
    {set: 2, binding: 3} fails here with BindingNotFound/
    UnexpectedExtraBinding, not with a transform-time error.
8.  Push-constant validation: metadata.pushConstantRanges must contain
    exactly the range(s) this material's own fixed expectation names
    (this round: one range, vertex stage, size == sizeof(float) * 16)
    -- same "compare against a fixed, hand-specified expectation"
    pattern as step 7, not a separate mechanism. Mismatch -> exit
    non-zero.
9.  Vertex-stage-only: every metadata.vertexInputAttributes[i].location
    must be explicit and unique within this reflection (no duplicate
    locations) -- Slang's own compiler already rejects two [[vk::location]]
    collisions as a compile error (caught at step 3), so this is a
    narrow, redundant sanity check on the SHAPE of what transform
    already produced, not a second independent source of truth.
10. (Runtime-only step, NOT performed here -- see the note below):
    cross-validation against the caller's own Mesh-schema stride/offset
    table happens later, inside ShaderSystemRhiIntegration, at whatever
    point a demo/test actually constructs a VertexInputLayout (Section
    6) -- Tools has no visibility into that C++ code at build time.
11. (Same non-build-time note): step 10's C++ Mesh schema is unavailable
    here -- omitted from this build-time algorithm entirely.
12. Supplementary cross-stage interface check -- ONLY performed when
    BOTH stages of a material have already been compiled in this same
    Tools invocation sequence (Section 1's CLI takes a --pair-with=<path>
    flag naming the other stage's own already-written reflection JSON,
    a Plan-stage detail): vertex metadata.varyingOutputLocations must be
    a superset of fragment metadata.varyingInputLocations, by location
    index. Mismatch -> exit non-zero. This supplements, and does not
    substitute for, Slang's own primary compile-time guarantee (the
    shared varying-interface struct authoring convention, Section 8) --
    per ADR-0030's own framing.
13. spirv-val --target-env vulkan1.0 <temp .spv path> (mandatory, ADR-0031).
    Non-zero exit -> exit non-zero; no artifact published. Its own
    captured diagnostics are surfaced verbatim on failure.
14. Only once every step above has succeeded, for BOTH stages: run
    Section 7's publish transaction (write final ReflectionMetadata via
    saveReflectionMetadata() to a temp path; publish all four final
    artifact files; write the stamp last, only after all four are
    safely in place). Any failure during publication itself is handled
    entirely by Section 7's own cleanup rules, not repeated here.
15. Exit 0 only after the stamp has actually been written.
```

**What is genuinely build-time vs. genuinely runtime — stated explicitly
because Spec 0008's own Requirements demand it (its Risk item on this
exact question):** steps 1–15 above are **entirely build-time** — a
descriptor-contract or push-constant mismatch fails the **build**, not a
later program run. What is **not**, and structurally cannot be,
build-time: the final combination of reflected `location`/`type` data
with a caller's own C++ Mesh-schema `stride`/`offset` table (Section 6) —
Tools has no access to that C++ code, which is compiled separately, by a
different target, possibly long after the shader itself was compiled.
This is not a design gap this Plan failed to close — it is the direct,
unavoidable consequence of ADR-0030's own "vertex stride is host-C++-
owned, never shader-owned" decision, restated honestly rather than
implied away.

## 6. RHI Integration Target

`src/shader_system/rhi_integration/include/atlantis/shader_system/rhi_integration/vertex_input_mapping.h`:

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include <atlantis/result.h>
#include <atlantis/rhi/types.h>
#include <atlantis/shader_system/reflection_metadata.h>

namespace atlantis::shader_system::rhi_integration {

// Caller-supplied, Mesh/vertex-schema-owned data -- exactly the two
// fields ReflectionMetadata's own VertexInputAttribute deliberately
// omits (Section 2). One entry per attribute, matched to
// ReflectionMetadata::vertexInputAttributes by `location` (not by
// array position -- order is not assumed to match).
struct MeshVertexAttributeSchema {
  std::uint32_t location = 0;
  std::uint32_t offsetBytes = 0;
};

enum class MappingError {
  AttributeCountMismatch,   // reflected attribute count != schema entry count
  LocationNotFoundInSchema, // a reflected location has no matching schema entry
  UnsupportedVertexAttributeType,  // mirrors TransformError's own case, re-checked here
                                     // defensively since this metadata may have been loaded
                                     // from disk independently of the compile that produced it
};

// Combines vertexMetadata's reflected {location, type} pairs with
// schema's caller-supplied {location, offsetBytes} pairs into RHI's
// existing VertexInputLayout -- cross-validates (does not invent):
// every reflected location must have a matching schema entry, and vice
// versa (ADR-0030's own "cross-validate, never silently accept a
// mismatch" rule). strideBytes is a direct, un-cross-validated pass-
// through of the strideBytes parameter below -- there is no reflected
// value to cross-validate it against (Section 2's own rationale).
[[nodiscard]] atlantis::Result<atlantis::rhi::VertexInputLayout, MappingError> toVertexInputLayout(
    const ReflectionMetadata& vertexMetadata, const std::vector<MeshVertexAttributeSchema>& schema,
    std::uint32_t strideBytes);

// Sums metadata.pushConstantRanges (this round: expected to be exactly
// one range) into the single std::size_t
// PipelineCreateParams::pushConstantSizeBytes expects. Returns 0 (not
// an error) if metadata has no push-constant ranges at all -- a
// legitimate, if unused-by-this-round's-material, case.
[[nodiscard]] std::size_t toPushConstantSize(const ReflectionMetadata& metadata);

}  // namespace atlantis::shader_system::rhi_integration
```

**Attachment format (`PipelineCreateParams::colorFormat`/`depthFormat`)
is not, and was never claimed to be, part of this target's
responsibility** — per Spec 0008's own authority table
(`Presentation::metadata().format`/the Vulkan Backend's fixed depth
format, per [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md),
unchanged) — this Plan's own demo/test call sites (Section 8) continue
sourcing those two fields exactly as they do today
(`examples/minimal_renderer_demo/main.cpp`'s own `currentFormat`/
`DepthFormat::D32Sfloat` literals, unchanged by this Plan).

**Runtime error handling:** a `MappingError` at this point (called once,
at whichever point a demo/test constructs its `Material`, well before
the render loop starts — never per-frame) is a program-startup-time
failure, not a per-frame one — consistent with how `createMaterial()`
failure is already handled in `examples/minimal_renderer_demo/main.cpp`
today (log and exit, Section 8 preserves this pattern). It is **not** a
"build failed" outcome in the CMake sense — by the time this code runs,
the build already succeeded (Section 5's step 14 already validated the
descriptor contract and push-constant layout at build time); a
`MappingError` here can only mean the demo's/test's own C++
`MeshVertexAttributeSchema` table itself is wrong (e.g. a location typo),
which no build-time check can catch since that C++ code is not visible
to `atlantis_shader_compiler`.

## 7. Artifact Location and CMake Pipeline

**Output location** (ADR-0031, unchanged from Spec 0008's own Decision):
`${CMAKE_BINARY_DIR}/shaders/<relative-path-mirroring-source>/`, single,
configuration-independent — for Minimal Renderer's own migrated shader,
concretely `${CMAKE_BINARY_DIR}/shaders/minimal_renderer/minimal_mesh.vert.spv`
and `.../minimal_mesh.vert.refl.json` (and the `.frag.` equivalents).

**Configure-time tool discovery** (root `CMakeLists.txt`, added after the
existing `find_package(Vulkan REQUIRED)`):

```cmake
find_program(ATLANTIS_SLANGC_EXECUTABLE
  NAMES slangc
  HINTS "$ENV{VULKAN_SDK}/Bin"
)
if(NOT ATLANTIS_SLANGC_EXECUTABLE)
  message(FATAL_ERROR
    "slangc not found. It ships with the Vulkan SDK (confirmed present "
    "since SDK 1.3.296.0) -- verify VULKAN_SDK is set and points at an "
    "SDK install whose Bin/ directory contains slangc.exe.")
endif()

find_program(ATLANTIS_SPIRV_VAL_EXECUTABLE
  NAMES spirv-val
  HINTS "$ENV{VULKAN_SDK}/Bin"
)
if(NOT ATLANTIS_SPIRV_VAL_EXECUTABLE)
  message(FATAL_ERROR
    "spirv-val not found. It ships with the Vulkan SDK alongside slangc "
    "-- verify VULKAN_SDK is set and points at an SDK install whose "
    "Bin/ directory contains spirv-val.exe. spirv-val is a MANDATORY "
    "build-time verification step (ADR-0031), not optional.")
endif()
```

Both resolved paths are passed to `atlantis_shader_compiler` invocations
as CLI arguments (`--slangc-path=...`/`--spirv-val-path=...`), not baked
into the tool's own source — this keeps the tool itself testable without
requiring a real Vulkan SDK install for its own pure-logic unit tests
(Section 9).

**Per-shader-pair custom-command chain — stamp-based, all-or-nothing
transaction** (a CMake function, `atlantis_add_slang_shader_pair(NAME ...
SOURCE ... VERTEX_ENTRY ... FRAGMENT_ENTRY ... OUTPUT_DIR ...
EXPECTED_CONTRACT ...)`).

**Where the function lives — resolved, not left as a new directory-level
pattern.** This repository's existing root `cmake/` directory
(`CompilerWarnings.cmake`, `AtlantisDependencies.cmake`) holds genuinely
repository-wide, ADR-backed helpers; a per-module `cmake/` subdirectory
has no precedent anywhere in this codebase. Rather than introduce that
new pattern for a single function with exactly one consumer this round,
**`atlantis_add_slang_shader_pair()` is defined directly inside
`src/shader_system/CMakeLists.txt` itself** — no separate `.cmake` file
is created. CMake functions defined in a processed `CMakeLists.txt`
remain callable by any directory processed later in the same configure
run, so `shaders/minimal_renderer/CMakeLists.txt` (Section 8) can call it
directly, **provided the root `CMakeLists.txt`'s
`add_subdirectory(src/shader_system)` call precedes its
`add_subdirectory(shaders/minimal_renderer)` call** — this ordering
requirement is fixed here, not left implicit, and is one of §11's
Verification Checklist items. (This removes
`src/shader_system/cmake/AtlantisShaderSystem.cmake` from §1's Files to
Create list — see the corrected list there.)

```cmake
# Inside src/shader_system/CMakeLists.txt, after the atlantis_shader_system
# and atlantis_shader_system_rhi_integration target declarations (Section 1/6).
function(atlantis_add_slang_shader_pair)
  # ... NAME/SOURCE/VERTEX_ENTRY/FRAGMENT_ENTRY/OUTPUT_DIR/EXPECTED_CONTRACT
  # argument parsing (cmake_parse_arguments), a Plan-stage mechanical detail.

  set(stamp "${OUTPUT_DIR}/${NAME}.stamp")
  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS
      "${OUTPUT_DIR}/${NAME}.vert.spv" "${OUTPUT_DIR}/${NAME}.vert.refl.json"
      "${OUTPUT_DIR}/${NAME}.frag.spv" "${OUTPUT_DIR}/${NAME}.frag.refl.json"
    COMMAND atlantis_shader_compiler
      --slangc-path=${ATLANTIS_SLANGC_EXECUTABLE}
      --spirv-val-path=${ATLANTIS_SPIRV_VAL_EXECUTABLE}
      --source=${SOURCE} --vertex-entry=${VERTEX_ENTRY} --fragment-entry=${FRAGMENT_ENTRY}
      --output-dir=${OUTPUT_DIR} --expected-contract=${EXPECTED_CONTRACT}
      --stamp=${stamp}
    DEPENDS ${SOURCE} atlantis_shader_compiler
    COMMENT "Compiling and validating Slang shader pair: ${NAME}"
    VERBATIM
  )
  add_custom_target(${NAME}_shaders ALL DEPENDS "${stamp}")
  set(ATLANTIS_${NAME}_SHADER_OUTPUT_DIR "${OUTPUT_DIR}" PARENT_SCOPE)
endfunction()
```

- **The `OUTPUT` is a single stamp file, not the four real artifacts —
  this is the load-bearing design decision this revision makes
  explicit**, not merely an implementation nicety: CMake's own dependency
  graph only ever asks "does `${NAME}.stamp` exist and is it newer than
  `DEPENDS`" to decide whether this custom command needs to (re)run.
  `${NAME}_shaders` (the target every consumer depends on, transitively —
  Section 1's `PRE_TEST`/`POST_BUILD` consumer wiring) itself depends
  only on the stamp. The four real files are declared as `BYPRODUCTS` —
  per the [official CMake documentation](https://cmake.org/cmake/help/latest/command/add_custom_command.html)'s
  own definition, "the files the command is expected to produce but
  whose modification time may or may not be newer than the
  dependencies" — which is exactly this shape: Ninja/Makefile generators
  need to know these files exist (for their own dependency-graph
  bookkeeping and `clean` support) without treating *their* individual
  timestamps as the staleness signal.
- `DEPENDS ${SOURCE}` (unchanged from the original design) gives correct
  incremental rebuild: editing `minimal_mesh.slang` makes the stamp
  stale, triggering exactly this one re-compilation on the next build.
- `DEPENDS atlantis_shader_compiler` (the target) makes CMake rebuild the
  Tools executable first if its own sources changed (ADR-0029's "build a
  tool, then use it" idiom, unchanged).
- The single `atlantis_shader_compiler` invocation still compiles **both**
  stages internally, unchanged from the original design (Section 5's own
  step 12 cross-stage check needs both results in one process
  invocation).

**Publish transaction — the exact algorithm CMake's stamp model above
depends on**, implemented inside `compile_and_validate.cpp` (Section 5's
own step 14 references this):

```
14a. Delete any pre-existing stamp at ${stamp} first, defensively --
     ensures a prior run that somehow left a stamp without this run's
     own fresh validation cannot be mistaken for "still current" if
     anything below fails partway (CMake itself already would not have
     invoked this command unless the stamp was missing/stale, but this
     is a cheap, explicit belt-and-suspenders guarantee, not reliance on
     that alone).
14b. Compile/reflect/validate both stages ENTIRELY into a per-invocation-
     unique temporary directory: "${OUTPUT_DIR}/.tmp-${NAME}-<pid>-<a
     monotonically-increasing counter or high-resolution-clock value>/"
     -- unique per invocation so ordinary parallel builds of DIFFERENT
     shader pairs (different NAME values) never collide; a single NAME's
     own custom command is never invoked twice concurrently by
     construction (CMake serializes a given OUTPUT-producing command
     against itself). Nothing under OUTPUT_DIR's own final paths is
     touched during this step.
14c. Only once ALL of: both slangc compiles (step 3), both transforms
     (step 5), the descriptor-contract check (step 7), the push-constant
     check (step 8), the cross-stage check (step 12), and both spirv-val
     runs (step 13) have succeeded for BOTH stages -- proceed to publish;
     otherwise skip directly to 14f (cleanup) and exit non-zero.
14d. Publish: for each of the four final artifact paths (vert.spv,
     vert.refl.json, frag.spv, frag.refl.json), std::filesystem::rename()
     the corresponding temp-directory file to its final OUTPUT_DIR path
     (same-volume rename, atomic per-file). If any single rename fails
     partway through the four: stop immediately, proceed to 14f, exit
     non-zero -- the stamp is never written (14e is never reached).
14e. Only after all four renames in 14d succeeded: write the stamp
     itself via the same temp-then-rename pattern (content is a plain
     text record of sdkProvenance, useful for human debugging but never
     parsed back by any Atlantis code -- the stamp's mere EXISTENCE, not
     its content, is what CMake's own dependency tracking checks).
14f. Cleanup (runs on every exit path, success or failure): remove the
     entire per-invocation temp directory from 14b. On a failure path
     reached from 14d (a partial publish), additionally best-effort-
     remove any of the four final-path files that step 14d successfully
     renamed in THIS invocation before failing -- "best effort" because
     if this removal itself fails, the process still exits non-zero and
     still has not written a stamp, so the guarantee below still holds
     regardless of whether this specific cleanup step fully succeeds.
```

**Why a consumer can never observe a half-updated pair.** CMake's own
build-order guarantee (a target depending on `${NAME}_shaders` — which
itself depends on the stamp `OUTPUT` — is never scheduled to build until
that `OUTPUT` exists) means no consumer (the demo's `POST_BUILD` copy
step, a GPU test) ever runs concurrently with, or before, a still-in-
progress or failed publish — by the time any consumer runs, the stamp
already exists, which by 14e's own ordering is only possible if all four
real files were already successfully published first. **Why the next
build always retries a failure.** A failed run (per 14c/14d/14f above)
never writes the stamp; CMake therefore always considers this `OUTPUT`
missing/stale on the next build invocation and re-runs the full command —
never treats a missing stamp as "nothing to do." A partially-published
file left behind by a best-effort cleanup failure in 14f does not change
this: the *stamp's* absence, not the four files' own state, is what
governs whether CMake reruns the command, and a rerun's own 14d simply
overwrites whatever stale partial files remain with freshly-published
ones once it succeeds again.

**Multi-config generator support boundary — a fixed Phase 1 policy, not
left to Implementation-time discovery.** Per the
[official CMake documentation](https://cmake.org/cmake/help/latest/command/add_custom_command.html)'s
own "Generating Files" guidance: *"Do not list the output in more than
one independent target that may build in parallel or the instances of
the rule may conflict,"* with configuration-scoped (`$<CONFIG>`-suffixed)
output paths named as the standard mitigation for multi-config
generators specifically. This Plan's design already avoids the literal
documented anti-pattern (the stamp `OUTPUT` is declared by exactly one
`add_custom_command()`, consumed by exactly one `add_custom_target()` —
never duplicated across independent targets). What remains genuinely
unconfirmed by official documentation is the narrower question of
whether that *one* target, realized as one `.vcxproj` per configuration
under Visual Studio, is safe when **multiple configurations of the same
binary tree are built concurrently by independent, simultaneously-running
build processes** (e.g. two terminal sessions each running
`cmake --build . --config Debug` / `--config Release` against the same
tree at the same time, or a single `msbuild /m` invocation that
parallelizes multiple project configurations together). Rather than
leave this open, this Plan fixes the supported workflow explicitly:

- **Exactly one, configuration-independent shader-artifact producer
  exists per binary tree** (unchanged from the original design; ADR-0031
  is not reopened).
- **Every configuration's consumer targets depend on that same,
  single producer.**
- **Ordinary parallel compilation *within* one configuration** (e.g.
  MSBuild's own `/m` parallelizing multiple `.cpp` files of the *same*
  configuration) is unaffected by anything above and remains safe —
  nothing about this design touches per-file, same-configuration
  parallelism.
- **Building different configurations of the *same* binary tree must be
  sequential, not concurrent, within Phase 1** — e.g. `cmake --build .
  --config Debug` completing before `cmake --build . --config Release`
  begins (or vice versa), whether invoked manually, via a script, or via
  Visual Studio's own "Batch Build" run one configuration at a time.
  This is safe under CMake's own ordinary, well-established staleness
  tracking (not a new or unverified mechanism): the second configuration's
  build checks the same, configuration-independent stamp `OUTPUT`,
  finds it already up to date relative to `DEPENDS`, and skips
  re-running the command entirely — exactly the same "second build is a
  no-op" behavior every other `add_custom_command()` in this build
  already relies on.
- **Two independent build processes concurrently building different
  configurations of the *same* binary tree is explicitly an unsupported
  Phase 1 workflow.** A contributor who wants to build Debug and Release
  at the same time must use two separate binary trees (e.g. `-B
  build-debug` and `-B build-release`) — an already-normal, low-cost
  CMake practice this Plan does not need to invent anything new to
  support.
- **§9/§10/§11 (Testing, Implementation Order, Verification Checklist)
  each gain an explicit, sequential-build regression test** — building
  Debug then Release (and, separately, Release then Debug) in the same
  binary tree, confirming the second configuration's build does not
  redundantly recompile the shader pair and does not corrupt or
  regenerate its artifacts.
- **If Implementation nonetheless observes a conflicting-rule symptom
  under this *supported* (sequential-only) workflow** — not the
  explicitly-unsupported concurrent-cross-config case above — **this is
  a Human Review Blocker** (§12/§13): it would mean CMake's own ordinary
  staleness tracking does not behave as documented for this specific
  shape, and Implementation must stop and return to Plan/ADR review
  rather than silently switch to per-configuration authoritative
  artifacts (which would conflict with `Accepted` ADR-0031) or invent a
  cross-process locking mechanism (a new concurrency primitive this Plan
  does not otherwise need).
- **This Plan does not claim CMake provides cross-process serialization
  no official documentation confirms** — the "unsupported" bullet above
  is a stated *workflow* boundary (contributors must not do this), not a
  claim that attempting it is verified safe or unsafe; it is simply out
  of this Plan's own supported scope, exactly the same way this
  repository already leaves "two independent `cmake` invocations racing
  against the same build tree for unrelated reasons" unsupported without
  needing to say so for every other target.

**Consumer artifact discovery — no hardcoded absolute path.** Every
consuming CMake target (the demo, the GPU test) references the shader
output directory via `ATLANTIS_${NAME}_SHADER_OUTPUT_DIR` (a variable
`atlantis_add_slang_shader_pair()` itself sets via `PARENT_SCOPE`, per
the function body above) — never a string literal path a developer typed
by hand, matching ADR-0031's own rule — and adds an explicit
`add_dependencies(<consumer target> ${NAME}_shaders)` so CMake's own
build-order guarantee (above) actually applies to it, not merely to
targets that happen to depend on the produced files by path convention.
The demo's/test's own existing `POST_BUILD` `copy_if_different` step
(Section 1's Files to Modify) is retargeted from
`shaders/minimal_renderer/*.spv` (source tree, checked-in) to this
build-tree variable (generated, not checked in) — the copy mechanism
itself (copy next to the consumer's own executable) is **unchanged**,
only its source path changes.

**`.gitignore`:** one line added,
`/build*/shaders/` (or the exact build-tree shader output pattern,
matched to whatever `${CMAKE_BINARY_DIR}` convention this repository's
existing `.gitignore` already uses for other build products — a
Plan-stage mechanical detail).

**Debug/Release share one artifact set** (Section 1's `OUTPUT_DIR` has no
`$<CONFIG>` component) because shader compilation reads no
`CMAKE_BUILD_TYPE`/`$<CONFIG>` value and produces no configuration-
dependent bytes — restated from ADR-0029/0031, not re-decided.

## 8. Minimal Renderer Migration

One atomic sequence of implementation steps (not one atomic commit —
Section 9's implementation order sequences it across several reviewable
steps), executed only after §1–§7's new modules are implemented and
independently tested:

1. **Add `shaders/minimal_renderer/minimal_mesh.slang`** — a single
   Slang module containing both `vertexMain`/`fragmentMain` entry
   points, sharing one explicitly-declared varying-interface `struct`
   (ADR-0030's authoring convention), functionally equivalent to today's
   `minimal_mesh.{vert,frag}.glsl` pair (camera uniform at
   `[[vk::binding(0,0)]]`, push-constant object-to-world matrix,
   position/color vertex inputs at explicit `[[vk::location(0/1)]]`,
   unlit per-vertex-color output) — content informed by, but not
   required to byte-match, Spec 0008's own Validation Evidence
   experiment shader.
2. **Add `shaders/minimal_renderer/CMakeLists.txt`**, calling
   `atlantis_add_slang_shader_pair()` (Section 7) with
   `EXPECTED_CONTRACT=minimal-renderer`; `include()`'d from the root
   `CMakeLists.txt` (a new `add_subdirectory(shaders/minimal_renderer)`
   entry, or an `include()` call — Plan-stage mechanical detail).
3. **Update `examples/minimal_renderer_demo/main.cpp`**: `loadSpirvFile()`
   calls now target the build-tree-copied `.spv` files (path unchanged
   in *shape*, since the `POST_BUILD` copy still lands them at
   `shaders/minimal_mesh.{vert,frag}.spv` relative to the executable,
   Section 7); `PipelineCreateParams::vertexInputLayout` is now built via
   `ShaderSystemRhiIntegration::toVertexInputLayout()` (loading each
   stage's `ReflectionMetadata` via `loadReflectionMetadata()` against
   the copied `.refl.json` files) instead of the existing hand-written
   `minimalMeshVertexLayout()` literal — which is **deleted**, not kept
   as a parallel/fallback path.
4. **Update `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`**: the
   identical call-site change as step 3.
5. **Update both `CMakeLists.txt` files' `POST_BUILD` copy commands**
   (Section 1's Files to Modify) to copy from the new build-tree source
   directory instead of `shaders/minimal_renderer/*.spv`.
6. **Delete** `shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl`
   and `.spv`, and rewrite `shaders/minimal_renderer/README.md` to
   describe the new Slang source + build-tree-artifact model, retiring
   the manual `glslc` regeneration instructions
   ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
   own migration boundary, now exercised).
7. **No two parallel, simultaneously-authoritative shader-sourcing
   mechanisms exist after this step** — confirmed by inspection (no
   remaining reference to a checked-in `.spv` anywhere in
   `examples/`/`tests/`) as part of this Plan's own Verification
   Checklist (§11).

**What this migration does not do** (Spec 0008 Non-Goals, restated):
introduce a second material, a texture, lighting, or any change to the
mesh geometry/camera behavior itself — the visual output is expected to
be pixel-for-pixel unchanged from Spec 0007's own verified output,
because the shader's own logic is unchanged, only its sourcing mechanism
is.

## 9. Testing Plan

### GPU-independent (`ctest -LE gpu` and no `tool` label, per below)

- **`json_parser_tests.cpp`:** valid-JSON round-trip (object/array/
  string/number/bool/null nesting); string escapes including `\uXXXX`;
  malformed input (unterminated string, trailing garbage, duplicate
  keys); numbers at typical small-integer values used by this module's
  own fields.
- **`reflection_metadata_tests.cpp`:** `loadReflectionMetadata()`/
  `saveReflectionMetadata()` round-trip on a fixture value; missing
  required field → `MissingRequiredField`; unknown extra field → ignored,
  not an error; `schemaVersion` newer than `kCurrentSchemaVersion` →
  `UnsupportedSchemaVersion`.
- **`slang_json_transform_tests.cpp`:** the real captured Spec 0008
  Validation Evidence sample transforms to the expected
  `ReflectionMetadata` value exactly; the module-level-`"parameters"`-vs-
  entry-point-`"bindings"`-`"used"`-filtering fixture; the set-0-implicit
  fixture (absent `"space"` → `set = 0`); **the nonzero-set positive
  fixture** — the exact `{"kind": "descriptorTableSlot", "space": 2,
  "index": 3}` shape ADR-0030 already recorded — transforms successfully
  to `{set: 2, binding: 3}` in the resulting `ReflectionMetadata` (this
  is a parser-level, expected-to-*succeed* case, per Section 2/3 — not a
  failure case); a malformed-`"space"`/`"index"`-value negative fixture
  (non-integer/negative/out-of-range) → a resource-limit parse failure,
  kept distinct from the nonzero-set positive case above; a push-constant
  fixture explicitly cross-checked for offset/size correctness (the
  issue #5676 regression case); malformed/unexpected-structure fixtures
  → the matching `TransformError` variant, never a crash or a best-effort
  partial result; the four resource limits (Section 3) each individually
  exercised (an input exceeding each one → the matching parse error, not
  a crash or a hang).
- **`descriptor_contract_tests.cpp`:** exact-match fixture ({set: 0,
  binding: 0}) → `Ok`; **the nonzero-set fixture from
  `slang_json_transform_tests.cpp` above, fed into
  `validateDescriptorContract()` against
  `minimalRendererExpectedDescriptorContract()`, asserts the specific
  resulting `ContractMismatchError`** (`BindingNotFound`/
  `UnexpectedExtraBinding`) — the second half of the parser-vs-contract
  split Section 2/3 both require; every other `ContractMismatchError`
  variant individually exercised (wrong count, wrong type, wrong stage).
- **`command_line_tests.cpp`:** `buildSlangcArgv()`'s output contains
  `-profile spirv_1_0`, `-warnings-disable 50011`, and **never**
  `-fvk-use-entrypoint-name`, for every fixture request; `buildSpirvValArgv()`'s
  output contains `--target-env vulkan1.0`; a source/output path
  containing a space produces a correctly-quotable argv entry (not a
  pre-quoted string — quoting itself is `process_launch.cpp`'s job,
  tested separately below).
- **`version_provenance_tests.cpp`:** a fixture directory tree with a
  `slang-standard-module-<version>` sibling directory → that version
  string returned; its absence → `std::nullopt`, not an error.
- **`rhi_integration/vertex_input_mapping_tests.cpp`** (links
  `Atlantis::RHI`, no Vulkan device needed — pure data mapping):
  matching-location fixture → correct `VertexInputLayout`; a schema
  entry with no matching reflected location → `LocationNotFoundInSchema`;
  a reflected location with no matching schema entry →
  `AttributeCountMismatch`/`LocationNotFoundInSchema` (exact variant a
  Plan-stage detail); `toPushConstantSize()` sums correctly, including
  the zero-ranges case.
- **`tests/tools/shader_compiler/process_launch_tests.cpp`:** the
  argv-vector-to-Windows-command-line-string quoting algorithm (Section
  4) exercised as a pure function, in isolation, against every documented
  edge case — an argument with an embedded space, an embedded double
  quote, a trailing backslash immediately before the closing quote, an
  empty-string argument, and a path under a directory whose own name
  contains a space — asserting the exact expected quoted output for
  each, not merely "does not crash." A genuinely-nonexistent executable
  path → `ExecutableNotFound`, exercised via a real (but trivially fast)
  `launchProcess()` call — this specific case needs no real tool and no
  process spawn to observe, since Section 4 fixes it as a
  `std::filesystem::exists()` check performed before `CreateProcessW` is
  ever called. A real, trivial, always-present Windows executable (e.g.
  `cmd.exe /c exit 0` / `cmd.exe /c exit 3`, or an equivalently simple
  fixture binary) is used to exercise `launchProcess()`'s full happy
  path end-to-end — successful launch, correct `exitCode` capture for
  both a zero and a nonzero exit, and `diagnostics` correctly capturing
  text the fixture process writes to both stdout and stderr (confirming
  the single-file, combined-capture design, Section 4, actually merges
  both streams as intended) — this does not require the Vulkan SDK and
  stays in the GPU-independent, no-`tool`-label category, since `cmd.exe`
  is a standard Windows component, not an SDK-provided tool.

### Tool integration (`ctest -L tool` — a new label; needs the real
Vulkan-SDK-provided `slangc`/`spirv-val` on the build machine, but **no**
GPU/Vulkan device — distinct from the existing `gpu` label per
[testing-strategy.md](../docs/process/testing-strategy.md)'s own
still-open "is a GPU-dependent-but-not-strictly-GPU category needed"
question, now answered concretely for this Plan's own scope)

- `tests/tools/shader_compiler/toolchain_integration_tests.cpp`:
  - A real `slangc` compile of a small fixture `.slang` file with
    `-profile spirv_1_0` succeeds, and the resulting `.spv`'s own
    `spirv-dis`-visible (or byte-inspected, without requiring
    `spirv-dis` itself as a test-time dependency — a Plan-stage choice
    between the two) `Version: 1.0` header is present.
  - Missing `-profile` (simulating an accidental future regression back
    to relying on `slangc`'s own default) demonstrably emits a
    **different** SPIR-V version — the concrete regression test for
    ADR-0028's own disclosed "default is 1.5" finding.
  - A real `spirv-val --target-env vulkan1.0` run against that compiled
    artifact exits 0.
  - `-warnings-disable 50011` demonstrably suppresses `E50011`'s stderr
    text on a fixture that would otherwise emit it, while producing a
    byte-identical `.spv` to the unsuppressed run (Spec 0008's own
    Validation Evidence already observed this locally; this test makes
    it a standing regression check).
  - `-fvk-use-entrypoint-name` is never present in any argv this test
    inspects across every code path that reaches `buildSlangcArgv()`.
  - A deliberately-invalid `.slang` fixture fails the real `slangc`
    invocation with a non-zero exit code, and `compile_and_validate.cpp`
    propagates that failure — **no stamp file is created, and none of
    the four final artifact paths exist afterward** (Section 7's publish
    transaction, step 14c short-circuits before any temp-directory
    content is ever renamed to a final path).
  - **Partial-publish recovery**: a fixture that fails specifically
    during Section 7's step 14d (simulated by making one of the four
    final-path directories read-only, or an equivalent injectable
    failure point — a Plan-stage test-harness detail) leaves **no**
    stamp file, and a subsequent, ordinary re-run (with the injected
    failure removed) succeeds and produces a complete, correct four-file
    set plus a stamp — the concrete regression test for Section 7's own
    "next build always retries a failure" guarantee.
  - `atlantis_shader_compiler`, run twice on identical input/flags,
    produces byte-identical `.spv` and reflection-JSON output — the
    Plan's own standing regression test for Spec 0008's own local
    (not vendor-guaranteed) determinism observation.
  - Incremental rebuild: touching the `.slang` source and re-running
    `cmake --build` recompiles exactly the affected shader pair (the
    stamp's timestamp advances); a no-op second build recompiles nothing
    (the stamp is untouched — verified by build-log/timestamp inspection,
    mirroring `testing-strategy.md`'s own build/tool-integration layer).
  - **Sequential Debug→Release and Release→Debug** (§7's multi-config
    support boundary): building Debug to completion, then building
    Release in the *same* binary tree (and, as a separate case, the
    reverse order) — the second configuration's build does not
    re-invoke `atlantis_shader_compiler` for an unchanged shader pair
    (the stamp is already up to date), and both configurations' own
    consumers (the demo, the GPU test) successfully locate and load the
    one shared artifact set. This is the concrete regression test for
    §7's supported-workflow guarantee — run manually/via script during
    Implementation (§10's own gate for this step), not necessarily a
    standing CTest case if CTest's own multi-config invocation model
    makes that awkward to express (a Plan-stage detail; a standing CTest
    case is preferred if practical).
  - Missing `slangc`/`spirv-val` (simulated by pointing `--slangc-path`/
    `--spirv-val-path` at a nonexistent file) fails
    `atlantis_shader_compiler` itself cleanly — this test exercises the
    Tools executable's own defensive handling, distinct from (and in
    addition to) the CMake-configure-time `find_program()`/
    `FATAL_ERROR` check (Section 7), which this specific test cannot
    exercise directly (configure already happened by the time any test
    runs).

### GPU-required (`ctest -L gpu`, unchanged label, extended coverage)

- `minimal_renderer_gpu_tests.cpp` (post-migration): the Slang-compiled,
  build-tree `minimal_mesh.{vert,frag}.spv`/`.refl.json` successfully
  back a real `Device::createPipeline()` call; Vulkan Validation Layers
  report zero warnings/errors; a multi-draw-item frame (unchanged from
  Spec 0007's own existing case) still produces correct per-item
  transforms. **This test is also this Plan's own designated regression
  test for the `vulkan_device.cpp`-hard-coded-layout-vs-
  `minimalRendererExpectedDescriptorContract()` duplication risk (§2's
  own disclosed, accepted single-source-of-truth gap)**: because it
  exercises a real `VkPipeline` built from the Vulkan Backend's actual,
  unchanged hard-coded binding layout, using a shader Shader System has
  already build-time-validated against its own separate, hand-kept-in-
  sync copy of that same layout, any future drift between the two
  (someone edits one without the other) surfaces here as either a real
  Vulkan Validation Layers error/warning (a genuine binding mismatch) or
  a pipeline-creation failure — not silently. This does not eliminate
  the duplication (see §13's own PHR-0008-07 entry for the full,
  honestly-bounded statement of this risk), but it is the concrete,
  already-existing test that would actually catch it in practice, and
  this Plan now says so explicitly rather than leaving that connection
  implicit.
- **Manual verification** (`examples/minimal_renderer_demo`, post-
  migration): a visible, correctly-shaded, correctly depth-ordered mesh,
  matching Spec 0007's own already-verified output, confirming the
  migration changed *how* the shader artifact is sourced without
  changing *what* is rendered; resize/minimize/restore/close behavior
  unchanged; Debug and Release builds both exercised.

### Explicitly not automated (stated, not silently assumed covered)

- **Two independent, concurrently-running build processes building
  different configurations of the same binary tree** — this is an
  explicitly *unsupported* Phase 1 workflow (§7), not merely an untested
  one; there is no reliable, portable way to force this specific
  scenario deterministically in an automated test, and this Plan does
  not attempt to support or verify it. Contributors needing concurrent
  Debug/Release builds use separate binary trees (§7).
- A real Slang/Vulkan-SDK version genuinely different from the one this
  Plan's own fixtures were captured against (Section 3's own SDK-upgrade
  re-verification requirement) — by construction, cannot be exercised
  until such an upgrade actually happens.

(The nonzero-descriptor-set JSON shape is **no longer in this list** —
it is now covered by a positive fixture in `slang_json_transform_tests.cpp`
and a contract-rejection fixture in `descriptor_contract_tests.cpp`,
both reusing ADR-0030's own already-recorded, `[JSON-verified]` sample —
see this section's own entries above.)

---

## 10. Implementation Order

Each step is independently buildable/testable and gated on the previous
step's own tests passing before the next begins, per
[AGENTS.md](../AGENTS.md)'s "build and run tests after every
implementation step" rule:

1. **CMake tool discovery + module skeletons.** `find_program()`
   guards (Section 7); empty `src/shader_system/` and
   `src/tools/shader_compiler/` targets (headers/stubs only, no logic)
   that link correctly and build clean under `/W4 /WX`. **Gate:** clean
   configure/build; a deliberately-broken `VULKAN_SDK` environment
   variable reproduces the `FATAL_ERROR` messages from Section 7.
2. **JSON parser + `ReflectionMetadata`/loader.** Section 3's grammar;
   Section 2's types; `reflection_loader.cpp`. **Gate:**
   `json_parser_tests.cpp` and `reflection_metadata_tests.cpp` green.
3. **`slang_json_transform.cpp` against captured fixtures.** No real
   `slangc` invocation needed yet — fixtures are static JSON files
   checked into `tests/shader_system/fixtures/` (new directory). **Gate:**
   `slang_json_transform_tests.cpp` green, including the issue-#5676
   push-constant regression fixture.
4. **`descriptor_contract.cpp` + `command_line.cpp` + `version_provenance.cpp`.**
   **Gate:** their own unit tests green.
5. **`process_launch.cpp` (Tools)**, implementing Section 4's full
   `CreateProcessW` design (resolved `lpApplicationName`, mutable
   `lpCommandLine` buffer, single-temp-file diagnostic capture,
   `PROCESS_INFORMATION` RAII guard). **Gate:** `process_launch_tests.cpp`
   green, including the `cmd.exe`-based end-to-end happy-path case
   (Section 9) — this is the first step exercising a real
   `CreateProcessW` call, but still needs no Vulkan SDK.
6. **`compile_and_validate.cpp` (Tools) wired to a real `slangc`/
   `spirv-val`**, implementing Section 7's full publish-transaction
   algorithm (14a–14f). This is the first step that actually needs the
   real Vulkan SDK toolchain. **Gate:** `toolchain_integration_tests.cpp`
   green against a small, throwaway fixture `.slang` file (not yet
   Minimal Renderer's own shader) — including the partial-publish-
   recovery check and the determinism regression check.
7. **`atlantis_add_slang_shader_pair()` (defined inside
   `src/shader_system/CMakeLists.txt`, Section 7) + end-to-end
   build-tree pipeline**, still against the throwaway fixture shader.
   **Gate:** a clean build produces the stamp plus the expected
   `.spv`/`.refl.json` pair (as `BYPRODUCTS`) at the expected build-tree
   path; incremental-rebuild and no-op-rebuild behavior verified; **the
   sequential Debug→Release and Release→Debug regression test (Section 9)
   is run and recorded here** — per Section 7's fixed supported-workflow
   boundary, this is a required gate, not an optional or deferred
   verification. If this specific, *sequential*, single-process test
   reveals a conflicting-rule symptom, Implementation stops and returns
   to Plan/ADR review per Section 12/13 — it does not silently switch to
   per-configuration artifacts.
8. **`ShaderSystemRhiIntegration` (`vertex_input_mapping.cpp`).**
   **Gate:** `vertex_input_mapping_tests.cpp` green.
9. **Minimal Renderer migration (Section 8), in its own seven sub-steps.**
   **Gate:** `minimal_renderer_gpu_tests.cpp` green on real hardware,
   Validation Layers clean; manual demo verification performed and
   recorded (visual output, resize, minimize/restore, close, Debug and
   Release); no remaining reference to the checked-in `.spv`/`.glsl`
   pair anywhere in the tree.
10. **Full-suite verification and PR write-up.** Every GPU-independent,
    `tool`-labeled, and `gpu`-labeled test green on both Debug and
    Release; §11's Verification Checklist walked item by item in the
    implementation PR's own description; any deviation from this Plan
    called out explicitly, per [AGENTS.md](../AGENTS.md).

---

## 11. Verification Checklist

- [ ] `Atlantis::RHI`'s public headers are byte-for-byte unchanged by
      this Plan's implementation (verifiable by `git diff` against
      `main` touching zero files under `src/rhi/include/`).
- [ ] No `Vk*` type, no Slang type (`Slang::ComPtr`, `slang::*`), and no
      Windows process-handle type (`HANDLE`, `PROCESS_INFORMATION`)
      appears in any public header of `Atlantis::ShaderSystem` or
      `Atlantis::ShaderSystemRhiIntegration` — verifiable by inspection/
      grep.
- [ ] `atlantis_shader_system`'s `CMakeLists.txt` links only
      `Atlantis::Core` — verifiable by inspection.
- [ ] `atlantis_shader_system_rhi_integration` is the *only* target in
      the repository depending on both `Atlantis::ShaderSystem` and
      `Atlantis::RHI` — verifiable by grepping every `CMakeLists.txt`.
- [ ] No `add_subdirectory()` for a Shader-System-RHI-integration-named
      directory appears in the root `CMakeLists.txt` — it is declared
      inside `src/shader_system/CMakeLists.txt` only.
- [ ] `AGENTS.md` and `docs/architecture/module_boundaries.md` are
      byte-for-byte unchanged.
- [ ] No `FetchContent_Declare()`/new `find_package()` for any JSON
      library appears anywhere in the diff.
- [ ] `buildSlangcArgv()`'s output for every test fixture contains
      `-profile spirv_1_0`, contains `-warnings-disable 50011`, and
      never contains `-fvk-use-entrypoint-name`.
- [ ] `spirv-val --target-env vulkan1.0` is invoked, and its exit code
      checked, for every emitted `.spv` — verifiable by inspection of
      `compile_and_validate.cpp` and by the `toolchain_integration_tests.cpp`
      suite passing.
- [ ] A missing `slangc` or `spirv-val` fails CMake **configure**
      (`FATAL_ERROR`), not build or runtime — verified manually per
      Step 1's implementation gate.
- [ ] `vulkan_device.cpp`'s `pName = "main"` and its hard-coded
      descriptor-binding layout are byte-for-byte unchanged by this
      Plan.
- [ ] `descriptorContract` validation runs at **build** time (inside
      `atlantis_shader_compiler`), not deferred to program startup or
      per-frame — verifiable by inspection that
      `validateDescriptorContract()` is called nowhere in
      `src/shader_system/rhi_integration/` or any demo/test's own
      per-frame code path.
- [ ] No file under `shaders/minimal_renderer/` is a checked-in `.spv`
      or `.glsl` file after Section 8's migration completes —
      verifiable by `git ls-files shaders/minimal_renderer/`.
- [ ] `examples/minimal_renderer_demo` and
      `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` load shader
      artifacts exclusively from the build-tree location Section 7
      defines — no remaining reference to `shaders/minimal_renderer/*.spv`
      as a source path.
- [ ] Every `VkResult`-adjacent failure this Plan's own new code
      surfaces (via `launchProcess()`'s captured exit codes,
      `slangc`/`spirv-val`'s own stderr) is checked and propagated —
      no discarded exit code.
- [ ] Debug and Release builds both succeed, sharing one shader artifact
      set with no redundant recompilation, **built sequentially in the
      same binary tree** (Section 7's supported-workflow boundary) — the
      concurrent-cross-config case remains explicitly unsupported and is
      not verified.
- [ ] A shader pair's `OUTPUT` is a single stamp file; `.spv`/reflection-
      JSON are declared `BYPRODUCTS` — verifiable by inspection of
      `atlantis_add_slang_shader_pair()`'s own `add_custom_command()`
      call.
- [ ] A failed compile/reflect/validate run leaves no stamp file and no
      partially-published final artifact set behind — verified by the
      partial-publish-recovery test (Section 9) passing.
- [ ] `slang_json_transform.cpp` parses a nonzero descriptor `"space"`
      value successfully (never fails closed on it), and
      `validateDescriptorContract()` separately rejects it against
      Minimal Renderer's `{set: 0, binding: 0}`-only expected contract —
      both halves verified by `slang_json_transform_tests.cpp`/
      `descriptor_contract_tests.cpp` passing (Section 9).
- [ ] `launchProcess()` passes the resolved executable path via
      `lpApplicationName` (never `NULL`), builds `lpCommandLine` into an
      owned, mutable `std::wstring` buffer (never a string literal or a
      `const`-sourced pointer), and captures combined stdout+stderr via
      a single temporary file (never two pipes) — verifiable by
      inspection of `process_launch.cpp`.
- [ ] `STARTUPINFOW::dwFlags` includes `STARTF_USESTDHANDLES` before
      every `CreateProcessW` call in `process_launch.cpp`, and
      `hStdInput` is set to an explicit, inheritable NUL-device handle
      (never left unset) — verifiable by inspection, and by
      `process_launch_tests.cpp`'s `cmd.exe`-based happy-path case
      (Section 9) actually observing non-empty, correctly-captured
      `diagnostics` output rather than an empty string.
- [ ] `json_parser.cpp` enforces its four documented resource limits
      (input size, nesting depth, string length, element count) —
      verifiable by the corresponding `json_parser_tests.cpp` cases
      passing.
- [ ] `ctest -LE gpu` (excluding both `gpu` and `tool` labels, or a
      combined `-LE "gpu|tool"` expression, a Plan-stage CTest-label-
      syntax detail) passes with no Vulkan SDK or GPU present beyond
      whatever this repository's existing GPU-independent suite already
      needs.
- [ ] `ctest -L tool` passes on a machine with the Vulkan SDK installed
      but requires no GPU device.
- [ ] `ctest -L gpu` passes on real hardware, Vulkan Validation Layers
      clean throughout.
- [ ] `git diff --check` clean; no `.claude/` or unrelated file in the
      implementation diff.
- [ ] Every item in Spec 0008's own Testing & Verification Plan and
      Acceptance-shaped Requirements is traceable to a specific test
      or manual-verification step above (cross-checked against
      [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)
      directly during the implementation PR's own write-up, not
      re-enumerated a third time here).

## 12. Human Review Blockers and Deviation Rules

**Stop and return to Human Review (do not resolve unilaterally during
Implementation) if any of the following is discovered:**

- Linking Slang's compiler library becomes necessary for any reason
  (e.g. the CLI/`-reflection-json` path is found insufficient in
  practice) — this would reopen ADR-0029's CLI-vs-library decision.
- `-reflection-json`'s real field shapes, on further real-world exercise
  beyond Spec 0008's own single captured sample, are found to
  meaningfully disagree with Section 2/3's assumed structure in a way
  that cannot be resolved by extending `slang_json_transform.cpp`'s
  narrow, additive field-handling alone (i.e. it would require
  *removing* or *reinterpreting* an already-relied-upon field, not just
  adding support for a new one).
- Any change to `Device::createPipeline()`, `PipelineCreateParams`, or
  any other RHI public header is found necessary — this would reopen
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
- Any change to `vulkan_device.cpp`'s fixed descriptor-binding layout is
  found necessary (e.g. because a real material needs a second binding) —
  out of this Plan's own scope entirely (Spec 0008 fixed "validation
  only, never general layout construction").
- Preserving the source Slang entry-point name (rather than relying on
  the default rename-to-`"main"` behavior) is found necessary for any
  reason — this requires a new Spec/ADR changing RHI's/Vulkan Backend's
  `pName` contract, never a Plan-level or Implementation-level flag flip
  to `-fvk-use-entrypoint-name`.
- Raising the Vulkan Backend's physical-device selection floor above
  `VK_API_VERSION_1_0` is found necessary for any reason — this would
  reopen [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)
  and ADR-0028's Option A decision; Option B remains available as a
  future, separately-reviewed choice (per each ADR's own Alternatives
  Considered), never adopted silently here.
- `spirv-val` is found unable to meaningfully validate Slang's SPIR-V 1.0
  output for this Plan's own shaders (e.g. it reports a false pass on a
  genuinely broken artifact, or cannot be made to target `vulkan1.0`
  correctly) — this would undermine the risk-mitigation basis for
  Option A itself.
- `slangc` or `spirv-val` are found to not be reliably provided by the
  supported Vulkan SDK on some real development/CI machine — this would
  reopen ADR-0028's/ADR-0031's dependency-acquisition model.
- Any new top-level module, or any dependency edge not already drawn in
  this Plan's own §1 target graph (in particular, any `Atlantis::RHI` →
  `Atlantis::ShaderSystem` edge, or any `Atlantis::ShaderSystem` →
  `Atlantis::RHI` edge on the *core* library), is found necessary.
- The Windows-only `process_launch.cpp` implementation is found to
  meaningfully block or complicate a concrete, near-term Android-build
  host-tooling need (not merely a hypothetical future one) — Spec 0008
  itself anticipated this risk was low (SPIR-V compilation always
  happens on a host, never the Android target device) but did not treat
  it as fully resolved.
- The **sequential** Debug→Release/Release→Debug regression test
  (Section 9, gated at Step 7 of Section 10) demonstrates a genuine
  conflicting-rule symptom under Section 7's *supported* (single-process,
  sequential-build) workflow — as distinct from the concurrent-cross-
  config case, which is already, explicitly out of Phase 1's supported
  scope and requires no escalation if a contributor simply doesn't
  attempt it. A sequential-build failure would mean CMake's own ordinary
  staleness tracking does not behave as documented for this specific
  configuration-independent-`OUTPUT` shape, and would call into question
  whether ADR-0031's "configuration-independent artifact" decision
  remains viable at all — Implementation must stop rather than silently
  switch to per-configuration authoritative artifacts or invent a new
  concurrency primitive.

**Not blockers — Implementation may resolve these directly, calling the
deviation out in the implementation PR per [AGENTS.md](../AGENTS.md):**
exact file names, exact type/function names, exact JSON field-name
spellings (once confirmed against a real, freshly-captured Slang
sample), exact CMake variable/property names, the exact `--expected-
contract=` CLI-flag mechanism (Section 5) if a cleaner alternative
emerges during implementation, and any other detail this Plan's own
"Candidate-API Status" section already flags as non-binding.

---

## 13. Plan-Stage Design Decisions for Human Review

The decisions below are each a genuine Plan-stage design choice — none
changes an `Approved` Spec's or an `Accepted` ADR's own conclusions —
gathered here in one place so Human Review can confirm them explicitly,
rather than needing to reconstruct them from scattered detail across
§1–§12. Numbered to match this Plan's own prior review history for
traceability; gaps in the numbering (e.g. PHR-0008-01, -06, -08 through
-13) correspond to decisions already confirmed in that prior review round
and not reopened here.

- **PHR-0008-02 — Nonzero descriptor-set parsing and rejection.**
  `slang_json_transform.cpp` parses ANY reflected descriptor `"space"`
  value (0 or otherwise) successfully into `ReflectionMetadata` — this is
  a `[JSON-verified]` capability (ADR-0030), not a guess. Separately,
  `validateDescriptorContract()` rejects any set/binding pair outside
  Minimal Renderer's own fixed `{set: 0, binding: 0}` expectation. Parser
  capability and contract acceptance are two independent, independently-
  tested layers (§2, §3, §9).
- **PHR-0008-03 — `CreateProcessW` model.** Resolved executable path via
  `lpApplicationName` (never `NULL`); a mutable, owned `std::wstring`
  `lpCommandLine` buffer; a single temporary file (not two pipes) for
  combined stdout+stderr capture, with narrowly-scoped handle
  inheritance and full `PROCESS_INFORMATION`/file-handle RAII (§4).
- **PHR-0008-04 — Stamp-based artifact-pair transaction.** A single,
  configuration-independent stamp file is the sole CMake `OUTPUT`
  driving rebuild/staleness decisions; the four real artifacts are
  `BYPRODUCTS`, published via temp-directory-then-rename only after
  every validation step succeeds, with the stamp written strictly last
  and only after all four real files are already safely in place (§7).
- **PHR-0008-05 — Multi-config support boundary.** Exactly one,
  configuration-independent producer per binary tree; sequential (not
  concurrent) Debug/Release builds within one binary tree are supported
  and must be verified (§9/§10); two independent, concurrently-running
  build processes targeting different configurations of the *same*
  binary tree are an explicitly unsupported Phase 1 workflow, not a gap
  this Plan attempts to close (§7).
- **PHR-0008-07 — Descriptor-contract duplication risk and its
  regression backstop.** `vulkan_device.cpp`'s hard-coded binding layout
  and `minimalRendererExpectedDescriptorContract()` remain two, hand-
  kept-in-sync copies — this Plan does not eliminate that duplication
  (doing so would require an RHI API change outside this Plan's
  authorized scope). `minimal_renderer_gpu_tests.cpp` (already planned,
  §9) is this Plan's own designated regression backstop: a real
  `VkPipeline`, built from the Vulkan Backend's actual layout, using a
  Shader-System-validated shader, would surface drift as a real Vulkan
  Validation Layers error or pipeline-creation failure — not silently. A
  general, single-source-of-truth descriptor system remains explicitly
  future work, requiring its own Spec/ADR.
- **PHR-0008-14 — JSON parser resource limits.** Fixed, conservative,
  non-configurable constants (16 MiB input, 64-level nesting, 64 KiB
  strings, 4096 array/object elements — §3) bound the hand-rolled
  parser's worst-case behavior against malformed/adversarial input,
  without becoming a Core-wide configuration surface.
- **PHR-0008-15 — CMake helper placement.** `atlantis_add_slang_shader_pair()`
  is defined directly inside `src/shader_system/CMakeLists.txt` — no new
  per-module `cmake/` subdirectory pattern is introduced, and it is not
  placed in the repository's existing, genuinely cross-cutting root
  `cmake/` directory either, since it has exactly one consumer this
  round (§7). Human Review may prefer the root-`cmake/` placement instead
  if a second consumer is anticipated sooner than this Plan assumes —
  either choice is mechanical and does not change any file outside §1's
  own list either way.

---

## Sequencing & Dependencies

This Plan depends on nothing beyond what Spec 0008/ADR-0028–0031 already
established as `Accepted`. §1–§4 (module skeletons, JSON parser,
process-launch) can be implemented and tested with zero dependency on a
real Vulkan SDK. §5–§7 (the real `slangc`/`spirv-val` integration and
CMake pipeline) require the Vulkan SDK on the implementation machine.
§8 (Minimal Renderer migration) must follow §1–§7 completely — it is
explicitly sequenced last, per Spec 0008's own Non-Goals ("does not
implement the migration itself").

## Rollback Plan

Each numbered step in §10 is its own reviewable unit; a problem
discovered after step N merges is reverted by reverting that step's own
commit(s) — steps 1–8 do not modify any existing, previously-shipped
file (Minimal Renderer's own migration, step 9, is the only step
touching already-shipped files, and is itself the last step, minimizing
what a rollback there would need to touch). If step 9 (migration) is
reverted after landing, `shaders/minimal_renderer/`'s GLSL/`.spv` pair
and the demo's/test's own prior call sites are restored from version
control — the new Shader System module itself (§1–§8) is not required to
be reverted just because its first real consumer was rolled back.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: the "Image regression tests" item is N/A
(unchanged from every prior spec in this line — headless rendering does
not exist yet); the "Headless integration tests" item is likewise N/A.
Every other item applies as written, including the `tool`-labeled test
category this Plan introduces as a genuine, first-class CTest label
alongside the existing `gpu` label.
