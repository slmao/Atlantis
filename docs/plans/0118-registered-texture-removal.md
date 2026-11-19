# Plan 0118 — Remove an Unused Registered Base Color Texture

- **Spec:** [Spec 0118](../specs/0118-registered-texture-removal.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Atlantis maintainer, 2026-11-20, [PR #401](https://github.com/slmao/Atlantis/pull/401) at `3ffa6d55`: approved Spec/Plan 0118 and J1–J4, accepted ADR-0169, and authorized S2–S4 after manual merge `5c7fb5a1`. Both Windows 100% workflows and applicable gates retained.
- **ADR:** [ADR-0169](../adr/0169-prove-unused-before-removing-managed-textures.md) (Accepted)

## Objective

Deliver one explicit remove-registration operation with authoritative
reference checks, safe publication and shared client access.

## Milestones

1. **S1 — Joint review.** Rule on J1–J4, accept ADR-0169 and merge the
   design by hand before implementation.
2. **S2 — Proof and publication.** Implement the bounded check on the
   existing managed worker and reconcile declarations, recipes and
   generated inputs. Add the private staging omission and exact catalog
   delta. Connect confirmation, rechecks and recovery to the existing
   locks and publication path. Exercise failure injection before client
   integration.
3. **S3 — Clients and Assets.** Expose the typed check, confirm and status
   surfaces across clients. Wire the existing Assets job owner and 0093
   confirmation styling. Document the supported proof domain and
   diagnostics.
4. **S4 — Verification and PR.** Run the mapped checks on the final
   candidate, link raw evidence and applicable gate results, and submit
   the implementation PR for human review.

## Expected files

| Files | Purpose |
| --- | --- |
| `cmake/AtlantisManagedImports.cmake` | Reuse ordinary configure to verify the current producer roster, accept the private one-texture omission and preserve the selected saved-slot refusal. |
| `src/runtime/{asset_rebuild_host.cpp,asset_rebuild_host.h,asset_host.cpp,asset_host.h}` | Worker check, source reconciliation, confirmation, candidate validation, and joint registry/name-map recovery. |
| `src/connection/include/atlantis/connection/asset_connection.h`, `src/connection/src/asset_connection.cpp`, `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}`, `src/remote/include/atlantis/remote/remote_client.h` | Shared calls, bounded receipt/readback and additive transport. |
| `src/cli/src/{command_layer.cpp,invocation.cpp}`, `src/gameplay_sdk/include/atlantis/gameplay/assets.h`, `src/csharp/Atlantis.Gameplay/Assets.cs`, `src/csharp/Atlantis.Gameplay/Remote/{AssetCodec.cs,AssetSession.cs}` | Shared check/confirm commands and client facades. |
| `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolSchemas.cs}` | Exact tools using the C# SDK. |
| `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs,EditorVisuals.cs}`, `src/editor_app/Atlantis.Editor.Model/AssetBrowser.cs` | Assets action, typed blocker feedback, confirmation guards and existing job settlement. |
| `tests/runtime/{asset_rebuild_host_tests.cpp,asset_host_tests.cpp}`, `tests/remote/{remote_asset_tests.cpp,remote_codec_tests.cpp}`, `tests/cli/cli_asset_tests.cpp`, `tests/gameplay_sdk/compile_check_tests.cpp` | Source and publication fixtures, native compatibility and shared calls. |
| `tests/csharp/Atlantis.Gameplay.Tests/AssetCodecSuite.cs`, `tests/csharp/fixture_server/main.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `src/mcp/tests/Atlantis.Mcp.Tests/{Program.cs,IntegrationTest.cs,M3Tests.cs,SessionChecks.cs}` | Managed contract parity, refusal and lost receipt cases, and closed MCP tool discovery. |
| `src/editor_app/tests/Atlantis.Editor.Tests/AssetBrowserTests.cs`, `docs/architecture/remote_protocol.md`, approval metadata and `docs/specs/README.md` | Assets intent regressions, protocol reference and navigation. |

Use the existing source parser and importer. Reconcile the closed roster
before building the removal candidate. Reuse 0085 admission and locks, 0095
sealed registration, 0116 recovery and 0117 name-map rules. Report small
file-list adjustments under the reviewed scope rule.

## Joint decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and references** | Recommend approval. Limit removal to one registered sRGB PNG under the ordinary managed profile. Keep the original PNG. Require the current unmasked producer graph and authoritative references; refuse known edges or incomplete coverage (R1–R2). |
| **J2 — Confirmation and settlement** | Recommend approval. Bind confirmation to exact checked inputs, build with a one-texture omission, and settle the sealed unit, name override and temporary selector with publication. Hold ambiguous workspaces for repair (R3). |
| **J3 — Shared UX and compatibility** | Recommend approval. Use one host-scoped, expiring check receipt, explicit confirmation and existing job status across all clients. Activate by explicit restart (R3–R4). |
| **J4 — Verification and authorization** | Recommend approval. Keep both Windows 100% paths and existing applicable gates. Authorize one S2–S4 execution after joint approval, ADR acceptance and manual design merge. |

Suggested authorization after joint review and the design merge. Execute
S2–S4 from latest main, including scoped fixes, applicable verification,
valid evidence reuse and a review-ready PR. Preserve the existing gates.
Final merge remains with the maintainer.

## Verification

- [ ] **R1–R2 — Scope and complete proof.** Refuse ineligible role,
  selected saved-slot profile, seed, historical snapshot and mismatched
  sealed identity. Cover a catalog edge, a source-only managed recipe
  edge hidden by stale cooked/staging evidence, and a case with fresh
  importer evidence. Include base color and emissive. Show that a
  material used only by a different saved scene still blocks removal.
  Detect a current declaration or producer absent from the frozen
  inventory. Test malformed, unreadable, missing, duplicate, stale
  generated and over-budget inputs. Preserve a known blocker when
  another input is incomplete. Cover an unexpected file in the
  registered unit, a proven absent inbox, and an inbox alias to the
  registered unit.
- [ ] **R3 — Confirm and publish.** Change the contents of a checked recipe
  without adding an edge, add a reference-bearing input after review, and
  add a reference after admission but before commit. Include a byte
  change with the original timestamp. Require stale refusal and no
  registry, name or named generation change. Check receipt expiry,
  single admission on repeated confirmation, supersession and cross-host
  refusal. Validate the exact catalog delta, surviving edges/files and
  name-map bytes. Exercise deadline, rollback between the unit move and
  name-map update, damaged prepared/final evidence, uncertain publication,
  and verified success with failed cleanup. Check selector restoration
  on refusal and rollback, and verify an unprovable reset retains evidence
  and blocks writes after restart. Confirm prior-generation readability.
- [ ] **R4 — Shared contract.** Verify old client/new host behavior, explicit
  Unsupported on old hosts, codec/vector parity and bounded typed results.
  Use separate authorized CLI/MCP connections for check and confirm.
  Verify known-ID status and original connection errors for lost receipts,
  no automatic resend, and Editor selection/reload/connection invalidation.
  An old snapshot can still read and preview, but cannot save a material
  against the now-obsolete registry.
- [ ] **R1, R3–R4 — Windows 100% removal.** With a registered unused
  texture that has an 0117 override, check, confirm and record the
  published ID. Verify the attached snapshot still contains it. Explicitly
  restart into that ID, search by label and GUID in Assets, and verify
  it is absent there, from `assets.get` and from the reference chooser.
  Compare the inbox PNG hash and surviving assets/labels against the
  starting state. Record the two generations and actual Windows evidence.
- [ ] **R2, R4 — Windows 100% referenced refusal.** Attempt removal of
  a registered texture bound to a material. Verify the blocking material
  is shown and confirmation is unavailable. Compare registry bytes,
  name map and original inbox hash before and after refusal. Use the
  same material and mesh to verify the binding and preview are unchanged.
  Verify no new named generation and restart the original generation
  with the same usable binding and preview. Run the Vulkan validation
  layers for both live paths.
- [ ] **Existing gates.** Build and run affected Runtime, asset pipeline,
  Editor and shared client checks. Assess native shared compilation and
  golden applicability using the final inputs. Use `--gate --applies`
  on the final diff and retain the existing protocol if selected. Windows
  100% is the only DPI acceptance target.

## Rollback and definition of done

Keep earlier named generations for read-only use and use ADR-0169 for
offline recovery of uncertain writes. A new registration is a separate
operation and does not restore the removed GUID. Satisfy the repository
[definition of done](../process/definition-of-done.md) and link validation
and any compatibility limits from the implementation PR.
