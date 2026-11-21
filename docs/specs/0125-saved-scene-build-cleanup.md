# Spec 0125 — Saved Scenes Build Cleanup

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0125](../plans/0125-saved-scene-build-cleanup.md)
- **Approval:** Maintainer approved `a4227c03` and J1–J4 in [PR #415](https://github.com/slmao/Atlantis/pull/415#issuecomment-7058022375), 2026-11-21. Design merged at `048680c3`.
- **Related ADR:** [ADR-0174](../adr/0174-saved-scene-build-cleanup-ownership.md) (Accepted)

## Summary

Saved Scenes can retain a build across Editor restarts. Add an explicit way to
remove one older build directory whose ownership and idle state the launcher can
establish for the current user and trusted checkout.

## Motivation

[0090](0090-saved-scene-build-open.md) creates a fresh directory per build.
[0123](0123-saved-scene-build-reuse.md) retains one receipt, so superseded build
trees accumulate. The attempts root is shared by the user's checkouts. Its
random directory names and empty reservation files establish neither checkout
ownership nor process lifetime. [0124](0124-saved-scenes-recent.md) tracks scene
shortcuts independently of these build directories.

## Requirements

- **R1 — Discover owned attempts.** Under Saved Scenes, expose **Clean Up
  Builds…** only with a trusted checkout. Offer attempts created with the
  ownership evidence defined by ADR-0174. For each eligible attempt show the
  full build path, the sum of ordinary file lengths (labelled as build size),
  and why it is eligible, such as a superseded successful build. Support an
  empty state and explain that protected or unverifiable directories are omitted.
  Size is an estimate of data in the directory. It can differ from disk space
  recovered. Opening and refreshing the list are read-only.
- **R2 — Protect current and in-use builds.** Keep the directory named by the
  current build slot, including when its receipt is incomplete or stale. A
  corrupt or unsupported slot whose target cannot be determined disables
  cleanup for that checkout. Require positive ownership and settlement evidence.
  Skip attempts used by an Editor, Runtime or build task, as well as those with
  unknown use, including the launch and compatibility cases in ADR-0174. An old
  timestamp, an unlisted process name or an absent child in the current window
  gives no authority to delete.
- **R3 — Recheck and delete the selected directory.** Require one row and an
  explicit confirmation that identifies its path and size and says that build
  outputs and logs will be permanently removed. Before the first deletion,
  serialize with Build and both Open entry points, and recheck the trusted
  checkout, slot, ownership, usage, and ordinary NTFS tree by filesystem object
  identity and relative membership. Bind confirmation to the observed row and
  tree, including file lengths and last-write metadata. A replaced object,
  changed inventory or eligibility, or incomplete check refuses the request
  without removing anything. Never traverse reparse points or delete outside
  the selected tree.
  Hold the relevant guards through deletion. If deletion fails after it starts,
  stop, report the path and partial result, and require a fresh list and
  confirmation before any further deletion. Preserve ownership evidence until
  the attempt directory is gone.
- **R4 — Preserve the launcher workflow.** Cleanup changes only the selected
  build tree and its private cleanup bookkeeping. Preserve the latest receipt,
  Recent history, source and assets, and the existing Build and Open checks.
  Keep the UI responsive while scanning or deleting, discard stale list results,
  and show the final outcome. Cancel or close before confirmation has no effect.

## Proposed design

Follow the [0093 visual design](0093-editor-visual-layout.md) for the Saved
Scenes entry, list, confirmation and refusal. After building A and then B,
the user can select A if its ownership and idle state are established. After
deleting A, the user can select B and explicitly Open from its existing receipt.
The same visit can report that another build could not be safely identified.

## Architectural impact

[ADR-0174](../adr/0174-saved-scene-build-cleanup-ownership.md) specifies private
ownership, the participation boundary for reusable attempts, and the OS evidence
required to delete. This is a Windows Editor operation. Runtime, CMake targets
and recipes, authoring, and shared client contracts retain their current behavior.

## Alternatives and trade-offs

The ADR explains why scanning old folders by name or age, or inferring use from
the latest receipt, cannot support safe deletion. The first release may retain
substantial output. It leaves legacy attempts and attempts admitted to Open on
disk because their complete Editor and Runtime lifetime is outside its proof.

## Verification

[Plan 0125](../plans/0125-saved-scene-build-cleanup.md#verification-checklist)
maps R1–R4 to focused checks and a native Windows 100% run. It covers deletion
of an idle older build, Open of the current build, and refusal when a confirmed
candidate changes before deletion.

## Out of scope

Automatic cleanup, scheduled retention, batch deletion, reclamation of legacy
or launch-admitted attempts, and cleanup across users or checkouts.
