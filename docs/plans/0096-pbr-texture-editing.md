# Plan: PBR Normal and Emissive Texture Editing

- **Spec:** [Spec 0096](../specs/0096-pbr-texture-editing.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The Atlantis maintainer approved J1–J4 and Spec/Plan 0096 on 2026-11-14 at `610591f7`. [PR #357](https://github.com/slmao/Atlantis/pull/357) was manually merged as `6e531d06`. Authorization covers S2–S4, ordinary in-scope fixes, applicable verification, valid evidence reuse and a review-ready PR. Windows DPI acceptance uses actual 100%. Final merge remains human.
- **ADR:** [ADR-0155](../adr/0155-pbr-material-map-authoring.md) (Accepted)

## Objective

Deliver selection, replacement, clear, compare, save and reopen for the
normal and emissive slots already consumed by opaque direct-lit PBR.

## Milestones / Task Breakdown

1. **S1. Joint review.** Approve the Spec and Plan together, accept the ADR
   and record J1–J4. Merge the design PR by hand.
2. **S2. Shared authoring and settlement.** Extend the internal recipe and
   bounded dependency checks. Add the three closed requests and typed client
   bindings. Route both forms through the same worker with legacy subset
   guards. Exercise exact dependency deltas, version compatibility and
   recovery before wiring the UI.
3. **S3. Pair preview and Editor.** Bind the existing normal and emissive
   resources in the offscreen pair path. Extend the material modal and wire
   draft and cooked previews through the existing admission and late-result
   guards. Prove the save, activation and independent scene reopen flow.
4. **S4. Verification and implementation PR.** Run the mapped checks on the
   final candidate. Report any file-list adjustments, reused evidence and
   remaining reviewer actions.

## Files / Modules Touched (expected)

- Connection asset values and codec, Remote asset provider/client, CLI
  asset commands, native Gameplay SDK asset facade, managed Gameplay SDK
  asset values and codec, and MCP asset registry/schema.
- Executable-private Runtime `asset_host.*`, `material_authoring.h`,
  `asset_rebuild_host.*` and preview callback wiring in `main.cpp`.
  RuntimeHost preview declarations/implementation and material realization,
  including the bounded upload helper. Asset System texture preview loader
  and loader header only as needed to reuse bounds for multiple roles.
- Editor App material modal, `MainWindow`, `EditorSession`, and material
  draft, preview and settlement models. Reuse 0093 styles and the existing
  AssetBrowser. Runtime and test build declarations only for linking current
  preview variants and focused fixtures.
- Focused Asset System, Runtime, Remote, CLI, Gameplay SDK, C#, MCP,
  Editor and image-regression fixtures. Update usage and as-built protocol
  and module docs plus Spec navigation. Reuse existing approved texture and
  mesh fixtures where representative.

## Sequencing & Dependencies

Build on merged [0095](../specs/0095-texture-import-and-base-color-binding.md),
[0094](../specs/0094-basic-material-authoring.md) and
[0092](../specs/0092-mesh-material-preview.md). Implement the complete recipe
and settlement semantics before relying on them in the modal. Reuse
role validation across draft, staged and activated paths. Preserve the
distinct cooked-preview and authoring eligibility checks.

| Decision | Recommendation |
| --- | --- |
| **J1. Slots and assets** | Approve normal and emissive only, including the existing emissive RGB factor. Use catalog UNORM normal assets and existing sRGB color assets. Keep metallic and roughness scalar. |
| **J2. Shared contract** | Approve three additive closed methods, one material history and worker, legacy subset guards, and one unchanged cooked-pair wire shape. |
| **J3. Preview and publication** | Approve the existing direct-lit variants in request-local preview, the three-texture bound, per-slot validation and dependency deduplication. Keep the current source commit, Resume, and explicit generation activation model. |
| **J4. Acceptance and delivery** | Approve the Windows 100% workflow, independent saved-scene reopen, cross-version matrix, and applicable Vulkan, golden and performance gates. |

Suggested authorization after joint approval and manual design merge:
continue from current `main` through S2–S4, including ordinary fixes within
scope, applicable checks, valid evidence reuse and a review-ready PR. Final
merge remains human.

## Verification Checklist

- [ ] **R1. Recipe and slots.** Round-trip none, each slot alone and
  both slots. Check known +Y normal samples and existing tangent basis,
  sRGB emission with an intermediate RGB sample, zero-factor behavior,
  factor-only white fallback, retained factor after Clear and numeric
  boundaries. Wrong format, wrong type, missing or corrupted dependency,
  exceeded bounds and incomplete recipe fail explicitly. Check mip
  selection and shared base/emissive GUID without duplicate dependencies
  or uploads.
- [ ] **R2. Shared compatibility.** Verify precise request and response
  shape, full-recipe correlation, exact legacy refusal versus lookalikes,
  and lost reply behavior. Run CLI, both SDKs and MCP on new and prior
  Runtime. Legacy clients can still read, create, edit or resume the basic
  subset on the new Runtime. Check explicit basic Create on an older host
  without a selected material or automatic retry. For an extended target
  or extended-only ahead source, check legacy Read/Copy/Edit or Resume as
  applicable. Verify the committed Resume receipt includes the original
  GUID, the unchanged source bytes and no new generation. Read extended
  jobs via legacy Status. Check syntactic refusals before admission and
  Edit when only the expected optional slot or emissive factor is stale.
- [ ] **R3. GPU and interaction.** Compare decoded draft and cooked pair
  pixels at the same camera and lighting. Check independent visible normal
  and emissive effects, both slots together, geometric normals after
  Clear and continued factor-only emission after clearing the emissive
  map. Include geometry and base-only goldens, a previously eligible 0092
  cooked material outside the authoring alpha constraint, and clean
  windowed and headless Vulkan validation. Check all preview variants
  competing across clients, an invalid dependency consuming the permit,
  recoverable pre-submit failure, and retained multi-texture upload and
  draw resources through terminal drain on unconfirmed completion. Cover
  rapid replacement, Clear and modal reopen with an admitted request,
  incompatible preview mesh, and correction of a failed dependency with
  draft and focus intact.
- [ ] **R4. Persistence and recovery.** Check stable material and scene
  references, exact dependency deltas, shared-edge retention on Clear,
  and removal of an unused edge from a saved and reactivated recipe.
  Verify first catalog access after publication still sees the old view.
  Activate an earlier basic generation while the committed registry is
  ahead with a new map, and verify the older view stays readable while
  unrelated writes are blocked. Exercise failure before source commit,
  failure after source commit, unknown receipt, explicit extended Resume
  and success with repair needed after cleanup failure. Compare the
  committed recipe bytes and GUID before and after Resume. Retain prior
  worker evidence where the inputs and behavior are unchanged.
- [ ] **R3/R4. Actual Windows Editor at 100%.** Open a saved scene that
  uses a managed opaque material on a compatible mesh. Select an existing
  UNORM normal and a catalog sRGB emissive texture, including one
  0095-registered color asset. Preview, replace and clear each map in the
  draft, including an invalid role selection and an explicit correction.
  Capture the original scene with fixed camera and settings. Save and
  publish with both slots populated. Verify the original scene and undo
  history remain intact, then restart into the named generation and reopen
  the same scene without editing its material reference. Verify the saved
  GUID, recipe, dependencies and visible normal and emissive effects.
  Use Saved Scenes Build/Open from the committed root in a second Editor
  and verify the same result with its own outputs and neither an asset
  build profile nor a generation override. Capture actual standard and
  compact windows at 100%, covering focus, long diagnostics, waiting and
  failure plus Clear, Save and recovery action reachability. Keep native
  window captures and scene/preview PNGs as separate evidence.
- [ ] **Applicable gates.** Build and run affected native and managed
  suites. Build Android if shared Asset System or RuntimeHost changes.
  Preserve existing goldens and submit any changed or new golden for
  human visual review under repository policy. Run `--gate --applies`
  on the final diff and use the existing protocol when selected. Keep the
  raw verdict and baseline unchanged. #340 remains deferred. Check doc
  links, whitespace and scope.

## Rollback Plan

Revert the additive entry points and extended preview while retaining
registered assets and published generations. An older executable may use
an earlier compatible selection. Keep the newer registered sources for a
supporting build or restore the prior complete project state from backup.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). Cite
design approval, trace R1–R4 to evidence, disclose any verification gap,
and record human review of any applicable golden or scope adjustment.
