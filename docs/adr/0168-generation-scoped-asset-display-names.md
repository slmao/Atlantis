# ADR 0168: Store Managed Texture Display Names with Immutable Generations

- **Status:** Proposed
- **Date:** 2026-11-19
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending.
- **Related Spec:** [Spec 0117](../specs/0117-registered-texture-rename.md)

## Context

[ADR-0143](0143-shared-cooked-asset-catalog-queries.md) freezes asset
discovery for the lifetime of a Runtime. The source column carries a
logical identity used by producers and reimport, so editing that column
is not a presentation change. [ADR-0167](0167-managed-texture-source-replacement.md)
protects the project registry and its immutable published generations.
Names need to survive later publication without requiring a new asset
format or an Editor-local store.

## Decision

1. Keep one Runtime-owned `texture-display-names.json` at the managed
   project root. It is a canonical JSON object from non-nil canonical
   texture GUIDs to explicit labels: compact UTF-8 JSON from the existing
   serializer with keys in GUID order, no BOM, trailing bytes or duplicate
   keys, at most 4,096 entries and at most 512 KiB. Only a verified absent
   file means an empty map; omit entries equal to the original registration
   token and remove the file when removing the last override. Validate
   canonical bytes, eligibility and namespace against the sealed registry;
   reject malformed, foreign or unsafe data. Check capacity before admission
   and report the limit without writing. The map is edited only under the
   existing managed writer locks; it is not a user-edited source or a CMake input.
2. The committed map is the write authority. A new named managed
   generation with any overrides has manifest v8, with the complete map
   embedded as `textureDisplayNames`. Keep existing frozen texture
   evidence according to the actual roles. With no overrides keep the
   existing manifest format and no names member. Reject a v8 manifest
   with a missing or empty map, or a pre-v8 manifest with a names member.
   Activation verifies the complete canonical map against that generation's frozen producer,
   catalog and texture evidence before exposing it; the approved existing
   frozen checks still apply. Never consult the mutable project map for
   the displayed names in a running process. Legacy generations/seed
   discovery use fallback labels; old snapshots whose map differs from
   committed state are read-only for managed writes on the upgraded host.
   Inconsistent or unreadable mutable metadata must not replace the view
   of a valid named generation; it blocks managed writes.
3. In the common managed publication path, freeze the complete map,
   validate it against the resulting registry and catalog, and carry it
   into every new generation, including registration, Resume, material
   save, rebuild and 0116 reimport. Guard the names on every managed
   mutation, including entry points that use the slot guard rather than
   ordinary rebuild admission. The existing Resume exceptions for one
   committed source unit do not bypass name-map agreement. Freeze the
   intended map with the job and recheck the committed map before any
   source commit, before publication and when confirming an uncertain
   result. Re-read any candidate or final manifest against the job's
   exact intended map, not just a structurally legal map. A rename-only
   job copies only the existing allow-list of files from validated active
   outputs, verifies their bytes, and never reads stage outputs or the
   inbox. No change to the cooked
   catalog file, artifact formats or logical-key derivation.
4. Reuse the existing publication lock, dirty markers, recovery discipline
   and no-replace generation rename. First prepare a private validated
   generation. Under the commit guard, revalidate the old registry and
   candidate and record and flush the job/GUID, expected active and intended
   generation IDs and exact old/new map evidence (including absence).
   Persist both dirty markers before the map exchange. Replace the single
   metadata file atomically using ordinary same-volume paths; use no-replace
   when creating and verify ownership before removal. Verify exact new
   committed bytes (or absence) and recheck the candidate before the
   named-generation rename, which is the publication commit point.
   Shutdown or timeout before the exchange prevents it; after it starts,
   settle the operation or retain repair evidence. Do not infer rollback
   from a failed API return: only verified absence of the intended final
   directory and exact recoverable state permit restoring the old map.
   Both unreadable and damaged destinations require a write hold. A
   verified complete intended generation plus the exact committed map
   settle success, with a write hold if cleanup fails. Offline recovery
   must stop writers and prove the intended outcome before restoring or
   keeping the map and clearing markers. Never delete an unrelated or
   damaged destination. Existing generations remain usable read-only.
5. Discovery is additive on the existing Connection/Remote contract.
   Older clients using a new host can continue consuming old fields.
   Pre-0117 Runtime executables do not accept v8; existing v1–v7
   generation validation remains supported. Do not downgrade a workspace
   containing overrides in place: pre-0117 writers would not preserve
   them. Run only updated Runtime writers for that managed project;
   a legacy workflow requires a separate compatible workspace from
   before rename. This is an operational compatibility requirement, not
   a claim that an old executable enforces the new guard.

## Consequences

- GUIDs, paths, assets and bindings remain stable. Labels get the same
  visibility lifetime as other discovery data.
- The host must preserve and validate one more registry input across
  publications. Ordinary rename copies one full generation and may
  require the same offline recovery as other managed writes.
- A compatible older client can still use the new host, but an older
  Runtime executable cannot open the new named generation.

## Alternatives considered

- Add name fields to the cooker catalog/identity fragment: that expands
  build, asset-format and reimport scope for no Runtime rendering benefit.
- Store names only in the newest generation: later publication can lose
  labels and there is no project write guard when reopening an old one.
- Update the mutable registry and expose it immediately: list/get would
  drift from the selected generation, violating ADR-0143.
- Add a second metadata service or a live refresh channel: unnecessary
  for a single explicit rename and restart.
