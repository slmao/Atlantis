# Spec: Shared Asset Reference Target Types

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-07
- **Related Plan:** [Plan 0084](../plans/0084-asset-reference-target-types.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0145](../adr/0145-authoring-asset-reference-target-types.md) (Proposed)

## Summary

Publish a target cooked asset type on eligible fields in the authoring schema.
The Inspector chooser uses that declaration to offer candidates of the right
catalog type. CLI, SDKs and MCP can inspect the same declaration, and Runtime
apply still decides whether the complete scene can load.

## Motivation / Problem Statement

[Spec 0082](0082-generic-asset-reference-picker.md) identifies asset references
using schema shape and flags. Its chooser lets users filter the cooked catalog
manually, so it can offer a mesh for a material slot. Such a GUID can enter the
document and fail at reload, as demonstrated by
[Spec 0083's implementation](https://github.com/slmao/Atlantis/pull/330).
The expected type is a property of the field, independent of the current
GUID, project contents and Editor App. The schema is the place to publish it.

## Goals

- Declare the expected type once for each covered field and expose it through
  the shared authoring schema.
- Use the declaration to constrain the Inspector's existing chooser.
- Keep saved and in-flight authoring state compatible with existing clients and
  retain the normal edit, history and recovery paths.

## Non-Goals

Full asset combination compatibility, artifact or dependency validation in the
chooser, authoring transaction enforcement by catalog, type-aware text input,
asset preview and asset-pipeline management. This slice adds no catalog query,
schema negotiation, Runtime change, or annotation of World, cooked data or
non-authoring descriptors.

## Requirements

### Functional

**R1 — Authoritative declaration.** The Asset System owns target-type
annotations on the semantic scene field descriptors it already publishes
through Authoring. Use the declaring type and field identity, including the leaf
descriptor for a nested field. Only a primitive `AssetGuid` with
`AssetReference` may carry this optional declaration. The first release
annotates exactly `asset_system::scene::Renderable.meshAsset` as `mesh` and
`asset_system::scene::Renderable.materialAsset` as `material`. The material
field remains optional. Match these declarations to the existing Runtime
dependency expectations and verify that agreement in tests. Editor App, CLI,
SDK and MCP must derive metadata from the published descriptor.

**R2 — Shared contract and compatibility.** Append an optional
`targetAssetType` member to the existing authoring schema field JSON object
on `atlantis.remote/1`. A declaration uses the exact cooked catalog token
`mesh`, `texture`, `material`, `scene` or `environment`. Omit the member
when unspecified. Absence means the server supplies no target constraint.
Reject a present null, wrong kind, empty, unknown or noncanonical token, and
a declaration on an ineligible field as malformed schema through the
existing typed-client error path. Older readers continue ignoring the extra
member. New readers accept the old shape. On an old Runtime that already
supports 0083, this allows the 0082 generic chooser. Earlier Runtime readiness
still follows 0083.

Expose an optional canonical token on native reflective
`schema::FieldDescriptor` (empty view means absent and normal schema lifetime
rules apply) and an optional existing catalog `AssetType` on C# `SchemaField`
without changing its positional constructor. Native SDK callers read it from
their existing `AuthoringConnection::schema()`. Add `scene schema <Type>`
to the CLI for authoring descriptor inspection, with existing qualified and
short name resolution and the token in text and the same optional key in
JSON. Inspection is available before document open. MCP's existing
`authoring.schema` tool forwards the member. Keep World schema responses and
CLI World inspection unchanged.
There is no new RPC or MCP tool. Keep field IDs, flags, value layouts and
type versions stable. This advisory member is outside the persisted semantic
scene hash. Preserve old cooked scene readability and World binding output.

**R3 — Inspector candidate filtering.** Keep the eligibility and current
value read checks from 0082. For an annotated target, the chooser shows the
required type and lists only records with that exact type in the existing
complete `assets.list` snapshot. The restriction is fixed for that chooser.
Search narrows the list, and Reload view uses the same Runtime snapshot.
An empty matching list remains a successful result distinct from an empty
catalog, a search miss, loading and a catalog or transport error. With absent
metadata, retain the generic chooser and label the type as unspecified.

Display the field's original value and distinguish absence, a GUID outside the
snapshot and a listed GUID of another type. Preselect only an eligible,
visible current record. Leave the field value and the choice of any
replacement to the user. A dependency may
become a candidate only if it satisfies the same typed list and visibility
rules. Keep out-of-type dependency GUIDs readable in details. Detail responses
cannot introduce a candidate or reenable Confirm. Retain the existing
list/detail request guards and catalog error semantics.

**R4 — Confirmation and recovery.** An annotated choice must pass the
visible-list check in the view and the worker's current typed membership check.
Use the complete list result already obtained by the chooser on the worker
and bind its eligible GUIDs to the active chooser and latest list request.
Clear that membership when loading, on failure and on invalidation. Reuse that
membership at confirmation without another catalog RPC. Recheck the field
annotation and existing target guards in the queued
Edit operation before submitting. A stale or wrong-type choice yields a refusal
with no transaction, history or apply. A valid changed choice uses the existing
SetProperty, history and apply flow. Unchanged choices, optional Clear, manual
GUID editing, Save and Reopen follow their existing rules.

Type matching compares the declared field type and the catalog record type.
It says nothing about the selected mesh/material pairing, texture color
space, dependencies, artifact availability or Runtime projection. Show this
limit in the chooser. A type-matching selection can still fail at apply.
Follow 0083 on failure and recovery, including retained document/history,
submit-only edits and history replay while suspended, explicit Retry, and
receipt/projection guards. The Assets panel retains its independent filters
and selection.

### Non-functional

**R5 — Scope and delivery.** Keep the existing authoring schema and catalog
fetch lifetimes, worker ownership, Remote transport limits, packages and
platforms. Client filtering uses the complete existing list and performs no
per-row or per-keystroke queries. The authoring validation rules, scene and
catalog formats, asset resolution, Runtime and rendering are outside this
slice. Run the applicable checks in Plan 0084.

## Proposed Design

Asset System annotates the descriptors and Core carries the optional token.
Authoring exposes the same descriptors through its current interface.
Remote codecs and clients carry the optional metadata alongside the existing
field attributes. Inspector captures the declaration with the target and
uses typed catalog membership to gate selection and submission.

## Architectural Impact

The native reflective descriptor, C# schema value and shared wire schema gain
an optional advisory member. CLI gains a read-only command over the existing
authoring connection. [ADR-0145](../adr/0145-authoring-asset-reference-target-types.md)
records the authority, encoding, versioning and validation boundary. Native
consumers rebuild with the descriptor change. There is no module dependency
edge between Core and Asset System or Connection.

## Alternatives Considered

ADR-0145 compares client name heuristics, an independent target registry and
enforcing catalog membership in authoring writes.

## Testing & Verification Plan

[Plan 0084](../plans/0084-asset-reference-target-types.md#verification-checklist)
maps R1–R5 to descriptor, wire, compatibility, editor and recovery checks.

## Risks & Open Questions

The published field type must track Runtime dependency expectations. Verify
both declared slots against existing resolution behavior and require human
agreement on the authority, compatibility and chooser policy before coding.

## Out of Scope / Future Work

Union or dynamic target types, dependency compatibility checks, previews,
catalog refresh, broader authoring schemas and asset-pipeline management.
