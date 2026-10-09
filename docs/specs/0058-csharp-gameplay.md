# Spec: C# Gameplay

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-09
- **Related Plan(s):** none yet. Plan drafting awaits this Spec's Approval.
- **Approval:** pending, in [PR #238](https://github.com/slmao/Atlantis/pull/238). The maintainer set this Spec's position and
  boundaries before drafting (2026-10-09, chat). They are recorded under
  Goals / Non-Goals and are not open to review; the open questions below
  are.
- **Related ADR(s)** (both `Proposed`, drafted alongside):
  - [ADR-0112](../adr/0112-csharp-gameplay-client-transport-and-bindings.md):
    the C# client's transport, its module, its generated bindings and its
    API shape (Q1, Q2, Q4, Q5);
  - [ADR-0113](../adr/0113-dotnet-toolchain-and-build-integration.md): the
    .NET toolchain and how the CMake repository builds and tests C# (Q3).

  They keep [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md)
  (`atlantis.remote/1` is the transport, unchanged),
  [ADR-0110](../adr/0110-gameplay-sdk-client-library-and-execution-model.md)
  (client-driven logic, no Runtime-hosted code) and
  [ADR-0111](../adr/0111-schema-generated-typed-bindings.md) (generated,
  committed, staleness-checked bindings; the C++ output unchanged). They
  extend [ADR-0006](../adr/0006-dependency-management.md)'s toolchain
  category. [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md),
  [ADR-0103](../adr/0103-runtime-world-operation-boundary.md)–[ADR-0105](../adr/0105-runtime-connection-and-cli-client.md)
  and [ADR-0004](../adr/0004-phase1-threading-baseline.md) are unchanged.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

The schema is the common source of truth, but there are two ways to call
the World:
- **Editor and agents** use the reflective surface (`EntityGuid`, `TypeId`,
  `FieldId`).
- **Gameplay** uses a generated typed surface. Spec 0057 delivered that
  surface in C++.

This Spec makes the generator emit a second, non-C++ target for the first
time: **C#**. It adds a C# client with a typed API in the shape
`world.Get<Transform>(entity)`. Like `atlantis_gameplay_demo`, it is one
more ordinary client in its own process, with no Renderer, ECS or Runtime
privilege.

**v1 is a deliberate compromise, stated up front.** The maintainer's
principle "the gameplay hot path does not go through IPC, Variant or
Reflection" cannot be fully met in v1:
- a separate process means IPC;
- calling native code from C# means marshaling.

v1 proves the model — one schema, generated typed C#, the same semantics,
an exact loop against a running Runtime. Hot-path optimizations (a binary
protocol v2, generated marshaling) are named only, not designed.

**North star:** a managed C# process attaches to `atlantis_runtime
--listen`, checks its generated bindings against the Runtime's schema, and
through the typed API:
1. finds the light;
2. spawns, moves and pulses a beacon light, one stepped frame per logic
   step, each frame's data exact;
3. sees a refused transaction change nothing;
4. captures an image that shows the beacon;
5. destroys the beacon.

Its output is identical run to run (the Spec 0057 J12 shape), and a
two-process ctest runs it.

## Motivation / Problem Statement

### What exists

- **The protocol** (Spec 0055, ADR-0106): `atlantis.remote/1`.
  - **Transport:** loopback TCP, one JSON object per line, each with an id;
    the 1 MiB line limit.
  - **Session:** found through a session file
    (`{"protocol","port","token","pid","scene"}`); a `hello` presents the
    per-run token.
  - **Methods:** eleven `connection.*` and five `control.*` methods (step
    is parked until its frame completes). Answers come at frame
    boundaries.
  - **Value encoding** (`src/remote/src/codec.h`):
    - `u64` as a decimal string;
    - floats as shortest round-trip numbers, or `"nan"` / `"inf"` /
      `"-inf"`;
    - ids as `"0x"` + 16 lowercase hex digits;
    - every `PropertyValue` alternative tagged (`{"vec3":[…]}`,
      `{"enum":n}`, `{"absent":true}`, …);
    - results as `{"ok":…}` / `{"err":"<name>"}`.

  Today the only description of the wire is that header's comment and the
  code. One implementation exists: Atlantis Remote's C++ halves.
- **The Gameplay SDK** (Spec 0057, ADR-0110/0111): C++ reflective and typed
  layers, the schema compatibility check, the `QueryBatch` seam, and the
  beacon demo.
- **The generator** (`src/tools/sdk_codegen/`): `generateBindings()` is a
  pure function from a descriptor table to C++ text. It is
  host-only. Its output is committed with `eol=lf` and checked for
  staleness, with `.expected` written to the build tree on a mismatch
  (Spec 0057 J2/J4).

### Found while drafting

- **No .NET SDK on the development machine.** It has the .NET 8.0.7
  runtime only (`dotnet --list-sdks` fails). Visual Studio 2026 ships
  Roslyn's `csc.exe`, but there are no .NET Framework reference
  assemblies to compile against.
  - So any C# build needs a new toolchain installed on each developer
    machine, and the build must decide what happens without it (Q3).
- **.NET support dates** (Microsoft's support policy, checked 2026-10-09):
  - .NET 8 (LTS) and .NET 9 reach end of support on **10 November 2026**,
    one month from now;
  - .NET 10 is the current LTS, supported to **14 November 2028**.
- **The protocol was written for one client.** Nothing outside Remote
  pins its grammar: there is no reference document and no shared
  conformance data. A second implementation in another language turns it
  into a contract (Q8).
- **The schema's names are C++-shaped.** Fields are camelCase
  (`localPosition`); C# convention is PascalCase. The C# mapping must
  rename deterministically and refuse collisions (Q2).
- **`System.Guid` is mixed-endian in its byte form, but its text form is
  RFC 9562.** The wire carries GUIDs as text, so text round-trips exactly.
  A C# binding must never use `Guid`'s byte layout.

## Goals

Maintainer-set (2026-10-09), not to be relaxed in review:

- **C# bindings generated from the same schema source.**
  `atlantis_sdk_codegen` gains a C# backend; the typed API takes the shape
  `world.Get<Transform>(entity)`.
- **The C# client is one more ordinary client.** It has no Renderer, ECS
  or Runtime privilege and runs in its own process, like the 0057 demo.
- **The v1 tension is stated, not hidden.** "The gameplay hot path does
  not go through IPC, Variant or Reflection" is not fully met in v1. v1
  proves the model; hot-path optimization (binary protocol v2, generated
  marshaling) is named only.

Goals of this Spec within those boundaries:

- **The North star above**, automated in a two-process ctest.
- **Zero behaviour change in the engine.** No change to Runtime, World,
  Connection, Remote, the CLI, the editor or the C++ Gameplay SDK. The
  generated C++ header stays byte-identical, and every golden is
  unchanged.

## Non-Goals

Maintainer-set (2026-10-09) — named only, not designed or scaffolded:

- **A managed runtime hosted inside the Runtime** (already excluded by
  Spec 0057).
- **Package / plugin** (0059), and 0060/0061.
- **Third-party NuGet dependencies.**
- **Android hosting of C#.**
- **C# editor extensions.**
- **Hot-path optimization:** a binary protocol v2, generated marshaling,
  shared memory.

Also out of scope (drafter's proposal, open to review):

- **New World or protocol capabilities.** No new method, query or event;
  `atlantis.remote/1` is used as it is.
- **Custom components, a time model, a scheduler** (Spec 0057 rulings
  Q5–Q7 carry over to C#).
- **A C# math library** beyond the schema's value kinds.

## Requirements

### Functional

- **R1 — An ordinary client** (Q1, Q4). The C# client reaches the World
  only through `atlantis.remote/1`'s `connection.*` methods and the
  Runtime's lifecycle only through `control.*`.
  - It loads no native engine code.
  - The engine links nothing of it.
- **R2 — Generated C#** (Q2). The generator's C# backend emits, from
  `worldSchema()`:
  - value types per struct;
  - enums with the constants' exact values;
  - typed field handles for every leaf;
  - the binding table used for compatibility.

  The output is committed and checked for staleness by the existing
  generator tests (no .NET needed for that check), with `eol=lf` and the
  `.expected` pattern. The C++ output is unchanged.
- **R3 — One semantics** (Q5). Every C# operation is the same sequence of
  protocol calls the C++ SDK makes over Remote:
  - values only;
  - commands apply at the next frame;
  - a transaction is all or nothing and resolved whole before submit;
  - failures are reported by ticket;
  - Spec 0057 R10's contracts hold (no snapshot; enum value 0).

  Refusals stay the boundary's. A C# typed operation submits the same
  commands, in the same order, as its C++ equivalent.
- **R4 — Schema compatibility** (Q5). The C# client checks each generated
  type, recursively, against the schema the Runtime serves (Spec 0057 R5's
  rule). A mismatch refuses that type's typed operations and any
  transaction containing one.
- **R5 — The example** (Q6). A C# console program runs the North star with
  the typed API: one line per step, exit 0 only if every check holds, the
  same output on every run.
- **R6 — Protocol conformance** (Q1, Q8). The C# codec agrees with the C++
  codec on every value kind and envelope, checked against shared vectors
  and against a live server.
- **R7 — Build and toolchain** (Q3). The C# projects use one pinned
  toolchain and no NuGet package. CMake builds and ctest runs them, and a
  machine without the toolchain is handled explicitly.
- **R8 — No engine change.** Zero changes under:
  - `src/runtime/`, `src/world/`, `src/connection/`, `src/remote/`,
    `src/cli/`, `src/editor/`;
  - `src/gameplay_sdk/` (the generated C++ header byte-identical).

  `runFrame()` stays byte-identical and every golden is unchanged.

### Non-functional

- **Latency (the v1 tension):** each synchronous call costs one frame
  boundary.
  - Measured for Spec 0057: about 33 ms on the default scene, about two
    vsync frames.
  - Beyond that, loopback TCP adds tens of microseconds and JSON encoding
    microseconds. P/Invoke (Q1 b) would add sub-microsecond to microsecond
    marshaling per call.
  - The frame boundary dominates every option; a pipelined batch is one
    boundary for many queries.
- **Threading:** the C# client's own process and its own threading model.
  ADR-0004 governs the engine's frame thread, not a client process.
  - One client object is not thread-safe (it owns one socket and its
    request ordering).
  - Q5 decides sync or async.
- **Dependencies:** the .NET SDK as a host toolchain (ADR-0006 category,
  ADR-0113), the base class library only, no NuGet packages.
- **Portability:** Windows in v1. The C# library is plain .NET with no
  Windows API, but Android hosting is a Non-Goal.

## Proposed Design

Under the recommendations below:

```
worldSchema() ─► atlantis_sdk_codegen ─┬─► generated/world.h          (C++, unchanged)
                 (host tool, + C# back) └─► Generated/World.g.cs        (C#, committed, eol=lf)

src/csharp/Atlantis.Gameplay/   (one class library, net10.0, no NuGet)
   Remote/     session file, socket, JSON-lines framing, hello, request ids,
               pipelining, the atlantis.remote/1 value codec (System.Text.Json)
   Reflective  World (by name/id), Transaction, Subscription, FailureLog,
               batched queries (native pipelining -- no adapter needed)
   Typed       World.Get<T>/Get(entity, field)/Set/Add/Remove, Decode,
               per-type recursive compatibility check
   Generated/  World.g.cs (value record structs, enums, Fields.*, bindings)

examples/csharp/GameplayDemo/   the C# beacon demo (console)
tests/csharp/                   C# test programs (exit code = assertion),
                                a no-GPU fixture server, conformance vectors
CMake: find .NET SDK ─► dotnet build of the csproj into the build tree ─► ctest
```

The typed API, illustratively (Q5 fixes the shape; the Plan fixes the
spellings):

```csharp
using Atlantis.Gameplay;
using Atlantis.Gameplay.World;               // generated

using var session = RemoteSession.Connect(SessionFile.Resolve(args));
var world = new GameplayWorld(session);
world.CheckCompatible<Light>();               // throws/returns SchemaMismatch per Q5
Light sun = world.Get<Light>(sunGuid);        // one pipelined batch of 4 leaf reads
var tx = new Transaction()
    .Create(beacon)
    .Add(beacon, new Light { Kind = LightKind.Point, Color = new(1f, .6f, .2f), Intensity = 4f, Range = 4f })
    .Add(beacon, new WorldMatrix { Column3 = new(1.5f, 2f, 0f, 1f) /* … */ });
TransactionTicket ticket = world.Submit(tx);
session.Control.Pause();
FrameReport report = session.Control.Step(1);
world.Set(beacon, Fields.Light.Intensity, 6f);
```

## Architectural Impact

Yes. Recorded in two ADRs drafted alongside (both `Proposed`):

- **[ADR-0112](../adr/0112-csharp-gameplay-client-transport-and-bindings.md)**
  covers:
  - the transport: managed, speaking `atlantis.remote/1` directly (Q1);
  - the protocol becoming a cross-language contract with conformance
    vectors and a reference document (Q8);
  - the generator's C# backend and the schema → C# type mapping (Q2);
  - the C# library's placement, namespace and AGENTS.md entry (Q4);
  - the API shape and threading model (Q5).
- **[ADR-0113](../adr/0113-dotnet-toolchain-and-build-integration.md)**
  covers:
  - the .NET SDK and TFM;
  - zero NuGet;
  - how CMake builds and ctest runs C#;
  - the gate when the toolchain is absent (Q3).

**Expected code impact** under the recommendations:

- **New:**
  - `src/csharp/Atlantis.Gameplay/` (csproj, sources, committed
    `Generated/World.g.cs`);
  - `examples/csharp/GameplayDemo/`;
  - `tests/csharp/` (test programs, a C++ no-GPU fixture server, shared
    conformance vectors);
  - a protocol reference document;
  - `global.json` (SDK pin).
- **Changed:**
  - `src/tools/sdk_codegen/` (a C# backend, the tool's `--lang`);
  - `tests/tools/sdk_codegen/` (C# staleness checks);
  - top-level `CMakeLists.txt` (the .NET gate);
  - `.gitattributes` (`*.g.cs` LF);
  - `tests/gameplay_sdk/gameplay_e2e_tests.cpp` (a C# demo case beside the
    C++ one, Q6).
- **Docs:** AGENTS.md (module list, the C# rule, the protocol's status) and
  `module_boundaries.md`, with the implementation.
- **Unchanged:**
  - the eight directories of R8;
  - the wire protocol;
  - goldens;
  - the Android build (it never sees C#).

## Alternatives Considered

The questions below compare their options. Rejected across them:

- **A managed runtime inside the Runtime process** — maintainer-excluded
  (Spec 0057).
- **NuGet packages** (a JSON library, a test framework, gRPC):
  maintainer-excluded. .NET 10's base class library has `System.Text.Json`
  and everything else v1 needs.

## Testing & Verification Plan

- **Generator (no .NET needed):**
  - the committed `World.g.cs` and a synthetic `Synthetic.g.cs` equal the
    C# backend's output, byte for byte;
  - determinism;
  - LF only;
  - PascalCase renaming with collision refusal;
  - exact enum values;
  - every leaf has one handle;
  - the C++ output is unchanged (Spec 0057's staleness test, untouched).
- **C# unit tests** (C# test programs whose exit code is the assertion,
  run by ctest; no test-framework package):
  - the codec against the shared conformance vectors, both directions;
  - the session-file parser;
  - JSON-lines framing and the line limit;
  - PascalCase handles;
  - generated bindings equal their schema;
  - value initialization (zeros, enum value 0, `zeroIsDeclared`).
- **C# against a live server, no GPU:** a small C++ fixture server serves
  the Spec 0054 fixture scene through a real `RemoteServer`, with a fake
  control that applies on frames, like `tests/remote`'s `ServedWorld`.
  Against it the C# tests cover:
  - reflective and typed get/set/read/add/remove;
  - transactions, all or nothing, refused whole on name or binding
    errors;
  - subscriptions and failures by ticket;
  - pipelined batches;
  - recursive compatibility (with modified schemas served);
  - command-sequence parity with the C++ SDK on the same operations (the
    server records what it received).
- **Conformance vectors:**
  - generated from the C++ codec by a C++ test;
  - committed;
  - a staleness check fails if the C++ codec's output for them changes.
- **The North star, two processes:** `atlantis_runtime --listen 0` and the
  C# demo, as a second test case in the 0057 two-process test.
  - exit 0;
  - every line ok;
  - identical lines on a second run;
  - a graceful Runtime close;
  - fatal VVL on the Runtime side.
- **Regression (R8):**
  - Debug and Release full suites;
  - every golden byte-identical;
  - zero changes in the eight directories;
  - `runFrame()` byte-identical;
  - `assembleDebug` unchanged.

## Risks & Open Questions

Risks:

- **A second protocol implementation can drift.** Conformance vectors
  generated from the C++ codec, plus live-server tests, pin the C# side to
  the C++ side. The reference document (Q8) makes the grammar reviewable.
- **A new toolchain.** Every developer machine needs the .NET 10 SDK
  installed; this machine does not have it yet. The gate (Q3) decides what
  an absent SDK does.
- **Float text.** C# must write floats as shortest round-trip text and read
  `"nan"` / `"inf"` exactly as the C++ codec does. .NET Core 3.0 and later
  format `float` shortest round-trip by default; the vectors test it.
- **Trigonometry differs across languages.** The C# demo's motion must not
  depend on `MathF.Sin` matching `std::sin` bit for bit. Determinism is
  required run to run, not C#-to-C++ (Q6).

Open questions — Q1–Q7 posed by the maintainer, Q8 surfaced while
drafting. Each lists options and a recommendation.

- **Q1 — Transport and architecture** (the heaviest; ADR-0112).
  - **(a) Pure managed C# speaking `atlantis.remote/1` directly.**
    - **Stack:** session file → TCP socket → JSON-lines → codec → C#
      reflective layer → generated typed layer. All of it is C#; there is
      no native code.
    - **Cost:**
      - a second implementation of the codec (about 990 lines of C++ in
        `codec.h/.cpp`), the client half's framing, pipelining and handshake
        (about 470 lines in `remote_client.cpp`, 60 for the session file),
        and the reflective and typed layers (about 1,160 lines in
        `src/gameplay_sdk/`; measured 2026-10-09). The JSON grammar itself
        is not ported: `System.Text.Json` parses it;
      - kept in step by conformance vectors and live-server tests.
    - **Gains:**
      - zero native dependency and no C ABI;
      - one managed assembly, debuggable as ordinary .NET;
      - the protocol is proven as a language-neutral contract — which
        Python (0061) and any later client also need;
      - pipelining is native: C# owns the socket, so it needs no adapter.
    - **Latency per call:**
      - a frame-boundary wait (about 33 ms measured);
      - plus loopback TCP (tens of µs);
      - plus `System.Text.Json` encode/decode (µs).
    - **Failure modes:**
      - a codec divergence (an edge value encoded differently), caught by
        the vectors;
      - a disconnect, which surfaces as a managed exception or result;
      - nothing can crash the managed process from native code.
  - **(b) P/Invoke over the native Gameplay SDK.**
    - **Stack:** C# → generated P/Invoke declarations → a new C ABI module
      (`extern "C"`: opaque handles, error codes, struct and string
      marshaling) → the C++ SDK → `RemoteSession` (C++) → socket.
    - **Cost:**
      - a new native module exporting a C ABI for every SDK operation —
        values, variants, spans, callbacks for steps, ownership of returned
        arrays;
      - a native DLL built per configuration and architecture, and copied
        next to the managed executable;
      - a marshaling layer per value kind;
      - generated P/Invoke stubs — a second generator target anyway;
      - two debuggers.
    - **Gains:** one implementation of codec and semantics (the C++ one,
      already tested), and the reused native compatibility check.
    - **Latency per call:**
      - the same frame-boundary wait (it is still IPC to the Runtime);
      - plus the same TCP and JSON work, in C++;
      - plus P/Invoke transitions and marshaling (sub-µs to µs, more for
        strings and arrays).

      Not measurably faster.
    - **Failure modes:**
      - `DllNotFoundException` or architecture mismatch at start;
      - an ABI mismatch between the DLL and the stubs (silent memory
        errors if the layouts disagree);
      - a native crash takes down the managed process;
      - a C ABI becomes a public surface to version.
  - **(c) Hosted inside the Runtime process.** Excluded by Spec 0057 and
    the maintainer; listed as rejected.
  - **Recommendation: (a).**
    - Both live options cross the same IPC boundary, so neither meets the
      hot-path principle in v1. Only (a) avoids a C ABI, native
      distribution and a crash domain shared with native code.
    - Its cost — a second codec — is bounded and pinned by vectors.
    - It is also the step that makes the protocol a genuine
      language-neutral contract before Python needs it.
    - (b)'s real benefit, reuse of tested code, is outweighed by a new ABI
      surface that the hot-path v2 would reshape anyway.

- **Q2 — C# code generation** (ADR-0112).
  - **Backend:** `generateCSharpBindings(schema, options)` beside
    `generateBindings()`, in the same pure library.
    - The tool gains `--lang cpp|csharp` (C++ stays the default, so its
      command and output are unchanged).
    - Output is deterministic, LF only, with no timestamp.
  - **Committed outputs:**
    - `src/csharp/Atlantis.Gameplay/Generated/World.g.cs`;
    - `tests/csharp/Generated/Synthetic.g.cs` (from the same synthetic
      table as 0057).
  - **Porting J2/J4:**
    - `.gitattributes` gains `*.g.cs text eol=lf` for generated C#;
    - the existing C++ generator tests regenerate the C# text in memory and
      compare bytes; on a mismatch they write `<build>/sdk_codegen/<name>.expected`
      and fail with the command;
    - no .NET is needed to know the C# is current.
  - **Type mapping:**

    | Schema | C# | Notes |
    |---|---|---|
    | `UInt64` | `ulong` | wire: decimal string |
    | `Float32` | `float` | NaN/±Inf reachable (the boundary refuses them) |
    | `Vec3Float32` | `System.Numerics.Vector3` | in-box; component order x, y, z |
    | `Vec4Float32` | `System.Numerics.Vector4` | in-box |
    | `AssetGuid` | `AssetGuid` (a `readonly record struct` over `System.Guid`) | text form only; the byte layout is never used |
    | `EntityGuid` | `EntityGuid` (the same, distinct type) | so asset and entity ids cannot be swapped |
    | Enum | `enum X : long` with exact values | value 0 may be undeclared (R10) |
    | Struct | `public record struct X` | value semantics; nested structs nested |
    | Optional | `T?` (`Nullable<T>`) | null ↔ `{"absent":true}` |
    | not Editable | a read-only handle type | a typed `Set` on it does not compile |

  - **Names:**
    - types keep their schema names (`Light`);
    - fields are PascalCased (`intensity` → `Intensity`);
    - handles mirror the canonical path (`Fields.Light.Intensity`,
      `Fields.Camera.Fog.Density`);
    - a rename that collides, or hits a C# keyword or a generator-reserved
      name, fails generation (the ADR-0111 D5 rule extended).
  - **Alternatives:**
    - keep camelCase names (unidiomatic C#);
    - our own `Float3`/`Float4` structs instead of `System.Numerics` (no
      SIMD semantics, but foreign to C# gameplay programmers);
    - C# source generators at build time (rejected: a build-time generator
      reads no committed output and cannot be staleness-checked without
      .NET).
  - **Recommendation:** as above.

- **Q3 — Toolchain and build integration** (ADR-0113, a selection ADR in
  the ADR-0006 / ADR-0082 manner).
  - **SDK and target:** the **.NET 10 SDK** (LTS to November 2028),
    **TFM `net10.0`**, pinned by a repository `global.json` (SDK
    `10.0.1xx` feature band, `rollForward: latestPatch`).
    - Rejected: .NET 8 (end of support 10 November 2026); .NET Framework
      4.8 (no in-box `System.Text.Json`, and no targeting pack here);
      .NET 11 (STS, not yet released).
  - **Zero NuGet:**
    - project files contain no `PackageReference`;
    - a repository `NuGet.config` clears all package sources, so a restore
      that tries to fetch fails;
    - a test checks both.
  - **How CMake builds C#:**
    - **(B-a)** separate SDK-style `.csproj` files, built by CMake custom
      targets running `dotnet build -c <config> -o <build tree>`. Plain
      `dotnet` remains usable for C# developers.
    - **(B-b)** CMake's native C# language support through the Visual
      Studio generator. This ties C# to one generator, and its SDK-style
      support is thin.
    - **(B-c)** C# outside CMake entirely. ctest could not run it, and the
      gate could not see it.
    - **Recommendation: B-a.**
  - **Without the toolchain:**
    - **(G-a)** configure fails, as for a missing Vulkan SDK or `slangc`.
      Every contributor must install .NET even to build the C++ engine.
    - **(G-b)** C# targets and tests are not declared, with one clear
      configure message, plus an option `ATLANTIS_REQUIRE_CSHARP` that
      turns the absence into a configure error. Gates and CI set it.
    - **(G-c)** silently skip. An absent SDK would pass the gate unnoticed.
    - **Recommendation: G-b.** It follows the Bistro content-gating
      precedent, and the Plan's gates run with `ATLANTIS_REQUIRE_CSHARP=ON`.
  - **ctest:**
    - C# test programs are console executables whose exit code is the
      assertion (the 0057 J12 shape), registered by CMake;
    - the two-process test stays in the C++ harness (Q6).

- **Q4 — Shape and place of the C# code** (ADR-0112).
  - **Layout:**
    - `src/csharp/Atlantis.Gameplay/`: one class library — remote client,
      codec, reflective and typed layers, generated bindings;
    - `examples/csharp/GameplayDemo/`;
    - `tests/csharp/`;
    - namespace `Atlantis.Gameplay`, generated types in
      `Atlantis.Gameplay.World` (the schema module, as in C++).
  - **Module status:**
    - **(M-a)** a new top-level module in AGENTS.md's list, **Atlantis C#
      SDK**, with its own rule: managed only, its only coupling to the
      engine is `atlantis.remote/1` and the generated bindings, no native
      code, .NET 10, zero NuGet. AGENTS.md says it is not a C++ module and
      not part of the native dependency graph.
    - **(M-b)** described only as a second client-half implementation
      under Atlantis Remote. This undersells a library with its own
      reflective and typed layers.
    - **(M-c)** examples only, with no library. Every C# client would copy
      the codec.
  - **Alternatives for layout:** `examples/csharp/` only, or `csharp/` at
    the root (outside `src/`).
  - **Recommendation: M-a with the layout above.**

- **Q5 — The typed API surface** (ADR-0112).
  - **Shape:**
    - `GameplayWorld` (over a session):
      - `Get<T>(entity)` (a component, one pipelined batch);
      - `Get(entity, Fields.X.Y)`;
      - `Set(entity, Fields.X.Y, value)`;
      - `EntitiesWith<T…>()`;
      - `Submit(Transaction)`;
      - `Subscribe(filter)` returning `IDisposable`;
      - `DrainFailures()`, `FailureLog`;
      - `Decode(event, field)`;
      - reflective overloads by path and id.
    - `Transaction` (a builder): `Create`, `Destroy`, `Add<T>(entity)`,
      `Add(entity, T value)` (only for all-Editable types), `Remove<T>`,
      `Set`.
    - `session.Control`: `Pause`, `Resume`, `Step`, `Status`.
    - **Batching:** built in. The client pipelines component reads and the
      component filter itself.
  - **Errors:**
    - **(E-a)** results (`Result<T>`-like, mirroring C++);
    - **(E-b)** exceptions for connection, schema-mismatch and name errors,
      with World refusals of commands still reported by ticket, not thrown;
    - **Recommendation: E-b.** Exceptions are idiomatic C#, and refusals stay
      values, as in C++.
  - **Sync or async.** The client process is outside ADR-0004; its
    threading is its own.
    - **(S-a)** synchronous: each call blocks until its frame-boundary
      answer, like the C++ SDK over Remote. Deterministic and simple.
    - **(S-b)** async (`Task`-returning): lets one client overlap requests,
      at the cost of ordering rules (request ids, response order) the
      protocol already serializes per connection.
    - **(S-c)** both.
    - **Recommendation: S-a**, async named for later.
    - One `GameplayWorld` and its session are not thread-safe. A client may
      run them on any one thread.

- **Q6 — The North star** (the C# beacon demo).
  - **The loop:** `examples/csharp/GameplayDemo` runs Spec 0057's loop with
    the typed API — connect and check bindings, find, pause and capture a
    baseline, spawn, K logic steps, refuse, capture (the image differs from
    the baseline, by PNG bytes), destroy, resume.
  - **Determinism:** one line per step, fixed beacon GUID (its own:
    `58005800-0000-4000-8000-0000000000b1`, so a C# and a C++ run never
    collide), identical output run to run.
    - Its motion is derived from k with operations exact in both languages
      (a fixed table, or rational arithmetic), not `MathF.Sin`.
    - Cross-language line equality is not required.
  - **The test:** a second test case in
    `tests/gameplay_sdk/gameplay_e2e_tests.cpp` runs `atlantis_runtime
    --listen 0` and `dotnet GameplayDemo.dll`. It reuses that file's harness,
    so there is no third copy, and the existing C++ case is untouched.
  - **Recommendation:** as above.

- **Q7 — Test surface.** As in the Testing section:
  - generator staleness for C# (C++ tests, no .NET);
  - C# test programs, exit code as the assertion;
  - a no-GPU C++ fixture server for live-protocol C# tests;
  - conformance vectors from the C++ codec;
  - the two-process North star;
  - the eight-directory zero-change guard and `runFrame()` byte guard.
  - **Alternatives:**
    - a C# test framework (NuGet; excluded);
    - C# tests only against a GPU Runtime (slow, and no fixture control
      for refusals or compatibility mutations).
  - **Recommendation:** as above.

- **Q8 — The protocol as a contract** (surfaced).
  - **(P-a)** a reference document, `docs/architecture/remote_protocol.md`,
    plus committed conformance vectors generated by the C++ codec. It
    describes `atlantis.remote/1` as it is:
    - framing and the session file;
    - `hello`;
    - every method's params and result;
    - the value encoding;
    - the error names and limits.

    The document is descriptive: any protocol change still goes through a
    Spec and ADR-0106's successor.
  - **(P-b)** the code as the only description (status quo). A second
    implementer reads C++ internals, and drift is found only by tests.
  - **Recommendation: P-a.**

## Out of Scope / Future Work

- **Hot path v2:** a binary protocol, generated marshaling, shared memory,
  in-process hosting — the maintainer's principle fully met.
- Async C# API.
- C# for Android, C# editor extensions, NuGet distribution of the C#
  library.
- Python bindings (0061) over the same protocol contract.
- Package / plugin (0059).
