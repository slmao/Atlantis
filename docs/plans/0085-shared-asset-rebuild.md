# Plan: Shared Rebuild of Existing Assets

- **Spec:** [Spec 0085](../specs/0085-shared-asset-rebuild.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending. Review Spec 0085, this Plan and
  [ADR-0146](../adr/0146-managed-asset-rebuild-and-generation-publication.md) together.

## Objective

Deliver a shared existing-asset rebuild with an isolated workspace, bounded
job reporting and explicit selection of a complete published generation.

## Milestones / Task Breakdown

1. **S1 — Joint design review.** Resolve J1–J4. Record the joint decision and
   ADR acceptance before authorizing implementation.
2. **S2 — Build and publish.** Generate the producer lookup from CMake.
   Implement trusted workspace/profile selection, the private coordinator,
   job/refusal rules and publication. Wire explicit startup selection for
   discovery, scene load/reload and imported authoring sources.
3. **S3 — Shared operations and Assets panel.** Carry the same typed start
   and status across Remote, both SDKs, CLI and MCP. Wire the Assets panel
   and local Editor launch options. Keep session and recovery boundaries.
4. **S4 — Verify and submit.** Run R1–R5 against the candidate, record
   inputs and gate verdicts in the implementation PR and leave merge to the
   human reviewer.

## Files / Modules Touched (expected)

| Responsibility | Expected scope |
|---|---|
| Pipeline declarations | `src/asset_system/CMakeLists.txt` for declaration-derived producer/stamp and imported-source locations. Reuse `atlantis_asset_catalog`, cooker and glTF importer without changing their formats or dependency rules. |
| Shared native surface | `src/connection/include/atlantis/connection/asset_connection.h` and asset value files in that module. Existing Remote asset sections in `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}`. Existing CLI asset commands in `src/cli/src/{command_layer.cpp,invocation.cpp}` and native SDK asset facade under `src/gameplay_sdk/`. |
| Windows composition | `src/runtime/{main.cpp,cli.h,cli.cpp,CMakeLists.txt,asset_host.h,asset_host.cpp}` plus private `asset_rebuild_host.{h,cpp}` for staging, process lifetime and publication. Pass the selected path through existing bootstrap and authoring composition. Keep `src/runtime/src/` and the frame loop outside this change. |
| Managed clients | Existing Assets values and `Remote/{AssetCodec.cs,AssetSession.cs}` in `src/csharp/Atlantis.Gameplay/`. MCP asset entries and closed schemas in `src/mcp/Atlantis.Mcp/`. Assets UI and worker in `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs,RuntimeProcess.cs,Program.cs}` and existing `Atlantis.Editor.Model/AssetBrowser.cs` if needed. |
| Verification | Focused existing suites under `tests/{asset_system,connection,remote,cli,gameplay_sdk,runtime,csharp}`, `src/mcp/tests/` and `src/editor_app/tests/`. A private coordinator test file and its existing Runtime test CMake registration are in scope. Use disposable Windows build/publication trees and a copy of the enabled Bistro inputs where needed. |
| Documentation | This Spec/Plan and ADR, spec index and blueprint navigation, `README.md`, `docs/architecture/{module_boundaries.md,remote_protocol.md}` and approval metadata. |

The Plan excludes package changes, renderer, World, Android packaging,
accepted ADR rewrites, asset format changes and committed golden changes.
An implementation file-list adjustment follows the existing approval rule.

## Sequencing & Dependencies

Start after the joint design PR has been approved and manually merged.
The slice builds on Specs 0047 and 0081–0084. Verify isolated rebuild and
startup selection before exposing the UI. Exercise the actual producer graph
and both startup sources before treating a status-only test as complete.

| Joint decision | Recommendation |
|---|---|
| **J1 — Producer scope** | Use cooked GUIDs, force one existing producer or whole registered glTF import, run the ordinary catalog graph, and require the full GUID/type/source identity set to stay stable. |
| **J2 — Ownership and recovery** | Opt-in trusted Windows workspace, one worker, one writer, immediate Busy, bounded process-local outcomes, a 30-minute deadline and explicit recovery for abandoned locks or unknown receipts. |
| **J3 — Publication and activation** | Publish complete isolated copies including generated imported scene sources. Require explicit local restart by generation ID. Retain prior generations with offline operator cleanup. |
| **J4 — Delivery** | Approve the two shared operations, client parity and the verification list. Authorize S2–S4 in one joint decision after the design PR is manually merged. Keep performance, Vulkan, golden and human merge gates. |

## Verification Checklist

- [ ] **S1 documentation.** Confirm numbering against current main and open
  work, internal links, approval statuses, scope and whitespace.
- [ ] **R1 producer and identity.** In a disposable configured Windows
  tree, rebuild a registered hand-authored material and a derived asset from
  the available Bistro import. Show the existing producer runs on explicit
  request with no timestamp change. Change valid source content and verify
  artifact bytes can change while the entire identity set stays stable.
  Verify the imported generated source comes from that rebuild. Refuse an
  unknown GUID, an unsupported producer and an import output-set change.
- [ ] **R2 job and failure.** Run success, two competing clients and a
  second managed Runtime sharing the workspace. Observe one admission and
  immediate Busy, including duplicate start. After orderly writer shutdown,
  verify restarting the other Runtime can claim the workspace. Observe the
  same ID after disconnect/reconnect and check latest, eviction and unknown status. Use
  controlled worker/process failures for launch, timeout, disk/publication
  and abandoned-lock cases. Exercise one real cooker/import failure, then
  correct the input and explicitly retry. Verify diagnostics are bounded,
  the failed job stays terminal, the child tree stops and the next attempt
  reruns validation. Check polling remains responsive while the child runs.
- [ ] **R3 publication and restart.** Hash the old catalog, artifacts and
  generated sources before and after both failure and success. Include
  missing output and a reparse/path-escape fixture. Verify no partial
  generation is selectable and the old Runtime's discovery is stable,
  including its first query after publication on a separate instance.
  Verify reconnect, Reload view and existing apply stay on the old
  generation. Start a new Runtime explicitly with the published generation
  and verify changed artifact bytes are actually loaded, including one
  scene observation or capture. Verify imported authoring resolves to that
  generation's generated source and hand-authored Save keeps its existing
  path. Verify an activation failure leaves previous output available and
  explicit restart with the previous generation succeeds. Confirm rebuild
  preserves the old document, projection, pending apply and history, and
  restart starts a new session with no carried-over undo.
- [ ] **R4 shared clients.** Native/C# codec conformance for start, status,
  refusal and terminal outcomes. Test new and legacy hosts/clients,
  unknown or malformed replies and unchanged list/get behavior. Observe
  the same admitted job and outcome across CLI, both SDKs, Editor and MCP.
  Check CLI terminal failure exit, MCP closed input, Editor status and
  restart guidance, and rejection of late callbacks after reconnect.
- [ ] **R5 builds and gates.** Build affected Windows Debug/Release and
  applicable Android native targets. Run focused SDK, Remote, CLI, MCP,
  Editor, Runtime and asset pipeline checks with existing pins. Use real
  Windows Runtime activation and Vulkan Validation Layers for default and
  available Bistro. Run or justify reuse of the applicable headless/golden
  checks using candidate input provenance. Keep intentionally altered
  fixture images separate from committed goldens. Run the full-diff
  performance selector and any selected gate under the
  [performance contract](../process/performance-contract.md). Changes to
  Runtime composition are expected to select the implementation gate.

## Rollback Plan

Run the ordinary Runtime without the managed profile, or explicitly choose a
previous generation on a build that supports selection. A code revert
removes the two operations, coordinator and launch options. Existing source
and cooked formats need no migration.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). S1 ends
with a design PR for human review. S2–S4 require joint approval, ADR
acceptance and the manual design merge. Final implementation merge is human.
