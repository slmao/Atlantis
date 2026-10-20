# ADR 0125: Out-of-Process Editor Platform and One Client Interface

- **Status:** Accepted
- **Date:** 2026-10-20 (accepted 2026-10-20)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-20 (owner rulings on Spec 0064 Q1–Q6, each as recommended, recorded once in
  [Plan 0064](../plans/0064-editor-platform-direction.md); the ADR text was reviewed in [PR #275](https://github.com/slmao/Atlantis/pull/275))
- **Related Spec:** [Spec 0064: Editor Platform Direction](../specs/0064-editor-platform-direction.md) (`Approved` 2026-10-20; D1–D3
  are its Q1, D4 its Q5, D5 its Q6)
- **Related ADR(s):**
  - Decides what the blueprint's Candidate 2 left open ("Editor process model … Not decided").
  - Keeps [ADR-0105](0105-runtime-connection-and-cli-client.md), [ADR-0106](0106-attachable-runtime-transport-and-control.md)
    (superseded in part by [ADR-0128](0128-remote-viewport-camera-and-raycast.md) only),
    [ADR-0110](0110-gameplay-sdk-client-library-and-execution-model.md),
    [ADR-0112](0112-csharp-gameplay-client-transport-and-bindings.md),
    [ADR-0113](0113-dotnet-toolchain-and-build-integration.md),
    [ADR-0099](0099-engine-schema-core-and-descriptor-vocabulary.md), [ADR-0111](0111-schema-generated-typed-bindings.md),
    [ADR-0103](0103-runtime-world-operation-boundary.md), [ADR-0104](0104-runtime-world-transaction-atomicity.md).
  - Keeps [ADR-0107](0107-editor-ui-library-selection.md), [ADR-0108](0108-editor-host-and-viewport-composition.md),
    [ADR-0123](0123-editor-viewport-camera-and-picking-host-services.md) and
    [ADR-0124](0124-editor-transaction-and-client-side-undo.md) for the ImGui editor, which D4 freezes.
  - Pairs with [ADR-0126](0126-viewport-embedding-and-input-routing.md) and
    [ADR-0127](0127-editor-application-managed-dependency-policy.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The maintainer's direction (2026-10-20):** an out-of-process C#/Avalonia editor, an ordinary client of
  `RuntimeConnection` / `atlantis.remote/1` with no Runtime privilege; GUI, scripts, CI and AI agents on one
  structured interface; the ImGui editor kept as a debug tool.
- **What already exists for it:** `atlantis.remote/1` (ADR-0106) carries `RuntimeConnection` and
  `RuntimeControl` verbatim; the C# SDK speaks it with no engine code (ADR-0112 D1); its typed bindings are
  generated from the descriptor tables (ADR-0111, ADR-0112 D3); the CLI and the C++ SDK are clients of the same
  surface (ADR-0105, ADR-0110).
- **What it must not change:** the closed World surface (ADR-0103 D1, ADR-0104), the schema as the one truth
  (ADR-0099 D2–D4), no managed runtime in the Runtime process (Spec 0057 Q6, ADR-0110 D3), no custom components
  (ADR-0110 D5).
- **The ImGui editor** (Specs 0056/0062/0063) is in-process, hosted by `atlantis_runtime --editor`, and has its own
  host interface (ADR-0108 D2, ADR-0123 D3).

## Decision

1. **The product editor is a separate managed process: Atlantis Editor App** (`src/editor_app/`, namespace
   `Atlantis.Editor`), a .NET 10 Avalonia application. Its only engine access is the Atlantis C# SDK over
   `atlantis.remote/1`. It links no native engine code, loads nothing into the Runtime, and has no host
   interface of its own: it starts `atlantis_runtime` as any user would (with `--listen` and, for embedding,
   ADR-0126's `--parent-window`). It is not in the native module graph, like Atlantis C# SDK.
2. **One structured interface.** Every capability the editor needs is a call of the shared client surface —
   `RuntimeConnection` for the World, `RuntimeControl` for lifecycle and frame-side services — exposed alike by the
   wire, the CLI, the C++ SDK and the C# SDK. A capability added for the editor is added for all of them in the
   same Spec (the first two: ADR-0128). There is no editor-only method, channel or file.
3. **The schema stays the one truth.** The editor reads types and fields from the generated C# bindings and from
   `connection.schema`. Any later C# way of declaring data (attributes, classes) is an input to the generator that
   lands in a descriptor table; the bindings are generated from the table, never the reverse (ADR-0099 D2/D4,
   ADR-0111 D1).
4. **The ImGui editor is frozen at E2 as the in-process debug tool** (Spec 0064 Q5, T-a). It keeps `--editor` and
   its ADRs; it takes bug fixes and changes forced by shared-surface Specs only. E3 and later iterations (E5, E4,
   E6 in the maintainer's reference order) target Atlantis Editor App once Spec 0064's Phase 1 has passed its
   checkpoint; until then none of them starts on either side. E1/E2's C++ models and ADR-0123/0124 are the
   reference contracts a C# port keeps.
5. **C# gameplay components stay out** (Spec 0064 Q6, G-a): ADR-0110 D5 is unchanged; a later Spec reopening it is
   bound by D3.

## Consequences

### Positive

- **The ordinary-client rule holds by construction:** a managed process with no native engine code cannot reach
  the ECS, the Renderer or the Runtime's internals.
- **Scripts, CI and agents get every editor capability** the day the editor does.
- **One line of editor development** after Phase 1, with existing contracts as the port's reference.

### Negative / Trade-offs

- **A second editor exists** (the frozen ImGui one) and two languages implement editor models for a while.
- **Every editor feature that needs the Runtime goes through a Spec on the shared surface**, which is slower than
  an in-process call.
- **Wire latency**: every request is answered at a frame boundary (ADR-0106 D1).

## Alternatives Considered

- **A native out-of-process editor** (Spec 0064 Q1 P-b): an ordinary client too, without the C# ecosystem the
  direction wants.
- **Growing the ImGui editor** (P-c): excluded by the direction.
- **An in-process managed editor**: excluded by Spec 0057 Q6 and Spec 0056's "no big object".
- **Keeping both editors moving** (Q5 T-b), or **removing the ImGui editor** (T-c).
