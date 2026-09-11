# Plan: Runtime Host Foundation

- **Spec:** [specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md) (`Approved`, Human Review Approval recorded 2026-08-20)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Human Review Approval (2026-08-21):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), as a joint review of `Approved` Spec
  0013, `Accepted` ADR-0046/ADR-0047, and this Plan together, following
  the Independent Review round below — see
  [PR #61](https://github.com/slmao/Atlantis/pull/61) for the full
  revision history. Explicitly accepts:
  1. The full seven-step implementation sequence.
  2. Step 1's and Step 3's atomic boundaries — no commit may land an
     unbuildable intermediate state or an object with an incomplete
     ownership/lifecycle story.
  3. The `atlantis_runtime_host` private library plus thin
     `atlantis_runtime` executable structure (D1) — no public alias,
     `install()`/export surface, or API any other top-level module may
     depend on.
  4. The `PlatformSession` Runtime-private RAII guard (D4a): declared as
     `RuntimeApplication`'s first member, destroyed last; the single call
     site for `platform::shutdown()` anywhere in the Runtime module,
     inside that guard's own destructor; no hand-written repeated teardown
     sequence in any initialization-failure path.
  5. The final member declaration and reverse destruction order:
     `platformSession_` → `device_` → `presentation_` → `mesh_` →
     `cameraBuffer_` → `depthTexture_` → `material_`, destroying
     `Material` → `Texture` → `Buffer` → `Mesh` → `Presentation` →
     `Device` → `PlatformSession`/window.
  6. The `RuntimeLifecycleTracker::hasEverRun()` contract, and that
     `Device::waitIdle()` is called only once a real frame loop has been
     entered (D2, D8).
  7. That a `waitIdle()` failure itself maps to
     `RuntimeExitReason::UnrecoverableRuntimeError`, with RAII-driven
     teardown still proceeding safely afterward (D8).
  8. The frame-scoped-object release ordering for every acquire/draw/
     submit/present early-return path (`RenderTarget`, `CommandList`,
     `SubmissionSignal`) (D7).
  9. Create-before-destroy `Material` rebuild on a format change, relying
     on the existing single-frame-in-flight drain guarantee to make
     destroying the old `Pipeline` safe (D7).
  10. The exhaustive, no-`default`-case switch design for Presentation/
      Submit error classification, together with the V3 requirement to
      run the real exhaustiveness-protection check by hand (D3). **[See
      the 2026-08-21 Human Review Amendment below — the no-`default`-case
      switch design and the requirement to verify exhaustiveness
      protection both stand; the specific mechanism and V3's own
      verification procedure are revised.]**
  11. The Runtime GPU smoke test's scope: real Windows windowed
      acquire/draw/submit/present plus Vulkan Validation Layers only — no
      windowed readback, no automated pixel comparison (D10).
  12. That the existing headless image-regression suite continues to own
      pixel-level regression, while manual window verification owns
      resize/minimize/restore/close and by-eye comparison against the
      existing golden PNG.
  13. That Runtime composes only the existing bootstrap
      asset/shader/camera/material, with no World/ECS, Client API, headless
      Runtime, Android, or change to any existing module's public API.
  14. The Plan's complete file list, CMake target/dependency graph, the
      V1–V11 verification matrix, and its stated prohibitions.

  This approval authorizes Implementation **once this approval record has
  merged into `main` via a human merge of PR #61** — not before.
- **Human Review Amendment / Correction (2026-08-21):** Reviewed and
  approved by slmao on 2026-08-21, during Implementation on
  `feature/0013-runtime-host-foundation`. Real MSVC compilation of
  `atlantis_runtime_host` (`exit_reason.cpp`'s `toProcessExitCode()`, a
  `switch` with no `default` covering every current `RuntimeExitReason`
  value, nothing after it) failed under this project's real `cl /W4 /WX`
  with `error C2220` promoting `warning C4715: not all control paths
  return a value` — **even though the switch already handled every current
  enumerator**. This falsifies Approval item 10 and D3's original claim
  that a no-`default`-case switch, on its own, is "real, verified compiler
  enforcement" of enum exhaustiveness: C4715 is a *return-path*
  diagnostic, not an *enum-completeness* diagnostic — confirmed by an
  isolated compile probe reproducing the exact shape, it fires (or
  doesn't) identically regardless of whether the switch is missing a case
  or already handles every one. **See D3's revised text for the corrected
  mechanism (MSVC `C4062` via a target-scoped `/w14062` on
  `atlantis_runtime_host` only), and V3's revised text for the corrected
  verification procedure — both empirically re-verified against this
  project's real MSVC invocation.** This amendment corrects **D3 and V3
  only**; it does not reopen Approval items 1–9 or 11–14, `Approved` Spec
  0013, or `Accepted` ADR-0046/ADR-0047. Plan 0013's `Status` remains
  `Approved / Ready for Implementation`.
- **Independent Review (2026-08-21):** Centralized, agent-performed review
  of this Plan's actual PR content against the real
  Platform/RHI/Vulkan Backend/Renderer/Asset System/Shader System source
  and the existing composition roots, focused on
  Platform-shutdown-versus-member-destruction ordering as the
  highest-priority risk. Found and fixed one real design gap (not an
  active bug in any traced-through path, but a real fragility: the
  original design's correctness rested entirely on six independent,
  hand-written teardown sequences each staying correctly ordered under
  future maintenance, with nothing to catch a mistake) and two smaller
  ones, all corrected directly on this branch — no fix required reopening
  `Accepted` ADR-0046/ADR-0047, changing any composed module's public API,
  or moving a module boundary. See "Deviations" and D4a for the
  `PlatformSession` RAII redesign this round's highest-priority finding
  produced.
- **Related ADR(s):**
  [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md)
  (composition, object ownership, frame lifecycle) and
  [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)
  (executable/library structure, test boundary) — both `Accepted`
  2026-08-20.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #151](https://github.com/slmao/Atlantis/pull/151) Batch 4. Original scope, ordered work, decisions D1–D10
  (including the 2026-08-21 D3/V3 amendment), and the V1–V11 matrix
  retained; candidate C++ blocks and the round-by-round review narration
  are condensed, with the full drafts preserved in
  [PR #61](https://github.com/slmao/Atlantis/pull/61) history.

## Objective

Implement Spec 0013 in full: a real `Atlantis Runtime` module —
`atlantis_runtime_host` (a private static library, never a public
dependency surface) plus a thin `atlantis_runtime` Windows executable —
that composes Atlantis Platform, RHI, the Vulkan Backend, Atlantis
Renderer, Atlantis Shader System, and Atlantis Asset System, exactly as
they exist today, with zero change to any of their public APIs, into one
fixed startup → windowed frame loop → shutdown lifecycle displaying the
already-cooked `minimal_cube` asset through the already-compiled
`minimal_mesh` shader.

## Plan-level decisions (fixed here, not left to Implementation)

### D1. CMake targets, namespace, directories, dependency graph

| Target | Kind | Location | Links | Notes |
|---|---|---|---|---|
| `atlantis_runtime_host` (alias `Atlantis::RuntimeHost`) | STATIC | `src/runtime/` | PUBLIC `Atlantis::Core`, `Atlantis::Platform`, `Atlantis::RHI`, `Atlantis::VulkanBackend`, `Atlantis::Renderer`, `Atlantis::ShaderSystem`, `Atlantis::ShaderSystemRhiIntegration`, `Atlantis::AssetSystem`; PRIVATE `atlantis_compiler_warnings` | **Not** `Atlantis::RenderGraph` — `Renderer::drawFrame()` already owns RenderGraph construction/compilation/execution internally; no `atlantis/render_graph/*.h` header is included anywhere in this Plan's file list. **Also** (2026-08-21 amendment, D3): `target_compile_options(atlantis_runtime_host PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/w14062>)` — this target only, MSVC only; `cmake/CompilerWarnings.cmake` itself is not touched. |
| `atlantis_runtime` (executable) | executable | `src/runtime/` | PRIVATE `Atlantis::RuntimeHost`, `atlantis_compiler_warnings` | The thin, per-OS entry point. No other target may depend on `Atlantis::RuntimeHost`, and no `install()`/export rule is added — see D1a. |
| `atlantis_runtime_tests` | executable | `tests/runtime/` | `Atlantis::RuntimeHost`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | GPU-independent — `RuntimeLifecycleState`, error classification, exit-code mapping, the module-boundary grep. |
| `atlantis_runtime_gpu_tests` | executable | `tests/runtime/` | `Atlantis::RuntimeHost`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | `gpu`-labeled — the real windowed smoke test (Step 5). |

- **Namespace:** `atlantis::runtime`. **Public header root:**
  `src/runtime/include/atlantis/runtime/` — chosen over Tools' flat style
  because `Atlantis::RuntimeHost` genuinely has two real consumers of its
  own headers within this module (`atlantis_runtime`'s `main.cpp` and
  `tests/runtime/`).
- **`main.cpp`** (the executable's entry point) lives directly in
  `src/runtime/`, sibling to `CMakeLists.txt`, matching
  `src/tools/shader_compiler/main.cpp`.
- **Root `CMakeLists.txt` ordering — genuinely load-bearing:**
  `add_subdirectory(src/runtime)` must be inserted **after** both
  `add_subdirectory(assets)` and `add_subdirectory(shaders/minimal_renderer)`
  — CMake's `add_dependencies(target other-target)` requires `other-target`
  to already be a declared target at the point the call is parsed (not
  deferred or order-independent within a single configure pass). This is
  why `examples/minimal_renderer_demo` and `tests/image_regression` are
  both already positioned after those two lines. `src/runtime` is inserted
  immediately after `add_subdirectory(shaders/minimal_renderer)` and
  before the `if(ATLANTIS_BUILD_EXAMPLES)` block — Runtime is a product
  executable, not an example, so it is unconditional, like `assets`/
  `shaders/minimal_renderer` themselves. `tests/runtime` joins the
  existing `ATLANTIS_BUILD_TESTS` block, after `tests/tools/asset_cooker`
  — that block is already positioned after both prerequisite directories.

**D1a. Why `Atlantis::RuntimeHost` gets no `install()`/export rule and no
cross-module scanning test.** This repository does not currently
`install()` or export any of its module targets (no `install()` call
appears anywhere in the existing `CMakeLists.txt` tree). Per Spec 0013's
Decisions item 1 and ADR-0047's Negative/Trade-offs, the "no other module
may depend on it" rule is enforced by documentation and ordinary code
review, the same way `module_boundaries.md`'s existing,
never-mechanically-tested "[Tools is] depended on by: nothing" and
"[Runtime is] depended on by: nothing" rules already are for every other
module — no existing module has a reverse-dependency scanning test, and
building the first one here would be new verification infrastructure with
no Spec 0013 requirement driving it. V4 verifies this by inspection at PR
time.

### D2. `RuntimeLifecycleState` — the GPU-independent state-machine boundary

`src/runtime/include/atlantis/runtime/lifecycle_state.h`:
`enum class RuntimeLifecycleState { Uninitialized, Initializing, Running,
ShuttingDown, ShutDown, Failed };` plus a `RuntimeLifecycleTracker` class
(`state()`, `hasEverRun()`, and the mutators `beginInitializing()` /
`markRunning()` / `markFailed()` / `beginShutdown()` / `markShutDown()`).

- Legal transitions: `Uninitialized → Initializing`; `Initializing →
  Running` (also sets `hasEverRun()` true, **permanently**, never reset);
  `Initializing | Running → Failed`; `Initializing | Running | Failed →
  ShuttingDown` (`ShuttingDown | ShutDown → no-op`, idempotent);
  `ShuttingDown → ShutDown`. Every transition not listed (e.g. `Running →
  Initializing`, `ShutDown → Running`, `markShutDown()` from anything but
  `ShuttingDown`) is a programmer-error precondition violation, rejected
  via `ATLANTIS_CHECK_MSG`.
- **`hasEverRun()`** — true once `markRunning()` has ever been called,
  even after a later transition to `Failed`/`ShuttingDown`/`ShutDown`.
  This distinguishes "failed during the six fixed initialization steps,
  before any frame ever ran" from "failed during the frame loop, after
  `Running` was genuinely reached" — the exact distinction D8's
  `waitIdle()` usage depends on (a `Device` that was never handed a
  command list has nothing to wait for; one that reached `Running` may
  have submitted real GPU work).
- This type has **no** dependency beyond `Atlantis::Core` (for
  `ATLANTIS_CHECK_MSG`) — it names no Platform, RHI, or Vulkan Backend
  type, so it is genuinely testable without a GPU or a window.
  `RuntimeApplication` (D4) owns exactly one `RuntimeLifecycleTracker` and
  is the only type that calls its mutators.

### D3. Error taxonomy — `RuntimeInitError`, `RuntimeExitReason`, exhaustive RHI-error classification

`exit_reason.h`: `enum class RuntimeExitReason { Success,
InitializationFailed, UnrecoverableRuntimeError };` and `int
toProcessExitCode(RuntimeExitReason) noexcept` — `Success → EXIT_SUCCESS
(0)`; the other two each map to their own distinct, fixed nonzero value
(exact integers a source-level detail; the requirement is three DISTINCT
values, not their literal numbers).

`init_error.h`: `enum class RuntimeInitError { PlatformInitFailed,
ShaderLoadFailed, DeviceCreateFailed, AssetLoadFailed, MeshCreateFailed,
CameraBufferCreateFailed };` plus a `toString()` (logging only). Every
`RuntimeInitError` value maps uniformly to
`RuntimeExitReason::InitializationFailed` — there is exactly one
initialization-failure exit-code category, so no per-`RuntimeInitError`
classification function is needed; the mapping is applied once, at
`createRuntimeApplication()`'s single `Err` return point (D6).

`error_classification.h`: `classifyPresentationError(atlantis::rhi::PresentationError)
noexcept` and `classifySubmitError(atlantis::rhi::SubmitError) noexcept`,
each `→ RuntimeExitReason`. Every `PresentationError`/`SubmitError`
enumerator that `acquireNextTarget()`/`present()`/`submit()` can actually
return as `Err(...)` is, uniformly, `RuntimeExitReason::UnrecoverableRuntimeError`
(Spec 0013's own outcome table). Confirmed against the real, current
enums: `PresentationError{SurfaceLost, SwapchainCreationFailed,
DeviceLost, Unknown}` (four values) and `SubmitError{QueueSubmitFailed,
DeviceLost}` (two values). The value is the exhaustiveness guarantee, not
per-value differentiation.

**Corrected by the 2026-08-21 Human Review Amendment.** Each function
switches over every real enumerator with NO `default` case; after the
switch a trailing `ATLANTIS_CHECK_MSG(false, "... unhandled enumerator")`
plus a conservative fallback `return
RuntimeExitReason::UnrecoverableRuntimeError;` (`toProcessExitCode()`'s
own three-value `switch` also needs this trailing statement to satisfy
C4715, confirmed by the same real build failure that motivated this
amendment).

- **The `ATLANTIS_CHECK_MSG` + fallback pattern satisfies C4715 on its own
  but provides no compile-time protection by itself** — a case silently
  removed from the switch would fall through to the `ATLANTIS_CHECK_MSG`
  line and still compile cleanly, its only remaining effect being a
  Debug-build crash (or, with a non-terminating handler installed, a
  logged failure) the *first time* that specific value is exercised at
  runtime — not a build-time guarantee. `ATLANTIS_CHECK_MSG`'s own
  installed handler is **not** guaranteed to terminate (Core's
  replaceable-handler contract), so the fallback `return` is required, not
  optional.
- **Real, additional compile-time protection requires MSVC's `C4062`
  ("enumerator ... in switch of enum ... is not handled") — off by
  default even at `/W4`, and not implied by `/Wall`; it must be explicitly
  requested.** Confirmed empirically: the reproduction with one case
  deliberately removed compiled cleanly under plain `/W4 /WX` (no signal
  at all) but failed under `/W4 /WX /w14062` with `error C2220` promoting
  `warning C4062: unhandled enumerator ... in switch of enum ...` naming
  the exact missing value — and, restoring the removed case, compiled
  cleanly again under `/w14062` (no false positive against an
  already-complete switch). `C4062` fires from the `switch`'s own
  case-label completeness against the enum's declaration, independent of
  what code follows the switch — unlike C4715, it genuinely distinguishes
  "complete" from "missing a case."
- **`/w14062` is added to `atlantis_runtime_host` specifically** (a
  `target_compile_options()` addition in `src/runtime/CMakeLists.txt`,
  MSVC-only), **not** by editing `cmake/CompilerWarnings.cmake` — that
  file is shared by every target, and enabling `C4062` project-wide would
  apply it to every existing `switch` in the whole codebase, an
  unaudited, out-of-scope change. This repository's existing repo-wide
  `/WX` is what promotes `C4062` from a warning to a hard build error once
  `/w14062` opts `atlantis_runtime_host` into seeing it at all.
- **Adding a `default:` case would additionally suppress `C4062` itself**
  (a `switch` with a `default` is, by definition, "handled" from `C4062`'s
  point of view, regardless of which named enumerators have their own
  `case`), silently defeating the one mechanism that *does* provide real
  protection. Both switches carry a code comment stating this explicitly.

### D4. `PlatformSession`, `BootstrapConfig`, and `RuntimeApplication` — object model, ownership, destruction order

**D4a. `PlatformSession` — an RAII guard around
`platform::initialize()`/`shutdown()`, and the single structural fix for
the window-vs.-GPU-resource destruction order this Plan's independent
review round identified as its highest-priority risk.** Atlantis
Platform's public API is entirely free functions with no owning object —
nothing in Platform prevents a caller from calling `shutdown()` too early
relative to GPU resources still alive. Every prior composition root
(`minimal_renderer_demo`, `frame_execution_demo`, the image-regression
fixture) is a flat `main()` whose hand-written statement order is the
*only* thing keeping this correct — acceptable there because each is a
single, short, linear function reviewed as a whole; `RuntimeApplication`
is a long-lived, stateful class with **six distinct failure points during
initialization alone** (D6), and hand-sequencing "destroy every GPU
resource, *then* call `platform::shutdown()`" correctly at every one of
those six points, forever, under future maintenance, is exactly the kind
of fragile convention RAII-by-default exists to replace.

`platform_session.h` declares `PlatformSession` — a move-only guard
(`= default` "not holding a session" state; deleted copy; move
constructor/assignment steal `active_`, leaving the source inactive;
`isActive()`), and a `friend`-only `createPlatformSession()` →
`Result<PlatformSession, atlantis::platform::PlatformError>` that calls
`platform::initialize()` and returns a `PlatformSession` with `active_ =
true` only on `Ok` — so its destructor never calls `shutdown()` for a
session that was never really established.

**`PlatformSession::~PlatformSession()` is the *only* call site for
`platform::shutdown()` anywhere in `Atlantis::RuntimeHost`.** No other
function — not `RuntimeApplication::shutdown()`, not `initializeSteps()`'s
failure branches, not `main.cpp` — calls `platform::shutdown()` directly.
This eliminates the double-call risk structurally: there is only one call
site, so there is nothing to keep in sync. Its body: `if (active_) {
platform::shutdown(); for (const auto& event : platform::processEvents())
{ /* log only */ } active_ = false; }` — bundling Spec 0013's adjacent
Shutdown steps 3–4 (`platform::shutdown()` then one final `processEvents()`
drain) into one operation that only ever runs once, driven entirely by
ordinary object destruction. `platform::shutdown()`'s real implementation
asserts `ATLANTIS_CHECK_MSG(s.initialized && !s.shutDown, "...")` — a
genuine crash if ever violated; because it is now called from exactly one
place which itself only runs once per `PlatformSession` object (C++
guarantees a destructor runs exactly once), this assertion cannot be
violated by `RuntimeApplication`'s own code under any control-flow path.

`bootstrap_config.h`: `struct BootstrapConfig { std::string
applicationName = "Atlantis Runtime"; std::string
vertexShaderSpirvPath/vertexShaderReflectionPath/fragmentShaderSpirvPath/
fragmentShaderReflectionPath; std::string assetArtifactPath/
assetMetadataPath; bool enableValidationLayers = true; };` — a plain,
caller-populated value struct (not a service, not a builder), every path
supplied by the caller (`main.cpp`, or `tests/runtime/`'s smoke test)
sourced from CMake-injected compile definitions (D9); no path is ever
hardcoded inside `src/runtime/`'s library sources; no command-line
parsing, no config file, no environment variable is read by
`Atlantis::RuntimeHost`.

**`RuntimeApplication` (`runtime_application.h`) — the composition
object.** Move-only (implicit, from its `unique_ptr`/`optional`/`Mesh`/
`Material` members). Public: `runFrame()` (one iteration, D7),
`shouldContinue() noexcept`, `RuntimeExitReason shutdown()` (idempotent,
D8), a `~RuntimeApplication()` whose entire body is `shutdown();`. Private:
a `friend Result<RuntimeApplication, RuntimeInitError>
createRuntimeApplication(const BootstrapConfig&)`, an
`initializeSteps(const BootstrapConfig&)` helper, and its **member list,
declared in exactly this order because C++ destroys members in the
REVERSE of declaration order**:

```
PlatformSession platformSession_;      // declared FIRST -> destroyed LAST: OS window outlives every GPU resource
std::unique_ptr<atlantis::rhi::Device> device_;
std::unique_ptr<atlantis::rhi::Presentation> presentation_;   // lazy: on first SurfaceCreated
std::optional<atlantis::renderer::Mesh> mesh_;
std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer_;
std::unique_ptr<atlantis::rhi::Texture> depthTexture_;        // lazy: first frame's extent-change check
std::optional<atlantis::renderer::Material> material_;        // lazy: first frame's format-change check
atlantis::renderer::Renderer renderer_;                       // stateless, default-constructed
RuntimeLifecycleTracker lifecycle_;
RuntimeExitReason lastExitReason_ = RuntimeExitReason::Success;
bool closeRequested_ = false;
std::optional<atlantis::rhi::Format> lastSeenFormat_;
std::optional<atlantis::rhi::Extent2D> lastSeenExtent_;
atlantis::rhi::VertexInputLayout vertexInputLayout_;          // resolved once at init, reused for every Material rebuild
std::vector<std::uint32_t> vertexSpirv_, fragmentSpirv_;      // retained for every Material rebuild
```

The `platformSession_`-declared-first ordering is a compiler-enforced
invariant, not a convention any method body has to get right by hand
(D4a). The remaining order (`Material` → `Texture` → `Buffer` → `Mesh` →
`Presentation` → `Device`) IS Spec 0013's/ADR-0046's fixed Shutdown
sequence, obtained the same way — for free, from ordinary member
destruction, not a manually-sequenced list of `reset()` calls that could
drift. **No abstract service interface, no DI container, no service
locator** — `initializeSteps()` calls `platform::initialize()`,
`shader_system::loadReflectionMetadata()`, `vulkan_backend::createDevice()`,
`asset_system::loadStaticMeshAsset()`, `renderer::createMesh()`, and
`device_->createBuffer()` directly, by name.

### D5. Bootstrap scene — asset/shader loading and the fixed `DrawItem`

- The `minimal_mesh` vertex/fragment SPIR-V and reflection JSON are read
  via plain `std::ifstream` (matching `loadSpirvFile()`'s existing pattern
  in `examples/minimal_renderer_demo/main.cpp`, **duplicated, not shared**
  into `src/runtime/src/runtime_application.cpp`) and
  `shader_system::loadReflectionMetadata()`, from the paths
  `BootstrapConfig` carries — never a hardcoded or working-directory-relative
  path (D9).
- `VertexInputLayout` is resolved once via
  `shader_system::rhi_integration::toVertexInputLayout()`, with the
  identical `Vertex{float position[3]; float color[3];}` schema and
  `MeshVertexAttributeSchema` table every existing windowed/fixture
  composition uses — required to be identical to the one the checked-in
  shader and the checked-in `minimal_cube` asset were both authored
  against.
- The `minimal_cube` asset is loaded via
  `asset_system::loadStaticMeshAsset()` from the paths `BootstrapConfig`
  carries, then passed to `renderer::createMesh()` exactly as
  `setUpMinimalCubeFixtureFromAsset()` does — no intermediate copy.
- **Camera:** fixed (not orbiting) — `lookAt(0, 1.5, 2.5, 0, 0, 0)` / 60°
  vertical FOV perspective, the exact values
  `minimal_cube_fixture.cpp` uses — chosen over `minimal_renderer_demo`'s
  orbiting camera because a **static** camera is what makes the manual
  by-eye comparison against the existing golden PNG meaningful
  frame-to-frame. `aspect` is recomputed every frame from the acquired
  target's own extent, so the by-eye golden comparison is a "is this
  recognizably the same cube, correctly shaded and depth-ordered" check,
  not a claim of geometric identity to the golden's own square framing.
- `DrawItem{ .mesh = &*mesh_, .material = &*material_, .objectToWorld =
  identityMatrix() }` — exactly one, rebuilt fresh each frame from the
  current `material_` (never cached across a `material_` rebuild).

### D6. Initialization sequence — `initializeSteps()`

`initializeSteps(const BootstrapConfig& config)` runs the six steps Spec
0013's Initialization order fixes. **Every failure branch does exactly two
things: `lifecycle_.markFailed()`, then `return
Err(RuntimeInitError::...)`.** No branch resets a member, and no branch
calls `platform::shutdown()` — there is nothing left to hand-sequence,
because D4a's `PlatformSession`-declared-first member order already
guarantees correct teardown, in the correct order, automatically, once
`initializeSteps()` returns `Err` and the local `RuntimeApplication` in
`createRuntimeApplication()` goes out of scope:

1. `createPlatformSession()` → `platformSession_`. `Err` →
   `markFailed()`; `Err(PlatformInitFailed)`. (`platformSession_` stays
   inactive — its destructor is a no-op.)
2. Load the four shader files; resolve `vertexInputLayout_`. Any failure →
   `markFailed()`; `Err(ShaderLoadFailed)`.
3. `vulkan_backend::createDevice({.applicationName = config.applicationName,
   .enableValidationLayers = config.enableValidationLayers})` → `device_`.
   `Err` → `markFailed()`; `Err(DeviceCreateFailed)`.
4. `asset_system::loadStaticMeshAsset(config.assetArtifactPath,
   config.assetMetadataPath)`. `Err` → `markFailed()`; `Err(AssetLoadFailed)`.
5. `renderer::createMesh(*device_, vertexInputLayout_, assetData
   vertexBytes/indices data+size)` → `mesh_`. `Err` → `markFailed()`;
   `Err(MeshCreateFailed)`.
6. `device_->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes
   = sizeof(float) * 32})` → `cameraBuffer_`. `Err` → `markFailed()`;
   `Err(CameraBufferCreateFailed)`.

On success: `lifecycle_.markRunning()` (also the **only** place
`hasEverRun()` becomes `true`); return `Ok(std::monostate{})`.
`createRuntimeApplication()` is a thin wrapper: construct a default
`RuntimeApplication`, `beginInitializing()`, `initializeSteps(config)`,
return `Ok(std::move(app))` on success or propagate the `Err` — on the
`Err` path the local `app` (holding whatever subset was constructed before
the failing step) goes out of scope and its destructor (D8) tears
everything down through ordinary RAII, in the correct order, with no
further code in `initializeSteps()` responsible for getting that order
right.

**`RuntimeInitError` covers exactly these six steps.** `Material` and the
depth `Texture` are never constructed during initialization (Spec 0013's
Bootstrap Sequencing Detail) — their creation failures are handled
entirely inside `runFrame()`'s retry-on-failure logic (D7), never a
`RuntimeInitError`. A `Device::waitIdle()` failure can only occur inside
`shutdown()` (D8) — never a `RuntimeInitError` either.

### D7. Per-frame order — `runFrame()`/`shouldContinue()`

`shouldContinue()` returns `!platform::shouldQuit() && !closeRequested_ &&
lifecycle_.state() != RuntimeLifecycleState::Failed`. `runFrame()`
(callable only while `shouldContinue()`; an `ATLANTIS_CHECK` guards the
precondition):

1. `platform::processEvents()`, handling each event exactly as
   `minimal_renderer_demo` does — `SurfaceCreated` (construct
   `presentation_` via `vulkan_backend::createPresentation()`; a second
   occurrence while `presentation_` already exists calls `markFailed()`),
   `WindowResize` (`presentation_->notifyResized()`, no-op if
   `presentation_` does not exist yet), `WindowCloseRequested`
   (`closeRequested_ = true`), `SurfaceDestroyed` (`markFailed()` if
   `presentation_` still exists), `Quit`/`FocusGained`/`FocusLost`/
   `ApplicationPause`/`ApplicationResume` (logged only).
2. If `!presentation_ || closeRequested_ ||
   lifecycle_.state() == RuntimeLifecycleState::Failed`: return.
3. `presentation_->acquireNextTarget()`. `Err` → `markFailed()` (the
   specific `PresentationError` is logged via `classifyPresentationError()`'s
   own input, purely for the log message — the classification result is
   always `UnrecoverableRuntimeError`, D3); return. `Ok(nullptr)` → return
   (zero extent or an internally-deferred out-of-date swapchain).
   `Ok(target)` → continue.
4. Format-change check: if `!lastSeenFormat_.has_value() ||
   presentation_->metadata().format != *lastSeenFormat_` — call
   `renderer::createMaterial()` to build a **new** `Material` value
   first; only on that call's own success is `material_` **move-assigned**
   over with the result. The **old** `Material` (and its `Pipeline`) is
   destroyed by that move-assignment itself, at the point the new one has
   already fully succeeded — never before. Create-before-destroy, matching
   `minimal_renderer_demo`'s already-verified sequence. Destroying the old
   `Pipeline` immediately afterward is safe: this check runs only after a
   successful `acquireNextTarget()` (step 3), whose contract already
   drains any previous frame's submission internally before returning
   (single-frame-in-flight, ADR-0019/ADR-0020) — so nothing on the GPU can
   still be reading the old `Pipeline`. On `createMaterial()` failure, log
   and **keep the existing** `material_` (or, on the very first frame,
   leave it unset), retry next frame — `lastSeenFormat_` is only updated
   on success.
5. Extent-change check: same keep-existing-and-retry-on-failure pattern
   for `depthTexture_`.
6. If `!material_ || !depthTexture_`: nothing valid to draw yet — return
   (the acquired `target` is dropped via RAII; legal per `RenderTarget`'s
   contract — no submitted GPU state).
7. Write this frame's fixed camera view/projection into
   `cameraBuffer_->mappedData()` (D5). Build the one `DrawItem`.
   `device_->createCommandList()`. `Err` → `markFailed()`; return.
8. `renderer_.drawFrame(*commandList, *target, *depthTexture_,
   *cameraBuffer_, drawItems, atlantis::rhi::ResourceState::PresentSource)`.
9. `device_->submit(std::move(commandList), *target)`. `Err` →
   `markFailed()`; return.
10. `presentation_->present(std::move(target), std::move(submissionSignal))`.
    `Err` → `markFailed()`; return.

**Frame-scoped objects on every early return, checked call by call — none
released after `presentation_`/`device_`, all correctly released before
them:** `target` is a plain local from step 3 onward; steps 3's `Err`/
`Ok(nullptr)` branches and step 6's "nothing to draw" branch never move it
anywhere — it is destroyed by ordinary RAII on return, legal per
`RenderTarget`'s documented contract (no `release()`/`consume()` method;
destroying an unconsumed borrow is the normal path). Step 9's
`device_->submit(std::move(commandList), *target)` takes `commandList`
**by value** (moved in, ADR-0020) — ownership has transferred into
`submit()` regardless of `Ok`/`Err`; `target` is passed by reference (not
consumed) and is still released via the same RAII path on a step 9
failure. Step 10's `present(std::move(target), std::move(submissionSignal))`
consumes **both** by value. **In every case, whatever `runFrame()` still
locally owns at an early return is released by that return alone — never
by anything `shutdown()` does later, and never in a way that could
outlive `presentation_`/`device_`, both of which are only ever reset from
inside `shutdown()` (D8), called strictly after the current `runFrame()`
call has returned.**

**Mid-frame close, confirmed unnecessary to special-case:** because step
1 always completes before step 3's `acquireNextTarget()`, and
`closeRequested_` short-circuits step 2 before any acquire happens this
same iteration, there is no code path in which a `RenderTarget` is
acquired and then abandoned because a close request arrived mid-frame.

### D8. Shutdown — one uniform path, `platform::shutdown()` called exactly once, always

`RuntimeExitReason RuntimeApplication::shutdown()`:

- If `lifecycle_.state() == RuntimeLifecycleState::ShutDown`, return
  `lastExitReason_` (idempotent). Otherwise: `hadFailure =
  (lifecycle_.state() == Failed)`; `lifecycle_.beginShutdown()`.
- `if (device_ && lifecycle_.hasEverRun()) { auto result =
  device_->waitIdle(); if (result.isErr()) hadFailure = true; }` —
  `waitIdle()` is meaningful only if this object ever reached `Running`
  (before that, `runFrame()` — the only place `Device::submit()` is ever
  called — has never run, so there is nothing outstanding). A `waitIdle()`
  failure (e.g. the device was already lost) is itself an unrecoverable
  condition for this run — teardown proceeds regardless; there is no
  alternative recovery action, and this codebase makes no claim the wait
  actually completed successfully.
- `material_.reset(); depthTexture_.reset(); cameraBuffer_.reset();
  mesh_.reset(); presentation_.reset(); device_.reset();` —
  `platformSession_` is deliberately NOT touched here. Its own destructor
  — guaranteed by C++'s member-destruction order to run only after every
  member above has already been destroyed (D4a) — is the ONLY place
  `platform::shutdown()` is ever called. This function's responsibility
  ends at "every GPU resource is gone."
- `lifecycle_.markShutDown(); lastExitReason_ = hadFailure ?
  UnrecoverableRuntimeError : Success; return lastExitReason_;`

This is now the one and only teardown path in the whole class — reached
explicitly from `main.cpp`'s post-loop call (`app.shutdown()`) or the GPU
smoke test (D10), **and** reached implicitly, via the destructor's
idempotent backstop call, for every `initializeSteps()` early-failure case
(D6), since those branches no longer perform any teardown of their own.
`~RuntimeApplication()`'s entire body is `shutdown();` — a genuine no-op
if already `ShutDown`; ordinary implicit member destruction then runs, and
finally `platformSession_`'s destructor runs last, closing the window.

### D9. Build-tree path threading — reusing existing exported CMake variables, no hardcoded path

`src/runtime/CMakeLists.txt` (for `atlantis_runtime`) and
`tests/runtime/CMakeLists.txt` (for `atlantis_runtime_gpu_tests`) each add
`target_compile_definitions(<target> PRIVATE
ATLANTIS_RUNTIME_SHADER_DIR="${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}"
ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH="${ATLANTIS_minimal_cube_ARTIFACT_PATH}"
ATLANTIS_RUNTIME_ASSET_METADATA_PATH="${ATLANTIS_minimal_cube_METADATA_PATH}")`
and `add_dependencies(<target> minimal_mesh_shaders
${ATLANTIS_minimal_cube_TARGET})`.

- **No new CMake mechanism.** `ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR`
  (absolute, configuration-independent — ADR-0031) and
  `ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH,TARGET}` (Plan
  0012's D1/D2, also configuration-independent: `src/asset_system/CMakeLists.txt`
  writes to `${CMAKE_BINARY_DIR}/assets`, never a `$<CONFIG>`-qualified
  path) are both **already exported, real CMake variables** — this Plan
  only reads them.
- Runtime reads the shader files **directly from
  `ATLANTIS_RUNTIME_SHADER_DIR`** at their real build-tree location — no
  `POST_BUILD copy_if_different` step (unlike `minimal_renderer_demo`).
  Simpler, not merely different: because the path is absolute and
  configuration-independent, there is nothing to copy.
  `RuntimeApplication` appends the fixed filenames
  (`minimal_mesh.vert.spv`, `.vert.refl.json`, `.frag.spv`,
  `.frag.refl.json`) onto `ATLANTIS_RUNTIME_SHADER_DIR` — a fixed constant
  matching every existing consumer.
- `main.cpp` and the GPU smoke test each construct their own
  `BootstrapConfig` from these three macros directly — `src/runtime/`'s
  library sources never reference an `ATLANTIS_RUNTIME_*` macro at all,
  keeping `Atlantis::RuntimeHost` fully parameterized and free of any
  build-tree-path assumption.
- `add_dependencies()` guarantees the shader-compile and asset-cook
  targets have already run before either consumer builds — matching
  `tests/image_regression/CMakeLists.txt`'s own identical two-line
  pattern.

### D10. GPU smoke test — deterministic exit with no product-level test hook

`tests/runtime/runtime_smoke_gpu_tests.cpp` links `Atlantis::RuntimeHost`
directly (not `atlantis_runtime`, never invoked as a subprocess by any
test) and reuses `RuntimeApplication`'s own public API exactly as
`main.cpp` does, with a bounded loop instead of an unbounded one: build a
`BootstrapConfig` from the three `ATLANTIS_RUNTIME_*` macros
(`enableValidationLayers = true`), `createRuntimeApplication(config)`,
`REQUIRE(appResult.isOk())`, then `for (int i = 0; i <
kSmokeTestFrameCount && app.shouldContinue(); ++i) app.runFrame();`
(`kSmokeTestFrameCount = 3`, matching this repository's own `kCycleCount`
precedent — `frame_execution_demo`, `headless_rendering_demo`,
`image_regression_gpu_tests`), `REQUIRE(app.shouldContinue())` (did not
fail), then `REQUIRE(app.shutdown() == RuntimeExitReason::Success)`.

- **No CLI flag, no environment variable, no test-only constructor
  parameter is added to `RuntimeApplication`, `BootstrapConfig`, or
  `atlantis_runtime`'s `main.cpp`.** The test achieves a bounded,
  deterministic exit purely by calling the already-public `shutdown()`
  method after a fixed number of `runFrame()` calls, instead of waiting
  for a real `WindowCloseRequested` event — no window-close simulation is
  needed, since `shutdown()` was already designed (D8) to be safely
  callable at any point once `Running`.
- **Validation Layers verification is not a separate manual log-grep
  step.** Confirmed against `src/vulkan_backend/src/validation.cpp`: the
  installed debug messenger callback calls `ATLANTIS_CHECK_MSG(false,
  message)` followed by `std::abort()` for any `WARNING`/`ERROR`-severity
  message — with `enableValidationLayers = true`, a genuine validation hit
  crashes the test process immediately, which `ctest` reports as a
  failure. A clean `REQUIRE` pass is therefore itself real evidence of
  zero Validation Layer warnings/errors for the exact sequence this test
  exercises; grepping the captured output for `VUID`/`Validation Error`
  afterward remains good practice but is confirmatory.
- **This is the first `gpu`-labeled test in this repository that creates a
  real, visible OS window during an automated `ctest` run** — every prior
  GPU-required test is offscreen-only. A disclosed, deliberate consequence
  of Spec 0013's approved design, flagged here so a human running the full
  suite is not surprised by a window briefly appearing and disappearing.

## Milestones / Task Breakdown

Each step leaves the repository configuring, building, and (from Step 1
onward) its own tests passing. Step 3 is the one step whose own new code
cannot be exercised by a new automated test in the same step (it is
GPU-dependent, real-window code) — it is verified in Step 5/6 instead;
disclosed, not silently skipped.

### Step 1 — Module skeleton, `RuntimeLifecycleState`, exit-reason type (**atomic**)

Atomic because CMake rejects a STATIC library with no sources, and the
root `CMakeLists.txt` edit references a directory that must already exist.

- `src/runtime/CMakeLists.txt` — declares `atlantis_runtime_host` +
  `Atlantis::RuntimeHost` alias, with D1's full PUBLIC dependency list.
  (The `atlantis_runtime` executable target is **not** declared yet —
  added in Step 4, once there is a `main.cpp` for it to build.)
- `src/runtime/{include/atlantis/runtime/lifecycle_state.h,src/lifecycle_state.cpp}`
  — D2, in full.
- `src/runtime/{include/atlantis/runtime/exit_reason.h,src/exit_reason.cpp}`
  — `RuntimeExitReason`, `toProcessExitCode()` (D3).
- `tests/runtime/{CMakeLists.txt,lifecycle_state_tests.cpp,exit_reason_tests.cpp}`
  — V1, V2 (only `atlantis_runtime_tests` exists after this step).
- Root `CMakeLists.txt` — `add_subdirectory(src/runtime)` and
  `add_subdirectory(tests/runtime)` (D1's ordering).

### Step 2 — `RuntimeInitError`, exhaustive RHI-error classification

- `src/runtime/{include/atlantis/runtime/init_error.h,src/init_error.cpp}`
  — D3.
- `src/runtime/{include/atlantis/runtime/error_classification.h,src/error_classification.cpp}`
  — `classifyPresentationError()`/`classifySubmitError()`, the two
  no-default exhaustive switches with their `ATLANTIS_CHECK_MSG` +
  conservative-fallback trailing statement (D3, as revised by the
  2026-08-21 amendment).
- `src/runtime/CMakeLists.txt` — the `/w14062` target-scoped MSVC compile
  option (D1, D3 amendment) — real enum-exhaustiveness protection for this
  target only; `cmake/CompilerWarnings.cmake` is not touched.
- `tests/runtime/error_classification_tests.cpp` — V3.

### Step 3 — `PlatformSession`, `BootstrapConfig`, `RuntimeApplication` (init/frame/shutdown), the fixed bootstrap scene (**atomic**)

The largest step, and explicitly marked atomic: this is where
`Atlantis::RuntimeHost` first depends on Platform/RHI/Vulkan Backend/
Renderer/Shader System/Asset System for real, and D4 through D8's design
is genuinely one cohesive unit — the same member layout, the same
lifecycle tracker, and the same failure-handling convention thread through
initialization, per-frame execution, and shutdown together, so a partial
landing would not compile at all, let alone compile *correctly*.
`PlatformSession` (D4a) is introduced here, alongside its sole real
consumer, rather than in Step 1: constructing a real `PlatformSession`
calls `platform::initialize()`, which creates an actual OS window —
violating Step 1's own GPU-independent test category's explicit "no
Device, no GPU, and **no real window**" bar — so `PlatformSession` is not,
and is not claimed to be, independently unit-tested; its correctness is
verified as part of the full composition, in Step 5/6.

- `src/runtime/{include/atlantis/runtime/platform_session.h,src/platform_session.cpp}`
  — D4a, in full.
- `src/runtime/include/atlantis/runtime/bootstrap_config.h` — D4.
- `src/runtime/{include/atlantis/runtime/runtime_application.h,src/runtime_application.cpp}`
  — the full class from D4, `initializeSteps()`/`createRuntimeApplication()`
  from D6, `runFrame()`/`shouldContinue()` from D7,
  `shutdown()`/`~RuntimeApplication()` from D8, and the fixed camera/
  `DrawItem` construction from D5.
- No new automated test in this step — `atlantis_runtime_host` must still
  **compile** cleanly against every module it now links, itself a real,
  meaningful check. This step's code is exercised for the first time by
  Step 5's GPU smoke test, and verified in full by Step 6.

### Step 4 — Thin `atlantis_runtime` Windows executable, build-tree path wiring

- `src/runtime/main.cpp` — `int main()` (matching every existing windowed
  composition root's own choice, not `WinMain`, so `ATLANTIS_LOG_*`
  output remains visible in a console window). Builds a `BootstrapConfig`
  from the three `ATLANTIS_RUNTIME_*` macros (D9), calls
  `createRuntimeApplication()`, logs and returns
  `toProcessExitCode(RuntimeExitReason::InitializationFailed)` on `Err`;
  on `Ok`, runs `while (app.shouldContinue()) { app.runFrame(); }`, then
  `return toProcessExitCode(app.shutdown());`.
- `src/runtime/CMakeLists.txt` — add the `atlantis_runtime` executable
  target (D1), its `target_compile_definitions()`/`add_dependencies()`
  (D9).

### Step 5 — GPU smoke test

- `tests/runtime/runtime_smoke_gpu_tests.cpp` — D10, in full.
- `tests/runtime/CMakeLists.txt` — declares `atlantis_runtime_gpu_tests`,
  `gpu`-labeled (`catch_discover_tests(... PROPERTIES LABELS "gpu")`),
  with D9's compile definitions and `add_dependencies()`.
- V5 (no `Vk*`/`vulkan/` include anywhere under `src/runtime/`) is
  implemented as a plain grep run and recorded at verification time (Step
  6), matching V4's inspection-based approach (D1a) — this Plan does not
  build a second, narrower automated scanning test.

### Step 6 — Full verification (Debug/Release, GPU-independent, GPU-required, manual)

- Clean Debug and Release configure + build.
- `ctest -LE gpu` and `ctest -L gpu`, both configurations — confirms V1–V3
  plus every existing GPU-independent suite unaffected (V10).
- `atlantis_runtime_gpu_tests` run on real hardware — V6.
- Manual interactive verification against the real `atlantis_runtime`
  executable (not the smoke test): a visible window shows the
  `minimal_cube` mesh, compared by eye against
  `tests/image_regression/goldens/minimal_cube/minimal_cube_512x512_rgba8unorm.png`;
  interactive resize (depth `Texture` recreated, `Material`/`Pipeline`
  **not**); minimize/restore (no Vulkan call while minimized, correct
  resume); a normal window-close exits cleanly with
  `RuntimeExitReason::Success`; Vulkan Validation Layers grepped clean
  throughout, on both configurations — V8.
- `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`,
  `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`,
  `src/shader_system/`, `src/asset_system/`, `shaders/`, or
  `tests/image_regression/goldens/` was modified, and no existing
  example/demo/CMake target/test was touched — V11.

### Step 7 — Documentation and registry closeout

- `AGENTS.md` — the Module boundaries section's Runtime entry updated to
  reflect that it is now implemented (exact wording an Implementation-time
  detail).
- `docs/architecture/module_boundaries.md` — the `## Atlantis Runtime`
  section's own `Depends on` line corrected to remove RenderGraph (Spec
  0013's Architectural Impact finding, explicitly deferred to this Plan)
  and to name the `atlantis_runtime_host`/`atlantis_runtime` split; status
  updated from `PROPOSED` to reflect this section is now implemented (the
  document's broader whole-document `PROPOSED` banner is unrelated
  systemic staleness this Plan does not touch).
- `docs/project-blueprint.md` — a new Milestone entry for Runtime Host
  Foundation.
- `src/README.md` — add a `runtime/` entry.
- `specs/README.md` — Spec 0013's Implementation column updated to
  reference the Implementation PR by number once opened (e.g.
  "Implemented — code complete on PR #N, `OPEN`, not yet merged into
  `main`"), matching this repository's established two-stage convention
  (Spec 0012's row carried exactly this wording until a human actually
  merged its PR). A small follow-up docs commit, opened only after a human
  has actually merged the Implementation PR, updates the wording to
  "Implemented and merged" — the same pattern used after PR #58 (Spec
  0012). This Plan's own `Related Plan(s)`/Plan-column update (the current
  PR) is the only `specs/README.md` edit this Plan-drafting round makes.

## Files / Modules Touched (expected)

**New — Runtime module:** `src/runtime/CMakeLists.txt`;
`src/runtime/include/atlantis/runtime/{lifecycle_state,exit_reason,init_error,error_classification,platform_session,bootstrap_config,runtime_application}.h`;
`src/runtime/src/{lifecycle_state,exit_reason,init_error,error_classification,platform_session,runtime_application}.cpp`;
`src/runtime/main.cpp`.

**New — tests:**
`tests/runtime/{CMakeLists.txt,lifecycle_state_tests.cpp,exit_reason_tests.cpp,error_classification_tests.cpp,runtime_smoke_gpu_tests.cpp}`.

**Modified:** `CMakeLists.txt` (root) — two `add_subdirectory()` lines;
`specs/0013-runtime-host-foundation.md` — `Related Plan(s)` field;
`specs/README.md` — Spec 0013's Plan column (this Plan-drafting round; the
Implementation-column update per Step 7 is future Implementation-PR work).

**Explicitly not touched (this Plan-drafting round):** `src/rhi/`,
`src/renderer/`, `src/render_graph/`, `src/vulkan_backend/`,
`src/platform/`, `src/shader_system/`, `src/asset_system/`, `src/tools/`,
`assets/`, `shaders/`, `examples/`, `tests/image_regression/`,
`tests/asset_system/`, `tests/shader_system/`, `cmake/`, every existing
example/demo, every existing `Accepted` ADR, and (beyond the two fields
named above) `AGENTS.md`/`docs/architecture/module_boundaries.md`/
`docs/project-blueprint.md`/`src/README.md` — those four are
Implementation-PR work per Step 7.

## Sequencing & Dependencies

```
Step 1 (skeleton, RuntimeLifecycleState, RuntimeExitReason)
  └─> Step 2 (RuntimeInitError, exhaustive error classification)
        └─> Step 3 (BootstrapConfig, RuntimeApplication: init/frame/shutdown)
              └─> Step 4 (atlantis_runtime executable, path wiring)
                    └─> Step 5 (GPU smoke test)
                          └─> Step 6 (full verification)
                                └─> Step 7 (documentation/registry closeout)
```

- Step 3 needs Step 2 because `initializeSteps()`'s `Err` returns are
  typed `RuntimeInitError`, and `runFrame()`'s failure logging calls
  `classifyPresentationError()`/`classifySubmitError()`.
- Step 4 needs Step 3 because `main.cpp` calls `createRuntimeApplication()`
  and drives the object it returns.
- Step 5 needs Step 4's own CMake path-wiring pattern (D9), reused
  verbatim for the second consumer (`atlantis_runtime_gpu_tests`).
- Step 6 needs Step 5's executable to exist to run it.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | `RuntimeLifecycleState`: every legal transition succeeds; every illegal one (`Running`→`Initializing`, `ShutDown`→`Running`, `markShutDown()` from anything but `ShuttingDown`) is rejected via `ATLANTIS_CHECK`; `beginShutdown()` is idempotent from `ShuttingDown`/`ShutDown`; `hasEverRun()` is `false` for a tracker that transitions `Initializing`→`Failed` directly, and `true` for one that reaches `Running` first, even after a later `Failed`/`ShutDown` transition — the exact property D8's `waitIdle()` gating depends on (exercised against the pure tracker type only). | `lifecycle_state_tests.cpp` | GPU-independent |
| V2 | `RuntimeExitReason`→process-exit-code mapping: three distinct values; `Success`→`EXIT_SUCCESS`. | `exit_reason_tests.cpp` | GPU-independent |
| V3 | *(Revised by the 2026-08-21 Human Review Amendment.)* `classifyPresentationError()`/`classifySubmitError()`: each of the four real `PresentationError` values and both real `SubmitError` values individually maps to `UnrecoverableRuntimeError` — confirmed exhaustive against `src/rhi/include/atlantis/rhi/types.h`. Separately, with `atlantis_runtime_host`'s own `/w14062` compile option in place (D1, D3): a fresh Debug build of `atlantis_runtime_host` with the complete switch is confirmed clean; a fresh Debug build after temporarily removing one `case` from either switch is confirmed to fail with `error C2220` promoting `warning C4062` naming the exact missing enumerator; a fresh Debug build after restoring the removed case is confirmed clean again — a one-time manual check recorded in the Implementation PR. `C4715` is **not** part of this check — it fires identically whether the switch is complete or missing a case, so it provides no exhaustiveness signal on its own; `C4062`, gated behind `/w14062`, is what this verification exercises. | `error_classification_tests.cpp`; `/w14062` mechanism check manual, recorded in the PR | GPU-independent + manual |
| V4 | No other top-level module depends on `Atlantis::RuntimeHost` — verified by inspection/grep across every other module's `CMakeLists.txt` at PR time (D1a; matching the existing, equally inspection-based "[Runtime/Tools are] depended on by: nothing" rules already in `module_boundaries.md`). | Manual/grep, recorded in the PR | Manual |
| V5 | No `Vk*` type and no `#include <vulkan/...>` appears anywhere under `src/runtime/` — verified by inspection/grep at verification time (D1a's own reasoning). | Manual/grep, recorded in the PR | Manual |
| V6 | Real windowed acquire/draw/submit/present succeeds for `kSmokeTestFrameCount` consecutive frames, then `shutdown()` returns `RuntimeExitReason::Success` — a genuine Vulkan Validation Layer warning/error during this run aborts the process (`validation.cpp`'s own existing mechanism), so a clean pass is direct evidence of zero warnings/errors for this exact sequence. | `runtime_smoke_gpu_tests.cpp` | `gpu`-labeled |
| V7 | Debug **and** Release: clean configure + build; `ctest -LE gpu` and `ctest -L gpu` both green. | Both configurations | Manual, recorded |
| V8 | Real, human-driven `atlantis_runtime`: visible window shows the correctly-shaded, correctly depth-ordered `minimal_cube` mesh, compared by eye against the existing golden PNG; interactive resize (depth `Texture` recreated, `Pipeline` not); minimize/restore (no Vulkan call while minimized); normal close exits with `RuntimeExitReason::Success`; Vulkan Validation Layers grepped clean throughout, both configurations. | Manual, recorded in the PR | Manual |
| V9 | `atlantis_runtime`/`atlantis_runtime_gpu_tests` each correctly `add_dependencies()` on `minimal_mesh_shaders` and `${ATLANTIS_minimal_cube_TARGET}`, and load their four shader files/two asset files from the `ATLANTIS_RUNTIME_*` compile-definition paths — verified by a fresh Debug build followed by a fresh Release build in the same tree (no missing-artifact failure on either), confirming the paths are genuinely configuration-independent. | Both configurations | Manual, recorded |
| V10 | Regression: every existing GPU-independent suite, every existing `gpu`-labeled suite, every existing headless/image-regression test, and every existing Asset System/Shader System test continues to pass, unchanged, on both configurations. | `ctest`, Step 6 | Both |
| V11 | No golden added or modified; no public API of Platform/RHI/Vulkan Backend/Renderer/Shader System/Asset System changed; no global mutable state introduced in `src/runtime/`; no Client API/IPC, World/ECS, headless Runtime, or Android implementation appears anywhere in the diff. | `git diff --stat`, Step 6 | Manual, recorded |

## Traceability — Spec / ADR → Plan

| Source requirement | Where satisfied |
|---|---|
| Spec 0013 — `atlantis_runtime_host`/`atlantis_runtime` split, private, not a public dependency surface | D1, D1a; V4 |
| Spec 0013 — Runtime depends on/selects Vulkan Backend directly, no `Vk*` leak | D1, D6; V5 |
| Spec 0013 — Full object ownership, initialization order, per-frame order, reverse-order destruction | D4, D6, D7, D8; Step 3 |
| Spec 0013 — `Device::waitIdle()` usage split (general Shutdown vs. early-failure teardown) | D2 (`hasEverRun()`), D8; V1 |
| Spec 0013 — Window destroyed strictly after every GPU resource, structurally guaranteed | D4a; Step 3 |
| Spec 0013 — `platform::shutdown()` called exactly once, no double-call risk | D4a, D8 |
| Spec 0013 — GPU-independent lifecycle/state-machine boundary, no DI/service locator | D2; V1 |
| Spec 0013 — Bootstrap scene: cooked mesh, shader, camera, fixed material only | D5; Step 3 |
| Spec 0013 — Presentation/error classification, including `SwapchainCreationFailed`, exhaustive | D3; V3 |
| Spec 0013 — Zero extent, resize, out-of-date/suboptimal, close, format/extent change | D7 |
| Spec 0013 — Build-tree shader/asset path threading, no hardcoded path, multi-config safe | D9; V9 |
| Spec 0013 — GPU smoke test, deterministic exit, no product-level test hook | D10; V6 |
| Spec 0013 — GPU-independent tests validate pure decisions only | D2, D3; V1, V2, V3 |
| Spec 0013 — Manual resize/minimize/restore/close, golden PNG by-eye comparison | Step 6; V8 |
| Spec 0013 — No windowed readback/pixel comparison; existing headless suite unchanged | Not built anywhere in this Plan; V10, V11 |
| Spec 0013 — Existing demos retained; no public API change; no global mutable state/Client API/World-ECS/headless Runtime/Android | V11 |
| ADR-0046 — Composition, ownership, frame lifecycle | D4–D8 |
| ADR-0047 — Executable/library split, test boundary, Vulkan Backend dependency location | D1, D1a, D10 |

## Rollback Plan

Every step is additive. Reverting the Implementation PR removes
`src/runtime/` and `tests/runtime/` wholesale, and reverts the two
`add_subdirectory()` lines in the root `CMakeLists.txt`. Because no
existing module's source is modified and no golden is touched, revert
restores the exact pre-Plan build and test behavior with no migration
step. Step 7's documentation edits revert independently and do not affect
build/test behavior either way.

## Deviations, objections, and open mechanical details

**No objection to Spec 0013 or ADR-0046/ADR-0047 was found while drafting
or independently reviewing this Plan.** Every `Accepted` decision proved
implementable against the real, current source tree exactly as written.

**Independent review found and fixed one real design gap** at the point an
earlier draft asserted it had already been solved: that earlier draft
avoided a double `platform::shutdown()` call by having `initializeSteps()`'s
six failure branches each hand-sequence their own teardown-then-shutdown,
and having the general `shutdown()` method do the same independently —
correct in every case traced through by hand, but resting entirely on each
hand-written sequence staying correct under future maintenance, with
nothing to catch a mistake. Replaced with `PlatformSession` (D4a): an RAII
guard declared as `RuntimeApplication`'s *first* member so it is destroyed
*last* — a compiler-enforced ordering guarantee, not a convention.
`platform::shutdown()` now has exactly one call site in the entire module;
`initializeSteps()`'s six failure branches no longer perform any teardown
of their own; and `RuntimeLifecycleTracker` gained `hasEverRun()`
specifically so the one, now-unified `shutdown()` method can still honor
Spec 0013's `waitIdle()`-usage distinction without two separate code paths
to get it right. A second, smaller gap was closed in the same round: a
`Device::waitIdle()` failure occurring *during* an otherwise-normal
shutdown now also routes to `RuntimeExitReason::UnrecoverableRuntimeError`
— the earlier draft would have silently reported `Success` in that case.

**The "compiler-enforced exhaustiveness" claim for
`classifyPresentationError()`/`classifySubmitError()` (D3) was corrected
from an assumption to an empirically-verified mechanism** — first during
the same review round (which incorrectly concluded C4715 provided the
enforcement) and then, definitively, by the 2026-08-21 Human Review
Amendment during real Implementation: real MSVC compilation of
`toProcessExitCode()` (a `switch` with no `default`, already handling
every current `RuntimeExitReason` value, nothing after it) failed with the
*same* C4715 diagnostic, proving C4715 does not distinguish a complete
switch from an incomplete one. Genuine compile-time protection instead
requires MSVC's `C4062` (off by default, not implied by `/W4`/`/Wall`),
enabled via a target-scoped `/w14062` on `atlantis_runtime_host` only (D1,
D3) — both the false-C4715-premise finding and the working
`C4062`-based replacement were verified against this project's real `cl`
invocation, not reasoned about in the abstract. No fix required reopening
`Accepted` ADR-0046/ADR-0047, changing any composed module's public API,
or moving a module boundary — this is a Plan-level implementation-mechanism
correction, scoped to D3 and V3 alone.

One honest limitation: **V6/V8 run on one GPU vendor/driver** — the same
disclosed single-vendor limitation every prior GPU-touching spec carries.

**Every mechanical detail this Plan needed to fix is fixed above — none is
left open for Implementation to choose.** Target/namespace/header layout
(D1), the `PlatformSession` RAII guard and why it is declared first (D4a),
the state-machine's exact enumerators, transition table, and
`hasEverRun()` (D2), the error-taxonomy types and their real,
empirically-verified exhaustiveness mechanism (D3), `RuntimeApplication`'s
exact member declaration order and why it produces the required
destruction order for free (D4), the six-step initialization sequence with
no per-step teardown of its own (D6), the ten-step per-frame order with
its own confirmed RAII release story for every early return (D7), the
now-single-call-site shutdown design (D8), the exact CMake
compile-definition/`add_dependencies()` pattern reusing existing exported
variables (D9), and the GPU smoke test's own bounded-loop design with no
product-level test hook (D10) are all decided, not proposed.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: the "Image regression tests" and "Headless
integration tests" items are N/A (this Plan adds no rendered output, and
its own GPU verification is a smoke test plus manual by-eye comparison —
Spec 0013's Testing & Verification Plan). Every other item applies as
written, including V1–V11 all executed and recorded, with V3's `/w14062`
mechanism check, V4/V5's inspection scans, and V7–V11 recorded as manual
verification in the Implementation PR.
