# ADR 0032: Conceptual Architecture Layers versus Source-Module Ownership

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0009-long-term-engine-architecture-alignment.md](../specs/0009-long-term-engine-architecture-alignment.md)

## Context

A human-provided external architecture draft (not part of this
repository, referenced but not reproduced by
[specs/0009](../specs/0009-long-term-engine-architecture-alignment.md))
proposes a five-layer conceptual model for a long-term engine product:
`Products/Clients`, `Public SDK`, `Authoritative Runtime`, `Runtime
Services`, `Core/Platform`.

Atlantis already has an established, narrower view: **nine top-level
source modules** (Core, Platform, RHI, Vulkan Backend, RenderGraph,
Renderer, Shader System, Runtime, Tools), named directly in
[AGENTS.md](../AGENTS.md) and detailed (status: `PROPOSED — pending
spec/ADR approval`, not yet itself `Accepted`) in
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md).
Five of those nine modules (Core, Platform, RHI, Vulkan Backend,
RenderGraph, Renderer, Shader System — seven, in fact) already have
`Accepted` ADRs and `Approved`, implemented Specs governing their real
source/build/dependency structure.

These two views are not obviously reconcilable by simple substitution:
the nine-module view is a **source/build ownership view** (which CMake
target owns which files, and which target may depend on which), while
the five-layer view is a **conceptual product-architecture view**
(which kind of responsibility sits at which distance from raw hardware
and from an external client). Naively replacing the nine-module view
with the five-layer view would silently invalidate every `Accepted` ADR
and `Approved` Spec that reasons in terms of the nine modules (e.g. "only
the Vulkan Backend module may include Vulkan headers," per
[ADR-0001](0001-rhi-backend-independence.md)) — an uncontrolled
architectural decision this ADR exists to prevent, per AGENTS.md's own
Golden Rule.

## Decision

**The five-layer conceptual model and the nine-module source-ownership
model are both retained, as two orthogonal, non-competing views of the
same system — neither supersedes the other.**

- The **nine top-level modules** (as named in AGENTS.md and detailed in
  `docs/architecture/module_boundaries.md`) remain the authoritative
  answer to "which CMake target owns this file, and what may it depend
  on." No Spec, ADR, or Plan may reassign a source file's module
  ownership, or add a dependency edge between modules, on the strength
  of the five-layer view alone.
- The **five conceptual layers** (Products/Clients, Public SDK,
  Authoritative Runtime, Runtime Services, Core/Platform) may be used,
  going forward, as a **non-binding, descriptive lens** for reasoning
  about where a *future* capability conceptually belongs, before that
  capability has its own Spec and therefore its own concrete module
  assignment. A rough, illustrative (not authoritative) mapping today:
  `Core`/`Platform` sit in Core/Platform; `RHI`/`Vulkan
  Backend`/`RenderGraph`/`Renderer`/`Shader System` sit in Runtime
  Services and parts of Authoritative Runtime; `Tools` and the
  as-yet-unbuilt `Runtime` module span Authoritative Runtime and
  (eventually) the SDK/Client boundary. This mapping is illustrative
  only and is not itself an architectural decision — it does not fix
  any module's real dependency edges.
- Every *future* Spec that introduces or restructures a top-level module
  (Runtime, or any new module a future Spec proposes) decides that
  module's real position in **both** views explicitly, as part of its
  own Architectural Impact section — this ADR does not pre-decide that
  placement for any module not yet specced.
- `docs/architecture/module_boundaries.md` is not modified by this ADR
  or by [Spec 0009](../specs/0009-long-term-engine-architecture-alignment.md).
  Any future update reconciling it with the five-layer view (e.g. adding
  a cross-reference section) is left to a future Plan, per Spec 0009's
  own Non-Goals.

## Consequences

### Positive

- Removes the false choice between "adopt the external draft's diagram
  wholesale" and "ignore it entirely" — Atlantis can use the five-layer
  language when discussing long-term product shape without disturbing
  any `Accepted` ADR's own module-boundary reasoning.
- Gives future Specs (Runtime Host, World/ECS, Asset, Tool/Editor
  Protocol) a shared vocabulary for describing where a new capability
  sits conceptually, while still requiring each of them to do the real
  work of assigning it a concrete module and dependency edges.
- Keeps the nine-module view as the single source of truth a build
  system, a dependency-boundary test, or a future machine-readable
  architecture rule (see
  [ADR-0036](0036-agent-native-automation-and-machine-verifiable-architecture-as-long-term-goals.md))
  can actually enforce — the five-layer view, being conceptual, is not
  by itself enforceable against source code.

### Negative / Trade-offs

- Maintaining two named views is a real cognitive-overhead cost;
  without this ADR being read, someone touching a future Spec unfamiliar
  with this ADR could still conflate them (see this ADR's Related Spec
  Risks section).
- Because the five-layer mapping above is explicitly illustrative and
  non-binding, it invites future bikeshedding about which layer a given
  future module "really" belongs in — deliberately not resolved by this
  ADR, since that resolution belongs to that module's own future Spec.

## Alternatives Considered

- **Replace the nine-module view with the five-layer view outright.**
  Rejected: would silently invalidate the module-boundary reasoning in
  every `Accepted` ADR from ADR-0001 onward, and is exactly the kind of
  "the AI quietly picks a module boundary" failure mode AGENTS.md's
  Golden Rule exists to prevent — no Spec or ADR round has re-reviewed
  those existing decisions against a five-layer restructuring.
- **Reject the five-layer view entirely, keep only the nine-module
  view.** Considered and not adopted: the five-layer view has real
  descriptive value for reasoning about *future*, not-yet-specced
  capabilities (Runtime, SDK, Client boundary) where the nine-module
  view alone gives no vocabulary for "how far this capability sits from
  a Client" — a distinction several future Candidate Backlog items
  (Runtime Host, Tool/Editor Connection Protocol) will need.
- **Fold the five-layer view into `docs/architecture/module_boundaries.md`
  directly, this round.** Rejected for this round specifically: that
  file is out of this Spec's authorized file scope (see Spec 0009's own
  Non-Goals) — a future Plan, once this ADR and its Spec are approved,
  decides how (or whether) to cross-reference it there.
