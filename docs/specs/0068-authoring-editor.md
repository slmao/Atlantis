# Spec: The E3 Authoring Editor in Atlantis Editor App (Spec 0064 Phase 2, step 2)

- **Status:** Approved
- **Implementation:** merged — M1–M7 in [PR #303](https://github.com/slmao/Atlantis/pull/303), on `main` at
  `3a99f74`. Q8 ruled at merge: the one miss (undo → `Applied` on Bistro, 173.7 ms median against 150) accepted as
  measured; the J7 acceptance run executed through the product's session layer with the real CLI as the second
  client and recorded on the PR (both scenes green; the same-frame step follows Spec 0065's north star,
  save → rebuild → restart).
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-27
- **Related Plan(s):** [Plan 0068](../plans/0068-authoring-editor.md) (`Approved`; Joint Human Review 2026-10-27
  recorded in its header). As with Specs 0062–0067, the Plan was drafted together with this Spec.
- **Approval:** slmao, 2026-10-27 — owner rulings Q1–Q8, each as its recommendation, with one change: a reparent
  whose world placement cannot be kept is refused (Plan J4). Recorded once in
  [Plan 0068](../plans/0068-authoring-editor.md). Reviewed in this Spec's own branch PR,
  [PR #302](https://github.com/slmao/Atlantis/pull/302), where the rulings are recorded before it merges. The
  open-question text keeps the options as the record of what was weighed.
- **Related ADR(s):**
  - [ADR-0135](../adr/0135-two-document-edit-and-apply.md) (`Accepted` 2026-10-27): how the editor edits on two documents. A drag
    previews on the Runtime World. A committed edit is one authoring transaction, followed by an apply. The history
    lives on the authoring document and is not cleared by the editor's own applies (Q1, Q2).
    - **Extends** ADR-0124 D1/D4 and ADR-0131 D7.
    - **Amends** ADR-0131 D5's history clause for a history over the authoring document.
  - [ADR-0136](../adr/0136-structural-edit-records.md) (`Accepted` 2026-10-27): structural edits are history records (create,
    destroy, add and remove component, reparent), with what each record holds and when its undo is refused (Q3).
    **Extends** ADR-0124 D1–D4 (its "later extension of the write list's variant").
  - [ADR-0137](../adr/0137-csharp-model-port-held-by-vectors.md) (`Accepted` 2026-10-27): the C# port of E1/E2's models is held
    to the C++ models by vectors that C++ tests generate. Behaviour with no C++ reference is specified by an ADR and
    tested in C# (Q4). **Extends** ADR-0125 D4, which already names the C++ models as the reference contracts a C#
    port keeps; D4 itself is not amended.
  - Cited, unchanged: [ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md) D1–D3,
    [ADR-0127](../adr/0127-editor-application-managed-dependency-policy.md),
    [ADR-0129](../adr/0129-authoring-document-ownership.md),
    [ADR-0130](../adr/0130-authoring-access-interface-and-module-boundary.md),
    [ADR-0132](../adr/0132-viewport-overlay-surface.md), [ADR-0133](../adr/0133-attachment-load-after-the-scene.md),
    [ADR-0123](../adr/0123-editor-viewport-camera-and-picking-host-services.md) (with its two amendments),
    [ADR-0134](../adr/0134-lit-normal-transform-by-cofactor.md), [ADR-0103](../adr/0103-runtime-world-operation-boundary.md)
    (no World verb).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Spec 0064's Phase 2 is "an editor games are developed in". Its data layer (Spec 0065) and its Viewport overlay
(Spec 0066) have landed. This Spec is the editor itself, first cut: **Atlantis Editor App edits the authoring
document** through `AuthoringConnection`, shows the result in the embedded Viewport, and draws its selection and
Gizmo through the Viewport overlay. It has:
- a tree **Hierarchy** over the document;
- a reflective **Inspector** driven by the authoring schema;
- a **Gizmo** that hit-tests and edits translate, rotate and scale;
- **undo/redo**, including structural edits;
- **apply / save / play** controls.

It recommends a two-document editing model. A drag previews on the Runtime World, as E1's Gizmo does. A committed
edit is one authoring transaction, applied automatically, with coalescing. The history lives on the authoring
document. E1/E2's C++ models are ported to C# and held to their contracts by vectors that C++ tests generate, as the
camera already is. No wire call, World verb or engine module is added. The ImGui editor stays frozen at E2.

## Motivation / Problem Statement

### Phase 2's split (background)

Spec 0066 recorded Phase 2's split:
1. the Viewport overlay and the input measurement (Spec 0066, merged);
2. **this Spec:** the E3 authoring editor in Atlantis Editor App;
3. node names in the scene grammar (Spec 0069, in parallel);
4. later: content browsing (it needs a catalog query on the wire), incremental validation (Plan 0065 J9) and live
   projection of document edits (Spec 0065 Out of Scope).

### What is settled and not reopened

- **One editor, one surface** (ADR-0125):
  - the product editor is the separate C#/Avalonia process, a client of the shared surface with no Runtime
    privilege;
  - every capability is a call on that surface for the CLI and both SDKs alike (D2);
  - the ImGui editor is frozen at E2, and its E1/E2 C++ models and ADR-0123/0124 are the reference contracts a C#
    port keeps (D4).
- **The authoring document and its apply** (ADR-0129–0131, with their amendments):
  - one C++ `AuthoringSession` owns the document, hosted by `atlantis_runtime`, and every client edits it through
    `AuthoringConnection` with a closed command set and closed events;
  - the document is valid after every transaction, which is refused whole otherwise;
  - an explicit apply cooks in memory and reloads the Runtime's scene all or nothing at a frame boundary, and a
    newer apply replaces a pending one (Plan 0065 J5);
  - a pause holds a requested reload until a frame applies commands (ADR-0131, amended 2026-10-23);
  - runtime-side edits are not written back, and the next reload replaces them (ADR-0131 D7);
  - save writes hand-authored sources only, and an imported scene is refused with `ImportedSceneNotSavable` (ADR-0129
    D4);
  - the C# `AuthoringSession` is reflective only (Spec 0065 Q8).
- **The Viewport overlay** (ADR-0132, ADR-0133):
  - per client, a closed primitive set with screen-constant groups, plus a selection set that the Runtime outlines;
  - the Gizmo's geometry and hit-testing are client-side;
  - a capture shows no overlay unless asked.
- **Input:** route R-a is kept. Plan 0066 J8 measured about three Runtime frames of drag latency (3.00 / 2.99 frames
  median) with no UI gap over 100 ms.
- **Drawing and picking:** lit entities with non-uniform scale or shear draw and pick (ADR-0134; ADR-0123's
  2026-10-25 and 2026-10-26 amendments).

### Facts found while drafting

1. **Two documents, two shapes:**
   - The Runtime World has no hierarchy (Spec 0051 R5). Its transform is `WorldMatrix`.
   - The authoring document has the hierarchy (`parent`) and each node's local `Transform` (position, Euler rotation,
     scale; R = Ry·Rx·Rz, ADR-0050).
   - A Runtime entity's GUID is its node's GUID (Spec 0050's `EntityGuidMap`, one entity per node), so a Viewport
     pick names a document node directly.
   - A node created but not yet applied has no entity: nothing to pick or outline until the apply.
2. **The command set is closed** (Spec 0065 R4, ADR-0130 D1): `SetProperty`, `CreateNode`, `DestroyNode`,
   `AddComponent`, `RemoveComponent`, `SetParent`, `SetActiveCamera`.
   - `CreateNode` takes a client-chosen or minted GUID and a position in the node order, which is immutable after
     creation (a Spec 0065 Non-Goal).
   - `SetParent` changes the parent only: the node keeps its local `Transform` and its position.
   - `DestroyNode` removes one node. A child left pointing at a missing parent fails validation (`ParentExists`), so
     destroying a node with children is refused unless the same transaction destroys or reparents them.
   - Validation also refuses cycles (`ParentAcyclic`). It does not require a parent to precede its child.
3. **Validation costs about 22 ms per transaction on Bistro** (Plan 0065 J9: 22.2 ms median against a 10 ms target,
   ruled to proceed; incremental validation is a later Spec).
   - An apply plus reload costs 18.5–20.3 ms, plus a 14–28 ms swap frame, on Bistro (Plan 0065 J3, recorded in the
     registry).
   - Authoring writes per pointer batch would therefore cost one validation each.
4. **E2's history is over the Runtime World:**
   - ADR-0124: an Editor Transaction is one gesture's property writes, addressed by `scene::PropertyAddress`;
     previews are unrecorded; undo and redo write forward; a step is refused "changed elsewhere"; depth 128.
   - ADR-0131 D5 clears E2's history whenever the scene generation changes, that is, at every apply.
   - The edit-target seam is proven over `AuthoringConnection` by a test-only target
     (`tests/authoring/authoring_edit_target_tests.cpp`, Spec 0065 R12), which runs E2's unchanged history.
5. **ADR-0124 excludes structural edits.** They are "a later extension of the write list's variant" (ADR-0124 D4).
6. **E1's Gizmo hit-testing is in the ImGui view, not the model.** `GizmoModel` (`gizmo.h`) composes, decomposes,
   snaps and drags along an axis it is given; it has no hit-testing.
   - The handle layout (80-pixel screen handles), the pick (within 8 pixels of a segment, or of a rotate ring) and
     the pointer-to-amount mapping are in `src/editor/src/view/editor_view.cpp`.
   - A C# port held by vectors needs that math in a place a C++ test can call (Plan J2).
7. **Atlantis Editor App today** (about 3 kLOC, `src/editor_app/Atlantis.Editor/`) has:
   - an orbit/dolly port of the editor camera held to `camera_vectors.jsonl`;
   - geometry picking through `raycast` (no light or camera markers);
   - a translate Gizmo drawn through the overlay (no hit-testing, no editing);
   - an entity list over the Runtime World;
   - the measurement modes.

   Its packages follow ADR-0127 (Avalonia's own, pinned, locked). It has no hierarchy, inspector, history or
   authoring use.
8. **The scene grammar has no node names.** Nodes are shown by GUID prefix and component kind. Spec 0069 adds names
   in parallel, and this Spec must work without them.

### Why now

The data layer and the overlay exist and have been measured; the editor that uses them is the remaining piece of
Phase 2's first cut. Everything below is client-side, so it can land without touching the frame path.

## Goals

- **Edit the authoring document from the editor:** select, create, destroy and reparent nodes; edit every component
  field; move, rotate and scale with a Gizmo. Each committed edit is one authoring transaction.
- **See every committed edit in the Viewport without a manual step,** with the apply's state always visible.
- **Undo and redo every edit the editor makes,** structural ones included, as authoring transactions, across the
  editor's own applies.
- **Hold the C# models to E1/E2's C++ contracts by generated vectors,** extending the camera precedent.
- **Measure and report** drag latency, release-to-applied, commit-to-applied and undo-to-applied on Bistro and the
  default scene.
- **Work with other clients:** CLI and agent edits appear in the editor; nothing is locked.

## Non-Goals

- **A content browser** (asset lists need a catalog query on the wire, Phase 2's later step). An Inspector GUID
  field takes a typed or pasted GUID.
- **Prefabs, multi-selection editing** (more than one selected node is outlined only), **custom inspectors,
  plugins, scripting in the editor.**
- **Node names** (Spec 0069). When that grammar lands, the Hierarchy shows a name where there is one.
- **Live projection** of document edits into the Runtime World (Spec 0065 Out of Scope), and **incremental
  validation** (Plan 0065 J9).
- **Reordering nodes**, a node move command, or any new `AuthoringConnection` command or event.
- **Any wire call, World verb, Runtime or Renderer change.** If one turns out to be needed, it is added to the shared
  surface for the CLI and both SDKs (ADR-0125 D2), in a Correction to this Spec.
- **Changes to the ImGui editor's behaviour** (frozen, ADR-0125 D4). The only C++ editor change is J2's
  behaviour-preserving extraction of the Gizmo handle math, if ruled.
- **Saving imported scenes, "save as", multi-document sessions** (Spec 0065 Out of Scope).
- **Android or non-Windows editor hosting.**

## Requirements

### Functional

- **R1 — The C# models.** A model library inside `src/editor_app/` with no Avalonia dependency, testable headless.
  - **Ported from E1/E2:** the camera (`viewport_camera.h`, all of it: look, orbit, pan, dolly, fly, frame), picking
    (`viewport_picking.h`, including light and camera markers), the Gizmo (`gizmo.h`: decompose, compose, snapping,
    world/local axes, drag), and the property history (`edit_history.h`).
  - **New:**
    - the Gizmo's handle layout, hit-test and pointer-to-amount mapping (E1's view math, Fact 6);
    - the world-matrix solve over the document's hierarchy: the authoring stage's `world::World`
      (`updateTransforms()` / `getWorldMatrix()`), whose result the bake copies bit for bit (Spec 0051);
    - a production authoring edit target (the C# counterpart of Spec 0065 R12's test-only target);
    - an Inspector model over the authoring schema;
    - a tree Hierarchy model over the document.
- **R2 — Vectors** (ADR-0137). Every ported function with a C++ reference is checked against vectors that a C++ test
  generates from the C++ code and commits under `tests/csharp/conformance/`. The C++ test fails when a committed
  file differs from what the C++ code computes now, as `camera_vectors.jsonl` does. The C# test runner checks them.
  Behaviour with no C++ reference (the tree, the structural records, the two-document flow) is specified by
  ADR-0135/0136 and tested in C# against fakes and the live Runtime.
- **R3 — Hierarchy.**
  - **The tree:** nodes in document order under their parents, each shown as a GUID prefix and its components'
    kinds (for example `2c5aa0de · Transform Camera`), with a filter.
  - **Following the document:** the tree follows authoring events (`NodeCreated`, `NodeDestroyed`, `ParentChanged`,
    `ComponentAdded`, `ComponentRemoved`, `DocumentReopened`).
  - **Create:** a node with a Transform is created under the selected node (or at the root), appended to the order,
    in one transaction.
  - **Destroy:** the selected node and its whole subtree go in one transaction (Fact 2).
  - **Reparent:** by drag and drop, one transaction: `SetParent` plus the local `Transform` that keeps the node's world matrix. Where that local matrix is not a TRS within `gizmo.h`'s tolerance (under a non-uniformly scaled parent), the reparent is **refused**: its reason is shown and the document is unchanged (J4, as ruled). A refusal by the document (`ParentAcyclic`, …) also
    shows its reason and changes nothing.
  - **Bistro:** about 6000 nodes stay usable (virtualized rows; Q8 reports the time to open).
- **R4 — Inspector.**
  - **Reflective:** the selected node's components and every field, from `authoring.schema`. Each field has a typed
    editor by its kind (integer, float, float3, float4, GUID, enum: E2's `FieldControl` set). Optional fields show
    absent or present.
  - **Commit:** on Enter or loss of focus, as one authoring transaction (one `SetProperty`). Add and remove
    component are commands on the same panel.
  - **Refusals and outside changes:** a refusal shows its reason, and the field reverts to the document's value.
    Values changed by other clients update the panel through events.
- **R5 — Gizmo.**
  - **Modes:** translate, rotate and scale, in world or local space (scale is always local), with E1's snapping
    (0.5 units, 15°, 0.1 scale; while Ctrl is held).
  - **Drawing and picking:** handles are drawn through the overlay's screen-constant groups (ADR-0132 S-a). They are
    hit-tested client-side with E1's layout (80-pixel handles, an 8-pixel pick) and the same anchor scale the
    Runtime uses.
  - **The drag:** previews and commits as Q1 says.
  - **Matrices:** the new world matrix is converted to the node's local `Transform` through its parent's world
    matrix. Where that local matrix is not a TRS within `gizmo.h`'s shear tolerance (a rotation or scale under a
    non-uniformly scaled parent), that mode's handles are shown disabled for the node, with the reason. Translate is
    always available. The same TRS rule refuses a reparent that cannot keep the node's world placement (R3).
- **R6 — Selection.** One selection, shared by the Viewport pick, the Hierarchy and the Inspector, and sent as the
  overlay's selection set with the Gizmo (Q6).
  - Lights and cameras are pickable by E1's markers (overlay points).
  - A selected node destroyed by any client is deselected.
  - A selection survives applies (GUIDs persist).
- **R7 — Two-document editing** (ADR-0135):
  - a Gizmo drag writes previews to the Runtime World;
  - a committed edit (release, Inspector commit, Hierarchy command, undo or redo) is one authoring transaction,
    then an apply.
  - Q1 and Q2 fix the details.
- **R8 — History** (ADR-0135, ADR-0136).
  - **What is recorded:** E2's property history ported, plus structural records for create, destroy (subtree),
    add/remove component and reparent.
  - **How a step runs:** undo and redo are authoring transactions, then an apply.
  - **Refusals:** a step is refused when the document no longer holds what the edit left: E2's "changed elsewhere"
    for values, and ADR-0136's rules for structure.
  - **Lifetime:** depth 128. The history is not cleared by applies; it clears on `DocumentReopened`.
- **R9 — Apply, save, play.**
  - **The apply state** is always visible: `applying`, `applied · generation N`, `pending: paused (applies on Step or
    Resume)`, or `failed: <reason>` (the document is unchanged and the next edit retries).
  - **Save** is refused for an imported scene, with the reason (`ImportedSceneNotSavable`).
  - **Play, Pause and Step** come from `RuntimeControl`.
  - There is no explicit Apply button; edits apply automatically (Q2).
- **R10 — Other clients** (Q7). The Hierarchy and Inspector follow every client's authoring events. The history
  refuses steps whose state changed elsewhere. Nothing is locked.

### Non-functional

- **Performance:** reported, not judged, on the reference machine (Q8): drag latency, release → `Applied`,
  Inspector commit → `Applied`, undo → `Applied`, and the time to open the Hierarchy, on Bistro and the default
  scene.
- **Memory:** the editor holds the document's node list, parents and component kinds, plus the selected node's
  values. It holds no copy of the document's values beyond what panels show (ADR-0129 D1: clients hold no copy).
- **Portability:** Windows only, as Atlantis Editor App. The model library is plain .NET 10, with no Avalonia and no
  Win32.
- **Dependencies:** no new package beyond Avalonia's own, under ADR-0127. A new Avalonia package (for example a
  tree data grid) is an allow-list addition reviewed in the implementation PR.
- **Engine:** no change to `src/runtime/**`, `src/renderer/**`, the wire, the SDKs' surfaces or the schema. Every
  golden and the performance gate are unaffected (`--gate --applies` is expected false).

## Proposed Design

**The editor's layers.**
- **`Atlantis.Editor.Model`** (new C# project): the models of R1, depending on the C# SDK only.
- **`Atlantis.Editor`** (the Avalonia app): binds panels to models.
- **One worker thread owns `RemoteSession`,** as `EditorWorker` does today. It drains authoring and Runtime events
  once per UI tick and posts model updates.

**The edit flow:**
1. **Pick or select.** A Viewport click ray-casts (`raycast`) or hits a marker. The result's GUID selects the node.
   The overlay is resent with the selection and the Gizmo for that node's world matrix.
2. **Drag.**
   - Each pointer batch moves the Gizmo model. The new world matrices of the node and its descendants are written to
     the Runtime World in one `RuntimeConnection` transaction (preview, unrecorded).
   - The Gizmo overlay is resent with the previewed matrix.
3. **Release.**
   - The final world matrix is converted to the node's local `Transform` (R5) and submitted as one authoring
     transaction: one `SetProperty` per changed field.
   - When it settles as applied, the history records it and an apply is requested.
   - If the transaction is refused, the preview is reverted on the Runtime World and the reason is shown.
4. **Apply.** The Runtime reloads at its next commanding frame and reports `Applied{generation}`. The previewed
   World is replaced by the baked result, with the same values. Applies requested while one is pending coalesce
   (J5).
5. **Undo / redo.** The history submits the reverse or forward authoring transaction, then requests an apply.

**Between release and `Applied`,** the Viewport keeps showing the last preview, which equals the committed result for
the node and its subtree, so nothing jumps. On Bistro the expected wait is the transaction's validation (about 22 ms),
the in-memory cook (about 19 ms) and the swap frame (14–28 ms): about 60–80 ms. Under a pause, previews and the apply
are both held, because a pause holds command application. The editor says so, and Step or Resume shows them.

**Inspector and Hierarchy edits** have no preview; they are visible at `Applied`.

**Hierarchy tree.** The model is built from `ListNodes()` (`NodeSummary(Node, Parent)`, document order) and the
components per node, then kept current by events. Reparenting runs a cycle check client-side and accepts the
document's refusal as final.

## Architectural Impact

Three decisions, each with an ADR (`Accepted` 2026-10-27):
- **Two documents** (ADR-0135):
  - previews go to the Runtime World and commits to the authoring document, followed by an automatic, coalesced
    apply;
  - a history over the authoring document survives the editor's own applies, which amends ADR-0131 D5's history
    clause for such a history.
- **Structural records** (ADR-0136): extends ADR-0124 with the record variants and their refusal rules.
- **The C# port held by vectors** (ADR-0137): extends ADR-0125 D4 with the mechanism (C++-generated vectors), and
  with where C# alone is the reference.

No subsystem boundary, public C++ API, dependency (beyond ADR-0127's own allow-list), threading or ownership model of
the engine changes. One C++ change is possible, behind J2: moving E1's Gizmo handle math from the ImGui view into
`gizmo.h` as pure functions, behaviour-preserving, so that a C++ test can generate its vectors.

## Alternatives Considered

- **Edit the Runtime World and write back on demand.** This is rejected by ADR-0131 D7 (runtime edits are not
  written back) and by the World's lack of hierarchy.
- **Keep the history on the Runtime World (E2 as is).** Every apply would clear it (ADR-0131 D5), and structural
  edits have no Runtime meaning.
- **An editor-only channel** (for example a direct document file edit). Rejected by ADR-0125 D2 and ADR-0129 D1.
- **Write a C# implementation without vectors.** Two implementations of the camera, Gizmo and history would drift
  apart unseen. The camera vectors of Spec 0064 Phase 1 exist to prevent exactly that.

## Testing & Verification Plan

- **Unit (C#, headless):** each model against its vectors (R2): camera, picking and markers, Gizmo
  compose/decompose/snap/drag, handle hit-testing and pointer mapping, the world-matrix solve, and property-history
  transcripts.
  - **Tested on fakes:** the tree model, the Inspector model, the structural records and their refusals, and the
    two-document flow (preview, commit, apply request, refusal revert).
- **C++:** the vector generators, each failing when its committed file is stale. If J2 is ruled extract, the ImGui
  editor's existing E1 Gizmo tests stay green unchanged.
- **Integration (live Runtime, GPU suite):** a scripted session on the default scene. It runs pick, drag, release,
  `Applied`, undo, redo, create, destroy, undo of destroy and save, and checks the document (`scene get`), the frame
  data and a capture after each step. On Bistro the same steps run without save, which is refused with
  `ImportedSceneNotSavable`.
- **Other clients:** a CLI client edits the selected node while the editor holds it. The Inspector updates, and the
  editor's undo of the overwritten field is refused "changed elsewhere".
- **Unaffected engine:** goldens unchanged; `--gate --applies` false.
- **Measurements** (Q8): reported, not judged.
- **Owner acceptance run** (Plan J7), scripted step by step and recorded on the PR, as Plan 0065 J7.

## Risks & Open Questions

Risks:
- **Preview cost on deep subtrees:** dragging a Bistro node with hundreds of descendants writes hundreds of
  `WorldMatrix` values per batch. Plan J3 measures it and keeps a node-only fallback.
- **Euler decomposition:** a rotate drag ends in a matrix decomposed to pitch/yaw/roll (R = Ry·Rx·Rz). Near ±90°
  pitch, the angles flip branches. The values stay correct, but the Inspector shows different numbers.
- **Applies during play:** an apply replaces the scene, gameplay clients' runtime-side changes included (ADR-0131
  D7). The editor names this in the apply state while gameplay clients are connected.
- **Validation latency:** about 22 ms per committed edit on Bistro. Rapid Inspector typing commits only on Enter or
  blur, so it adds no load per keystroke.
- **Imported scenes** (Bistro) cannot be saved: edits there are for trying things out, which the refusal states.

Open questions — all eight ruled by the owner (slmao, 2026-10-27), each as its recommendation, with Q5's reparent
rule changed to a refusal (Plan J4); rulings recorded in Plan 0068:

- **Q1 — The two-document editing model.** (ADR-0135)
  - **(E-a) Previews on the Runtime World per pointer batch; the release is one authoring transaction, then an
    apply; the history records the authoring transaction only** — **recommended**.
    - Previews keep E1's cost (no validation) and its three-frame latency.
    - ADR-0131 D7 makes them disposable: the next apply replaces them.
    - The release costs one validation.
    - The preview covers the dragged node and its descendants, which the editor computes from the document, because
      the Runtime World has no hierarchy.
    - Between release and `Applied` the user sees the last preview (equal to the result), the Gizmo stays at the
      released pose, and the apply state reads `applying`.
  - **(E-b) Authoring writes per pointer batch:** about 22 ms of validation per batch on Bistro (Plan 0065 J9), so a
    drag would lag several frames and load the session. Rejected.
  - **(E-c) Live projection of document edits into the World:** named by Spec 0065 as a later Spec. Not built here.
  - **Why (E-a):** it is E1/E2's preview contract (ADR-0124 D1) moved onto the document that owns the result.
- **Q2 — When to apply.** (ADR-0135)
  - **(A-a) Automatically after every committed edit, coalesced** — **recommended**.
    - A newer apply replaces a pending one (Plan 0065 J5). Each apply cooks the whole current document, so later
      edits include earlier ones.
    - The state is always visible (R9).
    - On Bistro each apply costs about 19 ms of cook plus a 14–28 ms swap frame. Coalescing keeps a burst of edits to
      one reload per Runtime frame at most.
  - **(A-b) An explicit Apply command:** cheaper on Bistro, but the Viewport then disagrees with the Hierarchy and
    Inspector until the user remembers to apply, and the Gizmo starts from stale World values.
  - **(A-c) Debounced (apply after N ms of no edits):** a middle ground that adds a delay to every edit and a second
    state to explain.
  - **Under a pause:** previews and the apply are held (ADR-0131, amended 2026-10-23). The state reads
    `pending: paused`, and Step or Resume shows them.
- **Q3 — Undo scope.** (ADR-0136)
  - **(U-a) Property edits and structural edits** — **recommended**. Records:
    - **create:** the GUID, the position, the parent and the components with their field values, so redo
      re-creates it exactly;
    - **destroy:** the subtree, each node with its GUID, position, parent, components and every field value, plus
      whether it was the active camera, so undo re-creates the subtree at its positions, ascending;
    - **add / remove component:** the component and its field values;
    - **reparent:** the old and new parent, and the old and new local `Transform` (Q5's world-placement rule).
    - An undo or redo is one authoring transaction, then an apply.
    - A step is refused when the document no longer holds what the edit left. For example, a created node that has
      since gained children or changed values is refused "changed elsewhere", and so is a GUID that exists again.
  - **(U-b) Property edits only, as E2:** structural mistakes could not be undone, and a destroy would lose the
    subtree's values for good.
- **Q4 — The C# models.** (ADR-0137)
  - **(M-a) Port E1/E2's models into a headless C# library, held by C++-generated vectors** — **recommended**.
    - Covers the camera, picking and markers, Gizmo math, handle hit-testing, the world-matrix solve and
      property-history transcripts.
    - Behaviour with no C++ reference (the tree, the structural records, the two-document flow) is specified by
      ADR-0135/0136 and tested in C#.
    - ADR-0125 D4 already names the C++ models as the reference contracts, so ADR-0137 extends it with the mechanism
      and needs no amendment.
  - **(M-b) A C# implementation with C# tests only:** no guard against drift from E1/E2.
  - **(M-c) Run the C++ models in the editor process (P/Invoke):** reopens ADR-0125 D1's "only engine access is the
    C# SDK", and adds a native dependency to the editor.
- **Q5 — Panels in this Spec.**
  - **(P-a) Recommended set:**
    - a tree Hierarchy (GUID prefix + kinds; select, create, destroy, reparent by drag);
    - a reflective Inspector (every field, typed editors, commit on Enter or blur);
    - the Gizmo with translate/rotate/scale hit-testing and editing (world/local, E1's snapping);
    - toolbar Undo/Redo with their labels;
    - the apply state;
    - Save (with the imported-scene refusal);
    - Play/Pause/Step.
  - **Reparent keeps the node's world placement, or is refused:** the transaction is `SetParent` plus the local
    `Transform` that keeps its world matrix. Where that is not a TRS within `gizmo.h`'s tolerance (R5's rule), the
    reparent is refused, its reason shown and the document unchanged. **Ruled** (J4 changed): refused, not kept with
    the local values.
  - **Out:** content browser, prefabs, multi-selection editing beyond the outline, custom inspectors, plugins.
  - **(P-b) A smaller first cut** (Hierarchy and Inspector, no Gizmo editing): the Gizmo is what the overlay was
    built for, and E1's math exists, so it costs little to include.
- **Q6 — Selection sync.**
  - **(S-a) One selection object** that the Viewport pick, the Hierarchy and the Inspector set and observe —
    **recommended**.
    - Every change resends the overlay (selection set and Gizmo).
    - `NodeDestroyed` for the selection, from any client, deselects.
    - A selection persists across applies by GUID.
    - A node created and not yet applied is selected in the panels with no outline until `Applied`.
  - **(S-b) Per-panel selections** synchronised by messages: more states, the same result.
- **Q7 — Concurrent editors and agents.**
  - **(C-a) Follow events, refuse stale history steps, no locking** — **recommended**.
    - The document is shared (ADR-0129), and every client sees every change (Spec 0065 R6).
    - An edit by the CLI or an agent appears in the Hierarchy and Inspector, and an apply by anyone updates the
      apply state.
    - The history's refusal rules (E2's "changed elsewhere", ADR-0136's structural rules) keep undo from overwriting
      another client's work.
  - **(C-b) Lock nodes while selected:** a new wire call (ADR-0125 D2), and an agent blocked by an idle editor.
- **Q8 — Budgets and acceptance.** Reported targets on the reference machine, Bistro and the default scene:

  | Measure | Target |
  |---|---|
  | drag latency (input → frame with the preview) | R-a's three Runtime frames (median), also with a subtree preview |
  | release → `Applied` | under 150 ms on Bistro, under 60 ms on the default scene (median) |
  | Inspector commit → `Applied` | same as release |
  | undo → `Applied` | same as release |
  | Hierarchy open (Bistro, ~6000 nodes) | under 2 s |

  - **Reporting:** reported, not judged; a miss is ruled by the owner at Plan J8.
  - **Acceptance:** the owner runs a scripted session (Plan J7) on both scenes. The result is recorded on the
    implementation PR.

## Out of Scope / Future Work

- **Later steps:** content browsing and a catalog query on the wire (Phase 2's later step), prefabs, multi-selection
  editing, custom inspectors and plugins (Phase 3), AI-native authoring (Phase 4).
- **Node names** (Spec 0069): the Hierarchy shows them once the grammar has them.
- **Platform work:** live projection, incremental validation, node reordering, "save as".
- **The ImGui editor adopting the overlay or the authoring document:** not planned (frozen).
