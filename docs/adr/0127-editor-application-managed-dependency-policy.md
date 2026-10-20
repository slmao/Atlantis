# ADR 0127: The Editor Application's Managed Dependency Policy

- **Status:** Proposed
- **Date:** 2026-10-20
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0064: Editor Platform Direction](../specs/0064-editor-platform-direction.md) (`Draft`; its Q4)
- **Related ADR(s):**
  - Follows the pinning precedent of [ADR-0006](0006-dependency-management.md),
    [ADR-0107](0107-editor-ui-library-selection.md) (Dear ImGui, URL + SHA-256) and
    [ADR-0116](0116-tracy-profiler-dependency.md) (Tracy, URL + SHA-256).
  - Keeps [ADR-0113](0113-dotnet-toolchain-and-build-integration.md) D2 (zero NuGet) for the Atlantis C# SDK, its
    tests and examples, unchanged; D2 here is a separate rule for one separate module.
  - Pairs with [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **Atlantis Editor App is an Avalonia application** (ADR-0125 D1). Avalonia ships as NuGet packages; the editor
  cannot exist under zero NuGet.
- **Zero NuGet** (ADR-0113 D2) is enforced by a repository-root `NuGet.config` that clears every source and by
  `tests/csharp/check_csharp_rules.cmake`, which scans `src/csharp/**`, `examples/csharp/**` and `tests/csharp/**`.
- **Every native dependency is pinned to exact bits:** FetchContent by tag or by URL and SHA-256; toolchains are
  installed, never fetched (ADR-0006).
- **NuGet natively supports exact pinning:** `packages.lock.json` records every resolved package (direct and
  transitive) with its exact version and content hash; `RestoreLockedMode` makes a restore fail on any drift.

## Decision

1. **Scope.** This policy covers `src/editor_app/**` and its own tests only. The C# SDK keeps zero NuGet; the SDK's
   rules check is unchanged and keeps passing.
2. **Exact pins with hashes.**
   - Every `PackageReference` names an exact version (no ranges, no floating versions), centralized in one
     `Directory.Packages.props` under `src/editor_app/`.
   - `packages.lock.json` is committed for every project; `RestorePackagesWithLockFile` and `RestoreLockedMode`
     are on, so any version or content-hash drift fails the restore.
3. **Sources.** `src/editor_app/NuGet.config` clears inherited sources and adds exactly one: nuget.org, or the
   directory named by `ATLANTIS_NUGET_OFFLINE_FEED` when set (an offline mirror holding the locked `.nupkg` files).
   The repository root's `NuGet.config` is not changed.
4. **An allow-list.** The direct packages the editor may reference are listed in this policy's companion file
   (`src/editor_app/allowed_packages.txt`, reviewed like source). Phase 1 allows Avalonia's own packages only
   (the application, desktop and theme packages it needs); adding a package to the list is a reviewed change with
   its licence named.
5. **A check.** A CMake script (the `check_csharp_rules.cmake` shape) fails when a project under `src/editor_app/`
   has a `PackageReference` without an exact version, has no committed lock file, references a package not on
   the allow-list, or when `NuGet.config` names any other source.
6. **Build integration.** CMake builds the editor with `dotnet build` like the SDK (ADR-0113 D3), gated on the
   .NET SDK as the SDK is (ADR-0113 D4), plus a separate option `ATLANTIS_BUILD_EDITOR_APP` (default OFF until
   Spec 0064 Phase 1 passes its checkpoint), so a default configure fetches nothing new. Android never declares it.
7. **Offline reproducibility** is part of the gate: the Phase 1 PR records a build with the network off from an
   offline feed populated from the lock file.

## Consequences

### Positive

- **Exact, verifiable bits** for managed dependencies, as for native ones.
- **The SDK's zero-NuGet guarantee is untouched.**
- **Default builds are unchanged** until the editor is switched on.

### Negative / Trade-offs

- **Third-party managed code enters the repository's build** for the first time, with a transitive graph to
  review on each upgrade.
- **Two NuGet policies** in one repository, separated by directory.
- **An offline feed** has to be populated and kept for fully offline builds.

## Alternatives Considered

- **Vendored binaries** (Spec 0064 Q4 D-b): reproducible, but large binaries in git and no package-graph upgrades.
- **Zero NuGet for the editor too** (D-c): makes an Avalonia editor impossible.
- **Version pins without lock files and hashes:** pins the version, not the bits.
