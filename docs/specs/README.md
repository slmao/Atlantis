# specs/

Specs define the problem, requirements, proposed design, and architectural impact.
Use [template.md](template.md); name files `NNNN-<slug>.md`, matching their Plans
and branches. Status values: `Draft` → `In Review` → `Approved`, or `Rejected` /
`Superseded by <link>`.

Approval evidence belongs in the reviewing PR; implementation results belong in
the implementation PR. Authoring rules:
[AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Review and editorial lifecycle:
[Git workflow](../process/git-workflow.md#specs-plans-and-adrs-are-versioned-like-code).
Required ADRs must exist before Spec approval and be Accepted before implementation
approval under the [repository workflow](../../AGENTS.md#the-workflow-stage-by-stage).

## Spec Registry

Current navigation only; rows confer no approval. Specs and Plans own their
requirements and link their ADRs. PRs own review and implementation evidence.
The broader roadmap is in [the blueprint](../project-blueprint.md).

### A. Existing Specs

All statuses summarize the linked evidence. A historical Plan header may still
say an implementation is pending even when its PR below has merged; this table
keeps approval and implementation state separate.

| ID | Title | Spec status | Plan | Implementation | Dependencies / Notes |
|---|---|---|---|---|---|
| 0001 | [Project Foundation](0001-project-foundation.md) | Approved | [Plan 0001](../plans/0001-project-foundation.md) (Approved) | Merged: [PR #1](https://github.com/slmao/Atlantis/pull/1) | No Spec dependency; Core/build foundation. |
| 0002 | [Platform Foundation](0002-platform-foundation.md) | Approved | [Plan 0002](../plans/0002-platform-foundation.md) (Approved) | Windows merged: [PR #2](https://github.com/slmao/Atlantis/pull/2), [PR #5](https://github.com/slmao/Atlantis/pull/5), [PR #6](https://github.com/slmao/Atlantis/pull/6) | Depends on 0001; Windows delivered, Android/iOS remain unimplemented. |
| 0003 | [RHI and Vulkan Windowed Foundation](0003-rhi-vulkan-windowed-foundation.md) | Approved | [Plan 0003](../plans/0003-rhi-vulkan-windowed-foundation.md) (Approved) | Merged: [PR #14](https://github.com/slmao/Atlantis/pull/14) | Depends on 0001, 0002; presentation construction/recreation foundation. |
| 0004 | [Context-Efficient Documentation and Code Comment Guidelines](0004-context-efficiency-guidelines.md) | Approved | [Plan 0004](../plans/0004-context-efficiency-guidelines.md) (Approved) | Merged: [PR #11](https://github.com/slmao/Atlantis/pull/11) | No Spec dependency; documentation/comment governance. |
| 0005 | [RenderGraph Foundation (GPU-Independent Graph Core)](0005-render-graph-foundation.md) | Approved | [Plan 0005](../plans/0005-render-graph-foundation.md) (Approved) | Merged: [PR #18](https://github.com/slmao/Atlantis/pull/18) | Depends on 0003; GPU-independent graph core. |
| 0006 | [RHI / RenderGraph Frame Execution Foundation](0006-rhi-render-graph-frame-execution-foundation.md) | Approved | [Plan 0006](../plans/0006-rhi-render-graph-frame-execution-foundation.md) (Approved) | Merged: [PR #23](https://github.com/slmao/Atlantis/pull/23), [PR #24](https://github.com/slmao/Atlantis/pull/24) | Depends on 0003, 0005; frame execution. |
| 0007 | [Minimal Renderer](0007-minimal-renderer.md) | Approved | [Plan 0007](../plans/0007-minimal-renderer.md) (Approved) | Merged: [PR #28](https://github.com/slmao/Atlantis/pull/28), [PR #30](https://github.com/slmao/Atlantis/pull/30) | Depends on 0005, 0006; dynamic-rendering correction in [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md). |
| 0008 | [Shader System Foundation](0008-shader-system-foundation.md) | Approved | [Plan 0008](../plans/0008-shader-system-foundation.md) (Approved) | Merged: [PR #36](https://github.com/slmao/Atlantis/pull/36) | Depends on 0007; replaces ADR-0027's temporary shader mechanism. |
| 0009 | [Long-Term Engine Architecture Alignment](0009-long-term-engine-architecture-alignment.md) | Approved | [Plan 0009](../plans/0009-long-term-engine-architecture-alignment.md) (Approved) | Merged: [PR #41](https://github.com/slmao/Atlantis/pull/41) | Depends on 0007, 0008; architecture overview delivered. |
| 0010 | [Headless Rendering Foundation](0010-headless-rendering-foundation.md) | Approved | [Plan 0010](../plans/0010-headless-rendering-foundation.md) (Approved) | Merged: [PR #48](https://github.com/slmao/Atlantis/pull/48) | Depends on 0007; headless RenderTarget path. |
| 0011 | [Image Regression Testing Foundation](0011-image-regression-testing-foundation.md) | Approved | [Plan 0011](../plans/0011-image-regression-testing-foundation.md) (Approved) | Merged: [PR #52](https://github.com/slmao/Atlantis/pull/52) | Depends on 0010; golden-review policy in [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md). |
| 0012 | [Asset System Foundation](0012-asset-system-foundation.md) | Approved | [Plan 0012](../plans/0012-asset-system-foundation.md) (Approved) | Merged: [PR #58](https://github.com/slmao/Atlantis/pull/58) | Depends on 0007; does not wait for Runtime. |
| 0013 | [Runtime Host Foundation](0013-runtime-host-foundation.md) | Approved | [Plan 0013](../plans/0013-runtime-host-foundation.md) (Approved) | Merged: [PR #63](https://github.com/slmao/Atlantis/pull/63) | Depends on 0002, 0003, 0005, 0007, 0008, 0012. |
| 0014 | [World / Scene Foundation](0014-world-scene-foundation.md) | Approved | [Plan 0014](../plans/0014-world-scene-foundation.md) (Approved) | Merged: [PR #68](https://github.com/slmao/Atlantis/pull/68), [PR #70](https://github.com/slmao/Atlantis/pull/70) | Depends on 0013; identity amendment in [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md). |
| 0015 | [Scene Asset & Serialization Foundation](0015-scene-asset-serialization-foundation.md) | Approved | [Plan 0015](../plans/0015-scene-asset-serialization-foundation.md) (Approved) | Merged: [PR #74](https://github.com/slmao/Atlantis/pull/74) | Depends on 0012, 0014; cross-session identity/catalog remains candidate work. |
| 0016 | [Texture & Sampler Foundation](0016-texture-sampler-foundation.md) | Approved | [Plan 0016](../plans/0016-texture-sampler-foundation.md) (Approved) | Merged: [PR #78](https://github.com/slmao/Atlantis/pull/78), [PR #80](https://github.com/slmao/Atlantis/pull/80) | Depends on 0003; texture identity correction via [PR #79](https://github.com/slmao/Atlantis/pull/79). |
| 0017 | [Mesh UV Attribute Foundation](0017-mesh-uv-attribute-foundation.md) | Approved | [Plan 0017](../plans/0017-mesh-uv-attribute-foundation.md) (Approved) | Merged: [PR #84](https://github.com/slmao/Atlantis/pull/84) | Depends on 0012, 0016; static-mesh UV0. |
| 0018 | [Material Asset & Scene Binding Foundation](0018-material-asset-scene-binding-foundation.md) | Approved | [Plan 0018](../plans/0018-material-asset-scene-binding-foundation.md) (Approved) | Merged: [PR #88](https://github.com/slmao/Atlantis/pull/88) | Depends on 0016, 0017; material assets and transactional realization. |
| 0019 | [Lighting Foundation](0019-lighting-foundation.md) | Approved | [Plan 0019](../plans/0019-lighting-foundation.md) (Approved) | Merged: [PR #96](https://github.com/slmao/Atlantis/pull/96) | Depends on 0018, 0020; later dynamic updates are Spec 0022. |
| 0020 | [Mesh Normal Attribute Foundation](0020-mesh-normal-attribute-foundation.md) | Approved | [Plan 0020](../plans/0020-mesh-normal-attribute-foundation.md) (Approved) | Merged: [PR #93](https://github.com/slmao/Atlantis/pull/93) | Depends on 0017; static-mesh normal attribute. |
| 0021 | [Descriptor Pool Capacity Foundation](0021-descriptor-pool-capacity-foundation.md) | Approved | [Plan 0021](../plans/0021-descriptor-pool-capacity-foundation.md) (Approved) | Merged: [PR #100](https://github.com/slmao/Atlantis/pull/100) | Backend descriptor capacity; [ADR-0064](../adr/0064-vulkan-backend-descriptor-pool-growth-ownership-model.md). |
| 0022 | [Dynamic Frame Uniform Updates Foundation](0022-dynamic-frame-uniform-updates-foundation.md) | Approved | [Plan 0022](../plans/0022-dynamic-frame-uniform-updates-foundation.md) (Approved) | Merged: [PR #106](https://github.com/slmao/Atlantis/pull/106) | Builds on 0019; corrected design in [PR #104](https://github.com/slmao/Atlantis/pull/104), ADR-0065 rejected. |
| 0023 | [PBR Material Foundation (Direct Lighting)](0023-pbr-material-foundation.md) | Approved | [Plan 0023](../plans/0023-pbr-material-foundation.md) (Approved) | Merged: [PR #111](https://github.com/slmao/Atlantis/pull/111) | Builds on 0019, 0021; layout correction in [PR #110](https://github.com/slmao/Atlantis/pull/110). |
| 0024 | [HDR Color Pipeline & Output Transfer Foundation](0024-hdr-color-pipeline-output-transfer-foundation.md) | Approved | [Plan 0024](../plans/0024-hdr-color-pipeline-output-transfer-foundation.md) (Approved) | Merged: [PR #115](https://github.com/slmao/Atlantis/pull/115) | Output-transfer follow-on to 0023 / ADR-0067. |
| 0025 | [Image-Based Lighting Foundation](0025-image-based-lighting-foundation.md) | Approved | [Plan 0025](../plans/0025-image-based-lighting-foundation.md) (Approved) | Merged: [PR #119](https://github.com/slmao/Atlantis/pull/119) | Builds on 0023, 0024; path-derived identity correction in [ADR-0069](../adr/0069-environment-asset-preprocessing-and-ownership.md). |
| 0026 | [Visible Sky Foundation](0026-visible-sky-foundation.md) | Approved | [Plan 0026](../plans/0026-visible-sky-foundation.md) (Approved) | Merged: [PR #122](https://github.com/slmao/Atlantis/pull/122) | Builds on 0024, 0025; visible sky. |
| 0027 | [Directional Shadow Foundation](0027-directional-shadow-foundation.md) | Approved | [Plan 0027](../plans/0027-directional-shadow-foundation.md) (Approved) | Merged: [PR #125](https://github.com/slmao/Atlantis/pull/125) | Builds on 0024–0026; bias limitation tracked by Spec 0030. |
| 0028 | [Integrated Multi-Object Showcase Scene](0028-integrated-multi-object-showcase-scene.md) | Approved | [Plan 0028](../plans/0028-integrated-multi-object-showcase-scene.md) (Approved) | Merged: [PR #128](https://github.com/slmao/Atlantis/pull/128) | Composes 0023–0027; default showcase scene. |
| 0029 | [Tangent-Space Normal Mapping Foundation](0029-tangent-space-normal-mapping-foundation.md) | Approved | [Plan 0029](../plans/0029-tangent-space-normal-mapping-foundation.md) (Approved) | Merged: [PR #135](https://github.com/slmao/Atlantis/pull/135) | Approved corrections in Spec/Plan and [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md); existing shadow acne remains disclosed. |
| 0030 | [Directional Shadow Bias Stability](0030-directional-shadow-bias-stability.md) | Approved; implementation deferred | [PR #134](https://github.com/slmao/Atlantis/pull/134) (Draft; closed without merge) | Deferred | No longer blocks 0029; see [ADR-0072 deferral](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md#empirical-deferral-record--2026-09-07). |
| 0031 | [Manual Camera Exposure Foundation](0031-manual-camera-exposure-foundation.md) | Approved | [Plan 0031](../plans/0031-manual-camera-exposure-foundation.md) (Approved) | Merged: [PR #138](https://github.com/slmao/Atlantis/pull/138) | Manual exposure / output-transform contract; [ADR-0075](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md). |
| 0032 | [Runtime Sample Scene Selection](0032-runtime-sample-scene-selection.md) | Approved | [Plan 0032](../plans/0032-runtime-sample-scene-selection.md) (Approved) | Merged: [PR #141](https://github.com/slmao/Atlantis/pull/141) | Selects existing 0025/0028/0029 scenes. |
| 0033 | [Documentation Lifecycle and Historical Compaction](0033-documentation-lifecycle-and-compaction.md) | Approved | [Plan 0033](../plans/0033-documentation-lifecycle-and-compaction.md) (Approved; implementation [PR #144](https://github.com/slmao/Atlantis/pull/144) merged) | Implemented: [PR #144](https://github.com/slmao/Atlantis/pull/144) merged | Follow-up to 0004; guidance/templates, registry, and 0004 pilot only. |

### B. Candidate Spec Backlog

Candidates are unapproved work, not implementation commitments. Assign a formal
Spec number only when drafting begins. The current candidate order is retained:

| Candidate Order | Working Title | Depends On | Intended Outcome | Governance State |
|---|---|---|---|---|
| 1 | Android Platform and Vulkan Presentation | Specs 0002, 0003 | Android Activity/Surface lifecycle, borrowed `ANativeWindow`, Android Vulkan WSI, shared rendering stack | Candidate |
| 2 | Tool/Editor Connection Protocol | Spec 0013 | Editor–Runtime communication; process model undecided | Candidate |
| 3 | Gameplay SDK | Spec 0014, Candidate 2 | First-party gameplay surface; language undecided | Candidate |
| 4 | Research/Simulation API | Specs 0013, 0010 | Observation/action/episode interface; language/transport undecided | Candidate |
| 5 | AI Inference Integration | Spec 0013 | Inference backend for gameplay/tools; backend undecided | Candidate; future phase |
| 6 | UGC Sandbox and Package Model | Candidate 3 | Capability-scoped user content; VM/language undecided | Candidate; future phase |
| 7 | Cross-Session Stable Identity and Asset Catalog | Spec 0015 | Persistent identity, schema migration, cross-scene asset-location registry; no consumer yet to design against | Candidate |

Spec 0015 delivered only the scene-local serialization slice; Candidate 7 keeps
the broader identity/catalog work visible. Android remains Candidate 1 and is
independent of the completed headless/image-regression milestones.
The maintainer's near-term Filament-quality presentation direction remains
recorded by [Spec 0025](0025-image-based-lighting-foundation.md); its initial IBL
milestone and subsequent sky/shadow/normal-map work have merged.
No new priority is assigned here. Future AI/UGC, neural, or GPU-driven workloads
must not shape Phase 1 abstractions ahead of an approved Spec.

### Backlog maintenance rules

- When a candidate is drafted, give it a Spec number and move its delivered
  scope into Section A. Retain any undelivered scope explicitly and correct
  candidate numbering/references without changing priorities or dependencies.
- Update statuses from the applicable Spec/Plan approval or implementation PR.
  Accepted ADRs and Approved Specs take precedence over this index. For a
  conflicting stale header, link clear review evidence with a short qualifier;
  escalate genuinely ambiguous states rather than inferring approval.
- Keep rows to navigation, coarse status, dependencies, and links. Put review
  discussions, corrections, implementation details, and verification evidence
  in their authoritative documents/PRs. Correct rows directly instead of
  appending status-history paragraphs.
