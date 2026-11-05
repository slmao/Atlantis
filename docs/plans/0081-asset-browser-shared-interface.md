# Plan: Asset Browser and Shared Asset Queries

- **Spec:** [Spec 0081](../specs/0081-asset-browser-shared-interface.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at slmao's direction
- **Joint Human Review:** Pending in [PR #325](https://github.com/slmao/Atlantis/pull/325); reviews this Plan, Spec 0081 and proposed ADR-0143 together. This round authorizes design submission only, not implementation.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Deliver the bounded read-only catalog discovery workflow in R1–R5 through the
existing client stack, then verify it proportionally under R6/0080. This round
submits the joint proposal only, based on main `374b9dd` after #324's human merge.

## Milestones / Task Breakdown

1. **S1 — Joint design (this PR).** Confirm latest main and #324's merge,
   inspect existing catalog/host/client/editor boundaries, submit this pair and
   ADR-0143, and update only the relevant registry/roadmap entries. Run document
   checks and stop; J1–J4 are one review decision, not four execution checkpoints.
2. **S2 — Shared query contract and host.** After joint sign-off, ADR acceptance
   and human proposal merge, add R2's Connection interface/DTOs, executable-private
   adapter and native Remote/CLI plumbing, including `--exec` injection. Test
   projection, GUID ordering, snapshot lifetime and in-band/transport errors with
   temporary catalogs and the existing local server fixture. No GPU for these tests.
3. **S3 — SDKs, MCP and Assets panel.** Add native SDK facade, managed AssetSession
   and C++-generated vectors; expose the two MCP tools and retain both version
   response forms. Add the UI-free browser model, worker wiring and minimal panel.
   Run focused tests as layers change; use the existing field edit/history path.
4. **S4 — Integration and review-ready implementation.** Finish the mapped matrix,
   audit actual whole-PR performance selection and run its applicable gate, update
   as-built docs/lifecycle metadata, and open/update an implementation PR. Reuse
   unchanged evidence with provenance and retain failures. Report specific blockers;
   human final review and merge are separate. No additional stage authorization.

## Files / Modules Touched (expected)

**S1 only, five documentation files:** this Plan; the new Spec; new ADR-0143;
`docs/specs/README.md`; the existing editor direction paragraph in
`docs/project-blueprint.md` (including #324's now-merged navigation status).

**Future implementation (new files marked new):**

| Responsibility | Expected narrow file scope |
|---|---|
| Connection | New `src/connection/include/atlantis/connection/asset_connection.h` for R2 values/interface; new `src/connection/src/asset_connection.cpp` for closed error/type spellings; `src/connection/CMakeLists.txt` source registration. No catalog/Tools/Runtime include or dependency. |
| Executable host | New `src/runtime/asset_host.h` / `.cpp` implements the provider using existing `loadAssetCatalog`; `src/runtime/main.cpp` constructs it from `config.assetCatalogPath`, wires `--listen` and `--exec`; `src/runtime/CMakeLists.txt` executable-only source registration. No new RuntimeHost link/public API, scene-load or `runFrame()` change. |
| Native transport | `src/remote/include/atlantis/remote/{remote_client.h,remote_server.h}` and `src/remote/src/{remote_client.cpp,remote_server.cpp,codec.h,codec.cpp}` for the two closed query methods/provider. Existing socket/session/token code unchanged. |
| CLI | `src/cli/include/atlantis/cli/{command_layer.h,invocation.h,script_runner.h}`, `src/cli/src/{command_layer.cpp,invocation.cpp,script_runner.cpp}` and `src/cli/app/main.cpp` as needed for provider injection, output and help. No local catalog reader or write command. |
| Native SDK | New `src/gameplay_sdk/include/atlantis/gameplay/assets.h`, an inline facade over Connection only; existing compile/reflective/e2e tests exercise it. No World/gameplay execution or generated binding change. |
| Managed SDK | New `src/csharp/Atlantis.Gameplay/Assets.cs`, `Remote/AssetSession.cs`, `Remote/AssetCodec.cs`; `Remote/RemoteSession.cs` for lazy property, no connect-time query. Existing zero-package policy unchanged. |
| MCP | `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolSchemas.cs}` and existing exact-mapping/version/integration tests. Two additive tools only; existing SDK request path and apply/drain behavior. |
| Editor | New `src/editor_app/Atlantis.Editor.Model/AssetBrowser.cs` for filtering/selection/detail state; `src/editor_app/Atlantis.Editor/{EditorSession.cs,MainWindow.cs}` for existing worker/dispatcher and panel. No Inspector/history contract or in-process editor change. |
| Native tests | New `tests/connection/asset_connection_tests.cpp`, `tests/remote/remote_asset_tests.cpp`, `tests/cli/cli_asset_tests.cpp`, `tests/runtime/asset_host_tests.cpp`; corresponding existing CMake test registrations; existing module-boundary, CLI fixture, script/e2e and Gameplay SDK suites for exposure/lifetime/error checks. Runtime test compiles the private adapter directly with Connection/Asset System, no new production test seam. |
| Managed tests/vectors | `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}` (JSONL generated only); new `tests/csharp/Atlantis.Gameplay.Tests/{AssetCodecSuite.cs,LiveAssetSuite.cs}` and their `Program.cs` registration; existing `tests/csharp/fixture_server/main.cpp` query provider; new editor `AssetBrowserTests.cs` and existing `Program.cs`/`IntegrationTest.cs`/`InspectorTests.cs`; existing MCP Program/M3/version/integration suites. |
| Documentation | `docs/architecture/{module_boundaries.md,remote_protocol.md}`, narrow shared-surface/module-rule summary in `AGENTS.md` if now stale, CLI help/usage and SDK/MCP usage descriptions; this pair/ADR status metadata, registry/roadmap links. No general process-rule rewrite. |

Verify actual existing fixture/registration paths before edits. Normal nonsemantic
file/sequencing adjustments follow AGENTS/0080; a changed contract, boundary,
format, dependency, build behavior or acceptance requirement needs design review.
Build edits are limited to registering the named files in existing targets/tests;
no new target/module, platform, option, CI job or package. Asset System production,
World/Core schema, renderer/backends, imported assets and generated World bindings
are outside scope. R2's host mapping uses existing Asset System APIs unchanged.

## Sequencing & Dependencies

Prerequisites are merged 0047/0054/0055/0057/0058/0064/0065/0068/0069/0070/0080;
the exact main baseline includes #324. S1 ends in a reviewable proposal. A later
explicit joint sign-off may approve J1–J4, Accept ADR-0143 and authorize S2–S4 after
human proposal merge, continuously through verification/review-ready PR under 0080.
There is no dependency on an unmerged feature or inherited performance verdict.
Honor future explicit stops/budgets; ordinary in-scope repairs are not new gates.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Minimum product slice** | Approve R1/R5: cooked-build asset browsing/detail/filter/copy; existing GUID Inspector edits finish the workflow. No asset writes, direct picker, import manager, preview or scene switch. |
| **J2 — Authority and lifecycle** | Approve R2/R3 and Accept ADR-0143: a sibling query interface, executable-private lazy immutable process snapshot, existing parser, typed owned values; restart after external build changes, no residency/loadability promise. |
| **J3 — Uniform client contract** | Approve R4 and ADR-0143's native SDK charter extension: the two methods in native Connection/Remote, CLI, C++/C# SDKs and MCP together; list-all under current transport limits, no capability/paging/event system. |
| **J4 — Delivery and acceptance** | Approve the bounded S2–S4 file scope and mapped checks below; after human proposal merge, one authorization carries implementation to a review-ready PR under 0080. Preserve actual full-PR gate selection, evidence/failure history and human final merge. Current instruction ends at S1. |

## Verification Checklist

**This proposal only:**

- [ ] Link immutable main/source evidence and #324 merge in the PR; no numbering
  collision; Spec/Plan In Review and ADR Proposed. Exactly the five S1 docs,
  valid local links and `git diff --check`. Engine build, GPU and measurements N/A.

**Future final candidate (focused checks during work; complete remaining checks
on relevant final inputs, reusing only unaffected evidence):**

- [ ] **R1–R3 / native contract and host:** Windows Debug/Release tests cover all
  five types, imported sub-keys, unavailable/unparseable catalogs, empty success,
  UnknownAsset, schema/tool/direct-dependency fidelity, summary/detail agreement,
  exact GUID ordering rather than AssetId order, and catalog closure/content gating.
  Use the existing assembled catalog and temporary copies, not new production
  assets. All clients share the first loaded snapshot, even after disk changes,
  reconnect, scene open/apply and UI Refresh; a new host sees the replacement.
  An unavailable first load remains unavailable until restart. Copies outlive a
  connection without aliasing host records; lazy construction and read-only calls
  produce no authoring/World mutation, event, failure drain or apply.
- [ ] **R4 / transport, CLI and SDKs:** native Remote/CLI/native SDK and C# codec/
  live tests in Debug/Release; generated vectors include all types, schema-null/
  present, imported source strings, uint32 boundaries, both errors and malformed
  shapes/unknown variants. Verify new calls on a provider and NoAssets without
  one; existing methods and connect handshake remain usable with an actual old
  peer from the baseline, with binary/source provenance. CLI human/JSON, one-shot,
  REPL and `--exec` output agree with SDK values. No old-peer asset-call success
  is promised. Do not hand-edit JSONL or regenerate World bindings.
- [ ] **R4 / MCP:** exactly two new tools and otherwise unchanged inventory;
  parameter validation, exact forwarding/results, failures and source-schema null;
  explicit `2025-11-25` and `2026-07-28` sessions with their legacy/new result forms.
  Existing command failure settlement, subscriptions, apply and capture checks
  stay green. No direct native/file/Tools path or new package.
- [ ] **R5 / editor model and actual Windows UI:** headless tests for type/text
  filters (including no match), independent GUID selection, detail/dependency
  navigation, empty/unavailable/disconnected states, refresh preservation and
  rejecting obsolete callbacks. Product UI smoke confirms opening/list/detail,
  filters, clipboard GUID copy and repeated refresh without polling/history or
  Hierarchy-selection changes. Use the actual Avalonia window/worker with real
  Runtime; routed focus/key/button events are acceptable, state the input boundary.
  No screenshot, OS-input or latency claim without evidence.
- [ ] **R1/R4/R5 integration:** existing real Runtime + CLI/SDK/MCP/Editor harnesses
  on default and available actual Bistro builds. Compare every returned record
  and dependency to the same cooked catalog, including unreferenced and imported
  assets; every client observes the same list/details. Read-only browse/refresh
  leaves scene generation, GUIDs, names, selection/picking, frame data and PNG
  unchanged. Copy a known compatible catalog material GUID into the existing
  Inspector field, commit/apply, undo/redo and reject a later external conflict;
  the CLI/SDK reads the same authoring value. Save→reopen and save→rebuild→restart
  this reference edit on a disposable hand-authored source fixture; an imported
  scene's save still refuses. Preserve tracked assets; no new golden.
- [ ] **R6 builds/boundaries:** affected native and managed Debug/Release builds,
  including Runtime, CLI, both SDKs, Editor Model/App, MCP and relevant tests, with
  pinned/locked dependencies. Android `assembleDebug` for native portability,
  serialized with Windows builds; build-only, no new Android service/UI claim.
  Run affected module/package boundary and generator-staleness tests; inspect
  source registration/link lists and no new OS/dependency includes. Preserve the
  zero-NuGet SDK and existing Editor/MCP allow-lists/pins.
- [ ] **R6 GPU/gates:** existing default/Bistro headless/image and frame-input
  smoke on the reference environment with fatal clean Vulkan Validation. Reuse
  unchanged rendering checks with exact provenance; after a reference-edit test
  revert, compare the baseline frame/capture again. Evaluate the actual complete
  implementation PR with unchanged `--gate --applies`. Runtime/main and CMake
  registration edits are expected to select it: if selected, register the exact
  final source/configuration and run the original native Release profiling-ON,
  Tracy-OFF `atlantis_runtime --gate --json` once using its standard protocol and
  timeouts. No overrides, observer, retry-to-pass, baseline/policy/environment
  change or inherited #324/#307/#315 verdict. Retain every outcome; a non-pass is
  a specific blocker for human disposition. If genuinely unselected, record the
  whole-PR selector result/reason, not a timing pass.
- [ ] **Closure:** full diff within scope, no silent accepted-document rewrite,
  every requirement accounted for, failures/reuse/configuration/hardware and
  deviations in the implementation PR. Update as-built/navigation metadata.
  Review-ready only without unresolved required gates; otherwise Draft with a
  concrete remaining decision. No automatic merge.

## Rollback Plan

Revert implementation through human review; catalog/source/artifact formats need
no migration. Old clients continue using existing capabilities; remove the new
browser/tools with the endpoint rather than retaining a private fallback. Retain
design/acceptance history and do not rewrite Accepted ADRs.

## Definition of Done

Apply [Definition of Done](../process/definition-of-done.md); no repository-wide
rule changes. S1 completes at a jointly reviewable design PR with document checks.
Implementation completion requires J1–J4's explicit future authorization, Accepted
ADR-0143, human proposal merge, the applicable matrix and human final review/merge.
