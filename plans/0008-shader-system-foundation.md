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
                                            # no separate add_subdirectory() for the latter

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
CMakeLists.txt (root)                       # + add_subdirectory(src/shader_system)
                                            # + add_subdirectory(src/tools/shader_compiler)
                                            # + add_subdirectory(tests/shader_system) under
                                            #   ATLANTIS_BUILD_TESTS
                                            # + add_subdirectory(tests/tools/shader_compiler)
                                            #   under ATLANTIS_BUILD_TESTS
                                            # + find_program(...) guards for slangc/spirv-val
                                            #   (§7) -- configure-time FATAL_ERROR if missing

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
// Validation Evidence] set/binding is read from Slang's own
// "descriptorTableSlot" binding kind; this round's shaders use
// descriptor set 0 exclusively, so `set` defaults to 0 and is populated
// from an explicit JSON field only when Slang's own raw output supplies
// one (see slang_json_transform.cpp's own documented handling of a
// missing/absent "space"-equivalent field for set 0).
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

**Fixture-based coverage for known real-world shapes, per Spec 0008's own
Validation Evidence and disclosed risks:**

- A fixture matching the actual sample JSON captured during Spec 0008's
  validation experiment (descriptor set 0, no explicit `"space"`/`"set"`
  field present — `slang_json_transform.cpp` defaults to `set = 0` when
  that field is absent, per Section 2's own comment) — this is the
  **only** descriptor-set shape this Plan has direct evidence for.
- A **negative-signal fixture for a nonzero descriptor set**: since no
  real sample with `set != 0` was captured during validation, this Plan
  does not assume a specific JSON field name/shape for that case —
  `slang_json_transform.cpp` instead **fails closed**
  (`TransformError::UnexpectedStructure`) if it observes any explicit
  set/space-like field with a nonzero value it does not recognize,
  rather than silently guessing a mapping. Minimal Renderer's own
  shaders (Section 8) use set 0 exclusively, so this failure-closed
  behavior does not block this Plan's own scope — it is a deliberate,
  disclosed limitation for any future material using a nonzero set,
  flagged again in Section 10.
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

`src/tools/shader_compiler/process_launch.h` (private to this target):

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <atlantis/result.h>

namespace atlantis::tools::shader_compiler {

struct ProcessOutput {
  std::string stdOut;
  std::string stdErr;
  std::int32_t exitCode = 0;
};

enum class ProcessLaunchError {
  ExecutableNotFound,
  LaunchFailed,        // CreateProcessW itself failed (Windows API error)
  WaitFailed,
  OutputCaptureFailed,
};

// Windows-only (WIN32_LEAN_AND_MEAN, CreateProcessW). This is a Tools-
// internal implementation detail, never an Atlantis::Platform API and
// never a ShaderSystem type -- see Critical Architectural Boundaries.
// argv[0] is the executable path; argv[1:] are its arguments, passed as
// a genuine argument VECTOR, never concatenated into a single shell
// command string -- this is what makes a path containing spaces (e.g.
// "C:\Program Files\..." if a Vulkan SDK were ever installed there)
// safe without ad hoc quoting logic at this call site; the vector-to-
// Windows-command-line-string conversion this function performs
// internally follows the documented CommandLineToArgvW-compatible
// quoting rules exactly once, in one place, not re-derived per call
// site. Blocking; runs the child process to completion and captures its
// full stdout/stderr via anonymous pipes. No timeout and no
// cancellation this round (Non-Goals -- see Section 10 for why this is
// flagged, not silently assumed acceptable forever). Not thread-safe;
// caller-thread-only, matching this whole executable's single-threaded,
// single-invocation model (Section 1).
[[nodiscard]] atlantis::Result<ProcessOutput, ProcessLaunchError> launchProcess(
    const std::filesystem::path& executablePath, const std::vector<std::string>& arguments);

}  // namespace atlantis::tools::shader_compiler
```

**Timeout/cancellation — explicitly out of scope, not silently assumed
safe.** A hung `slangc`/`spirv-val` invocation would hang the whole
build with no recovery this round. This Plan accepts that risk rather
than design a timeout/cancellation mechanism with no observed real-world
need to size it against — flagged again in Section 10 as a documented,
accepted limitation, not a Human Review blocker (no architectural
surface — API, ownership, module boundary — depends on the answer).

**Atomic publish / no partial-output-looks-like-success risk:**
`compile_and_validate.h/.cpp` (below) writes every intermediate and final
output file to a **temporary path first** (e.g.
`<final-path>.tmp-<processId>`), and only `std::filesystem::rename()`s it
to its final, CMake-`OUTPUT`-declared path **after every validation step
in Section 5 has succeeded**. A failed compile/validate run therefore
never leaves a stale-but-plausible `.spv`/reflection-JSON at the final
path from a *previous* successful run being silently reused — CMake's
own `OUTPUT` staleness tracking (Section 7) already requires the file to
not exist or be older than its `DEPENDS` for a rebuild to trigger, but
this atomic-rename discipline additionally guarantees a **failed**
rebuild does not leave the **previous** successful build's stale output
sitting at the final path looking like it's current when it is not (a
scenario CMake's own timestamp tracking alone does not prevent, since a
failed build target with no output write at all just leaves the OLD file
where it was, silently, still passing timestamp-freshness checks for
whatever consumed it before the edit that broke it).

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
      surfacing slangc's own captured stderr verbatim to the build log.
      No temp file is renamed to a final OUTPUT path (Section 4's atomic-
      publish discipline).
4.  Slang emits BOTH the SPIR-V bytes (to a temp .spv path) and its own
    raw reflection JSON (to a temp path) as part of step 3's single
    invocation (-o and -reflection-json in the same slangc call,
    Section 2's SlangCompileRequest already reflects this).
5.  ShaderSystem::transformSlangReflectionJson(tempRawJsonPath,
    entryPointName, stage, sdkProvenance) -> ReflectionMetadata.
    - TransformError -> exit non-zero; no temp file renamed.
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
      -> ContractMismatchError -> exit non-zero; no temp file renamed.
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
    Non-zero exit -> exit non-zero; no temp file renamed. Its own
    stdout/stderr is surfaced verbatim on failure.
14. Only once every step above has succeeded: write the FINAL
    ReflectionMetadata (Section 2's Atlantis-schema, via
    saveReflectionMetadata()) to a temp path, then atomically rename
    BOTH the .spv and the reflection-JSON temp files to their final,
    CMake-OUTPUT-declared paths (Section 4/7). Re-loading the just-
    written reflection JSON via loadReflectionMetadata() immediately
    after (a cheap, defensive round-trip check) is a Plan-stage nice-to-
    have, not fixed as required here.
15. Exit 0.
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

**Per-shader-pair custom-command chain** (a CMake function,
`atlantis_add_slang_shader_pair(NAME ... SOURCE ... VERTEX_ENTRY ...
FRAGMENT_ENTRY ... OUTPUT_DIR ... EXPECTED_CONTRACT ...)`, defined once
in `src/shader_system/cmake/AtlantisShaderSystem.cmake` and `include()`'d
from wherever a shader pair is declared — Minimal Renderer's own
`shaders/minimal_renderer/CMakeLists.txt`, a new file):

```cmake
add_custom_command(
  OUTPUT
    "${OUTPUT_DIR}/${NAME}.vert.spv" "${OUTPUT_DIR}/${NAME}.vert.refl.json"
    "${OUTPUT_DIR}/${NAME}.frag.spv" "${OUTPUT_DIR}/${NAME}.frag.refl.json"
  COMMAND atlantis_shader_compiler
    --slangc-path=${ATLANTIS_SLANGC_EXECUTABLE}
    --spirv-val-path=${ATLANTIS_SPIRV_VAL_EXECUTABLE}
    --source=${SOURCE} --vertex-entry=${VERTEX_ENTRY} --fragment-entry=${FRAGMENT_ENTRY}
    --output-dir=${OUTPUT_DIR} --expected-contract=${EXPECTED_CONTRACT}
  DEPENDS ${SOURCE} atlantis_shader_compiler
  COMMENT "Compiling and validating Slang shader pair: ${NAME}"
  VERBATIM
)
add_custom_target(${NAME}_shaders ALL
  DEPENDS "${OUTPUT_DIR}/${NAME}.vert.spv" "${OUTPUT_DIR}/${NAME}.frag.spv")
```

- `DEPENDS ${SOURCE}` gives correct incremental rebuild: editing
  `minimal_mesh.slang` makes every listed `OUTPUT` stale, triggering
  exactly this one re-compilation on the next build.
- `DEPENDS atlantis_shader_compiler` (the target, not a file path) makes
  CMake rebuild the Tools executable first if its own sources changed,
  before re-running it — the standard "build a tool, then use it" CMake
  idiom (ADR-0029).
- The single `atlantis_shader_compiler` invocation compiles **both**
  stages internally (looping steps 1–15 of Section 5 once per stage,
  then running step 12's cross-stage check once both are done) — this is
  a Plan-stage simplification over "two separate custom commands, one
  per stage," chosen specifically so the cross-stage check (Section 5
  step 12) has both reflection results available in the same process
  invocation without a second, separate "pair them up" build step.

**Multi-config generator safety — flagged for empirical verification, not
assumed solved by inspection alone.** The `OUTPUT` paths above are
configuration-independent (no `$<CONFIG>` in the path), matching
ADR-0031's Decision. For a single-config generator (Ninja, Makefiles)
this is unambiguously safe. For a multi-config generator (Visual Studio),
CMake generates one `.vcxproj` per configuration, each referencing the
**identical** `OUTPUT` paths and the identical `COMMAND` — MSBuild's own
incremental-build file-timestamp tracking is expected to treat the
second configuration's build as a no-op once the first has produced the
files, but this Plan does **not** claim this is guaranteed race-free
under a genuinely **parallel** multi-configuration build (e.g.
`msbuild /m` building Debug and Release in the same invocation) without
having empirically verified it. **Section 10 lists this as an
Implementation-stage verification item**, not a Human Review blocker (no
public API, module boundary, or ownership model depends on the outcome —
only a build-graph correctness detail with a well-understood, if
unverified-in-this-repo, mitigation path if a real race is found: adding
an explicit `add_dependencies()` ordering between configurations, or
serializing the shader-compile step, neither of which changes anything
this Plan's Human Review needs to approve).

**Consumer artifact discovery — no hardcoded absolute path.** Every
consuming CMake target (the demo, the GPU test) references the shader
output directory via a CMake variable this shader-pair function sets as
a directory property or returns as an out-variable
(`ATLANTIS_${NAME}_SHADER_OUTPUT_DIR`, exact mechanism a Plan-stage
detail Human Review may adjust) — never a string literal path a
developer typed by hand, matching ADR-0031's own rule. The demo's/test's
own existing `POST_BUILD` `copy_if_different` step (Section 1's Files to
Modify) is retargeted from `shaders/minimal_renderer/*.spv` (source tree,
checked-in) to this build-tree variable (generated, not checked in) — the
copy mechanism itself (copy next to the consumer's own executable) is
**unchanged**, only its source path changes.

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
  fixture; the nonzero-set negative fixture (fails closed, per Section
  3); a push-constant fixture explicitly cross-checked for offset/size
  correctness (the issue #5676 regression case); malformed/unexpected-
  structure fixtures → the matching `TransformError` variant, never a
  crash or a best-effort partial result.
- **`descriptor_contract_tests.cpp`:** exact-match fixture → `Ok`; each
  `ContractMismatchError` variant individually exercised (wrong count,
  missing binding, wrong type, wrong stage, unexpected extra binding).
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
- **`tests/tools/shader_compiler/process_launch_tests.cpp`:** argv-vector-
  to-Windows-command-line-string quoting logic (the piece
  `launchProcess()` owns internally) tested in isolation against known
  tricky inputs (embedded spaces, embedded quotes, trailing backslashes —
  the classic `CommandLineToArgvW` edge cases) — **without** actually
  spawning a process for this specific test file (a fake/mockable seam
  inside `process_launch.cpp`, exact mechanism a Plan-stage detail); a
  genuinely-nonexistent executable path → `ExecutableNotFound`, exercised
  via a real (but trivially fast) `launchProcess()` call, since this
  particular case needs no real tool to observe.

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
    propagates that failure (no output files written/renamed).
  - `atlantis_shader_compiler`, run twice on identical input/flags,
    produces byte-identical `.spv` and reflection-JSON output — the
    Plan's own standing regression test for Spec 0008's own local
    (not vendor-guaranteed) determinism observation.
  - Incremental rebuild: touching the `.slang` source and re-running
    `cmake --build` recompiles exactly the affected shader pair; a
    no-op second build recompiles nothing (verified by build-log/
    timestamp inspection, mirroring `testing-strategy.md`'s own build/
    tool-integration layer).
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
  transforms.
- **Manual verification** (`examples/minimal_renderer_demo`, post-
  migration): a visible, correctly-shaded, correctly depth-ordered mesh,
  matching Spec 0007's own already-verified output, confirming the
  migration changed *how* the shader artifact is sourced without
  changing *what* is rendered; resize/minimize/restore/close behavior
  unchanged; Debug and Release builds both exercised.

### Explicitly not automated (stated, not silently assumed covered)

- The nonzero-descriptor-set JSON shape (Section 3's fail-closed
  behavior has no real-SDK-sample-backed positive test this round).
- Multi-config-generator concurrent-build-race safety (Section 7's own
  flagged item) — verified by a manual, deliberate parallel Debug+Release
  build during Implementation, not a standing CTest case (there is no
  reliable, portable way to force the specific race condition
  deterministically in an automated test).

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
5. **`process_launch.cpp` (Tools).** **Gate:**
   `process_launch_tests.cpp` green (quoting logic; no real subprocess
   needed for most cases).
6. **`compile_and_validate.cpp` (Tools) wired to a real `slangc`/
   `spirv-val`.** This is the first step that actually needs the real
   Vulkan SDK toolchain. **Gate:** `toolchain_integration_tests.cpp`
   green against a small, throwaway fixture `.slang` file (not yet
   Minimal Renderer's own shader) — including the atomic-publish/no-
   partial-output check and the determinism regression check.
7. **`atlantis_add_slang_shader_pair()` CMake function + end-to-end
   build-tree pipeline**, still against the throwaway fixture shader.
   **Gate:** a clean build produces the expected `.spv`/`.refl.json`
   pair at the expected build-tree path; incremental-rebuild and
   no-op-rebuild behavior verified; **manual multi-config parallel-build
   verification performed here** (Section 7's flagged item), with the
   result (safe, or a concrete mitigation applied) recorded in the
   implementation PR.
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
      set with no redundant recompilation.
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
- CMake is found genuinely unable to guarantee configuration-independent
  artifact safety under Visual Studio's multi-config generator (Section
  7's flagged item) in a way no add-on mitigation (explicit
  `add_dependencies()` ordering, serializing the shader-compile step)
  can resolve without introducing a new architectural concept.

**Not blockers — Implementation may resolve these directly, calling the
deviation out in the implementation PR per [AGENTS.md](../AGENTS.md):**
exact file names, exact type/function names, exact JSON field-name
spellings (once confirmed against a real, freshly-captured Slang
sample), exact CMake variable/property names, the exact `--expected-
contract=` CLI-flag mechanism (Section 5) if a cleaner alternative
emerges during implementation, and any other detail this Plan's own
"Candidate-API Status" section already flags as non-binding.

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
