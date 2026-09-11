# Plan: &lt;Title&gt;

- **Spec:** &lt;link — required&gt;
- **Status:** Draft
- **Author:**
- **Joint Human Review:** &lt;pending; once approved: reviewer, date, PR naming
  this Plan and its Spec and explicitly authorizing implementation&gt;

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Restate, in one or two sentences, what slice of the spec this plan
implements.

## Milestones / Task Breakdown

Ordered, concrete, independently reviewable steps. Each step should be
small enough to review in isolation.

1. ...
2. ...

## Files / Modules Touched (expected)

List the files/modules this plan expects to create or change. If the
actual implementation touches something not listed here, that's a
deviation to call out in the PR, not to slip in silently.

## Sequencing & Dependencies

What has to land before what, and why.

## Verification Checklist

Map each item back to the spec's Testing & Verification Plan, made
concrete:

- [ ] Unit tests: ...
- [ ] Headless integration tests: ...
- [ ] Image regression tests: ...
- [ ] Vulkan Validation Layers clean: ...
- [ ] Other: ...

## Rollback Plan

How this change is reverted if it turns out to be wrong after merge.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
List any deltas specific to this plan below (or "None").
