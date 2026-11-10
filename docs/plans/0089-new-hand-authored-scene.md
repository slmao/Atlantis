# Plan: New Hand-Authored Scene

- **Spec:** [Spec 0089](../specs/0089-new-hand-authored-scene.md)
- **ADR:** [ADR-0149](../adr/0149-create-hand-authored-scene-through-shared-authoring.md) (Proposed)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending

## Objective

Deliver one shared operation that creates a durable starter scene and uses
the existing saved-slot launch path. Verify the full workflow without
changing the original session or introducing a new document lifecycle.

## Milestones / Task Breakdown

1. **S1 — Design.** Review the Spec, Plan, and ADR together and record J1–J4.
2. **S2 — Authoring and publication.** Add the starter and shared values in
   Authoring/Connection. Reuse the existing serialization and cook. Factor
   only the executable-private publication common to Create Scene and Save
   As; keep snapshot and result semantics in their respective operations.
3. **S3 — Shared clients and Editor.** Carry the operation through Remote,
   CLI, both SDKs, and MCP. Add the Editor action, lifecycle guards and
   saved-slot instructions. Forward the explicit existing Runtime opt-in
   through the ordinary Editor launcher. Reuse the second Editor instance,
   asset addition, Save and apply recovery on their existing paths.
4. **S4 — Verification and PR.** Run the mapped checks, cite valid reused
   evidence, and submit the exact implementation candidate for human review.

## Files / Modules Touched (expected)

- **Authoring and Connection:** AuthoringConnection headers and in-process
  implementation; AuthoringSession header/source and focused authoring
  tests. Use existing Asset System public helpers without format or module
  changes.
- **Executable host:** `src/runtime/authoring_host.*`, `main.cpp` and
  `cli.*` where the existing opt-in is routed or documented,
  `scene_save_as.*` for the shared publication primitive, and focused
  host/filesystem tests in `tests/runtime/`. Keep the helper in the
  existing translation unit. No new build target, saved-slot logic or
  RuntimeHost change is planned.
- **Clients:** Existing authoring codecs and dispatch in `src/remote/`,
  command handling in `src/cli/`, authoring wrappers in the Gameplay
  SDK and `src/csharp/`, MCP schema/registry in `src/mcp/`, and
  `src/editor_app/` Program, MainWindow, RuntimeProcess, EditorSession and
  focused Editor tests.
  Extend current boundary/codec/client tests and regenerate conformance
  vectors through their existing generator.
- **Docs:** README usage and opt-in, Remote protocol and module-boundary
  documentation, and navigation updates. Link the acceptance evidence
  from the implementation PR.

No changes are expected in Runtime frame/render code, Asset System
formats, build/CI, Android hosting, or committed goldens. Report any
necessary file-list adjustment in the implementation PR.

## Sequencing & Dependencies

Base on [0088 / PR #340](https://github.com/slmao/Atlantis/pull/340).
Reuse [0087](../specs/0087-add-mesh-to-scene.md) and
[0083](../specs/0083-authoring-apply-outcomes.md) in the new session.
Start S2 only after joint approval, ADR acceptance and manual design merge.

| Decision | Recommendation |
| --- | --- |
| **J1 — Starter and identity** | Approve the fixed active camera, no source asset references, and fresh persistent scene/camera GUIDs (R2). |
| **J2 — Destination and recovery** | Approve reuse of the existing Windows development opt-in, saved-unit namespace and no-replace publisher, including shared conflicts and 0088 recovery (R1/R3). |
| **J3 — Shared contract and lifecycle** | Approve one additive operation, explicit creation and recovery feedback, and opening the saved-slot build from a second ordinary Editor without closing the original (R4/R5). |
| **J4 — Acceptance and delivery** | Approve the verification below and one authorization for S2–S4 after human design merge, retaining existing gates. |

Suggested authorization: “I approve J1–J4 and Spec/Plan 0089 together and
accept ADR-0149 for the reviewed PR revision. After manual design merge,
proceed from current main through S2–S4, including scoped fixes, applicable
verification, valid evidence reuse and a review-ready implementation PR.
Retain the existing gates; final merge is human.”

## Verification Checklist

- [ ] **S1:** Check numbering, references, statuses, scope and whitespace.
- [ ] **R1–R3: creation and publication.** Check unopened and imported
  sessions, disabled/unavailable hosts, invalid names, unsafe roots, and
  occupied destinations. Cook and decode the starter; verify the exact
  active camera, defaults, absence of source references, distinct
  identities between creations, and fixed relative path. Exercise a
  cross-host Create/Save As race for one name, forced scene identity
  conflict, staging/commit failure, and dropped replies before and after
  commit. Check one intact winner, no partial publication, original/other
  files untouched and no automatic retry. Exercise the recovery guidance for
  both a valid committed starter and a still-unsettled absent destination.
  Reuse 0088 cases only when the affected primitive and inputs are unchanged.
- [ ] **R4: contract and current state.** Check native/C# codec vectors and
  parity across CLI, SDKs, MCP and Editor. Check old-client calls on the new
  host, new clients against an old host, exact legacy Unsupported handling,
  and malformed success (nil GUID or wrong path). Verify user-visible
  distinctions among confirmed creation, refusal, unsent request and unknown
  outcome after send. With unsaved accepted edits, a client draft and an
  outstanding apply, exercise success, refusal and response loss. Verify
  no create-induced save, edit, reopen, history item, receipt change, or
  reload; distinguish any ordinary focus or pending work. Exercise repeated
  clicks, unopened/read-only documents, a reopen on the same session,
  session replacement before send and a stale result after send.
- [ ] **R5: one live Windows acceptance.** In a disposable development
  checkout, launch the ordinary Editor with the explicit flag; verify
  forwarding and that default launch grants no creation authority. Create
  a starter while the original has a distinguishable unsaved edit. Read
  the result and follow its instructions: configure and build a separate
  tree, then start a second ordinary Editor on that build and saved scene.
  Do not use a measurement attach path. Verify distinct Runtime sessions,
  the original edit and history intact, and a running camera-only viewport
  with a new bound source, no selection and empty history.
  Add one catalog mesh that fits the default camera and renders using
  0087's fallback material. Observe successful apply, undo and redo the
  add with the existing history, and then edit the restored object. Induce
  one recoverable reload failure and verify the
  accepted document and history survive while the prior projection stays
  visible. Retry apply without another create or add, and verify success.
  Save, reopen the new document and confirm its values and cleared local
  history. Rebuild/restart it independently and verify the same scene,
  camera and mesh-node identities, reference, edited value and visible
  object. Check the original files and Runtime binding stayed unchanged.
  Verify a failed build/start leaves the committed unit available for an
  explicit retry with the same identity.
- [ ] **Applicable gates:** Run focused Windows native/managed build,
  boundary, conformance, Editor and live Vulkan validation checks. Cover
  shared Android compile paths as applicable. Run or justify reuse of
  applicable headless and golden checks; use the full-diff performance
  applicability selector and original protocol if selected. Cite the
  revision, inputs and configuration supporting any reuse. Do not
  reopen or waive the historical #340 performance record.

## Rollback Plan

Revert Create Scene and its client action. Units already committed use the
0088 scene and fragment formats and can still be opened with the saved slot.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
S4 records all applicable evidence for the exact candidate; final review
and merge remain human.
