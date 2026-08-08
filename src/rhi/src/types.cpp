#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

bool operator==(const Extent2D& lhs, const Extent2D& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

}  // namespace atlantis::rhi
