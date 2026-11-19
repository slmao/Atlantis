# Spec 0117 — Rename a Registered Base Color Texture

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-19
- **Related Plan:** [Plan 0117](../plans/0117-registered-texture-rename.md)
- **Approval:** Pending joint Spec/Plan review.
- **Related ADR:** [ADR-0168](../adr/0168-generation-scoped-asset-display-names.md) (Proposed)

## Summary

Rename the displayed label of one registered sRGB base color texture from
Assets. Publish a new named generation and explicitly restart into it to
search and read the new label. Keep the texture's GUID, all references and
the original inbox PNG path.

## Motivation / problem statement

[0081](0081-asset-browser-shared-interface.md) exposes GUID, type and logical
source in the immutable catalog snapshot. There is no shared name field.
[0114](0114-assets-search-and-browse.md) derives the displayed label for a
managed color texture from the name in the logical source. In
[0095](0095-texture-import-and-base-color-binding.md) that same original name
identifies both the producer and its inbox path. [0116](0116-registered-texture-reimport.md)
verifies the frozen producer, registered PNG and exact registration fragment
before reading that original inbox path. Changing the path to rename a row
would change inputs to those checks.

For a texture registered as `paint_old`:

| Value | Before rename | After restart into the renamed generation |
| --- | --- | --- |
| Displayed label | `paint_old` (fallback) | `paint_blue` (shared display name) |
| Logical source | `content:texture_paint_old/source.png` | Same |
| Inbox path | `inbox/paint_old/source.png` | Same |
| Registered unit | `registered/texture_paint_old/` with sealed PNG and canonical fragment | Same |
| Texture GUID and material references | Existing GUID | Same |

## Goals and non-goals

Limit writing to one existing 0095 registered color texture in an active
named generation of a trusted Windows managed project. Give all clients one
read/write contract, with clear failures for a duplicate name or stale
target. Directory moves, source path changes, batch operations, deletion,
other texture roles and asset types, live activation, and scene undo/redo
are out of scope. The same sRGB texture may already be used by emissive
slots; those references also stay intact.

## Requirements

### R1 — Resolve and validate one label

The shared request carries the texture GUID, required expected active
generation ID and requested display name. Resolve the GUID against the
active catalog and frozen managed registry with 0116's source, producer,
type, role and eligibility checks. Map the original name only from the
validated source. Require the expected generation to match the active
named generation, and the complete committed registry, including the names,
to agree with that snapshot. Check the cached state at admission; the
worker re-reads disk evidence before preparing and again under the
existing publication guard before commit. An active-generation mismatch
or a verified registry mismatch is stale, even if the requested label
would otherwise be free or unchanged. An unreadable or unverified
registry blocks managed writes. A seed or nonmanaged profile cannot
rename. Use the existing single job slot and update the committed names
before freeing it after publication, so a second client on the old
snapshot cannot write through the completion of the first job.

Use the existing 0095 name grammar: 1–48 lowercase ASCII characters,
starting with a letter, followed by letters, digits or underscore, excluding
reserved names. No implicit trim or case conversion. Within the managed
project's registered 0095 color textures, reserve both the original
registration tokens and assigned labels of other textures. Do not reserve
labels of other asset classes or roles. Reject a collision or stale request
without changing the registry or publishing a generation. An exact current
label is a no-op refusal with no new publication; rename back to one's own
original token removes the override. The 0095 color Register checks the
same namespace before GUID allocation and before source commit; Resume
checks the namespace for its already registered unit without allocating
a new identity. Existing shared registration-token collisions across
texture roles remain enforced independently.

### R2 — Persist and publish one change

The Runtime's managed worker owns a single bounded, canonical GUID-to-name
file in the managed project root. Read access for clients uses a validated,
immutable copy of the map in each named generation. Use
[ADR-0168](../adr/0168-generation-scoped-asset-display-names.md) for
the file shape, registry/generation consistency and commit order.

Prepare the new generation from verified active outputs via the existing
generation publisher; rename does not recook. Require byte-identical
catalog, producer inventory, source evidence and published asset files.
Check the committed unit's original registration fragment and seal
against the frozen identity; no binding, recipe or source files are
written. Verify the prepared generation using activation checks with the
exact intended name map and recheck before publication. Only the name
map and the necessary generation metadata change.
All subsequent managed publications carry the complete verified names forward,
including 0116 reimport. Publishing does not change the current snapshot;
explicit restart selects the new generation. Older complete generations can
still be opened by the new Runtime with their own names.

### R3 — Settle refusal and failure without exposing a partial rename

Before writing, validate and exclude other writers using the existing project
and staging locks. Stage and verify both the new metadata and generation before
touching committed metadata. Use the recovery and publication sequence in
ADR-0168. A definitive conflict, stale request, or known failure before commit
leaves the previous name and generation in place and permits a new request
after correction. If the source metadata has changed and publication
definitely did not happen, restore the verified previous metadata before
allowing further writes. If the outcome cannot be verified, keep the recovery
record and block managed writes; the previous complete generation remains
usable read-only. Confirm an uncertain publication only by validating the
expected final directory and exact intended registry. A cleanup failure after
verified publication preserves success and blocks further writes until repair.

Return a bounded job receipt. A provably unsent request is not admitted.
Preserve transport error categories; if the start may have been sent and
its receipt is missing or invalid, report an unknown outcome with the
target GUID, expected generation and proposed label. Query by job ID
when known and require the returned job to match that ID and asset. A
latest matching asset or label is not proof of ownership. Keep the known
job ID if a status reply is lost or the record has expired. If attribution
cannot be recovered, inspect the workspace and relevant generations and
ensure the prior request has settled before a fresh explicit attempt.
No automatic retry or Register/Resume fallback.

### R4 — One shared surface and search behavior

Add `assets.renameTexture` with exactly `asset`, `expectedGeneration`
(canonical non-nil GUIDs), and `displayName`. Reuse the existing
`RebuildJob` and `assets.rebuildStatus` by returned ID with the same bounded
history and workspace admission. For this new start method, expose distinct
refusals: `Unsupported`, `Unavailable`, `Busy`, `WorkspaceNeedsRepair`,
`CatalogUnavailable`, `UnknownAsset`, `UnsupportedProducer`, `InvalidName`,
`NameConflict`, `StaleSnapshot`, `Unchanged`. Malformed wire shapes are
protocol errors. A conflict or stale guard discovered after admission
reports `Validation/IdentityChanged` with a diagnostic distinguishing the
two; unverified state uses the existing repair behavior.
Keep legacy rebuild/import method result shapes unchanged. Editor, CLI,
both SDKs and MCP use these same operations. Only the existing exact
legacy unknown-asset-method envelope (BadRequest, “unknown or malformed
asset query”) maps to Unsupported for a well-formed request; retain other
connection or protocol failures.

Add one optional `displayName` string to the existing `assets.list` and
`assets.get` item; omit it when no override is present, preserve `source`,
old fields and GUID order. Both queries agree for a GUID in one snapshot.
Only eligible textures with an override carry the member. Old clients
ignore it; new clients read older snapshots/hosts
with the existing path-derived presentation. New readers reject a duplicate,
null, non-string or invalid present name; absent is the only fallback.
Use R1's name grammar. CLI text displays the label separately from the
source and retains the old presentation when absent; CLI JSON and MCP
preserve the optional field. No new catalog query, server-side search or
transport limit; retain 0081's complete-list behavior.

Assets offers Rename for a single visible, provisionally eligible texture
in a named generation, uses the name from the snapshot and shows the fixed
source/inbox path separately. Capture the GUID, accepted catalog/list
intent and the active generation reported by that connection's existing
rebuild status. Before sending from the Worker, recheck that the draft
still targets the same visible selection, list and connection; cancel on
hide, reload, selection change or disconnect. Drop late UI replies for a
replaced connection. Once sent, settlement is independent of selection;
once admitted, poll only by the returned job ID. Report the published ID
and unchanged active ID and give the explicit-restart instruction. After
restarting, Assets uses the shared name for display, sorting and name
search; the old path and GUID still match path/GUID search. The reference
picker and asset labels use the same name when showing an asset. All
operations that need the original path (including Reimport PNG) use the
source-derived token. No changes to the current scene, draft, Apply or
history.

## Proposed design and architectural impact

Extend the managed generation publisher with presentation metadata, and
extend the Connection DTOs and Remote surface additively. ADR-0168 records
the storage/activation authority, compatibility policy and recovery
boundary. Asset System, CMake/import formats, rendering and scene schemas
need no changes; the executable-private host owns file access. No per-frame
work is added, and there is no new dependency or worker.

## Alternatives considered

Renaming the logical source or directories would change producer and
reimport inputs. An Editor-only label would diverge from other clients.
Reading the live project file from list/get would make an old generation
show a new label without selecting it. See ADR-0168 for the persistent
format and compatibility trade-off.

## Testing and verification plan

Map the storage, recovery and shared-client checks to Plan 0117. On a real
Windows Editor at 100%, rename a registered texture already bound to a
material, observe the old snapshot, restart into the published generation
and find it by its new name. Verify the same GUID, binding and image; then
replace the PNG at the original inbox path, run 0116 reimport and restart
again to verify the new pixels and the retained label and binding. Try a
duplicate and a stale request; verify clear refusal and no persistent
mutation. Exercise known and uncertain publication outcomes via targeted
host tests, plus compatibility with old snapshots and clients.

## Risks and open questions

Joint review decides the label namespace, persistence and downgrade
boundary, shared method/status shape and verification scope (Plan J1–J4).
Publishing a complete generation copies existing files even when the name
is the only change; retain the existing timeout and avoid a new delta format.

## Out of scope / future work

Directory and source moves, asset deletion, batch rename, Unicode/localized
labels, reverse-reference migration and live reload require separate design.
