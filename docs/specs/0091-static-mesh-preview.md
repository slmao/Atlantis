# Spec: Read-Only Static Mesh Preview in Assets

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-11
- **Related Plan:** [Plan 0091](../plans/0091-static-mesh-preview.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0151](../adr/0151-shared-offscreen-mesh-preview.md) (Proposed)

## Summary

Add an explicit **Preview geometry** action for one selected cooked mesh in
the Windows Editor's Assets panel. It returns a still image through the same
read-only Runtime operation available to CLI, both SDKs and MCP.

## Motivation / Problem Statement

[Asset discovery](0081-asset-browser-shared-interface.md) identifies a mesh but
does not show its shape. [Adding a mesh](0087-add-mesh-to-scene.md) is an edit
and can trigger apply and reload. The existing Runtime capture shows the current
scene. Neither is a suitable way to inspect a mesh before using it.

## Goals

- Show the shape and cooked vertex colors of one mesh, even if the open scene
  does not reference it.
- Keep scene state and the asset snapshot independent of preview work.
- Reuse the existing Renderer, RenderGraph, offscreen target and capture codecs.

## Non-Goals

Original material or texture fidelity, lighting controls, camera interaction,
animation, import-source preview, other asset types, batch thumbnails, caching,
streaming, an Inspector preview, and new Android hosting or Editor UI.

## Requirements

### Functional

- **R1 — Shared, snapshot-bound operation.** `AssetConnection::previewMesh`
  accepts one non-nil asset GUID and has a default `Unsupported` for existing
  native providers. `assets.previewMesh` accepts exactly `{guid}` with the
  existing canonical GUID spelling on `atlantis.remote/1`. Resolve it as a
  `Mesh` in the serving Runtime's [0081](0081-asset-browser-shared-interface.md)
  lazy catalog snapshot, shared with `list/get` regardless of whether rebuild
  is enabled. Any of these calls can initialize it; the first successful or
  failed initialization is retained. Validate the cooked artifact and sidecar
  bytes actually read from the resolved record against its GUID, asset key and
  schema. Support both cooked index widths without requiring scene residency.
  Apply, Reload view, reconnect and publication do not replace the snapshot.
  Use a newly published generation by restarting into it under
  [0085](0085-shared-asset-rebuild.md). The snapshot freezes records and
  resolved paths; it does not promise stable bytes for files modified in place.

  Return one correlated response with `result: {ok: {guid, width, height,
  mimeType, pngBase64}}` or `result: {err: <token>}`; the alternatives are
  exclusive. There is no job, event, status query or cancellation call.
  Width and height are 256; `mimeType` is `image/png`. The server bounds the
  PNG to 512 KiB. Clients validate canonical padded base64, an encoded length
  of at most 699,052 characters and the actual decoded length implied by
  padding before allocating a decoded buffer. Require the requested GUID,
  matching dimensions, MIME type, PNG signature and a first chunk of type
  `IHDR` with a 13-byte body and matching dimensions. Invalid replies are
  protocol failures. SDKs return owned bytes and metadata; complete image
  decoding belongs to the consumer.

  `atlantis --json asset preview <guid>` returns image and metadata in the
  existing structured output; human output, including `--exec`, summarizes
  metadata. MCP `assets.preview_mesh` calls the typed C# SDK under its
  existing session guard and emits one PNG image block plus metadata. Editor
  uses the same SDK. No path, shared handle or Runtime file write. Existing
  calls, version and transport limits stay unchanged.

  In-band errors are `Unsupported` (no preview provider), `Unavailable`
  (provider has no usable rendering context), `Busy` (admission exhausted),
  `CatalogUnavailable`, `UnknownAsset`, `NotMesh`, `TooLarge`, `InvalidMesh`
  (including missing, invalid or mismatched artifact, sidecar, layout or
  bounds) and `RenderFailed` (recoverable resource, readback or PNG failure,
  including oversized encoder output). Bad request shape or GUID is a
  request error. For a well-formed preview only, map an older asset-capable
  Runtime's exact top-level `BadRequest` / `unknown or malformed asset query`
  refusal, with no `result`, to `Unsupported` before generic error handling.
  The connection stays usable for `list/get`. Old clients keep using old
  calls on a new Runtime. For protocol or transport failure, the native
  Remote session failure is authoritative (`Unavailable` is its value
  sentinel); the C# SDK uses its existing exception path.
- **R2 — Fixed framing and appearance.** Render one opaque DrawItem with
  the existing unlit vertex-color fallback, built-in dark background, and
  standard output transform at zero exposure compensation. No scene lights,
  material overrides, textures, environment, fog, bloom or overlays enter
  the image. Without authored vertex colors, a glTF import generally appears
  white. Label this as geometry preview, so it does not imply how the mesh
  will appear when used in a scene.

  Frame the indexed geometry, including off-origin and planar meshes. Use the
  midpoint and half-diagonal of the AABB of referenced positions, computed
  in double precision. Recenter and scale referenced positions to unit radius
  in preview-owned vertex data before using the existing float camera math;
  preserve vertex colors and index order. Use a +Y-up view from direction
  `(3, 2, 4)`, a 35° vertical FOV at aspect 1 and a 15% sphere margin: for
  padded radius `1.15`, use distance `1.15 / sin(17.5°)` and near/far at
  distance minus/plus the padded radius. Reject nonfinite data, geometry
  with no non-degenerate 3D triangle after normalization, or a fit with
  invalid matrices or clip planes.
  Never write a transform back to the asset. A fixed view can show a thin
  surface edge-on; there is no orbit or second view.
- **R3 — Isolation, bounds and lifetime.** Runtime records the render and
  readback through Renderer/RenderGraph on its owning frame thread between
  frames. The request owns the mesh and upload data, camera/light uniforms,
  offscreen color, depth/HDR, shadow resources, readback and all pipelines
  that bind preview resources. Vulkan pipelines own descriptor sets mutated
  during recording: create private fallback material, shadow and output
  pipelines even if scene pipelines are format-compatible. Reuse shader
  binaries, reflection layouts and read-only inputs such as samplers and
  fullscreen geometry. Use a fixed RGBA8 UNORM target and matching output
  transform. The existing shadow pass gets an empty caster span and private
  attachment and uniform. Do not present a preview target.

  Confirm completion before reading pixels or releasing GPU-referenced
  objects, and end the offscreen borrow before releasing its owner. Release
  resources on success and on recoverable failure. Use the Runtime's fatal
  path for a device submission or completion error, or device loss. Stop
  frame and preview work; retain unconfirmed submitted resources through the
  existing shutdown drain and release them in terminal teardown before
  destroying the device. Do not read pixels or offer same-session retry
  after a fatal failure. There is no retained GPU cache or scene attachment.

  One service interval covers the whole existing Remote poll and the
  following `--exec` step, if enabled. Reset one shared permit at its start,
  keeping the existing service order. The first well-formed request to a
  usable preview provider consumes it before lazy catalog initialization or
  file/GPU work and holds it for that interval even if it fails. Subsequent
  eligible requests get `Busy`; there is no host queue, fairness guarantee
  or automatic retry. A lost reply is not replayed. After catalog resolution,
  bound the actual opened cooked artifact and sidecar reads to 32 MiB and
  64 KiB respectively, before large allocations. These are per-preview
  limits; 0081 catalog initialization
  retains its existing loading behavior. Reject more than 1,500,000 indices
  before upload. All three limits are inclusive; above them return `TooLarge`.
  Synchronous upload/readback can delay rendering and other Runtime requests;
  these limits give no wall-clock guarantee. Preview adds no work to
  `runFrame()`, no new startup GPU allocations and no extra submission when
  unused. Transport I/O and queue limits remain unchanged.

  Do not call `control.step`, change pause or step state, query or edit World
  for the mesh, switch Runtime scene or generation, or write active render
  resources. Preview does not affect document revision, dirty state, drafts,
  selection in the scene, undo/redo, or pending apply/recovery. Normal frames
  and unrelated client operations keep their existing behavior. Recoverable
  preview failures leave the serving session usable; device loss follows
  existing Runtime failure/teardown rules.
- **R4 — Editor state and retry.** Enable Preview geometry for one currently
  visible `Mesh` selection with a connected Runtime; selecting, filtering or
  browsing alone never starts a preview. An explicit action enters Loading and
  clears the previous image/error. Show the current result or a diagnostic
  with explicit Retry where appropriate. For `Unsupported`, explain that an
  updated Runtime is needed. For `CatalogUnavailable`, use the existing
  restart guidance. A bitmap decode failure after a valid wire header is a
  retryable display failure. Failure leaves catalog details and editing
  controls intact.

  Use a separate ordinary SDK connection, opened lazily on its owning worker
  from the current edit session's fixed connection details. This isolates
  preview transport errors from the edit connection. Keep one active slot
  (including connection setup and reply wait) and one replaceable latest
  explicit intent per Editor, even across reconnects. Ignore duplicate
  actions for the same GUID and current epoch. Recheck the parent session,
  epoch and visible selection at send admission after connection setup,
  serialized with invalidation. Recheck them before decoding or displaying
  a result or error. Selection change, deselection, a filter that hides the
  selection, Reload view, leaving Assets, disconnect or shutdown invalidates
  the image and any unsent intent. A request already admitted may finish;
  discard stale output and admit only a still-current newer intent after
  settlement. Release discarded or replaced image resources.

  On session end or window close, invalidate locally at once. The owning
  worker disposes its connection once setup or the call has settled or timed
  out, without a UI wait. Transport failure releases the slot; explicit
  Retry opens a fresh preview connection without reconnecting the editing
  session. Reload view keeps the same Runtime snapshot and does not render
  again automatically.

### Non-functional

- Windows Editor and Windows Runtime serve the first release. No new
  dependency, shader, Vulkan extension or change to Android hosting.
- No preview data is persisted. Use the existing response timeout. A client
  can discard obsolete output; it cannot abort GPU work already admitted.

## Proposed Design

The executable's Assets host resolves and admits requests; a Runtime-owned
offscreen helper renders, reads back and encodes the result. Reuse the capture
building blocks, without invoking the scene-specific `captureImage()`.
Reuse `assets.list/get` for discovery; no authoring call is part of preview.

## Architectural Impact

[ADR-0151](../adr/0151-shared-offscreen-mesh-preview.md) records the additive
shared operation, the Runtime ownership and execution boundary, and the
transport and lifetime limits. It extends ADR-0143 and ADR-0106 for this new
operation only. No new module or change to Renderer/RHI public contracts.

## Alternatives Considered

See [ADR-0151](../adr/0151-shared-offscreen-mesh-preview.md#alternatives-considered).

## Testing & Verification Plan

[Plan 0091](../plans/0091-static-mesh-preview.md#verification-checklist) maps
each requirement to contract, framing, lifetime, Windows Editor and GPU
checks. Use one headless golden from the production preview path and run the
existing performance gate when selected by the final implementation diff.

## Risks & Open Questions

Joint review settles appearance fidelity, limits and synchronous admission
trade-offs. A large or slow request can still delay a frame, and a fixed
unlit view does not show shading detail. This design adds no timing policy.

## Out of Scope / Future Work

Revisit interactive previews, authored look, and reusable thumbnails only in
a separately reviewed slice.
