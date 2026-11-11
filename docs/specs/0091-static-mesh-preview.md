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
  accepts one non-nil asset GUID and defaults to `Unsupported` for existing
  native providers. `assets.previewMesh` accepts exactly
  `{guid}` on `atlantis.remote/1`; it returns an ordinary correlated result,
  with no job, event, status query, or cancellation call. Resolve the GUID as
  `Mesh` in the serving Runtime's [0081](0081-asset-browser-shared-interface.md)
  lazy catalog snapshot. A first preview may initialize it. Reuse the same
  frozen success or failure as `list/get`. Use the record's resolved cooked
  artifact and sidecar, and verify the bytes actually read against its GUID,
  asset key and artifact schema. Support the current 16-bit and 32-bit cooked
  index formats. No source path or residency requirement. Apply, Reload view,
  reconnect and publication do not replace the snapshot; switch to a newly
  published generation by restarting into it under [0085](0085-shared-asset-rebuild.md).
  Editing artifacts in place in a running build retains 0081's lack of a
  consistency guarantee.

  The owned success value contains `guid`, `width`, `height`, and PNG bytes.
  Width and height are both 256. On the wire add `mimeType: "image/png"` and
  carry the bytes as `pngBase64` using canonical padded base64. No file path,
  shared image handle or renderer handle crosses the interface. Cap decoded
  bytes at 512 KiB and encoded bytes at 699,052 characters, checking both
  lengths before decoding. A mismatched reply GUID, dimensions, MIME type,
  signature or IHDR is a protocol failure. Native and C# SDKs expose typed
  results; CLI `asset preview <guid>` returns the bounded
  image and metadata in the existing structured output and only a metadata
  summary in human output. No Runtime file write. MCP `assets.preview_mesh`
  calls the typed C# SDK and emits one PNG image block plus metadata without
  duplicating the image in text. Editor uses the C# SDK. Existing calls,
  version and transport limits stay unchanged.

  In-band errors are `Unsupported` (no preview provider), `Unavailable`
  (provider has no usable rendering context), `Busy` (admission exhausted),
  `CatalogUnavailable`, `UnknownAsset`, `NotMesh`, `TooLarge`, `InvalidMesh`
  (including missing, invalid or mismatched artifact, sidecar, layout or
  bounds) and `RenderFailed` (resource, draw/readback or PNG failure). Bad
  request shape or GUID is a request error. For a well-formed new call only,
  map an older asset-capable Runtime's exact `BadRequest` / `unknown or
  malformed asset query` refusal to `Unsupported` and leave the connection
  usable. Old clients can keep using old calls on a new Runtime. A transport
  failure stays distinct:
  the native Remote session failure is authoritative (`Unavailable` is its
  value sentinel), and the C# SDK uses its existing transport exception.
- **R2 — Fixed framing and appearance.** Render one opaque DrawItem with
  the existing unlit vertex-color fallback, built-in dark background, and
  standard output transform at zero exposure compensation. No scene lights,
  material overrides, textures, environment, fog, bloom or overlays enter
  the image. Without authored vertex colors, a glTF import generally appears
  white. Label this as geometry preview, so it does not imply how the mesh
  will appear when used in a scene.

  Frame the indexed geometry, including off-origin and planar meshes. Use
  finite referenced positions, a conservative bounds sphere, a +Y-up oblique
  view from direction `(3, 2, 4)`, a 35° vertical perspective FOV and a
  15% fit margin. Choose distance and positive clip planes that enclose the
  sphere; use existing camera math and a preview-only transform if needed to
  keep calculations representable. Refuse unusable geometry, including no
  non-degenerate triangles or an unrepresentable camera fit. Never write a
  transform back to the asset. A single fixed view can show a thin surface
  edge-on; there is no orbit or second view in this slice.
- **R3 — Isolation, bounds and lifetime.** Runtime records the render and
  readback through Renderer/RenderGraph on its owning frame thread between
  frames. Only immutable compatible pipeline/sampler/geometry inputs may be
  borrowed from the application; all preview camera/light buffers, mesh
  upload, offscreen color, depth/HDR, any required shadow attachments and
  readback are request-owned. Use a fixed RGBA8 UNORM output with the matching
  output transform. Reuse the existing RHI submission/completion rules: keep
  GPU-referenced objects alive until completion or normal device teardown,
  end the target borrow before releasing its owner, and release request
  resources on all settled paths. No retained GPU cache or scene attachment.

  Admit at most one preview attempt across Remote and `--exec` in each
  existing between-frame service interval, before preview-specific file or GPU
  work. Excess requests return `Busy` immediately; there is no host queue or
  automatic retry. After catalog resolution, bound the actual opened cooked
  artifact and sidecar reads to 32 MiB and 64 KiB respectively, before large
  allocations. These are per-preview limits; 0081 catalog initialization
  retains its existing loading behavior. Reject more than 1,500,000 indices
  before upload. All three limits are inclusive; above them return `TooLarge`.
  The fixed image, input and admission limits bound work per interval, not
  wall-clock latency.
  A request may briefly stall rendering or other Runtime requests during
  synchronous upload/readback. Preview must create no per-frame work in
  `runFrame()`, no new startup GPU allocations and no extra submission when
  unused. Only already-enabled asset dispatch may reset the admission budget.

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

  Use a separate ordinary SDK connection to the same Runtime, opened lazily
  and owned by the preview worker, so a preview transport failure does not
  poison the edit session. Keep at most one sent request and one replaceable
  latest explicit intent per Editor, including across reconnects. Ignore a
  duplicate action for the same GUID and current epoch. Guard both success
  and failure with the parent session
  identity, selected GUID and request epoch. A new selection, deselection,
  filter that hides the selection, Reload view, leaving Assets, disconnect or
  shutdown invalidates the image and any unsent intent. A late response never
  restores them; the sent request may finish. Send a still-current newer
  intent only after settlement. Release replaced or discarded image resources.
  On session end or window close, invalidate locally at once and have the
  worker dispose its connection after any call settles or times out, without
  blocking the UI. A retry
  after transport failure uses a new preview connection to the current
  Runtime, without reconnecting the editing session. Reload view continues
  to use the same Runtime snapshot and does not automatically render again.

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
