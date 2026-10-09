#include <atlantis/gameplay/world.h>

#include <atlantis/connection/text.h>

#include <algorithm>
#include <utility>

namespace atlantis::gameplay {

namespace access = ::atlantis::world::access;
namespace text = ::atlantis::connection::text;

World::World(::atlantis::connection::RuntimeConnection& connection, QueryBatch* batch) : connection_(connection) {
  if (batch == nullptr) {
    sequential_ = std::make_unique<SequentialQueryBatch>(connection_);
    batch_ = sequential_.get();
  } else {
    batch_ = batch;
  }
}

bool World::exists(const EntityGuid& entity) const { return connection_.findEntity(entity); }

std::vector<EntityGuid> World::entities() const { return connection_.listEntities(); }

atlantis::Result<std::vector<schema::TypeId>, Error> World::components(const EntityGuid& entity) const {
  using ResultT = atlantis::Result<std::vector<schema::TypeId>, Error>;
  auto listed = connection_.listComponents(entity);
  if (listed.isErr()) return ResultT::Err(Error::refused(listed.error(), text::formatEntity(entity)));
  return ResultT::Ok(std::move(listed.value()));
}

atlantis::Result<schema::TypeId, Error> World::typeNamed(std::string_view type) const {
  using ResultT = atlantis::Result<schema::TypeId, Error>;
  const schema::TypeDescriptor* found = text::findType(connection_.schema(), type);
  if (found == nullptr || found->kind != schema::TypeKind::Struct) {
    Error error;
    error.kind = ErrorKind::UnknownType;
    error.subject = std::string(type);
    return ResultT::Err(std::move(error));
  }
  return ResultT::Ok(found->id);
}

atlantis::Result<std::vector<EntityGuid>, Error> World::entitiesWith(std::initializer_list<std::string_view> types) {
  using ResultT = atlantis::Result<std::vector<EntityGuid>, Error>;
  std::vector<schema::TypeId> ids;
  for (const std::string_view type : types) {
    auto id = typeNamed(type);
    if (id.isErr()) return ResultT::Err(std::move(id.error()));
    ids.push_back(id.value());
  }
  return entitiesWith(std::span<const schema::TypeId>(ids));
}

atlantis::Result<std::vector<EntityGuid>, Error> World::entitiesWith(std::span<const schema::TypeId> types) {
  using ResultT = atlantis::Result<std::vector<EntityGuid>, Error>;
  const std::vector<EntityGuid> listed = connection_.listEntities();
  const auto components = batch_->listComponents(listed);
  std::vector<EntityGuid> out;
  for (std::size_t i = 0; i < listed.size() && i < components.size(); ++i) {
    if (components[i].isErr()) continue;  // destroyed between the listing and its query
    const std::vector<schema::TypeId>& held = components[i].value();
    const bool all = std::all_of(types.begin(), types.end(), [&](schema::TypeId type) {
      return std::find(held.begin(), held.end(), type) != held.end();
    });
    if (all) out.push_back(listed[i]);
  }
  return ResultT::Ok(std::move(out));
}

atlantis::Result<PropertyAddress, Error> World::resolve(const EntityGuid& entity, std::string_view path) const {
  using ResultT = atlantis::Result<PropertyAddress, Error>;
  const auto resolved = text::parsePath(connection_.schema(), path);
  if (resolved.isErr()) return ResultT::Err(Error::fromText(resolved.error(), std::string(path)));
  return ResultT::Ok(PropertyAddress{entity, resolved.value().component, resolved.value().leaf->id});
}

atlantis::Result<PropertyValue, Error> World::get(const EntityGuid& entity, std::string_view path) const {
  using ResultT = atlantis::Result<PropertyValue, Error>;
  auto address = resolve(entity, path);
  if (address.isErr()) return ResultT::Err(std::move(address.error()));
  auto value = connection_.getProperty(address.value());
  if (value.isErr()) return ResultT::Err(Error::refused(value.error(), std::string(path)));
  return ResultT::Ok(std::move(value.value()));
}

atlantis::Result<PropertyValue, Error> World::get(const PropertyAddress& address) const {
  using ResultT = atlantis::Result<PropertyValue, Error>;
  auto value = connection_.getProperty(address);
  if (value.isErr()) return ResultT::Err(Error::refused(value.error(), text::formatEntity(address.entity)));
  return ResultT::Ok(std::move(value.value()));
}

atlantis::Result<access::CommandTicket, Error> World::set(const EntityGuid& entity, std::string_view path,
                                                          PropertyValue value) {
  using ResultT = atlantis::Result<access::CommandTicket, Error>;
  auto address = resolve(entity, path);
  if (address.isErr()) return ResultT::Err(std::move(address.error()));
  return ResultT::Ok(connection_.submit(access::SetProperty{address.value(), std::move(value)}));
}

atlantis::Result<DynamicComponent, Error> World::readComponent(const EntityGuid& entity, std::string_view type) const {
  using ResultT = atlantis::Result<DynamicComponent, Error>;
  auto id = typeNamed(type);
  if (id.isErr()) return ResultT::Err(std::move(id.error()));
  const auto schemaSpan = connection_.schema();
  const std::vector<text::LeafPath> leaves = text::leavesOf(schemaSpan, id.value());
  std::vector<PropertyAddress> addresses;
  addresses.reserve(leaves.size());
  for (const auto& leaf : leaves) addresses.push_back(PropertyAddress{entity, id.value(), leaf.leaf->id});
  auto values = batch_->getProperties(addresses);
  DynamicComponent component;
  component.type = id.value();
  component.name = std::string(text::findType(schemaSpan, id.value())->name);
  for (std::size_t i = 0; i < leaves.size(); ++i) {
    if (i >= values.size()) return ResultT::Err(Error::refused(access::AccessError::UnknownEntity, leaves[i].path));
    if (values[i].isErr()) return ResultT::Err(Error::refused(values[i].error(), leaves[i].path));
    component.leaves.push_back(DynamicLeaf{leaves[i].path, leaves[i].leaf->id, std::move(values[i].value())});
  }
  return ResultT::Ok(std::move(component));
}

atlantis::Result<access::Command, Error> World::resolve(const Transaction::Operation& operation) {
  using ResultT = atlantis::Result<access::Command, Error>;
  using Kind = Transaction::Kind;
  const Transaction::Target& target = operation.target;
  if (!target.bindings.empty()) {  // typed: its binding must match (R5), or the whole transaction is refused
    auto compatible = checkBinding(target.bindings, target.bindingIndex);
    if (compatible.isErr()) return ResultT::Err(std::move(compatible.error()));
  }
  switch (operation.kind) {
    case Kind::Create: return ResultT::Ok(access::CreateEntity{operation.entity});
    case Kind::Destroy: return ResultT::Ok(access::DestroyEntity{operation.entity});
    case Kind::AddComponent:
    case Kind::RemoveComponent: {
      schema::TypeId type = target.component;
      if (!target.name.empty()) {
        auto named = typeNamed(target.name);
        if (named.isErr()) return ResultT::Err(std::move(named.error()));
        type = named.value();
      }
      if (operation.kind == Kind::AddComponent) return ResultT::Ok(access::AddComponent{operation.entity, type});
      return ResultT::Ok(access::RemoveComponent{operation.entity, type});
    }
    case Kind::SetProperty: {
      PropertyAddress address{operation.entity, target.component, target.field};
      if (!target.name.empty()) {
        auto resolved = resolve(operation.entity, target.name);
        if (resolved.isErr()) return ResultT::Err(std::move(resolved.error()));
        address = resolved.value();
      }
      return ResultT::Ok(access::SetProperty{address, operation.value});
    }
  }
  return ResultT::Err(Error::refused(access::AccessError::UnknownEntity, "an unrecognized operation"));
}

atlantis::Result<access::TransactionTicket, Error> World::submit(const Transaction& transaction) {
  using ResultT = atlantis::Result<access::TransactionTicket, Error>;
  std::vector<access::Command> commands;
  commands.reserve(transaction.operations().size());
  for (const Transaction::Operation& operation : transaction.operations()) {
    auto command = resolve(operation);
    if (command.isErr()) return ResultT::Err(std::move(command.error()));  // nothing submitted (J6)
    commands.push_back(std::move(command.value()));
  }
  return ResultT::Ok(connection_.submitTransaction(std::move(commands)));
}

access::CommandTicket World::submit(access::Command command) { return connection_.submit(std::move(command)); }

Subscription World::subscribe(::atlantis::connection::EventFilter filter) {
  return Subscription(connection_, connection_.subscribe(std::move(filter)));
}

std::vector<access::CommandFailure> World::drainFailures() { return connection_.drainFailures(); }

}  // namespace atlantis::gameplay
