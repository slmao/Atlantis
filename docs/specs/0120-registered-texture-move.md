# Spec 0120 — Move a Registered Base Color Texture in Assets

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-20
- **Related Plan:** [Plan 0120](../plans/0120-registered-texture-move.md)
- **Approval:** Atlantis maintainer, 2026-11-20, [PR #405](https://github.com/slmao/Atlantis/pull/405) at `491e5b0c`.
- **Related ADR:** [ADR-0170](../adr/0170-generation-scoped-texture-locations.md) (Accepted)

## Summary and scope

Move one existing [0095](0095-texture-import-and-base-color-binding.md)
registered sRGB base color texture to a different logical folder in Assets.
Publish a named generation and explicitly restart to see the new location.
Limit writes to eligible textures in a trusted Windows managed project with
an active named generation. Directory and batch moves, physical file moves,
other asset classes or texture roles, aliases, live activation and scene
undo/redo are out of scope.

[0081](0081-asset-browser-shared-interface.md) exposes a catalog `source`
that identifies the producer. [0116](0116-registered-texture-reimport.md)
uses that identity to verify frozen registration and derive the inbox.
[0117](0117-registered-texture-rename.md) changes a display label without
changing either. This proposal adds a separate catalog-facing logical path
for placement in Assets. The physical source, sealed registration and cooked
catalog source column each keep their existing meaning.

| Value | Before move | After restart into the published generation |
| --- | --- | --- |
| Assets logical path | `textures/brick.png` | `textures/exterior/walls/brick.png` |
| Display name | `brick` or its existing 0117 override | Same |
| Catalog `source` | `content:texture_brick/source.png` | Same |
| Reimport input | `inbox/brick/source.png` | Same |
| Registered unit | `registered/texture_brick/` | Same |
| GUID, material slots and cooked texture | Existing GUID, bindings and bytes | Same |

## Requirements

### R1 — One location, one identity

The logical namespace is `textures/` for registered base color textures.
The fallback path is `textures/<registration-token>.png`. A destination is
a full logical path under `textures/` with zero to four folder segments
and a required `.png` leaf. Folder segments and the leaf stem follow 0095's
1–48 character lowercase ASCII name grammar, including reserved-name
validation. Reject blank segments, case variants, dot segments, backslashes,
trailing separators, drive prefixes and whitespace. Use exact byte
comparison. The dialog defaults to the current leaf. It accepts a different
leaf with a different folder so users can resolve a collision during a move.
The display name, registration token and source paths are independent of
the logical leaf. Virtual folders are derived from paths. Returning to the
exact fallback path removes the override.

Reserve each effective full path for one GUID in this namespace, including
fallback locations and moved entries. Equal folder names and shared folders
are allowed. The other catalog assets retain their own source identities and
are outside this virtual namespace. Register and Resume check the resulting
namespace before creating or publishing a registered color texture. A move
back to the fallback requires a vacant destination like any other move.
Refuse the current path as `Unchanged` and a different leaf in the same
folder as `InvalidPath`. Before admitting a job, validate the asset GUID, expected named
generation, expected current logical path, registration eligibility, role,
full committed metadata and the intended path. Refuse a different owner at
the destination. Under the existing single-flight writer and publication guards,
repeat the checks from frozen and current exact bytes before the first write.
A change to checked inputs after admission fails the job. Earlier stale
requests do not become valid through a refresh of the UI.

### R2 — Publish and recover as one operation

Apply ADR-0170's exact-byte preparation, exchange, publication and recovery
rules. Move uses the metadata-only publisher lineage from 0117. The job
copies verified frozen outputs without recooking or looking at the inbox.
The move changes exactly one logical path plus publication metadata.
The old attached snapshot keeps its path and references until explicit restart
into the resulting generation. Previous complete generations remain readable.
Keep the same managed writer and bounded job history. Confirmed conflicts,
stale requests, and pre-commit failures leave the old path and publish nothing.
Roll back a committed metadata exchange only with proven nonpublication and
verified old/new evidence. For ambiguous results, preserve the record and
pause writes. A published generation which has been verified remains the
successful result if cleanup subsequently fails.

### R3 — Reuse the shared surface and reimport entry

Add `assets.moveTexture` with exactly `asset`, `expectedGeneration`,
`expectedLogicalPath` and `destinationLogicalPath`. Use canonical non-nil
GUIDs and the path grammar in R1 for both paths. Use the existing
`RebuildJob` / `assets.rebuildStatus` by job ID.
Expose `Unsupported`, `Unavailable`, `Busy`, `WorkspaceNeedsRepair`,
`CatalogUnavailable`, `UnknownAsset`, `UnsupportedProducer`, `InvalidPath`,
`PathConflict`, `StaleSnapshot` and `Unchanged`. Bad wire shapes follow the
existing protocol error convention. After admission, distinguish stale and
conflict in the validation diagnostic. Older hosts refuse a well-formed move
as Unsupported using the established exact unknown-method mapping.

Add optional `logicalPath` on list/get only for an eligible texture with
a moved path in the selected snapshot. Old clients keep reading original
fields. Updated clients derive the fallback from the original registration
token where applicable. Reject malformed, null, duplicate or ineligible present
fields and
keep the catalog's complete list and GUID order. CLI JSON and MCP preserve
the field; CLI text can show logical path and original source separately.
All clients use the same mutation and status contract.

Capture the visible selected GUID, list/connection intent, expected path
and generation reported by the existing rebuild status in the Editor.
Before send, discard an intent superseded
by selection change, hide, reload or reconnect. Once sent, settle the job
independently of UI selection. Preserve connection error categories and,
when delivery might have occurred without a valid receipt, report the
outcome as unknown with GUID, expected generation and destination. Poll by
the known job ID. Check the workspace and settle the previous attempt before
a fresh explicit retry when attribution cannot be recovered. Known status must
match the job ID and texture GUID. A lost or expired status keeps the known ID
and never starts a new move automatically.

Show the effective logical path in selected details alongside the source. A
canonical full logical path entered in Assets search matches that path exactly.
Other search text uses the existing name, GUID and source rules and also
matches substrings of the effective path. After restart the old full path no
longer finds the moved row. Keep sorting and grouping, and use the existing
0093 layout and job controls. On success show both the resulting generation ID
and the still-active one, with explicit restart guidance. Carry the display
name and location forward in later publications.
**Reimport PNG** continues to resolve and show the original
`inbox/<registration-token>/source.png` using 0116. Moving never changes the
path used to edit or reimport the PNG. [0119](0119-texture-reference-locator.md)
continues to locate the same material GUID and slot via existing dependency
and recipe reads. Neither move nor locate writes to the current scene.

## Architectural impact

The plan adds a shared write operation and versioned managed location
metadata under the existing Runtime owner. ADR-0170 records that storage,
compatibility and recovery boundary. Asset System, the cooked catalog,
CMake producer keys, renderer and frame loop keep their current contracts.
There is no additional worker, package or filesystem API in the clients.

## Alternatives and verification

Moving the registered or inbox file would change producer identity and the
0116 recovery boundary. An Editor-only folder would diverge across clients.
Changing display names would not assign a location. The proposed virtual
path composes with the existing generation model. See Plan 0120 for the
verification checklist and joint decisions.
