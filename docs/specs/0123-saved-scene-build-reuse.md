# Spec 0123 — Reuse the Last Saved Scene Build

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0123](../plans/0123-saved-scene-build-reuse.md)
- **Approval:** Jointly approved at `022bc4d5` in [PR #411](https://github.com/slmao/Atlantis/pull/411). See Plan 0123.
- **Related ADR:** [ADR-0172](../adr/0172-saved-scene-build-receipts.md) (Accepted)

## Summary

An ordinary Windows development Editor can reopen the last successfully built
Saved Scene after a launcher restart. The user selects the same unit and
explicitly chooses Open once the Editor verifies that the build still matches
the local inputs and its Runtime files are intact.

## Motivation

[Spec 0090](0090-saved-scene-build-open.md) already discovers saved units,
configures and builds a fresh `atlantis_runtime` attempt, resolves the Debug
executable through the CMake File API, and checks the two saved-unit files and
executable before launching a separate Editor. `SavedSceneAttempts` keeps the
result in memory. It cannot prove freshness after a restart, and the existing
Open check does not cover an edited committed asset, tool configuration, or a
missing cooked file.

## Goals and non-goals

Reuse one build for one selected unit in the same trusted checkout and local
Windows user profile. Explain any refusal and keep Build explicit. Keep 0090's
Visual Studio 18 2026, x64, Debug recipe and child ownership. Cross-scene
caching, automatic build or launch, output cleanup, relocation, toolchain
presets, general incremental build caching, cross-launcher child discovery and
asset generation activation are out of scope.

## Requirements

### R1 — A single recorded build

After a successful explicit Build, record a reusable result only if the
required evidence in R2 is complete. Keep one slot per Windows user and
canonical checkout. A launcher restart, discovery, selection and Refresh
never configure, build or start a child. The user explicitly selects the
unit and supplies the same managed-root setting, if any. A receipt neither
supplies that setting nor authorizes a workspace, executable or launch.

A newly admitted Build for any unit supersedes the slot before running
CMake. If it fails or is interrupted, an earlier result never returns as a
fallback. Selecting other units alone leaves the slot intact. Retain the
original attempt and logs at their recorded absolute paths.

### R2 — Evidence of current inputs and usable outputs

Compare exact file bytes and canonical identities, including the presence
and membership of relevant input sets. The supported profile must establish
coverage for all of the following before it can produce a reusable result.

| Category | Evidence |
| --- | --- |
| Saved scene | Canonical `save_<name>` unit and both `scene.scene.txt` and `catalog-source.txt`. |
| Asset inputs | Checkout catalog, declared offline asset source trees and external glTF buffers/images, optional Bistro inputs and overlay (including absent/present selection), and the selected managed root's committed registration set and its input files. Include source/recipe, registration metadata and texture seals where consumed. A named live Runtime generation and an uncommitted inbox replacement do not enter this build. |
| Build configuration | Canonical checkout, configured CMake and resolved VS/MSVC/Windows SDK/Vulkan SDK selections, fixed configure/build arguments, effective CMake configuration and build-affecting environment, build scripts/source/shaders, and pinned third-party inputs. Establish coverage of configure files, build-consumed toolchain files and dependency inputs for the supported layout. A changed recorded setting, contents or resolution is stale even if the file names match. |
| Runtime files | Resolve the target again from the attempt's Debug CMake File API reply. Match the recorded executable and the launch-required cooked catalog, asset files and shader outputs from that attempt, including the effective authoring catalog. Missing files or changed bytes invalidate reuse even when the executable still matches. |

Prefer conservative directory inventories where the build owns the input or
output root and hashing extra files is acceptable. CMake-owned resolution
identifies inputs reachable outside those roots. Refuse reuse if a consumed
dependency falls outside covered paths, an input or output is unreadable or
unsafe, or the effective configuration cannot be established. Timestamps,
Git status, directory names, existing CMake stamps, or a catalog alone are
insufficient. Hash off the UI thread. Validation runs no configure, build,
cook, or Runtime process.

For the default toolchain profile, compare the current tool resolution as
well as the recorded paths and contents. Include changes in eligible
installations or search settings that could select a different compiler, SDK
or shader tool. A prior CMake cache proves what the original build selected.
It does not establish what the same command would select today. Custom
toolchain, package or MSBuild overrides are eligible only when their actual
inputs can be covered by this profile.

Compare the available source and configuration evidence from admission
through completion, including a check before the build command after
configure has resolved its inputs. A pinned dependency freshly populated by
CMake may enter the evidence at configure settlement when CMake validated its
approved digest and the consumed bytes are covered through build completion.
If a checked input changes during the attempt, the result is stale. In the
absence of a verifiable durable receipt, an explicitly fresh, otherwise
successful 0090 build can still be opened in the current launcher under the
existing 0090 checks. Report that limitation. A detected mismatch never
falls through to that path for the stale result.

### R3 — Explain eligibility and recheck at Open

When a unit is selected, show Checking and keep Open disabled until its
result settles. Show Ready to open for verified evidence and identify the
build/log. For a missing or superseded receipt, changed inputs, changed or
missing outputs, or an inconclusive check, keep Open disabled and show the
reason and the explicit Build action. A new selection, root, Refresh or Build
invalidates older asynchronous feedback. Build remains available during a
selection check. Bind the result to the checked selection and receipt.

Before starting the independent child, reserve the selected unit as in
0090 and repeat eligibility checks including the current recorded build.
Reject a late mismatch without starting a process. A process-start failure
can be retried only through another explicit, guarded Open. Retain the
original Editor's document, draft, history and Runtime. The child owns its
Runtime and reports startup or document readiness through the existing UI,
including the 0122 recovery flow when applicable. The checks do not lock
external source writers through process start.

## Architectural impact

[ADR-0172](../adr/0172-saved-scene-build-receipts.md) supplements ADR-0150's
session-local result with a private durable receipt and assigns ownership of
freshness evidence. The saved slot recipe, asset/scene formats, Runtime,
shared clients and dependencies stay on their existing boundaries.

## Alternatives and verification

See ADR-0172 for timestamps, process-based checks, and multi-scene caches.
[Plan 0123](../plans/0123-saved-scene-build-reuse.md#verification-checklist)
covers Windows 100% Open after a launcher restart with zero additional CMake
invocations, specific invalidation cases, and preservation of 0090 behavior.
