# Spec: Const ECS Query Compatibility Boundary

- **Status:** Approved by owner joint review; PR #316 manual merge pending
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-11-01
- **Related Plan(s):** [Plan 0076](../plans/0076-ecs-query-compatibility.md) (jointly approved; manual merge pending)
- **Approval:** slmao, 2026-11-01; [joint approval evidence](https://github.com/slmao/Atlantis/pull/316#issuecomment-6554469797). Implementation awaits manual merge of [PR #316](https://github.com/slmao/Atlantis/pull/316).
- **Related ADR(s):** [ADR-0140](../adr/0140-distinct-const-query-entry-point.md) (Accepted), partially superseding ADR-0139's same-name API decision only.
- **Origin:** [Draft implementation #315](https://github.com/slmao/Atlantis/pull/315), blocked at Plan 0075 S2; [0075 approval evidence](https://github.com/slmao/Atlantis/pull/314#issuecomment-6553843026).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Replace only the newly proposed const member's name with
`queryReadOnly<Ts...>(fn) const`, keeping the existing mutable `query` overload
set intact. Preserve the new entry's unqualified/const component terms and
declaration-level direct-call constraint, and all other approved 0075 obligations.
This is a joint-review proposal, not a repair or authority to resume #315.

## Motivation / Problem Statement

The constrained const overload in #315 is considered while compiling valid old
mutable queries. Checking its call expression can instantiate a generic lambda
body with const components and cause a hard error before mutable overload
selection completes. The retained regression violates 0075 R2.

A bounded design review first examined same-name approaches. No evaluated
approach retained all approved R1/R2 requirements. Restricting const queries to
explicitly const terms avoids the primary component-write case, but still
considers the new constraint for old all-const and empty queries. Their existing
id argument is a const lvalue, whereas the new entry uses an EntityId temporary;
old generic callbacks can legally distinguish these and fail in that constraint.
The evidence and finite review limits belong in the reviewing PR; this is not a
proof that every possible C++20 same-name design is impossible.

## Goals

- Remove the new callability constraint from every old `query` call's candidate set.
- Keep a genuinely const World's constrained, live, read-only traversal capability.
- Resume the smallest approved implementation slice only after a new joint ruling.

## Non-Goals

No Runtime adoption, new query owner/view wrapper, language/toolchain upgrade,
callable reflection framework, dependency, storage/threading change or consumer
workaround. No #307 dependency, performance investigation, baseline/policy change,
failed-evidence removal or verification exemption.

## Requirements

- **C1 — Explicit API delta.** Replace 0075 R1's new member spelling
  `query<Ts...>(fn) const` with `queryReadOnly<Ts...>(fn) const`. The old non-const
  `query` declaration/body, receiver domain, callback argument expressions and
  overload behavior remain unchanged. Add no const `query` alias, forwarding
  compatibility shim or deleted same-name overload. A const receiver uses the
  new name; it does not acquire `query`. The new entry is also callable on a
  mutable receiver and always supplies read-only component references.
- **C2 — Retained access requirements.** All remaining 0075 R1 behavior is
  retained under the new spelling: `T` and `const T`, mixed terms, id by value,
  empty all-entity query, existing term domain and void result. R2's actual
  lvalue direct-call constraint, dependent rejection of writable typed callbacks,
  generic read-only const propagation, callable lifetime and all old-call
  compatibility obligations are unchanged; they apply to the renamed const
  entry where relevant. Do not substitute `std::invoke` semantics or defer
  the sole boundary check to an unconstrained body.
- **C3 — Retained architecture and scope.** Inherit 0075 R3–R6 without change:
  distinct const view/walk/projection, only mutable query-depth bookkeeping,
  shared guard, aliasing/nesting/CommandBuffer protection and single-threaded
  live-value behavior. No production CommandBuffer or Runtime consumer edit,
  schema/version bump or mutation of approved normative text. The narrow
  future file list is in Plan 0076; unexpected needs stop for reporting.
- **C4 — Compatibility evidence.** Retain #315's failure and old evidence.
  Future tests must exercise actual old mutable calls, including generic
  component mutation, all-const/empty generic id-category callbacks, overloads,
  lvalue-only/move-only callables and the existing receiver domain. New-entry
  tests must use its real constrained declaration and traversal. A reduced
  design model alone is not implementation acceptance; do not remove a failing
  test or change old callback argument categories to make it pass.
- **C5 — Gates and authority.** Inherit 0075 R7 and Plan 0075 S2–S4's full
  build/test, Android, Runtime CPU, headless/image/Vulkan Validation and formal
  performance obligations. Actual full implementation-PR applicability remains
  to be evaluated; expected **applies=true** for World paths, with no unused-API
  or unchanged-consumer waiver. Retain every non-pass and unresolved blocker,
  keeping #315 Draft. This proposal authorizes no implementation or measurement;
  future work needs joint Spec+Plan approval, ADR-0140 acceptance, human merge
  and explicit implementation authorization. No automatic ready/merge.

### Non-functional

Keep C++20 and Windows/Android portability, 0075's complexity, ownership and
temporary-column bookkeeping bounds. The spelling change adds no allocation,
indirection, timing target or concurrent-reader promise.

## Proposed Design

Use the distinct entry name in C1 and reuse the already scoped read-only
traversal design. Name lookup for existing `query` calls cannot consider the
new function template. A caller opts into read-only traversal explicitly by
name; component qualifiers keep the original receiver-const propagation rules.
No equivalent const same-name alias is provided, since it reintroduces the
constraint candidate that caused the compatibility failure.

## Architectural Impact

This changes the reviewed public access API. Proposed
[ADR-0140](../adr/0140-distinct-const-query-entry-point.md) partially supersedes
ADR-0139 only for the same-name const entry decision and its related alternative
rationale. ADR-0139's logical iteration state, separate read-only boundary,
alias/nesting/lifetime/single-thread decisions remain in force. Neither accepted
decision body is rewritten; the new decision must be accepted at joint review.
Module boundaries, ADR-0101's old access contract and ADR-0102 remain unchanged.

## Alternatives Considered

- Keep all R1/R2 with same-name constraint refactoring: preferred if demonstrated,
  but the bounded direct/ref-qualified/trait/return-substitution models did not
  resolve the old generic mutation failure. Broader exploration is not required
  or authorized implicitly; no universal impossibility claim is made.
- Require only `const T` terms: loses unqualified/mixed R1 terms and does not
  isolate all old calls, notably id-sensitive all-const/empty queries.
- Remove constraints or check only the body: loses R2's dependent rejection.
- Require callers to annotate generic returns, constrain callbacks or cast Worlds:
  breaks old-call compatibility and shifts the API defect to consumers.
- Add wrappers/bases/explicit-object language features: more public/structural or
  language scope than a distinct name; no such design is validated here.

## Testing & Verification Plan

Plan 0076 maps C1–C5 to the renamed entry and legacy overload separation,
compile-time checks, actual traversal/protection tests and the unchanged Plan
0075 matrix. Review-stage reduced compile probes are evidence of design risks,
not a build/test pass for #315. No Runtime/GPU/performance execution this round.

## Risks & Open Questions

Joint review must decide whether the name delta is acceptable or to authorize a
separately bounded all-requirements-preserving design study instead. No unseen
consumers have adopted the new API as a supported merged capability. Windows
design probes do not establish Android compiler or real traversal correctness;
implementation and all gates remain pending. The formal gate can remain blocked
for independent reasons; the API decision supplies no exemption or repair scope.

## Out of Scope / Future Work

Resume Plan 0075 S2–S4 only under C5. Runtime consumer conversion and any new
performance/environment investigation require separate authorization. This round
ends at joint-review submission with #315 still Draft.
