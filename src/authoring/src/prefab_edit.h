#pragma once

#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/guid_mint.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/authoring/authoring_session.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

namespace atlantis::authoring::prefab_edit {

namespace as = asset_system;
namespace scene = as::scene;
namespace ac = connection::authoring;

inline std::optional<scene::AuthoringScene> decode(std::string_view source) {
  if (source.size() > 64 * 1024) return std::nullopt;
  const auto parsed = as::parseSceneSource(source);
  if (parsed.isErr() || parsed.value().nodes.empty() || parsed.value().nodes.size() > ac::kMaxGuardedDuplicateNodes ||
      as::serializeSceneSource(parsed.value()) != source) return std::nullopt;
  auto mapped = scene::toAuthoringScene(parsed.value());
  if (mapped.isErr()) return std::nullopt;
  return std::move(mapped.value());
}

inline std::optional<std::string> encode(const scene::AuthoringScene& subtree) {
  const auto parsed = scene::toParsedSceneSource(subtree);
  if (parsed.isErr()) return std::nullopt;
  auto value = as::serializeSceneSource(parsed.value());
  if (value.size() > 64 * 1024) return std::nullopt;
  return value;
}

inline std::optional<std::string> normalized(scene::AuthoringScene subtree, const as::EntityGuid& root,
                                            const as::AssetGuid& origin, bool compareTemplate) {
  auto at = std::ranges::find(subtree.nodes, root, &scene::AuthoringNode::guid);
  if (at == subtree.nodes.end()) return std::nullopt;
  at->parent.reset();
  at->name.clear();
  at->transform = {};
  at->prefabOrigin.reset();
  at->prefabBaseline.reset();
  if (compareTemplate) {
    // The root anchor is placement. Compare the managed order of descendants
    // even when the source originally listed a child before the root.
    std::rotate(subtree.nodes.begin(), at, at + 1);
    auto parsed = scene::toParsedSceneSource(subtree);
    if (parsed.isErr()) return std::nullopt;
    for (auto& node : parsed.value().nodes)
      node.entityGuid = as::deriveEntityGuid(origin, "node/" + std::to_string(node.nodeId));
    return as::serializeSceneSource(parsed.value());
  }
  return encode(subtree);
}

inline std::optional<std::string> digest(const scene::AuthoringScene& subtree, const as::EntityGuid& root,
                                        const as::AssetGuid& origin, const PrefabDigest& sha) {
  const auto canonical = normalized(subtree, root, origin, false);
  if (!canonical) return std::nullopt;
  return sha("atlantis.prefab.baseline/1\n" + as::toString(origin) + "\n" + *canonical);
}

inline std::optional<ac::PrefabSubtree> capture(const scene::AuthoringScene& document, const as::EntityGuid& root,
                                               const PrefabDigest& sha) {
  std::string order = "atlantis.prefab.document-order/1\n";
  std::map<as::EntityGuid, const scene::AuthoringNode*> nodes;
  std::map<as::EntityGuid, std::vector<as::EntityGuid>> children;
  for (const auto& node : document.nodes) {
    nodes.emplace(node.guid, &node);
    order += as::toString(node.guid);
    if (node.parent) {
      children[*node.parent].push_back(node.guid);
      order += ' ';
      order += as::toString(*node.parent);
    }
    order += '\n';
  }
  const auto ordering = sha(order);
  if (!ordering) return std::nullopt;
  ac::PrefabSubtree captured{{}, {}, {}, *ordering};
  const auto found = nodes.find(root);
  if (found == nodes.end()) return captured;
  const auto& anchor = *found->second;
  if (!anchor.prefabOrigin) return std::nullopt;
  for (auto parent = anchor.parent; parent; parent = nodes.at(*parent)->parent)
    if (nodes.at(*parent)->prefabOrigin) return std::nullopt;
  captured.parent = anchor.parent;

  std::vector<as::EntityGuid> selected{root};
  for (std::size_t i = 0; i < selected.size(); ++i) {
    const auto& dependants = children[selected[i]];
    if (dependants.size() > ac::kMaxGuardedDuplicateNodes - selected.size()) return std::nullopt;
    selected.insert(selected.end(), dependants.begin(), dependants.end());
  }
  const std::set<as::EntityGuid> included(selected.begin(), selected.end());
  if (document.activeCamera && included.contains(*document.activeCamera)) return std::nullopt;
  scene::AuthoringScene subtree;
  for (std::size_t slot = 0; slot < document.nodes.size(); ++slot) {
    const auto& node = document.nodes[slot];
    if (!included.contains(node.guid)) continue;
    if (node.camera || node.light || (node.guid != root && node.prefabOrigin)) return std::nullopt;
    captured.slots.push_back(slot);
    auto& copy = subtree.nodes.emplace_back(node);
    if (node.guid == root) copy.parent.reset();
  }
  const auto bytes = encode(subtree);
  if (!bytes) return std::nullopt;
  captured.source = *bytes;
  return captured;
}

inline bool assetsAvailable(std::string_view source, const PrefabAssets& available) {
  if (source.empty()) return true;
  const auto subtree = decode(source);
  return subtree && std::ranges::all_of(subtree->nodes, [&](const auto& node) {
    return !node.renderable || available(*node.renderable);
  });
}

inline std::optional<scene::AuthoringScene> reinsert(const scene::AuthoringScene& document, const as::EntityGuid& root,
    const ac::PrefabSubtree& required, const ac::PrefabSubtree& replacement, const PrefabDigest& sha) {
  scene::AuthoringScene result = document;
  std::vector<scene::AuthoringNode> old;
  for (const auto slot : required.slots) old.push_back(document.nodes[static_cast<std::size_t>(slot)]);
  for (auto it = required.slots.rbegin(); it != required.slots.rend(); ++it)
    result.nodes.erase(result.nodes.begin() + static_cast<std::ptrdiff_t>(*it));

  if (replacement.source.empty()) {
    if (!replacement.slots.empty() || replacement.parent || required.parent) return std::nullopt;
  } else {
    auto next = decode(replacement.source);
    if (!next || next->activeCamera || next->nodes.size() != replacement.slots.size() ||
        !std::ranges::is_sorted(replacement.slots) ||
        std::ranges::adjacent_find(replacement.slots) != replacement.slots.end()) return std::nullopt;
    const auto anchor = std::ranges::find(next->nodes, root, &scene::AuthoringNode::guid);
    if (anchor == next->nodes.end() || !anchor->prefabOrigin || anchor->parent ||
        std::ranges::count_if(next->nodes, [](const auto& node) { return !node.parent; }) != 1)
      return std::nullopt;
    const auto previous = std::ranges::find(old, root, &scene::AuthoringNode::guid);
    if (previous != old.end() &&
        (previous->parent != replacement.parent || previous->name != anchor->name ||
         previous->prefabOrigin != anchor->prefabOrigin || previous->transform != anchor->transform))
      return std::nullopt;
    const auto nextDigest = digest(*next, root, *anchor->prefabOrigin, sha);
    if (!nextDigest || (anchor->prefabBaseline && anchor->prefabBaseline != nextDigest)) return std::nullopt;
    if (!anchor->prefabBaseline) {
      const auto before = decode(required.source);
      if (!before || normalized(*next, root, *anchor->prefabOrigin, false) !=
                     normalized(*before, root, *anchor->prefabOrigin, false))
        return std::nullopt;
    }
    anchor->parent = replacement.parent;
    for (std::size_t i = 0; i < next->nodes.size(); ++i) {
      auto& node = next->nodes[i];
      const auto prior = std::ranges::find(old, node.guid, &scene::AuthoringNode::guid);
      if (node.guid != root && prior != old.end() && *prior != node) return std::nullopt;
      if (std::ranges::find(result.nodes, node.guid, &scene::AuthoringNode::guid) != result.nodes.end() ||
          replacement.slots[i] > result.nodes.size()) return std::nullopt;
      result.nodes.insert(result.nodes.begin() + static_cast<std::ptrdiff_t>(replacement.slots[i]), std::move(node));
    }
  }
  // Commit validates the whole document. Check parent pointers and order only
  // after the payload is known to be a single bounded replacement subtree.
  if (replacement.parent &&
      std::ranges::find(result.nodes, *replacement.parent, &scene::AuthoringNode::guid) == result.nodes.end())
    return std::nullopt;
  return result;
}

}  // namespace atlantis::authoring::prefab_edit
