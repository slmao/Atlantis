#pragma once

#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/result.h>

#include <string_view>

namespace atlantis::asset_system::scene {

// Plan 0049 P4 (Spec 0049 R5, ruling Q5; ADR-0100 D1): the scene source
// serializer's mapping to and from the semantic model. It lives on the
// serializer side -- the only code naming both the Decoded* codec DTOs and
// the semantic types. Both directions are pure and exact: a ParsedSceneSource
// maps to an AuthoringScene and back to one with equal content, node_ids
// renumbered (they are syntax). They resolve references only; every other
// scene constraint stays the cooker's to check (ruling Q3).
enum class SceneMappingError {
  DuplicateNodeId,                 // two source nodes share a node_id
  UndeclaredParentReference,       // parent= names no node_id
  UndeclaredActiveCameraReference, // active_camera: names no node_id
  AmbiguousEntityGuid,             // two nodes share an EntityGuid
  MaterialWithoutMesh,             // a parsed node with material= but no mesh=
  DanglingParentReference,         // a parent GUID that names no node
  DanglingActiveCameraReference,   // an active-camera GUID that names no node
};

[[nodiscard]] std::string_view toString(SceneMappingError error) noexcept;

[[nodiscard]] atlantis::Result<AuthoringScene, SceneMappingError> toAuthoringScene(const ParsedSceneSource& source);

// node_id = declaration index + 1.
[[nodiscard]] atlantis::Result<ParsedSceneSource, SceneMappingError> toParsedSceneSource(const AuthoringScene& scene);

}  // namespace atlantis::asset_system::scene
