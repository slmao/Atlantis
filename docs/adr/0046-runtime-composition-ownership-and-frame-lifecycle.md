# ADR 0046: Runtime Composition, Object Ownership, and Frame Lifecycle

- **Status:** Accepted
- **Date:** 2026-08-20
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-20 as part of Spec 0013's Human Review Approval
- **Related Spec:** [specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md)

## Context

Every windowed composition Atlantis has built so far —
`examples/rhi_vulkan_demo`, `examples/frame_execution_demo`,
`examples/minimal_renderer_demo` — is explicitly disclosed, in its own
spec, as a non-shipping verification composition, not a preview of
Runtime's real architecture. None of them is required to get object
ownership, initialization order, or shutdown ordering *right* in any
generalizable sense; each only needs to work for its own spec's own
narrow verification purpose. `tests/image_regression/fixture/minimal_cube_fixture.cpp`
additionally proves that an Asset-System-sourced mesh feeds
`atlantis::renderer::createMesh()` correctly, but only against an
offscreen target, never a real window.

Spec 0013 needs Runtime to combine both halves — windowed presentation and
Asset-System-sourced content — for the first time, as a real product
entry point rather than another disclosed demo. Doing so requires fixing,
precisely, an object model that has never had to be precise before:
exactly what Runtime owns, in what order it is constructed, what happens
every frame, and in what order everything is destroyed — including every
failure path a `Result`-returning call can actually produce, not merely
the happy path every existing demo already exercises.

`docs/architecture/module_boundaries.md`'s own `PROPOSED` (not `Accepted`)
Runtime section already sketches ownership at a coarse level ("owns the
Platform instance and `Presentation` for their full lifetime... each
frame: acquires a `RenderTarget`... hands it to `Renderer`, then
presents") but was written before Shader System or Asset System existed,
and does not account for either — nor does it fix a concrete
initialization order, an error taxonomy, or an exit-code contract, none of
which any `Accepted` ADR has ever needed to fix before, since no prior
composition root's failure behavior was itself part of what a spec
verified end to end.

## Decision

**Runtime's composition, object ownership, initialization order, per-frame
order, presentation/error-state handling, and reverse-order destruction
are fixed exactly as [specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md)'s
own Requirements section states them — this ADR is the permanent record
of *why* those choices were made, not a restatement of the sequencing
itself.**

- **Object ownership is single-owner, RAII, explicit — never a global,
  static, or singleton.** The Runtime Host composition object owns, by
  value or `std::unique_ptr`, exactly the resources Requirements names:
  `Device`, `Presentation` (constructed lazily, on the first observed
  `SurfaceCreated`), `Mesh`, `Material` (rebuilt, not mutated, on a
  format change), the camera `Buffer`, the depth `Texture` (recreated, not
  mutated, on an extent change), and a default-constructed `Renderer`.
  This is not a new ownership model — it is the same explicit,
  no-hidden-refcounting model ADR-0003 already established for RHI
  resources, applied here for the first time to a composition that owns
  *all* of them together rather than one spec's own narrow subset.
- **Initialization order is fixed, and Material's construction is
  deliberately deferred** to the frame loop's own existing format-change
  check, rather than attempted eagerly with a guessed/placeholder color
  format. This is the one genuinely new sequencing decision this ADR
  records: no prior composition root ever needed to reconcile "the shader
  and asset are loaded and validated before any window exists" with "the
  swapchain format is not known until the first `SurfaceCreated`" in the
  same object's own constructor sequence, because no prior composition
  root loaded a shader/asset before constructing its window in the first
  place — every existing demo interleaves shader loading and window/
  device construction loosely enough that this tension was implicit.
  Deferring `Material` to the existing format-change path (rather than
  inventing a new "construct Material once we know the format" special
  case) reuses Spec 0007's own already-verified mechanism unchanged.
- **Error-state handling is exhaustive and classified into exactly three
  categories** — recoverable-silent (already handled internally by
  existing RHI calls; Runtime does nothing), recoverable-Runtime-visible
  (format/extent change; Runtime rebuilds/recreates and retries on
  failure, unbounded, matching existing demo behavior), and unrecoverable
  (Runtime logs, drains via `waitIdle()`, tears down in reverse order, and
  exits with a distinct exit-code category). This is the first time any
  Atlantis composition's failure behavior has been specified exhaustively
  against the real, current enumerators of `PresentationError` and
  `SubmitError` (confirmed live-mapped from real `VkResult`s in
  `src/vulkan_backend/src/vulkan_result.cpp`, not merely declared) rather
  than left to whatever a demo's own ad hoc `if (result.isErr())` happened
  to do.
- **Destruction is reverse-order, unconditional, and drains outstanding
  GPU work first** — `waitIdle()`, then `Material`/`Texture`/`Buffer`/
  `Mesh`/`Presentation`/`Device` in that order, then `platform::shutdown()`.
  This is not a new rule; it is ADR-0003's and ADR-0019's existing
  lifetime-precondition rule ("every resource a `Device` backed is
  destroyed before that `Device`") applied to Runtime's own complete
  resource set for the first time, fixed here so a future spec extending
  Runtime's bootstrap scene has an unambiguous ordering to preserve rather
  than an emergent one to reverse-engineer from example code.
- **[ADR-0033](0033-runtime-authority-and-client-boundary.md)'s Runtime-
  authority principle is exercised only trivially by this composition.**
  Runtime's bootstrap state (one `Mesh`, one `Material`, one camera
  `Buffer`) is not framed as *world state* in ADR-0033's sense, and no
  Client — real or hypothetical — observes or mutates it. This ADR does
  not attempt to generalize the object model above into a Client-
  accessible boundary; that remains entirely future work for whichever
  spec first gives Runtime a real second consumer.

## Consequences

### Positive

- Gives every future spec that extends Runtime's bootstrap scene (a
  second mesh, a real camera controller, eventually World/ECS) a fixed,
  unambiguous ordering to build on, rather than needing to reverse-engineer
  one from whichever demo happened to be closest.
- Makes Runtime's own failure behavior a first-class, reviewed, testable
  property for the first time in this codebase — no prior composition
  root's error handling was itself something a spec's own Acceptance
  Criteria or Testing & Verification Plan held to an exhaustive standard.
- Resolves the Material-construction timing tension cleanly, by reusing
  an existing, already-`Accepted` mechanism (Spec 0007's format-change
  contract) rather than inventing a new one — keeping this ADR's actual
  net-new surface area small despite composing six modules together for
  the first time.

### Negative / Trade-offs

- Fixing an exhaustive error taxonomy against `PresentationError`'s and
  `SubmitError`'s *current* enumerators means this ADR's own table is only
  as complete as those types are today; a future RHI change adding a new
  variant would need this ADR (or its Requirements table in Spec 0013)
  revisited, not silently reinterpreted.
- The deferred-`Material`-construction design has not yet been run against
  real hardware at the time this ADR is drafted (see Spec 0013's own Risks
  & Open Questions) — a real, if small, residual risk that this ADR's own
  reasoning, however carefully derived from Spec 0007's existing contract,
  could still surface a genuine first-frame timing issue Implementation
  must resolve.
- Retry-without-limit on a persistent format/extent-change failure (the
  same behavior every existing demo already has) means a persistently
  misbehaving driver could, in principle, retry forever rather than fail
  fast — an accepted, unchanged trade-off inherited from Spec 0007, not a
  new one this ADR introduces.

## Alternatives Considered

- **Construct `Material` eagerly at startup against a guessed or
  placeholder color format**, then rely on the existing format-change path
  to correct it on the first real frame. Rejected: this would spend a
  real `Pipeline` construction on a value known in advance to be
  immediately superseded, and risks a future reader mistaking the
  placeholder value for a meaningful default rather than recognizing it as
  arbitrary.
- **Introduce a new, Runtime-specific "first swapchain format" query or
  callback on `Presentation`/`Device`**, so `Material` could be constructed
  synchronously during initialization rather than deferred into the frame
  loop. Rejected: this would be exactly the kind of new public RHI API
  Spec 0013's own Non-Goals rule out unless a genuine gap is found — and
  none is, since the existing format-change mechanism already solves this
  problem correctly for the one case (first frame) that matters here.
- **Treat every unrecoverable error identically, with one single exit
  code**, rather than distinguishing initialization failure from a
  runtime/presentation failure. Rejected: Spec 0013's own instructions
  explicitly require the distinction (recoverable presentation states are
  not exit-triggering at all; initialization failure and an unrecoverable
  runtime error are meaningfully different conditions for anything
  scripting or monitoring Runtime's own process exit code to distinguish),
  and the cost of maintaining two categories instead of one is negligible.
- **Bound the format/extent-change retry loop** (e.g., fail Runtime after
  N consecutive failures) instead of retrying unboundedly. Rejected for
  this round: no existing composition root does this, inventing a bound
  now would be a new, unreviewed policy with no evidence it is actually
  needed, and Spec 0013's own scope is to match existing, already-verified
  behavior precisely, not to improve on it speculatively.
