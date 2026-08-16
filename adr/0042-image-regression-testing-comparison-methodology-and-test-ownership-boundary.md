# ADR 0042: Image Regression Testing — Comparison Methodology, Golden Provenance, and Test Ownership Boundary

- **Status:** Proposed
- **Date:** 2026-08-16
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md)

## Context

- Spec 0010 delivered the render-and-readback foundation but explicitly
  deferred "golden-image comparison, tolerance methodology, or CI
  image-regression gating of any kind" to a future spec — this one (see
  Spec 0010's own Non-Goals).
- [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  already commits to a golden-image comparison layer ("layer 3") and
  names two things not yet decided: where goldens live under `tests/`
  (path "to be fixed by that harness's spec") and the comparison method
  ("exact pixel match vs. perceptual diff vs. SSIM/threshold-based ...
  not yet decided and must be settled by the spec that introduces the
  regression harness").
- [AGENTS.md](../AGENTS.md)'s Definition of Done already requires "Image
  regression tests added/updated if rendered output changed, and any
  golden-image diffs in the PR were reviewed by a human, not
  auto-accepted" — an existing, standing rule this ADR must satisfy, not
  invent.
- Atlantis's current Phase 1 rendering is a single, static,
  non-antialiased, non-time-varying, non-random, fixed-camera mesh draw
  (Spec 0007's and Spec 0010's own verification fixtures) — there is no
  currently-shipping scene with animation, transparency blending, or
  multisampling whose output is inherently non-deterministic frame to
  frame on the same hardware.
- Only one real GPU (Intel Arc B370, integrated, Windows) has ever been
  available to this project — see Spec 0010's own disclosed
  single-GPU-vendor verification limitation. No second vendor/driver
  exists to test or design cross-vendor reproducibility against.
- No CI pipeline exists (confirmed: no `.github/workflows/` directory in
  this repository) — see
  [docs/process/ci-strategy.md](../docs/process/ci-strategy.md). Today's
  actual enforcement mechanism for any GPU-touching correctness gate is
  a human (or agent) running the relevant test suite locally/
  interactively and reporting the result, exactly as Spec 0010's own
  "Manual verification record" did for its interactive windowed
  regression requirement.
- **Empirical calibration performed 2026-08-16**, in response to an
  independent review of this ADR's first draft finding that a starting
  tolerance of "2, out of 255" was an unverified placeholder. Using
  Spec 0010's existing, unmodified `examples/headless_rendering_demo`
  fixture (fixed cube, fixed camera, fixed material, fixed background
  clear color, 512×512 `Rgba8Unorm`), temporarily instrumented
  (uncommitted, fully reverted afterward — no repository file reflects
  this instrumentation) to dump each cycle's raw readback buffer to
  disk, on the reference machine (GPU: Intel(R) Arc(TM) B370, integrated,
  vendorID `0x8086`, deviceID `0xb081`; driver `101.8509`,
  `DRIVER_ID_INTEL_PROPRIETARY_WINDOWS`; Vulkan instance/loader `1.4.357`,
  device apiVersion `1.4.335`; Windows 11 Home, Build 26200), against
  source revision `217db1a` (this branch's tip; byte-identical to `main`
  in every file under `src/`, `examples/`, `tests/` as of the PR #48
  merge commit `9bd74a5` — confirmed via `git diff`, zero output):
  - **Debug:** 7 fresh process launches × 3 in-process render/readback
    cycles each = 21 total captures. Every capture compared byte-for-byte
    against the first, per channel, across all 512×512×4 = 1,048,576
    bytes each. **Result: 0 pixels differing at any threshold (>0, >1,
    >2) in any of the 20 comparisons — every capture was bit-for-bit
    identical.**
  - **Release:** identical procedure, 21 total captures. **Result:
    identical — 0 pixels differing at any threshold in any of the 20
    comparisons.**
  - This evidence directly informs the tolerance decision below. It
    covers only this one fixture, on this one reference GPU/driver, as
    of this date — it does not cover a driver upgrade, different
    hardware, or a future scene with a genuine source of pixel-level
    nondeterminism (anti-aliasing, transparency); those remain this
    ADR's own disclosed, unresolved scope (see Consequences and
    Alternatives Considered).

## Decision

**Golden validity check — runs first, always, before any actual-vs-
golden comparison.** A malformed or internally-inconsistent golden is a
different category of problem than a real rendering regression, and
must never be reported as one. Before a captured buffer is compared
against a golden at all, the golden itself must pass every one of the
following, in order; the first one that fails stops right there with a
result explicitly classified as **`INVALID GOLDEN`** — never silently
downgraded, never merged into, and never gated by, the channel-tolerance/
failing-pixel-budget comparison or the provenance-mismatch diagnostic
(both of those only apply once a golden is already known-valid):
1. **The golden PNG file must exist and must decode successfully**
   (`stb_image` reports no decode error). A missing file and a decode
   failure are each reported as `INVALID GOLDEN`, distinctly named as
   such (not conflated with each other, and not conflated with a
   regression failure).
2. **The decoded PNG's own actual properties must satisfy this
   project's RGBA8 contract**, checked against the decoder's own
   metadata, not just the shape of the forced-4-channel output buffer:
   width and height are both non-zero; the decoder's `channels_in_file`
   out-parameter equals 4 (the file's real, as-encoded channel count —
   see [ADR-0041](0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)'s
   own "Decode channel contract" for why the forced-4-channel *output*
   buffer alone cannot be trusted for this check); and the file is
   confirmed 8-bit-per-channel, not 16-bit (`stb_image`'s own
   `stbi_is_16_bit`-family query must report `false`).
3. **The sidecar's own recorded format and extent must match the
   decoded PNG's actual width/height** (and, implicitly, the 4-channel/
   8-bit contract step 2 already confirmed). A sidecar claiming one
   extent or format while the PNG file itself is actually another is
   `INVALID GOLDEN` — this is an internal-consistency failure of the
   committed golden artifact itself, categorically distinct from an
   actual-capture-vs-golden mismatch (which requires a real, valid
   golden to compare against in the first place).
4. **The sidecar's recorded golden path must correspond to the file
   actually being read** (i.e. the sidecar found alongside a given PNG
   is the one for that PNG, not a stray/mismatched pairing) — a
   structural check, not a content one.
Only after all four steps pass does the golden count as valid, and only
then do "Comparison algorithm" (channel tolerance / failing-pixel
budget) and "Behavior when the current environment does not match a
golden's recorded provenance" (below) apply. `INVALID GOLDEN` is its own
third outcome, alongside `PASS`/`FAIL` — a test result reporter must
never fold it into either.

**Comparison algorithm.** Per-pixel, per-channel (R, G, B, A) absolute
byte difference between a freshly captured buffer and its golden, both
in the identical `atlantis::rhi::Format` and extent the capturing scene
declared — a format or extent mismatch between actual and golden is a
hard failure, never silently resized or reformatted. (This is the
actual-capture-vs-golden mismatch case — distinct from the golden's own
internal PNG-vs-sidecar mismatch the "Golden validity check" above
already screens out before reaching this point.)

This decision distinguishes two separate parameters, deliberately not
collapsed into one vague "tolerance":
- **Channel tolerance** — the maximum allowed absolute difference on any
  single channel of any single pixel for that pixel to still count as
  passing.
- **Failing-pixel budget** — the maximum allowed count (or proportion)
  of pixels that may exceed the channel tolerance while the overall
  comparison still passes.

**Channel tolerance = 0. Failing-pixel budget = 0.** Every channel of
every pixel must match its golden exactly (bit-for-bit); a single
out-of-tolerance channel on a single pixel anywhere fails the entire
comparison. This is not a placeholder — it is a confirmed value, backed
by the empirical calibration recorded in Context above: 42 total
captures (21 Debug, 21 Release; each set spanning 7 independent process
launches and 3 in-process cycles) against the one reference GPU/driver
this project has ever verified against, with **zero** differing pixels
at any threshold in every comparison performed. Per Context's own
disclosure, this confirmed value is scoped to the fixture and
reference GPU/driver actually calibrated against — see Consequences and
Alternatives Considered for what would trigger revisiting it.

**Failure output.** A failing comparison must produce, at minimum:
pass/fail; per-channel and combined max and mean absolute difference;
the count and percentage of pixels exceeding tolerance; the actual
captured image (written as its own PNG); and a diff image (per-pixel
absolute difference, amplified for visibility) — all written to a
predictable, documented build-output location (exact path fixed by the
Plan) so a human can inspect a failure without reproducing it
interactively, and so a future CI job can upload them as artifacts per
[ci-strategy.md](../docs/process/ci-strategy.md)'s existing "Image
regression tests" bullet.

**Golden image ownership and location.**
`tests/image_regression/goldens/<scene-slug>/<golden-name>.png`, one PNG
per golden, each accompanied by a sidecar file (exact text encoding left
to the Plan, within the bounds "Sidecar encoding and parsing" below —
but every field below is fixed by this ADR, not left open) recording:
capture date; **source revision** (see below); GPU vendor/model; driver
version; OS build; **three separate Vulkan version fields** (see
below, not one combined string); and the exact `OffscreenTarget`
extent/format used.

**Vulkan version fields, three, separate, never concatenated.** A
combined phrase like "Vulkan instance/loader and device API version"
was this ADR's own first-draft wording — vague enough to leave a Plan
guessing whether it meant one field or several. Verified directly
against this codebase's own Vulkan Backend
(`src/vulkan_backend/src/vulkan_instance.cpp`,
`src/vulkan_backend/src/instance_api_version.h/.cpp`,
`src/vulkan_backend/src/vulkan_device.cpp`): all three values below
already exist as distinct, independently-computed quantities in the
current implementation — this is not a speculative subdivision, it
matches values the Vulkan Backend already queries and branches on for
its own Spec 0007/ADR-0024 dynamic-rendering-path decision:
- **Loader-reported API version** — the value `vkEnumerateInstanceVersion()`
  returns, queried via `vkGetInstanceProcAddr(nullptr, ...)` before any
  `VkInstance` exists (`vulkan_instance.cpp`'s `loaderVersion`). **Not
  always available:** a genuine Vulkan-1.0-only loader has no
  `vkEnumerateInstanceVersion` entry point at all — this codebase's own
  `loaderVersionQueryAvailable` flag already models exactly this case.
  When unavailable, the sidecar records that fact explicitly (e.g. a
  recorded value of "unavailable (pre-1.1 loader)"), never a fabricated
  version number.
- **Requested instance API version** — the value this codebase's own
  `decideRequestedInstanceApiVersion()` computes and sets into
  `VkApplicationInfo::apiVersion` at `vkCreateInstance()` time. Always
  available (it is a value this process itself chose and passed in).
- **Selected physical device `apiVersion`** — `VkPhysicalDeviceProperties::apiVersion`
  for the physical device `Device` construction actually selected,
  queried via `vkGetPhysicalDeviceProperties()` (`vulkan_device.cpp`).
  Always available once a `Device` exists.
Alongside these three: GPU vendor/model and driver version come from
the same `VkPhysicalDeviceProperties` query (`vendorID`, `deviceID`,
`deviceName`, `driverVersion`) already used throughout this project's
own verification records (e.g. Spec 0010's disclosed hardware). No
field in this list is ever concatenated into another — each is its own
named sidecar value, so a provenance-mismatch diagnostic (see below)
can name precisely which of the three changed.

**Source revision, precisely — not "the commit hash of this golden's
own commit."** A sidecar records `git rev-parse HEAD` **as run at
capture time, against a clean working tree** (no uncommitted changes) —
i.e. the revision the source tree already existed at *before* the golden
and its sidecar are added. This is deliberately not, and can never be,
"the commit hash of the commit that adds this golden": a commit's hash
is only computable from its own final tree contents, so a sidecar cannot
record its own commit's hash without a circular dependency. Concretely:
- If a golden is being added or updated **because of a rendering change
  in the same PR**, that rendering change must be **committed first**.
  The golden is then captured against that already-existing commit
  (a clean working tree at that commit), and the sidecar records that
  commit's hash. The golden PNG and its sidecar are then added via a
  **separate, subsequent commit** — never folded into the same commit as
  the rendering change, and never recording a hash that does not yet
  exist at capture time.
- If no rendering change is involved (see "Golden update reasons"
  below), the recorded source revision is simply whatever commit the
  working tree was clean at when the capture ran.
- Capturing against a dirty working tree is a capture-tool usage error,
  not a supported case — the recorded source revision would not
  correspond to any real, inspectable commit.

**Behavior when the current environment does not match a golden's
recorded provenance.** The image regression suite has no `skip`
outcome. A test whose golden exists always runs the real pixel
comparison, unconditionally. Before comparing, the test reads the
current process's actual GPU vendor/model, driver version, OS build,
and each of the three Vulkan version fields above independently, and
compares each one against the golden's own sidecar provenance:
- On a **match** (every field equal), the test's pass/fail result is
  reported as ordinary reference-environment verification evidence,
  exactly as today.
- On a **mismatch** (any one or more fields differ), the pixel
  comparison still runs and still produces a real pass/fail result —
  but the test additionally emits a distinct, prominent
  `PROVENANCE MISMATCH` diagnostic naming **which specific field(s)**
  differ (current vs. golden's recorded vendor/model/driver/OS build/
  loader version/requested instance version/physical device
  `apiVersion`, individually) — never a single opaque "environment
  differs" message — reported **separately from,
  and never merged into,** the pass/fail verdict. **A pass obtained under
  a provenance mismatch must never be cited or reported as evidence that
  the harness was verified against the reference environment** — it is
  evidence only that this specific run, on this specific (non-reference)
  environment, happened to match; a failure obtained under a mismatch
  must not be assumed to be a real regression without first checking
  whether the mismatch itself is the explanation.

**Golden regeneration — locked to a distinct, unreachable-by-default
mechanism.** Regenerating a golden is performed **only** by a separate,
standalone developer tool/entry point (exact name left to the Plan, but
its architecture is fixed here, not left open):
- It is **never registered with CTest** — it does not appear in
  `ctest -L gpu`, in the default build target set, or in any target an
  ordinary `cmake --build` invocation produces by default.
- The ordinary GPU-required comparison test binary(ies) that `ctest -L
  gpu` runs are **read-only** with respect to golden files: no
  environment variable, command-line flag, or code path reachable from
  that binary's normal invocation may write to
  `tests/image_regression/goldens/` under any circumstance. Regeneration
  requires invoking the separate tool directly — never a hidden mode of
  the comparison test itself.
- A golden's PNG and sidecar are only ever changed by a dedicated PR,
  never bundled silently into an unrelated code or test change, and
  always human-reviewed before merge — matching
  [testing-strategy.md](../docs/process/testing-strategy.md)'s existing
  rule verbatim ("never a silent regeneration step that a CI job runs
  and commits automatically").

**Golden update reasons — categorized, not left implicit.** Every PR
that touches a file under `tests/image_regression/goldens/` must state,
in its own description, which of the following applies:
1. **Rendering change** — an intentional change to the rendered output
   (a code change elsewhere in the same PR, or a prior, already-merged
   commit this PR's golden update catches up to). The PR must identify
   that change.
2. **Reference-environment change** — the reference machine's GPU
   driver, OS, or Vulkan runtime changed, and the previously-recorded
   golden no longer matches even though no rendering code changed. The
   PR must include **both** the old and new provenance (old golden's
   sidecar vs. the new capture's actual environment) **and** diff
   evidence (the actual-vs-old-golden diff image/metrics ADR-0042's own
   failure-output contract already produces) demonstrating the reviewer
   has a real basis to believe no rendering regression is hiding behind
   the environment change — a driver upgrade is not, by itself,
   sufficient justification.
3. **Approved rebaseline** — a deliberate, explicitly human-approved
   decision to accept a new baseline for a stated reason not covered by
   (1) or (2) (e.g. correcting a golden that was itself wrong when
   captured). Requires the same explicit reasoning as (2).
A golden-update PR must not leave this categorization implicit, and a
"reference-environment change" must never be described using the same
language as an ordinary "rendering change" update.

**Golden PNG checksum — considered, not adopted.** A cryptographic
checksum (e.g. SHA-256) of each golden PNG's own byte content was
considered for the provenance sidecar, to give a reviewer an integrity
signal independent of the PNG diff itself. **Not adopted**, because
adding one would require either a new hashing dependency or hand-rolled
hash code — this repository has no existing SHA-256 (or equivalent)
capability today (confirmed: no such utility exists anywhere under
`src/core/`) — and this ADR's own dependency discipline (justify or
avoid, per [ADR-0041](0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md))
applies equally to a second dependency introduced only to satisfy a
review suggestion. Instead: a golden PNG and its sidecar are always
added or changed together, in the same commit, which git's own commit
object already atomically binds — there is no window in which one
could reference stale content in the other. The sidecar's own recorded
file path, dimensions, format, and provenance fields are the
correlation signal; a PNG corrupted independently of its sidecar would
either fail to decode or fail the PNG-vs-sidecar consistency check
(both caught immediately as `INVALID GOLDEN`, per "Golden validity
check" above) or decode into content that fails the pixel comparison
against every future capture — a checksum would not catch a case none
of those already does, given this project's threat model (accidental
corruption/tooling bugs, not adversarial tampering of a reviewed,
version-controlled binary file).

**Sidecar encoding and parsing — bounded, so "JSON is the working
assumption" cannot quietly become a second unreviewed dependency.**
This ADR's exact text encoding for a sidecar is left to the Plan, but
within firm boundaries, not a blank check:
- **No new parsing/serialization dependency without its own Spec → ADR
  → Human Review**, following exactly the precedent
  [ADR-0041](0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  already set for `stb`. Silently adding a JSON (or other) library
  during Plan/Implementation to make sidecar parsing convenient is
  exactly the uncontrolled dependency decision
  [AGENTS.md](../AGENTS.md)'s Golden Rule prohibits — it does not become
  acceptable merely because the library in question is small or common.
- **No reuse of `src/shader_system/`'s own private JSON parser, and no
  dependency of any kind from `tests/image_regression/` on Shader
  System's private implementation.** `src/shader_system/`'s JSON parser
  (`specs/README.md`'s own Spec 0008 row: "a private JSON parser") is
  private to that module by design — reaching into it from
  `tests/image_regression/` would be a real module-boundary violation
  (a new, undeclared coupling between two otherwise-unrelated areas),
  not a harmless reuse, regardless of how convenient it would be.
- **The default, expected encoding is a simple, deterministic, flat
  text format** — one that requires nothing beyond the C++ standard
  library and a small amount of `tests/image_regression/`'s own code to
  read and write (e.g. one fixed-order `key: value` line per field,
  or an equally simple hand-rolled scheme) — **not general-purpose
  JSON**, since every sidecar's actual content is a small, flat,
  non-nested set of scalar fields (strings and integers) with no
  legitimate need for JSON's nesting, escaping, or Unicode-handling
  machinery, and the C++ standard library has no built-in JSON support
  of its own to lean on.
- **If a future Plan concludes it genuinely needs a real JSON library
  (with a third-party parser) despite the above, that is itself a new
  architectural decision** — it goes back through Spec/ADR/Human Review
  before Implementation, exactly like any other new dependency; it is
  never decided silently mid-Plan or mid-Implementation.
- **Whatever encoding is chosen must have a stable, fixed field order**
  (so a sidecar's own diff in a PR reviews cleanly, field-for-field, not
  as a reordered blob) **and an explicit format/schema version marker**
  (so a future field addition or format change can be detected and
  migrated deliberately, rather than silently misread by older/newer
  tooling).

**Golden set strategy.** One unified golden set, captured on and
compared only against the one real GPU vendor/family this project has
ever verified against (Intel Arc / integrated, Windows Vulkan) — no
per-vendor golden branching is designed or implemented by this
decision, because no second vendor exists to design it against. This is
a disclosed, explicit limitation carried forward from Spec 0010, not a
claim of cross-vendor stability. A future spec/ADR revisits per-vendor
golden sets if and when a second real GPU vendor becomes available to
this project.

**Reproducibility constraints on any scene used for image regression
testing.** No wall-clock time, RNG, or uninitialized-memory dependence
in camera, transform, or material state — every input a covered scene's
output depends on must be a fixed, checked-in constant. This decision
adopts Spec 0010's own existing `examples/headless_rendering_demo`
fixture (fixed cube, fixed camera, fixed material, fixed background
clear color, 512×512 `Rgba8Unorm`) as the first golden-image target
scene, rather than inventing a new one. Determinism is verified
empirically, not assumed: the same cycle repeated multiple times against
the same `OffscreenTarget` instance must produce byte-identical (within
tolerance) captures — extending Spec 0010's own established "repeated
cycle produces consistent results" verification from its coarse content
check to a real pixel comparison.

**Test/module ownership boundary.** A new test-suite area,
`tests/image_regression/`, parallel to the existing `tests/core/`,
`tests/rhi/`, `tests/render_graph/`, `tests/renderer/`,
`tests/vulkan_backend/` areas — **not a new top-level `src/` module**,
and no existing module's public dependency set changes. Internally
layered per
[testing-strategy.md](../docs/process/testing-strategy.md)'s own layer
split (exact file/target structure left to the Plan):
- **GPU-independent:** the pixel-diff/tolerance algorithm, PNG
  encode/decode round-trip correctness, the golden validity check's four
  ordered steps (including the `channels_in_file`/bit-depth metadata
  check — exercisable against a deliberately non-RGBA or non-8-bit test
  PNG, no Vulkan device needed), and provenance-sidecar parsing
  (including the fixed field order/version-marker contract), exercised
  against synthetic in-memory buffers and test PNGs with no Vulkan
  device — Catch2, matching every other GPU-independent suite in this
  repository.
- **GPU-required:** the real capture-via-`OffscreenTarget` →
  compare-against-checked-in-golden path, labeled `gpu` per the existing
  ctest convention Spec 0006/Spec 0010 already established — no new
  CI/test-category label is introduced by this decision (Spec 0010's own
  open question about a distinct headless-GPU-test label remains open,
  unresolved by this ADR).

The `stb` dependency [ADR-0041](0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
introduces is linked only into these test-support targets.

**Gating today vs. CI gating.** This decision defines what a
**local/manual gate** looks like today: a human or agent runs the
`gpu`-labeled image regression suite against real Windows/Vulkan
hardware and reports pass/fail, exactly as every other GPU-touching
verification in this project already works absent CI (Spec 0010's own
"Manual verification record" precedent). **Automated CI enforcement of
this gate is explicitly not decided or implemented by this ADR** — it
remains blocked on prerequisites
[ci-strategy.md](../docs/process/ci-strategy.md) already logs as open
(GPU access in CI: a software Vulkan implementation vs. a self-hosted
real-GPU Windows runner; the dependency-fetch-in-CI strategy) and on
actual infrastructure provisioning, neither of which this Spec/ADR
resolves or fabricates. Once that infrastructure exists, this decision's
failure-output contract (above) is what a future CI job uploads as
artifacts.

## Consequences

### Positive

- A single, strict, unambiguous per-pixel-must-pass rule (channel
  tolerance 0, failing-pixel budget 0) is simple to implement, simple to
  reason about, cannot silently mask a small but real localized
  regression, and — unlike the original placeholder — is now backed by
  real repeated-capture evidence rather than an assumption (see Context)
  — directly satisfies the requirement that a golden-image gate must not
  degrade into "roughly looks the same."
- Reusing Spec 0010's own already-verified fixture as the first golden
  scene means the harness proves itself against known-good, already
  human-reviewed rendering output rather than a newly authored,
  unreviewed scene.
- CI-agnostic at its core (a pass/fail function plus a documented
  artifact contract) — a future build-system/CI spec can wire automatic
  invocation on top without this ADR needing revision.
- A dedicated `INVALID GOLDEN` outcome, checked first, means a broken or
  self-inconsistent committed artifact is reported as exactly what it
  is — never mistaken for (or hidden behind) a real rendering
  regression, and never silently passed because a forced-4-channel
  decode papered over a golden that was never really RGBA.
- Three separately-named Vulkan provenance fields (loader/instance/
  physical-device) let a `PROVENANCE MISMATCH` diagnostic name exactly
  which one changed, instead of an opaque "something about the Vulkan
  environment differs."

### Negative / Trade-offs

- A zero channel tolerance and zero failing-pixel budget are strict: if
  Phase 1 ever adds anti-aliasing, transparency, or any other source of
  legitimate sub-pixel nondeterminism, or if a future driver upgrade or
  different hardware is found empirically to introduce real, non-
  regression pixel noise, this exact-per-pixel rule will need
  revisiting with new calibration evidence of its own (flagged
  explicitly in Spec 0011's own Risks & Open Questions, not solved
  here) — the calibration in Context above covers only the fixture and
  reference GPU/driver it was actually run against, not any future
  change to either.
- A single, unified (not per-vendor) golden set means this harness
  cannot yet be run meaningfully against any GPU other than the one this
  project has verified — a real, disclosed capability gap, not hidden by
  this decision.
- Manual/local gating (vs. CI-enforced) depends on a human or agent
  remembering to run the suite before merging — the same limitation
  every other GPU-touching test in this project already has today, not a
  new one this ADR introduces.
- Provenance sidecar files add a second file to review/maintain per
  golden, rather than embedding metadata in the PNG itself (e.g. via
  `tEXt` chunks) — chosen for easy diffability in an ordinary PR review,
  at the cost of one more file per golden.
- The golden validity check (four ordered steps) and three separate
  Vulkan provenance fields are more moving parts than a single opaque
  "does it look okay" check would be — accepted because each part
  answers a genuinely different question (is the golden itself valid;
  does the capture match it; does the environment match) that a
  collapsed check cannot distinguish when something goes wrong.
- A dependency-free, hand-rolled flat sidecar encoding (the default per
  "Sidecar encoding and parsing" above) is less flexible than a real
  JSON library would be if this project's provenance schema ever grows
  more complex/nested than today's flat scalar-field set — accepted as
  the right trade-off for the schema's current, genuinely simple shape;
  revisit only if that shape changes, through its own Spec/ADR, not by
  reaching for a library pre-emptively now.

## Alternatives Considered

- **Percentage-based pixel tolerance** (e.g., "≥99.9% of pixels within
  tolerance passes"). Rejected for Phase 1: this project's own static,
  non-antialiased scenes have no legitimate source of widespread
  sub-pixel noise that would need this slack, and any slack budget risks
  silently absorbing a real, small, localized regression — exactly what
  this ADR is designed to avoid. May be revisited by a future ADR once
  Phase 1 introduces a genuine source of pixel-level nondeterminism
  (anti-aliasing, transparency).
- **SSIM or another perceptual-similarity metric.** Rejected: perceptual
  metrics are designed to tolerate exactly the kind of small, structured
  difference (a shifted or recolored region) that a rendering regression
  often looks like — the wrong tool for a correctness gate whose entire
  purpose is catching small, real differences.
- **Per-vendor golden sets, decided now.** Rejected: no second GPU
  vendor exists in this project's environment to design or verify a
  per-vendor strategy against; deciding one now would be speculative,
  not evidence-based — left as explicit Future Work.
- **Embedding provenance directly in PNG metadata chunks instead of a
  sidecar file.** Considered; rejected for Phase 1 in favor of a
  separate, plain-diffable sidecar — a PR reviewer can read a sidecar's
  provenance change in an ordinary text diff, whereas a PNG chunk change
  is invisible in a normal `git diff` and requires tooling to inspect.
- **A new top-level `src/` module** (e.g. an "Atlantis Image
  Regression" or general "Testing" module). Rejected: this is
  test-support infrastructure with a single consumer (this project's own
  test suites), not a runtime/engine capability any product module
  depends on; [AGENTS.md](../AGENTS.md)'s module list is deliberately
  not extended for it, matching Spec 0011's explicit instruction not to
  introduce an unreviewed new top-level module.
- **Designing and asserting CI gating as already actionable.** Rejected:
  no CI pipeline, no GPU CI runner, and no resolved dependency-fetch-in-
  CI strategy currently exist (verified: `.github/workflows/` does not
  exist in this repository); asserting otherwise would fabricate
  infrastructure, contrary to [AGENTS.md](../AGENTS.md)'s Golden Rule.
- **Adding a cryptographic checksum field to the provenance sidecar.**
  Considered; not adopted — see "Golden PNG checksum" under Decision
  above. Would require a new dependency or hand-rolled hash code for a
  signal git's own atomic commit boundary and the sidecar's existing
  correlated fields (path, dimensions, format, provenance) already
  provide for this project's actual threat model.
- **Silently skipping the image regression suite on non-reference
  hardware/drivers.** Rejected: a `skip` outcome would make the
  local/manual gate this ADR defines meaningless on any machine other
  than the one exact reference environment, defeating its purpose as a
  usable-today gate. The comparison always runs; a provenance mismatch
  is reported as a separate, explicit diagnostic instead (see "Behavior
  when the current environment does not match a golden's recorded
  provenance" under Decision above).
- **Recording "the commit hash of the commit this golden is added in"
  as originally drafted.** Rejected on discovering the self-reference:
  a commit's hash cannot be known before that commit's own content
  (including this sidecar) is finalized. Corrected to record the
  clean-working-tree source revision *at capture time*, which is always
  a real, already-existing, inspectable commit — see "Source revision,
  precisely" under Decision above.
- **A single combined "Vulkan instance/loader and device API version"
  provenance field, as originally drafted.** Rejected: ambiguous about
  whether it meant one value or several, and — verified against this
  codebase's own Vulkan Backend — collapses three values
  (`vkEnumerateInstanceVersion()`'s loader-reported version, the
  requested `VkApplicationInfo::apiVersion`, and the selected physical
  device's `VkPhysicalDeviceProperties::apiVersion`) that are already
  computed as distinct quantities in the real implementation, and that
  can legitimately differ from one another. Corrected to three separate,
  named fields — see "Vulkan version fields, three, separate, never
  concatenated" under Decision above.
- **Trusting `stb_image`'s forced-4-channel output buffer alone as proof
  a golden PNG is really RGBA.** Rejected: `desired_channels = 4`
  forces the *output buffer* shape but silently synthesizes a channel
  (e.g. alpha) the source file never actually had — it cannot, by
  itself, distinguish a real RGBA PNG from one that was accidentally
  re-saved as RGB or grayscale. Corrected to also check the decoder's
  own `channels_in_file` out-parameter and bit-depth query — see
  [ADR-0041](0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)'s
  "Decode channel contract" and this ADR's own "Golden validity check."
- **Treating a golden's own PNG-vs-sidecar internal inconsistency as an
  ordinary actual-vs-golden regression failure.** Rejected: a malformed
  or self-inconsistent golden is a defect in the committed test artifact
  itself, not evidence about the code under test — conflating the two
  would make a broken golden look like (or mask) a real rendering
  regression. Corrected to a dedicated `INVALID GOLDEN` outcome that
  runs first and is never downgraded by tolerance or provenance-mismatch
  handling — see "Golden validity check" under Decision above.
- **Leaving sidecar parsing implementation unconstrained beyond "JSON is
  the working assumption."** Rejected: this is exactly the same
  uncontrolled-dependency risk
  [ADR-0041](0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  exists to close for the golden PNG codec itself — an unconstrained
  Plan could silently add a JSON library, or silently reuse
  `src/shader_system/`'s private JSON parser (a real module-boundary
  violation). Corrected to a bounded default (a simple, dependency-free,
  flat text format) with any real third-party-parser-requiring JSON
  approach explicitly routed back through Spec/ADR — see "Sidecar
  encoding and parsing" under Decision above.
