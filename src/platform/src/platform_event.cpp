#include <atlantis/platform/platform_event.h>

namespace atlantis::platform {

bool operator==(const WindowExtent& lhs, const WindowExtent& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

}  // namespace atlantis::platform
