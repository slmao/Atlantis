# ADR 0044: Asset System — Identity, Provenance, and Import Methodology

- **Status:** Accepted
- **Date:** 2026-08-18 (accepted 2026-08-19 — see Revision History)
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-19 as part of Spec 0012's Human Review Approval; see
  that spec's own Human Review Approval note for the full approval
  record and the directed corrections this ADR carries.
- **Related Spec:** [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md) (`Approved`)
- **Related ADR(s):**
  [ADR-0043](0043-asset-system-module-boundary.md) (module boundary) and
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format, versioning, and dependency policy) — siblings from the
  same Spec. This ADR's own Asset ID hash algorithm/width/serialization
  choices operate under ADR-0045's blanket no-new-dependency policy; the
  metadata sidecar's own wire encoding is ADR-0045's Decision, cross-
  referenced under "Metadata schema" below rather than restated.

## Revision History

- **2026-08-18 (original):** Drafted alongside Spec 0012, initially
  leaving the Asset ID's concrete path/hash contract as a Plan-stage
  detail; that contract was completed in full within this ADR later the
  same day, before Human Review.
- **2026-08-19 (Human Review corrections, then `Accepted`):** Human
  Review directed two corrections before accepting this ADR. (1) The
  Asset ID's binary serialization is now **unconditionally
  little-endian**, matching
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  own corrected fixed byte-order contract — the earlier
  "native/little-endian" phrasing conflated a property of the *format*
  with a property of the *writing host*. (2) The collision-detection
  guarantee is now explicitly **scoped to one importer/validator
  invocation's own declared asset set**, since with no global asset
  database this Spec cannot detect collisions between assets never
  presented together; repository-global uniqueness is delegated to a
  Plan-arranged validation step covering every declared asset, and to a
  future asset registry/database spec. This ADR then moved to
  `Accepted`.

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
root.** This section fixes the full contract — the asset-source root
and logical-path definition, path syntax rules, case sensitivity,
character set, and the concrete hash algorithm/width/serialization — as
this ADR's own Decision, not left open past this Spec's approval; only
the asset-source root's own exact repository location (e.g. `assets/`)
remains a Plan-stage detail.

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

**Asset-source root and logical path.** A single, fixed, documented
directory within the repository (exact path a Plan-stage detail, e.g.
`assets/`) is the asset-source root. Every authoring source file this
Spec's importer accepts must live under that root. The **logical path**
is the source file's own path relative to that root, expressed with `/`
as its only separator — the value this scheme actually hashes.

**Path syntax rules, fixed here so Windows and Android can never
disagree about a legal logical path's own bytes:**

- **Separator:** before any further processing, every `\` in the input
  path is rewritten to `/`. Windows accepts `/` as a path separator in
  virtually all of its own APIs, so this rewrite is lossless for any
  path Windows can express; a purely `/`-separated string is therefore
  both platforms' own canonical form, with no further translation
  needed on the Android/POSIX side.
- **Absolute paths and Windows drive prefixes are rejected, never
  normalized.** The importer's own public entry point accepts only a
  path already expressed relative to the asset-source root (e.g.
  supplied by the CMake build graph as a relative path). An absolute OS
  path, or a path carrying a Windows drive prefix (e.g. `C:`), handed to
  it is a caller precondition violation — rejected with a distinct
  `Err`, never silently reinterpreted as relative. This closes the door
  on ever hashing a machine-specific root, which Android's own
  filesystem layout does not share with Windows drive letters at all.
- **`.`/`..`/empty-segment resolution, with root-escape rejected:** the
  logical path is lexically normalized (equivalent to
  `std::filesystem::path::lexically_normal()` — C++ standard library
  only, no new dependency, per
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)) —
  collapsing `.` segments, resolving `..` segments against preceding
  segments, and collapsing empty/doubled separators (e.g. `foo//bar`, a
  trailing `/`). If, after normalization, any leading `..` segment
  remains — the path would escape the asset-source root — the importer
  rejects it with a distinct `Err`; it is never hashed. This is both a
  correctness rule (no asset may claim an identity rooted outside the
  space this scheme actually governs) and a real containment boundary
  for the asset-source root, not just a starting point for path math.
- **Case sensitivity: case is preserved exactly as authored and hashed
  case-sensitively — never folded to one case, on either platform.**
  Two logical paths differing only by case are, by this scheme, two
  distinct assets. Because Windows' own default filesystem (NTFS) is
  case-insensitive-but-case-preserving, two such files cannot actually
  coexist at the same path on an ordinary Windows checkout — this
  situation can only arise if content is committed to git (itself
  case-sensitive in its own index) in a way a Windows checkout cannot
  faithfully materialize both of; the importer must reject a
  case-only-differing pair of logical paths within one invocation's own
  declared asset set with a distinct `Err` (see "Collision detection"
  below for that scope's own precise bounds), rather than silently hash
  whichever one the filesystem happened to return. Case-sensitive
  comparison, not
  case-folding, was chosen because it matches Android/Linux-style
  case-sensitive filesystems (the platform a folding scheme would
  otherwise have to work hardest to fake); it avoids needing real
  Unicode case-folding for arbitrary text; and this project's own
  `snake_case` file-naming convention makes an accidental case-only
  collision unlikely in practice.
- **Character set: ASCII-only for this Spec's own Phase 1 scope.** Every
  authoring-source logical path is restricted to ASCII letters (`a`-`z`,
  `A`-`Z`), digits (`0`-`9`), `_`, `-`, `.`, and `/` as the path
  separator. No other byte value — including any non-ASCII UTF-8
  sequence — is permitted; the importer validates this explicitly and
  rejects any path containing a disallowed byte with a distinct `Err`,
  rather than accepting and hashing arbitrary bytes it cannot verify
  round-trip identically across platforms. This sidesteps Unicode
  normalization form (NFC vs. NFD — macOS's own HFS+ historically
  normalizes to NFD while Windows/Android/git typically leave NFC, a
  well-documented, genuinely easy-to-get-wrong cross-platform source of
  path-identity bugs unrelated to this Spec's own scope) entirely, at
  the cost of disallowing non-ASCII authoring-source filenames for Phase
  1. This is a disclosed scope-narrowing limitation, not an oversight —
  named as future work (see Out of Scope) once a real need for
  non-ASCII asset paths exists to justify solving Unicode normalization
  properly, matching this Spec's own repeated pattern (see the
  path-derived-not-GUID identity choice above) of narrowing to what
  today's one real fixture needs and revisiting once a real second need
  exists.

**Hash algorithm, width, and serialization.** The Asset ID is computed
by applying **FNV-1a, 64-bit variant** (a well-known, public-domain-
equivalent, non-cryptographic hash — a few lines of C++, no third-party
dependency, per
[ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md))
to the normalized, case-preserved, ASCII-restricted logical path
string's own bytes.

- **64-bit width** is chosen deliberately: at this project's realistic
  per-repository asset count (plausibly hundreds to low thousands over
  this scheme's own working lifetime, not billions), a 64-bit hash's
  birthday-bound collision probability is negligible — a 50% collision
  chance is not expected until roughly 2^32 (~4 billion) distinct
  assets, several orders of magnitude beyond any plausible use of this
  scheme. 32-bit would make collision plausible at a scale (tens of
  thousands of assets) this project could realistically reach; 128-bit
  is unnecessary weight for a non-cryptographic, single-repository
  identity scheme this ADR already discloses as a Phase 1 substitute for
  full durable identity, not a permanent cryptographic commitment.
- **Byte serialization:** in the binary runtime artifact or any other
  binary context, the Asset ID is a fixed 8-byte (`std::uint64_t`) field
  written **unconditionally little-endian**, the same fixed on-disk
  byte-order contract
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  fixes for the rest of the binary surface — one byte-order rule for
  this Spec's entire binary surface, and a property of the format
  itself rather than of whichever host wrote it. In the metadata
  sidecar's own flat text format, the Asset ID is written as a
  fixed-width, lowercase, 16-hex-digit string (`%016x`) — a text
  representation sidesteps endianness entirely in the human-readable
  format, matching ADR-0042's own precedent of keeping provenance data
  in a sidecar as plain, unambiguous text.

**Collision detection — scoped to one invocation's declared asset set.**
The hash is non-cryptographic and the space of possible logical paths is
unbounded, so two distinct logical paths producing the same 64-bit Asset
ID cannot be ruled out by construction the way a random 128-bit GUID's
collision probability effectively can be — though, per the width
analysis above, it is practically negligible at this project's realistic
scale, not merely asserted to be. What this Spec's own scope can and
does guarantee is bounded accordingly:

- **Within the set of assets declared to a single importer/validator
  invocation**, two distinct logical paths computing the same Asset ID,
  and two logical paths differing only by case, must each be detected
  and reported as a distinct `Err` — never silently merged, and never
  resolved by whichever entry the filesystem or build system happened to
  supply first. This is the same fail-fast-on-invalid-input discipline
  this Spec's own Error handling requirements apply everywhere else.
- **This Spec explicitly does *not* claim repository-global collision
  detection.** With no asset registry or database
  ([specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)'s
  own "no global mutable asset database" requirement, and no
  derived-data cache per this ADR's own re-import-triggering decision),
  nothing in this Spec's scope observes assets that were never presented
  together in one invocation — for example, two assets declared by
  independent build targets, or an asset added in a later, separate
  build. A collision between such assets is not detected by this Spec's
  own implementation, and this ADR does not pretend otherwise.
- **This Spec's own Plan must therefore include a validation step whose
  declared input set covers every asset the repository declares**, so
  that in practice the "one invocation" scope above is the whole
  repository's asset set rather than an arbitrary subset. Exactly how
  that step is organized in CMake — one validator invocation over an
  aggregated list, a dedicated validation target, or another
  arrangement — is a Plan-stage decision this ADR deliberately does not
  fix.
- **A future asset registry/database Spec must re-establish global
  uniqueness on its own terms.** Whatever mechanism such a future Spec
  introduces (a persisted registry, a real derived-data cache, a
  multi-repository asset space) inherits the obligation to validate
  uniqueness across everything it governs; it may not assume this Spec
  already did so, because this Spec's own guarantee is explicitly scoped
  to one invocation's declared set.

**Why Windows and a future Android cook path are guaranteed to compute
the identical Asset ID for the identical logical authoring source:**
every rule above is platform-invariant by construction, not by
convention. The ASCII-only character-set restriction removes any
Unicode-normalization-form divergence; the fixed `/`-separator canonical
form removes any `\`-vs-`/` divergence; rejecting absolute paths and
drive prefixes before hashing removes any host-root divergence; case is
preserved and hashed case-sensitively regardless of the host
filesystem's own case-folding behavior; and the same standard-library-
only hash algorithm, bit width, and byte order are used unconditionally
on every host. The metadata sidecar's own recorded source-file-identity
field (see "Metadata schema" below) records this same normalized logical
path, so the sidecar and the Asset ID it names never disagree about what
a source file's own identity is.

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

This section fixes the metadata sidecar's own **field semantics** —
which fields it carries and what each means. The sidecar's own **wire
encoding** (flat text, anchored-prefix parsing, strict `schema_version`
handling) is
[ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
own Decision, cross-referenced rather than restated here so the two
concerns each have exactly one authoritative description.

Every imported asset's metadata sidecar (exact field-by-field layout a
Plan-stage detail, bound by this Spec's own Requirements) records, at
minimum:

- `schema_version` (first line, mandatory, checked strictly — an
  unrecognized version is rejected outright, never guessed at,
  mirroring
  [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own `schema_version` handling).
- The computed Asset ID (serialized per "Hash algorithm, width, and
  serialization" above — a fixed-width, lowercase, 16-hex-digit
  string).
- The authoring source file's own identity — its logical path, relative
  to the fixed asset-source root, in the same normalized form the Asset
  ID is itself derived from (see "Asset-source root and logical path"
  above), so the sidecar and the Asset ID it names never disagree.
- The importer/cooker's own version/build identity — an explicit
  provenance anchor, in the same spirit as
  [ADR-0031](0031-shader-system-artifact-versioning-and-reproducibility.md)'s
  own Vulkan-SDK-version anchor for Shader System's artifacts (exact
  anchor — a source-revision hash, a build/tool version string, or
  both — a Plan-stage detail).
- Per-asset-type fields this Spec's one supported asset type needs
  (vertex count, index count, the `VertexInputLayout` identity/hash it
  was cooked against).

This Spec's own metadata parser is new, independent code — never a
dependency on, or shared implementation with,
`tests/image_regression/support/provenance.*`, which remains test-only
code outside any `src/` module's dependency surface. The two sidecar
formats may share the same *pattern* (see ADR-0045); they share no
code.

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
