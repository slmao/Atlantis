#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/scene_semantic_types.h>

#include <optional>
#include <vector>

namespace atlantis::asset_system::scene {

// Spec 0049 R2 / ADR-0100 D3 (rulings Q4, Q5): the in-memory authoring
// scene, a semantic instance. Nodes are ordered (the order is semantic: it is
// the instantiation order). A node is identified by its EntityGuid, and every
// reference to a node -- parent, active camera -- is EntityGuid-valued, never
// an index or a serializer's node_id. Validity (exclusivity, acyclic parents,
// value domains, ...) is stated by sceneSchema(), not enforced by these types.
// Plain values, safe for concurrent use; not an editing API.

struct AuthoringNode {
  EntityGuid guid;
  std::optional<EntityGuid> parent;
  Transform transform;
  std::optional<Camera> camera;
  std::optional<Renderable> renderable;
  std::optional<Light> light;
  friend bool operator==(const AuthoringNode&, const AuthoringNode&) = default;
};

// Document-level fields sit here, never on a node (Spec 0049 ruling Q4).
struct AuthoringScene {
  std::vector<AuthoringNode> nodes;
  std::optional<EntityGuid> activeCamera;
  friend bool operator==(const AuthoringScene&, const AuthoringScene&) = default;
};

}  // namespace atlantis::asset_system::scene
