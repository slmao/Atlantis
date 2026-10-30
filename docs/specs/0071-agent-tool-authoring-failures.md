# Spec: Authoring Failure Drain for the Agent Tool Layer

- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-10-30
- **Related Plan(s):** [Plan 0071](../plans/0071-agent-tool-authoring-failures.md) (In Review)
- **Approval:** Pending in [PR #308](https://github.com/slmao/Atlantis/pull/308);
  this proposal does not authorize implementation.
- **Related ADR(s):** [ADR-0138](../adr/0138-agent-tool-layer-host.md) and
  [ADR-0130](../adr/0130-authoring-access-interface-and-module-boundary.md), both Accepted and
  proposed unchanged; see Architectural Impact.
- **Supplements:** [Spec 0070](0070-agent-tool-layer.md) R2/R4 and its M3/M4 acceptance;
  gap disclosed in [PR #307](https://github.com/slmao/Atlantis/pull/307).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
The Approved Spec/Plan 0070 and Accepted ADRs are not rewritten by this follow-up.

## Summary

Add exactly one MCP exact-tier tool, `authoring.drain_failures`, forwarding the existing
`authoring.drainFailures` call. It makes document command refusals observable through tools,
including Spec 0070's convenience tier and its two-client live acceptance. The existing protocol,
both SDKs, CLI and engine remain unchanged.

## Motivation / Problem Statement

Spec 0070 R2 enumerates 22 tools, including `authoring.submit_transaction`, but omits the
authoring failure drain. A submission returns a transaction ticket even when its document edit
was refused; the refusal is retained in the submitting connection's failure queue. Events are
not that queue, and draining another client's connection cannot retrieve it. The MCP mapping
omission prevents Spec 0070 R4's refusal reporting from covering document submissions.

This is an exposure gap, not a missing engine capability. The existing contracts are visible in
[the protocol reference, section 5.1](../architecture/remote_protocol.md#51-the-authoring-document-spec-0065-adr-0130-d3),
[the server dispatch](../../src/remote/src/remote_server.cpp),
[the authoring connection's queue](../../src/authoring/src/authoring_session.cpp), and
[the C# SDK's `AuthoringSession.DrainFailures()`](../../src/csharp/Atlantis.Gameplay/Remote/AuthoringSession.cs).
[The CLI](../../src/cli/src/command_layer.cpp) already uses the same capability to settle scene
commands. This follows Spec 0065 / ADR-0130's shared client surface; Spec 0070 R9 does not require
adding a capability again to clients that already have it.

There is also an acceptance ambiguity: [Spec 0068's J7 driver](../../src/editor_app/tests/Atlantis.Editor.Tests/J7Acceptance.cs)
refuses an undo as "changed elsewhere" in the editor's own history. That is not a wire refusal of an ordinary overwrite.
Spec 0070 adds no history or expected-version command. Its two-client case must therefore name
an existing wire refusal rather than promise optimistic concurrency control.

## Goals

- Complete document submission/refusal observation with one existing wire mapping.
- Keep the convenience tier's one-transaction contract and apply's explicit boundary.
- Make M3 and M4 acceptance precise without changing engine or client semantics.

## Non-Goals

- No other exact-tier addition or general inventory expansion.
- No automatic drain, retry, apply, failure cache, ticket filtering or new error vocabulary.
- No editor history, compare-and-set, version precondition or overwrite-conflict detector.
- No changes to the protocol revision, schemas, SDKs, CLI, engine, packages or module boundaries.

## Requirements

### Functional

- **R1 — One exact mapping.** Extend Spec 0070 R2 only with `authoring.drain_failures` →
  `authoring.drainFailures`, on the host's existing session seam. Its input schema is an empty
  object, with no properties; one invocation makes one wire request with `{}` parameters.
  It returns and clears that connection's outstanding authoring failures in wire order,
  preserving the complete result array and every `ticket` and `error` value. The exact tier
  grows from 22 to 23 entries; the wire stays `atlantis.remote/1`.
- **R2 — Refusal representation.** An empty array is an MCP success. A nonempty array is an
  MCP tool error (`isError: true`) with that same array as structured JSON and matching JSON
  text, following the existing `world.drain_failures` rule. No wrapper, record filtering or
  renamed reason is introduced. Protocol refusals such as `NoAuthoring` and transport failures
  follow the existing exact-tier error path; no retry or reconnect occurs.
- **R3 — Explicit settlement.** Neither `authoring.submit_transaction` nor any of Spec 0070's
  seven convenience tools drains failures or applies the document implicitly. Their transaction
  tickets are not proof of edit success. Descriptions teach clients to submit, explicitly drain
  failures and correlate returned tickets, then call `authoring.apply` only when the intended
  edit has no refusal. A drain consumes all queued failures for that connection, not only the
  most recent transaction. Failure drains and event drains never consume each other's data.
- **R4 — M3 acceptance delta.** Before M3 is accepted, the additional exact mapping and R2/R3
  behavior are unit-tested alongside the seven convenience tools, two event drains and image
  content already required by Plan 0070 M3. In particular a convenience call still makes exactly
  one submission, its refusal remains available for a later explicit failure drain, a second
  drain is empty, and events remain independently available. Completed M3 discovery contains
  32 tools: 23 exact, seven convenience and two event drains. Original M3 requirements remain.
- **R5 — M4 acceptance delta.** On both the default scene and Bistro, the MCP live script
  explicitly checks the submitting client's failure drain after document submissions, before
  treating an edit as successful or applying it. The two-client refusal case uses a scratch
  node: one client observes it, another destroys it, and the observing client submits a stale
  property edit. The submitting client receives a ticket, then its own drain reports
  `UnknownNode` under that ticket as an MCP tool error. The refused transaction changes no
  document property; the other client's drain is empty, and the submitting client's next drain
  is empty. This specifies Plan 0070 P5's "second-client conflict refused" using existing wire
  semantics; it does not claim the editor history's "changed elsewhere" behavior. Successful
  edit → explicit drain → apply → `Applied{generation}` → capture remains required on both scenes.

### Non-functional

- **Performance/memory:** one round trip per explicit failure drain; the host adds no queue,
  background polling or hidden calls. Existing session serialization is retained.
- **Portability/packaging:** inherit Spec 0070's Windows managed host, stdio transport and pinned
  dependencies; no build switch, package, Android or native-code change.

## Proposed Design

Declare the new entry beside the authoring transaction tool, through the existing table and
`IAtlantisSession`. Treat nonempty failure arrays consistently across the two families and
extend the mapping, refusal and explicit-settlement tests. No new seam is needed.

The client workflow gains an explicit observation step before apply. Apply acceptance and the
later Runtime reload remain distinct: `Applied{generation}` still proves the reload, not the
acceptance of any individual command. The new drain is consumptive and connection-local;
clients must retain any failure records they need after displaying them.

## Architectural Impact

**No new architectural decision; no new or superseding ADR is proposed.** There is a public MCP
inventory change, reviewed here under the significant-work workflow, but its shape is already
decided by ADR-0138 D3: an exact 1:1 tool over an existing client call, with wire results verbatim.
The shared client capability was established by ADR-0130 D1/D3; no interface, command/refusal
set or transport behavior there changes. Placement, dependencies, session ownership, threading,
event subscriptions and the explicit-apply boundary remain as accepted.

This recommendation is a joint-review ruling, not an assertion of approval. If review instead
requires changing an accepted architectural decision, the corresponding superseding ADR must
be drafted and reach Accepted before implementation authorization; Accepted ADRs are not edited.

## Alternatives Considered

- **Drain inside every convenience call or submission:** hides a second request, consumes
  unrelated failures and changes Spec 0070's one-transaction forwarding contract.
- **Report failures via event drains or a host-side status cache:** invents a result/lifetime
  model and confuses connection-local refusals with subscribed document events.
- **Use a second SDK client to inspect the host's failures:** cannot read the host connection's
  queue; using SDK calls in place of the MCP assertions also leaves the tool gap untested.
- **Add version checks or port editor undo history for M4:** changes semantics and scope.
- **Expose every currently unmapped wire method:** larger than the disclosed failure-drain gap;
  other mappings need their own scope decision.

## Testing & Verification Plan

- **R1/R2, GPU-free:** mapping/discovery, exact `{}` parameters and one request; unchanged arrays,
  order, large tickets and every existing AuthoringError reason; empty/nonempty error flags;
  protocol/transport errors without retries; both raw MCP content forms.
- **R3/R4, GPU-free:** fake-session sequences for explicit drains, accumulation/clear, ticket
  correlation, no convenience-tool extra calls and independent event queues; retain all original
  M2/M3 tests and check the stage-appropriate discovery count.
- **R5, live GPU integration:** the existing M4 script on both scenes, through real MCP stdio
  hosts with separate connections for the two-client case. Use disposable document edits and
  never save them; retain original capture and clean Vulkan Validation Layer requirements.
- **Boundary regression:** existing package policy and SDK zero-NuGet/conformance tests stay
  green; engine/SDK/protocol files, committed goldens and performance baselines remain unchanged.
  Plan 0070 M5's final verification remains required.

## Risks & Open Questions

- A failure drain is destructive: automatic or competing consumers could lose evidence. R3
  keeps consumption explicit and the live test must drain the submitting connection.
- An ordinary valid overwrite can succeed after another client's write. R5 deliberately tests
  a stale structural address instead of asserting a nonexistent concurrency refusal.
- The requested rulings and distinct implementation gate are recorded once in Plan 0071's
  Joint Review section; all are pending.

## Out of Scope / Future Work

Broader tool parity, editor conflict/history semantics and new concurrency control are outside
this correction. This review does not itself implement the drain or resume M3–M5.
