# plans/

A plan turns an approved spec into an ordered, reviewable set of concrete
changes. It is written *before* implementation and approved by a human
before code is written against it.

- Template: [template.md](template.md)
- File naming: `plans/<feature-slug>.md`, matching the spec it implements.
- Every plan links exactly one approved spec. A spec may have multiple
  plans over time (e.g. phased implementation), but each plan traces back
  to one spec.
- Status values: `Draft` → `In Review` → `Approved` (or `Superseded by
  <link>`).

Record joint Spec+Plan approval once through a concise evidence reference.
Implementation progress and results belong in the implementation PR.

Authoring rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Review, corrections, and completed-document compaction:
[Git workflow](../process/git-workflow.md#specs-plans-and-adrs-are-versioned-like-code).
