# Spec: Long-Term Engine Architecture Alignment

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, from a human-provided external architecture draft.
- **Created:** 2026-08-15
- **Related Plan(s):**
  [plans/0009-long-term-engine-architecture-alignment.md](../plans/0009-long-term-engine-architecture-alignment.md)
  (`Approved`, Human Review 2026-08-15). Approving the Plan authorizes a
  separate Implementation branch/PR — scoped to creating
  `docs/architecture/engine_architecture.md` plus minimal, non-duplicative
  navigation-link updates to a small set of existing documents (see that
  Plan for the exact list) — opened only after
  [PR #40](https://github.com/slmao/Atlantis/pull/40) is merged by a human.
- **Related ADR(s):**
  [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md),
  [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md),
  [ADR-0034](../adr/0034-stable-public-boundary-versus-internal-cpp-layout.md),
  [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md),
  [ADR-0036](../adr/0036-agent-native-automation-and-machine-verifiable-architecture-as-long-term-goals.md),
  [ADR-0037](../adr/0037-long-term-device-backend-extensibility-without-phase1-scaffolding.md)
  — all `Accepted` alongside this Spec. See Architectural Impact.
- **Human Review Approval (2026-08-15):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), following an independent architecture
  review of this Spec, ADR-0032–0037, and PR #39, plus an HR-0009 decision
  table covering sixteen topics. This Spec and ADR-0032–0037 are approved
  as drafted. Two points resolved as part of the approval: (1) ADR-0037's
  citation of AGENTS.md's iOS-backend rule is corrected to quote it
  verbatim ("...is not to be designed or scaffolded for now") — a
  quotation-accuracy fix only, the substantive meaning (iOS
  Metal-vs-MoltenVK undecided) unchanged; (2) Plan 0009 is confirmed
  required before `docs/architecture/engine_architecture.md` may be
  created, and this approval authorizes drafting it through its own Human
  Review. The approval does **not** authorize any Direct3D 12, Metal, or
  other second-Device-Backend code, directory, CMake target, SDK
  dependency, or scaffolding; does not change Phase 1's Vulkan-only scope;
  does not decide iOS's Metal-vs-MoltenVK question; and does not modify the
  Candidate Spec Backlog (`specs/README.md` Section B) or its priorities.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #150](https://github.com/slmao/Atlantis/pull/150) Batch 3. Original
  scope and obligations retained; the full HR-0009 topic-by-topic
  narration is preserved in PR #39/#40 history.

## Summary

This Spec aligns Atlantis's long-term architectural direction with a
human-provided external architecture draft (`ENGINE_ARCHITECTURE.md`, not
part of this repository and not reproduced here — see Documentation
Authority), **without** implementing, designing, or authorizing any new
subsystem. It is a governance/direction document, not a Runtime, ECS,
Asset, Editor, or SDK design. It states a small number of long-term
principles as ADRs, sorts the external draft's much larger surface into
what Atlantis can adopt now versus what must wait for its own future Spec,
and identifies where the external draft conflicts with Atlantis's own
`Accepted` Phase 1 decisions — in every such case keeping the existing
decision and deferring or rejecting the conflicting direction rather than
silently overriding it.

## Motivation / Problem Statement

Over Specs 0001–0008 Atlantis built an `Accepted`, implemented
rendering-first foundation: Core, Windows Platform, a backend-independent
RHI, a Vulkan Backend, RenderGraph, a minimal Renderer, and a Slang-based
Shader System (see [specs/README.md](../specs/README.md)). None of that is
changed here. What Atlantis does not yet have — Runtime (the module),
World/ECS, Asset, a Runtime/Editor/Client boundary, a public SDK — is
already named on the Candidate Spec Backlog (`specs/README.md` Section B),
but with no shared architectural direction connecting those future items.
A human maintainer provided an external, independently-authored
architecture draft describing one coherent long-term vision for that space
(Runtime authority, Client model, Schema-first SDK, Authoring/Runtime
separation, Agent-native development, and more).

That draft is a valuable reference but was not written for this
repository: it assumes its own MVP ordering (not matching Atlantis's
completed history), its own repository layout, and states positions (a
multi-backend graphics roadmap, a multi-target shader compiler, Linux as
an early platform tier, a Job System in MVP 0) that conflict with
Atlantis's `Accepted` Phase 1 decisions. Per [AGENTS.md](../AGENTS.md)'s
Golden Rule, an external draft cannot silently become Atlantis's
architecture; it must go through the same Spec → Plan → Human Review
process. This Spec is that step: sort what is genuinely adoptable now,
from a reasonable long-term direction not yet ready to commit to, from
what actively conflicts with decisions already made.

## Goals

- Define the relationship between a conceptual, product-level architecture
  view and Atlantis's existing source/build-ownership-based nine-module
  view, without replacing either (ADR-0032).
- State a small number of long-term architectural principles — Runtime
  authority and Client symmetry; a stable schema/identity/protocol
  boundary distinct from internal C++ layout; Authoring/Runtime data
  separation as an available (not mandatory) option; Agent-native,
  vendor-neutral development tooling as a direction; and a reserved
  long-term boundary position for future sibling Device Backends without
  Phase 1 scaffolding — each as its own narrowly-scoped ADR (ADR-0033
  through ADR-0037).
- Propose a future, not-yet-authorized `docs/architecture/engine_architecture.md`
  overview document giving these two views and Atlantis's
  current-versus-long-term status a single navigation entry point —
  without it becoming an authoritative source.
- Classify the external draft's major directions against Atlantis's
  current state, so future Specs (Runtime Host, World/ECS, Asset,
  Tool/Editor Protocol) inherit a stated position rather than each
  re-litigating it.
- Identify, by name, every place the external draft conflicts with an
  `Accepted` ADR or `Approved` Spec, and confirm none of them is changed
  by this Spec.
- Suggest a non-binding dependency ordering for the existing Candidate
  Spec Backlog, without rewriting the backlog or committing any candidate
  to implementation.

## Non-Goals

This Spec does not:

- Write, generate, or modify any source code, test, CMake file, Shader, or
  CI configuration.
- Implement, or produce a Plan for implementing, Runtime, World, ECS,
  Asset, Asset Database, Editor, SDK (any language), Package System, Job
  System, a second graphics backend (Direct3D 12, Metal, or other), Shader
  System language/target extensions, serialization, a wire protocol, an
  ABI, or any AI/UGC/Neural-rendering capability. Naming Direct3D 12 and
  Metal as long-term candidate Device Backends (ADR-0037) is not an
  exception — no backend code, directory, target, or dependency is created
  or authorized.
- Modify `AGENTS.md`, any existing `Accepted` ADR, any existing `Approved`
  Spec or its Plan, `docs/architecture/module_boundaries.md`, or
  `docs/project-blueprint.md`. Aligning those documents with the ADRs this
  Spec introduces is explicit future Plan work (see Roadmap Impact).
- Reorganize Atlantis's existing nine top-level modules, their CMake
  target structure, or any existing public API.
- Adopt the external draft's own repository layout, module list, MVP
  ordering, or directory structure (`Engine/`, `RFC/`, a two-repository
  `Engine`/`Game` split, etc.) as Atlantis's own.
- Decide any concrete mechanism this Spec's Requirements marks as deferred
  — including ECS archetype/chunk design, `EntityId` bit layout, a C ABI
  or FFI surface, C#/Luau/Python binding design, an IPC/RPC protocol, a
  serialization format, a Package manifest format, a Physics/Audio/Inference
  backend interface, Job System design, Editor process model,
  in-process-vs-remote Runtime transport, Asset Database schema, a second
  graphics backend's concrete interface/selection mechanism, a UGC sandbox
  mechanism, a structured-diagnostic schema, or a concrete engine CLI
  command set.
- Create `docs/architecture/engine_architecture.md` or any other new file
  under `docs/architecture/`. This Spec proposes that document as a future
  Plan deliverable; drafting it is not authorized here.
- Authorize a Plan or any Implementation. Approving this Spec and its ADRs
  authorizes drafting a Plan for the next concrete step (most likely Plan
  0009 itself) — it does not authorize writing code, CMake, or shaders.

## Requirements

### Immediately-effective invariants (this Spec's own Architectural Impact)

Take effect once this Spec and its ADRs are `Approved`/`Accepted`, and
bind how *future* Specs are written — they require no change to existing,
already-implemented code:

- The five-layer conceptual view and the nine-module source-ownership view
  co-exist as orthogonal, non-competing lenses (ADR-0032).
- A future Runtime holds the sole authoritative copy of engine world
  state; every external module, including a future Editor, accesses it as a
  Client through the same category of access surface, never through an
  exposed internal pointer (ADR-0033).
- Atlantis's externally-stable public boundary is expressed through
  schema/identity/protocol concepts, not internal C++ layout — a
  generalization of an already-`Accepted` pattern (ADR-0001, ADR-0030),
  binding future World/ECS, Serialization, Asset, and Package Specs
  (ADR-0034).
- Authoring-facing and runtime-execution data representations are
  permitted to differ, and a future World/ECS or Asset Spec must
  explicitly address whether they do, rather than defaulting into an
  unconsidered answer (ADR-0035).
- Important development capabilities should move, over time, toward being
  reachable via CLI/API rather than GUI-only, and architectural
  constraints toward machine-verifiability — a vendor-neutral direction,
  not a Phase 1 requirement or an authorization to build any specific tool
  now (ADR-0036).
- Vulkan remains the only implemented Device Backend in Phase 1; a
  conceptual, boundary-level position is reserved for future sibling
  Device Backends without creating any code, directory, target,
  dependency, or abstraction for them now (ADR-0037).

### Long-term direction (compatible, not adopted as binding invariant now)

Recorded as accepted direction for future Specs to draw on, but not
elevated to ADR/invariant status this round:

- Backend-replaceable services (rendering, physics, audio, navigation,
  inference, networking) as a shape for future Runtime Services —
  consistent with [AGENTS.md](../AGENTS.md)'s existing "RHI is
  backend-agnostic in interface; Vulkan is Phase 1's only implementation"
  principle, generalized to future non-rendering services once they exist.
  No second backend for any of these is authorized.
- Direct3D 12 and Metal as candidate future sibling Device Backends behind
  RHI's existing boundary, per ADR-0037 — a long-term position, not an
  implementation commitment or timetable. Phase 1 remains Vulkan-only; no
  Direct3D 12/Metal code, directory, CMake target, SDK dependency,
  capability-tier abstraction, or backend-selection mechanism is
  authorized. WebGPU is given no reserved position by ADR-0037 at all.
- Headless, fixed-step, and snapshot/replay as valuable future Runtime
  capabilities — Atlantis's existing windowed-before-headless sequencing
  (AGENTS.md) is unchanged.
- Samples-as-executable-specification, package-local Agent context, and a
  documentation hierarchy from architecture down to recipes — reasonable
  future practices once real packages/samples exist; not binding process
  changes today.

### Deferred to a future subsystem Spec (valuable, not decidable yet)

Out of this Spec's scope, each requiring its own future Spec with a
concrete consumer: World/ECS design (entity/component/archetype/query
shape, `EntityId` representation); Runtime Host and composition-root
mechanism; Asset identity, Asset Database, and cook pipeline; Package
System and manifest format; Job System; concrete
Query/Command/Event/Transaction/Snapshot API; Stable Native ABI; Runtime
Client transport (in-process vs. remote, any IPC/RPC protocol); Editor
process model; gameplay-language bindings (C#, Luau, Python) and their
trust model; UGC sandbox and capability model; Research/Observation-Action
API; Inference Service and backend interface; structured-diagnostic
schema; Module Manifest format; a concrete engine CLI command set.

### Explicitly rejected or deferred for this round

- **Linux as a target platform**, in any form (including "Linux Headless"
  as an early platform tier). AGENTS.md is explicit that Linux is not a
  target platform; a future platform-scope Spec would need its own Human
  Review to change that.
- **Implementing or scaffolding a second graphics backend now, in any
  form** — RHI capability-tier abstractions (`RayTracingCapability`,
  `MeshShaderCapability`, etc.), backend registries/factories,
  conditional-compilation scaffolding, or second-backend SDK dependencies.
  Naming Direct3D 12 and Metal as long-term candidates (above, and
  ADR-0037) does not reopen, weaken, or except itself from this Phase 1
  prohibition.
- **WebGPU, in any role, at any time horizon** — not given even a
  long-term reserved position; outside this round's scope entirely, not
  merely deferred.
- **Shader multi-target compilation, a permutation system, or
  runtime/hot-reload shader compilation.** Spec 0008 / ADR-0028 already
  decided Slang → SPIR-V only; not reopened or reinterpreted here.
- **A Job System as an immediate (MVP 0-equivalent) requirement.**
  ADR-0004's Phase 1 single-threaded frame-orchestration baseline is
  unchanged; a future Core Runtime candidate may propose one later, driven
  by a concrete need.
- **Rewriting Atlantis's own implementation history to match the external
  draft's MVP ordering.** Atlantis implemented RHI, Vulkan Backend,
  RenderGraph, Renderer, Shader System — in that order — before any
  Runtime/World/ECS/Asset work; this Spec does not retroactively claim a
  different order.
- **The external draft's own repository layout, module list, or a new
  `RFC/` document tier.** Atlantis keeps its existing `specs/`/`adr/`/
  `plans/` structure and its nine top-level source modules.

## Proposed Design

Two intentionally orthogonal views, per ADR-0032 — neither replaces the
other, and this Spec does not merge them into one diagram.

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
surrounding prose):**

- This is a conceptual, descriptive layering, not a list of source modules
  and not a build/CMake dependency graph. The connecting lines do not
  represent call direction, ownership, or link order.
- The only Device Backend that exists and is implemented today is Atlantis
  Vulkan Backend. Direct3D 12 and Metal appear here solely as future
  candidate positions (ADR-0037); naming them here authorizes no code,
  directory, target, dependency, or timetable.
- Atlantis's nine-module, source/build-ownership view (below) remains the
  sole authoritative structure for CMake targets and module dependencies —
  this diagram never overrides it.
- Any future Device Backend, if and when approved by its own Spec, becomes
  an independent sibling module in that nine-module view (a tenth-plus
  module, alongside — not inside — Atlantis Vulkan Backend), the same way
  Atlantis Vulkan Backend itself is a named module today. This diagram
  does not create, and this Spec does not authorize, a new public
  `DeviceBackend` abstraction module.
- Atlantis Vulkan Backend is not renamed, restructured, or reinterpreted
  by this diagram or by ADR-0037.

**Repository module ownership** (authoritative for source/build/dependency
structure — unchanged by this Spec):

```text
Atlantis Core → Atlantis Platform → Atlantis RHI → Atlantis Vulkan Backend
    → Atlantis RenderGraph → Atlantis Renderer → Atlantis Shader System
    → (Atlantis Runtime, Atlantis Tools — not yet implemented as real modules)
```

Today's seven implemented modules sit, approximately and
non-authoritatively, in the lower two conceptual layers (Core/Platform/
RHI/Device Backends, Runtime Services); the not-yet-implemented Runtime and
Tools modules will eventually span Authoritative Runtime and the
SDK/Client boundary once their own Specs place them there. Any future,
independently-approved Device Backend (Direct3D 12, Metal) would sit in the
same bottom layer as Atlantis Vulkan Backend does today, as a sibling —
not a replacement, and not a new abstraction layer of its own. This
mapping is illustrative only, per ADR-0032 — it grants no dependency edge
and reassigns no file's module ownership.

## Compatibility with Existing Architecture

- **Core / Platform / RHI / Vulkan Backend / RenderGraph / Renderer /
  Shader System:** all unaffected in their current `Accepted` public
  surfaces. ADR-0034 names RHI's existing never-expose-`Vk*`-types pattern
  (ADR-0001) and ADR-0030's schema/protocol framing as instances of the
  generalized principle it states — reinforcing, not changing, those
  boundaries. ADR-0037 records a long-term, boundary-level position for
  future sibling Device Backends (Direct3D 12, Metal) without renaming,
  restructuring, or reinterpreting Atlantis Vulkan Backend. Spec 0008 /
  ADR-0028–0031 / [PR #36](https://github.com/slmao/Atlantis/pull/36) are
  not reopened — Slang/SPIR-V-only, no multi-target, no permutation, no
  runtime/hot-reload compilation, all unchanged.
- **Future Runtime:** gains a stated direction (ADR-0033: authoritative
  state, Client symmetry) to design against once its own Spec exists, but
  no design, API, or module restructuring is authorized now.
  `module_boundaries.md`'s current, still-`PROPOSED` "composition root,
  not a library" framing of Runtime is not changed by this Spec;
  reconciling it with ADR-0033 is left to Runtime's own future Spec.
- **Future Headless:** unaffected in sequencing (windowed-first, per
  AGENTS.md).
- **Future Android:** unaffected. Primary target-platform status (Windows
  and Android) unchanged; Linux is explicitly not adopted as a target.

## Roadmap Impact

The existing Candidate Spec Backlog (`specs/README.md` Section B) is **not
rewritten** by this Spec — every candidate item remains at its existing
position, and this Spec does not reorder it. This section records only
non-binding observations for whoever drafts the next Spec or conducts a
future roadmap review; it commits nothing and authorizes nothing (see
AGENTS.md's "a milestone being listed does not authorize starting it").

- Windowed rendering is already complete (Specs 0001–0008). Headless
  rendering (Candidate 2) remains the existing, unchanged next step in
  AGENTS.md's windowed-then-headless sequencing.
- Whether a future Runtime Host Spec (Candidate 5) should be drafted
  before, after, or alongside Candidate 2 is left to a future roadmap
  review or to whichever of those Specs is drafted first — not resolved
  here. ADR-0033's authority/Client principle would first be exercised in
  earnest wherever Runtime Host lands.
- Candidate 6 (World/ECS Foundation) and Candidate 7 (Serialization and
  Stable Identity) are where ADR-0034 and ADR-0035 would first be tested
  against a real data model; Candidate 4 (Asset System Foundation)
  plausibly benefits from Candidate 7's identity work landing first, but
  is not described as strictly blocked by it.
- Candidate 8 (Tool/Editor Connection Protocol) is where ADR-0033's Client
  model would get a second real Client to validate against.
- Candidates 9–12 (Gameplay SDK, Research/Simulation API, AI Inference
  Integration, UGC Sandbox), and Candidate 1 (Android Platform) and
  Candidate 3 (Image Regression Testing), are unaffected by this section —
  no change to their existing backlog position or gating, and no
  implication of lower priority.
- **No Direct3D 12 or Metal work is added to the Candidate Backlog at any
  position.** ADR-0037 records a long-term architectural *direction* only.
- **WebGPU has no roadmap position and is not discussed further.**
- Whether Agent-native/machine-verifiable tooling (ADR-0036) should become
  its own new Candidate Backlog item (none exists for it) is an open
  question left to Human Review (see Risks & Open Questions).

## Architectural Impact

Six new architectural decisions, each drafted as its own `Accepted` ADR
alongside this Spec, none reopening or modifying any existing `Accepted`
ADR or `Approved` Spec:

- [ADR-0032](../adr/0032-conceptual-architecture-layers-versus-source-module-ownership.md)
  — Conceptual Architecture Layers versus Source-Module Ownership.
- [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md) —
  Runtime Authority and Client Boundary.
- [ADR-0034](../adr/0034-stable-public-boundary-versus-internal-cpp-layout.md)
  — Stable Public Boundary (Schema/Identity/Protocol) versus Internal C++
  Layout.
- [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)
  — Authoring/Runtime Data Separation as a Long-Term Principle.
- [ADR-0036](../adr/0036-agent-native-automation-and-machine-verifiable-architecture-as-long-term-goals.md)
  — Agent-Native Automation and Machine-Verifiable Architecture as
  Long-Term Goals.
- [ADR-0037](../adr/0037-long-term-device-backend-extensibility-without-phase1-scaffolding.md)
  — Long-Term Device Backend Extensibility Without Phase 1 Scaffolding.

### A future architecture overview document

This Spec proposes that, once it and its ADRs are `Approved`/`Accepted`, a
future Plan 0009 create `docs/architecture/engine_architecture.md` as a
navigation/overview document. It would: give a single top-level entry
point describing the conceptual-layers view and the nine-module
source-ownership view as two orthogonal lenses (ADR-0032), including the
Device Backend position from ADR-0037; state current as-built status
versus long-term `Proposed`/future direction; link to (not duplicate)
every relevant `Accepted` ADR, `module_boundaries.md`, `threading.md`, and
`project-blueprint.md`; and note briefly that Runtime/SDK/World/Asset
remain unimplemented and that ADR-0036's goals are a development-tooling
direction, not a current runtime AI-capability claim.

It would explicitly **not**: restate any `AGENTS.md` governance rule;
restate per-module ownership owned by `module_boundaries.md`; restate
roadmap/status owned by `project-blueprint.md`/`specs/README.md`; restate
the *reasoning* behind any `Accepted` ADR (link instead); reproduce the
external draft; describe any concrete future subsystem's API; or state any
capability as approved that is not actually `Accepted`/`Approved`. Per
AGENTS.md's single-authoritative-source principle it is a
navigation/overview layer only, and must not become a second, competing
`AGENTS.md`. This Spec does not create the file now (see Non-Goals);
whether to create it, and its exact content, is delivered through Plan
0009.

## Alternatives Considered

- **Adopt the external draft wholesale, in one large Spec.** Rejected:
  would approve roughly 80 distinct topics in a single Human Review pass,
  several conflicting with `Accepted` decisions (Linux platform tier,
  multi-backend RHI, multi-target Shader System, MVP-0 Job System).
- **Ignore the external draft entirely; make no alignment Spec.**
  Rejected: the draft holds real value for the exact gap the Candidate
  Backlog already names; declining to engage would leave each future Spec
  to rediscover the same principles with no shared starting point.
- **Split this into three separate Specs** (long-term principles;
  Runtime/SDK/Client boundary; Agent-native development and
  machine-verifiable architecture). Considered, not adopted as default:
  the six linked ADRs already provide independent, separately
  acceptable/rejectable decision units. Flagged as an open question for
  Human Review — splitting remains available if preferred.
- **Fold this work into `AGENTS.md` / `module_boundaries.md` /
  `project-blueprint.md` directly, skipping a Spec/ADR round.** Rejected:
  a new architectural decision belongs in a Spec/ADR first, with those
  navigation documents updated afterward by a future Plan.

## Testing & Verification Plan

Not applicable in the code-verification sense — this Spec produces no
code. Verification means Human Review confirms: (a) this Spec's
classification of the external draft (adopted now / long-term direction /
deferred / rejected) is accurate and complete for the topics in
Requirements, including the three-tier treatment of Direct3D 12 and Metal
(current Vulkan-only / long-term sibling-backend direction / implementation
still deferred); (b) each of ADR-0032–0037 states exactly one decision,
does not silently reopen any `Accepted` ADR or `Approved` Spec, and is
narrow enough to be individually accepted or rejected; and (c) the
Compatibility with Existing Architecture section's per-module claims are
correct against the actual current repository state.

## Risks & Open Questions

- **Scope creep / vision bloat.** Mitigation: this Spec fixes its
  Non-Goals explicitly and defers all concrete subsystem design to future
  Specs named by their Candidate Backlog entries.
- **Premature SDK/ECS stabilization.** Naming a "stable
  schema/identity/protocol boundary" (ADR-0034) before any real schema or
  ECS exists risks the future World/ECS Spec feeling pre-constrained.
  Mitigation: ADR-0034 is stated as a boundary *category*, not a concrete
  format, and explicitly allows a future Spec to propose a documented
  exception.
- **Conceptual layers vs. source modules staying confused in practice.**
  Mitigation: each diagram is labelled explicitly as descriptive vs.
  authoritative; a future Plan updating `module_boundaries.md` should
  carry the same labelling forward.
- **Documentation/code drift.** Recording long-term principles now, with
  no code to check them against, risks them going stale before any future
  Spec exercises them — flagged honestly as a real risk, not solved here.
- **Agent-native goals inviting over-tooling.** Mitigation: ADR-0036
  authorizes no concrete tool, and this Spec's Non-Goals list a concrete
  CLI command set and manifest format as deferred.
- **Future workloads (AI, UGC, Neural Rendering) pressuring Phase 1
  abstractions.** Unchanged risk already named in AGENTS.md; this Spec
  does not increase it.
- **Direct3D 12/Metal naming read as a product commitment.** Mitigation:
  ADR-0037, Requirements, and Compatibility repeat at every mention that
  no timetable, code, or implementation is authorized; Roadmap Impact adds
  neither backend to the Candidate Backlog.
- **No code-level reservation means RHI may still need to evolve later.**
  A future Direct3D 12 or Metal Spec may find RHI's current interface
  needs real changes — honestly flagged as a real future cost, to be
  addressed by that future Spec and its own ADR.
- **Open questions for Human Review** (recommendation given, not decided
  here):
  - Keep this as one Spec with six linked ADRs (as drafted), or split into
    the three-Spec structure.
  - Whether "Agent-native / AI-native" should be recorded anywhere as part
    of Atlantis's top-level product positioning (e.g. `README.md`), versus
    staying scoped to development-time tooling direction only (ADR-0036).
  - Whether the five-layer conceptual view (ADR-0032) should become a
    real, checked-in `docs/architecture/` document — recommendation is
    yes, as `docs/architecture/engine_architecture.md` via Plan 0009.
  - Whether Candidate 5 (Runtime Host) should be sequenced before, after,
    or alongside Candidate 2 (Headless Rendering) — left to a future
    roadmap review or whichever Spec is drafted first.
  - Whether Agent-native/machine-verifiable tooling (ADR-0036) warrants
    its own new Candidate Backlog item.
  - Whether to accept ADR-0037's "boundary reservation without
    scaffolding" as the right level of long-term commitment for Direct3D
    12/Metal, versus one of its own Alternatives Considered.

## Out of Scope / Future Work

Everything under Non-Goals and under "Deferred to a future subsystem Spec"
in Requirements — including any Direct3D 12 or Metal implementation work,
which remains entirely future-Spec territory per ADR-0037's own future
approval gate. Two distinct pieces of future work follow this Spec's
approval, neither authorized or started here:

- **Plan 0009**, covering `docs/architecture/engine_architecture.md` and
  the related documentation-navigation updates in Architectural Impact —
  the direct, docs-only follow-up.
- A **future subsystem Spec** for whichever Candidate Backlog item Human
  Review prioritizes next — a separate piece of work, not gated on Plan
  0009 landing first.

## Documentation Authority

Per [AGENTS.md](../AGENTS.md)'s single-authoritative-source principle: the
human-provided external draft (`ENGINE_ARCHITECTURE.md`) is a reference
input to this Spec's drafting, not itself an authoritative document of
this repository, and is not reproduced or checked in. Once this Spec and
its ADRs are `Approved`/`Accepted`, **this Spec and its linked ADRs are
the authoritative record** of what was adopted, deferred, or rejected —
not the external draft. Nothing in this repository should cite the
external draft as a source of truth going forward; cite this Spec and its
ADRs instead.

This also governs the future `docs/architecture/engine_architecture.md`:
once created it is a navigation/overview document, not an independent
source of authority. The *why* behind any decision stays with the
relevant `Accepted` ADR; the current *what* stays with the relevant
`Approved` Spec and
[module_boundaries.md](../docs/architecture/module_boundaries.md); and
roadmap/status stays with
[project-blueprint.md](../docs/project-blueprint.md)/[specs/README.md](../specs/README.md).
That future document must link to these sources, not restate or duplicate
their content, and must not become a second, competing `AGENTS.md`.
