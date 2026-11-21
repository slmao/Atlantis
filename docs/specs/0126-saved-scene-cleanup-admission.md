# Spec 0126 — Saved Scenes Cleanup Admission and Recovery

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0126](../plans/0126-saved-scene-cleanup-admission.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0175](../adr/0175-saved-scene-cleanup-admission-and-recovery.md) (Proposed)

## Summary

Close the remaining deletion race on [PR #416](https://github.com/slmao/Atlantis/pull/416).
This narrow follow-up refines the deletion admission and recovery requirements
of [Spec 0125](0125-saved-scene-build-cleanup.md#requirements). The reviewed
candidate `f0751a27` can still remove a reviewed file when another member is
created after the final check and before the first Windows delete disposition.

## Requirements

- **R1 — Isolate one confirmed attempt on the same NTFS volume.** Under the
  existing checkout lease, verify the selected snapshot, ownership, current
  slot, path and use protection. Record an operation outside the tree, then
  rename the verified root into a dedicated cleanup area by handle, without
  replacement or copy. Keep its identity. The original name may be occupied
  by a later writer. Never touch a replacement at that name. The checkout,
  current receipt, executable and other attempts stay protected.
- **R2 — Make the first deletion conditional on stable membership.** After
  isolation, obtain the Windows write exclusion in ADR-0175 and compare the
  entire isolated tree to the user's confirmation, including relative names,
  file identities, lengths and last-write evidence. A difference, uncertain
  identity, loss of exclusion or use conflict before the first disposition
  refuses with **zero delete dispositions**. In particular, an addition after
  the final original-path recheck must survive along with every reviewed file.
  Keep the exclusion through deletion of the admitted tree. Reject when the
  exclusion cannot be established. Count changes to the selected tree by
  identity even when the writer used an alias.
- **R3 — Recover one pending operation.** Retain durable evidence before any
  rename or permission change. Before deletion, restore the original name
  and permissions when both can be verified safely. If the original name is
  occupied, authority is unclear, or restoration fails, retain the isolated
  state and show the paths and reason. After a crash or partial deletion,
  reconcile actual identities and permissions under the lease. Ordinary startup
  and refresh perform no deletion. Allow a verified restore of an intact tree
  or a fresh explicit confirmation of the verified remainder. Preserve the
  operation if identity, ownership, or restoration is uncertain. Permit only
  one pending cleanup per checkout.

## Proposed Design and Architectural Impact

[ADR-0175](../adr/0175-saved-scene-cleanup-admission-and-recovery.md) updates
ADR-0174 D3 with a reversible rename, Windows membership exclusion, and one
bounded recovery record. Preserve ADR-0174 D1 and D2, including the resolved
pending Editor alias guard in `f0751a27`. Keep all state in the Windows Editor
launcher. Saved Scenes discovery, Build, Open and the v1/v2 receipt contract
retain their existing behavior.

## Verification

[Plan 0126](../plans/0126-saved-scene-cleanup-admission.md#verification-checklist)
defines Windows filesystem and recovery checks, including the exact red case
from #416 and proof of exclusion after the isolated tree is verified.

## Out of Scope

Other asset or build cleanup, automatic deletion or retry on startup, arbitrary
destination paths, cross-volume movement, global permission changes, deliberate
rewrites or bypass of the enforced NTFS security, and shared client changes.
