# ADR 0095: Goldens over Pinned, Fetched Content

- **Status:** Proposed
- **Date:** 2026-09-25
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0046: Bistro Finale](../specs/0046-bistro-finale.md) (`Draft`)
- **Related ADR(s):** extends [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  (the golden workflow; its capture, tolerance and evidence rules are
  unchanged).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- Every golden so far renders committed assets only. The one GPU test over
  Bistro deliberately is not a golden: "the frame depends on content fetched
  from an upstream repository, which is not a reference environment
  ADR-0042 goldens may rest on" (`bistro_large_mesh_gpu_tests.cpp:10-12`).
- Spec 0036 ⑦'s verification contract requires a Bistro golden (ADR-0042
  Initial baseline).
- The Bistro fetch is not "whatever upstream has": `fetch_bistro.ps1` pins
  a commit and, per file, a Git LFS oid (the file's SHA-256) and byte size,
  and keeps no file that does not verify. The content cannot be committed
  (~2.1 GB; Spec 0037's content policy).
- Tests over that content already exist and SKIP when it is absent
  (`[bistro]`, the `content` label).

## Decision

A golden may render fetched, uncommitted content when all of these hold:

1. **Pinned input.** Every fetched file the frame depends on is pinned by
   SHA-256 and size in a committed fetch script, and the fetch refuses any
   file that does not verify.
2. **Recorded pin.** The golden's sidecar records the content pin (the
   fetch script's source commit) beside ADR-0042's existing provenance
   fields; the golden test checks it against the fetch script.
3. **Content-gated.** Its capture-compare and discriminator TEST_CASEs SKIP
   — not pass, not fail — when the content is absent, under the existing
   `content` label, and say so.
4. **Otherwise ADR-0042 unchanged:** zero tolerance, captured by a
   generator on a clean committed tree, the four Initial-baseline evidence
   items, discriminators that must fail.

The `bistro_large_mesh` comment's premise is superseded by this ADR; that
test stays a non-degeneracy check (it is not a golden and need not become
one).

## Consequences

### Positive

- Spec 0036 ⑦'s contract can be met with the same rigour as every other
  golden, on the real scene.
- The rule is general but narrow: pinned per file, recorded, gated.

### Negative / Trade-offs

- The Bistro golden runs only where the content was fetched; elsewhere it
  SKIPs and guards nothing. The PR records which machines ran it.
- An upstream re-pin (a new commit or file) requires a re-capture, as a
  driver change already does.

## Alternatives Considered

- **No Bistro golden** — a non-degeneracy check only (the
  `bistro_large_mesh` precedent). Fails the roadmap's contract row, and a
  2909-draw assembly would have no regression guard. Rejected.
- **Commit a reduced Bistro subset** as test assets. The subset would not be
  the scene under review, and it still carries tens of MB of third-party
  textures into git. Rejected.
- **A tolerance-based golden** to absorb content drift. The content does not
  drift (it is pinned), and ADR-0042's zero tolerance is not reopened.
  Rejected.
