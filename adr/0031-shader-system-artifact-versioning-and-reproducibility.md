# ADR 0031: Shader System — Artifact Location, Versioning, and Reproducibility

- **Status:** Proposed
- **Date:** 2026-08-13
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md) (`Draft`)

## Context

[ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
`.spv` files (and their JSON reflection siblings, once
[ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)
adds them) are, under
[ADR-0029](0029-shader-system-build-time-compilation-boundary.md),
produced by a build step rather than checked into the repository. This
raises questions no prior spec in this line has had to answer for a
binary build product: where do generated shader artifacts live in the
build tree, how does a consumer (a test executable, a demo, a future
Renderer call site) find them without a hardcoded developer-machine path,
what happens across `Debug`/`Release`/multi-config generators, and what
"reproducible" means for an artifact that is no longer a checked-in file
a reviewer can diff.

[docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
and
[docs/process/definition-of-done.md](../docs/process/definition-of-done.md)
already require builds to be reproducible and CI-buildable once CI exists
([docs/process/ci-strategy.md](../docs/process/ci-strategy.md)); this ADR
is the first to fix what that means for a generated (not checked-in)
binary artifact category.

## Decision

**Compiled `.spv` bytecode and its JSON reflection sidecar are ordinary,
non-checked-in build products, analogous to a `.obj`/`.lib` file — not
checked-in source, and not committed to version control by this spec's
implementation.** Reproducibility is achieved through pinned/recorded
tooling versions and deterministic build inputs, not through committing
binary output.

- **Output location: a single, configuration-independent directory under
  the build tree** — `${CMAKE_BINARY_DIR}/shaders/<relative-path-mirroring-source>/`
  (exact interpolation syntax a Plan-stage detail). Not nested under a
  `$<CONFIG>`-specific subdirectory (Visual Studio/multi-config
  generators): SPIR-V bytecode does not vary by C++ build configuration
  ([ADR-0029](0029-shader-system-build-time-compilation-boundary.md)), so
  compiling once into one shared location — rather than once per
  configuration — is both correct and avoids redundant compilation.
- **No developer-machine absolute path appears in any checked-in file, any
  CMake script, or any consumer's source code.** Every consuming CMake
  target (a test executable, a demo) locates its shaders via a CMake
  target property or generator expression Shader System's own CMake
  integration exports (e.g. an interface target property naming the
  output directory, or a generated small header/constant a Plan-stage
  detail may choose) — never a string literal path a developer typed by
  hand. This directly extends the existing rule
  [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)
  already follows informally ("each consumer's own CMake target copies the
  `.spv` files next to its own build output") into a formal, Shader-
  System-owned mechanism.
- **Test and demo consumption**: tests
  (`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`-equivalent
  future tests) and demos
  (`examples/minimal_renderer_demo`-equivalent future demos) locate their
  compiled shader artifacts via the same CMake-exported mechanism, not by
  assuming the test/demo's own binary output directory is the artifact's
  authoritative location — the build-tree shader output directory is
  authoritative; a test/demo consumes from it (by reading directly, or by
  a `POST_BUILD` copy step next to its own executable, mirroring the
  existing `minimal_renderer/README.md` convention) but never becomes a
  second authoritative copy other tooling must know about.
- **Reproducibility model**: a build is reproducible in the sense that the
  same GLSL source, compiled by the same `glslc` version with the same
  fixed flags (`--target-env=vulkan1.0`, matching the Vulkan Backend's
  unraised minimum supported API version — see
  [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
  existing precedent, carried forward unchanged by this spec), produces
  byte-identical SPIR-V output. This spec does **not** attempt to
  guarantee bit-for-bit reproducibility across *different* `glslc`/Vulkan
  SDK versions (shaderc/glslang's own optimization passes are not
  contractually guaranteed stable across releases) — instead:
  - The exact `glslc`/shaderc/glslang/SPIRV-Tools version string actually
    used for a given build is recorded in the reflection JSON sidecar's
    metadata (a `"compilerVersion"` field, populated by the Tools CLI
    invoking `glslc --version` or reading its own linked version info),
    giving every compiled artifact self-describing provenance without a
    separate checked-in note — superseding
    [shaders/minimal_renderer/README.md](../shaders/minimal_renderer/README.md)'s
    manual, per-directory plain-text compiler-version note, which this
    spec's own migration (see below) retires.
  - A recommended/tested Vulkan SDK version range is documented (exact
    text a Plan-stage detail, likely in `shaders/README.md`'s revised
    content) as guidance for contributors and CI image provisioning, the
    same way [ADR-0006](0006-dependency-management.md) already documents
    a category for large SDK versions without machine-enforcing an exact
    pin.
- **Freshness/staleness**: guaranteed correct by
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)'s
  `add_custom_command()` `DEPENDS` mechanism — a stale artifact (source
  changed, output not yet regenerated) cannot exist after a successful
  `cmake --build` invocation, by construction of CMake's own dependency
  graph, not by any Atlantis-side staleness check.
- **No artifact is ever checked into git by this spec's implementation.**
  `.gitignore` is extended (a Plan-stage detail names the exact pattern)
  to exclude the build-tree shader output directory, consistent with how
  every other build product (`.obj`, `.exe`, `CMakeCache.txt`) is already
  excluded.

### Migration: superseding ADR-0027's checked-in artifacts

- [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)
  **remains `Accepted` and is not rewritten** — it is a permanent record
  of a decision that was correct for Spec 0007's own moment, per
  [AGENTS.md](../AGENTS.md)'s "ADRs are not silently rewritten" rule. This
  ADR (0031), together with
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)–[ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md),
  **supersedes ADR-0027's *mechanism*** (checked-in, human-compiled
  bytecode) for every shader Shader System's build pipeline covers,
  starting with `shaders/minimal_renderer/`'s existing two shader pairs.
  ADR-0027's own "Migration boundary" section explicitly anticipated and
  authorized exactly this outcome ("once a future Shader System spec
  exists and is implemented, it becomes the sole producer of SPIR-V
  bytecode and reflection metadata... superseding this path entirely").
- **`shaders/minimal_renderer/minimal_mesh.{vert,frag}.glsl` become
  Shader System build inputs** — unchanged as GLSL source, now compiled
  by the Tools CLI instead of by a human running `glslc` manually.
  **`shaders/minimal_renderer/minimal_mesh.{vert,frag}.spv` (the
  checked-in bytecode) and the plain-text compiler-version note in
  `shaders/minimal_renderer/README.md` are retired** — removed once a
  future Plan/implementation switches
  `examples/minimal_renderer_demo`/the GPU test's `Material` construction
  from loading the checked-in `.spv` file to consuming Shader System's
  build-tree-generated artifact and reflection metadata instead. **This
  removal, and the call-site change it requires, is explicitly not
  performed by this spec** — Spec 0008 is a spec/ADR-only round (see its
  own Non-Goals); it is scoped as required follow-up for Shader System's
  implementation Plan.
- **No two parallel, simultaneously-authoritative shader-sourcing
  mechanisms persist once Shader System's Plan is implemented.** Once the
  migration above lands, ADR-0027's checked-in-bytecode path is fully
  retired for every shader it covered — not kept "just in case" alongside
  the new build-time path. A future spec introducing a shader Shader
  System's own scope does not yet cover (none identified by this spec) is
  the only case that could reintroduce a need for a comparable manual
  path, and would need its own explicit justification to do so.

## Consequences

### Positive

- Treating compiled shader artifacts as ordinary build products (not
  checked-in files) matches how every other compiled Atlantis artifact
  (object files, libraries, executables) is already handled — no new
  "binary artifacts get special repository treatment" precedent is
  introduced.
- Self-describing compiler-version provenance (recorded in the reflection
  JSON itself) is strictly more automatically verifiable than
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s
  manual plain-text note, with zero risk of the note and the actual
  compiler used silently drifting apart.
- A single, configuration-independent output location avoids redundant
  recompilation across `Debug`/`Release` and keeps multi-config generator
  support simple, rather than requiring per-configuration shader build
  logic.
- The explicit migration section gives Shader System's future
  implementation Plan an unambiguous, pre-reviewed target: retire
  `shaders/minimal_renderer/*.spv` and its README note, without that
  decision needing fresh architectural review at implementation time.

### Negative / Trade-offs

- Losing checked-in `.spv` bytes means a reviewer can no longer diff a
  binary artifact directly in a PR the way
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)'s path
  allowed (though that "diff" was never meaningful for binary content
  either — only the checked-in GLSL source was ever human-reviewable,
  which remains checked in and reviewable unchanged by this ADR).
- Not guaranteeing bit-for-bit reproducibility across different `glslc`
  versions is a real, accepted gap versus a fully hermetic/pinned
  toolchain — mitigated, not eliminated, by version recording and
  documented recommended-version guidance; a future spec could pursue a
  fully pinned/vendored compiler toolchain if this gap becomes a genuine
  problem (e.g. for CI reproducibility across machine images), which this
  ADR does not attempt to solve now.
- Every developer/CI machine needs a working, build-time-reachable
  `glslc` (already required by
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)) with no
  fallback to a previously-checked-in artifact if the toolchain is
  temporarily broken or unavailable — accepted as the direct cost of
  moving off a checked-in-artifact model, consistent with
  [ADR-0029](0029-shader-system-build-time-compilation-boundary.md)'s own
  accepted trade-offs.
- The migration work itself (retiring `shaders/minimal_renderer/*.spv`
  and its README note, updating the demo/test call sites) is real,
  deferred implementation work this ADR does not perform — a known,
  explicitly-scoped follow-up, not a silent gap.

## Alternatives Considered

- **Continue checking in compiled `.spv` bytes, now generated by the
  build rather than by a human, committing the build's own output back
  into the repository as a post-build step.** Rejected: conflates
  "generated build artifact" with "checked-in source," which
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)'s
  own golden-image rule already establishes a strong repository norm
  against ("never a silent regeneration step that a CI job runs and
  commits automatically") — the same reasoning applies here: an
  automatically-regenerated binary checked in by CI/a build step is
  exactly the kind of silent-drift risk that norm exists to prevent.
- **Nest shader output under `$<CONFIG>`-specific subdirectories**,
  mirroring how C++ object/library output is already configuration-
  scoped on multi-config generators. Rejected: SPIR-V bytecode carries no
  configuration-dependent content in this spec's scope (no debug-info
  variant, no configuration-gated shader permutation), so
  configuration-scoping would only add redundant recompilation and path
  complexity with no corresponding benefit.
- **A fully pinned, vendored shader-compiler toolchain** (e.g. `FetchContent`-
  fetching a specific shaderc/glslc binary release rather than relying on
  the host's installed Vulkan SDK), for stronger bit-for-bit
  reproducibility guarantees. Rejected for this round:
  [ADR-0028](0028-shader-system-source-language-and-compiler.md) already
  established that reusing the existing Vulkan-SDK requirement (rather
  than adding a second acquisition mechanism for essentially the same
  tool) is the minimal-new-dependency path; revisiting this trade-off is
  left to a future spec if reproducibility across CI/developer machine
  images becomes a concrete, measured problem.
- **Store generated artifacts in a fixed, non-build-tree location** (e.g.
  a top-level `generated/` directory outside `build/`, so it survives a
  `build/` directory deletion). Rejected: this reintroduces exactly the
  "generated content living outside the build tree" ambiguity this ADR's
  primary decision avoids — CMake's own build tree is already the
  established, understood location for build products in this
  repository, and a shader-specific exception would be a special case
  with no corresponding benefit.
