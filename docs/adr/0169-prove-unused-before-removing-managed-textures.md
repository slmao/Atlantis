# ADR 0169: Prove a Managed Texture Is Unused Before Removing Its Registration

- **Status:** Accepted
- **Date:** 2026-11-20
- **Deciders:** Atlantis maintainer
- **Acceptance:** Atlantis maintainer, 2026-11-20, [PR #401](https://github.com/slmao/Atlantis/pull/401) at `3ffa6d55`, manually merged as `5c7fb5a1`.
- **Related Spec:** [Spec 0118](../specs/0118-registered-texture-removal.md)

## Context

[ADR-0143](0143-shared-cooked-asset-catalog-queries.md) describes an
immutable catalog snapshot. It cannot prove the absence of references
in current project sources. A removal also changes both the sealed
registry (ADR-0154) and, when present, the generation-scoped name map
([ADR-0168](0168-generation-scoped-asset-display-names.md)). Those changes
must settle with one publication outcome.

## Decision

1. **Define a closed proof domain.** Use the active cooked catalog and
   regenerate the producer inventory and effective source declarations
   with the supported ordinary managed CMake profile and an empty
   selection. Reconcile the current roster against the base catalog
   source, each committed managed fragment and the verified checkout,
   content and registry roots. The frozen inventory describes the active
   generation. Resolve each authored material to its authoritative file
   in the declared checkout or committed registry and parse it with the
   existing material parser. The staging copy of a mutable recipe is
   evidence only. Include every declared material producer irrespective
   of scene use. For generated materials, use importer and assembly
   evidence freshly built from the unmasked graph unless exact current
   input and output provenance is available. Include importer-resolved
   inputs and catalog mapping. A stamp or cached catalog alone cannot
   establish provenance. A missing, duplicate or unaccounted producer,
   unrecognized reference-bearing source, or unresolved import mapping
   prevents Ready. Proving the full material roster covers the indirect
   edge from saved scenes through materials under the existing grammar.
2. **Bind a check to what was read.** Run a bounded job on the existing
   managed worker only when requested, within the existing job deadline
   and applicable source limits. Refresh evidence in owned staging
   while leaving the registry, name map and named generations untouched.
   Use existing dirty-state discipline when a refresh may leave staging
   unsuitable for reuse.
   Capture the host instance, generation, target, canonical source roster,
   exact content digests for reference-bearing inputs, declarations and
   relevant importer/configuration inputs, and committed registry/name
   state. Use the host's existing workspace writer ownership. Verify
   stable membership and bytes across the check, and re-enumerate and
   recheck reviewed bytes after confirmation and before the first
   commit mutation. Reject additions, removals, mapping changes and
   content drift even when the GUID is still unused. Timestamps or
   generation ID equality alone cannot prove this. The trusted local
   workspace requires independent configure/build and direct filesystem
   edits to checked roots to be quiescent during checks and commit.
   Managed writers share the existing exclusivity boundary. Return an
   incomplete check when stability cannot be established.
3. **Use one bounded check receipt.** Add `assets.checkTextureRemoval`,
   `assets.textureRemovalCheckStatus` and `assets.removeTexture` to the
   shared surface. Check status echoes the check ID, GUID and expected
   generation and distinguishes Checking, Ready, Referenced,
   CheckIncomplete and StaleCheck. An evicted or unknown ID is a
   refusal. Ready requires complete proof and agreement with committed
   state. An independently verified blocker yields Referenced even if
   other inputs are missing or stale. Report up to 16 ordered blockers,
   with truncation and incomplete coverage indicated separately.
   Catalog-only blockers need no slot. Keep one check receipt per host,
   with a Ready receipt expiring two minutes after the Ready result or
   when superseded. An authorized connection to that host confirms
   with the GUID, expected generation and opaque check ID. The receipt
   grants no write rights. Check validity and consume the receipt
   atomically with single-flight admission. Once admitted, the job
   uses the existing deadline and rebuild status. The confirmation
   dialog holds no worker slot. Writer ownership keeps its current lifetime.
4. **Prove the exact candidate before mutation.** Reserve a distinct
   job-owned staging selection that identifies exactly one existing unit
   by its canonical token and GUID. Refuse absent, conflicting or ambiguous
   units. Omit it only from this candidate's producer roster and effective
   catalog source. Reuse the cooker/catalog pipeline and require
   current evidence from the configured producers. Keep the unmasked
   reference proof separate from the candidate roster. Recheck the
   reviewed inputs and the expected one-producer projection without
   treating the masked graph as evidence of completeness. Validate the full
   candidate with activation rules, including the inventory header and
   all frozen files. Require the exact one-record and one-producer delta,
   no edge to the target, and unchanged surviving identities, sources,
   dependency sets and cooked outputs. Aggregate catalog and manifest
   bytes may reflect their intended changes. Preserve all other name
   overrides and use the existing manifest/version rules. Avoid a file
   format or wire version bump.
5. **Settle both mutations with named publication.** Under existing
   workspace ownership, persist both dirty markers and record selector
   ownership before staging the omission. Under the publication guard,
   flush the commit evidence before the first source mutation. Record
   the job, target, canonical slot, backup, expected and intended
   generations, exact unit and fragment evidence, intended frozen
   output, and old/new name-map evidence including absence. Recheck the
   unit's exact allowed contents before moving it and before restoring
   or deleting its backup. Verify that the inbox path is absent or
   ordinary and disjoint from every mutation
   path, with no junction, reparse point or hard-link alias. Inspect
   path safety only. Move the sealed unit to a job-owned same-volume
   backup with no replacement. Update the name map only if it changes,
   using verified staged bytes and ordinary paths. Before the no-replace
   generation rename, revalidate the prepared directory, expected unit
   absence, exact backup, name state and unaffected checked inputs. Check
   timeout or shutdown immediately before the first mutation. After it
   starts, settle the operation or keep the evidence and write hold.
   If publication is uncertain, success requires activation-equivalent
   verification of the intended final directory against the job's exact
   frozen evidence and proof of the exact committed state. Roll back
   only with proven nonpublication and verified recoverable state for
   both the unit and the map. Never overwrite an unexpected slot or
   remove an unrelated destination. A cleanup failure
   after verified success preserves success and holds subsequent writes.
   Stop managed writers for offline repair, use the same proof before
   restoring or keeping state. Before releasing the write hold, clear
   only the owned selector and verify the unmasked staging graph
   against the settled registry. Preserve the settlement record across
   incomplete cleanup, including a failure to clear either marker.
   Prior complete generations remain readable.

## Consequences

- The published change is limited to one registered texture and its
  label, and the editable PNG can be used for a later explicit Register.
- Previewing an old generation stays independent of project changes.
  A stale snapshot cannot serve as write authority.
- A complete check and candidate build cost more than a catalog query.
  Unsupported inputs cause a refusal, including some workspaces where
  the target happens to be unused. Work happens on explicit requests;
  there is no idle frame-path scan.

## Alternatives considered

- A catalog reverse lookup misses changes to recipes and incomplete views.
- A fresh build alone cannot bind the confirmation to what the operator
  reviewed or explain a source-only blocker before confirmation.
- A project-wide persistent index, forced dependency rewrites or a locking
  protocol across arbitrary file editors exceeds this local workflow.
