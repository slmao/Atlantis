# Architecture Review: Platform / Vulkan WSI Boundary

> **Status: Review — pending ADR amendment. Not itself an ADR.** This
> document records the analysis and recommendation for resolving a
> conflict [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)
> identified between the existing Platform/RHI rules and what a Vulkan
> WSI implementation actually requires. It does not amend any ADR itself
> — see Recommended Decision for what follow-up edit is proposed, not yet
> made.

## Problem

[specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)'s
design has Platform expose a `NativeWindowHandle` whose public shape is
platform-header-free (opaque `void*` payload, not typed `HWND`/
`ANativeWindow*`). A future Vulkan Backend must eventually call
`vkCreateWin32SurfaceKHR`/`vkCreateAndroidSurfaceKHR` to obtain a
`VkSurfaceKHR`. Vulkan's own WSI extension headers
(`vulkan_win32.h`, `vulkan_android.h`) declare those functions using the
**real, typed** OS handles (`HWND`, `HINSTANCE`, `ANativeWindow*`), which
in turn requires transitively including `<windows.h>` /
`<android/native_window.h>` for those types to exist at all.

That is flatly incompatible with the literal current wording of
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md), which
says no module other than Platform may "include any OS-specific header."
As written, Vulkan Backend cannot both obey that sentence and call
Vulkan's own platform surface-creation functions. Left unresolved, this
is either a hard blocker on the first Vulkan RHI spec, or a rule that
gets silently violated the first time someone writes the WSI code.

## Existing Conflicting Rules

Only one ADR statement is in direct, literal conflict. The rest of
ADR-0001–0010 are compatible with the boundary this review proposes and
need no change — checked explicitly below so nothing is amended that
doesn't need to be.

**Conflicting — [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md),
Decision, second bullet:**

> "Atlantis Platform owns all OS-specific types (Win32, Android NDK,
> future UIKit) and exposes only an opaque native-surface handle plus
> lifecycle events to its caller (Runtime). **No other module — RHI,
> Vulkan Backend, RenderGraph, Renderer — depends on Atlantis Platform or
> includes any OS-specific header.**"

The second sentence is the exact conflict: it forbids Vulkan Backend
from including *any* OS-specific header, with no carve-out for a graphics
API's own WSI headers.

**Not conflicting, verified individually, no change needed:**

- [ADR-0001](../adr/0001-rhi-backend-independence.md) (as amended): its
  forbidden-dependency list is scoped to **Renderer and RenderGraph**
  (Win32, Android NDK, GLFW/SDL, `VkSurfaceKHR`, `VkSwapchainKHR`, `Vk*`
  types, Vulkan Backend, Platform) — it never states a constraint on
  Vulkan Backend's own header inclusions. Fully compatible as-is.
- [ADR-0002](../adr/0002-presentation-rendertarget-unification.md): concerns
  `RenderTarget`/`Presentation` unifying windowed-vs-headless and
  multi-OS; says nothing about header inclusion. Compatible as-is.
- [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md): unrelated to this
  topic. Compatible as-is.
- [ADR-0006](../adr/0006-dependency-management.md)–[ADR-0010](../adr/0010-cmake-structure.md)
  (Spec 0001's foundation ADRs): unrelated to this topic. Compatible
  as-is.
- `docs/architecture/module_boundaries.md`'s **Atlantis Vulkan Backend**
  section already says Vulkan Backend "does not depend on Atlantis
  Platform" (the module/its types) and "internally dispatches to the
  correct WSI extension" — this is about not depending on the *Platform
  module*, which remains true under this review's proposal; it does not
  itself forbid OS SDK header inclusion, so it needs no change.
- `docs/architecture/module_boundaries.md`'s **Atlantis RHI** section
  ("Forbidden: no `Vk*` type, no Vulkan header... no windowing-library
  type and no Atlantis Platform type anywhere in RHI") constrains RHI's
  *public surface* only — compatible with, and actually required by, the
  boundary proposed below.

**Also derived from the conflicting sentence, will need a matching
follow-up (not an ADR, flagged for completeness):**
[AGENTS.md](../../AGENTS.md)'s Module Boundaries section states "only
the Atlantis Platform module may include Win32/Android NDK/(future) iOS
platform headers" — copied down from ADR-0005's now-to-be-amended
wording. Not in scope for this review to edit (out of this task's
explicit scope), but it will read as stale once ADR-0005 is amended.

## Proposed Boundary

Adopting the boundary given for this review, restated precisely:

- **Platform**: owns platform lifecycle, owns native window lifetime,
  creates/destroys native windows where applicable, exposes minimal
  opaque native-window information, may include platform SDK headers
  internally. *(Unchanged from [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md).)*
- **Renderer**: never depends on Win32/Android NDK/other platform APIs;
  never exposes `HWND`/`HINSTANCE`/`ANativeWindow`/`CAMetalLayer`.
  *(Unchanged.)*
- **RHI public API**: stays platform-independent; never exposes OS-specific
  types. *(Unchanged.)*
- **Vulkan Backend**: gains an explicit, narrow exception — it may
  contain a **dedicated WSI boundary** that includes Vulkan's own
  platform WSI headers (`vulkan_win32.h`, `vulkan_android.h`), consumes
  the opaque native-window information Platform already produces, and
  translates it into a `VkSurfaceKHR`. Platform-specific WSI types must
  not leak past that boundary into RHI's public API, Renderer, or
  RenderGraph.

## Dependency Direction

Data flow (what the user's task described conceptually):

```
Platform  -->  Opaque Native Window Information  -->  Vulkan WSI  -->  VkSurfaceKHR  -->  Vulkan RHI  -->  Renderer
```

Code dependency direction (dependent → dependency; unchanged from
`docs/architecture/overview.md` except the one narrow addition, marked):

```
Core
 ^
 +-- Platform (Windows/Android/future iOS impls; owns OS SDK headers)
 |
 +-- RHI (public API: no Vk*, no OS types)
 |    ^
 |    | implements
 |    Vulkan Backend
 |      ^
 |      | contains (private, not a separate module)
 |      Vulkan WSI boundary  <-- NEW: may include vulkan_win32.h /
 |                                vulkan_android.h, and therefore the
 |                                OS handle types those headers require
 |                                (HWND, ANativeWindow*), solely to call
 |                                vkCreate*SurfaceKHR
 +-- RenderGraph
 +-- Renderer

Runtime -> Platform, RHI (Device + Presentation), Renderer, RenderGraph
           (passes the opaque native-window handle from Platform to
           RHI's Presentation creation call, as already established)
```

**Vulkan Backend still does not depend on the Atlantis Platform module**
— it never includes `atlantis/platform/*.h` or links against
`Atlantis::Platform`. It independently includes the OS SDK headers itself
(the same headers Platform's implementation also includes, but as two
separate, non-coupled inclusions — Vulkan Backend does not get OS header
access *through* depending on Platform; it has its own, for its own WSI
purpose). This is the key distinction that keeps Platform and Vulkan
Backend siblings, exactly as ADR-0005 already established for Platform
and RHI generally.

## Ownership Model

**Unchanged from [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md).**
This review resolves a *code-organization/header-visibility* question,
not a *window-lifecycle-ownership* question — the two are separate.
Platform still creates/owns/destroys native windows exactly as Spec
0002's Ownership and Lifetime table already specifies (Windows Platform
owns and destroys its `HWND`; Android's framework owns/destroys the
surface and Android Platform only observes). Vulkan WSI never creates,
destroys, resizes, or otherwise manages the native window — it only
*reads* the opaque handle Platform (via Runtime) hands to RHI, once per
`Presentation` creation/recreation, and converts it into a `VkSurfaceKHR`.

## Public API Constraints

- **RHI's public headers**: no change — still zero `Vk*` types, zero OS
  types, per ADR-0001/RHI's existing "Forbidden" list.
- **Renderer, RenderGraph**: no change — never see `NativeWindowHandle`,
  never see `Vk*`, never see OS types.
- **Vulkan Backend's own public surface** (the part of it that
  implements RHI's interfaces, e.g. `Presentation`): stays
  platform-independent — it's still just an implementation of RHI's
  already-platform-independent interface. Only Vulkan Backend's
  *internal* WSI boundary sees OS types; nothing about `Presentation`'s
  public shape changes.

## Vulkan WSI Responsibilities

The WSI boundary is a small, clearly delineated part of Vulkan Backend
(not a separate module, not part of Platform) responsible for exactly:

1. Receiving the opaque `NativeWindowHandle` (produced by Platform,
   passed through Runtime to RHI's `Presentation` creation/recreation
   call, then handed down into Vulkan Backend's implementation).
2. Dispatching on `NativeWindowHandle::kind` to the matching WSI path.
3. Reinterpreting the handle's opaque payload back into the real,
   typed OS handle(s) that Vulkan's platform surface-creation function
   requires.
4. Calling the matching `vkCreate*SurfaceKHR` function to produce a
   `VkSurfaceKHR`, which becomes purely a `Vk*` type from that point on
   — already fully inside Vulkan Backend's existing, established
   Vulkan-type territory.
5. Nothing else. It does not create, resize, or destroy the native
   window; does not read window-manager state beyond what the handle
   already carries; does not run any OS event loop.

## Windows Behavior

A dedicated file (illustratively, `src/vulkan_backend/wsi/win32_surface.cpp`
— exact path is a future Plan decision, not fixed by this review)
includes `<vulkan/vulkan_win32.h>` (which requires `<windows.h>`-derived
`HWND`/`HINSTANCE`), reads `NativeWindowHandle` with
`kind == PlatformKind::Windows`, reinterprets its opaque payload back to
`HWND`/`HINSTANCE`, and calls `vkCreateWin32SurfaceKHR`. This file is the
only place in the entire graphics stack (Platform excepted) that has
`HWND`/`HINSTANCE` as visible types.

## Android Behavior

Symmetrically, a dedicated file (illustratively,
`src/vulkan_backend/wsi/android_surface.cpp`) includes
`<vulkan/vulkan_android.h>` (which requires `<android/native_window.h>`-
derived `ANativeWindow*`), reinterprets the opaque payload for
`kind == PlatformKind::Android`, and calls `vkCreateAndroidSurfaceKHR`.
Per [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)'s
already-established Ownership and Lifetime rules, Android's native window
can be destroyed and later recreated with a *new* handle independent of
app lifetime; this WSI code does not change that contract — it is simply
re-invoked (as part of a full `Presentation` teardown/rebuild, per
Spec 0002 and ADR-0002) whenever Runtime observes a new `SurfaceCreated`
handle. The WSI boundary does not need to know *why* it's being
re-invoked, only that it is.

## Future iOS Considerations

Not designed further here — this review only confirms the boundary
doesn't foreclose either iOS path
`docs/architecture/overview.md` already left open:

- **MoltenVK path**: an `src/vulkan_backend/wsi/ios_surface.mm`-shaped
  file, using `VK_EXT_metal_surface`/`VK_MVK_ios_surface`, reinterpreting
  the opaque payload as a `CAMetalLayer*` for `kind == PlatformKind::IOS`
  — fits this same WSI-boundary pattern directly.
- **Native Metal RHI backend path**: a wholly separate backend module
  (not Vulkan Backend at all) implementing RHI's interfaces directly in
  Metal — this review's Vulkan-specific WSI boundary is simply
  inapplicable to that path; a native Metal backend would define its own
  analogous internal boundary if and when that spec is written.

Neither is chosen here, consistent with [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)
leaving this open.

## Alternatives Considered

- **Have Platform itself create the `VkSurfaceKHR`.** Rejected outright:
  violates the explicit constraint that Platform must not depend on
  Vulkan, and would compromise Platform's reusability if a future
  non-Vulkan backend ever needed it.
- **A generic, cross-API "Surface abstraction" module between Platform
  and every graphics backend.** Rejected: explicitly out of scope per
  this task's constraints, and unjustified while exactly one graphics
  backend (Vulkan) exists — RHI's own `Presentation` interface (already
  established by ADR-0002) is already the correct cross-backend
  abstraction point; a second one would duplicate it for no current
  consumer.
- **Expose OS-specific types directly in RHI's public API** (e.g. a
  `Presentation::createFromWin32(HWND, HINSTANCE)` overload). Rejected:
  directly violates the explicit constraint that RHI's public API must
  stay platform-independent.
- **Let Renderer or RenderGraph branch on `PlatformKind` and dispatch
  accordingly.** Rejected: directly violates the explicit constraint that
  Renderer must never depend on platform APIs, and would introduce a
  platform abstraction inside the Renderer, which this task explicitly
  forbids.

## Recommended Decision

**Amend [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) in
place**, not supersede it and not create a new ADR, for one concrete
reason: ADR-0005's `Status` is still `Proposed`, not `Accepted`. Per
[adr/README.md](../adr/README.md), superseding (a new ADR replacing an
old one) is the mechanism for *changing a decision already `Accepted`*,
to preserve the historical record of what was actually decided at the
time. ADR-0005 hasn't reached that bar — the established practice already
used in this repository for ADR-0001 and ADR-0002 (both also still
`Proposed`) was a dated, in-place amendment, and the same treatment is
the smallest correct fix here.

**Proposed amendment (for a future edit — not made by this review):**
narrow ADR-0005's Decision bullet from an absolute "no OS-specific header
anywhere but Platform" to something like:

> Atlantis Platform owns all OS-specific window/surface *lifecycle and
> event-loop* code. No module other than Platform performs window
> creation, destruction, or OS event-loop handling. **A graphics
> backend's dedicated WSI boundary (e.g. Vulkan Backend's) may
> additionally include OS-specific headers strictly to satisfy that
> graphics API's own platform-surface extension functions** (e.g.
> `vulkan_win32.h`, `vulkan_android.h`), consuming the opaque native-
> window information Platform produces — without itself creating,
> destroying, or managing a window. Platform-specific types must not
> cross into RHI's public API, Renderer, or RenderGraph regardless.

If the human reviewer prefers not to touch ADR-0005's text at all even
while `Proposed`, the fallback is a new ADR that formally narrows
ADR-0005's scope without editing it — presented here as the alternative,
not the recommendation, since it's a larger artifact for the same effect.

**No additional ADR beyond this one amendment is required** for the WSI
boundary question itself. See Report, item 6, for a caveat on Spec
0002's own required-ADR list.

## Impact on Spec 0002

Minimal, and clarifying rather than corrective.
[specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)'s
actual design — the opaque, tagged `NativeWindowHandle` shape, the
Ownership and Lifetime table, the zero-extent and Android-recreation
rules — remains fully valid and unchanged; none of it assumed the now-
resolved header-visibility question either way. This review resolves
exactly the fourth item in that spec's "ADRs Required Before Approval"
list (the boundary clarification it explicitly flagged rather than
guessed at). Once ADR-0005 is actually amended, Spec 0002's text can be
lightly updated (not done by this review, per its explicit scope) to
drop the "flagged for ADR, not resolved here" hedge in its Architecture
/ Design Constraints section and cite the amended ADR-0005 directly.

## Impact on Future Vulkan RHI Spec

That spec (informally "Spec 0003" or whatever it ends up numbered)
inherits a settled answer instead of hitting this same wall itself. It
can define Vulkan Backend's internal structure — including a dedicated
`wsi/`-style boundary, per-OS files, and the "opaque handle in, VkSurfaceKHR
out" responsibility — directly, citing this review and the amended
ADR-0005, rather than re-deriving the reasoning or (worse) discovering
the conflict mid-implementation the way this review's trigger did.

## Decision

This review is **accepted**. Resolution:

- **[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) is
  amended in place** (it is still `Proposed`, so an in-place amendment is
  the correct mechanism per `adr/README.md` — no supersession needed).
  The amendment is made as part of this same resolution; see ADR-0005's
  own "Boundary (amended 2026-08-02 ...)" text.
- **No new WSI ADR is created.** The amended ADR-0005 fully covers the
  boundary; introducing a separate ADR for the same decision would only
  fragment the record.
- **Vulkan WSI is a private boundary inside the Vulkan Backend module —
  not a separate module, and not part of Platform.** It exists solely to
  translate an opaque native-window handle into a `VkSurfaceKHR`.
- **Vulkan WSI may consume `Platform::NativeWindowHandle`.** The handle
  is borrowed/non-owning; consuming it creates no ownership transfer and
  no dependency from Platform onto Vulkan Backend or Vulkan Backend onto
  the Platform module.
- **Platform remains completely independent of Vulkan.** No Vulkan
  header, no `Vk*` type, anywhere in Platform, before or after this
  resolution.
- **Generic RHI, Renderer, and RenderGraph remain platform-independent.**
  None of them gain any OS-specific type or header as a result of this
  resolution — the exception is scoped exclusively to Vulkan Backend's
  private WSI boundary.

This decision resolves the fourth item in
[specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)'s
"ADRs Required Before Approval" list; that spec's remaining required ADRs
are the other three (native window handle representation, application
lifecycle/event model, platform ownership model), all still unresolved
and unaffected by this review.
