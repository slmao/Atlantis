#include <atlantis/gameplay/transaction.h>

namespace atlantis::gameplay {

namespace access = ::atlantis::world::access;

Transaction& Transaction::create(const EntityGuid& entity) {
  operations_.push_back(Operation{Kind::Create, entity, {}, access::Absent{}});
  return *this;
}

Transaction& Transaction::destroy(const EntityGuid& entity) {
  operations_.push_back(Operation{Kind::Destroy, entity, {}, access::Absent{}});
  return *this;
}

Transaction& Transaction::add(const EntityGuid& entity, std::string_view type) {
  Target target;
  target.name = std::string(type);
  operations_.push_back(Operation{Kind::AddComponent, entity, std::move(target), access::Absent{}});
  return *this;
}

Transaction& Transaction::add(const EntityGuid& entity, schema::TypeId type) {
  Target target;
  target.component = type;
  operations_.push_back(Operation{Kind::AddComponent, entity, std::move(target), access::Absent{}});
  return *this;
}

Transaction& Transaction::add(const EntityGuid& entity, std::string_view type,
                              const std::vector<std::pair<std::string, PropertyValue>>& leaves) {
  add(entity, type);
  for (const auto& [leaf, value] : leaves) set(entity, std::string(type) + "." + leaf, value);
  return *this;
}

Transaction& Transaction::remove(const EntityGuid& entity, std::string_view type) {
  Target target;
  target.name = std::string(type);
  operations_.push_back(Operation{Kind::RemoveComponent, entity, std::move(target), access::Absent{}});
  return *this;
}

Transaction& Transaction::remove(const EntityGuid& entity, schema::TypeId type) {
  Target target;
  target.component = type;
  operations_.push_back(Operation{Kind::RemoveComponent, entity, std::move(target), access::Absent{}});
  return *this;
}

Transaction& Transaction::set(const EntityGuid& entity, std::string_view path, PropertyValue value) {
  Target target;
  target.name = std::string(path);
  operations_.push_back(Operation{Kind::SetProperty, entity, std::move(target), std::move(value)});
  return *this;
}

Transaction& Transaction::set(const PropertyAddress& address, PropertyValue value) {
  Target target;
  target.component = address.component;
  target.field = address.field;
  operations_.push_back(Operation{Kind::SetProperty, address.entity, std::move(target), std::move(value)});
  return *this;
}

void FailureLog::absorb(std::vector<access::CommandFailure> failures) {
  failures_.insert(failures_.end(), failures.begin(), failures.end());
}

std::optional<access::CommandFailure> FailureLog::refusal(access::CommandTicket ticket) const {
  for (const auto& failure : failures_) {
    if (failure.ticket == ticket) return failure;
  }
  return std::nullopt;
}

std::optional<access::CommandFailure> FailureLog::refusal(access::TransactionTicket ticket) const {
  for (const auto& failure : failures_) {
    if (ticket.contains(failure.ticket)) return failure;
  }
  return std::nullopt;
}

}  // namespace atlantis::gameplay
