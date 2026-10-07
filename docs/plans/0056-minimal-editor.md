# Plan: Minimal Editor

- **Spec:** [Spec 0056: Minimal Editor](../specs/0056-minimal-editor.md)
  (`Approved`, 2026-10-08, [PR #230](https://github.com/slmao/Atlantis/pull/230);
  rulings Q1–Q9 binding) —
  [ADR-0107](../adr/0107-editor-ui-library-selection.md) (UI library),
  [ADR-0108](../adr/0108-editor-host-and-viewport-composition.md) (host,
  module, Viewport composition, D1–D5),
  [ADR-0109](../adr/0109-platform-input-events.md) (Platform input events),
  all `Accepted`.
  - The editor is a client of [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md)'s
    `RuntimeConnection` and [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md)'s
    `RuntimeControl`; neither changes.
  - It keeps [ADR-0103](../adr/0103-runtime-world-operation-boundary.md),
    [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) and
    [ADR-0004](../adr/0004-phase1-threading-baseline.md).
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending. Implementation needs the Joint Human
  Review of this Plan together with Spec 0056, explicitly authorizing it
  (J1–J12 below).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0056 R1–R7 as ruled: Atlantis Editor (`src/editor/`), a
client library over `RuntimeConnection` and `RuntimeControl` with a
Hierarchy, a schema-generated Inspector, a Viewport and a Transform Gizmo,
drawn with Dear ImGui's core; hosted by `atlantis_runtime --editor`; its UI
drawn by a new Renderer overlay pass over an RHI offscreen target that can be
sampled; its input from new Platform input events. Without `--editor`, the
frame, every test and every golden are unchanged.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `5833565` (PR #230 merged).

1. **Hosting precedents:**
   - `--exec` hosts the CLI with one connection from
     `RuntimeApplication::openConnection()` (the `EndpointSlot` move guard:
     the endpoint exists only after the application is in place);
   - `--listen` creates a `RuntimeControlHost` and calls
     `beforeFrame()` / `runFrame()` / `afterFrame()` (`src/runtime/main.cpp`);
   - `src/runtime/cli.h/.cpp` is executable-private, compiled into both
     `atlantis_runtime` and `atlantis_runtime_tests` (ADR-0076 D2).
2. **The frame** (`runtime_application.cpp`, `runFrame()`, 754 lines):
   P7's hold line first; the Platform pump loop; acquire; the format-change
   and extent-change checks (`const Extent2D currentExtent =
   target->extent();`); extraction; `renderer_.drawFrame(*commandList,
   *target, …, ResourceState::PresentSource, …)`; submit; present.
3. **Capture's offscreen path** (`frame_capture.cpp`, Spec 0055 P8): an
   `OffscreenTarget` in the swapchain's format reuses the output-transform
   Pipeline and the window-sized depth/HDR/bloom targets; `drawFrame(…,
   TransferSource, …)`; a RenderGraph copy pass. `captureFrameData()` gives
   the frame's view and projection.
4. **RHI:**
   - `OffscreenTargetCreateParams{extent, format}`; the Vulkan image is
     `COLOR_ATTACHMENT | TRANSFER_SRC` (`vulkan_device.cpp:1364`);
     `HdrColorTarget` is `COLOR_ATTACHMENT | SAMPLED` (`:1557`) and has a
     `bindTexture(binding, const HdrColorTarget&, const Sampler&)`;
   - `CommandList` has `drawIndexed(indexCount)` only (no first index, no
     vertex offset) and sets the scissor only to the render area in
     `beginRendering()`;
   - `VertexAttributeFormat` is `Float2/Float3/Float4`; every Buffer is
     host-visible and mapped (`vulkan_device.cpp:839`);
   - `PipelineCreateParams` has `ColorBlendMode::AlphaBlend`
     (ADR-0090) and a `std::variant<Format, HdrFormat>` colour format.
5. **Renderer:** `drawFrame()` builds one RenderGraph: shadow, draw,
   bloom, `output_transform`.
6. **Platform:** `PlatformEvent` is a `std::variant` of window and lifecycle
   events (`platform_event.h`); Windows Platform's `windowProc` handles
   `WM_SIZE`, `WM_SETFOCUS`, `WM_KILLFOCUS`, `WM_CLOSE`, `WM_DESTROY`;
   `tests/platform/windows_platform_smoke_tests.cpp` drives it with
   `SendMessageW`.
7. **Dependency precedents:** `cmake/AtlantisCgltf.cmake` and
   `cmake/AtlantisStb.cmake` — `FetchContent_Declare` with a commit/tag
   archive `URL` + `URL_HASH SHA256`, `DOWNLOAD_EXTRACT_TIMESTAMP`, wrapped
   in a local target (`Cgltf::Cgltf`, `Stb::Stb`).
8. **Boundary scans:** `tests/cli/cli_boundary_tests.cpp` (includes and the
   link block), `tests/remote/remote_boundary_tests.cpp` (OS headers only in
   `src/os/`).
9. **Dear ImGui:** latest release `v1.92.9b` (2026-07-31), MIT. Core files
   `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`;
   `backends/` is not compiled. Since 1.92 a renderer may opt into dynamic
   font textures (`ImGuiBackendFlags_RendererHasTextures`); not setting it
   keeps the single prebuilt font atlas.
10. **The north-star lights:** the default scene's Directional
    `0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea` (3); Bistro's `6b63b12c-…` (12,
    outside the camera's view); the café light `425c3b17-dcac-4148-831c-22a454829357`
    (4.5, in view) — the Spec 0055 Correction 2026-10-08 pattern.

## Plan-stage decisions

**P1 — Files and modules.**

| Where | Contents |
|---|---|
| `cmake/AtlantisImGui.cmake` (new) | `FetchContent_Declare(imgui URL …/v1.92.9b.tar.gz URL_HASH SHA256=… DOWNLOAD_EXTRACT_TIMESTAMP TRUE)`; a STATIC `atlantis_imgui` of the four core sources only (no `backends/`), `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, SYSTEM includes; alias `ImGui::ImGui`. Included from the top level outside `if(NOT ANDROID)` (Android compiles the editor library). |
| `src/editor/include/atlantis/editor/` | `editor.h` (`atlantis::editor::Editor`: the host-facing class), `input.h` (the editor's own plain input values), `ui_draw_list.h` (the engine-neutral draw list), `model/hierarchy.h`, `model/inspector.h`, `model/gizmo.h`. **No public header includes ImGui.** |
| `src/editor/src/model/*.cpp` | the models; never include ImGui (P8 scan) |
| `src/editor/src/view/*.cpp` | the ImGui view layer: layout, panels, draw-list translation, input feeding — the only files that include ImGui |
| `src/editor/CMakeLists.txt` | `atlantis_editor` (`Atlantis::Editor`): PUBLIC `Atlantis::Connection`, PRIVATE `ImGui::ImGui`, `atlantis_compiler_warnings` |
| `src/platform/` | `platform_event.h` input events (P2); `windows_platform.cpp` messages → events |
| `src/rhi/`, `src/vulkan_backend/` | sampleable offscreen target, sampling it, scissor, ranged indexed draw (P4) |
| `src/renderer/` | `ui_overlay.h/.cpp`: `renderer::UiDrawList` and `Renderer::drawOverlay()` (P5) |
| `shaders/editor_ui/` (new) | the overlay's Slang pair (P5) |
| `src/runtime/include/atlantis/runtime/frame_overlay.h` (new) | `FrameOverlay`: the Runtime-host interface a frame overlay implements (P6) |
| `src/runtime/` | `RuntimeApplication::attachOverlay()`, the named `runFrame()` lines (P7), the Viewport target and the overlay resources |
| `src/runtime/editor_attachment.h/.cpp` (new, executable-private) | the adapter: `FrameOverlay` over `atlantis::editor::Editor`, translating Platform events and draw lists (P6); compiled into `atlantis_runtime` and the GPU test target, as `cli.cpp` is |
| `src/runtime/main.cpp`, `cli.h/.cpp`, `CMakeLists.txt` | `--editor`; the executable links `Atlantis::Editor` |

**Dependency-graph changes:**

- **New:** `atlantis_imgui` (third-party, pinned), `atlantis_editor`
  (Connection + ImGui).
- **`atlantis_runtime`** links `Atlantis::Editor`;
  **`atlantis_runtime_host`** does not (ADR-0108 D2). The host gains
  `frame_overlay.h`, which names only Renderer and Platform types it
  already depends on.
- **No other link changes.** Renderer and RHI gain functions, not
  dependencies.

**P2 — Platform input events** (ADR-0109). Added to `PlatformEvent`:

```cpp
enum class PointerButton : std::uint8_t { Left, Right, Middle };
enum class Key : std::uint16_t { /* Tab, arrows, Home/End, PageUp/Down, Insert, Delete,
  Backspace, Space, Enter, Escape, A–Z, 0–9, F1–F12, LeftCtrl/Shift/Alt, RightCtrl/Shift/Alt */ };
struct KeyModifiers { bool ctrl = false; bool shift = false; bool alt = false; };
struct PointerMoved   { float x; float y; };                  // framebuffer pixels, window-relative
struct PointerButtonChanged { PointerButton button; bool down; float x; float y; };
struct WheelScrolled  { float dx; float dy; };                // notches (WHEEL_DELTA = 1.0)
struct KeyChanged     { Key key; bool down; KeyModifiers modifiers; };
struct TextEntered    { std::array<char, 8> utf8; std::uint8_t size; };  // one code point
```

- **Plain values only**, no OS type; the `Key` set is closed and
  Platform-defined (the list above; other keys are not reported).
- **Windows:** `WM_MOUSEMOVE`, `WM_{L,R,M}BUTTON{DOWN,UP}`,
  `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL`, `WM_KEYDOWN/UP` and `WM_SYSKEYDOWN/UP`
  (virtual-key → `Key`), `WM_CHAR` (UTF-16 → UTF-8, surrogate pairs joined).
  Positions are framebuffer pixels (the process is DPI-aware as today).
- **Android** emits none.
- **Consumers:** today's Runtime frame ignores them (its `std::get_if` chain
  has no branch for them), so the frame is unchanged.

**P3 — ImGui wiring** (ADR-0107).

- Core only; no `backends/` file compiled; no `imconfig.h` edits beyond
  `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` (a compile definition).
- `ImGuiBackendFlags_RendererHasTextures` is **not** set: the font atlas
  is built once (`GetTexDataAsRGBA32`) and uploaded once by the host.
- One `ImGuiContext` per `Editor`, created and destroyed by it; no global
  ImGui state outside that context.
- `ImDrawIdx` stays 16-bit; the neutral list carries 32-bit indices and
  per-command vertex offsets.

**P4 — RHI additions** (ADR-0108 D3).

1. `OffscreenTargetCreateParams::sampled` (default `false`): the Vulkan
   image adds `SAMPLED`. Existing creations are unchanged.
2. `CommandList::bindTexture(std::uint32_t binding, const RenderTarget&
   sampledTarget, const Sampler&)` — CHECKs the target came from a sampled
   offscreen target; and `transitionResource(RenderTarget&, …)` already
   exists for `ColorAttachmentOutput → ShaderRead`.
3. A new `rhi::Rect2D {offset x/y, Extent2D}` value type,
   `CommandList::setScissor(Rect2D)` (clamped to the render area) and
   `CommandList::drawIndexed(std::uint32_t indexCount, std::uint32_t
   firstIndex, std::int32_t vertexOffset)`. The existing
   `drawIndexed(indexCount)` is unchanged.

Items 1–2 are D3's "sampleable offscreen target … bound as a sampled
texture"; item 3 is what D3's "scissored triangles" needs (J2).

**P5 — The Renderer overlay pass** (ADR-0108 D3).

- **Type** (Renderer-owned, no ImGui):

  ```cpp
  struct UiVertex { float x, y, u, v; float r, g, b, a; };     // pixels; uv; sRGB-authored colour
  enum class UiTexture : std::uint8_t { FontAtlas, Viewport };
  struct UiDrawCommand { std::uint32_t firstIndex, indexCount; std::int32_t vertexOffset;
                         Rect2D clip; UiTexture texture; };
  struct UiDrawList { std::vector<UiVertex> vertices; std::vector<std::uint32_t> indices;
                      std::vector<UiDrawCommand> commands; Extent2D display; };
  ```

- **`Renderer::drawOverlay(commandList, RenderTarget& target, ResourceState
  finalState, const UiDrawList&, const UiOverlayResources&)`**: one
  RenderGraph pass `ui_overlay` that clears the target, binds the overlay
  Pipeline, writes this frame's vertices and indices into growable
  host-visible buffers, and per command sets the scissor, binds the font
  atlas or the Viewport target, and draws its range.
  `UiOverlayResources` (Pipeline, buffers, font `SampledTexture`, samplers,
  the Viewport target) are the host's.
- **Pipeline:** `shaders/editor_ui` (Slang): vertex position →
  clip space via a push-constant scale/translate; fragment = texture ×
  vertex colour, `AlphaBlend`, no depth. Built for the swapchain's format,
  rebuilt on a format change like the output-transform Pipeline.
- **sRGB:** a push-constant flag linearizes vertex colours when the target
  is an sRGB format (ImGui colours are authored in sRGB); the Viewport is
  sampled through a view of the same format as the swapchain, so its
  pixels pass through unchanged.

**P6 — The host interface and the adapter** (ADR-0108 D1, D2, D4).

- **`runtime::FrameOverlay`** (Runtime host, `frame_overlay.h`):

  ```cpp
  class FrameOverlay {
   public:
    virtual void onPlatformEvent(const platform::PlatformEvent&) = 0;   // during the pump
    [[nodiscard]] virtual rhi::Extent2D viewportExtent() const = 0;     // the scene's render size
    [[nodiscard]] virtual const renderer::UiDrawList& drawList() const = 0;
    [[nodiscard]] virtual FontAtlasPixels fontAtlas() const = 0;        // RGBA8, uploaded once
  };
  ```

  `RuntimeApplication::attachOverlay(FrameOverlay&)` (borrowed; detached
  before the overlay dies) creates the Viewport target, the overlay
  Pipeline, buffers, samplers and the font texture.
- **`EditorAttachment`** (`src/runtime/editor_attachment.*`,
  executable-private): implements `FrameOverlay` over an `editor::Editor`;
  translates `PlatformEvent` input → `editor::InputEvent`, and
  `editor::UiDrawList` → `renderer::UiDrawList` (same shape, two owners).
  It opens the editor's connection (`openConnection()`) and is given the
  `RuntimeControlHost`.
- **The editor's frame runs between frames** (J3): `main.cpp` calls
  `attachment.update()` after `afterFrame()` — the editor drains its
  subscription, applies the input gathered during the last pump, runs one
  ImGui frame and stores its draw list. All connection calls happen between
  frames (ADR-0105's contract); the next `runFrame()` draws that list.
- **Gizmo camera:** while a Gizmo is shown, `update()` reads
  `captureFrameData()`'s `view` / `projection` (the Spec 0055 frame data,
  Spec 0056 :414); otherwise it does not compute it (the draw-item walk
  costs milliseconds on Bistro).

**P7 — The named `runFrame()` lines** (ADR-0108 D4). Relative to
`origin/main` `5833565`, `runFrame()` differs in exactly these places, each
of which reduces to the current text when no overlay is attached
(`overlay_ == nullptr`):

1. **In the pump loop**, first statement of its body:
   `if (overlay_) overlay_->onPlatformEvent(event);`
2. **The scene's extent:** `const Extent2D currentExtent = target->extent();`
   becomes `const Extent2D currentExtent = overlay_ ? overlayViewportExtent()
   : target->extent();` (the depth/HDR/bloom targets then follow the
   Viewport's size through the existing extent-change branch).
3. **The scene's target:** in the `drawFrame()` call, `*target` becomes
   `overlay_ ? overlayViewportTarget() : *target` and
   `ResourceState::PresentSource` becomes `overlay_ ? ResourceState::ShaderRead
   : ResourceState::PresentSource`.
4. **The overlay:** after the `drawFrame()` call, one statement:
   `if (overlay_) drawOverlayInto(*commandList, *target);`

The helpers (`overlayViewportExtent()`, `overlayViewportTarget()`,
`drawOverlayInto()`) are private members outside `runFrame()`. Submit,
present and every other line stay as they are.

- **The gate's guard (mode "overlay")** extracts `runFrame()` from base and
  HEAD and requires the diff to equal exactly these four hunks (an expected
  diff checked into the gate script). Through M5 the guard runs in the
  0055 mode (byte-identical to base).
- **Proof that the non-editor path is unchanged** (the Spec 0055 P7
  pattern): (1) the guard; (2) `overlay_` has one writer,
  `attachOverlay()`/`detachOverlay()`, reached only from `--editor`
  (a source scan); (3) the full suites — the smoke tests with exact
  `FrameLightingData`, the 0054/0055 north stars, capture — pass
  unmodified. Goldens come from the headless fixture and cannot see
  `runFrame()`; they stay byte-identical regardless.

**P8 — The editor library's shape** (ADR-0107, ADR-0108 D2).

- **`editor::Editor`** (`editor.h`):

  ```cpp
  Editor(connection::RuntimeConnection&, connection::RuntimeControl&);
  FontAtlas fontAtlas() const;                                   // RGBA8 pixels + size
  void frame(std::span<const InputEvent>, const FrameContext&);  // one UI frame
  const UiDrawList& drawList() const;                            // its output
  ViewportSize viewportSize() const;                             // the Viewport panel's size in pixels
  ```

  `FrameContext` carries the window size and, optionally, the frame's view
  and projection.
- **Layout:** a fixed 2×2 split filling the window — Hierarchy (top left),
  Viewport (top right), Content (bottom left: an empty reserved panel,
  ruling Q6), Inspector (bottom right); a toolbar row above the Viewport
  with Play / Pause / Step and the status (paused, frame). No docking.
- **Models are UI-free:** `model/*.h` and `src/model/*.cpp` include no
  ImGui header; the boundary scan enforces it.
- **Boundary scan** (`tests/editor/editor_boundary_tests.cpp`):
  - all of `src/editor/` includes no Runtime, ECS, RHI, Renderer, Vulkan,
    Platform or OS header;
  - ImGui headers (`imgui*.h`) appear only under `src/editor/src/view/`;
  - `atlantis_editor`'s link block is exactly `Atlantis::Connection`,
    `ImGui::ImGui`, `atlantis_compiler_warnings`.

**P9 — The models.**

- **Hierarchy** (R2; ruling Q4):
  - **First listing:** `listEntities()`, then `listComponents()` for each,
    in one editor frame.
  - **Increments:** one subscription with kinds `EntityCreated,
    EntityDestroyed, ComponentAdded, ComponentRemoved`, drained once per
    editor frame. Created → one `listComponents()`; destroyed → removed (and
    deselected); added/removed → the row's component set updated.
  - **Rows** in GUID order: the GUID and the component short names;
    a filter box matching a GUID prefix or a component name.
  - **Large lists:** only visible rows are emitted
    (`ImGuiListClipper` in the view); the model is a sorted vector.
- **Inspector** (R3): for the selected entity, each component's
  `text::leavesOf()` order, grouped by nested struct; values from
  `getProperty()`, re-read after every applied edit event and once per
  frame for the selected entity only. **PrimitiveKind → control:**

  | Field | Control | Committed value |
  |---|---|---|
  | `UInt64` | integer input (decimal) | `uint64_t` |
  | `Float32` | drag float | `float` |
  | `Vec3Float32` | drag float ×3 | `array<float,3>` |
  | `Vec4Float32` | drag float ×4 | `array<float,4>` |
  | `AssetGuid` / `EntityGuid` | text input, canonical GUID | parsed GUID (invalid text not committed) |
  | Enum | combo of the constants | `EnumValue` |
  | Nested struct | a collapsible group of its leaves | — |
  | `Optional` | a "set" checkbox beside the control | `Absent` when cleared |
  | not `Editable` | the control disabled | — |

  - **Commit:** one `SetProperty` when an edit finishes (the control is
    deactivated after an edit: drag released, Enter, combo picked) — never
    per drag step (R3: "an edit is one `SetProperty`"). Its event or
    refusal is shown beside the field until the next edit.
  - **`Transform`:** shown and editable, with the note "does not affect
    rendering (Spec 0052 ruling Q8)" (ruling Q5).
- **Gizmo** (R5; ruling Q5):
  - **Subject:** the selected entity's `WorldMatrix`; none without one.
  - **Decomposition:** T = `column3.xyz`; scale = the lengths of
    `column0..2`; R = the normalized columns. If two columns are not
    orthogonal (|dot| > 1e-4 after normalization), the matrix is sheared:
    no Gizmo (the Inspector still edits its columns).
  - **Composition:** `M = T · R · S`; translate-only drags change only
    `column3`.
  - **Axes and interaction:** world axes X/Y/Z (red/green/blue);
    translate, rotate, scale modes (toolbar buttons; keys W/E/R while the
    Viewport is hovered); dragging an axis handle projects the pointer delta
    on the axis's screen direction (translate, scale) or measures the
    angle around the handle's screen centre (rotate). No snapping, no
    planar or uniform handles in v1.
  - **Writes:** at most one transaction per editor frame — `SetProperty` of
    `column0..column3` — carrying the latest drag state; one more on
    release with the final value.

**P10 — `--editor`** (ADR-0108 D1, D5).

- `atlantis_runtime --editor` (combinable with `--scene`, `--listen`;
  not with `--exec`, whose script and the editor would both drive the
  frame): creates the `RuntimeControlHost` (shared with `--listen` when
  both are given), the `EditorAttachment`, and `attachOverlay()`.
- **Loop:** `beforeFrame(); runFrame(); afterFrame(); server.poll();
  attachment.update();` — the editor after the server, so both see the
  same between-frames World.
- **Windows-only** (the executable is); Android builds the editor library
  (Gradle `targets`) but no host.

## Milestones / Task Breakdown

Eight milestones. Commit prefixes `feat:` / `test:` / `docs:`.

**Every gate:** Debug + Release builds and full `ctest` with every golden
compared exactly (a moved golden stops the work and is reported, never
re-captured); the golden-directory guard; the path guard (the P1 list;
`src/runtime/` limited to its listed files); the `runFrame()` guard (0055
mode through M5, overlay mode from M6); Android `assembleDebug` (targets
gain `atlantis_editor` from M1); GPU suites under fatal VVL.

### M1 — ImGui and the editor skeleton

1. `cmake/AtlantisImGui.cmake` (P3); `atlantis_imgui`.
2. `src/editor/` with `editor.h`, `input.h`, `ui_draw_list.h` and empty
   model headers; `atlantis_editor`; top-level `add_subdirectory`.
3. `tests/editor/` with the boundary scan (P8), including that no public or
   model file includes ImGui and that `backends/` is not compiled.
4. Gradle `targets` gain `atlantis_editor`.

### M2 — Platform input events (R1's precondition; ADR-0109)

1. P2's types and `windows_platform.cpp`'s messages.
2. Tests: `platform_event_tests.cpp` gains value-type cases; the Windows
   smoke test sends `WM_MOUSEMOVE`, button, wheel, key and `WM_CHAR`
   (incl. a surrogate pair) with `SendMessageW` and checks the events'
   order and values; Android's command-mapping tests still see no input
   events.
3. The Runtime frame ignores them (its smoke tests unchanged).

### M3 — RHI sampleable target, scissor and ranged draws; the Renderer overlay pass

1. P4 in RHI and the Vulkan Backend.
2. P5: `ui_overlay.h/.cpp`, `shaders/editor_ui`.
3. GPU tests (fatal VVL):
   - a sampled offscreen target: render a solid colour, sample it in a
     second pass into another offscreen target, read back — exact;
   - `drawOverlay()` with a fixed `UiDrawList` (two textured quads, one
     scissored, both texture slots) into an offscreen target — exact pixels
     at chosen points, scissor respected, sRGB and UNORM targets;
   - existing targets unchanged (their usage flags pinned).

### M4 — The models (R2, R3, R5)

1. P9's Hierarchy, Inspector and Gizmo models.
2. Tests (no GPU, InProcess connection on a baked fixture scene, the 0054
   fixture):
   - **Hierarchy:** the first listing equals `listEntities()` with each
     entity's components; after creates, destroys, adds and removes (single
     and in transactions), the incremental state equals a fresh listing;
     a destroyed selection clears;
   - **Inspector:** for every World component type, the generated field
     table equals the schema's leaves (paths, kinds, enum constants,
     nesting, `Optional`, read-only); each kind's commit submits exactly
     one `SetProperty` of the right `PropertyValue` alternative; refusals
     are reported;
   - **Gizmo:** decompose(compose(T,R,S)) within 1e-5 over seeded random
     TRS; translate drags change only `column3`, bit-exactly elsewhere;
     sheared matrices give no Gizmo; a drag of N input events in one frame
     submits one transaction of four `SetProperty`s; release submits the
     final one.

### M5 — The view layer (R2–R6)

1. P8's layout and panels; draw-list translation; input feeding.
2. Headless UI tests (ImGui without any backend, a fixed 1280×720
   display): scripted input clicks a Hierarchy row (selection), edits a
   float field and releases (one `SetProperty`), picks an enum constant,
   presses Pause / Step (the fake control sees `pause`, `step(1)`); the
   produced `UiDrawList` is non-empty, its commands' ranges and clips are
   within bounds, and it references both texture slots; two identical
   frames produce identical draw lists.
3. Boundary scan still green (only `src/view/` includes ImGui).

### M6 — `atlantis_runtime --editor` and the named `runFrame()` lines (R4, R6, R7)

1. P6, P7, P10: `frame_overlay.h`, `attachOverlay()`/`detachOverlay()`, the
   Viewport target and overlay resources, the four named lines,
   `editor_attachment.*`, `--editor` in `cli.h/.cpp` and `main.cpp`.
2. Tests:
   - `cli_tests.cpp`: `--editor` parse cases (with `--scene`, `--listen`;
     refused with `--exec`, `--help`, `--list-scenes`);
   - GPU (fatal VVL): with the editor attached on the default scene, frames
     render; the Viewport target's readback (a test-only friend read)
     equals a capture of the same world at the same size (`captureImage()`,
     the 0055 path) byte for byte; resizing the Viewport recreates the
     scene targets;
   - the `overlay_` single-writer source scan.
3. The gate's `runFrame()` guard switches to overlay mode.

### M7 — The north star, and normative docs (Spec Q8)

1. **In process, fatal VVL** (`runtime_smoke_gpu_tests.cpp`, new
   TEST_CASEs), driving the editor through its model actions (the same calls
   the view makes; M5 covers the clicks):
   - **Default scene:** select `0b2c1db2-…` in the Hierarchy model; the
     Inspector model's table has `Light.intensity = 3`; commit 6; next frame
     the frame data's directional intensity is 6 exactly and the Viewport
     target's readback differs from the frame before; Pause holds a further
     edit across three frames; Step applies it.
   - **Bistro (content-gated, `SKIP` without content):** `6b63b12c-…` 12 →
     24, exact in frame data; the café light `425c3b17-…` 4.5 → 9, the
     Viewport image changes.
2. **Docs (normative, J10):** AGENTS.md's module list gains Atlantis Editor
   (its dependency rule: Connection + the UI library; hosted by
   `atlantis_runtime --editor`), and its Platform/Runtime sentences their
   input events and `--editor`; `module_boundaries.md` gains the Editor
   section and the Platform, RHI, Renderer and Runtime lines.

### M8 — Acceptance

1. Final guards (path, `runFrame()` overlay mode) and diff review.
2. **Human run:** `atlantis_runtime --editor` on the default scene and on
   Bistro: select, edit, gizmo-drag, pause/step; screenshots before/after.
3. **The Plan 0052 J9 runs repeated** without `--editor` (Windows whitelist,
   Debug and Release, VVL on; the Android emulator default scene).

## Files / Modules Touched (expected)

**New:** `cmake/AtlantisImGui.cmake`; `src/editor/**`; `tests/editor/**`;
`shaders/editor_ui/**`; `src/renderer/include/atlantis/renderer/ui_overlay.h`,
`src/renderer/src/ui_overlay.cpp`;
`src/runtime/include/atlantis/runtime/frame_overlay.h`;
`src/runtime/editor_attachment.h`, `src/runtime/editor_attachment.cpp`;
`tests/renderer/` and `tests/vulkan_backend/` overlay/sampled-target test
files.

**Changed (planned):**

- `CMakeLists.txt` (ImGui include, `src/editor`, `tests/editor`,
  `shaders/editor_ui`);
- `src/platform/include/atlantis/platform/platform_event.h`,
  `src/platform/src/platform_event.cpp`,
  `src/platform/src/windows/windows_platform.cpp`;
- `src/rhi/include/atlantis/rhi/{types.h,command_list.h}`,
  `src/vulkan_backend/src/{vulkan_device.cpp,vulkan_command_list.h,vulkan_command_list.cpp,vulkan_offscreen_target.*,vulkan_offscreen_render_target.*}`;
- `src/renderer/CMakeLists.txt`;
- `src/runtime/include/atlantis/runtime/runtime_application.h`,
  `src/runtime/src/runtime_application.cpp` (the four P7 lines plus private
  members/helpers outside `runFrame()`), `src/runtime/main.cpp`, `cli.h`,
  `cli.cpp`, `CMakeLists.txt`;
- `tests/platform/{platform_event_tests.cpp,windows_platform_smoke_tests.cpp}`
  (new TEST_CASEs only), `tests/runtime/{cli_tests.cpp,runtime_smoke_gpu_tests.cpp,CMakeLists.txt}`;
- `android/app/build.gradle` (`atlantis_editor`);
- `AGENTS.md`, `docs/architecture/module_boundaries.md` (normative only).

**Not touched:** `RuntimeConnection`, `RuntimeControl`, Atlantis Remote,
Atlantis CLI, `RuntimeWorldAccess`, the ECS, extraction, `drawFrame()`'s
passes, existing shaders, assets, goldens, `android_main.cpp`.

## Sequencing & Dependencies

M1 → M2 → M3 are independent foundations in this order (the editor
library, its input, its drawing); M4 needs M1; M5 needs M4; M6 needs M2,
M3 and M5; M7 needs M6; M8 last.

## Verification Checklist

- [ ] **R1 (M1, M5, M6):** boundary scan (includes, ImGui only in `view/`,
  link block); the editor's World access only through its connection.
- [ ] **R2 (M4, M5):** Hierarchy first listing and incremental equivalence;
  virtualized rows.
- [ ] **R3 (M4, M5):** generated field table equals the schema; one
  `SetProperty` per edit; `Transform` note.
- [ ] **R4 (M3, M6, M7):** sampled offscreen target; Viewport readback
  equals a capture; north-star image change.
- [ ] **R5 (M4):** Gizmo math and one four-column transaction per frame.
- [ ] **R6 (M5, M7):** Play/Pause/Step on `RuntimeControl`; Pause holds,
  Step applies.
- [ ] **R7 (every gate):** full suites, goldens exact, `runFrame()` guard,
  the `overlay_` writer scan.
- [ ] **North star:** default scene 3 → 6 exact; Bistro 12 → 24 exact and
  the café light's image change.
- [ ] **Android:** `assembleDebug` compiles `atlantis_editor` (with ImGui)
  and the Platform input events.

## Risks

- **ImGui 1.92's font system.** Not opting into dynamic font textures keeps
  the prebuilt atlas; if 1.92.9b requires the opt-in for some path, M1
  reports it before M5 depends on it.
- **Four named `runFrame()` lines** (P7). Each reduces to today's text
  without an overlay; the guard, the writer scan and the full suites carry
  the proof.
- **The Viewport at the panel's size** changes the scene's render size
  when the editor is attached — the capture path follows it
  (`lastSeenExtent_`), consistently.
- **Bistro's frame time** (about 130 ms with its log flood) is the editor's
  UI rate on Bistro.
- **sRGB handling** in the overlay is new; M3's pixel tests cover both
  target formats.

## Joint Review decisions (recommendation first)

- **J1 — No Spec Correction proposed.** The Plan stays within Spec 0056's
  rulings and ADR-0108's decisions.
- **J2 — RHI scissor and ranged indexed draw** (P4 item 3) are read as part
  of ADR-0108 D3's "scissored triangles", not a new decision. Alternative:
  one index buffer per draw command (no RHI change, many small buffers per
  frame).
- **J3 — The editor's UI frame runs between frames** (P6), one frame of
  input latency, so every connection call keeps ADR-0105's "between
  frames" contract. Alternative: inside `runFrame()` after the pump (no
  latency, but connection calls mid-frame and a fifth named line).
- **J4 — The adapter is executable-private** (`editor_attachment.*`,
  compiled into `atlantis_runtime` and the GPU tests, the `cli.cpp`
  precedent), so the host library never links the editor.
- **J5 — Inspector edits commit on completion**, one `SetProperty` per edit
  (no live writes while dragging a field). Alternative: live, throttled
  like the Gizmo.
- **J6 — sRGB:** linearize UI vertex colours for sRGB targets; sample the
  Viewport through a same-format view.
- **J7 — Gizmo camera from `captureFrameData()` only while a Gizmo is
  shown** (its draw-item walk costs milliseconds on Bistro).
- **J8 — ImGui pinned to `v1.92.9b`**, core only,
  `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, no dynamic font textures.
- **J9 — `--editor` combines with `--listen`** (one shared
  `RuntimeControlHost`) and is refused with `--exec`.
- **J10 — Docs:** normative sentences with the implementation PR
  (AGENTS.md module list and Editor rule; `module_boundaries.md`'s Editor
  section and Platform/RHI/Renderer/Runtime lines); narratives post-merge
  (the Plan 0054/0055 J6 split).
- **J11 — Android:** Gradle `targets` gain `atlantis_editor` (ImGui and the
  input-event header compile on the NDK); no Android host.
- **J12 — Acceptance runs:** the human editor run (default scene and
  Bistro) and the Plan 0052 J9 runs repeated, since Platform, RHI,
  Renderer and `runFrame()` change.

## Rollback Plan

- **After merge:** revert the implementation PR as a whole; the editor,
  overlay pass, RHI additions and input events are additive, and the four
  `runFrame()` lines revert with it. No format, asset or golden changes.
- **Before merge:** revert milestone by milestone, in reverse order.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] Every golden exact in Debug and Release at the final gate; none
      re-captured.
- [ ] `runFrame()` differs from base in exactly P7's four lines; the path
      guard holds.
- [ ] Boundary scans green: Editor (includes, ImGui only in `view/`, link
      block), CLI, Remote.
- [ ] The post-merge docs items (J10) are queued.
