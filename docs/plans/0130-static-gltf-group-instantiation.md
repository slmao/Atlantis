# Plan 0130 — Import and Add a Static glTF Group

- **Spec:** [Spec 0130](../specs/0130-static-gltf-group-instantiation.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** J1–J4 approved for [873f1271](https://github.com/slmao/Atlantis/pull/423#issuecomment-7062867632) after joint review. The maintainer merged the design in #423. Implementation, verification and a review-ready PR authorized.
- **ADR:** [ADR-0179](../adr/0179-static-gltf-group-projection-and-instantiation.md) (Accepted)

## Objective

Register a bounded static storefront glTF and place its complete authored
structure into a hand-authored scene. Keep the group movable, undoable and
reopenable using ordinary authored nodes and shared operations.

## Milestones

1. **S1 — Joint review.** Resolve J1–J4, accept ADR-0179 and record explicit
   joint approval before implementation. Base the implementation on the
   latest `main` after the maintainer merges the design.
2. **S2 — Build, publish and describe.** Extend the managed producer's
   admission and validate its staged cooked group before committing the
   registration. Add the bounded typed asset read on all shared clients.
   Verify parity between activated named generations and clean managed
   builds. Exercise old clients, old hosts and interrupted jobs.
3. **S3 — Add and replay.** Compose the group as guarded ordinary scene
   commands. Adapt the existing 0108 verified suffix/history path for the
   new entry and label, retaining previous duplication semantics. Connect
   the Assets action and selection feedback. Verify write admission,
   settlement, conflicts and independent Apply recovery.
4. **S4 — Verify and open the implementation PR.** Run the Windows 100%
   storefront path and applicable gates. Link exact inputs and outputs,
   reuse valid unaffected evidence and report unresolved checks. Keep
   final merge with the maintainer.

S2 supplies the read-only description and portable fixture consumed by S3.
Both must settle before the Windows end-to-end run.

## Expected files and modules

| Files | Scope |
| --- | --- |
| `src/tools/gltf_importer/import_command.cpp`, `tests/tools/gltf_importer/import_identity_tests.cpp` | Bounded managed group admission and original-shape regression. Keep the existing import CLI, identity subkeys and cook outputs. |
| `src/runtime/{asset_host.cpp,asset_rebuild_host.cpp,static_group_projection.h}` (projection header new), `tests/runtime/{asset_host_tests,asset_rebuild_host_tests}.cpp` | Share bounded cooked projection between staged admission and the active snapshot query. Reuse the selected catalog, normal scene decoder and named generation commit rules. |
| `src/connection/include/atlantis/connection/asset_connection.h`, `src/connection/src/asset_connection.cpp`, `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}`, `docs/architecture/remote_protocol.md` | One typed read and refusal contract. No authoring codec, frame-loop or wire envelope changes. |
| `src/gameplay_sdk/include/atlantis/gameplay/assets.h`, `src/cli/{include/atlantis/cli/command_layer.h,src/command_layer.cpp,src/invocation.cpp}`, `src/csharp/Atlantis.Gameplay/{Assets.cs,Remote/AssetCodec.cs,Remote/AssetSession.cs}`, `src/mcp/Atlantis.Mcp/{ToolRegistry,ToolSchemas}.cs` | Shared query through each normal client. Reuse existing guarded transaction and failure drain. |
| `tests/{cli/cli_asset_tests.cpp,remote/remote_asset_tests.cpp,csharp/conformance/conformance_tests.cpp,csharp/conformance/codec_vectors.jsonl,csharp/Atlantis.Gameplay.Tests/AssetCodecSuite.cs}`, `src/mcp/tests/Atlantis.Mcp.Tests/{M3Tests,SessionChecks}.cs` | Protocol parity, client outcomes, tool coverage and existing import/list/get compatibility. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession}.cs`, `src/editor_app/Atlantis.Editor.Model/{AssetBrowser,HierarchyCommands,StructuralEdits,EditHistory}.cs` | Group action, stale-context fences, guarded append and reuse of one suffix history entry. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{AssetBrowserTests,HierarchyTests,HistoryTests,Program,InspectorTests,StaticGroupUiTests}.cs` (UI test new) | Model coverage and focused hosted Windows fixture. Use the existing native suite entry. |
| Spec/Plan 0130, ADR-0179 and `docs/specs/README.md` | Review record and navigation. |

The fixture lives outside the product tree. No new checked-in art, package,
asset format, build target, CI job or performance baseline is proposed.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Source and budget** | Approve one self-contained static glTF with bounded emitted nodes and geometry and opaque untextured PBR factors. Keep 0086 registration, derived GUIDs, immutable sources and same-name Resume. |
| **J2 — Shared read** | Approve `assets.staticGroup` and ADR-0179. Use the current cooked Scene and same-snapshot typed assets. Keep descendant names outside the first release. Preserve old client and managed artifact behavior. |
| **J3 — Scene edit** | Approve one root placed at origin and one existing 0108 guarded transaction/history step. Use the existing Move Gizmo after creation. Keep import, transaction and Apply failure handling independent. |
| **J4 — Acceptance and authorization** | Approve the storefront fixture and checks below. After joint approval, ADR acceptance and maintainer merge of the design, authorize one continuous S2–S4 execution with in-scope fixes, applicable verification and a review-ready PR. |

## Verification checklist

- [ ] **Importer and registration (R1).** Exercise nested transforms,
  multiple roots, shared mesh instances, multiple primitives and distinct
  material factors. Cover explicit versus absent material, a rejected
  material/transform or resource and the 63/64 emitted-node boundary.
  Check the geometry budget before expansion. Repeat the original single
  mesh acceptance. Verify exact producer identities, catalog collision and
  closure, known invalid input before registration, committed registration
  with publication failure and same-name `Resume`, and the existing
  unknown-result/repair hold. Compare IDs after rebuilding cleanly.
- [ ] **Shared read and activation (R2).** Compare ordered node values,
  parent indices, local TRS and nullable material GUID between native and
  managed codecs and through CLI, SDKs and MCP. Refuse bad scene identity,
  nonmanaged records, malformed or oversize artifacts and missing/mismatched
  typed dependencies with zero partial nodes. Cover a legacy client on the
  new host, a new client on the old host, catalog snapshot isolation and
  explicit availability in the activated generation. Verify the existing
  MCP exact transaction plus `authoring.drain_failures` path without new
  compound write tools.
- [ ] **Admission and history (R3–R4).** Check selection or document
  invalidation during the read and before send, a host revision race, an
  unsupported host and transaction refusal with zero partial writes and
  zero history or Apply. Cover lost receipt, known commit without verified
  history, independent Apply failure and explicit recovery without a second
  Add. Verify whole-block Undo/Redo, reinserted GUIDs and conflicts involving
  external edits, added children, replaced incarnations and append slots.
- [ ] **Windows 100% storefront (R1–R4).** In a disposable trusted checkout,
  use one static glTF with a wall, nested window frames and an awning, three
  distinct opaque material assignments and non-default local transforms.
  Record source and cooked identities, hierarchy, values, generation and
  scene sources. From Assets register the inbox, wait for publication and
  confirm the original catalog remains unchanged. Activate the named
  generation in a supported development Runtime and confirm the Scene row
  and group query. Use [0090](../specs/0090-saved-scene-build-open.md)
  Build/Open with the same committed managed registry to enter an empty
  hand-authored saved slot with a camera. Add the group there, verify one
  new root and exact children, use one Undo and Redo
  and compare the same node GUIDs. Move the root with the ordinary Gizmo and
  verify children's local values stay equal while their world positions move
  together. Save and reopen the same source and check its ordered links,
  transforms, mesh/material references and visible appearance. Build/Open
  again from the same saved source and committed managed registry,
  and confirm an independent Editor starts with the saved result. Select the
  managed inputs at build time for the saved slot. The saved-slot Runtime
  does not take a named-generation switch. Keep native captures of the
  added group and reopened result alongside structural readback. Run the
  invalid-input and stale-add checks above to show the two failure boundaries.
- [ ] **Applicable gates.** Run affected importer, Runtime, CLI, SDK, MCP
  and Editor builds/tests plus Windows Vulkan validation on the hosted
  storefront. Evaluate golden, headless and platform applicability from the
  final changed inputs per the existing testing strategy. Record
  `atlantis_runtime --gate --applies` on the final diff and execute every
  applicable gate. Keep prior raw failures and the existing baselines.

## Rollback

Revert the new query and Editor entry. Saved instances are ordinary scene
nodes. Preserve the registered sources and known usable cooked catalog.
Older managed import tools may refuse to rebuild the widened group sources.
Use the existing publication/Resume recovery and retained products when
rolling back. Follow the [Definition of Done](../process/definition-of-done.md).
