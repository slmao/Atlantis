# Plan: Linear Normal Map Import

- **Spec:** [Spec 0097](../specs/0097-linear-normal-map-import.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The Atlantis maintainer approved J1–J4 and Spec/Plan 0097 on 2026-11-14 at `d81dd9b6`. [PR #359](https://github.com/slmao/Atlantis/pull/359) was manually merged as `bde296ab`. Authorization covers S2–S4, ordinary in-scope fixes, applicable verification, valid evidence reuse and a review-ready PR. Windows UI acceptance is 100% DPI. Final merge remains human.
- **ADR:** [ADR-0156](../adr/0156-managed-linear-normal-map-import.md) (Accepted)

## Objective

Deliver one managed +Y RGBA8 UNORM PNG import with stable identity and
recovery, then use the imported texture in 0096's existing Normal map slot.

## Milestones / Task Breakdown

1. **S1. Joint design review.** Approve Spec/Plan 0097, resolve J1–J4 and
   accept ADR-0156. Merge the design PR by hand.
2. **S2. Producer, worker and clients.** Preserve 0095's canonical color
   form and add normal registration, role evidence and activation checks.
   Keep the two kinds in one worker and implement the shared methods and
   status correlation across clients. Verify recovery before UI integration.
3. **S3. Editor and reopen.** Extend Assets import using 0093 styles.
   Connect the existing normal binding and preview to the registered GUID.
   Exercise both supported restart and reopen flows.
4. **S4. Candidate gates and implementation PR.** Run the mapped checks,
   record evidence provenance, fixture boundaries and any scope adjustments,
   and submit a review-ready PR for human merge.

## Files / Modules Touched (expected)

- `cmake/AtlantisManagedImports.cmake`, `cmake/AtlantisTextureEvidence.cmake`,
  `src/asset_system/CMakeLists.txt` and `src/tools/asset_cooker/` for the
  managed source leaf, format dispatch, dependencies, bounded PNG and frozen
  evidence. Any Asset System changes stay CPU-side.
- Executable-private `src/runtime/asset_rebuild_host.*`, `asset_host.*` and
  existing bounded texture loading/validation used by the worker. Keep
  RuntimeHost rendering and the scene launch entry points intact.
- Connection asset interface and codecs, Remote client/server and wire
  codec, CLI commands and output, native SDK asset facade, C# SDK assets
  contract and codec, MCP registry and schemas.
- Editor App Assets controls, worker dispatch and task/status model under
  `src/editor_app/`. Reuse Assets discovery, the 0096 material modal and
  preview. Changes to that modal are limited to diagnostics if required.
- Focused fixtures under `tests/{asset_system,runtime,remote,cli,gameplay_sdk,csharp,image_regression}/`,
  `src/mcp/tests/`, `src/editor_app/tests/`, and existing fixture build
  declarations. Update usage, as-built module/protocol docs and navigation.

## Sequencing & Dependencies

Build on merged [0095](../specs/0095-texture-import-and-base-color-binding.md),
[0096](../specs/0096-pbr-texture-editing.md) and
[0090](../specs/0090-saved-scene-build-open.md). Reuse the producer's PNG
validation for both interpretations. Complete the durable role checks and
dual-version exercises before relying on the Editor flow.

| Decision | Recommendation |
| --- | --- |
| **J1. PNG and convention** | Approve the 0095 PNG subset, unchanged sample bytes in one-mip RGBA8 UNORM, and +Y toward the existing positive tangent-space bitangent. |
| **J2. Registration and recovery** | Approve the `normal.png` leaf/key in the shared namespace, role-aware identity comparisons, v5 only for normal-bearing views, prepared-directory verification, and explicit same-kind Resume. |
| **J3. Client contract and Editor** | Approve the two closed normal methods, exact old-host refusal, kind-specific status over one bounded history, and one workspace-wide write guard across both Editor choices. |
| **J4. Acceptance** | Approve Windows 100%, ordinary named-generation restart of the original scene without rebinding, and independent saved-copy reopen under 0090. Keep applicable Vulkan, golden and performance gates. |

Suggested one-time authorization after joint approval and manual design
merge: run S2–S4 from current `main`, including ordinary in-scope fixes,
applicable verification, valid evidence reuse and a review-ready PR.
Final merge remains human.

## Verification Checklist

- [ ] **R1. Cook and format.** Check exact RGB/RGBA bytes, RGB alpha fill,
  RGBA alpha preservation, one mip and UNORM sidecar/artifact. Check tagged
  and untagged inputs against the same output and verify non-extreme channel
  values. Run representative 0095 invalid inputs through the new operation,
  including malformed CRC, APNG, CgBI, `tRNS` and over-limit inputs, and retain
  the color path's byte results. Refuse a contradictory managed cooker
  key/color-space pair even when the PNG is otherwise valid.
  Confirm that a strict old builder refuses a normal registration before
  cooking. Reject mixed leaves, noncanonical units and clashes between color
  and normal names. Prove both kinds' GUIDs survive fresh opt-in builds.
- [ ] **R2. Generation and recovery.** Check earlier compatible views,
  color-only v4 and mixed v5, including activation while the registry is
  ahead, old-host refusal of v5 and first catalog access after publication.
  Exercise a configure-only workspace and GUID/key/edge preservation.
  Change the frozen leaf/key, role, PNG, artifact and metadata independently.
  Cover a role-only registry change with identical PNG bytes and GUID, and
  invalid or mislabeled manifest versions. Change the private fragment or
  prepared directory after preparation and verify that neither rename
  occurs. A wrong-format cook must fail before registration.
  Force a postcommit failure, then confirm same-kind Resume with no inbox
  and byte-identical committed source/fragment/seal and stable GUID. Check
  wrong-kind Resume in both directions, a cached correct kind changed before
  the worker runs, extra ahead changes, contention, unknown settlement, and
  a cleanup fault on confirmed publication. Reuse only valid 0095 evidence
  for unchanged behavior and inputs.
- [ ] **R3. Cross-version clients.** Run new CLI, SDKs and MCP against a
  0095 host, and exercise legacy color import/status and normal discovery
  against the new host. Test exact legacy refusal, extra and duplicate fields,
  in-band lookalikes, mismatched correlations and lost replies. Check
  wrong-kind explicit IDs, per-kind latest during opposite-kind work,
  shared history eviction across kinds, repair with no matching job, and
  correct exit/error behavior.
- [ ] **R4. Windows Editor and Vulkan.** At 100% use Assets to register a
  +Y fixture and follow restart guidance. Exercise a refusal and explicit
  recovery, and a kind switch while admission or settlement is pending.
  Confirm the original receipt, no second write and correct Resume routing
  after switching back. At standard and compact sizes check focus, status
  and reachable actions. Retain native UI
  captures separately from engine PNGs. Activate, discover, bind and
  preview the new GUID. Check cross-slot refusals. Compare a fixed-camera
  capture before and after activation with a known asymmetric green-channel
  normal. With fixed geometry, tangent handedness, lighting and exposure,
  use a neutral-normal control and lit-region or pixel expectations that
  distinguish the exported +Y sign from flipped green and an sRGB decode.
  Verify draft and cooked pair previews agree. Save & Publish the material
  and verify preview and publication leave the original document and
  history intact.
  Restart the ordinary opted-in Runtime with the material generation and
  verify the persisted material reference, recipe, edge and visible effect
  with no scene write or Apply to recreate the binding. For this leg use an
  ordinary managed seed. In an isolated development checkout, put a copy of
  the fixture scene with its managed material reference at an existing
  ordinary scene slot before configuring seed and staging. Retain that
  scene GUID and source hash across publication and restart. After the texture
  generation activates and before the material edit, use Save As to create
  one saved unit from that scene. Record its distinct scene GUID, both saved
  file hashes, and the shared node and material GUIDs. After publication,
  independently build and open that saved unit through 0090 from committed
  sources in a second Editor without profile or generation override.
  Verify the saved unit's own identity, unchanged file hashes, original
  node/material reference, persisted recipe and matching fixed-camera
  appearance. Record the actual startup arguments for both legs. Check the
  production saved-slot launch still refuses a generation override.
- [ ] **Applicable gates.** Build and run affected Windows native and
  managed suites. Build Android if portable Asset System or RuntimeHost
  changes. Verify clean Vulkan validation on the exercised preview and scene
  paths and preserve existing golden comparisons; seek manual visual review
  for any new or changed golden. Run the final diff through `--gate --applies`
  and the existing protocol if selected. Retain raw verdicts and baseline.
  Keep #340 deferred. Check links, whitespace and scope.

## Rollback Plan

Use a compatible earlier seed or generation. Preserve the normal units and
their dependent material sources in backup. A prior builder cannot cook a
root containing the new normal form, so use a compatible project snapshot
when rolling back tools. Restoring the tool does not rewrite registered
PNGs, material bindings or generation files.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). Cite
joint approval, map R1–R4 to evidence, disclose any remaining gaps and record
human review of any applicable golden or scope adjustment.
