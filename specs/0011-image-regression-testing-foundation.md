# Spec: Image Regression Testing Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved by human review — see Human Review Approval
  below.
- **Created:** 2026-08-16
- **Human Review Approval (2026-08-16):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-16, following two independent,
  read-only review rounds — agent-performed, not Human Review, each
  conducted against this spec's actual drafted text and its two ADRs
  rather than against the drafter's own summaries — see
  [PR #50](https://github.com/slmao/Atlantis/pull/50) for the full
  revision history and the "Independent Review — Round 1/Round 2" notes
  below for what each round found and fixed.

  **This approval explicitly accepts the following:**
  1. The PNG golden format plus the `stb_image`/`stb_image_write`
     test-only dependency, `FetchContent`-pinned to a specific, full
     commit hash (`nothings/stb` has no tagged releases) — see
     [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md).
  2. **Channel tolerance = 0, failing-pixel budget = 0**, for the reused
     `examples/headless_rendering_demo` fixture on the one reference
     GPU/driver this project has verified against — backed by the
     empirical calibration recorded in
     [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
     own Context (42 total captures, zero differing pixels at any
     threshold).
  3. That this evidence is **not** generalized to any other GPU/driver
     combination — a driver upgrade or different hardware requires its
     own fresh calibration before this rule is treated as still valid
     for that environment; see ADR-0042's own Consequences and Spec
     0011's Risks & Open Questions.
  4. The full golden provenance contract: **source revision** (not a
     self-referential commit hash), the **golden validity check**
     (`INVALID GOLDEN` as its own outcome, checked before any
     regression comparison), **`PROVENANCE MISMATCH`** as a diagnostic
     always separate from pass/fail, and the three-category
     (rendering change / reference-environment change / approved
     rebaseline) human-reviewed golden-update contract — all per
     ADR-0042.
  5. Golden regeneration locked to a distinct, standalone developer
     tool that is never registered with CTest and never reachable from
     the ordinary comparison test binary by any environment variable,
     flag, or default code path.
  6. The sidecar boundary: no new parsing/serialization dependency
     without its own future Spec/ADR/Human Review, and no coupling of
     `tests/image_regression/` to `src/shader_system/`'s private JSON
     parser or any other private implementation of another module.
  7. That automated CI-enforced gating remains explicit, disclosed
     follow-up work, blocked on
     [ci-strategy.md](../docs/process/ci-strategy.md)'s own open
     questions (GPU-in-CI approach, dependency-fetch-in-CI strategy)
     plus actual infrastructure provisioning — **this approval does not
     authorize or implement any CI infrastructure.**

  Following this approval:
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  and
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  each move to `Accepted`, and this spec moves to `Approved`. Per
  [AGENTS.md](../AGENTS.md), a Plan may now be drafted against this
  spec; **this approval is not itself an authorization to implement** —
  that future Plan must still pass its own (or a joint Spec+Plan) Human
  Review before any code, test, shader, or build-configuration file for
  this spec's scope is written.
- **Independent Review — Round 1 (2026-08-16):** An independent,
  read-only review of this Spec's first draft and ADR-0041/ADR-0042
  found four Must Fix issues — a self-referential "commit hash" field in
  golden provenance (a commit's hash cannot be known before that
  commit's own content is finalized); ADR-0041's "pinned to a tagged
  commit" claim not matching `nothings/stb`'s actual upstream repository
  (verified: zero git tags, zero GitHub Releases); the channel-tolerance/
  failing-pixel-budget rule resting on an unverified placeholder value
  with no calibration evidence; and undefined behavior when the
  comparison suite runs on hardware/drivers other than the one recorded
  in a golden's provenance — plus six Should Fix issues (missing
  `stb_image`/`stb_image_write` implementation-macro, forced-channel-
  decode, and no-vertical-flip contract details; an underspecified
  golden-regeneration safety boundary; a missing golden-checksum
  evaluation; undifferentiated golden-update-reason categorization).
  This revision addresses all ten: golden provenance now records a
  clean-working-tree **source revision** with an explicit same-PR
  ordering rule (rendering change committed first, golden added in a
  separate, later commit); ADR-0041's stb pin wording is corrected and
  its full usage contract (implementation macros, forced 4-channel
  decode, no-flip contract, PNG color-profile-chunk non-use, license/
  offline-build implications) is now specified; a real **empirical
  calibration** was performed on the reference Windows/Vulkan GPU (42
  captures total — 21 Debug, 21 Release, each spanning 7 independent
  process launches × 3 in-process cycles — against Spec 0010's existing,
  unmodified fixture) and found **zero** differing pixels at any
  threshold in every comparison, confirming channel tolerance = 0 and
  failing-pixel budget = 0 as evidence-backed values, not placeholders,
  for that fixture and reference GPU/driver; non-reference-environment
  behavior is now fully specified (the comparison always runs, never
  skips, with a provenance mismatch reported as a separate, explicit
  diagnostic, never conflated with pass/fail); golden regeneration is
  now locked to a distinct, non-CTest-registered tool, with ordinary
  comparison tests read-only by design; golden-update PRs must now
  categorize their reason (rendering change / reference-environment
  change / approved rebaseline); and a golden PNG checksum was
  evaluated and explicitly not adopted (no existing SHA-256 capability
  in this repository; would require a new dependency to add one — see
  ADR-0042's own "Golden PNG checksum" decision). See
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  and
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  for the full corrected/completed text. **This Spec has not yet
  received Human Review Approval** — `In Review` reflects that an
  independent review round has occurred and been acted on, not that a
  human has signed off.
- **Independent Review — Round 2 (2026-08-16):** A second independent,
  read-only review, focused on re-verifying Round 1's fixes against
  primary evidence rather than trusting the prior summary (independently
  re-ran the calibration comparator, independently hashed all 42 raw
  captures, independently re-fetched/re-checked `nothings/stb`'s
  tags/releases/license/source), found **no Must Fix issues** and three
  Should Fix issues, all addressed in this revision: (1) the sidecar's
  "Vulkan instance/loader and device API version" field was one vague
  phrase covering three values this codebase's own Vulkan Backend
  already computes distinctly (loader-reported version, requested
  instance version, selected physical device `apiVersion`) — now three
  separate, named fields, never concatenated; (2) a golden's own PNG
  could disagree with its own sidecar (or fail `stb`'s
  `channels_in_file`/bit-depth metadata check despite the forced-4-
  channel output buffer looking fine) with no distinct failure mode —
  now a dedicated, four-step **golden validity check** that runs first
  and reports `INVALID GOLDEN`, never downgraded by tolerance and never
  conflated with a regression failure or a provenance mismatch; (3) the
  sidecar's "JSON is the working assumption" left its parsing
  implementation dependency unconstrained — now bounded: no new parsing
  dependency without its own Spec/ADR, no reuse of
  `src/shader_system/`'s private JSON parser, a dependency-free flat
  text format as the default, and a stable field order plus an explicit
  version marker required of whatever encoding the Plan picks. See
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  and
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  for the full corrected/completed text.
- **Related Plan(s):**
  [plans/0011-image-regression-testing-foundation.md](../plans/0011-image-regression-testing-foundation.md)
  (`Draft`, drafted against this Spec's own `Approved` status, per
  [AGENTS.md](../AGENTS.md)). Drafting this Plan is not itself an
  Implementation authorization — that Plan must still pass its own (or
  a joint Spec+Plan) Human Review first.
- **Related ADR(s):**
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  (golden image data format and codec dependency, `Accepted`),
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  (comparison methodology, golden provenance, and test ownership
  boundary, `Accepted`) — both accepted alongside this spec's Human
  Review Approval above. Builds on
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)–[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)
  (all `Accepted`, Spec 0010) and
  [ADR-0006](../adr/0006-dependency-management.md)/[ADR-0007](../adr/0007-test-framework.md)
  (both `Accepted`).

## Summary

This spec introduces Image Regression Testing: a golden-image comparison
harness built entirely on top of Spec 0010's (`Approved`, implemented)
headless render-and-readback foundation. It reuses the exact same
`Renderer` → RenderGraph → RHI → Vulkan Backend stack and
`OffscreenTarget`/readback path unchanged, adds no new rendering
feature, and touches no RHI/Renderer/RenderGraph public API. Its entire
contribution is test-support infrastructure: a way to persist a
human-reviewed reference ("golden") image, capture a fresh one from the
same fixed scene, compare the two with an unambiguous, hard-to-fool
per-pixel rule, and produce actionable diagnostics on failure. It also
defines, precisely, what "gating" means today (a local/manual run
against real hardware) versus what remains blocked on infrastructure
this spec does not and cannot decide (automated CI enforcement).

## Motivation / Problem Statement

[AGENTS.md](../AGENTS.md)'s Phase 1 constraints state: "Headless
rendering and image regression testing follow once the windowed/
swapchain path works — they are still Phase 1 scope, not deferred to a
future phase." Headless rendering is now genuinely working: Spec 0010 is
`Approved` and implemented, merged via
[PR #48](https://github.com/slmao/Atlantis/pull/48), with a real
render → readback cycle verified on real Windows/Vulkan hardware.
[specs/README.md](README.md)'s Candidate Spec Backlog lists Image
Regression Testing as depending only on Spec 0010, already satisfied.

Three genuine gaps stand between "a frame can be rendered headlessly and
read back" and "a rendering regression is automatically caught," none of
which Spec 0010 resolves — its own Non-Goals explicitly named all three
as this spec's future scope:

- **No golden-image storage, provenance, or update-approval workflow
  exists.** There is no format, no location, no versioning convention,
  and no rule preventing a test from silently overwriting a reference
  image.
- **No comparison algorithm or tolerance methodology exists.** Nothing
  in this repository today can say "this captured image is (or is not)
  the same as that one, within an acceptable, evidence-based margin,"
  or explain *why* it differs when it does.
- **No demonstrated proof that a real rendering regression is actually
  caught.** [docs/process/definition-of-done.md](../docs/process/definition-of-done.md)
  already requires image regression tests for any change to rendered
  output — that requirement is currently unenforceable because nothing
  implements it.

A fourth question, procedural rather than architectural but requiring an
explicit, reviewed answer rather than a silent default: whether this
spec's own scope forces CI-enforced automated gating to be designed and
implemented now. **It does not, and this spec does not attempt it.** No
CI pipeline exists in this repository (no `.github/workflows/`
directory), and
[docs/process/ci-strategy.md](../docs/process/ci-strategy.md) already
logs, as still-open, exactly the prerequisites automated gating would
need: a decided GPU-in-CI approach (software Vulkan implementation vs. a
self-hosted real-GPU Windows runner) and a decided dependency-fetch-in-CI
strategy. Neither is this spec's to decide — `ci-strategy.md` is explicit
that writing CI configuration "depends on build-system decisions ...
that haven't been specced yet," and doing so anyway "would be exactly
the kind of uncontrolled architectural decision AGENTS.md prohibits."
This spec instead delivers the part that *is* real and useful without
that infrastructure: a working local/manual gate, runnable today by a
human or agent against real hardware, exactly as every other
GPU-touching verification in this project already works absent CI (see
Spec 0010's own "Manual verification record"). See Non-Goals and Risks &
Open Questions.

## Goals

- Build a golden-image comparison harness on top of Spec 0010's
  unmodified `Renderer` → RenderGraph → RHI → Vulkan Backend →
  `OffscreenTarget`/readback path — no rendering-side change of any
  kind.
- Reuse Spec 0010's own existing, already-verified
  `examples/headless_rendering_demo` fixture (fixed cube, fixed camera,
  fixed material, fixed 512×512 `Rgba8Unorm` `OffscreenTarget`, fixed
  background clear color) as the first golden-image target scene, rather
  than authoring a new one.
- Define golden image data format, storage location, naming, provenance,
  and a human-reviewed update workflow that a test or CI run can never
  bypass — see
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  and
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
- Define a comparison algorithm and tolerance methodology precise enough
  that it cannot be trivially satisfied by, e.g., "the image is not
  empty" — every pixel of a captured image must match its golden within
  an explicit, evidence-based tolerance, with no aggregate pass-rate
  slack in this Phase 1 design (see ADR-0042).
- On failure, produce actionable diagnostics: pass/fail, per-channel
  difference metrics, the actual captured image, and a diff image — not
  merely a boolean.
- **Require, as part of this spec's own Testing & Verification Plan
  (executed during the future Plan/Implementation stage, not by this
  Spec document itself), a demonstrated proof that a deliberately
  introduced rendering regression is actually caught** — a temporarily
  altered scene input must produce a failing comparison with the
  expected diagnostics, then be reverted.
- Fix golden images' and comparison logic's ownership under `tests/` —
  a new test-suite area, not a new top-level engine module — with
  GPU-independent comparison logic layered separately from GPU-required
  capture-and-compare tests, per
  [testing-strategy.md](../docs/process/testing-strategy.md).
- State precisely what CI gating means today (local/manual) versus what
  remains blocked on undecided infrastructure (automated enforcement),
  without fabricating that infrastructure — see Non-Goals.
- Identify and file the minimum number of new ADRs this spec's decisions
  require, and make the minimal, status-driven update to
  [specs/README.md](README.md)'s registry that promoting a Candidate
  Backlog entry to a real spec number requires.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Any change to `Renderer`'s output or its public rendering API.**
  This spec consumes Spec 0010's existing `OffscreenTarget`/readback
  capability exactly as shipped; it adds no RHI, RenderGraph, or
  Renderer type, method, or parameter.
- **Any new rendering feature** (anti-aliasing, transparency/blending,
  additional materials, additional scenes beyond the one reused
  fixture, multisampling, etc.). A future scene or rendering-feature
  addition is a separate spec's own scope; this spec only proves the
  comparison harness against the one existing, already-verified
  fixture.
- **Android, iOS, or Linux.** Windows/Vulkan only, matching Spec 0010
  and [AGENTS.md](../AGENTS.md); Linux is not a target platform for
  Atlantis under any circumstance.
- **A general screenshot system, editor visual-testing platform, or any
  capability beyond this project's own headless render-and-readback
  regression suite.** This is not a general-purpose visual testing tool.
- **Video, temporal, or multi-frame-sequence regression testing.** Every
  comparison in this spec's scope is a single still-image comparison
  against a single golden.
- **Performance benchmarking of any kind.** This spec measures pixel
  correctness, never frame time, throughput, or any other performance
  metric.
- **The Atlantis Runtime module or any runtime asset system.** This
  spec's fixture is a fixed, checked-in constant, exactly like Spec
  0007's and Spec 0010's own fixtures — no asset loading, no asset GUID/
  metadata model.
- **Automatic acceptance or automatic update of any golden image, by a
  test run, a CI job, or any other automated process, under any
  circumstance.** A golden image changes only through a human-reviewed
  PR diff — see [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
- **Multiple frames in flight, asynchronous/non-blocking readback, or
  any change to Spec 0010's existing single-frame, synchronous
  `waitIdle()`-based readback model.** This spec captures and compares
  single, synchronously-read frames exactly as Spec 0010 already
  produces them.
- **Pre-designing a cross-graphics-API baseline/comparison framework for
  a future second backend.** Phase 1 is Vulkan-only
  ([AGENTS.md](../AGENTS.md)); this spec's comparison methodology is not
  designed with, or validated against, any non-Vulkan backend in mind.
- **Automated CI enforcement of the comparison harness.** This spec
  defines and delivers a working **local/manual** gate (a human or agent
  runs the suite against real Windows/Vulkan hardware and reports
  pass/fail) and defines what a future CI job's artifact contract would
  look like — it does not implement, provision, or assume the existence
  of any actual CI pipeline, GPU-in-CI strategy, or CI dependency-fetch
  mechanism, none of which exist or are decided in this repository
  today. See Motivation and Risks & Open Questions.
- **Per-GPU-vendor golden image sets, or any claim of cross-vendor
  rendering stability.** Only one real GPU vendor (Intel Arc, integrated)
  has ever been available to verify against in this project's
  environment; this spec's one, unified golden set is scoped to that
  vendor/family only, disclosed explicitly, not overstated.
- **Percentage-based, SSIM, or any other perceptual-similarity
  comparison metric.** See
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  Alternatives Considered for why a strict, all-pixels-must-pass rule is
  chosen instead, for this project's current, static, non-antialiased
  scenes.
- **A new top-level engine module.** Image Regression Testing is test
  infrastructure, owned under `tests/image_regression/`, not a module in
  [AGENTS.md](../AGENTS.md)'s module list.
- **Editing [specs/README.md](README.md)'s Section A entries for prior
  specs, [docs/project-blueprint.md](../docs/project-blueprint.md), or
  any other governance/roadmap document beyond this spec's own required,
  minimal backlog-registry promotion** (Candidate Backlog → Spec
  Registry, per that registry's own maintenance rules).

## Requirements

### Functional

**Golden image storage, naming, and provenance**

- Golden images are stored as PNG, 8-bit-per-channel RGBA, at
  `tests/image_regression/goldens/<scene-slug>/<golden-name>.png`. Full
  format/dependency decision in
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md);
  full location/provenance/update-workflow decision in
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
- Each golden PNG is accompanied by a sidecar file recording its
  provenance: capture date; **source revision** (`git rev-parse HEAD`
  as run at capture time against a **clean working tree** — never the
  hash of the commit the golden itself is being added in, which cannot
  be known until that commit's own content is finalized); GPU
  vendor/model; driver version; OS build; **three separate Vulkan
  version fields — loader-reported API version, requested instance API
  version, and selected physical device `apiVersion` — never
  concatenated into one combined value** (each already computed as a
  distinct quantity by this codebase's own Vulkan Backend; see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  for exactly where); and the exact `OffscreenTarget` extent/format
  used. Sidecar encoding is bounded, not a blank check for Plan: no new
  parsing/serialization dependency without its own Spec/ADR/Human
  Review, no reuse of `src/shader_system/`'s private JSON parser or any
  other dependency of `tests/image_regression/` on Shader System's
  private implementation, a simple dependency-free flat text format as
  the default (not general-purpose JSON), and a stable field order plus
  an explicit format/version marker — full bounds in
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "Sidecar encoding and parsing."
- When a golden is added or updated in the same PR as the rendering
  change that motivates it, the rendering change must be **committed
  first**; the golden is captured against that already-existing commit
  and added via a **separate, subsequent commit** recording that
  commit's hash as its source revision.
- No golden's PNG checksum is recorded in its sidecar — considered and
  explicitly not adopted; see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "Golden PNG checksum" decision (no existing SHA-256 capability in
  this repository; adding one would itself be a new dependency).
- **A golden must pass a validity check before it is ever compared
  against a captured buffer:** the PNG file exists and decodes; its
  actual decoded properties (width, height, the decoder's own
  `channels_in_file` metadata — not just the forced-4-channel output
  buffer — and confirmed 8-bit depth) satisfy this project's RGBA8
  contract; its own sidecar's recorded format/extent matches those
  actual decoded properties; and the sidecar found alongside a PNG is
  really the one for that PNG. Any failure here is reported as a
  distinct **`INVALID GOLDEN`** outcome — never downgraded by channel
  tolerance, never conflated with a provenance mismatch, and never
  reported as an ordinary regression failure (a broken golden is a
  defect in the committed test artifact, not evidence about the code
  under test). Full four-step check in
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "Golden validity check"; the channel-metadata mechanism in
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)'s
  "Decode channel contract."
- The comparison suite has no `skip` outcome for non-reference hardware/
  drivers. Once a golden is confirmed valid, the suite always runs the
  real pixel comparison; if the current device's vendor/model/driver/OS
  build or any of the three Vulkan version fields does not match a
  golden's recorded provenance, a separate, prominent
  `PROVENANCE MISMATCH` diagnostic — naming which specific field(s)
  differ — is emitted alongside (never merged into) the ordinary
  pass/fail result. A pass obtained under a provenance mismatch must
  never be reported as reference-environment verification evidence.
  Full decision in
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
- Golden regeneration is performed **only** by a distinct, standalone
  developer tool/entry point that is **never registered with CTest**
  (never part of `ctest -L gpu` or any default build target). The
  ordinary comparison test binary(ies) are read-only with respect to
  golden files — no environment variable, command-line flag, or default
  code path reachable from an ordinary test run may write to
  `tests/image_regression/goldens/`. A golden's PNG and sidecar are only
  ever changed by a dedicated, human-reviewed PR.
- Every PR touching a file under `tests/image_regression/goldens/` must
  categorize its reason as one of: **rendering change** (an intentional
  output change, identified in the PR); **reference-environment change**
  (the reference machine's driver/OS/Vulkan runtime changed — requires
  before/after provenance and diff evidence, not just an assertion that
  no regression is believed present); or **approved rebaseline** (a
  deliberate, human-approved exception, same evidentiary bar as a
  reference-environment change). A reference-environment-triggered
  update must never be described using the same language as an ordinary
  rendering-change update.
- The first golden for the reused `examples/headless_rendering_demo`
  fixture must itself be captured once, reviewed by a human as "this is
  what correct output looks like today," and committed through the same
  reviewed-PR-diff workflow as any later update — standing up the
  harness is not an exception to the update-approval rule.

**Comparison algorithm**

- Per-pixel, per-channel absolute byte difference between a freshly
  captured buffer and its golden, both in the identical
  `atlantis::rhi::Format` and extent — a format or extent mismatch is a
  hard failure, never silently resized or reformatted. Full algorithm
  and tolerance decision in
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
- Two separate parameters, not one vague "tolerance": **channel
  tolerance** (max allowed absolute per-channel difference for one pixel
  to pass) and **failing-pixel budget** (max allowed count/proportion of
  pixels that may exceed channel tolerance). **Both are 0** — every
  channel of every pixel must match exactly. This is a confirmed value,
  backed by empirical calibration (42 captures — 21 Debug, 21 Release,
  each spanning 7 independent process launches × 3 in-process cycles —
  against the reused fixture on the one reference GPU/driver, zero
  differing pixels at any threshold in every comparison; see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own Context and Decision for the full methodology and evidence), not
  an unverified placeholder.
- A failing comparison must produce: pass/fail; per-channel and combined
  max/mean absolute difference; the count and percentage of
  out-of-tolerance pixels; the actual captured image, saved as its own
  PNG; and a diff image (per-pixel absolute difference, amplified for
  visibility) — all written to a predictable, documented build-output
  location, exact path left to the Plan.

**Reproducibility**

- Any scene covered by this spec's comparison harness must have no
  wall-clock time, RNG, or uninitialized-memory dependence in its
  camera, transform, or material state — every input its output depends
  on must be a fixed, checked-in constant.
- This spec's first covered scene is Spec 0010's existing
  `examples/headless_rendering_demo` fixture, reused unchanged (exact
  code-sharing mechanism — shared fixture vs. duplicated, matching Spec
  0010's own precedent for its relationship to Spec 0007's fixture — left
  to the Plan).
- Determinism was verified empirically, not assumed: 21 captures each
  (Debug and Release) of the reused fixture's render-and-readback cycle,
  spanning both same-process repetition and independent fresh-process
  launches, produced byte-identical results in every case — see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  for the full calibration methodology and evidence. This determinism
  evidence is scoped to the fixture and reference GPU/driver actually
  calibrated against; a future scene, driver upgrade, or different
  hardware is not covered by it and would need its own evidence.
- All real-hardware verification in this spec's scope is understood to
  run on a single GPU vendor/driver (Intel Arc, integrated) — this must
  never be described or reported as cross-vendor stability evidence. One
  unified golden set is used, scoped to that vendor/family; see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).

**Data format and dependencies**

- PNG via `stb_image`/`stb_image_write`, fetched by CMake `FetchContent`
  pinned to a specific, full commit hash (`nothings/stb` has no tagged
  releases — verified, zero git tags and zero GitHub Releases exist on
  that repository), linked only into
  `tests/image_regression/`'s own test-support targets. Full decision,
  alternatives, and licensing/build/maintenance analysis, plus the full
  usage contract (implementation-macro single-translation-unit rule,
  forced 4-channel decode **plus an explicit check of the decoder's own
  `channels_in_file`/bit-depth metadata**, not the forced-4-channel
  output buffer alone, no vertical-flip calls on either side, no PNG
  color-profile chunks written or interpreted), in
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md).
- No dependency introduced by this spec is linked into, or referenced
  by, any `src/` module.

**CI gating (scope boundary)**

- This spec defines and its future implementation must deliver a
  **local/manual gate**: a human or agent runs the `gpu`-labeled image
  regression suite against real Windows/Vulkan hardware and reports
  pass/fail — usable today, with no CI infrastructure required.
- This spec does **not** implement, provision, or design a specific
  automated CI pipeline for this gate. It documents the artifact
  contract (see "Comparison algorithm" above) a future CI job would
  need once the prerequisites
  [ci-strategy.md](../docs/process/ci-strategy.md) already logs as open
  (GPU-in-CI approach; dependency-fetch-in-CI strategy) are resolved by
  their own future build-system/CI spec, and once that infrastructure is
  actually provisioned — neither of which this spec resolves or
  fabricates.
- Any PR touching a file under `tests/image_regression/goldens/` must
  state, in its own description, why the golden changed. Enforcing this
  today is a human-review/process rule, not a technical control — no
  CI or branch-protection/CODEOWNERS mechanism exists yet to enforce it
  automatically.

**Test/module ownership**

- All new code and data this spec introduces lives under
  `tests/image_regression/` — a new test-suite area parallel to
  `tests/core/`, `tests/rhi/`, `tests/render_graph/`,
  `tests/renderer/`, `tests/vulkan_backend/`, **not a new top-level
  `src/` module** and not an addition to
  [AGENTS.md](../AGENTS.md)'s module list. Full boundary decision in
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
- GPU-independent logic (pixel-diff/tolerance algorithm, PNG
  encode/decode round-trip correctness, provenance-sidecar parsing) is
  layered separately from GPU-required tests (real
  `OffscreenTarget`-backed capture and comparison against a checked-in
  golden), per
  [testing-strategy.md](../docs/process/testing-strategy.md)'s existing
  layer split. GPU-required tests use the existing `gpu` ctest label,
  introducing no new CI/test-category label.
- No existing module's public dependency set changes as a result of
  this spec.

### Non-functional

- **Performance:** not a goal beyond "does not stall or leak
  unnecessarily" — PNG encode/decode cost at 512×512 is not evaluated as
  a performance concern; no performance claim or benchmark is made or
  required.
- **Memory:** no new allocation strategy. Captured and golden buffers
  are loaded fully into ordinary heap memory for comparison, consistent
  with Phase 1's existing "no general allocator" posture; no streaming
  or partial-load design is introduced.
- **Portability (within the Vulkan-only Phase 1 constraint):** Windows
  only, using the one real GPU vendor available to this project (Intel
  Arc, integrated) — no cross-vendor or cross-platform claim is made.
- **Other:** no new dependency beyond `stb_image`/`stb_image_write` (see
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)).
  GPU-independent unit tests use the existing Catch2 v3 framework
  ([ADR-0007](../adr/0007-test-framework.md)); no new test framework is
  introduced.

## Proposed Design

### Module boundaries (additive test infrastructure, no engine change)

This spec adds no new node to the existing module graph
([AGENTS.md](../AGENTS.md) module boundaries): `Renderer`, RenderGraph,
RHI, and Vulkan Backend are consumed exactly as Spec 0010 already ships
them, with zero modification. The only new boundary is a test-suite
area, `tests/image_regression/`, depending on the existing RHI/
Vulkan Backend headless path (for GPU-required capture) and on the new
`stb`-based codec dependency (for PNG encode/decode), consumed
privately, never exposed to any `src/` module.

```
Image regression capture-and-compare cycle, conceptually:

  [Exactly Spec 0010's own headless composition, unchanged]
  Device::createOffscreenTarget(...) -> OffscreenTarget
  ... acquire -> Renderer::drawFrame(finalColorState = TransferSource)
  ... caller-built copy-pass execute() -> Device::submit() -> waitIdle()
  readbackBuffer now holds the captured frame (Spec 0010, unchanged)

  [New in this spec]
  Load golden PNG + provenance sidecar for this scene
    (tests/image_regression/goldens/<scene-slug>/<golden-name>.png)
  Golden validity check (runs first, always -- ADR-0042):
    PNG missing or undecodable, OR decoded channels_in_file != 4,
    OR PNG is 16-bit, OR sidecar's format/extent != PNG's own actual
    decoded format/extent, OR sidecar/PNG pairing mismatch
      -> INVALID GOLDEN (distinct outcome; never reached the checks
         below; never downgraded by tolerance or provenance mismatch)
  IF golden is valid:
    Read current device's GPU vendor/model/driver/OS build and the
      three Vulkan version fields (loader/instance/physical-device);
      compare against golden's sidecar provenance
      -> on mismatch: emit PROVENANCE MISMATCH (names which field(s)
         differ), reported separately from, never merged into, the
         pass/fail result below
    IF format/extent of golden != format/extent of capture:
      FAIL (hard mismatch, never resized/reformatted -- distinct from
        an INVALID GOLDEN outcome, which is about the golden's own
        internal consistency, not the capture)
    ELSE:
      Per-pixel, per-channel |captured - golden| <= channel tolerance ?
        ALL pixels pass -> PASS
        ANY pixel fails (failing-pixel budget exceeded) -> FAIL:
          write actual-captured PNG, write diff-image PNG,
          report per-channel max/mean diff and out-of-tolerance
          pixel count/percentage, at a documented build-output path
```

### Golden image storage, provenance, and update workflow

See
[ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
for the data-format/dependency decision, and
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
for location, provenance-sidecar content, and the human-reviewed update
workflow's full decision and rationale.

### Comparison algorithm and tolerance

See
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
for the full decision: the per-pixel comparison rule, the confirmed
channel tolerance and failing-pixel budget (both 0, backed by empirical
calibration — see ADR-0042's own Context), the failure-output contract,
and why a percentage-based or perceptual metric is rejected for Phase 1.

### Reproducibility strategy

See
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
for the full decision: reuse of Spec 0010's existing fixture, the
reproducibility constraints imposed on any covered scene, the
repeated-capture empirical-determinism verification requirement, and the
single-GPU-vendor, unified-golden-set strategy (with its explicit,
disclosed scope limitation).

### Test/module ownership and layering

See
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
for the full decision: the `tests/image_regression/` boundary, why it is
not a new top-level module, and the GPU-independent/GPU-required layer
split.

### CI gating scope

See Motivation, Non-Goals, and Requirements above: this spec delivers a
real, usable local/manual gate today, and precisely documents — without
implementing or fabricating — what a future CI job would need once
[ci-strategy.md](../docs/process/ci-strategy.md)'s own already-open
prerequisites are resolved by a separate, future build-system/CI spec.

### Threading

Single logical thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md) — unchanged from
Spec 0010; this spec introduces no concurrency of any kind. Comparison
and PNG encode/decode run synchronously on the same thread that performs
the capture.

### Error handling

- Recoverable runtime errors (golden file missing/unreadable, PNG
  decode failure, provenance sidecar malformed, format/extent mismatch
  between actual and golden) use `atlantis::Result<T, E>`, consistent
  with every prior spec's convention — a missing or malformed golden is
  a reported test failure with a clear diagnostic, never a crash or a
  silent pass.
- Every `VkResult` along any GPU-touching capture path this spec's tests
  perform is checked, exactly per Spec 0010's own unchanged contract.
- Vulkan Validation Layers are enabled unconditionally for every
  GPU-required test this spec adds; a validation warning or error is a
  test failure, not advisory output.

## Architectural Impact

This spec introduces exactly two new, independently-reviewable
decisions — both drafted alongside this spec, both now `Accepted`
alongside this spec's own Human Review Approval above:

1. **Golden image data format and codec dependency** — PNG via
   `stb_image`/`stb_image_write`, fetched through `FetchContent`, linked
   only into test-support targets. This is a new third-party dependency,
   which [AGENTS.md](../AGENTS.md) explicitly requires a Spec → ADR →
   Human Review path for. Filed as
   [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md).
2. **Comparison methodology, golden provenance, and test ownership
   boundary** — the per-pixel comparison algorithm and tolerance policy,
   golden storage/provenance/update-workflow, the single-GPU-vendor
   golden-set strategy, and the new `tests/image_regression/` test-suite
   area (a repository-structure addition, per
   [AGENTS.md](../AGENTS.md)'s "Anything that changes ... repository
   structure" significance rule). Filed as
   [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).

**No RHI, RenderGraph, or Renderer public API is touched.** No new
top-level module is introduced —
[AGENTS.md](../AGENTS.md)'s module list (Atlantis Core, Platform, RHI,
Vulkan Backend, RenderGraph, Renderer, Shader System, Runtime, Tools)
is unchanged by this spec. **No CI/build-system decision is made by
this spec** — automated gating remains explicitly deferred to whatever
future build-system/CI spec resolves
[ci-strategy.md](../docs/process/ci-strategy.md)'s own already-logged
open questions; this spec neither decides nor assumes an answer to them.

**This spec's approval is not itself an authorization to implement** —
per [AGENTS.md](../AGENTS.md), a Plan may be drafted once this spec and
both ADRs above reach `Approved`/`Accepted`, and that future Plan must
still pass its own (or a joint Spec+Plan) Human Review before any code,
test, or build-configuration file for this spec's scope is written.

## Alternatives Considered

- **Design and implement CI-enforced automatic gating in this same
  spec.** Rejected: automated gating depends on undecided build-system/
  CI infrastructure (a GPU-in-CI approach, a dependency-fetch-in-CI
  strategy) that
  [ci-strategy.md](../docs/process/ci-strategy.md) explicitly reserves
  for its own, separate, not-yet-drafted spec. Bundling it in here would
  either block this spec indefinitely on decisions outside its control,
  or force exactly the kind of fabricated, uncontrolled architectural
  assumption [AGENTS.md](../AGENTS.md)'s Golden Rule prohibits.
- **Defer the golden image format/dependency decision to the Plan stage
  instead of deciding it (with an ADR) in this Spec.** Rejected:
  [AGENTS.md](../AGENTS.md) requires a new-dependency decision to go
  through Spec → ADR → Human Review before a Plan exists, not be picked
  ad hoc during implementation planning.
- **Design per-GPU-vendor golden sets now, anticipating future
  multi-vendor CI.** Rejected: no second GPU vendor exists in this
  project's environment to validate or even meaningfully design such a
  strategy against; speculative, contrary to
  [AGENTS.md](../AGENTS.md)'s "no speculative abstraction" architecture
  principle. See
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  Alternatives Considered.
- **Adopt a percentage-based or perceptual (SSIM) tolerance now, to
  reduce expected flakiness risk.** Rejected — see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  Alternatives Considered: for this project's current static,
  non-antialiased scenes, such slack risks masking a real, small,
  localized regression, which is precisely what this harness exists to
  catch.
- **Extend Spec 0010's own "basic content check" instead of building a
  real golden-image comparison.** Rejected: Spec 0010's check was
  deliberately weak and explicitly not a golden-image comparison, by
  its own stated design (see Spec 0010 Non-Goals) — this spec's entire
  purpose is the real, evidence-based comparison Spec 0010 explicitly
  deferred to it.
- **Silently amend [specs/README.md](README.md)'s Candidate Spec
  Backlog ordering or numbering beyond the single, mechanical promotion
  this spec's own drafting requires.** Rejected: per
  [AGENTS.md](../AGENTS.md) and this registry's own maintenance rules,
  governance/roadmap documents change only through their own review or
  explicit, minimal, status-driven registry maintenance — this spec's PR
  makes only the mechanical Candidate Backlog → Spec Registry promotion
  and Candidate Order renumbering that promotion requires.

## Testing & Verification Plan

This section states what the future Plan/Implementation for this spec
must verify — this Spec document itself introduces no code and performs
no verification.

- **Unit tests (GPU-independent):** the pixel-diff algorithm (channel
  tolerance and failing-pixel budget, both fixed at 0 — see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md))
  exercised against synthetic in-memory buffers (not real GPU captures),
  covering at minimum:
  - Two identical buffers compare equal (pass).
  - A single differing pixel anywhere in the buffer fails the comparison
    — confirming the failing-pixel budget is genuinely 0, not merely
    small.
  - A format or extent mismatch between actual and golden is reported as
    a hard failure, distinct from a content mismatch.
  - Failure output contains correct per-channel max/mean diff and
    out-of-tolerance pixel count/percentage for a buffer with a known,
    constructed set of differing pixels.
  - PNG encode-then-decode round-trips a buffer byte-for-byte (lossless
    round trip, confirming ADR-0041's no-added-quantization claim).
  - Provenance sidecar parsing succeeds for a well-formed sidecar and
    fails clearly for a malformed one; a provenance mismatch (current
    device vs. sidecar, on any one or more of the three Vulkan version
    fields, GPU vendor/model, driver version, or OS build) produces the
    separate diagnostic ADR-0042 requires, naming the specific
    field(s), distinguishable from the pixel-comparison pass/fail
    result.
  - **Golden validity check, each of its four steps, exercised
    independently against constructed test PNGs/sidecars (no Vulkan
    device needed):** a missing/undecodable PNG; a PNG whose decoder-
    reported `channels_in_file` is not 4 (e.g. a real RGB or grayscale
    PNG) even though the forced-4-channel output buffer looks fine; a
    16-bit-depth PNG; a sidecar whose recorded format/extent does not
    match the PNG's own actual decoded properties; and a sidecar/PNG
    pairing mismatch. Each produces the distinct `INVALID GOLDEN`
    outcome — never the ordinary pass/fail result, never downgraded by
    channel tolerance, never conflated with a provenance mismatch.
- **GPU integration tests (Windows/Vulkan, `gpu`-labeled):**
  - A full capture-via-`OffscreenTarget` cycle against the reused
    `examples/headless_rendering_demo` fixture, compared against its own
    committed golden, passes with Vulkan Validation Layers clean.
  - The same cycle repeated multiple times against the same
    `OffscreenTarget` instance produces byte-identical captures — the
    automated, permanent form of the empirical determinism verification
    this spec's Reproducibility requirements demand. (An ad hoc,
    uncommitted version of this check was already performed once during
    this Spec's own review, as calibration evidence for the tolerance
    decision — see ADR-0042's Context — but that was one-off evidence
    gathering, not a standing test; this bullet is the permanent,
    Plan/Implementation-stage test that must exist going forward.)
  - **A deliberately introduced rendering regression is caught:** a
    temporary, reverted-before-merge change to the fixture (e.g. an
    altered vertex color, an altered clear color, or an altered camera
    position) produces a failing comparison with the expected
    diagnostics (non-zero out-of-tolerance pixel count, a non-trivial
    diff image) — proving the harness actually detects a real
    regression, not merely that it can report "PASSED" against
    unchanged input. This check must be performed and its evidence
    recorded during Implementation; the regression is reverted
    immediately afterward and must not ship.
  - A missing golden file produces `INVALID GOLDEN`, not a crash and not
    a silent pass; a captured buffer whose format/extent genuinely
    differs from a *valid* golden's recorded format/extent (the
    actual-vs-golden case, distinct from a golden's own internal
    PNG-vs-sidecar inconsistency, which is `INVALID GOLDEN` instead —
    see the unit-test bullet above) produces the ordinary hard-failure
    comparison result `INVALID GOLDEN` is never confused with.
  - Running against a golden whose recorded provenance does not match
    the current device produces the pixel-comparison result as normal,
    plus the separate `PROVENANCE MISMATCH` diagnostic naming the
    specific field(s) that differ — and that diagnostic is never
    allowed to suppress or replace the pass/fail result.
- **Vulkan Validation Layers:** mandatory, must run clean for every
  manual and automated exercise of the GPU-required capture path,
  exactly as every prior spec in this project already requires.
- **Manual/local verification:** a human or agent runs the full
  `gpu`-labeled image regression suite against real Windows/Vulkan
  hardware and records: hardware/driver/Vulkan version used (matching
  Spec 0010's own disclosure format), pass/fail per test, and
  confirmation that the deliberate-regression check above was performed
  and reverted. This is this spec's real, working gate, usable
  immediately, independent of any CI infrastructure.
- **Not applicable / explicitly out of scope for this spec's own
  verification:** any CI-automated run of the above (see Non-Goals); any
  cross-GPU-vendor run (no second vendor available).

## Risks & Open Questions

- **Resolved by empirical calibration** (see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  Context/Decision): channel tolerance and failing-pixel budget are both
  confirmed at 0 for the reused cube fixture's silhouette/edge pixels
  included — 21 Debug and 21 Release captures, spanning both same-process
  repetition and independent fresh-process launches, produced zero
  differing pixels at any threshold. This resolves what was previously
  an open, unverified prediction about edge-pixel flakiness for this
  fixture on this reference GPU/driver. **Not resolved, and still
  genuinely open:** whether a future driver upgrade or different
  hardware would preserve this result — no evidence exists either way,
  and none can exist until such a change actually occurs; a real
  regression in that future evidence is a design gap requiring a spec
  revision or follow-up spec, not a silent, undocumented tolerance
  loosening during implementation.
- **Exact provenance-sidecar text encoding syntax** (a specific flat
  `key: value` scheme vs. some other simple, dependency-free layout) is
  left to the Plan — but bounded, not open-ended: no new parsing
  dependency without its own Spec/ADR, no reuse of
  `src/shader_system/`'s private JSON parser, a dependency-free default,
  and a stable field order plus an explicit version marker, all fixed by
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own "Sidecar encoding and parsing." Every provenance *field* itself
  (including "source revision" and the three separate Vulkan version
  fields) is likewise fixed by ADR-0042, not open.
- **Exact name and CMake target shape of the golden-regeneration
  tool** is left to the Plan; its architecture (a distinct,
  never-CTest-registered entry point, unreachable from the ordinary
  comparison test binary by any environment variable, flag, or default
  code path) is fixed by
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md),
  not open.
- **Whether a distinct CI/test-category label for headless GPU
  integration tests is needed**, separate from the existing
  `gpu`-labeled pattern — the same open question Spec 0006 and Spec 0010
  already flagged, now recurring here with a second consumer (image
  regression GPU tests); flagged, not resolved.
- **Automated CI enforcement of this spec's gate remains blocked on
  prerequisites outside this spec's control** —
  [ci-strategy.md](../docs/process/ci-strategy.md)'s own already-open
  GPU-in-CI-approach and dependency-fetch-in-CI-strategy questions, plus
  actual infrastructure provisioning (a human/ops action). This is a
  genuine external blocker for the *automated* half of "gating," not a
  Plan-stage detail this spec can resolve by writing more design — it is
  called out explicitly here rather than worked around or assumed away.
- **Whether and when a second real GPU vendor becomes available** to
  this project's environment, which would be the trigger for revisiting
  the single-vendor, unified-golden-set strategy — an external
  environment constraint, not a design gap this spec can close on its
  own.
- **Whether percentage-based tolerance or a perceptual-diff metric will
  eventually be needed** once Phase 1 introduces a genuine source of
  pixel-level nondeterminism (anti-aliasing, transparency) — explicitly
  deferred, not designed now (see
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  Alternatives Considered).
- **Whether `stb_image`/`stb_image_write` is the right dependency
  choice at all**, versus the no-new-dependency raw/PPM fallback
  [ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
  records as its alternative — this is precisely the kind of decision
  this spec surfaces for explicit Human Review rather than deciding
  unilaterally; if Human Review rejects the dependency, ADR-0041 is
  revised or superseded, not silently reworked.

## Out of Scope / Future Work

Automated CI-enforced gating is the direct, named next consumer of this
spec's local/manual harness, once a future build-system/CI spec resolves
[ci-strategy.md](../docs/process/ci-strategy.md)'s own open questions
(GPU-in-CI approach, dependency-fetch-in-CI strategy) and that
infrastructure is actually provisioned — neither predicted nor
pre-designed here beyond the artifact contract this spec already fixes.
Per-GPU-vendor golden sets are future work, triggered only by a second
real GPU vendor becoming available to this project. Percentage-based or
perceptual-diff tolerance is future work, triggered only by Phase 1
introducing a genuine source of legitimate pixel-level nondeterminism
(anti-aliasing, transparency). Additional covered scenes beyond the one
reused fixture are each a future spec's or Plan's own scope, not
predicted here. A general image-loading/asset-pipeline capability is
explicitly not implied by this spec's `stb` dependency, which is scoped
strictly to `tests/image_regression/`'s own test-support targets — a
future Asset System spec (Candidate Backlog) decides its own
image-loading dependency independently, not constrained by this
decision. Android Platform and Vulkan Presentation (a separate Candidate
Backlog entry) is unaffected by, and does not depend on, this spec in
either direction — see this spec's own accompanying registry update.
