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
| **J1 — First source and trust boundary** | Approve one untextured static-mesh `.gltf` with embedded buffers, the size cap and the trusted local inbox. Expose only a logical name and closed mode to clients. |
| **J2 — Identity and pipeline** | Approve project-owned committed source units, one explicit v4 mint, existing v8 derivation and deterministic effective catalog source. Require old identity preservation and additions only from the requested root. |
| **J3 — Recovery and activation** | Approve the shared worker and two commits, retained registration with explicit resume, and generation-local validation with explicit restart. Accept that a saved scene requires the project registry and compatible generation. |
| **J4 — Shared delivery** | Approve the start/status and compatibility rules. Authorize S2–S4 together after the design merge, with the existing verification and human merge gates. |

### Suggested joint authorization

> I approve J1–J4 and jointly approve Spec/Plan 0086 at the reviewed revision.
> I accept ADR-0147. After this design PR is manually merged, proceed from
> current main through S2–S4, including applicable verification and a
> review-ready implementation PR. Ordinary in-scope fixes and reuse of
> valid unchanged-input evidence are authorized. Existing gates, deviation
> rules and human final merge apply.

## Verification Checklist

- [ ] **S1 design.** Check numbering, navigation, cross-links, status and
  scope of the documentation-only PR.
- [ ] **R1 source boundary.** Import the existing fixture shape with one
  embedded untextured primitive. Refuse an external buffer or image before
  opening it, oversize input, unsupported geometry, traversal, case-alias
  and Windows reparse inputs. Verify the inbox and checkout are unchanged.
- [ ] **R2 identity and build.** Use the real configure, importer,
  cook-manifest and assembly. Check the persisted root GUID and derived
  mesh/scene identities. Reject a name conflict and injected catalog
  identity or key collision before registration. In a clean opt-in build,
  consume committed sources only and obtain the same GUIDs. Check the
  default build's registration inputs are unchanged.
- [ ] **R3 recovery and activation.** Exercise overlapping import/rebuild
  and a second host sharing the import workspace. Inject failures before
  registration, between the two renames and after publication with a lost
  receipt. Check the actual committed directory, prior generation
  integrity, dirty recovery and explicit resume with the original GUID.
  Confirm old and new generation selection after later registration, and
  verify a legacy Spec 0085 profile/generation can still start.
- [ ] **R3 scene path.** Start from a disposable hand-authored scene with
  a known compatible material. The old session, including a first
  discovery query after publication, must omit the new mesh. Explicitly
  restart, find it through shared list/get and the typed Inspector
  picker, then select it and observe successful apply/reload and a rendered
  instance. Exercise undo/redo and save the selected GUID. Reopen and apply
  it in a fresh session using the same generation. Check saved source
  and generation bytes separately. Reuse unchanged-input failure recovery
  evidence for existing apply semantics.
- [ ] **R4 clients.** Compare native/managed codec vectors and the same
  admitted job and terminal data across CLI, both SDKs, MCP and Editor.
  Verify closed inputs, known/unknown outcomes, disconnect or a late
  callback, and failure payload/exit behavior. Cover old clients on the
  new host and new calls' narrow fallback on the pinned older host while
  list/get and authoring remain usable.
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
