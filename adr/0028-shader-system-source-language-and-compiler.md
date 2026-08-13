# ADR 0028: Shader System — Phase 1 Source Language and Compiler (Slang)

- **Status:** Proposed
- **Date:** 2026-08-13 (revised 2026-08-14 — see Revision History)
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Revision History

- **2026-08-13 (original):** Proposed GLSL as Phase 1's shader source
  language, compiled by `glslc` (shaderc's CLI, from the Vulkan SDK).
- **2026-08-14 (revised):** Superseded by this version, following an
  explicit human direction to re-evaluate the whole Shader System design
  around **Slang** instead. This revision does not restate the original
  GLSL-based reasoning as still valid — it replaces it. See
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)'s own
  Revision History for the corresponding compiler-integration change, and
  [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)'s
  own revision note for the full context of this direction change.

## Context

[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md) fixed a
temporary, human-run shader sourcing path for Spec 0007's minimal
material and explicitly deferred "shader source language" as a decision
for Shader System's own future spec — this is that spec. Two checked-in
GLSL sources currently exist
(`shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl`), compiled by a
human running `glslc.exe` from the Vulkan SDK. This ADR's original
version proposed continuing with GLSL/`glslc` on the grounds of zero
migration cost; a human reviewer explicitly redirected this design toward
**Slang** before Human Review, on the basis that Slang is now a
Khronos-hosted, Vulkan-first shading language with its own integrated
compiler and reflection story, and that adopting it now (before any
second GLSL shader exists beyond Spec 0007's own two files) is
meaningfully cheaper than adopting it later.

Phase 1's graphics backend is Vulkan/SPIR-V only
([AGENTS.md](../AGENTS.md)); no second backend is scaffolded "for later."
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-shader-system)
already names "shader language choice" as an open extension point for
Shader System's own spec to resolve — this ADR is that resolution.

### What official Slang material establishes

All facts below are drawn from Khronos/LunarG/shader-slang official
sources, not community wikis or memory — each is cited at its point of
use.

- **Governance and status.** In November 2024, the Khronos Group
  launched "the Slang Initiative," hosting the open-source Slang shading
  language and compiler as a multi-company-governed Khronos project,
  building on code contributed by NVIDIA and roughly fifteen years of
  prior research/production use
  ([Khronos press release](https://www.khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia)).
  Slang is not a from-scratch or experimental project — per its own
  README, it "is based on years of collaboration between researchers at
  NVIDIA, Carnegie Mellon University, Stanford, MIT, UCSD and the
  University of Washington"
  ([shader-slang/slang README](https://github.com/shader-slang/slang/blob/master/README.md)).
- **Compilation targets and their support tier.** Per Slang's own README,
  officially-supported targets are **Direct3D 12: Supported, Vulkan:
  Supported, Direct3D 11: Supported, CUDA: Supported**, while **Metal:
  Experimental, WebGPU: Experimental, CPU: Experimental**
  ([shader-slang/slang README](https://github.com/shader-slang/slang/blob/master/README.md)).
  Vulkan is in Slang's own highest support tier, alongside D3D12 — "the
  platforms (Windows, Linux) and target APIs (Direct3D 12, Vulkan) where
  Slang is used most heavily" are its stated current focus (same source).
- **Distribution.** The Vulkan Documentation Project states Slang "comes
  with both an offline compiler (a binary for multiple operating systems)
  and a library for runtime compilation," obtainable "via
  [github](https://github.com/shader-slang/slang/releases)" or as "part
  of the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)"
  ([docs.vulkan.org — Slang Shading Language in Vulkan](https://docs.vulkan.org/guide/latest/slang.html)).
- **Vulkan SDK bundling, confirmed with version numbers.** LunarG's own
  release announcement for Vulkan SDK 1.3.296.0 (October 2024) states
  that release "contains a beta version of slang"
  ([Khronos news / LunarG release announcement](https://www.khronos.org/news/permalink/lunarg-releases-vulkan-sdk-1.3.296.0-for-windows-linux-macos)).
  The current Vulkan SDK release notes (version **1.4.357.0**, dated July
  28, 2026 — **the exact SDK version already installed and recorded as
  in use in this repository**, per
  [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
  own `glslc.exe --version` provenance note) confirm Slang is bundled,
  tagged to a matching Slang release
  (`https://github.com/shader-slang/slang/tree/vulkan-sdk-1.4.357`, tag
  `vulkan-sdk-1.4.357.0`), listed among the SDK's shader tools across all
  supported platforms (Windows x64/x86/ARM, Linux, macOS)
  ([Vulkan SDK 1.4.357.0 release notes](https://vulkan.lunarg.com/doc/view/latest/windows/release_notes.html)).
  **This means `slangc` is obtainable from exactly the same Vulkan SDK
  installation this repository's development environment already has
  installed for `glslc`/the Vulkan loader** — no separate download.
- **Building from source is a real, non-trivial commitment**, distinct
  from `FetchContent`-friendly small dependencies: the official build
  guide requires a **recursive git clone (git submodules)**, **CMake 3.26
  preferred (3.22 minimum)**, **C++17**, and specific supported compiler
  versions (GCC ≥ 11.4 recommended / 10 best-effort, Clang ≥ 17.0, MSVC
  19), building on Linux, Windows, macOS, WebAssembly (Emscripten), and
  Android (NDK)
  ([shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md)).
  `cmake --install` produces a `SlangConfig.cmake`, making `find_package`
  work against a *self-built* tree — but as of a still-open upstream
  issue, prebuilt GitHub Releases did not necessarily ship an equivalent
  `SlangConfig.cmake` for `find_package`-based consumption of a
  *downloaded* binary
  ([shader-slang/slang issue #5649, "Provide SlangConfig.cmake in
  releases," filed November 2024](https://github.com/shader-slang/slang/issues/5649)).
  No official `FetchContent`-specific integration guidance exists.
- **Runtime/redistribution obligations for the compiler library.** Slang
  ships its compiler as a shared library (renamed from `slang.dll`/
  `libslang.so` to `slang-compiler.dll`/`libslang-compiler.so` in release
  v2025.21, with compatibility shims scheduled for removal at the end of
  2026); the official docs state "downstream users of Slang distributing
  their products as binaries should... redistribute the Slang libraries
  they linked against" on all platforms
  ([shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md)).
- **License.** Slang's own code is Apache-2.0 WITH LLVM-exception (SPDX
  identifier, per its `LICENSE` file); its build depends on third-party
  projects (glslang, lz4, miniz, spirv-headers, spirv-tools, LLVM), each
  with its own license recorded in the repository's `LICENSES/` directory
  ([shader-slang/slang `LICENSE`](https://github.com/shader-slang/slang/blob/master/LICENSE)).
- **SPIR-V version support tier — a real, disclosed constraint.** Per
  Slang's own SPIR-V-specific documentation: **"Slang's SPIR-V backend is
  stable when emitting SPIR-V 1.3 and later; however, support for SPIR-V
  1.0, 1.1 and 1.2 is still experimental"**
  ([docs.shader-slang.org — SPIR-V-Specific Functionalities](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/a2-01-spirv-target-specific.html)).
  This is a genuine tension with the Vulkan Backend's current, unraised
  minimum supported API version (`VK_API_VERSION_1_0`,
  [ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)'s Accepted
  Amendment explicitly declining to raise it) — see Decision and Risks,
  below, for how this ADR resolves it and what remains open.

## Decision

**Phase 1 shader source is authored exclusively in Slang, targeting
Vulkan/SPIR-V only. No GLSL, no HLSL, no second source language is
supported, scaffolded, or planned for.** The two GLSL files
[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md) checked
in are superseded going forward — their migration to Slang source is
scoped to a future implementation Plan (see
[ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)),
not performed by this ADR or Spec 0008 itself.

- **Slang, not GLSL or HLSL.** Slang is a Khronos-governed, actively
  maintained shading language whose highest official support tier
  already includes Vulkan (alongside D3D12) — not an experimental or
  unproven target for the API Atlantis actually uses
  ([shader-slang/slang README](https://github.com/shader-slang/slang/blob/master/README.md)).
  Unlike GLSL (Vulkan-native but with no cross-stage type-checked module
  system) or HLSL (D3D-first idioms requiring `[[vk::...]]` annotations
  bolted on for Vulkan semantics — the same annotation family Slang
  itself borrows and extends, see
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)),
  Slang's module system lets a vertex and fragment entry point share one
  compiled module and one common varying-interface `struct` type,
  giving genuine compiler-enforced cross-stage type safety "by
  construction" rather than by a downstream, string/index-based
  cross-check — a real capability improvement this ADR chooses to use,
  detailed in
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md).
- **Vulkan/SPIR-V is the only compilation target this spec configures**,
  even though Slang itself supports several
  ([shader-slang/slang README](https://github.com/shader-slang/slang/blob/master/README.md)).
  No D3D12/D3D11/CUDA/Metal/WebGPU/CPU target is invoked, built for, or
  scaffolded — matching [AGENTS.md](../AGENTS.md)'s Phase 1 constraint
  and the "no speculative abstraction" principle exactly as strictly as
  the original GLSL-based design did. Choosing a multi-target-capable
  compiler does not, by itself, authorize using more than one of its
  targets.
- **SPIR-V version target: SPIR-V 1.0 — a deliberate, disclosed choice,
  not an oversight, and flagged for explicit Human Review confirmation.**
  Atlantis's Vulkan Backend's minimum supported API version remains
  `VK_API_VERSION_1_0`
  ([ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)'s
  Accepted Amendment), matching the existing GLSL shaders'
  `--target-env=vulkan1.0` compilation flag
  ([shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)).
  Preserving this floor with Slang means targeting **SPIR-V 1.0**
  output — which Slang's own documentation places in its **"experimental"**
  support tier, not its "stable" (SPIR-V 1.3+) tier
  ([docs.shader-slang.org — SPIR-V-Specific Functionalities](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/a2-01-spirv-target-specific.html)).
  This ADR's recommendation is to **accept SPIR-V 1.0 targeting, with
  this experimental-tier status explicitly disclosed**, because the
  alternative — targeting SPIR-V 1.3 for Slang's "stable" tier — would
  require raising the minimum SPIR-V *consumption* requirement (per
  Vulkan's own SPIR-V-environment-to-API-version mapping, SPIR-V 1.3
  becomes a mandated floor at Vulkan 1.1, not Vulkan 1.0), which is
  functionally equivalent to raising the Vulkan Backend's own minimum
  supported device/driver capability — an architectural compatibility-
  floor decision [ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)'s
  Human Review already explicitly declined to make, and one this ADR is
  not authorized to reopen unilaterally. **This is stated here as a
  recommendation requiring explicit Human Review confirmation before
  Plan/implementation, not a silently resolved question** — see Risks &
  Open Questions in [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)
  for the two concrete options a human must choose between.
- **Slang's compiler is sourced as a prebuilt binary from the Vulkan SDK
  already required by [ADR-0006](0006-dependency-management.md)** for
  the Vulkan Backend — confirmed bundled since SDK 1.3.296.0 and present,
  tagged, in the current SDK release (1.4.357.0) this repository's
  development environment already has installed (see Context, above).
  This is **not a new third-party dependency acquisition mechanism** in
  [AGENTS.md](../AGENTS.md)'s sense: it is a new *use* of a tool that
  ships inside an SDK Atlantis already requires developers and CI
  machines to install, exactly mirroring how `glslc` was sourced from the
  same SDK in this ADR's original version. **No `FetchContent`, no
  from-source Slang build, and no separate Slang SDK/installer download
  is introduced.** See
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md) for
  the full compiler-library-vs-CLI decision this acquisition choice
  feeds into.
- **License/dependency posture.** Because Slang is consumed exclusively
  as an externally-installed SDK tool (never vendored, never linked, per
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)),
  Atlantis's own repository carries no third-party-notice bundling
  obligation for Slang or its own bundled third-party components
  (glslang, lz4, miniz, spirv-headers, spirv-tools, LLVM) — the same
  posture Atlantis already has for the Vulkan SDK itself. Slang's own
  Apache-2.0-with-LLVM-exception license and its `LICENSES/` directory
  ([shader-slang/slang `LICENSE`](https://github.com/shader-slang/slang/blob/master/LICENSE))
  govern Slang's own distribution, not Atlantis's.

## Consequences

### Positive

- Adopting Slang now, while only two GLSL files exist, is the cheapest
  point at which to make this change — before any second material's
  worth of shader source accumulates in the superseded language.
- Slang's module system and shared-struct cross-stage typing (see
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  gives Atlantis a genuinely stronger cross-stage-compatibility guarantee
  than GLSL's location-index-only convention could, for the same
  authoring effort.
- Zero new acquisition mechanism: `slangc` rides on the exact same Vulkan
  SDK requirement `glslc` already relied on — no new installer, no new
  environment variable category, no new CI provisioning story beyond what
  [ADR-0006](0006-dependency-management.md) already established.
- Khronos governance and Vulkan's status as one of Slang's two
  highest-tier-supported targets give confidence this is not a niche or
  soon-to-be-abandoned tool choice.

### Negative / Trade-offs

- Slang's own SPIR-V 1.0–1.2 emission is documented as "experimental,"
  not "stable" — a real, disclosed risk this ADR accepts in order to
  avoid silently narrowing Atlantis's device-compatibility floor, but one
  that could surface real bugs a "stable"-tier target would not. This is
  the single largest open risk this ADR introduces relative to its
  original GLSL-based version, and is called out explicitly rather than
  minimized — see Risks & Open Questions in
  [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md).
- Slang's own reflection API has at least one documented open bug report
  concerning push-constant reporting
  ([shader-slang/slang issue #5676](https://github.com/shader-slang/slang/issues/5676)) —
  a real, disclosed maturity caveat for a tool this young relative to
  glslang/shaderc's own multi-decade GLSL lineage, accepted as a
  reasonable risk for a Khronos-governed, actively developed project, not
  hidden from this record.
- Migrating Spec 0007's two existing GLSL files to Slang is real,
  deferred implementation work (see
  [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)),
  a cost the original GLSL-based version of this ADR did not have.
- Slang is a substantially larger, more actively-evolving project than
  `glslc` alone (it *subsumes* glslang and SPIRV-Tools internally, per
  its own bundled-license list) — a broader dependency surface to track
  for security/compatibility updates than a single, narrowly-scoped GLSL
  compiler invocation, even though it is consumed only as an external SDK
  tool, never linked.

## Alternatives Considered

- **Continue with GLSL + `glslc`** (this ADR's own original 2026-08-13
  decision). Rejected upon human redirection: while it had zero migration
  cost for Spec 0007's existing files, it forecloses Slang's shared-
  module cross-stage type-safety capability
  ([ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  and commits Atlantis to a shader language with no first-class
  reflection-JSON CLI output built in the way Slang's `slangc
  -reflection-json` provides
  ([shader-slang/slang `docs/command-line-slangc-reference.md`](https://github.com/shader-slang/slang/blob/master/docs/command-line-slangc-reference.md)),
  which would otherwise have required a separate third-party reflection
  library ([ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  now-superseded original SPIRV-Reflect proposal).
- **HLSL compiled via DXC's SPIR-V backend.** Still rejected, for the
  same reason as this ADR's original version: HLSL's binding idioms are
  oriented toward D3D-first authoring Phase 1's Vulkan-only scope gets no
  benefit from, and Slang already subsumes HLSL's practical advantages
  (a mature, statically-typed HLSL-like surface) while adding a genuinely
  better-fit Vulkan/SPIR-V-first design and Khronos governance DXC does
  not have.
- **Target SPIR-V 1.3 (Slang's "stable" tier) instead of SPIR-V 1.0**,
  accepting a raised minimum Vulkan/driver floor. Not rejected outright —
  this is a real, live alternative this ADR flags rather than forecloses,
  since it trades a real, disclosed technical risk (SPIR-V 1.0's
  "experimental" tier) for a real, disclosed architectural cost (raising
  a compatibility floor [ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)'s
  Human Review explicitly chose not to raise). Left as an explicit
  Human Review decision point, not resolved by this ADR alone — see
  Decision, above.
- **Support both Slang and GLSL simultaneously**, letting a shader's file
  extension select its compiler (mirroring this ADR's own original
  "support both GLSL and HLSL" rejection). Rejected for the same reason:
  doubles the compiler/reflection-integration surface for no Phase 1
  consumer that needs a choice, and undermines the exact "no two parallel
  authoritative shader-sourcing mechanisms" principle
  [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)'s
  migration boundary already establishes for the GLSL-to-Slang
  transition itself.
- **Link Slang's compiler library (rather than invoking `slangc` as a
  subprocess) to compile shaders.** Addressed in full in
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md), not
  repeated here — rejected primarily because the CLI path reuses the
  existing Vulkan SDK acquisition story with no redistribution or
  `SlangConfig.cmake`-availability risk
  ([shader-slang/slang issue #5649](https://github.com/shader-slang/slang/issues/5649)),
  while `slangc -reflection-json` already provides everything
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  reflection scope needs.
