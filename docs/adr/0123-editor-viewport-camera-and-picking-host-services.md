# ADR 0123: Editor Viewport Camera and Picking as Host Services

- **Status:** Accepted
- **Date:** 2026-10-19 (accepted 2026-10-19)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-19 (owner rulings on Spec 0062 Q1–Q6, each as recommended, recorded once in
  [Plan 0062](../plans/0062-editor-viewport-navigation-picking.md); the ADR text was reviewed in
  [PR #270](https://github.com/slmao/Atlantis/pull/270))
- **Related Spec:** [Spec 0062: Editor Viewport Navigation and Picking (E1)](../specs/0062-editor-viewport-navigation-picking.md)
  (`Approved` 2026-10-19; D1 is its Q1, D2 its Q2 and Q4, D3 its Q3)
- **Related ADR(s):**
  - **Supersedes in part** [ADR-0108](0108-editor-host-and-viewport-composition.md) D4's sentence "The camera is
    the scene's active camera; the editor gets the frame's view and projection for its Gizmo", and **extends**
    D2's list of host-interface values. ADR-0108 D1, D3, D5 and the rest of D2 and D4 are unchanged.
  - Keeps [ADR-0103](0103-runtime-world-operation-boundary.md) and
    [ADR-0104](0104-runtime-world-transaction-atomicity.md) (the World's verbs),
    [ADR-0105](0105-runtime-connection-and-cli-client.md) (`RuntimeConnection`),
    [ADR-0106](0106-attachable-runtime-transport-and-control.md) (`RuntimeControl`, the protocol),
    [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) (extraction stays
    Runtime-private) and [ADR-0004](0004-phase1-threading-baseline.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The editor is an ordinary client** (Spec 0056 R1, ADR-0108 D2). It reaches the World only through
  `RuntimeConnection` and the lifecycle only through `RuntimeControl`. Its host gives it plain values and takes
  plain values back.
- **The World's verb surface is closed** (Spec 0052: five commands, five events; Spec 0053 transactions). The
  maintainer requires E1 to stay inside it and to prefer client-side solutions (2026-10-19).
- **E1 needs two things the client cannot do alone** (Spec 0062 Motivation):
  - **Render from another camera.** The frame reads the active camera at one place (`runFrame()`, and its
    `frame_capture.cpp` replica). The connection does not even say which entity that is. Writing the camera
    entity edits the World every client sees.
  - **Intersect geometry.** The connection carries World data only; `Renderable` names a mesh `AssetId`; Runtime
    drops CPU mesh data after upload.
- **Bistro's scale** (Spec 0062): 1.75 M triangles in 2909 instances of 551 meshes. A CPU copy of positions and
  indices is about 42 MB.
- **Alpha-masked materials exist**, so a geometric hit can differ from the drawn image.

## Decision

1. **The editor camera is client state; the host renders the Viewport from it** (Spec 0062 Q1, C-a).
   - The editor library owns the camera's pose, lens and navigation. It is never written to the World.
   - When the editor supplies a Viewport camera — a world matrix (eye and forward; up is world +Y), a vertical
     fov, near and far — the host substitutes it for the active camera's world matrix and lens. It does so at the
     one place `runFrame()` reads them, and at the same reads in the capture path (D3).
   - Exposure, fog and bloom remain the active camera's `Camera` component values, and the aspect remains the
     Viewport's.
   - Without a supplied camera, the frame is ADR-0108 D4's.
   - The host gives the editor the scene camera's pose and lens. The editor computes its own view/projection for
     its overlay with the host's convention, which a cross-check test pins.
   - The host receives a pose and lens, never matrices: the projection convention stays the host's.
2. **Picking is a host service over Runtime's own copy of mesh geometry** (Spec 0062 Q2, K-a; Q4, G-a).
   - Runtime retains each mesh's CPU positions, indices and local bounds. It does so only when its bootstrap
     configuration asks, which only the editor-hosting executable does.
   - Runtime answers a world-space ray with the nearest hit entity's GUID and distance. It tests every entity
     with `Renderable` and `WorldMatrix`, both triangle faces, at its current `WorldMatrix`, and ignores alpha
     masks.
   - It also answers an entity's mesh local bounds.
   - The geometry is Runtime's, owned beside the meshes it mirrors and released with them.
   - Both services are public `RuntimeApplication` methods, frame thread only and between frames, like
     `captureFrameData()`. They are called by the executable-private editor adapter.
   - They are not `RuntimeConnection` or `RuntimeControl` calls, and nothing reaches them over Atlantis Remote.
   - Entities without geometry (lights, cameras) are picked by the client from their projected positions.
3. **The editor–host interface grows by plain values, request/response, deferred by one frame** (ADR-0108 D2
   extended).
   - In, through `FrameContext`: the scene camera's pose and lens; the answer to the previous pick request
     (tagged with its request id); the bounds of the entity last asked about.
   - Out, after `Editor::frame()`: the Viewport camera (optional), a pick request (an id and a ray), and the
     entity whose bounds the editor wants.
   - The host answers between frames and hands the answer to the next editor frame.
   - No callback crosses the boundary. No editor type appears in a Runtime header, and no Runtime type in an
     editor header.
   - `FrameOverlay` gains one query, the Viewport camera, which defaults to none.
   - Under a supplied camera, `captureFrameData()` and `captureImage()` report and render the drawn camera
     (Spec 0062 Q3, F-a): frame data remains "the frame's own".
4. **The World surface does not change.** No command, event, query, component or protocol message is added for
   E1. Picking for remote clients or agents, if ever needed, is a separate Spec that would add a verb under its
   own ADR. This ADR does not pre-authorize it.

## Consequences

### Positive

- **E1 without a verb:** the World, its access layer, the connection, the protocol and the C# SDK are untouched.
- **Nothing below Runtime changes:** no Renderer pass, shader, RHI type or Vulkan code.
- **The game camera stays the game's:** gameplay clients that move the active camera, and agents reading frame
  data without an editor, see no difference.
- **Picking is deterministic and GPU-free to test**, works while paused, and follows live `WorldMatrix` edits.

### Negative / Trade-offs

- **`runFrame()` gains a fifth named overlay place** (the camera substitution), guarded like ADR-0108 D4's four.
  The capture path gains the same substitution.
- **Pixel-inexact picking** where alpha masks cut geometry; a click on a cut-out selects the masked surface.
- **Memory under `--editor`:** about 42 MB on Bistro.
- **A copied camera convention** in the editor, held equal by a test rather than by construction.
- **Host-only picking:** a remote editor or an agent cannot pick until a Spec adds it.
- **Captures follow the editor camera** when `--editor` and `--listen` are combined. An agent attached to an
  editor session sees the editor's view.

## Alternatives Considered

- **Navigate by writing the active camera's `WorldMatrix`** (Spec 0062 Q1 C-b): every client sees the game
  camera move, gameplay fights the editor, and the client does not know which entity is the active camera.
- **An editor-camera entity made active** (Q1 C-c): needs a sixth command to set the active camera, and puts an
  editor artefact into the World.
- **The editor sends view/projection matrices** instead of a pose and lens: the projection convention would
  then belong to the editor, and the host would invert matrices to recover the eye for shading.
- **A GPU ID pass** (Q2 K-b): pixel-exact, but a new Renderer pass, shader pair and readback stall for an
  editor convenience; GPU-only tests.
- **Client-only picking by origins** (Q2 K-c): not exact enough for overlapping meshes. It is kept for lights
  and cameras.
- **A `raycast` query on `RuntimeConnection`** (Q2 K-d): a new verb on a closed surface and a protocol change,
  answering from render-side data that is not World data.
- **Lazy or unconditional geometry retention** (Q4 G-b, G-c): a first-click stall and kept catalog state, or
  42 MB for every non-editor run.
- **A synchronous host callback for picking:** the result would be immediate, but a host callback would enter
  the editor library's interface, which ADR-0108 D2 keeps as plain values.

## Accepted Amendment — 2026-10-25

**Status of this section:** proposed in a bug-fix PR, [PR #295](https://github.com/slmao/Atlantis/pull/295), at the
owner's direction (slmao, 2026-10-25); **accepted by the owner's review and merge of that PR**. Until that merge it is
pending. **This section does not itself change this ADR's top-level Status** (`Accepted` above is unchanged). The
Decision, Context, Consequences and Alternatives Considered above are preserved verbatim and are not superseded except
where this section says so explicitly.

### Human Review — Amendment Acceptance (2026-10-25)

**Deciders:** slmao. The owner directed the fix on 2026-10-25 and accepts this amendment in the review of
[PR #295](https://github.com/slmao/Atlantis/pull/295); the merge of that PR is the acceptance.

### Amendment

1. **D2's picking service picks what the frame draws.** D2 says the service "tests every entity with `Renderable`
   and `WorldMatrix`". It now reads: every such entity **the frame draws**.
   - **Excluded:** an entity the frame's extraction skips for a non-conformal world matrix is not pickable: a
     `LitTextured`- or `PbrDirectLit`-bound entity whose `WorldMatrix` fails `checkConformalTransform()` (Spec 0019
     D7, Plan 0019 P15, Plan 0023 M8).
   - **Same test, same condition:** `pickRenderable()` applies the extraction's own test under the extraction's own
     condition, per entity, at the entity's current `WorldMatrix`. An unlit or material-less entity is picked at
     any matrix, because the frame draws it at any matrix.
   - **Everything else in D2 is unchanged:** both triangle faces, alpha masks ignored, the nearest hit, the
     retention rule. `renderableLocalBounds()` still answers an excluded entity's bounds.
2. **Why it changes.** Found by Spec 0066's M6 visual check: on Bistro a far-away pick outlined an entity the scene
   does not draw.
   - 368 of Bistro's 2909 Renderables carry a non-uniform scale in their transform chain, so the frame skips them
     every frame. The pick geometry kept them, so a ray could stop on invisible geometry.
   - For example, the centre click from Bistro's scene camera selected `64f9581d-f2e3-…`, an undrawn entity 1.74 m
     in front of the camera.
3. **What inherits it.** `RuntimeControl::raycast()` is `pickRenderable()` (ADR-0128 D2), so the wire, the CLI's
   `runtime raycast`, both SDKs and both editors inherit the rule with no change of their own.

### Consequences of this amendment

- A pick never names an entity the Viewport does not show.
- **Picking costs slightly more:** one more property read per Renderable per pick (the entity's material). On
  Bistro in Debug the 144-ray grid's mean went from 55.5 to 57.3 ms per pick.
- **The content-side cause is not fixed here:** Bistro's non-uniformly scaled meshes are never drawn. That belongs
  to a later importer Spec, which would bake non-uniform scale into the vertices at glTF import.

## Accepted Amendment — 2026-10-26

**Status of this section:** accepted by Human Review on 2026-10-26 (slmao's rulings on Spec 0067, recorded in
[PR #296](https://github.com/slmao/Atlantis/pull/296) together with [ADR-0134](0134-lit-normal-transform-by-cofactor.md)).
**This section does not itself change this ADR's top-level Status** (`Accepted` above is unchanged). It changes the
wording of the 2026-10-25 amendment's item 1 and nothing else; the Decision, Context, Consequences, Alternatives
Considered and the rest of that amendment are unchanged.

### Human Review — Amendment Acceptance (2026-10-26)

**Deciders:** slmao — ruled Spec 0067 Q5 (P-a: picking uses ADR-0134 D2's predicate) on 2026-10-26, in the review of
[PR #296](https://github.com/slmao/Atlantis/pull/296). No decision above is reopened.

### Amendment

1. **The rule stays; its predicate changes.** The 2026-10-25 amendment's item 1 reads: an entity "the frame's
   extraction skips for a non-conformal world matrix is not pickable", under the conformal test
   (`checkConformalTransform()`).
   - From ADR-0134 the frame skips a lit entity only for a degenerate world matrix (ADR-0134 D2), so the item now reads:
     **the service picks what the frame draws**, and an entity the frame skips under ADR-0134 D2's predicate
     (`checkInvertibleNormalTransform()`) is not pickable.
   - The same predicate, under the same material condition, is called by the frame's extraction, the capture path and
     `pickRenderable()`.
2. **Effect.** A non-conformal but invertible lit entity, such as one of Bistro's 368, is drawn and therefore pickable.
   A degenerate one is neither. `RuntimeControl::raycast()` (ADR-0128 D2) inherits this as before.
