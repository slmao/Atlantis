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

- [ ] **R1/R2. Contract and inputs.** Check canonical request shape and both
  echoed GUIDs, exclusive arms, the 0091 PNG bounds and invalid metadata,
  transport and protocol failures, and typed refusals. Verify old client
  calls on the new Runtime and new-client fallback on a 0091 Runtime with
  geometry/list/get still available in the same session. Compare the same
  pair's image and metadata across CLI, both SDKs and one MCP image block.
  Exercise both cooked mesh index widths, preview-first/list-first lazy
  catalog initialization with and without rebuild support, retained first
  failure and a newly published material/texture visible only after Runtime
  restart. Use the existing managed glTF publication flow for that check.
  Test missing and wrong-role GUIDs, a mismatched material or
  texture sidecar/schema, absent or inconsistent dependencies, ineligible
  material and texture variants, degenerate mesh and the inclusive size
  limits. Use an opaque base material, a mapped variant and a transparent
  one from existing assets where practical.
- [ ] **R2/R3. Image and isolation.** With the production helper on headless
  Vulkan, use the off-origin `textured_quad_left` and
  `pbr_dielectric_rough` for one new golden. Compare the same mesh with a
  distinct supported material such as `showcase_wood_reddish_stain`.
  Check clear margins, recognizable geometry, nonblank pixels and a
  material-dependent difference. Verify 0091's geometry golden and affected
  scene captures remain exact. Follow ADR-0042 category 4 for the new
  golden. Capture from clean committed source on the reference GPU, commit
  the PNG and provenance separately, confirm immediate zero difference
  and repeatability, and require direct human visual review.
- [ ] **R3. Admission and lifetime.** Exercise geometry followed by pair
  and pair followed by geometry across Remote clients and `--exec` in one
  service interval. Invalid arguments take no permit. A refusal or
  disconnect after admission still holds it, with explicit retry on the
  next interval. Verify texture upload precedes sampling, repeated previews
  release all request GPU objects, recoverable failure frees scratch, and
  injected unconfirmed completion retains it until terminal drain with
  neither readback nor retry. Use normal windowed frames and capture before
  and after both preview modes. Require zero Vulkan validation warnings
  or errors on the preview and affected normal GPU paths.
- [ ] **R4. Windows Editor.** In one real session preview a mesh/material
  pair absent from the scene. With a paused Runtime, unsaved authoring edit
  and pending apply, compare document, World generation, viewport camera
  and overlay, pause/step and undo/redo before and after previews. In the
  material chooser verify the displayed opening mesh, changing candidate,
  Cancel with no mutation and normal Confirm even after a refused preview.
  Verify the unavailable state without a valid opening mesh and the
  fixed, labeled mesh when the sibling value changes. Resume and verify
  apply, undo and redo. Model or controlled UI checks cover A→B→A, switching
  geometry/pair and Assets/chooser, hidden/deselected candidate, reload,
  invalid target, reopen or disconnect, and close during retry. Exercise
  send admission after delayed setup, stale success and refusal, decode
  failure, timeout, fresh explicit retry and bitmap release.
- [ ] **Applicable gates.** Build focused Windows native and managed
  targets and run affected suites. Build Android when shared RuntimeHost
  or Asset System inputs change. Run the complete final diff through
  `atlantis_runtime --gate --applies`. Planned Runtime paths select the gate.
  On the reference machine run the existing profiling-on/Tracy-off Release
  `--gate --json` protocol with no preview requested. Keep the raw verdict
  and applicability result. Preserve the current baseline and policy.
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
