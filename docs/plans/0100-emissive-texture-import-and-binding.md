# Plan: Emissive Texture Import and Binding

- **Spec:** [Spec 0100](../specs/0100-emissive-texture-import-and-binding.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Atlantis maintainer, 2026-11-15, [PR #365](https://github.com/slmao/Atlantis/pull/365) at `9bc50950`. Approved Spec/Plan 0100 and J1–J4 together and authorized S2–S4 after the human merge. No new ADR.

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
| `tests/runtime/asset_rebuild_host_tests.cpp`, `tests/tools/asset_cooker/cook_command_tests.cpp` | Existing fixtures for a missing publication or emitted dependency-edge regression. |
| `tests/image_regression/image_regression_gpu_tests.cpp`, `tests/image_regression/emissive_demo_gpu_tests.cpp` | Existing preview and scene image fixtures for an uncovered emissive case. Keep the baseline goldens. |

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
  evidence. Verify that the same imported GUID remains available in the
  base slot and that UI names do not change identity, candidate membership,
  or the existing source/GUID details.
- [ ] **R2 — Binding and image behavior.** Inspect the recipe and a
  catalog emitted by the real cooker on bind, replace and clear. A
  distinct emissive GUID contributes its own edge and loses that edge
  when no slot uses it. On a base-GUID alias, both slot values survive
  cook with one edge, and Clear removes only the emissive value.
  Verify that the shared service refuses managed linear inputs in the
  emissive slot. Use existing image fixtures with a black texel, an sRGB
  midtone, unequal RGB factors and an alpha-only control. Compare zero,
  textured nonzero and Clear with the same factor, using the existing
  output conversion or linear HDR path to distinguish decoded RGB from
  raw byte multiplication. For a draft/cooked comparison use
  `previewMaterialDraftWithMaps` and an otherwise plain material with no
  normal, packed or AO binding. Both routes then use fixed directional
  light. Exercise the Editor's actual draft route separately. Cover
  PNG updates for the current request and state isolation. Reuse
  applicable multi-client admission, resource release and terminal-failure
  evidence where source, assets, fixture, configuration and environment
  still match.
- [ ] **R3 — Shared clients and settlement.** Verify the same color GUID,
  complete recipe and job status through Editor, CLI, both SDKs and MCP,
  using existing shared-conformance evidence where applicable. Exercise
  a successful WithMaps recipe and the existing Basic refusals for
  bound/zero-factor and unbound/nonzero-factor originals. Verify that
  Copy, Edit (including attempted clear), and Resume cannot drop an
  unrepresented field. Confirm that a compatible old active read does
  not authorize an ahead write. Cover a clean refusal, same-role color
  Resume from committed input with the inbox absent, and compatible
  material Resume without a source rewrite. Check that an uncertain
  publication with damaged target evidence remains unconfirmed, and
  that lost or unavailable receipts cause no write replay. Reuse
  applicable generic fault-injection evidence and tie the imported
  emissive GUID, frozen bytes, complete recipe and dependency set to
  the published generation.
- [ ] **R4 — Two reopen paths.** Use the sequence in Spec 0100 R4, with
  an isolated ordinary fixture such as `ibl_material_demo`. Preserve a
  matched unbound capture. Record the ordinary startup arguments and
  active named generation. Record the 0090 explicit managed root,
  saved-slot configure/build and launch arguments. The second launch
  uses `--scene saved_scene` and carries no profile or generation
  override. Record the two scene GUIDs, hashes of the original source
  and the saved copy's two fixed source files before and after reopen,
  stable node/material/texture references, and the same material source
  digest and emissive edge. Use the matched nonzero-factor unbound
  control and compare the two fixed-camera captures. Use two real
  Windows Editors and confirm the ordinary Editor stays attached to
  its named-generation Runtime while the independent slot opens.
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
