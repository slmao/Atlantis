# Plan 0149 — Directional Shadow Diagnostics V1

- **Spec:** [Spec 0149](../specs/0149-directional-shadow-diagnostics.md)
- **ADR:** [ADR-0195](../adr/0195-isolated-directional-shadow-diagnostic.md) (Proposed)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending.

## Objective

Provide one on-demand comparison of current applied directional shadows
using a saved observation and the isolated normal-frame path.

## Milestones and sequencing

1. **S1 — Joint design review.** Resolve J1–J4 and accept ADR-0195.
2. **S2 — Shared operation and Runtime.** Add the optional capability,
   typed pair and matching client surfaces. Admit one verified applied basis,
   exercise the existing normal capture helper with the two caster spans, and
   assert private clear and completion behavior.
3. **S3 — Bookmark action.** Add the guarded comparison action, consume
   correlated results, and reuse the existing paired-image display and
   temporary-output lifetime.
4. **S4 — Verification and implementation PR.** Collect controlled, Windows
   and shared-client evidence, run applicable gates on the final candidate,
   and submit for maintainer review.

S2–S4 start after joint approval and maintainer merge of this design.
S2 builds on 0145–0148. S3 consumes the shared contract from S2. Fixture
preparation and codec/UI tests can proceed independently within the approved
implementation scope.

## Expected files and module scope

| Files / modules | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/runtime_control.h`, `src/remote/`, `src/cli/`, `src/gameplay_sdk/`, `src/csharp/`, `src/mcp/`, `docs/architecture/remote_protocol.md` | Add the bounded pair and capability through the existing shared control path, with exact request/result validation, category-preserving errors and compatibility checks. |
| `src/runtime/main.cpp`, `src/runtime/src/{runtime_control_host,frame_capture}.cpp`, `src/runtime/src/frame_capture.h` and their corresponding Runtime headers | Capture one frozen basis, feed both caster spans to the existing isolated helper, and correlate reports and output completion. |
| `src/editor_app/Atlantis.Editor/{ViewBookmarkWindow,EditorSession,MaterialDiagnosticWindow}.cs` and existing paired-image UI/Editor Model | Add the bookmark action and reuse the draft, intent, expiry, image and comparison mechanisms. |
| Existing Remote, CLI, SDK, MCP, Runtime, Editor and image-regression suites and controlled scene fixture | Cover the boundaries and measurable pixel behavior below. |

## Joint review decisions

| Item | Recommendation |
| --- | --- |
| **J1 — Interaction and observation** | Offer the diagnostic from a settled bookmark with current applied authoring, default hosted visibility and no unsubmitted gesture or draft. |
| **J2 — Shadow control** | Keep the same directional light, receiving shaders and output chain. Clear the capture-owned shadow depth through the existing pass and supply an empty caster span only for the control image. |
| **J3 — Shared contract and lifetime** | Accept ADR-0195. Use one capability and one pair request across all clients, with capture-owned bounds, private mutable resources and strict stale-result handling. |
| **J4 — Evidence and authorization** | Approve the controlled and Bistro checks below. A single explicit joint approval may authorize implementation, in-scope repairs, applicable verification and a review-ready PR after maintainer merge of this design. |

## Verification checklist

- [ ] **R1/R3 — Request and delivery.** Exercise shared-client round trips
  and older hosts/clients, exact capability and request decoding, extent and
  output collisions, current token/Apply/generation and hosted visibility
  guards, absent directional light, and a zero-contribution light. Refuse
  starts that would commit or discard a pending draft. Hold a successful
  old result until the Worker observes a newer projection and then deliver
  it; it must not display, and a fresh return and capture must work.
  Exercise lost receipt with the existing transport error category, check
  both paths guidance and zero automatic resends.
- [ ] **R2 — Controlled receiver.** Run a headless image test and repeat
  in the hosted Windows Editor at 100% DPI with one opaque occluder, a
  matte PBR plane and an oblique directional light. Keep the camera,
  geometry and lighting settings fixed. Use fixed exposure, fog and Bloom
  disabled for this fixture, and an unsaturated receiver. Record two
  masks of at least 32 interior plane pixels before reading differences.
  Place one inside the projected shadow away from the footprint of the
  visible occluder, and one on the unshadowed part of the same plane. In
  the unshadowed image, at least 90% of the shadow mask must increase by
  at least 8 levels in one RGB channel, with no decrease greater than 1
  level in any channel there. The unshadowed control mask must match
  decoded RGBA byte for byte. Both masks must be on exposed plane geometry
  in both images. Verify the same nonzero directional-light data, exact
  requested dimensions and matrices within `0.0001` per element. Retain
  masks, PNGs and reports. Repeat the pair and check that the normal image
  gets its shadow back.
- [ ] **R2 — Bistro Windows 100%.** Use an editable Bistro copy with a
  contributing sun and the [0131](../specs/0131-bistro-reference-views.md)
  Storefront Terrace camera saved as a 0147 bookmark at 960 × 540 physical
  pixels. Settle the fixture before the trial and keep all parameters fixed
  across captures, including indirect lighting and camera post effects.
  Record the two PNGs, native comparison screenshot, view/projection
  matrices and matching directional-light and exposure readback. Before
  inspecting the pair, mark a chair/table contact patch, a patch under the
  awning, and an open reference patch. Report the changed-pixel count,
  median signed RGB change and visible differences in each region.
  Interpret differences as the effect of the current
  directional shadow map, potentially carried by post processing. Treat
  residual darkness, unchanged regions and differences from visual
  references as unattributed. No minimum Bistro image delta is required.
- [ ] **R3 — Isolation and final gates.** Interleave ordinary captures and
  state queries from a second client before and after the pair on a stable
  scene. Compare decoded image bytes and document, camera, viewport and
  Apply state. Confirm draft, selection, saved bytes and Undo/Redo
  preservation in the Editor. At 1920 × 1080 with Bloom enabled, record
  peak descriptor use and explicit scratch, confirm no ordinary descriptor
  operations, and verify the second shadow clear and retirement across
  success, output failure and failed GPU completion. Preserve the existing
  bounds and run affected Windows builds/suites, clean Vulkan validation,
  applicable headless and unchanged golden checks, Android build/package
  where shared native inputs select them, and performance applicability and
  the selected existing protocol. Reuse only evidence from unchanged inputs.

## Rollback and definition of done

Revert the optional operation and bookmark action together. Reuse the
existing app, Runtime and file formats. Complete the repository
[definition of done](../process/definition-of-done.md) and hand off to the
maintainer for manual merge.
