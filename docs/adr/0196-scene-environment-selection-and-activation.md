# ADR 0196: Scene-owned environment choice and Runtime activation

- **Status:** Accepted
- **Date:** 2026-11-28
- **Deciders:** Maintainer
- **Acceptance:** [Maintainer approval](https://github.com/slmao/Atlantis/pull/463#issuecomment-7236980882), 2026-11-28.
- **Related Spec:** [Spec 0150](../specs/0150-scene-environment.md)

## Context

The existing cooked Environment is one package of radiance, irradiance and
DFG data. The launch configuration currently owns its selection. Presence at
startup controls shader/pipeline preparation, while the live reload path
currently replaces World and scene dependencies and leaves environment bindings
alone. A saved choice has to reach the normal cooker, the independent launch and
the live projection with the same meaning. The existing formats can express a
camera or a Renderable binding and have no scene environment field.

## Decision

**D1 — Store an optional scene-level GUID.** Persist explicit selection in
strict source v11 as `environment: <asset-guid>` directly after the active
camera header, with a non-nil canonical GUID and all prior v10 node semantics
retained. Absence uses the existing Inherit encoding: unchanged sources remain
readable and serialization continues to choose the established 8/9/10 form
when there is no explicit selection. Carry the same GUID in a version-8
scene artifact with semantic version 3 and a required GUID header extension.
Keep existing v7/semantic2 emission for Inherit and read both version pairs.
Old cookers and Runtime reject explicit new formats. An older Runtime can
continue using its existing cooked catalog. A catalog containing the new
dependency edge may be rejected by its older validator. The cooker checks
declared type against the catalog source and writes a direct Scene → Environment
catalog dependency. Catalog validation admits this edge; for an explicit
cold scene the cooked scene GUID reference and its sole Environment edge must
agree. Asset System holds CPU data and format validation. World and node
component schemas carry no environment field.

**D2 — Keep edits and readbacks shared.** Add one capability-gated
`SetSceneEnvironment` authoring transaction command containing the exact
`DocumentToken` and either Inherit or a non-nil asset GUID. It is a lone
command in its transaction and changes the document revision once. Identical
values settle as no-ops. Native host admission uses its active cooked catalog
snapshot to validate that an explicit candidate is an eligible Environment,
including for Undo/Redo. Authoring follows the existing value-only host
lookup boundary. The Editor uses `assets.list/get`, scopes its choices to
the originating Runtime connection/snapshot and rechecks the selection before
sending. Server-side validation is authoritative. Advertise support and add
the authored value and the settled effective kind/identity in optional shared
document/control readbacks. Absence of capability means unsupported; present
Inherit is a real authored value. Distinguish no environment and configured
startup-only resources without catalog identity. A settled Apply keeps its existing outer failure category. New clients read
an optional typed environment detail (requested choice and preparation reason,
including Build/Open required). Older clients still decode an ordinary Apply
failure. Use the existing projection generation and Apply receipt for correlation. Old clients continue reading
ordinary forms and editing unrelated fields through the newer host, which
preserves the environment on save. New clients use the capability before
sending and keep the old view read-only on incapable hosts. Carry the same
contract across the native control/Authoring clients, Remote, CLI, C# SDK
and MCP.

**D3 — Resolve explicit choice first.** Match the saved GUID to the active
catalog's Environment record and check the supported cooked schema, artifact
path containment, sidecar GUID/asset identity, size and payload shape using
the existing environment cooking/decoding rules. Check the record against the
pair actually read. Studio and Warehouse already use those rules. Cold launch
resolves from the catalog chosen by the existing launch or named-generation
configuration before deciding which existing shader family to load. Inherit
follows the existing immutable session launch default (possibly none).
The live operation uses the same active catalog snapshot used for asset
queries; source edits or a later on-disk catalog do not become active
implicitly. Windows Saved Scenes uses its coherent cooked catalog and existing
build receipt checks. Include both environment producers and their output pairs
in the build recipe, and require the selected pair if the slot uses a named
generation. Android retains its current packaged scene and Studio startup
path in V1. Explicit missing, invalid or incompatible assets are failures
at the appropriate build/startup/Apply boundary. Log the requested GUID,
phase and reason and expose a bounded shared failure usable by each client.
A committed edit and a saved scene stay distinct from its successful
Runtime activation.

**D4 — Publish only complete environment sets.** Keep a live IBL-to-IBL
change on the normal Runtime reload path. Prepare a single candidate using the
existing environment realization and RenderGraph transfer recording with the
existing offscreen submission and GPU completion mechanisms. Upload and
validate the whole set before publishing it with the baked scene at the
reload boundary. Existing IBL shaders and material pipeline families remain
valid. The sky/reflection cube and sampler, irradiance coefficients and DFG
bindings always come from the same settled set on both the viewport and
normal capture paths. Retain the previous set and its descriptors until the
GPU finishes work using them. Keep candidate buffers alive until upload
completion is proved; pending or failed waits use existing bounded retention
and fatal-device handling, with no successful Apply receipt. A second
candidate cannot accumulate behind unsettled GPU work. Defer or refuse a
reload that would retire borrowed resources from an incompletely settled
capture, using the existing capture settlement rules. No periodic reads,
shader changes, live IBL family crossing or GPU submission outside
RenderGraph. For a crossing between IBL and none, report a typed
Build/Open-required Apply failure with the old projection intact. Explicit
cold startup handles that transition. Reload reuses resources only when the
resolved effective identity and artifact pair are the same. A display label
alone cannot establish equivalence between a startup default and an explicit
catalog record.

## Consequences

The saved file and its normal cooked output carry the choice in any supported
launch. The visible scope is the scene projection shared by running clients.
Selection changes use bounded preparation and at most one staged set, plus the
current environment while it can still be referenced. There is one additional
scene source/artifact version and a narrow additive client contract. Switching
from or to a no-environment startup default requires the user to save and
independently Build/Open. Existing [camera looks](../specs/0145-render-look-presets.md),
[bookmarks](../specs/0147-scene-view-bookmarks.md) and standalone asset preview
retain their ownership.

## Alternatives considered

- Keep the override in a launcher setting or local sidecar. Independent launch
  and normal scene dependency validation would then require editor-specific
  state.
- Default to Studio if explicit resolution fails. That would make a successful
  start ambiguous and could silently change the saved look.
- Change to three independent packages or general live IBL enable/disable.
  Neither is needed to select the two existing environments. The latter would
  also require an atomic material/shader family rebuild during Apply.

This supplements the launch-default behavior described in
[Spec 0032](../specs/0032-runtime-sample-scene-selection.md) and the reload
boundary in [ADR-0131](0131-authoring-apply-and-runtime-scene-reload.md).
