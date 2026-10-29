# Plan: The Agent Tool Layer — an MCP Host over atlantis.remote/1

- **Spec:** [Spec 0070: The Agent Tool Layer — an MCP Host over atlantis.remote/1](../specs/0070-agent-tool-layer.md) (`Approved`
  2026-10-30 — owner rulings Q1–Q6, each as recommended, recorded on the branch PR).
  - **ADR:** [ADR-0138](../adr/0138-agent-tool-layer-host.md) (`Accepted` 2026-10-30).
  - **Unchanged ADRs:** ADR-0106, ADR-0112, ADR-0113, ADR-0125 (D2 held), ADR-0127 (mechanism
    extended, allow-list not), ADR-0128, ADR-0130, ADR-0131, ADR-0132.
- **Status:** Draft (for joint review with the Spec)
- **Author:** slmao (drafted by ZCode at explicit human direction)
- **Joint Human Review:** pending — with [Spec 0070](../specs/0070-agent-tool-layer.md), in
  [PR #306](https://github.com/slmao/Atlantis/pull/306); J1–J6 below are put for ruling, and the
  review's authorization gates implementation.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Build Spec 0070's host (ADR-0138): the managed MCP host in `src/mcp/` — the exact tool tier over
the whole `atlantis.remote/1` surface, the composed tier, host-held event subscriptions with drain
tools, captures as MCP image content, its packaging under its own allow-list, and its tests — with
no wire call, engine path or SDK surface changed.

## Pre-drafting reading (cited, not restated)

- **The protocol surface:** [docs/architecture/remote_protocol.md](../architecture/remote_protocol.md)
  (the method tables of §`connection.*`, §`control.*`, §`authoring.*`; descriptive — where it and
  the code differ, the code is right).
- **The clients' patterns:** the CLI's session discovery and `scene`/`runtime` groups; the C# SDK's
  `RemoteSession`/`RemoteControl`/`AuthoringSession` (including its raw `Send` for pipelining);
  `RemoteAuthoringDocument` (Spec 0068's model boundary over the SDK) as the seam-pattern precedent.
- **The packaging mechanism:** ADR-0127 and `cmake/check_editor_app_packages.cmake` (the allow-list,
  exact pins, lock files, locked restore, offline feed, negative tests), and
  `cmake/AtlantisDotnet.cmake` (the gated managed-target declaration).
- **The acceptance precedent:** Spec 0068's J7 driver (`J7Acceptance.cs`) — a programmatic client
  scripting a full session, and the live integration tests it sits beside.

## Plan of changes

- **P1 — The host skeleton** (`src/mcp/Atlantis.Mcp/`, a .NET 10 console application, CMake-gated
  as ADR-0138 D1): session discovery exactly the CLI's (`--session`, `ATLANTIS_SESSION`, the
  default path), one session per process, MCP over stdio through the pinned official SDK
  (`ModelContextProtocol`, exact version in P1's commit, currently 2.3.0, MIT), the first tool
  (`control.status`) as the skeleton's proof, clean exit on stdin close or session death.
- **P2 — The session seam and the exact tier** (Spec R2): the host's one boundary
  `IAtlantisSession` (its own interface, one implementation over the C# SDK's `RemoteSession`,
  `RemoteControl` and `AuthoringSession` — the `IAuthoringDocument` pattern), and the tool table as
  declared data: name, wire call, argument schema, result forwarding. Every R2 tool, results
  verbatim, refusals as tool errors with the reason text (R4).
- **P3 — The composed tier, events and captures** (Spec R3/R6/R7): the seven convenience tools,
  each composing exactly one `authoring.submitTransaction` of the closed command set; the two
  host-held subscriptions (created at connect, empty filter) with `drain_events` tools that return
  and clear; `control.step` with an image returning the frame report, the file path and the PNG as
  an MCP image content block.
- **P4 — Packaging and policy** (Spec R8, ADR-0138 D2): `src/mcp/allowed_packages.txt` (the one
  entry, pinned exactly), its `Directory.Packages.props`, `NuGet.config`, committed
  `packages.lock.json`, locked-mode restore, offline-feed support; the package policy check
  parameterized over both managed areas with negative tests for the new one; the C# SDK's
  zero-NuGet check unchanged.
- **P5 — Tests** (`src/mcp/tests/`, a console runner like the editor app's):
  the mapping of every tool against a fake `IAtlantisSession` (exact wire call, exact result,
  refusal reasons, drain accumulate/clear, the composed tier's exact transactions); a live
  integration test that spawns the real host over stdio, speaks MCP, and scripts the J7 shape
  against `atlantis_runtime --listen` on the default scene and Bistro (open, schema, edit, apply,
  drain until `Applied`, capture with a non-empty image block, a second client's conflicting edit
  refused with its reason).
- **P6 — Docs:** normative with the implementation — `docs/architecture/module_boundaries.md`
  (an Atlantis MCP section) and `AGENTS.md`'s module list (one sentence, the boundary rules);
  narratives after merge (the registry row's implementation cell, the blueprint).

## Milestones / Task Breakdown

1. **M1 — Skeleton and packaging** (P1, P4): the host builds and runs under
   `ATLANTIS_BUILD_MCP=ON`, answers `tools/list` with `control.status`, connects to a Runtime by
   session file, exits cleanly; the policy check covers both areas with negative tests; a default
   configure declares and fetches nothing.
2. **M2 — The exact tier** (P2): every R2 tool over the seam, unit-tested against the fake
   session.
3. **M3 — Composition, events, captures** (P3): the convenience tier, the drains, the image
   blocks; unit-tested.
4. **M4 — Live integration** (P5's live half): the scripted MCP session on both scenes, green.
5. **M5 — Verification sweep and normative docs** (P6): the full checklist below; the docs.

## Files / Modules Touched (expected)

- **New:** `src/mcp/**` — `Atlantis.Mcp/` (the host), `tests/Atlantis.Mcp.Tests/` (the runner),
  `allowed_packages.txt`, `NuGet.config`, `Directory.Packages.props`, `CMakeLists.txt`.
- **Changed:**
  - the root `CMakeLists.txt` (one gated `add_subdirectory`, as the editor app's);
  - `cmake/check_editor_app_packages.cmake` (parameterized over the two areas) and its negative
    test cases;
  - `docs/architecture/module_boundaries.md`, `AGENTS.md` (normative, with the implementation);
  - `docs/specs/README.md` (status only).
- **Not touched:** `src/runtime/**`, `src/remote/**`, `src/connection/**`, `src/authoring/**`,
  `src/editor/**`, the C# SDK, the CLI, the schema, every golden, `tests/performance/**`.

## Sequencing & Dependencies

- M1 first (nothing runs without the skeleton and the policy).
- M2 before M3 (the composed tier forwards through the seam M2 builds).
- M4 after M3 (the script uses all three tiers).
- M5 last.

## Joint Review — put for ruling (J1–J6)

- **J1 — The pinned package:** `ModelContextProtocol` (the official C# SDK,
  modelcontextprotocol/csharp-sdk, MIT), exact version pinned at M1's commit (2.3.0 at drafting),
  its transitive graph frozen by the lock file. **Recommended: accept.**
- **J2 — The seam:** the host defines `IAtlantisSession` and one implementation over the SDK's
  three objects, so every tool maps and forwards against a fake — the editor's
  `IAuthoringDocument` pattern, one seam, no SDK change. **Recommended: accept.**
- **J3 — The composed tier's set:** exactly R3's seven (`create_node`, `destroy_node`,
  `set_properties`, `add_component`, `remove_component`, `set_parent`, `set_active_camera`), no
  more in v1. **Recommended: accept.**
- **J4 — The live script:** the J7 shape listed in P5, on both scenes, asserting tool results at
  each step. **Recommended: accept.**
- **J5 — One policy script, two areas:** parameterize `check_editor_app_packages.cmake` over a
  directory argument rather than copying it. **Recommended: accept.**
- **J6 — Docs split:** normative with the implementation PR, narratives after merge (the standing
  practice). **Recommended: accept.**
- **Authorization asked:** implementation of M1–M5 on `feature/0070-agent-tool-layer` from
  `origin/main` once this PR merges; deviations disclosed in the implementation PR.

## Verification Checklist

- [ ] **Unit tests:** every tool's mapping and forwarding against the fake session (P5); the
      composed tier's exact transactions; refusal reasons verbatim; drains accumulate and clear.
- [ ] **Packaging:** the policy check green for both areas, its refusals negative-tested; the C#
      SDK's zero-NuGet check unchanged and green; a default configure declares nothing.
- [ ] **Live integration (GPU):** the scripted MCP session on the default scene and Bistro.
- [ ] **Unaffected engine:** goldens byte-identical; Vulkan Validation Layers clean on the GPU
      suites (the host adds no Runtime path); `--gate --applies` false; Android untouched.
- [ ] **Docs:** `module_boundaries.md` and `AGENTS.md` with the implementation.

## Rollback Plan

Revert the implementation PR: the host, its tests and its policy parameters go; the parameterized
check returns to serving the editor app alone; nothing else changed, so no golden, baseline, wire
or engine state is affected.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas: none — this
Spec has no measured targets (its non-functional bar is one round trip per tool call), so no J8-style
checkpoint applies.
