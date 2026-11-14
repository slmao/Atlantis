# Plan: Metallic–Roughness Texture Import and Binding

- **Spec:** [Spec 0098](../specs/0098-metallic-roughness-texture.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The Atlantis maintainer approved J1–J4 and Spec/Plan 0098 at `8241daab` on 2026-11-14 and authorized S2–S4 after design merge. [PR #361](https://github.com/slmao/Atlantis/pull/361) was merged by the maintainer as `f24618d2`. Windows UI acceptance is 100% DPI. Final merge remains human.
- **ADR:** [ADR-0157](../adr/0157-metallic-roughness-texture.md) (Accepted)

## Objective

Deliver the standalone packed PNG, managed opaque direct-lit material
binding and both reopens in one slice.

## Milestones / Task Breakdown

1. **S1. Joint review.** Resolve J1–J4, approve Spec/Plan 0098, accept
   ADR-0157 and merge the design by hand.
2. **S2. Import, material and shared clients.** Extend the existing PNG
   producer and texture worker with the canonical role and generation
   checks. Add dual-format material cooking and loading with metadata,
   plus the shared recipe methods. Verify refusal, compatibility and
   Resume before UI work.
3. **S3. Render, preview and Editor.** Wire the two mapped permutations
   to scene loading and realization and to the existing pair-preview
   path. Integrate Assets and the existing material dialog. Verify the
   unchanged fallback and both supported reopens.
4. **S4. Candidate gates.** Run affected builds and verification against
   the final candidate. Record provenance, limitations and any required
   human golden or scope decisions in a review-ready implementation PR.

## Files / Modules Touched (expected)

- `cmake/AtlantisManagedImports.cmake`, `cmake/AtlantisTextureEvidence.cmake`,
  `src/asset_system/CMakeLists.txt` and managed asset declarations as needed.
  Keep existing evidence and catalog formats.
- `src/asset_system/` material source, artifact, metadata, texture role/key and
  catalog validation paths, and `src/tools/asset_cooker/` for bounded
  PNG dispatch, role validation and material cook/catalog output.
- `src/runtime/asset_rebuild_host.*`, `asset_host.*`, `material_authoring.h`
  and the Runtime scene loader, material realization, preview, startup
  resource wiring and corresponding private headers/build declarations.
  Preserve the launch contracts.
- `src/renderer/` material descriptors and pipeline selection,
  `shaders/` direct-lit variants and their build/deployment declarations,
  including Android packaging. No new RHI, Vulkan Backend or RenderGraph
  API.
- Connection assets, Remote codecs/dispatch, CLI, both SDKs and MCP
  registry and schemas. Editor App Assets and material dialog/model.
- Focused fixtures and declarations in `tests/{asset_system,runtime,renderer,remote,cli,gameplay_sdk,csharp,image_regression}/`,
  and `src/{mcp,editor_app}/tests/`. Update usage, as-built architecture
  and navigation.

## Sequencing & Dependencies

Start from the merge of [0097](../specs/0097-linear-normal-map-import.md)
and the two precommit fixes in [PR #360](https://github.com/slmao/Atlantis/pull/360).
Use [0096](../specs/0096-pbr-texture-editing.md) for material editing and
preview and [0090](../specs/0090-saved-scene-build-open.md) for the saved
copy. Review source/cooked compatibility and the authoritative role before
shipping the binding, then hold the rendering and editor flow to it.

| Decision | Recommendation |
| --- | --- |
| **J1. Format and scope** | Approve one linear RGB/RGBA PNG, roughness in G and metallic in B, retained scalar multipliers, and the opaque direct-lit subset including existing normal mapping. |
| **J2. Durable contracts** | Approve the canonical registered leaf, v6 only for packed-map views, same-role Resume, and bound-only source v11 plus artifact and metadata v10. Keep existing formats when the slot is cleared. |
| **J3. Shared editing and rendering** | Approve five additive methods, shared status/recovery, legacy overwrite guards, a single preview permit, and two opt-in descriptor permutations. Require the canonical packed role for the slot and reject it in other slots. |
| **J4. Acceptance** | Approve Windows 100% and both reopen legs from the same published edit, Android build/startup and applicable Vulkan, golden and performance gates. |

Suggested one-time authorization following joint approval and manual
design merge is to execute S2–S4 from current `main`, including ordinary
in-scope fixes, valid evidence reuse, applicable gates and a review-ready
PR. Final merge remains human.

## Verification Checklist

- [ ] **R1. Import and frozen views.** Use a non-extreme RGB/RGBA fixture
  with unequal G/B and tagged and untagged inputs. Check exact bytes,
  alpha fill/preservation, one mip and UNORM. Run representative bounded
  rejects and key/format mismatch. Check cross-role name collisions,
  a mixed-leaf unit, and a role-only change with identical GUID/PNG.
  Validate v4/v5 fallback and v6 with all three kinds, including repair
  and reads while the registry is ahead. Verify an old builder rejects
  the new leaf and an old Runtime rejects v6 in a controlled test with
  otherwise compatible profile ownership and intact prior-generation
  control. Change the role/format evidence or frozen membership and
  prove activation refuses the inconsistent snapshot. Exercise #360's
  header and fragment faults on the new path and prove neither rename
  occurs. Exercise intact and damaged destinations after an ambiguous
  publication rename.
- [ ] **R2. Material and image.** Check old source/artifact/metadata reads
  and byte-compatible empty-slot writes. Check bound v11/v10/v10
  round-trip, actual catalog schema values, size/version/key mismatches,
  older Runtime refusal of a scene requiring the new artifact, replace
  and clear, wrong-role/format and missing/malformed dependencies, and refusal on
  other material modes. Compare the complete cooked recipe and unique
  edges. Under fixed camera/light/UV, compare constant-channel G-only
  and B-only variations against scalar-only references using the expected
  effective factor values. Use non-unit factors and vary only R/A to
  verify those channels have no effect. Include zero metallic and a
  clear/no-map control. The image expectations should catch G/B reversal,
  gamma conversion, factors replaced by texels and applying G after BRDF
  alpha calculation. Exercise normal-map coexistence, shader
  reflection and descriptor bindings, cooked/draft image agreement and
  the corresponding 3D scene. Keep unrelated goldens. Obtain human
  visual review for new or changed golden images.
- [ ] **R3. Clients and recovery.** Exercise new CLI, SDKs and MCP on a
  0097 peer and legacy representable reads/writes on the candidate. Check
  exact legacy refusal and malformed lookalikes, mismatched/late IDs and
  lost write receipt without replay. Verify both legacy recipe versions
  refuse Read/Copy/Edit when a packed recipe is active. While the old
  snapshot is still active, verify legacy Read returns that snapshot,
  an Edit using it cannot overwrite the ahead source, and legacy Resume
  retains the committed GUID and bytes without publishing. Verify a
  supported Resume and legacy clients still handle recipes they can
  represent. Verify per-kind latest, wrong-kind IDs,
  shared history/worker and repair. Inject failures before registration
  and after each of the texture and material source commits. Confirm
  source-only same-kind Resume preserves bytes, role/GUID, recipe and
  dependencies, while the running catalog and scene stay stable.
- [ ] **R4. Windows 100% and reopen.** In actual standard and compact
  windows use the Assets choice, switch kinds during admission, discover
  the activated texture and select/replace/clear in the existing material
  dialog. Verify wrong-role feedback, focus, visible diagnostics and
  pending/restart feedback with native screenshots. Exercise two preview
  clients and rapid changes. Check resource release at settlement and
  preserve scene document, projection and undo history. Establish a
  scene fixture with the existing registered material reference before
  seed/staging configuration and record its source hash. Save As after
  texture activation and before editing. Confirm ordinary Runtime
  reopens the same scene and material on the named generation without
  a scene transaction or Apply. Open the saved copy using the standard second-Editor 0090 build
  from committed sources. Record exact startup arguments, original and
  distinct saved scene GUIDs, node/material/texture GUIDs, saved file
  hashes, complete cooked recipe and unique edges. Verify both legs
  consumed the same edit, with unchanged source hashes and matching
  fixed-camera images at the same size.
  Verify the saved-slot refuses the ordinary generation override.
- [ ] **Gates.** Relevant Windows builds and native/managed suites;
  Android arm64 build, shader packaging and Vulkan startup smoke with a
  cooked fixture exercising the new slot. Run supported Windows Vulkan
  validation and affected goldens, including no-map comparisons. Check
  the final diff with `--gate --applies` and run the existing protocol
  when selected. Preserve raw verdicts and the baseline. #340 stays
  deferred. Review documentation links, whitespace and file scope.

## Rollback Plan

Run a compatible prior build or generation with a compatible project
snapshot. Preserve new registered textures and bound recipes in backup.
Clearing with a supporting client and publishing emits the earlier
material formats, but an older tool still requires a project snapshot
without the new texture registration role.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). Map
evidence to R1–R4 and identify any applicable gate, file-scope or visual
rulings needed for manual merge.
