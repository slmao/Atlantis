# Plan 0109 — Viewport Navigation and Selection Framing

- **Spec:** [Spec 0109](../specs/0109-viewport-navigation.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The maintainer approved J1–J4 and Spec/Plan 0109 at [PR #383, `e448c730`](https://github.com/slmao/Atlantis/pull/383) on 2026-11-16, accepted ADR-0165 and authorized S2–S4 after manual design merge `f103f211`.
- **Related ADR:** [ADR-0165](../adr/0165-shared-viewport-navigation-query.md) (Accepted)

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
for visuals. Use the existing worker and retain the generation from its
status and navigation reads. Schedule the discrete acknowledgement and
following service-boundary read in worker order. Share the intent check
with existing coalesced camera writes and tag picks with the camera state
they were calculated against.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — User controls** | Approve one toolbar group, four cardinal views and near-vertical Top/Bottom at the existing ±89° pitch clamp. Use the current perspective projection and 0.5×/1×/2× for orbit and dolly. Keep gesture thresholds and Gizmo priority. |
| **J2 — Shared read and compatibility** | Accept ADR-0165. Approve one additive query for all clients, with 0–64 targets, a same-dispatch camera/bounds snapshot, explicit refusals and legacy behavior on older hosts. Keep the existing shared override ownership. |
| **J3 — Fit and recovery** | Approve live selected-target bounds and origins for non-renderables, quantized-pose validation and refusal for missing geometry or an unrepresentable fit. Approve intent guards and service-boundary settlement of the existing camera setter, with fresh active-camera readback after clear. |
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
  multiple cameras, unknown GUIDs, a missing Renderable versus missing
  mesh geometry, outward rounding and invalid matrices, malformed versus
  duplicate or nil GUIDs, and 64 accepted versus 65 refused. Exercise native
  and C# codec vectors, CLI output/exit behavior, SDK and MCP old-host
  mapping, and an unchanged older client's camera, pick and overlay
  operations against the new host. Exercise malformed replies (including a
  different target or order) and actual disconnects separately.
- [ ] **R1–R4 — Model and Editor worker.** Cover eight-corner bounds under
  parent transform, rotation, negative or nonuniform scale, point-only and
  coincident bounds, large valid coordinates and finite clip/precision
  refusals, including a finite eye where float `eye + forward` loses the
  direction. Check padded fit and clip planes for normal and portrait
  aspects, all six directions, Top/Bottom at ±89°, further orbit and
  `Normal` speed equivalence. Preserve existing camera conformance vectors.
  Cover selection change and same-GUID row replacement, resize or collapse,
  newer Focus/direction/return overtaking an old bounds read or queued
  motion, and a sent older write whose replacement is refused. Cover a
  generation change across the discrete setter and read, an outstanding
  old pick, clear refusal, acknowledged clear with read failure, lost reply,
  and a fresh return after failure. Verify dispatch, the confirmed camera
  state and suppression of stale UI completion and hit tests.
- [ ] **R1–R5 — Windows Editor App at 100%.** Attach the actual Editor to a
  disposable hand-authored scene with two visually distinct, separated
  meshes, an off-origin transform-only node and an active camera whose pose
  and lens differ from the first Camera in enumeration. Save a reference
  copy of the authored source through the ordinary Save path. Focus one
  mesh, the group and the non-renderable alone. Repeat the group fit in a
  narrow window and observe that both meshes fit. Click a recognizable
  off-center mesh in each window size after settlement and compare the
  selected GUID and outline to the clicked object. Exercise all six views,
  Top/Bottom picking on a visible mesh, wheel and drag speed changes, Gizmo
  priority and an Inspector draft across a toolbar action.
  With an override installed, apply a live-only active-camera pose change
  via the existing Runtime World client and settle the command (step when
  paused). Leave the pose stable, return to Scene Camera and verify the new
  view and an off-center pick match the active camera. In a launch with
  pick geometry disabled, verify Focus refusal for a renderable and
  preservation of the prior pose. Compare document
  revision, dirty state, history, camera fields, scene generation and Apply
  state before and after the navigation actions, independently of the test
  setup changes and deliberate picks. Save through the same path and compare
  to the reference; reopen the same source and check the persisted authored
  camera and scene values. Confirm a fresh hosted attachment starts from
  the active scene camera without a persisted navigation pose.
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
