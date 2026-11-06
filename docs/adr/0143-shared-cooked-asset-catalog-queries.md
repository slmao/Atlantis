# ADR 0143: Shared Cooked Asset Catalog Queries

- **Status:** Accepted
- **Date:** 2026-11-05
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-11-05; [joint ruling](https://github.com/slmao/Atlantis/pull/325#issuecomment-6660593240), J1–J4 as recommended at `5126f8f`; #325 manually merged.
- **Related Spec:** [Spec 0081](../specs/0081-asset-browser-shared-interface.md), R1–R5
- **Related ADRs:** Extends [ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md),
  [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md),
  [ADR-0110](0110-gameplay-sdk-client-library-and-execution-model.md),
  [ADR-0112](0112-csharp-gameplay-client-transport-and-bindings.md) and
  [ADR-0138](0138-agent-tool-layer-host.md). Accepted historical text is retained.

## Context

The build already assembles an immutable, GUID-keyed catalog with types, source
identifiers, versions and dependencies. Runtime consumes it, but clients cannot
discover its records. ADR-0125 requires the product editor to use the same shared
surface as scripts, SDKs and agents. Asset System must remain Core-only; Remote,
CLI and Gameplay SDK must not acquire Runtime/Tools/filesystem knowledge. Neither
World state nor the open authoring document owns a build-wide catalog. Existing
scene load/reload reads the catalog file and does not expose a retained catalog
object for clients; pretending to borrow such an object would misstate ownership.

## Decision

1. **A read-only sibling interface in Connection.** `AssetConnection` owns the
   public value contract defined once in Spec 0081 R2. It lists summaries and
   looks up details; it has no commands, transactions, subscriptions or file
   access. DTOs use existing AssetGuid and their own closed asset type, not the
   Asset System's catalog/file types or internal AssetId. Asset System keeps
   its Core-only dependency; Connection's link/dependency graph is unchanged.
2. **An executable-private host.** `atlantis_runtime` composes the adapter from
   the configured cooked-catalog path, using the existing Asset System loader.
   Its first well-formed endpoint query retains one process-lifetime snapshot or
   unavailable outcome, shared by every connection. This freezes the catalog at
   first use, not Runtime startup; R3's Uninitialized → Ready/Unavailable transition
   is one-way, including an empty success or a failed load. Reconnect, apply and
   Reload view do not change it; recovery after a failed initial load needs a
   restart. The editor shows this freshness/recovery limit. The adapter is
   frame-thread-only; its connections borrow it and close first, and results
   are owned copies. It does not change
   RuntimeHost, RuntimeApplication, scene load/reload or `runFrame()`. R3 owns
   freshness and external-build limits. No new module, watcher or scheduler.
3. **Every current client in one delivery.** Native Remote serves/implements the
   interface, CLI maps the two queries, C# exposes an AssetSession, and MCP adds
   exactly the two query tools in R4 through that SDK. Native Gameplay SDK gains
   an additive Assets facade over AssetConnection: this extends ADR-0110 D1's
   RuntimeConnection-only surface solely for asset discovery, retaining its
   Connection-only link, client-owned execution and no-Runtime/Remote rule.
   No typed World binding or schema generator changes. Existing calls and MCP
   version response behavior are preserved; new calls need an updated provider.
   R4 distinguishes host refusals from the native Remote failure sentinel and
   managed exceptions, so a failed query cannot be presented as an empty catalog.
   A complete list-all reply is the bounded first delivery: existing queue/line
   overflow remains a session failure, not truncation, implicit paging, a new
   catalog error or a reason to raise transport limits. The current-content
   acceptance verifies complete serialized replies under those limits.
4. **Catalog browsing, ordinary document editing.** The editor holds a local view
   of returned facts and copies GUIDs. Existing Inspector/authoring/history paths
   perform reference edits. R5 adds no direct picker, asset-write operation,
   privileged channel or separate undo model. Catalog presence is not a promise
   of residency, source editability or GPU loadability.

## Consequences

### Positive

- One discovery contract for GUI, CLI, both SDKs and agents, over existing identity.
- Imported sub-assets and build/content gating are represented without guessing
  from filenames or introducing a second catalog authority.
- A small CPU-only adapter and owned values preserve existing module boundaries.
- Process-lifetime data requires no invalidation/event or mutable database design.

### Negative / Trade-offs

- A new interface and closed DTO/codec set must be carried by every client.
- The host retains another CPU view of the catalog; it does not reuse a catalog
  instance that the existing Runtime does not retain. First-query parsing and
  list copies execute between frames, with no arbitrary-size latency promise.
- An external rebuild after startup may differ from what Runtime loaded even
  before this first query; modifying a running build has no consistency promise.
  A rebuilt/repaired catalog requires Runtime restart. List-all is subject to
  existing transport limits and can fail its connection when they are exceeded.
  There is no live-refresh, paging or asset-residency contract.
- The first UI uses copy/paste into existing GUID fields; direct assignment and
  file/import management remain later work.

## Alternatives Considered

Direct GUI filesystem access breaks the ordinary-client boundary. Adding asset
queries to World or authoring interfaces assigns ownership to the wrong state.
A new catalog/database module, a live mutable index, or an asset-write service
introduces unused lifecycle and dependency decisions. Reworking Runtime's loader
to retain/share its catalog broadens the rendering composition change without
helping this immutable read-only delivery. Full query paging/search and a direct
assignment picker can be reconsidered for a demonstrated later consumer.
