# ADR-0188 — On-Demand Cooked Content Metrics

- **Status:** Proposed
- **Date:** 2026-11-22
- **Deciders:** Atlantis maintainers
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0140](../specs/0140-scene-statistics-content-diagnostics.md)

## Context

The 0081 asset service exposes a process-lifetime catalog snapshot and
typed dependency edges. It does not return mesh index counts or texture
payload sizes. The existing 0131 capture report provides a whole-scene
draw-item count. Advancing a frame just to retrieve it would change the
semantics of a read-only statistics action. Current CPU artifact readers
already define the binary layout and checks needed to count the cooked
content. Reading full texture payloads for statistics would raise I/O and
memory costs substantially.

## Decision

Add one read-only shared Asset Connection method and its additive
atlantis.remote/1 route, assets.contentMetrics. Its only request member is
guids, an array of one to 32 distinct canonical non-nil asset GUIDs.
Success returns a rows array of the same length in request order, within
a 16 KiB reply budget. Each row has its guid and exactly one result.

| Row result | Fields |
| --- | --- |
| mesh | artifactSchema, indexedTriangles |
| texture | artifactSchema, width, height, layout (Rgba8 or Bc7), mipCount, pixelPayloadBytes |
| err | UnknownAsset, UnsupportedAssetType, UnsupportedArtifactSchema, UnreadableArtifact, or InvalidArtifact |

CatalogUnavailable is a whole-request refusal. Reject malformed shapes,
duplicate GUIDs, over-limit requests, invalid numbers and mismatched
reply rows. Count and byte values use exact nonnegative integers.
Existing 0081 request and response shapes and semantics are unchanged.
Existing unknown-method handling maps old hosts to an Unsupported
outcome for new clients. Old clients retain their existing behavior.
Editor, CLI, native and C# SDKs, and MCP use the same contract. MCP
continues to use the C# SDK surface.

Resolve identities via the same AssetHost snapshot as assets.list/get,
including the first-query semantics. The caller supplies GUIDs only.
Check catalog artifact paths under the established allowed root and refuse
unsafe links or paths. Obtain the fixed header and logical length from an
ordinary opened file, with no payload allocation or GPU reads. Asset System
owns byte-span and file-length validation shared with existing decoders.
Runtime owns file access; the validators need no OS or Renderer headers.
Support mesh v4/u16 and v5/u32. Validate matching schema and mesh AssetId,
index count divisibility, stride, offsets and exact length with checked
arithmetic. For texture v3, validate schema, layout, dimensions,
mip count, offsets and exact length using existing mip layout calculations.
Header and length validation establishes the metadata values. It does not
verify vertex/index/pixel contents or prove GPU readiness. Refuse detected
change during a read. Each file is an observation of a cooked artifact for
that catalog entry, not a guarantee that the physical file set is immutable
or that identically named files were uploaded into the current projection.

The shared operation supplies typed facts only. The Editor combines the
document and frozen catalog using existing queries, deduplicates by GUID
per refresh, and owns all UI intent and navigation. It reuses an already
completed frame report from the same session as historical Runtime evidence.
No new frame report fields, Runtime Control method, renderer hooks,
background poll, persistent statistics cache, or path to mutate the scene.
The RPC uses bounded request-driven work. Cancel remaining requests or
retire their replies on supersession through the existing client lifetime.

## Consequences

### Benefits

- Existing client queries and the frame loop retain their behavior.
- The same cooked measurements are available to all clients, and the
  Editor can combine them with explicit document scope.
- Material and texture dependency facts continue to come from the
  existing catalog.

### Trade-offs

- Old hosts can still supply nodes and direct checks when existing queries
  work, with cooked metrics unavailable.
- Per-file reads have a finite budget and provide partial results if
  metadata is inaccessible. Byte totals describe cooked payloads, not VRAM.
- Direct binding checks intentionally leave dependency-chain diagnosis and
  whole-scene validation to their own features.

## Alternatives Considered

Extending assets.get changes an established result contract and would make
normal Inspector browsing pay for optional I/O. Full artifact decoding
adds unnecessary reads and allocation. Frame instrumentation measures a
different workload and adds an always-on path. The bounded additive request
keeps the existing client and render paths intact.
