#pragma once

namespace atlantis::platform {

// See ADR-0011 and specs/0002-platform-foundation.md Platform
// Identification. Reserves IOS now even though iOS is architecture-only
// (no implementation) per that spec's Non-Goals.
enum class PlatformKind {
  Windows,
  Android,
  IOS,
};

}  // namespace atlantis::platform
