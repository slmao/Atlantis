# Plan: Combined Occlusion, Normal and Metallic–Roughness Maps

- **Spec:** [Spec 0101](../specs/0101-combined-pbr-maps.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending joint approval and explicit implementation authorization
- **Related ADR:** [ADR-0159](../adr/0159-combined-pbr-maps.md) (Proposed)

## Objective

Deliver coexistence on opaque PbrDirectLit through the existing material
editor and shared asset contract, then verify both independent scene
reopen paths.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Approve Spec and Plan, accept ADR-0159 and
   merge the design by hand before implementation.
2. **S2 — Implementation.** Land the material encoding and versioned
   recipe rules first. Carry them through catalog cooking, activation,
   publication, Runtime realization and shared clients. Add the three
   IBL variants and then connect the existing Editor controls and
   preview to the capability.
3. **S3 — Verification.** Run the checklist against the final candidate.
   Compare legacy and supporting peers. Attach results, evidence
   provenance and limitations in the implementation PR.
4. **S4 — Review-ready PR.** Link approval, the checked scope and
   evidence. Report required gates and hand off final merge.

## Files / Modules Touched (expected)

| Scope | Work |
| --- | --- |
| `src/asset_system/{include/atlantis/asset_system,src}/material_{source,artifact,metadata}.*`, `src/asset_system/src/{cook_material,load_material}.cpp`, `src/tools/asset_cooker/cook_command.cpp` | Canonical versions, codec, schema checks and catalog edges. |
| `src/runtime/{asset_host.*,asset_rebuild_host.*,material_authoring.h,main.cpp,CMakeLists.txt}`, `src/runtime/src/{material_realization,scene_load,mesh_preview,runtime_application}.*` and matching Runtime headers | Version capability, frozen preparation and activation, independent resources, preview and startup. |
| `src/renderer/{src,include/atlantis/renderer}/material.*`, `src/renderer/src/renderer.cpp`, `src/vulkan_backend/src/{vulkan_device.cpp,vulkan_command_list.h}`, `src/rhi/include/atlantis/rhi/types.h`, `shaders/{pbr_ibl,pbr_ibl_normal_map}/` | Binding ownership, layouts, descriptors, Pipeline contract comment and fragment variants. |
| `src/runtime/android/android_main.cpp`, `android/app/build.gradle` | Package and load the new Vulkan variants through the existing Android build. |
| `src/connection/{include/atlantis/connection/asset_connection.h,src/asset_connection.cpp}`, `src/remote/src/remote_{client,server}.cpp`, `src/cli/src/{command_layer,invocation}.cpp`, `src/csharp/Atlantis.Gameplay/Remote/{AssetSession,AssetCodec}.cs`, `src/mcp/Atlantis.Mcp/{ToolRegistry,ToolSchemas}.cs` and owning public headers where signatures are declared | Versioned exact operations, compatibility and shared status. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession,MaterialEditorWindow}.cs`, `src/editor_app/Atlantis.Editor.Model/AssetBrowser.cs` | Capability, controls, preview intent and recovery. |
| Existing material/asset fixtures in `tests/{asset_system,runtime,renderer,image_regression,remote,cli,csharp}/`, `tests/tools/asset_cooker/` and corresponding MCP and Editor asset tests | Codec, rendering, lifetime, cross-version and publishing regression coverage. Add only cases needed for the approved checklist. |
| `README.md`, `docs/architecture/remote_protocol.md`, `docs/specs/README.md` | Operator and shared method documentation and navigation. |

0090 launcher sources, import role contracts and generation manifest
format are reference inputs. Report necessary nonsemantic scope
adjustments under the existing rule.

## Sequencing & Dependencies

Start from latest main after S1. Build on 0098 and 0099 and keep 0100's
sRGB workflow green. Verify codecs and emitted dependency sets before
reusing their output in the GPU tests. The reopen fixture uses the
same committed root for publication and 0090. A previous compatible
build is needed for both cross-version directions.

## Verification Checklist

- [ ] **R1 — Material and catalog.** Round-trip AO with normal, AO with
  packed G/B and all three. Check exact versions, bytes for old forms,
  malformed/mismatched source, catalog, artifact and sidecar, and
  role/format refusals. Verify separate slot keys, unique dependencies,
  0100's base/emissive alias, and canonical downgrade on clear.
- [ ] **R2 — GPU.** Validate reflected seven- and eight-sampler layouts
  and both vertex layouts. In fixed IBL headless captures compare
  R=0, midrange and R=255 with other inputs fixed. Compare in linear
  HDR to verify that the change is confined to diffuse plus specular
  IBL. Exercise the normal direction, G/B and scalar interaction,
  separately varied channels and unchanged direct/shadow/emissive
  contributions. In no-IBL capture verify retained normal and packed
  shading. Check neutral AO clear for AO plus normal and the documented
  return to packed direct lighting for the packed case. Verify apply
  reuse, one-slot changes and retirement of each texture separately.
- [ ] **R3 — Clients and durable state.** Verify exact methods and
  echoes through CLI, native client, C# SDK, Editor and MCP. Exercise
  old peer capability refusal and legacy Read/Copy/Edit/Resume
  refusal on a supporting Runtime. Check compatible old active Read
  while the registry is ahead, stale Edit, and full Resume without
  source rewrite. Keep the original receipt or target when settlement
  is unknown. Verify precommit mismatch leaves source and publication
  unmodified and damaged uncertain destination is unconfirmed.
  Compare activation of the previous compatible generation and the
  new coexistence generation on the same older build/profile. Check
  a build from coexistence source fails on the old cooker. Reuse
  applicable frozen evidence fault injection and client conformance
  where the relevant source and fixtures match.
- [ ] **R4 — Preview and Windows.** Compare cooked and draft under the
  same bounded studio lighting for all three combinations. Verify
  old routes, one shared permit, absent/bad inputs, late responses,
  release after confirmed completion and retention through terminal
  failure. On Windows 100%, use standard and compact windows. Check
  independent select/replace/clear, clear and no-IBL guidance, failure
  and restart states, focus, draft/history isolation and native captures.
- [ ] **R5 — Both reopens.** On a real Windows desktop, run the
  Spec sequence using a disposable IBL scene such as
  `ibl_material_demo`. Retain the combined reference captures,
  configs and startup arguments. Compare the published
  named-generation scene and the real 0090 child Editor at matched
  inputs. Verify the complete recipe and edges in both cooked catalogs,
  unchanged ordinary and both Save As file hashes and material source
  digest, with no rebind or Apply. Verify the child uses committed
  managed sources and its independent `saved_scene` build.
- [ ] **Gates.** Run affected builds, suites, Vulkan Validation Layers
  and impacted image/golden checks. Keep the old goldens and obtain
  human review for any changed golden. Verify Android build and
  shader packaging where affected under existing platform policy.
  Keep interactive authoring acceptance on Windows.
  Apply `--gate --applies` to the final implementation diff. Where
  selected, use the existing reference protocol and baseline and keep
  raw verdicts. #340 remains deferred. Verify links, whitespace and
  planned file scope.

## Rollback Plan

Use the last compatible build and generation together. Preserve
committed coexistence source and frozen generations. For old tools,
publish a compatible recipe explicitly through a supporting client
before rebuilding from the modified source.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
Record evidence for R1–R5, applicable gates and any required human
ruling in the implementation PR.
