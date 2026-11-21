# Plan 0134 — Project Assets Explorer V1

- **Spec:** [Spec 0134](../specs/0134-project-assets-explorer.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending
- **ADR:** [ADR-0183](../adr/0183-project-source-inventory-and-catalog-links.md) (Proposed)
- **Design baseline:** `d814059f`, maintainer merge of [#430](https://github.com/slmao/Atlantis/pull/430)

## Objective

Add directory browsing and checked source-to-asset navigation to Assets.
Keep the existing import and scene operations as the way to make an
asset usable and place it.

## Milestones and sequencing

1. **S1 — Joint review.** Resolve J1–J4 on the exact revision and record
   the accepted ADR. Start S2 on current `main` after maintainer merge.
2. **S2 — Verified inventory.** Add the read-only Windows launch permission,
   checked root resolution and bounded background scan. Compose checked
   registration evidence with the existing immutable catalog. Define the
   shared results, unsupported behavior and scan ID settlement in the
   host, transport and clients. Verify incomplete and stale cases before
   exposing a Project action.
3. **S3 — Assets views and handoff.** Extend the existing Assets projection
   for two trees, search scope and filters. Wire checked cross-view
   navigation, status and prefilled existing import controls. Keep
   selection, preview and drag guards. Verify the UI against asynchronous
   model and host results.
4. **S4 — Verify and submit.** Run the Windows 100% Bistro workflow and
   targeted suites and gates. Preserve raw evidence and submit a
   review-ready implementation PR.

## Expected file and module scope

| Area | Work |
| --- | --- |
| `src/runtime/{asset_host,asset_rebuild_host,prefab_projection,main,cli}.*`, executable-private scan helper and matching Runtime tests | Checked Windows roots, bounded reads and identities, authorization forwarding, frozen-catalog join and scan settlement. |
| `src/connection/{include,src}`, `src/remote/src`, `src/cli/src`, `src/gameplay_sdk/include`, `src/csharp/Atlantis.Gameplay`, `src/mcp/Atlantis.Mcp` and their existing tests | Add two shared read operations and compare contract and compatibility across clients. Record the protocol in `docs/architecture/remote_protocol.md`. |
| `src/editor_app/Atlantis.Editor`, `src/editor_app/Atlantis.Editor.Model`, matching Editor tests | Launch opt-in, Assets trees and filters, stale intent guards, status and existing import handoff. |
| Spec/Plan 0134, ADR-0183 and the Spec registry | Approval and navigation. |

List concrete implementation files inside these areas when work starts.
Record a necessary non-semantic file-list change in the implementation PR
under the existing approval rule.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and authority** | Approve Runtime's virtual catalog tree and a separate Project tree limited to the checked managed inbox and checkout Prefab roots. Approve the explicit read opt-in, isolated from write permission and ordinary attachment. |
| **J2 — Shared evidence** | Approve bounded background scan and status operations with checked producer and registration identities. Require explicit completeness and conservative refusal on ambiguity or staleness. |
| **J3 — Workflow** | Approve explicit, guarded two-way reveal, per-view search state and prefilled existing Import controls. Preserve snapshot activation, preview, drag/drop and Instantiate Prefab behavior. |
| **J4 — Authorization and acceptance** | After joint approval and maintainer design merge, authorize one S2–S4 run covering implementation, in-scope fixes, applicable verification and a review-ready PR. Retain Windows 100% native captures, applicable gates and maintainer merge. |

## Verification checklist

- [ ] **R1 — Scope and browsing.** Exercise both roots independently, no
  opt-in, old hosts, wrong checkout, reparse/alias rejection, unreadable
  roots, depth/count/byte limits and long relative names. Verify stable
  ordering, focus, virtualization, standard and compact widths. Test
  filtered-away targets and unchanged reference chooser behavior. Verify
  search and directory changes issue no scan or preview.
- [ ] **R2 — Evidence and status.** Exercise a complete empty result and a
  partial result, registration without activation, verified publication,
  unknown receipt, checked source modification, malformed registration and
  the same stem in another project. Test glTF one-to-many, logical PNG
  location versus original inbox slot and missing frozen Prefab. Compare
  native/managed SDK, CLI and MCP read contracts, including old client and
  old host behavior and unchanged `assets.list/get`.
- [ ] **R3–R4 — Intent and handoff.** Replace, move or remove a source or
  registration between scan and navigation. Delay replies across selection,
  filter and Runtime changes. Verify no wrong reveal, implicit import,
  resumed job or raw-file drag. Verify disabled writes on an unavailable
  or unattached profile and reuse the existing authoritative import checks.
- [ ] **Windows 100% — Bistro authoring.** Use a disposable trusted project
  and an existing editable Bistro fixture.
  1. Browse the managed inbox for a supported static storefront glTF and a
     base-color PNG. Use search and source-kind filters. Open Import from
     each source. Observe the original status and explicit publication.
     Restart into the confirmed named generation at each required existing
     activation boundary. Verify active output is absent from the old
     snapshot and shows up with the expected GUIDs in the new one.
  2. Use both navigation directions. Verify one storefront source exposes
     distinct catalog outputs, the PNG follows its original inbox slot,
     and an obsolete result or mismatched registration refuses navigation
     without changing selection. Use the existing mesh preview and place
     a returned mesh through the existing Hierarchy root drop. Verify its scene
     asset reference by GUID. Capture the supported states and exact
     catalog and source keys.
  3. On a separate trusted 0090 Build/Open path, browse one registered
     checkout Prefab, reveal its verified Runtime row, locate the checkout
     source again and use **Instantiate Prefab**. Capture the identity and
     registered/frozen status in the independent Editor. Keep live managed
     import authority scoped to the managed-profile Runtime.
- [ ] **Applicable gates.** Run affected Windows Runtime, Editor and
  shared-client checks. Run Vulkan validation for exercised preview and
  placement. Evaluate headless/golden, Android and performance
  applicability on the final diff, including `--gate --applies`. Record raw
  results and scoped reuse by unchanged input and revision.

## Rollback and definition of done

Retire the additive read capability and Project view. Sources, saved
scenes, registrations, build receipts and actively selected generations
remain usable through the existing paths. Follow the
[Definition of Done](../process/definition-of-done.md), including the
Windows capture and authoritative identity read-back.
