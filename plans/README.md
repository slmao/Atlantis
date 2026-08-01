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

Full process: [AGENTS.md](../AGENTS.md).
