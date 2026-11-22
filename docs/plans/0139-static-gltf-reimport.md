# Plan 0139 — Preview and Reimport a Registered Static glTF

- **Spec:** [Spec 0139](../specs/0139-static-gltf-reimport.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer approved Spec/Plan 0139 and J1–J4 at `69d35d2f` on 2026-11-22. S2–S4 authorized after [PR #441](https://github.com/slmao/Atlantis/pull/441#issuecomment-7077272130) merged as `be9e0ad2`.
- **ADR:** [ADR-0187](../adr/0187-static-gltf-reimport-identity.md) (Accepted)
- **Design baseline:** `2ea0cd9b`

## Objective

Review a single storefront model update and publish it under stable asset
GUIDs. Preserve the placed scene and material overrides, and keep the last
complete generation usable on refusal or failure.

## Milestones

1. **S1 — Joint review.** Decide J1–J4 on the exact design revision. Begin
   implementation from latest `main` after maintainer merge.
2. **S2 — Identity and shared service.** Add the opt-in managed identity
   mode, keyed registration, v10 activation evidence and staging checks.
   Reuse the existing worker, publisher, recovery and job status. Implement
   and test the additive review and confirm contracts across clients.
3. **S3 — Assets workflow.** Add the explicit review entry, complete change
   list and guarded confirmation to the existing Assets and Tasks surfaces.
   Connect selection and connection lifetimes, errors and explicit restart
   guidance. Validate cold build resolution and old-host refusal.
4. **S4 — Verify and submit.** Run the checklist on the final candidate,
   retain real evidence, and submit an implementation PR for human review.

## Files / modules touched (expected)

| Area | Scope |
| --- | --- |
| `src/tools/gltf_importer/`, `src/asset_system/` catalog-source test coverage, `cmake/AtlantisManagedImports.cmake` and managed importer/CMake invocation in `src/asset_system/CMakeLists.txt` | Validate the opt-in envelope, carry stable subkeys through the existing importer, and configure one staged producer replacement. Preserve legacy registrations and general importer behavior. |
| `src/runtime/asset_rebuild_host.{cpp,h}`, `asset_host.{cpp,h}`, `static_group_projection.h` and focused `tests/runtime` | Bind staged review to a verified group. Extend generation and registered-unit evidence, compare the exact delta, and reuse job/recovery boundaries. |
| `src/connection` asset types and codecs, `src/remote`, `src/cli`, `src/csharp/Atlantis.Gameplay`, `src/mcp`, corresponding tests and `docs/architecture/remote_protocol.md` | Add shared review and confirm calls, including exact wire values and compatible older-host refusals. Use MCP over the C# SDK. |
| `src/editor_app/Atlantis.Editor/MainWindow.cs`, `EditorSession.cs`, an Assets review dialog, related Assets/Tasks models and corresponding tests | Show the two source revisions and their matches, reasons and completion state. Reuse selection, Tasks and restart conventions. |
| Existing glTF importer, managed configure and publication test areas, a disposable Bistro storefront fixture and retained evidence | Inputs, identity, no-write refusals, recovery, and independent cold start. Keep shipped assets and golden references unchanged unless separately reviewed. |

## Sequencing and dependencies

Reuse [0086](../specs/0086-new-asset-import.md) registration and staging,
[0130](../specs/0130-static-gltf-group-instantiation.md) group bounds and
ordinary authoring copies, [0116](../specs/0116-registered-texture-reimport.md)
guarded replacement, [0121](../specs/0121-editor-tasks-and-issues.md) job
feedback, and [0090](../specs/0090-saved-scene-build-open.md) independent
build/open. Use [0138](../specs/0138-viewport-material-slot.md) to verify the
Editor-confirmed binding of the original awning after activation. Existing
shared queries and asset authority remain the entry points for all clients.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Identity and eligibility** | Accept ADR-0187's explicit stable IDs and new keyed producer. Require that the original registration already uses keyed identity. Defer migration of unkeyed or positional registrations. |
| **J2 — Preview and admissible changes** | Show matched, added, removed and ambiguous parts and material slots in a structural review. Allow geometry, reorder and additions. Block removal, missing or changed existing assignments, changes to existing material recipes and ambiguity. Require an explicit one-use Confirm. |
| **J3 — Publication and compatibility** | Accept versioned keyed registration and generation v10, shared review and confirm, current-toolchain cold builds, verified rollback and explicit activation. Leave existing readers and v1–v9 confirmed builds on their existing contracts. |
| **J4 — Implementation authorization** | After joint approval and maintainer merge, authorize S2–S4 once for implementation, in-scope repairs, applicable verification and a review-ready PR. Final merge remains with the maintainer. |

## Verification checklist

- [ ] **R1 — Import and identity.** Cover managed opt-in and unchanged
  legacy registration. Reject missing, duplicated and malformed keys,
  unsupported glTF features, and positional registrations with merely added
  keys. Reorder mesh, primitive and material arrays independently using
  deliberately distinct products. Prove derived GUIDs, source keys, stable
  paths, exact normalized unchanged recipes, cooked assignments and explicit
  unassigned state. Check permitted additions and repeated mesh instances.
- [ ] **R2 — Review and confirmation.** Test new and removed rows, duplicate
  IDs, deleted/changed material slots and incomplete comparison, including
  malformed input. Check selection change, cancellation, late results,
  expiry, concurrent admission, stale generation and exact byte changes with
  the same timestamp. If Cancel wins, Confirm must leave zero jobs or
  publishable changes. Confirm cannot be consumed twice.
- [ ] **R3–R4 — Publication and recovery.** Use original GUIDs and relative
  paths on both restart paths. Turn unrelated recipe, producer header,
  dependency or output drift into a refusal before a durable write. Inject
  invalid input, cook failure, definite publication failure and an uncertain
  publish result. Verify original catalog, source and generation availability,
  verified rollback, repair hold and no automatic retry. A changed staged
  file or corrupt final directory must never be confirmed as success. Check
  v1–v9 input compatibility, explicit older-host refusal of v10 and older
  managed configure refusal of a keyed unit. Agree on wire results across
  CLI, native/C# SDKs, MCP and Editor. Keep normal import and queries working.
- [ ] **Windows 100% — Bistro storefront.** Use a bounded fixture derived
  from the Bistro storefront with distinct wall, window and awning parts
  and recorded stable keys. Register and explicitly activate it. Add it to
  a saved hand-authored scene at a nondefault pose and give the awning a
  distinct material override. Record node GUIDs, local/world transforms,
  mesh and material GUIDs and ticket/generation identities. Capture the
  fixed 0131 view at 960 × 540. Retain native screenshots and raw reports.
  1. Update geometry at the original inbox path, reorder meshes, primitives
     and materials, and add a small part. Check the paired slot rows,
     added count and stable GUIDs. Cancel once and verify the registered unit
     and publication set have the original bytes and IDs. Open a fresh review
     and explicitly confirm.
  2. Verify the old Editor still renders the old generation. Explicitly
     restart into the new named generation and reopen the same saved scene.
     Check the updated geometry and unchanged placements and slot GUIDs,
     including the awning override. Confirm that the added part is available
     for a future Add Group. Run 0090 independent Build/Open on the same
     saved source and verify the same identities and a fixed-view capture.
  3. Preview a reordered source with a missing slot and one with an
     ambiguous key. Verify blocking explanations, zero publication or
     rebinding and a usable last version. Try invalid input, then correct
     it and start a fresh preview. Verify retry capability and the same
     last usable version following an injected job failure.
- [ ] **Applicable gates.** Run affected importer, CMake, Runtime,
  Editor and shared-client checks. Verify the actual Windows GPU path with
  Vulkan validation and use the applicable headless/image regression tests.
  Check Android build/package applicability and run `--gate --applies` on the
  final diff. Preserve the existing performance protocol and baselines when
  a run is selected. Record provenance for every reused result.

## Rollback plan

For a failed job use the guarded restoration and repair rules in ADR-0187.
For a product rollback, first retain a runtime and toolchain that can read
any existing keyed generations. Select an earlier compatible generation or
retain the keyed reader as appropriate. Reverting the writer cannot convert
an already committed keyed registration to a positional one.

## Definition of Done

Use the [repository definition of done](../process/definition-of-done.md).
Record the joint approvals, exact reviewed identities, native Windows
evidence and applicable gate outcomes in the implementation PR.
