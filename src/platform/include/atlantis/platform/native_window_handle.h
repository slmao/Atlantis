#pragma once

#include <atlantis/platform/platform_kind.h>

namespace atlantis::platform {

// Opaque, tagged, borrowed, non-owning — see ADR-0011. value0/value1's
// meaning is platform-specific and is interpreted only by the active
// graphics backend's private WSI boundary (per ADR-0005, as amended) —
// never by generic RHI, Renderer, or RenderGraph, and never by Runtime
// itself beyond transporting this value unchanged.
//
// Windows: value0 = HWND, value1 = HINSTANCE.
// Android (future): value0 = ANativeWindow*, value1 = unused.
// iOS (future, not implemented): value0 = a CAMetalLayer-equivalent.
struct NativeWindowHandle {
  PlatformKind kind;
  void* value0 = nullptr;
  void* value1 = nullptr;
};

}  // namespace atlantis::platform
