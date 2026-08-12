# ADR 0028: Shader System — Phase 1 Source Language and Compiler

- **Status:** Proposed
- **Date:** 2026-08-13
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Context

[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md) fixed a
temporary, human-run shader sourcing path for Spec 0007's minimal
material and explicitly deferred "shader source language" as a decision
for Shader System's own future spec — this is that spec. Two checked-in
GLSL sources already exist
(`shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl`), compiled by a
human running `glslc.exe` from the Vulkan SDK
(`shaders/minimal_renderer/README.md` records the exact compiler/version
used). No HLSL source exists anywhere in this repository.

Phase 1's graphics backend is Vulkan/SPIR-V only
([AGENTS.md](../AGENTS.md)); no second backend is scaffolded "for later."
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
already names "shader language choice" as an open extension point for
Shader System's own spec to resolve — this ADR is that resolution.

## Decision

**Phase 1 shader source is authored exclusively in GLSL, compiled to
SPIR-V by `glslc` (the shaderc-based command-line compiler distributed
with the Vulkan SDK).** No HLSL, no Slang, no second source language is
supported, scaffolded, or planned for in this round.

- **GLSL, not HLSL.** GLSL is Vulkan/SPIR-V's native source language —
  `layout(binding=...)`, `layout(push_constant)`, and
  `layout(location=...)` map directly onto exactly the descriptor-binding,
  push-constant, and vertex-interface concepts Shader System's own
  reflection (see
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  needs to extract, with no target-environment translation step. HLSL's
  SPIR-V path (via DXC) is real and viable, but HLSL's own idioms
  (`register(t0, space0)`, `cbuffer`, `[[vk::binding(...)]]` annotations
  needed to express Vulkan-specific binding semantics) exist primarily to
  serve D3D-first shader authoring — a orientation Phase 1 has no use for,
  since Phase 1 targets Vulkan exclusively and D3D12 is explicitly
  excluded from every current or planned milestone
  ([docs/project-blueprint.md](../docs/project-blueprint.md) Section 7).
  Choosing GLSL also means zero migration cost for the two shader files
  Spec 0007 already checked in — they compile unchanged under this
  decision.
- **This choice is scoped to Phase 1's Vulkan-only backend and is not a
  commitment for any future second backend.** If a future phase adds a
  D3D12, Metal, or WebGPU backend, that phase's own spec chooses its own
  source-language/compiler strategy (which may mean adopting HLSL/Slang
  at that point, cross-compiling GLSL via SPIRV-Cross, or maintaining
  parallel source) — this ADR does not pre-decide that outcome and does
  not design a multi-target shader architecture now, per
  [AGENTS.md](../AGENTS.md)'s "no speculative abstraction" principle.
- **`glslc` is a build tool, not a runtime or link-time dependency.**
  Shader System's compile step invokes `glslc.exe`/`glslc` as an external
  process at build time (see
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)); no
  Atlantis executable links against shaderc's library form
  (`shaderc_combined`/`shaderc_shared`), and no Atlantis process invokes
  shader compilation at runtime.
- **`glslc` is sourced from the Vulkan SDK already required by
  [ADR-0006](0006-dependency-management.md)** for the Vulkan Backend
  (located via the `VULKAN_SDK` environment variable /
  `find_package(Vulkan)`, per that ADR's "external system/toolchain
  dependency" category). This is not a new third-party dependency in
  [AGENTS.md](../AGENTS.md)'s sense — it is a new *use* of a component
  (`glslc`) that ships inside an SDK Atlantis already requires developers
  and CI machines to install, not a new thing to install. CMake locates
  it via `find_program(Atlantis_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin"
  ...)` (exact hint paths are a Plan-stage detail) — never fetched,
  downloaded, or built by CMake, matching ADR-0006's existing rule for
  SDK-provided tools.
- **`glslangValidator`/glslang (invoked directly, bypassing shaderc) is
  not chosen.** `glslc` is shaderc's own CLI wrapper around glslang plus
  SPIRV-Tools optimization passes, and is the compiler already used to
  produce the two checked-in `.spv` files this decision inherits — no
  reason exists to introduce a second, less-integrated GLSL frontend.
- **No shader compiler library (`shaderc`, `glslang`) is linked into any
  Atlantis target.** Only the standalone `glslc` executable is invoked, as
  a subprocess, by whichever build-time tool
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)
  defines.

## Consequences

### Positive

- Zero migration cost for Spec 0007's two existing checked-in `.glsl`
  files — this decision codifies the language/compiler already in use,
  it does not change it.
- GLSL's Vulkan-native binding/push-constant/location syntax requires no
  translation layer between what a shader author writes and what
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  reflection step extracts.
- No new third-party dependency to install, fetch, or version-pin
  separately — `glslc` rides on the Vulkan SDK requirement every Windows/
  Android Vulkan Backend build already has.
- Keeps the shader-source-language question genuinely settled for Phase 1
  without constraining a future multi-backend phase's own choice.

### Negative / Trade-offs

- Locks Phase 1 shader authoring to GLSL's syntax and tooling ecosystem;
  any future contributor more familiar with HLSL authors in GLSL instead.
  Accepted: no concrete Phase 1 need for HLSL exists, and introducing it
  now would be exactly the "target more than one shading language ahead
  of a real need" pattern [AGENTS.md](../AGENTS.md) warns against.
- `glslc`'s availability becomes a hard build-time requirement for any
  target that compiles a shader — a machine with the Vulkan SDK installed
  for its loader/headers but with `glslc` excluded from the install (the
  SDK installer has historically allowed component-level exclusion) would
  fail to configure. See
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md) for
  the explicit configure-time failure this produces, rather than a silent
  skip.
- Version drift between different developers' or CI's installed Vulkan
  SDK (and therefore `glslc` version) is not strictly pinned the way a
  `FetchContent`-fetched dependency would be — mitigated, not eliminated,
  by recording a tested/recommended SDK version in documentation (see
  [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)).

## Alternatives Considered

- **HLSL compiled via DXC's SPIR-V backend.** Rejected: DXC's SPIR-V
  output path is mature and real, but HLSL's own binding-model idioms are
  oriented toward D3D-first authoring in a way Phase 1's Vulkan-only scope
  gets no benefit from, and it would require rewriting Spec 0007's two
  already-checked-in GLSL files for no functional gain. Revisitable if a
  future D3D12 backend phase makes HLSL's D3D/Vulkan dual-target property
  actually valuable — not decided or scaffolded now.
- **Slang.** A newer shading language capable of targeting SPIR-V (and
  other backends) with a more modern module system. Rejected for Phase 1:
  meaningfully less mature/battle-tested toolchain than glslc+GLSL, adds a
  second new dependency (the Slang compiler itself) beyond what
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md) and
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)
  already introduce, and Phase 1 has no concrete multi-backend need that
  would make Slang's cross-target design pay for itself yet.
- **Support both GLSL and HLSL from the start, letting a shader's file
  extension select its compiler.** Rejected: doubles the compiler/
  reflection-integration surface for no Phase 1 consumer that needs a
  choice — exactly the kind of premature generality
  [AGENTS.md](../AGENTS.md)'s "no speculative abstraction" principle
  excludes; a future spec can add a second source language if a real need
  appears.
- **Link `shaderc`'s C++ library directly into Shader System**, rather
  than shelling out to `glslc.exe`. Rejected: adds a genuine new link-time
  dependency (shaderc's library artifacts, and its own transitive
  glslang/SPIRV-Tools link graph) requiring its own dependency-management
  decision beyond what ADR-0006's "SDK-provided tool" category already
  covers; the standalone `glslc` executable already ships in the same
  Vulkan SDK and is sufficient for a build-time-only compilation need with
  no runtime compilation use case in this spec's scope.
