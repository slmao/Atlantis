# ADR 0191 — Render Look Preview and Guarded Adoption

- **Status:** Proposed
- **Date:** 2026-11-26
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0145](../specs/0145-render-look-presets.md)

## Context

The current capture reads live camera, light, shadow and Bloom resources.
Tracked Apply changes the shared projection. A comparison must render a
candidate without an Apply or a live preview override. The existing revision
guard in [ADR-0162](0162-revision-guarded-authoring-property-batch.md) admits
one common leaf across nodes. The transform guard of
[ADR-0163](0163-guarded-transform-batches.md) admits TRS writes only. Adopting
a whole look spans several different components and leaves.

## Decision

1. **A bounded shared capture.** Add one optional shared look-pair capture
   operation. Its request carries two complete typed looks, the same expected
   document token and settled projection identity for both, a single fixed
   960 × 540 pose/lens, and two distinct fresh destinations under the existing
   caller-owned capture file contract. Refuse to replace an occupied output.
   Keep payloads within current transport
   budgets and reject an incomplete roster or duplicate address. Bound the
   Editor to one outstanding pair and the host to one in-flight pair. Report
   busy, unsupported, stale, invalid or capture failure explicitly. A success
   identifies both roles, the base token, Apply ID, Runtime generation,
   camera, effective values and image metadata. The PNG bytes travel through
   existing capture files, with two bounded reads and validated PNG extents.
   A pair succeeds only with both images. Cancellation retires the caller's
   intent and never removes another attempt's files.
2. **Request-owned projection data.** At the existing serialized service
   boundary derive both variants from one frozen authoring snapshot. Use the
   existing candidate validation, scene conversion and extraction paths.
   Borrow only already-resolved immutable mesh, material, environment and
   pipeline inputs from the settled scene/catalog. Use request-owned mutable
   camera, light, depth, HDR, shadow, Bloom, output and readback resources and
   descriptor bindings. This includes an off-to-on Bloom pair when the main
   projection has no Bloom targets. Use the existing RenderGraph renderer
   and PNG path with the ordinary 0131 output format and color transform.
   Process variants sequentially using one request-local scratch set. Refuse
   if the required bindings are unavailable in the settled catalog.
   Admission and capture do not step, pause, resume or
   write to the live World or authoring document. Keep the ordinary per-frame
   path and ordinary capture contract. Release after GPU completion, including
   error and cancellation paths. A normally playing Runtime can continue its
   ordinary loop outside the capture interval.
3. **A closed authoring guard.** Add one capability-advertised
   `RequireRenderLookDocument` as the leading command in a guarded transaction.
   It carries the expected document token, active camera GUID, canonical
   target basis and complete light roster. Follow it with one to 203 distinct
   changed `SetProperty` addresses from Spec R1 and nothing else. The bound is
   eight camera leaves plus three leaves for each of up to 65 lights. At the
   serialized host admission point check the exact token, active camera,
   light membership/kind, directional leaf restriction, basis, target
   type/address and schema value domain. Reuse candidate cook and normal
   ticket-range/failure semantics. Reject the entire transaction for stale
   and malformed requests. The guard emits no events. A committed change
   advances one revision and emits normal field events. The Editor records
   one verified original-to-new history entry and uses the same guard for
   replay against fresh revision and expected values. An uncertain receipt
   uses existing reconciliation and explicit Apply recovery.
4. **Local names and bounded persistence.** Use a per-user versioned store
   keyed by physical canonical source path plus scene Asset GUID, with one
   file per scene and an inclusive 512 KiB serialized size limit. Validate
   the schema, finite values, exact roster, unique names (trimmed,
   case-insensitive and up to 64 UTF-8 bytes) and scope
   before accepting the file. Use the existing local receipt/settings
   precedents for ordinary files, same-volume atomic replacement and reader
   sharing. Check the reviewed store revision while serializing writes so
   concurrent Editors cannot silently replace each other's entry. Invalid or
   unknown versions remain available for diagnosis. Permissions derive from
   the existing session and OS identity. The store carries no authoring or
   launcher capabilities and does not participate in Save, Build or catalog
   publication.

## Trade-offs

The on-demand pair uses extra device memory and can occupy the host service
interval while both renders complete. Bound and measure the peak resources
and interval on the reference fixture, and document refusals. Editor capture
and adoption require a host with both capabilities for the complete flow.
Local preset names travel with the Windows user and local source scope.

## Alternatives considered

- Save two captures produced by applying each look in the live scene. This
  would change projection or history and would lose the ability to render
  both against the same current authored inputs.
- Start a second ordinary Runtime against the same saved source. Its
  authoring session could update recovery state for that source. Requiring a
  separate build and disposable source copy would change the compare workflow.
- Compose existing guarded edits across individual fields. A failure between
  writes would expose a partial adoption and split Undo. Broadening the old
  guard would affect unrelated clients. The new guard has a closed shape.
