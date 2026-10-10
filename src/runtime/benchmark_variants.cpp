#include "benchmark_variants.h"

#include <atlantis/connection/text.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/light.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace atlantis::runtime::benchmark {

namespace access = atlantis::world::access;
namespace text = atlantis::connection::text;
using atlantis::asset_system::EntityGuid;
using cli::BenchmarkVariant;

namespace {

[[nodiscard]] bool hasComponent(atlantis::connection::RuntimeConnection& connection, const EntityGuid& entity,
                                atlantis::schema::TypeId type) {
  const auto components = connection.listComponents(entity);
  return components.isOk() &&
         std::find(components.value().begin(), components.value().end(), type) != components.value().end();
}

[[nodiscard]] bool isPointLight(atlantis::connection::RuntimeConnection& connection, const EntityGuid& entity,
                                const text::ResolvedPath& kindPath) {
  const auto kind = connection.getProperty({entity, kindPath.component, kindPath.leaf->id});
  if (!kind.isOk()) return false;
  const auto* value = std::get_if<access::EnumValue>(&kind.value());
  return value != nullptr && value->value == static_cast<std::int64_t>(atlantis::world::LightKind::Point);
}

// A property path in the connection's schema, or an error.
[[nodiscard]] atlantis::Result<text::ResolvedPath, std::string> resolve(
    atlantis::connection::RuntimeConnection& connection, std::string_view path) {
  const auto resolved = text::parsePath(connection.schema(), path);
  if (resolved.isErr()) {
    return atlantis::Result<text::ResolvedPath, std::string>::Err("the schema has no property " + std::string(path));
  }
  return atlantis::Result<text::ResolvedPath, std::string>::Ok(resolved.value());
}

}  // namespace

atlantis::Result<VariantEdit, std::string> submitVariant(atlantis::connection::RuntimeConnection& connection,
                                                         BenchmarkVariant variant) {
  using ResultT = atlantis::Result<VariantEdit, std::string>;
  if (variant == BenchmarkVariant::Baseline) return ResultT::Ok(VariantEdit{});

  std::vector<access::Command> commands;
  const std::vector<EntityGuid> entities = connection.listEntities();

  if (variant == BenchmarkVariant::NoFog || variant == BenchmarkVariant::NoBloom) {
    const std::string_view path = variant == BenchmarkVariant::NoFog ? "Camera.fog.density" : "Camera.bloom.strength";
    const auto resolved = resolve(connection, path);
    if (resolved.isErr()) return ResultT::Err(resolved.error());
    for (const EntityGuid& entity : entities) {
      if (!hasComponent(connection, entity, resolved.value().component)) continue;
      commands.push_back(access::SetProperty{{entity, resolved.value().component, resolved.value().leaf->id}, 0.0f});
    }
    if (commands.empty()) return ResultT::Err("the scene has no camera to edit");
  } else {
    const auto kind = resolve(connection, "Light.kind");
    if (kind.isErr()) return ResultT::Err(kind.error());
    for (const EntityGuid& entity : entities) {
      if (!hasComponent(connection, entity, kind.value().component)) continue;
      if (isPointLight(connection, entity, kind.value())) {
        commands.push_back(access::RemoveComponent{entity, kind.value().component});
      }
    }
    if (commands.empty()) return ResultT::Err("the scene has no point light to remove");
  }

  VariantEdit edit;
  edit.commands = commands.size();
  connection.submitTransaction(std::move(commands));
  return ResultT::Ok(edit);
}

std::string verifyVariant(atlantis::connection::RuntimeConnection& connection, BenchmarkVariant variant) {
  if (variant == BenchmarkVariant::Baseline) return {};
  if (!connection.drainFailures().empty()) return "the Runtime refused the variant's edit";

  const std::vector<EntityGuid> entities = connection.listEntities();
  if (variant == BenchmarkVariant::NoFog || variant == BenchmarkVariant::NoBloom) {
    const std::string_view path = variant == BenchmarkVariant::NoFog ? "Camera.fog.density" : "Camera.bloom.strength";
    const auto resolved = resolve(connection, path);
    if (resolved.isErr()) return resolved.error();
    for (const EntityGuid& entity : entities) {
      if (!hasComponent(connection, entity, resolved.value().component)) continue;
      const auto value = connection.getProperty({entity, resolved.value().component, resolved.value().leaf->id});
      const auto* number = value.isOk() ? std::get_if<float>(&value.value()) : nullptr;
      if (number == nullptr || *number != 0.0f) return std::string(path) + " is not 0 after the edit";
    }
    return {};
  }
  const auto kind = resolve(connection, "Light.kind");
  if (kind.isErr()) return kind.error();
  for (const EntityGuid& entity : entities) {
    if (hasComponent(connection, entity, kind.value().component) && isPointLight(connection, entity, kind.value())) {
      return "a point light is still present after the edit";
    }
  }
  return {};
}

}  // namespace atlantis::runtime::benchmark
