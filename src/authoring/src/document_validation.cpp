#include <atlantis/authoring/document_validation.h>

#include "document_access.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/scene_semantic_schema.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace atlantis::authoring {

namespace {

namespace scene = atlantis::asset_system::scene;
namespace ac = atlantis::connection::authoring;
using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;

// One field domain of sceneSchema(), where it lives: the node component that
// reaches its field, and how to read it there.
struct DomainSite {
  scene::FieldDomain domain;
  schema::TypeId component;
  detail::ResolvedField field;
};

const std::vector<DomainSite>& domainSites() {
  static const std::vector<DomainSite> sites = [] {
    std::vector<DomainSite> out;
    for (const scene::FieldDomain& domain : scene::sceneSchema().domains) {
      bool placed = false;
      for (const schema::TypeId component : detail::nodeComponentTypes()) {
        const auto resolved = detail::resolveField(component, domain.field);
        if (resolved.isErr()) continue;
        out.push_back({domain, component, resolved.value()});
        placed = true;
      }
      ATLANTIS_CHECK_MSG(placed, "domainSites(): a sceneSchema() domain reaches no node component");
    }
    return out;
  }();
  return sites;
}

[[nodiscard]] bool withinDomain(const scene::FieldDomain& domain, const ac::PropertyValue& value) {
  const auto eachFloat = [&](auto predicate) {
    if (const auto* f = std::get_if<float>(&value)) return predicate(*f);
    if (const auto* v = std::get_if<std::array<float, 3>>(&value)) {
      return std::all_of(v->begin(), v->end(), predicate);
    }
    return true;
  };
  switch (domain.kind) {
    case scene::DomainKind::Finite:
      return eachFloat([](float x) { return std::isfinite(x); });
    case scene::DomainKind::Closed:
      return eachFloat([&](float x) { return std::isfinite(x) && x >= domain.min && x <= domain.max; });
    case scene::DomainKind::AtLeast:
      return eachFloat([&](float x) { return std::isfinite(x) && x >= domain.min; });
    case scene::DomainKind::NonNilGuid:
      if (const auto* guid = std::get_if<AssetGuid>(&value)) return *guid != AssetGuid{};
      return true;  // Absent: an Optional reference that is not set
    case scene::DomainKind::Enumerated:
      return true;  // checked when the value was written (EnumValueOutOfRange)
  }
  return false;
}

// sceneSchema()'s exclusive groups: the node components of each non-zero
// exclusiveGroup.
struct ExclusiveGroup {
  std::uint8_t id = 0;
  std::vector<schema::TypeId> components;
};

const std::vector<ExclusiveGroup>& exclusiveGroups() {
  static const std::vector<ExclusiveGroup> groups = [] {
    std::vector<ExclusiveGroup> out;
    for (const scene::ComponentRule& rule : scene::sceneSchema().components) {
      if (rule.exclusiveGroup == 0) continue;
      auto at = std::find_if(out.begin(), out.end(), [&](const auto& g) { return g.id == rule.exclusiveGroup; });
      if (at == out.end()) at = out.insert(out.end(), ExclusiveGroup{rule.exclusiveGroup, {}});
      at->components.push_back(rule.component);
    }
    return out;
  }();
  return groups;
}

// node GUID -> its index, for lookups over the whole document.
class GuidIndex {
 public:
  explicit GuidIndex(const scene::AuthoringScene& document) {
    entries_.reserve(document.nodes.size());
    for (std::size_t i = 0; i < document.nodes.size(); ++i) entries_.emplace_back(document.nodes[i].guid, i);
    std::sort(entries_.begin(), entries_.end());
  }
  [[nodiscard]] bool hasDuplicate() const {
    return std::adjacent_find(entries_.begin(), entries_.end(),
                              [](const auto& a, const auto& b) { return a.first == b.first; }) != entries_.end();
  }
  [[nodiscard]] std::optional<std::size_t> find(const EntityGuid& guid) const {
    const auto at = std::lower_bound(entries_.begin(), entries_.end(), std::make_pair(guid, std::size_t{0}));
    if (at == entries_.end() || at->first != guid) return std::nullopt;
    return at->second;
  }

 private:
  std::vector<std::pair<EntityGuid, std::size_t>> entries_;
};

[[nodiscard]] bool hasParentCycle(const scene::AuthoringScene& document, const GuidIndex& index) {
  enum class State : std::uint8_t { Unvisited, Visiting, Done };
  std::vector<State> state(document.nodes.size(), State::Unvisited);
  std::vector<std::size_t> path;
  for (std::size_t start = 0; start < document.nodes.size(); ++start) {
    if (state[start] != State::Unvisited) continue;
    path.clear();
    std::optional<std::size_t> current = start;
    while (current.has_value()) {
      if (state[*current] == State::Visiting) return true;
      if (state[*current] == State::Done) break;
      state[*current] = State::Visiting;
      path.push_back(*current);
      const auto& parent = document.nodes[*current].parent;
      current = parent.has_value() ? index.find(*parent) : std::nullopt;
    }
    for (const std::size_t node : path) state[node] = State::Done;
  }
  return false;
}

[[nodiscard]] std::optional<ac::Error> checkConstraint(const scene::ConstraintRule& rule,
                                                       const scene::AuthoringScene& document, const GuidIndex& index) {
  switch (rule.id) {
    case scene::Constraint::NonEmptyDocument:
      if (document.nodes.empty()) return ac::Error::NonEmptyDocument;
      return std::nullopt;
    case scene::Constraint::NodeGuidNonNil:
      for (const auto& node : document.nodes) {
        if (node.guid == EntityGuid{}) return ac::Error::NodeGuidNonNil;
      }
      return std::nullopt;
    case scene::Constraint::NodeGuidUnique:
      if (index.hasDuplicate()) return ac::Error::NodeGuidUnique;
      return std::nullopt;
    case scene::Constraint::ParentExists:
      for (const auto& node : document.nodes) {
        if (node.parent.has_value() && !index.find(*node.parent).has_value()) return ac::Error::ParentExists;
      }
      return std::nullopt;
    case scene::Constraint::ParentAcyclic:
      if (hasParentCycle(document, index)) return ac::Error::ParentAcyclic;
      return std::nullopt;
    case scene::Constraint::ActiveCameraExists:
      if (document.activeCamera.has_value() && !index.find(*document.activeCamera).has_value()) {
        return ac::Error::ActiveCameraExists;
      }
      return std::nullopt;
    case scene::Constraint::ActiveCameraHasCamera:
      if (document.activeCamera.has_value()) {
        const auto camera = index.find(*document.activeCamera);
        if (camera.has_value() && !document.nodes[*camera].camera.has_value()) return ac::Error::ActiveCameraHasCamera;
      }
      return std::nullopt;
    case scene::Constraint::ComponentExclusivity:
      for (const auto& node : document.nodes) {
        for (const ExclusiveGroup& group : exclusiveGroups()) {
          const auto present = std::count_if(group.components.begin(), group.components.end(),
                                             [&](schema::TypeId type) { return detail::hasComponent(node, type); });
          if (present > 1) return ac::Error::ComponentExclusivity;
        }
      }
      return std::nullopt;
    case scene::Constraint::LightRangeMatchesKind:
      for (const auto& node : document.nodes) {
        if (!node.light.has_value()) continue;
        const bool point = node.light->kind == scene::LightKind::Point;
        if (point ? !(node.light->range > 0.0f) : node.light->range != 0.0f) return ac::Error::LightRangeMatchesKind;
      }
      return std::nullopt;
    case scene::Constraint::MaxDirectionalLights:
    case scene::Constraint::MaxPointLights: {
      const scene::LightKind kind = rule.id == scene::Constraint::MaxDirectionalLights ? scene::LightKind::Directional
                                                                                       : scene::LightKind::Point;
      const auto count = std::count_if(document.nodes.begin(), document.nodes.end(),
                                       [&](const auto& node) { return node.light && node.light->kind == kind; });
      if (static_cast<std::uint64_t>(count) > rule.limit) {
        return rule.id == scene::Constraint::MaxDirectionalLights ? ac::Error::MaxDirectionalLights
                                                                  : ac::Error::MaxPointLights;
      }
      return std::nullopt;
    }
  }
  ATLANTIS_CHECK_MSG(false, "checkConstraint(): unhandled scene constraint");
  return std::nullopt;
}

}  // namespace

std::optional<ac::Error> checkSchema(const scene::AuthoringScene& document) {
  for (const scene::AuthoringNode& node : document.nodes) {
    for (const DomainSite& site : domainSites()) {
      const std::byte* component = detail::componentBytes(node, site.component);
      if (component == nullptr) continue;
      const ac::PropertyValue value = detail::readField(component, site.component, site.domain.field, site.field);
      if (!withinDomain(site.domain, value)) return ac::Error::FieldOutOfDomain;
    }
  }
  const GuidIndex index(document);
  for (const scene::ConstraintRule& rule : scene::sceneSchema().constraints) {
    if (const auto refusal = checkConstraint(rule, document, index)) return refusal;
  }
  return std::nullopt;
}

std::optional<ac::Error> checkCook(const scene::AuthoringScene& document, const AssetGuid& scene) {
  const auto cooked = asset_system::cookSceneToMemory(document, scene);
  if (cooked.isOk()) return std::nullopt;
  return cooked.error() == asset_system::SceneCookError::NotRepresentableInSource ? ac::Error::NotRepresentableInSource
                                                                                   : ac::Error::CookRefused;
}

std::optional<ac::Error> validateDocument(const scene::AuthoringScene& document, const AssetGuid& scene) {
  if (const auto refusal = checkSchema(document)) return refusal;
  return checkCook(document, scene);
}

}  // namespace atlantis::authoring
