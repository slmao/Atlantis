# ADR 0113: .NET Toolchain Selection and Build Integration

- **Status:** Proposed
- **Date:** 2026-10-09
- **Deciders:** slmao
- **Acceptance:** pending (review of Spec 0058's own branch PR, [PR #238](https://github.com/slmao/Atlantis/pull/238))
- **Related Spec:** [Spec 0058: C# Gameplay](../specs/0058-csharp-gameplay.md) (`In Review`)
- **Related ADR(s):**
  - [ADR-0006](0006-dependency-management.md): large SDKs and toolchains
    (the Vulkan SDK, the Android NDK) are host-installed and located by
    CMake, never fetched. New categories need their own ADR — this is one.
  - Written in the selection manner of
    [ADR-0082](0082-gltf-parser-dependency-selection.md).
  - It pairs with [ADR-0112](0112-csharp-gameplay-client-transport-and-bindings.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **No C# exists in the repository.** The build is CMake (Visual Studio 18
  2026 on Windows, Gradle and CMake for Android). Tests are Catch2 under
  ctest.
- **The development machine** (checked 2026-10-09):
  - the .NET 8.0.7 runtime only — no .NET SDK (`dotnet --list-sdks`
    fails);
  - Visual Studio's Roslyn `csc.exe`, but no .NET Framework reference
    assemblies.
- **.NET support** (Microsoft's support policy, checked 2026-10-09):
  - .NET 8 (LTS) and .NET 9 (STS) end support on 10 November 2026;
  - .NET 10 (LTS) is supported to 14 November 2028;
  - .NET 11 is not released.
- **The maintainer excludes NuGet dependencies.** The .NET 10 base class
  library covers what v1 needs: sockets, `System.Text.Json`,
  `System.Numerics`, processes and files.
- **Not every contributor builds C#.** The C++ engine and its Android
  build do not need it.

## Decision

1. **The .NET 10 SDK, TFM `net10.0`** (Spec 0058 Q3).
   - A repository `global.json` pins the SDK to the `10.0.1xx` feature
     band with `rollForward: latestPatch`; the Plan records the exact
     version installed.
   - Language version: the SDK's default for `net10.0`.
   - .NET is a host toolchain in ADR-0006's category: installed by the
     developer (or a CI image), never fetched by CMake.
2. **Zero NuGet.**
   - No project file contains a `PackageReference`.
   - A repository `NuGet.config` clears every package source, so an
     accidental restore fetch fails.
   - A test checks both.
   - The SDK's own targeting packs are not packages in this sense.
3. **CMake builds C# by driving `dotnet`** (B-a).
   - SDK-style `.csproj` files are built by CMake custom targets running
     `dotnet build -c <config> -o <build tree>`.
   - Dependencies are wired so the generated `.g.cs` and the example build
     before their tests.
   - Plain `dotnet build` of a project also works for C# developers.
   - CMake's native C# language support is not used: it ties C# to the
     Visual Studio generator.
4. **The gate** (G-b).
   - **Discovery:** configure looks for `dotnet` and a matching SDK.
   - **If absent:**
     - no C# target or test is declared;
     - configure prints one clear message naming the missing SDK;
     - option `ATLANTIS_REQUIRE_CSHARP` (default OFF) turns the absence
       into a configure error.
   - Gates and CI that cover C# set it ON.
   - The Android build never declares C# targets.
5. **ctest runs C#**:
   - C# test programs are console executables whose exit code is the
     assertion (0 pass), registered with `add_test`;
   - no C# test framework, since a test framework would be a NuGet
     package;
   - two-process tests stay in the C++ harness.

## Consequences

### Positive

- One LTS toolchain until November 2028, and no third-party package
  surface.
- C++-only contributors are unaffected unless they opt in. The C# gate is
  explicit, not silent.
- C# builds and tests run through the same `cmake --build` / `ctest`
  commands as everything else.

### Negative / Trade-offs

- **A new host install** for anyone building C#. This
  machine needs it before implementation.
- **A default-OFF requirement** means an unconfigured machine skips C#. The
  configure message and the gates' `ATLANTIS_REQUIRE_CSHARP=ON` are the
  guard.
- **Hand-rolled C# test programs** instead of a framework: less tooling,
  more explicit assertions.
- **.NET 10's end of support (November 2028)** schedules a toolchain move
  within this ADR's lifetime.

## Alternatives Considered

- **.NET 8.** Rejected: end of support on 10 November 2026, one month
  away.
- **.NET 9.** Rejected: STS, ending the same day.
- **.NET Framework 4.8 with Visual Studio's `csc`.** Rejected:
  - no in-box `System.Text.Json` (it would need NuGet);
  - a legacy runtime;
  - targeting packs absent here too.
- **CMake native C#** (`enable_language(CSharp)`, VS generator). Rejected:
  generator-specific, with thin SDK-style support.
- **Configure fails without .NET** (G-a). Rejected: every C++ contributor
  would need .NET.
- **A silent skip** (G-c). Rejected: an absent SDK would pass unnoticed.
- **xUnit/NUnit/MSTest.** Rejected: NuGet packages.
