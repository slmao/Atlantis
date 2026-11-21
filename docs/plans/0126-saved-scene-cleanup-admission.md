# Plan 0126 — Saved Scenes Cleanup Admission and Recovery

- **Spec:** [Spec 0126](../specs/0126-saved-scene-cleanup-admission.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending
- **ADR:** [ADR-0175](../adr/0175-saved-scene-cleanup-admission-and-recovery.md) (Proposed)

## Objective and Milestones

Close the remaining P2 on [PR #416](https://github.com/slmao/Atlantis/pull/416)
at `f0751a27`. This design starts at `048680c3` on `main`. Keep #416 draft
until the design is approved and the final implementation passes verification.

1. **S1 — Joint review.** Resolve J1–J4 and ADR-0175. After approval and
   maintainer merge, integrate the approved design base into #416.
2. **S2 — Prove Windows admission.** First exercise the proposed by-handle
   exclusion, recheck and DACL restore in disposable NTFS trees. Stop dependent
   work if the zero-delete regression or exclusion and restoration evidence
   cannot be satisfied. Implement the one-operation record and recovery.
3. **S3 — Integrate and verify.** Wire existing Saved Scenes controls to the
   admission paths and show restore and retry choices for a pending operation.
   Run the checks below against the final candidate.
4. **S4 — Update the original PR.** Retain the historical red result and the
   final evidence, review the scoped diff, then return #416 to review when all
   blockers close. The maintainer performs the final merge.

## Expected Files

| Files | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor/SavedSceneBuildCleanup.cs` | Pending record, isolation, guarded recheck and recovery. |
| `src/editor_app/Atlantis.Editor/SavedSceneCleanupWindows.cs` | Verified same-volume rename, NTFS exclusion, guarded enumeration and DACL restoration. |
| `src/editor_app/Atlantis.Editor/MainWindow.cs` | Pending state and explicit recovery within Clean Up Builds. |
| `src/editor_app/tests/Atlantis.Editor.Tests/SavedSceneBuildCleanupTests.cs`, `src/editor_app/tests/Atlantis.Editor.Tests/SavedSceneTests.cs` | Deterministic race, recovery and affected Windows acceptance. |
| `docs/specs/README.md` and approval metadata for Spec 0126, Plan 0126 and ADR-0175 | Approval and navigation. |

## Joint Review

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and isolation** | Approve a single recoverable same-volume rename under the checkout lease, preserving existing eligibility, v1/v2 receipts and the pending Editor alias fix. |
| **J2 — Delete admission** | Approve proven Windows membership exclusion and the sealed inventory recheck. Unsupported or lost exclusion refuses before the first disposition. |
| **J3 — Recovery** | Approve journaled permission restoration and no-replace return to the original name. Keep a conflicting or interrupted state visible. Resume deletion only on fresh confirmation of the verified remainder. |
| **J4 — Evidence and authorization** | Approve the checks below. After approval and merge of this design, authorize S2–S4 once for #416, including scope-preserving fixes and applicable verification. |

## Verification Checklist

- [ ] **R1/R2 — Red to green.** On real NTFS, pause after the final original-path
  inventory recheck and before the first disposition. From a separate process,
  add a file to the selected tree, including under a nested directory. Continue
  the operation. Require refusal, zero disposition calls, and unchanged bytes
  for every original and newly added member. Check the root ID and receipt.
  Repeat after isolation and before the guard is armed. Preserve the `f0751a27`
  baseline result of one disposition. Exercise a superseded slot, a replaced
  root and a newly introduced junction.
- [ ] **R2 — Sealed interval.** Pause after the authoritative guarded inventory
  check and before the first disposition. A second ordinary process tries
  creation and move-in by the isolated name and an alias. Exercise a writable
  directory handle obtained before admission, and removal or rename of an
  existing member. Exclusion must either prevent the change or refuse admission
  with zero dispositions. Force a lost guard at the pause, then mutate the
  tree, and require zero dispositions. The accepted unchanged control deletes
  only the confirmed tree. Use barriers and raw observations, with no timing
  assumptions or substitution of a denial message for file preservation.
- [ ] **R3 — Recovery.** Interrupt after recording, after rename, during
  permission changes, before the first disposition, and after an observed
  disposition. Include an occupied original name and an unexpected descriptor.
  Verify no startup deletion, byte-exact ACL restoration for known objects,
  no overwrite of the replacement, and explicit restore or retry as applicable.
- [ ] **Windows 100% and gates.** Exercise the affected cleanup dialog with a
  refused confirmation, successful retry or fresh confirmation, and visible
  recovery. Verify the current receipt and executable remain intact. Reuse
  the existing zero-CMake Open, alias, Vulkan and performance-applicability
  evidence when the relevant inputs match. Run affected Editor tests and any
  applicable final gates. See [Definition of Done](../process/definition-of-done.md).

## Rollback

Keep #416 draft if the guard or recovery cannot be verified. An implementation
rollback must account for any pending isolation record before removing its
recovery behavior. Preserve the directory and the evidence of its identity when
safe restoration cannot be confirmed.
