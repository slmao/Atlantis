# Plan 0133 — Update a Prefab Source and Synchronize One Instance

- **Spec:** [Spec 0133](../specs/0133-prefab-source-sync.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Spec and Plan 0133 and J1–J4 approved on `e9235765`, with S2–S4 authorized after the maintainer merged [#429](https://github.com/slmao/Atlantis/pull/429) as `39e404ce`. [Joint approval](https://github.com/slmao/Atlantis/pull/429#issuecomment-7066200488)
- **ADR:** [ADR-0182](../adr/0182-prefab-source-updates-and-sync-baselines.md) (Accepted)
- **Design baseline:** `457c43bf`, maintainer merge of [#428](https://github.com/slmao/Atlantis/pull/428)

## Objective

Add a checked update of one registered static definition, followed by an
explicit synchronization of one placed instance with guarded local conflict
detection and exact Undo/Redo.

## Milestones and sequencing

1. **S1 — Joint review.** Resolve J1–J4 and record approval for the exact
   reviewed revision. Begin implementation from current `main` once the
   maintainer merges the design.
2. **S2 — Source update and snapshot read.** Extend 0132's capture and writer
   per ADR-0182 D1. Add the shared request and revision read. Verify checkout
   settlement, existing Build/Open receipt invalidation and old Runtime
   snapshot isolation before wiring synchronization.
3. **S3 — Tracked instances, sync and replay.** Add v10 authoring-only
   serialization and additive guarded modes per D2–D3. Verify whole-scene
   commit and exact replay before wiring the Editor actions, unknown-outcome
   feedback and separate Apply settlement. Extend the shared clients and
   wire fixtures in the same change.
4. **S4 — Verify and submit.** Complete the focused and Windows checks, record
   applicable gate results and submit the implementation for review.

## Expected files and modules

| Area | Scope |
| --- | --- |
| `src/asset_system/{include,src}`, `tests/asset_system` | Scene source grammar, serializer and cooked-equivalence checks. |
| `src/authoring/{include,src}`, `src/connection/{include,src}`, matching Authoring and Connection tests | Capture, document token, baseline storage, guarded modes, atomic replacement/replay and shared values. |
| `src/runtime/{authoring_host,scene_save_as,asset_host,prefab_projection}.*`, existing Runtime digest/registration helpers and Runtime tests | Checked update, source revision retention, hosted sync admission and settlement. Reuse existing build inputs and launch permission. |
| `src/remote/src`, `src/cli/src`, `src/gameplay_sdk/include/atlantis/gameplay`, `src/csharp/Atlantis.Gameplay`, `src/mcp/Atlantis.Mcp`, existing client/conformance tests and `docs/architecture/remote_protocol.md` | Additive modes and revision query, compatibility and diagnostics. |
| `src/editor_app/Atlantis.Editor`, `src/editor_app/Atlantis.Editor.Model` and affected Editor tests | Assets update confirmation, Inspector sync, guarded history, task feedback, 0090/0123 regression and 0093 visual conventions. |
| Spec/Plan 0133, ADR-0182 and the Spec registry | Approval and navigation. |

Name the concrete files within these areas when starting implementation.
Document any necessary non-semantic scope adjustment in the implementation
PR under the existing approval rule.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Source and activation** | Approve reusing the independent Prefab write opt-in for one checked same-unit source replacement. Keep GUID, path and fragment, and activate the update only through explicit 0090 Build/Open. |
| **J2 — Baseline and compatibility** | Approve v10 authoring-only baseline, additive source revision, opt-in tracked instantiation and matching-only preparation of 0132 instances. Keep cooked and legacy shared reads stable. |
| **J3 — Sync and recovery** | Approve explicit single-instance replacement with retained root placement, fresh changed descendants, conservative replay conflicts, one history step and independent Apply recovery. |
| **J4 — Execution and evidence** | After joint approval and maintainer merge, authorize one S2–S4 run for implementation, in-scope fixes, verification and a review-ready PR. Require the Windows 100% scenario and applicable gates. Final merge remains with the maintainer. |

## Verification checklist

- [ ] **R1 — Source replacement.** Check 64/65 nodes, byte bound, permission,
  stale capture or source, invalid typed dependencies, wrong fragment and
  changed staged or target data at the replacement boundary. Verify old or
  new exact bytes and unchanged GUID/path/fragment, donor and document state.
  Exercise interrupted staging outside the build inputs, failed replacement,
  unchanged bytes and unknown receipt without automatic retry.
- [ ] **R2 — Activation and compatibility.** Check old Runtime source bytes and
  revision after a checkout update, stale 0123 receipt and successful explicit
  Build/Open. Test a failed Build followed by corrected retry. Compare
  shared outcomes across clients. Verify new client/old host refusal, unchanged
  legacy reads/edits on a new host, v7–v10 round trips, old v10 source-reader
  refusal, cooked equivalence, and Save As/recovery baseline retention.
- [ ] **R3–R4 — Guards and settlement.** Cover matching legacy preparation and
  mismatched legacy refusal, equal payload without GUID churn, edited child
  or root binding, root placement exceptions, noncontiguous old slots and
  preserved unrelated nodes, external references, stale occurrence/revision
  and late replies. Exercise exact Undo/Redo, replay conflict, zero writes on
  rejection, unknown commit feedback and confirmed commit with unavailable
  read-back. Verify an Apply failure keeps verified history and explicit
  Apply does not repeat synchronization.
- [ ] **Windows 100% — table and four chairs.** Use a disposable trusted checkout.
  1. Save a scene with two distinctly placed tracked instances and an unlinked
     donor. Give one instance a renamed root under an ordinary parent. Record
     exact root/descendant identities, order, local/world placement, and
     bindings. Change the donor's descendant structure and a material binding.
     Update the existing Prefab from the donor. Verify the original Runtime
     still reads its old definition and both instances are unchanged.
  2. Explicitly Build/Open the same saved scene. Synchronize only the renamed
     instance. Verify root GUID, parent, name and TRS, the new descendant content
     and the other instance's unchanged subtree. Undo once and redo once.
     Compare exact before/after node GUIDs, order and source association.
  3. Save and reopen. Verify the settled data and distinct instances from
     scene read-back and native captures. Make a managed child edit, save and
     reopen it, then attempt synchronization. Verify conflict feedback and no
     changes to nodes, history, or Apply from the refused operation.
- [ ] **Applicable gates.** Run affected Windows Runtime, authoring, shared
  client and Editor suites. Run Vulkan validation; check final-diff
  applicability for headless/golden, Android and performance via the
  existing process, including `--gate --applies`. Retain raw evidence and
  explain any reuse by revision and unchanged inputs.

## Rollback and definition of done

Retain project registrations and original saved scenes. A rollback must keep
a v10-capable authoring reader for affected files, or a separately reviewed
conversion that accounts for the baseline. Keep original evidence and apply
the [Definition of Done](../process/definition-of-done.md) before requesting
implementation review.
