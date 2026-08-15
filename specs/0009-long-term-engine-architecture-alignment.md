# Spec: Long-Term Engine Architecture Alignment

- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, from a human-provided external architecture draft; pending
  Human Review.
- **Created:** 2026-08-15
- **Related Plan(s):** None — this Spec explicitly does not authorize a
  Plan or Implementation (see Non-Goals).
- **Related ADR(s):**
  [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md),
  [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md),
  [ADR-0034](../adr/0034-stable-public-boundary-versus-internal-cpp-layout.md),
  [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md),
  [ADR-0036](../adr/0036-agent-native-automation-and-machine-verifiable-architecture-as-long-term-goals.md)
  — all `Proposed`, drafted alongside this Spec. See Architectural Impact.

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
  separation as an available (not mandatory) option, and Agent-native,
  vendor-neutral development tooling as a direction — each as its own
  narrowly-scoped `Proposed` ADR (ADR-0033 through ADR-0036).
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
  Job System, a second graphics backend, Shader System language/target
  extensions, serialization, a wire protocol, an ABI, or any AI/UGC/
  Neural-rendering capability.
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
  schema, a second graphics backend, a UGC sandbox mechanism, a
  structured-diagnostic schema, or a concrete engine CLI command set.
- Authorize a Plan or any Implementation. Approving this Spec and its
  ADRs authorizes drafting a Plan for the next concrete step (most
  likely a Candidate Backlog item this Spec's Roadmap Impact section
  discusses) — it does not itself authorize writing code.

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
  authorized by this Spec.
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
- **A second graphics backend, or RHI capability-tier scaffolding for
  one** (D3D12, Metal, WebGPU; `RayTracingCapability`,
  `MeshShaderCapability`, and similar). Phase 1 is Vulkan-only, per
  AGENTS.md; RHI stays backend-independent in interface but no second
  backend is implemented or scaffolded for. Not reopened by this Spec.
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
Core / Platform              (RHI, Jobs, IO, Memory, OS — Core/Platform/RHI exist today)
```

**Repository module ownership** (authoritative for source/build/
dependency structure — unchanged by this Spec):

```text
Atlantis Core → Atlantis Platform → Atlantis RHI → Atlantis Vulkan Backend
    → Atlantis RenderGraph → Atlantis Renderer → Atlantis Shader System
    → (Atlantis Runtime, Atlantis Tools — not yet implemented as real modules)
```

Today's seven implemented modules sit, approximately and
non-authoritatively, in the lower two conceptual layers (Core/Platform,
Runtime Services); the not-yet-implemented Runtime and Tools modules will
eventually span Authoritative Runtime and the SDK/Client boundary once
their own Specs place them there. This mapping is illustrative only, per
ADR-0032 — it grants no dependency edge and reassigns no file's module
ownership.

## Compatibility with Existing Architecture

- **Core:** unaffected. No change to logging, assertions, or `Result<T,
  E>`.
- **Platform:** unaffected. Windows-implemented, Android/iOS
  architecture-only status unchanged. ADR-0033's future Client-boundary
  principle does not apply retroactively to Platform's own existing
  Windows/Android abstraction.
- **RHI:** unaffected in its current, `Accepted` public surface.
  ADR-0034 explicitly names RHI's existing never-expose-`Vk*`-types
  pattern (ADR-0001) as the *first instance* of the generalized
  principle it states — reinforcing, not changing, that boundary.
- **Vulkan Backend:** unaffected. Remains Phase 1's sole graphics
  backend; no second backend scaffolding is introduced or implied.
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
unchanged, at its existing backlog position. This section only records a
non-binding dependency-ordering suggestion this alignment work surfaced,
for whoever drafts the next Spec to consider; it commits nothing.

A plausible near-term ordering, consistent with both Atlantis's existing
windowed-then-headless commitment and the long-term principles this Spec
introduces:

1. Candidate 5 (Runtime Host and Composition Root) — the first place
   ADR-0033's authority/Client principle would actually be exercised
   against a real design.
2. Candidate 2 (Headless Rendering) — already next after windowed per
   AGENTS.md's own sequencing; also the natural first real Runtime Host
   entry point beyond the existing non-shipping verification demos.
3. Candidate 6 (World/ECS Foundation) and Candidate 7 (Serialization and
   Stable Identity) — where ADR-0034 (stable boundary) and ADR-0035
   (authoring/runtime separation) first get tested against a real data
   model.
4. Candidate 4 (Asset System Foundation) — benefits from Candidate 7's
   identity work landing first, but is not strictly blocked by it.
5. Candidate 8 (Tool/Editor Connection Protocol) — the first place
   ADR-0033's Client model gets a second real Client (beyond Runtime
   Host itself) to validate against.
6. Candidates 9–12 (Gameplay SDK, Research/Simulation API, AI Inference
   Integration, UGC Sandbox) — unchanged position, still gated on the
   Runtime/World work above landing first.

This ordering is a suggestion for the next Spec author, not a
commitment — see AGENTS.md's own "a milestone being listed does not
authorize starting it" principle, which applies here identically.

## Architectural Impact

This Spec identifies five new architectural decisions, each drafted as
its own `Proposed` ADR alongside this Spec, none of which reopens or
modifies any existing `Accepted` ADR or `Approved` Spec:

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
- **Split this into three separate Specs** (long-term principles;
  Runtime/SDK/Client boundary; Agent-native development and
  machine-verifiable architecture), as suggested by this Spec's own
  governance instructions. Considered, and not adopted as the default in
  this draft: the five linked ADRs already provide independent,
  separately-acceptable/-rejectable decision units, which was judged to
  give most of the benefit of splitting without tripling the Human
  Review overhead for a single, coherent alignment pass. **This is
  flagged explicitly as an open question for Human Review** — see Risks
  & Open Questions; splitting remains available if preferred.
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
Requirements; (b) each of ADR-0032–0036 states exactly one decision,
does not silently reopen any `Accepted` ADR or `Approved` Spec, and is
narrow enough to be individually accepted or rejected; and (c) the
Compatibility with Existing Architecture section's per-module claims are
correct against the actual current repository state (verifiable by
reading the cited Specs/ADRs and, for Shader System, PR #36's own merged
content).

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
- **Open questions for Human Review** (recommendation given, not a
  decision made by this Spec):
  - Whether to keep this as one Spec with five linked ADRs (as drafted)
    or split into the three-Spec structure this Spec's own governance
    instructions offered as an alternative.
  - Whether "Agent-native / AI-native" should be recorded anywhere as
    part of Atlantis's own top-level product positioning (e.g. in
    `README.md`), versus staying scoped, as this Spec keeps it, to
    development-time tooling direction only (ADR-0036) with no runtime
    product-positioning claim.
  - Whether the five-layer conceptual view (ADR-0032) should eventually
    become a real, checked-in `docs/architecture/` document, or remain
    only in this Spec/ADR pair.
  - Whether the Roadmap Impact ordering above reflects the human
    maintainer's actual near-term priority, or should be reordered
    before any of those Candidate Backlog items gets its own Spec.

## Out of Scope / Future Work

Everything listed under Non-Goals and under "Deferred to a future
subsystem Spec" in Requirements, above. Concretely, the next expected
piece of future work — not authorized or started by this Spec — is
drafting a Spec for whichever Candidate Backlog item Human Review
prioritizes, most likely Candidate 5 (Runtime Host and Composition Root)
per this Spec's own Roadmap Impact suggestion.

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
