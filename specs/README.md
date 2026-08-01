# specs/

A spec is a proposal: problem, requirements, proposed design, and an
explicit statement of architectural impact. It is written *before*
implementation, reviewed, and approved by a human before a plan is written
against it.

- Template: [template.md](template.md)
- File naming: `specs/<feature-slug>.md`, matching the eventual
  `plans/<feature-slug>.md` and branch name for traceability.
- Status values: `Draft` → `In Review` → `Approved` (or `Rejected` /
  `Superseded by <link>`).
- If a spec's Architectural Impact section says an ADR is required, the ADR
  must exist (at least as `Proposed`) before the spec can move to
  `Approved`.

Full process: [AGENTS.md](../AGENTS.md).
