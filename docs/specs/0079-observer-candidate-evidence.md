# Spec: Temporary Observer Candidate Evidence Before Identity Assertions

- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-11-02
- **Related Plan(s):** [Plan 0079](../plans/0079-observer-candidate-evidence.md) (In Review)
- **Approval:** Pending joint human review.
- **Related ADR(s):** None new; see Architectural Impact.
- **Supplements:** [Spec 0078](0078-observer-environment-identity.md) /
  [Plan 0078](../plans/0078-observer-environment-identity.md), without changing its association rules.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Retain the temporary observer's already selected direct-child candidates and read
errors before identity assertions can discard them. Preserve strict association and
first-anomaly stops. This supplement authorizes no unknown-image classification,
exception or retry; a rejected candidate remains rejected even when its evidence is
successfully retained. This round submits documents only.

## Motivation / Problem Statement

The [latest Q1 report](https://github.com/slmao/Atlantis/pull/307#issuecomment-6560945690)
records `Owned child image mismatch`, no counter frame or gate verdict, and canceled
F1. The initial 0078 environment comparison succeeded, but `observe-gate.ps1` constructs
a candidate and calls `Assert-ChildIdentity` before saving it. A property-read failure
can occur even earlier. Neither the rejected identity nor its raw command line survived
in this packet, so the exact image and mismatch cause remain unknown. A conjectured
auxiliary process is not evidence and supplies no basis for a whitelist.

## Goals

- Preserve available candidate facts and partial errors at the actual failing boundary.
- Demonstrate that evidence publication precedes validation without changing its decisions.
- Define offline verification and a separately authorized bounded next execution.

## Non-Goals

- No association-rule, accepted-image set, path normalization, command/stage parsing or pinning change.
- No auxiliary-process allowlist, filtering of unknown images, new identity source or global process scan.
- No Runtime/SDK/protocol/build/CI dependency, permanent collector or benchmark-format change.
- No environment, policy, applicability, baseline, gate requirement, failure waiver or old-evidence rewrite.
- No implementation, new collection or measurement in this proposal round.

## Requirements

### Functional

**R1 — Persist available facts before reads and assertions.** Within the existing
known-gate/direct-child query only, retain the selected batch before reading individual
process handles. Include UTC as an exact string, monotonic tick/sequence, observation
mode, known gate PID/full creation time/expected image, and each returned raw
`ProcessId`, `ParentProcessId` and `CommandLine`. Retain query errors as errors, not
empty successful batches. An empty successful batch is not proof of six Runtime children.

For each candidate, retain the assembled PID/PPID, full creation-time string, original
image path and command line, available CPU/affinity/priority values and their source/
read status **before** calling the unchanged verifier. If handle acquisition or any
property read fails, retain the raw row, available partial values, failed read step
and exception/error facts before stopping. Distinguish unattempted, failed, missing
and returned-null values; do not invent a path/time or reread to fill a gap. Do not
substitute reconstructed arguments for the provider's command line.

Publish completed records atomically under unique session/batch/candidate names, with
hash-linked batch/read/decision references, no overwrites and no accepted outcome
before its source records exist. Preserve the original assertion rejection and its
record reference in a separate outcome/error record. Read/publication/serialization
failure is fatal even if saving its error also fails; retain whatever evidence is
available without claiming completeness. Dispose untransferred process handles on all
paths; do not kill or change the candidate to obtain readable data.

**R2 — Strict association remains unchanged.** Keep `association.ps1` byte-identical:
exact normalized full executable path, PID/PPID/full creation-time checks, command/
output/stage parsing, ordered distinct stage identities, pinning, lifecycle bounds and
output ownership remain mandatory. Preserve the existing counter/archive and 0078
environment/accounting checks. A candidate record is not an admission. A wrong,
unknown, missing or unreadable identity still stops immediately; no name/prefix
exception, exclusion of a nonmatching row, scan for a more convenient child, or
continuation after rejection. New classification semantics require another review.

The first rejection ends observation; retaining the raw returned batch does not
authorize reading subsequent candidates after it. Preserve any original error even
if outcome publication also fails. A successfully retained rejection proves only
that evidence survived; it does not verify six children, authorize F1 or pass M5.

**R3 — Closed collection and cost boundary.** Add no query, provider, process-tree
scope, sampling frequency, live helper probe or tracing facility. Publication is tied
to the existing query/read/assertion passes, rather than an additional polling loop.
Q1 keeps the nine counters at no more than1Hz, initial/20-second environment checks
and CPU0/normal constraints; F1 gains no periodic candidate collection. Account for
the new logical writes/hash reads and CPU work using the existing evidence publisher
and accounting, retaining their provider/kernel/physical-IO/tail limitations. No new
interference threshold, subtraction or total-overhead bound is introduced. The
controller must not qualify Q1 with missing or inconsistent candidate evidence.

**R4 — Offline tests before any live authority.** Use supplied batches, fake process
readers/disposable handles, injected publishers and the actual unchanged verifier.
Guard Runtime launches, live process/CIM/counter/power reads as forbidden. Test raw
publication before the first read and assembled publication before validation;
correct identity, exact path mismatch, bad parent/time/PID/stage/command, missing/
null values, acquisition/property-read errors and PID reuse under existing rules.
Inject storage failures at batch/read/outcome boundaries and verify refusal with no
ready result, no second candidate read after a rejection and proper handle disposal.
Keep full creation-time and command quoting through JSON round trips. Two invocations
must use separate output directories and leave the first outputs hash-identical.
Retain the existing 0078 environment, association/output/counter/archive regressions
and every failed offline iteration. Synthetic facts establish no live association.

**R5 — Separate execution authority and retained acceptance.** Joint approval and
human merge precede a distinctly authorized S2 offline repair; recommend a new
**45-minute** budget including tests/reporting. Offline success is a hold point.
Any subsequent live work requires another explicit authority and new **150-minute**
budget, retaining 0078 R5's **40/15/35/2/35/23-minute** caps, complete input/evidence
reuse checks, actual-source provenance, immutable registration and quiet periods.
Freeze these new record rules/scripts with all native/content/policy/tool inputs
before the first launch; changed native inputs require matching verification or stop.

The fixed schedule remains **Q1→F1, at most two gate parents**, unchanged formal gate
defaults/settings/store/timeouts. A normal comparable/conformant pass **or fail**
continues only with complete association and candidate/environment/counter/archive/
accounting evidence. Every other anomaly, drift, missing input, read/storage/identity
failure, validation diagnostic or phase/total-budget exhaustion stops with F1 canceled.
No in-session repair, retry, replacement, third gate, ABBA, helper/idle preflight or
budget transfer. A future wrong-image stop with good records may complete this narrow
negative-evidence check, but remains an incomplete formal acceptance.

All previous budgets stay closed and every old result remains intact. Whole-PR
`applies=true`, M5/0071 S5 and prior-failure disposition remain required; no observer
repair or later pass alone disposes of the original M5 fail. Keep current Windows10
scope and mandatory checks under 0073, Windows11 unverified, and #307 Draft while
acceptance remains unresolved. No automatic readiness or merge follows this proposal.

### Non-functional

Disposable ignored evidence only, with versioned local record rules and no migration
of old packets. Reuse the 0078 publisher/normalizer/assemblies without altering their
semantics. No native/public API, official schema, timing assertion or new dependency.

## Proposed Design

Insert evidence publication around the existing selected-batch/read/verification
boundary, sharing a small pure wrapper between injected fixtures and the sampler.
Only evidence publication/order, partial-read handling and referenced-record auditing
change. The original verifier and admission decisions remain authoritative; the
controller treats incomplete new evidence as a stop. The Plan names the file boundary
and separate authorization gates; no candidate implementation is part of this Spec.

## Architectural Impact

**None — no new or superseding ADR.** This is a disposable observer evidence repair,
not a native module/API, dependency, threading/ownership model or protocol/gate decision.
ADR-0118/0119/0130/0138 and approved 0070–0078 requirements remain unchanged. A future
classification exception, permanent collector or performance-contract change returns
to separately scoped review, with ADR impact assessed then.

## Alternatives Considered

- Ignore a suspected auxiliary image: its identity is unknown and this weakens strict association.
- Change the verifier or reconstruct the lost child: exceeds the evidence and disguises the gap.
- Save only after the first successful counter frame: repeats the pre-frame loss boundary.
- Enumerate globally, add a live probe or retry the stopped attempt: widens scope or violates the stop.

## Testing & Verification Plan

This proposal uses documentary link/whitespace/scope and old-packet integrity checks
only. Later R1–R4 verification uses injected readers/publishers and actual helper/audit
code; R5 requires explicit authority, a frozen registration and literal live outcomes,
including successful retention of a rejection without accepting it. No native/GPU or
performance result is claimed here. All later applicable correctness/Vulkan/formal
gates retain their obligations under the existing
[testing strategy](../process/testing-strategy.md).

## Risks & Open Questions

The rejected image remains unknown; evidence retention may expose another stop rather
than enable measurement. More record IO adds disturbance; finite cadence/budgets do
not establish a total bound. A process can disappear or become unreadable; preserve
the partial facts and stop rather than repair the record through another query.
Joint review must approve the retention/order scope, unchanged rejection semantics,
offline tests and separate bounded launch conditions, not an imagined root cause.

## Out of Scope / Future Work

Classifying a newly identified image, changing selection/association, physical
performance diagnosis and any environment/baseline action remain separate review
items. No such work is implied by successful retention or this proposal's approval.
