# ADR 0076: Runtime Sample Scene Selection Boundary

- **Status:** Proposed
- **Date:** 2026-09-09
- **Deciders:** slmao (drafted by Claude Code at explicit human
  direction; pending Human Review)
- **Related Spec:** [specs/0032-runtime-sample-scene-selection.md](../specs/0032-runtime-sample-scene-selection.md)

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
2. **The new parsing/mapping logic lives inside the existing
   `atlantis_runtime_host` static library** (new source files only) —
   `atlantis_runtime` (the executable) still depends on
   `Atlantis::RuntimeHost` alone, and `Atlantis::RuntimeHost` still has
   exactly its two existing consumers (`atlantis_runtime` itself and
   `tests/runtime/`'s own GPU-independent tests). ADR-0047's own "two
   CMake targets" Decision is unchanged by this ADR — see that ADR's
   own new Proposed Amendment, which updates only its item-8 boundary
   reference, not its target-count Decision.
3. **`RuntimeApplication`/`BootstrapConfig` gain no new field, method,
   or CLI awareness.** The new logic only *produces* a `BootstrapConfig`
   value from a parsed scene name — `main.cpp` still calls
   `createRuntimeApplication(config)` exactly as today, and
   `BootstrapConfig`'s own "populated by the caller from CMake-injected
   compile definitions, no command-line parsing inside
   Atlantis::RuntimeHost's own composition logic" contract (its own
   header comment) is read narrowly: the *composition* logic still
   never parses `argv`; the new, separate CLI-parsing code that
   *produces* a `BootstrapConfig` is not that composition logic.
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
- `atlantis_runtime_host`'s own existing "private, testable, no
  external consumer" shape (ADR-0047) already fits this new logic
  exactly — no new CMake target, no new module boundary.
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
