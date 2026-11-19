# Spec 0118 — Remove an Unused Registered Base Color Texture

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-20
- **Related Plan:** [Plan 0118](../plans/0118-registered-texture-removal.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0169](../adr/0169-prove-unused-before-removing-managed-textures.md) (Proposed)

## Summary

From Assets, check one registered sRGB PNG for project references and,
when the host can prove it is unused, remove its registration. Publish a
named generation and explicitly restart into it to observe the removal.
The original inbox PNG and earlier complete generations remain available.

## Motivation

[0116](0116-registered-texture-reimport.md) can list known direct material
dependents of a texture in the attached catalog. A zero-result lookup
describes that catalog only. A registered or declared recipe may have
changed since the catalog was cooked, and the current view may omit
producers. [0117](0117-registered-texture-rename.md) adds a separately
stored label that must be settled with the registration.

## Requirements

### R1 — Remove one verified registration

Support one 0095 managed `source.png` texture in a Windows development
Runtime attached to a named generation that matches the trusted managed
project's committed state. Use the ordinary managed build profile. A
selected saved-slot build or an unverified producer configuration is
ineligible for removal. Resolve its GUID, role, canonical logical key,
original registration fragment and sealed PNG against the active producer view.
The registration unit must be an ordinary directory containing exactly
the canonical PNG, seal and fragment. Unexpected entries block removal.
Use the generation and GUID as inputs. Derive paths and the original
registration token on the host. Require the existing managed write
authorization and workspace ownership. The check can report a blocker
even if the reviewed material sources differ from the active view.
Only a confirmable verdict and mutation require full agreement with
committed state. Seed-only and out-of-date snapshots remain usable for
reading.

Remove the sealed registration unit, its effective catalog entry and any
0117 name override for that GUID. Preserve the original
`inbox/<name>/source.png` byte for byte when present. A missing inbox PNG
does not by itself prevent removal of a valid sealed registration. Reject
an inbox path that cannot be proved separate from the unit and job paths,
including aliases through junctions or hard links. Keep other
registrations, material recipes, bindings and prior named
generations intact. A later explicit Register from the kept inbox is a
new registration with a new GUID. No reference is rebound automatically.

### R2 — Prove the project has no blockers

The host must establish the proof in
[ADR-0169](../adr/0169-prove-unused-before-removing-managed-textures.md).
Any direct dependency on the GUID in the active immutable catalog blocks
removal. An authoritative source reference also blocks it, including a
material recipe that is newer than its cooked record or a material unused
by the open scene. Check every texture slot in the existing material
schema, including base color and emissive, and include material recipes
produced by supported importers. The host never writes a reference away.
Today scene sources reach textures through materials. A saved scene
referencing such a material is protected by the material's direct edge;
checking only the scene in the viewport is insufficient.

Report `Referenced` with the known blocking asset or logical source and,
where derived from a recipe, the slot. Report `CheckIncomplete` for missing,
unreadable, unsafe, invalid, over-budget or unsupported evidence, or when
the host cannot account for all reference-bearing inputs. If a known
reference is found before an incomplete scan stops, show it and mark the
diagnostic list incomplete. Display an unused verdict only after the
complete check. Bound the list and distinguish its truncation from an
incomplete proof. A safely established reference takes precedence over
an incomplete or stale verdict. Readable evidence of a newer recipe may
explain refusal even when that newer recipe could never authorize a write.

The proof covers the one managed project and its declared build inputs.
Previously published generations are self-contained. Other checkouts,
exported builds and private client drafts are outside the proof. A client
with a draft against an old generation may keep viewing that generation;
existing write guards must prevent committing its obsolete asset reference
after removal. An unsupported or partially configured workspace cannot
claim complete coverage.

### R3 — Bind confirmation to checked state and publish safely

The host returns a short-lived check ID for the GUID, generation and
checked inputs. After a complete unused verdict, require explicit
confirmation with that ID. The host verifies the existing writer ownership
and admits one work item. The worker revalidates the reviewed state before
starting the candidate build and again before any registration or name-map
change. Refuse a changed snapshot, registration, source set or checked
source bytes, even if a new check might also find no references. Return
`StaleCheck` at admission or
settle the admitted job as `Validation/IdentityChanged` with stale-check
guidance. Expired, consumed and foreign check IDs require another check.
A fresh check and explicit confirmation are required for retry.

Build and validate a private candidate with only the target excluded.
Use the existing catalog, cooking and publication paths, and verify the
exact removal and the unchanged surviving assets and dependency edges
before commit. Use ADR-0169 for the backup, name-map settlement and
activation-equivalent validation. A clean refusal or proven rollback
leaves the existing registration and name map intact. Uncertain outcomes
hold managed writes and retain the local recovery record. An already
verified publication stays successful if cleanup subsequently fails.
There is no automatic retry, Register Resume or live activation.

After success, show the published ID and direct the operator to the
existing explicit restart flow. The attached catalog, preview, scene
document and scene history continue to belong to the attached Runtime.
After restart into the published generation, the removed GUID cannot be
read through `assets.get` or found in Assets or a reference picker.

### R4 — Shared clients and Editor feedback

Use additive shared calls for starting the check, querying its receipt,
and confirming removal. The check receipt and blockers are typed. The
confirmation starts the existing rebuild job and reports published ID,
failure stage and recovery diagnostics through its existing status.
Keep old reads and writes compatible; clients connected to an older host
get an explicit Unsupported outcome for the new operation. Editor, CLI,
native and C# SDKs, and MCP use this same contract. No path, force flag,
or client assertion of completeness authorizes removal.

In Assets, offer **Remove registration…** for a visible, eligible
texture. Explain that the original PNG is kept. Ask for confirmation only
after a matching complete-unused reply. Otherwise show a blocking source
or an explanation of incomplete/stale results. Recheck GUID, generation,
selection and connection before send, ignore late results for superseded
checks, and keep an admitted job tied to its original GUID if selection
changes. Cancel before send without writing. A lost response preserves
the connection error and tells the user when the outcome is unknown.
Query the known job ID when available; never infer ownership from the
latest job for the same asset or silently resend. Follow 0093 and keep
the existing Assets browsing and preview behavior.

## Architectural impact and alternatives

[ADR-0169](../adr/0169-prove-unused-before-removing-managed-textures.md)
records the shared confirmation contract, completeness boundary and
source/publication recovery. The design reuses the managed worker and
immutable generation model. A catalog-only refusal check misses current
sources, and deleting files before cooking leaves an ambiguous registry
if the build fails. A permanent reverse index and project-wide asset
garbage collection are disproportionate to one explicit removal.

## Verification and scope limits

[Plan 0118](../plans/0118-registered-texture-removal.md) maps Windows 100%
remove/publish/restart and referenced-refusal/preview cases to the
requirements, plus source-only blockers, incomplete inputs, stale
confirmations and failure settlement. Force removal, batch operations,
source-file deletion, dependency repair, moves, and removal of other
asset roles are future work.
