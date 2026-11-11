# Spec: Read-Only Mesh and Material Preview

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-12
- **Related Plan:** [Plan 0092](../plans/0092-mesh-material-preview.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0152](../adr/0152-shared-mesh-material-preview.md) (Proposed)

## Summary

Render one cooked mesh with one explicitly chosen cooked material in a
read-only still. Assets can inspect a pair before use, and the Inspector
material chooser can inspect a candidate against the mesh read when it opened.
The same operation serves Editor, CLI, both SDKs and MCP.

## Motivation / Problem Statement

[Spec 0091](0091-static-mesh-preview.md) shows geometry and vertex colors.
The [typed reference chooser](0084-asset-reference-target-types.md) filters
references by asset type. A user still has to edit and apply the scene to
see how a specific material looks on a specific mesh.

## Goals

- Show the selected pair under repeatable light and camera settings.
- Explain missing dependencies, unsupported materials and failed combinations.
- Keep preview independent of scene editing, apply and undo history.

## Non-Goals

Scene appearance prediction, general apply validation, material editing,
automatic thumbnails, camera controls, environment lighting, shadow casting,
additional material variants, new asset formats, and Android hosting.

## Requirements

### Functional

**R1. Shared result and snapshot.** Add `AssetConnection::previewMeshMaterial`
with default `Unsupported` and wire method `assets.previewMeshMaterial` on
the existing protocol. Its parameters are exactly
`{"meshGuid":"<GUID>","materialGuid":"<GUID>"}`, both canonical and non-nil.
Resolve both and their dependencies from the serving Runtime's same retained
catalog as list, get and geometry preview. Retain its full records whenever
pair preview is served, including with rebuild or geometry preview disabled.
Retain the first catalog success or failure. Refresh, apply and reconnect
keep that snapshot. A published generation takes effect on explicit restart
into that generation.

Success is `result: {ok: {meshGuid, materialGuid, width, height, mimeType,
pngBase64}}`. Refusal is `result: {err: <token>}`. Preserve the existing
response correlation and mutually exclusive result/error and ok/err arms.
Reject repeated response IDs, envelope arms and required image members
for this operation. Match each GUID to its requested role. Apply 0091's
PNG signature, IHDR, `image/png`, 256 × 256 extent and canonical Base64 checks, including
the 699,052 encoded-character and 512 KiB actual decoded limits before
allocating decoded bytes. Return owned bytes. Map the older asset-capable
Runtime's refusal to `Unsupported` only for a single top-level error with
code `BadRequest`, message `unknown or malformed asset query` and no
`result`. The code and message must each occur once. The same session keeps
list, get and any geometry preview support that Runtime has. Malformed or
lost replies follow existing protocol or transport handling. The native
Remote session failure is authoritative over its `Unavailable` sentinel.

Expose CLI `asset preview-material <mesh-guid> <material-guid>` with
structured PNG output under `--json` and a metadata summary in human output
and `--exec`. The native SDK exposes `previewMeshMaterial` and the C# SDK
exposes `Assets.PreviewMeshMaterial`. MCP
`assets.preview_mesh_material` uses the typed C# SDK and returns one PNG
image block and metadata. Keep 0091's geometry request and response intact.

**R2. Eligible pairs and dependencies.** Support a static mesh accepted by
0091 and a current cooked `PbrDirectLit` material with `Opaque` alpha, one
sRGB RGBA8 base-color texture, and no authored normal map or emissive texture,
with zero emissive RGB. Honor the decoded sampler, base-color, metallic and
roughness parameters and the existing valid mip layout. Use cooked mesh
positions, UVs and normals through the existing layout and decoder. A mesh
that cannot supply that layout or be framed is an invalid pair. A type
match and a successful preview say only that the pair renders in this recipe.

Validate the requested mesh with the 0091 loader. For the material and
base-color texture, check the GUID and key in each sidecar against the
resolved record, the actual artifact header schema against the catalog, and
the decoder's artifact/sidecar field agreement. Check that the validated
material is in the supported kind, alpha and slot subset before resolving
or opening any texture dependency. Resolve its texture key by AssetId within
the same catalog. The Texture record must be the sole direct dependency
declared by the material. The texture must have no declared dependencies.
The material and texture artifact formats carry no own identity or
content hash. The check relies on
the sidecars and the fields the current formats can cross-check.

Use 0091's actual opened-file mesh and index limits. Bound actual opened
material artifacts and each sidecar to 64 KiB, base-color texture artifacts
to 32 MiB and base-color dimensions to 2048 × 2048 per axis, inclusive.
Reuse 0091's bounded read on one opened handle, including the exact read
and EOF check. Check texture dimensions from the artifact header before
allocating decoded pixels or uploading. Use the existing mip
validation and accept at most the one declared texture. Resolve trusted
catalog paths only. There are no source-file reads or scene-residency
requirements.

Use closed refusal tokens `Unsupported`, `Unavailable`, `Busy` and
`CatalogUnavailable` with the 0091 meanings. Use `UnknownMesh` or
`UnknownMaterial` for absent GUIDs, `NotMesh` or `NotMaterial` for a
record of the wrong type, and `InvalidMesh` or `InvalidMaterial` for
unreadable, malformed or mismatched root artifacts or sidecars. A valid
material kind, alpha mode, optional map or emissive factor outside the
recipe returns `UnsupportedMaterial`. The same token covers a valid
base-color texture with another color space or layout. An inconsistent
dependency list or a missing, unreadable, corrupt, mistyped or mismatched
required texture returns `DependencyUnavailable`. A decoded mesh that fails the preview layout or
framing check returns `IncompatiblePair`. Use `TooLarge` for input limits
and `RenderFailed` for recoverable GPU, readback or PNG failure. Invalid
parameters are outer `BadRequest`. Show the refusal and an actionable hint
in Editor and human CLI output, such as selecting a supported material,
repairing the cooked dependency or retrying a busy Runtime. A refusal
produces no image and does not fall back to geometry.

**R3. Fixed image and ownership.** Keep 0091's position normalization,
oblique camera, dark background, 256 × 256 RGBA8 UNORM target and zero
exposure compensation. Use the existing direct-lit PBR shader with the
`Rgba16Float` intermediate and existing output transform. Supply one
white directional light at intensity `3.0` whose rays travel along
`-normalize(4, 5, 3)` in normalized preview space.
Fill the full existing camera block with the preview view and projection,
their corresponding eye position, the fixed light, zero fog and cluster
geometry from `buildClusterLightData` with the preview camera and zero
point lights.
Use the existing light-space matrix helper for the fixed light and an
empty-caster shadow pass clearing a private 1 × 1 depth target to 1.
Bind the request's base texture, shadow target and white emissive texture
with zero emissive contribution.
Do not incorporate the scene's environment, camera, fog, bloom, overlays
or light state.

Both preview methods share the one synchronous permit across each
existing Remote poll and following `--exec` step. The first well-formed
request to a usable provider consumes it before catalog or file work, even
if it fails or its client disconnects. Malformed requests and unavailable
providers take no permit and perform no catalog work. Later eligible
requests return `Busy` before loading anything. Retain 0091's frame-thread
ownership. Reuse existing texture upload, Renderer and RenderGraph readback
paths. The request owns its mesh, base
texture, upload staging, sampler, uniforms, attachments and the pipelines
with mutable descriptor sets. Reuse material realization without using
scene resource maps. Use request-owned scratch for the white emissive
binding, with the existing upload pattern. Immutable fullscreen
geometry and samplers already shared by 0091 may be borrowed. Transition
the uploaded textures to `ShaderRead` before sampling. Record upload,
draw and readback in one between-frame submission. Release scratch on a
recoverable pre-submit refusal. Confirm completion before reading pixels or
freeing GPU-referenced resources, ending the RenderTarget borrow before
releasing its OffscreenTarget. On fatal submit or completion failure or
device loss, follow 0091's stop and terminal drain and surface `Unavailable`.
Keep the entire potentially referenced bundle, including upload staging
and the offscreen borrow, until the drain. Preview leaves scene and normal
frame state intact.

**R4. Explicit Editor entry points.** Assets retains geometry preview.
With a visible selected mesh, offer a local material selector drawn from
the full ready catalog and a separate **Preview with material** action.
Name the mesh and material being previewed. A material selection is local
to the panel and starts no request. The chooser entry applies only to the
schema-identified `Renderable.materialAsset` field. Capture the same
node's non-nil authoring `Renderable.meshAsset` from the Inspector refresh
that opens the chooser. Label this ordinary read as the opening mesh and
hold it fixed until reopen, including if the sibling value later changes.
The full ready catalog must contain that Mesh, and the current visible
candidate must be a Material. Missing or unreadable mesh values and
ineligible rows only disable preview. Session failure follows existing
chooser invalidation. Keep the existing candidate filtering, draft and
focus handling, Confirm, Cancel, apply and undo/redo rules. Confirm remains
available under its existing guard independently of preview, including
when target-type metadata is absent or a preview is still in flight.

Extend the 0091 preview intent guard to include mode, both GUIDs where
applicable, surface or chooser instance, visible input and session epoch.
Assets and the modal chooser share one active slot and one replaceable
latest explicit intent per Editor, using the separate ordinary SDK
connection from 0091. The modal owns preview display while open, even if
the Assets tab remains selected. Opening or leaving the modal invalidates
the previous surface's intent and image. Changes to either input, filtering
away the candidate, Reload view, invalid target, submission of Confirm,
disconnect and close invalidate the affected intent and clear its image.
Serialize post-connect send admission with invalidation. Check the same
intent and live surface before decoding or displaying success or failure,
including when the same pair was chosen again after reopening.
Dispose replaced images and let admitted work settle on its worker.
Retry is explicit on a fresh connection, without resetting the editing
session or waiting during close. Do not auto-preview on selection,
reload or return from the chooser.

### Non-functional

The first release uses the existing Windows Runtime and Editor, current
Vulkan backend and cooked formats. It adds no dependency, startup GPU
work, idle submission, persistent cache, new transport limit or preview
timing threshold. A synchronous preview can delay other Runtime work.
Use the existing client response timeout. A timeout does not cancel
admitted work.

## Proposed Design

AssetHost admits the explicit pair and resolves its single direct texture.
Runtime builds an isolated direct-lit preview using the 0091 framing and
readback path, with existing material realization and texture upload
building blocks. The Editor's two surfaces consume the same typed image
result and share an intent guard.

## Architectural Impact

[ADR-0152](../adr/0152-shared-mesh-material-preview.md) records the additive
shared API, supported recipe, dependency authority, and reuse of the
preview execution and ownership boundary. It extends ADR-0151 for this
operation. Asset System remains CPU-only, and Renderer and RHI contracts
stay unchanged.

## Alternatives Considered

See [ADR-0152](../adr/0152-shared-mesh-material-preview.md#alternatives-considered).

## Testing & Verification Plan

[Plan 0092](../plans/0092-mesh-material-preview.md#verification-checklist)
maps contract, fixture, editor and lifetime checks to R1–R4. Verify one
new headless golden under ADR-0042, windowed Vulkan validation and the
existing performance gate where the final implementation diff selects it.

## Risks & Open Questions

Joint review settles the eligible material subset, fixed light and limits,
the chooser's fixed mesh input, and the proposed verification. A supported
material can look different under scene lighting. Catalog facts remain
fixed for the Runtime, while local replacement can still invalidate
individual cooked files.

## Out of Scope / Future Work

Other material variants and texture slots, preview from the mesh chooser,
light or camera controls, and full scene compatibility checks can be
considered as separate slices.
