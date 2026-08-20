# Plan: Runtime Host Foundation

- **Spec:** [specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md) (`Approved`, Human Review Approval recorded 2026-08-20)
- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Related ADR(s):**
  [ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md)
  (composition, object ownership, frame lifecycle) and
  [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)
  (executable/library structure, test boundary) — both `Accepted`
  2026-08-20.

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

These are the details Spec 0013 and ADR-0046/ADR-0047 explicitly leave to
Plan stage. Each is decided here, checked directly against the real
headers of every module this Plan composes, so Implementation has nothing
architectural left to choose.

### D1. CMake targets, namespace, directories, dependency graph

| Target | Kind | Location | Links | Notes |
|---|---|---|---|---|
| `atlantis_runtime_host` (alias `Atlantis::RuntimeHost`) | STATIC | `src/runtime/` | PUBLIC `Atlantis::Core`, `Atlantis::Platform`, `Atlantis::RHI`, `Atlantis::VulkanBackend`, `Atlantis::Renderer`, `Atlantis::ShaderSystem`, `Atlantis::ShaderSystemRhiIntegration`, `Atlantis::AssetSystem`; PRIVATE `atlantis_compiler_warnings` | **Not** `Atlantis::RenderGraph` — confirmed by Spec 0013's own Independent Review that `Renderer::drawFrame()` (`src/renderer/include/atlantis/renderer/renderer.h`) already owns RenderGraph construction/compilation/execution internally; no `atlantis/render_graph/*.h` header is included anywhere in this Plan's file list. |
| `atlantis_runtime` (executable) | executable | `src/runtime/` | PRIVATE `Atlantis::RuntimeHost`, `atlantis_compiler_warnings` | The thin, per-OS entry point. No other target may depend on `Atlantis::RuntimeHost`, and no `install()`/export rule is added for it — see D1a. |
| `atlantis_runtime_tests` | executable | `tests/runtime/` | `Atlantis::RuntimeHost`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | GPU-independent — `RuntimeLifecycleState`, error classification, exit-code mapping, and the module-boundary grep. |
| `atlantis_runtime_gpu_tests` | executable | `tests/runtime/` | `Atlantis::RuntimeHost`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | `gpu`-labeled — the real windowed smoke test (Step 5). |

- **Namespace:** `atlantis::runtime`, matching
  [AGENTS.md](../AGENTS.md)'s C++ coding conventions.
- **Public header root:** `src/runtime/include/atlantis/runtime/`,
  matching every other real module's own layout (Core, RHI, Renderer,
  Shader System, Asset System all use this exact pattern) — chosen over
  Tools' flat, no-`include/`-directory style (`src/tools/asset_cooker/`)
  because `Atlantis::RuntimeHost` genuinely has two real consumers of its
  own headers within this module (`atlantis_runtime`'s `main.cpp` and
  `tests/runtime/`), unlike a Tools CLI, which has none beyond its own
  `main.cpp`.
- **`main.cpp`** (the executable's own entry point) lives directly in
  `src/runtime/`, sibling to `CMakeLists.txt` — not under `include/` or
  `src/`, matching `src/tools/shader_compiler/main.cpp`'s own placement.
- **Root `CMakeLists.txt` ordering — genuinely load-bearing, confirmed
  against the real file, not assumed:** `add_subdirectory(src/runtime)`
  must be inserted **after** both `add_subdirectory(assets)` (line 62)
  and `add_subdirectory(shaders/minimal_renderer)` (line 71) — `CMake`'s
  `add_dependencies(target other-target)` requires `other-target` to
  already be a declared target at the point the call is parsed; it is
  **not** deferred or order-independent within a single configure pass.
  This is exactly why `examples/minimal_renderer_demo` (which calls
  `add_dependencies(atlantis_minimal_renderer_demo minimal_mesh_shaders)`)
  and `tests/image_regression` (which calls `add_dependencies(...
  ${ATLANTIS_minimal_cube_TARGET})`) are both already positioned, in the
  real root `CMakeLists.txt`, after line 71 — `src/runtime` needs the
  identical placement for the identical reason (D9's own
  `add_dependencies()` calls). Concretely: `add_subdirectory(src/runtime)`
  is inserted immediately after `add_subdirectory(shaders/minimal_renderer)`
  (line 71) and before the `if(ATLANTIS_BUILD_EXAMPLES)` block (line 73)
  — Runtime is a product executable, not an example, so it does not join
  that `if()` block; it is unconditional, like `assets`/
  `shaders/minimal_renderer` themselves. `tests/runtime` joins the
  existing `ATLANTIS_BUILD_TESTS` block, after `tests/tools/asset_cooker`
  — that block is already positioned after both prerequisite directories
  (confirmed: `tests/image_regression` already lives in this same block
  and already depends on both), so no further ordering constraint applies
  there.

**D1a. Why `Atlantis::RuntimeHost` gets no `install()`/export rule and no
cross-module scanning test.** This repository does not currently `install()`
or export any of its module targets (confirmed: no `install()` call
appears anywhere in the existing `CMakeLists.txt` tree) — Runtime Host
does not need to introduce one to stay "unexported." Per Spec 0013's own
Decisions Requiring Human Review item 1 and ADR-0047's own Negative/
Trade-offs, the "no other module may depend on it" rule is enforced by
documentation and ordinary code review, the same way `module_boundaries.md`'s
existing, never-mechanically-tested "[Tools is] depended on by: nothing"
and "[Runtime is] depended on by: nothing" rules already are for every
other module today — no existing module in this codebase has a reverse-
dependency scanning test, and building the first one here would be new
verification infrastructure with no Spec 0013 requirement driving it.
This Plan does not add one; see V4 for how this is verified instead
(inspection at PR time, same as those existing rules).

### D2. `RuntimeLifecycleState` — the GPU-independent state-machine boundary

`src/runtime/include/atlantis/runtime/lifecycle_state.h`:

```cpp
enum class RuntimeLifecycleState {
  Uninitialized, Initializing, Running, ShuttingDown, ShutDown, Failed,
};

class RuntimeLifecycleTracker {
 public:
  RuntimeLifecycleTracker() noexcept;  // starts Uninitialized
  [[nodiscard]] RuntimeLifecycleState state() const noexcept;

  void beginInitializing();  // Uninitialized -> Initializing
  void markRunning();        // Initializing -> Running
  void markFailed();         // Initializing | Running -> Failed
  void beginShutdown();      // Initializing | Running | Failed -> ShuttingDown;
                              // ShuttingDown | ShutDown -> no-op (idempotent)
  void markShutDown();       // ShuttingDown -> ShutDown

 private:
  RuntimeLifecycleState state_ = RuntimeLifecycleState::Uninitialized;
};
```

Every transition not listed above (e.g. `Running` → `Initializing`,
`ShutDown` → `Running`, `markShutDown()` called from anything but
`ShuttingDown`) is a programmer-error precondition violation, rejected via
`ATLANTIS_CHECK_MSG`, per [AGENTS.md](../AGENTS.md)'s existing convention
— not a new assertion mechanism. This type has **no** dependency beyond
`Atlantis::Core` (for `ATLANTIS_CHECK_MSG`) — it names no Platform, RHI,
or Vulkan Backend type, so it is genuinely testable without a GPU or a
window, independent of what else `Atlantis::RuntimeHost`'s own target
happens to link. `RuntimeApplication` (D6) owns exactly one
`RuntimeLifecycleTracker` and is the only type that calls its mutators.

### D3. Error taxonomy — `RuntimeInitError`, `RuntimeExitReason`, exhaustive RHI-error classification

`src/runtime/include/atlantis/runtime/exit_reason.h`:

```cpp
enum class RuntimeExitReason { Success, InitializationFailed, UnrecoverableRuntimeError };
[[nodiscard]] int toProcessExitCode(RuntimeExitReason reason) noexcept;
// Success -> EXIT_SUCCESS (0); the other two each map to their own
// distinct, fixed nonzero value (exact integers a source-level detail,
// e.g. 1 and 2) -- the requirement fixed here is three DISTINCT values,
// not their literal numbers.
```

`src/runtime/include/atlantis/runtime/init_error.h`:

```cpp
enum class RuntimeInitError {
  PlatformInitFailed, ShaderLoadFailed, DeviceCreateFailed,
  AssetLoadFailed, MeshCreateFailed, CameraBufferCreateFailed,
};
[[nodiscard]] const char* toString(RuntimeInitError error) noexcept;  // for logging only
```

Every `RuntimeInitError` value maps uniformly to
`RuntimeExitReason::InitializationFailed` — there is exactly one
initialization-failure exit-code category (Spec 0013's own Requirements),
so no per-`RuntimeInitError` classification function is needed; the
mapping is applied once, at `createRuntimeApplication()`'s own single
`Err` return point (D6).

`src/runtime/include/atlantis/runtime/error_classification.h`:

```cpp
// Every PresentationError/SubmitError enumerator that acquireNextTarget()/
// present()/submit() can actually return as Err(...) is, uniformly,
// Runtime-unrecoverable (Spec 0013's own Presentation-and-error-state
// table) -- these two functions exist so that mapping is exhaustive and
// *compiler-enforced*: each switches over every real enumerator with NO
// default case, so a future RHI change adding a new PresentationError/
// SubmitError variant fails to compile here rather than silently falling
// through -- directly closing the exact class of gap Spec 0013's own
// independent review round found and fixed (the originally-omitted
// SwapchainCreationFailed).
[[nodiscard]] RuntimeExitReason classifyPresentationError(atlantis::rhi::PresentationError error) noexcept;
[[nodiscard]] RuntimeExitReason classifySubmitError(atlantis::rhi::SubmitError error) noexcept;
```

Both always return `RuntimeExitReason::UnrecoverableRuntimeError` today —
confirmed against `src/rhi/include/atlantis/rhi/types.h`'s real, current
enums: `PresentationError{SurfaceLost, SwapchainCreationFailed, DeviceLost,
Unknown}` (four values) and `SubmitError{QueueSubmitFailed, DeviceLost}`
(two values). The value is the exhaustiveness guarantee, not per-value
differentiation — Spec 0013's own outcome table assigns the identical
response to every one of these six values.

### D4. `BootstrapConfig` and `RuntimeApplication` — object model, ownership, destruction order

`src/runtime/include/atlantis/runtime/bootstrap_config.h`:

```cpp
struct BootstrapConfig {
  std::string applicationName = "Atlantis Runtime";
  std::string vertexShaderSpirvPath;
  std::string vertexShaderReflectionPath;
  std::string fragmentShaderSpirvPath;
  std::string fragmentShaderReflectionPath;
  std::string assetArtifactPath;
  std::string assetMetadataPath;
  bool enableValidationLayers = true;
};
```

A plain, caller-populated value struct — not a service, not a builder, no
default path values baked into `Atlantis::RuntimeHost` itself. Every path
is supplied by the caller (`main.cpp`, or `tests/runtime/`'s own smoke
test), sourced from CMake-injected compile definitions (D9) — no path is
ever hardcoded inside `src/runtime/`'s own library sources. This is the
whole of Runtime's own configuration surface (Decisions Requiring Human
Review item 8) — no command-line parsing, no config file, no environment
variable is read by `Atlantis::RuntimeHost`.

**`RuntimeApplication` (`src/runtime/include/atlantis/runtime/runtime_application.h`)
— the composition object:**

```cpp
class RuntimeApplication {
 public:
  RuntimeApplication() = default;
  ~RuntimeApplication();

  RuntimeApplication(const RuntimeApplication&) = delete;
  RuntimeApplication& operator=(const RuntimeApplication&) = delete;
  RuntimeApplication(RuntimeApplication&&) noexcept = default;
  RuntimeApplication& operator=(RuntimeApplication&&) noexcept = default;

  void runFrame();                                  // one iteration (D7)
  [[nodiscard]] bool shouldContinue() const noexcept;
  RuntimeExitReason shutdown();                      // idempotent (D8)

 private:
  friend atlantis::Result<RuntimeApplication, RuntimeInitError> createRuntimeApplication(const BootstrapConfig&);
  atlantis::Result<std::monostate, RuntimeInitError> initializeSteps(const BootstrapConfig& config);

  // Declared in EXACTLY this order because C++ destroys non-static data
  // members in the REVERSE of their declaration order -- this ordering IS
  // Spec 0013's/ADR-0046's fixed Shutdown sequence (Material, Texture,
  // Buffer, Mesh, Presentation, Device), obtained for free from ordinary
  // member destruction rather than a manually-sequenced list of reset()
  // calls that could drift out of order under a future edit. See D8 for
  // why shutdown() still performs this teardown EXPLICITLY (not by
  // merely falling out of scope) and why that is not a contradiction.
  std::unique_ptr<atlantis::rhi::Device> device_;
  std::unique_ptr<atlantis::rhi::Presentation> presentation_;      // lazy: constructed on first SurfaceCreated
  std::optional<atlantis::renderer::Mesh> mesh_;
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer_;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture_;           // lazy: first frame's extent-change check
  std::optional<atlantis::renderer::Material> material_;           // lazy: first frame's format-change check (Bootstrap Sequencing Detail)

  atlantis::renderer::Renderer renderer_;             // stateless, default-constructed
  RuntimeLifecycleTracker lifecycle_;
  RuntimeExitReason lastExitReason_ = RuntimeExitReason::Success;
  bool closeRequested_ = false;
  std::optional<atlantis::rhi::Format> lastSeenFormat_;
  std::optional<atlantis::rhi::Extent2D> lastSeenExtent_;
  atlantis::rhi::VertexInputLayout vertexInputLayout_;             // resolved once at init, reused for every Material rebuild
  std::vector<std::uint32_t> vertexSpirv_;                         // retained for every Material rebuild
  std::vector<std::uint32_t> fragmentSpirv_;
};

[[nodiscard]] atlantis::Result<RuntimeApplication, RuntimeInitError> createRuntimeApplication(const BootstrapConfig& config);
```

- **Move-only** (implicit, from its `unique_ptr`/`optional`/`Mesh`/
  `Material` members), matching every other composition-adjacent type in
  this codebase.
- **No abstract service interface, no DI container, no service locator.**
  `initializeSteps()` calls `platform::initialize()`,
  `atlantis::shader_system::loadReflectionMetadata()`,
  `atlantis::vulkan_backend::createDevice()`,
  `atlantis::asset_system::loadStaticMeshAsset()`,
  `atlantis::renderer::createMesh()`, and `device_->createBuffer()`
  directly, by name — the exact same calls, in the exact same order,
  every existing composition root (`examples/minimal_renderer_demo`,
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`) already
  makes. Nothing here is behind an interface a test could substitute; see
  D2/D3 for what actually is unit-testable and why that scope is
  deliberately narrower.

### D5. Bootstrap scene — asset/shader loading and the fixed `DrawItem`

- The `minimal_mesh` vertex/fragment SPIR-V and reflection JSON are read
  via plain `std::ifstream` (matching `loadSpirvFile()`'s existing
  pattern in `examples/minimal_renderer_demo/main.cpp`, duplicated — not
  shared — into `src/runtime/src/runtime_application.cpp`, the same
  "duplicated, not shared" precedent
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`'s own
  top-of-file comment already establishes for this exact helper) and
  `atlantis::shader_system::loadReflectionMetadata()`, from the paths
  `BootstrapConfig` carries — never a hardcoded path, never a
  working-directory-relative one (D9).
- `VertexInputLayout` is resolved once, via
  `atlantis::shader_system::rhi_integration::toVertexInputLayout()`, with
  the identical `Vertex{float position[3]; float color[3];}` schema and
  `MeshVertexAttributeSchema` table every existing windowed/fixture
  composition already uses — this is not a new schema, it is required to
  be identical to the one the checked-in shader and the checked-in
  `minimal_cube` asset were both authored against.
- The `minimal_cube` asset is loaded via
  `atlantis::asset_system::loadStaticMeshAsset()` from the paths
  `BootstrapConfig` carries, then passed to
  `atlantis::renderer::createMesh()` exactly as
  `setUpMinimalCubeFixtureFromAsset()` already does — no intermediate
  copy or transformation.
- **Camera:** fixed (not orbiting) — `lookAt(0, 1.5, 2.5, 0, 0, 0)` /
  60° vertical FOV perspective, the exact values
  `tests/image_regression/fixture/minimal_cube_fixture.cpp` already uses
  — chosen over `minimal_renderer_demo`'s own orbiting camera because a
  **static** camera is what makes the manual by-eye comparison against
  the existing golden PNG (Spec 0013's own Testing & Verification Plan)
  meaningful frame-to-frame; an orbiting camera would only match the
  golden's own single fixed viewpoint on whichever frame happens to
  align, which is not a reliable manual-verification signal. `aspect`
  is recomputed every frame from the acquired target's own extent
  (matching `minimal_renderer_demo`'s existing `aspect` computation), so
  the window's own aspect ratio — not the golden's fixed 1:1 — is what
  the camera actually renders at; the golden comparison is therefore a
  by-eye "is this recognizably the same cube, correctly shaded and
  depth-ordered" check, not a claim of geometric identity to the golden's
  own square framing.
- `DrawItem{ .mesh = &*mesh_, .material = &*material_, .objectToWorld =
  identityMatrix() }` — exactly one, rebuilt fresh each frame from the
  current `material_` (never cached across a `material_` rebuild).

### D6. Initialization sequence — `initializeSteps()`

`initializeSteps(const BootstrapConfig& config)` runs the six steps Spec
0013's own Initialization order fixes, each with its own narrow,
immediate teardown-and-return on failure — **not** routed through the
general `shutdown()` method (D8 explains why):

1. `platform::initialize()`. `Err` → return
   `Err(RuntimeInitError::PlatformInitFailed)` immediately — nothing else
   exists yet.
2. Load `config.vertexShaderSpirvPath`/`fragmentShaderSpirvPath` and both
   reflection JSON paths; resolve `vertexInputLayout_`. Any failure →
   `platform::shutdown()`, then return
   `Err(RuntimeInitError::ShaderLoadFailed)`.
3. `vulkan_backend::createDevice({.applicationName = config.applicationName,
   .enableValidationLayers = config.enableValidationLayers})` → `device_`.
   `Err` → `platform::shutdown()`, return
   `Err(RuntimeInitError::DeviceCreateFailed)`.
4. `asset_system::loadStaticMeshAsset(config.assetArtifactPath,
   config.assetMetadataPath)`. `Err` → `device_.reset()`;
   `platform::shutdown()`; return
   `Err(RuntimeInitError::AssetLoadFailed)`. (Nothing else has been
   constructed yet — no `waitIdle()` call: see D8.)
5. `renderer::createMesh(*device_, vertexInputLayout_,
   assetData.vertexBytes().data(), assetData.vertexBytes().size(),
   assetData.indices().data(), assetData.indices().size())` → `mesh_`.
   `Err` → same teardown as step 4 (`device_.reset()`;
   `platform::shutdown()`), return
   `Err(RuntimeInitError::MeshCreateFailed)`.
6. `device_->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes
   = sizeof(float) * 32})` → `cameraBuffer_`. `Err` → `mesh_.reset()`;
   `device_.reset()`; `platform::shutdown()`; return
   `Err(RuntimeInitError::CameraBufferCreateFailed)`.

On success: `lifecycle_.markRunning()`; return `Ok(std::monostate{})`.
`createRuntimeApplication()` itself is a thin wrapper: construct a
default `RuntimeApplication`, call `beginInitializing()`, call
`initializeSteps(config)`, and return `Ok(std::move(app))` on success or
propagate the `Err` (the local `app`'s own destructor then runs D8's
already-idempotent no-op path, since `initializeSteps()`'s own failure
branches already leave it in `ShutDown` — see D8).

**No `Material` construction step exists here** — Spec 0013's own
Bootstrap Sequencing Detail fixes this deliberately: no real swapchain
format is known before the first `SurfaceCreated`, so `Material`'s first
construction happens inside `runFrame()`'s own format-change check (D7),
exactly the code path that later handles every subsequent format change
identically.

### D7. Per-frame order — `runFrame()`/`shouldContinue()`

```cpp
bool RuntimeApplication::shouldContinue() const noexcept {
  return !platform::shouldQuit() && !closeRequested_ &&
         lifecycle_.state() != RuntimeLifecycleState::Failed;
}
```

`runFrame()` (callable only while `shouldContinue()`; an `ATLANTIS_CHECK`
guards the precondition):

1. `platform::processEvents()`, handling each event exactly as
   `minimal_renderer_demo` already does — `SurfaceCreated` (construct
   `presentation_` via `vulkan_backend::createPresentation()`; a second
   occurrence while `presentation_` already exists calls
   `lifecycle_.markFailed()`), `WindowResize`
   (`presentation_->notifyResized()`, no-op if `presentation_` does not
   exist yet), `WindowCloseRequested` (`closeRequested_ = true`),
   `SurfaceDestroyed` (`markFailed()` if `presentation_` still exists —
   an anomaly on Windows, per Spec 0013's own analysis of
   `windows_platform.cpp`'s real `WM_DESTROY` handling), `Quit`/
   `FocusGained`/`FocusLost`/`ApplicationPause`/`ApplicationResume`
   (logged only).
2. If `!presentation_ || closeRequested_ ||
   lifecycle_.state() == RuntimeLifecycleState::Failed`: return — no
   render this iteration.
3. `presentation_->acquireNextTarget()`. `Err` →
   `lifecycle_.markFailed()` (the specific `PresentationError` is logged
   via `classifyPresentationError()`'s own input, purely for the log
   message — the classification result itself is always
   `UnrecoverableRuntimeError`, per D3); return. `Ok(nullptr)` → return
   (zero extent or an internally-deferred out-of-date swapchain; no
   Vulkan call this iteration). `Ok(target)` → continue.
4. Format-change check: if `!lastSeenFormat_.has_value() ||
   presentation_->metadata().format != *lastSeenFormat_` — rebuild
   `material_` from `vertexSpirv_`/`fragmentSpirv_`/`vertexInputLayout_`
   and the current format; on failure, log and **keep the existing**
   `material_` (or, on the very first frame, leave it unset), retry next
   frame — `lastSeenFormat_` is only updated on success, matching
   `minimal_renderer_demo`'s own unbounded-retry behavior exactly.
5. Extent-change check: if `!lastSeenExtent_.has_value() ||
   target->extent() != *lastSeenExtent_` — recreate `depthTexture_`; same
   keep-existing-and-retry-on-failure pattern as step 4.
6. If `!material_ || !depthTexture_`: nothing valid to draw yet — return
   (the acquired `target` is simply dropped via RAII; this is legal per
   `RenderTarget`'s own contract — no leaked GPU state, since nothing was
   submitted).
7. Write this frame's fixed camera view/projection into
   `cameraBuffer_->mappedData()` (D5). Build the one `DrawItem`.
   `device_->createCommandList()`. `Err` → `lifecycle_.markFailed()`;
   return.
8. `renderer_.drawFrame(*commandList, *target, *depthTexture_,
   *cameraBuffer_, drawItems, atlantis::rhi::ResourceState::PresentSource)`.
9. `device_->submit(std::move(commandList), *target)`. `Err` →
   `lifecycle_.markFailed()`; return.
10. `presentation_->present(std::move(target), std::move(submissionSignal))`.
    `Err` → `lifecycle_.markFailed()`; return.

**Mid-frame close, confirmed unnecessary to special-case:** because step
1 always completes before step 3's `acquireNextTarget()` call, and
`closeRequested_` short-circuits step 2 before any acquire happens this
same iteration, there is no code path in which a `RenderTarget` is
acquired and then abandoned because a close request arrived mid-frame —
exactly Spec 0013's own analysis, now expressed as the concrete step
order above.

### D8. Shutdown — avoiding a double `platform::shutdown()` call

`RuntimeExitReason RuntimeApplication::shutdown()`:

```cpp
if (lifecycle_.state() == RuntimeLifecycleState::ShutDown) return lastExitReason_;  // idempotent
const bool hadFailure = (lifecycle_.state() == RuntimeLifecycleState::Failed);
lifecycle_.beginShutdown();
if (device_) {
  auto result = device_->waitIdle();
  if (result.isErr()) { /* log only -- teardown continues regardless */ }
}
material_.reset(); depthTexture_.reset(); cameraBuffer_.reset();
mesh_.reset(); presentation_.reset(); device_.reset();
platform::shutdown();
for (const auto& event : platform::processEvents()) { /* log only */ }
lifecycle_.markShutDown();
lastExitReason_ = hadFailure ? RuntimeExitReason::UnrecoverableRuntimeError
                              : RuntimeExitReason::Success;
return lastExitReason_;
```

**This is the general Shutdown path — reachable only from the frame
loop** (`main.cpp`'s own `while (app.shouldContinue()) { app.runFrame();
}` loop, once it exits, always calls `shutdown()` next) or from the
destructor as an idempotent backstop. It is **not** what
`initializeSteps()`'s own early-failure branches (D6) call — those
perform their own narrower, explicit teardown (reset only what was
actually constructed so far, no `waitIdle()`, since nothing could have
been submitted before `runFrame()` ever runs) and **transition
`lifecycle_` directly to `ShutDown`** (via `beginShutdown()` +
`markShutDown()` called back-to-back inside `initializeSteps()`'s own
failure paths, with `lastExitReason_` set to
`RuntimeExitReason::InitializationFailed`) after already having called
`platform::shutdown()` themselves.

**Why this split matters, concretely:** `platform::shutdown()`'s own real
implementation (`src/platform/src/windows/windows_platform.cpp`) asserts
`ATLANTIS_CHECK_MSG(s.initialized && !s.shutDown, "shutdown() called
without a successful initialize(), or called twice")`. If the general
`shutdown()` method above were called unconditionally from the
destructor after an early `initializeSteps()` failure had already called
`platform::shutdown()` itself, this assertion would fire — a real crash,
not a theoretical one. Because `initializeSteps()`'s own failure paths
leave `lifecycle_` in `ShutDown` directly, the destructor's own call to
`shutdown()` (below) sees `state() == ShutDown` and returns immediately,
calling `platform::shutdown()` zero additional times. This is the
concrete reason `RuntimeLifecycleState` needs a `ShutDown` state reachable
from `Initializing` (via the early-failure path) as well as from
`Running`/`Failed` (via the general path) — both converge on the same
terminal, idempotent state.

`RuntimeApplication::~RuntimeApplication()` calls `shutdown()`
unconditionally — a genuine no-op if `shutdown()` was already called
explicitly (the normal path, from `main.cpp`/the GPU smoke test), and the
correct, narrow no-op described above if `initializeSteps()`'s own
failure path already ran.

### D9. Build-tree path threading — reusing existing exported CMake variables, no hardcoded path

`src/runtime/CMakeLists.txt` (for `atlantis_runtime`) and
`tests/runtime/CMakeLists.txt` (for `atlantis_runtime_gpu_tests`) each:

```cmake
target_compile_definitions(<target> PRIVATE
  ATLANTIS_RUNTIME_SHADER_DIR="${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH="${ATLANTIS_minimal_cube_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_ASSET_METADATA_PATH="${ATLANTIS_minimal_cube_METADATA_PATH}"
)
add_dependencies(<target> minimal_mesh_shaders ${ATLANTIS_minimal_cube_TARGET})
```

- **No new CMake mechanism.** `ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR`
  (an absolute, configuration-independent build-tree path — ADR-0031) and
  `ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH,TARGET}` (Plan
  0012's own D1/D2, also configuration-independent — confirmed:
  `src/asset_system/CMakeLists.txt` writes to
  `${CMAKE_BINARY_DIR}/assets`, never a `$<CONFIG>`-qualified path) are
  both **already exported, real CMake variables** — this Plan only reads
  them.
- Runtime reads the shader files **directly from
  `ATLANTIS_RUNTIME_SHADER_DIR`** at their real build-tree location — no
  `POST_BUILD copy_if_different` step next to the executable's own output
  (unlike `minimal_renderer_demo`'s own convention). This is simpler, not
  merely different: because the path is absolute and
  configuration-independent, there is nothing to copy — the file already
  lives at a fixed, known location regardless of which directory the
  executable is launched from or which configuration built it.
  `RuntimeApplication`'s own code appends the fixed filenames
  (`minimal_mesh.vert.spv`, `.vert.refl.json`, `.frag.spv`,
  `.frag.refl.json`) onto `ATLANTIS_RUNTIME_SHADER_DIR` — the four
  filenames themselves are a fixed constant matching every existing
  consumer, not derived from any macro.
- `main.cpp` and the GPU smoke test each construct their own
  `BootstrapConfig` from these three macros directly — `src/runtime/`'s
  library sources never reference an `ATLANTIS_RUNTIME_*` macro at all
  (macros are consumed only by the two `.cpp` files that define `main()`/
  the test's own entry, both outside the library target), keeping
  `Atlantis::RuntimeHost` itself fully parameterized and free of any
  build-tree-path assumption.
- `add_dependencies()` guarantees the shader-compile and asset-cook
  targets have already run before either consumer builds — matching
  `tests/image_regression/CMakeLists.txt`'s own identical two-line
  pattern exactly.

### D10. GPU smoke test — deterministic exit with no product-level test hook

`tests/runtime/runtime_smoke_gpu_tests.cpp` links `Atlantis::RuntimeHost`
directly (not `atlantis_runtime`, which is never itself invoked as a
subprocess by any test in this Plan) and reuses `RuntimeApplication`'s
own public API exactly as `main.cpp` does, with a bounded loop instead of
an unbounded one:

```cpp
TEST_CASE("Runtime constructs a window and completes real windowed acquire/draw/submit/present frames",
          "[runtime][gpu]") {
  BootstrapConfig config{ /* four ATLANTIS_RUNTIME_* macros, enableValidationLayers = true */ };
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());

  constexpr int kSmokeTestFrameCount = 3;  // matches this repository's
      // own existing kCycleCount precedent (frame_execution_demo,
      // headless_rendering_demo, image_regression_gpu_tests)
  for (int i = 0; i < kSmokeTestFrameCount && app.shouldContinue(); ++i) {
    app.runFrame();
  }
  REQUIRE(app.shouldContinue());  // did not fail during those frames

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
}
```

- **No CLI flag, no environment variable, no test-only constructor
  parameter is added to `RuntimeApplication`, `BootstrapConfig`, or
  `atlantis_runtime`'s own `main.cpp`.** The test achieves a bounded,
  deterministic exit purely by calling the already-public `shutdown()`
  method itself, after a fixed number of `runFrame()` calls, instead of
  waiting for a real `WindowCloseRequested` event — no window-close
  simulation of any kind is needed, since `shutdown()` was already
  designed (D8) to be safely callable at any point once `Running`.
- **Validation Layers verification is not a separate manual log-grep
  step for this test.** Confirmed directly against
  `src/vulkan_backend/src/validation.cpp`: the installed debug messenger
  callback calls `ATLANTIS_CHECK_MSG(false, message)` followed by
  `std::abort()` for any `WARNING`/`ERROR`-severity message — with
  `enableValidationLayers = true` (this test's own config), a genuine
  validation hit crashes the test process immediately, which `ctest`
  already reports as a failure. A clean `REQUIRE` pass is therefore
  itself real evidence of zero Validation Layer warnings/errors for the
  exact sequence this test exercises — grepping the captured test output
  for `VUID`/`Validation Error` afterward remains good practice (matching
  every prior GPU-touching spec's own verification report convention),
  but is confirmatory, not the only enforcement mechanism.
- **This is the first `gpu`-labeled test in this repository that creates
  a real, visible OS window during an automated `ctest` run** — every
  prior GPU-required test (`atlantis_vulkan_backend_gpu_tests`,
  `atlantis_render_graph_tests`, `atlantis_image_regression_gpu_tests`)
  is offscreen-only. This is a disclosed, deliberate consequence of Spec
  0013's own approved design (a windowed smoke test is explicitly
  required), not an oversight — flagged here so a human running the full
  suite is not surprised by a window briefly appearing and disappearing.

## Milestones / Task Breakdown

Each step leaves the repository configuring, building, and (from Step 1
onward) its own tests passing. Step 3 is the one step whose own new code
cannot be exercised by a new automated test in the same step (it is
GPU-dependent, real-window code) — it is verified in Step 5/6 instead;
this is disclosed, not silently skipped.

### Step 1 — Module skeleton, `RuntimeLifecycleState`, exit-reason type (**atomic**)

Atomic because CMake rejects a STATIC library with no sources, and the
root `CMakeLists.txt` edit references a directory that must already
exist.

- `src/runtime/CMakeLists.txt` — declares `atlantis_runtime_host` +
  `Atlantis::RuntimeHost` alias, with D1's full PUBLIC dependency list.
  (The `atlantis_runtime` executable target is **not** declared yet —
  added in Step 4, once there is a `main.cpp` for it to build; declaring
  an empty/placeholder executable here would violate this step's own
  "leaves the repo building meaningfully" bar for no benefit.)
- `src/runtime/include/atlantis/runtime/lifecycle_state.h` /
  `src/runtime/src/lifecycle_state.cpp` — D2, in full.
- `src/runtime/include/atlantis/runtime/exit_reason.h` /
  `src/runtime/src/exit_reason.cpp` — `RuntimeExitReason`,
  `toProcessExitCode()` (D3).
- `tests/runtime/CMakeLists.txt`,
  `tests/runtime/lifecycle_state_tests.cpp`,
  `tests/runtime/exit_reason_tests.cpp` — V1, V2 (only
  `atlantis_runtime_tests` exists after this step;
  `atlantis_runtime_gpu_tests` is declared in Step 5).
- Root `CMakeLists.txt` — `add_subdirectory(src/runtime)` and
  `add_subdirectory(tests/runtime)` (D1's ordering).

### Step 2 — `RuntimeInitError`, exhaustive RHI-error classification

- `src/runtime/include/atlantis/runtime/init_error.h` /
  `src/runtime/src/init_error.cpp` — D3.
- `src/runtime/include/atlantis/runtime/error_classification.h` /
  `src/runtime/src/error_classification.cpp` — `classifyPresentationError()`/
  `classifySubmitError()`, the two no-default exhaustive switches (D3).
- `tests/runtime/error_classification_tests.cpp` — V3: every one of the
  six real enumerators (four `PresentationError`, two `SubmitError`)
  individually asserted to map to `UnrecoverableRuntimeError`.

### Step 3 — `BootstrapConfig`, `RuntimeApplication` (init/frame/shutdown), the fixed bootstrap scene

The largest step: this is where `Atlantis::RuntimeHost` first depends on
Platform/RHI/Vulkan Backend/Renderer/Shader System/Asset System for real,
so it is not further subdivided — D4 through D8's design is genuinely one
cohesive unit (the same member layout, the same lifecycle tracker, and
the same failure-handling convention thread through initialization,
per-frame execution, and shutdown together).

- `src/runtime/include/atlantis/runtime/bootstrap_config.h` — D4.
- `src/runtime/include/atlantis/runtime/runtime_application.h` /
  `src/runtime/src/runtime_application.cpp` — the full class from D4,
  `initializeSteps()`/`createRuntimeApplication()` from D6, `runFrame()`/
  `shouldContinue()` from D7, `shutdown()`/`~RuntimeApplication()` from
  D8, and the fixed camera/`DrawItem` construction from D5.
- No new automated test in this step (see this section's own preamble) —
  `atlantis_runtime_host` must still **compile** cleanly against every
  module it now links, which is itself a real, meaningful check this
  step's own build performs.

### Step 4 — Thin `atlantis_runtime` Windows executable, build-tree path wiring

- `src/runtime/main.cpp` — `int main()` (matching every existing
  windowed composition root's own choice, not `WinMain`, so
  `ATLANTIS_LOG_*` output remains visible in a console window exactly
  like `minimal_renderer_demo`/`frame_execution_demo` already do; a Plan
  choosing `WinMain` instead would be an unexplained departure from
  established precedent with no stated benefit). Builds a
  `BootstrapConfig` from the three `ATLANTIS_RUNTIME_*` macros (D9),
  calls `createRuntimeApplication()`, logs and returns
  `toProcessExitCode(RuntimeExitReason::InitializationFailed)` on `Err`;
  on `Ok`, runs `while (app.shouldContinue()) { app.runFrame(); }`, then
  `return toProcessExitCode(app.shutdown());`.
- `src/runtime/CMakeLists.txt` — add the `atlantis_runtime` executable
  target (D1), its `target_compile_definitions()`/`add_dependencies()`
  (D9).

### Step 5 — GPU smoke test

- `tests/runtime/runtime_smoke_gpu_tests.cpp` — D10, in full.
- `tests/runtime/CMakeLists.txt` — declares
  `atlantis_runtime_gpu_tests`, `gpu`-labeled
  (`catch_discover_tests(... PROPERTIES LABELS "gpu")`, matching every
  existing `gpu`-labeled target's own registration pattern), with D9's
  compile definitions and `add_dependencies()`.
- `src/runtime/include/atlantis/runtime/module_boundary_check.h`? —
  **not added.** V5 (no `Vk*`/`vulkan/` include anywhere under
  `src/runtime/`) is implemented as a plain grep run and recorded at
  verification time (Step 6), matching V4's own already-decided
  inspection-based approach (D1a) — this Plan does not build a second,
  narrower automated scanning test alongside a decision that the broader
  reverse-dependency scan is out of scope; both are inspection-based for
  the same stated reason.

### Step 6 — Full verification (Debug/Release, GPU-independent, GPU-required, manual)

- Clean Debug and Release configure + build.
- `ctest -LE gpu` and `ctest -L gpu`, both configurations — confirms V1–V3
  plus every existing GPU-independent suite unaffected (V10).
- `atlantis_runtime_gpu_tests` run on real hardware — V6.
- Manual interactive verification, run against the real
  `atlantis_runtime` executable (not the smoke test): a visible window
  shows the `minimal_cube` mesh, compared by eye against
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
  reflect that it is now implemented (exact wording an Implementation-
  time detail, matching every prior Plan's own precedent of fixing *what*
  changes, not final prose).
- `docs/architecture/module_boundaries.md` — the `## Atlantis Runtime`
  section's own `Depends on` line corrected to remove RenderGraph (Spec
  0013's own Architectural Impact finding, explicitly deferred to this
  Plan) and to name the `atlantis_runtime_host`/`atlantis_runtime` split;
  status updated from `PROPOSED` to reflect this specific section is now
  implemented (the document's own broader, whole-document `PROPOSED`
  banner, covering every other still-undocumented-as-built module, is
  unrelated systemic staleness this Plan does not touch, matching the
  same judgment already applied during Spec 0012's own post-merge
  closeout).
- `docs/project-blueprint.md` — a new Milestone entry for Runtime Host
  Foundation, matching the existing per-milestone format (Governance
  state, scope delivered, acceptance signal).
- `src/README.md` — add a `runtime/` entry, matching the existing
  per-module paragraph format.
- `specs/README.md` — Spec 0013's own Implementation column updated from
  "Not started" to "Implemented and merged via [PR #N]", matching every
  prior spec's own registry-update convention; this Plan's own
  `Related Plan(s)`/Plan-column update (this current PR) is the only
  `specs/README.md` edit this Plan-drafting round itself makes — Step 7's
  edit here is future Implementation-PR work, not performed now.

## Files / Modules Touched (expected)

**New — Runtime module**

- `src/runtime/CMakeLists.txt`
- `src/runtime/include/atlantis/runtime/{lifecycle_state,exit_reason,init_error,error_classification,bootstrap_config,runtime_application}.h`
- `src/runtime/src/{lifecycle_state,exit_reason,init_error,error_classification,runtime_application}.cpp`
- `src/runtime/main.cpp`

**New — tests**

- `tests/runtime/{CMakeLists.txt,lifecycle_state_tests.cpp,exit_reason_tests.cpp,error_classification_tests.cpp,runtime_smoke_gpu_tests.cpp}`

**Modified**

- `CMakeLists.txt` (root) — two `add_subdirectory()` lines
  (`src/runtime`, `tests/runtime`).
- `specs/0013-runtime-host-foundation.md` — `Related Plan(s)` field
  updated to link this Plan.
- `specs/README.md` — Spec 0013's own Plan column updated (this
  Plan-drafting round). The Implementation-column update described under
  Step 7 above is future Implementation-PR work, not part of this round.

**Explicitly not touched (this Plan-drafting round):** `src/rhi/`,
`src/renderer/`, `src/render_graph/`, `src/vulkan_backend/`,
`src/platform/`, `src/shader_system/`, `src/asset_system/`,
`src/tools/`, `assets/`, `shaders/`, `examples/`,
`tests/image_regression/`, `tests/asset_system/`,
`tests/shader_system/`, `cmake/`, every existing example/demo, every
existing `Accepted` ADR, and (beyond the two fields named above)
`AGENTS.md`/`docs/architecture/module_boundaries.md`/
`docs/project-blueprint.md`/`src/README.md` — those four are
Implementation-PR work per Step 7, not edited by drafting this Plan.

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

- Step 3 needs Step 2 because `initializeSteps()`'s own `Err` returns are
  typed `RuntimeInitError`, and `runFrame()`'s own failure logging calls
  `classifyPresentationError()`/`classifySubmitError()`.
- Step 4 needs Step 3 because `main.cpp` calls `createRuntimeApplication()`
  and drives the object it returns.
- Step 5 needs Step 4's own CMake path-wiring pattern (D9) to exist once,
  reused verbatim for the second consumer (`atlantis_runtime_gpu_tests`).
- Step 6 needs Step 5's own executable to exist to run it.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | `RuntimeLifecycleState`: every legal transition succeeds; every illegal one (`Running`→`Initializing`, `ShutDown`→`Running`, `markShutDown()` from anything but `ShuttingDown`) is rejected via `ATLANTIS_CHECK`; `beginShutdown()` is idempotent from `ShuttingDown`/`ShutDown`, reaching `ShutDown` exactly once with no double `platform::shutdown()` call (exercised against the pure tracker type only — no real `Device`/`Platform`). | `lifecycle_state_tests.cpp` | GPU-independent |
| V2 | `RuntimeExitReason`→process-exit-code mapping: three distinct values; `Success`→`EXIT_SUCCESS`. | `exit_reason_tests.cpp` | GPU-independent |
| V3 | `classifyPresentationError()`/`classifySubmitError()`: each of the four real `PresentationError` values and both real `SubmitError` values individually maps to `UnrecoverableRuntimeError` — confirmed exhaustive against `src/rhi/include/atlantis/rhi/types.h`'s actual current enum. | `error_classification_tests.cpp` | GPU-independent |
| V4 | No other top-level module depends on `Atlantis::RuntimeHost` — verified by inspection/grep across every other module's `CMakeLists.txt` at PR time (D1a; matching the existing, equally inspection-based "[Runtime/Tools are] depended on by: nothing" rules already in `module_boundaries.md`). | Manual/grep, recorded in the PR | Manual |
| V5 | No `Vk*` type and no `#include <vulkan/...>` appears anywhere under `src/runtime/` — verified by inspection/grep at verification time (D1a's own stated reasoning: matching V4's inspection-based approach rather than a second, narrower automated scan). | Manual/grep, recorded in the PR | Manual |
| V6 | Real windowed acquire/draw/submit/present succeeds for `kSmokeTestFrameCount` consecutive frames, then `shutdown()` returns `RuntimeExitReason::Success` — a genuine Vulkan Validation Layer warning/error during this run aborts the process (`validation.cpp`'s own existing mechanism), so a clean pass is direct evidence of zero warnings/errors for this exact sequence. | `runtime_smoke_gpu_tests.cpp` | `gpu`-labeled |
| V7 | Debug **and** Release: clean configure + build; `ctest -LE gpu` and `ctest -L gpu` both green. | Both configurations | Manual, recorded |
| V8 | Real, human-driven `atlantis_runtime`: visible window shows the correctly-shaded, correctly depth-ordered `minimal_cube` mesh, compared by eye against the existing golden PNG; interactive resize (depth `Texture` recreated, `Pipeline` not); minimize/restore (no Vulkan call while minimized); normal close exits with `RuntimeExitReason::Success`; Vulkan Validation Layers grepped clean throughout, both configurations. | Manual, recorded in the PR | Manual |
| V9 | `atlantis_runtime`/`atlantis_runtime_gpu_tests` each correctly `add_dependencies()` on `minimal_mesh_shaders` and `${ATLANTIS_minimal_cube_TARGET}`, and load their four shader files/two asset files from the `ATLANTIS_RUNTIME_*` compile-definition paths — verified by a fresh Debug build followed by a fresh Release build in the same tree (no missing-artifact failure on either), confirming the paths are genuinely configuration-independent, not merely asserted to be. | Both configurations | Manual, recorded |
| V10 | Regression: every existing GPU-independent suite, every existing `gpu`-labeled suite, every existing headless/image-regression test, and every existing Asset System/Shader System test continues to pass, unchanged, on both configurations. | `ctest`, Step 6 | Both |
| V11 | No golden added or modified; no public API of Platform/RHI/Vulkan Backend/Renderer/Shader System/Asset System changed; no global mutable state introduced in `src/runtime/`; no Client API/IPC, World/ECS, headless Runtime, or Android implementation appears anywhere in the diff. | `git diff --stat`, Step 6 | Manual, recorded |

## Traceability — Spec / ADR → Plan

| Source requirement | Where satisfied |
|---|---|
| Spec 0013 — `atlantis_runtime_host`/`atlantis_runtime` split, private, not a public dependency surface | D1, D1a; V4 |
| Spec 0013 — Runtime depends on/selects Vulkan Backend directly, no `Vk*` leak | D1, D6; V5 |
| Spec 0013 — Full object ownership, initialization order, per-frame order, reverse-order destruction | D4, D6, D7, D8; Step 3 |
| Spec 0013 — `Device::waitIdle()` usage split (general Shutdown vs. early-failure teardown) | D8 |
| Spec 0013 — GPU-independent lifecycle/state-machine boundary, no DI/service locator | D2; V1 |
| Spec 0013 — Bootstrap scene: cooked mesh, shader, camera, fixed material only | D5; Step 3 |
| Spec 0013 — Presentation/error classification, including `SwapchainCreationFailed`, exhaustive | D3; V3 |
| Spec 0013 — Zero extent, resize, out-of-date/suboptimal, close, format/extent change | D7 |
| Spec 0013 — Build-tree shader/asset path threading, no hardcoded path, multi-config safe | D9; V9 |
| Spec 0013 — GPU smoke test, deterministic exit, no product-level test hook | D10; V6 |
| Spec 0013 — GPU-independent tests validate pure decisions only | D2, D3; V1, V2, V3 |
| Spec 0013 — Manual resize/minimize/restore/close, golden PNG by-eye comparison | Step 6; V8 |
| Spec 0013 — No windowed readback/pixel comparison; existing headless suite unchanged | Not built anywhere in this Plan; V10, V11 |
| Spec 0013 — Existing demos retained, unchanged | V11 (no example/demo file in the touched list) |
| Spec 0013 — No public API change | V11 |
| Spec 0013 — No global mutable state, Client API, World/ECS, headless Runtime, Android | V11 |
| ADR-0046 — Composition, ownership, frame lifecycle | D4–D8 |
| ADR-0047 — Executable/library split, test boundary, Vulkan Backend dependency location | D1, D1a, D10 |

## Rollback Plan

Every step is additive. Reverting the Implementation PR removes
`src/runtime/` and `tests/runtime/` wholesale, and reverts the two
`add_subdirectory()` lines in the root `CMakeLists.txt`. Because no
existing module's source is modified and no golden is touched, revert
restores the exact pre-Plan build and test behavior with no migration
step. Step 7's documentation edits (AGENTS.md,
`docs/architecture/module_boundaries.md`, `docs/project-blueprint.md`,
`src/README.md`, `specs/README.md`'s Implementation column) revert
independently and do not affect build/test behavior either way.

## Deviations, objections, and open mechanical details

**No objection to Spec 0013 or ADR-0046/ADR-0047 was found while drafting
this Plan.** Every `Accepted` decision proved implementable against the
real, current source tree exactly as written; no accepted boundary needed
relaxing, and no public API of any composed module needed changing. The
one genuinely new design point this Plan had to work out from scratch —
avoiding a double `platform::shutdown()` call between `initializeSteps()`'s
own early-failure path and the general `shutdown()` method (D8) — is a
Plan-level implementation detail, not a gap in the Spec's own design; the
Spec's own distinct-`waitIdle()`-usage requirement is exactly what this
Plan's D8 solution satisfies.

One honest limitation, disclosed rather than papered over:

1. **V6/V8 run on one GPU vendor/driver.** The same disclosed
   single-vendor limitation every prior GPU-touching spec in this
   repository carries; not cross-vendor coverage.

**Every mechanical detail this Plan needed to fix is fixed above — none
is left open for Implementation to choose.** Target/namespace/header
layout (D1), the state-machine's exact enumerators and transition table
(D2), the error-taxonomy types and their exhaustive-switch design (D3),
`RuntimeApplication`'s exact member declaration order and why it produces
the required destruction order for free (D4), the six-step initialization
sequence with per-step teardown (D6), the ten-step per-frame order (D7),
the shutdown/double-`platform::shutdown()`-avoidance design (D8), the
exact CMake compile-definition/`add_dependencies()` pattern reusing
existing exported variables (D9), and the GPU smoke test's own bounded-
loop design with no product-level test hook (D10) are all decided, not
proposed.
