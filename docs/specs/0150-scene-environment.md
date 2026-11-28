# Spec 0150 — Scene Environment Lighting and Reflections V1

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-28
- **Design baseline:** `79c061a3` on `main`, maintainer merge of [PR #462](https://github.com/slmao/Atlantis/pull/462)
- **Related Plan:** [Plan 0150](../plans/0150-scene-environment.md)
- **Related ADR:** [ADR-0196](../adr/0196-scene-environment-selection-and-activation.md) (Accepted)
- **Approval:** [Maintainer approval of `36e87142`](https://github.com/slmao/Atlantis/pull/463#issuecomment-7236980882).

## Summary and motivation

Give a hand-authored scene a saved choice of an existing cooked environment. Use a
bookmarked view to compare how changing that choice affects the background, a rough
dielectric surface, and a smooth reflective surface. The first release exposes one
choice for the entire environment. The lighting workbench, post-processing panel,
look presets, and bookmarks keep their current parameter ownership.

## Existing rendering paths

The asset catalog already registers **Studio** (repository-authored) and
**Warehouse Interior** (Poly Haven, CC0, with attribution in the
[asset provenance](../../assets/environments/warehouse_interior_source.provenance.txt)).
The existing cook produces a prefiltered HDR cubemap, nine irradiance spherical
harmonics coefficients, and a split-sum BRDF (DFG) lookup texture in one `.aenv`
and its sidecar. The desktop executable currently selects Studio through
`BootstrapConfig`; the built-in Bistro entry deliberately leaves it unset.
Android packages Studio as its default. Warehouse is cooked through the existing
catalog pipeline and exercised by the material showcase image suite. The
ordinary desktop selector does not currently select Warehouse as the environment.

| Path | Inputs in the existing renderer |
|---|---|
| Sky | The environment cubemap at mip 0 in the world-space camera-ray direction, using camera orientation and projection. It draws over uncovered pixels. |
| Diffuse environment lighting | The package's nine irradiance coefficients evaluated along the shading normal, modulated by base color and the material's diffuse reflectance. |
| Specular environment reflection | The same cubemap sampled along the reflection vector at a roughness-dependent mip, the DFG table and material Fresnel. The normal, view direction and material parameters shape the result. |

The IBL-capable PBR variants also evaluate direct light separately. Where the
material uses AO, its existing IBL attenuation still applies. Fog, exposure and
Bloom keep their current pass inputs and can change the final appearance of these
signals. The Studio environment for the isolated material preview remains on its
existing independent path. See [preprocessing](../adr/0069-environment-asset-preprocessing-and-ownership.md),
[IBL bindings](../adr/0070-ibl-frame-binding-and-cubemap-resource-contract.md), and
[sky integration](../adr/0071-visible-sky-background-rendering-integration.md).

## Goals and scope

### R1 — Select and persist one environment for a hand-authored scene

Offer one **Scene Environment** control showing **Inherit startup default** or an
existing supported environment from the connected Runtime's asset catalog.
List the label and GUID and show the currently effective environment separately.
Reuse `assets.list/get` and catalog type `environment`. The initial choices are
Studio and Warehouse Interior. Source HDR files, external paths, and unfinished
assets are ineligible. An unsupported host or imported scene has no write control.

One explicit confirmation commits one guarded authoring transaction and one
Editor undo entry when the value changes. It guards the exact document occurrence
and revision and the catalog scope from which the choice was made. A stale
selection, a wrong type, or an unavailable catalog entry is rejected with zero
writes and no Apply. Undo and Redo use the same guard against external edits
and the recorded old/new choice. A no-op creates no history or Apply. Pending
input, changed documents and late responses follow existing Inspector protection.

Persist the choice as a scene-level optional asset GUID in the formal hand-authored
source and bake it into the scene artifact. Save and Save As use the existing
write flow, recovery captures the value, and a Prefab subtree carries no scene-level
environment. Old sources and cooked scenes with no choice inherit the same
launch default as today. Returning to Inherit removes the override. The
[ADR](../adr/0196-scene-environment-selection-and-activation.md) defines version
and shared-client compatibility.

### R2 — Resolve it against the running or newly built scene's catalog

An explicit valid scene choice takes priority over the startup default. Inherit
uses the startup configuration, including the no-environment default for the
built-in Bistro sample. Reuse the cooked catalog and loader with type, schema,
identity, bounds and artifact/metadata validation. Emit the selected GUID and a
diagnosable reason when explicit resolution fails. Startup and Saved Scenes
Build/Open must surface missing or invalid dependencies through existing build or
launch diagnostics. A failed explicit choice never renders with the default as a
substitute. The last valid running projection remains available on recoverable
live failures.

### R3 — Distinguish document commit, visible Apply and save

Apply can switch between valid environments when the running projection and
requested projection both use IBL, including Inherit when its default is an
environment. It uses the same normal shader family, existing RenderGraph upload
and the existing atomic reload point. The settled projection, sky, diffuse
coefficients and reflection bindings advance together. Any failed preparation or
pending GPU work prevents a new settled projection. Repeated Apply with the same
effective choice reuses the settled resources.

A crossing between no IBL and IBL requires Save and an explicit independent
Saved Scenes Build/Open in V1. Show that requirement separately from **Applied**
and keep the previous generation. A recoverable Apply failure preserves the
committed edit and history. After fixing the cause the user can explicitly Apply
or use Undo to restore the previous choice. Save stores the document choice and
does not advance the running projection. Receipt loss uses the existing
unknown-outcome/readback flow with no automatic retry.

### R4 — Compare the settled output at one bookmark

Use [0147](0147-scene-view-bookmarks.md) ordinary bookmark captures after each
settlement. Keep one camera pose, projection and output extent and record the
applied revision, effective environment, direct light, material identities,
exposure, fog and Bloom for the compared frames. Correlate the readback with
the capture generation and reject evidence from a changed projection. Existing
render look presets
continue to own lights and camera fields. The two environment images can be
reviewed with the existing local reference PNG comparison. The observation does
not modify scene state, history or bookmarks.

## Architectural impact

[ADR-0196](../adr/0196-scene-environment-selection-and-activation.md) records
scene-format and shared authoring changes, startup/Apply precedence, and the
ownership of the active and candidate resources. Asset System owns CPU data and
the stored reference. Runtime resolves, realizes and activates it through the
existing Renderer and RenderGraph. The Renderer, RHI, backend, threading model,
shaders and lighting equations keep their existing contracts.

## Alternatives and limits

A startup path setting cannot travel with a saved scene. Editor-only storage
would also miss the independent Build/Open path. Three independent controls
would create unsupported combinations of sky and IBL inputs. V1 leaves HDR
import, separate strength/orientation controls, a sky-only or reflection-only
switch, local probes, environment blending, and new light transport out of scope.
The proposal limits live changes to the already-loaded IBL pipeline family.

## Verification

[Plan 0150](../plans/0150-scene-environment.md) covers a controlled Windows 100%
comparison, explicit independent Build/Open, shared client compatibility, and
failure and resource lifetime checks with the applicable gates.
