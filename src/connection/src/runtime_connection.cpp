#include <atlantis/connection/runtime_connection.h>

#include <atlantis/assert.h>

#include <type_traits>
#include <variant>

namespace atlantis::connection {

namespace access = atlantis::world::access;

EventKind kindOf(const access::Event& event) noexcept {
  return std::visit(
      [](const auto& e) {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, access::EntityCreated>) {
          return EventKind::EntityCreated;
        } else if constexpr (std::is_same_v<E, access::EntityDestroyed>) {
          return EventKind::EntityDestroyed;
        } else if constexpr (std::is_same_v<E, access::ComponentAdded>) {
          return EventKind::ComponentAdded;
        } else if constexpr (std::is_same_v<E, access::ComponentRemoved>) {
          return EventKind::ComponentRemoved;
        } else {
          static_assert(std::is_same_v<E, access::PropertyChanged>);
          return EventKind::PropertyChanged;
        }
      },
      event);
}

bool EventFilter::matches(const access::Event& event) const {
  if (!kinds.contains(kindOf(event))) return false;
  return std::visit(
      [&](const auto& e) {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, access::EntityCreated> || std::is_same_v<E, access::EntityDestroyed>) {
          return (!entity || *entity == e.entity) && !component;
        } else if constexpr (std::is_same_v<E, access::PropertyChanged>) {
          return (!entity || *entity == e.address.entity) && (!component || *component == e.address.component);
        } else {
          return (!entity || *entity == e.entity) && (!component || *component == e.component);
        }
      },
      event);
}

std::string_view toString(ConnectionError error) noexcept {
  switch (error) {
    case ConnectionError::UnknownSubscription: return "UnknownSubscription";
  }
  ATLANTIS_CHECK_MSG(false, "toString(ConnectionError): unhandled enumerator");
  return "(unrecognized ConnectionError)";
}

}  // namespace atlantis::connection
