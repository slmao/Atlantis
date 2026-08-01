# Definition of Done

A PR is not done until every applicable item below is true. "Applicable"
excludes items that genuinely don't apply (e.g. a docs-only PR has no image
regression tests) — but the default assumption is that an item applies;
absence of relevance must be obvious, not assumed.

## Process

- [ ] An approved spec exists for this work, or the work is explicitly
      exempt (see "What counts as significant" in [AGENTS.md](../../AGENTS.md))
- [ ] An approved plan exists and the implementation matches it — any
      deviation is called out explicitly in the PR description
- [ ] Any architectural decision introduced by this work has a
      corresponding ADR in [adr/](../../adr/)

## Code

- [ ] Builds cleanly with no new warnings introduced
- [ ] Follows the module boundaries established by the relevant spec/ADR —
      no new coupling that the spec didn't call for
- [ ] No dead code, no commented-out code, no unresolved `TODO` without a
      tracked follow-up (issue or spec reference)

## Testing

- [ ] Unit tests added/updated for new logic (see
      [testing-strategy.md](testing-strategy.md))
- [ ] Image regression tests added/updated if rendered output changed, and
      any golden-image diffs in the PR were reviewed by a human, not
      auto-accepted
- [ ] Vulkan Validation Layers run clean (no errors, no warnings) for any
      code path that touches the GPU
- [ ] Headless verification performed for any rendering-adjacent change

## Documentation

- [ ] `docs/architecture/` updated if this PR is the implementation of a
      spec that changes as-built architecture
- [ ] `README.md` / other top-level docs updated if this PR changes how the
      project is built, run, or contributed to

## CI / Review

- [ ] CI is green (once CI exists — see [ci-strategy.md](ci-strategy.md))
- [ ] PR reviewed and approved by a human
- [ ] PR merged by a human, not by the agent that authored it
