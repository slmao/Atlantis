# ADR 0172 — Local Receipt for the Last Saved Scene Build

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Maintainer
- **Acceptance:** Accepted at `022bc4d5` in [PR #411](https://github.com/slmao/Atlantis/pull/411), merged by the maintainer at `b081db2f`.
- **Related Spec:** [Spec 0123](../specs/0123-saved-scene-build-reuse.md)

## Context

[ADR-0150](0150-local-saved-scene-build-launch.md) assigns build and launch to
the trusted local Editor and makes success session-local. That owner can retain
evidence across restarts. Since the Runtime uses absolute paths to cooked
content in the build tree, preserving the `.exe` alone would be insufficient.

## Decision

**D1 — One private receipt.** Use a versioned, bounded local record under the
Windows user's Local AppData for each canonical trusted checkout. Keep reusable
attempts in an ordinary local NTFS directory under that area so they survive
ordinary launcher restarts with unchanged absolute paths. Bind the receipt to
one unit, one managed-root setting and one completed attempt. Store its
identifier, build profile, comparable input and output fingerprints and
diagnostic context. Resolve the executable and known outputs from the trusted
attempt. Receipt data never expands the allowed roots or selects a command.

Use one checkout-local, per-user OS lease to serialize Build admission,
publication and the final Open decision among participating launchers. Replace
the slot atomically with a no-result state before an admitted Build invokes
CMake. If that write fails, refuse to start the Build. After admission,
publication failure leaves the no-result state. Publish a complete result
only after successful settlement under the lease. A crashed or superseded
attempt cannot republish. The lease ends after build settlement or the launch
decision and adds no coordination with manual tools, source writers or running
children. The existing child guard remains launcher-local.

**D2 — Observe the existing build.** CMake remains authoritative about what the
saved-slot recipe resolves. Start with the CMake File API, the existing build
cache and producer metadata, plus conservative inventories for controlled roots.
If indispensable, emit only private, versioned saved-slot metadata from the
existing CMake declarations to close evidence gaps, without changing targets,
inputs or argument semantics. Use the producer inventory as evidence about
declared asset producers. Establish coverage separately for indirect files,
directory membership, effective includes and non-asset build inputs. Use content
digests over deterministic entries and resolved paths. A layout, input class or
override without coverage cannot produce a reusable receipt. Validation uses
the current selection and local evidence, and performs no CMake invocation.

This supplements only ADR-0150's session-local result and limited freshness
checks for the reuse workflow. Build readiness and child Runtime readiness are
still separate. No shared protocol or asset generation participates.

## Consequences

Launching a checked result can avoid an entire configure and build. Verification
has an I/O cost and a conservative inventory may invalidate on an unrelated
change within a covered tree. Supported workspace inputs can be checked entirely
locally. Other layouts can continue with an explicit 0090 build. Old, missing
or damaged receipts cannot enable Open after restart. Retained attempts consume
disk space until the user removes them.

## Alternatives considered

- Modification times or Git revision miss same-timestamp changes, dirty bytes,
  external inputs and cooked output changes.
- Running CMake or MSBuild to ask whether work is needed makes selection depend
  on developer processes and can change the previous attempt.
- Persisting multiple scenes or relocating outputs adds an eviction or relocation
  contract. A single receipt covers the requested restart workflow.
