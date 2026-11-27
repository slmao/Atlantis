# Plan 0147 — Scene View Bookmarks and Reference Comparison V1

- **Spec:** [Spec 0147](../specs/0147-scene-view-bookmarks.md)
- **ADR:** [ADR-0193](../adr/0193-bounded-render-look-capture-extent.md) (Accepted)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer approved Spec/Plan 0147, J1–J4 and ADR-0193 at 517e597c and authorized S2–S4 after merge. [Approval record](https://github.com/slmao/Atlantis/pull/457#issuecomment-7218716662). The maintainer merged #457 as 4f6960fa.

## Objective and milestones

Deliver named, repeatable compositions for a Windows Editor manual scene,
with ordinary capture, a local reference and existing Render Looks at the
same saved pose and extent.

1. **S1 — Joint review.** Decide J1–J4, record the approved design revision
   and integrate the maintainer's design merge before implementation.
2. **S2 — Bookmarks and reference workflow.** Add a typed, validated
   observation and local store. Reuse 0131's fixed-drawable settle, camera
   routing, frame checks and aspect-preserving comparison. Keep the built-in
   entry points and verify document, draft and input isolation.
3. **S3 — Shared A/B extent.** Add the bounded optional value and its
   capability through the shared clients and Runtime. Render both variants
   from the same stored observation, reuse the 0146 pool and scratch lifetime,
   and retain the prior default request and response shapes.
4. **S4 — Verification and PR.** Run the mapped focused tests and Windows
   acceptance, then final applicable gates. Attach command, revision, raw
   report, screenshot and resource-budget evidence to the implementation PR.

S2's view and extent value feeds the Editor side of S3. Shared admission
and client conformance can be validated independently of the window flow.

## Expected files and module scope

| Files / modules | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/` view-bookmark values, `BistroReferenceViews.cs` and `ViewportNavigation.cs` | Pose/lens validation, extent and correlation with expected matrices. Reuse the existing camera convention. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession,MainWindow.RenderLooks,BistroReferenceWindow,ViewportHost,RenderLookStore}.cs` and bookmark store/window files | Local storage, fixed viewport, draft and input protection, capture/ref comparison and A/B selection. Reuse canonical scope and atomic file helpers where applicable. |
| `src/connection/include/atlantis/connection/runtime_control.h`, `src/remote/`, `src/cli/`, `src/gameplay_sdk/`, `src/csharp/`, `src/mcp/`, `docs/architecture/remote_protocol.md` | Shared request and capability, backwards compatibility and result validation. Extend existing surfaces and conformance vectors. |
| `src/runtime/src/{frame_capture,runtime_control_host}.cpp` and executable-private control wiring | Admission before cook/allocation, bounded scratch sizing and requested pair extents. Retain existing pipeline budgets, settlement and renderer ownership. |
| `src/editor_app/tests/Atlantis.Editor.Tests/`, existing Runtime, Remote, CLI, SDK, MCP, conformance and image-regression fixtures | Targeted tests for R1–R4 and Vulkan capture evidence. |

## Joint-review decisions

| Item | Recommendation |
| --- | --- |
| **J1 — Ownership and persistence** | Approve the per-user, canonical-source + scene-GUID store. Keep 16 local bookmarks per scope, and keep manually selected references session-local. |
| **J2 — Exact observation** | Approve capture from the settled hosted drawable, physical pixel extent, exact restore with no size substitution, and the draft, focus and input guards. |
| **J3 — Compatibility and resources** | Approve ADR-0193, one optional extent on the existing A/B request, the 2,073,600-pixel envelope and the original 960 × 540 default. |
| **J4 — Acceptance and authorization** | Approve the Windows 100% non-built-in scene matrix and applicable gates. Once the design is jointly approved and merged by the maintainer, one authorization can cover S2–S4, scoped fixes, verification and a reviewable implementation PR. |

## Verification checklist

- [ ] **R1 — Storage and invalidation.** Round-trip exact typed components
  and physical extents. Cover trimmed names, case-only collision, explicit
  replacement, limits, stale two-writer reads, native replacement refusal,
  malformed/unsupported versions and unreadable source. Check saved scene
  reopen, Save As, canonical aliases, changed scene GUID and moved source.
  Ensure failed or unexpected data preserves the original file and makes no
  authoring or workspace-layout write.
- [ ] **R2–R3 — Settle and local comparison.** Exercise exact physical extent
  reuse, window/DPI changes, fit refusal, fallback navigation, focus and draft
  protection, and picks/Gizmo only after both camera and drawable settle.
  Cover late camera, capture, file picker and decode replies across bookmark,
  document, viewport and host changes. Check refusal or missing/invalid/large
  PNGs, unknown capture receipts, no automatic retry and independent
  aspect-preserving presentation. Verify the existing ORCA fixed views.
- [ ] **R4 — Shared clients and allocation.** Check omitted/default/custom
  forms, malformed and over-budget requests before cook/allocation, capability
  absence with new and old clients, exact echo paths and both image sizes.
  Test a non-16:9 pair, the maximum allocation case and overflow/encoded-size
  refusal. Verify clean Vulkan Validation Layers with Bloom on and off,
  serial reuse and safe failure settlement under the 0146 budget. Report
  actual peak descriptor and explicit scratch allocations. An ordinary
  capture from a second client must retain the live view and settings.
- [ ] **Windows 100% — Create, reopen and return.** Use an independent
  non-built-in hand-authored scene with reproducible geometry, three stable
  landmarks per view and two saved 0145 looks with visibly different light
  or post-processing parameters. Save one distant and one close bookmark
  from two settled viewport sizes, including a non-16:9 extent. Suggested
  fixture extents are 960 × 540 and 800 × 600. Retain first capture data,
  navigate away, reopen the same source and explicitly return to each view.
  Check physical extent, PNG dimensions, each matrix element within `0.0001`
  and all three repeated landmark projections within one physical pixel.
  Verify pick alignment, too-small viewport refusal and retry after enlarge.
- [ ] **Windows 100% — Compare and preserve authoring.** Choose a local PNG
  at a different aspect ratio and record the uncropped, proportional pair.
  Select each named view in Render Looks, capture both appearances and switch
  A → B → A → B. Check exact extent, the same matrix/landmark tolerances,
  visible appearance difference and the same settled projection basis.
  Confirm authored camera identity and values, source bytes, document
  revision, selection, Apply receipt and history do not change. Attempt the
  operation with an active Inspector draft and verify its text and focus are
  preserved with zero implicit writes. Save native screenshots, original PNGs
  and raw frame reports.
- [ ] **Final gates.** Run affected Editor, native Runtime and shared client
  checks, Vulkan, applicable headless/image regression and golden checks.
  Run final performance applicability and the existing selected protocol.
  Apply the current Android rules to shared native changes. Reuse prior
  evidence only where its revision, inputs and environment remain valid.

## Rollback and definition of done

Reverting withdraws the local bookmark UI and the optional extent
capability. Existing scene sources and 0131/0145 data remain usable.
Apply the repository [definition of done](../process/definition-of-done.md).
