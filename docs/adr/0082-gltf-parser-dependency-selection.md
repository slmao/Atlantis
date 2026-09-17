# ADR 0082: glTF Parser Dependency Selection

- **Status:** Accepted
- **Date:** 2026-09-16 (accepted 2026-09-17)
- **Deciders:** slmao — Human Review, chat confirmation, no reviewing
  PR, alongside [Spec 0037](../specs/0037-gltf-importer.md)'s own
  Approval (2026-09-17)
- **Related Spec:** [Spec 0037: glTF 2.0 Importer](../specs/0037-gltf-importer.md) (`Approved`)
- **Related ADR(s):** [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (`Accepted`) — this ADR explicitly revisits and narrows one line of
  that ADR's own "No new third-party dependency" Decision (the "No
  `glTF`/`Assimp`... parser is added" sentence, and its own Alternatives
  Considered entry naming `cgltf` as "a strong future candidate, not a
  rejected one" once "a real multi-mesh, multi-material authoring
  workflow exists to justify it"). [Spec 0036](../specs/0036-bistro-parity-roadmap.md)
  (`Approved`) — workflow ①'s own named ADR obligation (b): "Tools
  subsystem/dependency choice... whether to adopt a third-party glTF
  parser (`cgltf`/`tinygltf`) or hand-roll one, explicitly reopening
  ADR-0045's own 'no glTF/Assimp dependency' line." [ADR-0006](0006-dependency-management.md)
  (dependency-management gate this ADR satisfies).

## Context

ADR-0045's own "No new third-party dependency" Decision (Accepted
2026-08-19) rejected adopting `glTF`/`Assimp` as this repository's
authoring-source format, for a stated, narrow reason: that ADR's own
scope was "exactly one hand-known, eight-vertex cube" — no consumer
existed for a general interchange-format parser's own scene-graph/
material/texture/skinning surface. That same ADR's own Alternatives
Considered section named the exact reopening condition: "Revisit once a
real multi-mesh, multi-material authoring workflow exists to justify
it... `cgltf`'s small size, permissive (MIT) license, and single-header/
`FetchContent` compatibility make it a strong future candidate, not a
rejected one."

Spec 0036 (Bistro Parity Roadmap, `Approved`) names exactly that
condition as now satisfied — the Bistro glTF asset (Spec 0037's own
Requirement 1, re-confirmed below) is a real, externally-authored,
multi-thousand-primitive, 254-material scene, structurally nothing like
ADR-0045's own eight-vertex-cube scope. This ADR is the decision that
condition was deferred to.

**This ADR narrows one line of ADR-0045's own Decision only** — the
"no `glTF`/`Assimp` parser" sentence — leaving every other item in that
same "No new third-party dependency" paragraph (no UUID-generation
library, no general hashing library, no JSON/YAML/TOML parser) fully
intact and unaffected, since none of those needs is created by this
decision (a self-contained glTF parser brings its own internal JSON
handling — see Decision below — so no *separate* JSON library dependency
is added on top of it). ADR-0045's own file is not edited by this ADR —
per AGENTS.md's "Accepted ADRs are not silently rewritten" rule and this
repository's own established precedent (ADR-0043/0044/0045 were
themselves split from one combined draft so each independently-evolvable
decision gets its own record), this is drafted as a new, sibling ADR
that explicitly narrows one named line of an already-`Accepted` ADR,
exactly the relationship ADR-0045's own Alternatives Considered section
already anticipated in its own text.

### Real evidence gathered against the actual Bistro asset

This ADR's own recommendation is grounded in real measurement, not
assumption. Spec 0037's own pre-drafting investigation (full detail in
that Spec) downloaded and directly inspected the recommended Bistro
source (`NVIDIA-RTX/RTXDI-Assets`' own `bistro/bistro.gltf`, MIT
licensed) and found:

- `extensionsUsed`: **`KHR_materials_pbrSpecularGlossiness`**,
  **`KHR_materials_transmission`**, **`MSFT_texture_dds`** — no other
  extensions; `extensionsRequired` is empty.
- 254 materials: 234 use `KHR_materials_pbrSpecularGlossiness` (the
  *majority* of the file's own material data is **not** expressed in
  core glTF metallic-roughness fields at all), 18 use
  `KHR_materials_transmission` (glass).
- 551 meshes/primitives, all `TRIANGLES` mode; 0 cameras, 0 skins, 0
  animations, no `KHR_lights_punctual` block anywhere in the file.

## Decision

**Adopt `cgltf`** (single-header, C99, MIT-licensed,
`https://github.com/jkuhlmann/cgltf`) as the glTF 2.0 parsing dependency
for the new `src/tools/gltf_importer/` subsystem (ADR-0084), via the
existing pinned `FetchContent` mechanism ([ADR-0006](0006-dependency-management.md)),
matching the `stb`/`ADR-0041` precedent for a small, permissively-
licensed, single-file source dependency.

**Candidate matrix** (this ADR's own required comparison,
[Spec 0037](../specs/0037-gltf-importer.md)'s Investigation 3):

| | `cgltf` | `tinygltf` v3 | Hand-written |
|---|---|---|---|
| License | MIT | MIT | N/A (this repo's own code) |
| Distribution | Single header (`cgltf.h`, ~206 KB) | 3 files (`tiny_gltf_v3.h` + `.c` + `tinygltf_json_c.h`, ~330 KB combined) | N/A |
| External deps | None (bundled minimal JSON parsing) | None mandatory (bundled JSON backend; `stb_image` opt-in, disabled here) | None |
| Native `KHR_materials_pbrSpecularGlossiness` support | **Yes** — typed `cgltf_pbr_specular_glossiness` struct, confirmed by direct source inspection | Not directly confirmed this round (v3 is a from-scratch C11 rewrite; extension-struct coverage not individually verified) | N/A — would be hand-written either way |
| Native `KHR_materials_transmission` support | **Yes** — typed `cgltf_transmission` struct, confirmed | Not directly confirmed this round | N/A |
| Native `KHR_lights_punctual` support | **Yes** — typed `cgltf_light` struct, confirmed (unexercisable against Bistro itself, which ships none, but real for future glTF content per Spec 0036's own multi-light workflow) | Not directly confirmed this round | N/A |
| `MSFT_texture_dds` support | No typed struct; reachable via `cgltf`'s own generic unrecognized-extension JSON pass-through (`cgltf_texture.extensions`) — a small amount of manual field extraction needed either way | Same caveat, not individually verified | N/A |
| Maturity / activity | Established since 2018; ~1,988 GitHub stars; last pushed 2026-02-02; 62 open issues | `v3` is a **recent, from-scratch rewrite** (latest tag `v3.0.1`); ~2,526 stars; last pushed 2026-08-02 (very recently active); 6 open issues; extensive fuzzing/hardening claimed in its own README | N/A |
| Already named by this repository's own prior ADR | **Yes** — ADR-0045's own Alternatives Considered names `cgltf` specifically | No | N/A |
| Integration cost into `src/tools/` (C++20 codebase) | Trivial — a C99 single header compiles and links directly from C++ | Slightly higher — three files, a `.c` translation unit needs its own compile step alongside the header, C11 (not C99) | Highest — full custom JSON + glTF schema parser, an open-ended, multi-week reimplementation of a solved problem |

**Recommendation rationale:** `cgltf` is chosen over `tinygltf` v3 for
three concrete reasons, not a coin flip: (1) it is the candidate
ADR-0045 itself already named as the anticipated future choice — reusing
that continuity rather than introducing a new preference; (2) this ADR's
own direct source inspection **confirmed** `cgltf` has first-class,
typed struct support for all three extensions Bistro's own real file
actually uses two of (`KHR_materials_pbrSpecularGlossiness`,
`KHR_materials_transmission`) plus `KHR_lights_punctual` (needed by a
future glTF asset per Spec 0036's multi-light workflow, even though
Bistro itself does not exercise it) — `tinygltf` v3's own equivalent
coverage was not independently verified this round, a real, disclosed
gap in this ADR's own diligence, not a claim either way; (3) a single
header is a strictly smaller build-integration footprint than a
header-plus-translation-unit-plus-JSON-backend distribution, for this
repository's own established "small, pinned source dependency" taste
(ADR-0041's `stb` precedent).

`tinygltf` v3 remains a credible, real, more-recently-active alternative
— not rejected as unsuitable, only as this ADR's own second choice given
`cgltf`'s confirmed extension coverage and ADR-0045's own prior
continuity. If Plan-stage implementation finds a real integration
blocker with `cgltf` (Risks below), `tinygltf` v3 is the named fallback,
not a return to hand-rolling.

**Hand-writing a parser is rejected** for the same reason ADR-0045
itself rejected adopting glTF in the first place, inverted: a hand-
rolled glTF JSON+binary parser reimplements a large, versioned,
extension-bearing public specification this project has no comparative
advantage authoring itself, for a dependency `cgltf` already solves at
MIT-license, single-header cost — exactly the kind of "reasonable but
uncoordinated" effort AGENTS.md's Golden Rule exists to avoid when a
proven, small, permissively-licensed alternative exists.

## Consequences

### Positive

- `cgltf`'s own native typed support for both extensions Bistro's real
  materials actually use removes a real, otherwise-necessary hand-
  written JSON-extension-parsing burden from workflow ①'s own
  implementation.
- Single-header, `FetchContent`-pinned, matches this repository's own
  established `stb` precedent exactly — no new build-system shape.
- Continuity with ADR-0045's own already-recorded reasoning; Human
  Review is not asked to evaluate a preference sprung fresh.

### Negative / Trade-offs

- A new third-party dependency, however small — ADR-0045's own "zero new
  third-party dependency" consequence for Asset System's *original*
  scope no longer holds for this new Tools-subsystem scope; disclosed
  here explicitly, not silently.
- `cgltf`'s own maintenance cadence (last pushed 2026-02-02, 62 open
  issues) is real but slower than `tinygltf` v3's (2026-08-02, 6 open
  issues) — a genuine, disclosed trade-off against the recommendation
  above, not hidden by it.
- No typed `MSFT_texture_dds` support in `cgltf` — the importer's own
  Plan-stage implementation must read that one extension's JSON via
  `cgltf`'s generic unrecognized-extension pass-through, a small,
  bounded, but real piece of hand-written parsing this decision does not
  eliminate.

## Alternatives Considered

See the candidate matrix above (`tinygltf` v3, hand-written) — both
evaluated in the Decision section itself rather than restated here,
since each candidate's own trade-off is the direct basis for the
recommendation, not a separate rejected-alternatives list.

## Risks & Open Questions

- `cgltf`'s own real extension-parsing behavior (particularly for
  `KHR_materials_pbrSpecularGlossiness`/`KHR_materials_transmission`)
  has been confirmed present via direct source inspection of
  `cgltf.h`'s own struct/parsing-function definitions, **not** via
  actually compiling and running it against the real `bistro.gltf`
  file — that remains Plan-stage/Implementation-stage verification, not
  claimed as done here.
- `MSFT_texture_dds` extraction via `cgltf`'s generic extension
  pass-through is untested; if it proves more awkward than expected at
  implementation time, the `tinygltf` v3 fallback named above is the
  documented escape hatch, not a silent workaround.
