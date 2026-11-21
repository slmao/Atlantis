# ADR 0181 — Register Static Prefabs and Persist Instance Provenance

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Atlantis maintainer
- **Acceptance:** Accepted with J1–J4 on `7cf1472e`. [Joint approval](https://github.com/slmao/Atlantis/pull/427#issuecomment-7064986853)
- **Related Spec:** [Spec 0132](../specs/0132-scene-prefabs.md)

## Context

[ADR-0179](0179-static-gltf-group-projection-and-instantiation.md) describes an
immutable asset group and an atomic copy to the document. Prefabs also need to
preserve authored labels and source identity. Existing scene cooking projects
authoring labels away under [ADR-0141](0141-authoring-node-name-storage-and-projection.md).
The saved-slot launch boundary also separates trusted source builds from live
managed asset generations.

## Decision

**Definition.** A Prefab is a project registration unit at
`assets/prefabs/prefab_<slug>/`. Its two leaves are `source.scene.txt`,
using ordinary source v8 with no active camera, and `catalog-source.txt`,
containing exactly one Scene declaration at the verified root/path. Template
node GUIDs are derived from the Prefab asset GUID and canonical template node
index using the existing GUID code. Parent indices are local to the template.

**Write boundary.** Add optional shared `authoring.createPrefab`. The
request carries the logical name, selected EntityGuid and the existing
document token. The result identifies the registered asset and captured
revision. The Runtime executable owns the checkout sink and validates the
dependency snapshot. Authoring owns selecting and freezing committed values.
An explicit `--allow-prefab-registration` permission on the trusted
development launch enables the sink. The trusted Saved Scenes launcher can
forward this separate opt-in. Ordinary attach and Save As permission alone
grant no Prefab write access.

Reuse 0088's canonical path checks and same-volume exclusive publication.
Share the checkout writer exclusion with Save As and New Scene. Validate
registered and staged units, exact bytes, expected leaves, safe boundaries
and collisions across checkout scene and Prefab identities before commit.
Use a private staging unit and a single no-replace rename. No rollback
removes a committed unit after a lost receipt.

**Build and read.** For v1 include registrations in the trusted Saved Scenes
slot's effective catalog and the existing 0123 file-set check. Verify the
registered pair together, stage its bounded source in the build and cook
those exact bytes through the ordinary Scene cooker. Preserve the declaration's
logical identity and apply the existing catalog dependency checks. The source
snapshot and its cooked result settle in the same successful build receipt.
Query only that output, paired with the current
startup catalog and cooked Scene. Cross-check identity, ordered tree, TRS
and exact typed dependencies through the existing codec. Preserve labels
from the frozen source and reject projections that disagree with the cooked
carrier. Existing ordinary builds and named generation activation keep
their existing scope. Do not expose a mutable checkout source as a query
response for an already-running Runtime.

Extend list/get with an optional Prefab classification. Legacy clients
still receive legal Scene rows. Those rows do not enter the glTF group
projection or Saved Scenes discovery. `assets.prefab` is an additive
shared, bounded read. Older hosts report the existing unsupported outcome.
No new wire version or asset artifact type is needed.

**Instance provenance.** Store one optional nonzero AssetGuid on the root
of each expanded instance as authoring metadata, alongside the persistent
node name. Expose it as an optional member of shared node summaries. Store
it in scene source v9 as an optional `prefab=<guid>` field after the node
name and before the parent. The reader accepts v7 and v8 as unlinked. The
writer keeps v8 for unlinked documents and writes v9 while a link exists.
Older source tools refuse v9. A new host preserves the field during legacy
value edits, Save, Save As and recovery. Separate additive document
capabilities report origin-write support and whether registration is
enabled. An omitted capability means unavailable. An omitted node member on
a capable host means the node has no origin.

Use a bounded `SetPrefabOrigin` command only to attach provenance to a
root newly created by a revision-guarded append. Validate the requested
Prefab against the immutable Assets snapshot at admission. Include the
field in structural history and conflict checks. Old clients can use
existing transactions and read existing members. Clients that cannot
represent a link cannot promise to duplicate or restore it. The new Editor
preserves it in supported replay or refuses the operation before send.

Scene source and the authoring model own the link. Exclude it from cooked
Scene schema 7 and its reflected component semantics, World bake and render
dependencies. Validate lossless source mapping separately. Old or missing
Prefab sources do not invalidate an expanded scene's concrete mesh/material
references. A link identifies origin, with no promise that the editable
instance still matches the definition.

## Consequences and alternatives

- Explicit Build/Open is one additional user action after registration. It
  preserves the existing separation between saved scenes and named generations.
- The snapshot copy retains labels unavailable in cooked Scene 7. The
  cooked carrier still provides dependency closure and validated geometry
  bindings. A dedicated Prefab runtime artifact or a live loader would add an
  independent format, dependencies and consumers outside this slice.
- An Editor sidecar would split Save, recovery and history across files.
  Deriving origin from names or GUID patterns would lose the relation on
  rename or admit false matches. Source-versioned document metadata persists
  the exact association and leaves existing cooked outputs unchanged.
