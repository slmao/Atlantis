# ADR 0033: Runtime Authority and Client Boundary

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0009-long-term-engine-architecture-alignment.md](../specs/0009-long-term-engine-architecture-alignment.md)

## Context

Atlantis does not yet have a real `Atlantis Runtime` module — it exists
today only as a `PROPOSED`, not-yet-`Accepted` description in
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md#atlantis-runtime),
which currently frames Runtime narrowly as "the application/executable
layer... the composition root, not a library other modules link
against." No Editor, no external tool, and no second Runtime-consuming
process exists in this repository today; `specs/0003`'s own frame-loop
composition is explicitly disclosed (in that Spec and in
`specs/README.md`'s own registry) as non-shipping verification code, not
a preview of Runtime's real architecture.

A human-provided external architecture draft proposes a specific,
detailed model: Runtime compiled as an independent library
(`EngineRuntime`), a thin `RuntimeHost` executable that loads it, an
`IRuntimeConnection` abstraction with in-process and remote
implementations, and Editor, AI agents, and Python research clients all
treated symmetrically as Clients of that Runtime through a
Query/Command/Event/Snapshot boundary — never given special internal
pointer access.

Several items already on Atlantis's own Candidate Spec Backlog
(`specs/README.md` Section B) will need to resolve exactly this kind of
question when they are eventually specced: Candidate 5 (Runtime Host and
Composition Root), Candidate 6 (World/ECS Foundation), and Candidate 8
(Tool/Editor Connection Protocol). Deciding the *mechanism* now (library
vs. executable structure, in-process vs. remote transport, IPC protocol,
process model) would be exactly the kind of premature, uncontrolled
architectural decision AGENTS.md's Golden Rule prohibits — none of those
future modules has a Spec yet, and no concrete consumer exists to test
the decision against. But leaving the *authority and access* question
completely unaddressed risks each future Spec re-deciding it
independently, or an Editor-shaped future Spec quietly re-introducing
special internal access "just this once" before the principle is ever
written down.

## Decision

**Runtime, once it exists as a real module, is the sole authoritative
owner of engine world state. Every external module that observes or
modifies that state — including any future Editor — does so as a
Client, through the same category of access surface, never through a
raw internal pointer or a privileged back door.** Concretely, as a
long-term invariant binding future Specs, not a design decided today:

- Runtime owns the one authoritative copy of whatever state a future
  World/ECS Spec defines. No other module (Editor, a future scripting
  layer, a future AI/research client) holds or is granted its own
  authoritative copy.
- A future Editor Spec must treat Editor as a Client of Runtime, subject
  to the same category of access boundary as any other Client (a future
  AI agent, a future automation/research client) — not as a distinguished
  module with direct access to Runtime's internal C++ types or memory
  layout. This does not itself decide whether Editor and Runtime run
  in the same process or different processes — see Out of Scope below.
- Public, cross-module access to Runtime state is expressed through a
  small family of access *categories* — reading (query-shaped),
  mutating (command-shaped), and observing (event-shaped) — rather than
  through direct field access or exposed internal pointers. This ADR
  fixes the *category distinction*, not concrete type names, method
  signatures, or a transaction/undo model — those belong to whichever
  future Spec designs the real World/Runtime API.
- No public, cross-module API may return or accept a raw pointer/reference
  to a Runtime-owned object whose lifetime Runtime itself controls (e.g.
  an internal entity record, a component array). This continues an
  Atlantis Golden-Rule-adjacent pattern already present in the existing,
  `Accepted` architecture — RHI already never exposes an internal `Vk*`
  handle across its own public boundary — extended here as a Runtime-wide
  principle for a future World/ECS Spec to apply to entity/component
  access specifically.

## Out of Scope (left to future Specs named explicitly, not decided here)

- Whether Runtime is built as a linked library, a statically-linked
  executable, or some other packaging shape.
- Whether a Client (in particular, a future Editor) runs in-process or
  out-of-process relative to Runtime, and any IPC/transport/protocol
  mechanism for the out-of-process case.
- The concrete shape of query/command/event types, any transaction or
  undo/redo model, or any snapshot/replay mechanism.
- Entity/handle representation (index+generation or otherwise), ECS
  storage layout, or any other World/ECS implementation detail —
  entirely deferred to Candidate 6 (World/ECS Foundation).
- Runtime's own internal threading/concurrency model beyond what
  [ADR-0004](0004-phase1-threading-baseline.md)'s existing Phase 1
  single-frame-thread baseline already states, which this ADR does not
  change.
- Whether or how this principle applies to Atlantis's own **existing**
  Spec 0003 non-shipping verification composition — that code is already
  explicitly disclosed as not a preview of Runtime's real architecture,
  and this ADR does not require retrofitting it.

## Consequences

### Positive

- Gives Candidate 5 (Runtime Host), Candidate 6 (World/ECS), and
  Candidate 8 (Tool/Editor Connection Protocol) a shared starting
  constraint, reducing the risk that whichever of them is specced first
  locks in an Editor-privileged access model the others then have to
  work around.
- Keeps the door open for Runtime to run headless, remotely, or
  alongside multiple simultaneous Clients later, without a future Spec
  needing to retrofit "oh, and now Editor doesn't get direct access
  either" as a breaking change.
- Consistent with a pattern already `Accepted` elsewhere in this
  codebase (RHI never exposing internal Vulkan handles across its public
  boundary) — this ADR generalizes an already-approved pattern rather
  than inventing an unrelated one.

### Negative / Trade-offs

- A future Editor Spec inherits a real constraint it must design against
  from day one, even before Editor's own concrete requirements are
  known — could turn out to be more restrictive than a future Editor
  genuinely needs (e.g. a tight in-process debugging/inspection tool
  might reasonably want cheaper access than a general Client boundary
  provides). That future Spec remains free to propose a documented,
  reviewed exception; this ADR does not forbid revisiting it, only
  requires doing so explicitly rather than by default.
- Establishes a principle with no real module or consumer to validate it
  against yet — carries the ordinary risk of any ADR written ahead of
  its first concrete implementation: it may need amendment once a real
  World/ECS or Runtime Host Spec actually tries to build against it.

## Alternatives Considered

- **Decide the full external-draft mechanism now** (library/host split,
  `IRuntimeConnection`, in-process/remote/device/cloud connection
  variants, channel taxonomy). Rejected for this round: none of it has a
  concrete consumer yet, and Spec 0009's own governance boundary
  explicitly defers IPC protocol, process model, and transport
  decisions to their own future Specs.
- **Say nothing now, let each future Spec (Runtime Host, World/ECS,
  Tool/Editor Protocol) decide authority and access independently.**
  Rejected: this is precisely the risk this ADR exists to close off —
  without a shared prior principle, an Editor-shaped Spec drafted before
  a Runtime-Client-boundary Spec could easily default to "Editor gets
  direct access, it's simpler," which later Specs would then have to
  either accept as precedent or break as a compatibility change.
- **Treat this as pure vision/roadmap prose in the Spec, not an ADR.**
  Considered: this repository's own convention (per AGENTS.md) is that
  ADRs record decisions "for the permanent record of *why*," and this
  principle is intended to actually bind how future Specs are written,
  not merely describe an aspiration — an ADR is the more accurate
  instrument for that than Spec prose that no future Spec is formally
  required to honor.
