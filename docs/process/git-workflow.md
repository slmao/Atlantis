# Git Workflow

## Branch model

- `main` is protected: no direct pushes, no force-pushes, no direct commits
  by anyone (human or agent). All changes land via PR.
  > **Human action required:** branch protection is a GitHub repository
  > setting, not something enforceable from inside the repo. Configure it
  > (require PR, require review, require status checks) once CI exists.
- All work happens on branches cut from `main`.

## Branch naming

| Prefix | Use |
|---|---|
| `spec/<slug>` | Adding or revising a spec |
| `plan/<slug>` | Adding or revising a plan |
| `feature/<slug>` | Implementing an approved plan |
| `fix/<slug>` | Bug fix with no design implication |
| `docs/<slug>` | Documentation-only change |
| `chore/<slug>` | Tooling, formatting, repo hygiene |

`<slug>` should match the spec/plan filename it corresponds to, so a
branch, its spec, and its plan are traceable to each other at a glance.

## Commits

- Use [Conventional Commits](https://www.conventionalcommits.org/) style
  prefixes: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`,
  `spec:`, `plan:`, `adr:`.
- Keep commits small and reviewable. A commit should represent one
  coherent change, not a snapshot of "everything so far."
- Never use `--no-verify` or otherwise bypass hooks once hooks exist,
  unless a human explicitly says to.

> **Open question for human review:** squash-merge vs. merge-commit vs.
> rebase-merge into `main` has not been decided. This document assumes
> squash-merge (one commit per PR on `main`) as a working default because
> it keeps `main` history aligned 1:1 with specs/plans, but this should be
> confirmed and set as the repository's merge policy before the first real
> PR merges.

## Pull requests

- Every PR uses [.github/PULL_REQUEST_TEMPLATE.md](../../.github/PULL_REQUEST_TEMPLATE.md)
  and links its spec, plan, and any ADRs. Identify the stage: a Spec/Plan review
  PR requests approval of its proposal; an implementation PR cites prior
  approval and the distinct joint Spec+Plan Human Review.
- The reviewing PR owns discussion and approval evidence. Keep a concise
  reviewer/date/PR reference in the approved document; record joint review once
  in the Plan, naming both documents and the implementation authorization.
- The implementation PR owns deviations, commands, test results, hardware,
  limitations, and any required human golden review. Registries link that PR
  with a coarse current state. Mark genuinely inapplicable checks N/A with a
  reason; a proposal's own approval is pending until the human gives it.
- An agent opens PRs but never merges them. A human reviews and merges.
- See [Definition of Done](definition-of-done.md) for merge criteria.

## Specs, plans, and ADRs are versioned like code

- They live in the repo, get PRs, get reviewed, and get approved the same
  way code does. "Approved" means merged to `main` with reviewer sign-off,
  not a verbal go-ahead in chat.
- A spec/plan is edited in place while in `Draft`/`In Review` status. Once
  `Approved` and implementation has started against it, further changes go
  through a new revision or a follow-up spec — don't rewrite history
  underneath work already in flight.

- Completed Specs/Plans may undergo editorial compaction only through an
  explicitly scoped, reviewed Plan under
  [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md).
  Preserve effective contracts, historical scope, and links; include an
  obligation-to-retained-text comparison and descriptive before/after metrics
  in the compaction PR, plus an editorial reference in each compacted document.
  This is not authorization for semantic changes or editing in-flight work.
- Accepted ADRs retain their decisions and history; changed decisions get a
  new superseding ADR, not an in-place amendment or compaction.
- For information ownership and proportional reading, follow
  [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
