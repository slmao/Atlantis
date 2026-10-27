# Plan: The E3 Authoring Editor in Atlantis Editor App

- **Spec:** [Spec 0068: The E3 Authoring Editor in Atlantis Editor App](../specs/0068-authoring-editor.md) (`Draft`).
  - **ADRs** (all `Proposed`): [ADR-0135](../adr/0135-two-document-edit-and-apply.md),
    [ADR-0136](../adr/0136-structural-edit-records.md),
    [ADR-0137](../adr/0137-csharp-model-port-held-by-vectors.md).
  - **Drafted together with the Spec** at the maintainer's direction (2026-10-27), against the Spec's recommended
    options: Q1 E-a, Q2 A-a, Q3 U-a, Q4 M-a, Q5 P-a, Q6 S-a, Q7 C-a, Q8's targets.
  - **Unchanged ADRs:** ADR-0103, ADR-0123, ADR-0125 D1–D3, ADR-0127, ADR-0129, ADR-0130, ADR-0132, ADR-0133 and
    ADR-0134.
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending. Once approved, it records the reviewer, the date, and the PR naming this Plan and
  its Spec and authorizing implementation on `feature/0068-authoring-editor`, M1–M7 in this Plan's order, with the J8
  checkpoint after M4.
  - **Constraints (proposed, as Plans 0064–0067):**
    - no merge by the agent, no push to `main`;
    - every golden byte-identical, none re-captured;
    - `tests/performance/baselines/` and `gate_policy.json` untouched;
    - changes limited to this Plan's file list, deviations disclosed;
    - `--gate --applies` reported (expected false: no engine path changes);
    - the C# SDK's zero-NuGet check and the editor-app package checks unchanged and passing;
    - the measurements reported against Spec 0068 Q8, never test verdicts;
    - the owner's acceptance run (J7) recorded on the PR.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Build the E3 authoring editor in Atlantis Editor App (Spec 0068 R1–R10):
- a headless C# model library held to E1/E2's C++ models by generated vectors;
- a tree Hierarchy, a reflective Inspector, and a Gizmo that hit-tests and edits;
- two-document editing with automatic, coalesced apply;
- an undo/redo history over the authoring document, structural edits included;
- apply / save / play controls.

The engine, the wire and the SDKs are unchanged.

## Pre-drafting reading (cited, not restated)

- **The models:** ADR-0123 and its two amendments, ADR-0124, ADR-0125, ADR-0127, and E1/E2's models
  (`src/editor/include/atlantis/editor/model/*.h`), with the ImGui view's Gizmo handle math
  (`src/editor/src/view/editor_view.cpp`, the handle layout, the 8-pixel pick and the pointer mapping).
- **The authoring layer:** ADR-0129–0131 and their amendments, Spec 0065 (R4's closed command set, R12's test-only
  edit target in `tests/authoring/authoring_edit_target_tests.cpp`, Q8's reflective C# session) and Plan 0065 (J3,
  J5, J9).
- **The overlay:** ADR-0132/0133, Spec 0066 and Plan 0066 (J8: R-a kept).
- **Drawing and picking:** ADR-0134.
- **The C# SDK:** `AuthoringSession` (`src/csharp/Atlantis.Gameplay/Remote/AuthoringSession.cs`, `Authoring.cs`),
  `RemoteControl` and `RemoteSession.SubmitTransaction` (`RemoteSession.cs`), and `ViewportOverlay`.
- **The conformance mechanism:** `tests/csharp/conformance/` (`checkCurrent`, `camera_vectors.jsonl`).
- **The editor app today:** `src/editor_app/` (`EditorSession.cs`'s worker, `EditorCamera.cs`, `GizmoOverlay.cs`, the
  test runner in `tests/Atlantis.Editor.Tests`).

## Plan of changes

- **P1 — Model library** (`src/editor_app/Atlantis.Editor.Model/`, a new .NET 10 class library with no Avalonia and
  no package). It depends on the C# SDK.
  - **Ported:** the camera (all of `viewport_camera.h`), picking and markers (`viewport_picking.h`), the Gizmo
    (`gizmo.h` math, and J2's handle functions), the world-matrix solve, and the property history (`edit_history.h`
    with ADR-0136's record variants).
  - **New:** the authoring edit target (ported from the test-only target); the tree Hierarchy model; the Inspector
    model over `authoring.schema` (E2's `FieldControl` kinds); the selection; the apply-state machine (ADR-0135 D3);
    the two-document drag controller (ADR-0135 D1–D2).
  - The existing `EditorCamera.cs` and `GizmoOverlay.cs` move into it, held by the same vectors.
- **P2 — Vectors** (ADR-0137 D1), generated in `tests/csharp/conformance/conformance_tests.cpp`, each with a
  stale-file test:
  - `camera_vectors.jsonl` (extended to the whole camera);
  - `picking_vectors.jsonl`;
  - `gizmo_vectors.jsonl` (math and handles);
  - `world_matrix_vectors.jsonl`;
  - `history_transcripts.jsonl` (scripted operations against a fake target, for E2's property semantics).
- **P3 — The Gizmo handle math in C++** (J2, ADR-0137 D3).
  - `gizmo.h` gains pure functions for the handle layout from a camera, a viewport and a matrix, the hit-test, and
    the pointer-to-amount mapping.
  - `editor_view.cpp` calls them instead of its inline math, with no change in behaviour. E1's tests stay unchanged
    and green, and a new C++ test pins the functions.
- **P4 — Panels** (`src/editor_app/Atlantis.Editor/`): the Hierarchy (a virtualized tree), the Inspector, the Gizmo
  interaction, the toolbar (Undo/Redo with labels, the apply state, Save, Play/Pause/Step) and the selection wiring.
  The worker (`EditorSession.cs`) drains authoring and Runtime events per UI tick.
- **P5 — Tests.**
  - **The model runner** (`src/editor_app/tests/`): vectors, fakes for the target, the session and the Runtime
    World, the tree, the Inspector, the structural records and refusals, the drag controller (preview, commit,
    revert on refusal) and the apply-state machine.
  - **A live GPU integration test:** a scripted session against `atlantis_runtime` on the default scene and Bistro
    (Spec Testing).
- **P6 — Measurement mode** `--measure-edit` (as Plan 0066 M6): drag latency with node and subtree previews,
  release → `Applied`, commit → `Applied`, undo → `Applied`, and Hierarchy open time, on both scenes.
- **P7 — Docs:**
  - **normative, with the implementation:** `docs/architecture/module_boundaries.md` (the editor app's
    paragraph: its model library, the vectors and two-document editing);
  - **narrative, after merge** (J9): the registry row, the Spec/Plan implementation lines and the blueprint.

## Milestones / Task Breakdown

1. **M1 — Model library and vectors** (P1's ported models, P2, P3). The library builds headless, the C# runner
   checks every vector file, the C++ generators are green, and E1's tests are unchanged and green.
2. **M2 — Hierarchy and selection** (R3, R6; P4 in part).
   - The tree follows events; create, destroy (subtree) and reparent (keeping world placement, Q5) each run as one
     transaction and then an apply.
   - The one selection drives the overlay (outline, markers).
   - Another client's `NodeDestroyed` deselects.
3. **M3 — Inspector** (R4): reflective editors, commit on Enter or blur as one transaction and an apply, add/remove
   component, refusal shown with the field reverted, outside changes followed.
4. **M4 — Gizmo hit-test and edit** (R5, R7).
   - Translate/rotate/scale, world/local and snapping.
   - Subtree previews on the Runtime World per batch; release → local `Transform` → one transaction → apply; revert
     on refusal; the disabled-handle rule under a non-TRS parent.
   - `--measure-edit`'s drag and release numbers.
   - **The J8 checkpoint follows M4.**
5. **M5 — Undo/redo, structural included** (R8, ADR-0136): property and structural records, the refusal rules, and
   the history across applies (cleared on `DocumentReopened`). Each undo and redo is a transaction and then an apply.
6. **M6 — Apply, save and play UI** (R9): the apply-state indicator (including `pending: paused`), Save with the
   imported-scene refusal, Play/Pause/Step, and the gameplay-client note during play.
7. **M7 — Acceptance and normative docs:**
   - the live integration test;
   - every `--measure-edit` number on both scenes, reported against Q8;
   - `module_boundaries.md`;
   - the owner's acceptance run (J7) scripted and recorded on the PR.

## Files / Modules Touched (expected)

- **New:** `src/editor_app/Atlantis.Editor.Model/**`; `tests/csharp/conformance/{picking,gizmo,world_matrix}_vectors.jsonl`
  and `history_transcripts.jsonl`.
- **Changed:**
  - `src/editor_app/Atlantis.Editor/**` (panels, worker, toolbar);
  - `src/editor_app/tests/**` (the runner and the integration test);
  - `src/editor_app/CMakeLists.txt` and the solution or project files (the new project and its tests);
  - `src/editor_app/allowed_packages.txt` and `packages.lock.json` only if an Avalonia package is added (ADR-0127,
    disclosed);
  - `tests/csharp/conformance/conformance_tests.cpp`, `camera_vectors.jsonl` (extended) and its `CMakeLists.txt`;
  - `docs/architecture/module_boundaries.md`.
- **J2 only:** `src/editor/include/atlantis/editor/model/gizmo.h`, `src/editor/src/model/gizmo.cpp`,
  `src/editor/src/view/editor_view.cpp`, and a new test under `tests/editor/`.
- **Not touched:** `src/runtime/**`, `src/renderer/**`, `src/authoring/**`, `src/connection/**`, `src/remote/**`, the
  C# SDK, the CLI, the schema, every golden, `tests/performance/**`.

## Sequencing & Dependencies

- **M1 first:** every panel builds on the models.
- **M2 before M3 and M4:** they act on the selection.
- **M4 before the J8 checkpoint, and J8 before M5:** a subtree-preview ruling can change the drag controller that
  undo replays.
- **M5 before M6:** the toolbar shows the history.
- **M7 last.**
- **No dependency on Spec 0069** (node names). When it lands, the Hierarchy's label gains the name in a follow-up.

## Risks & Mitigations

- **Subtree previews on Bistro:** measured at M4; the node-only fallback is J3's.
- **Euler branch flips near ±90° pitch** after a rotate drag: correct values, surprising numbers. Disclosed in the
  Inspector's note for `Transform`, as E2 notes it today.
- **Hierarchy scale (about 6000 nodes on Bistro):** virtualized rows. The open time is measured (Q8).
- **A model that drifts from the C++ reference:** the stale-file and C# vector tests fail in the same build.

## Joint Review decisions — proposed

- **J1 — Spec Corrections:** none proposed.
- **J2 — Where the Gizmo handle vectors come from:**
  - **Recommended:** move E1's handle layout, hit-test and pointer mapping from `editor_view.cpp` into `gizmo.h` as
    pure functions, with no change in behaviour (ADR-0137 D3). The frozen view calls them, and E1's tests stay
    green.
  - **The alternative:** a C++ test-only copy of that math, with the risk of drift from the view recorded.
- **J3 — Preview scope during a drag:**
  - **Recommended:** the dragged node and all its descendants (ADR-0135 D1), measured at M4.
  - **If Bistro's deep subtrees break Q8's drag target,** the owner rules at J8 between node-only previews and a cap
    (for example 256 descendants, beyond which only the node previews).
- **J4 — Reparent keeps world placement** (Spec Q5): `SetParent` plus the local `Transform` that keeps the world
  matrix. Where that is not a TRS within `gizmo.h`'s tolerance, the local values are kept, and the result is stated in
  the status line. Proposed: accept.
- **J5 — Project layout:** `Atlantis.Editor.Model` as its own project with no Avalonia reference, so the model runner
  needs no UI. The editor app references it. Proposed: accept.
- **J6 — Vector tolerances:** 2e-5 absolute for floats, as the camera vectors use; exact for integers, GUIDs, enums
  and history results. Proposed: accept.
- **J7 — The owner's acceptance run,** after M6, scripted step by step and recorded on the PR (as Plan 0065 J7):
  1. **The default scene:** pick, drag-translate, release (applied); rotate with snapping (applied); edit a light's
     intensity in the Inspector (applied); create a node under the camera, reparent it, destroy its parent's
     subtree, undo (subtree back), redo; save, restart the Runtime on the saved file, and see the same frame.
  2. **Bistro:** the same edits without save, which is refused with the reason.
  3. **A second client:** a CLI client edits the selected field; the Inspector follows, and the editor's undo is
     refused "changed elsewhere".
- **J8 — The M4 checkpoint:** the owner rules on M4's drag and release numbers against Q8 (J3's preview scope) before
  M5 starts.
- **J9 — Docs split:** normative sentences with the implementation PR, narratives after merge (Plans 0062–0067).

## Verification Checklist

- [ ] **Unit tests:** the C# vectors (camera, picking, Gizmo math and handles, world matrix, history transcripts);
      fakes for the tree, the Inspector, the structural records and their refusals, the drag controller and the
      apply state; the C++ generators' stale-file tests; E1's tests unchanged (J2).
- [ ] **Headless integration tests:** the model runner against fakes, with no UI.
- [ ] **Live integration** (GPU suite): the scripted session on the default scene and Bistro, checking the document,
      the frame data and captures after each step.
- [ ] **Image regression:** every golden byte-identical, none re-captured.
- [ ] **Vulkan Validation Layers:** the GPU suites stay clean; the editor adds no Runtime path.
- [ ] **Other:** `--gate --applies` reported (expected false); `assembleDebug` (the Android build must not see the
      editor app); the C# SDK's zero-NuGet check and the editor-app package checks; `--measure-edit` reported against
      Q8; the owner's J7 run recorded.

## Rollback Plan

Revert the implementation PR:
- the editor app returns to its Spec 0066 state;
- the vectors and their generators go with it;
- J2's extraction reverts to the view's inline math.

The engine is untouched, so no golden, baseline or wire format is affected.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:
- the owner's J7 acceptance run recorded on the implementation PR;
- Q8's measurements reported, and ruled at J8 where missed;
- the narrative docs (J9) in their own PR after merge.
