# Spec: Build and Open Saved Scenes

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-11
- **Related Plan:** [Plan 0090](../plans/0090-saved-scene-build-open.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0150](../adr/0150-local-saved-scene-build-launch.md) (Proposed)

## Summary

Add a Windows development Editor entry point for the saved scene units produced
by [Save As](0088-scene-save-as.md) and [New Scene](0089-new-hand-authored-scene.md).
The user selects a unit, explicitly builds it with the existing CMake saved slot,
and opens the successful build in a separate ordinary Editor. The original
Editor keeps its Runtime and document throughout.

## Motivation / Problem Statement

Both publication operations can report a durable source path. To use it today,
the user must translate that receipt into a CMake selection, find the executable
for the configuration they built, and pass the correct arguments to another
Editor. This also leaves room to mistake an old output or the current Runtime's
asset generation for the new build.

## Goals

- Make selection, configure/build, result, and open distinct user actions or states.
- Reuse the saved-slot build and ordinary Editor launch so the same build remains
  usable from the command line and by shared Runtime clients.
- Keep source publication, asset generations, editing, and process ownership separate.

## Non-Goals

Project browsing outside the selected checkout, asset generation selection or
publication, arbitrary CMake flags or build presets, build queues, cancellation,
recovery across launcher restarts, deletion of build outputs, live scene switching,
and cross-process editing or build locks.

## Requirements

### Functional

- **R1 — Local workspace and selection.** The ordinary Windows development
  Editor apphost enables this panel only with explicit local launch settings
  naming an absolute trusted Atlantis checkout and CMake executable. The
  operator may select an absolute committed managed import root in the panel
  while idle. These inputs do not derive from Runtime paths, RPC data, or the
  current document. Show the selected checkout. On opening the panel or on explicit
  Refresh, list names in ordinal order from the immediate children of
  `assets/scenes/saved` that have canonical `save_<name>` names and the two
  ordinary leaves `scene.scene.txt` and `catalog-source.txt`. Reuse the logical
  name rules, refuse reparse points in the inspected paths, and ignore private
  staging, unrelated entries and incomplete units. Treat this as discovery
  only. CMake remains authoritative for catalog, identity and source validation.
  A confirmed New Scene or Save As result may offer its logical name as a
  selection hint after Refresh. Show the receipt and the local checkout
  separately. A matching name does not verify that a receipt came from this
  checkout. Refusals and unknown outcomes never imply that a unit is ready to
  build. Existing inspection and settlement rules still apply.
- **R2 — Build inputs and generation.** Build starts only on explicit user
  action. First release uses one profile, Visual Studio 18 2026, x64, Debug.
  Use the configured CMake executable with an argument vector, the selected
  checkout as source, a fresh absolute build directory owned by this attempt,
  and `-DATLANTIS_SAVED_SCENE=<name>`. Configure with the optional
  `ATLANTIS_MANAGED_IMPORT_ROOT` only when explicitly supplied, then build the
  `atlantis_runtime` target with `--config Debug`. Show these settings and the
  build directory with the result. The saved slot cooks the checkout and any
  committed managed imports at build time. It does not consume, change, or
  publish the original Runtime's asset generation. The child gets no
  `--asset-build-profile` or `--asset-generation` arguments. If a scene refers to
  a managed asset, use its committed source and stable identity in the supplied
  root. Missing dependencies are build failures with actionable diagnostics.
- **R3 — Build state, isolation and retry.** Allow one configure/build in
  flight per launcher. Lock selection, Refresh, and managed root changes while it runs. Disable
  Build and Open during the attempt. Show Configuring, Building, Failed or Succeeded, with the relevant
  exit result and a path to the retained stdout/stderr log. The launcher owns
  the configure and build processes, drains their output off the UI thread
  and defers ordinary window close until they settle. On forced exit, leftover
  tool processes may finish in that attempt's isolated directory. A new
  launcher starts with no eligible result. Each accepted Build discards the
  previous Open eligibility and uses a fresh directory, including retries. Changing the managed root invalidates Open eligibility.
  Record the selected two files' content fingerprints at admission. Recheck
  them after building and before Open. If either differs or is missing, mark
  the attempt stale and require explicit Build. This check covers the two
  saved-unit files and does not lock them or certify other build inputs.
  Configure and build must both succeed. Resolve the executable from the
  fresh CMake File API codemodel for the Debug `atlantis_runtime` target and
  require an existing ordinary executable resolved inside the attempt's build
  directory. Missing
  or ambiguous artifact metadata is a build failure. No previous output can
  satisfy the new attempt. Retain output and logs for inspection.
- **R4 — Explicit independent open.** Enable Open only for the selected unit's
  successful, current build result. Use the same ordinary Windows Editor
  apphost with the built executable and `--scene saved_scene`. The child
  Editor owns its Runtime and session file. The launcher reports whether
  the Editor process started, and the child reports its Runtime connection
  and document readiness through the ordinary UI. The launcher never
  attaches to that Runtime or transfers the original session, source-write
  opt-in or build-launch settings to the child. Suppress repeat Open while
  starting or while a child launched by this launcher for that source is
  alive. Disable Build for that same source while the child is alive. A child
  exit releases both guards. A process start failure is
  retryable against the same eligible build. If Runtime startup fails
  inside the child, the user can inspect its diagnostics, close it and
  retry Open or Build as appropriate. A successfully started child
  survives closure of the launcher. External Editors and manual launches
  are outside the duplicate guard. The UI must identify the source so
  users can avoid opening it for writing in multiple Editors.
- **R5 — Editing continuity.** Ordinary authoring in the child binds to the
  source of `saved_scene`. Existing shared editing, apply, undo/redo, and Save
  rules govern that document. The original Editor remains on its own Runtime,
  with its current document, accepted edits, draft and history under their
  usual owners. Build and Open do not trigger an authoring Save or apply in
  the original. Build failure and launch failure leave both source unit files
  available for another attempt.

### Non-functional

Keep filesystem discovery and developer process orchestration local to the
Editor. Use existing CMake and Editor apphost dependencies. No work enters the
Runtime frame loop, no asset or scene format changes, and no change to Runtime,
authoring, asset service or Remote contracts.

## Proposed Design

Add a small saved-scene panel with Refresh, Build, an attempt result, and Open.
A successful result is session-local and binds the selected name, source-file
fingerprints, fixed build settings and resolved executable to one attempt.
Existing CMake selection and ordinary Editor startup perform the authoritative
work. The CLI can use the same CMake commands and launch arguments directly.
The SDK and MCP continue to connect through the existing Runtime session.

## Architectural Impact

[ADR-0150](../adr/0150-local-saved-scene-build-launch.md) records ownership of
local developer builds and process launch and their boundary with shared
authoring and asset generation. It builds on the offline saved slot in
[ADR-0148](../adr/0148-scene-save-as-and-explicit-launch.md) and the separation of
creation from activation in [ADR-0149](../adr/0149-create-hand-authored-scene-through-shared-authoring.md).

## Alternatives Considered

See ADR-0150 for keeping only the manual recipe, adding a Runtime build verb,
and making a build immediately switch the active scene.

## Testing & Verification Plan

[Plan 0090](../plans/0090-saved-scene-build-open.md#verification-checklist) maps
R1–R5 to focused tests and an end-to-end Windows run. Exercise both publication
receipts, including a copy with a committed managed mesh, and follow the
successful build into a second Editor, edit and save.

## Risks & Open Questions

The proposed first release uses a single toolchain profile and retains build
directories until the user cleans them up. Its freshness check is limited to
the saved unit. The build consumes live checkout and managed source inputs.
The launcher is not a source lock. Confirm these bounds in joint review.

## Out of Scope / Future Work

Build presets, caching, cancellation, persisted jobs, generation-backed
saved-slot builds, and a shared build service may be proposed separately.
