# Spec: Build and Open Saved Scenes

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-11
- **Related Plan:** [Plan 0090](../plans/0090-saved-scene-build-open.md)
- **Approval:** Jointly approved at revision `6a128904` of [PR #343](https://github.com/slmao/Atlantis/pull/343), 2026-11-11. See Plan 0090.
- **Related ADR:** [ADR-0150](../adr/0150-local-saved-scene-build-launch.md) (Accepted)

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
  Editor apphost enables the panel through explicit local launch settings for
  the absolute trusted Atlantis checkout and local CMake executable. The operator
  may choose an absolute trusted committed managed import root in the panel
  while idle. These settings do not come from a Runtime session, receipt,
  current document, or the child apphost. Build authority and the Runtime's
  source-write opt-in are separate. The panel can work without a connected
  Runtime. Limit the panel to an ordinary launcher whose own scene selection
  is other than `saved_scene`, so it cannot open a second copy of its own
  saved document.

  Show the checkout and distinguish a missing saved directory from a path or
  access error. On opening the panel or on Refresh, list the immediate
  `assets/scenes/saved` children in ordinal order. Use the existing canonical
  `save_<name>` spelling and logical name rules. Inspect ordinary paths on
  the local case-insensitive NTFS checkout. Refuse reparse points in the
  checkout ancestry and through the unit and both fixed leaves. Omit private staging,
  unrelated names, and incomplete or unsafe units. Repeat the same checks
  before Build and Open. The list is a discovery view. CMake owns catalog,
  identity and source validation.

  Only a typed success from New Scene or Save As may suggest a name. Show its
  source path and scene GUID separately from the candidate's checkout. The
  user explicitly selects the local unit, even when its name matches the
  hint. A match does not establish identity or location. Refusal and unknown
  outcomes retain the existing inspection and settlement guidance. Neither
  outcomes nor Refresh start a build or launch.
- **R2 — Build inputs and generation.** Build starts only on explicit user
  action. First release uses one profile, Visual Studio 18 2026, x64, Debug.
  Invoke the configured CMake executable with structured arguments and no
  shell. Configure with `-S <checkout> -B <attempt>`, `-G "Visual Studio 18 2026"`,
  `-A x64`, and `-DATLANTIS_SAVED_SCENE=<name>`. Pass
  `-DATLANTIS_MANAGED_IMPORT_ROOT=<root>` only for an explicit selection.
  Then use `cmake --build <attempt> --config Debug --target atlantis_runtime`.
  Show the captured settings, selected name and attempt directory.
  The saved slot cooks the checkout and any committed managed imports at
  build time. It does not consume, change, or publish the original Runtime's
  asset generation. The child receives neither `--asset-build-profile` nor
  `--asset-generation`. A managed reference uses the committed source and
  stable asset identity in the selected root. The existing build or Runtime
  startup path can reject missing dependencies. A successful build does not
  prove that the Runtime can load the scene.
- **R3 — Build state, isolation and retry.** Allow one configure/build in
  flight per launcher. Lock selection, Refresh, and managed root changes
  while it runs. Disable Build and Open during the attempt. Show Configuring,
  Building, Failed, Stale or Succeeded, with the phase, available exit
  result and path to retained stdout/stderr. Drain output off the UI thread.
  The launcher owns these tool processes. Refuse ordinary close while the
  attempt is active and tell the user when they can close again. On forced
  exit, leftover tools may finish in the isolated attempt directory. A new
  launcher starts with no eligible result.

  Each accepted Build immediately invalidates the previous result, even
  when configuration never starts. Reserve a fresh directory under the launcher's local work area for every
  attempt. Changing the managed root or losing the selected unit from a
  refresh invalidates eligibility. Open can use only the last successful
  result for the selected unit and inputs.

  Capture fingerprints of both saved-unit files at admission, and compare
  after building and immediately before Open. Require successful configure
  and build, then resolve the executable from the attempt's current CMake
  File API codemodel for the exact Debug `atlantis_runtime` target. Request
  the codemodel before configure. Require an unambiguous ordinary executable
  within the attempt directory and capture its fingerprint with the result.
  Immediately before Open, require that the executable still resolves
  inside that directory and matches the fingerprint. A changed, missing,
  or unsafe checked input or executable makes the result stale and requires
  explicit Build. A fresh attempt cannot fall back to earlier output.
  These checks detect changes in the two source files and the executable.
  Broader build freshness and source locking remain out of scope.
- **R4 — Explicit independent open.** Enable Open only for an eligible
  selected result. Start the same ordinary Windows Editor apphost with an
  absolute `--runtime <built executable>` and `--scene saved_scene`. The child
  owns its Runtime and session file. The launcher reports process start or
  start failure. The child reports Runtime startup, connection and document
  readiness through its ordinary UI. Do not transfer the original session,
  source-write opt-in or build-launch settings to the child.

  Reserve the canonical local unit before starting the process. Suppress
  repeated Open and Build for that unit until start fails or the launched
  Editor exits. Refuse ordinary close while start is in flight. A
  successfully started child survives closure of the launcher. A start
  failure with an eligible result permits another Open. If Runtime startup
  or document open fails in the child, show the ordinary diagnostics there.
  Close the child and retry Open if the same artifact remains eligible.
  For missing build inputs, correct the inputs, Build again, then Open.
  The guard is per launcher and does not cover other Editors or manual
  launches. Show the source so users can avoid simultaneous writers.
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
the saved unit and resolved executable. The build consumes live checkout and managed source inputs.
The launcher is not a source lock. Confirm these bounds in joint review.

## Out of Scope / Future Work

Build presets, caching, cancellation, persisted jobs, generation-backed
saved-slot builds, and a shared build service may be proposed separately.
