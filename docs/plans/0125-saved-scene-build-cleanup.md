# Plan 0125 — Saved Scenes Build Cleanup

- **Spec:** [Spec 0125](../specs/0125-saved-scene-build-cleanup.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Spec/Plan 0125 and J1–J4 approved at `a4227c03`, recorded on [PR #415](https://github.com/slmao/Atlantis/pull/415#issuecomment-7058022375). S2–S4 authorized after maintainer merge `048680c3`.
- **ADR:** [ADR-0174](../adr/0174-saved-scene-build-cleanup-ownership.md) (Accepted)

## Objective and milestones

Remove one verified older Saved Scenes build from the current trusted checkout
through explicit selection and confirmation. The design baseline is `c6ea2e57`,
the maintainer merge of [PR #414](https://github.com/slmao/Atlantis/pull/414).

1. **S1 — Joint review.** Resolve J1–J4 and accept ADR-0174. Begin S2 after
   joint human approval and maintainer merge of this design.
2. **S2 — Ownership and Windows guards.** Add scoped attempt evidence and the
   build witness. Integrate both Open entry points and compatibility as defined
   in D2. Prove Windows process and filesystem behavior in focused tests before
   exposing deletion. Unsupported observation stays ineligible.
3. **S3 — Saved Scenes integration.** Add discovery, confirmation, guarded
   deletion and outcome feedback. Use one checkout lease for the destructive
   interval and preserve 0123 reuse. Exercise the full workflow on Windows.
4. **S4 — Verification and review PR.** Review the final diff and applicability,
   retain evidence and submit a review-ready implementation PR for manual merge.

## Expected files and boundaries

| Files | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor/SavedSceneBuildReceipt.cs` | Keep one slot and add the private receipt version handling. |
| `src/editor_app/Atlantis.Editor/SavedSceneWorkspace.cs` | Connect build reservation, process settlement and shared Open admission to the cleanup state. |
| `src/editor_app/Atlantis.Editor/SavedSceneBuildCleanup.cs` (new), `src/editor_app/Atlantis.Editor/SavedSceneCleanupWindows.cs` (new) | Scoped attempt evidence, list and confirmation rechecks, local Windows job and filesystem guards. |
| `src/editor_app/Atlantis.Editor/MainWindow.cs` | 0093 list, confirmation and feedback within Saved Scenes. |
| `src/editor_app/tests/Atlantis.Editor.Tests/SavedSceneTests.cs`, `src/editor_app/tests/Atlantis.Editor.Tests/SavedSceneBuildCleanupTests.cs` (new) | Targeted races, Windows guard checks and native Saved Scenes acceptance. |
| `docs/specs/README.md`, approval metadata for Spec 0125, Plan 0125 and ADR-0174 | Navigation and approval. |

Reuse existing Editor trust, UI and build output location. Any required change
outside these files follows the repository's scope adjustment rules.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Discovery** | Accept durable provenance for new attempts only. Present one eligible path, logical file size and eligibility reason per row. |
| **J2 — Protection and compatibility** | Accept D2. Protect the current slot and any launch-admitted attempt. Read v1 receipts and use the new version for tracked reusable attempts. Treat incomplete process evidence as unknown. |
| **J3 — Deletion** | Accept the explicit single-directory confirmation, guarded recheck, and partial-failure behavior in R3 and D3. |
| **J4 — Acceptance and authorization** | Approve the checks below. After joint approval and manual design merge, authorize S2–S4 once, including scope-preserving fixes, valid evidence reuse and submission of a review-ready PR. Final merge remains manual. |

## Verification checklist

- [ ] **R1/R2 — Ownership and scope.** Check settled successful and failed
  attempts, same-user different checkout, legacy claims, corrupt or missing
  records, unidentified slot target and replaced filesystem objects. An
  incomplete build and the current slot stay protected. Verify list, cancel
  and refresh perform no deletion and report an empty state where appropriate.
- [ ] **R2/R4 — Use and compatibility.** Verify a running configure/build
  descendant stays protected if the initiating process exits, and an uncertain
  build settlement does not authorize cleanup. Cover a child Editor and its
  Runtime after launcher exit, launch-record write failure, an ambiguous launch,
  and ordinary and Recent Open. Verify v1 reuse with the new Editor, v2 rejection
  by the old reader, the single receipt, and the session-only fallback guard.
  Exercise manually held files, a direct live Runtime using the tree, and an
  independent Editor referring to that Runtime before it starts. An unknown
  observation must omit the candidate or refuse confirmation.
- [ ] **R3 — Confirmed target races and failures.** Between display and delete,
  change the slot or use status, replace the root or attempt, introduce a nested
  junction to an outside sentinel, or change directory membership. Verify
  recheck refuses before any removal. Exercise a sharing failure at admission
  and a partial I/O refusal after admission. Preserve the ownership record for
  the remainder, report the result, and require fresh confirmation. Verify no
  outside files, unrelated attempts or metadata are modified.
- [ ] **R1–R4 — Native Windows 100%.** With one trusted checkout, build A without
  opening it, then build B. Reopen the launcher, check A's actual path, size and
  reason, select A and confirm deletion. Verify the tree is gone and B's receipt
  is unchanged. Explicitly Open B in an independent Editor from the current
  receipt, with no new CMake configure/build call. Show confirmation and refusal
  when a candidate is changed during confirmation. Retain native evidence and
  run with clean Vulkan validation for the hosted launch.
- [ ] **Applicable gates.** Build and run affected Editor suites. Determine
  platform, golden and performance applicability for the final diff, using
  `atlantis_runtime --gate --applies` and the existing protocol where selected.
  Cite reused evidence only when its relevant inputs and environment match.

## Rollback and definition of done

Disable cleanup and retain its metadata and receipts. A previous Editor rejects
tracked receipts and can explicitly Build again. A deliberately deleted output
can be rebuilt from the unchanged source. Follow the
[definition of done](../process/definition-of-done.md) for implementation.
