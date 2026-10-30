# Plan: Authoring Failure Drain for the Agent Tool Layer

- **Spec:** [Spec 0071](../specs/0071-agent-tool-authoring-failures.md) (In Review), supplementing
  Approved [Spec 0070](../specs/0070-agent-tool-layer.md) and
  [Plan 0070](0070-agent-tool-layer.md).
- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** Pending. This Plan and Spec 0071 require joint review and explicit
  implementation authorization; this documentation PR authorizes no code work.
- **Related ADRs:** ADR-0138 and ADR-0130 remain Accepted; no ADR change proposed (Spec 0071,
  Architectural Impact).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
This is an additive plan; it does not rewrite the approved 0070 milestones or their history.

## Objective

Close [PR #307](https://github.com/slmao/Atlantis/pull/307)'s authoring failure-drain omission
within its existing MCP seam, then carry the explicit-settlement and two-client acceptance
deltas into Plan 0070 M3/M4. Protocol, SDKs, CLI and engine remain unchanged.

## Milestones / Task Breakdown

1. **S1 — Joint review, documentation only (this PR).** Review the Spec's R1–R5, resolve J1–J4
   below and record approval evidence in this Plan's header only after the human review.
   No implementation milestone begins in this request, even if the review PR is opened.
2. **S2 — Exact-tier correction, before M3 acceptance.** Starting from the M1/M2 implementation
   checkpoint in PR #307, add Spec 0071 R1's table entry and R2's matching failure-array handling
   using the existing seam. Extend the current mapping/MCP tests and fake queue sequences for
   R1–R3. Build the host/test runner and run the relevant tests before moving on.
3. **S3 — M3 integration.** While implementing the already-approved seven convenience tools,
   event drains and image content, carry R3's settlement guidance into the tool descriptions.
   Verify R4's explicit-drain, queue independence and discovery obligations in the existing
   console runner. M3 cannot be accepted without S2/S3 and its original Plan 0070 checks.
4. **S4 — M4 acceptance.** Add R5's explicit failure observations and deterministic two-client
   case to the planned real-host script on the default scene and Bistro. Use two independent
   MCP host processes/connections; arrange the stale-node interleaving and inspect the refusing
   connection's own queue. Check transaction identity, no document change on refusal, isolation
   and a cleared second drain. Retain Plan 0070's successful apply/event/capture session.
5. **S5 — Existing M5 completion.** Carry this correction into Plan 0070 M5's verification and
   normative MCP documentation. Record commands, results and any deviation in PR #307, not in
   the approved Spec/Plan. No new documentation subsystem or verification milestone is added.

## Files / Modules Touched (expected)

**This proposal PR only:**

- New `docs/specs/0071-agent-tool-authoring-failures.md` and this Plan.
- `docs/specs/README.md`: one navigation/status row for 0071.

**After joint approval, implementation delta over Plan 0070:**

- `src/mcp/Atlantis.Mcp/ToolRegistry.cs`: the single additional mapping, failure-array error
  handling, affected exact/convenience-tool descriptions. If M3 places its convenience
  declarations in another file under `src/mcp/Atlantis.Mcp/`, only those descriptions gain R3's
  guidance; the composition implementation remains Plan 0070's work.
- `src/mcp/tests/Atlantis.Mcp.Tests/Program.cs`: extend mapping, refusal and MCP discovery tests;
  add fake-session queue/settlement coverage here or in
  `src/mcp/tests/Atlantis.Mcp.Tests/AuthoringFailureTests.cs` if separation aids readability.
- `src/mcp/tests/Atlantis.Mcp.Tests/IntegrationTest.cs`: the M4 live script already planned in
  0070 P5, with R5's additions. `src/mcp/CMakeLists.txt` may register this script under the
  existing `ATLANTIS_BUILD_MCP` / test gates as part of M4; no new switch or dependency.
- `docs/architecture/module_boundaries.md` and `AGENTS.md`: only within Plan 0070 P6's already
  authorized MCP documentation, reflecting this explicit drain; no new governance rule.
- `docs/specs/README.md`: implementation/status links under the existing lifecycle.

**Unchanged:** Spec/Plan 0070, all Accepted ADRs, `IAtlantisSession`'s contract, wire reference
and protocol implementation, SDKs, CLI, native engine modules, schemas, packages/lock files,
goldens, performance policies/baselines and Android configuration. An unexpected need to touch
these is a deviation to report before coding, not permission to expand this list.

## Sequencing & Dependencies

- This documentation branch starts at `origin/main` and has no M1/M2 implementation commits.
  PR #307 remains a separate draft implementation PR; this proposal does not merge it.
- After joint approval is recorded and this follow-up is merged by a human, integrate it into
  `feature/0070-agent-tool-layer` and implement S2 in PR #307. M1/M2 remain the historical
  22-entry checkpoint; the corrected exact tier is 23 entries, not a retroactive M2 rewrite.
- S2 precedes S3 acceptance; S3 precedes S4. S4 cannot satisfy the planned refusal assertion by
  replacing a missing MCP tool with a separate SDK connection's drain.
- The original M3–M5 requirements and their ordering remain; this follow-up adds only the
  correction and acceptance deltas. The current request ends after S1's review submission.

## Joint Review — Pending Rulings

| Ruling | Recommendation |
|---|---|
| **J1 — Minimal surface** | Accept Spec R1/R2: one additional exact tool over the existing call, preserving the array and using a tool error only for a nonempty failure result or existing request/transport errors. |
| **J2 — Settlement and M3** | Accept R3/R4 and S2/S3: explicit client-driven failure drains; no hidden calls in the seven convenience tools; the corrected drain is a prerequisite to M3 acceptance. |
| **J3 — M4 meaning** | Accept R5/S4: a two-client stale-node `UnknownNode` refusal on both scenes; do not import the editor history's "changed elsewhere" or invent overwrite rejection. |
| **J4 — ADR disposition** | Confirm no new/superseding ADR is needed: the inventory addition follows Accepted ADR-0138 D3 and the existing ADR-0130 client capability. If this is rejected, resolve the architectural decision through a superseding ADR before authorizing code. |

**Distinct authorization requested:** after reviewing this Plan and Spec 0071 together, explicitly
authorize S2–S5 on `feature/0070-agent-tool-layer` in PR #307, alongside the remaining Approved
Plan 0070 M3–M5 scope, once this documentation PR merges. Individual Spec/Plan approval or PR
creation does not satisfy that joint implementation gate. No automatic merge is requested.

## Verification Checklist

**Proposal stage (this PR):**

- [ ] Check requirements and source citations against existing server/SDK/authoring behavior;
      check M3/M4 deltas against Plan 0070 and the editor J7 precedent.
- [ ] Validate Markdown links, `git diff --check` and a docs-only diff; no implementation,
      approved-document or Accepted-ADR edits.
- [ ] Obtain J1–J4 rulings and record the distinct joint review/authorization before code work.

**Implementation stage (not executed by this request):**

- [ ] **R1/R2:** build `atlantis_mcp_tests`; run `ctest --test-dir build -C Debug -L mcp
      --output-on-failure`; verify the 23-entry exact checkpoint, all authoring reason strings,
      complete ordered ticket arrays and matching MCP structured/text content, no retries.
- [ ] **R3/R4:** run the M3 console suites; verify one submission per convenience tool,
      later explicit drain, accumulation/clear and independent events, and 32 tools after M3.
- [ ] **R5:** run the two M4 live tests on the default scene and Bistro through MCP stdio,
      validating successful settlement/apply/capture and stale-node refusal/isolation; no source
      save or golden update; Vulkan Validation Layers clean on both GPU runs.
- [ ] **Boundary regression:** both managed package policies/negative suites and C# SDK rules /
      conformance tests green; protected paths unchanged; run and report performance applicability
      (`--gate --applies`, expected false), with any selected timing gate handled by repository
      policy. Complete the remaining Plan 0070 M5 checklist and normative docs.

## Rollback Plan

Revert only the drain mapping/handling/tests and its description changes if the implementation
is withdrawn; no data, wire or SDK migration is involved. The authoring refusal gap then remains
open and M3/M4 must not be claimed complete under this supplement. Original M1/M2 code is not
rewritten, and a partial M4 run is not substituted for the reviewed acceptance.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). At proposal stage,
verification is documentary; no build, GPU or image-regression result is claimed. Implementation
requires the joint gate above and the applicable original 0070 checks as well as this delta.
