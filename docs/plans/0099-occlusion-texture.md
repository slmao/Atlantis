# Plan: Occlusion Texture Import and Binding

- **Spec:** [Spec 0099](../specs/0099-occlusion-texture.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending J1–J4 and one-time implementation authorization
- **ADR:** [ADR-0158](../adr/0158-occlusion-texture.md) (Proposed)

## Objective

Deliver the linear Occlusion import, shared opaque material binding,
preview, publication recovery and both supported reopens.

## Milestones / Task Breakdown

1. **S1. Joint review.** Review and approve Spec/Plan 0099 and accept
   ADR-0158. Merge the design by hand.
2. **S2. Source, formats and shared clients.** Extend the existing
   linear producer and registry, validate frozen evidence at the current
   commit and recovery points, and add the material formats and typed
   shared requests. Prove compatibility and refusal before connecting UI.
3. **S3. Render, preview and Editor.** Add the one IBL permutation,
   route scene and preview realizations through existing boundaries,
   and add the managed Assets and material controls. Verify reopens.
4. **S4. Final candidate gates.** Record candidate, relevant inputs and
   R1–R4 results in a review-ready implementation PR. Include any
   previous evidence with its provenance and applicability.

## Files / Modules Touched (expected)

- CMake managed producer declarations and evidence in
  `cmake/AtlantisManagedImports.cmake`, `cmake/AtlantisTextureEvidence.cmake`,
  `src/asset_system/CMakeLists.txt` and relevant asset declarations.
- `src/asset_system/` material source, artifact and metadata, texture
  role and catalog validation. `src/tools/asset_cooker/` cook and
  catalog producer validation. No glTF importer change.
- Runtime asset and material hosts, scene load, material realization,
  mesh preview, bounded built-in studio load and shader startup/deployment
  declarations. Reuse environment decode, realization and upload helpers.
- `src/renderer/` descriptor/pipeline selection, one opt-in variant of
  the existing `shaders/pbr_ibl/` shader and shader build/deployment
  declarations including the existing Android packaging path. No
  new RHI, Vulkan Backend or RenderGraph API.
- Connection asset surface, Remote dispatch/codecs, CLI, native and
  C# SDKs, MCP tools and Editor App Assets/material models and views.
- Focused tests and existing harnesses in
  `tests/{asset_system,runtime,renderer,remote,cli,gameplay_sdk,csharp,image_regression}/`
  and `src/{mcp,editor_app}/tests/`. Update material usage,
  Remote protocol, as-built architecture and navigation.

## Sequencing & Dependencies

Use [PR #362](https://github.com/slmao/Atlantis/pull/362) as the baseline
for registration and frozen texture checks. Use the
[0092 combination preview](../specs/0092-mesh-material-preview.md),
[0093 visual layout](../specs/0093-editor-visual-layout.md) and
[0090 Saved Scenes flow](../specs/0090-saved-scene-build-open.md).
Verify the producer and format boundaries before integrating GPU
resources and UI.

| Decision | Recommendation |
| --- | --- |
| **J1. Lighting and first slice** | Linear R for both IBL terms at fixed strength 1. Clear is neutral. Require empty normal and packed metallic–roughness slots while AO is bound, with explicit clear in either direction. Keep the existing direct path without IBL. |
| **J2. Durable state and recovery** | Canonical Occlusion leaf, manifest v7 only when needed, bound-only material source v12 and artifact/metadata v11. Reuse sole-ahead same-role Resume, complete frozen rechecks at commit and uncertain settlement, explicit activation and older format emission. |
| **J3. Shared clients and preview** | Five additive methods, version-aware legacy guards and one shared admission per service interval. Bounded built-in studio IBL for bound cooked pairs and AO-compatible complete draft calls, including null AO as the comparison control. |
| **J4. Acceptance** | Verify channel math and no-map controls, recovery and compatibility, 100% Windows UI and both independent reopens. Keep applicable Vulkan, golden and performance gates. No dedicated Android acceptance for this class; preserve existing Vulkan packaging and ordinary CI. |

Recommended authorization is one joint approval for S2–S4 after manual
design merge, covering implementation, ordinary in-scope fixes,
applicable verification, valid evidence reuse and a review-ready PR.
Human approval is still required for protected changes, required golden
acceptance and final merge.

## Verification Checklist

- [ ] **R1. Bytes and identity.** Test tagged and untagged linear RGB
  and RGBA PNGs, including a non-extreme R, unequal G/B and alpha.
  Check exact samples, one mip, alpha fill and channel metadata. Refuse
  malformed/bounded inputs, colliding names or keys, cross-role leaves and
  inconsistent evidence with unchanged bytes and GUID. Exercise v4–v6
  compatible controls, v7 with all roles, an older builder refusing
  the new leaf and an older Runtime rejecting v7 under the same otherwise
  compatible profile, with an earlier accepted generation as control.
  Verify a supporting Runtime can read its compatible prior snapshot
  while unsafe registry writes remain unavailable or activation-gated. Inject
  complete-header, noncanonical Register-fragment, same-bytes role/key
  and frozen texture evidence faults. Verify no source commit or
  publication for precommit failures. Compare intact and damaged
  destinations after an uncertain rename.
- [ ] **R2. Material and rendering.** Check bound round-trip and exact
  earlier bytes on clear, catalog schema versions, dependency identity
  and edge changes, invalid role/format/schema/length, cache-hit validation,
  missing dependency, legacy artifact refusal isolated from the v7 gate
  using a cooked-catalog build without the generation profile,
  and explicit conflicting normal/packed/alpha refusals. After an
  ordinary apply that reuses the AO material, check the texture remains
  live. Verify reflected layout and a matched IBL image series with
  R bytes 0, midrange and 255. Include a pair varying only G/B/A.
  In geometry pixels, check `C(R) − C(0) = R × (C(1) − C(0))` within
  existing numeric tolerances in linear HDR. Compare clear and R=1 under
  the same IBL. Use a regular output PNG for preview. Exercise diffuse-
  and specular-dominant fixtures, unchanged direct/shadow and emissive
  contributions, and the no-IBL path. Compare bound cooked and draft
  under the same studio inputs and null draft against R=255. Keep current
  goldens. Obtain human review for new or changed golden images.
- [ ] **R3. Clients and recovery.** Check CLI, both SDKs and MCP on the
  candidate and precise unsupported mapping on the 0098 peer. Include
  malformed and mismatched refusals, transport errors and lost Register
  and Save receipts without replay. Verify a missing or evicted receipt
  leaves the write outcome unknown. Run compatible old calls and reject
  missing or malformed new `occlusionMap` fields and mismatched draft
  echoes. Check Basic, WithMaps and WithMetallicRoughness Read/Copy/Edit
  on a bound active recipe and Resume on an ahead committed recipe.
  Verify older active Read, stale complete Edit and ahead write guards.
  Test role-filtered latest, wrong-kind/evicted ID, shared worker and
  history, permission/lock/repair, wrong-kind or missing target and
  multiple ahead edits, including cross-operation changes. Inject
  failures before source commit, after texture registration and after
  material commit. Remove the texture inbox before same-role Resume.
  Verify the eligible Resume retains exact committed bytes, recipe,
  dependencies and GUIDs. Check frozen revalidation before material Save
  and Resume and at uncertain publication settlement.
- [ ] **R4. Windows, preview and both reopens.** At native Windows 100%
  use standard and compact windows. Verify import mode, long diagnostics,
  select/replace/clear, explicit conflict resolution in both directions,
  stable focus, pending/failure and activation guidance. Capture native
  evidence. Exercise two clients and an early failed request that spends
  the shared permit for the interval. Verify legacy preview lighting,
  AO-bound cooked and draft agreement and the matched null-AO control
  even with live IBL disabled. Cover rapid mesh/draft/catalog changes,
  late errors/results, oversized or missing studio input, pre-submit
  refusal and release after successful completion or terminal drain.
  Verify preview and Save & Publish preserve the source scene, current
  projection, catalog and undo history. Run the IBL fixture sequence from Spec 0099. Compare
  named-generation and formal 0090 second-Editor outcomes for the same
  committed edit against its unbound control, with fixed camera, output
  size and studio environment. Record ordinary startup arguments and the
  active generation. Record the second Editor's explicit managed root,
  saved-slot configure/build and launch arguments. Verify distinct scene
  GUIDs, unchanged scene source hashes and node/material references, one
  AO dependency edge, the same material source digest and AO texture
  GUID, and matching visible effect. Check the saved slot refuses an
  ordinary generation override.
- [ ] **Gates.** Relevant Windows builds, native/managed checks, Vulkan
  Validation Layers and impacted goldens. Evaluate the final diff using
  `--gate --applies` and use the existing performance protocol if selected,
  preserving raw verdicts and the baseline. #340 stays deferred.
  Verify documentation links, whitespace and approved file scope.

## Rollback Plan

Run a compatible prior build/generation against its own compatible
snapshot. Preserve the new registrations and bound recipes. An explicit
clear and publication from a supporting client can emit the previous
material format. An older tool also needs a compatible workspace
snapshot without the new registration role.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
Link candidate evidence to R1–R4 and give the maintainer any unresolved
scope or visual decisions for manual merge.
