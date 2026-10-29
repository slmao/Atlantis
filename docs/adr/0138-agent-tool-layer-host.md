# ADR 0138: The Agent Tool Layer Host — Placement, Dependency Policy and Tool Shape

- **Status:** Accepted
- **Date:** 2026-10-29 (accepted 2026-10-30)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-30 (owner rulings on Spec 0070 Q1–Q6, each as recommended, recorded
  once in [Spec 0070](../specs/0070-agent-tool-layer.md); the ADR text was reviewed in
  [PR #306](https://github.com/slmao/Atlantis/pull/306))
- **Related Spec:** [Spec 0070: The Agent Tool Layer — an MCP Host over atlantis.remote/1](../specs/0070-agent-tool-layer.md) (`Approved` 2026-10-30)
- **Related ADR(s):**
  - **Keeps** [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md) D2 — no
    client-only capability: every tool is a wire call any client can make.
  - **Extends the mechanism of** [ADR-0127](0127-editor-application-managed-dependency-policy.md) — a
    managed area with an allow-list of pinned packages — to a second area.
  - **Builds on** [ADR-0106](0106-attachable-runtime-transport-and-control.md) (the attachable
    Runtime), [ADR-0112](0112-csharp-gameplay-client-transport-and-bindings.md) (the C# SDK speaks
    the protocol itself) and [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
    (the authoring surface over the wire).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- Atlantis's client surface is one loopback protocol, `atlantis.remote/1`, shared by the CLI, the
  C# SDK, the editor and the editor's acceptance tooling; ADR-0125 D2 keeps it uniform — no
  client gains a method, channel or file of its own.
- The editor platform's maintainer direction (2026-10-29, recorded in the blueprint) makes the
  AI-native phase's first step an agent tool layer: agents should operate a running Runtime
  through the same surface as the GUI, not through bespoke per-agent integrations.
- MCP (Model Context Protocol) is the de-facto standard agent hosts speak: tools listed with typed
  schemas, called by JSON-RPC, results with text and images back in context. Its reference SDKs
  are managed packages.
- The engine, the wire and both SDKs are to stay unchanged.

## Decision

1. **The host is a new managed module outside the native dependency graph:** `src/mcp/`
   (`Atlantis.Mcp`, namespace `Atlantis.Mcp`), a console application project-referencing the C# SDK
   (`Atlantis.Gameplay`) and nothing native — a client with the CLI's and the editor's standing,
   with the CLI's session discovery (`--session`, `ATLANTIS_SESSION`, the default path) and one
   process bound to one Runtime session, serving MCP over stdio. CMake declares it only with
   `ATLANTIS_BUILD_MCP=ON` (default `OFF`) and a .NET 10.0.1xx SDK; Android never declares it.
2. **Its dependency policy is ADR-0127's mechanism with its own allow-list:** only packages on
   `src/mcp/allowed_packages.txt` (the pinned MCP SDK the first entry), each pinned exactly, with
   committed lock files, locked-mode restore, its own `NuGet.config` and offline-feed support,
   checked by the same policy script and negative tests. The C# SDK's zero-NuGet rule is unchanged.
3. **The tool shape is two tiers, both closed and versioned with the wire, and never beyond it:**
   - an **exact tier**, one tool per wire call, named after it, results verbatim;
   - a **composed tier** of convenience tools that build transactions from the closed command set
     only, client-side, with apply always an explicit call. The host composes shapes; it never
     invents semantics, validates policy, or gains a capability before the wire, CLI and SDKs have
     it in the same Spec (ADR-0125 D2 held).
4. **Events are host-held subscriptions drained by tools**, not exposed subscribe state and not
   push; captures return both the file path and an MCP image content block. (Both are shape
   decisions of the host, revisitable by a later Spec without touching the wire.)

## Consequences

### Positive

- Any MCP-capable agent becomes an Atlantis client by configuration, with typed discovery,
  structured refusals and in-context captures — no per-agent integration, no engine change.
- The uniform-client rule survives its first AI-era test: the agent is a peer, visibly unable to
  do what the GUI cannot.
- The host's composition tier gives agents the editor's own ergonomics (one gesture, one
  transaction) while the exact tier keeps the whole surface reachable.

### Negative / Trade-offs

- A second managed dependency area to police (one allow-list, one set of lock files, one policy
  check) — the cost of not hand-rolling MCP in C++.
- The MCP SDK is a moving protocol dependency; it is pinned like every managed package, and the
  host's protocol surface stays small (tools only) to keep that pin cheap.
- Polling for events costs an agent a round trip per check; accepted for v1 (agents are
  human-scale), revisitable if measured to matter.

## Alternatives Considered

- **Agents drive the CLI (no host):** no new package, but no standard discovery, no typed schemas,
  no in-context images, no transferable integration. Kept available; rejected as the deliverable.
- **A C++ `atlantis mcp` subcommand** (links `atlantis_remote_client` only, hand-rolled
  JSON-RPC/tools): no managed dependency, but a hand-maintained MCP implementation beside a C# SDK
  that already speaks the wire. Rejected.
- **A wire v2 designed agent-first:** rejected on principle — agents are clients, not protocol
  authors (ADR-0125 D2).
