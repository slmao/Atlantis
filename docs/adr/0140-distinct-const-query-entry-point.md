# ADR 0140: Distinct Const Query Entry Point

- **Status:** Accepted
- **Date:** 2026-11-01
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-11-01; [joint acceptance evidence](https://github.com/slmao/Atlantis/pull/316#issuecomment-6554469797). [PR #316](https://github.com/slmao/Atlantis/pull/316) manually merged as `4cf7546`.
- **Related Spec:** [Spec 0076](../specs/0076-ecs-query-compatibility.md) (Approved)
- **Supersedes:** ADR-0139 in part: same-name const query API and the related alternative rationale only. Its remaining decisions are retained.
- **Prior acceptance:** [Owner acceptance of ADR-0139](https://github.com/slmao/Atlantis/pull/314#issuecomment-6553843026), recorded in [Draft #315](https://github.com/slmao/Atlantis/pull/315).

## Context

ADR-0139 approved a constrained receiver-const overload of the existing query.
That constraint is considered for mutable calls too. Instantiating an old generic
callback with the const candidate's arguments can fail in its body before the
mutable overload wins. Declaration-level rejection of typed writable callbacks
and preservation of valid old calls are both required.

The bounded review did not demonstrate a same-name solution satisfying all
0075 R1/R2 requirements. Restricting terms alone leaves all-const/empty queries
exposed, including old generic callbacks sensitive to the id argument category.
This does not establish universal impossibility, but makes a small explicit name
change preferable to unbounded metaprogramming or weakening the access boundary.

## Decision

Propose `World::queryReadOnly<Ts...>(fn) const` as the single new public read-only
entry. Keep existing `query` overloads and invocation expressions unchanged,
and add no const same-name alias. Retain the approved new entry's unqualified
and const component terms, empty query, EntityId value and direct lvalue-call
constraint. Name separation prevents its constraint from participating in old
query overload resolution.

Partially supersede ADR-0139's same-name API selection. Its accepted historical
rationale remains recorded there. Retain its separate read-only traversal, sole mutable depth
bookkeeping, shared protection, alias/live-value/lifetime and single-thread
decisions, together with ADR-0101/0102. Do not rewrite ADR-0139's accepted body.
No consumer migration, dependency, storage or threading change is decided here.

## Consequences

### Positive

- Old query calls do not instantiate new read-only callability constraints.
- The new entry retains the approved term convenience and strong const boundary.
- Existing scoped traversal work remains usable after review and verification.

### Negative / Trade-offs

- Mutable and const traversal use different public names; receiver constness
  alone no longer selects the new traversal.
- There is no `constWorld.query` convenience alias, deliberately avoiding the
  original candidate interaction; new callers must use `queryReadOnly`.
- Reduced Windows probes are design evidence only; actual World behavior and
  Android/compiler portability still need the unchanged implementation gates.

## Alternatives Considered

- Keep the name with direct, ref-qualified, callability-trait or return-SFINAE
  forms: the bounded evaluated models retain the generic-body hard failure.
- Explicitly const terms only: loses part of R1 while leaving id-sensitive old
  all-const/empty queries vulnerable; not a complete R2 disposition.
- Remove declaration constraints or change old callbacks/invocation categories:
  violates the compatibility or dependent-rejection obligations.
- Larger wrapper/base/public API or language upgrade: separate architectural
  scope, unvalidated here. Further same-name research needs its own bounded
  authorization rather than silently delaying or widening this repair.
