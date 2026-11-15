# Spec: Emissive Texture Import and Binding

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0100](../plans/0100-emissive-texture-import-and-binding.md)
- **Approval:** Pending
- **Related ADRs:** [ADR-0096](../adr/0096-emissive-texture-always-bound-slot.md), [ADR-0154](../adr/0154-managed-base-color-texture-import.md), [ADR-0155](../adr/0155-pbr-material-map-authoring.md) (Accepted, reused)

## Summary

Make the existing sRGB PNG registration discoverable as an input to the
emissive slot. An operator imports one image in Assets, activates its
generation, binds it on a managed opaque PBR material, previews and publishes
the edit, then sees it on the original scene and a saved copy after explicit
reopen.

## Motivation / Problem Statement

[Spec 0095](0095-texture-import-and-base-color-binding.md) registers a managed
sRGB PNG under a stable texture GUID. [Spec 0096](0096-pbr-texture-editing.md)
already accepts its cooked format in the emissive slot and exposes linear
RGB factors, Clear, and draft PNG preview. The import choice is named
"Base color" and registered color textures can all appear as "source".
That obscures a supported workflow and makes multiple imports hard to tell
apart. Existing emissive rendering and material tests cover parts of the
workflow. Acceptance here connects registration to a reopened saved scene.

## Goals and Non-Goals

Complete a single-texture, single-material workflow using the existing
managed Windows project and asset worker. Keep the 0093 visual system and
ordinary material editing. Dedicated emissive import roles, new formats or
color transforms, new shader or lighting behavior, new slots, texture
replacement on disk, batch imports, and node editing are out of scope.

## Requirements

### R1. Import and discovery

Use the current color selection in Assets with the label **sRGB color**
and a short note that the result can serve Base texture or Emissive texture.
The existing logical name points to `inbox/<name>/source.png`. Reuse the
0095 RGB/RGBA 8-bit PNG checks, 1–2048 pixels per axis and 24 MiB encoded
limits, and the one-mip RGBA8 sRGB artifact. Inputs carry sRGB color samples.
The registration source key remains `content:texture_<name>/source.png`.
The same name, GUID, producer evidence, source bytes and receipt recovery
rules apply to both uses.

After explicit activation, use the registered logical name as the primary
label for canonical managed color entries in Assets and material choices,
with source and GUID available in existing details. Canonical naming guides
presentation. The retained catalog and its cooked artifact determine
slot eligibility. Preserve existing catalog choices outside this
presentation change.

### R2. Binding and rendering

Use the existing Emissive texture slot on editable opaque `PbrDirectLit`
recipes. Users can select, replace or clear the GUID. The shared provider
requires a supported dependency-free RGBA8 sRGB Texture in the active
snapshot. An existing valid color GUID can serve both Base texture and
Emissive texture, with one catalog dependency edge. Clearing the emissive
binding retains an edge still used by the base slot. Preserve the base
texture, sampler, other supported slots and their existing compatibility rules.

The shader multiplies sRGB-decoded RGB by the existing per-channel linear
emissive factor and adds the result at the current emission composition
point. Texture alpha has no emissive or opacity effect. Factor channels
remain finite in [0, 65504]. All-zero factors produce no emission with
any texture. Binding keeps the current factor. Clear removes the GUID and
keeps that factor, so the white fallback produces uniform emission when
the factor is nonzero. Retain the material dialog's guidance for these
two cases.

Keep the existing mesh and complete-draft preview routes, camera, sampler,
lighting selection, PNG bounds and error mapping. Compare draft and cooked
images under like-for-like lighting, since current draft and cooked routes
can select different lighting. Reuse the retained catalog, shared permit,
request-local GPU resources, terminal drain and stale completion checks.
Preview does not write the recipe, change live shading or touch the scene's
projection or undo history.

### R3. Shared clients and recovery

Editor, CLI, both SDKs and MCP use the existing shared color import/status,
catalog, material read/save/status and preview contracts. Use an existing
recipe version that represents every field in the original and draft.
The full recipe settled in [Spec 0099](0099-occlusion-texture.md) covers
the current fields. Existing narrower clients can read and write their
representable subset. A legacy version that cannot represent the source
or original recipe keeps its existing refusal, including on Resume or
an Edit that would otherwise clear the unrepresented field. Preserve the
current precise unsupported mapping. Transport failures retain their
original category and are not a reason to downgrade and replay a write.

Registration and Save & Publish are separate jobs. Reuse the current
permission, project ownership, worker, committed-source and frozen
generation checks, including target-directory verification when
publication is uncertain. A clean precommit failure permits explicit
retry after correction. If publication fails after registration or
material source commit, preserve the GUID and committed bytes. Use the
existing matching color import Resume or material Resume after settlement
or workspace repair. Keep known receipt IDs and targets on an unknown
outcome. A missing or evicted receipt does not settle it. Reconcile the
durable state before retry.

Successful publication leaves the running catalog, scene document,
projection and undo history in place. Show the existing explicit
activation guidance. Material Edit preserves identity. The current
saved scene needs no rebind, Apply or scene save to pick up the asset after
restart.

### R4. Reopen and observable result

Use one isolated, opted-in Windows checkout with an ordinary scene
referencing the managed material GUID before the seed and staging
configuration. After activating the imported texture generation, create a
saved copy with Save As before changing the material. Publish an in-place
material edit with the new texture and visibly nonzero linear RGB factors.
Use a fixed camera and a matched unbound control.

1. Reopen the original ordinary scene using its profile and the named
   material generation. Verify both asset GUIDs, the exact recipe and
   dependency edge, and the visible emissive pattern without a scene edit.
2. In the ordinary Editor, explicitly select the saved copy and same
   managed import root in [Saved Scenes](0090-saved-scene-build-open.md).
   Build and Open in a second Editor. The saved slot cooks the committed
   inputs and launches with `--scene saved_scene` under 0090. Compare the
   fixed-camera result against the ordinary named-generation result. Keep
   scene hashes unchanged, scene identities distinct, and node/material
   references, texture GUID and material source digest consistent. Both
   legs show the same committed effect without rebind or Apply.

## Architectural Impact

None. The visibility change in Assets and material choices stays in the
existing Editor views. Asset identity and publication stay with ADR-0154
and the accepted managed-generation rules. The shared material and preview
contract and slot semantics follow ADR-0155 and ADR-0096. No ADR revision
or new ADR is needed.

## Alternatives Considered

A dedicated emissive role would split one interchangeable color asset
into two registrations and introduce another client and recovery surface.
A documentation-only change would still leave ambiguous rows in the
material chooser. Reusing the color choice and showing the registered
name addresses both points in the existing flow.

## Testing & Verification Plan

[Plan 0100](../plans/0100-emissive-texture-import-and-binding.md#verification-checklist)
maps R1–R4 to Windows interaction at 100%, shared-client and recovery
evidence, bounded image/preview checks and both reopen paths. Apply the
existing Vulkan, golden and performance selection rules for the final
implementation diff.

## Risks & Open Questions

The default zero factor can make a valid import appear inert. Review the
wording and the matched zero, textured and white-fallback controls.
Review the shared-version and alias cases and the two independent reopen
legs in the same joint decision.
