# ADR 0179 — Managed Static Group Projection and Instantiation

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Atlantis maintainer
- **Acceptance:** J1–J4 approved for [873f1271](https://github.com/slmao/Atlantis/pull/423#issuecomment-7062867632) after joint review. The maintainer merged the design in #423.
- **Related Spec:** [Spec 0130](../specs/0130-static-gltf-group-instantiation.md)

## Context

Managed registration already publishes stable derived scene, mesh and
material assets, while the admitted source shape is a single renderable.
The cooked scene already contains parent order, local TRS and renderable
AssetIds. The shared catalog lists dependency identities without the
relationships required to reconstruct a group. The destination authoring
document can already commit an ordered structural batch at an expected
revision under [ADR-0164](0164-guarded-duplicate-transactions.md).

## Decision

1. Extend the managed import validator with the bounded group shape in
   Spec 0130. Keep the original single-mesh admission, registration
   fragment, identity derivation and publication format. The existing
   build and Resume paths apply the same validator. A pre-feature tool
   can still read compatible cooked products and ordinary scene source.
   A checkout that rebuilds or resumes a registered group needs the new
   managed importer. Existing artifacts, profiles and clients retain
   their current contracts.

2. Add a read-only `assets.staticGroup` query using one nonzero cooked Scene
   GUID. Runtime resolves it in the same catalog snapshot as `assets.get`.
   Recognize the managed producer's reserved structured source key
   `content:managed_<name>/source.gltf#scene` and require the current
   scene-cooker record. At import, check the key and derived Scene GUID
   against its registered root before publication. At read time use only
   the selected build or generation's catalog and its resolved artifact
   and metadata pair. Read within the catalog root, reject unsafe paths
   and overlarge scene/metadata inputs, then use the existing decoder.
   Bound the combined file bytes to 64 KiB for this query. Check the
   emitted record count and shape even on reads from older catalogs.
   Map AssetIds back to typed mesh/material GUIDs using that same catalog;
   require the exact de-duplicated direct reference set to match the Scene
   record, and each direct asset to belong to the same managed producer.
   Keep the current catalog activation lifecycle. Publishing a generation
   does not replace a running snapshot. Neither mutable inbox bytes nor an
   authoring session's generated text supplies the query.

3. Return a closed bounded value with Scene GUID, the validated managed
   name and the complete ordered nodes. Parent is a nullable zero-based
   index in the same array. TRS uses finite engine-space Float32
   position, Euler radians and scale. Renderable is either absent or a
   mesh GUID plus an explicitly nullable material GUID. Preserve an
   absent material so the normal scene fallback applies. Cooked order
   and local values are authoritative. The cooked artifact does not carry
   authoring labels; this query does not reopen text to add them. Fail
   without a partial response. Distinguish catalog unavailable, unknown
   asset, unsupported group and invalid group data. Bound the encoded
   response to 64 KiB, within the unchanged Remote envelope. No disk
   path, GPU data, capture, asset edit or document operation is exposed.

4. Add one matching read to Connection, Remote, native and C# SDKs, CLI
   and the existing curated MCP asset tools. Legacy clients can ignore
   the addition. New clients treat an unsupported endpoint or old host
   unknown-method response as unavailable and never infer a group from
   a dependency list. Keep `assets.list/get`, import DTOs and status
   replies unchanged. Remote transports the same typed description.
   Compose the existing `authoring.submitTransaction`, revision guard
   and failure drain for clients that choose to add it. Editor history
   remains local. The query never grants permission to write and the
   hand-authored document must positively advertise the existing guard.

5. Add one ordinary transform root and the complete decoded group under
   it. Use the existing closed append/removal shapes and the 64-node
   budget under ADR-0164, including the root. All scene identity, Apply,
   Undo/Redo conflicts and Save semantics stay with the existing hosts.
   Scene source contains ordinary nodes and mesh/material references.
   It has no persistent link to the imported Scene asset.

## Consequences

- The existing cook and publication path remains the sole source of
  model assets. Clients need one bounded read and an existing transaction.
- A saved group can be reopened by existing scene clients given the
  same managed assets. Rebuilding the widened managed source requires
  a compatible importer. Existing world and renderer boundaries stay
  intact.
- This slice omits source node labels and linked updates. The root's name
  and the ordered child hierarchy remain available for editing.

## Alternatives considered

- Reading glTF or generated scene text in clients duplicates parsers and
  couples clients to host paths. A shared cooked projection covers the
  required values using the selected catalog.
- Returning the full cooked scene would include irrelevant component
  types and unbounded payloads. The bounded value expresses only a group
  that fits the existing guarded transaction.
- A dedicated instantiate command or a prefab reference adds write or
  persistence semantics. Existing guarded append and ordinary authored
  nodes cover this first release.
