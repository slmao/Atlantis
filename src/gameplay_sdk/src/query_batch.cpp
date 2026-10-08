#include <atlantis/gameplay/query_batch.h>

namespace atlantis::gameplay {

namespace access = ::atlantis::world::access;

std::vector<atlantis::Result<std::vector<schema::TypeId>, access::AccessError>> SequentialQueryBatch::listComponents(
    std::span<const ::atlantis::asset_system::EntityGuid> entities) {
  std::vector<atlantis::Result<std::vector<schema::TypeId>, access::AccessError>> out;
  out.reserve(entities.size());
  for (const auto& entity : entities) out.push_back(connection_.listComponents(entity));
  return out;
}

std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> SequentialQueryBatch::getProperties(
    std::span<const access::PropertyAddress> addresses) {
  std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> out;
  out.reserve(addresses.size());
  for (const auto& address : addresses) out.push_back(connection_.getProperty(address));
  return out;
}

}  // namespace atlantis::gameplay
