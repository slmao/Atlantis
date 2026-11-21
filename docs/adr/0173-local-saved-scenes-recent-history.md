# ADR 0173 — Keep Saved Scenes History in the Local Launcher

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Maintainer
- **Acceptance:** Accepted at `6767ef6f` in [PR #413](https://github.com/slmao/Atlantis/pull/413), merged by the maintainer at `0212913b`.
- **Related Spec:** [Spec 0124](../specs/0124-saved-scenes-recent.md)

## Context

[ADR-0150](0150-local-saved-scene-build-launch.md) gives the trusted local
launcher responsibility for discovery and independent Editor startup.
[ADR-0172](0172-saved-scene-build-receipts.md) gives it one verified build
receipt per canonical checkout and Windows user. Recent navigation needs a
separate lifetime. A shortcut can outlast the build for that scene, and a
source path can later hold a different scene.

## Decision

Keep the history in a separate bounded, versioned file under the Windows
user's Local AppData. Reuse 0123's normalized checkout scope, derived from
the trusted launch configuration, to address the history. Each entry stores
the canonical saved unit name, scene GUID and an opaque entry version. File
order records recency. The file contains no absolute source or tool paths.

Use a short history-local writer lease and atomic replacement under ordinary
local NTFS paths. Read the latest complete file under the lease before an
update. Upsert and guarded Remove serialize among participating launchers.
Keep this lease separate from 0123's build and launch lease. Record after
launch settlement, outside the build lease. An unrecognized, corrupt or
unsafe history is unavailable and is left intact. A history failure only
affects Recent.

Resolve shortcuts through the ordinary 0090 source discovery boundary and
match the saved registration's GUID. Record the identity from the inputs
verified for the actual Open. Any identity mismatch at a Recent action's
final check refuses that action. The existing 0123 result remains the only
persisted build eligible for Open after a restart.

## Consequences

Recent is available without a Runtime connection and remains local to a
trusted launcher. Existing editors and shared clients need no new calls.
Manual moves stay unavailable at the original location until the user opens
the new location explicitly or removes the shortcut. A successfully started
child may still report an ordinary Runtime or document error.

## Alternatives considered

- Store a list of build or launch records. That would conflate navigation
  with launch authority and change the single-receipt contract.
- Persist names alone. A replacement at the same location could inherit the
  old shortcut without the user's explicit choice.
- Wait for child readiness or track manual launches. That would require a
  cross-process completion signal or wider observation than this feature needs.
