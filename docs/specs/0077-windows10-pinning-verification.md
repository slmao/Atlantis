# Spec: Independent Windows 10 Pinning Test Acceptance for PR #315

- **Status:** Approved
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-11-01
- **Related Plan(s):** [Plan 0077](../plans/0077-windows10-pinning-verification.md) (Approved)
- **Approval:** slmao, 2026-11-01; [joint approval evidence](https://github.com/slmao/Atlantis/pull/317#issuecomment-6555054368). Owner manually merged [PR #317](https://github.com/slmao/Atlantis/pull/317) as `dec11cd` and explicitly authorized S2–S4 in #315; existing ADRs retained.
- **Related ADR(s):** None new; see Architectural Impact.
- **Supplements:** [Spec/Plan 0075](../plans/0075-ecs-const-query.md) and [Spec/Plan 0076](../plans/0076-ecs-query-compatibility.md) for [Draft PR #315](https://github.com/slmao/Atlantis/pull/315) only.
- **Prior design basis:** [0072 R2](0072-mcp-acceptance-verification.md) and [0073 R1/R2](0073-windows10-pinning-acceptance.md), with approval evidence in [#309](https://github.com/slmao/Atlantis/pull/309#issuecomment-6539457400) / [#310](https://github.com/slmao/Atlantis/pull/310#issuecomment-6544807766). Their #307-specific authority does not transfer automatically.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Propose a single-translation-unit test correction for #315: distinguish optional
power-state readback from mandatory production pinning, adopting 0072's strict
capability classification and 0073's current-Windows-10-only acceptance scope
through this independent joint review. No #307 merge, code integration, prior
passing result or investigation authorization is required or inherited.

## Motivation / Problem Statement

The [retained #315 failure](https://github.com/slmao/Atlantis/pull/315#issuecomment-6554829759)
occurs after pinning setup and priority checks but before affinity checks, at an
unconditional power-state query. It records a zero return, not its last-error;
that historical attempt cannot now be classified as error87/unavailable. The
ECS name-isolation tests pass, but the original S2 verification remains blocked.

0072/0073 already reviewed a precise test-only correction for #307, including a
later Windows 10 acceptance narrowing. Their documents are present on main;
their native repair is absent from #315's test. A new supplement must explicitly
adopt the needed test scope and acceptance basis for #315, without importing
MCP work, observer fixes, environmental investigations or their budgets.

## Goals

- Verify setup, priority and exact affinity regardless of readback availability.
- Admit only the narrowly identified legacy readback limitation, with actual facts.
- Keep #315 independent of #307 and retain all ECS and formal acceptance gates.

## Non-Goals

No production pinning/Runtime consumer change, optimization, observer, sampling,
ABBA or performance investigation. No platform upgrade, environment intervention,
new dependency/test target, schema, SDK/protocol, baseline, tolerance, selector,
golden or repository-wide Windows support change. No repair or execution this round.

## Requirements

- **R1 — Independent single-file exception.** Extend 0075/0076's #315 test-file
  scope only to `tests/runtime/gate_process_gpu_tests.cpp`. Add test-private
  identity/classification helpers, adjust the existing live case and add pure
  classification cases there. No production `src/**`, public seam, CMake/link
  dependency or other native file change. Implement against the reviewed current
  base; do not merge/cherry-pick #307 or depend on its merge. Its prior repair
  may be read as design evidence, not used as implementation/verification authority.
- **R2 — Mandatory checks first.** Every applicable live pinning case must
  require unchanged production `pinCurrentProcess()` success, an empty setup
  error, `HIGH_PRIORITY_CLASS`, successful affinity/topology queries, a valid
  processor selected by the unchanged production selector, and an affinity mask
  equal to exactly that single processor and contained in the system mask.
  Reach these checks before optional readback can stop the case. Unavailable
  readback never excuses setup/priority/affinity failure or skips the whole case.
- **R3 — Exact capability classification.** Query the current process's
  `ProcessPowerThrottling` once with a zero-initialized current-version
  `PROCESS_POWER_THROTTLING_STATE` and its exact size. Preserve request facts
  before the call; on failure capture `GetLastError()` immediately, before any
  other Windows call/logging. Normalize the success error field to ERROR_SUCCESS;
  do not treat failed-query output masks as observed state. Classify only the
  readback subcheck:

  | Facts | Required result |
  |---|---|
  | Valid request, query succeeds on any version/identity | Observed only if the execution-speed ControlMask bit is set and StateMask bit clear; wrong bits fail, including on a legacy host. Unrelated bits are allowed. |
  | Valid request, query fails with ERROR_INVALID_PARAMETER (87), independently known NT desktop/workstation identity, major10/minor0, build10240–22620 inclusive | Explicit readback unavailable; all R2 checks must still pass. |
  | Malformed request, or failed query outside that exact exception | Failure, including unknown/probe-failed identity, server/unknown product, other errors and error87 on build22621+. |

  Obtain real major/minor/build/product/platform identity independently of
  manifest version emulation, using test-private dynamic `RtlGetVersion` lookup
  in already-loaded `ntdll.dll`; no new library, driver header or production
  abstraction. Probe failure stays unknown and cannot excuse a failed query.
  Always emit observed/unavailable/failure with OS/product, request version/size,
  success/error and meaningful mask facts, even if the enclosing case passes.
  No catch-all error87/unsupported rule, version-only skip or silent success.
- **R4 — Local Windows 10 acceptance.** For this #315 repair, choose the current
  Windows 10 reference host as the only required real-host acceptance platform,
  with actual identity recorded in fresh Debug and Release validation. No real
  Windows 11 run is a prerequisite; explicitly state **Windows 11 unverified**.
  This independently adopts 0073's local choice and does not edit 0072 R6 or
  change general platform support. R3's build range is a readback classification
  range, not a marketing-name or support claim: it also includes early Windows
  11 values, exercised synthetically only. Do not widen it or label synthetic
  supported/early-Windows-11 cases as real-host validation.

  Positively establish the current Windows 10 host identity for local acceptance;
  an unknown identity cannot establish that acceptance even if the query succeeds
  and its bits can be classified observed.

  A live test may pass its mandatory checks with an explicit unavailable
  subcheck; that is not successful readback, performance acceptance or universal
  Windows 10 coverage. A successful readback must be checked, not forced into
  unavailable. If fresh identity/error facts fail R3, retain failure and stop.
- **R5 — Fresh verification and evidence.** Preserve #315's old failures,
  skips, incomplete cases and archives byte-for-byte. Historical #307 logs may
  explain the proposal but cannot pass #315's changed test. Future validation
  must build the changed Runtime GPU test executable in Debug/Release and run
  the live case plus pure classifier cases on the current Windows 10 host with
  verbose diagnostics and frozen source/binary/config provenance. Cover valid
  success/wrong masks, legacy/supported boundaries, unknown/probe-failed/product
  identities, malformed requests and other errors; no OS query/GPU in the pure
  helper cases. Run relevant existing Runtime regressions after this test edit;
  GPU paths retain fatal-clean Vulkan Validation. A new verification failure or
  unexpected scope need is reported before expansion, without selective retries.
- **R6 — Retained gates and authority.** All unchanged 0075/0076 requirements,
  S2–S4 gates and Definition of Done remain binding: full Debug/Release suites,
  Android, World/Runtime CPU, remaining GPU/headless/image/Validation, actual
  full-PR applicability and formal performance acceptance. Expected whole #315
  **applies=true** remains; a test-only correction cannot exempt its World diff.
  Resolving this subtest does not close S2/S3/S4 or waive any formal non-pass.
  No inherited #307 result, measurement budget or stopped attempt may be used.
  This proposal schedules no investigation, fresh formal gate or observer work.
  Joint Spec+Plan approval, human merge and explicit follow-up implementation
  authorization are required before repair or resumption; keep #315 Draft for
  unresolved acceptance. No automatic ready/merge.

### Non-functional

C++20, existing Windows test guard/target and private value-only classification.
No timing assertion, new measured metric, persistent telemetry or shipped API.

## Proposed Design

Keep production pinning untouched. Move the existing affinity assertions ahead
of the optional readback, then isolate the single query's facts and the R3
classifier in the same test translation unit. Pure injected-value cases cover
both capability and failure branches; the live case owns real OS calls and
prints its evidence. Separate test-repair acceptance from full #315 acceptance.

## Architectural Impact

**None — no new or superseding ADR.** This is a feature-local verification-scope
exception and test-private OS inspection, retaining ADR-0118/0119's production
gate/baseline decisions and ADR-0139/0140's World access decisions. No module,
public API, ownership/threading model, dependency or protocol changes. The
0072/0073 precedents inform this new ruling; their approved bodies remain intact.
Any need for production/policy changes returns to separate Spec/ADR review.

## Alternatives Considered

- Wait for or merge #307: unnecessary source coupling and imports unrelated work.
- Treat the old #315 zero return as error87: missing evidence cannot be recovered.
- Skip the test, accept any failed query or remove affinity checks: loses R2/R3.
- Change production settings to enable readback: not evidence of setup failure
  and exceeds the test-only correction.
- Require real Windows 11 evidence here: outside the proposed local acceptance
  scope; synthetic branches remain tested without a Windows 11 success claim.

## Testing & Verification Plan

Plan 0077 maps R1–R6 to a three-document proposal check, future exact file scope,
fresh Debug/Release real-host/classification evidence and existing regressions.
All original #315 gates remain pending until actually completed. This round is
read-only code/evidence review plus documentation checks; no executable, build,
probe, GPU, observer, benchmark or gate is launched.

## Risks & Open Questions

Future actual query facts may not fit the legacy exception; the current failure
alone does not establish that they will. Version probing can fail; that remains
an explicit failure boundary. Windows 11 is unverified, and no claim is made for
every Windows 10 build. Even a correctly repaired test may leave independent
formal-performance or other acceptance blockers; no investigation follows.

## Out of Scope / Future Work

Production fixes, Runtime adoption, Windows 11 real-host validation, telemetry,
performance/environment diagnosis and any baseline action need separate scope
and applicable authorization. This round ends at joint-review submission.
