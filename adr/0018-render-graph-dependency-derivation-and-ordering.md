# ADR 0018: RenderGraph Dependency Derivation and Deterministic Ordering

- **Status:** Accepted
- **Date:** 2026-08-09
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-09; see
  [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)'s
  Human Review Approval note for the full 16-item approval record this
  ADR's Decision is part of.
- **Related Spec:** [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md) (`Approved`)

## Context

For RenderGraph to be the mandatory path for GPU work
([AGENTS.md](../AGENTS.md), [ADR-0001](0001-rhi-backend-independence.md)),
it must derive inter-pass ordering constraints automatically from
declared resource usage — requiring pass authors to hand-author
dependency edges themselves would reduce RenderGraph to bookkeeping
around exactly the ad hoc, hand-scheduled GPU work
[docs/render_graph/README.md](../docs/render_graph/README.md) says this
module exists to replace.

**An earlier draft of this ADR was internally inconsistent.** It stated
both: (a) a write-after-write usage pair automatically produces an
ordering edge from the earlier writer to the later one (by declaration
order), and (b) multiple writers to the same resource are a compile error
*unless* already ordered by some other edge. Rule (a) makes rule (b)'s
error unreachable: if every write-after-write pair is automatically
ordered by declaration order, no multiple-writer situation is ever
"otherwise ambiguous," so the compile error rule (b) describes can never
actually fire. The same draft also asserted read-after-write and
write-after-read edges without a rule for *direction*: given only that
pass A reads and pass B writes the same un-versioned logical resource,
"A reads what B produces, so B runs first" and "A reads the prior value,
B then overwrites it, so A runs first" are both coherent readings, and
nothing in the resource model as previously stated picked between them.
Both problems trace to the same root cause: a plain read/write usage
declaration on an un-versioned logical resource does not, by itself,
carry enough information to derive edge direction or to make a
multiple-writer error reachable. A prior revision of this ADR fixed that
root cause by adopting the single-producer resource model below.

**A second revision removes this ADR's caller-authored explicit
pass-to-pass dependency edge.** The prior revision kept explicit edges as
an "escape hatch" for ordering constraints usage tracking cannot express,
justified only by the possibility that some future side effect might need
one. On further review that justification does not hold up: there is no
current, approved use case that needs it; "might need an escape hatch
later" is not a sufficient reason to ship a public scheduling-control
surface now; and a caller-authored dependency edge is itself a second way
to control pass order, sitting alongside — and potentially in tension
with — the resource-usage-derived edges that are supposed to be
RenderGraph's actual value proposition (deriving order from resource
relationships, not from hand-authored priorities). If a real workload
later needs to express a side effect resource usage cannot capture,
*that* future spec should decide the right mechanism — an explicit edge,
a side-effect resource/token, or something else — rather than this round
guessing preemptively at a mechanism with no concrete case to validate it
against.

## Decision

**Single-producer logical resource model, with dependency derived solely
from resource usage — no caller-authored dependency edge of any kind.**
Within one compiled graph, a given logical resource has **at most one
producing pass** — the pass that declares a write usage against it. This
is the sole dependency primitive; there is no resource versioning, no
in-place-mutation model, and no attempt to support every traditional
read/write hazard category in this round.

- **Producer → reader is the only derived edge kind, and the only edge
  kind of any sort.** If a logical resource has a producer, every pass
  that declares a read usage against that same resource gets a derived
  ordering edge from the producer to that reader. Direction is
  unambiguous by construction: a resource's value, for the purposes of
  this graph, *is* whatever its one producer wrote, so every reader
  necessarily runs after it.
- **Reader ↔ reader: no edge.** Two passes that both read the same
  resource are independent of each other with respect to that resource.
- **Producer-less resources are valid.** A logical resource with no
  producer at all — never written by any pass in this graph — is valid
  and may still be read; it carries no derived edge for any of its
  readers (there is no producer to order them after). This is the
  supported way to represent an externally-provided input to the graph —
  see Proposed Design's "Logical resources" section in
  [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)
  for what this token does and does not represent.
- **A logical resource that is declared but never given any usage at
  all (no producer, no reader) is a valid, harmless declaration.** It
  participates in no dependency relation and is not a compile error.
- **Multiple producers for the same logical resource is unconditionally
  a compile error.** There is no declaration-order tie-break and no
  "already ordered by some other edge" escape — there is no edge type
  left that could provide one, since there is no caller-authored edge at
  all. This is what makes the error actually reachable: nothing in this
  model ever auto-orders two writers of the same resource into a legal
  sequence.
- **A pass declaring both a read and a write usage against the same
  logical resource is an unsupported declaration**, not a supported
  read-modify-write pattern. This round's model has no way to express
  "read the old value, then produce a new one" on a single resource
  identity — that is exactly the kind of resource-versioning concept
  deferred to a future spec (see Alternatives Considered). Whether this
  is rejected as a programmer-error assertion or a compile error is fixed
  in [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)'s
  Error Model, not by this ADR.
- **There is no caller-authored pass-to-pass dependency edge of any
  kind** — no explicit edge declaration, no `dependsOn`-shaped API, no
  before/after relation, no manual edge list, no priority/order override,
  and no integer sort key. The only way to establish that pass B runs
  after pass A is for A to produce a resource B reads.
- **Cycle detection runs over the producer-derived edge set** (the only
  edge set that exists). A cycle spans **two or more distinct passes** —
  it cannot be a single-pass self-loop, because a pass cannot
  simultaneously be the producer and a reader of the same resource (that
  declaration is rejected at declaration time as a programmer error, per
  [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)'s
  Error Model, before compile ever runs); there is therefore no
  degenerate self-loop case for compile-time cycle detection to worry
  about. A genuine cycle arises across two or more passes and two or more
  resources — for example:

  ```
  Pass A: writes Resource X, reads Resource Y
  Pass B: writes Resource Y, reads Resource X

  Derived edges: A -> B (through X: A produces X, B reads X)
                 B -> A (through Y: B produces Y, A reads Y)
  ```

  This is a cycle formed **entirely from producer-derived edges across
  two resources** — no explicit edge is needed to construct it, and none
  is needed to detect it. A detected cycle is a compile error carrying
  enough information to identify the participating passes; it is never
  silently broken, partially applied, or resolved by picking an arbitrary
  edge to drop.
- **Deterministic ordering / tie-break:** compilation resolves a
  deterministic topological order using **pass declaration order as the
  tie-break** for any two passes with no path between them in the
  (producer-derived-only) dependency graph. Compiled order is therefore a
  pure function of declaration order plus that edge set — never hash-map
  iteration order, pointer/handle identity, or any thread-scheduling or
  wall-clock effect. **Declaration order is used only for this
  tie-break.** It plays no role in deriving edges, inferring
  producer/consumer direction, or resolving a multiple-producer
  conflict — those are governed entirely by the rules above, never
  guessed from the order passes happened to be declared in.
- **Every successfully declared pass appears in the compiled pass order
  exactly once.** This holds regardless of whether the pass has any
  producer/reader relationship to any other pass (an isolated pass is
  retained) and regardless of whether a pass's own produced resource is
  ever read by anything (a producer with no readers is retained). There
  is **no dead-pass, unreferenced-pass, or output-root-based culling of
  any kind** in this round — see Alternatives Considered.

## Consequences

### Positive

- The multiple-producer compile error is now **actually reachable**:
  nothing in this model auto-orders two writers of the same resource, so
  declaring two producers for one resource always surfaces the error.
- Edge direction is unambiguous by construction — producer-to-reader is
  the only derived relationship, so there is no case where two equally
  plausible readings of a read/write pair need an arbitrary tie-break.
- Removing the caller-authored dependency edge keeps RenderGraph's public
  API to a single ordering mechanism (resource usage) instead of two
  potentially-conflicting ones, and keeps the module's stated value
  proposition — deriving order from resource relationships, not from
  hand-authored priorities — true without exception.
- Cycle detection remains meaningful and testable with a genuine
  multi-pass, multi-resource example (see Decision), without needing any
  explicit-edge or self-loop machinery to exercise it.
- Determinism (declaration-order tie-break, scoped strictly to otherwise-
  unordered passes) makes compiled output reproducible across runs and
  directly testable with exact-order assertions.
- No resource-versioning API and no pass-culling API is invented ahead of
  a spec that actually needs one; a future spec can introduce either as
  an additive extension without this ADR having guessed its shape.

### Negative / Trade-offs

- The single-producer model cannot express several traditionally-useful
  patterns in this round: in-place read-modify-write on one resource
  identity, multiple sequential writers (e.g. accumulation passes), or
  any resource-history/version concept. Any graph needing those patterns
  today must be expressed as multiple distinct logical resources, which
  is more verbose than a versioned-resource API would be — accepted here
  as the cost of not inventing that API without its own review.
- With no caller-authored dependency edge, a real side effect that
  resource usage genuinely cannot express (e.g. an external ordering
  requirement with no corresponding resource) has **no way to be
  expressed in this round at all** — not even as an escape hatch. This is
  a real capability gap, accepted deliberately: it is judged better to
  surface that gap loudly (a caller simply cannot express it, and must
  wait for a future spec) than to ship an underspecified escape hatch
  against no concrete case.
- With no pass culling, a graph that declares a producer nobody reads
  still pays whatever cost that pass's presence in the compiled order
  implies (though this spec attaches no execution cost to compiled order
  by itself — see [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)
  Non-Goals). A future spec that wants to skip truly-unreferenced work
  needs to add culling deliberately, on top of a real notion of graph
  output.
- Declaration-order-as-tie-break still means reordering unrelated builder
  calls in caller code can silently change the compiled order between
  passes that are already independent — not a correctness bug, but a
  caller relying on incidental prior ordering between logically-unrelated
  passes could be surprised by a change elsewhere in the same file.

## Alternatives Considered

- **The previous draft's model: WAR/RAW/WAW all auto-derive edges, with
  multiple writers legal if "otherwise ordered" (declaration-order-based
  hazard derivation).** Rejected — this is the internally-inconsistent
  model an earlier revision of this ADR replaced; see Context.
- **Explicit-edges-only (no usage-derived dependencies at all).**
  Rejected: this pushes hazard analysis back onto every pass author,
  which is exactly the ad hoc/hand-authored-barrier failure mode
  RenderGraph exists to prevent per
  [docs/render_graph/README.md](../docs/render_graph/README.md)'s own
  framing.
- **Retaining a caller-authored explicit pass-to-pass dependency edge
  alongside producer-derived edges** (this ADR's own prior revision).
  Deferred, not permanently foreclosed: there is no current, approved use
  case for it, and shipping a second ordering-control surface against a
  hypothetical future need is not justified. If a concrete workload later
  needs to express an ordering constraint resource usage cannot capture,
  a future spec should decide the mechanism (an explicit edge, a
  side-effect resource/token, or something else) against that real case,
  rather than this round guessing at one now.
- **Allowing multiple producers when "already ordered by some other
  edge."** Rejected: with no caller-authored edge left to provide such an
  ordering, and with producer-derived edges themselves never able to
  order two producers of the *same* resource relative to each other, this
  exception has no remaining edge source to invoke — it would be dead
  language. This also matches the direct reasoning for why an earlier
  "let an explicit edge legalize multiple producers" idea is rejected:
  either mechanism would make "is this resource declaration legal" depend
  on unrelated declarations elsewhere in the graph, which is confusing to
  reason about and reintroduces the original unreachable-error problem in
  a different form.
- **Versioned/history resources** (each write produces a new version;
  reads bind to a specific version; multiple sequential writers are
  naturally expressible). This is a legitimate, more expressive model,
  and is not rejected as *wrong* — it is deferred: it requires deciding a
  version-identity scheme and a binding rule (does a read bind to "the
  latest version at declaration time," "the final version," or an
  explicitly named version?) that is significant enough to deserve its
  own spec/ADR once a real consumer (e.g. Renderer, or a concrete
  multi-pass accumulation use case) motivates it, rather than being
  guessed at now against no consumer.
- **Automatic dead-pass / unreferenced-pass / output-root-based
  culling.** Deferred, not adopted: culling requires a real notion of
  "this pass's output matters" (a graph output or root concept), which
  this spec does not define — there is no Renderer yet to say what a
  frame's actual outputs are. Retaining every declared pass unconditionally
  avoids guessing at that semantics now; a future spec can add root-based
  compilation once a real consumer defines what a graph's outputs are.
- **Non-deterministic / "any valid topological sort" ordering.**
  Rejected: breaks reproducible tests, makes CI failures
  non-reproducible, and directly contradicts this spec's own testing
  goal that repeated compilation of the same input yields the same
  result.
