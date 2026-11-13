# ADR 0153: Managed Material Authoring and Draft Preview

- **Status:** Proposed
- **Date:** 2026-11-13
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0094](../specs/0094-basic-material-authoring.md)

## Context

[ADR-0146](0146-managed-asset-rebuild-and-generation-publication.md) keeps
generation publication separate from Runtime activation.
[ADR-0147](0147-managed-import-registration-and-publication.md) adds a managed
registration commit ahead of publication, but its registered units are
immutable. A material edit can preserve all catalog identities while changing
the source and its texture dependency.
[ADR-0152](0152-shared-mesh-material-preview.md) accepts a cooked pair from
the retained catalog, which cannot represent unsaved values.

## Decision

1. Add typed material Read, Save, Status and Draft Preview to the existing
   shared asset surface. The Windows Runtime executable authorizes and
   coordinates persistent writes using the managed project worker. The
   Editor does not write source files or run a separate build workflow.
2. Store each project-owned material as ordinary material source plus a
   stable catalog registration. A copy reads an eligible cooked recipe from
   the selected catalog. Managed edit changes the source only. Freeze the
   exact managed material source inputs with the selected build's producer
   view so an old Runtime can detect an ahead registry even when all GUIDs
   and source keys still match. Older generation metadata has an empty
   managed-material set. Its existing import and catalog validation still
   applies.
3. Prepare and validate before committing source, then publish with the
   existing generation rename. The two commit points remain observable for
   repair. Recover a committed source by explicit resume or selection of
   the already-published generation. Runtime activation remains an explicit
   local restart. See [Spec 0094 R3](../specs/0094-basic-material-authoring.md#functional).
4. Preview a value-only draft against a mesh and existing texture from the
   retained catalog. Share the fixed pair render path, admission and lifetime
   with ADR-0151/0152. Keep the existing GUID-pair request and response.
   Asset System stays CPU-only. Renderer and RenderGraph continue to serve
   the same material recipe.

## Consequences

- Checkout and generated material assets remain usable as copy sources.
  Project backups must include registered material sources and catalog
  fragments.
- Clients can distinguish local draft, durable source and selectable
  generation. A failed or lost publication reply may require local repair.
  Asset changes do not enter scene undo history.
- The opt-in build gains one producer kind and a private frozen source view.
  The preview operation accepts a closed recipe without requiring a temporary
  registry entry or compiled material file.

## Alternatives Considered

- Directly edit the selected source. That would give clients write authority
  over the checkout and importer outputs.
- Publish before saving source. A generation could then contain a material
  that a clean build from the registered project cannot reproduce.
- Preview only after publishing and restarting. This preserves the current
  pair API alone, but requires a build and restart for every iteration.
- Add a general draft shader or graph format. The existing recipe and
  renderer already cover the first workflow.
