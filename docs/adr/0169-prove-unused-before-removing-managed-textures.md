# ADR 0169: Prove a Managed Texture Is Unused Before Removing Its Registration

- **Status:** Proposed
- **Date:** 2026-11-20
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0118](../specs/0118-registered-texture-removal.md)

## Context

[ADR-0143](0143-shared-cooked-asset-catalog-queries.md) describes an
immutable catalog snapshot. It cannot prove the absence of references
in current project sources. A removal also changes both the sealed
registry (ADR-0154) and, when present, the generation-scoped name map
([ADR-0168](0168-generation-scoped-asset-display-names.md)). Those changes
must settle with one publication outcome.

## Decision

1. **Define a closed proof domain.** Use the active cooked catalog,
   the verified managed project/profile, its complete effective source
   declarations and producer inventory, and the registered source units.
   Reconcile the input roster against the base catalog source, current
   producer declarations and managed fragments. Cover all declared material
   producers, including those absent from an open scene or current cooked
   results. Use the existing material parser for authored recipes. For
   generated recipes, resolve dependencies with the configured importer
   and its current source/catalog resolution, or use generated evidence
   proved current against the exact input bytes. A cached cooked result
   alone cannot prove this. A variant, undeclared or unaccounted producer,
   unrecognized reference-bearing source, or unresolved import mapping
   prevents a complete result. A saved scene reaches textures through a
   material in the current scene grammar. Its material must be in the
   proof roster. There is no arbitrary search for GUID-like text or
   assertion of safety outside the supported build graph.
2. **Bind a check to what was read.** Run a bounded read job on the existing
   managed worker only when requested. Capture the host instance, active
   generation, target identity, complete ordered source roster and
   content digests for reference-bearing inputs, declarations, relevant
   importer/configuration inputs, and matching registry/name state.
   Read under the existing writer lease and verify stable membership and
   bytes across the check. Ordinary project inputs use the trusted local
   workspace rule from managed rebuild: independent configure/build and
   direct changes to checked roots must be quiescent during checks and
   commit. Managed writers share the existing exclusivity boundary.
   Re-enumerate and recheck reviewed bytes under that boundary at confirm
   and before the first commit mutation. Refuse visible drift. If a source
   set cannot be held stable under this model, return an incomplete check.
   Do not promise isolation from concurrent out-of-band filesystem edits.
3. **Use one bounded check receipt.** Add `assets.checkTextureRemoval`,
   `assets.textureRemovalCheckStatus` and `assets.removeTexture` to the
   shared surface. In check status distinguish Checking, Ready, Referenced,
   CheckIncomplete and StaleCheck; an evicted/unknown ID is a refusal.
   A Referenced result reports up to 16 ordered blockers and whether the
   report is truncated or incomplete. Catalog-only blockers need no slot.
   The worker is single-flight. Keep one check receipt per host, with a
   Ready receipt expiring after two minutes or when superseded. Confirmation
   supplies the GUID, expected generation and check ID. The ID is opaque
   and usable from another authorized connection to the same host, so
   separate CLI and MCP invocations can finish the flow. It grants no
   write permission. Consume it when confirmation is admitted, including
   a job that later fails. Use the existing bounded rebuild status for
   that job. No lease spans the human confirmation dialog.
4. **Prove the exact candidate before mutation.** Reserve a distinct
   job-owned staging selection that excludes exactly the verified existing
   texture for this candidate. Reuse the cooker/catalog pipeline and require
   current evidence from the configured producers. Validate the full
   candidate with activation rules, including the inventory header and
   all frozen files. Require the exact one-record and one-producer delta,
   no edge to the target, and unchanged surviving identities, sources,
   dependency sets and cooked outputs. Aggregate catalog and manifest
   bytes may reflect their intended changes. Preserve all other name
   overrides and use the existing manifest/version rules. Avoid a file
   format or wire version bump.
5. **Settle both mutations with named publication.** Under the existing
   project, staging and publication guards, persist both dirty markers and
   flush one recovery record identifying the job, target, original slot,
   backup, expected and intended generations, exact target evidence and
   old/new name-map bytes or absence. Verify that the inbox is outside
   all mutation paths. Move the sealed unit to a job-owned same-volume
   backup with no replacement. Update the name map only if it changes,
   using verified staged bytes and ordinary paths. Verify expected registry
   absence and name state before the no-replace generation rename. Check
   timeout or shutdown immediately before the first mutation. After it
   starts, settle the operation or keep the evidence and write hold.
   If publication is uncertain, success requires activation-equivalent
   verification of the intended final directory and exact committed
   state. Roll back only with proven nonpublication and verified
   recoverable state for both the unit and the map. Never overwrite an
   unexpected slot or remove an unrelated destination. A cleanup failure
   after verified success preserves success and holds subsequent writes.
   Stop managed writers for offline repair, use the same proof before
   restoring or keeping state, and clear markers only after verification.
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
