# Plan 0116 — Reimport a Registered Base Color Texture

- **Spec:** [Spec 0116](../specs/0116-registered-texture-reimport.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending, including [ADR-0167](../adr/0167-managed-texture-source-replacement.md) and S2–S4 authorization

## Objective and baseline

Publish a changed 0095 base color PNG under its existing GUID. Preserve the
usable generation on failure and expose direct material dependencies through
existing clients. Design baseline is main `3a1598b7`
([PR #396](https://github.com/slmao/Atlantis/pull/396) manually merged).
Implementation starts from latest main after joint approval and manual
design merge.

## Milestones and dependencies

1. **S1 — Joint review.** Resolve J1–J4 and approve the Spec, Plan and ADR
   before coding.
2. **S2 — Implement.** First add the eligible registered texture branch to
   the existing rebuild worker, configure only its verified replacement, and
   validate the prepared generation. Wire the guarded directory exchange,
   rollback/recovery evidence and ordinary rebuild status reporting. Then
   expose the Editor action, the required restart/retry guidance and the
   guarded direct-dependency view, reusing shared asset APIs.
3. **S3 — Verify.** Run the mapped checks below against the final candidate,
   retaining failure and retry evidence. Reuse valid unchanged-input evidence.
4. **S4 — Submit.** Link approval, final checks, result provenance, deviations
   and any remaining merge conditions in a review-ready PR for human merge.

## Expected files and modules

| Files | Purpose |
| --- | --- |
| `src/runtime/asset_rebuild_host.{h,cpp}` | Existing Windows worker admission, private staged replacement and recovery, full validation and existing job settlement. |
| `cmake/AtlantisManagedImports.cmake` | Distinct pending replacement for precisely one existing color unit in the dedicated staging configuration; unchanged registration and standard builds. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession}.cs` | Selected-texture action and guidance; shared status and guarded dependency scan on the existing worker. |
| `src/editor_app/Atlantis.Editor.Model/AssetBrowser.cs` | Local accepted-catalog/selection intent for the dependent list; existing browse and detail behavior retained. |
| `tests/runtime/asset_rebuild_host_tests.cpp` | Focused native host checks for eligibility, evidence, same-identity publication and fault settlement. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{AssetBrowserTests,InspectorTests,Program}.cs` | Focused UI, scan/late-response and Windows preview/restart verification. |
| `docs/architecture/remote_protocol.md` and `docs/specs/README.md` | Describe additive producer eligibility and diagnostics within the same shared operations, and update navigation. |

No shared codec/schema, CLI/SDK/MCP interface, renderer, Runtime frame loop,
new dependencies or CI changes are planned. Use existing client suites to
verify the unchanged contract. Changes beyond these files follow the
existing deviation and approval rules.

## Joint review recommendations

| Decision | Recommendation |
| --- | --- |
| **J1 — Input and identity** | Approve one registered 0095 color PNG in an active named generation and the original project `inbox/<name>/source.png` as the editable path. Keep GUID, key, fragment bytes, color role, bindings and other registrations (R1–R2). |
| **J2 — Commit and recovery** | Accept ADR-0167: privately build and verify a replacement, exchange the sealed unit under the existing locks, then publish. Restore the old unit on a proven nonpublication; hold ambiguous cases for offline repair. Require explicit activation (R2–R3). |
| **J3 — Shared operation and known impact** | Reuse rebuild status and current snapshot list/get. Use an explicit, cancellable scan for unique direct material edges, without slot attribution or a new remote endpoint (R3–R4). |
| **J4 — Acceptance and delivery** | Approve the mapped Windows 100% success/failure/retry workflows and all applicable gates. Grant one S2–S4 execution authorization after the design is approved and manually merged. |

Suggested one-time authorization:

> Jointly approve J1–J4 and Spec/Plan 0116, and accept ADR-0167. After the
> design PR is manually merged, implement, fix ordinary in-scope issues,
> complete applicable verification and submit a review-ready PR from
> latest main. Reuse evidence when inputs match, retain existing gates,
> and leave final merge to a human.

## Verification checklist

- [ ] **R1 — Eligibility and frozen input.** One existing registered sRGB
  GUID in a named generation. Reject missing, wrong-kind, wrong-identity,
  path alias/link and unsafe or out-of-bounds input. Edit the inbox again
  after the candidate is frozen and prove the job uses the captured bytes.
  Confirm the other managed producers' ordinary rebuild and Register/Resume
  semantics remain intact. Out-of-scope producers must never take this
  replacement path.
- [ ] **R2 — Invariants and activation.** Check the exact GUID/type/source key,
  raw fragment, producer inventory/header, complete catalog identity and
  direct edges, unchanged material recipe bytes and reference GUIDs, and
  frozen PNG/stamp/artifact/sidecar/seal consistency. Inject a mismatch in
  the prepared evidence and prove there is no exchange/publication. Verify
  the existing generation manifest remains readable by an earlier compatible
  loader; older compatible clients read job results unchanged, and an older
  host refuses without fallback or modification.
- [ ] **R3 — Clean failure and recovery.** Assert invalid PNG keeps the old
  unit/seal, old generation and preview usable and clears the staging
  selection/ownership on proved cleanup. Assert a fresh corrected request
  builds the same GUID. Exercise overlapping requests, failure between the
  two exchange renames, definite publication refusal with rollback, and an
  ambiguous publish/cleanup outcome. Only validated intended publications
  are confirmed. Verify old generation read-only startup during repair,
  bounded job lookup/lost-reply handling and the documented offline choices.
- [ ] **R4 — Known material edges.** Include two matching materials (one
  uses the same GUID in more than one slot), an unrelated material and a
  non-material with an edge. Check unique, stable direct material rows;
  no false empty result on a failed detail read. Check one in-flight lookup,
  responsive worker scheduling, selection/hide/reload/disconnect guards
  and late results. Verify job ownership independent of the selection.
- [ ] **R1–R4 — Windows 100%, valid input.** Start in a named generation
  with the registered texture already bound to a known material and a mesh
  that exposes its base color. Record GUIDs, material reference and a
  before-preview. Replace the PNG at the original inbox path and request
  reimport through Assets. Check known affected materials. While attached
  to the old Runtime the preview stays unchanged. Select the returned
  generation on explicit restart, choose the same mesh and material without
  rebinding, and capture/render the new pattern in the base color region.
  Check unchanged GUIDs, recipe/binding and direct dependency edge via the
  shared view. Keep before/after Windows UI captures and generation evidence.
- [ ] **R1–R4 — Windows 100%, invalid then corrected.** From a clean known
  generation replace the same input with invalid PNG bytes. Check actionable
  terminal feedback, no selectable new generation and unchanged source,
  reference and preview. Confirm the previous generation can also be
  explicitly restarted. Replace with valid bytes and retry through the same
  action; verify a new named generation with the same GUID after restart.
  A fresh attempt uses the corrected snapshot, without Resume or rebinding.
- [ ] **Existing gates.** Run the affected Windows Runtime and Editor builds,
  host/managed regressions, shared-client checks and clean Vulkan validation
  for the exercised preview/activation paths. Reuse unchanged renderer
  golden evidence with input/build provenance. Windows-only host and Editor
  behavior adds no Android acceptance case; reassess applicability on the
  final diff. Evaluate the full final diff with `atlantis_runtime --gate
  --applies` and run any selected gate using the existing protocol and
  baseline. Preserve original verdicts. Design-only S1 needs document,
  link, scope and whitespace checks.

## Rollback and definition of done

Revert the source replacement branch and its UI together. Stop writers and
settle any marked workspace under ADR-0167 before rollback. A known complete
generation with a verified seal still works through the existing activation
and preview paths; rollback does not delete or rewrite published generations.
Follow the [Definition of Done](../process/definition-of-done.md) and retain
human merge.
