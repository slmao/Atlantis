#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

bool operator==(const Extent2D& lhs, const Extent2D& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

bool operator==(const ClearColorValue& lhs, const ClearColorValue& rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

}  // namespace atlantis::rhi
