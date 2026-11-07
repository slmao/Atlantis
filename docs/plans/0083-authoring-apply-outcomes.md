# Plan: Authoring Apply Outcomes and Recovery

- **Spec:** [Spec 0083](../specs/0083-authoring-apply-outcomes.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending in [PR #329](https://github.com/slmao/Atlantis/pull/329); this PR authorizes no implementation
- **Related ADR:** [ADR-0144 — Proposed](../adr/0144-correlated-authoring-apply-outcomes.md)

## Objective

Deliver the correlated acceptance/outcome query and explicit editor recovery
specified by R1–R6, through the common client surface. This round is S1 design
only; S2–S4 require explicit joint approval, ADR acceptance and human design merge.

## Milestones / Task Breakdown

1. **S1 — Joint design (this PR).** Check main/#328 and number availability;
   review the actual sink, pending slot, success-only host notification, CLI
   waiter, SDK/MCP and editor/history boundaries. Submit this pair and ADR-0144
   with navigation updates, J1–J4 and the proposed execution authorization.
2. **S2 — Shared lifecycle and native clients.** Add Connection values/operations;
   implement Authoring revisions, bounded observation and sink identity. Report
   every Runtime reload result and replaced pending request through AuthoringHost,
   preserving swap/resource/pause/fatal behavior. Add the exact Remote methods,
   CLI matching wait/status output and narrow native SDK facade. Verify lifecycle,
   host mapping, native codecs, client isolation and legacy compatibility.
3. **S3 — Managed clients and editor recovery.** Expose SDK values/operations and
   exact MCP tools, keeping explicit transaction settlement. Replace the editor's
   generation-only waiter with id/query settlement on its existing worker.
   Implement the Spec's failure/reopen/unknown recovery and Gizmo restrictions;
   preserve successful edit/history settlement, pipelining and one coalesced
   successful follow-up. Add focused model/real UI regressions.
4. **S4 — Applicable verification and review-ready PR.** Run affected checks and
   the final-candidate gates below, audit any reuse, restore disposable fixtures,
   update narrow as-built/usage/navigation and submit implementation for human
   review/merge. Ordinary in-scope repairs continue under the one authorization;
   no new milestone approvals are inferred. No automatic merge.

## Files / Modules Touched (expected)

**S1 only, exactly five documentation files:** this Plan; Spec 0083; new
ADR-0144; `docs/specs/README.md`; the existing editor direction paragraph in
`docs/project-blueprint.md`, including #328's now-merged navigation. No source,
build, dependency, asset, policy, baseline or Accepted ADR edits in this PR.

**Future implementation; new files explicitly marked:**

| Responsibility | Narrow expected scope |
|---|---|
| Connection contract | `src/connection/include/atlantis/connection/authoring_connection.h` and `src/connection/src/authoring_connection.cpp`: owned receipt/status/error values and two operations; existing events, legacy apply and World surface unchanged. |
| Authoring owner | `src/authoring/include/atlantis/authoring/authoring_session.h`, `src/authoring/src/authoring_session.cpp`: revisions, minting via existing GUID helper, bounded record retention, shared query, matching terminal report and legacy delegation. `src/asset_system/include/atlantis/asset_system/guid_mint.h` may receive only the caller-contract comment update naming this additional Authoring use; no mint algorithm/API change. Document validation/serializer semantics unchanged. |
| Runtime and bridge | `src/runtime/include/atlantis/runtime/runtime_application.h`, `src/runtime/src/runtime_application.cpp`, `src/runtime/authoring_host.h` / `.cpp`, `src/runtime/main.cpp`: request identity, supersession, typed outcomes on all reload exits and shutdown, bridge delivery order. Reuse `scene_load` error returns; do not redesign loading, resources or frame scheduling. No Authoring module dependency in RuntimeHost. |
| Native Remote | `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}` and `src/remote/include/atlantis/remote/remote_client.h` only if its authoring implementation needs declaration changes. Add exact encodings/dispatch; session/auth/socket/protocol-version logic unchanged. |
| CLI | `src/cli/include/atlantis/cli/{command_layer.h,invocation.h,script_runner.h}` and `src/cli/src/{command_layer.cpp,invocation.cpp,script_runner.cpp}` as required for matching normal/deferred outcomes, `scene apply-status [id]`, JSON/text/help. No log parser or local document/asset reader. |
| Native SDK | New inline `src/gameplay_sdk/include/atlantis/gameplay/authoring_apply.h` over injected AuthoringConnection. Keep the existing Connection-only link and include allow-list; no typed authoring CRUD or generated binding edits. |
| Managed SDK | `src/csharp/Atlantis.Gameplay/Authoring.cs`, `src/csharp/Atlantis.Gameplay/Remote/{AuthoringSession.cs,AuthoringCodec.cs}`. Existing error/transport dispatch helpers in `Remote/Codec.cs` only if needed for the two methods. No package, new assembly or codegen changes. |
| MCP | `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolSchemas.cs}` and existing session forwarding in `IAtlantisSession.cs` only if needed. Exactly two additive tools; legacy apply and explicit failure-drain contract preserved. |
| Editor model | `src/editor_app/Atlantis.Editor.Model/{AuthoringDocument.cs,DocumentTree.cs,AuthoringAliases.cs}` for receipt/status, pipelined adapter and ApplyController; existing `GizmoDrag.cs` / `EditHistory.cs` only if a demonstrated local recovery guard is required. Preserve record semantics and conflict checks. |
| Editor view/worker | `src/editor_app/Atlantis.Editor/{EditorSession.cs,MainWindow.cs}` for worker queries, owned callbacks, notices, Apply current document/Retry and preview gating. Picker/Assets UI uses ordinary edit paths; no new discovery or assignment operation. |
| Native tests | Existing `tests/connection/authoring_connection_tests.cpp`, `tests/authoring/{authoring_session_tests.cpp,authoring_fixture.h,authoring_edit_target_tests.cpp}`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/{cli_scene_tests.cpp,cli_e2e_tests.cpp,cli_script_tests.cpp}`, `tests/gameplay_sdk/{compile_check_tests.cpp,reflective_tests.cpp,gameplay_e2e_tests.cpp}`, `tests/runtime/scene_reload_gpu_tests.cpp`. New `tests/runtime/authoring_apply_outcome_tests.cpp` for private adapter mapping/lifetime only if existing fixtures cannot cover it; no general diagnostic framework. |
| Managed tests/vectors | `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}` (generate JSONL, never hand edit); `tests/csharp/fixture_server/main.cpp`; existing `tests/csharp/Atlantis.Gameplay.Tests/{CodecSuite.cs,LiveAuthoringSuite.cs}` and new `ApplyOutcomeSuite.cs` with `Program.cs` registration if separated. Editor `Program.cs`, `HistoryTests.cs`, `GizmoDragTests.cs`, `HierarchyTests.cs`, `AssetReferencePickerTests.cs`, `InspectorTests.cs`, `IntegrationTest.cs` and new `ApplyOutcomeTests.cs`, including existing IAuthoringDocument test doubles; MCP existing `Program.cs`, `M3Tests.cs`, `IntegrationTest.cs`, `SessionChecks.cs`, `VersionTests.cs` as applicable. |
| Registration/docs | Existing test `CMakeLists.txt` files only for the named new test registration if needed; no new target/option/dependency/CI behavior. `docs/architecture/{module_boundaries.md,remote_protocol.md}`, `README.md`, relevant SDK/MCP usage descriptions and a narrow module-summary update in `AGENTS.md` if stale. This pair/ADR status metadata, registry and blueprint navigation; no process-rule rewrite. |

Verify existing fixture/helper names before editing. A necessary nonsemantic
file-list adjustment uses the repository's narrow PR approval rule; do not
expand API, acceptance, dependencies, module boundaries or build behavior under
that rule. World/schema, Asset System behavior/API, renderers/backends, fixed
catalog semantics, generated World bindings, assets/goldens and performance
policy/store are outside implementation scope; only the named Asset System
caller-contract comment may change.

## Sequencing & Dependencies

Start from a newly fetched main after manual design merge, record the human
joint decision and compare inputs. #328 is a merged prerequisite, not authority
to alter its accepted design in place. S2 precedes clients consuming the new
contract; S3 can then use identical fixtures/vectors. Maintain ordinary client
access throughout, not a temporary editor-only capability. Check the pipelined
commit/apply adapter explicitly: its receipt belongs to the actual captured
snapshot, and a transaction refusal must not be reclassified as reload failure
even if the existing pipeline also applies the unchanged document.

### Joint review recommendations

| Decision | Recommendation for human ruling |
|---|---|
| **J1 — Minimum shared contract** | Approve R1/R3/R4: additive applyTracked + non-destructive applyStatus, opaque identity and observational document revision, unchanged legacy apply/Applied, no new event. Both SDKs, CLI, MCP and editor use the same semantics. Accept old-server unsupported behavior and no blind replay after a lost reply. |
| **J2 — Lifecycle and bounds** | Approve R2/R3 and ADR-0144: latest pending wins with explicit Superseded, one terminal result, 64 terminal metadata records plus pending/projection, volatile/UnknownApply boundary, typed coarse reload errors and existing fatal lifecycle. No FIFO/cancel/persistence/timeout verdict. |
| **J3 — Recovery and ownership** | Approve R5: preserve committed document/history and old World, suspend auto-apply on failure/supersession/unknown, explicit latest-document retry, guarded Undo/Redo and Save, destructive Reopen with old accepted snapshot still independent, preview restriction until a current successful projection. No automatic rollback or repeated retry. |
| **J4 — Delivery and gates** | Approve S2–S4 file scope and mapped checks, with ADR-0144 Accepted at joint review; retain native/managed/Android/shared-client and actual UI/GPU obligations, selected final-candidate performance protocol, evidence provenance and human final merge. |

### Suggested single implementation authorization

After deciding J1–J4, jointly approve Spec/Plan 0083 and accept ADR-0144; require
human merge of this design PR, then authorize continuous S2–S4 from latest main
through implementation, applicable verification and a review-ready PR. Ordinary
in-scope repairs need no renewed authorization; valid evidence may be reused,
with existing gates, budgets and stop conditions retained. Protected-category
changes require reviewed design, narrow nonsemantic file adjustments require
the existing explicit PR ruling, and final merge remains human. This is a
recommendation for a future ruling, not permission granted by this draft.

## Verification Checklist

**S1 design only:**

- [ ] Record immutable main/#328 manual merge and available 0083/0144 numbering
  in the PR; no conflicting open design PR. In Review/Proposed, approval pending.
- [ ] Check the five-file scope, templates, R1–R6/J1–J4 mapping, partial ADR
  supersession, relative links and `git diff --check`. Builds, Runtime/UI/GPU,
  image regression and performance sampling N/A for this documentation-only PR.

**Future final candidate (focused checks during work; reuse only unchanged inputs):**

- [ ] **R1 acceptance/version:** before-open/no-sink/cook/decode/sink refusal
  remains distinct and does not displace a pending request. Success returns the
  exact snapshot identity; edit/open later cannot mutate it. Revision advances
  only on successful open/nonempty accepted transactions, including equal value;
  empty/refused transactions, save/apply and World writes do not advance it.
- [ ] **R2 result coverage:** real success swaps once, generation matches and
  legacy Applied still arrives. DependencyTypeMismatch reproduces #328's
  accepted-then-failed path with ScenePreparationFailed, no swap/generation change,
  exact document edit retained and usable old World. Cover each other recoverable
  reload exit/error mapping in existing unit/fake fixtures; fatal waitIdle keeps
  fatal lifecycle/RuntimeStopped, not a claim of usable-device recovery. Check
  temporary resource cleanup and no changed all-or-nothing behavior.
- [ ] **R2 continuous/pause:** two clients accept A then B before swap while
  paused; A is Superseded by B, only B executes on Step/Resume. Refused B leaves A
  pending. Cover equal-revision repeats, A success/failure then a later B, three
  replacements, duplicate/stale/mismatched reports, and unrelated successful
  generations never settling a client's other receipt. No auto-step or timeout
  failure while paused.
- [ ] **R3 observation/lifetime:** queries are non-destructive and identical
  across clients/subscription drains. Check no-id empty/latest view, pending and
  all terminal payloads, exact 64-terminal eviction with pending/projection
  retained, UnknownApply for unknown/evicted/foreign ids, reopen with old pending
  snapshot, disconnect/reconnect to the same Runtime, and old ids after restart.
  A dropped acceptance reply, transport loss or unknown id produces unknown
  completion and no automatic duplicate request.
- [ ] **R4 shared compatibility:** C++/C# codec conformance includes valid and
  malformed ids, every state/error, optional fields, large revisions/generations
  and legacy bytes. Native Connection/Remote/SDK and C# SDK query the same real
  request; CLI normal/deferred/status text+JSON and exit behavior match it. MCP
  exact mapping/tool-schema/discovery/live tests cover both new tools and the
  unchanged explicit failure drain. Old method results/event filters remain
  byte-compatible; old Runtime returns unsupported tracking, not false success
  or silent fallback. No native/OS/GPU type leaks.
- [ ] **R5 models/history:** matching success releases one coalesced latest apply;
  failure/supersession/refusal releases in-flight, clears the queued auto retry
  and latches explicit recovery. Several later edits/Undo/Redo are ordinary
  guarded history operations and send no automatic apply while suspended. An
  explicit retry sends exactly one newest-document receipt, keeps history,
  ignores old results and returns to suspension on another failure. Include
  no-op/refused transaction and pipelined unchanged-document apply behavior.
- [ ] **R5 actual Windows UI:** actual MainWindow, worker, SDK and native Runtime
  on default and available Bistro in managed Debug/Release. Select a known
  incompatible reference, observe accepted receipt then visible terminal reload
  failure (not authoring ApplyError), usable old viewport, disabled new Gizmo
  previews and retained edit/history. Correct/Undo it without auto-retry; click
  Retry once, see current projection, normal editing and guarded Undo/Redo resume.
  Compare SDK/CLI observation with UI status. Test paused retry→Step/Resume,
  duplicate Retry, repeated failure, coalesced edits during recovery, external
  supersession, unknown/late callbacks, Save and imported-save refusal, Reopen
  clearing history while old pending completion cannot settle the reopened
  document. Declare routed input/controlled-callback boundaries; no unperformed
  physical-keyboard, screenshot or latency claim.
- [ ] **R5 persistence/state:** disposable hand-authored Save→reopen retains the
  repaired document without implying a reload; existing unchanged cook/restart
  portions may be reused with exact provenance. Restore fixture sources and
  compare hashes. Failed reload preserves the old frame/World including an
  intentional disposable tweak; successful recovery matches the known compatible
  saved/cooked scene. No tracked asset or golden updates.
- [ ] **R6 builds/boundaries:** affected native Debug/Release and managed SDK,
  Editor Model/App/tests and MCP with existing locked pins; focused Connection,
  Authoring, Remote, CLI, SDK, Runtime and managed suites plus module/package and
  generator-staleness checks. Android compile checks for changed native public
  interfaces/libraries; no Android session/editor hosting added. Reuse unrelated
  tests only after an input audit; #328's old feedback behavior is not evidence
  that the new contract passes. Record actual revisions/configurations/commands.
- [ ] **R6 rendering/gates:** new real reload failure/success/recovery paths run
  with clean Vulkan Validation. Run applicable existing headless/golden checks
  and exact before/failed/recovered frame comparisons without changing goldens;
  reuse only unaffected cases with provenance. Evaluate the entire PR's actual
  selector: Runtime paths **are selected**, even if the runFrame swap-point
  guard text remains unchanged. Complete the existing native Release,
  profiling-ON/Tracy-OFF final-candidate gate on the reference machine under
  [performance-contract.md](../process/performance-contract.md), retaining
  binding budgets/stop conditions. Report pass/failure/inconclusive/incomparable
  faithfully; no retry-to-pass, new latency benchmark, inherited old timing
  verdict or baseline/policy refresh. An unresolved required gate is a blocker.
- [ ] **Closure:** full diff/scope/approvals/deviations checked; as-built and usage
  descriptions distinguish acceptance, result and document/history state;
  evidence reuse and preserved failures documented in the implementation PR.
  Submit review-ready only when applicable gates pass or a human has explicitly
  decided a required unresolved item. Never infer approval from an empty CI rollup.

## Rollback Plan

This design PR can be withdrawn without product changes. A later implementation
rollback must revert the new shared methods, clients and recovery UI together;
existing documents/assets need no migration. A rollback returns to the disclosed
diagnostic-only limitation and must say so, rather than promising lost-request
recovery. Do not revert document edits or accepted history as part of rollback.

## Definition of Done

Follow [Definition of Done](../process/definition-of-done.md) at the applicable
stage. S1 is a reviewable design, not implementation approval. Future completion
requires mapped evidence, an Accepted ADR-0144, explicit joint authorization and
human design merge; final implementation review/merge remains human.
