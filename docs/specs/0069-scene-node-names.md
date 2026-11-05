# Spec: Scene Node Display Names

- **Status:** Approved
- **Author:** Codex, at slmao's direction
- **Created:** 2026-11-04
- **Related Plan(s):** [Plan 0069](../plans/0069-scene-node-names.md)
- **Approval:** slmao, 2026-11-04; [joint ruling](https://github.com/slmao/Atlantis/pull/323#issuecomment-6634427244), J1–J4 as recommended; PR #323 manually merged as `1e04452`.
- **Related ADR(s):** [ADR-0141](../adr/0141-authoring-node-name-storage-and-projection.md), [ADR-0142](../adr/0142-shared-node-rename-and-history.md) (Accepted)

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Give scene nodes an optional display name, preserved in scene source and exposed
through the existing shared authoring surface. Atlantis Editor App displays it
in the Hierarchy and edits it in one Inspector field with undo/redo. Names never
replace GUID identity or become Runtime components. This uses the 0069 slot
reserved by Specs 0066/0068 and the [roadmap](../project-blueprint.md).

## Motivation / Problem Statement

At main `37cda711b655b4fb483aef7633614fde4afd9696`, `AuthoringNode`,
`ParsedSceneNode` and authoring `NodeSummary` have no name; `DocumentTree.Label`
shows a GUID prefix and component kinds. The existing transaction, source-save,
event and client-history machinery already supplies the rest of this workflow.
Specs 0065/0068, ADR-0100/0129/0130/0135/0136 and merged 0080 are reused, not
redesigned. Immutable code evidence belongs in the reviewing PR.

## Goals

- Recognizable labels on hand-authored and imported nodes, editable by all
  existing authoring clients through one contract.
- Lossless name persistence, event synchronization and safe editor history.
- One joint design/authorization decision and proportional verification.

## Non-Goals

No Runtime name/query/lookup service, ECS string component, reflected string
primitive, asset renaming, unique paths, name-based addressing, search/filter,
inline tree editing, multi-selection, localization, prefab/content browser,
in-process ImGui editor work, new packages or diagnostic tooling.

## Requirements

- **R1 — Meaning and domain.** Add `name`, an owned string defaulting to empty,
  to the authoring node and parsed source node. Empty means unnamed; duplicate
  names are valid. GUID, order, parent and asset identity are independent of it.
  A nonempty name is valid UTF-8, at most **255 encoded bytes**, excluding
  U+0000–001F, U+007F–009F and U+2028/U+2029. Preserve case, spaces and Unicode
  scalar sequence exactly: no trim, normalization, truncation or automatic suffix.
  Reject invalid UTF-8 and unpaired UTF-16 surrogates at managed input boundaries.
  Put the name constraint/limit in scene-local schema data, include it in the
  semantic fingerprint, and bump scene semantics **1 → 2**. Component descriptors,
  their versions, Core schema primitives and World PropertyValue stay unchanged.
- **R2 — Source and runtime projection.** Write source **v8**; read v8 and the
  explicitly supported legacy v7 (all names empty), rejecting other versions.
  v8 adds optional `name="…"` immediately after `guid=` and before `parent=` on
  a node line. A missing token or `name=""` means empty; canonical serialization
  omits an empty name. Quoted text may contain spaces; only `\"` and `\\` escape
  quote and backslash. Reject unknown/dangling escapes, unterminated/duplicate/
  misplaced tokens and invalid name values. v7 never accepts this new token.
  Source ⇄ semantic round trips retain names exactly, including through
  `cookSceneToMemory`'s existing representability check and save/reopen. No bulk
  rewrite of old sources or general migration framework. Artifact format **7**,
  its bytes and `ValidatedSceneData` remain unchanged: runtime projection drops
  names deliberately; decoded artifact → authoring produces empty names. Both
  serializer semantic declarations follow v2; this projection is explicit in
  conformance checks, not mistaken for a lossless artifact round trip.
- **R3 — Import.** Preserve each emitted glTF node's existing `name` in generated
  source, empty when absent; validate against R1 and refuse invalid input with
  a named import error, never sanitize it silently. Name changes alone do not
  change node GUID derivation, node order, asset keys or cooked scene bytes.
  Overlay source names use the same codec. Imported documents remain editable
  and applyable but not saveable; reopening restores their generated source.
  Synthetic nodes created for mesh primitives remain unnamed; no generated
  naming convention or GUID derivation change is introduced.
- **R4 — Shared edit contract.** Extend authoring `NodeSummary` with `name`, add
  closed command `SetNodeName{node,name}` and event `NodeNameChanged{node,name}`.
  Append the summary field with empty default so existing two-field construction
  remains valid in native and managed client source.
  `CreateNode` still creates unnamed nodes; create+rename may be one transaction.
  Unknown nodes use the existing refusal; invalid names use `InvalidNodeName`.
  Preserve ticketing, whole-transaction validation/rollback, failure draining and
  command-order event delivery. Every successful rename command emits one event,
  including a same-value command, as existing setters do; refused transactions
  emit none. The event has a node and no component for existing filters; `all()`
  includes it. Open/apply/save, document ownership and reload remain unchanged;
  even a name-only apply follows the existing reload path.
  Append new cases without renumbering existing event kinds or changing their
  wire spellings.
- **R5 — All current clients.** Carry R4 through native Connection/Remote,
  `atlantis.remote/1`, CLI and the reflective C# AuthoringSession, with generated
  codec vectors. Add CLI `scene name <node-guid> <json-string>` (`""` clears),
  the command in existing `scene tx` input, and name in node/event output.
  The CLI name argument is one JSON string (including quotes), parsed from the
  remaining command text/argument using the existing JSON codec, so spaces,
  quotes and empty names require no general shell/tokenizer redesign.
  JSON uses `name` on node summaries and `kind:"SetNodeName"` /
  `kind:"NodeNameChanged"` with `node,name`; no new RPC method. New clients read
  a missing summary name as empty, but reject a present non-string/invalid name;
  new encoders write it. Old/new clients may read summaries (old decoders ignore
  unknown members); using new commands/events requires an updated peer, with
  existing unsupported-value behavior and no protocol-version/capability system.
  An old all-events subscriber may reject a rename event from another client;
  rename sessions require updated participating peers, not transparent mixed-
  version operation.
  MCP extends only its existing authoring transaction's closed command schema
  and node/event handling through the C# SDK: no new tool, bypass or direct wire
  access. Preserve both supported MCP versions' response behavior. Native
  Gameplay SDK/World APIs and generated typed bindings gain no authoring API.
- **R6 — Minimal editor delivery.** A named Hierarchy row shows
  `name · GUID-prefix · component-kinds`; an unnamed row keeps today's label.
  Selection, expansion and ordering stay GUID-based. Add one Name text field
  above the selected node's existing Inspector properties: Enter commits one
  rename, Escape cancels; leaving without Enter cancels. Empty clears, identical
  text submits nothing, invalid text reports a refusal without mutation. Follow
  existing busy/selection and edit+apply/coalescing rules; external name events
  refresh labels and stale edit text cannot overwrite an observed external edit.
  This is the existing latest-observed-state check, not a new server CAS promise.
  One committed rename is one before/after name history entry. Undo/redo compares
  the current name with what the edit left; a mismatch/missing node refuses the
  step under existing notices. Include names in full node-life snapshots,
  comparisons and recreation, so subtree delete/undo restores them and another
  client's rename prevents destructive replay. Existing history depth, apply
  survival and DocumentReopened clearing remain unchanged.
- **R7 — Verification and scope.** Complete the mapped checks in Plan 0069,
  retaining failures and valid reuse provenance under 0080. Existing Vulkan,
  image and full-PR performance applicability rules remain in force. No policy,
  baseline, golden refresh, extra latency threshold or observer is introduced.

### Non-functional

Owned values and existing single-thread ownership; no Runtime per-frame name
storage/work. Windows and Android codec/build portability, managed clients on
their existing platforms, unchanged package pins and dependency boundaries.
The 255-byte bound limits document and wire growth without a new string system.

## Proposed Design

Use node metadata, not a component property. Existing non-component authoring
commands (parent/active camera) establish this pattern; property schemas need no
string kind. One narrowly versioned quoted field supplies persistence, and
existing transactions/events carry rename to every client. The two ADRs record
storage/projection and shared editing/history decisions, respectively.

## Architectural Impact

Public node DTOs, source grammar, scene semantic version and the closed authoring
command/event/history sets change. ADR-0141 extends ADR-0100 and the scene codec
decisions in ADR-0045/0053 with the specific v7 compatibility path and name-drop
runtime projection. ADR-0142 extends ADR-0130/0136 and retains ADR-0125's shared
surface, ADR-0135's document model and ADR-0138's managed-only MCP boundary.
Both must be Accepted in the joint review. No accepted ADR text is rewritten;
no module, dependency, threading, rendering or World schema decision changes.

## Alternatives Considered

- Runtime/reflected Name component: adds World strings and wider schema/SDK
  obligations for an authoring label; not needed by this delivery.
- Editor-only label cache/sidecar: cannot be saved or edited consistently by
  other clients and breaks the shared-document model.
- Strict v8-only reader: coherent but forces unrelated source/fixture migration;
  the bounded v7 reader retains old scenes without a migration subsystem.
- Store names in artifact8: extra Runtime data and binary migration with no
  consumer; reconsider only with a reviewed runtime-name use case.

## Testing & Verification Plan

Plan 0069 maps R1–R7 to codec/import/schema, atomic edit/event/client conformance,
editor history/UI and real multi-client checks. This proposal runs document
checks only; no implementation, build, collection or performance result claimed.

## Risks & Open Questions

Joint review must rule on the proposed domain, bounded legacy source support,
authoring-only projection, closed API additions and minimal editor/history scope.
Mixed peers cannot use new values transparently; imported files with invalid
names now refuse import. Those are explicit trade-offs, not implementation fixes.

## Out of Scope / Future Work

All Non-Goals remain separate work. Runtime name retention, generalized strings
or broad migration support need a later reviewed decision, not this feature's
implementation. Stop this round after joint-review submission.
