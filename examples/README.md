# examples/

Non-shipping demonstration programs — not part of the Atlantis runtime
architecture. Kept out of `src/` deliberately so a smoke-test/demo
program can never be mistaken for a shipping engine module; see
[ADR-0010](../adr/0010-cmake-structure.md).

- **`foundation_demo/`** — proof-of-build executable for
  [specs/0001-project-foundation.md](../specs/0001-project-foundation.md).
  Links `Atlantis::Core` and exercises the logging and assertion
  abstractions. Not a preview of Atlantis Runtime.
- **`platform_demo/`** — Windows Platform lifecycle demo for
  [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md).
  Links `Atlantis::Platform`, opens a real top-level window, and logs
  every `PlatformEvent` (resize, focus, close request, shutdown ordering)
  and elapsed monotonic time. The window stays entirely blank — no
  Vulkan, RHI, Renderer, or RenderGraph code is involved. Not a preview of
  Atlantis Runtime.
- **`rhi_vulkan_demo/`** — RHI + Vulkan Backend verification demo (target
  `atlantis_rhi_vulkan_demo`) for
  [specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md).
  Links `Atlantis::Core`, `Atlantis::Platform`, `Atlantis::RHI`, and
  `Atlantis::VulkanBackend`. Opens a real top-level window, creates a
  Vulkan `Device` and `Presentation` through Atlantis's public API only,
  and interactively verifies resize-driven recreation, zero-extent
  deferral on minimize, restore-driven recreation, swapchain metadata,
  and ordered `Presentation`/`Device`/Platform destruction. The window
  stays entirely blank throughout — no acquire, present, `RenderTarget`,
  command buffer, or rendering of any kind is involved. Validation Layers
  are explicitly requested regardless of build configuration; any
  WARNING/ERROR fails the process. Per-resize-event detail (raw event
  size, successful-recreation metadata) logs at `DEBUG` — Win32 live
  resize delivers one event per intermediate size, so this keeps default
  console output readable during interactive dragging; startup/shutdown,
  lifecycle transitions, zero-extent deferral, and errors stay at
  `INFO`/`ERROR`. Not a preview of Atlantis Runtime.
  `build/examples/rhi_vulkan_demo/Debug/atlantis_rhi_vulkan_demo.exe`
  (path varies by generator/configuration).
