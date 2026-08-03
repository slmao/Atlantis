# ADR 0011: Native Window Handle Representation

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _pending human review_
- **Related Spec:** [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)

## Context

`specs/0002-platform-foundation.md` needs a platform-neutral representation
for a native window handle: the value Atlantis Platform hands to Runtime,
which **transports it, without interpreting it,** to the active graphics
backend's private WSI boundary — for Phase 1, a future Vulkan Backend's
WSI layer (per [ADR-0005](0005-platform-module-multi-os-windowing.md), as
amended) — which alone consumes it to call
`vkCreateWin32SurfaceKHR`/`vkCreateAndroidSurfaceKHR` and produce a
`VkSurfaceKHR` that generic RHI's `Presentation` is then built from. The
handle is never a parameter of generic RHI's own public interface.
The spec's Architecture / Design Constraints section already proposed a
tagged, opaque-payload design and named this exact decision — native
window handle representation — as requiring its own ADR. This ADR is
that decision.

The representation must satisfy several requirements simultaneously:
Platform's public headers must stay free of Win32/Android NDK/future
UIKit headers; Renderer and RHI's public API must never see the handle
or any OS-specific type; the handle must identify which platform
produced it (so a consumer like Vulkan WSI knows how to interpret it);
and it must express "borrowed, not owned" cleanly, since — per
[specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)'s
Ownership and Lifetime table — Platform alone owns the underlying native
window, and a handle obtained once is not guaranteed to remain valid
(especially on Android).

## Decision

`NativeWindowHandle` is a small, trivially-copyable, tagged POD value
type, defined entirely in terms of primitive/opaque fields — no Win32,
Android NDK, or UIKit header is included to define it:

```cpp
enum class PlatformKind {
  Windows,
  Android,
  IOS,  // future — no implementation
};

struct NativeWindowHandle {
  PlatformKind kind;
  void* value0 = nullptr;  // Windows: HWND · Android: ANativeWindow* · iOS (future): CAMetalLayer*-equivalent
  void* value1 = nullptr;  // Windows: HINSTANCE · Android/iOS: unused (nullptr)
};
```

- **Representation:** exactly the struct above — a `PlatformKind` tag
  plus two opaque pointer-sized fields. No `std::variant`, no
  inheritance, no heap allocation, no per-platform conditional
  compilation inside the struct's own definition (its size and layout
  are identical on every target).
- **Ownership:** `NativeWindowHandle` never owns anything. It is a
  **borrowed, non-owning** view onto a native window Platform owns.
  Copying a `NativeWindowHandle` copies two pointer-sized values and a
  tag; it never extends, transfers, or implies any lifetime guarantee.
- **Lifetime:** the underlying native window's lifetime is governed
  entirely by Platform (Windows) or the OS framework (Android) — see
  [ADR-0013](0013-platform-window-ownership-and-lifetime.md). A
  `NativeWindowHandle` value itself has no destructor and needs none;
  what can become stale is not the value but the thing it points at.
- **Validity:** not encoded in the handle itself. Callers must not cache
  a `NativeWindowHandle` across frames and assume it stays valid.
  Validity is governed entirely by **Platform's lifecycle state**: a
  `SurfaceCreated` event (see
  [ADR-0012](0012-application-lifecycle-and-event-model.md)) establishes
  a currently-valid native-window identity and the `NativeWindowHandle`
  that goes with it; a subsequent `SurfaceDestroyed` **invalidates that
  identity**; a later `SurfaceCreated` provides a **new** identity, never
  a continuation of the old one (see
  [ADR-0013](0013-platform-window-ownership-and-lifetime.md)). Nothing on
  the handle itself is inspectable to determine validity — the
  `SurfaceCreated`/`SurfaceDestroyed` event stream is the sole source of
  truth.
- **Platform identification:** the `kind` field. Any consumer (in
  practice, only Vulkan Backend's WSI boundary) branches on `kind` to
  know which of `value0`/`value1` means what, and which platform WSI
  extension to call — without needing to ask Platform anything further.
- **API visibility:** `NativeWindowHandle` is defined in Platform's
  public header and requires no OS SDK header to compile. **It is not
  part of generic RHI's public API** — no generic RHI interface method
  accepts or returns it, and generic RHI's public headers never expose
  it. It may be **transported** from Platform through Runtime (carried on
  a `PlatformEvent`, per
  [ADR-0012](0012-application-lifecycle-and-event-model.md)); Runtime
  forwards it by value, unchanged, to whichever graphics backend is
  active, but **does not interpret** `value0`/`value1` itself. It is
  **consumed only by the active graphics backend's private WSI
  boundary** (for Phase 1, Vulkan Backend's WSI layer), which alone
  reinterprets `value0`/`value1` to construct a backend-specific surface
  object (e.g. `VkSurfaceKHR`) that generic RHI's `Presentation` is built
  from afterward. Renderer and RenderGraph never see it.
- **Thread-safety assumptions:** `NativeWindowHandle` is a plain value
  with no shared mutable state, so the type itself raises no
  thread-safety question. What matters is where it's produced and
  consumed: per the Phase 1 single-application-thread baseline
  ([ADR-0004](0004-phase1-threading-baseline.md)), a `NativeWindowHandle`
  is produced by Platform and consumed by Runtime/Vulkan WSI entirely on
  that one thread. This ADR does not extend validity guarantees to any
  other thread.
- **Future iOS compatibility:** `PlatformKind::IOS` is reserved now. The
  two-opaque-field payload accommodates a future `CAMetalLayer`-equivalent
  pointer in `value0` (with `value1` unused) without changing the
  struct's shape — whether iOS ends up using this via a MoltenVK path
  inside Vulkan Backend's WSI boundary or a separate native Metal RHI
  backend (both left open by [ADR-0005](0005-platform-module-multi-os-windowing.md))
  is not decided here.

This ADR does not design any Vulkan-side API, function, or type — how
Vulkan Backend's WSI boundary consumes `NativeWindowHandle` to produce a
`VkSurfaceKHR` is left to a future Vulkan RHI spec, constrained only by
what this ADR fixes about the handle itself.

## Rationale

The chosen representation is the smallest one that satisfies every
requirement simultaneously: it needs no heap allocation or indirection
(unlike an opaque object or integer-handle-into-a-table), no virtual
dispatch (unlike typed subclasses), and no conditional compilation inside
its own header (unlike a `std::variant` over per-platform structs, whose
alternatives would need to exist as real or forward-declared types). A
trivially-copyable POD is also the most natural expression of "borrowed,
not owned" — there is nothing to destroy, so there is nothing to get
wrong by copying it freely.

## Alternatives Considered

- **Typed platform-specific classes** (e.g. a polymorphic
  `NativeWindowHandle` base with `WindowsNativeWindowHandle`/
  `AndroidNativeWindowHandle` subclasses). Rejected: implies heap
  allocation and virtual dispatch for a value that should be cheap and
  stack-friendly, and each subclass's header would need to declare
  typed OS members somewhere, in tension with keeping Platform's public
  headers OS-header-free.
- **A single untagged `void*` payload, no `PlatformKind`.** Rejected
  (also rejected in the spec's own Alternatives): a consumer has no way
  to know which WSI extension to call without an out-of-band query,
  reintroducing implicit coupling.
- **An integer handle** (e.g. a `uint64_t` opaque ID resolved via a
  lookup back into Platform). Rejected: adds an indirection and a
  dependency — Vulkan WSI would need to call back into the Atlantis
  Platform module's API to resolve the ID, which conflicts with Vulkan
  Backend not depending on the Platform module (per
  [ADR-0005](0005-platform-module-multi-os-windowing.md)).
- **An opaque object** (a Platform-owned type behind a pointer,
  effectively a PIMPL). Rejected: heavier than needed for "one or two
  pointer values," and works against the "trivially copyable, no
  lifetime to manage" property a value type gives for free.
- **`std::variant<WindowsHandle, AndroidHandle, IosHandle>`** with
  per-platform structs as alternatives. Rejected: the variant's
  alternative types would need to be visible (even if forward-declared)
  wherever the variant itself is defined, which tends to leak
  platform-conditional compilation into the shared public header — the
  exact thing the two-opaque-field POD design avoids by having an
  identical shape on every platform.

## Consequences

### Positive

- Zero-cost, trivially copyable, no allocation, no dispatch — the
  cheapest possible representation for what is fundamentally "one or two
  pointer values plus a tag."
- Platform's public header needs no OS SDK header to define this type,
  satisfying the "RHI/Renderer must not need Win32/Android headers
  unnecessarily" requirement structurally, not by convention.
- Uniform shape across every platform means no `#ifdef` is needed inside
  the type's own definition, keeping it trivial to reason about from
  any consumer.

### Negative / Trade-offs

- Two generically-named opaque fields (`value0`/`value1`) carry
  platform-dependent meaning documented only in comments, not enforced
  by the type system — a future WSI implementation must get the
  reinterpretation right per platform; nothing at compile time prevents
  misreading `value0` as the wrong OS's handle if code branches
  incorrectly on `kind`.
- Fixed at two opaque fields; if some future platform genuinely needs a
  third distinct native value, this ADR would need revisiting (not
  anticipated for Windows/Android/iOS as currently understood, but
  flagged as a real constraint, not a hidden one).

## Constraints on Future Specs

Any future RHI/Vulkan spec must:
- Accept `NativeWindowHandle` exactly as defined here **only at the
  active graphics backend's private WSI construction entry point** (e.g.
  Vulkan Backend's WSI boundary) — **never as a parameter on generic
  RHI's public `Presentation` interface itself** — and must not require
  Platform to expose any additional typed accessor.
- Ensure Runtime transports `NativeWindowHandle` (e.g. via
  `PlatformEvent`) without interpreting `value0`/`value1` — only the
  active backend's private WSI boundary may do so.
- Reinterpret `value0`/`value1` only within that private WSI boundary
  (per [ADR-0005](0005-platform-module-multi-os-windowing.md) as
  amended) — never in generic RHI's public API, Renderer, or
  RenderGraph.
- Not treat a `NativeWindowHandle` as valid beyond the
  `SurfaceCreated`-to-`SurfaceDestroyed` window in which it was obtained.
  Validity tracking is the `SurfaceCreated`/`SurfaceDestroyed` event pair
  and Platform's lifecycle state — not a method call on the handle or on
  any window object.

## Related Specs

- [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)

## Related ADRs

- [ADR-0001](0001-rhi-backend-independence.md) — RHI/Renderer backend
  and platform independence, which this ADR's API-visibility rule
  upholds for the native-handle case specifically.
- [ADR-0005](0005-platform-module-multi-os-windowing.md) — introduces
  Atlantis Platform and (as amended) the Vulkan WSI boundary that
  consumes this handle.
- [ADR-0010](0010-cmake-structure.md) — the module/namespace convention
  (`atlantis::platform`, public headers under `include/atlantis/...`)
  this type's home follows.
