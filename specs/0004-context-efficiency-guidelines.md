# Spec: Context-Efficient Documentation and Code Comment Guidelines

- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; human authorship/ownership of this spec is pending
  confirmation at Human Review.
- **Created:** 2026-08-06
- **Related Plan(s):** None yet — a plan is written only after this
  spec reaches `Approved` (see [AGENTS.md](../AGENTS.md); not created as
  part of this spec).
- **Related ADR(s):** None — see Architectural Impact.

## Summary

This spec proposes a short, stable addition to [AGENTS.md](../AGENTS.md)
establishing two related conventions: (1) a **single-authoritative-source**
principle for documentation, so that a given decision or status lives in
exactly one authoritative place and every other document links to it
instead of restating it, and (2) a **necessary-comment** convention for
code, so that comments explain non-obvious *why* (design rationale,
invariants, lifetime/ownership/thread-safety contracts, protocol
requirements, easy-to-misuse behavior) rather than restating what the
code already says. Both exist to reduce unnecessary token/attention cost
for AI agents and human readers navigating this repository, without
weakening Atlantis's existing Spec → Plan → Human Review → Implementation
→ Verification → PR → Merge governance or dropping any required content
(architectural rationale, constraints, verification steps, ownership/
thread-safety contracts).

This spec does **not** touch `AGENTS.md` itself — per
[AGENTS.md](../AGENTS.md)'s own Golden Rule, a governance-document change
requires Spec → Plan → Human Review before any edit lands. This spec and
its companion plan are that process; no rule described here is in effect
until both are `Approved`, Human Review is explicitly recorded, and the
approved plan is implemented.

## Motivation / Problem Statement

Atlantis's documentation set has grown quickly and by design: `AGENTS.md`
(canonical rules), `README.md`, `docs/architecture/*` (as-built design),
`docs/process/*` (prescriptive process), `docs/project-blueprint.md`
(roadmap/status navigation), `specs/*`, `plans/*`, `adr/*`. This is
intentional structure, not accidental sprawl — see
[docs/README.md](../docs/README.md) and
[docs/project-blueprint.md](../docs/project-blueprint.md) Section 1. But
as it grows, several concrete risks are already visible or foreseeable:

- **Navigation documents can drift into restating authoritative content.**
  `docs/project-blueprint.md` (a navigation document by its own Section 1)
  already summarizes architecture facts drawn from ADRs and specs; without
  an explicit convention, nothing stops a future edit from copying a
  spec's full design rationale into the blueprint "for convenience,"
  creating two texts that must be kept in sync by hand.
- **The same rule or status maintained in multiple files drifts.** A
  status update (e.g. a spec moving from `Approved` to a later state, or
  an ADR being superseded) is easy to update in its authoritative file and
  easy to forget in a navigation document that quoted it — see
  [docs/project-blueprint.md](../docs/project-blueprint.md) Section 8's
  own maintenance rules, which this spec's convention generalizes rather
  than duplicates.
- **An agent prompt can request undifferentiated reading of the entire
  documentation set** ("read AGENTS.md, README.md, every spec, every ADR,
  every architecture doc...") even when the task at hand needs only a
  narrow slice of it, burning context/attention on material irrelevant to
  the task.
- **Verbose, low-value code comments increase maintenance and review
  cost.** A comment that restates what a line of code already says (e.g.
  `// increment counter` above `counter++;`) adds nothing and must still
  be read, reviewed, and kept from going stale.
- **The opposite failure is equally real and must not be introduced by
  this spec's fix:** a blanket push toward "shorter" documentation or
  "fewer" comments can just as easily delete a genuinely necessary design
  rationale, a non-obvious constraint, or a required verification step —
  exactly the content [AGENTS.md](../AGENTS.md)'s existing rules (e.g.
  "every module boundary... is a reviewed decision, recorded in a
  spec/ADR", "every type used across threads documents its thread-safety
  contract") depend on existing somewhere legible. A rule that only says
  "be concise" without also saying "and never at the cost of this specific
  required content" would be an uncontrolled weakening of governance, not
  a documentation-quality improvement.

No existing rule in [AGENTS.md](../AGENTS.md) currently addresses
either of these two conventions (verified by inspection: `AGENTS.md`'s C++
coding conventions section covers naming/formatting/includes, not comment
content policy; nothing in the document currently states a
single-source-of-truth principle for documentation). This spec exists to
add exactly that, as a short, durable addition — not to re-derive or
duplicate the workflow, module-boundary, or coding-convention rules
`AGENTS.md` already states.

## Goals

- Establish a **single-authoritative-source-plus-link** principle for
  Atlantis documentation: a given architectural decision, spec scope, plan
  sequencing, or governance rule has exactly one authoritative home (an
  Accepted ADR, an Approved Spec/Plan, or `AGENTS.md` itself); every other
  document that needs to reference it links to that home and states at
  most a short summary, not a restatement of the full rationale.
- Direct navigation documents (README, blueprint, spec/plan registries,
  and similar index-style documents) to prioritize index/status/
  dependency/roadmap content over reproducing full spec/plan/ADR design
  and argumentation.
- State that an agent should read documentation **proportional to the
  task at hand** — not read the entire historical documentation set by
  default when a narrower, task-relevant subset suffices — while leaving
  the judgment of "what's task-relevant" to the agent and the user, not
  encoding a mechanical file list.
- Establish a **necessary-comment** convention: comments should explain
  non-obvious rationale, constraints, and contracts; comments should not
  restate what code already expresses through clear naming, structure,
  and types.
- Explicitly preserve every category of comment/documentation content
  [AGENTS.md](../AGENTS.md) already implicitly or explicitly requires:
  thread-safety contracts, ownership/lifetime/borrowing rules,
  Vulkan-specific synchronization/WSI/platform-difference notes, links to
  the spec/ADR backing a non-obvious design choice, and any precondition/
  error-semantics contract on a public API.
- Keep the eventual `AGENTS.md` addition short — a compact section, not a
  restatement of this spec's own reasoning.

## Non-Goals

Explicitly out of scope for this spec and the plan built on it:

- **No mechanical line-count, word-count, or comment-density limit** on
  any file, function, or comment — a fixed numeric threshold is exactly
  the kind of rule that produces bad incentives (padding to hit a minimum,
  cutting necessary content to hit a maximum) instead of the judgment this
  spec asks for.
- **No deletion of necessary design rationale for the sake of token
  reduction.** This spec does not authorize, and its resulting rule must
  not be read to authorize, stripping a spec's Alternatives Considered,
  Risks & Open Questions, or an ADR's Context/Consequences to save space.
- **No automatic or batch rewriting of existing Markdown.** This spec does
  not propose, and no future work under it may perform without its own
  review, a sweep that edits existing specs/plans/ADRs/architecture docs
  for concision. Any such cleanup of a *specific* existing document is a
  separate, individually reviewable change if ever proposed.
- **No deletion of historical Specs, Plans, or ADRs**, and no change to
  their `Superseded`/immutability conventions already stated in
  [adr/README.md](../adr/README.md), [specs/README.md](../specs/README.md),
  and [plans/README.md](../plans/README.md).
- **No requirement that code be uncommented.** This spec narrows *what
  kind* of comment is expected, not whether comments are allowed or
  required at all.
- **No change to any C++ API, module boundary, dependency, or build
  system.** This spec is documentation/process-only.
- **No change to the Spec → Plan → Human Review → Implementation →
  Verification → PR → Merge workflow stages themselves**, their file
  locations, their status-value vocabularies, or any existing role
  (Accepted ADR, Approved Spec, Approved Plan) as the authoritative source
  for its own subject matter — this spec's single-authoritative-source
  principle formalizes and generalizes a pattern the repository already
  follows (see [docs/project-blueprint.md](../docs/project-blueprint.md)
  Section 1's own "Accepted ADRs are the authoritative source of *why*..."
  language); it does not reassign any authority.
- **No new lint tool, formatter, CI check, or third-party dependency.**
  Enforcement is human/agent judgment at review time, consistent with how
  every other qualitative rule in [AGENTS.md](../AGENTS.md) (e.g. "match
  surrounding code," "no speculative abstraction") is already enforced.
- **No blanket instruction that agents must read fewer files regardless of
  task need.** The goal is proportionality to the task, not a minimum- or
  maximum-file-count mandate.

## Requirements

### Functional

- `AGENTS.md` gains exactly one new, compact section (see Proposed Design)
  containing both the documentation-concision convention and the
  code-comment convention. It is additive: no existing `AGENTS.md`
  section's rules are removed, weakened, reworded to a lesser obligation,
  or contradicted.
- The new section explicitly states that required contracts (thread-
  safety, ownership/lifetime, precondition/error semantics on public
  APIs, and any content [AGENTS.md](../AGENTS.md) elsewhere requires to be
  documented) are not subject to the concision convention — they must
  still be stated, in full, wherever `AGENTS.md` already requires them.
- The new section states the single-authoritative-source-plus-link
  principle in terms general enough to apply to `README.md`,
  `docs/project-blueprint.md`, and any future navigation/index document,
  without naming implementation specifics (e.g. exact wording of any
  individual existing document) that would go stale as those documents
  evolve.
- The new section's code-comment convention is stated compatibly with
  `AGENTS.md`'s existing C++ coding conventions section (same file,
  adjacent concerns) without duplicating its content (naming, formatting,
  includes) or overlapping it — this new section is about comment
  *content*, the existing section is about code *structure/style*.
- Nothing in this spec's proposed rule text is prescriptive at the level
  of "this specific document must now say X" for any existing spec, plan,
  ADR, or architecture document — those are out of scope (see Non-Goals);
  the rule is prospective guidance for future documentation and code
  changes.

### Non-functional

- **Performance / Portability:** not applicable — no code or build system
  is touched.
- **Memory:** not applicable.
- **Other:** the eventual `AGENTS.md` addition must be short enough that a
  future agent reading `AGENTS.md` (already a ~360-line file as of this
  spec) incurs materially less reading cost than the problem it solves
  would otherwise cost across the documentation set — i.e., the fix
  itself must not become a second instance of the problem it addresses.
  This is a design goal for Plan-stage wording, not a numeric constraint
  (see Non-Goals — no mechanical limit).

## Proposed Design

### Where the new section lives

A new, single, compact section is added to `AGENTS.md`, placed near the
existing **C++ coding conventions** section (the natural neighbor for the
code-comment half of this rule) or as its own short top-level section
directly after **Architecture principles** (the natural neighbor for the
documentation half, since it is a repository-wide convention, not a
C++-specific one). The exact placement and heading text (e.g.
`Documentation and context efficiency`, or a title covering both
documentation and code comments explicitly) is a Plan-stage decision,
not fixed here — Non-functional requirements above state only the
placement *goal* (adjacent to related existing content, not duplicating
it), consistent with this being a governance-document edit that itself
follows Spec → Plan → Human Review rather than being pre-written verbatim
in this Spec.

### Core rule content (semantics fixed by this spec; exact wording is a
Plan-stage/Human-Review-stage decision)

**Documentation concision:**

- A document should retain the information needed to fulfill its own
  stated role (per [docs/README.md](../docs/README.md)'s and each
  template's existing role definitions), but should not restate another
  document's authoritative content in detail — it should summarize and
  link instead.
- A given decision or status has one authoritative source: an Accepted
  ADR for *why* an architectural decision was made, an Approved Spec for
  *what* a piece of work is scoped to build, an Approved Plan for *how*
  it is sequenced and verified, and `AGENTS.md` for repository-wide
  governance/coding rules. Every other document referencing that decision
  or status uses a short summary plus a link, not a parallel restatement.
- Navigation/index-style documents (README, blueprint, spec/plan
  registries, and anything serving a similar role in the future)
  prioritize index, status, dependency, and roadmap content; they do not
  reproduce a spec's or ADR's full design rationale or argumentation.
- An agent reads documentation proportional to the task at hand; absent a
  task-specific reason, an agent should not default to reading the entire
  historical documentation set when a narrower, clearly task-relevant
  subset is sufficient. This does not authorize skipping documents a task
  actually requires (e.g. the existing per-task reading lists already
  established by this repository's own working conventions remain in
  force — this is a proportionality principle, not a shortcut).
- This convention never justifies omitting a requirement, constraint,
  design rationale, verification step, risk, or governance status that
  the source document's own template or role requires it to state. See
  Requirements above and Non-Goals.

**Code comments:**

- A comment should explain what the code itself cannot: non-obvious
  design rationale, invariants, ownership/lifetime/borrowing rules,
  thread-safety contracts, platform or backend differences, protocol/
  format requirements, or behavior that is easy to misuse without the
  comment's warning.
- A comment should not restate, line by line, what the code already says
  through clear naming, structure, and types. Prefer improving naming,
  extracting a small well-named function, or using a more precise type
  over adding a comment to compensate for an unclear one — reach for a
  comment only when naming/structure/types cannot themselves carry the
  needed information.
- Avoid comment patterns that add reading cost without adding information:
  large banner comments, comments duplicated at multiple sites, narrated
  edit history (that belongs in commit messages / PR descriptions, not
  inline), and TODOs with no actionable tracking (issue or spec
  reference).
- Public API surfaces keep whatever contract documentation
  [AGENTS.md](../AGENTS.md) already requires or implies elsewhere
  (thread-safety per the Threading rules section, ownership/lifetime per
  the Ownership and lifetime rules section, error semantics per the Error
  handling section) — this convention narrows *how* such comments are
  written (concise, contract-focused), never *whether* they exist.
- Complex algorithms, Vulkan synchronization, resource lifetime,
  platform/WSI boundary code, and non-obvious workarounds keep comments
  sufficient to understand them, including a link to the backing spec/ADR
  where one exists — this is an explicit carve-out, not an afterthought,
  precisely because this is where under-commenting is costliest.
- When code changes, any comment that no longer matches the new behavior
  is updated or removed in the same change — a stale comment is worse
  than no comment.
- Comments should be concise, but never at the cost of correctness or
  completeness of the contract they document.

### Relationship to existing rules

This spec's proposed rule is additive and narrower than it might first
appear:

- It does not change what content [AGENTS.md](../AGENTS.md)'s Threading
  rules, Ownership and lifetime rules, Error handling, or Vulkan-specific
  rules sections already require to be documented at a type's public API
  — it only says *how* to write that documentation (concisely, on
  substance).
- It formalizes, rather than invents, the single-authoritative-source
  pattern [docs/project-blueprint.md](../docs/project-blueprint.md)
  Section 1 already states for itself ("Accepted ADRs are the
  authoritative record of why... Approved Specs are the authoritative
  record of what... etc.") — generalizing an already-adopted convention
  to the rest of the documentation set, not introducing a new one.
- It does not change [docs/README.md](../docs/README.md)'s as-built vs.
  process split, or any spec/plan/ADR template.

## Architectural Impact

**None.** This spec does not introduce or change a subsystem boundary, a
public API, a runtime dependency, a threading model, a memory-ownership
model, or a backend-abstraction contract. It changes only: (a) a
documentation/governance convention recorded in `AGENTS.md`, and (b) a
convention for what code comments should contain — neither is C++ API
shape, module boundary, dependency, or runtime behavior. No ADR is
required.

This is nonetheless governed as **significant work**, per
[AGENTS.md](../AGENTS.md)'s "What counts as 'significant'" list ("Any
change to process/governance docs in this repository"): it requires this
Spec, a Plan, and explicit Human Review before `AGENTS.md` is touched.
Neither this spec nor its plan may edit `AGENTS.md` themselves — only a
future, separately-verified implementation step following an `Approved`
spec and an `Approved / Ready for Implementation` plan, with Human Review
explicitly recorded, may do that (see this spec's Out of Scope / Future
Work).

If implementation-time work under the approved plan discovers that
achieving this spec's goals actually requires reclassifying an existing
document's role (e.g. redefining what `docs/architecture/` vs.
`docs/process/` is for), reassigning authority away from an existing
Accepted ADR/Approved Spec, or otherwise touching something with
architectural or governance weight beyond a compact `AGENTS.md` addition,
implementation must stop and return to Spec review rather than silently
expanding scope — per this task's explicit instruction and
[AGENTS.md](../AGENTS.md)'s Golden Rule.

## Alternatives Considered

- **Add a numeric limit** (e.g. "navigation documents must be under N
  lines," "no comment may exceed N words"). Rejected: mechanical limits
  produce bad incentives (padding to a minimum, cutting necessary content
  to hit a maximum) and this task's own instructions explicitly rule this
  out.
- **Write a fully worked example/checklist directly into `AGENTS.md`.**
  Rejected for this spec's proposal: `AGENTS.md`'s own existing style is
  short, principle-stated rules with pointers to detail documents (e.g.
  its Module boundaries section summarizes and links
  `docs/architecture/module_boundaries.md` rather than inlining it) — a
  long worked example in the new section would itself violate the
  concision principle being added. Illustrative detail, if ever wanted,
  belongs in a process doc the new section can link to, not in
  `AGENTS.md` itself; that is left to the Plan/implementation stage to
  decide, not mandated by this spec.
- **Batch-edit existing documents for concision as part of this same
  spec.** Rejected: conflates a governance-rule change with content edits
  to many existing files, each of which would need its own review for
  whether it drops something necessary; explicitly excluded by this
  task's Non-Goals and kept as a possible, separately-reviewed future
  spec if ever pursued.
- **Skip the Spec/Plan/Human-Review process and edit `AGENTS.md`
  directly**, on the reasoning that a documentation-quality rule is
  low-risk. Rejected: `AGENTS.md` explicitly classifies "any change to
  process/governance docs" as significant, with no low-risk exception, and
  this task's own instructions require the full process.
- **Fold this into an existing spec/plan's scope** (e.g. an amendment to
  the not-yet-written Plan 0003). Rejected: this is an independent,
  repository-wide governance concern unrelated to Spec 0003's RHI/Vulkan
  scope; conflating them would make both harder to review.

## Testing & Verification Plan

No GPU, build, or runtime testing applies — this is a documentation/
governance change. Verification instead consists of:

- **Markdown validity:** the eventual `AGENTS.md` diff and this spec/plan
  pass `git diff --check` (no whitespace/line-ending errors) and contain
  no broken relative Markdown links.
- **Content review against this spec's Requirements:** a human reviewer
  confirms the new `AGENTS.md` section (a) is additive only — no existing
  rule removed, weakened, or contradicted; (b) does not introduce a
  mechanical numeric limit; (c) does not authorize omitting any content
  category `AGENTS.md` already requires elsewhere (thread-safety,
  ownership/lifetime, error semantics, design rationale, verification
  steps); (d) is short relative to the problem it addresses, per
  Non-functional requirements above.
- **Self-consistency check:** the new section itself should not be an
  instance of the problem it describes — i.e., it should not restate this
  spec's own reasoning in detail (it should be a compact rule, with this
  spec as the linked rationale, consistent with the single-authoritative-
  source principle it establishes).
- **Explicit Human Review sign-off** recorded on both this spec and its
  plan before either is marked `Approved` — per
  [AGENTS.md](../AGENTS.md) and [docs/process/git-workflow.md](../docs/process/git-workflow.md)'s
  "Approved means merged to `main` with reviewer sign-off, not a verbal
  go-ahead in chat."

## Risks & Open Questions

- **Exact heading text and placement within `AGENTS.md`** (adjacent to
  C++ coding conventions vs. its own top-level section vs. adjacent to
  Architecture principles) is not fixed by this spec — left to the Plan
  stage, since it doesn't carry architectural weight on its own, but
  flagged here so it is a deliberate Plan-stage choice, not an
  implementation-time improvisation.
- **Risk of the new rule being cited to justify removing necessary
  content from an existing document.** Mitigated by this spec's explicit
  Non-Goals and Requirements (concision "never justifies omitting a
  requirement, constraint, design rationale, verification step, risk, or
  governance status"), but ultimately depends on reviewer judgment at
  each future use — same enforcement model as every other qualitative
  `AGENTS.md` rule (e.g. "no speculative abstraction"), not a gap unique
  to this spec.
- **Risk of the rule being read as encouraging an agent to skip reading a
  document a task genuinely needs**, in the name of "proportional"
  reading. Mitigated by this spec's explicit statement that the
  convention "does not authorize skipping documents a task actually
  requires" — but, like the risk above, ultimately a matter of judgment
  applied per task, not something a written rule alone can fully
  guarantee.
- **Whether a future, separate spec should also define a lightweight
  "which documents does task type X typically need" reference** (as
  opposed to leaving proportionality entirely to per-task judgment) is
  flagged as a possible future idea, not decided or proposed by this
  spec — see Out of Scope / Future Work.

## Out of Scope / Future Work

- The actual edit to `AGENTS.md` — performed only after this spec and its
  plan are both `Approved` and Human Review is explicitly recorded,
  exactly matching the approved plan's file list. Not performed by this
  spec or its plan.
- Any retroactive concision pass over existing specs, plans, ADRs,
  architecture docs, `README.md`, or `docs/project-blueprint.md` — left
  as a possible future, separately-reviewed change if ever proposed (see
  Non-Goals and Alternatives Considered).
- A task-type-to-reading-list reference document, if ever wanted — not
  proposed here (see Risks & Open Questions).
- Any lint/CI enforcement of either convention — explicitly excluded (see
  Non-Goals); would itself be a "new dependency"/"CI change" under
  [AGENTS.md](../AGENTS.md) requiring its own future spec.

## Acceptance Criteria

- [ ] This spec and its companion plan both reach `Approved` only after
      explicit Human Review is recorded (not inferred from the request to
      draft them).
- [ ] The eventual `AGENTS.md` addition is a single, compact section that
      does not restate this spec's full explanation or reasoning.
- [ ] The documentation-concision rule explicitly states the
      single-authoritative-source-plus-link principle.
- [ ] The code-comment rule explicitly distinguishes "explains non-obvious
      rationale/constraints/contracts" from "restates what the code
      already says."
- [ ] The rule explicitly preserves required thread-safety, ownership/
      lifetime, error-semantics, and other public-API contract
      documentation already required elsewhere in `AGENTS.md`.
- [ ] No source file, build file, test file, existing spec, existing
      plan, existing ADR, or existing architecture/process document is
      modified by this spec or its plan.
- [ ] No mechanical line-count, word-count, or comment-density metric is
      introduced.
- [ ] `git diff --check` passes on every file this spec's work touches.
- [ ] Every Markdown link introduced resolves to an existing repository
      file.
- [ ] The reviewing human explicitly confirms the proposed rule does not
      weaken, remove, or contradict any existing `AGENTS.md` rule.
