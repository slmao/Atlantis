# ADR 0034: Stable Public Boundary (Schema / Identity / Protocol) versus Internal C++ Layout

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0009-long-term-engine-architecture-alignment.md](../specs/0009-long-term-engine-architecture-alignment.md)

## Context

Atlantis's existing, `Accepted` architecture already distinguishes
public and internal boundaries in one concrete place: RHI's public
headers never expose a `Vk*` type or an internal Vulkan Backend object
layout ([ADR-0001](0001-rhi-backend-independence.md)), and Shader
System's public schema (`ReflectionMetadata`) is deliberately a
versioned, Atlantis-owned re-projection of Slang's own raw,
undocumented, unversioned reflection JSON — never that raw JSON
re-exposed verbatim ([ADR-0030](0030-shader-system-reflection-strategy-and-rhi-boundary.md)).
Both are narrow, subsystem-scoped instances of the same underlying idea,
decided independently, without a shared name or a stated general
principle.

A human-provided external architecture draft names this idea explicitly
as a long-term principle: what stays stable across releases is *Schema,
SDK surface, Asset identity, Protocol, Package contract, Serialization
contract* — not *C++ class layout, ECS memory layout, internal object
pointers*. Several items on Atlantis's own Candidate Spec Backlog will
need exactly this distinction once they're specced: Candidate 6
(World/ECS Foundation) will need to decide what about entities/components
is externally stable versus freely refactorable; Candidate 7
(Serialization and Stable Identity) is *already* framed around exactly
this question in the backlog's own "Intended Outcome" column.

Without a named, general principle, each future Spec would need to
independently re-derive "which parts of my public surface must survive
an internal refactor" from first principles, with no shared vocabulary
and no precedent beyond the two narrow, subsystem-specific cases above.

## Decision

**A cross-cutting long-term principle is adopted: Atlantis's externally-
stable public boundary is expressed through schema, identity, and
protocol concepts — never through direct exposure of internal C++ type
layout, memory representation, or object pointers whose lifetime an
internal subsystem controls.** This generalizes, and is grounded in,
ADR-0001's and ADR-0030's own already-`Accepted` instances of the same
idea; it does not change either of them.

- "Stable" here means: a public consumer (a future SDK binding, a
  serialized asset, a network message, a future Editor/Client) can
  depend on the *shape and identity* of a schema/protocol concept
  surviving an internal refactor, without needing to depend on how that
  concept happens to be laid out in C++ memory today.
- This principle applies **prospectively** — it does not require
  retrofitting anything already `Accepted`/`Approved`/implemented. It
  governs how *future* Specs (World/ECS, Serialization/Identity, Asset,
  Runtime Client boundary, Package System) design their own public
  surfaces, the same way ADR-0001 already governs RHI's and ADR-0030
  already governs Shader System's.
- This ADR does not itself define any schema format, identity scheme
  (e.g. GUID width/generation scheme), serialization format, or protocol
  — those are Requirements a future Spec must satisfy, not decisions this
  ADR makes on that Spec's behalf.
- Internal implementation (ECS storage layout, internal object graphs,
  internal pointers) remains free to change without being treated as a
  public API break, *provided* the schema/identity/protocol surface built
  on top of it is preserved or the change goes through this repository's
  own existing compatibility/versioning discipline once a future Spec
  defines one (see Out of Scope).

## Out of Scope (left to future Specs, not decided here)

- Any concrete schema definition language, reflection mechanism, or
  identity/GUID scheme.
- Serialization format choice, versioning/migration mechanism, or
  wire-protocol design.
- Whether/how a future SDK (C++, or any other language binding) is
  generated from schema versus hand-written.
- Package manifest format or Package System design.
- Any concrete API surface for a future World/ECS, Asset, or Runtime
  Client Spec — this ADR states a boundary *principle* those Specs must
  satisfy, not their APIs.

## Consequences

### Positive

- Gives Candidate 6 (World/ECS) and Candidate 7 (Serialization and
  Stable Identity) a named, pre-agreed principle to design against,
  rather than each independently rediscovering "don't expose internal
  layout" the way ADR-0001 and ADR-0030 each independently did.
- Makes explicit that this is not a new idea invented for this ADR —
  it is the generalization of two decisions this repository has already
  made and lived with (RHI, Shader System reflection), reducing the risk
  of the principle being read as untested aspiration.
- Supports Atlantis's own stated Phase 1 value of Renderer/RenderGraph
  and Backend implementations being freely refactorable
  ([AGENTS.md](../AGENTS.md) Architecture principles) by giving that same
  freedom a name that extends naturally to future Runtime-side
  subsystems.

### Negative / Trade-offs

- A principle stated ahead of the concrete subsystems it will govern
  carries the usual risk that the real World/ECS or Serialization Spec
  finds a case this ADR's language does not cleanly cover (e.g. a
  performance-critical path where *some* internal detail arguably needs
  to be part of the stable contract) — that Spec remains free to propose
  a documented, reviewed exception; this ADR does not forbid revisiting
  it, only requires doing so explicitly.
- Two already-`Accepted` decisions (ADR-0001, ADR-0030) are now also
  framed as instances of a newly-named general principle; readers must
  not mistake this ADR as having *changed* either of them — it does not,
  and neither is reopened or modified by this ADR.

## Alternatives Considered

- **Leave the principle implicit, let each future Spec re-derive it.**
  Rejected: this is the status quo, and it already produced two
  independently-worded (though compatible) instances of the same idea in
  ADR-0001 and ADR-0030 — naming the principle once reduces the risk of
  a future Spec drifting into a genuinely different, incompatible
  answer.
- **Adopt the external draft's full schema-driven-ecosystem design now**
  (Schema generating C++ API, C# binding, Luau types, Python client, RPC
  messages, serialization, Agent tool schema, and documentation, all from
  one source of truth). Rejected for this round: no schema mechanism,
  no second language binding, and no RPC layer exist or are specced yet
  — deciding the generation mechanism now would be speculative
  abstraction with no concrete consumer to validate it against.
- **Fold this into ADR-0033 (Runtime Authority and Client Boundary).**
  Considered: the two are related (both concern what crosses a public
  boundary) but distinct in scope — ADR-0033 is about *who* may access
  Runtime state and *how* (Client model, access categories); this ADR is
  about *what form* a stable public boundary takes regardless of which
  subsystem exposes it (also applying to Asset, Serialization, and
  Package, not just Runtime/World access). Keeping them separate lets
  either be revisited independently.
