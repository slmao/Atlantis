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
| 0003 | [RHI and Vulkan Windowed Foundation](0003-rhi-vulkan-windowed-foundation.md) | `Approved` | [plans/0003-rhi-vulkan-windowed-foundation.md](../plans/0003-rhi-vulkan-windowed-foundation.md) (`Approved / Ready for Implementation`) | **Implemented and merged** via [PR #14](https://github.com/slmao/Atlantis/pull/14) — `src/rhi/`, `src/vulkan_backend/`, `tests/rhi/`, `tests/vulkan_backend/`, `examples/rhi_vulkan_demo/` exist and match the spec/plan. This is a non-frame, windowed Vulkan presentation *foundation*: swapchain/surface construction and recreation only — no frame acquire/present loop, no `RenderTarget`, and no rendered output are implemented. Spec 0003 itself did not implement RenderGraph; RenderGraph was subsequently implemented under Spec 0005 and merged via [PR #18](https://github.com/slmao/Atlantis/pull/18) (see the Spec 0005 row below). `src/renderer/` still does not exist. | Depends on Specs 0001, 0002 (Windows). ADR-0001–0005 (amended), ADR-0011, ADR-0013, ADR-0014–0016 (`Accepted`). |
| 0004 | [Context-Efficient Documentation and Code Comment Guidelines](0004-context-efficiency-guidelines.md) | `Approved` | [plans/0004-context-efficiency-guidelines.md](../plans/0004-context-efficiency-guidelines.md) (`Approved / Ready for Implementation`) | **Implemented** — `AGENTS.md` now contains the `## Documentation and code comments` section; merged via PR #11 | Governance/documentation convention only; no ADR required (see Architectural Impact). No runtime architecture impact. No dependencies on other specs. |
| 0005 | [RenderGraph Foundation (GPU-Independent Graph Core)](0005-render-graph-foundation.md) | `Approved` | [plans/0005-render-graph-foundation.md](../plans/0005-render-graph-foundation.md) (`Approved / Ready for Implementation`) | **Implemented and merged** via [PR #18](https://github.com/slmao/Atlantis/pull/18) — `src/render_graph/`, `tests/render_graph/` exist and match the spec/plan. This is a GPU-independent RenderGraph *construction/compilation* foundation: pass/resource declaration, single-producer dependency derivation, and deterministic compilation only — no RHI resource binding, command recording, GPU execution, barriers, pass culling, or resource lifetime/aliasing are implemented, and `src/renderer/` still does not exist. | Depends on Spec 0003 (RHI/Vulkan windowed foundation) — implemented, dependency satisfied. [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md) and [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md) both `Accepted` alongside this spec's Human Review Approval, recorded 2026-08-09 (see the spec's own Human Review Approval note — all 16 architectural decisions accepted). Joint Spec 0005 + Plan 0005 Human Review completed 2026-08-09, accepting all 19 Plan-stage details in the Plan's own Section 7 with zero Human Review blockers. |
| 0006 | [RHI / RenderGraph Frame Execution Foundation](0006-rhi-render-graph-frame-execution-foundation.md) | `Approved` | None yet — **not drafted**. Per [AGENTS.md](../AGENTS.md), a Plan may now be drafted against this `Approved` spec, but implementation may not begin until that Plan itself passes Human Review. | **Not implemented** — no Plan exists yet, so no `src/` change has been made. This spec fixes the concrete `RenderTarget`/acquire/present/`CommandList`/RenderGraph-execution contract; it does not itself implement anything. | Depends on Spec 0003 (RHI/Vulkan windowed foundation, `Approved`/implemented) and Spec 0005 (RenderGraph Foundation, `Approved`/implemented) — both dependencies satisfied. [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md), [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md), and [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md) all `Accepted` alongside this spec's Human Review Approval, recorded 2026-08-09 (see the spec's own Human Review Approval note). This spec is the frame-execution prerequisite the Candidate Spec Backlog's "Minimal Renderer" entry (Section B below) needs in addition to Spec 0005. |

No spec above is marked `Complete` — this registry only uses the status
values `specs/README.md` and `plans/README.md` already define
(`Draft` / `In Review` / `Approved` / `Rejected` / `Superseded`, and for
plans additionally `Approved / Ready for Implementation`). Where a
spec's Plan records an explicit Human Review approval note (Specs 0001,
0002, and 0003 do, with dates, in their own Plan files), that is the
authoritative record — this table does not restate it, only points to
it. Plan 0003 records a joint Spec + Plan Human Review completed
2026-08-08; Spec 0003 itself remains `Approved` (an approved spec does
not change status when its plan is implemented). This table's
Implementation column records the fact that PR #14 merged Spec 0003's
implementation into `main`; the registry itself grants no new approval
and is not a substitute for reading the Plan or PR.

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
| 1 | Minimal Renderer | Spec 0005 (RenderGraph Foundation) — `Approved`, implemented; Spec 0006 (RHI / RenderGraph Frame Execution Foundation) — `Approved`, no Plan yet, not implemented | Minimal mesh/depth/camera/material closed loop, built only on Core, RHI, and RenderGraph | Candidate — spec not yet drafted |
| 2 | Shader System Foundation | Candidate 1 (Renderer, for a real consumer) | Phase 1 shader source compilation to SPIR-V, reflection, pipeline layout — Vulkan/SPIR-V only, no other backend target | Candidate — spec not yet drafted |
| 3 | Android Platform and Vulkan Presentation | Spec 0002 (Platform interface), Spec 0003 (RHI/Vulkan Backend construction boundary) | Android Activity/Surface lifecycle, `ANativeWindow` borrowed semantics, `VK_KHR_android_surface` WSI, reusing the existing RHI/RenderGraph/Renderer stack unchanged | Candidate — spec not yet drafted |
| 4 | Headless Rendering | Candidate 1 (Renderer) — must follow the windowed path per [AGENTS.md](../AGENTS.md) | Offscreen `RenderTarget` construction and GPU readback, sharing Renderer/RHI/RenderGraph with the windowed path | Candidate — spec not yet drafted |
| 5 | Image Regression Testing | Candidate 4 (Headless rendering) | Golden-image comparison, tolerance methodology, CI gating | Candidate — spec not yet drafted |
| 6 | Asset System Foundation | Candidate 1 (Renderer, for real asset consumers) | Asset GUID/metadata model, authoring-to-runtime conversion boundary | Candidate — spec not yet drafted |
| 7 | Runtime Host and Composition Root | Spec 0002 (Platform), Spec 0003 (RHI), Spec 0005 (RenderGraph Foundation), Candidate 1 (Renderer) | The actual `Atlantis Runtime` module — distinct from Spec 0003's own non-shipping verification composition, which is explicitly not a preview of Runtime | Candidate — spec not yet drafted |
| 8 | World/ECS Foundation | Candidate 7 (Runtime) | Authoritative world/entity state model — no ECS library or design chosen yet | Candidate — spec not yet drafted; ECS choice not made |
| 9 | Serialization and Stable Identity | Candidate 8 (World/ECS) | Stable GUID/handle schemes, schema versioning and migration | Candidate — spec not yet drafted |
| 10 | Tool/Editor Connection Protocol | Candidate 7 (Runtime) | Editor↔Runtime communication boundary — in-process vs. multi-process model not chosen | Candidate — spec not yet drafted |
| 11 | Gameplay SDK | Candidate 8 (World/ECS), Candidate 10 (Tool/Editor protocol) | First-party gameplay authoring surface — gameplay language not chosen | Candidate — spec not yet drafted |
| 12 | Research/Simulation API | Candidate 7 (Runtime), Candidate 4 (Headless rendering) | Observation/Action/Episode-style research interface — language/transport not chosen | Candidate — spec not yet drafted |
| 13 | AI Inference Integration | Candidate 7 (Runtime) | Pluggable inference backend for gameplay/tools use — must not shape Phase 1 rendering abstractions; inference backend not chosen | Candidate — spec not yet drafted; future AI workload, does not shape Phase 1 |
| 14 | UGC Sandbox and Package Model | Candidate 11 (Gameplay SDK) | Sandboxed, capability-scoped user content execution — UGC VM/language not chosen | Candidate — spec not yet drafted; future UGC workload, does not shape Phase 1 |

Items 12–14 (and any future neural-rendering, GPU-driven-rendering, or
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
