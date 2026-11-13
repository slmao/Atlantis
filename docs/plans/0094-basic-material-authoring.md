# Plan: Basic Material Creation and Editing

- **Spec:** [Spec 0094](../specs/0094-basic-material-authoring.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The maintainer approved J1–J4 and Spec/Plan 0094 at `9bc75f0f` on 2026-11-13. [PR #353](https://github.com/slmao/Atlantis/pull/353) was manually merged as `67aa7743`. Authorization covers S2–S4, scoped fixes, applicable verification, valid evidence reuse and a review-ready implementation PR. Final merge remains human.
- **ADR:** [ADR-0153](../adr/0153-managed-material-authoring-and-draft-preview.md) (Accepted)

## Objective

Deliver one complete managed material workflow through the existing asset
surface and Windows Editor, from draft and fixed preview to a published
generation and a persistent scene reference.

## Milestones / Task Breakdown

1. **S1. Joint review.** Settle J1–J4 and record Spec/Plan approval and ADR
   acceptance. Start from current `main` after the human design merge.
2. **S2. Shared operations and producers.** Implement managed source
   registration/replacement, frozen input comparison and recovery in the
   existing worker. Add the shared asset methods and typed client bindings.
   Reuse the cooker and pair preview with fixture tests before adding UI.
3. **S3. Assets workflow.** Add the modal and its local draft/settlement
   state. Reuse the catalog selectors, preview worker, 0093 resources and
   ordinary generation launch. Exercise the real scene reference path.
4. **S4. Verification and implementation PR.** Complete the checklist on
   the final candidate. Include outcomes, raw gate evidence or valid reuse,
   and a Windows acceptance record. Submit for human review and merge.

## Files / Modules Touched (expected)

- Asset System material source, validation/cook and focused tests. Share
  numeric validation where needed. Keep v10 and cooked schema unchanged.
- Runtime executable `src/runtime/{asset_host.*,asset_rebuild_host.*,main.cpp,CMakeLists.txt}`
  and private managed material job helpers if needed. Extend
  `src/runtime/src/{mesh_preview.*,material_realization.cpp}` and the narrow
  RuntimeHost preview declaration to use the existing renderer for a
  validated recipe. Keep work out of the normal frame path.
- `assets/CMakeLists.txt`, `cmake/AtlantisManagedImports.cmake`,
  `src/asset_system/CMakeLists.txt` and existing
  `src/tools/asset_cooker/` command/manifest handling as needed for a
  managed Material declaration with a stable content source key, a
  different private physical input during edit, and a matching producer
  stamp. Reuse the existing catalog fragment assembler and Saved Scenes
  build inputs. Default build declarations retain their existing behavior.
- Connection `asset_connection.*`, Remote codec/client/server, CLI asset
  commands, native SDK `assets.h`, C# SDK asset types/session/codec and MCP
  asset tools and schemas. Keep the current protocol and package policies.
- Editor App `MainWindow.cs`, `EditorSession.cs`, existing asset selector
  and preview helpers where needed, and the new material modal. Keep draft
  and settlement state in Editor Model and reuse 0093 visual resources.
- Focused fixtures under `tests/{asset_system,runtime,remote,cli,gameplay_sdk,csharp,image_regression}/`,
  `src/mcp/tests/` and `src/editor_app/tests/`. Update shared protocol,
  module boundary, operator usage and navigation docs as applicable.

## Sequencing & Dependencies

Build on [0081](../specs/0081-asset-browser-shared-interface.md) for the
catalog, [0085](../specs/0085-shared-asset-rebuild.md) and
[0086](../specs/0086-new-asset-import.md) for durable publication,
[0090](../specs/0090-saved-scene-build-open.md) for saved scene launch,
[0092](../specs/0092-mesh-material-preview.md) for preview and
[0093](../specs/0093-editor-visual-layout.md) for Editor presentation.

| Decision | Recommendation |
| --- | --- |
| **J1. First material subset** | Approve opaque direct-lit PBR with one existing compatible base texture, editable linear RGB/metallic/roughness and the proposed defaults. Copy eligible cooked materials and edit only project-owned material sources. |
| **J2. Persistence and recovery** | Approve locked compare-before-replace, frozen source comparison, three source outcomes, and explicit resume or restart after source commit. |
| **J3. Shared clients and Editor** | Approve the four additive asset methods, exact legacy fallback, bounded receipts and the 0093-style Assets modal. Draft preview shares the 0092 renderer, admission and intent guard. |
| **J4. Delivery and verification** | Approve separate proof from generation restart and from a fresh Saved Scenes build, plus the focused compatibility, recovery, GPU and performance checks below. |

Suggested single authorization after joint approval and manual design merge
covers S2–S4, scope-preserving fixes, applicable verification, valid evidence
reuse and a review-ready implementation PR. Final merge remains human.

## Verification Checklist

- [ ] **R1/R2. Shared values and compatibility.** Check parser/serializer/cook
  round trips at float32 precision, inclusive ranges, finite values and
  malformed recipes, sampler preservation on Copy/Edit, role and dependency
  validation, eligible checked-in and generated copy sources, and refusal
  of unsupported recipes, including opaque materials with non-default
  alpha fields that still preview in 0092. Check read/preview without write
  authority and refusal of attempts to edit checkout/generated assets.
  Check closed request arms, omitted/duplicate members and correlation,
  exact fallback on an older Runtime, unchanged older-client calls on a
  newer Runtime, and matching values/refusals/job state through CLI, both
  SDKs and MCP. Cover lookalike legacy errors and mismatched receipt fields.
  Verify lost/malformed receipts remain unknown, latest is insufficient to
  identify an unacknowledged write, explicit IDs survive a dropped status
  response, and status distinguishes evicted IDs from running jobs.
- [ ] **R1–R3. Build and recovery.** Exercise configured-but-unbuilt staging,
  no-replace name and GUID collisions, stable edit identity, stale expected
  values/generation, and ahead source bytes with identical catalog identities.
  Check concurrent material/import/rebuild starts and a second workspace
  owner. Check unsafe paths and reparse points. Inspect the complete
  catalog delta and dependency changes. Force clean refusal before source
  commit, failure between source and publication, and interruption or lost
  receipt around each rename. Verify all three source states, cleanup failure
  after confirmed publication, repair, and explicit resume for both an
  added and an edited source. Refuse resume with a second registry delta.
  Select a complete generation after a lost receipt. Verify the old snapshot
  and artifacts stay usable, including a
  first catalog query after publish, and that a clean opt-in build from the
  committed registry reproduces identities, factors and dependencies.
  Exercise both an older generation and a new generation with imported and
  authored materials.
- [ ] **R4. Preview and isolation.** With clean Vulkan validation, compare a
  draft and a cooked preview of the same recipe/mesh at equal decoded
  pixels. Change a factor and verify the image changes. Run existing
  geometry/pair golden checks with their original inputs. Cover shared
  admission among all three preview modes, repeated and failed requests,
  resource release and the inherited terminal drain. Check invalid request
  shape before admission and dependency reads after admission. Verify no
  registry, job, scene or Runtime state changes from draft preview. In the
  Editor exercise A→B→A, late success and failure, invalidation at modal
  close/disconnect, cross-surface intent and explicit retry.
  The draft path uses the existing rendered recipe. Any affected golden
  difference goes through ADR-0042 and human visual review.
- [ ] **R3/R4. Real Windows closed loop.** In Assets create a material, choose
  an existing texture and mesh, edit factors, preview, and Save & Publish.
  Confirm the active Runtime and scene history are unchanged. Explicitly
  restart into the reported generation, assign the original to a compatible
  renderable via the existing chooser, apply and save. Duplicate the
  material and edit the managed original. Restart after each publication
  before starting a fresh write. Reopen the saved scene after the edit and
  verify its existing reference keeps the same GUID and uses the edited
  material. Record the selected generation, cooked values and rendered
  result. Verify the copy has a distinct GUID and retains its values. After
  writers settle, select the committed managed root
  in Saved Scenes and run Build and Open. Independently verify the same
  reference and cooked values in the new Editor, using its own build
  outputs and no generation arguments. Exercise an actionable failed
  save and recovery, Cancel without scene edits, and existing scene
  apply recovery and undo/redo for the reference edit. Capture the modal
  at standard and compact sizes at 100% and a higher actual Windows
  scale, with long name/diagnostics, keyboard focus, pending and failure
  states. Check that primary actions remain reachable.
- [ ] **Applicable gates.** Build focused Windows native and managed targets
  and run affected suites. Build Android if shared Asset System or
  RuntimeHost changes. Require clean windowed and headless Vulkan validation
  on affected paths. Evaluate final-diff performance applicability. Planned
  Runtime paths select the existing reference gate. Run the original protocol
  with idle authoring and preview, preserve the raw verdict, and leave
  baseline and policy intact. Reuse evidence only with matching relevant
  inputs and environment. Verify docs, links and whitespace.

## Rollback Plan

Remove the additive methods and Editor entry points with the producer support.
Preserve registered sources, catalog fragments and complete generations on
disk. A build predating managed material support may need an older compatible
generation and scene without those references. Keep newer generations for a
compatible build or restore from the project backup.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR links each requirement to its evidence and records gate
results, applicable reuse and any unresolved case for human disposition.
