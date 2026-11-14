# Plan: Texture Import and Base Color Binding

- **Spec:** [Spec 0095](../specs/0095-texture-import-and-base-color-binding.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The Atlantis maintainer approved J1–J4 and Spec/Plan 0095 on 2026-11-14 at `95e0186b`. [PR #355](https://github.com/slmao/Atlantis/pull/355) was manually merged as `e355bed5`. Authorization covers S2–S4, fixes within scope, applicable verification, valid evidence reuse and a review-ready implementation PR. Windows DPI acceptance uses actual 100%. Final merge remains human.
- **ADR:** [ADR-0154](../adr/0154-managed-base-color-texture-import.md) (Accepted)

## Objective

Deliver one PNG registration and one base-color binding through the existing
managed workspace and material editor. Demonstrate the result in the active
generation and in an independent saved-scene build.

## Milestones / Task Breakdown

1. **S1. Joint review.** Settle J1–J4 and record approval of Spec/Plan 0095
   and acceptance of ADR-0154. Merge the design by hand.
2. **S2. Producer and shared operations.** Add the bounded managed PNG
   input, catalog fragment and producer. Extend the existing worker and
   generation validation with texture registration and recovery. Wire both
   SDKs, Remote, CLI and MCP to the shared operations.
3. **S3. Editor workflow.** Add the Assets import action and status display.
   Route binding through the 0094 material and preview flow. Exercise
   publication, activation, dependency change and scene reopen together.
4. **S4. Verification and implementation PR.** Run the applicable checklist
   against the final candidate. Attach requirement evidence, raw gate
   verdicts, scope changes and the remaining human review actions.

## Files / Modules Touched (expected)

- `cmake/AtlantisManagedImports.cmake`, `assets/CMakeLists.txt`,
  `src/asset_system/CMakeLists.txt` and `src/tools/asset_cooker/` for
  producer setup, validation, frozen input and stable catalog identity.
  Reuse Asset System's texture and metadata support. Changes to Asset
  System headers or source, if needed, are limited to CPU-side input or
  cooked texture validation.
- Executable-private `src/runtime/{asset_rebuild_host.*,asset_host.*,CMakeLists.txt}`
  and a private helper if needed for bounded PNG preparation, CNG verification,
  registry and manifest checks. Existing RuntimeHost renderer and frame loop
  need no changes.
- Connection asset interface/values, Remote client/server, native SDK
  asset facade, CLI assets commands, C# SDK assets API and codec, and MCP
  tool registry/schema. Use current package and protocol policies.
- Editor App Assets, worker dispatch and import status/model under
  `src/editor_app/`. Existing material modal changes only if needed for
  display or recovery of the base texture selection.
- Focused fixtures under `tests/{asset_system,runtime,remote,cli,gameplay_sdk,csharp,image_regression}/`,
  `src/mcp/tests/` and `src/editor_app/tests/`. Update the usage guide,
  as-built protocol/module docs and navigation during implementation.

## Sequencing & Dependencies

Depends on [0081](../specs/0081-asset-browser-shared-interface.md) for the
retained catalog, [0085](../specs/0085-shared-asset-rebuild.md) and
[0086](../specs/0086-new-asset-import.md) for the worker and registration,
[0090](../specs/0090-saved-scene-build-open.md) for independent build and
open, and [0092](../specs/0092-mesh-material-preview.md),
[0093](../specs/0093-editor-visual-layout.md) and
[0094](../specs/0094-basic-material-authoring.md) for preview and editing.

| Decision | Recommendation |
| --- | --- |
| **J1. Source and color** | Approve one logical inbox PNG, 8-bit RGB/RGBA, up to 2048 per axis and 24 MiB, with fixed sRGB interpretation and the existing one-mip output. |
| **J2. Identity and recovery** | Approve a sealed immutable registration with a stable GUID, v4 producer evidence, one worker slot, registration before publication and explicit Resume or offline repair. |
| **J3. Shared contract and binding** | Approve the two additive import methods and reuse of the 0094 base texture field, preview and material save. Require explicit activation after each publication. |
| **J4. Acceptance and delivery** | Approve real Windows 100% UI acceptance, an independent saved-scene build, both client version directions and the applicable GPU and performance gates. |

Suggested single authorization after joint approval and manual design
merge: continue from current `main` through S2–S4, including scope-preserving
fixes, applicable verification, valid evidence reuse and a review-ready
implementation PR. Final merge remains human.

## Verification Checklist

- [ ] **R1. Input and producer.** Cook known RGB/RGBA fixtures and verify
  sample bytes, alpha, sRGB metadata, one mip and preview eligibility.
  Use an intermediate RGB value to distinguish sRGB sampling from linear
  sampling. Check metadata tags leave sample bytes unchanged. Cover RGB
  alpha 255 and RGBA alpha preservation in an opaque material.
  Check invalid/truncated framing, unsupported channel or bit depth,
  APNG, CgBI and tRNS, input and dimension bounds before decoding, and unsafe/noncanonical
  inbox paths. Verify clean configure-only staging, stable GUID/key across
  clean builds, separate namespaces and collision refusal against
  checked-in, managed and generated identities. Exercise normal opt-in and
  Saved Scenes configuration with registered texture and material units.
- [ ] **R2. Publication and recovery.** Verify exact whole-catalog identity
  and dependency deltas. Swap or change the frozen PNG, artifact and
  metadata independently and verify seed/generation rejection. Check a
  pre-v4 generation against a workspace that has since added textures.
  Check job contention with glTF, material and rebuild, and
  with a second workspace owner. Exercise refusal before registration,
  interruption around registration, known committed failure before
  publication, unverified settlement, and confirmed publication with
  cleanup failure. Verify repair markers, lost receipt recovery, explicit
  Resume from committed source with the inbox removed, refusal for changed
  bytes after registration even before first activation, rejection of
  multiple ahead changes including glTF/material Resume while a texture
  is ahead, refusal of a targeted
  managed texture rebuild, and activation under the same GUID. Check old
  Runtime artifacts and the first old catalog query
  after publication. Test malformed frozen paths/metadata and unexpected
  dependency changes.
- [ ] **R3. Shared compatibility.** Check exact request/reply shapes,
  name/mode/job correlation, lost admission and status transport errors,
  evicted receipt behavior, independently retained glTF and texture
  status, success with repair required, legacy exact fallback and lookalike
  failures. Run CLI, both SDKs and MCP against a supported Runtime for
  discovery and matching admission, status and refusal. Verify unchanged
  older-client discovery and legacy operations against the new Runtime, and
  new-client refusals against the older Runtime and a host without opt-in.
- [ ] **R4. Windows workflow.** In an opted-in Editor, import a PNG whose
  pattern visibly differs from the material's original base texture.
  See its receipt and restart guidance; verify old snapshot, scene and
  history. Explicitly activate, select the texture in the existing Base
  texture field, preview and compare to the previous texture. Exercise a
  missing or incompatible dependency, keep the draft and retry explicitly.
  Use a saved scene with an existing material reference. Publish the edit
  and verify the previous texture edge is absent, the new one is present
  and the material GUID and other recipe values are unchanged. Restart and
  reopen the scene without editing its material reference. Compare the
  rendered result with the pre-edit scene at the same camera and settings.
  Build the same scene through Saved Scenes
  using the committed root, open in an independent Editor and verify the
  recipe/edge and rendered result from its own build without a generation
  override. At actual Windows 100% scaling, capture the standard and
  compact Assets import and material layouts. In both sizes check keyboard
  focus, long diagnostics, Save/Register/Resume reachability and controls
  during waiting and failure. Record the scale, size and status with the
  screenshots.
- [ ] **Applicable gates.** Build and run affected Windows native and
  managed suites. Build Android if shared Asset System changes. Run
  windowed and headless affected Vulkan paths cleanly, retain existing
  geometry/pair goldens, and compare the imported-texture draft with the
  same cooked recipe at equal decoded pixels. Show a different texture
  changes the result. Follow ADR-0042 for any golden change and human
  visual review. Evaluate the final diff with `--gate --applies`; when
  selected, use the existing protocol, preserve raw outcomes and leave
  baseline and policy intact. The historical #340 investigation stays
  outside this Plan. Verify documentation links and whitespace.

## Rollback Plan

Revert the additive surfaces and producer with the corresponding Editor
entry. Preserve registered PNGs, GUID fragments and complete generations.
To run an older build, use a compatible older generation and saved
material/scene sources whose dependencies that build can resolve. Retain
newer sources and generations for a compatible build or backup restore.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR records the approved decisions, checks, reused evidence
and any open gate. The reviewing PR records the joint design decision.
