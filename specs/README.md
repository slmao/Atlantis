# specs/

A spec is a proposal: problem, requirements, proposed design, and an
explicit statement of architectural impact. It is written *before*
implementation, reviewed, and approved by a human before a plan is written
against it.

- Template: [template.md](template.md)
- File naming: `specs/<feature-slug>.md`, matching the eventual
  `plans/<feature-slug>.md` and branch name for traceability.
- Status values: `Draft` → `In Review` → `Approved` (or `Rejected` /
  `Superseded by <link>`).
- If a spec's Architectural Impact section says an ADR is required, the ADR
  must exist (at least as `Proposed`) before the spec can move to
  `Approved`.

Full process: [AGENTS.md](../AGENTS.md).

## Spec Registry

This registry is a status index, maintained alongside the specs
themselves. It is not a substitute for reading the spec/plan/ADR files
it links, and it does not grant approval on its own — see the
maintenance rules at the end of this section. For the broader roadmap
this registry feeds into, see
[docs/project-blueprint.md](../docs/project-blueprint.md).

### A. Existing Specs

| ID | Title | Status | Plan | Implementation | Dependencies / Notes |
|---|---|---|---|---|---|
| 0001 | [Project Foundation](0001-project-foundation.md) | `Approved` | [plans/0001-project-foundation.md](../plans/0001-project-foundation.md) (`Approved / Ready for Implementation`) | **Implemented** — `src/core/`, `tests/core/`, `examples/foundation_demo/` exist and match the spec/plan | ADR-0006–0010 (`Accepted`). No dependencies on other specs. |
| 0002 | [Platform Foundation](0002-platform-foundation.md) | `Approved` | [plans/0002-platform-foundation.md](../plans/0002-platform-foundation.md) (`Approved / Ready for Implementation`, Windows portion) | **Windows portion implemented** — `src/platform/` (Windows), `tests/platform/` (incl. Windows smoke tests), `examples/platform_demo/`. Android and iOS remain architecture-only; no `src/platform/src/android/` or `.../ios/` directory exists | Depends on Spec 0001 (Core). ADR-0005 (amended), ADR-0010–0013 (`Accepted`). |
| 0003 | [RHI and Vulkan Windowed Foundation](0003-rhi-vulkan-windowed-foundation.md) | `Approved` | **Not yet created** — the spec's own header states "Related Plan(s): None yet"; no `plans/0003-*.md` file exists in this repository | **Not implemented** — no `src/rhi/`, `src/vulkan_backend/`, `src/render_graph/`, or `src/renderer/` directory exists. Next step per [AGENTS.md](../AGENTS.md) is writing Plan 0003 and clearing Human Review, not implementation. | Depends on Specs 0001, 0002 (Windows). ADR-0001–0005 (amended), ADR-0011, ADR-0013, ADR-0014–0016 (`Accepted`). |

No spec above is marked `Complete` — this registry only uses the status
values `specs/README.md` and `plans/README.md` already define
(`Draft` / `In Review` / `Approved` / `Rejected` / `Superseded`, and for
plans additionally `Approved / Ready for Implementation`). Where a
spec's Plan records an explicit Human Review approval note (Specs 0001
and 0002 do, with dates, in their own Plan files), that is the
authoritative record — this table does not restate it, only points to
it. Spec 0003 currently has no Plan and therefore no recorded Human
Review; this table does not assert one occurred.

### B. Candidate Spec Backlog

**Not an approved commitment.** This is a dependency-ordered list of
candidate future work, for planning visibility only. No entry here has
a Spec written, and none is authorized for implementation — see
[AGENTS.md](../AGENTS.md) and
[docs/project-blueprint.md](../docs/project-blueprint.md) Section 1.
No formal spec number is pre-assigned to any entry; numbers are
assigned only when a real spec is drafted.

| Candidate Order | Working Title | Depends On | Intended Outcome | Governance State |
|---|---|---|---|---|
| 1 | RenderGraph Foundation | Spec 0003 (RHI/Vulkan windowed foundation) | Pass declaration, resource dependency tracking, barrier/lifetime resolution, execution ordering — the mandatory path for all GPU work | Candidate — spec not yet drafted |
| 2 | Minimal Renderer | Candidate 1 (RenderGraph) | Minimal mesh/depth/camera/material closed loop, built only on Core, RHI, and RenderGraph | Candidate — spec not yet drafted |
| 3 | Shader System Foundation | Candidate 2 (Renderer, for a real consumer) | Phase 1 shader source compilation to SPIR-V, reflection, pipeline layout — Vulkan/SPIR-V only, no other backend target | Candidate — spec not yet drafted |
| 4 | Android Platform and Vulkan Presentation | Spec 0002 (Platform interface), Spec 0003 (RHI/Vulkan Backend construction boundary) | Android Activity/Surface lifecycle, `ANativeWindow` borrowed semantics, `VK_KHR_android_surface` WSI, reusing the existing RHI/RenderGraph/Renderer stack unchanged | Candidate — spec not yet drafted |
| 5 | Headless Rendering | Candidate 2 (Renderer) — must follow the windowed path per [AGENTS.md](../AGENTS.md) | Offscreen `RenderTarget` construction and GPU readback, sharing Renderer/RHI/RenderGraph with the windowed path | Candidate — spec not yet drafted |
| 6 | Image Regression Testing | Candidate 5 (Headless rendering) | Golden-image comparison, tolerance methodology, CI gating | Candidate — spec not yet drafted |
| 7 | Asset System Foundation | Candidate 2 (Renderer, for real asset consumers) | Asset GUID/metadata model, authoring-to-runtime conversion boundary | Candidate — spec not yet drafted |
| 8 | Runtime Host and Composition Root | Spec 0002 (Platform), Spec 0003 (RHI), Candidate 1–2 (RenderGraph, Renderer) | The actual `Atlantis Runtime` module — distinct from Spec 0003's own non-shipping verification composition, which is explicitly not a preview of Runtime | Candidate — spec not yet drafted |
| 9 | World/ECS Foundation | Candidate 8 (Runtime) | Authoritative world/entity state model — no ECS library or design chosen yet | Candidate — spec not yet drafted; ECS choice not made |
| 10 | Serialization and Stable Identity | Candidate 9 (World/ECS) | Stable GUID/handle schemes, schema versioning and migration | Candidate — spec not yet drafted |
| 11 | Tool/Editor Connection Protocol | Candidate 8 (Runtime) | Editor↔Runtime communication boundary — in-process vs. multi-process model not chosen | Candidate — spec not yet drafted |
| 12 | Gameplay SDK | Candidate 9 (World/ECS), Candidate 11 (Tool/Editor protocol) | First-party gameplay authoring surface — gameplay language not chosen | Candidate — spec not yet drafted |
| 13 | Research/Simulation API | Candidate 8 (Runtime), Candidate 5 (Headless rendering) | Observation/Action/Episode-style research interface — language/transport not chosen | Candidate — spec not yet drafted |
| 14 | AI Inference Integration | Candidate 8 (Runtime) | Pluggable inference backend for gameplay/tools use — must not shape Phase 1 rendering abstractions; inference backend not chosen | Candidate — spec not yet drafted; future AI workload, does not shape Phase 1 |
| 15 | UGC Sandbox and Package Model | Candidate 12 (Gameplay SDK) | Sandboxed, capability-scoped user content execution — UGC VM/language not chosen | Candidate — spec not yet drafted; future UGC workload, does not shape Phase 1 |

Items 13–15 (and any future neural-rendering, GPU-driven-rendering, or
world-model candidate) are future AI/UGC/neural workloads: per
[AGENTS.md](../AGENTS.md), they are future phases and must not shape
Phase 1 abstractions ahead of their own approved spec.

### Backlog maintenance rules

- When a candidate item gets a real Spec drafted, assign it a formal
  spec number at that point and move/replace its row into Section A —
  do not pre-assign a number while it is still a backlog entry.
- Status changes in either table come only from a real Spec, Plan, or
  merged PR — never from editing this registry's prose alone.
- This registry is not a Human Review approval and does not authorize
  implementation of anything listed in it.
- An Accepted ADR or an Approved Spec always takes precedence over
  anything stated in this registry.
