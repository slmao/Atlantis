# ADR 0190 — Editor Mesh Thumbnail Admission and Cache

- **Status:** Accepted
- **Date:** 2026-11-23
- **Deciders:** Atlantis maintainer
- **Acceptance:** Maintainer, 2026-11-23, [joint approval of `fc5c7974`](https://github.com/slmao/Atlantis/pull/447#issuecomment-7105301892)
- **Related Spec:** [Spec 0142](../specs/0142-assets-mesh-thumbnails.md)

## Context

[ADR-0151](0151-shared-offscreen-mesh-preview.md) runs one admitted preview
in the Runtime service interval and can wait for GPU completion while serving
it. The Editor currently has one explicit preview slot across geometry,
material and draft previews. Automatic calls for a virtualized catalog
could otherwise contend with those actions or complete after a row has
been rebound. A catalog GUID is stable across changes of Runtime and cannot
alone prove that an earlier image is current.

## Decision

1. Make thumbnails an automatic consumer of the existing ordinary preview
   connection pattern and shared Editor slot. Keep one operation in flight
   per Editor, including connection setup, transport settlement and local
   decode. Derive automatic intent from the latest visible demand. An
   explicit operation has priority over any unsent automatic intent,
   including one awaiting connection setup. A sent operation retains the
   slot until settlement or the existing timeout. The latest valid explicit
   request goes next without the automatic debounce or rate limit. Preserve
   the manual surface's request, status and retry semantics.
2. Call the current SDK `PreviewMesh(guid)` on its owning preview worker,
   using the attached Runtime's connection identity. Recheck admission
   after setup, and reconcile results using view and row identity. Reuse
   ADR-0151's PNG, bounds, timeout and transport behavior. Runtime's shared
   permit and its handling of Busy and fatal errors govern both consumers.
   Keep scheduling, image decoding, ownership and diagnostics in the Editor.
3. Give the Editor catalog-view cache the Runtime incarnation and catalog
   epoch scope defined in Spec 0142. The fixed geometry variant uses the
   asset GUID within that scope. The visible-demand epoch is an admission
   and delivery guard, so browsing changes can retire pending work while
   keeping valid previously accepted images. Bound retained resources and
   release them with the view lifetime.

## Consequences

The same preview implementation serves automation clients and the Editor.
The cache avoids GPU work for repeat visits in the active catalog view.
A synchronous automatic request already sent can still delay an explicit
preview or other Runtime operations. The single slot, visibility debounce
and rate limit bound automatic demand. First-visit thumbnails fill
progressively, and an explicit reload or Runtime replacement starts a fresh
cache.

## Alternatives considered

- Give thumbnails their own worker slots. This would allow automatic and
  manual GPU requests to compete inside the same serving Runtime.
- Precompute or persist thumbnails. This would need build/storage versioning
  and more invalidation rules before the in-session workflow is proven.
- Add a batched or smaller-output preview API. That could reduce round trips
  and payload cost, with a new protocol and Runtime admission contract.
  The current single-mesh result is sufficient for this slice.
