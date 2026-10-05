#include <atlantis/runtime/entity_ref_resolution.h>

#include <atlantis/assert.h>

namespace atlantis::runtime {

const char* toString(EntityRefError error) noexcept {
  switch (error) {  // no default -- matching init_error.cpp's own established idiom
    case EntityRefError::UnknownScene:
      return "UnknownScene";
    case EntityRefError::UnknownEntity:
      return "UnknownEntity";
    case EntityRefError::DeadEntity:
      return "DeadEntity";
  }
  ATLANTIS_CHECK_MSG(false, "toString(EntityRefError): unhandled enumerator");
  return "(unrecognized EntityRefError)";
}

atlantis::Result<atlantis::world::EntityId, EntityRefError> resolveEntityRef(
    const LoadedSceneView& loaded, const atlantis::asset_system::EntityRef& ref) {
  using ResultT = atlantis::Result<atlantis::world::EntityId, EntityRefError>;
  if (ref.scene != loaded.sceneGuid) return ResultT::Err(EntityRefError::UnknownScene);
  const auto entity = loaded.entities.find(ref.entity);
  if (!entity.has_value()) return ResultT::Err(EntityRefError::UnknownEntity);
  if (!loaded.world.isValid(*entity)) return ResultT::Err(EntityRefError::DeadEntity);
  return ResultT::Ok(*entity);
}

}  // namespace atlantis::runtime
