#include <atlantis/asset_system/authoring_scene_mapping.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>

namespace atlantis::asset_system::scene {

namespace {

[[nodiscard]] Transform toSemantic(const DecodedTransform& t) {
  return {{t.positionX, t.positionY, t.positionZ},
          {t.eulerXRadians, t.eulerYRadians, t.eulerZRadians},
          {t.scaleX, t.scaleY, t.scaleZ}};
}

[[nodiscard]] DecodedTransform toDecoded(const Transform& t) {
  DecodedTransform out;
  out.positionX = t.localPosition[0];
  out.positionY = t.localPosition[1];
  out.positionZ = t.localPosition[2];
  out.eulerXRadians = t.localEulerAnglesRadians[0];
  out.eulerYRadians = t.localEulerAnglesRadians[1];
  out.eulerZRadians = t.localEulerAnglesRadians[2];
  out.scaleX = t.localScale[0];
  out.scaleY = t.localScale[1];
  out.scaleZ = t.localScale[2];
  return out;
}

[[nodiscard]] Camera toSemantic(const DecodedCamera& c) {
  Camera out;
  out.fovYRadians = c.fovYRadians;
  out.nearZ = c.nearZ;
  out.farZ = c.farZ;
  out.exposureCompensationEv = c.exposureCompensationEv;
  out.fog = {{c.fog.colorR, c.fog.colorG, c.fog.colorB},
             c.fog.density,
             c.fog.height,
             c.fog.heightFalloff,
             c.fog.maxOpacity};
  out.bloom = {c.bloom.strength, c.bloom.threshold};
  return out;
}

[[nodiscard]] DecodedCamera toDecoded(const Camera& c) {
  DecodedCamera out;
  out.fovYRadians = c.fovYRadians;
  out.nearZ = c.nearZ;
  out.farZ = c.farZ;
  out.exposureCompensationEv = c.exposureCompensationEv;
  out.fog.colorR = c.fog.color[0];
  out.fog.colorG = c.fog.color[1];
  out.fog.colorB = c.fog.color[2];
  out.fog.density = c.fog.density;
  out.fog.height = c.fog.height;
  out.fog.heightFalloff = c.fog.heightFalloff;
  out.fog.maxOpacity = c.fog.maxOpacity;
  out.bloom.strength = c.bloom.strength;
  out.bloom.threshold = c.bloom.threshold;
  return out;
}

[[nodiscard]] Light toSemantic(const DecodedLight& l) {
  return {l.kind == DecodedLightKind::Point ? LightKind::Point : LightKind::Directional,
          {l.colorR, l.colorG, l.colorB},
          l.intensity,
          l.range};
}

[[nodiscard]] DecodedLight toDecoded(const Light& l) {
  DecodedLight out;
  out.kind = l.kind == LightKind::Point ? DecodedLightKind::Point : DecodedLightKind::Directional;
  out.colorR = l.color[0];
  out.colorG = l.color[1];
  out.colorB = l.color[2];
  out.intensity = l.intensity;
  out.range = l.range;
  return out;
}

}  // namespace

std::string_view toString(SceneMappingError error) noexcept {
  switch (error) {
    case SceneMappingError::DuplicateNodeId: return "DuplicateNodeId";
    case SceneMappingError::UndeclaredParentReference: return "UndeclaredParentReference";
    case SceneMappingError::UndeclaredActiveCameraReference: return "UndeclaredActiveCameraReference";
    case SceneMappingError::AmbiguousEntityGuid: return "AmbiguousEntityGuid";
    case SceneMappingError::MaterialWithoutMesh: return "MaterialWithoutMesh";
    case SceneMappingError::DanglingParentReference: return "DanglingParentReference";
    case SceneMappingError::DanglingActiveCameraReference: return "DanglingActiveCameraReference";
  }
  return "Unknown";
}

atlantis::Result<AuthoringScene, SceneMappingError> toAuthoringScene(const ParsedSceneSource& source) {
  using ResultT = atlantis::Result<AuthoringScene, SceneMappingError>;

  std::unordered_map<std::uint32_t, std::size_t> indexOfNodeId;
  std::map<EntityGuid, std::size_t> indexOfGuid;
  for (std::size_t i = 0; i < source.nodes.size(); ++i) {
    if (!indexOfNodeId.emplace(source.nodes[i].nodeId, i).second) {
      return ResultT::Err(SceneMappingError::DuplicateNodeId);
    }
    if (!indexOfGuid.emplace(source.nodes[i].entityGuid, i).second) {
      return ResultT::Err(SceneMappingError::AmbiguousEntityGuid);
    }
  }

  AuthoringScene scene;
  scene.nodes.reserve(source.nodes.size());
  for (const ParsedSceneNode& parsed : source.nodes) {
    AuthoringNode node;
    node.guid = parsed.entityGuid;
    if (parsed.parentNodeId.has_value()) {
      const auto parent = indexOfNodeId.find(*parsed.parentNodeId);
      if (parent == indexOfNodeId.end()) return ResultT::Err(SceneMappingError::UndeclaredParentReference);
      node.parent = source.nodes[parent->second].entityGuid;
    }
    node.transform = toSemantic(parsed.transform);
    if (parsed.camera.has_value()) node.camera = toSemantic(*parsed.camera);
    if (parsed.materialAsset.has_value() && !parsed.meshAsset.has_value()) {
      return ResultT::Err(SceneMappingError::MaterialWithoutMesh);
    }
    if (parsed.meshAsset.has_value()) node.renderable = Renderable{*parsed.meshAsset, parsed.materialAsset};
    if (parsed.light.has_value()) node.light = toSemantic(*parsed.light);
    scene.nodes.push_back(std::move(node));
  }

  if (source.activeCameraNodeId.has_value()) {
    const auto camera = indexOfNodeId.find(*source.activeCameraNodeId);
    if (camera == indexOfNodeId.end()) return ResultT::Err(SceneMappingError::UndeclaredActiveCameraReference);
    scene.activeCamera = source.nodes[camera->second].entityGuid;
  }
  return ResultT::Ok(std::move(scene));
}

atlantis::Result<ParsedSceneSource, SceneMappingError> toParsedSceneSource(const AuthoringScene& scene) {
  using ResultT = atlantis::Result<ParsedSceneSource, SceneMappingError>;

  std::map<EntityGuid, std::uint32_t> nodeIdOfGuid;
  for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
    if (!nodeIdOfGuid.emplace(scene.nodes[i].guid, static_cast<std::uint32_t>(i + 1)).second) {
      return ResultT::Err(SceneMappingError::AmbiguousEntityGuid);
    }
  }

  ParsedSceneSource source;
  source.nodes.reserve(scene.nodes.size());
  for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
    const AuthoringNode& node = scene.nodes[i];
    ParsedSceneNode parsed;
    parsed.nodeId = static_cast<std::uint32_t>(i + 1);
    parsed.entityGuid = node.guid;
    if (node.parent.has_value()) {
      const auto parent = nodeIdOfGuid.find(*node.parent);
      if (parent == nodeIdOfGuid.end()) return ResultT::Err(SceneMappingError::DanglingParentReference);
      parsed.parentNodeId = parent->second;
    }
    parsed.transform = toDecoded(node.transform);
    if (node.camera.has_value()) parsed.camera = toDecoded(*node.camera);
    if (node.renderable.has_value()) {
      parsed.meshAsset = node.renderable->meshAsset;
      parsed.materialAsset = node.renderable->materialAsset;
    }
    if (node.light.has_value()) parsed.light = toDecoded(*node.light);
    source.nodes.push_back(std::move(parsed));
  }

  if (scene.activeCamera.has_value()) {
    const auto camera = nodeIdOfGuid.find(*scene.activeCamera);
    if (camera == nodeIdOfGuid.end()) return ResultT::Err(SceneMappingError::DanglingActiveCameraReference);
    source.activeCameraNodeId = camera->second;
  }
  return ResultT::Ok(std::move(source));
}

}  // namespace atlantis::asset_system::scene
