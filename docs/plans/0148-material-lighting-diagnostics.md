# Plan 0148 — Material and Lighting Diagnostics V1

- **Spec:** [Spec 0148](../specs/0148-material-lighting-diagnostics.md)
- **ADR:** [ADR-0194](../adr/0194-isolated-base-color-diagnostic-capture.md) (Accepted)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** [Maintainer joint approval of 5144fe7a](https://github.com/slmao/Atlantis/pull/459#issuecomment-7223673579) on 2026-11-28 approves Spec/Plan 0148, J1–J4 and S2–S4. Design merged by the maintainer as b1f14f08.

## Objective

Compare the normally shaded scene with the effective base color of one
verified opaque material binding at a saved viewpoint.

## Milestones and sequencing

1. **S1 — Design review.** Settle J1–J4 and ADR-0194 jointly. Start the
   following milestones after approval and a maintainer merge of the design.
2. **S2 — Shared contract and capture.** Add the request, capability,
   typed results and client conformance. Resolve and freeze the reviewed
   snapshot at Runtime admission. Reuse normal rendering and the isolated
   pair path. Add the capture-only diagnostic shader and RenderGraph pass
   and validate against known color and coverage inputs.
3. **S3 — Editor workflow.** Add the comparison action to the verified
   material result at a settled bookmark. Wire intent, draft and stale
   result checks. Display the paired PNGs and provenance with equal
   scale and release the results on retirement.
4. **S4 — Verification and PR.** Collect Windows and shared-client evidence.
   Run applicable build, Vulkan, image and formal performance gates.
   Submit a reviewable implementation PR with the final candidate and
   resolved deviations for maintainer merge.

S2 depends on the merged 0138 binding and 0145–0147 capture and bookmark
contracts. Contract tests and fixture preparation can proceed independently
of the capture implementation. S3 consumes the shared pair from S2.

## Expected files and module scope

| Files / modules | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/runtime_control.h`, `src/remote/`, `src/cli/`, `src/gameplay_sdk/`, `src/csharp/`, `src/mcp/`, `docs/architecture/remote_protocol.md` | Additive control and exact client bindings. Reuse camera, extent, output path and failure conventions. |
| `src/runtime/main.cpp`, `src/runtime/src/{runtime_control_host,frame_capture,runtime_application}.cpp`, existing corresponding Runtime headers and the capture resource definitions | Validate and freeze the applied projection and material binding. Render the pair, reuse admission, readback, storage and lifetime machinery. |
| `src/renderer/{include,src}/`, new capture-only Slang shader pair under `shaders/`, root and shader/Runtime CMake and shader reflection/copy wiring | Record the small base-color pass and register its shader with the existing toolchain. Retain the normal path and RHI boundary. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession,ViewBookmarkWindow}.cs`, new comparison window if needed, existing Editor Model and tests | Reuse 0138 target and 0147 observation guards. Show equal scale, provenance, diagnostics and release behavior. |
| Existing Remote, CLI, SDK, MCP, Runtime, shader and headless/image-regression suites, with a small controlled fixture | Cover color math, masks, admission, lifetime, capability, outputs and paired UI acceptance. |

## Joint review decisions

| Item | Recommendation |
| --- | --- |
| **J1 — Scope and display** | Approve the 0138 opaque `PbrDirectLit` binding and 0147 bookmark entry. Normal shows the entire scene. The diagnostic shows only that Renderable with a neutral comparison background and an explicit isolation label. |
| **J2 — Color and appearance** | Approve sampled linear RGB times the authored RGB factor, followed by a single sRGB display transfer. Approve conventional post processing for normal and no post processing for the diagnostic. |
| **J3 — Contract and isolation** | Accept ADR-0194. Both images come from one settled snapshot with shared clients, guarded delivery and capture-owned GPU work. |
| **J4 — Acceptance and authorization** | Approve the measurable Bistro checks below and applicable gates. After a joint sign-off and design merge, one authorization covers S2–S4, ordinary in-scope repairs, verification and a reviewable PR. |

## Verification checklist

- [ ] **R1/R3 — Identity and lifecycle.** Cover old host and old client
  behavior, omitted/invalid capability, strict request shape, extreme and
  invalid extents, same or existing outputs, stale document/Apply/viewport,
  changed bindings, unsupported or ambiguous hits and stale UI replies.
  Ensure a delayed capture on an old projection cannot display after the
  Worker observes a newer one. Verify draft/focus, selection, source,
  document, active camera, Undo/Redo and Apply stay stable during comparison.
  Exercise CLI, native and managed SDKs and MCP, with exact paths, dimensions
  and provenance in both results. Preserve the unknown-result diagnostic
  and check that a partial or lost result is never automatically retried.
- [ ] **R2 — Headless color and coverage.** Render a small fixture with known
  sRGB texels, fixed UVs and distinct channel factors. Compare interior
  covered pixels against the analytic linear product and sRGB transfer
  within one byte per channel. Verify factor-only white, missing input
  refusal, outside alpha 0, covered alpha 255 and self-occlusion. Vary
  light, shadow, exposure, fog and Bloom with fixed inputs and prove the
  diagnostic decoded pixels are unchanged. Include a non-16:9 capture and
  maximum extent with a Bloom-on normal image.
- [ ] **R2 — Windows 100% Bistro comparison.** Use a disposable hand-authored
  Bistro copy and a saved 960 × 540 bookmark. Identify a stone paving seam
  or wooden tabletop that 0138 can locate and whose normal shading responds
  to an existing light. Before recording a baseline, verify the material is
  editable through 0094. If necessary, use 0138 and 0094 to duplicate a
  supported material into a writable asset and bind the copy with the same
  base-color input. Complete publication, activation and Apply during fixture
  setup. Record a covered interior pixel region around the feature before
  either trial. Capture the baseline pair, then change
  only one existing light's intensity through the Editor and settle Apply.
  Return to the bookmark and re-pick. The base-color decoded RGBA must be
  identical and at least 5% of covered pixels in the recorded region must
  change by at least three RGB byte levels in one channel in normal shading.
  Keep geometry, camera lens, environment, asset inputs and post processing
  fixed. Match report matrices within `0.0001` per element and physical
  extents exactly. Retain both pairs and the source/input comparison.
- [ ] **R2 — Change factor alone.** Save the settled lighting-only stage of
  the disposable scene. Hold that source and light state fixed, change one
  RGB base-color factor channel, and use the existing edit, publish and
  explicit reactivation path. Verify the saved light values after reopening.
  Return to the same bookmark and re-pick the same entity, mesh and
  material GUID. Check unchanged cooked texture bytes, unchanged coverage
  and unchanged unaffected channels within one byte. Show that the changed
  channel follows the new linear factor at the recorded interior samples
  within two bytes for nonsaturated samples, choosing a factor decrease to
  avoid clipping. Preserve the raw PNGs, matrices, light and factor readback,
  and native comparison screenshots. Record whether the seam or grain is
  visible in the input as a diagnostic observation. Similarity to ORCA is
  outside this acceptance criterion.
- [ ] **R3 — Isolation and final gates.** Use a second client to compare
  ordinary captures and state before and after the pair on a quiet fixture.
  Record descriptor and explicit scratch peaks and validate release after
  GPU completion and refusal on failed admission/record/readback/publication.
  Run clean Vulkan validation, applicable headless/image and golden tests,
  affected builds/suites, Android rules for shared native inputs, and
  performance applicability and the selected existing protocol. Preserve
  the baseline and report any remaining applicable gate to the maintainer.

## Rollback and definition of done

Revert the optional capability, diagnostic capture and Editor action together.
Existing bookmark files, look presets, authored materials and scenes retain
their formats. Apply the repository [definition of done](../process/definition-of-done.md).
