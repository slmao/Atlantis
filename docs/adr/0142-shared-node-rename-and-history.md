# ADR 0142: Shared Authoring Node Rename and Client History

- **Status:** Proposed
- **Date:** 2026-11-04
- **Deciders:** slmao, pending joint review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0069](../specs/0069-scene-node-names.md), R4–R6
- **Related ADRs:** Extends [ADR-0130](0130-authoring-access-interface-and-module-boundary.md) and [ADR-0136](0136-structural-edit-records.md); retains [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md), [ADR-0135](0135-two-document-edit-and-apply.md) and [ADR-0138](0138-agent-tool-layer-host.md).

## Context

The shared authoring surface already edits node-level structure outside reflected
properties. A name is likewise not a World PropertyValue. The editor's history
has closed entries and node-life snapshots; ignoring names there would silently
lose them on undo or permit replay over another client's rename.

## Decision

1. Extend the closed authoring command/event sets with SetNodeName and
   NodeNameChanged and listNodes' summary with name, as R4/R5 specify. Reuse
   transactions, settlement, filters and draining. Native/Remote/CLI/C# clients
   expose the same operation; no editor-only method or new wire method/version.
   Mixed-peer compatibility is the explicit R5 limit, not a new negotiation layer.
2. MCP's existing authoring transaction tool gains this command alternative;
   existing node/event output exposes the data through the C# SDK. Its tool
   inventory, package policy, response-version compatibility and apply-settlement
   rule do not change. Native Runtime and Gameplay SDK APIs stay unchanged.
3. Extend the C# editor history with a name before/after entry and include name
   in node-life snapshots/comparisons/recreation. Undo/redo sends the ordinary
   shared command, refusing stale or missing state under ADR-0136's rules.
   This extends its closed entry set and full-state coverage only, not its
   ownership, depth or lifetime. The in-process C++ editor/history stays frozen;
   the C#-only additions need no invented C++ reference/vector model.
4. Name editing follows the existing authoring edit+apply path. No Runtime-world
   preview or reload optimization is added for metadata. UI scope is one Inspector
   field and Hierarchy labels; no new editing mode, panel or command framework.

## Consequences

### Positive

- Every current authoring client can observe and change the same name.
- Existing atomic validation and history conflict protection cover the new data.
- No broad string API, server-side undo history or transport redesign.

### Negative / Trade-offs

- Closed codecs, event filters and managed snapshots must all be updated together.
- Old peers cannot perform or consume the new command/event variants.
- Rename still incurs the existing full-document validation and apply/reload;
   optimizing that cost is deliberately separate.

## Alternatives Considered

SetProperty with a new string kind spreads into unrelated schema/World clients.
Editor-only rename violates the shared surface. A second history stack or snapshot
replacement API duplicates existing settlement/conflict semantics. A convenience
MCP tool adds an unnecessary inventory entry when its existing transaction suffices.
