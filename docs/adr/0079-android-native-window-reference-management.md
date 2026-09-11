# ADR 0079: Android Native Window Reference Management

- **Status:** Accepted
- **Date:** 2026-09-12
- **Deciders:** slmao
- **Acceptance:** Human approval confirmed 2026-09-12 (chat confirmation);
  no reviewing PR yet — see [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)'s
  own Approval note.
- **Related Spec:** [specs/0034-android-platform-vulkan-presentation.md](../specs/0034-android-platform-vulkan-presentation.md)

## Context

[ADR-0013](0013-platform-window-ownership-and-lifetime.md) established that
the Android framework owns `ANativeWindow`'s lifetime completely, that
Atlantis Platform never destroys it, and that "Vulkan WSI may temporarily
retain an `ANativeWindow` reference when its implementation requires doing
so… following Android's own NDK acquire/release discipline… A future Android
Platform implementation must pair any retained reference with the matching
release, exactly as Android's platform contract requires; this ADR does not
itself design that mechanism, only mandates that it be followed." That
mechanism is what this ADR designs, for both the two places an
`ANativeWindow*` is actually held in this Spec's design: Android Platform
itself (which observes it via `APP_CMD_INIT_WINDOW`/`APP_CMD_TERM_WINDOW`,
per [ADR-0077](0077-android-native-entry-point-and-process-model.md)) and
Vulkan Backend's Android WSI boundary (which consumes it once, to call
`vkCreateAndroidSurfaceKHR`).

## Decision

- **Android Platform does not call `ANativeWindow_acquire`/`_release` at
  all.** `android_native_app_glue` already guarantees `app->window` is a
  valid, non-null `ANativeWindow*` for the exact interval between the
  `APP_CMD_INIT_WINDOW` command it posts and the matching
  `APP_CMD_TERM_WINDOW` command — this is the glue library's own documented
  contract, not something Atlantis needs to separately enforce. Android
  Platform's `SurfaceCreated`/`SurfaceDestroyed` `PlatformEvent` pair is
  reported in exact lockstep with that same interval (per
  [ADR-0077](0077-android-native-entry-point-and-process-model.md)'s mapping
  table), so `NativeWindowHandle`'s existing validity contract
  ("valid exactly between a `SurfaceCreated` and its matching
  `SurfaceDestroyed`," per [ADR-0013](0013-platform-window-ownership-and-lifetime.md))
  already coincides precisely with the pointer's real validity window.
  Android Platform stores `app->window` verbatim as `NativeWindowHandle::value0`
  without taking any additional reference.
- **Vulkan Android WSI does not call `ANativeWindow_acquire`/`_release`
  either.** Per the Vulkan specification's `VK_KHR_android_surface` extension,
  `vkCreateAndroidSurfaceKHR` itself internally acquires an implementation-
  managed reference to the `ANativeWindow` it is given, held until the
  returned `VkSurfaceKHR` is destroyed via `vkDestroySurfaceKHR` — Vulkan's
  own WSI implementation, not Atlantis code, is responsible for that
  reference's lifetime. `android_surface.cpp`'s `createAndroidSurface()`
  (mirroring `createWin32Surface()`) reads the borrowed `ANativeWindow*` from
  `NativeWindowHandle::value0` and passes it directly to
  `vkCreateAndroidSurfaceKHR`, taking no reference of its own — consistent
  with [ADR-0013](0013-platform-window-ownership-and-lifetime.md)'s existing
  rule that Vulkan WSI's use of a retained reference is "a lifetime/
  reference-management mechanism, not ownership," here resolved to "no
  additional reference beyond what Vulkan's own driver already manages."
- **This satisfies [ADR-0013](0013-platform-window-ownership-and-lifetime.md)'s
  "any acquire must have a matching release" mandate by construction**: no
  code in this repository performs a manual acquire, so there is no manual
  release to pair it with — the only two references in play (glue's own
  internal handling, and the Vulkan driver's internal handling per the WSI
  extension spec) are both managed entirely outside Atlantis's own code, by
  contracts Atlantis does not need to duplicate.
- **Ordering discipline, restated precisely for implementers**: a
  `VkSurfaceKHR` built from a given `ANativeWindow*` must be destroyed (via
  `Presentation`'s existing teardown path, triggered by the observed
  `SurfaceDestroyed` event) no later than the frame in which
  `APP_CMD_TERM_WINDOW` is observed — never held past it — since the pointer
  itself becomes invalid at that point regardless of Vulkan's own internal
  reference bookkeeping (Vulkan can only extend how long the *system
  resource* the pointer denotes stays alive for its own presentation
  purposes; it cannot make a dangling `ANativeWindow*` value on Atlantis's
  own side valid to read again). This is the same "`SurfaceDestroyed` teardown
  before rebuild" ordering [ADR-0013](0013-platform-window-ownership-and-lifetime.md)
  already mandates generically; this ADR adds no new ordering rule, only
  confirms it applies unchanged to Android's concrete pointer type.

## Consequences

### Positive

- Zero new reference-counting code in Atlantis — the two existing external
  contracts (`android_native_app_glue`'s command/window-validity pairing,
  Vulkan's own `VK_KHR_android_surface` internal reference management)
  already cover exactly what was needed, so there is nothing extra to get
  wrong.
- Directly satisfies [ADR-0013](0013-platform-window-ownership-and-lifetime.md)'s
  deferred obligation without introducing a new, Atlantis-owned lifetime
  concept that would need its own testing and documentation.

### Negative / Trade-offs

- This decision's correctness rests on two external contracts (the glue
  library's window-validity guarantee, and the Vulkan spec's internal
  `ANativeWindow` reference handling) that this ADR does not itself enforce
  in code — if either contract is misunderstood or a future NDK/driver
  update behaves differently, the failure mode is a use-after-free or
  dangling-pointer bug that no compile-time check catches. Implementers must
  verify both claims against the actual NDK/Vulkan SDK versions used at
  build time, not merely trust this ADR's restatement of them.
- Relies on `APP_CMD_TERM_WINDOW`/`SurfaceDestroyed` being processed and
  acted upon (full `Presentation` teardown) within the same `processEvents()`
  call that observes it — any deferral of that teardown reintroduces exactly
  the dangling-pointer risk this decision otherwise avoids. This must be an
  explicit implementation/testing focus at Plan time.

## Alternatives Considered

- **Android Platform manually calls `ANativeWindow_acquire()` on
  `APP_CMD_INIT_WINDOW` and `ANativeWindow_release()` on
  `APP_CMD_TERM_WINDOW`**, holding its own reference for the observed
  validity window. Rejected: redundant given `android_native_app_glue`
  already guarantees the pointer's validity for exactly that interval — an
  extra manual reference would only add code with no additional safety
  benefit, and a mismatched acquire/release pair (e.g. an early return
  skipping the release) would be a new bug class this Spec would otherwise
  never need to guard against.
- **Vulkan WSI manually acquires before `vkCreateAndroidSurfaceKHR` and
  releases after `vkDestroySurfaceKHR`.** Rejected for the same reason: the
  Vulkan specification already documents `vkCreateAndroidSurfaceKHR`/
  `vkDestroySurfaceKHR` as internally managing their own reference to the
  `ANativeWindow`; adding a redundant manual acquire/release around a call
  that already does so does not increase correctness and adds a second place
  a reference-count mismatch could occur.
- **A shared, Atlantis-owned refcounted wrapper type around
  `ANativeWindow*`**, used uniformly by both Platform and Vulkan WSI.
  Rejected: [ADR-0013](0013-platform-window-ownership-and-lifetime.md)
  already explicitly rejected "a reference-counted common ownership model
  spanning both platforms" for the broader window-ownership question; this
  ADR does not reopen that decision for the narrower `ANativeWindow`
  reference-counting question either, since neither consumer in this
  design actually needs to hold a reference independent of the two external
  contracts already covering it.
