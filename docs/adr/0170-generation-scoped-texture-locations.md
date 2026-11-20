# ADR 0170 — Store Texture Locations with Immutable Generations

- **Status:** Accepted
- **Date:** 2026-11-20
- **Deciders:** Atlantis maintainer
- **Acceptance:** Atlantis maintainer, 2026-11-20, [PR #405](https://github.com/slmao/Atlantis/pull/405) at `491e5b0c`, manually merged as `87e9beb9`.
- **Related Spec:** [Spec 0120](../specs/0120-registered-texture-move.md)

## Context

Cooked catalog `source` identifies a producer and is part of the proof
used for registration, reimport and safe removal. The existing named
generation fixes what a client sees until restart. A logical folder
belongs to the same lifetime as [ADR-0168](0168-generation-scoped-asset-display-names.md)
display names and needs independent identity and validation.

## Decision

1. Keep a Runtime-owned `texture-locations.json` in the managed project
   root. Use the existing canonical JSON serializer and GUID lexical order.
   Map an eligible texture GUID to its full logical path using Spec 0120
   R1. Omit exact fallback entries and remove the file when the
   map is empty. Require compact canonical UTF-8 bytes, no duplicate keys
   or trailing data, at most 4,096 entries and at most 2 MiB. Treat only
   verified absence as empty. Check ordinary paths, exact ownership,
   capacity, eligibility and effective-path uniqueness. Treat the complete
   committed map as write authority. It is unrelated to the display-name
   map and is outside cooker input.
2. Freeze the whole map as `textureLocations` in generation manifest v9 if
   nonempty. Preserve all applicable texture, producer, source and optional
   display-name evidence, using the preceding field rules. With no moved
   entries retain the applicable existing version and omit this member.
   Reject a missing or empty v9 location map or a location member on prior
   versions. Validate against the resulting frozen registry, catalog and
   producer view before exposing it. List/get derive moved paths only from
   activated immutable metadata. A valid old snapshot remains readable
   even if committed locations have changed. The upgraded writer refuses
   a snapshot whose committed registry or metadata no longer agrees. There
   is no global latest-generation pointer.
3. Use the same authorizations, project/staging locks and worker as other
   managed writes. Freeze the complete name and location maps with every
   job. Verify their exact authority before source commit, before publication
   and when confirming an uncertain result. Carry the location map through
   reimport, rename, rebuild, registration, material save and Resume.
   Keep existing one-unit Resume exceptions without weakening metadata
   agreement. For removal, drop the removed GUID's location and extend
   0118's journal to settle it with the unit and display-name override.
   Before any rollback write, prove recoverability of the entire affected
   set. Reopening with unresolved records or incomplete cleanup keeps writes
   paused under the existing recovery discipline.
4. Before a move writes committed metadata, privately clone allow-listed,
   validated active output files. Verify byte-identical catalog, inventory,
   sources, artifacts and other metadata content. Under the commit guard,
   recheck authority, expected logical path, destination, deadline, intended
   map and prepared generation with activation-equivalent validation.
   Flush durable evidence for both metadata states including absence, and
   the job, GUID and expected/intended generation IDs. Persist dirty markers
   before committing the map. Reuse same-volume atomic replacement or
   no-replace creation, with an exact check of the staged file and its bytes
   before exchanging it. Recheck committed state and the complete candidate
   before the no-replace generation rename.
5. The generation rename is the publish point. After the first metadata
   change, prove nonpublication and exact recoverable state before restoring
   anything. Do not infer nonpublication from an error return alone. Require
   activation-equivalent validation of the exact intended final directory and
   matching committed inputs before confirming an uncertain result. Preserve
   the recovery record through required cleanup and keep the write hold across
   restart, including a verified success with incomplete cleanup. Offline
   repair stops writers, proves the intended outcome, then restores or keeps
   state and retires only owned evidence. Never overwrite an unrelated
   destination. Do not reintroduce the original logical path as an alias.
6. Preserve older client reads and existing wire version. Use only updated
   Runtime writers once workspace location overrides exist. Older Runtime
   executables reject v9 activation and do not enforce new write guards
   against an older generation in that workspace. Use a separate
   legacy-compatible workspace for downgrade and retain old generations as
   read-only views on upgraded hosts when authority differs.

## Consequences

- A move has one shared location and leaves producer identities and bindings
  intact. Later reimport still uses the original inbox.
- Publishing a move copies verified existing output files. Managed writes
  now also verify a small bounded location map and recover it on failure.
- The path namespace supports folder placement and search without exposing
  disk paths, managing directory nodes or adding server search.

## Alternatives considered

- Mutate the cooked catalog source and move files. This would require producer
  re-registration and a different original-inbox contract.
- Store the path only in the Editor or in the latest generation. Other clients
  would disagree or later writable publications could lose the location.
- Merge display labels and locations into one new authority file. Two
  independently reviewed fields can extend the existing metadata model
  without migrating current name maps.
