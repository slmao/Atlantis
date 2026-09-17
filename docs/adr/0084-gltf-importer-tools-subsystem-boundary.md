# ADR 0084: glTF Importer Tools Subsystem Boundary

- **Status:** Accepted
- **Date:** 2026-09-16 (accepted 2026-09-17)
- **Deciders:** slmao — Human Review, chat confirmation, no reviewing
  PR, alongside [Spec 0037](../specs/0037-gltf-importer.md)'s own
  Approval (2026-09-17)
- **Related Spec:** [Spec 0037: glTF 2.0 Importer](../specs/0037-gltf-importer.md) (`Approved`)
- **Related ADR(s):** [ADR-0082](0082-gltf-parser-dependency-selection.md)
  (`Accepted`) — the dependency this module links.
  [ADR-0083](0083-gltf-to-atlantis-asset-format-mapping.md) (`Accepted`)
  — the mapping logic this module implements.
  [ADR-0043](0043-asset-system-module-boundary.md) (`Accepted`) — the
  Asset System module boundary this new Tools subsystem depends on and
  must not violate. [AGENTS.md](../../AGENTS.md) Module boundaries —
  the top-level module list this ADR adds to (`Atlantis Tools`, already
  named).

## Context

AGENTS.md already names `Atlantis Tools` as a top-level module (the home
of `atlantis_asset_cooker` and `atlantis_shader_compiler`, both under
`src/tools/`). Spec 0036's own ADR obligation for workflow ① names, as
one surface: "Tools subsystem/dependency choice — a new
`src/tools/gltf_importer/` module boundary." This ADR is that decision.

## Decision

### A new `src/tools/gltf_importer/` module, mirroring
`atlantis_asset_cooker`'s own established structure exactly

Confirmed by direct inspection of `src/tools/asset_cooker/CMakeLists.txt`:
that target's own established shape is a small `_lib` **static library**
(the actual parsing/mapping/orchestration logic, independently unit-
testable — `tests/tools/asset_cooker/` already links it this way) plus a
thin executable (`main.cpp`, argument handling only). `atlantis_asset_cooker_lib`
links `Atlantis::AssetSystem` and `Atlantis::Core` **only** — no RHI,
Renderer, or Shader System dependency — with third-party dependencies
(`Stb::Stb`, for its own texture-cook `stbi_load()` call) kept **PRIVATE**,
never propagated to anything linking the library.

**This ADR adopts that exact shape** for the new subsystem:

- `atlantis_gltf_importer_lib` (STATIC) — the real parsing (via
  ADR-0082's `cgltf`), mapping (ADR-0083's D1-D8), and cooked-artifact-
  emission logic. Links `Atlantis::AssetSystem` and `Atlantis::Core`
  **PUBLIC**, matching `atlantis_asset_cooker_lib`'s own identical
  dependency set exactly — this new subsystem needs nothing else from
  the rest of the engine, the same way the existing cooker doesn't.
  `Cgltf::Cgltf` (ADR-0082) is linked **PRIVATE** — no caller of this
  library needs to know `cgltf` exists, mirroring `Stb::Stb`'s own
  existing precedent exactly.
- `atlantis_gltf_importer` (executable) — thin `main.cpp`, argument
  handling only, mirroring `atlantis_asset_cooker`'s own executable
  shape.
- `tests/tools/gltf_importer/` — GPU-independent unit tests linking
  `atlantis_gltf_importer_lib` directly, mirroring
  `tests/tools/asset_cooker/`'s own existing precedent (Spec 0037's own
  Testing & Verification Plan).

### Relationship to `atlantis_asset_cooker` — a separate tool, not a
merged one

**Decision:** the glTF importer is its **own** executable/library pair,
**not** a new mode folded into `atlantis_asset_cooker` itself. Three
concrete reasons: (1) `atlantis_asset_cooker`'s own established
responsibility (per its own file/Plan 0012 comment) is cooking *already-
authored* Atlantis-native source files (`.mesh.txt`/`.material.txt`/
etc.) into runtime artifacts — the glTF importer's own responsibility is
a fundamentally different operation, *producing* that authored content
(or, per ADR-0083 D1, bypassing it entirely for mesh data) from an
external format; conflating "cook Atlantis source" and "import glTF
source" into one binary's own argument grammar blurs a real distinction
a future contributor would have to rediscover. (2) `cgltf` (ADR-0082)
becomes a link dependency only the importer needs — keeping it out of
`atlantis_asset_cooker_lib`'s own dependency set avoids growing that
already-shipping tool's own build surface for a capability it does not
use. (3) Matches this repository's own established precedent of one
executable per distinct tooling concern (`atlantis_asset_cooker` vs.
`atlantis_shader_compiler` are already two separate binaries, not one
combined tool with subcommands).

**What the glTF importer *does* reuse from `atlantis_asset_cooker`:**
its own existing cooking logic for materials and scene-graph nodes
(ADR-0083 D5/D6) — the importer generates `.material.txt`/`.scene.txt`
authoring-source text and hands it to the *existing, unmodified*
`atlantis_asset_cooker` as a second pipeline stage, exactly the way a
human author's own hand-written source files already flow through it
today. Only mesh data (ADR-0083 D1) bypasses this two-stage flow,
emitting a cooked `.amesh` artifact directly from within the importer
itself.

### Dependency surface — bounded, matching Spec 0036's own Tools-only
scope

`atlantis_gltf_importer_lib` depends on: `Atlantis::AssetSystem`,
`Atlantis::Core`, `cgltf` (ADR-0082, PRIVATE). It depends on **none** of:
RHI, Renderer, RenderGraph, Shader System, Vulkan Backend, Platform,
Runtime, or World — matching `atlantis_asset_cooker_lib`'s own identical
restriction, and matching ADR-0083 D4's own explicit refusal to decide
a texture-format question that would require an RHI dependency this
module does not have and should not acquire.

### CMake integration

New `add_subdirectory(gltf_importer)` under `src/tools/CMakeLists.txt`
(mirroring `asset_cooker`'s own existing entry), a new `FetchContent`
declaration for `cgltf` (mirroring `stb`'s own existing declaration
shape, ADR-0041/ADR-0006), and a new `Cgltf::Cgltf` imported target —
**Plan-stage detail**, not fixed here beyond naming that these three
additions are the expected shape, following existing precedent exactly.

## Consequences

### Positive

- Reuses a proven, already-shipping structural pattern
  (`_lib`/executable split, PUBLIC-vs-PRIVATE dependency discipline) —
  no new organizational precedent for a future contributor to learn.
- Keeps `cgltf` (a new dependency, ADR-0082) scoped to exactly the one
  subsystem that needs it.
- Reusing `atlantis_asset_cooker` unmodified for the material/scene-
  graph half of the pipeline (ADR-0083 D5/D6) means this ADR introduces
  no change at all to an already-shipping, already-tested tool.

### Negative / Trade-offs

- A second, structurally-similar-but-separate tools binary is a real,
  if small, additional build/maintenance surface versus folding the
  capability into the existing cooker.
- The importer's own two-stage flow for materials/scene nodes (generate
  source text, then invoke the existing cooker) versus mesh data's own
  direct-artifact-emission (ADR-0083 D1) is a real, disclosed asymmetry
  within one tool — not a single, uniform pipeline shape.

## Alternatives Considered

- **Fold glTF import into `atlantis_asset_cooker` as a new mode/flag.**
  Rejected — see Decision's own three-reason argument above.
- **A `gltf_importer` library with no separate executable**, invoked
  only from tests/Plan-stage tooling. Rejected: every other Tools
  subsystem in this repository ships as a real, standalone executable a
  human can run directly against a real file — no precedent for a
  library-only tool, and Spec 0037's own end-to-end verification
  (Testing & Verification Plan) needs a real invokable binary against
  the real Bistro asset.

## Risks & Open Questions

- The exact CMake `FetchContent` pin (which `cgltf` tag/commit) is
  Plan-stage detail, not fixed here — ADR-0082 names `v1.15` as the
  latest tag at investigation time, not a hard pin.
- Whether `tests/tools/gltf_importer/`'s own end-to-end test vendors a
  copy of the real Bistro asset into the repository, or fetches it at
  test time, is Spec 0037's own Testing section's decision, not this
  ADR's.
