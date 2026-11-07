# ADR 0146: Managed Asset Rebuild and Generation Publication

- **Status:** Proposed
- **Date:** 2026-11-07
- **Deciders:** Pending joint review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0085](../specs/0085-shared-asset-rebuild.md)
- **Related ADRs:** Extends [ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md),
  [ADR-0106](0106-attachable-runtime-transport-and-control.md),
  [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md)
  and [ADR-0143](0143-shared-cooked-asset-catalog-queries.md).

## Context

The build graph owns GUID declarations, import options, cooker execution and
whole-catalog validation. An import can remove and rewrite a tree even when
a Runtime that uses it is running. Discovery retains the first catalog
result and authoring scene reload independently reads the configured
catalog path. The same running process cannot safely treat a changed set of
files as its original snapshot. Asset System is Core-only and the product
editor is an ordinary shared client.

## Decision

1. The Windows Runtime executable hosts one opt-in build coordinator,
   with one worker and exclusive writer ownership of a configured staging
   workspace from bootstrap to shutdown. The existing Tools and CMake graph perform
   all importing, cooking and assembly. Producer lookup comes from
   the existing declarations. Only fixed launch inputs and admitted
   GUIDs enter the coordinator. Asset System, Authoring, RuntimeHost
   and the frame loop do not acquire tool execution or worker ownership.
2. Connection's asset surface gains the two operations in Spec 0085.
   Remote projects those values. Native and managed SDKs, CLI, Editor App
   and MCP use the same operations. The Runtime process owns job IDs and
   bounded outcome retention. The workspace lock arbitrates writers across
   processes. Job observation survives a client disconnect within that
   process. Activation authority stays with the trusted local launcher.
3. The managed pipeline publishes by copying an assembled catalog and
   its outputs to a self-contained immutable generation. A new directory
   becomes visible only after successful validation and a same-volume
   rename. Ordinary builds still own their build outputs. No process
   edits a published catalog in place. Discovery retains ADR-0143's
   snapshot lifecycle for each process. Explicit generation selection
   at bootstrap makes loader, reload resolution, generated imported
   source and discovery agree. This narrows ADR-0098 D2's runtime-write
   restriction to prohibit in-place writes while permitting the
   executable's private coordinator to drive isolated publication.

## Consequences

The build stays the authority for artifacts and existing identities. Clients
can distinguish rebuild, publication and activation. A slow or failed tool
does not stall frame processing or overwrite an active build. The first
delivery pays for a separate workspace, a whole-catalog check and copied
outputs. It needs an explicit restart and local cleanup. A process failure
can lose a job receipt even when the generation was published.

## Alternatives Considered

- **Rebuild in the active CMake tree.** A normal build rewrites outputs
  before the final catalog is available and may leave mixed files on failure.
- **Launch Tools independently from each client.** Duplicates producer
  selection, permission, concurrency and failure handling. Scripts and
  agents would have a different contract from the editor.
- **Replace assets inside the active Runtime.** Requires generation-aware
  caches, reload and rendering transactions and a new catalog lifetime.
  Those concerns exceed the first management slice.
