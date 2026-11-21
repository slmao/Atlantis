# ADR 0174 — Ownership and Use Evidence for Saved Scenes Cleanup

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Maintainer
- **Acceptance:** Maintainer accepted `a4227c03` in [PR #415](https://github.com/slmao/Atlantis/pull/415#issuecomment-7058022375), 2026-11-21. Design merged at `048680c3`.
- **Related Spec:** [Spec 0125](../specs/0125-saved-scene-build-cleanup.md)

## Context

[ADR-0150](0150-local-saved-scene-build-launch.md) allows the independent Editor
and its Runtime to outlive the launcher. [ADR-0172](0172-saved-scene-build-receipts.md)
serializes Build and Open admission per checkout but does not track old attempts
or child lifetime. Neither the receipt nor the attempts root provides a deletion
inventory.

## Decision

**D1 — Durable provenance.** Under the existing checkout lease, record ownership
outside the tree at attempt reservation in versioned, bounded, user- and
checkout-scoped records. Bind each record to the canonical trusted checkout,
the generated attempt leaf, and the local NTFS volume and directory file ID.
Resolve the leaf beneath the existing short managed attempts root. A record
cannot add roots, paths, or permissions. Publish state by atomic replacement.
Existing attempts lacking this evidence remain ineligible.

**D2 — Positive idle evidence.** Record build settlement only after configure and
build have finished and Windows has reported no active process in the attempt's
non-breakaway job, with each CMake process assigned at creation. Keep existing
process and log outcomes if tracking is unavailable. Such attempts receive no
cleanup eligibility. A crash before durable settlement leaves the attempt
protected. Tracking sets no kill-on-close policy. Cleanup never terminates a
process.

Protect a tracked attempt durably before admitting either ordinary or Recent
Open, holding the same checkout lease through the start decision. The first
release retains that protection permanently, including ambiguous launches or
when the launcher and child exit. An attempt whose protection cannot be
recorded cannot be opened through that tracked path. Use a new private 0123
receipt version for tracked reusable builds, so older Editors cannot read a
cleanup-tagged receipt and launch without recording protection. New Editors
still read existing v1 receipts and never infer ownership from them. On a
session-only build result, durably protect the attempt before returning it for
Open, including the receipt-write fallback. If protection cannot be established,
do not expose that attempt as openable. The single-slot and invalidation rules
stay in force regardless of version.

Also refuse when a manual or otherwise untracked process is observed using the
tree. Use Windows file-use evidence and identifiable Editor, Runtime and build
process references to the tree, including an Editor with a pending launch whose
Runtime has not opened the files yet. An inaccessible relevant process, sharing
conflict or incomplete observation leaves use unknown. A process list or an
empty file-use query alone cannot prove idle. Custody covers Saved Scenes
Build/Open through its lease and records. Private output directories are not a
supported workspace for external tools to continue writing between uses.

**D3 — Guard the selected objects.** Reopen the scoped roots, the attempt and
descendants by handle without following reparse points. Hold ancestors against
replacement and hold the objects to delete against conflicting use. Complete
the confirmation recheck and acquire the required guards before any disposition.
Delete only the verified objects by handle, bottom up. A membership change,
unsupported object kind, reparse point, ambiguous alias or unavailable guard
fails closed. A recursive deletion is not atomic once it starts. Preserve the
record on an interrupted removal and require a new confirmed scan of the
remaining tree before another attempt.

Windows provides [job process-tree tracking](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects),
[assignment at process creation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
and [file disposition on an open handle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle).

## Consequences and alternatives

Ownership, build use and deletion stay local to the Editor. Explicit Open keeps
the same process and scene checks and still uses a single receipt. Tracked
receipts require a cleanup-aware launcher. Superseded, settled builds that meet
the custody and filesystem checks can be reclaimed. Previously opened builds
remain on disk; reclaiming them would require proof that spans both the
independent Editor and its Runtime.

Folder age, CMake files and old reservation names are insufficient provenance.
Treating a closed parent or an old PID as proof of idle loses surviving children.
Extending child lifetime tracking would broaden the first release. A recursive
delete by path cannot preserve the checked identity across replacement.
