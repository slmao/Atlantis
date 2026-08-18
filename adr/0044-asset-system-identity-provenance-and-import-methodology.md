# ADR 0044: Asset System — Identity, Provenance, and Import Methodology

- **Status:** Proposed
- **Date:** 2026-08-18
- **Deciders:** &lt;pending Human Review&gt;
- **Related Spec:** [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)

## Context

[specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)
requires a deterministic way to name a piece of authoring content (an
**Asset ID**), a versioned record of where an imported artifact came
from (a **metadata schema**), a way to know when a re-import must happen
(**dependency tracking and re-import triggering**), and empirical proof
that importing is **deterministic**. None of these exists anywhere in
this repository today; no hashing, UUID-generation, or database
capability exists in `src/` (confirmed by direct inspection).

[ADR-0034](0034-stable-public-boundary-versus-internal-cpp-layout.md)
(`Accepted`) requires this Spec's externally-stable public boundary to
be expressed through identity and schema concepts, not internal C++
layout, but supplies "no versioning scheme, no migration mechanism, and
no compatibility-checking process" itself — this ADR must supply the
concrete identity scheme and schema
[ADR-0034](0034-stable-public-boundary-versus-internal-cpp-layout.md)
requires without inventing a compatibility/migration mechanism ahead of
any real need for one.

A **durable identity** (survives a source file rename/move) and a
**cache-invalidation key** (must change whenever content, importer
version, or a dependency changes) are frequently conflated in informal
asset-pipeline design, but answer different questions: the former names
*what* a piece of content is, independent of incidental detail; the
latter determines *when* it must be reprocessed. Conflating them — e.g.
using a content hash as the asset's own permanent identity — silently
breaks the first property every time the second property's own input
legitimately changes (an ordinary content edit). This ADR's own Decision
below adopts a **deterministic, path-derived** Asset ID — a narrower
property than full durable identity, since it does *not* survive a
rename/move of its own source file (see Decision) — as a disclosed
Phase 1 substitute. The two concepts are named distinctly throughout
this ADR — "durable identity" as the general property a rename-surviving
scheme would have, and "deterministic, path-derived identity" as what
this ADR actually decides to build — specifically so this ADR never
claims a stronger guarantee for its own scheme than it actually
provides.

[ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)
(Shader System) already established this repository's own precedent for
what "reproducible" and "deterministic" mean for a generated artifact
that is not itself checked in verbatim: a provenance anchor recorded in
the artifact's own metadata, and an empirically-verified (not assumed)
determinism claim, scoped honestly to what was actually measured.
[ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
(Image Regression Testing) separately established this repository's own
precedent for a strict, versioned, dependency-free flat-text sidecar
format, and for treating any checked-in generated artifact's update as a
reviewed, categorized, human-visible PR diff — never a silent
regeneration step.

## Decision

### Asset ID: path-derived, not random-GUID or content-hash

**An Asset ID is a deterministic value derived from the authoring
source file's own path, relative to a fixed, documented asset-source
root** (exact hash/encoding a Plan-stage detail — a hand-rolled,
non-cryptographic hash of the normalized relative path string is
sufficient; no third-party hashing dependency, per
[ADR-0043](0043-asset-system-module-boundary-and-data-format-dependency.md)).

- **Disclosed limitation, accepted for this Spec's Phase 1 scope:**
  renaming or moving an authoring source file changes its Asset ID. This
  is expected, tested behavior under this scheme — not a defect this
  Spec's implementation must work around. A future spec may supersede
  this scheme with a random-GUID-plus-sidecar approach (see Alternatives
  Considered) once a real, multi-author or frequent-rename authoring
  workflow justifies that scheme's own added bookkeeping cost.
- **An Asset ID is never used as, or derived from, a cache/rebuild key**
  (see below) — the two remain conceptually and mechanically distinct,
  closing the conflation risk named in Context.
- **The relative path this scheme hashes must be normalized by one
  fixed, platform-invariant rule before hashing** — case sensitivity,
  path separator (`/` vs. `\`), `.`/`..` segment resolution, the fixed
  asset-source-root anchor point, and Unicode normalization form are all
  fixed once (exact rule a Plan-stage detail). This is required, not
  merely an encoding nicety: it exists because
  [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)'s
  own Windows-now, Android-later artifact-sharing principle depends on
  the *same* logical authoring source producing the *same* Asset ID
  regardless of which platform's cook path computed it. An unspecified
  or platform-dependent normalization rule would silently break that
  principle the first time a real Android cook path exists to test it
  against. The metadata sidecar's own recorded source-file-identity
  field (see "Metadata schema" below) uses this same normalized form, so
  the two never disagree about what a source file's own identity is.
- **Hash collisions are possible in principle and must fail loudly, not
  merge silently.** The hash is non-cryptographic and the space of
  possible authoring-source paths is unbounded, so two distinct source
  paths producing the same Asset ID cannot be ruled out by construction
  the way a random 128-bit GUID's collision probability effectively can
  be. If the importer/cooker ever observes two distinct, simultaneously-
  known source paths sharing one computed Asset ID, it must reject the
  situation with a distinct `Err` rather than silently associating
  either path's data with the shared ID — the same fail-fast-on-invalid-
  input discipline this Spec's own Error handling requirements apply
  everywhere else. The exact hash width chosen at Plan stage should make
  this practically negligible at this project's realistic asset-count
  scale; this ADR does not claim it is eliminated by construction.

### Re-import triggering (not a cache): reuse CMake's existing incremental-build tracking

**No new, Asset-System-owned derived-data cache or database is built.**
Re-import is driven by CMake's own existing
`add_custom_command()`/`DEPENDS`-based incremental-rebuild mechanism —
the same, already-`Accepted` mechanism
[ADR-0029](0029-shader-system-build-time-compilation-boundary.md)
established for Shader System's own build-time compilation step. A
source file's own content and the importer/cooker executable itself are
both named as `DEPENDS` inputs, so CMake re-invokes the importer exactly
when either changes — no separate content-hash bookkeeping is
implemented by this Spec to duplicate what CMake's own dependency
tracking already provides correctly. **This is ordinary incremental-
build dependency tracking, identical in kind to how CMake already
reruns a C++ compile when a `.cpp`/header changes — it is not a cache**
in the sense of retaining or reusing previously-computed output across
clean builds, separate build trees, or machines; nothing is "invalidated"
because nothing is cached in the first place. This section's own heading
uses "re-import triggering," not "cache invalidation," specifically to
avoid implying a cache exists where this Spec's own scope builds none —
see "If a future spec needs a real... cache" below for what an actual
derived-data cache would add beyond what is described here.

- **If** a future spec needs a real, shared, content-addressed
  derived-data cache (e.g. once a second cook target — such as a future
  Android-specific import path producing different output from the same
  source — or a shared build farm creates a genuine need a single build
  tree's own dependency tracking cannot serve), **the recommended cache
  key**, named here for that future spec to adopt or refine, **is a
  hash of: the source file's own bytes, the importer/cooker's own
  version/build identity, and the (recursively hashed) identity of every
  asset this import depends on** — not the Asset ID, which must stay
  stable across exactly the edits that must invalidate a cache entry.
  This ADR names the key's *shape* as guidance; it does not build the
  cache itself.

### Metadata schema

Every imported asset's metadata sidecar is a strict, versioned, flat
text format (exact field-by-field layout a Plan-stage detail, bound by
this Spec's own Requirements) recording, at minimum:

- `schema_version` (first line, mandatory, checked strictly — an
  unrecognized version is rejected outright, never guessed at,
  mirroring
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own `schema_version` handling).
- The computed Asset ID.
- The authoring source file's own identity (its path, relative to the
  fixed asset-source root the Asset ID is itself derived from).
- The importer/cooker's own version/build identity — an explicit
  provenance anchor, in the same spirit as
  [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)'s
  own Vulkan-SDK-version anchor for Shader System's artifacts (exact
  anchor — a source-revision hash, a build/tool version string, or
  both — a Plan-stage detail).
- Per-asset-type fields this Spec's one supported asset type needs
  (vertex count, index count, the `VertexInputLayout` identity/hash it
  was cooked against).

**Parsing is strict**, matching
[ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
own precedent exactly: anchored-prefix field matching (never a
delimiter scan), a fixed field order, no embedded-newline support, and
an unrecognized `schema_version` rejected outright. This Spec's own
metadata parser is new, independent code — never a dependency on, or
shared implementation with,
`tests/image_regression/support/provenance.*`, which remains test-only
code outside any `src/` module's dependency surface. The two sidecar
formats may share the same *pattern*; they share no code.

### Deterministic import — proven empirically

Importing the same authoring source twice, with the same importer
version, must produce **byte-identical** runtime-artifact and metadata-
sidecar output. This is verified empirically as part of this Spec's own
Testing & Verification Plan (running the importer twice and comparing
output, the same discipline
[ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)
and
[ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
own Context each already performed for their own artifacts) — not
assumed from the importer's own implementation being "obviously"
deterministic.

### Checked-in imported artifacts — human-reviewed, never silently regenerated

If this Spec's own Plan checks in the one closed-loop asset's runtime
artifact and metadata sidecar (as opposed to generating them purely at
build time, the way Shader System's own `.spv` artifacts are never
checked in) — a Plan-stage detail this ADR does not fix — any such
checked-in artifact follows the same discipline
[ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
already established for golden images: a reviewed, visible PR diff,
never a silent regeneration step a build or CI job commits
automatically, and never overwritten by an ordinary build/test run.

## Consequences

### Positive

- Cleanly separating Asset ID (a deterministic, path-derived identifier
  — not fully rename/move-durable, see Decision) from cache key
  (re-import trigger) avoids a real, easy-to-make design mistake this
  ADR's own Context names explicitly — a future spec inheriting this
  distinction does not have to rediscover it.
- Reusing CMake's own existing, already-`Accepted` incremental-build
  mechanism means zero new cache/database code to write, test, or
  maintain at this Spec's own one-fixture scale.
- A mandatory `schema_version` field and an explicit importer-version
  provenance anchor from day one give this Spec's metadata format the
  same "stable boundary with a version to gate future change on"
  property [ADR-0034](0034-stable-public-boundary-versus-internal-cpp-layout.md)
  requires, without building a migration mechanism ahead of any real
  need for one.
- The strict, anchored-prefix-parsed metadata format directly reuses a
  proven, already-reviewed *pattern* (not code) from
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md),
  reducing the risk of re-inventing a weaker parsing discipline from
  scratch.

### Negative / Trade-offs

- Path-derived identity is genuinely fragile under rename/move — a real,
  disclosed cost this Spec's own users must understand, not a
  theoretical one. A future spec adopting GUID-based identity would need
  a migration story for every asset imported under this scheme first.
- Not building a real derived-data cache now means this Spec's own
  import step always re-runs whenever CMake's own dependency tracking
  says it should, with no cross-build-tree or cross-machine cache reuse
  — acceptable at one fixture, a real cost once import work becomes
  substantial enough to want sharing across CI runners or developer
  machines.
- The recommended future cache-key shape (source bytes + importer
  version + recursive dependency identity) is named but not built or
  tested by this ADR — a future spec adopting it inherits an unverified
  design, not a proven one.

## Alternatives Considered

- **Random-GUID identity, persisted in a sidecar file next to the
  authoring source** (the common Unity/Unreal-style `.meta` pattern).
  Considered seriously, not adopted now: this is very likely the right
  long-term answer once real, frequent authoring/renaming workflows
  exist, but it requires a durable place to persist the once-assigned
  GUID and a story for "what happens when the sidecar is lost, deleted,
  or the source is copied instead of renamed" — real design work with no
  concrete second-author or frequent-rename need yet, at this Spec's
  one-fixture scope, to validate the design against. Named explicitly as
  future work a later spec should pick up, not dismissed as wrong.
- **Content-hash identity** (hashing the authoring source's own bytes
  to produce the Asset ID directly). Rejected as an identity scheme
  specifically: identity would change on every content edit, breaking
  the property that other content should be able to reference "this
  same asset" stably across a routine edit — exactly the conflation risk
  this ADR's own Context names. Recommended instead, and adopted above,
  as the *cache key* mechanism, where changing on every content edit is
  precisely the desired property.
- **A real, content-addressed derived-data cache/database now**,
  matching a mature engine's own asset pipeline (e.g. Unreal's Derived
  Data Cache). Rejected at this Spec's own scope: one asset type, one
  build target, no shared build farm — CMake's own existing
  incremental-build mechanism already provides correct invalidation with
  zero new code, and AGENTS.md's own "no speculative abstraction"
  principle counsels against building shared-cache infrastructure ahead
  of a second cook target or a real multi-machine need to justify it.
- **A loosely-typed, JSON-shaped metadata schema** (matching a common
  industry convention for asset metadata). Rejected for the same reason
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  already rejected general JSON for the image-regression sidecar: this
  Spec's own metadata fields are a small, flat, non-nested scalar set
  with no legitimate need for JSON's own nesting/nesting/Unicode-handling
  machinery, and adopting one here would introduce exactly the kind of
  second, unreviewed parsing dependency
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "Sidecar encoding and parsing" section was written to prevent.
- **Skip empirical determinism verification; assume the importer is
  deterministic because its own logic looks straightforward.** Rejected:
  this repository's own established precedent
  ([ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md),
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md))
  is to verify determinism empirically for exactly this class of claim,
  not assume it — an assumption this ADR declines to make an exception
  for.
