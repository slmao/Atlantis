# Plan 0124 — Recent Saved Scenes

- **Spec:** [Spec 0124](../specs/0124-saved-scenes-recent.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Spec/Plan 0124 and J1–J4 jointly approved for [PR #413](https://github.com/slmao/Atlantis/pull/413) at `6767ef6f`. ADR-0173 accepted and S2–S4 authorized after maintainer merge `0212913b`.
- **ADR:** [ADR-0173](../adr/0173-local-saved-scenes-recent-history.md) (Accepted)

## Objective and milestones

Make a previously opened saved unit selectable after an Editor restart,
using the existing launcher and build receipt. The design baseline is
`94f42d19`, the maintainer merge of [PR #412](https://github.com/slmao/Atlantis/pull/412).

1. **S1 — Joint review.** Resolve J1–J4 and ADR-0173. Begin S2 once the
   Spec and Plan are approved and the maintainer merges the design.
2. **S2 — History and scene identity.** Add the bounded ordering and
   guarded removal, local scoped store, and canonical saved registration
   lookup. Associate the checked identity with the existing Open settlement.
3. **S3 — Saved Scenes integration.** Add Recent and row feedback using the
   0093 styles. Route the active target through the existing eligibility and
   action controls. Cover state changes while checks are pending, then exercise
   the native Windows workflow.
4. **S4 — Verification and PR.** Review the final diff, retain applicable
   gate evidence, and submit a review-ready PR for maintainer merge.

## Expected files and boundaries

| Files | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/SavedSceneRecent.cs` (new) | Bounded ordering, entry identity and guarded removal. |
| `src/editor_app/Atlantis.Editor/SavedSceneRecentStore.cs` (new), `src/editor_app/Atlantis.Editor/SavedSceneWorkspace.cs` | History storage, guarded identity lookup and Open settlement. Reuse the checkout scope and existing build evidence. |
| `src/editor_app/Atlantis.Editor/MainWindow.cs`, `src/editor_app/Atlantis.Editor.Model/SavedSceneAttempts.cs` | Recent list, explicit selection origin, stale checks and the existing Build/Open route. |
| `src/editor_app/tests/Atlantis.Editor.Tests/SavedSceneTests.cs` | Focused model, store and hosted Windows acceptance. Reuse the existing Saved Scenes fixture and native screenshot path. |
| `docs/specs/README.md`, approval metadata for Spec 0124, Plan 0124 and ADR-0173 | Navigation and approval. |

The launcher owns all history behavior. The 0123 receipt data and admission
format and 0090 build recipe remain as implemented.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Recording and ordering** | Record successful explicit child process start. Keep eight entries, deduplicated by scene GUID, and update the name only after an explicit Open at that location (R1). |
| **J2 — Selection and removal** | Require the recorded name and GUID to resolve together, retain unavailable entries with distinct reasons, and remove only the observed history entry. Keep selection inert and recheck the identity and 0123 eligibility at Open (R2–R3). |
| **J3 — Scope and storage** | Accept ADR-0173's independent user/checkout-local history. Use existing launch authority, isolated failure handling and one 0123 receipt (R4). |
| **J4 — Verification and authorization** | Approve the checks below. After joint approval and manual design merge, authorize S2–S4 once, including scope-preserving fixes, valid evidence reuse and submission of a review-ready PR. Final merge stays manual. |

## Verification checklist

- [ ] **R1/R4 — History settlement.** Verify successful Open promotion,
  including a prior GUID explicitly opened at a new valid location. Check
  stable ordering, eight-entry eviction, and no update on selection, Build,
  a publication hint, a refused Open, or a failed process start. Cover
  two launchers updating the same history, a stale Remove, truncated or
  unrecognized history, unsafe or unwritable storage, and separate
  user/checkout scopes. Ordinary discovery, Build and Open remain usable
  on history failure.
- [ ] **R2/R3 — Target checks.** Exercise a missing or moved unit, an
  incomplete unit, same-name replacement with a different GUID, and an
  unsafe or unreadable registration. Verify the reason, no retargeting and
  history-only Remove. Test a replacement after selection and before Build
  or Open, stale check completion after a new selection or Refresh, and
  explicit selection of the replacement through the ordinary list.
- [ ] **R1/R2/R4 — Windows 100% restart.** Build a disposable saved scene,
  explicitly Open it, and close the child and launcher. Reopen the normal
  launcher with the same trusted checkout. Select Recent and confirm the
  same unit is active. Observe zero CMake configure/build invocations and
  no launched child up to explicit Open. Then Open and verify the independent
  Editor loads the expected source and GUID using the recorded Runtime.
  Verify promotion on repeated Open without a duplicate. Capture the
  Recent list and the selected state at native 100%.
- [ ] **R2–R4 — Windows 100% stale and isolation.** Change a build input after
  a successful eligibility check and verify explicit Open refuses via 0123.
  Move or remove a disposable unit, Refresh and verify the old shortcut is
  unavailable and removable. Confirm removal leaves sources, output and
  the single build receipt unchanged. Confirm the Recent list does not
  expose the first checkout's history when the launcher is started with a
  different trusted checkout or with Saved Scenes disabled. Capture the
  unavailable state. Reuse targeted checks for supersession by a different
  build to demonstrate that Recent cannot open an older build.
- [ ] **Applicable gates.** Build and run affected Editor model and Saved
  Scenes checks. Record clean Vulkan validation for newly run hosted
  launches. Assess platform and golden applicability for the final diff.
  Run `atlantis_runtime --gate --applies` and follow the existing performance
  protocol if selected. Reuse only matching evidence.

## Rollback and definition of done

Revert the history integration to restore 0123 Saved Scenes behavior.
Leave the private history file in place. Follow the
[definition of done](../process/definition-of-done.md) and record final
readiness and evidence in the implementation PR.
