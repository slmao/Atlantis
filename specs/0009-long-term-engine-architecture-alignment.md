# Spec: Long-Term Engine Architecture Alignment

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, from a human-provided external architecture draft;
  approved by human review — see Human Review Approval below.
- **Created:** 2026-08-15
- **Related Plan(s):** None yet — this Spec's own approval does not
  itself authorize Implementation (see Non-Goals). Because this Spec
  proposes a concrete future documentation deliverable
  (`docs/architecture/engine_architecture.md` — see Architectural
  Impact), drafting **Plan 0009** is authorized per
  [AGENTS.md](../AGENTS.md) as the next step following this approval;
  Plan 0009 remains subject to its own Human Review before any
  Implementation, and is not created by this approval itself — unlike
  Specs 0001–0008, this Spec is not expected to close out with "no Plan
  is needed."
- **Related ADR(s):**
  [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md),
  [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md),
  [ADR-0034](../adr/0034-stable-public-boundary-versus-internal-cpp-layout.md),
  [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md),
  [ADR-0036](../adr/0036-agent-native-automation-and-machine-verifiable-architecture-as-long-term-goals.md),
  and
  [ADR-0037](../adr/0037-long-term-device-backend-extensibility-without-phase1-scaffolding.md)
  — all `Accepted` alongside this Spec's own approval below. See
  Architectural Impact.
- **Human Review Approval (2026-08-15):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-15, following an independent,
  read-only architecture review of this Spec, ADR-0032–0037, and PR #39
  (covering git/PR state verification; the Device Backend long-term
  design in ADR-0037 against AGENTS.md's current Vulkan-only/no-second-
  backend-scaffolding/iOS-undecided/macOS-and-Linux-not-targeted rules;
  the proposed future `docs/architecture/engine_architecture.md`
  overview document and its documentation-authority boundaries; each
  ADR's individual clarity and long-term maintainability; Candidate
  Backlog non-disturbance; and a full documentation-consistency pass) and
  a dedicated HR-0009 decision table covering all sixteen topics listed
  there — this Spec, and ADR-0032 through ADR-0037, are approved as
  drafted, with one non-blocking clarification and one confirmed
  process point, both resolved as part of this approval:

  1. **ADR-0037's citation of AGENTS.md's iOS-backend-undecided rule
     (previously "...is not to be prejudged") is corrected to quote
     AGENTS.md verbatim** ("...is not to be designed or scaffolded for
     now") — see that ADR's own Context section. This was a quotation-
     accuracy issue only; the substantive meaning (iOS Metal-vs-MoltenVK
     remains undecided) was never in question and is unchanged.
  2. **Plan 0009 is confirmed required** before
     `docs/architecture/engine_architecture.md` may be created. This
     approval authorizes drafting Plan 0009 — scoped to that document
     plus related navigation-only updates to
     `docs/architecture/module_boundaries.md`, `docs/project-blueprint.md`,
     and optionally a short `README.md` entry point — through its own
     Human Review; it does not authorize `AGENTS.md` changes without a
     separately justified reason, and does not itself create any file.

  This approval does not authorize any Direct3D 12, Metal, or other
  second-Device-Backend code, directory, CMake target, SDK dependency,
  or scaffolding of any kind; does not change Phase 1's Vulkan-only
  scope; does not decide whether a future iOS Platform uses Metal or
  Vulkan via MoltenVK; and does not modify the existing Candidate Spec
  Backlog (`specs/README.md` Section B) or its priorities.

## Summary

This Spec aligns Atlantis's long-term architectural direction with a
human-provided external architecture draft (`ENGINE_ARCHITECTURE.md`,
not part of this repository and not reproduced here — see Documentation
Authority below), **without** implementing, designing, or authorizing
any new subsystem. It is a governance/direction document, not a Runtime,
ECS, Asset, Editor, or SDK design. It states a small number of long-term
principles as `Proposed` ADRs, sorts the external draft's much larger
surface into what Atlantis can adopt now versus what must wait for its
own future Spec, and explicitly identifies where the external draft
conflicts with Atlantis's own `Accepted` Phase 1 decisions — in every
such case, this Spec keeps the existing decision and defers or rejects
the conflicting direction, rather than silently overriding it.

## Motivation / Problem Statement

Atlantis has, over Specs 0001–0008, built a real, `Accepted`,
implemented rendering-first foundation: Core, Windows Platform, a
backend-independent RHI, a Vulkan Backend, RenderGraph, a minimal
Renderer, and a Slang-based Shader System — see
[specs/README.md](../specs/README.md) for the full, current registry.
None of this is changed by this Spec.

What Atlantis does not yet have — Runtime (the module), World/ECS,
Asset, a Runtime/Editor/Client boundary, and a public SDK — is already
named on the Candidate Spec Backlog (`specs/README.md` Section B), but
with no shared architectural direction connecting those future items to
each other or to what already exists. A human maintainer provided an
external, independently-authored architecture draft describing one
coherent long-term vision for exactly that space (Runtime authority,
Client model, Schema-first SDK, Authoring/Runtime separation,
Agent-native development, and more).

That draft is valuable as a reference, but it was not written for this
repository: it assumes its own MVP ordering (which does not match
Atlantis's own already-completed history), its own repository layout
(which does not match Atlantis's own), and states several concrete
positions (a multi-backend graphics roadmap, a multi-target shader
compiler, Linux as an early platform tier, a Job System in MVP 0) that
directly conflict with Atlantis's own `Accepted` Phase 1 decisions. Per
[AGENTS.md](../AGENTS.md)'s own Golden Rule, an external draft — however
well-reasoned — cannot silently become Atlantis's architecture; it must
go through the same Spec → Plan → Human Review process as any other
architectural proposal. This Spec is that process step: sort what's
genuinely adoptable now, from what's a reasonable long-term direction
Atlantis isn't ready to commit to, from what actively conflicts with
decisions already made.

## Goals

- Define the relationship between a conceptual, product-level
  architecture view and Atlantis's existing, source/build-ownership-based
  nine-module view, without replacing either (see ADR-0032).
- State a small number of long-term architectural principles — Runtime
  authority and Client symmetry, a stable schema/identity/protocol
  boundary distinct from internal C++ layout, Authoring/Runtime data
  separation as an available (not mandatory) option, Agent-native,
  vendor-neutral development tooling as a direction, and a reserved
  long-term boundary position for future sibling Device Backends without
  Phase 1 scaffolding — each as its own narrowly-scoped `Proposed` ADR
  (ADR-0033 through ADR-0037).
- Propose a future, not-yet-authorized `docs/architecture/` overview
  document (`engine_architecture.md`) that gives these two views, and
  Atlantis's current-versus-long-term status, a single navigation entry
  point — without itself becoming an authoritative source (see
  Architectural Impact and Documentation Authority).
- Explicitly classify the external draft's major directions against
  Atlantis's current state, so future Specs (in particular the Candidate
  Backlog's Runtime Host, World/ECS, Asset, and Tool/Editor Protocol
  items) inherit a stated position rather than each independently
  re-litigating it.
- Identify, by name, every place the external draft conflicts with an
  `Accepted` ADR or `Approved` Spec, and confirm none of them are changed
  by this Spec.
- Suggest a non-binding dependency ordering for the existing Candidate
  Spec Backlog informed by this alignment work, without rewriting the
  backlog itself or committing any candidate to implementation.

## Non-Goals

This Spec does not:

- Write, generate, or modify any source code, test, CMake file, Shader,
  or CI configuration.
- Implement, or produce a Plan for implementing, Runtime, World, ECS,
  Asset, Asset Database, Editor, SDK (in any language), Package System,
  Job System, a second graphics backend (Direct3D 12, Metal, or any
  other), Shader System language/target extensions, serialization, a
  wire protocol, an ABI, or any AI/UGC/Neural-rendering capability.
  Naming Direct3D 12 and Metal as long-term candidate Device Backends
  (ADR-0037) is not an exception to this Non-Goal — no backend code,
  directory, target, or dependency is created or authorized.
- Modify `AGENTS.md`, any existing `Accepted` ADR, any existing
  `Approved` Spec or its Plan, `docs/architecture/module_boundaries.md`,
  or `docs/project-blueprint.md`. Any future alignment of those documents
  with the ADRs this Spec introduces is explicit future Plan work (see
  Roadmap Impact), not authorized here.
- Reorganize Atlantis's existing nine top-level modules, their CMake
  target structure, or any existing public API.
- Adopt the external draft's own repository layout, module list, MVP
  ordering, or directory structure (`Engine/`, `RFC/`, a two-repository
  `Engine`/`Game` split, etc.) as Atlantis's own.
- Decide any concrete mechanism this Spec's own Requirements section
  marks as deferred to a future Spec — including, but not limited to:
  ECS archetype/chunk design, `EntityId` bit layout, a C ABI or any
  concrete FFI surface, C#/Luau/Python binding design, an IPC/RPC
  protocol, a serialization format, a Package manifest format, a
  Physics/Audio/Inference backend interface, Job System design, Editor
  process model, in-process-vs-remote Runtime transport, Asset Database
  schema, a second graphics backend's concrete interface/selection
  mechanism, a UGC sandbox mechanism, a structured-diagnostic schema, or
  a concrete engine CLI command set.
- Create `docs/architecture/engine_architecture.md` or any other new
  file under `docs/architecture/`. This Spec proposes that document as a
  future Plan deliverable (see Roadmap Impact and Architectural Impact)
  — drafting it is explicitly not authorized by this Spec itself.
- Authorize a Plan or any Implementation. Approving this Spec and its
  ADRs authorizes drafting a Plan for the next concrete step — most
  likely Plan 0009 itself, covering `docs/architecture/engine_architecture.md`
  and related documentation-navigation updates (see Roadmap Impact) — it
  does not itself authorize writing code, CMake, or shaders.

## Requirements

### Immediately-effective invariants (this Spec's own Architectural Impact)

These take effect once this Spec and its named ADRs are `Approved`/
`Accepted`, and bind how *future* Specs are written — they do not
require any change to existing, already-implemented code:

- The five-layer conceptual view and the nine-module source-ownership
  view co-exist as orthogonal, non-competing lenses (ADR-0032).
- A future Runtime holds the sole authoritative copy of engine world
  state; every external module, including a future Editor, accesses it
  as a Client through the same category of access surface, never through
  an exposed internal pointer (ADR-0033).
- Atlantis's externally-stable public boundary is expressed through
  schema/identity/protocol concepts, not internal C++ layout — a
  generalization of an already-`Accepted` pattern (ADR-0001, ADR-0030),
  binding future World/ECS, Serialization, Asset, and Package Specs
  (ADR-0034).
- Authoring-facing and runtime-execution data representations are
  permitted to differ, and a future World/ECS or Asset Spec must
  explicitly address whether they do, rather than defaulting into an
  unconsidered answer (ADR-0035).
- Important development capabilities should move, over time, toward
  being reachable via CLI/API rather than GUI-only, and architectural
  constraints should move toward machine-verifiability — as a
  vendor-neutral direction, not a Phase 1 requirement or an authorization
  to build any specific tool now (ADR-0036).
- Vulkan remains the only implemented Device Backend in Phase 1; a
  conceptual, boundary-level position is reserved for future sibling
  Device Backends without creating any code, directory, target,
  dependency, or abstraction for them now (ADR-0037).

### Long-term direction (compatible, not adopted as binding invariant now)

Recorded here as accepted direction for future Specs to draw on, but
explicitly **not** elevated to ADR/invariant status this round because
doing so now would either lock in a mechanism no concrete subsystem yet
needs, or restate something Atlantis's own existing documents already
say adequately:

- Backend-replaceable services (rendering, physics, audio, navigation,
  inference, networking) as a shape for future Runtime Services —
  consistent with, and not contradicting, [AGENTS.md](../AGENTS.md)'s
  existing "RHI is backend-agnostic in interface; Vulkan is Phase 1's
  only implementation" principle, generalized to future non-rendering
  services once they exist. No second backend for any of these is
  authorized by this Spec; see ADR-0037 specifically for the graphics
  Device Backend case.
- Direct3D 12 and Metal as candidate future sibling Device Backends
  behind RHI's existing boundary, per ADR-0037 — a long-term position,
  not an implementation commitment or timetable. Phase 1 remains
  Vulkan-only; no Direct3D 12/Metal code, directory, CMake target,
  SDK dependency, capability-tier abstraction, or backend-selection
  mechanism is authorized by this Spec or by ADR-0037. WebGPU is not
  given a reserved position by ADR-0037 at all.
- Headless, fixed-step, and snapshot/replay as valuable future Runtime
  capabilities — Atlantis's own existing sequencing (windowed ships
  before headless, per AGENTS.md) is unchanged; this Spec's Roadmap
  Impact section discusses relative near-term priority, not a change to
  that sequencing rule itself.
- Samples-as-executable-specification, package-local Agent context, and
  a documentation hierarchy from architecture down to recipes — reasonable
  future practices once real packages/samples in this space exist; not
  binding process changes today.

### Deferred to a future subsystem Spec (valuable, not decidable yet)

Explicitly out of this Spec's own scope, each requiring its own future
Spec with a concrete consumer before it can be responsibly decided —
matches this Spec's own Non-Goals list:

World/ECS design (entity/component/archetype/query shape, `EntityId`
representation); Runtime Host and composition-root mechanism; Asset
identity, Asset Database, and cook pipeline; Package System and manifest
format; Job System; concrete Query/Command/Event/Transaction/Snapshot
API; Stable Native ABI; Runtime Client transport (in-process vs. remote,
any IPC/RPC protocol); Editor process model; gameplay-language bindings
(C#, Luau, Python) and their trust model; UGC sandbox and capability
model; Research/Observation-Action API; Inference Service and backend
interface; structured-diagnostic schema; Module Manifest format; and a
concrete engine CLI command set.

### Explicitly rejected or deferred for this round

- **Linux as a target platform**, in any form (including "Linux
  Headless" as an early platform tier). AGENTS.md is explicit: Linux is
  not a target platform. This Spec does not reopen that; a future
  platform-scope Spec would need its own explicit Human Review to change
  it.
- **Implementing or scaffolding a second graphics backend now, in any
  form** — RHI capability-tier abstractions (`RayTracingCapability`,
  `MeshShaderCapability`, and similar), backend registries/factories,
  conditional-compilation scaffolding, or second-backend SDK
  dependencies. Phase 1 is Vulkan-only, per AGENTS.md; RHI stays
  backend-independent in interface but no second backend is implemented
  or scaffolded for. Not reopened by this Spec. Naming Direct3D 12 and
  Metal as long-term candidate sibling Device Backends is a separate
  question, addressed above under "Long-term direction" and in
  ADR-0037 — that naming does not reopen, weaken, or except itself from
  this Phase 1 prohibition.
- **WebGPU, in any role, at any time horizon.** Unlike Direct3D 12 and
  Metal, WebGPU is not given even a long-term reserved position by this
  Spec or by ADR-0037 — it is outside this round's scope entirely, not
  merely deferred.
- **Shader multi-target compilation, a permutation system, or runtime/
  hot-reload shader compilation.** Spec 0008 / ADR-0028 already decided
  Slang → SPIR-V only, no DXIL/MSL/WGSL, no runtime compilation, no
  hot-reload. Not reopened, modified, or reinterpreted by this Spec —
  see Compatibility with Existing Architecture below.
- **A Job System as an immediate (MVP 0-equivalent) requirement.**
  ADR-0004's Phase 1 single-threaded frame-orchestration baseline is
  unchanged. A future Core Runtime candidate may propose one later,
  driven by a concrete need, per `docs/architecture/threading.md`'s own
  Open Questions.
- **Rewriting Atlantis's own implementation history to match the
  external draft's MVP ordering.** Atlantis already implemented RHI,
  Vulkan Backend, RenderGraph, Renderer, and Shader System — in that
  order — before any Runtime/World/ECS/Asset work began, the reverse of
  the external draft's own Core → Runtime Host → ECS → Asset → RHI →
  Renderer ordering. This Spec does not, and cannot, retroactively claim
  Atlantis followed a different order than it did.
- **The external draft's own repository layout, module list, or a new
  `RFC/` document tier.** Atlantis keeps its existing `specs/`/`adr/`/
  `plans/` structure and its existing nine top-level source modules —
  see Non-Goals and ADR-0032.

## Proposed Design

Two intentionally orthogonal views, per ADR-0032 — neither replaces the
other, and this Spec does not merge them into one diagram:

**Conceptual product/runtime layers** (descriptive only; not a source
tree, not a dependency-enforcement mechanism):

```text
Products / Clients        (Game, Editor, AI Agent, Research Client, Automation — future)
        |
Public SDK                 (Schema, Query, Command, Event, Package — future)
        |
Authoritative Runtime      (World, ECS, Asset, Gameplay, Replay — future)
        |
Runtime Services            (Render, Physics, Audio, Inference, ... — Render exists today)
        |
Core / Platform / RHI / Device Backends
                             (Core, Platform, RHI exist and are implemented today; Device
                              Backends: Atlantis Vulkan Backend is the only implemented one —
                              Direct3D 12 and Metal are named only as future candidate
                              sibling backends, per ADR-0037)
```

**Diagram caption (read together with the diagram above, not only in
surrounding prose — per the independent architecture review's
recommendation):**

- This is a conceptual, descriptive layering, not a list of source
  modules and not a build/CMake dependency graph. The connecting lines
  do not represent call direction, ownership, or link order.
- The only Device Backend that exists and is implemented today is
  Atlantis Vulkan Backend. Direct3D 12 and Metal appear here solely as
  future candidate positions (ADR-0037); naming them here authorizes no
  code, directory, target, dependency, or timetable.
- Atlantis's nine-module, source/build-ownership view (below) remains
  the sole authoritative structure for CMake targets and module
  dependencies — this diagram never overrides it.
- Any future Device Backend, if and when approved by its own Spec,
  becomes an independent sibling module in that nine-module view (a
  tenth-plus module, alongside — not inside — Atlantis Vulkan Backend),
  the same way Atlantis Vulkan Backend itself is a named module today.
  This diagram does not create, and this Spec does not authorize, a new
  public `DeviceBackend` abstraction module.
- Atlantis Vulkan Backend is not renamed, restructured, or reinterpreted
  by this diagram or by ADR-0037.

**Repository module ownership** (authoritative for source/build/
dependency structure — unchanged by this Spec):

```text
Atlantis Core → Atlantis Platform → Atlantis RHI → Atlantis Vulkan Backend
    → Atlantis RenderGraph → Atlantis Renderer → Atlantis Shader System
    → (Atlantis Runtime, Atlantis Tools — not yet implemented as real modules)
```

Today's seven implemented modules sit, approximately and
non-authoritatively, in the lower two conceptual layers (Core/Platform/
RHI/Device Backends, Runtime Services); the not-yet-implemented Runtime
and Tools modules will eventually span Authoritative Runtime and the
SDK/Client boundary once their own Specs place them there. Any future,
independently-approved Device Backend (Direct3D 12, Metal) would sit in
the same bottom layer as Atlantis Vulkan Backend does today, as a
sibling — not a replacement, and not a new abstraction layer of its own.
This mapping is illustrative only, per ADR-0032 — it grants no
dependency edge and reassigns no file's module ownership.

## Compatibility with Existing Architecture

- **Core:** unaffected. No change to logging, assertions, or `Result<T,
  E>`.
- **Platform:** unaffected. Windows-implemented, Android/iOS
  architecture-only status unchanged. ADR-0033's future Client-boundary
  principle does not apply retroactively to Platform's own existing
  Windows/Android abstraction.
- **RHI:** unaffected in its current, `Accepted` public surface. ADR-0034
  names RHI's existing never-expose-`Vk*`-types pattern (ADR-0001) as
  one instance of the generalized principle it states — reinforcing, not
  changing, that boundary; ADR-0030 is the instance that most directly
  matches the schema/protocol framing (see ADR-0034's own Context for
  the distinction). ADR-0037 states that any future Device Backend
  behind RHI would need to satisfy the same backend-independence
  boundary — this is a long-term expectation for future Specs, not a
  change to RHI's public API today.
- **Vulkan Backend:** unaffected. Remains Phase 1's sole implemented
  Device Backend; no second-backend scaffolding is introduced or
  implied. ADR-0037 records a long-term, boundary-level position for
  future sibling Device Backends (Direct3D 12, Metal) without renaming,
  restructuring, or reinterpreting Atlantis Vulkan Backend in any way.
- **RenderGraph:** unaffected. Construction/compilation/execution
  design unchanged.
- **Renderer:** unaffected. Its existing single-fixed-material,
  single-fixed-mesh scope is unchanged; this Spec does not accelerate
  or authorize a scene graph, multiple materials, or an asset system.
- **Shader System:** unaffected. Spec 0008, ADR-0028–0031, and
  [PR #36](https://github.com/slmao/Atlantis/pull/36)'s implementation
  are not reopened, modified, or reinterpreted by this Spec in any way —
  Slang/SPIR-V-only, no multi-target compilation, no permutation system,
  no runtime/hot-reload compilation, all unchanged.
- **Future Runtime:** gains a stated direction (ADR-0033: authoritative
  state, Client symmetry) to design against once its own Spec is
  drafted, but no design, API, or module restructuring is authorized
  now. `docs/architecture/module_boundaries.md`'s own current, still-
  `PROPOSED` framing of Runtime as "the composition root, not a library"
  is not changed by this Spec — reconciling it with ADR-0033's
  library/Client-boundary-friendly framing is explicitly left to
  Runtime's own future Spec.
- **Future Headless:** unaffected in sequencing (windowed-first, per
  AGENTS.md, unchanged) — see Roadmap Impact for a non-binding priority
  note.
- **Future Android:** unaffected. Primary target-platform status
  (Windows and Android, per AGENTS.md) unchanged; Linux is explicitly
  not adopted as a target, per Requirements above.

## Roadmap Impact

The existing Candidate Spec Backlog (`specs/README.md` Section B) is
**not rewritten** by this Spec — every existing candidate item remains,
unchanged, at its existing backlog position, and this Spec does not
reorder it. This section only records a non-binding observation this
alignment work surfaced, for whoever drafts the next Spec or conducts a
future roadmap review to consider; it commits nothing and decides
nothing.

- **Windowed rendering is already complete** (Specs 0001–0008, all
  `Approved`/implemented). **Headless rendering (Candidate 2) remains
  the existing, unchanged next step in that sequencing** — AGENTS.md's
  windowed-then-headless commitment is not altered, relaxed, or
  reordered by this Spec in any way.
- Whether a future Runtime Host Spec (Candidate 5) should be drafted
  before, after, or alongside Candidate 2 is **explicitly left to a
  future roadmap review or to whichever of those Specs is drafted
  first** — this Spec does not resolve that ordering question, and the
  previous draft of this section's numbered list is corrected here to
  avoid implying it did. ADR-0033's authority/Client principle would
  first be exercised in earnest wherever Runtime Host lands, whenever
  that turns out to be.
- Candidate 6 (World/ECS Foundation) and Candidate 7 (Serialization and
  Stable Identity) are where ADR-0034 (stable boundary) and ADR-0035
  (authoring/runtime separation) would first be tested against a real
  data model; Candidate 4 (Asset System Foundation) plausibly benefits
  from Candidate 7's identity work landing first, but is not described
  as strictly blocked by it.
- Candidate 8 (Tool/Editor Connection Protocol) is where ADR-0033's
  Client model would get a second real Client (beyond Runtime Host
  itself) to validate against.
- Candidates 9–12 (Gameplay SDK, Research/Simulation API, AI Inference
  Integration, UGC Sandbox) are not discussed further here; nothing in
  this Spec changes their existing backlog position or gating.
- **Candidate 1 (Android Platform) and Candidate 3 (Image Regression
  Testing) are unaffected by this section** — this Spec's observations
  above do not cover them, do not imply they are lower priority, and do
  not change their existing backlog position.
- **No Direct3D 12 or Metal work is added to the Candidate Backlog at
  any position**, near-term or otherwise. ADR-0037 records a long-term
  architectural *direction* only; it is deliberately not translated into
  a roadmap or backlog item by this Spec. A future Spec proposing actual
  Direct3D 12 or Metal work would itself need to be added to the backlog
  first, per this repository's normal process.
- **WebGPU has no roadmap position and is not discussed further** — see
  Requirements above.
- Whether Agent-native/machine-verifiable tooling (ADR-0036) should
  become its own new Candidate Backlog item — there is currently no
  existing candidate number for it — is an **open question left to
  Human Review** (see Risks & Open Questions and HR-0009), not decided
  by this Spec.

None of the above is a commitment — see AGENTS.md's own "a milestone
being listed does not authorize starting it" principle, which applies
here identically. This section observes and flags; it does not order or
authorize.

## Architectural Impact

This Spec identifies six new architectural decisions, each drafted as
its own `Proposed` ADR alongside this Spec, none of which reopens or
modifies any existing `Accepted` ADR or `Approved` Spec. It also
proposes one future, not-yet-authorized documentation deliverable (see
"A future architecture overview document" below).

- [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md) —
  Conceptual Architecture Layers versus Source-Module Ownership.
- [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md) —
  Runtime Authority and Client Boundary.
- [ADR-0034](../adr/0034-stable-public-boundary-versus-internal-cpp-layout.md) —
  Stable Public Boundary (Schema/Identity/Protocol) versus Internal C++
  Layout.
- [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md) —
  Authoring/Runtime Data Separation as a Long-Term Principle.
- [ADR-0036](../adr/0036-agent-native-automation-and-machine-verifiable-architecture-as-long-term-goals.md) —
  Agent-Native Automation and Machine-Verifiable Architecture as
  Long-Term Goals.
- [ADR-0037](../adr/0037-long-term-device-backend-extensibility-without-phase1-scaffolding.md) —
  Long-Term Device Backend Extensibility Without Phase 1 Scaffolding.

### A future architecture overview document

This Spec proposes that, once it and its ADRs are `Approved`/`Accepted`,
a future Plan 0009 (see Related Plan(s) above) create
`docs/architecture/engine_architecture.md` as a navigation/overview
document. It would:

- Give Atlantis a single top-level entry point describing the
  conceptual-layers view and the nine-module source-ownership view as
  two orthogonal lenses (per ADR-0032), including the Device Backend
  position from ADR-0037.
- State current as-built status (what exists today) versus long-term
  direction (what is `Proposed`/future), consistent with what this Spec
  and its ADRs already say.
- Link to, rather than duplicate, every relevant `Accepted` ADR,
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md),
  [docs/architecture/threading.md](../docs/architecture/threading.md),
  and [docs/project-blueprint.md](../docs/project-blueprint.md).
- Note, briefly, that Runtime/SDK/World/Asset remain unimplemented, and
  that Agent-native/machine-verifiable goals (ADR-0036) are a
  development-tooling direction, not a claim about Atlantis's current
  runtime AI capability.

It would explicitly **not**: restate or duplicate any governance rule
from `AGENTS.md`; restate detailed per-module ownership already owned by
`module_boundaries.md`; restate roadmap/status information already owned
by `project-blueprint.md` and `specs/README.md`; restate the *reasoning*
behind any `Accepted` ADR (link to it instead); reproduce the external
draft; describe any concrete future subsystem's API; or state any
capability as approved that is not actually `Accepted`/`Approved`. Per
[AGENTS.md](../AGENTS.md)'s single-authoritative-source principle, this
document would be a navigation/overview layer only — the *why* stays
with `Accepted` ADRs, the current *what* stays with `Approved` Specs and
`module_boundaries.md`, and roadmap/status stays with
`project-blueprint.md`/`specs/README.md`. It must not become a second,
competing `AGENTS.md`.

This Spec does **not** create this file now — see Non-Goals. Whether to
actually create it, and its exact content, is confirmed or adjusted by
Human Review and delivered through Plan 0009 (see below), not decided
unilaterally by this Spec.

## Alternatives Considered

- **Adopt the external draft as Atlantis's architecture baseline
  wholesale, in one large Spec.** Rejected: would approve roughly 80
  distinct topics in a single Human Review pass, several of which
  directly conflict with `Accepted` decisions (Linux platform tier,
  multi-backend RHI, multi-target Shader System, MVP-0 Job System) —
  exactly the "large, uncontrollable architecture baseline" this Spec's
  own governance instructions warn against.
- **Ignore the external draft entirely; make no alignment Spec at all.**
  Rejected: the draft contains real, load-bearing value for the exact
  gap Atlantis's own Candidate Backlog already names (Runtime, World,
  Asset, SDK, Editor) — declining to engage with it at all would leave
  each future Spec to independently rediscover the same principles (or
  fail to), with no shared starting point.
- **Split this into three separate Specs** (long-term principles,
  including Device Backend extensibility; Runtime/SDK/Client boundary;
  Agent-native development and machine-verifiable architecture), as
  suggested by this Spec's own governance instructions. Considered, and
  not adopted as the default in this draft: the six linked ADRs already
  provide independent, separately-acceptable/-rejectable decision units,
  which was judged to give most of the benefit of splitting without
  tripling the Human Review overhead for a single, coherent alignment
  pass. **This is flagged explicitly as an open question for Human
  Review** — see Risks & Open Questions; splitting remains available if
  preferred.
- **Fold this work into updates to `AGENTS.md`,
  `docs/architecture/module_boundaries.md`, or `docs/project-blueprint.md`
  directly, skipping a Spec/ADR round.** Rejected: those documents state
  either governance rules or as-built/proposed architecture; per
  AGENTS.md's own single-authoritative-source principle, a new
  architectural decision belongs in a Spec/ADR first, with those
  documents updated afterward by a future Plan — not decided by editing
  navigation documents directly.

## Testing & Verification Plan

Not applicable in the code-verification sense — this Spec produces no
code. Verification here means: Human Review confirms (a) this Spec's own
classification of the external draft (adopted now / long-term direction
/ deferred / rejected) is accurate and complete for the topics listed in
Requirements, including the three-tier treatment of Direct3D 12 and
Metal (current Vulkan-only / long-term sibling-backend direction /
implementation still deferred); (b) each of ADR-0032–0037 states exactly
one decision, does not silently reopen any `Accepted` ADR or `Approved`
Spec, and is narrow enough to be individually accepted or rejected; and
(c) the Compatibility with Existing Architecture section's per-module
claims are correct against the actual current repository state
(verifiable by reading the cited Specs/ADRs and, for Shader System,
PR #36's own merged content).

## Risks & Open Questions

- **Scope creep / vision bloat.** An umbrella alignment Spec is
  inherently at risk of accumulating "just one more principle" over
  successive revisions. Mitigation: this Spec fixes its own Non-Goals
  list explicitly and defers all concrete subsystem design to future
  Specs named by their existing Candidate Backlog entries.
- **Premature SDK/ECS stabilization.** Naming "stable schema/identity/
  protocol boundary" (ADR-0034) before any real schema or ECS exists
  risks the future World/ECS Spec feeling pre-constrained by an untested
  principle. Mitigation: ADR-0034 is stated as a boundary *category*, not
  a concrete format, and explicitly allows a future Spec to propose a
  documented exception.
- **Conceptual layers vs. source modules staying confused in practice.**
  Despite ADR-0032 stating the two views are orthogonal, a future
  contributor (human or agent) skimming a diagram out of context could
  still misread the five-layer view as a directory structure. Mitigation:
  this Spec's own Proposed Design section labels each diagram explicitly
  as descriptive vs. authoritative; a future Plan updating
  `docs/architecture/module_boundaries.md` should carry the same
  labeling forward.
- **Documentation/code drift.** Recording long-term principles now, with
  no code to check them against, risks the principles quietly going
  stale before any future Spec exercises them. Mitigation: none built
  into this Spec beyond ordinary repository hygiene — flagged honestly
  as a real risk, not solved here.
- **Agent-native goals inviting over-tooling.** ADR-0036's own direction
  could be read as license to start building CLI/manifest/diagnostic
  infrastructure ahead of real need. Mitigation: ADR-0036 explicitly
  authorizes no concrete tool, and this Spec's Non-Goals list a concrete
  CLI command set and manifest format as deferred.
- **Future workloads (AI, UGC, Neural Rendering) pressuring Phase 1
  abstractions.** Unchanged risk already named in AGENTS.md; this Spec
  does not increase it — ADR-0036 explicitly scopes "agent-native" to
  development tooling, not a runtime AI/UGC feature set.
- **Direct3D 12/Metal naming read as a product commitment.** Naming
  specific future backends, even only as long-term candidates, risks
  being misread — by a human skimming this Spec, or by an AI agent
  working from this repository — as a scheduled deliverable rather than
  a boundary-level architectural direction. Mitigation: ADR-0037,
  Requirements, and Compatibility above all repeat, at every mention,
  that no timetable, code, or implementation is authorized; Roadmap
  Impact explicitly does not add either backend to the Candidate
  Backlog.
- **No code-level reservation means RHI may still need to evolve later.**
  Because ADR-0037 deliberately adds no factory, capability system, or
  other code-level scaffolding, a future Direct3D 12 or Metal Spec may
  still find RHI's current interface needs real changes to accommodate
  it. Mitigation: none built into this Spec — honestly flagged as a real
  future cost, to be addressed by that future Spec and its own ADR, not
  papered over here.
- **Open questions for Human Review** (recommendation given, not a
  decision made by this Spec):
  - Whether to keep this as one Spec with six linked ADRs (as drafted)
    or split into the three-Spec structure this Spec's own governance
    instructions offered as an alternative.
  - Whether "Agent-native / AI-native" should be recorded anywhere as
    part of Atlantis's own top-level product positioning (e.g. in
    `README.md`), versus staying scoped, as this Spec keeps it, to
    development-time tooling direction only (ADR-0036) with no runtime
    product-positioning claim.
  - Whether the five-layer conceptual view (ADR-0032, now naming Device
    Backends explicitly at its base) should eventually become a real,
    checked-in `docs/architecture/` document — this Spec's own
    recommendation is yes, as `docs/architecture/engine_architecture.md`
    via a future Plan 0009 (see Architectural Impact) — or remain only
    in this Spec/ADR pair.
  - Whether Candidate 5 (Runtime Host and Composition Root) should be
    sequenced before, after, or alongside Candidate 2 (Headless
    Rendering) — left to a future roadmap review or to whichever of
    those Specs is drafted first, not resolved by this Spec (see
    Roadmap Impact).
  - Whether Agent-native/machine-verifiable tooling (ADR-0036) warrants
    its own new Candidate Backlog item, since none currently exists for
    it (see Roadmap Impact).
  - Whether to accept ADR-0037's "boundary reservation without
    scaffolding" as the right level of long-term commitment for Direct3D
    12/Metal, versus one of its own Alternatives Considered (saying
    nothing, building factory/capability scaffolding now, or declaring a
    committed roadmap item).

## Out of Scope / Future Work

Everything listed under Non-Goals and under "Deferred to a future
subsystem Spec" in Requirements, above — including any Direct3D 12 or
Metal implementation work, which remains entirely future-Spec territory
per ADR-0037's own future approval gate. Concretely, two distinct pieces
of future work follow this Spec's approval, neither authorized or
started here:

- **Plan 0009**, covering `docs/architecture/engine_architecture.md` and
  the related documentation-navigation updates described in
  Architectural Impact — the direct, docs-only follow-up to this Spec
  itself.
- A **future subsystem Spec** for whichever Candidate Backlog item Human
  Review prioritizes next (see Roadmap Impact) — a separate piece of
  work, not gated on Plan 0009 landing first.

## Documentation Authority

Per [AGENTS.md](../AGENTS.md)'s single-authoritative-source
documentation principle: the human-provided external draft
(`ENGINE_ARCHITECTURE.md`) is a reference input to this Spec's own
drafting process, not itself an authoritative document of this
repository, and is not reproduced or checked in here. Once this Spec and
its ADRs are `Approved`/`Accepted`, **this Spec and its linked ADRs are
the authoritative record** of what was adopted, deferred, or rejected —
not the external draft. Nothing in this repository should cite the
external draft as a source of truth going forward; it should cite this
Spec and its ADRs instead.

This also governs the future `docs/architecture/engine_architecture.md`
proposed above: once created by a future Plan, it is a
navigation/overview document, not an independent source of authority.
The *why* behind any decision stays with the relevant `Accepted` ADR;
the current *what* stays with the relevant `Approved` Spec and
[module_boundaries.md](../docs/architecture/module_boundaries.md); and
roadmap/status stays with
[project-blueprint.md](../docs/project-blueprint.md)/[specs/README.md](../specs/README.md).
That future document must link to these sources, not restate or
duplicate their content, and must not become a second, competing
`AGENTS.md`.
