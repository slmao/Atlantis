# Spec: Close Scene Codec Validation Gaps F1, F3 and F4

- **Status:** In Review; joint approval recorded, human merge pending
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-11-01
- **Related Plan(s):** [Plan 0074](../plans/0074-scene-codec-validation-gaps.md) (joint approval recorded; human merge pending)
- **Approval:** slmao, 2026-11-01; [PR #311 approval evidence](https://github.com/slmao/Atlantis/pull/311#issuecomment-6546350130). Human merge pending.
- **Related ADR(s):** ADR-0052/0053, ADR-0097 and ADR-0100 retained; see Architectural Impact.
- **Supplements:** [Plan 0049 findings and J1/J2](../plans/0049-schema-driven-authoring-scene.md#joint-review-decisions--ruled-joint-human-review-slmao-2026-10-05-pr-203).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Close three deliberately deferred serializer validation gaps: artifact component exclusivity
(F1), active mesh/material key zero (F3), and the cooker's missing artifact-capacity guard (F4).
This is an additive proposal from `origin/main`, independent of PR #307. The current round
ends at joint-review submission; no fix or performance investigation is authorized here.

## Motivation / Problem Statement

Plan 0049 J1 recorded F1/F3 as KnownGap conformance cases, leaving the decoder unchanged;
J2 classified F4 as a projection precondition outside semantic-schema fingerprinting. Static
review at `447b03f` still finds the accepting decoder cases and a cooker that can encode a
source with more nodes than the decoder's existing limit. The reviewing PR owns immutable
code-line evidence and the limits of this read-only review.

The resulting asymmetry allows otherwise invalid artifacts past `decodeSceneArtifact()`
and then `decodeScene()`'s sole `ValidatedSceneData` construction boundary, or allows a cook
to succeed with an artifact its own decoder refuses. No new scene feature is needed to fix it.

## Goals

- Enforce the existing exclusivity and non-zero active-reference contracts at artifact decode.
- Refuse an unrepresentable node count consistently through every existing cook entry point.
- Preserve valid outputs and existing architectural/version contracts through an explicit review.

## Non-Goals

- No new scene grammar, component/domain rule, serializer, catalog lookup or GUID/key algorithm.
- No source-parser streaming/resource-limit redesign, generic validator or codec/schema rewrite.
- No F2/F5/F6/F7 work: F5 is already handled by Spec 0065's reviewed corrections.
- No World/Runtime/Renderer, SDK/wire, dependency, build/CI, golden or performance-policy change.
- No rewriting approved Specs/Plans/Accepted ADRs, automatic implementation or merge.

## Requirements

### Functional

- **R1 — F1 artifact exclusivity.** Reject a decoded node carrying more than one of Camera,
  Renderable and Light with a new `SceneArtifactDecodeError::ConflictingComponents` outcome.
  Any nonzero presence flag retains its current meaning of present; do not impose a new
  flags-must-be-0-or-1 rule. All three pairs and the triple are rejected; none or one remains
  valid when the other existing checks pass. `decodeScene(CookedScene)` and its file overload
  must propagate refusal without constructing `ValidatedSceneData`.
- **R2 — F3 active reference domain.** A present Renderable must have nonzero mesh AssetId;
  a present material must have nonzero material AssetId. Reject either zero with new
  `SceneArtifactDecodeError::ZeroAssetReference`. Keep absent component/material slots ignored
  as today, including their zero values. Preserve `MaterialWithoutRenderable` for that existing
  invalid combination. This is a local value check, never an asset-existence, type, GUID or
  catalog-resolution check. Nonzero keys remain subject to later resolution under existing ADRs;
  this change does not prove they exist or change catalog zero/collision ownership.
- **R3 — F4 cooker capacity.** In the shared `cookParsedSceneSource()` path, immediately after
  the existing empty-scene refusal, reject `parsed.nodes.size() > kMaxSceneArtifactNodeCount`
  with new `SceneCookError::NodeCountOutOfRange`. Compare the full container size before any
  narrowing, derived validation-container allocation/sort, index construction or encoding.
  Reuse the existing inclusive limit **65536**; do not create a second limit. Preserve
  `EmptyScene` for zero and existing checks for representable inputs. Cover `cookSceneSource`,
  `cookSceneToMemory` and file `cookScene`; count refusal reaches the caller before artifact or
  sidecar writes, leaving existing outputs untouched. Parsing/mapping may already have allocated
  the input document: this guard does not claim a pre-parse memory bound.
- **R4 — Error and ordering contract.** Append the three named enumerators to their existing
  enums without renumbering existing values, changing result signatures or serializing new
  error codes into another interface. Add the cooker's existing CLI message-table entry.
  Preserve all existing `decodeSceneArtifact()` checks and their relative order; run one final declaration-order
  node-validation pass after them and before decode success, checking R1 then R2 at each node.
  Thus old artifact errors retain precedence; public-loader I/O and artifact-before-metadata-parse
  ordering remain unchanged. If only these new artifact defects occur,
  the earliest node determines the error and exclusivity wins over zero reference at that node.
  The R3 capacity guard precedes later cook-time semantic/reference errors for oversized inputs.
- **R5 — Compatibility and scope.** Advance the existing independent cooker behavior marker
  `kSceneCookerVersion` from `atlantis-scene-cooker/1` to `atlantis-scene-cooker/2` for the changed
  count-refusal behavior; use ADR-0052's existing mechanism, adding no serialized field.
  Keep source/artifact format versions **7**, record layout,
  metadata grammar, scene semantic version **1**, descriptors and fingerprint unchanged, subject
  to explicit J3 review. R1 enforces the existing schema exclusivity group; R2 enforces the
  existing reserved-zero projection contract; R3 enforces format capacity, not a new semantic
  constraint. Valid committed/imported corpus source, artifact and metadata bytes remain identical.
  Previously accepted malformed artifacts and oversized cooks intentionally change to refusal;
  do not claim universal behavioral compatibility. Do not widen this patch to rare hash-zero
  GUID cooking, global catalog policy or other previously deferred validation questions.

### Non-functional

Implementation remains Asset System-private validation, plus its public error enumerators and
the existing Tools diagnostic. No new dependency, public validation service or module coupling.
The final decode pass is O(n), bounded by the already-checked artifact count; no timing verdict
is added to CTest. Tests generate boundary inputs in memory/scratch, not committed large fixtures.

## Proposed Design

Add the final decode validation pass and the shared early cook guard described by R1–R4.
Convert the F1/F3 conformance KnownGap acceptances into explicit expected refusals, retaining
the existing source-side cases and coverage registries. Exercise both low-level artifact decode
and public `decodeScene` with otherwise consistent metadata. No decoder body is duplicated in
the high-level loader, and `ValidatedSceneData`'s constructor/ownership boundary stays intact.

## Architectural Impact

**None proposed — no new or superseding ADR.** The additive enum outcomes are an explicitly
reviewed public API extension, not a new API shape or subsystem. Existing error numeric values
and module boundaries stay fixed. [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)
retains the independent cooker behavior marker; [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
retains decode-time independent validation; [ADR-0097 D2](../adr/0097-guid-keyed-asset-and-entity-identity.md)
retains reserved-zero/catalog ownership; [ADR-0100 D4–D6](../adr/0100-scene-semantic-schema-layering-model-and-versioning.md)
retains normative schema/conformance and semantic versioning. J3 must confirm that closing
these recorded enforcement gaps adds/tightens no normative constraint. If review instead
requires a semantic bump under the existing ADR, revise the proposed versions, files and
verification before implementation. A changed architectural/versioning decision additionally
needs its superseding ADR; do not silently reinterpret or edit Accepted ADR-0100.

## Alternatives Considered

- Retain KnownGap acceptance: leaves the reviewed boundary inconsistency unresolved.
- Reuse `NonFiniteValue`/`SourceParseFailed`: obscures structural or capacity refusal.
- Reject all zero slots or canonicalize flags: expands the accepted-value contract unnecessarily.
- Add catalog lookup or schema-driven codec refactoring: exceeds a local validation repair.
- Bump versions without a new meaning/layout: not recommended for existing-rule enforcement;
  the competing interpretation is explicitly put to joint review rather than assumed approved.

## Testing & Verification Plan

Plan 0074 maps the requirements to targeted rejection/positive-control tests, public-loader
propagation, cook boundary and no-write checks, strict conformance coverage, existing schema
fingerprint/version pins and valid corpus byte identity. Future implementation builds and tests
Debug/Release, including relevant Asset System/Tools and downstream GPU-independent regressions.
No rendered-output change is expected. Current proposal verification is documentation/static
review only: no new reproducer, engine build, GPU invocation or performance measurement.

## Risks & Open Questions

New error cases affect native exhaustive callers, and malformed v7 artifacts formerly accepted
will be refused; review must approve that compatibility tightening. Boundary tests need distinct
node IDs and GUIDs so duplicate identity does not mask the count case. Counts can still consume
memory during parse/mapping; input streaming is excluded. A non-nil GUID is not by itself a proof
that its projected hash key is nonzero; catalog checks remain separate. Version/ADR disposition
is unresolved until J3, not inferred from this draft.

## Out of Scope / Future Work

Parser resource limits, broader cook validation, schema/domain additions and any new serializer
need separate review. No dependency on #307 or its merge, and no performance investigation.
