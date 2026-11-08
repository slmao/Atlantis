# ADR 0147: Managed Import Registration and Publication

- **Status:** Proposed
- **Date:** 2026-11-08
- **Deciders:** Pending joint human review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0086](../specs/0086-new-asset-import.md)

## Context

[ADR-0146](0146-managed-asset-rebuild-and-generation-publication.md) can
rebuild registered inputs and publish an unchanged identity set. A new
glTF root needs a persistent source and GUID before the importer can derive
its outputs. The checked-in catalog and CMake graph currently serve
[ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) and
[ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md).
A Runtime process retains a fixed snapshot under
[ADR-0143](0143-shared-cooked-asset-catalog-queries.md).

## Decision

1. Add a trusted project-local import workspace to the opt-in Windows
   executable host. One committed source directory holds the copied glTF
   and its catalog-source fragment. This extends ADR-0097 D1 and ADR-0098
   D1 for managed roots. Mint once on explicit registration and derive
   outputs as before. CMake generates the effective catalog-source v1 and
   declares the existing producer from this data. The host gives an
   isolated build access to one temporary candidate. A regular opt-in
   configure uses committed registrations. The checkout and build
   scripts remain read-only during import. Existing Asset System and
   RuntimeHost dependency boundaries hold.
2. Share ADR-0146's exclusive build worker between import and rebuild.
   Extend exclusivity to the canonical import workspace. Prepare the
   isolated catalog and generation before registering the source. Commit
   registration and publication with separate same-volume directory
   renames. Once registered, retain source and GUID across a failed
   publication and permit explicit resume subject to the active identity
   set. The staging marker protects the build tree, and the project
   marker protects the registry even if a later host uses a different
   staging tree. Offline repair consults the committed source and
   generation directories for the durable outcome.
3. Keep publication separate from activation. The import-enabled profile
   binds a separately owned publication root and stores per-generation
   source and producer metadata. Activation validates that immutable
   metadata against the selected generation rather than comparing it with
   the current registry. New registrations cannot invalidate older generations.
   The cooked catalog and discovery snapshot retain their existing
   formats and lifetimes. Local launch selects a generation explicitly.
4. Add the typed start and status operations of Spec 0086 to the existing
   Connection/Remote asset surface. Editor App, CLI, SDKs and MCP use the
   same contract. Source placement and Runtime restart remain local
   operator actions. The asset selector discovers the derived mesh through
   the existing catalog after activation.

## Consequences

The same registry can produce stable identities in a clean build and be
backed up independently of generated outputs. A saved scene can reference
the derived GUID using ordinary authoring. The operator manages both
registry and scene inputs. Two filesystem commits allow a registered source
to outlive a failed publication. Explicit resume can recover with the same
identity. Immutable generation metadata adds a private manifest version.

## Alternatives Considered

- **Rewrite the checkout's catalog and CMake scripts.** This couples an
  admitted job to the user's working tree and splits recovery across source,
  identity and executable build declarations.
- **Use published output as the registration authority.** Offline generation
  retention could then discard inputs or root identities required by a saved
  scene. Clean rebuilds would depend on old build products.
- **Run Tools from each client.** This duplicates validation and admission
  and removes the shared worker's conflict and recovery semantics.
