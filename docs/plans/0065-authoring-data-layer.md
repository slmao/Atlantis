# Plan: The Authoring Data Layer (E3, no UI)

- **Spec:** [Spec 0065: The Authoring Data Layer](../specs/0065-authoring-data-layer.md) (`Approved`) —
  [ADR-0129](../adr/0129-authoring-document-ownership.md),
  [ADR-0130](../adr/0130-authoring-access-interface-and-module-boundary.md),
  [ADR-0131](../adr/0131-authoring-apply-and-runtime-scene-reload.md) (all `Accepted` 2026-10-22); the Spec is
  `Approved` 2026-10-22.
  - Drafted together with the Spec at the maintainer's direction (2026-10-22). The owner ruled Q1 (c), Q2 D-a,
    Q3 I-a + V-1, Q4 B-a + L-a, Q5 S-a, Q6 P-a, Q7 T-a and Q8 G-b, exactly the options this Plan was drafted against.
  - Keeps ADR-0097, ADR-0100, ADR-0103, ADR-0104, ADR-0112, ADR-0125 and ADR-0004.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-22 — reviewed this Plan and
  [Spec 0065](../specs/0065-authoring-data-layer.md) together in [PR #280](https://github.com/slmao/Atlantis/pull/280).
  - **Spec:** Q1–Q8 ruled as recommended.
  - **Plan:** J1–J9 ruled as recommended; no Spec Correction.
  - **ADRs:** ADR-0129, ADR-0130 and ADR-0131 `Accepted`.
  - **Authorization:** implementation on `feature/0065-authoring-data-layer` once PR #280 has merged, M1–M7 in this
    Plan's order.
  - **Constraints:** no merge by the agent, no push to `main`; `tests/performance/baselines/` and
    `gate_policy.json` untouched; goldens byte-identical; changes limited to this Plan's file list; the changed
    `runFrame()` lines enumerated in the PR; the performance gate applies (`src/runtime/**`, the root
    `CMakeLists.txt`) and its verdict is reported as measured; the C# SDK's zero-NuGet check unchanged and passing;
    J3 (apply) and J9 (validation) reported as measured, not as test verdicts; J7's acceptance run is the owner's.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Build Spec 0065's data layer and nothing above it: an authoring session that owns the open document, the
`AuthoringConnection` every client edits through, an apply that cooks the document in memory and reloads the
Runtime's scene at a frame boundary, save for hand-authored sources, and the same capability on the wire, the CLI
and the C# SDK. No editor UI.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `45fce69`.

1. **The document model:** `scene::AuthoringScene` / `AuthoringNode` (`authoring_scene.h`, a plain value);
   `toAuthoringScene()` / `toParsedSceneSource()` (`authoring_scene_mapping.h`); `parseSceneSource()` /
   `serializeSceneSource()` (`scene_source.h`); `sceneSchema()` and the semantic fingerprint
   (`scene_semantic_schema.h`); `scene::PropertyAddress` (`scene_property_address.h`).
2. **Cook and decode:** `cookScene(sourcePath, assetGuid, artifactPath, metadataPath)` (`cook_scene.h`, file to
   file, atomic writes); `encodeSceneArtifact()` / `decodeSceneArtifact()` (`scene_artifact.h`, bytes);
   `decodeScene(artifactPath, metadataPath)` (`decode_scene.h`), the only constructor of `ValidatedSceneData`.
3. **The Runtime's load:** `loadAndBakeScene(config, device, layout)` (`scene_load.h`) returning
   `SceneLoadOutcome{scene, meshResourceMap, materialDataMap, textureDataMap, sceneGuid, pickGeometry}`, published in
   `initializeSteps()` (`runtime_application.cpp:1090–1133`) into `scene_` and `worldAccess_`; the InProcess
   endpoint and connections over `worldAccess_`.
4. **The client surface:** `RuntimeConnection` (`atlantis/connection/runtime_connection.h`); `RuntimeControl` and
   `RuntimeStatus{paused, frame, scene}` (`runtime_control.h`); Remote's `connection.*` / `control.*` dispatch
   (`remote_server.cpp`, `remote_client.cpp`), `remote_protocol.md`; the CLI's command layer (`src/cli/`); the C#
   SDK's `RemoteSession` and codec; conformance vectors (`tests/csharp/conformance/`).
5. **E2:** `EditTarget`, `EditHistory` (`src/editor/include/atlantis/editor/model/`), the history cleared on a
   scene-GUID change (`editor.cpp:96`).
6. **Scenes:** hand-authored sources under `assets/scenes/` with catalog entries in `assets/asset_catalog.txt`;
   Bistro's source generated into the build tree by `atlantis_add_imported_scene()` from the glTF and
   `assets/bistro/bistro_overlay.scene.txt`.
7. **Gate applicability** (`tests/performance/applicability.txt`): `src/runtime/**` and the root `CMakeLists.txt`
   are included, so the implementing PR runs the gate; `src/asset_system/**` and the new `src/authoring/**` are not
   listed; `src/connection/**`, `src/remote/**`, `src/cli/**`, `src/csharp/**` and `tests/**` are excluded.

## Plan-stage decisions

- **P1 — In-memory cook and decode** (ADR-0131 D2).
  - `cookScene()` is split: `cookSceneToMemory(const scene::AuthoringScene&, AssetGuid) → Result<CookedScene,
    SceneCookError>` runs every existing check and returns the artifact bytes and the sidecar text; `cookScene()`
    parses its source, calls it, and writes both atomically as today.
  - `decodeScene()` gains an overload over `CookedScene` (bytes and sidecar text) and stays the only constructor of
    `ValidatedSceneData` (its friend declaration names both overloads).
  - Byte identity with the build is a test over every committed scene and Bistro.
- **P2 — The authoring session** (ADR-0129 D1–D4; Spec R1–R6, R9).
  - `atlantis::authoring::AuthoringSession`: the open document, its provenance (`HandAuthored{sourcePath}` or
    `Imported{generatedSourcePath}`), the scene AssetGuid; `open()` from the catalog entry of the Runtime's scene.
  - Commands apply to a copy of the document; the copy is validated (the mapping's reference checks, every
    `sceneSchema()` constraint, the cooker's checks via P1's `cookSceneToMemory()` dry run) and swapped in only if
    valid; events are queued per subscription in application order. The dry run's cost is measured on Bistro
    against J9.
  - Minting a node's EntityGuid when the client gives none uses the asset cooker's mint (`guid_mint`, today in
    `src/tools/asset_cooker/`, ADR-0097 D1's only minting code), moved into Asset System so both share it (J8).
  - `save()`: hand-authored only; `toParsedSceneSource()` → `serializeSceneSource()` → temporary file → rename.
- **P3 — `AuthoringConnection`** (ADR-0130 D1).
  - `atlantis/connection/authoring_connection.h`: the interface, its value types (commands, events — `Applied`
    included — refusals, node summaries), tickets and subscription ids mirroring `RuntimeConnection`'s; the
    InProcess endpoint in `src/authoring/`.
  - Connection's dependency statement gains one Asset System header, `atlantis/asset_system/scene_property_address.h`
    (AGENTS.md, M7).
  - `PropertyValue` stays `world::access::PropertyValue`.
- **P4 — Reload** (ADR-0131 D3–D7; Spec R8, R10).
  - `RuntimeApplication::requestSceneReload(ValidatedSceneData)` stores one pending reload (a newer request
    replaces an older one).
  - At the start of `runFrame()`, after `applyPending()`, a named place performs it: dependency resolution reusing
    the loaded maps (`loadAndBakeScene()`'s steps factored so the resolution and upload take existing maps),
    `bakeScene()`, new `RuntimeWorldAccess`, swap of `scene_` / `worldAccess_` and the resource maps, re-binding of
    the InProcess endpoint, pick geometry rebuilt when retained, then a `waitIdle()` and the release of every mesh,
    material and texture the new scene does not reference (ADR-0131 D4); on failure nothing swaps and a diagnostic
    is recorded.
  - The sink reports a successful swap back to the host, which tells the session to emit `Applied{generation}`.
  - `RuntimeStatus` gains `sceneGeneration` (`std::uint64_t`); `RuntimeControlHost` reports it.
  - E2: `editor.cpp`'s scene-change clear also compares the generation (one line; a change forced by a
    shared-surface Spec, allowed by ADR-0125 D4).
- **P5 — The host** (ADR-0129 D2).
  - `atlantis_runtime --listen` creates the session lazily on the first `authoring.open`; in-process clients (the
    CLI under `--exec`) get it from the host. The sink is `requestSceneReload()`.
  - The scene's source is found through the catalog source the build already knows (its path compiled in, as the
    shader directories are) and, for an imported scene, the generated source the build records.
- **P6 — Wire, CLI, C# SDK** (ADR-0130 D3).
  - Remote: `authoring.open`, `authoring.document`, `authoring.listNodes`, `authoring.listComponents`,
    `authoring.getProperty`, `authoring.schema`, `authoring.submit`, `authoring.submitTransaction`,
    `authoring.subscribe`, `authoring.unsubscribe`, `authoring.drainEvents`, `authoring.drainFailures`,
    `authoring.apply`, `authoring.save`; codec values, the `Applied` event included; `remote_protocol.md` §5/§6.
  - CLI: the `scene` group (Spec 0065 Q5) with `cli/1` envelopes and `--json`.
  - C# SDK: a reflective `AuthoringSession` on `RemoteSession` (Spec 0065 Q8, G-b): types and fields by name or
    id from `authoring.schema`, values as `PropertyValue`, records for every command and event (`Applied`
    included); conformance vectors extended. No generated bindings of the scene components: `atlantis_sdk_codegen`
    is not changed.
- **P7 — The second edit target** (ADR-0130 D4; Spec R12): `tests/authoring/authoring_edit_target_tests.cpp`
  implements `EditTarget` over `AuthoringConnection` and runs E2's `EditHistory` unchanged.

## Milestones / Task Breakdown

Each milestone builds and runs the affected suites (Debug) before the next.

### M1 — In-memory cook and decode (P1; R7's determinism)

- `src/asset_system/` (`cook_scene.{h,cpp}`, `decode_scene.{h,cpp}`, `validated_scene_data.h`'s friend line).
- Tests: in-memory artifact = `cookScene()` file, byte for byte, for every committed scene and Bistro when present;
  in-memory decode = file decode; every existing cook and decode test unchanged.

### M2 — The authoring session and `AuthoringConnection` (P2, P3; R1–R6, R9)

- `src/connection/include/atlantis/connection/authoring_connection.h`; `src/authoring/` (session, InProcess endpoint);
  CMake.
- Tests (GPU-free): each command and its events; transactions all or nothing; one refusal per `sceneSchema()`
  constraint (the Spec 0049 conformance cases reused); a document after any sequence equals re-parsing its saved
  text; save byte-identical for an unchanged document; imported save refused; the boundary scan (no Runtime, ECS,
  Platform, RHI, Renderer or OS include).
- Measurement (reported, not a verdict): one single-property transaction on Bistro's generated document, Release,
  median and maximum over 100 transactions, against J9.

### M3 — The second edit target (P7; R12)

- `tests/authoring/authoring_edit_target_tests.cpp`.
- Tests: a recorded authoring edit undoes and redoes through E2's history; a second client's change refuses an undo;
  a destroyed node invalidates its entries.

### M4 — Reload (P4; R8, R10)

- `src/runtime/` (`runtime_application.{h,cpp}`, `scene_load.{h,cpp}`, `runtime_control_host.cpp`),
  `src/connection/include/atlantis/connection/runtime_control.h` (`sceneGeneration`), `src/editor/src/editor.cpp`
  (one line).
- Tests (GPU, fatal VVL): after a reload of an edited document, frame data and a capture equal a fresh Runtime on
  the saved document; a failing reload leaves the old World and its frame data; connections survive, a
  subscription keeps its filter, `sceneGeneration` increments and an authoring subscriber receives
  `Applied{generation}` with the same value; a CLI `property set` lasts until the next reload; E2's history clears
  on the generation change; Bistro's apply time measured (J3).
- Scene-sized state: an edited default scene that adds materials, textures and point lights beyond the original's
  (within the scene limits) reloads to frame data and a capture equal to a fresh start on that document —
  descriptor pool capacity (Spec 0021), the light clustering and shadows exercised on the reload path; applying back
  to the original leaves the mesh, material and texture maps equal to a fresh start's (release, ADR-0131 D4).

### M5 — Host, wire and CLI (P5, P6; R11)

- `src/runtime/main.cpp`, `src/remote/` (codec, server, client, `remote_protocol.md`), `src/cli/`.
- Tests: codec round trips; server and client end to end over loopback; the CLI's `scene` envelopes; conformance
  vectors regenerated.

### M6 — The C# SDK (P6; R11)

- `src/csharp/Atlantis.Gameplay/` (the reflective `AuthoringSession`, values, codec), the fixture server, the C#
  suites.
- Tests: the new vectors (the `Applied` event included) decoded and re-encoded byte for byte; the session against
  the fixture server, by type and field name. Zero NuGet unchanged.

### M7 — Normative docs and acceptance

- AGENTS.md and `docs/architecture/module_boundaries.md`: Atlantis Authoring's section; Connection's second
  interface; Asset System's in-memory cook and decode; Runtime's reload place; the `runFrame()` guard's lines.
- North star (Spec 0065), through the CLI only, on the default scene and Bistro: open, set a light's intensity,
  reparent a node, apply — the Runtime shows it — save (default scene), rebuild, restart: the same frame data.
- Acceptance: Debug and Release full suites; goldens byte-identical; VVL clean; `assembleDebug`; the C# SDK rules
  check unchanged; the performance gate run and its verdict reported, with the changed `runFrame()` lines
  enumerated.

## Files / Modules Touched (expected)

- **Asset System:** `src/asset_system/include/atlantis/asset_system/cook_scene.h`, `decode_scene.h`,
  `validated_scene_data.h`; `src/asset_system/src/cook_scene.cpp`, `decode_scene.cpp`.
- **Connection:** `src/connection/include/atlantis/connection/authoring_connection.h` (new),
  `runtime_control.h` (`sceneGeneration`), `src/connection/CMakeLists.txt`.
- **GUID mint:** `src/tools/asset_cooker/guid_mint.{h,cpp}` moved to `src/asset_system/` (J8), with the cooker's
  call sites.
- **Authoring (new):** `src/authoring/**`, the root `CMakeLists.txt` (one `add_subdirectory`).
- **Runtime:** `src/runtime/include/atlantis/runtime/runtime_application.h`, `src/runtime/src/runtime_application.cpp`,
  `src/runtime/include/atlantis/runtime/scene_load.h`, `src/runtime/src/scene_load.cpp`,
  `src/runtime/src/runtime_control_host.cpp`, `src/runtime/main.cpp`, `src/runtime/CMakeLists.txt`.
- **Remote:** `src/remote/src/{codec,remote_server,remote_client}.{h,cpp}`,
  `src/remote/include/atlantis/remote/remote_client.h`.
- **CLI:** `src/cli/` (the `scene` group, help text).
- **Editor:** `src/editor/src/editor.cpp` (the generation in the history-clear condition).
- **C# SDK:** `src/csharp/Atlantis.Gameplay/**` (authoring session, values, codec).
- **Tests:** `tests/asset_system/`, `tests/authoring/` (new), `tests/connection/`, `tests/runtime/`, `tests/remote/`,
  `tests/cli/`, `tests/csharp/` (vectors, suites, fixture server), `tests/editor/editor_fixture.h` (the
  `RuntimeStatus` field).
- **Docs:** `docs/architecture/remote_protocol.md` (M5); AGENTS.md, `docs/architecture/module_boundaries.md` (M7);
  registry, README, blueprint and status lines after merge (J6).
- **Not touched:** `src/world/` (`bakeScene()` unchanged), the scene source grammar and artifact format, the
  catalog format, Renderer, RenderGraph, RHI, Vulkan Backend, shaders, `src/editor_app/`, `src/tools/sdk_codegen/`
  and the committed generated bindings (Spec 0065 Q8, G-b), goldens, `tests/performance/baselines/`,
  `gate_policy.json`.

## Sequencing & Dependencies

- M1 first (everything cooks through it). M2 needs M1; M3 needs M2; M4 needs M1; M5 needs M2 and M4; M6 needs M5;
  M7 is last. M3 and M4 are independent.
- One branch, `feature/0065-authoring-data-layer`, one PR.

## Verification Checklist

- [ ] Unit / headless: in-memory cook = file cook (every scene, Bistro); the session's commands, events,
  transactions and refusals; save byte identity and the imported refusal; the second edit target over E2's
  history; codec round trips and vectors; CLI envelopes; the reflective C# session; boundary scans; the validation
  cost on Bistro (J9).
- [ ] GPU (fatal VVL): reload = fresh start (frame data, capture); failed reload keeps the old World; connections
  and subscriptions across a reload; `sceneGeneration` and `Applied{generation}`; scene-sized state (more materials,
  textures and lights) on the reload path; unreferenced resources released; runtime-side edits gone after a reload;
  E2's history cleared; Bistro's apply time.
- [ ] Image regression: all goldens byte-identical.
- [ ] Vulkan Validation Layers clean.
- [ ] Other: Debug + Release full suites; `assembleDebug`; the C# SDK's zero-NuGet check unchanged; the
  performance gate's verdict with the `runFrame()` lines enumerated; the north star on the default scene and Bistro.

## Risks

- **Apply is slow on Bistro:** measured in M4 against J3's budget; above it, the owner rules between accepting it
  and specifying live projection (Spec 0065 Q4 (B-c)) next.
- **Re-binding connections** touches the Remote server's per-client connections and every subscription; M4's tests
  cover open connections from both the InProcess endpoint and Remote.
- **Validation per transaction on large documents:** measured in M2 on Bistro's generated source against J9;
  above it, incremental validation is specified next (Spec 0065 Out of Scope).
- **The decode boundary's friend declaration** grows by one overload; any other constructor of `ValidatedSceneData`
  stays impossible.

## Joint Review decisions — ruled (slmao, 2026-10-22, PR #280): J1–J9 as proposed

- **J1 — Spec Corrections:** none proposed.
- **J2 — Validation through a cook dry run** (P2): each transaction is validated by running P1's in-memory cook on
  the candidate document, so the authoring session cannot accept what the cooker would refuse. Proposed: accept; a
  separate validator would be a second statement of the same constraints.
- **J3 — Apply's budget on Bistro** (P4): reported as measured; proposed target under 1 s for cook, decode, bake and
  swap with no new GPU uploads, on the reference machine. Proposed: accept as a reported target, not a test verdict.
- **J9 — Validation's budget on Bistro** (P2, J2): one single-property transaction on Bistro's generated document
  (about 6,000 nodes), validated by the in-memory cook dry run on the frame thread; proposed target under 10 ms
  median in Release on the reference machine (a sixth of Bistro's frame of about 60 ms), reported as measured like
  J3, not a test verdict. Proposed: accept; if it is missed, incremental validation — checking only what the
  transaction's commands can affect, plus the document-wide limits — is the follow-up.
- **J4 — Session creation** (P5): lazily on the first `authoring.open` under `--listen`, no new command-line option.
  Proposed: accept; a Runtime that is never edited pays nothing.
- **J5 — One pending reload** (P4): a newer apply replaces a pending one in the same frame. Proposed: accept; only
  the newest document matters.
- **J6 — Docs split:** normative sentences with the implementation PR, narratives after merge (Plans 0056, 0062–0064).
- **J7 — The acceptance run:** the owner runs the north star through the CLI on both scenes; recorded in the PR as
  Plans 0062/0063 recorded J6.
- **J8 — The mint's home** (P2): `guid_mint` moves from the asset cooker tool into Asset System, still the only
  minting code, used by the cooker and by `CreateNode`. Proposed: accept; the alternative, requiring every
  `CreateNode` to carry a client-minted GUID, would put minting in every client (the C# SDK included), against
  ADR-0097 D1's one place.

Standing rules, as for Plans 0062–0064: no merge by the agent and no push to `main`; goldens byte-identical, none
re-captured; `tests/performance/baselines/` and `gate_policy.json` untouched; changes limited to this Plan's file
list, deviations disclosed; the changed `runFrame()` lines enumerated in the PR; the performance gate's verdict
reported as measured; the C# SDK's zero-NuGet check unchanged and passing.

## Rollback Plan

Revert the implementation PR. The authoring session exists only when a client opens it; without an apply the
frame path is unchanged; the wire additions are additive; `cookScene()` keeps its signature and output. No format,
golden or baseline depends on this work.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- The performance gate's verdict is reported and the changed `runFrame()` lines enumerated.
- Bistro's apply time is reported against J3.
- The owner's acceptance run (J7) is recorded in the PR.
