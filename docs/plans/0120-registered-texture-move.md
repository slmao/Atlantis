# Plan 0120 — Move a Registered Base Color Texture in Assets

- **Spec:** [Spec 0120](../specs/0120-registered-texture-move.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Atlantis maintainer, 2026-11-20, [PR #405](https://github.com/slmao/Atlantis/pull/405) at `491e5b0c`: approved Spec/Plan 0120 and J1–J4, accepted ADR-0170 and authorized S2–S4 after manual merge `87e9beb9`.
- **Related ADR:** [ADR-0170](../adr/0170-generation-scoped-texture-locations.md) (Accepted)

## Objective and sequence

Deliver one Assets Move, publish, restart, locate references and reimport
workflow. Use the current main descendant of [PR #404](https://github.com/slmao/Atlantis/pull/404)
(`42ef1719`) after design approval and manual merge.

1. **S1 — Joint review.** Rule on J1–J4, accept ADR-0170 and record
   authorization before implementation.
2. **S2 — Runtime authority and shared contract.** Extend the existing
   managed publisher, validation and recovery to cover the path map.
   Implement move admission and settle its job through the existing status.
   Carry the authority through all managed writes, including removal's
   rollback, and support legacy activation as specified. Add native and
   managed codecs and tests.
3. **S3 — Client wiring.** Add the CLI, SDK and MCP accessors and diagnostics.
   In Assets, add a single-texture Move dialog with current location,
   destination and original inbox guidance. Reuse the current selection
   intents, job presentation, path search and Inspector/Assets read paths.
   Add Editor model and hosted interaction checks. Keep View References on
   its 0119 read-only contract.
4. **S4 — Verify and submit.** Run applicable checks on the final candidate,
   link fresh or reusable evidence in the implementation PR and leave the
   final merge to the maintainer.

## Expected implementation files

| Files | Responsibility |
| --- | --- |
| `src/runtime/{asset_host.cpp,asset_rebuild_host.cpp,asset_rebuild_host.h}` | Immutable discovery, guard, persistence, activation, and joint recovery. |
| `src/connection/include/atlantis/connection/asset_connection.h`, `src/connection/src/asset_connection.cpp`, `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}` | Closed request/refusals and optional read field. |
| `src/cli/src/{command_layer.cpp,invocation.cpp}`, `src/gameplay_sdk/include/atlantis/gameplay/assets.h`, `src/csharp/Atlantis.Gameplay/Assets.cs`, `src/csharp/Atlantis.Gameplay/Remote/{AssetCodec.cs,AssetSession.cs}`, `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolSchemas.cs}` | Shared clients and status, including receipt loss. |
| `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs,EditorVisuals.cs}`, `src/editor_app/Atlantis.Editor.Model/{AssetBrowser.cs,AssetBrowseProjection.cs}` | Move intent and form, path search/details, independent reimport guidance. |
| `tests/runtime/{asset_rebuild_host_tests.cpp,asset_host_tests.cpp}`, `tests/remote/{remote_asset_tests.cpp,remote_codec_tests.cpp}`, `tests/cli/cli_asset_tests.cpp`, `tests/gameplay_sdk/compile_check_tests.cpp` | Publication, recovery, client parity and refusal coverage. |
| `tests/csharp/Atlantis.Gameplay.Tests/AssetCodecSuite.cs`, `tests/csharp/fixture_server/main.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `src/mcp/tests/Atlantis.Mcp.Tests/{Program.cs,IntegrationTest.cs,M3Tests.cs,SessionChecks.cs}` | Wire and adapter checks. Count assertions change only for the new tool. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{AssetBrowserTests.cs,InspectorTests.cs}` | Path/filter and intent coverage, Windows acceptance. |
| `docs/architecture/remote_protocol.md`, `docs/specs/README.md`, approval metadata for this Spec, Plan and ADR | Record contract and approvals and link execution evidence. |

There is no planned Asset System, renderer, CMake, library version or
package change. Use the repository's narrow approval rule for any needed
file-list adjustment.

## Joint decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Location and identity** | Move a single existing base color texture to a different virtual Assets folder, defaulting its leaf to the current leaf and allowing an explicit destination leaf. Keep the 0117 label, catalog `source`, GUID, slots and original inbox. Reserve canonical full paths in the registered base color namespace (R1). |
| **J2 — Frozen publication and failure** | Accept the separate canonical map and v9 activation. Clone verified outputs, guard all managed writes and extend 0118 joint recovery. Restore only with proof of nonpublication and retain a write hold on ambiguity (R2, ADR-0170). |
| **J3 — Shared contract and UI** | Use one additive shared start, optional list/get path and existing job status. Require updated writers for a workspace with locations. Reuse Assets search and original-inbox reimport rather than adding a directory manager (R3). |
| **J4 — Verification and authorization** | Require the Windows 100% workflow and applicable gates below. Authorize S2–S4 once after joint approval, ADR acceptance and manual design merge. |

Suggested authorization after review. Approve Spec/Plan 0120 and J1–J4
at the reviewed revision and accept ADR-0170. After manual design merge,
authorize S2–S4 from latest main, including implementation, scoped fixes,
applicable verification, valid evidence reuse and a review-ready PR.
Final merge remains with the maintainer.

## Verification checklist

- [ ] **R1 — Identity and refusal.** Test exact fallback and moved paths,
  grammar boundaries, path equality, same-folder leaf changes and
  unsupported roles. Use two registered textures with different tokens
  and a shared logical leaf in different folders. Move one to the other's
  full path and verify distinct conflict and byte-identical registry,
  location map and generation inventory. Cover stale generation, stale
  expected path, changed registered bytes and destination claimed after
  admission. Verify busy exclusion and a consistently read old snapshot.
- [ ] **R2 — Prepare, publish, recover.** Verify byte-identical cloned
  catalog, producer inventory, material sources, all artifact evidence and
  display names. Exercise corrupted staging, wrong or damaged final,
  timeout before mutation, confirmed nonpublication, rolled-back map,
  unknown publish and cleanup hold across restart. Exercise a known
  confirmed success after a lost publish acknowledgement. Verify all
  managed write paths preserve locations, registration and Resume respect
  authority, and removing a moved unused texture removes only its location
  while preserving the original inbox and 0118 reference proof.
- [ ] **R3 — Shared compatibility.** Check list/get parity and strict
  optional-field decoding, SDK/CLI/MCP parity, legacy host Unsupported,
  legacy client read, v1–v8 activation and v9 refusal on an older Runtime.
  Cover unsent, unknown sent receipt and returned job-ID settlement.
  Verify connection/list/selection invalidation before send and independent
  operation settlement after send.
- [ ] **R1–R3 — Windows 100% actual Editor.** Use one eligible moved texture
  bound to both Base Color and Emissive of a material. Read the old path
  while attached, publish via Move, and explicitly restart into the new
  named generation. Search the exact new path and confirm the selected GUID
  and original source and display label. Search the previous logical path
  and verify that result is absent from the current snapshot. Use View
  References to focus both original material slots. Edit the PNG in the
  indicated original inbox, use Reimport PNG, publish and explicitly
  restart. Verify changed preview pixels with the moved path, GUID and both
  slots retained. Attempt a collision and a stale request and check the
  refusal with no added generation or partial state. Use shared client
  readback where needed to drive a second stale client. Record native
  screenshots, generation IDs, exact paths and before/after readback.
- [ ] **Applicable gates.** Build and run affected Windows Runtime,
  Editor and shared client checks. Exercise the live flow with clean Vulkan
  validation. Determine Android and image/golden applicability from the
  final shared/native diff and unchanged inputs. Run `--gate --applies`
  on the final diff and retain the standard gate when selected. No DPI
  target beyond 100%.

## Rollback and definition of done

For an ordinary logical placement change, publish an explicit move back to the
previous valid path on the updated writer. Keep complete prior generations.
Use ADR-0170 for ambiguous writes and downgrade. Meet the repository
[definition of done](../process/definition-of-done.md) and link final evidence
from the implementation PR.
