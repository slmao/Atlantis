# ADR 0037: Long-Term Device Backend Extensibility Without Phase 1 Scaffolding

- **Status:** Accepted
- **Date:** 2026-08-15
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-15 as part of Spec 0009's Human Review Approval
- **Related Spec:** [specs/0009-long-term-engine-architecture-alignment.md](../specs/0009-long-term-engine-architecture-alignment.md)

## Context

Atlantis's existing, `Accepted` architecture already draws a specific,
narrow line around graphics backends. [AGENTS.md](../AGENTS.md) states it
explicitly: "Graphics backend: **Vulkan only** — do not scaffold for
other backends behind the RHI 'for later'; the RHI must stay
backend-independent in interface, but no second backend gets implemented
in Phase 1" (AGENTS.md, Architecture principles). [ADR-0001](0001-rhi-backend-independence.md)
already establishes RHI as a backend-agnostic interface with exactly one
concrete implementation today — Atlantis Vulkan Backend, the only module
permitted to include Vulkan headers or reference `Vk*` types. Atlantis
Renderer and Atlantis RenderGraph depend only on RHI, RenderGraph, and
Core — never on a concrete backend. Atlantis's Phase 1 target platforms
are Windows and Android; AGENTS.md states iOS is a future target ("not
started, not designed") and explicitly leaves open whether it eventually
uses Vulkan via MoltenVK or a native Metal RHI backend — "that choice is
explicitly undecided and is not to be designed or scaffolded for now."
Atlantis Shader System
([Spec 0008](../specs/0008-shader-system-foundation.md),
[ADR-0028](0028-shader-system-source-language-and-compiler.md)) compiles
Slang to SPIR-V only; no other shader target exists or is scaffolded.

A human maintainer has now given a further, explicit long-term
direction: Atlantis should eventually be able to support Direct3D 12 and
Metal as additional concrete Device Backends, and the long-term
architecture should reserve a clear place for them — without this
meaning Phase 1 starts building, scaffolding, or designing either one.
This is a narrower and more concrete instruction than [Spec 0009](../specs/0009-long-term-engine-architecture-alignment.md)'s
original treatment of "a second graphics backend," which — before this
revision — recorded the topic only as "explicitly rejected or deferred
for this round," without distinguishing between rejecting *scaffolding
now* (which AGENTS.md already rules out and this ADR does not reopen)
and rejecting *ever naming a long-term position* for a second backend
(which this ADR exists to correct).

The risk this ADR has to navigate on both sides: saying nothing leaves
a genuine long-term intent unrecorded, risking a future Spec either
re-litigating "should Atlantis ever support D3D12/Metal at all" from
scratch, or — worse — a future contributor reading AGENTS.md's Phase 1
prohibition as if it were a permanent architectural verdict rather than
a Phase 1 scoping rule. Saying too much — a concrete backend factory,
capability tiers, conditional compilation, or SDK dependencies with no
consumer — would be exactly the premature scaffolding AGENTS.md already
prohibits, and exactly the kind of uncontrolled architectural decision
the Golden Rule warns against.

## Decision

**Vulkan remains the only implemented Device Backend in Phase 1, and
this ADR authorizes no change to that.** Atlantis's long-term
architecture reserves a conceptual, boundary-level position for future
sibling Device Backends (Direct3D 12, Metal), without creating any
code, directory, target, dependency, or abstraction for them now.

### Current implementation (unchanged by this ADR)

- Vulkan remains the only implemented Device Backend in Phase 1.
- The existing Atlantis Vulkan Backend remains the sole concrete
  implementation of RHI. No existing module is renamed, restructured, or
  reinterpreted by this decision.
- `Device Backend` is a conceptual category this ADR names for
  discussion purposes — it is not a new C++ interface, source directory,
  CMake target, or tenth top-level module. Atlantis's nine-module
  source-ownership view ([ADR-0032](0032-conceptual-architecture-layers-versus-source-module-ownership.md))
  is unchanged: it still lists Atlantis Vulkan Backend as the one
  concrete backend module, not "a Device Backend module."

### Long-term position

- Future Direct3D 12 and Metal implementations, if and when approved by
  their own future Specs, would be sibling concrete Device Backend
  modules behind the same RHI boundary Atlantis Vulkan Backend already
  sits behind — not a replacement for it, not a reason to change RHI's
  existing public surface today.
- Atlantis Renderer and Atlantis RenderGraph must continue to have no
  direct dependency on Vulkan, Direct3D 12, Metal, DXGI, MetalKit, or any
  backend-specific handle type — the same boundary [ADR-0001](0001-rhi-backend-independence.md)
  already enforces for Vulkan today, stated here as a standing
  expectation for any future sibling backend, not a new rule.
- Backend-specific API types remain private to their own concrete
  Backend module, exactly as Vulkan types are private to Atlantis Vulkan
  Backend today ([AGENTS.md](../AGENTS.md) Vulkan-specific rules,
  [ADR-0001](0001-rhi-backend-independence.md)).
- Selecting and owning a concrete Backend at runtime remains a future,
  separately reviewed Runtime/composition-root decision — this ADR does
  not decide, and does not need to decide, how or when a Backend gets
  chosen.

### What "reservation" means, precisely

Reserving a long-term position for future Device Backends is limited to
the following, and only the following:

- Keeping RHI's public interface free of Vulkan (or any backend-specific)
  types, as already required.
- Keeping Renderer's and RenderGraph's dependencies backend-neutral, as
  already required.
- Keeping Backend source ownership open to future, independently-approved
  sibling implementations — i.e., not writing anything into Atlantis's
  architecture that would make adding a second backend module structurally
  impossible later.
- Naming Direct3D 12 and Metal as candidate positions in Atlantis's
  conceptual architecture view and in future-facing roadmap language —
  never as committed, scheduled, or in-progress work.
- Recording, in this ADR, the boundary constraints a future backend
  would need to satisfy, so that whichever future Spec proposes one does
  not have to re-derive them from first principles.

Reservation explicitly does **not** include, and this ADR authorizes
none of the following now:

- Empty `d3d12/` or `metal/` source directories.
- Empty CMake targets for either backend.
- `#ifdef ATLANTIS_D3D12` / `#ifdef ATLANTIS_METAL` conditional-compilation
  scaffolding anywhere in the tree.
- A Backend registry or factory abstraction with no real second
  consumer to justify it.
- An abstract Capability/feature-tier system layered onto RHI ahead of
  any second backend needing one.
- A Backend "extension" or escape-hatch API on RHI's public surface.
- A DXIL or MSL shader compilation pipeline, or any change to Shader
  System's Slang-→-SPIR-V-only scope ([Spec 0008](../specs/0008-shader-system-foundation.md),
  [ADR-0028](0028-shader-system-source-language-and-compiler.md)–[ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md),
  unchanged, not reopened by this ADR).
- Any Direct3D 12/DXGI or Metal/MetalKit SDK dependency added to the
  build.
- Any change to RHI's current public API made in anticipation of a
  future backend.
- Narrowing Vulkan's own current capability surface down to some
  imagined lowest-common-denominator subset shared with unbuilt
  backends.
- Deciding, now, whether a future iOS Platform uses Vulkan via MoltenVK
  or a native Metal backend — AGENTS.md already states this is
  undecided, and this ADR does not decide it either.

### Future approval gate

Direct3D 12 and Metal each require their own, independently reviewed:

- Spec, Plan, and ADR(s), each going through this repository's normal
  Spec → Plan → Human Review → Implementation → Verification → PR →
  Merge process.
- Statement of platform and toolchain requirements (SDK versions, target
  OS/device support).
- Resource, synchronization, and presentation mapping onto RHI's
  existing abstractions (or a proposed, separately reviewed change to
  those abstractions, if one turns out to be needed).
- A Shader System target strategy — Direct3D 12 needs DXIL, Metal needs
  MSL; neither exists today, and Spec 0008/ADR-0028's Slang-→-SPIR-V-only
  decision is not reopened by this ADR.
- A validation strategy analogous to Atlantis's existing mandatory
  Vulkan Validation Layers discipline.
- Real GPU verification on the target hardware/OS, following this
  repository's existing verification standards.

This ADR explicitly does not commit to a timetable for either backend.
Direct3D 12 would most plausibly target Windows, but this ADR makes no
scheduling commitment. Metal may plausibly relate to a future iOS
Platform, but this ADR does not decide whether iOS uses Metal or Vulkan
via MoltenVK — that remains explicitly undecided, per AGENTS.md.
**macOS is not, and does not become, an Atlantis target platform by
virtue of Metal being named here** — Atlantis's target platforms remain
Windows and Android for Phase 1, with iOS as a future, not-yet-started,
not-yet-designed target; naming Metal as a candidate Windows/Android-era
non-target's backend does not imply macOS support. WebGPU is not
reserved a position by this ADR at all — it is outside this ADR's scope
entirely, not merely deferred. Before any second backend implementation
begins, Atlantis will likely need to revisit whether Shader System's
current Slang-→-SPIR-V-only scope should change — this ADR flags that as
a foreseeable future question and does not modify Spec 0008 or
ADR-0028–0031 in any way now.

## Alternatives Considered

- **Say nothing about future backends; leave AGENTS.md's Phase 1
  prohibition as the only statement on the subject.** Rejected: risks a
  future contributor reading a Phase 1 scoping rule as a permanent
  architectural verdict, and gives no shared long-term position for a
  future D3D12 or Metal Spec to build against if one is ever proposed.
- **Build a general Backend factory/capability scaffolding now**, so a
  future backend "slots in" more easily. Rejected: this is precisely the
  premature, no-consumer scaffolding AGENTS.md's Phase 1 rule and the
  Golden Rule both prohibit — no second backend exists to validate any
  such abstraction against, and it risks constraining Vulkan's own
  Phase 1 design around a hypothetical future need.
- **Declare Direct3D 12 and Metal as committed roadmap items now**, with
  implied scheduling. Rejected: this repository's own governance
  ("a milestone being listed does not authorize starting it") and this
  Spec's own Roadmap Impact section both treat backlog/roadmap language
  as non-binding; declaring a specific backend "committed" would overstate
  what has actually been decided and risks a product-positioning claim
  this ADR is not positioned to make.
- **Record only the long-term sibling-backend boundary, with no
  implementation scaffolding.** **Recommended.** Gives future Specs a
  named, stable position to design against, without adding any
  no-consumer code, dependency, or abstraction to Phase 1 — consistent
  with how this repository has already handled every other long-term
  direction in Spec 0009's companion ADRs. Human Review remains free to
  select a different alternative above.

## Consequences

### Positive

- States a real, previously-unrecorded long-term intent (support for
  D3D12 and Metal) without touching Phase 1's actual scope, code, or
  RHI/Renderer/RenderGraph boundaries.
- Gives a future Direct3D 12 Spec or Metal Spec a stated boundary
  (RHI-level sibling backend, no Renderer/RenderGraph backend
  dependency, private backend-specific types) to design against, rather
  than each having to independently re-derive what AGENTS.md's and
  ADR-0001's Vulkan-specific rules would imply for a different backend.
- Keeps Phase 1 exactly as scoped today — Vulkan-only, no scaffolding —
  fully consistent with AGENTS.md's existing, unmodified prohibition.

### Negative / Trade-offs

- Because this ADR deliberately adds no factory, capability system, or
  other code-level reservation, a future Direct3D 12 or Metal Spec may
  still find that RHI's current interface needs to evolve to
  accommodate it — this ADR does not pretend the current interface is
  already sufficient for a second backend; any such evolution must go
  through its own future Spec and ADR, not be assumed here.
- Naming Direct3D 12 and Metal explicitly, even as long-term candidates
  only, carries a real risk of being misread — by a human skimming
  Atlantis's architecture docs, or by an AI agent working from this
  repository — as a product commitment or near-term timetable. This ADR
  and Spec 0009 must both repeat, wherever these names appear, that no
  timetable and no implementation is authorized.
- Two already-`Accepted` decisions (AGENTS.md's Phase 1 Vulkan-only rule,
  ADR-0001) are referenced extensively here; readers must not mistake
  this ADR as having loosened or reinterpreted either — it does not, and
  neither is reopened or modified by this ADR.
