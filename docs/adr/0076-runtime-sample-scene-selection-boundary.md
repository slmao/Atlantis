# ADR 0076: Runtime Sample Scene Selection Boundary

- **Status:** Accepted
- **Date:** 2026-09-09
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-09-10; see this ADR's own
  [Acceptance Record — 2026-09-10](#acceptance-record--2026-09-10) below
- **Related Spec:** [specs/0032-runtime-sample-scene-selection.md](../specs/0032-runtime-sample-scene-selection.md) (`Approved`)

## Context

[Spec 0013](../specs/0013-runtime-host-foundation.md)'s own approved
"Decisions Requiring Human Review item 8" fixed Runtime's configuration
boundary as: everything the bootstrap needs is fixed at build/
composition time, zero required command-line surface, and "a trivial,
optional window-title/size command-line override may be added at Plan
stage without being architecturally significant." [ADR-0047](0047-runtime-host-executable-library-structure-and-test-boundary.md)'s
own Decision names that same item 8 as the explicit boundary on
`atlantis_runtime`'s own "minimal argument handling."

Spec 0032 asks for more than that pre-approved "trivial" carve-out: a
closed, three-way `--scene <name>` selection among discrete,
build-time-cooked scene *configurations* (not a cosmetic override),
plus `--list-scenes`/`--help`. This is a real widening of Runtime's own
configuration boundary, not an implicit extension of item 8's own
"trivial" scope — it needs its own decision record, reviewed alongside
Spec 0032, not silently read into item 8's own already-approved text.

## Decision

1. **Runtime's configuration boundary widens from "zero required CLI
   surface, one trivial optional cosmetic override" to: a closed,
   whitelisted, exactly-three-value `--scene <name>` flag, plus
   `--list-scenes` and `--help`.** No other command-line surface is
   introduced by this decision — no config file, no environment
   variable, no arbitrary path, no scene registration mechanism. The
   whitelist is fixed at exactly `integrated_showcase_demo`,
   `ibl_material_demo`, `pbr_normal_map_demo` — extending it to a
   fourth name is a decision for a future spec, not authorized here.
2. **The new parsing/mapping logic is a private, executable-scoped
   `cli.h`/`cli.cpp` pair — outside `atlantis_runtime_host`'s own
   public `include/` directory and not compiled into that library at
   all.** The same `cli.cpp` source file is compiled directly into
   both `atlantis_runtime` (the executable) and the existing
   GPU-independent `atlantis_runtime_tests` target — a shared source
   file across two already-existing executable targets, not a new
   library and not a new CMake target. `Atlantis::RuntimeHost`'s own
   real consumer count is not "exactly two" today (confirmed: at least
   `atlantis_runtime`, `atlantis_runtime_tests`,
   `atlantis_runtime_gpu_tests`, and `tests/image_regression/`'s own
   fixture and golden-generator targets already link it) — ADR-0047's
   own "exactly two" framing described that library's state as of its
   own 2026-08-20 acceptance date, not this ADR's own current baseline;
   this Decision does not restate or rely on that count, and does not
   change it either way, since the new logic never touches
   `Atlantis::RuntimeHost`. ADR-0047's own "two CMake targets" Decision
   (the library/executable split itself) is unchanged by this ADR —
   see that ADR's own new Proposed Amendment, which updates only its
   item-8 boundary reference.
3. **`RuntimeApplication`/`BootstrapConfig` gain no new field, method,
   or CLI awareness.** `main.cpp` builds the real `BootstrapConfig`
   from the parsed scene's own paths and calls
   `createRuntimeApplication(config)` exactly as today.
   `BootstrapConfig`'s own existing "populated by the caller from
   CMake-injected compile definitions, no command-line parsing inside
   Atlantis::RuntimeHost's own composition logic" contract (its own
   header comment) stays literally true, with no reinterpretation
   needed: the new CLI code is not inside `Atlantis::RuntimeHost`, so
   that library's own composition logic still never sees `argv` in any
   sense.
4. **All parsing, whitelist validation, and print-and-exit paths
   (`--help`, `--list-scenes`, any error) run strictly before
   `createRuntimeApplication()` — before any window, Platform session,
   or Vulkan device exists.**

## Consequences

### Positive

- Closes the real gap Spec 0032 identifies (two already-shipping,
  already-tested scenes reachable only through non-product binaries)
  with the narrowest possible boundary widening — three fixed names,
  nothing arbitrary.
- No new CMake target and no new module boundary — `cli.cpp` reuses
  the two already-existing executable targets directly, and stays
  entirely outside `atlantis_runtime_host`'s own public surface, so
  that library's own real (and larger-than-two) set of consumers gains
  no header they never asked for.
- Explicit, reviewed widening of Spec 0013 item 8's own boundary,
  rather than an implicit one a future reader would have to notice was
  never actually approved.

### Negative / Trade-offs

- Runtime's configuration boundary is measurably wider than Spec 0013
  originally fixed — a real, if narrow and whitelisted, increase in
  startup-time surface a future contributor must keep closed (no
  silent whitelist growth without its own review).
- Two governance documents (this ADR and ADR-0047's own Amendment) are
  needed instead of one, because ADR-0047's own Accepted text
  explicitly names the boundary this ADR moves — the alternative
  (silently reinterpreting ADR-0047's existing reference) was rejected
  as exactly the kind of un-reviewed drift AGENTS.md's Golden Rule
  exists to prevent.

## Alternatives Considered

See Spec 0032's own Alternatives Considered section (general scene-
registry system; arbitrary `--scene-file <path>`; a third CMake
target for the CLI layer) — reproduced there rather than duplicated
here, since each was rejected using evidence gathered while answering
that Spec's own Requirements, not a separate architectural trade-off
this ADR needs to re-litigate independently.

## Acceptance Record — 2026-09-10

**Status: Accepted.** Recorded against
[PR #139](https://github.com/slmao/Atlantis/pull/139). Human Review's
own words: *"我分别认可 Spec 0032、ADR-0076，以及 ADR-0047 的 2026-09-09
Proposed Amendment"* ("I separately approve Spec 0032, ADR-0076, and
ADR-0047's 2026-09-09 Proposed Amendment").

This approval covers this ADR's complete Decision as corrected in this
round, with no further change to the Decision/Consequences/
Alternatives text above — in particular: the closed, three-value
`--scene`/`--list-scenes`/`--help` boundary (Decision 1); the private,
executable-scoped `cli.h`/`cli.cpp` pair, outside
`atlantis_runtime_host`'s own public `include/` directory and not
compiled into that library, shared as a compiled-twice source file
between `atlantis_runtime` and `atlantis_runtime_tests` rather than a
new library or CMake target, and the real (not "exactly two")
`Atlantis::RuntimeHost` consumer count this Decision does not restate
or rely on (Decision 2); `RuntimeApplication`/`BootstrapConfig` gaining
no new field, method, or CLI awareness, with `BootstrapConfig`'s own
existing no-CLI-parsing contract staying literally true (Decision 3);
and all parsing/validation/print-and-exit paths running strictly before
`createRuntimeApplication()` (Decision 4). Approved independently of
[Spec 0032](../specs/0032-runtime-sample-scene-selection.md)
(`Approved`) and ADR-0047's own
[Accepted Amendment — 2026-09-09](0047-runtime-host-executable-library-structure-and-test-boundary.md#accepted-amendment--2026-09-09)
— this is not a blanket approval of one implying the others.

**This approval authorizes drafting Plan 0032 only once
[PR #139](https://github.com/slmao/Atlantis/pull/139) itself has merged
to `main` — not before, and not Implementation of any kind.**

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-10, accepting this ADR in full, as corrected
this round, with no further change.
