# ADR 0008: Logging Abstraction — Minimal, Standard-Library-Backed

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _Human approval confirmed 2026-08-04_
- **Related Spec:** [specs/0001-project-foundation.md](../specs/0001-project-foundation.md)

## Context

`specs/0001-project-foundation.md` requires a logging abstraction:
leveled logging with at least one default sink, used by the proof
executable. Its Architectural Impact section flags "logging abstraction
design" as requiring its own ADR.
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md)
proposed a concrete design pending this ADR; this ADR is that decision.

Requirements driving this decision: a simple API; low coupling (no
Renderer dependency — logging is `Atlantis Core`, usable by every module
without pulling anything render-specific in); no adoption of a complex
global third-party logging framework; a replaceable sink/backend; must
work on both primary target platforms (Windows and Android — see
[AGENTS.md](../AGENTS.md) Phase 1 constraints); and no premature
asynchronous logging infrastructure (no background thread, no queue) —
consistent with [threading.md](../docs/architecture/threading.md)'s
"no speculative abstraction ahead of a need" principle.

## Decision

Atlantis provides its own minimal logging abstraction in `Atlantis Core`
(namespace `atlantis` — see [ADR-0010](0010-cmake-structure.md) for the
namespace decision), backed by the C++ standard library, using
`std::format` (C++20) for message formatting where the project's
toolchain supports it — avoiding a third-party formatting dependency
(e.g. `fmt`) for this need.

- **Levels:** `enum class LogLevel { Trace, Debug, Info, Warn, Error,
  Fatal };`.
- **Sink abstraction:** an abstract `LogSink` interface
  (`write(LogLevel, std::string_view message)`), replaceable at runtime.
  Default implementation (`ConsoleLogSink`) writes `Warn`/`Error`/`Fatal`
  to stderr and everything below to stdout — a reasonable Windows
  console default. This is the seam a future Android-specific sink
  (routing through `<android/log.h>`'s `__android_log_print`, so output
  reaches `adb logcat`) plugs into later, without changing the public
  logging API — not implemented now, no Android build support exists yet
  per the spec's Non-Goals.
- **Access:** a narrow, deliberate singleton
  (`atlantis::log::initialize(sink)` / `atlantis::log::instance()`) — an
  explicit, scoped exception to AGENTS.md's "no global mutable
  singletons" rule, which already names logging/diagnostics
  infrastructure as an allowed exception.
- **Call sites:** macros (`ATLANTIS_LOG_TRACE` .. `ATLANTIS_LOG_FATAL`)
  using `std::format` for the message and `std::source_location` to
  capture file/line automatically.
- **Level filtering:** a runtime-configurable minimum level, checked
  before formatting, so filtered-out calls pay no formatting cost.
- **Synchronous only:** every log call performs its sink write inline, on
  the calling thread — no background thread, no queue, no deferred flush.
  Calls are safe to make concurrently from multiple threads (an internal
  mutex serializes sink dispatch), but "thread-safe" here means "safe to
  call from any thread," not "asynchronous." This satisfies the
  "no premature async logging" requirement directly.

## Consequences

### Positive

- No new third-party dependency: `std::format`/`std::source_location` are
  standard C++20, keeping this module dependency-free like the rest of
  `Atlantis Core`.
- Renderer and every other module can log without any coupling to a
  render-specific or platform-specific type — the sink is the only
  platform-varying piece, and it varies behind an interface, not in the
  call-site API.
- The sink seam means Windows-vs-Android behavior is a future
  implementation detail, not a design change, when Android build support
  lands.
- Synchronous-by-default keeps Phase 1 simple and matches the project's
  general "don't build threading machinery ahead of a need" stance.

### Negative / Trade-offs

- Synchronous logging could become a throughput bottleneck if called from
  a future hot path (e.g. per-frame render diagnostics) — not a concern
  today (nothing renders yet), but a known limitation, not an oversight.
- No structured logging (key-value fields) — messages are plain formatted
  strings only. Adequate for Phase 1's actual need; a real limitation if
  future tooling wants machine-parseable logs.
- No file sink, no log rotation, no per-category/per-subsystem filtering
  in this design — only a global minimum level and whatever `LogSink`
  implementations are written. Any of these can be added later as new
  `LogSink` implementations without changing the public API, but none
  exist now.
- The Android sink's actual behavior is documented intent, not verified
  design — it cannot be validated until Android build support exists.

### Limitations and future migration path

If high-throughput or asynchronous logging is ever needed (e.g. once
real-time rendering exists and per-frame diagnostics become a concern),
the `LogSink` interface is the intended extension seam: an async sink
(internal queue + background writer thread) could be introduced as a new
`LogSink` implementation without changing the public logging API or
call-site macros. That migration is explicitly out of scope now — it
introduces a threading model this ADR deliberately avoids — and would
need its own spec/ADR when a real need exists, not a speculative design
here.

## Alternatives Considered

- **Wrap a third-party logging library (e.g. spdlog).** Rejected for
  Phase 1: spdlog is a capable, complex library (formatting, async
  sinks, pattern-based configuration) that exceeds "simple API, low
  coupling" for a foundation-stage need of "leveled logging with a
  replaceable sink." Revisiting this is not precluded if Atlantis's
  logging needs grow substantially, but that would be a new decision
  with its own ADR, not assumed now.
- **`fmt` library instead of `std::format`.** Rejected: `std::format` is
  part of C++20, already the project's language standard, and avoids
  adding a dependency for formatting specifically — see
  [ADR-0006](0006-dependency-management.md)'s "keep dependencies
  minimal."
- **Asynchronous logging from the start (background thread + queue).**
  Rejected per explicit requirement to avoid premature async
  infrastructure, and per [threading.md](../docs/architecture/threading.md)'s
  Phase 1 single-frame-thread baseline — nothing today needs it, and
  building it now would be exactly the "speculative abstraction ahead of
  a need" [AGENTS.md](../AGENTS.md) warns against.
- **No abstraction at all — call `std::cout`/`printf`/platform log
  functions directly at call sites.** Rejected: fails "replaceable
  sink/backend" and "suitable for Windows and Android" outright — direct
  calls can't be redirected per-platform or intercepted for testing
  without a wrapping abstraction, which is exactly what this ADR defines.
