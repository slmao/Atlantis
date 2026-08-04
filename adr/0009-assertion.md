# ADR 0009: Assertion Semantics — ATLANTIS_ASSERT and ATLANTIS_CHECK

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _Human approval confirmed 2026-08-04_
- **Related Spec:** [specs/0001-project-foundation.md](../specs/0001-project-foundation.md)

## Context

`specs/0001-project-foundation.md` requires an assertion abstraction
consistent with [AGENTS.md](../AGENTS.md)'s "programmer errors are
assertions, not error returns" rule, and its Architectural Impact section
flags "assertion abstraction design" as requiring its own ADR.
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md)
proposed a concrete design pending this ADR; this ADR is that decision.
The failure mechanism must be testable and replaceable (a failing
assertion normally terminates the process, which is otherwise impossible
to unit-test), and exceptions must not be the default mechanism —
consistent with AGENTS.md's error-handling rule that Core/RHI/RenderGraph/
Renderer/Runtime use explicit result/error types rather than exceptions,
and with Vulkan's own non-exception (`VkResult`) error model that later
Phase 1 work will integrate with.

## Decision

Two macros, matching two distinct needs:

- **`ATLANTIS_ASSERT(condition)` / `ATLANTIS_ASSERT_MSG(condition, msg)`**
  — Debug/development invariant checking. **Compiles out entirely in
  Release** (the condition is not evaluated), so assert conditions must
  be free of required side effects — standard `assert`-style discipline.
- **`ATLANTIS_CHECK(condition)` / `ATLANTIS_CHECK_MSG(condition, msg)`**
  — **always evaluated**, in both Debug and Release, for conditions that
  must remain validated in production.

**Failure handling (both macros, no exceptions):** on failure, the
condition text, an optional message, and source location
(`std::source_location`) are logged via `ATLANTIS_LOG_FATAL` (see
[ADR-0008](0008-logging.md), so output reaches whatever sink is active —
console on Windows, the future Android log sink once it exists), then a
**replaceable failure handler** is invoked — a swappable
`std::function`-based hook, defaulting to "log, then `std::abort()`."
**No C++ exception is thrown by either macro.**

- **Debug behavior:** both macros active. On failure, the default handler
  may trigger a debugger breakpoint (a platform intrinsic, e.g.
  `__debugbreak()` on Windows/MSVC) before aborting, when a debugger is
  likely attached — a nice-to-have, not required for correctness.
- **Release behavior:** `ATLANTIS_ASSERT` is a zero-cost no-op.
  `ATLANTIS_CHECK` still evaluates and still fails fatally — Release
  removes development-only overhead, not the safety net `CHECK` provides.
- **Fatal/non-fatal semantics:** both macros are unconditionally fatal on
  failure by default (log, then abort via the failure handler). There is
  no built-in "log and continue" tier — see Alternatives Considered.
- **Testability:** the failure handler is swappable
  (`atlantis::assertions::setFailureHandler(...)`), so test code can
  substitute a handler that records the failure (condition text,
  message) instead of terminating the process. This is what makes it
  possible for unit tests to verify `ATLANTIS_CHECK`'s firing logic
  without crashing the test binary, per the Unit Test Structure already
  proposed in [plans/0001-project-foundation.md](../plans/0001-project-foundation.md).
- **Windows behavior:** default handler logs then calls `std::abort()`;
  in Debug, `__debugbreak()` first when reasonable, so the failure is
  caught at its exact call site under a debugger rather than in a generic
  abort handler.
- **Android behavior:** identical macros and semantics. The default
  handler is expected to route its log output through the Android log
  sink (once it exists — see [ADR-0008](0008-logging.md)) so failures
  reach `adb logcat`, since Android has no equivalent of a Windows
  message-box/debugger-break UX by default and stdout/stderr alone may
  not be visible. **This is documented intent, not implemented or
  verified behavior** — no Android build support exists yet per the
  spec's Non-Goals; this ADR records what the eventual Android-specific
  wiring should do, not a design decided ahead of that future spec.

## Consequences

### Positive

- Matches AGENTS.md's error-handling rule directly: assertions are
  reserved for programmer errors/invariant violations, distinct from the
  Result/error-type path already designated for recoverable runtime
  errors.
- Safety-critical checks (`ATLANTIS_CHECK`) survive into Release without
  Debug-only assertions paying their overhead there.
- The swappable failure handler solves production customization (e.g. a
  future crash-reporting hook) and unit testability with one mechanism,
  rather than inventing a second code path for tests.

### Negative / Trade-offs

- Two macros to choose between correctly; using `ATLANTIS_ASSERT` for a
  check that actually needed to survive Release silently removes a safety
  check in shipping builds. Mitigated by documentation and naming, not by
  the macros themselves.
- No exception-based unwinding means a failure is always a hard stop by
  default — it cannot be "caught" and handled higher up the call stack
  without installing a custom failure handler. Deliberate, per the
  no-exceptions requirement, but a real constraint on what error recovery
  can look like in code using these macros.
- Android's logcat-routing behavior is aspirational until Android build
  support actually exists to verify it against.

## Alternatives Considered

- **A single always-on assert macro, no Debug/Release distinction.**
  Rejected: loses the zero-cost Debug-only invariant-checking use case
  that has real value during development without paying its cost in
  shipping builds, and doesn't match the explicit Debug-vs-Release
  requirement.
- **Exceptions as the default failure mechanism.** Rejected per explicit
  requirement and AGENTS.md's exception-free-render-path rule; an
  exception-based assert also sits awkwardly next to Vulkan's
  non-exception `VkResult` error model that later Phase 1 work integrates
  with, and doesn't suit an invariant violation that should be
  unconditionally fatal regardless of how far up the stack a `catch`
  might be.
- **A third "soft assert" tier that logs but doesn't abort.** Considered,
  not adopted: not requested by the spec, and would blur the line between
  "this is a bug, stop" (the two macros here) and "this is a recoverable
  runtime error" (the Result/error-type path AGENTS.md already assigns to
  that case). Keeping exactly two tiers preserves that distinction.
- **Wrapping a third-party assertion library.** Rejected for the same
  "keep dependencies minimal" reasoning as [ADR-0006](0006-dependency-management.md)
  — the macro/handler design here is small enough to own directly, and
  owning it keeps the testable-failure-handler hook exactly matched to
  Atlantis's own test framework ([ADR-0007](0007-test-framework.md))
  rather than adapting to someone else's.
