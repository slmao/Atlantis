# ADR 0118: Performance as a Gated Contract

- **Status:** Accepted
- **Date:** 2026-10-11 (accepted 2026-10-11)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-11 (chat rulings on Spec 0060, including keeping this ADR
  and its pair separate; review of Spec 0060's own branch PR,
  [PR #246](https://github.com/slmao/Atlantis/pull/246)).
- **Related Spec:** [Spec 0060](../specs/0060-performance-contract.md) (`Approved`)
- **Related ADR(s):**
  - [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md):
    the precedent — committed goldens, a read-only comparison, categorized update
    reasons, a local/manual gate — followed, not changed.
  - [ADR-0117](0117-benchmark-host-json-schema-and-baseline-comparison.md): the
    benchmark host and its never-failing `--baseline` comparison — unchanged.
  - Paired with [ADR-0119](0119-benchmark-gate-protocol-and-verdict.md) (the
    protocol, documents and command surface).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- Spec 0059 made performance measurable (`atlantis_runtime --benchmark`) and kept
  the result out of the repository (Plan 0059 J11: hardware-specific, ad hoc) and
  out of every failing path (Spec 0059 R11 and R13). A later PR therefore has no
  committed reference and no verdict.
- Image output already has the opposite arrangement: committed goldens, a
  categorized reason for every update, and a gate that whoever opens the PR runs
  and reports (ADR-0042; Definition of Done; no CI exists).
- Performance differs from pixels in one way that shapes everything below: it is
  noisy. Run-to-run variation on the reference machine ranges from about 1 % (CPU
  metrics in steady state) to above 10 % (GPU metrics across sessions), while
  counters are exact (Spec 0060, Motivation).
- The first optimization Spec is to be validated against a stated contract, and
  coding agents need a one-command, machine-readable answer to "did this PR make
  the frame slower?".

## Decision

1. **The Performance Contract** is the combination of: a repeatable-measurement
   protocol and gate (ADR-0119), a **committed baseline store**, a categorized
   **refresh rule**, an **applicability policy**, and Definition-of-Done and
   AGENTS.md items that make running the gate part of finishing a PR it applies to.
2. **Baselines are committed artifacts**, under `tests/performance/baselines/
   <profile-id>/`, scoped to a hardware profile (ADR-0119 D4 defines the identity).
   This supersedes Plan 0059 J11 **for the store only**; ad-hoc benchmark JSON
   (PR evidence, captures) stays uncommitted. The gate and every other tool only
   read the store.
3. **Baseline refresh is a reviewed, categorized act**, mirroring ADR-0042. A PR
   that adds or changes a file under `tests/performance/baselines/` is a dedicated
   PR (never bundled into an unrelated change), is reviewed by a human, and states
   one of: (1) **performance change** — an intentional change to the frame cost,
   naming it, with an explicit justification when the change is a regression;
   (2) **reference-environment change** — driver, OS, CPU or GPU changed and the
   stored baseline no longer matches though no frame code changed; the PR carries
   old and new provenance and evidence that no regression hides behind the
   environment change; (3) **approved rebaseline** — a deliberate human decision for
   another stated reason, with the same reasoning as (2); (4) **initial baseline**
   for a hardware profile that has none. A reference-environment change is never
   described with the language of a performance change. Tooling never regenerates
   the store automatically, and an agent never refreshes a baseline without an
   explicit human instruction.
4. **The gate is a local/manual, opt-in check, not a CTest and not CI.** It is run
   and reported by whoever opens the PR, like image regression. The default
   `--baseline` comparison keeps never failing (Spec 0059 R11); failure exists only
   in the separate gate mode (ADR-0119). Nothing in CTest asserts a measured time
   (Spec 0059 R13); the gate's logic is unit-tested on synthetic documents.
5. **Applicability.** The paths whose changes require the gate are a committed data
   file, `tests/performance/applicability.txt` (frame path, renderer, render graph,
   backend, RHI, runtime, World, shaders, profiling code, build-flag files, and the
   performance tree itself); docs-only, Android-only and tooling-client changes are
   exempt. The file is the single source of truth for the Definition of Done, the PR
   template and the gate's `--applies` helper. No CI enforces it.
6. **The contract is stated in the rules.** `docs/process/definition-of-done.md`
   gains a Testing item ("performance gate run and its verdict recorded, or N/A with
   the reason, for a PR the applicability file selects"); `AGENTS.md` Testing
   requirements state the contract and the agent rule of D3; the PR template gains
   the matching line; `docs/process/performance-contract.md` is the operator note.
7. **A gate verdict is evidence, not authority.** A `fail` or `inconclusive` is
   reported in the PR with its verdict document; the merge decision remains a
   human's, as for goldens.

## Consequences

### Positive

- Performance regressions become reviewable by diff and checkable by a coding
  agent with one command, the way image regressions are.
- The first optimization Spec has a committed reference and a stated resolution to
  be validated against.
- The refresh rule keeps "the baseline moved" visible and justified, not silent.

### Negative / Trade-offs

- A new class of committed, hardware-specific files and a review burden per driver
  or hardware change (the profile identity includes the driver).
- A local gate depends on people running it; the applicability file and the PR
  template are the only enforcement until a CI exists.
- Repository process changes (Definition of Done, AGENTS.md, PR template) that every
  later PR touching the listed paths must follow.
- The contract states a per-profile minimum detectable regression; on noisy
  hardware the GPU tier resolves only large regressions (ADR-0119 D3).

## Alternatives Considered

- **Keep benchmark results out of the repository (J11 as is).** Leaves no committed
  reference; every comparison needs a file from somewhere.
- **A baseline database or artifact store outside the repository.** Loses
  review-by-diff and commit provenance; there is no CI to host one.
- **A CI performance check.** Out of scope: no CI exists; the gate's exit code and
  verdict are designed so a CI could later call it unchanged.
- **Make the default comparison fail.** Contradicts Spec 0059 R11 and the owner's
  requirement of an explicit opt-in failing mode.
- **Enforce applicability by tooling that blocks a PR.** Needs CI; the data file and
  helper give the machine-readable answer without it.
- **Allow per-metric waivers instead of baseline refresh for accepted regressions.**
  One mechanism (a category-1 refresh) is reviewed by diff; waivers accumulate and
  rot.
