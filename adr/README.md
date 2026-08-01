# adr/

An Architectural Decision Record (ADR) is the permanent record of a
significant decision: what was decided, why, and what it costs. Unlike
specs and plans, ADRs are not rewritten in place once accepted — a changed
mind gets a new ADR that supersedes the old one, so the history of
*why* stays intact.

- Template: [template.md](template.md)
- File naming: `adr/NNNN-<slug>.md`, sequential four-digit number, e.g.
  `adr/0001-rhi-abstraction-model.md`.
- Status values: `Proposed` → `Accepted` (or `Rejected` /
  `Superseded by ADR-NNNN`).
- A spec that identifies architectural impact must reference the ADR that
  will record it. The ADR does not need to be `Accepted` before the spec
  enters review, but it must be `Accepted` before the spec (and any plan
  built on it) is approved for implementation.

Full process: [AGENTS.md](../AGENTS.md).
