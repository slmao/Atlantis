#include <atlantis/renderer/draw_order.h>

#include <algorithm>
#include <cstddef>

#include <atlantis/assert.h>

namespace atlantis::renderer {

std::vector<std::uint32_t> computeDrawOrder(std::span<const DrawSortInput> items,
                                            const std::optional<std::array<float, 3>>& cameraWorldPosition) {
  std::vector<std::uint32_t> order;
  order.reserve(items.size());
  std::vector<std::uint32_t> blended;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].blended) {
      blended.push_back(static_cast<std::uint32_t>(i));
    } else {
      order.push_back(static_cast<std::uint32_t>(i));
    }
  }
  if (blended.empty()) return order;

  if (!cameraWorldPosition.has_value()) {
    ATLANTIS_CHECK_MSG(false, "computeDrawOrder(): blended draw items require a camera world position");
  } else {
    const std::array<float, 3>& camera = *cameraWorldPosition;
    const auto squaredDistance = [&](std::uint32_t index) {
      const std::array<float, 3>& point = items[index].worldSortPoint;
      const float dx = point[0] - camera[0];
      const float dy = point[1] - camera[1];
      const float dz = point[2] - camera[2];
      return dx * dx + dy * dy + dz * dz;
    };
    // Farthest first; stable, so equal distances keep input order.
    std::stable_sort(blended.begin(), blended.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
      return squaredDistance(lhs) > squaredDistance(rhs);
    });
  }
  order.insert(order.end(), blended.begin(), blended.end());
  return order;
}

}  // namespace atlantis::renderer
