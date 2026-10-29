# Spec: The Agent Tool Layer — an MCP Host over atlantis.remote/1

- **Status:** Approved
- **Author:** slmao (drafted by ZCode at explicit human direction, 2026-10-29)
- **Created:** 2026-10-29
- **Related Plan(s):** [Plan 0070](../plans/0070-agent-tool-layer.md) (`Approved`; Joint Human Review
  2026-10-30 — J1–J6 each as recommended — recorded in its header)
- **Approval:** slmao, 2026-10-30 — owner rulings Q1–Q6, **each as recommended**, recorded on this
  Spec's own branch PR, [PR #306](https://github.com/slmao/Atlantis/pull/306), before it merges.
  The open-question text keeps the options as the record of what was weighed.
- **Related ADR(s):** [ADR-0138](../adr/0138-agent-tool-layer-host.md) (`Accepted` 2026-10-30, with
  this Spec's review: the host's placement, dependency policy and tool shape)

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

A Model Context Protocol (MCP) server host that exposes the existing `atlantis.remote/1` client
surface — the Runtime World's query/command family, `RuntimeControl`, and the authoring document —
as standard, discoverable tools, so that any MCP-capable agent (an AI coding agent, a script, a CI
job) becomes one more ordinary Atlantis client. The engine, the wire and both SDKs are unchanged;
the host links the C# SDK and adds no capability any other client lacks.

## Motivation / Problem Statement

Everything an agent needs to operate a running Runtime is already on the wire, and the evidence is
in the tree: the CLI (`scene`/`runtime`/`entity` groups), the C# SDK (reflective and typed, speaking
the protocol itself), and Spec 0068's J7 acceptance run — pick, edit, apply, save, rebuild-restart,
same-frame check, second-client conflict — executed entirely through ordinary clients.

What is missing is the *tool shape*. Today an agent must be hand-integrated: bespoke scripting
against the CLI's text output or the protocol's JSON, with no discovery, no typed tool schemas, and
an integration that does not transfer between agent hosts. MCP is the de-facto standard that agent
hosts speak (tools listed, typed, called; results with text and images back in context). An MCP
host turns "an AI operates the engine" from a per-agent integration project into configuration.

This is the first step of the editor platform's AI-native phase (the blueprint's maintainer
direction, 2026-10-29) and the cheapest one: it reuses the whole existing surface and changes no
engine code.

### What exists, and what this Spec builds on without reopening

- The attachable Runtime (Spec 0055, ADR-0106): loopback JSON-lines RPC, session file with port and
  token; a client is anything that connects.
- The three client families of `atlantis.remote/1` (the reference document is
  [docs/architecture/remote_protocol.md](../architecture/remote_protocol.md); where it and the code
  differ, the code is right): `connection.*` (the Runtime World's Query/Command/Event/Transaction),
  `control.*` (`RuntimeControl`: status, pause/resume, step with capture, diagnostics, viewport
  camera, raycast, viewport overlay — ADR-0128, ADR-0132), and `authoring.*` (the authoring
  document: open, nodes, components, properties, the closed command set with closed refusals,
  apply, save, events — ADR-0130; the apply/reload contract of ADR-0131).
- The C# SDK (Spec 0058, ADR-0112/ADR-0113): managed, zero-NuGet, speaks the protocol itself,
  with batched reads and the reflective authoring session already in it.
- The CLI's session discovery: `--session <path>`, else `ATLANTIS_SESSION`, else
  `./.atlantis/runtime.session.json`.
- ADR-0125 D2's rule — no editor-only method, channel or file. The host adds none: every tool is
  one or more wire calls that any client could make today. This Spec keeps that rule as the
  invariant that makes the agent a peer, not a privileged operator.

## Goals

- Any MCP client can attach to a running Runtime through this host: list tools, call them, and get
  typed results.
- The tool surface covers the whole client surface: both query families, both command families,
  the authoring lifecycle (open/apply/save), and `RuntimeControl` including captures — closed and
  versioned with the wire.
- The wire's refusal sets surface as structured tool errors, reasons verbatim — an agent sees
  "refused: ParentExists", not a swallowed failure.
- Agents learn the schema in-band: component and field names, kinds and domains, through tools.
- Captures come back as images the agent can see, not just paths.
- The host is stateless except its one session: it holds no engine state, and dies with its
  session.

## Non-Goals

- No wire call, World verb, engine path or SDK surface change — including no generic
  `ExecuteCommand` (the closed command sets are ADR-0130's deliberate boundary).
- No push: MCP notifications/server-initiated messages are not used; events arrive by polling
  (Q3).
- No authentication or multi-user anything: loopback, one user, exactly as the wire is today.
- No Android target; no embedding in the Runtime process (the host is a separate client, like the
  CLI and the editor).
- Not a "headless editor": the clients already are headless; this is a tool surface, not a
  product.
- No semantic/world-model layer: scene understanding, procedural generation, evaluation policies
  are future work *on top of* these tools.
- No MCP server surfaces beyond tools (no resources, no prompts) in this Spec.

## Requirements

### Functional

- **R1 — The host.** A managed console host, one process bound to one Runtime session (the CLI's
  discovery: `--session`, `ATLANTIS_SESSION`, the default path), serving MCP over stdio; it exits
  when its stdin closes or the Runtime session dies. It links the C# SDK by project reference and
  nothing native.
- **R2 — The exact tier.** A closed, 1:1 mapping over the wire's calls, named after them
  (`world.list_entities`, `world.get_property`, `world.submit_transaction`, `world.drain_failures`,
  `world.schema`, `authoring.list_nodes`, `authoring.get_property`, `authoring.submit_transaction`,
  `authoring.open`, `authoring.document`, `authoring.apply`, `authoring.save`,
  `authoring.active_camera`, `authoring.schema`, `control.status`, `control.pause`,
  `control.resume`, `control.step`, `control.raycast`, `control.set_viewport_camera`,
  `control.set_viewport_overlay`, `control.diagnostics`). Each tool's result is the wire result
  as structured JSON; nothing is invented.
- **R3 — The convenience tier.** A small closed set of composed tools an agent reaches for first —
  at least `create_node`, `destroy_node`, `set_properties`, `add_component`, `remove_component`,
  `set_parent`, `set_active_camera` — each composing client-side into exactly one
  `authoring.submitTransaction` of the closed command set (the editor's own pattern), never more;
  applying stays an explicit `authoring.apply` call.
- **R4 — Refusals.** Every wire refusal maps to a tool error carrying the refusal's reason
  verbatim (the closed error sets of both families). The host retries nothing and hides nothing.
- **R5 — Schema in-band.** `world.schema` and `authoring.schema` return the schema types (the
  protocol's §6.7 shape) as structured JSON; each tool's description states the editing model it
  belongs to (document edits land on `apply`; Runtime World writes are disposable runtime-side
  tweaks, ADR-0131 D7).
- **R6 — Events.** The host holds one subscription per family (created at connect, empty filter)
  and exposes `world.drain_events` and `authoring.drain_events`; the wire's subscribe/unsubscribe
  calls themselves are not tools.
- **R7 — Captures.** `control.step` with an image request returns the frame report and the PNG both
  as its file path and as an MCP image content block, so the agent sees the frame in-context.
- **R8 — Packaging and build.** Its own managed project under the ADR-0127 mechanism: its own
  allow-list (`src/mcp/allowed_packages.txt`, the MCP SDK the first entry), every package pinned
  exactly, committed lock files, locked-mode restore, its own `NuGet.config`, offline feed support,
  checked by the same policy script; the C# SDK's zero-NuGet rule is unchanged. CMake declares it
  only with `ATLANTIS_BUILD_MCP=ON` (default `OFF`) and a .NET 10.0.1xx SDK; Android never.
- **R9 — Versioning.** The tool set is versioned with the wire (`atlantis.remote/1`); a new tool
  follows a Spec that adds the capability for the wire, the CLI and both SDKs together, exactly as
  ADR-0125 D2 requires — the host never gains a capability first or alone.

### Non-functional

- **Performance:** a tool call is one wire round trip (two for the convenience tier's read-then-
  submit, none planned); no throughput target — an agent's cadence is human-scale. The host adds
  no protocol of its own to keep fast.
- **Memory:** the host holds its session, its two subscriptions' pending events, and nothing
  scaled by scene size.
- **Portability:** Windows first (as every client is today); managed-only code, no OS calls beyond
  stdio and the loopback socket the SDK makes.
- **Other:** loopback only; the host opens no listening socket (stdio server only).

## Proposed Design

```
Agent host (Claude Code / any MCP client)          the editor, the CLI, scripts
        │ stdio, MCP (JSON-RPC)                          │ atlantis.remote/1
        ▼                                                 ▼
  atlantis-mcp  ──────────── C# SDK ────────────►  atlantis_runtime --listen
  (one process,      (project reference,             (loopback, session file,
   one session)       zero NuGet)                     the one engine)
```

- **Tier 1 tools** are one-line forwards: tool in, wire call, wire result out as JSON. The mapping
  table is data, generated or hand-declared against the protocol document, and unit-tested call by
  call.
- **Tier 2 tools** compose: `create_node` reads nothing, builds one `CreateNode` (a minted GUID, a
  position, a component list) and submits one transaction; `set_properties` builds one
  transaction of `SetProperty`s. The composition rules live in the host, stated once, tested
  against the same fixtures the SDK uses.
- **Events** accumulate in the host's two subscriptions; `drain_events` returns and clears them.
  The pattern the tools' descriptions teach: act → `authoring.apply` → `authoring.drain_events`
  until `Applied{generation}` → `control.step` with capture → look.
- **An agent session** (the J7 shape through tools): `authoring.open` → `authoring.schema` →
  `set_properties(node, "Light.intensity", 4)` → `authoring.apply` →
  `authoring.drain_events` (until applied) → `control.step(image)` → the agent sees the frame and
  decides.

The MCP protocol revision and the exact MCP SDK package are pinned by the Plan (implementation
detail, like every pinned dependency).

## Architectural Impact

Yes — three decisions, all recorded in [ADR-0138](../adr/0138-agent-tool-layer-host.md):

1. A new managed module outside the native dependency graph (`src/mcp/`), a client like the CLI
   and the editor, linking the C# SDK only.
2. A second managed area allowed NuGet packages, under the ADR-0127 mechanism with its own
   allow-list (ADR-0127's mechanism extended, not its allow-list).
3. The tool shape: closed, versioned with the wire, an exact tier plus a composed tier that never
   leaves the closed command set (ADR-0125 D2 held).

## Alternatives Considered

- **Agents drive the CLI (no new host).** Works today and stays available; rejected as the
  deliverable because it has no standard discovery, no typed tool schemas, no images in-context,
  and the integration does not transfer between agent hosts.
- **A C++ `atlantis mcp` subcommand** (links `atlantis_remote_client`, hand-rolled JSON-RPC).
  No new package, but a hand-maintained MCP implementation against a moving protocol, beside a C#
  SDK that already speaks the wire. Rejected; revisitable if the managed toolchain ever goes.
- **A wire v2 designed agent-first.** Rejected on principle: the wire is deliberately closed and
  client-serving; agents are clients, not protocol authors (ADR-0125 D2).

## Testing & Verification Plan

- **Unit (GPU-free):** every tier-1 tool maps to the exact wire call and result against a fake
  session (the C# SDK's fixture pattern); every tier-2 tool composes the exact transaction;
  refusals carry their reasons; the event drains accumulate and clear.
- **Live integration (GPU):** a scripted agent session through the MCP host against
  `atlantis_runtime --listen` on the default scene and Bistro — open, schema, edit, apply,
  drain until `Applied`, capture, second-client conflict refused — asserting tool results at each
  step (the J7 shape, through tools).
- **Policy:** the package checks extended to the host's allow-list (refusals tested, as the editor
  app's are); the C# SDK's zero-NuGet check unchanged and green.
- **Unaffected engine:** goldens byte-identical; `--gate --applies` false; the conformance vectors
  untouched.

## Risks & Open Questions

- **Q1 — What the host is.** (ADR-0138 D1)
  - **Recommended:** a C# console host in `src/mcp/` (`Atlantis.Mcp`), project-referencing the C#
    SDK, speaking MCP over stdio through the official MCP SDK package, pinned, under the ADR-0127
    mechanism with its own allow-list.
  - The alternatives: the C++ CLI subcommand (above, rejected); or a repository-external host
    (fragmented, unpinned — rejected).
- **Q2 — The tool tiers.** (ADR-0138 D3)
  - **Recommended:** both tiers — the exact 1:1 tier (the whole surface, no invention) plus the
    small composed tier (agent ergonomics), with apply always explicit.
  - The alternative: exact-only (purest, but every edit becomes a hand-composed command list —
    measurable friction for tool selection in agent hosts).
- **Q3 — Events.** (ADR-0138 D4)
  - **Recommended:** host-held subscriptions with `drain_events` tools; no push.
  - The alternatives: exposing subscribe/unsubscribe as tools (stateful tool calls, awkward for
    agents), or MCP notifications for push (a later Spec if agents need it).
- **Q4 — Captures as image content.** **Recommended:** yes, path and image block; the alternative
  (path only) leaves the agent blind without a file reader.
- **Q5 — Transport and revision.** **Recommended:** stdio only in this Spec; the MCP revision
  pinned by the Plan. Streamable HTTP (a remote agent host) is a later addition if ever needed.
- **Q6 — What the host may compose.** (ADR-0138 D3's boundary)
  - **Recommended:** transactions from the closed command set only — never new semantics, never
    client-side validation beyond argument shape. The host is a shape-maker, not a policy-maker.

## Out of Scope / Future Work

- MCP resources/prompts server surfaces; MCP push notifications; multi-session multiplexing in one
  host process.
- Authentication, remote (non-loopback) transport.
- An "AI panel" inside Atlantis Editor App (editor work, later, over the same SDK).
- The semantic layer: scene generation, evaluation loops, world-model integration — future Specs
  that will call these tools.
- C#-authored gameplay components, the asset browser, plugins — the roadmap's other items, each
  its own Spec.
