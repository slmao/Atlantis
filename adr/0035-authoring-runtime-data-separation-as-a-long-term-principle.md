# ADR 0035: Authoring/Runtime Data Separation as a Long-Term Principle

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0009-long-term-engine-architecture-alignment.md](../specs/0009-long-term-engine-architecture-alignment.md)

## Context

Atlantis has no scene graph, entity/component model, or asset pipeline
yet — `Atlantis Renderer` ([specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md))
deliberately scoped itself to a single, fixed, hand-authored mesh and a
single fixed material, explicitly excluding a scene graph, asset system,
or model loader as Non-Goals. Candidate Backlog items 4 (Asset System
Foundation) and 6 (World/ECS Foundation) — both not yet specced — are
where a real authoring-facing data model would first appear.

A human-provided external architecture draft proposes a specific,
named separation: an "Authoring World" (Scene/Entity/Component, shaped
for editing) that is baked/compiled into a "Runtime World" (ECS/SoA,
shaped for execution), explicitly stating these need not correspond
one-to-one. Atlantis's own existing precedent already leans this
direction narrowly: Spec 0008's Shader System reflection pipeline
already separates an authoring-time concept (a `.slang` source file) from
a runtime-consumed artifact (build-time-compiled SPIR-V plus a versioned
reflection schema) — but this has never been named as a general
principle intended to apply beyond shaders.

Deciding this now, in the abstract, without a real World/ECS or Asset
Spec to apply it to, risks locking in a specific baking/compilation
*mechanism* before any concrete authoring or runtime data model exists
to validate it against — exactly the kind of premature architectural
commitment AGENTS.md's Golden Rule warns against. But leaving the
question entirely open until Candidate 6 is specced risks that Spec
inventing (or, worse, silently *not* separating authoring and runtime
representations) without having weighed the option at all.

## Decision

**Authoring-facing data representations and runtime-execution data
representations are permitted, as a matter of long-term principle, to
differ — a future World/ECS or Asset Spec is not required to make
runtime storage layout match whatever shape is most convenient for a
human author or tool to edit.** This is adopted as a standing
*permission and expectation to weigh*, not a mandate that every future
data model must include a distinct bake/compile step.

- A future World/ECS Spec (Candidate 6) and a future Asset System Spec
  (Candidate 4) must each explicitly address, in their own Architectural
  Impact section, whether their authoring-facing representation and
  their runtime-execution representation are the same data structure or
  two related-but-distinct ones — and if distinct, name the
  transformation step (however it works) that connects them. Silence on
  this question is not acceptable once this ADR is `Accepted`.
- This principle generalizes an already-real precedent in this
  repository — Shader System's own `.slang` source → build-time-compiled
  artifact → versioned `ReflectionMetadata` schema pipeline
  ([ADR-0028](0028-shader-system-source-language-and-compiler.md)–[ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md))
  — as the first concrete instance Atlantis has actually built and
  shipped, not a hypothetical.
- This ADR does not decide: whether a bake/compile step is synchronous or
  asynchronous, whether it is a build-time or load-time operation,
  whether authoring and runtime representations live in the same process
  or different ones, or any concrete Entity/Component/ECS design. Those
  belong entirely to Candidate 4 and Candidate 6's own future Specs.
- This ADR does not require every future data model to have a
  distinct authoring representation — a future Spec may explicitly
  decide "authoring and runtime representation are the same structure
  here" if it states that decision and its reasoning, rather than
  defaulting into it without consideration.

## Consequences

### Positive

- Gives Candidate 4 (Asset System) and Candidate 6 (World/ECS) explicit
  license, from day one of their own Spec drafting, to choose a
  data-oriented runtime representation without being constrained by
  whatever shape is most natural for a future editing tool to present —
  supporting AGENTS.md's own stated goal of Windowed-then-Headless
  rendering eventually feeding large-scale simulation/research use cases
  that need runtime memory efficiency.
- Names and generalizes a pattern this repository has already built once
  (Shader System's own source → artifact → schema pipeline), making it
  easier to recognize and reuse the pattern rather than re-derive it.
- Keeps the actual bake/compile mechanism, and the question of whether
  one is even needed, as an open Requirement for the Spec that actually
  needs it — avoiding speculative ECS/authoring-tool design ahead of any
  concrete Editor or asset-authoring consumer.

### Negative / Trade-offs

- A bake/compile boundary, once a future Spec adopts one, is itself a
  real engineering cost (two representations to keep consistent, a
  transformation step to build/test/version) — this ADR states the
  *option* is available and expected to be considered, not that it is
  free; the future Spec choosing it must justify it on its own terms.
- Because this ADR intentionally does not mandate universal separation,
  a future Spec could still choose "same representation for authoring
  and runtime" for a case where separation would have served better —
  this ADR only requires that choice be made explicitly, it cannot force
  it to be made well.

## Alternatives Considered

- **Mandate authoring/runtime separation unconditionally for every
  future data model.** Rejected: too strong a commitment to make before
  Candidate 4/6 exist — some future data (e.g. small, rarely-touched
  configuration) may have no meaningful reason to maintain two
  representations, and mandating one anyway would be exactly the
  speculative over-engineering AGENTS.md warns against.
- **Say nothing, defer entirely to Candidate 6's own Spec.** Rejected:
  the risk is not that Candidate 6 lacks the freedom to choose separation
  — it already has that freedom — but that without this ADR naming the
  option and requiring it be explicitly weighed, a future Spec could
  default into "authoring IS runtime" (the simplest thing to write first)
  without ever having stated that as a considered trade-off, closing the
  door on the option later once real content depends on the merged
  representation.
- **Decide the concrete Bake/Compile mechanism now** (e.g. a general
  "Component Bake" system translating one authoring component into many
  runtime components, as the external draft illustrates). Rejected: no
  ECS, no authoring component model, and no concrete multi-component
  bake case exists in this repository yet — this is exactly a
  future-subsystem implementation detail Spec 0009's own governance
  boundary defers to Candidate 6.
