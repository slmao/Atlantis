# Plan: Emissive Texture Import and Binding

- **Spec:** [Spec 0100](../specs/0100-emissive-texture-import-and-binding.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending approval of Spec 0100 and this Plan together, with explicit implementation authorization.

## Objective

Expose the existing sRGB-color-to-emissive workflow in Assets and the
material chooser. Verify the published result through ordinary
named-generation restart and the independent Saved Scenes flow.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Resolve the decisions in the reviewing PR.
   Record the approval and wait for the design merge.
2. **S2 — Implementation.** Update the existing Assets color option and
   managed texture names using the 0093 view conventions. Update the
   existing user guide. Add focused regressions where existing evidence
   leaves a feature-specific gap.
3. **S3 — Verification.** Run the checklist on the implementation
   candidate. Cite reused evidence by revision, relevant inputs,
   environment and limitation. Preserve failed checks and explain reruns.
4. **S4 — Review-ready PR.** Link the approved design, checks and evidence,
   and report remaining decisions for human merge.

## Files / Modules Touched (expected)

| Scope | Purpose |
| --- | --- |
| `src/editor_app/Atlantis.Editor/MainWindow.cs` | Color import choice, description and status wording. |
| `src/editor_app/Atlantis.Editor/EditorVisuals.cs` | Shared presentation of canonical color entries in Assets and material choices. |
| `src/editor_app/Atlantis.Editor/MaterialEditorWindow.cs` | Keep the existing emissive and factor guidance legible on standard and compact windows. |
| `README.md` | Operator steps and existing shared CLI examples. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{AssetBrowserTests,MeshMaterialPreviewTests,IntegrationTest}.cs` | Existing test entry points for labels, draft preservation or stale replies where coverage is missing. |
| `tests/runtime/asset_rebuild_host_tests.cpp`, `tests/runtime/material_realization_gpu_tests.cpp`, `tests/image_regression/emissive_demo_gpu_tests.cpp` | Existing fixtures for a missing frozen-evidence, role/alias or emissive image regression. Keep the baseline goldens. |

Runtime, asset cooker, renderer, Remote and client production sources are
reference and verification inputs. A change to their contract or boundary
needs a design decision. Report any nonsemantic file-list adjustment in
the implementation PR under the existing rule.

## Sequencing & Dependencies

Base S2 on latest main after the design is approved and merged. Rely on
0095–0099 for managed registration, material authoring, preview and
settlement, and 0090 for independent scene build/open. Set up the shared
managed material reference before seed configuration. Activate the
texture generation before binding and save the scene copy before the
material edit. Hold committed project inputs fixed between publication
and the independent build.

## Verification Checklist

- [ ] **R1 — Managed input and Editor.** Reuse applicable 0095/0099 PNG,
  identity and producer coverage. At native Windows 100%, check the
  standard and compact Assets/material flow. Import two differently
  named color textures sharing the `source.png` leaf and identify them
  by name and existing source details. Check explicit activation,
  select/replace/clear, zero-factor guidance, keyboard focus and
  building/failure text including a long diagnostic. Capture native
  evidence. A valid sRGB texture remains available in the base slot.
- [ ] **R2 — Binding and image behavior.** Check distinct texture and
  base-GUID alias, one edge per dependency, and clearing only the
  emissive reference. Verify that managed linear inputs are refused in
  the emissive slot by the shared service. With existing image fixtures
  and fixed inputs, use an sRGB pattern with a midtone and a black texel,
  different channel factors and an alpha-only control. Compare zero
  factor, textured nonzero factor and Clear with the same factor. Use
  the existing output conversion or linear HDR path to distinguish
  decoded RGB multiplication from raw byte multiplication. Check a
  draft/cooked pair with matched lighting. Cover current-request-only
  PNG updates and state isolation. Reuse applicable multi-client
  admission, resource release and terminal-failure evidence where
  source, assets, fixture, configuration and environment still match.
- [ ] **R3 — Shared clients and settlement.** Verify the same color GUID,
  complete recipe and job status through Editor, CLI, both SDKs and MCP,
  using existing shared-conformance evidence where applicable. Exercise
  legacy representable recipes and the refusal to drop an existing
  emissive binding or nonzero factor, plus preservation of any other
  fields outside the caller's recipe version. Cover clean refusal,
  committed-but-unpublished texture and material Resume, and receipt
  loss without replay. Reuse applicable generic fault-injection evidence.
  The asset-specific check ties the imported emissive GUID, source
  bytes, complete recipe and dependency set to the published generation.
- [ ] **R4 — Two reopen paths.** Use the sequence in Spec 0100 R4, with
  an isolated ordinary fixture such as `ibl_material_demo`. Preserve a
  matched unbound capture. Record the ordinary startup arguments and
  active named generation. Record the 0090 explicit managed root,
  saved-slot configure/build and launch arguments. The second launch
  uses `--scene saved_scene` and carries no profile or generation
  override. Record unchanged scene source hashes, stable reference
  identities and matching fixed-camera captures showing the committed
  effect. Use real Windows Editors for both legs.
- [ ] **Gates.** Verify affected Windows builds, model and UI checks,
  Vulkan Validation Layers on the relevant preview/render/reopen paths
  and impacted image/golden checks under the existing strategy. Obtain
  human review if golden output changes. Run `--gate --applies` against
  the final implementation diff. If selected, use the existing protocol,
  preserve the raw verdict and keep the baseline. The historical #340
  investigation remains deferred. There is no dedicated Android
  acceptance for this Windows authoring flow. Check documentation links,
  whitespace and approved file scope.

## Rollback Plan

Revert the Editor wording and guide if needed. The same color assets,
material recipes and compatible generations remain readable by the prior
supported clients. An operator can use the existing Clear and Save &
Publish operation when they want to remove an emissive binding.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). Link
evidence for R1–R4, report any required gate awaiting a decision, and
hand off the final merge to the maintainer.
