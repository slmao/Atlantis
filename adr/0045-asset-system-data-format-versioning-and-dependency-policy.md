# ADR 0045: Asset System — Data Format, Versioning, and Third-Party Dependency Policy

- **Status:** Accepted
- **Date:** 2026-08-18 (accepted 2026-08-19 — see Revision History)
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-19 as part of Spec 0012's Human Review Approval; see
  that spec's own Human Review Approval note for the full approval
  record and the directed corrections this ADR carries.
- **Related Spec:** [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md) (`Approved`)
- **Related ADR(s):**
  [ADR-0043](0043-asset-system-module-boundary.md) (module boundary) —
  sibling ADR this content was originally drafted alongside, split out
  2026-08-18 so each permanent decision has exactly one authoritative
  record (see ADR-0043's own Revision History for why).
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)
  (identity, provenance, and import methodology) owns the metadata
  sidecar's own field semantics and the Asset ID's own concrete hash
  algorithm/parameters; this ADR owns the sidecar's wire encoding
  (flat text, versioned, anchored-prefix parsing) and the blanket
  no-new-third-party-dependency policy those algorithm/parameter choices
  operate under — the two are cross-referenced rather than duplicated.

## Revision History

- **2026-08-18 (split from ADR-0043):** Created by splitting the
  data-format, versioning, and third-party-dependency content out of
  ADR-0043's own original combined draft — see Context.
- **2026-08-19 (Human Review corrections, then `Accepted`):** Human
  Review directed two corrections before accepting this ADR. (1) The
  runtime artifact's on-disk byte order is now stated as an
  **unconditional little-endian format contract**, replacing the earlier
  "native/little-endian" phrasing that conflated a property of the
  format with a property of whichever host wrote it. (2) The runtime
  artifact format's own description no longer names
  `atlantis::renderer::createMesh()` or `atlantis::rhi::VertexInputLayout`
  as things Asset System itself touches — it loads into the CPU-side
  `StaticMeshAssetData` boundary
  [ADR-0043](0043-asset-system-module-boundary.md) fixes, and the
  composition root performs the GPU handoff. This ADR then moved to
  `Accepted`.

## Context

This ADR was originally part of ADR-0043's own combined draft
("Module Boundary and Data Format/Dependency"). It was split out
2026-08-18 at Human Review's own explicit request: the module-boundary
question (is Asset System a new top-level module) and the data-format/
dependency question (what do the authoring source, runtime artifact,
and metadata sidecar look like, and is any new third-party dependency
justified) are independently evolvable decisions that a single combined
ADR would make awkward to accept, amend, or supersede separately. This
matches this project's own established precedent of one ADR per
independently-evolvable decision — Spec 0009 alone produced six separate
ADRs (ADR-0032–0037) rather than one combined document.

Asset System's authoring source and runtime artifact both need a
concrete data format. No parsing, hashing, serialization, or
3D-model-interchange dependency exists anywhere in `src/` today —
confirmed by direct inspection (`src/core/include/atlantis/` contains
only `assert.h`, `log.h`, `result.h`; no hash utility of any kind exists
in this repository). [ADR-0006](0006-dependency-management.md) requires
any dependency that does not fit the "small, pinned `FetchContent`
source dependency" model to get its own ADR — this is that ADR for
Asset System's own data formats and any dependency question they raise.

## Decision

### Data formats — hand-rolled, dependency-free

**Both the authoring source format and the runtime artifact format are
new, hand-rolled, dependency-free formats scoped exactly to this Spec's
one supported asset type (a static triangle mesh: per-vertex position
and color, `std::uint16_t` indices) — no third-party parsing, model-
interchange, hashing, or serialization dependency is introduced.**

- **Authoring source format:** a small, human-readable, flat text
  format a human can author and diff in an ordinary PR — a fixed,
  documented `key: value`/list structure sufficient to describe a
  vertex list and an index list, in the same spirit as (not the same
  code as, and not sharing an implementation with)
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own hand-rolled sidecar format. Exact field layout is a Plan-stage
  detail, fixed by Requirements in
  [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md).
- **Runtime artifact format:** a small, versioned binary layout — a
  fixed header (magic bytes, `schema_version`, vertex count, index
  count) followed by raw vertex bytes, followed by raw `std::uint16_t`
  index bytes — loaded into the CPU-side `StaticMeshAssetData` structure
  [ADR-0043](0043-asset-system-module-boundary.md) fixes as Asset
  System's own output type, with no transformation at load time beyond a
  byte-count/header-consistency check. The composition root (a test, an
  example, or eventually a future Runtime) is what subsequently hands
  that CPU data to `atlantis::renderer::createMesh()`; Asset System
  itself never does, and this format therefore names no RHI or Renderer
  type. Not a general-purpose binary serialization format; specific to
  this one asset type's own two flat arrays. **Byte order: the on-disk
  format is unconditionally little-endian.** This is a fixed property of
  the file format itself, not a property inherited from whatever host
  happens to write or read it: every multi-byte field (header integers,
  the Asset ID, vertex floats, index values) is little-endian on disk by
  definition. Both of Atlantis's currently-supported targets (x86-64
  Windows development, and the ARM/AArch64 Android ABI) are natively
  little-endian, so a conforming reader/writer on either needs no byte
  swapping in practice — but that is a convenient coincidence of the
  supported platforms, not the contract. A future big-endian host would
  be required to byte-swap on read and write to conform; it would not be
  permitted to write host-endian bytes and call them valid. No such
  target exists or is planned. This same fixed little-endian rule
  governs the Asset ID's own binary serialization — see
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md) —
  keeping exactly one byte-order rule for this Spec's entire binary
  surface.
- **Metadata sidecar format (wire encoding):** a strict, versioned flat
  text format — anchored-prefix field matching (never a delimiter
  scan), a fixed field order, no embedded-newline support, and an
  unrecognized `schema_version` rejected outright, implemented as new,
  independent parsing code — never a dependency on, or shared
  implementation with,
  `tests/image_regression/support/provenance.*` (test-only code, outside
  any `src/` module's dependency surface). This ADR fixes the sidecar's
  own *wire encoding*; which fields it contains and what they mean is
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)'s
  own "Metadata schema" Decision — the two are deliberately
  cross-referenced rather than restated in both places.
- Both the runtime artifact and metadata sidecar formats carry a
  **mandatory `schema_version` field from their first implementation**,
  satisfying
  [ADR-0034](0034-stable-public-boundary-versus-internal-cpp-layout.md)'s
  own "stable public boundary" requirement — no migration *mechanism* is
  built now (matching ADR-0034's own explicit disclosure that it
  supplies no versioning scheme or migration mechanism itself), only the
  version marker a future migration would need to gate on.

### No new third-party dependency

No `glTF`/`Assimp`/other 3D-interchange parser, no UUID-generation
library, no hashing library, and no JSON/YAML/TOML parser is added by
this ADR. This is a blanket policy covering every hashing need this
Spec's own scope has — the Asset ID's own hash computation
([ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md))
and a future spec's own possible cache-key hash alike — not a decision
scoped to one format or algorithm only. Every format and mechanism this
Spec's own scope needs is achievable with the C++ standard library
alone — see Alternatives Considered for the specific alternatives
weighed and why each is deferred, not adopted, at this Spec's scope.

## Consequences

### Positive

- Zero new third-party dependency means zero new license, build, or
  long-term-maintenance surface to manage — consistent with this
  project's own demonstrated default (ADR-0041's `stb` decision is the
  one exception in this repository's history, justified there by PNG's
  own real external-interchange need; no such need exists for this
  Spec's own internal-only formats).
- A mandatory `schema_version` field from day one avoids the same
  retrofit risk a missing version marker would otherwise create once a
  second asset type or a real compatibility need arises.
- Keeping this decision separate from
  [ADR-0043](0043-asset-system-module-boundary.md) means a future
  amendment to the data-format/dependency decision (e.g. adopting
  `cgltf` once a real multi-mesh workflow exists) does not require
  reopening or re-dating the module-boundary decision, and vice versa.

### Negative / Trade-offs

- Two new hand-rolled data formats (authoring source, runtime artifact)
  plus a third hand-rolled wire encoding (metadata sidecar) are formats
  this project must maintain and document, instead of reusing an
  established, widely-supported interchange format a new contributor
  might already know.
- Maintaining two sibling ADRs (this one and ADR-0043) for what began as
  one Asset System foundation decision is a small, real bookkeeping cost
  — mitigated by each one's own Related ADR(s) field cross-referencing
  the other.

## Alternatives Considered

- **Adopt `glTF`** (via `cgltf`, a small, single-header, MIT-licensed C
  parser, or the larger `tinygltf`/Assimp) **as the authoring source
  format.** Considered in real depth, not dismissed reflexively:
  `cgltf` specifically would be a small, permissively-licensed,
  `FetchContent`-compatible dependency in the same weight class as
  `stb` (ADR-0041's own precedent). Rejected as this Spec's *default*
  regardless: this Spec's own scope is exactly one hand-known, eight-
  vertex cube — glTF's own scene-graph, material, animation, texture,
  and skinning surface (the overwhelming majority of what any glTF
  parser exists to read) has no consumer in this Spec's scope at all.
  Adopting a general interchange-format parser to read eight vertices is
  exactly the "dependency justified by a future, anticipated need rather
  than a present, real one" pattern
  [ADR-0006](0006-dependency-management.md)'s own dependency discipline
  warns against. Revisit once a real multi-mesh, multi-material
  authoring workflow exists to justify it — at that point, `cgltf`'s
  small size, permissive (MIT) license, and single-header/`FetchContent`
  compatibility make it a strong future candidate, not a rejected one.
- **A UUID-generation library** (e.g. `stduuid`, `boost::uuid`) for a
  random-GUID identity scheme. Rejected alongside the identity-scheme
  decision itself (see
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)):
  this Spec's own recommended path-derived identity scheme needs no
  random-number-based UUID generation at all, so the dependency question
  does not arise under that recommendation; it would arise again if
  Human Review instead directs a random-GUID scheme, at which point this
  ADR would need its own revision.
- **A general hashing library** (e.g. `xxHash`, `wyhash`) for any of
  this Spec's own hashing needs — the Asset ID's own hash computation
  ([ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md))
  or a future spec's own content-hash-based cache key. Rejected for this
  Spec's own scope: a hand-rolled, well-known, public-domain-equivalent
  algorithm (FNV-1a, a few lines of C++ — see ADR-0044's own concrete
  choice) is sufficient for non-cryptographic hashing at this Spec's
  one-fixture scale, matching this project's own existing precedent of
  hand-rolling small algorithms (CRC32/Adler32 for
  `tests/image_regression/png_codec_tests.cpp`'s own hand-assembled PNG
  test fixture) rather than reaching for a dependency. Revisit if a
  future spec's real performance/collision-resistance requirements
  outgrow a hand-rolled hash.
- **A JSON/YAML/TOML parser** for the authoring source and/or metadata
  formats. Rejected for the same reason
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  already rejected one for the image-regression sidecar: every field
  this Spec's own formats need is a small, flat, non-nested set of
  scalar values with no legitimate need for a general markup language's
  nesting/escaping/Unicode-handling machinery.
- **Keep this content inside ADR-0043**, as originally drafted. Rejected
  at Human Review's own request — see Context and
  [ADR-0043](0043-asset-system-module-boundary.md)'s own Revision
  History.
