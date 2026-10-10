# ADR 0116: Tracy Profiler as a Third-Party Dependency

- **Status:** Accepted
- **Date:** 2026-10-10 (accepted 2026-10-10)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-10 (chat rulings on Spec 0059 Q1–Q12; review of
  Spec 0059's own branch PR, [PR #242](https://github.com/slmao/Atlantis/pull/242)).
- **Related Spec:** [Spec 0059](../specs/0059-profiling-instrumentation.md) (`Approved`)
- **Related ADR(s):**
  - [ADR-0006](0006-dependency-management.md): small pinned source dependencies
    use `FetchContent`; tools that are installed on the host are not fetched.
  - Selection manner of [ADR-0107](0107-editor-ui-library-selection.md) and
    [ADR-0082](0082-gltf-parser-dependency-selection.md).
  - Paired with [ADR-0114](0114-profiling-api-build-configuration-and-recorder.md)
    and [ADR-0115](0115-rhi-instrumentation-boundary.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- The owner asks for Tracy CPU and GPU timelines. Tracy has two parts: a client
  library compiled into the profiled program, and a separate viewer application.
- Facts checked 2026-10-10 against the upstream repository: latest release
  `v0.14.1` (2026-08-22), 3-clause BSD license; the Vulkan integration
  (`TracyVulkan.hpp`) needs Vulkan headers included first, defines no-op macros
  when `TRACY_ENABLE` is absent, supports calibrated timestamps through
  `VK_EXT_calibrated_timestamps`; the viewer is released as a prebuilt
  `windows-0.14.1.zip`.
- The viewer and client must be the same protocol version.
- A profiler client opens a network listener; the engine's precedent for
  network surface is loopback only (Spec 0055).
- Only the Vulkan Backend may include Vulkan headers; Tracy's Vulkan header
  includes them.

## Decision

1. **Tracy `v0.14.1`**, acquired by `FetchContent` from the release archive by
   URL and `URL_HASH SHA256=…` (the `cmake/AtlantisImGui.cmake` shape; the hash
   is measured against that exact archive in the Plan's first milestone). Not
   fetched unless `ATLANTIS_PROFILING` and `ATLANTIS_PROFILING_TRACY` are both on.
2. **Built as a private static library `atlantis_tracy`** from
   `public/TracyClient.cpp`, third-party warnings excluded (`SYSTEM`), with
   `TRACY_ENABLE`, `TRACY_ON_DEMAND` and `TRACY_ONLY_LOCALHOST`.
3. **Include boundary.** Tracy headers are included only by Core's profile
   implementation (through Tracy's C API, so `profile.h` exposes a plain
   source-location struct) and by one Vulkan Backend file that owns the Tracy
   Vulkan context. A boundary test scans for any other include.
4. **The viewer is a developer-installed tool**, in ADR-0006's host-tool
   category: not fetched by CMake. The matching release asset is documented in
   `docs/process/`; a version mismatch is a developer-visible refusal to connect.
5. **GPU context.** The backend creates a calibrated Tracy Vulkan context when
   `VK_EXT_calibrated_timestamps` is available and an uncalibrated one
   otherwise; Atlantis's own timestamps (ADR-0115) are independent of it.
6. **Threading.** Tracy's internal thread is third-party implementation detail;
   it adds no engine threading model and no engine API takes a thread.
7. **Android** never declares the target.

## Consequences

### Positive

- Live CPU/GPU timeline, counter plots and tooling with a pinned, reproducible
  build and no change to default builds.
- Business code cannot depend on Tracy by construction.

### Negative / Trade-offs

- A new third-party dependency with its own release cadence; upgrading means a
  new pin and a matching viewer.
- First profiling configure needs network (like the first Catch2 and ImGui
  fetches); `-DATLANTIS_PROFILING_TRACY=OFF` avoids it and the recorder still works.
- Tracy's client adds a thread and a loopback listener to profiling builds.
- Two GPU timing paths in profiling builds (ADR-0115).

## Alternatives Considered

- **Vendored source** and **git submodule** — rejected by ADR-0006.
- **System-installed Tracy** — unpinned, not reproducible.
- **Fetching the viewer too** — a large prebuilt GUI binary is not a build input;
  developers already install host tools (ADR-0006).
- **Another profiler** (Optick, Superluminal, Remotery, PIX markers) — weaker or
  non-portable GPU timeline support, or proprietary; none is better integrated
  with Vulkan GPU zones than Tracy.
- **No third-party profiler** (recorder plus a Chrome-trace export) — zero
  dependency but no live GPU timeline or plots; retained as the fallback if this
  ADR is rejected, and supported by ADR-0114's recorder.
