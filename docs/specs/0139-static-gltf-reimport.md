# Spec 0139 — Preview and Reimport a Registered Static glTF

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan:** [Plan 0139](../plans/0139-static-gltf-reimport.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0187](../adr/0187-static-gltf-reimport-identity.md) (Proposed)
- **Design baseline:** `2ea0cd9b`, maintainer merge of [PR #440](https://github.com/slmao/Atlantis/pull/440)

## Summary

After updating a registered Bistro storefront glTF at its original inbox path,
review its submesh and material-slot changes in Assets before publishing a
replacement. Stable part and material IDs let the updated geometry reach
existing scene references after explicit activation. Missing or ambiguous
matches keep the last published version available.

## Motivation and scope

[0086](0086-new-asset-import.md) and [0130](0130-static-gltf-group-instantiation.md)
already register a managed source, publish its cooked dependency closure and
let the Editor add the group to a hand-authored scene. The existing importer
derives mesh and material GUIDs from array indices. Reusing that derivation on
a different ordering could send an existing reference to a different part.
An instantiated group stores ordinary nodes with explicit asset references.
[0138](0138-viewport-material-slot.md) can replace one of those material
bindings independently of the imported group.

V1 covers one [0130](0130-static-gltf-group-instantiation.md) managed,
self-contained static glTF with identity opted in at registration and active
from a verified named generation. It supports geometry updates, array reorder
and additions. Existing parts and materials must all survive with their
original source slot assignments. The preview reports removals, missing slots
and ambiguities and blocks those candidates. Existing unkeyed registrations
remain usable through their original flows. Their migration, removal of even
apparently unused outputs, manual mapping, automatic scene synchronization,
batch reimport and a GPU preview of staged geometry are future work.

## Requirements

### R1 — Establish identity at registration

Opt-in [ADR-0187](../adr/0187-static-gltf-reimport-identity.md) keys are the
author's contract for mesh, primitive slot and explicit material identity. An
exporter must preserve each key on the same logical item across revisions.
Registration validates the keys as well as the existing 0130 input shape and
limits, including the 16 MiB single embedded-buffer source, bounded geometry,
untextured opaque PBR and at most 63 emitted nodes. The existing import for
unkeyed sources retains its identity and accepted inputs. A keyed registration
produced by an older positional importer is still a positional registration.

For reimport choose a visible registered group in Assets. The host derives
`inbox/<name>.gltf` from the verified active producer and identifies its root
GUID, cooked scene GUID, original registration fragment and frozen source.
It admits only an authority-backed managed profile whose committed inputs and
active generation agree. Caller-supplied paths, bytes and reassignment maps
are outside the request. Reuse existing bounded reads and ordinary-file,
path containment and workspace exclusion checks.

### R2 — Preview and confirm the same candidate

The host compares the active frozen glTF with a frozen copy of the current
inbox input. Show matched and new submeshes by stable mesh/slot pair, removed
pairs, conflicts, explicit material and no-material slot assignments, and new
or removed material identities. Show the previous and candidate labels and
source positions as context. Each matched row identifies which published mesh
and material GUIDs would remain in use. Mark invalid input, missing or
duplicate keys, missing or changed existing assignments, removed materials or
parts, and changes to existing material recipes as blocking. Comparing an
existing material recipe uses the importer's normalized supported material
values, so JSON formatting, labels and array positions do not affect it.
Keep complete counts and identify offending rows when the input permits an
honest comparison. A parse failure gets a diagnostic rather than inferred
matches. The preview describes import changes without claiming project-wide
scene or prefab coverage.

Host computation uses the existing bounded asset worker. Show progress,
the exact reviewed target and blocking reasons. Confirm only a complete,
eligible review with a current one-use ID. Bind the review to the Runtime,
profile, named generation, original registration, reviewed inventory and
exact candidate bytes. Repeat observation of project state, deadlines and
inbox bytes before accepting Confirm and at the last precommit boundary.
Changed evidence, including a same-timestamp rewrite, requires a new review.
Cancel before Confirm retires the review without changing registrations,
published content, the attached Runtime or scene state. Cancellation after
admission belongs to job settlement. Editor drops late responses on selection,
catalog, dialog or connection changes. It keeps the existing selection and
preview while the review is open.

### R3 — Replace one registered producer and publish explicitly

Reuse the importer, staged catalog build and named-generation publisher.
Retain the root GUID, scene GUID, original catalog key and original inbox
location. Preserve the identity and type of every existing part and material,
their assignment edges and any generated fallback they use. Only this
producer may change. New keys may add new derived assets and expected group
dependency edges. Existing scene copies continue to use their assigned mesh
and material GUIDs, including Editor-confirmed overrides. New parts become
available to subsequent Add Group operations. This job performs zero
authoring writes, changes no placed node transforms or history and requires no
scene Apply. Existing copies do not acquire added nodes.

Verify the exact candidate, registration fragment and staged dependency
closure in the prospective generation using the normal activation rules
before touching the registered unit. Publish using the guarded replacement
and recovery sequence in [ADR-0187](../adr/0187-static-gltf-reimport-identity.md).
Show the verified job outcome and distinguish Published from Active. Use the
existing explicit restart into the named generation. A fresh authorized
Saved Scenes build from the committed inputs must resolve the same keys.

### R4 — Leave a usable version and settle failures

Validation, cook or publish failure preserves the last complete generation.
Confirmed precommit failure retires private staging. Roll back a started
source exchange only when publication is ruled out and the old unit and
backup are verified. When the outcome is unknown, retain the journal and
write hold, keep the previous complete generation usable read-only and
give inspection guidance. Prove the intended final directory and committed
unit before reporting success. Query a known job ID via the existing status
flow. No automatic retry, Register, Resume or activation. After source
correction and verified cleanup, the operator explicitly starts a new
preview.

## Proposed design and architectural impact

[ADR-0187](../adr/0187-static-gltf-reimport-identity.md) proposes an opt-in
importer identity mode, frozen generation v10 evidence and two additive shared
operations for review and confirmation. Editor, CLI, native/C# SDKs and MCP
consume the same service. The existing rebuild status surface settles an
admitted job. CPU-only work uses the existing host and worker boundaries.
Renderer, RenderGraph, scene source format and ordinary frame work keep their
existing contracts.

## Alternatives considered

See [ADR-0187](../adr/0187-static-gltf-reimport-identity.md#alternatives-considered)
for the index, name and geometry-based alternatives and the legacy trade-off.

## Testing and verification

[Plan 0139](../plans/0139-static-gltf-reimport.md#verification-checklist)
maps the matching, failure, compatibility and Windows 100% storefront
checks. Joint review sets the V1 boundary for legacy sources, removed parts
and existing material assignments before any implementation.
