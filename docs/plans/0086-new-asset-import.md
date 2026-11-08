# Plan: Managed Import and Registration of New Assets

- **Spec:** [Spec 0086](../specs/0086-new-asset-import.md)
- **ADR:** [ADR-0147](../adr/0147-managed-import-registration-and-publication.md) (Proposed)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending

## Objective

Register one self-contained static-mesh glTF source through a shared job,
publish it in an isolated generation and use its derived mesh GUID in a
saved hand-authored scene.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Resolve J1–J4 and record the decision and ADR
   acceptance on the reviewing revision.
2. **S2 — Registration, build and publication.** Implement the restricted
   input mode, committed/pending registration units and opt-in declarations.
   Extend the existing Windows coordinator with admission, validation,
   registration, resume and generation-local verification. Prove a clean
   opt-in build and explicit restart using the real producer.
3. **S3 — Shared clients and scene use.** Carry the same typed operations
   across Connection, Remote, both SDKs, CLI, MCP and the Assets panel.
   Use existing discovery, Inspector and authoring flows after activation.
4. **S4 — Verify and submit.** Run the checks below. Record the exact
   candidate, gates and evidence reuse in a review-ready implementation PR.

## Files / Modules Touched (expected)

| Responsibility | Expected scope |
|---|---|
| Build and importer | `assets/CMakeLists.txt`, `src/asset_system/CMakeLists.txt`, and a private managed-import declaration include under `cmake/` if needed. Extend the existing `atlantis_add_imported_scene` input options for restricted import without changing ordinary imports. `src/tools/gltf_importer/{main.cpp,import_command.h,import_command.cpp}` for the guard before buffer/resource load. Reuse Asset System catalog source parsing, serialization and GUID mint, the cooker and assembly. |
| Windows executable composition | `src/runtime/{asset_rebuild_host.h,asset_rebuild_host.cpp,asset_host.h,asset_host.cpp,main.cpp,CMakeLists.txt}` for the existing coordinator, source registration, generation validation and activation. If split for readability, allow one executable-private `managed_import.{h,cpp}` pair with its existing target registration. Keep `src/runtime/src/` and the frame loop outside the change. |
| Shared client surface | Connection asset values in `src/connection/{include/atlantis/connection/asset_connection.h,src/asset_connection.cpp}`. Existing Remote asset sections in `src/remote/src/{codec.h,codec.cpp,remote_client.cpp,remote_server.cpp}`. CLI asset dispatch in `src/cli/src/{command_layer.cpp,invocation.cpp}` and the native SDK asset facade. |
| Managed clients | `src/csharp/Atlantis.Gameplay/{Assets.cs,Remote/AssetCodec.cs,Remote/AssetSession.cs}`, MCP asset tools and closed schemas in `src/mcp/Atlantis.Mcp/`, and Assets UI/worker in `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs}`. Reuse local Runtime launch options, Inspector and authoring contracts. |
| Verification | Focused existing tests under `tests/{tools/gltf_importer,asset_system,connection,remote,cli,gameplay_sdk,runtime,csharp}` and `src/{editor_app,mcp}/tests/`. Extend the existing Runtime rebuild fixture and importer test builder. Use a disposable Windows project, scene, build and publication tree for the live path. |
| Documentation | Spec/Plan approval metadata and ADR acceptance, spec index and blueprint navigation, `README.md`, `docs/architecture/{module_boundaries.md,remote_protocol.md}`. |

The scope excludes package and dependency changes, Android packaging changes,
rendering and World changes, approved ADR rewrites, asset format changes,
committed fixtures from user projects and golden rebaselines. Classify and
record deviations in the implementation PR.

## Sequencing & Dependencies

Start S2 only after joint approval and human merge of this design PR.
Specs 0047 and 0081–0085 provide the pipeline, identity, discovery, selector,
authoring outcomes and rebuild baseline. Establish crash/retry behavior and
generation compatibility before exposing the operation in the Editor.

| Joint decision | Recommendation |
|---|---|
| **J1 — First source and trust boundary** | Approve the 16 MiB untextured, indexed static-mesh `.gltf` subset with one verified embedded buffer. Keep source placement local and expose only a logical name and closed mode to clients. |
| **J2 — Identity and pipeline** | Approve project-owned registration units, explicit v4 mint, existing v8 derivation and deterministic catalog-source composition. Require every old identity plus exactly the validated producer fragment. Accept Spec 0085's whole-graph artifact behavior. |
| **J3 — Recovery and activation** | Approve the shared worker, staging and project markers, ordered registration/publication renames, and explicit resume with the original GUID. Use frozen generation metadata and explicit restart. Keep the project registration with any saved dependent scene. |
| **J4 — Shared delivery** | Approve shared start/status, typed settlement and narrow legacy fallback. Reuse discovery and scene authoring after activation. Authorize S2–S4 together after manual design merge, with the existing verification and human merge gates. |

### Suggested joint authorization

> I approve J1–J4 and jointly approve Spec/Plan 0086 at the reviewed revision.
> I accept ADR-0147. After this design PR is manually merged, proceed from
> current main through S2–S4, including implementation, applicable verification
> and submission of a review-ready implementation PR. Ordinary in-scope fixes
> and reuse of valid unchanged-input evidence are authorized. Existing gates,
> deviation rules and human final merge apply.

## Verification Checklist

- [ ] **S1 design.** Check numbering, navigation, cross-links, status and
  scope of the documentation-only PR.
- [ ] **R1 source boundary.** Import the existing fixture shape with one
  embedded untextured indexed primitive. Refuse an external buffer or image
  before opening it, oversized input, invalid base64 or mismatched decoded
  length, unsupported geometry, traversal, case aliases and Windows
  reparse inputs. Verify inbox and checkout hashes stay unchanged.
- [ ] **R2 identity and build.** Use real configure, importer,
  cook-manifest and assembly. Compare the full staged delta with the
  expected fragment and recompute mesh/scene GUIDs from the persisted
  root. Refuse duplicate name and injected unrelated addition, missing
  record, old identity change or GUID/key collision before either rename.
  In a clean opt-in build with no inbox dependency or pending selection,
  reproduce the registered identities. Confirm the ordinary build's inputs
  and the next managed rebuild's full identity comparison.
- [ ] **R3 recovery and activation.** Exercise overlapping import/rebuild
  and a second host sharing the project with a distinct staging tree.
  Inject failure before registration, between the renames, and process
  loss after publication before receipt. Check the committed directory,
  selectable generations and dirty markers, including repair through the
  original workspace. Verify duplicate register after commitment refuses.
  Remove the inbox copy in the fixture and resume from the committed
  source with the original GUID. Verify a known complete generation can
  be selected after receipt loss and that a stale pending selection never
  participates in a later job. After another registration, start the
  older generation for discovery and confirm its mutation guard. Verify
  legacy Spec 0085 profile/generation startup.
- [ ] **R3 scene path.** Use a disposable hand-authored scene with a known
  compatible material and distinguishable original mesh. Before restart,
  list/get must omit the new mesh even if discovery initializes after
  publication. Start the selected generation and find the new GUID through
  list/get and the typed Inspector picker. Confirm selection and wait for
  the correlated successful Runtime reload. Observe the new rendered mesh,
  undo to the original GUID, then redo to the imported GUID. Save and check
  the literal GUID in the hand-authored source and that cooked generation
  files are unchanged. Start another session on that generation, reopen
  the saved source and apply it, then verify the same mesh resolves and
  renders. Reuse unchanged-input failure recovery evidence for existing
  apply semantics.
- [ ] **R4 clients.** Compare native/managed codec vectors and the same
  admitted job and terminal data across CLI, both SDKs, MCP and Editor.
  Cover a mismatched start echo or status ID, unknown job, lost start
  response, late session callback and terminal failure payload/exit
  behavior. Check exact `Unsupported` fallback on the pinned 0085 host,
  plus a different malformed reply that follows normal protocol failure.
  Check an old client on the new host and a new client without a profile.
  Verify list/get, rebuild and authoring remain usable where supported.
- [ ] **R5 builds and gates.** Run affected Windows Debug/Release and
  applicable Android build checks, focused native/managed suites and live
  Vulkan validation. Run or justify reuse of applicable headless/golden
  evidence. Run the full-diff performance applicability selector and
  follow the original [performance contract](../process/performance-contract.md)
  for any selected gate. Preserve failures and distinguish fixture outputs
  from committed references.

## Rollback Plan

Stop managing imports and retain the registered input directories and
generations offline. Select an older usable generation with a compatible
scene source or restore the prior scene reference. A saved GUID that depends
on a managed import requires a generation or an opt-in build containing it.
No automatic deletion or source rewrite is part of rollback.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR links the joint ruling, maps R1–R5 to evidence, and
records applicable gates and any reviewed deviations. Merge is manual.
