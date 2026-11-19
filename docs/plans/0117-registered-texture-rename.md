# Plan 0117 — Rename a Registered Base Color Texture

- **Spec:** [Spec 0117](../specs/0117-registered-texture-rename.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending.
- **Related ADR:** [ADR-0168](../adr/0168-generation-scoped-asset-display-names.md) (Proposed)

## Objective and baseline

Deliver one rename, publish, restart and search flow for an existing
base color texture. Start from `917718cd`, the manual merge of
[0116 implementation PR #398](https://github.com/slmao/Atlantis/pull/398),
or its latest main descendant at implementation. Retain 0081's snapshot
boundary, 0095's canonical identities and 0116's original inbox.

## Milestones / task breakdown

1. **S1 — Joint design review.** Rule on J1–J4 and ADR-0168. Record
   approval and wait for the design to merge by hand.
2. **S2 — Shared contract and managed publication.** Add the optional read
   field and one guarded start request following existing client source
   compatibility conventions. Implement canonical metadata parsing and frozen
   validation, collision/staleness admission, copy-based preparation,
   commit/recovery and propagation in existing managed publish paths.
   Check unchanged inputs in the dedicated host tests. Keep rebuild
   history/status and transport refusal paths intact.
3. **S3 — Editor, CLI, SDKs and MCP.** Expose the start and status flow,
   using the same named generation and target guards. In Editor use 0093
   controls, 0114 name projection/search and the existing rebuild job
   owner. Show display and original path separately, including in the
   post-publication guidance. Keep dependency and preview behavior
   on the attached snapshot. Document the shared and downgrade contracts.
4. **S4 — Verification and implementation PR.** Run the mapped checks
   on the final candidate. Link actual Windows 100% evidence, commands,
   configuration and any valid reused evidence to the review-ready PR.
   Human merge only.

## Files / modules touched (expected)

| Files | Purpose |
| --- | --- |
| `src/runtime/{asset_rebuild_host.h,asset_rebuild_host.cpp,asset_host.cpp}` | Existing worker and publisher, registry and activation validation, immutable names in the discovery adapter. |
| `src/connection/include/atlantis/connection/asset_connection.h`, `src/connection/src/asset_connection.cpp` | DTO field, request, error vocabulary and optional start capability; reuse the existing job/status. |
| `src/remote/src/{codec.h,codec.cpp,remote_server.cpp,remote_client.cpp}` | Optional read field and the shared write method. |
| `src/cli/src/{command_layer.cpp,invocation.cpp}`, `src/gameplay_sdk/include/atlantis/gameplay/assets.h` | CLI help/commands and native facade. |
| `src/csharp/Atlantis.Gameplay/Assets.cs`, `src/csharp/Atlantis.Gameplay/Remote/{AssetCodec.cs,AssetSession.cs}` | Optional property and shared method, including connection error preservation. |
| `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolSchemas.cs}` | Closed write adapter through C# SDK, existing status and read mappings. |
| `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs,EditorVisuals.cs}`, `src/editor_app/Atlantis.Editor.Model/{AssetBrowseProjection.cs,AssetBrowser.cs,AssetReferencePicker.cs}` | Draft/intent, existing task status, name projection, and fixed source/inbox presentation. |
| `tests/runtime/{asset_rebuild_host_tests.cpp,asset_host_tests.cpp}`, `tests/remote/{remote_asset_tests.cpp,remote_codec_tests.cpp}`, `tests/cli/cli_asset_tests.cpp`, `tests/gameplay_sdk/compile_check_tests.cpp` | Host, native wire, client facade and CLI acceptance/refusal cases. |
| `tests/csharp/Atlantis.Gameplay.Tests/AssetCodecSuite.cs`, `tests/csharp/fixture_server/main.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `src/mcp/tests/Atlantis.Mcp.Tests/{Program.cs,IntegrationTest.cs}` | Managed boundary and legacy/malformed/unknown codecs, tool dispatch and transport evidence. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{AssetBrowserTests.cs,AssetReferencePickerTests.cs,InspectorTests.cs}` | Search/selection/focus guards and hosted Windows acceptance. |
| This Spec/Plan/ADR's approval metadata, `docs/specs/README.md` and `docs/architecture/remote_protocol.md` | Record review/status and describe the shared read/write contract. |

Asset System, build scripts, renderer, protocol version and package pins
do not need changes. Apply the repository's narrow scope approval
rule to any necessary file-list adjustment.

## Sequencing and dependencies

Validate storage and shared DTOs before wiring Editor actions. Use existing
0085/0095/0116 locks and failure ownership and 0081/0114 snapshot and search
behavior. The approved design and Accepted ADR must be merged before S2.

| Joint decision | Recommendation |
| --- | --- |
| **J1 — Name and identity** | Use a separately editable lowercase display name; reserve other managed color labels and their original tokens; keep paths, GUID and bindings unchanged (R1). |
| **J2 — Persistence and recovery** | Use the project map plus a frozen manifest copy, with reuse of verified outputs. Prove nonpublication before rollback and hold ambiguous writes for repair (R2–R3). |
| **J3 — Shared contract and compatibility** | Add a guarded start and optional list/get name, reuse job status, preserve legacy client reads. Only updated Runtime writers may use an in-place renamed workspace; runtimes without v8 require an earlier compatible workspace (R4, ADR-0168). |
| **J4 — Acceptance and delivery** | Accept the Windows 100% and shared/host checks below and authorize one S2–S4 execution after human approval and merge. |

Suggested authorization: “Approve Spec/Plan 0117 (J1–J4) and accept
ADR-0168 at the reviewed revision. After manual merge, authorize S2–S4
from latest main, including scoped fixes, applicable verification, valid
evidence reuse, and a review-ready PR. Keep the existing gates. Merge by
hand.”

## Verification checklist

- [ ] **R1/R3 — Admission and conflict.** Test another color texture's
  original token and assigned label, invalid form, same-name no-op, stale
  generation and registry/name drift, wrong role and two clients competing.
  Check no registry/assets mutation or publication on refusal. Show 0095
  registration cannot steal a reserved label and an earlier read-only
  generation never admits stale writes. Rename back to the original token
  without altering bindings.
- [ ] **R2/R3 — Frozen publication and recovery.** Verify exact before/after
  identity, source and output evidence; normal publications carry the map,
  including 0116 reimport, and an unrelated publisher (material save or
  texture Register/Resume). Target an altered frozen map, definitive
  nonpublication after map exchange, and an uncertain or damaged destination.
  Assert successful rollback only with proof; unverified state remains a
  write hold across restart and does not erase the old valid snapshot.
  Confirm known publication with later cleanup failure remains success.
- [ ] **R4 — Shared contract.** Verify native and managed codec parity,
  optional absence/presence, bounds, malformed values and old client/new
  host versus new client/old host. Exercise CLI/SDK/MCP write and readback
  against the same managed behavior, including typed refusals, transport
  errors and unknown settlement without silent resends. Confirm legacy
  named generations and seed discover with fallback on the upgraded host;
  the previous Runtime rejects the new v8 generation. Verify full list
  responses respect the existing limit.
- [ ] **R1/R2/R4 — Real Windows Editor, 100%.** Start in a named generation
  with a registered texture bound to a material; capture name, source,
  GUID, binding and preview. Rename, observe published ID and the old
  attached snapshot, then explicitly restart into that ID and find the
  texture by a label absent from the old path. Confirm name in list/detail,
  fixed path and GUID search, texture and material identities and unchanged
  preview. Update the original inbox PNG, reimport and restart into its new
  generation; confirm changed preview pixels, the renamed label and
  untouched binding. Record original and both resulting generation IDs,
  screenshots and pixel evidence. Exercise duplicate refusal and verify
  stale UI drafts do not submit when their asset or snapshot changes; test
  the stale wire request against a newer active generation in the host.
- [ ] **Existing gates.** Run affected Windows Runtime, Editor and shared
  client checks, and keep Vulkan validation clean during activation and
  preview. Assess golden evidence on the final inputs; the rename should
  leave the existing rendering output invariant, and reimport evidence
  demonstrates the expected texture change. Run the complete performance
  applicability check on the final diff and the existing protocol if
  selected (expected for native Runtime changes). The historical PR #398
  performance decision is not reusable as a gate disposition. The managed
  host and Editor are Windows-only; reassess shared native compilation
  applicability on the final diff; no Android UI acceptance is proposed.

## Rollback plan

Keep the previous complete named generation selectable and recover only
under ADR-0168. If a deployed workspace needs to return to a pre-0117
Runtime, use an earlier compatible workspace/legacy generation backed up
before rename. Do not run old writers against a workspace with overrides
or remove metadata while a publication outcome is uncertain.

## Definition of Done

Follow [the repository checklist](../process/definition-of-done.md).
Report observed compatibility and document any remaining operational
downgrade limits in the implementation PR.
