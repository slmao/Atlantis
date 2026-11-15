# Plan 0109 — Viewport Navigation and Selection Framing

- **Spec:** [Spec 0109](../specs/0109-viewport-navigation.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending
- **Related ADR:** [ADR-0165](../adr/0165-shared-viewport-navigation-query.md) (Proposed)

## Objective

Deliver selection framing, perspective direction views, navigation speed and
Scene Camera in the existing embedded Viewport, using shared Runtime reads
and the existing override path.

## Milestones

1. **S1 — Joint design review.** Resolve J1–J4, approve the Spec and Plan,
   accept ADR-0165, and manually merge this design.
2. **S2 — Implementation.** Add and verify the shared read and compatibility
   path. Add fit math and direction poses in the model, then connect controls,
   request guards and clear/read settlement to the existing Editor worker.
3. **S3 — Verification.** Complete the mapped checks on the final candidate
   with attributable Windows 100% and applicable gate evidence. Cite reused
   results and the unchanged inputs that make them applicable.
4. **S4 — Review-ready PR.** Link joint approval, final revision, verification
   and any scope adjustments requiring review. Leave merge to the maintainer.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/runtime_control.h`, `src/runtime/include/atlantis/runtime/{runtime_application,runtime_control_host}.h`, `src/runtime/src/{runtime_application,runtime_control_host}.cpp` | Shared value contract, bounded read on owner thread, active scene camera, current World matrices and retained mesh-local bounds. |
| `src/remote/src/{codec.h,codec.cpp,remote_server.cpp,remote_client.cpp}`, `src/cli/src/{command_layer,invocation}.cpp` | Wire codec and dispatch, old-host mapping and CLI read. |
| `src/csharp/Atlantis.Gameplay/{Control.cs,Remote/Codec.cs,Remote/RemoteSession.cs}`, `src/mcp/Atlantis.Mcp/{ToolSchemas,ToolRegistry}.cs` | Managed typed read and exact read-only tool on the existing session. |
| `src/editor_app/Atlantis.Editor.Model/{Camera,ViewportNavigation}.cs` (`ViewportNavigation.cs` new), `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Fit and canonical poses, worker intent checks, toolbar wiring, camera mode and matching pick/Gizmo projection. Reuse child-window dimensions and pointer forwarding. |
| `tests/runtime/{scene_picking_tests,runtime_control_tests}.cpp`, `tests/remote/{remote_control_tests,remote_codec_tests}.cpp`, `tests/cli/{cli_tree_fixture.h,cli_tree_tests.cpp}`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}` | Live-bound semantics, refusal/generation, old-host behavior, CLI and cross-codec contract. |
| `tests/csharp/Atlantis.Gameplay.Tests/{CodecSuite,Program}.cs`, `src/mcp/tests/Atlantis.Mcp.Tests/M3Tests.cs`, `src/editor_app/tests/Atlantis.Editor.Tests/{ViewportNavigationTests,IntegrationTest,Program}.cs` (`ViewportNavigationTests.cs` new) | SDK and MCP compatibility, model fit and intent tests, and desktop route coverage. |
| `README.md`, `docs/architecture/{remote_protocol,module_boundaries}.md`, `docs/specs/README.md` | Shared invocation, protocol/boundary references and navigation state. |

The Scene Asset System, World, Authoring, Window, Platform, Renderer,
RenderGraph and Vulkan backend are existing integration inputs.

## Sequencing and dependencies

Design baseline is `26d5cee6`, the [manual merge of PR #382](https://github.com/slmao/Atlantis/pull/382).
Start S2 from current main after the approved design merges. Reuse the
camera and child-window path established by [ADR-0128](../adr/0128-remote-viewport-camera-and-raycast.md),
camera vector conventions from [ADR-0137](../adr/0137-csharp-model-port-held-by-vectors.md),
selection and routing from [0106](../specs/0106-scene-multiselect-batch-edit.md)
and [0107](../specs/0107-multi-object-gizmo.md), and [0093](../specs/0093-editor-visual-layout.md)
for visuals. Use existing Editor worker serialization and observed scene
generation; never let an old navigation read queue a camera command after a
superseding UI intent or a newer generation observed by the worker.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — User controls** | Approve one toolbar group, six exact perspective directions, and 0.5×/1×/2× for existing orbit and dolly. Keep gestures, thresholds and Gizmo routing. |
| **J2 — Shared read and compatibility** | Accept ADR-0165. Approve one additive camera-and-bounds query for all clients, 0–64 targets, legacy behavior on older hosts and no change to camera override ownership. |
| **J3 — Fit and recovery** | Approve live selected-target bounds plus origins for transform-only nodes, conservative fit against the existing lens, and whole-action refusal for missing data or an unrepresentable fit. Approve scene-camera clear/read settlement and invalidation on stale selection or observed scene change with two separate shared operations. |
| **J4 — Evidence and authorization** | Approve Windows 100% actual-window acceptance, mixed-version checks and the applicable repository gates. After manual design merge, authorize S2–S4 together. |

Suggested authorization for the reviewed revision

> I approve J1–J4, Spec/Plan 0109 and ADR-0165. After manual merge of the
> design, proceed from latest main through S2–S4, including implementation,
> scoped fixes, applicable verification, valid evidence reuse and a
> review-ready PR. Keep Windows 100% actual-window acceptance and the
> established gates. The maintainer performs final merge.

## Verification checklist

- [ ] **R2, R5 — Shared snapshot and compatibility.** Check camera-only
  reads with retained geometry disabled, ordered mixed mesh/origin results
  and one generation while paused and after scene replacement. Verify that a
  runtime-only World transform contributes the displayed position. Cover
  multiple cameras, unknown GUIDs, absent meshes, invalid matrices, duplicate
  and nil GUIDs, and 64 accepted versus 65 refused. Exercise native and C#
  codec vectors, CLI output/exit behavior, SDK and MCP old-host mapping, and
  an unchanged older client's camera, pick and overlay operations against the
  new host. Exercise malformed replies and actual disconnects separately.
- [ ] **R1–R4 — Model and Editor worker.** Cover eight-corner bounds under
  parent transform, rotation, negative or nonuniform scale, point-only and
  coincident bounds, large valid coordinates and finite clip/precision
  refusals. Check padded fit and clip planes across normal and portrait
  aspects, six directions and pole orbit handoff. Preserve existing camera
  conformance vectors. Cover same-GUID row replacement, selection changes,
  resize or collapse while pending, a second Focus, explicit return and
  newer orbit overtaking an old callback, observed projection generation
  change, clear failure, successful clear followed by read failure, and
  disconnect. Verify dispatch guards and absence of stale accepted status.
- [ ] **R1–R5 — Windows Editor App at 100%.** Attach the actual Editor to a
  disposable scene with two separated renderables, a transform-only node,
  a camera rolled relative to world up, and an active camera that is not
  first in normal enumeration. Focus one mesh, the group, and the origin
  alone. Repeat the group fit in a narrow window, check the edge of the
  fitted objects against the physical viewport and click a mesh to verify
  outline and hit alignment. Exercise six views, speed changes, wheel and
  drag, Gizmo priority and an Inspector draft across a toolbar action.
  While an override is installed, change the live active camera pose
  through the existing shared controls; return to Scene Camera and check
  rendered view and picking against the new pose. Exercise the geometry
  unavailable refusal and confirm the previous camera stays usable. Verify
  camera actions preserve selection, document values/revision, dirty state,
  history, scene camera data (apart from the explicit test edit), generation
  and Apply state; save/reopen the unchanged source to confirm it contains
  no Editor camera state.
- [ ] **Existing gates.** Build and test affected native and managed targets
  and exercise Runtime and Editor integration. Retain applicable Vulkan
  Validation Layers and golden checks. Assess Android build and packaging
  for the touched native modules, with Windows as the hosted Editor check.
  Run `atlantis_runtime --gate --applies` against the complete final diff
  and use the established protocol and baseline if selected. S1 needs
  document verification.

## Rollback Plan

Revert the controls and additive read in a follow-up implementation PR if
needed. The existing camera setter, authoring documents and scene sources
continue to work with the previous implementation.

## Definition of Done

Follow [the repository definition of done](../process/definition-of-done.md).
Complete S3 and link the final candidate evidence in S4.
