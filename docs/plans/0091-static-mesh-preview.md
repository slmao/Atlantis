# Plan: Read-Only Static Mesh Preview in Assets

- **Spec:** [Spec 0091](../specs/0091-static-mesh-preview.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending; implementation is not authorized
- **ADR:** [ADR-0151](../adr/0151-shared-offscreen-mesh-preview.md) (Proposed)

## Objective

Deliver one fixed geometry still from the serving Runtime's cooked catalog,
using the shared clients and an isolated, disposable rendering context.

## Milestones / Task Breakdown

1. **S1 — Joint design review.** Settle J1–J4 and record acceptance before
   the human design merge.
2. **S2 — Runtime and shared clients.** Implement bounded cook reads and
   admission, framing, the offscreen render/readback path, and the shared
   reply across Connection, Remote, CLI, both SDKs and MCP. Verify limits,
   compatibility, and isolated resource ownership before integration.
3. **S3 — Assets integration.** Add explicit Preview geometry, diagnostics,
   retry and stale-result guards in the ordinary Windows Editor. Exercise
   the production render path in the focused headless and Windows flows.
4. **S4 — Verification and implementation PR.** Complete the checklist on
   the final candidate. Cite provenance for reuse, preserve gate verdicts,
   and submit a review-ready PR for human merge.

## Files / Modules Touched (expected)

- Asset System `src/asset_system/include/atlantis/asset_system/load.h` and
  `src/asset_system/src/load.cpp`: reuse the mesh decoder in a bounded load
  that checks the expected record against the bytes read. Preserve the
  existing scene-load call and behavior.
- Runtime `src/runtime/{asset_host.h,asset_host.cpp,main.cpp,CMakeLists.txt}`
  for the executable dispatch boundary and host composition;
  `src/runtime/include/atlantis/runtime/runtime_application.h` and
  `src/runtime/src/{mesh_preview.h,mesh_preview.cpp,frame_capture.h,frame_capture.cpp}`
  for an internal RuntimeHost entry, request resources, camera setup and
  shared PNG encoding. Reuse existing shader binaries, reflection layouts
  and the standard RenderGraph readback pattern. No changes planned to
  `runFrame()`, Renderer, RenderGraph, RHI, Vulkan Backend, or shaders.
- Shared interfaces: `src/connection/include/atlantis/connection/asset_connection.h`
  and `src/connection/src/asset_connection.cpp` for typed values and bounded
  native wire/text encoding shared by CLI and Remote;
  `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}`;
  `src/cli/src/{command_layer.cpp,invocation.cpp}`;
  `src/gameplay_sdk/include/atlantis/gameplay/assets.h`;
  `src/csharp/Atlantis.Gameplay/{Assets.cs,Remote/AssetCodec.cs,Remote/AssetSession.cs}`;
  `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolSchemas.cs,IAtlantisSession.cs}`.
  No generated World or authoring schemas, package allow-lists or
  transport-limit changes.
- Editor `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs}`
  plus a small preview worker if needed, and UI-free selection/intent state
  in `src/editor_app/Atlantis.Editor.Model/AssetBrowser.cs`. Leave the
  reference picker and authoring, recovery and history code paths unchanged.
- Focused tests and target registration under `tests/{asset_system,runtime,remote,cli,gameplay_sdk,csharp,image_regression}/`,
  `src/mcp/tests/`, and `src/editor_app/tests/`. Add one preview golden and
  sidecar with the existing generator and comparator infrastructure under
  `tests/image_regression/`. Update `README.md`,
  `docs/architecture/{module_boundaries.md,remote_protocol.md}` and the spec
  registry to describe the delivered surface.

## Sequencing & Dependencies

Start S2 from current `main` after joint approval, ADR acceptance and
manual design merge. Uses [0081](../specs/0081-asset-browser-shared-interface.md)
for the catalog, [0085](../specs/0085-shared-asset-rebuild.md) for generation
activation, and [0055](../specs/0055-agent-runtime-cli.md) for the capture
building blocks. Does not depend on a change to scene creation, build/open
or authoring apply.

| Decision | Recommendation |
| --- | --- |
| **J1 — Contract and input identity** | Approve one additive shared GUID → bounded PNG call; use the existing catalog snapshot, check cooked identity and support both index widths. Keep both version directions working (R1). |
| **J2 — Framing and look** | Approve one fixed oblique 256 × 256 still, automatic fit, existing unlit vertex colors and no scene lights or authored material fidelity (R2). |
| **J3 — Isolation, bounds and settlement** | Approve request-owned GPU state, one synchronous admission per existing service interval, bounded input and explicit Busy/retry. Approve a separate ordinary SDK connection and latest-intent guards in the Editor (R3/R4). |
| **J4 — Verification and delivery** | Approve the focused contract/Windows/headless checks, one reviewed golden and the existing performance gate. After human design merge, authorize one S2–S4 pass through a review-ready implementation PR. |

Suggested authorization: approve J1–J4, Spec/Plan 0091 and ADR-0151 at the
reviewed revision, then authorize implementation, scoped fixes, applicable
verification and valid evidence reuse through the implementation PR. Final
merge remains human.

## Verification Checklist

- [ ] **R1 — Shared contract and catalog.** Test old client → new Runtime
  and new client → older asset-capable Runtime with a successful list/get
  on the same session. Cover strict request shape, no provider, catalog unavailable,
  unknown GUID, wrong type, both index widths and a replaced valid but
  mismatched artifact/sidecar pair. Cross-check the same GUID, dimensions and
  PNG through CLI structured output, native SDK, typed C# SDK and one MCP image
  block. Reject mismatched GUID, invalid image header/dimensions, malformed
  or oversize base64, and distinguish protocol/transport failures from
  refusals. Preview before list initializes the same snapshot; apply,
  Reload view, reconnect and a published generation do not change it until
  restart into the new generation.
- [ ] **R2 — Correct image.** Offline tests cover centered/off-origin, planar,
  differently scaled, and unusable geometry; check the conservative fit and
  positive near/far planes. With Vulkan validation, run the same production
  preview render helper on the headless/offscreen test device with the cooked
  `minimal_cube`; compare one new golden. Use `textured_quad_left` or an
  equivalent cooked off-origin mesh for a distinct framed image. Capture
  the new golden on the reference environment with a provenance sidecar
  under [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
  Human review the new pixels as a rendering change. Run affected existing
  goldens unchanged, or cite valid evidence where the inputs remain identical.
- [ ] **R3 — Admission and release.** Verify the inclusive file, index and
  image bounds, and that extra same-interval requests across clients and
  local dispatch get `Busy` before file/GPU work. Check next-interval retry,
  repeated successes, recoverable load/render failure and client disconnect
  through normal shutdown. Verify request-owned resources return to their
  pre-request count after settlement, and that idle use adds no GPU resources
  or submission. Use narrow fixture/test seams and clean Vulkan Validation
  Layers (zero warnings/errors); no new diagnostics framework.
- [ ] **R3/R4 — Windows Editor.** In one ordinary live session, leave an
  unsaved edit with undo/redo history and a known paused Runtime state.
  Preview a catalog mesh the scene has never loaded. Compare document
  revision/dirtiness/history, World and scene generation, viewport camera
  and overlay, pause/step state and a pending apply before and after preview.
  Check that preview requests no step and settles no apply; ordinary frames
  may continue while commands are held.
  Resume via the existing flow and verify editing, undo/redo and apply
  still work. Force a recoverable preview failure, verify its message and
  retry the same GUID. Model/UI tests control ordering for A → B and
  A → B → A, late success and failure, deselection/hidden row, Reload view,
  tab exit, reconnect, bitmap decode failure and transport loss. Verify
  discarded/replaced bitmaps are released and the preview reconnect does not
  reset the editing session.
- [ ] **Applicable gates.** Build focused Windows native and managed targets,
  run affected unit, protocol/conformance, Editor, headless/image and GPU
  checks, and verify docs. Run the full implementation diff through
  `atlantis_runtime --gate --applies`: planned `src/runtime/**` changes are
  selected even when preview is idle. Run the existing reference-machine
  Release profiling-on/Tracy-off `--gate --json` protocol and retain the
  raw verdict. No new preview timing threshold, baseline or policy edit.
  Keep #340's historic record parked. Evidence reuse follows AGENTS.md;
  an unresolved required gate is not a pass.

## Rollback Plan

Remove the additive operation and the Assets action. No saved state, asset
artifacts or protocol migration needs reversal; updated clients see the
same `Unsupported` fallback against a Runtime without the operation.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR maps evidence to R1–R4 and preserves raw applicable gate
results and human golden review. Joint design approval and final merge
remain human.
