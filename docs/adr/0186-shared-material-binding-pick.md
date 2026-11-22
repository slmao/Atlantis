# ADR 0186 — Shared Viewport Material Binding Query

- **Status:** Proposed
- **Date:** 2026-11-22
- **Deciders:** Atlantis maintainers
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0138](../specs/0138-viewport-material-slot.md)

## Context

The existing shared raycast exposes one entity and distance. It intentionally
does not expose an ambiguity result, a live material GUID or a slot. The
rectangular-cap query in [ADR-0184](0184-shared-alignment-face-query.md)
cannot identify general opaque fabric. Runtime already owns geometry,
material realization and the identity mapping for the current catalog.

## Decision

1. **Add a single opt-in read.** Add `RuntimeControl::pickMaterialBinding`
   and `control.pickMaterialBinding` to `atlantis.remote/1`. The request
   contains the existing finite normalized world `ray`, its finite
   nonnegative inclusive `minDistance` and finite `maxDistance` greater
   than the minimum, and a required `sceneGeneration`. The optional
   `viewport` is the existing 0136 `ViewportStamp`; its generation must
   equal the requested generation. Editor derives the interval from the
   settled perspective's near and far planes along the ray. Without a
   viewport stamp the read uses the ordinary unfiltered scene. With a stamp
   the Runtime verifies generation, exact revision and presented state at
   dispatch, and requires no active hosted policy. This explicitly avoids
   interpreting a hit through hidden or locked geometry.
2. **Return verified identities with a closed outcome.** Success contains
   `sceneGeneration`, the matching `viewport` stamp when requested,
   `entity`, `mesh` and `material` canonical non-nil GUIDs, the finite
   ray `distance`, and `slot: "Renderable.materialAsset"`. Allow no other
   slot in V1. A miss and ambiguity are distinct typed outcomes.
   Distinguish invalid request, unavailable/unsupported, stale generation,
   stale/unpresented viewport, active policy, no geometry, unsupported
   binding and exceeded work bound. Resolve the GUIDs through retained
   geometry and validated records of the active Runtime catalog. Only an
   explicit material realized for the drawn Renderable can qualify.
   A fallback or unresolved identity cannot yield a writable slot.
3. **Prove the nearest unique binding.** Reuse Runtime's draw eligibility
   and retained CPU triangle traversal, with the clip interval. Consider
   geometric intersections before testing material eligibility so an
   unsupported front surface can block success. If a potential front blocker
   cannot be verified, return a refusal. Different Renderables within
   `ε = 1e-5 × max(1, nearestDistance)` world units of the nearest hit are
   ambiguous regardless of reference equality. Same-Renderable triangles
   share its single field. Do not infer primitive slot ordinals. Limit
   one ray to 8,192 examined instances and 262,144 triangle tests across all
   passes. Exceeding either budget yields no result; ordinary pick and
   alignment keep their own budgets and result shapes.
4. **Preserve client and storage boundaries.** Supply typed C++ and C#
   access, CLI and an exact MCP read tool over the existing SDK. A default
   Connection implementation refuses unsupported callers. A newer client
   on an older host exposes the unsupported result and keeps ordinary
   navigation and Inspector available. Prior clients keep existing results
   against a newer host. Do not add a camera/asset binding write, protocol
   transport, renderer feature, native Editor link or dependency.
   The authoring and asset mutation contracts remain as in
   [0106](../specs/0106-scene-multiselect-batch-edit.md) and
   [0094](../specs/0094-basic-material-authoring.md).

## Consequences

There is one additional explicit Runtime read and bounded geometry traversal
on demand. The result is evidence from one Runtime dispatch, not a lease.
Editor checks the sampled identities against the authoring document and
revalidates before its existing write paths. Ineligible surfaces receive
an explicit refusal. Additional material or viewport display modes require
their own reviewed semantics.

## Alternatives Considered

- Enrich every legacy raycast response. This would alter an existing
  contract and could put extra work in ordinary selections or drag motion.
- Add Editor-private access to the native mesh or draw order. This would
  duplicate the evidence and introduce a new trust boundary.
- Identify triangles through a GPU ID pass. The first slice only needs one
  opaque Renderable's bound asset, so retained CPU geometry is sufficient
  within the documented refusal boundaries.
