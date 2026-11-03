# Spec: Temporary Observer Environment Evidence and Identity Boundaries

- **Status:** Approved
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-11-01
- **Related Plan(s):** [Plan 0078](../plans/0078-observer-environment-identity.md) (Approved)
- **Approval:** slmao, 2026-11-01, J1–J4 as recommended; [PR #318](https://github.com/slmao/Atlantis/pull/318) human-merged. Joint authorization is recorded in Plan 0078.
- **Related ADR(s):** None new; see Architectural Impact.
- **Supplements:** [0072](0072-mcp-acceptance-verification.md),
  [0073](0073-windows10-pinning-acceptance.md) and the bounded formal-execution
  registrations in [PR #307](https://github.com/slmao/Atlantis/pull/307).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Repair only the disposable observer's environment evidence and comparison boundary.
Separate machine identity from process-local processor availability and independently
enforce the already approved affinity constraints. Require offline verification and
a fresh explicit bounded authorization before any further live execution. This round
submits a proposal only; all prior stopped executions remain closed.

## Motivation / Problem Statement

The [latest Q1 report](https://github.com/slmao/Atlantis/pull/307#issuecomment-6557483792)
records an immediate `Registered environment changed` exception, zero counter samples,
no archived child documents and no gate verdict. F1 was not launched. The observer
did not persist the failing environment object, so the particular changed field and
whether it represented real drift cannot be recovered from this packet.

The frozen `environment.ps1` assigns `Environment.ProcessorCount` to the environment's
`logicalProcessors` field. The controller starts the observer after confining itself
to CPU0; the observer also enforces CPU0 before reading that field. Microsoft's
[API contract](https://learn.microsoft.com/en-us/dotnet/api/system.environment.processorcount?view=net-10.0)
defines process-available capacity, affected by startup affinity/CPU limits and fixed
for that process lifetime. It is unsuitable as independent machine topology.
This is a concrete design defect and a candidate explanation, **not proof of the
unrecorded Q1 field difference**. Independently returned device/property ordering
is another comparison boundary; no historical mismatch is invented for it.

## Goals

- Preserve enough evidence to distinguish a comparison defect, real drift and an unreadable state.
- Keep hardware identity and mandatory process-affinity checks independently meaningful.
- Bound offline repair and any separately authorized next formal execution.

## Non-Goals

- No Runtime, SDK/protocol, production pinning, build/CI, permanent telemetry or dependency change.
- No new live helper probe, idle observation, ABBA, independent benchmark, canary or environment investigation.
- No OS/power/driver/device setting, gate, applicability, baseline, tolerance or approval change.
- No reconstruction of missing historical snapshots, retry in a closed budget, performance waiver or automatic merge.

## Requirements

### Functional

**R1 — Persist before deciding.** Every scheduled environment check must first retain
its selected provider results, partial-read/error facts, UTC string and monotonic tick,
caller role/PID/full creation time, actual affinity/priority, process-available count,
frozen-reference hash and comparison-rule version. Preserve the frozen reference,
observed projection and field-level difference/error report with linked hashes before
accepting equality or raising a mismatch. Use immutable per-session sequence names
and atomic completed-file publication; never overwrite a previous check or archive.
Unreadable/partial required input, duplicate identity, malformed JSON or failed
evidence publication stops readiness/execution. Preserve any partial evidence and
minimal error when possible; failed storage is not a successful check. Acquisition
failure still stops even if writing the error also fails.

**R2 — Explicit comparison domains.** Use the following closed projection, not whole
object JSON-text equality. Preserve original selected values alongside the projection.

| Domain | Comparison and failure rule |
|---|---|
| Machine identity/configuration | Compare OS caption/version/build; all CPU rows keyed by `DeviceID`, with name, ProcessorId, core/logical counts; all GPU rows keyed by `PNPDeviceID`, with name, driver, display extent/refresh and device error state; AC and configured power scheme. Obtain counts from the existing CIM CPU query, retaining per-device values and their validated sum. No process-local count substitutes for this topology. |
| Representation | Fix property order and sort device rows by exact unique key. Integer-width differences may normalize only when the exact valid integral value is preserved. Keep strings exact; do not trim away identity changes, coerce numeric strings, round, clamp or discard fields. Missing and null differ. Registered null display fields on an inactive GPU remain explicit values; they do not excuse a missing device/field or read failure. Reject empty/duplicate keys, invalid counts and unknown projection fields/schema. |
| Process context | Record `Environment.ProcessorCount` as process-local diagnostic data, not hardware equality, an affinity substitute or a required value of 1. Record host/runtime identity and `DOTNET_PROCESSOR_COUNT` presence/value in the frozen launch inputs; changing those inputs is a stop. Do not set an override to manufacture equality. |
| Process enforcement | Independently require the observer/controller to have CPU0 mask 1 and normal priority during observation/archive work. Preserve the controller's explicit full-mask launch transition, gate parent initial mask 255, and the existing CPU7 mask 128/high-priority child checks. A correct hardware projection never excuses a wrong/unreadable mask, priority, PID, image or creation time. Do not derive a mask from the process-available count or change production pinning. |

R2 is limited to the current registered eight-logical-processor Windows 10 host.
New topology, multi-group requirements or an unknown identity stop for review; this
is not a general hardware-discovery framework. Hardware/configuration differences
remain hard stops. Expected process-local count differences do not waive those stops.

**R3 — Same observation limits and honest cost.** Reuse the existing selected CIM
classes/queries and process handles; adding the specified key/status fields to the
same query does not authorize a wider inventory. Both controller and sampler use
the same projection. Q1 keeps the original nine counters at no more than 1 Hz and
the sampler's initial/20-second environment checks plus controller before/after checks.
Do not add per-counter-frame environment snapshots, new providers, global process
enumeration or extra checks to seek equality. F1 keeps only before/after environment
checks and the minimum event archiver: no periodic sampler, Get-Counter, CIM or
process polling. Count snapshot/archival work in CPU/IO accounting. On all exit
paths attempt a final observer CPU/time/affinity/error record and retained-handle
cleanup; missing accounting remains an explicit gap, never a zero-overhead claim.
No new interference percentage threshold, deduction or total-overhead guarantee.

**R4 — Offline gate before live authority.** In new ignored output, implement and
test pure projection/comparison, serialization, failure publication and controller/
sampler integration with injected inputs only. Keep every earlier fixture and error.
Test identical hardware with differing process-available counts, independently wrong
affinity, harmless property/device order, exact numeric-width equality, real hardware/
power/display/driver differences, missing/null/duplicate/invalid values, full timestamp
round trips, query/write failures and two isolated fixture invocations. Retain the
existing process/output/archive/counter audit regressions. Stub live queries/process
launches to fail if invoked; fixtures must not launch Runtime or read live counters,
environment inventory or process trees. A supplied affinity is synthetic, not a real
Windows enforcement result. Offline success cannot establish Q1 readiness, child
association, idle compliance, cost bounds or acceptance.

**R5 — Separate authority and bounded next execution.** Joint approval and human
merge are prerequisites for implementing this supplement, with distinct authorization
for the offline S2 repair (recommended cap: **45 minutes including verification/report**).
That authority alone grants no live launch. After S2 passes, request explicit authority
for a new **150-minute** execution, with **40/15/35/2/35/23-minute** caps for preparation,
registration, Q1, quiet interval, F1 and reporting. No borrowing, resetting or reuse of
old unlaunched attempts. Before Q1 register source/base, clean build provenance,
binary/content/dependency/tool/host hashes, complete input/script manifest, projection
rules, commands, output paths, observer mode/cadence, limits and all known gaps in #307.
Reuse the frozen `9a363fa` native build only if its provenance and all relevant inputs
still match; distinguish its actual source revision from any later docs-only PR head.
Never relabel an old binary. Native/build/content input changes require a fresh clean
matching build and affected correctness checks within preparation, or stop for review.

The registered order remains **Q1 then F1, at most two gate parents**, same frozen
Runtime and unchanged `--gate --json --output <unique path>`. Preserve the approved
Bistro/reference settings, committed store, idle/mains/pinning, preheat/run counts and
timeouts. Q1 must verify real parent/direct-child PID, full creation time/image/PPID,
stage/output/lifetime association and complete archival. F1's lack of detailed child
sampling and continuous environment visibility remains disclosed. Require at least
60 seconds quiet after final registration and between calls, with no concurrent builds,
tests, diagnostic probes, hashing, compression or reporting during acquisition.

A normal comparable/conformant Q1 pass **or fail** permits F1 only when its registered
identity, collection, input/environment and archival checks are complete. Any refusal,
unknown/incomparable/inconclusive result, actual drift, query/identity/collection/storage
failure, dirty/mismatched input, GPU validation diagnostic, scope expansion or phase/
total-budget exhaustion stops further launches immediately. No in-session fix/retry,
replacement, third gate or ABBA. Terminate only positively identified owned processes,
preserve actual exits and gaps, and cancel F1 when Q1 cannot qualify. Failure to obtain
diagnostics is not permission to repeat. A normal performance fail is retained and is
not itself permission to depart from the fixed schedule.

**R6 — Evidence and acceptance remain independent.** Preserve all original packets,
scripts, raw partial outputs, attempted invocations, failures and stopped budgets;
never rewrite a prior snapshot to the new projection. Results and hash manifests belong
in #307/ignored output, not a committed measurement store. Keep the whole-PR
`applies=true` and formal M5/0071 S5 obligation, the original M5 fail and every earlier
result. Neither a repaired observer nor a later pass disposes of prior failures by
itself. Retain approved 0073's current-Windows-10 scope, mandatory checks and Windows 11
unverified status. Keep #307 Draft while any applicable verification/failure disposition
is unresolved; no automatic ready transition or merge.

### Non-functional

Only disposable external PowerShell helpers/tests and evidence bookkeeping are affected.
No permanent data format, new native/public seam or timing assertion is introduced.
Projection semantics are versioned in the frozen local evidence, with no migration of
older measurements. No system setting or inherited environment override is changed.

## Proposed Design

One selected-source read yields machine values, process context and explicit read-status
facts. A pure, closed projection and comparator are shared by controller and sampler.
Publication precedes the readiness decision. Distinct rule checks enforce process
identity/affinity independently. Offline injected fixtures validate these boundaries;
any real validation is reserved for R5's separately authorized Q1. The Plan identifies
the exact disposable files and review gates, without adding engine instrumentation.

## Architectural Impact

**None — no new or superseding ADR.** The proposal corrects disposable evidence handling
and the meaning of an observer field, without changing engine modules/APIs, ownership,
threading, dependencies, benchmark/protocol schemas or the formal performance contract.
ADR-0118/0119/0130/0138 and approved 0070–0073 bodies remain unchanged. A permanent
collector, different performance requirement or native fix needs separate Spec/ADR review.

## Alternatives Considered

- Remove the environment stop or ignore all counts: would hide real topology/configuration drift.
- Force an environment override or full affinity on the observer: changes the registered environment or isolation constraint to make comparison pass.
- Compare serialized objects or accept any missing field: retains representation sensitivity or hides failed collection.
- Diagnose Q1 as a proved false alarm or retry it in the old budget: unsupported by missing evidence and violates the closed stop.
- Add a separate live helper preflight: expands the acquisition scope; injected offline checks followed by the bounded Q1 are sufficient for this proposal.

## Testing & Verification Plan

R1–R4 use injected offline fixtures and explicit no-live-call guards. R5 requires a
published immutable registration and, only after distinct authority, the bounded
two-call protocol with complete actual outcomes/association/cost/gaps. R6 requires
protected-path and old-packet integrity checks and a truthful Draft/remaining-holds
report. This docs-only review runs local-link/whitespace/scope checks, not builds,
GPU work or measurements. Existing correctness evidence remains subject to input
validity; this proposal does not waive the repository's later applicable gates.

## Risks & Open Questions

Q1's missing observed state remains unrecoverable. CIM can fail or return ambiguous
identities; the response is a documented stop. Serialization, added evidence IO,
short-lived process capture and provider/kernel interference may still block Q1;
offline success is not a claim of live correctness. Joint review must rule on the
comparison domain, minimal snapshot/cost scope, offline boundary and future launch
conditions. Authorization for any execution remains a separate decision.

## Out of Scope / Future Work

Locating the historical physical performance shift, environment remediation, baseline
action and broader tracing remain outside this supplement. A future bounded stop
with uncertainty is a valid report, not completed performance acceptance.
