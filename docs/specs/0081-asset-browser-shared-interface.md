# Spec: Asset Browser and Shared Asset Queries

- **Status:** Approved
- **Author:** Codex, at slmao's direction
- **Created:** 2026-11-05
- **Related Plan(s):** [Plan 0081](../plans/0081-asset-browser-shared-interface.md)
- **Approval:** slmao, 2026-11-05; [joint ruling](https://github.com/slmao/Atlantis/pull/325#issuecomment-6660593240), revised `5126f8f`, J1–J4 as recommended; #325 manually merged as `6718f7d`.
- **Related ADR(s):** [ADR-0143](../adr/0143-shared-cooked-asset-catalog-queries.md) (Accepted)

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Make the existing build's cooked asset catalog discoverable through one read-only
client interface. Atlantis Editor App gains an Assets panel for browsing, inspecting
and copying an asset GUID. CLI, native and managed SDK clients, and MCP expose the
same queries. The first delivery stops at discovery: a copied mesh/material GUID
can already be entered in the Inspector or an ordinary authoring SetProperty.

## Motivation / Problem Statement

Baseline: main `374b9ddb362aeebeeee0ca205906478663255320`, the human merge of
[PR #324](https://github.com/slmao/Atlantis/pull/324). Node names are delivered;
the next editor/asset-pipeline slice is currently only named in the roadmap.

Spec 0047 already supplies persistent GUIDs, imported sub-asset identities,
`AssetCatalogRecord`, an immutable `AssetCatalog`, and a validated build assembly.
It is a build output, not an asset database (ADR-0098). Runtime resolves assets
through it, while the editor's reflective Inspector accepts asset GUID values
without offering a way to discover them. Neither the wire nor either SDK exposes
catalog queries. Giving the GUI filesystem access would create a second data
source and violate ADR-0125 D2. Immutable source references and sizing observations
belong in the reviewing PR, not in this requirement list.

## Goals

- Discover every asset in the configured cooked catalog, including built imported
  sub-assets and assets not referenced by the active scene.
- Expose identity, provenance and direct dependencies to every current client.
- Provide a useful browsing-to-existing-edit workflow with no new write semantics.

## Non-Goals

No import/reimport/cook/build command, source-catalog editing, GUID minting, asset
create/delete/rename/move, arbitrary file access, thumbnail/mesh/material preview,
drag-and-drop, direct assignment picker, scene switching, material/texture editing,
usage/reverse-dependency analysis, live filesystem watcher, hot reload, derived-data
cache/asset database, packaging service, new package, new top-level module, or
in-process ImGui editor work.
This is the first shared asset surface, not the entire asset-pipeline management API.

## Requirements

- **R1 — Catalog scope and truth.** Browse the assembled or closure cooked catalog
  at the existing `BootstrapConfig::assetCatalogPath`, never merge it with the
  committed source catalog or scan directories. Include exactly its records:
  `mesh`, `texture`, `material`, `scene`, `environment`. A source-only or
  content-gated unbuilt asset, including a `gltf_import` root, is absent; its built
  sub-assets appear under their own GUIDs and existing `root:path#sub-key` source
  identifiers. A closure catalog intentionally shows only that closure. Identity
  and resolution remain GUID-based; paths are display provenance. Presence means
  cataloged in this build, not currently loaded, resident, editable, or guaranteed
  to load successfully. Do not probe artifact files to invent an availability flag.
- **R2 — Two read-only queries.** Add `AssetConnection` in Atlantis Connection,
  with owned values in `connection::assets` and these public operations:

  | Operation / value | Contract |
  |---|---|
  | `listAssets()` | `Result<vector<AssetSummary>, AssetError>`; the complete list, strictly ascending by GUID's canonical bytes, including an empty successful catalog. |
  | `getAsset(AssetGuid)` | `Result<AssetDetails, AssetError>`; lookup of one non-nil GUID. No name/path lookup. A nil native argument is a documented programmer precondition violation, consistent with existing GUID APIs. |
  | `AssetSummary` | `guid`, closed `type` from R1, `source` in the existing canonical catalog source-ID grammar. |
  | `AssetDetails` | The same three fields plus `artifactSchema` (`uint32`), optional `sourceSchema` (`uint32` or absent), exact `tool` string, and `dependencies` (direct GUIDs, sorted and unique). No recursive expansion. |
  | `AssetError` | Closed `CatalogUnavailable` (existing catalog read/parse validation failed), `UnknownAsset` (a valid lookup GUID is absent). `listAssets` can return only the former. |

  DTOs do not expose filesystem artifact/metadata locations, artifact contents,
  internal AssetId keys, GPU/Runtime handles, or schema offsets. Map existing
  Asset System records at the host; do not move catalog file grammar into
  Connection or change Asset System's dependency on Core only. Existing catalog,
  asset/source/artifact formats, schema versions and generated World bindings
  remain unchanged. No query mutates a scene, creates a ticket/event, drains any
  existing queue, applies, saves or loads a GPU resource.
- **R3 — Snapshot and hosting.** An executable-private `AssetHost`, following
  `AuthoringHost`'s composition pattern, serves the interface. At the first
  well-formed asset query reaching the endpoint in a Runtime process it loads
  the configured cooked catalog through `loadAssetCatalog()` and retains one
  immutable snapshot, or one unavailable outcome, until process shutdown.
  The state is initially Uninitialized and transitions once to Ready (an empty
  catalog included) or Unavailable. Both list and get can cause that transition;
  a get of an unknown valid GUID still fixes the successfully loaded snapshot.
  Invalid wire arguments, a missing service provider, connection construction
  and the handshake do not load it. Load failure takes precedence over lookup,
  so UnknownAsset is possible only after a successful load. All asset connections
  share that outcome; scene open/apply, client reconnect, browser reopening and
  Reload view do not reload it. A rebuild or external catalog/artifact edit requires restarting Runtime
  before these queries describe the new build. This is a first-query snapshot,
  not a snapshot captured when Runtime started and not a snapshot of its active
  World. External edits between startup and first query can therefore change
  what is captured. No consistency or hot-reload promise is made for a running
  build modified on disk; the existing scene loader/reloader still reads its
  configured catalog as before. Do not change that loader to force consistency.

  Host construction is cheap; the first query pays the catalog read. Subsequent
  queries perform no filesystem I/O. Cache the GUID order once rather than sorting
  every list. The endpoint uses the existing frame-thread, between-frame dispatch;
  no job, observer or new thread. Connections borrow host state and close before
  it; returned values are independent copies. Android builds the portable
  Connection/Remote/SDK additions but hosts no asset service in this delivery.
- **R4 — Uniform wire and clients.** Extend `atlantis.remote/1` additively,
  preserving its authentication, framing, limits, dispatch and existing methods:

  | Surface | New entry points |
  |---|---|
  | Native Remote | `RemoteSession::assets()` implements `AssetConnection`; server `serveAssets` provider, analogous to `serveAuthoring`. |
  | Wire | `assets.list` with `{}`; `assets.get` with `{"guid":"<asset GUID>"}`. In-band results are `{"ok":...}` / `{"err":"<AssetError>"}`. Summary/detail field names are those of R2; absent source schema is JSON `null`; dependency GUIDs are strings. |
  | CLI | `asset list` and `asset get <asset-guid>` in existing human and `cli/1` JSON output, one-shot, REPL and `--exec` read commands. JSON `result` is the list/detail; in-band errors use existing CLI refusal mapping, exit 1. No asset write/transaction syntax. |
  | Native Gameplay SDK | An additive `atlantis::gameplay::Assets` facade over an explicitly supplied `AssetConnection&`; same two queries and owned values. No Remote/Runtime/Tools dependency or World binding generation. |
  | C# SDK | `RemoteSession.Assets`, an `AssetSession` with `ListAssets` / `GetAsset`, closed DTOs and `Outcome` errors. Existing request/codec path, no native calls or packages. |
  | MCP | Exactly two additional exact tools, `assets.list` and `assets.get`, with the same arguments/results through the C# SDK session. Existing authoring apply/failure settlement and every other tool stay unchanged. |

  A server without a provider returns the outer RPC error `NoAssets`; old servers
  use their existing unknown-method refusal. Invalid request arguments (malformed
  or nil GUIDs and wrong field types) return the existing outer `BadRequest`
  before invoking an asset query. Native/C# typed query decoders require the declared field
  types, canonical non-nil GUIDs, known type/error spellings, uint32 schema
  integers, unique strictly GUID-ordered summaries and dependency GUIDs, and a
  get result whose GUID matches its request. Every declared DTO member is
  required on the wire; `sourceSchema` is present with `null` when absent in
  the native value. An in-band envelope has exactly one of `ok` or `err`.
  Additional object members are tolerated, as in existing codecs. Source/tool
  strings are display facts; clients do not acquire the Asset System's catalog
  parser. A well-typed schema
  integer is displayed even if this client cannot load that artifact version.
  A malformed typed-query result fails the Remote session; it is never a successful empty
  list. An unknown valid lookup GUID is the in-band `UnknownAsset`.

  In-band `CatalogUnavailable` and `UnknownAsset` do not fail the Remote session
  or stop its other capabilities. Transport/protocol failure is a different
  outcome: the native Remote adapter sets the existing sticky
  `RemoteSession::failure()` and returns `Err(CatalogUnavailable)` as its failure
  sentinel, never `Ok([])`. Callers using Remote must check that failure before
  interpreting the sentinel; the transport-neutral native SDK gains no second
  transport-error channel. C# uses its existing `RemoteException`. CLI checks
  native session failure before reporting an in-band refusal: connection failure
  retains exit 3, genuine catalog refusals exit 1. MCP retains the existing exact
  tier's raw SDK `AwaitResponse` path: preserve the result JSON without typed
  decode/re-encode or new DTO/policy validation. It marks outer RPC and in-band
  refusals as errors and catches transport exceptions; an outer RPC refusal
  need not poison this raw SDK session, unlike the typed query path. The typed
  decoder checks above do not add a promise that MCP diagnoses invalid DTOs in
  otherwise valid JSON. No observed refusal/transport failure becomes empty
  success. Neither sentinel nor exception changes the
  host's retained catalog outcome.
  New native/managed clients ask for no asset data during their existing connect
  handshake, so using only existing capabilities against old servers is retained.
  A typed call of a new capability on an old/unprovided peer follows existing
  Remote session-failure behavior; MCP preserves its raw error-envelope behavior.
  No capability negotiation or fallback filesystem
  reader is added. MCP must preserve both supported `2025-11-25` and `2026-07-28`
  response forms, including the legacy array wrapper and error flag. C++-owned
  conformance vectors cover all new DTOs/errors and exact wire values.

  List-all means one complete owned list and one JSON-lines reply, not an iterator
  or automatically repeated requests. It is recommended for the current content
  size, not unlimited catalogs. No pagination, subscription, revision token,
  size-limit option or server search API is introduced. Existing transport limits
  remain binding: the default server has a 64 MiB pending-output budget shared
  by replies on that connection, while native/managed clients accept at most
  256 MiB per response line. The 1 MiB server request-line limit is not a response
  limit. Count the actual reply envelope, encoding and queued bytes, not the
  cooked catalog file's length. If the server cannot queue a whole reply it
  drops that client under the existing transport rule; a client line-limit
  violation likewise fails its session. Neither case is CatalogUnavailable from
  the host, UnknownAsset, truncation, or partial/empty success. No automatic
  retry, fallback or limit increase. Catalogs needing larger replies require a
  later reviewed query change; the default/Bistro acceptance must demonstrate
  complete replies under unchanged default limits.
- **R5 — Minimal Editor App.** Add one Assets panel, backed only by the C# SDK:
  a flat list with type, source and GUID, a type filter, an ordinal case-insensitive
  substring filter over source/GUID, and a detail area for R2's data. Selection is
  by GUID and independent of Hierarchy selection. Dependency GUIDs can select a
  record for inspection; a missing record reports the ordinary lookup failure.
  Copy GUID uses the editor's own clipboard; it does not open a file or edit a node.
  Show loading, an empty catalog, no filter matches, catalog-unavailable and
  disconnected states distinctly. Display a short freshness note that the catalog
  is fixed at its first query and a changed build (or failed initial load) needs
  Runtime restart. Load on first panel use through the existing worker; name the
  action Reload view and explain that it re-queries this snapshot, not the disk.
  It preserves a still-present asset selection. Publish list/detail data only
  after a complete successful reply; show failures separately instead of
  interpreting them as an empty catalog. Filter/search are local presentation
  over that snapshot; no background polling or automatic
  request per keystroke. Keep UI work on the dispatcher and query work on the
  existing session worker; discard callbacks belonging to an older session,
  selection or superseded view request using local UI state, not a wire revision
  token. Browsing, copy and Reload view add no document history entry.

  To use a mesh/material, paste its GUID into the existing selected node's
  Inspector asset field; the existing authoring SetProperty, validation,
  edit+apply and history rules govern that edit. This Spec adds no assignment
  command, reference-field schema metadata, compatibility guarantee for an
  arbitrary asset/material combination, or changed undo/conflict semantics.
- **R6 — Verification and delivery bounds.** Complete Plan 0081's mapped checks,
  using 0080's relevant verification and provenance rules. New service/query tests
  are GPU-independent; real multi-client and Editor checks also use the existing
  Runtime and fixtures. Vulkan validation, image checks and actual whole-PR
  performance applicability remain mandatory where selected. No policy, baseline,
  golden refresh, new timing threshold, diagnostic subsystem or inherited gate
  verdict is authorized. This proposal itself runs documentation checks only.

### Non-functional

Keep C++20, Windows/Android portability, existing .NET/package pins and module
boundaries. One retained CPU catalog projection per hosted process; list results
are copies, details copy only the selected record. No name or asset work is added
to `runFrame()`, Renderer, RenderGraph or World. The first-query parse and linear
list copy run on the current dispatch thread; that bounded implementation choice
does not promise responsiveness for arbitrary future catalog sizes.

## Proposed Design

Use the existing cooked catalog as the authoritative discovery index, a sibling
Connection query interface as its shared public boundary, and an executable-private
adapter as the composition point. The editor's model holds the returned summaries
and its own filters/selection, while existing Inspector edits remain ordinary
authoring edits. [ADR-0143](../adr/0143-shared-cooked-asset-catalog-queries.md)
records the query/snapshot and dependency decision.

## Architectural Impact

New public DTOs/interface and two additive wire methods; matching CLI/native SDK/
C# SDK/MCP surfaces, including a narrow extension of the native Gameplay SDK's
RuntimeConnection-only charter to read-only AssetConnection. Runtime gains an
executable-private adapter and wiring, not a subsystem or a public Runtime API.
ADR-0143 extends ADR-0098, ADR-0125, ADR-0110/0112 and ADR-0138; it must be Accepted
in the joint review. Asset System stays Core-only, Remote/CLI/native SDK stay
Connection-only, and Editor/MCP stay C# SDK clients. No dependency, source/artifact
format, World/schema, threading, rendering or accepted ADR text changes.

## Alternatives Considered

- GUI reads source files or invokes Tools: a second, privileged client path and
  an unbuilt-source catalog masquerading as usable build assets.
- Put queries on RuntimeConnection/AuthoringConnection: conflates the build-wide
  index with World/document state and their mutation/subscription lifetimes.
- A new asset database/service/module or live import manager: greater ownership,
  invalidation and write contracts than this discovery workflow needs.
- Add assignment picker and asset-reference schema annotations now: useful later,
  but existing GUID edits already complete this first browsing workflow.
- Page, search and version every query now: unnecessary for current content; R4
  keeps transport limits explicit instead of promising unlimited list-all support.

## Testing & Verification Plan

Plan 0081 maps R1–R6 to catalog projection/lifetime tests, native/managed codecs,
CLI/SDK/MCP compatibility, headless editor models and product UI, real default/
Bistro shared-client checks, builds/boundaries and existing GPU/performance gates.
Reuse unchanged checks with source/input provenance, preserving failures and their
corrections. No implementation or engine verification result is claimed here.

## Risks & Open Questions

Joint review must decide the proposed read-only delivery, process-lifetime snapshot,
new Connection/SDK surface and list-all trade-off, and the mapped verification/
future authorization in Plan J1–J4. The catalog does not prove GPU residency or
loadability; stale external builds require restart. Large future catalogs may
need paging/off-thread preparation under another Spec, not an implicit addition.

## Out of Scope / Future Work

All Non-Goals remain separate decisions. A direct picker, import/cook automation,
live catalog changes and asset editing can build on this surface only through
reviewed additions. Stop this round after submitting the joint design PR.
