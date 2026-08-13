# ADR 0031: Shader System — Artifact Location, Versioning, and Reproducibility

- **Status:** Proposed
- **Date:** 2026-08-13 (revised 2026-08-14 — see Revision History)
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Revision History

- **2026-08-13 (original):** Proposed a `glslc`-version provenance note
  (assuming a `glslc --version`-equivalent string) and a GLSL-to-nothing
  migration framing.
- **2026-08-14 (revised):** Superseded by this version, following the
  same human-directed re-evaluation toward Slang described in
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)'s
  Revision History. Compiler-version provenance is now anchored on the
  **resolved Vulkan SDK version** rather than an assumed `slangc
  --version` flag, because no official Slang CLI documentation this
  research reviewed confirmed such a flag exists (see Context, below) —
  this revision does not repeat the original's unverified assumption.
  The migration section now describes a GLSL-to-Slang source migration,
  not merely "checked-in bytes to build artifact."
- **2026-08-14 (second revision — evidence-based):** Updated after the
  hands-on toolchain experiment recorded in
  [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md)'s
  "Validation Evidence" section. Three changes: (1) the absence of a
  `slangc --version` flag is now a **confirmed** local observation, not
  an inference, and a concrete alternative version anchor was found;
  (2) local build determinism was **measured** rather than assumed;
  (3) the previously-unaddressed Debug/Release shader-debug-info question
  is now explicitly scoped out, closing an audit-identified gap.

## Context

[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
`.spv` files (and their JSON reflection siblings, per
[ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
are, under
[ADR-0029](0029-shader-system-build-time-compilation-boundary.md),
produced by a build step rather than checked into the repository. This
raises questions no prior spec in this line has had to answer for a
binary build product: where do generated shader artifacts live in the
build tree, how does a consumer find them without a hardcoded
developer-machine path, what happens across `Debug`/`Release`/multi-
config generators, and what "reproducible" means for an artifact no
longer checked in for a reviewer to diff.

### What official material establishes about compiler-version provenance

The `slangc` command-line reference this research reviewed
([shader-slang/slang `docs/command-line-slangc-reference.md`](https://github.com/shader-slang/slang/blob/master/docs/command-line-slangc-reference.md))
documents target/profile/capability/entry-point/output/reflection-JSON
flags in detail but was not confirmed to document a `-v`/`--version` flag
printing the compiler's own version string — this ADR does not assert one
exists. What *is* confirmed, with a version number, is that a specific
Vulkan SDK release bundles a specific, tagged Slang release: SDK version
**1.4.357.0** (the version this repository's development environment
already has installed, per
[shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md))
is tagged to Slang release `vulkan-sdk-1.4.357.0`
([Vulkan SDK 1.4.357.0 release notes](https://vulkan.lunarg.com/doc/view/latest/windows/release_notes.html)).
This gives a reliable, officially-confirmed provenance anchor —
**the resolved Vulkan SDK version** — that does not depend on an
unverified compiler CLI flag.

## Decision

**Compiled `.spv` bytecode and its Atlantis-schema reflection JSON
sidecar are ordinary, non-checked-in build products, analogous to a
`.obj`/`.lib` file — not checked-in source, and not committed to version
control by this spec's implementation.** Reproducibility is achieved
through recording the resolved Vulkan SDK version and deterministic
build inputs, not through committing binary output.

- **Output location: a single, configuration-independent directory under
  the build tree** — `${CMAKE_BINARY_DIR}/shaders/<relative-path-mirroring-source>/`
  (exact interpolation syntax a Plan-stage detail). Not nested under a
  `$<CONFIG>`-specific subdirectory: SPIR-V bytecode does not vary by C++
  build configuration
  ([ADR-0029](0029-shader-system-build-time-compilation-boundary.md)), so
  compiling once into one shared location avoids redundant compilation
  across `Debug`/`Release`.
- **No developer-machine absolute path appears in any checked-in file, any
  CMake script, or any consumer's source code.** Every consuming CMake
  target locates its shaders via a CMake target property or generator
  expression Shader System's own CMake integration exports — never a
  string literal path a developer typed by hand. This directly extends
  the existing rule
  [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)
  already follows informally ("each consumer's own CMake target copies
  the `.spv` files next to its own build output") into a formal,
  Shader-System-owned mechanism.
- **Test and demo consumption**: tests
  (`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`-equivalent
  future tests) and demos
  (`examples/minimal_renderer_demo`-equivalent future demos) locate their
  compiled shader artifacts via the same CMake-exported mechanism — the
  build-tree shader output directory is authoritative; a test/demo
  consumes from it (by reading directly, or by a `POST_BUILD` copy step
  next to its own executable, mirroring the existing
  `minimal_renderer/README.md` convention) but never becomes a second
  authoritative copy other tooling must know about.
- **Reproducibility model**: a build is reproducible in the sense that
  the same Slang source, compiled by the same `slangc` (i.e. the same
  Vulkan SDK version) with the same fixed flags (target `spirv`, plus the
  SPIR-V version profile
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)'s
  Decision fixes, pending its own Human-Review-confirmed resolution),
  produces byte-identical SPIR-V output.
  - **Local observation supporting this** (not a vendor guarantee):
    compiling the same source twice with identical flags produced
    SHA-256-identical `.spv` output *and* SHA-256-identical reflection
    JSON. **This is a single-machine, single-Slang-version observation
    only.** It is deliberately **not** escalated into a claim that Slang
    guarantees deterministic output — no such guarantee was found in any
    official Slang material, and none is assumed here.
  - This spec does **not** attempt to guarantee bit-for-bit
    reproducibility across *different* `slangc`/Vulkan SDK versions.
  - **`slangc` exposes no version flag — now confirmed, not inferred.**
    Invoking `slangc --version` on the installed toolchain returns
    `error[E00017]: unknown command-line option '--version'`. The
    provenance anchor is therefore the **resolved Vulkan SDK version**,
    recorded in the reflection JSON sidecar's metadata (a
    `"vulkanSdkVersion"` field or equivalent), read from the
    `find_program()`-located `slangc`'s own install path. This supersedes
    [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
    manual, per-directory plain-text compiler-version note.
  - **A second, more precise anchor is available and recommended
    alongside it:** the SDK's `Bin` directory contains a
    `slang-standard-module-<version>` directory whose name encodes the
    exact bundled Slang release (observed:
    `slang-standard-module-2026.13.1`). Recording that string as well
    gives artifact provenance at Slang-release granularity rather than
    only SDK granularity. The exact discovery mechanism is a Plan-stage
    detail; that *some* Slang-release-granular identifier is recorded is
    fixed here.
  - A recommended/tested Vulkan SDK version range is documented (exact
    text a Plan-stage detail, likely in `shaders/README.md`'s revised
    content) as guidance for contributors and CI image provisioning.
- **Optional but recommended: static validation of every emitted
  artifact.** `spirv-val.exe` is present in the same Vulkan SDK `Bin`
  directory as `slangc.exe` (confirmed on the installed SDK; it reports
  `SPIRV-Tools v2026.3` and lists "SPIR-V 1.0 (under Vulkan 1.0
  semantics)" among its supported targets). Running
  `spirv-val --target-env vulkan1.0` on each emitted module, as part of
  the same Tools CLI invocation that produced it, is **recommended** — it
  is the concrete mitigation
  [ADR-0028](0028-shader-system-source-language-and-compiler.md) relies on
  for the SPIR-V-1.0-tier risk. Both experiment artifacts passed with
  exit code 0. Because `spirv-val` ships in the same already-required SDK,
  it introduces **no new independent dependency acquisition mechanism**,
  though — like `slangc` — it would become **an additional required build
  tool** whose absence must fail at configure time, not silently disable
  validation. **This static validation does not replace Vulkan Validation
  Layers**, which remain the mandatory gate on any GPU-touching path per
  [AGENTS.md](../AGENTS.md), and **does not convert Slang's
  experimental-tier SPIR-V 1.0 path into a stable one.**
- **Freshness/staleness**: guaranteed correct by
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)'s
  `add_custom_command()` `DEPENDS` mechanism.

### Debug/Release and shader debug information — scoped out deliberately

A configuration-independent shader artifact is only coherent if shader
compilation genuinely does not vary by C++ build configuration. This
section states that condition explicitly rather than leaving it implied:

- **Shader debug-information generation is out of scope for Spec 0008's
  Phase 1.** No Slang debug-info flag, no source-level shader debugging
  support, and no optimization-level selection is configured.
- **Debug and Release C++ configurations use identical shader
  compilation flags.** There is exactly one flag set, applied uniformly.
- **Therefore a single, configuration-independent SPIR-V artifact is a
  deliberate design consequence, not an oversight** — this closes the
  gap an audit of the previous revision correctly identified, where
  "one artifact for all configurations" was asserted without stating
  that nothing in the compile step varies by configuration.
- **Explicitly deferred to future work:** RenderDoc/source-level shader
  debugging, shader debug-info variants, differing optimization levels
  per configuration, and any shader permutation keyed on build
  configuration. **No Debug/Release shader permutation system is designed
  by this ADR.**
- **Trigger for revisiting:** if any future spec introduces
  configuration-dependent shader compilation flags, both the artifact
  output path model (currently one shared, `$<CONFIG>`-independent
  directory) and the incremental-build dependency model **must** be
  re-examined together — a configuration-dependent flag set makes the
  single-shared-output-directory decision above invalid, not merely
  suboptimal.
- **No artifact is ever checked into git by this spec's implementation.**
  `.gitignore` is extended (a Plan-stage detail names the exact pattern)
  to exclude the build-tree shader output directory.

### Migration: superseding ADR-0027's checked-in artifacts, GLSL to Slang

- [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)
  **remains `Accepted` and is not rewritten** — it is a permanent record
  of a decision that was correct for Spec 0007's own moment, per
  [AGENTS.md](../AGENTS.md)'s "ADRs are not silently rewritten" rule.
  This ADR (0031), together with
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)–[ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md),
  **supersedes ADR-0027's *mechanism*** (checked-in, human-compiled
  bytecode) for every shader Shader System's build pipeline covers,
  starting with `shaders/minimal_renderer/`'s existing two shader pairs.
  ADR-0027's own "Migration boundary" section explicitly anticipated and
  authorized exactly this outcome.
- **`shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl` are migrated
  to Slang source** (e.g. a single `minimal_mesh.slang` module containing
  both the vertex and fragment entry points, sharing one varying-
  interface `struct`, per
  [ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)'s
  authoring convention — exact file layout a Plan-stage detail), compiled
  by Atlantis Tools' CLI instead of checked in as pre-compiled bytecode.
  **`shaders/minimal_renderer/minimal_mesh.{vert,frag}.spv` (the
  checked-in bytecode), the original `.glsl` source files, and the
  plain-text compiler-version note in
  `shaders/minimal_renderer/README.md` are all retired** — removed once a
  future Plan/implementation switches
  `examples/minimal_renderer_demo`/the GPU test's `Material` construction
  from loading the checked-in `.spv` file to consuming Shader System's
  build-tree-generated artifact and the reflection metadata mapped by the
  Shader System module's RHI-integration target instead.
  **This migration, and the call-site change it requires, is explicitly
  not performed by this spec** — Spec 0008 is a spec/ADR-only round; it
  is scoped as required follow-up for Shader System's implementation
  Plan.
- **No two parallel, simultaneously-authoritative shader-sourcing
  mechanisms persist once Shader System's Plan is implemented.** Once the
  migration above lands, ADR-0027's checked-in-bytecode path (and its
  GLSL source) is fully retired — not kept "just in case" alongside the
  new Slang/build-time path.

## Consequences

### Positive

- Treating compiled shader artifacts as ordinary build products matches
  how every other compiled Atlantis artifact is already handled.
- Anchoring provenance on the resolved Vulkan SDK version is a claim this
  ADR can actually back with an official, version-numbered citation
  ([Vulkan SDK 1.4.357.0 release notes](https://vulkan.lunarg.com/doc/view/latest/windows/release_notes.html)),
  rather than assuming an unverified CLI flag — a more honest
  reproducibility story than this ADR's original version had.
- A single, configuration-independent output location avoids redundant
  recompilation across `Debug`/`Release`.
- The explicit migration section gives Shader System's future
  implementation Plan an unambiguous, pre-reviewed target, including the
  Slang-specific authoring-convention detail
  ([ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md))
  the original GLSL-based migration framing did not need to state.

### Negative / Trade-offs

- Losing checked-in `.spv` bytes means a reviewer can no longer diff a
  binary artifact directly in a PR (though that "diff" was never
  meaningful for binary content either — only the checked-in shader
  *source* was ever human-reviewable, which remains checked in and
  reviewable, now as Slang instead of GLSL).
- Not guaranteeing bit-for-bit reproducibility across different `slangc`/
  Vulkan SDK versions is a real, accepted gap — mitigated, not
  eliminated, by version recording and documented recommended-version
  guidance.
- Every developer/CI machine needs a working, build-time-reachable
  `slangc` with no fallback to a previously-checked-in artifact if the
  toolchain is temporarily broken or unavailable.
- The migration work itself (retiring `shaders/minimal_renderer/`'s GLSL
  source and `.spv` files, authoring their Slang replacement, updating
  the demo/test call sites) is real, deferred implementation work this
  ADR does not perform.

## Alternatives Considered

- **Continue checking in compiled `.spv` bytes, now generated by the
  build rather than by a human.** Rejected: conflates "generated build
  artifact" with "checked-in source," which
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)'s
  own golden-image rule already establishes a strong repository norm
  against.
- **Nest shader output under `$<CONFIG>`-specific subdirectories.**
  Rejected: SPIR-V bytecode carries no configuration-dependent content in
  this spec's scope.
- **Assume `slangc` exposes a `--version` flag and record its output
  directly**, as this ADR's original version implicitly did by analogy
  with `glslc --version`. Rejected on revision: no official documentation
  reviewed for this ADR confirmed such a flag exists for `slangc`;
  asserting it without verification would repeat exactly the kind of
  unverified-API-shape guess the human redirection for this spec
  explicitly warned against. The Vulkan-SDK-version anchor is used
  instead because it is directly, officially verifiable
  ([Vulkan SDK 1.4.357.0 release notes](https://vulkan.lunarg.com/doc/view/latest/windows/release_notes.html)).
- **A fully pinned, vendored Slang toolchain** (e.g. building Slang from
  source and vendoring the resulting binary), for stronger bit-for-bit
  reproducibility guarantees. Rejected for this round: Slang's own build
  requirements (recursive submodule clone, CMake ≥ 3.26, C++17 toolchain
  versions,
  [shader-slang/slang `docs/building.md`](https://github.com/shader-slang/slang/blob/master/docs/building.md))
  are a substantially heavier commitment than reusing the existing
  Vulkan SDK requirement
  ([ADR-0028](0028-shader-system-source-language-and-compiler.md));
  revisiting this trade-off is left to a future spec if reproducibility
  across CI/developer machine images becomes a concrete, measured
  problem.
- **Store generated artifacts in a fixed, non-build-tree location.**
  Rejected: reintroduces the "generated content living outside the build
  tree" ambiguity this ADR's primary decision avoids.
