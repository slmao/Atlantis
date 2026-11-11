# Plan: Read-Only Mesh and Material Preview

- **Spec:** [Spec 0092](../specs/0092-mesh-material-preview.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending. Implementation awaits explicit joint approval and the design PR's human merge.
- **ADR:** [ADR-0152](../adr/0152-shared-mesh-material-preview.md) (Proposed)

## Objective

Render a bounded still for one cooked mesh and one supported cooked material.
Expose it to shared clients and to two explicit Editor entry points while
preserving existing geometry previews and editing behavior.

## Milestones / Task Breakdown

1. **S1. Joint design review.** Settle J1–J4 and record Spec, Plan and ADR
   approval. Start implementation from current `main` after the human design
   merge.
2. **S2. Shared operation and image.** Implement the pair wire and SDK
   contract, bounded resolution, shared admission and isolated rendering.
   Exercise the production helper with cooked fixtures before UI integration.
3. **S3. Editor entry points.** Add the Assets material selection and the
   material reference chooser's fixed mesh context. Share the intent guard
   and exercise both surfaces in the Windows Editor.
4. **S4. Verification and implementation PR.** Complete the checklist on the
   final candidate. Record applicable gate results, any valid evidence reuse,
   and the new golden for human review. Submit the PR for human merge.

## Files / Modules Touched (expected)

- Asset System `src/asset_system/include/atlantis/asset_system/{load.h,load_material.h,load_texture.h}`
  and `src/asset_system/src/{load.cpp,load_material.cpp,load_texture.cpp}`,
  the existing material and texture artifact decoder headers and sources
  for bounded header validation, plus a private bounded-read helper if
  needed. Preserve existing scene loaders, artifact formats and public
  load behavior.
- Runtime executable `src/runtime/{asset_host.h,asset_host.cpp,main.cpp,CMakeLists.txt}`.
  RuntimeHost `src/runtime/include/atlantis/runtime/{runtime_application.h,material_realization.h}`
  and `src/runtime/src/{mesh_preview.h,mesh_preview.cpp,material_realization.cpp}`
  for private preview wiring and reuse of material creation, uploads and
  uniform helpers. Share the 0091 terminal resource slot and preserve
  `runFrame()`, normal material realization semantics, Renderer,
  RenderGraph, RHI, Vulkan and shaders.
- Connection `src/connection/include/atlantis/connection/asset_connection.h`
  and `src/connection/src/asset_connection.cpp`. Remote
  `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}`.
  CLI `src/cli/src/{command_layer.cpp,invocation.cpp}`. Native Gameplay SDK
  `src/gameplay_sdk/include/atlantis/gameplay/assets.h`. C# SDK
  `src/csharp/Atlantis.Gameplay/{Assets.cs,Remote/AssetCodec.cs,Remote/AssetSession.cs}`.
  MCP `src/mcp/Atlantis.Mcp/{IAtlantisSession.cs,ToolRegistry.cs,ToolSchemas.cs}`.
  Preserve the existing protocol version, transport limits, generated
  bindings and package policies.
- Editor `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs,AssetReferencePickerWindow.cs}`
  and a small shared preview UI/worker helper if needed.
  UI-free state in `src/editor_app/Atlantis.Editor.Model/{AssetBrowser.cs,AssetReferencePicker.cs}`.
  The captured mesh is preview context only. Leave field metadata,
  confirmation, transactions, apply recovery and history behavior intact.
- Focused tests under `tests/{asset_system,runtime,remote,cli,gameplay_sdk,csharp,image_regression}/`,
  `src/mcp/tests/` and `src/editor_app/tests/`. Extend the existing preview
  fixture, generator and comparator with one separate golden and sidecar.
  Use committed cooked-asset inputs from the normal build. Update usage,
  as-built protocol and module boundaries, and navigation in `README.md`,
  `docs/architecture/` and the spec registry.

## Sequencing & Dependencies

Build on [0091](../specs/0091-static-mesh-preview.md) for preview,
[0081](../specs/0081-asset-browser-shared-interface.md) and
[0085](../specs/0085-shared-asset-rebuild.md) for the catalog and generation,
and [0082](../specs/0082-generic-asset-reference-picker.md) and
[0084](../specs/0084-asset-reference-target-types.md) for the chooser.
Use the existing [direct-lit PBR material](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
and [texture upload contract](../adr/0093-static-texture-mip-chain-atex-v3-layout-data-shape-and-per-level-upload.md).

| Decision | Recommendation |
| --- | --- |
| **J1. Pair contract and dependency authority** | Add a separate shared two-GUID operation, keep 0091 compatibility, and resolve the single cooked texture from the same retained catalog with bounded validation and typed refusals. |
| **J2. First rendering recipe** | Approve opaque base `PbrDirectLit` with one sRGB RGBA8 texture and the proposed bounds, fixed camera and white directional light. Share 0091's admission and keep GPU ownership within one request. |
| **J3. Editor entry points** | Offer explicit pairing in Assets and preview in the `Renderable.materialAsset` chooser. Use the labeled opening mesh in the chooser, one shared intent guard, and the existing Confirm contract. |
| **J4. Acceptance and delivery** | Approve the focused cross-client, Editor and GPU checks, one new golden and the existing performance protocol. |

Suggested authorization after joint approval and human design merge covers
S2–S4, including in-scope fixes, applicable verification, valid evidence
reuse and submission of a review-ready implementation PR. Final merge
remains human.

## Verification Checklist

- [ ] **R1/R2. Contract and inputs.** Check canonical request shape,
  independent mismatches of either echoed GUID, swapped roles, repeated
  correlation or required members, exclusive arms, PNG bounds and invalid
  metadata. Distinguish typed refusals from transport and protocol failures.
  Verify old client calls on the new Runtime and new-client fallback on a
  0091 Runtime with geometry/list/get still available in the same session.
  Reject lookalike refusals with the wrong message, duplicate code or both
  envelope arms. Compare the same pair's image and metadata across CLI,
  both SDKs and one MCP image block.
  Exercise both cooked mesh index widths, preview-first/list-first lazy
  catalog initialization, including a pair-only provider without rebuild
  support, and retained first-load failure. Use an eligible managed glTF
  publication to verify the new pair is unavailable on the old session
  through refresh, apply and reconnect, and previews after explicit restart.
  Test missing and wrong-role GUIDs, a mismatched material or
  texture sidecar/schema, absent or inconsistent dependencies, ineligible
  kind, alpha, slot and texture color space or layout, degenerate mesh
  and inclusive input limits. Verify an unsupported material is refused
  before dependency reads, including with a missing optional map.
- [ ] **R2/R3. Image and isolation.** With the production helper on headless
  Vulkan, use the off-origin `textured_quad_left` and
  `pbr_dielectric_rough` for one new golden. Check clear margins, recognizable
  geometry and nonblank pixels. Compare the same mesh with
  `showcase_wood_reddish_stain` for a different texture, and with
  `showcase_wood_ash_tint` to distinguish authored factors on that same
  wood texture. Use a small RGBA8 multi-mip fixture to cover upload regions
  and sampler LOD without a second golden. Verify 0091's geometry golden
  and affected scene captures remain exact. Follow ADR-0042 category 4.
  Capture from clean committed source on the reference GPU with Vulkan
  validation clean, add PNG and full provenance in a subsequent commit,
  and record immediate zero difference, empirical 0/0 repeatability and
  direct human visual review.
- [ ] **R3. Admission and lifetime.** Exercise geometry followed by pair
  and pair followed by geometry across Remote clients and `--exec` in one
  service interval. Invalid arguments take no permit. A refusal or
  disconnect after admission still holds it, with explicit retry on the
  next interval. Verify both uploads reach `ShaderRead` before sampling.
  Repeated previews release request GPU objects and descriptor sets.
  Exercise a recoverable pre-submit failure and inject unconfirmed
  completion after upload, draw and readback have been submitted. Check
  the whole bundle stays alive until the terminal drain with no CPU
  readback or retry. Use normal windowed frames and capture before
  and after both preview modes. Require zero Vulkan validation warnings
  or errors on the preview and affected normal GPU paths.
- [ ] **R4. Windows Editor.** In one real session preview a mesh/material
  pair absent from the scene. With a paused Runtime, unsaved authoring edit
  and pending apply, compare document, World generation, viewport camera
  and overlay, pause/step and undo/redo before and after previews. In the
  material chooser verify the labeled opening mesh after a sibling change,
  a changed candidate and Cancel without mutation. A missing opening mesh
  and a non-Material row on a host without target-type metadata disable
  only preview. After the isolation comparisons, verify normal Confirm
  and focus restoration with preview outstanding or refused. Resume and
  verify apply, undo and redo. Model or controlled UI checks cover A→B→A,
  switching geometry/pair, an Assets reply arriving under the modal, and
  a same-pair reply after chooser reopen. Exercise both input changes,
  hidden or deselected candidate, reload, target invalidation, disconnect,
  delayed connection admission, stale success and refusal, decode failure,
  timeout, fresh explicit retry, close and bitmap release.
- [ ] **Applicable gates.** Build focused Windows native and managed
  targets and run affected suites. Build Android when shared RuntimeHost
  or Asset System inputs change. Run the complete final diff through
  `atlantis_runtime --gate --applies`. Planned Runtime paths select the gate.
  On the reference machine run the existing profiling-on/Tracy-off Release
  `--gate --json` protocol with no preview requested. Keep the raw verdict
  and applicability result for that candidate. A failed, inconclusive or
  incomparable result leaves the gate unresolved. Preserve the current
  baseline and policy.
  Reuse evidence only with unchanged relevant source, tests, inputs and
  environment. Verify documentation and links.

## Rollback Plan

Remove the additive pair operation and its two explicit UI entry points.
The geometry-only operation, authoring data and cooked formats remain
usable without migration. Updated clients map the old unknown-method
refusal to `Unsupported`.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR maps evidence to R1–R4 and records the applicable gates
and human golden review. Joint approval and final merge remain human.
