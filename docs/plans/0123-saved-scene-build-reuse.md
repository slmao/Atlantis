# Plan 0123 — Reuse the Last Saved Scene Build

- **Spec:** [Spec 0123](../specs/0123-saved-scene-build-reuse.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending
- **ADR:** [ADR-0172](../adr/0172-saved-scene-build-receipts.md) (Proposed)

## Objective and milestones

Reuse the last eligible saved-slot build after restarting the launcher. The
design baseline is `9ac997cf`, the merge of [PR #410](https://github.com/slmao/Atlantis/pull/410).

1. **S1 — Joint review.** Resolve J1–J4 and ADR-0172. Begin S2 on latest main
   after the maintainer merges the design.
2. **S2 — Evidence and receipt.** Trace the actual default Windows saved-slot
   inputs and outputs. Reuse existing CMake evidence. Cover the supported
   managed-root case, resolved toolchain and optional content. Record any
   unsupported inputs that prevent reuse. Implement capture/check and single
   slot replacement with deterministic failure coverage. Demonstrate that the
   ordinary reference setup actually qualifies before proceeding to UI evidence.
3. **S3 — Saved Scenes integration.** Keep eligibility separate from starting
   a build. Connect background validation to current selection and Open, with
   the existing independent child and 0093 feedback. Test both Windows flows.
4. **S4 — Final verification and PR.** Review final input provenance, apply
   the required gates, and submit a review-ready PR for maintainer merge.

## Expected files and module boundaries

| Files | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor/{SavedSceneWorkspace.cs,SavedSceneBuildReceipt.cs,MainWindow.cs}`, `src/editor_app/Atlantis.Editor.Model/SavedSceneAttempts.cs` | Workspace evidence and process orchestration, private local receipt, selection admission and UI. `SavedSceneBuildReceipt.cs` is new. Keep `RuntimeProcess.cs` and the shared connection unchanged. |
| `src/editor_app/tests/Atlantis.Editor.Tests/SavedSceneTests.cs` | Extend existing model, workspace and Windows hosted cases. |
| `src/runtime/CMakeLists.txt`, `src/asset_system/CMakeLists.txt`, `cmake/AtlantisManagedImports.cmake` | Conditional scope for private build-evidence declarations only if the existing CMake evidence does not close a specific gap in S2. No native source, target/link topology or flag changes. |
| `docs/specs/README.md`, approval metadata for these documents | Index and approval. |

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and ownership** | Approve one durable local receipt per user and checkout, fixed 0090 settings and explicit independent Open. Adopt ADR-0172 D1. |
| **J2 — Freshness** | Require the input and output evidence in R2. Accept conservative invalidation and a fresh 0090 build for unsupported evidence layouts. Adopt ADR-0172 D2. |
| **J3 — Settlement and UX** | Approve superseding the previous result at Build admission, reasoned refusal, stale-feedback exclusion and final eligibility check at explicit Open (R1/R3). |
| **J4 — Verification and authorization** | Approve the checks below. After joint approval and design merge, authorize S2–S4 once, including scope-preserving fixes, applicable validation, valid evidence reuse and submission of a review-ready PR. Final merge remains manual. |

## Verification checklist

- [ ] **R1/R3 — Windows 100% reuse.** In the ordinary launcher explicitly
  build one saved scene with a committed managed asset. Record its attempt,
  log and CMake process invocations. Close the launcher, start a new one with
  the same trusted settings, select the same unit and Open. Verify there are
  zero further configure/build invocations, the independent Editor starts with
  the exact prior executable, and the expected document and managed reference
  appear. Verify the new launcher's own document and Runtime remain intact.
- [ ] **R2/R3 — Windows 100% refusal.** Change the official scene source
  after a good Build, restart, reselect, and verify Open is disabled with a
  reason and Build available. Build explicitly and verify the resulting attempt
  opens. Retain native UI evidence for ready and rebuild-required states.
- [ ] **R2 — Evidence coverage.** Change each saved-unit file, a committed
  managed asset, registration set membership, an indirect asset input (such as
  a glTF buffer), a resolved build setting/tool, and the executable separately.
  Include same-size/same-timestamp edits, a missing or altered catalog or shader,
  and a missing, unsafe or unreadable input. Compare unchanged inputs, changed
  managed-root selection and optional Bistro membership. Verify the intended
  reason and no launch for stale or unknown evidence. Verify a known-unrelated
  live generation change alone does not invalidate the saved-slot build.
- [ ] **R1/R3 — Receipt and ordering.** Cover missing, corrupt and unsupported
  receipts, admission and publication faults, superseding failed Build,
  overlapping launchers, an input changed during Build, and stale check replies
  across selection, root and Refresh. Change evidence after Ready to open and
  before launch, and confirm the final check refuses. Exercise duplicate Open,
  start retry and the existing explicit fresh-build path where evidence cannot
  be persisted. Assert no configure or build is started by an eligibility check.
- [ ] **Applicable gates.** Build the managed Editor and run affected 0090,
  model and workspace tests. Run the Windows hosted flow with Vulkan validation
  and retain the log. Assess CMake changes, if any, for affected build checks.
  Evaluate the final diff using `atlantis_runtime --gate --applies` and follow
  the existing protocol if selected. Record why rendering goldens and Android
  checks are inapplicable if the candidate leaves those inputs unchanged.

## Rollback and definition of done

Revert the receipt and verification integration to restore 0090 session-local
behavior. Existing attempts and formal sources remain untouched. Follow the
[definition of done](../process/definition-of-done.md) and record evidence and
manual merge readiness in the implementation PR.
