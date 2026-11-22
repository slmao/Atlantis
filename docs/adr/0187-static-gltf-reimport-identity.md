# ADR-0187 — Stable Identity for Managed Static glTF Reimport

- **Status:** Accepted
- **Date:** 2026-11-22
- **Deciders:** Atlantis maintainers
- **Acceptance:** Maintainer accepted `69d35d2f` on 2026-11-22 in [PR #441](https://github.com/slmao/Atlantis/pull/441#issuecomment-7077272130)
- **Related Spec:** [Spec 0139](../specs/0139-static-gltf-reimport.md)

## Context

The [0130 importer](0179-static-gltf-group-projection-and-instantiation.md)
emits `mesh/<mesh-index>/<primitive-index>` and `material/<material-index>`
identities. Array positions describe one export. Mesh and material names can
be missing or duplicated, and the cooked catalog loses the author-supplied
matching evidence. The existing managed glTF registration stores the inbox
copy and root GUID. Named generations freeze cooked products and generated
scene sources, without a seal of the original glTF suitable for this review.

## Decision

### D1 — Explicitly keyed producer

Recognize `asset.extras.atlantisReimportIdentity` equal to JSON integer `1`
in the trusted managed static-group import only. Require an exact string
`extras.atlantisId` on each reachable mesh, primitive and explicit material.
IDs are 1–32 ASCII characters, start with a lowercase letter, and admit
lowercase letters, digits, `_` and `-`. Each mesh ID and material ID is unique
within its own array. Primitive IDs are unique within each mesh. Reject
duplicate reserved members, wrong JSON types, missing IDs and unknown opt-in
versions. Input aliases, case conversion and index-derived defaults are excluded.
The input producer owns ID continuity. Reusing an ID for a different logical
item violates the producer contract and is indistinguishable from an intended
geometry edit. Display the planned pairs so the user can review them.

Derive keyed products from the original root GUID using
`mesh-key/<mesh-id>/<primitive-id>` and `material-key/<material-id>`.
Carry that same stable key through metadata, relative artifact paths, asset
lookup and scene generation. Keep the existing `scene` and `fallback/white`
identities. The node tree still follows the importer. Reordering source
nodes or adding source parts therefore affects future Add Group operations.
Existing instantiated nodes are ordinary authoring copies by ADR-0179.

Match by exact typed keys and compare the old and candidate assignments for
every matched part. The unassigned state is explicit. Missing, duplicated or
different assignments block the whole replacement. The V1 closure is
additive. Every old keyed part and material must survive. Added parts may
refer to preserved or new materials, provided all assets and edges validate
under the existing group limits. Recipe changes for a preserved material
block V1. This avoids rewriting a material already in use or needing a
project-wide reference proof in order to publish a removal.

### D2 — Seal input and preserve the checkout build boundary

Store the exact source bytes and a versioned, canonical keyed registration
envelope in the existing `registered/managed_<name>` unit. The local
envelope records identity mode, registered source SHA-256 and the single
ordinary catalog entry. Use a distinct `atlantis_managed_gltf_registration_version: 1`
header in the unit's `catalog-source.txt`. This header and envelope are
consumed by managed configure and the host. The generated effective
catalog-source remains v1. An older host or CMake reader rejects the keyed
unit at its existing fragment check. Open older frozen generations with a
compatible profile for read-only use. A fresh current-toolchain Saved Scenes
build validates the committed envelope and uses the same keyed derivation.

Named generation v10 carries the existing fields and adds typed keyed-glTF
source evidence. For every keyed producer include its root, exact fragment
and source SHA-256, and a relative path to the bounded frozen source and
registration envelope. Activation checks both files, reconstructs the typed
derived identities and verifies the producer/catalog agreement. Use v10 when
there are keyed glTF producers. Keep the current activation semantics of
versions 1–9 and the existing cooked schemas. Old Runtime builds reject v10
activation. Previous positional registrations, including sources whose extra
fields were ignored by older tools, never gain keyed identity implicitly.

### D3 — Single review and guarded replacement

Add shared `assets.staticGroupReimportReview` with closed Begin by scene GUID,
Read by review ID and Cancel by review ID variants. Keep one review per
managed workspace in the existing worker, expiring ten minutes after result
publication, on Runtime exit or at a conflicting workspace operation. Bound
requests and complete reports to the existing static-group wire budget (64
KiB). Return Pending, Ready, Blocked, Failed or Expired. Ready includes the
one-use ID and complete comparison. Keep the review in session-owned staging
and retain no durable registration edits. A Cancel that wins admission
invalidates the ID. A losing Cancel reports that commit was already admitted.

Add shared `assets.commitStaticGroupReimport` by review ID. The host enforces
the existing profile authority and serializes admission. The worker takes
the existing exclusions and checks the reviewed project, producer, catalog,
generation, canonical registered unit and candidate byte digest at admission
and before the first durable write. Cook only from the frozen review copy.
An inbox edit made after the final recheck can affect a later review, while
this job still cooks the reviewed bytes. A one-use ID belongs to the current
Runtime and can be transferred between clients using that same host. It
does not carry permission to another profile. Reuse `assets.rebuildStatus`
and the existing job receipt for commit. Older clients retain ordinary
catalog and job behavior. New clients surface Unsupported on older hosts.

Use the [0116 replacement sequence](0167-managed-texture-source-replacement.md)
for registered unit backup, recovery journal, verified exchange and no-replace
generation publication. Configure stages exactly one replacement and keeps
the checked-in build and other producers unchanged. Before source exchange
and before publication, validate the full producer inventory header, exact
reviewed unit and new seal, exact catalog delta, scene, imported source,
artifact/sidecar bytes and dependency closure. Compare unrelated records,
edges and frozen bytes as well. Recheck inputs and the deadline at the first
write. If the publish response is uncertain, run activation validation on
the intended final directory and verify the committed unit before success.
Verified absence permits guarded rollback. Uncertainty or unverified cleanup
retains repair evidence and blocks managed writes. Previously complete
generations remain independently usable.

## Consequences

- Reordering arrays cannot redirect an existing GUID to another key. The
  source contract and the same build derivation cover Editor restart and cold
  Saved Scenes builds.
- Producers must preserve IDs at export. Legacy groups need a future
  migration with its own evidence. Blocking removals and assignment changes
  limits V1 to a subset of ordinary model edits.
- Named generations carry source evidence for keyed groups. The registry
  envelope deliberately prevents older managed writers from recooking keyed
  units. The shared review is structural and has no new GPU resource cost.

## Alternatives considered

- **Index continuity.** Unsafe when the exporter reorders arrays, even
  when the resulting catalog has the same count and positional GUID set.
- **Names or geometry hashes as identity.** Names are ambiguous across
  ordinary exports. Exact geometry hashes change during the intended edit.
  Similarity matching cannot prove whether two neighboring parts swapped.
- **Blind positional-to-keyed migration or manual retargeting.** Neither can
  recover absent original identity evidence automatically. A reviewed
  migration or scene/reference replacement belongs in a separate slice.
