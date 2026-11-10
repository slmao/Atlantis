# ADR 0150: Local Saved Scene Build and Launch

- **Status:** Proposed
- **Date:** 2026-11-11
- **Deciders:** Pending human review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0090](../specs/0090-saved-scene-build-open.md)

## Context

[ADR-0148](0148-scene-save-as-and-explicit-launch.md) makes a saved source unit
buildable through a single offline slot. [ADR-0149](0149-create-hand-authored-scene-through-shared-authoring.md)
keeps the source publication receipt separate from activation. The remaining
user steps require local knowledge of a checkout, toolchain and executable.
Runtime sessions already own live authoring and asset generation separately.

## Decision

Place the workflow in the ordinary Windows development Editor launcher. The
operator chooses the trusted local workspace and tools. The launcher can list
candidate saved unit names, own a fresh CMake configure/build attempt and use
CMake's codemodel to locate the target. The existing saved-slot configuration
continues to own validation, catalog assembly and cooking. A confirmed source
receipt can suggest a name and carries no authority to run tools or choose a
workspace. Local launch authority is independent of the Runtime's
source-write permission. Scripts and CLI users can invoke the same CMake
and launch recipe.

Opening is a separate action that starts another ordinary Editor. That Editor
owns its Runtime and session. This extends the local process-launch
responsibility in [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md).
The shared client surface still starts at the Runtime connection. The original
asset generation stays with its own Runtime, and the saved-slot build uses
its configured committed source inputs. Build success identifies a local
artifact. The child Editor owns startup and authoring readiness.

## Consequences

The Editor can provide a complete path from a durable receipt to an editing
session without introducing a Runtime task type or a project service. Each
attempt can be diagnosed from its own directory and log. The first release
costs a fresh build per attempt and requires a supported local Windows
toolchain. It uses session-local results and duplicate guards. Freshness
checks cover the saved unit and chosen executable. Independent manual
launches can still use the same source and require coordination by the user.

## Alternatives Considered

- **Keep the manual recipe only.** This keeps the smallest Editor surface but
  leaves output selection, result reporting and correct launch to the user.
- **Add a shared Runtime build command.** This would give a live Runtime
  responsibility for a trusted checkout, developer tools and a second
  executable. Those inputs and outputs belong to the local build workflow.
- **Build and activate in one step.** This would conflate a successful build
  with Runtime readiness and require a new policy for the old document and
  unsaved work.
