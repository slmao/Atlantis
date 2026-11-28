#include <atlantis/authoring/authoring_session.h>

#include "document_access.h"
#include "prefab_edit.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/guid_mint.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/scene_semantic_schema.h>
#include <atlantis/authoring/document_validation.h>

#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <fstream>
#include <map>
#include <numbers>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace atlantis::authoring {

namespace {

namespace fs = std::filesystem;
namespace scene = atlantis::asset_system::scene;
namespace ac = atlantis::connection::authoring;
using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::AssetGuid;
using atlantis::connection::ConnectionError;
using atlantis::connection::SubscriptionId;
using atlantis::world::access::CommandTicket;
using atlantis::world::access::TransactionTicket;

struct Subscription {
  SubscriptionId id;
  ac::EventFilter filter;
  std::vector<ac::Event> queue;
};

// One connection's subscriptions and the failures of its own submissions.
struct ConnectionState {
  std::vector<Subscription> subscriptions;
  std::vector<ac::CommandFailure> failures;
};

[[nodiscard]] const fs::path& sourcePathOf(const Provenance& provenance) {
  return std::visit(
      [](const auto& p) -> const fs::path& {
        if constexpr (std::is_same_v<std::decay_t<decltype(p)>, HandAuthored>) {
          return p.sourcePath;
        } else {
          return p.generatedSourcePath;
        }
      },
      provenance);
}

[[nodiscard]] std::optional<std::size_t> findNode(const scene::AuthoringScene& document, const EntityGuid& node) {
  for (std::size_t i = 0; i < document.nodes.size(); ++i) {
    if (document.nodes[i].guid == node) return i;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ac::RenderLookBasis> renderLookBasis(const scene::AuthoringScene& document) {
  if (!document.activeCamera) return std::nullopt;
  std::map<EntityGuid, const scene::AuthoringNode*> nodes;
  ac::RenderLookBasis basis{*document.activeCamera, {}, {}};
  std::optional<EntityGuid> directional;
  for (const auto& node : document.nodes) {
    nodes.emplace(node.guid, &node);
    if (!node.light) continue;
    basis.lightRoster.push_back(node.guid);
    if (node.light->kind == scene::LightKind::Directional) directional = node.guid;
  }
  if (directional && std::ranges::any_of(document.nodes, [&](const auto& node) { return node.parent == directional; }))
    return std::nullopt;
  if (basis.lightRoster.size() > ac::kMaxRenderLookLights) return std::nullopt;
  std::ranges::sort(basis.lightRoster);
  const auto camera = nodes.find(basis.activeCamera);
  if (camera == nodes.end() || !camera->second->camera) return std::nullopt;

  std::set<EntityGuid> placementNodes;
  const auto includePath = [&](EntityGuid start) {
    auto current = std::optional{start};
    while (current && placementNodes.insert(*current).second) {
      const auto node = nodes.find(*current);
      if (node == nodes.end()) return false;
      current = node->second->parent;
    }
    return true;
  };
  if (!includePath(basis.activeCamera) || !std::ranges::all_of(basis.lightRoster, includePath))
    return std::nullopt;
  // Canonical GUID order and exact binary32 bits. Names and look values are
  // excluded. A directional leaf's rotation is itself a look value.
  auto& signature = basis.placementSignature;
  const auto appendFloat = [&](float value) {
    signature.push_back('/');
    signature.append(std::to_string(std::bit_cast<std::uint32_t>(value)));
  };
  for (const auto& id : placementNodes) {
    const auto& node = *nodes.at(id);
    signature.append(asset_system::toString(id)).push_back('/');
    if (node.parent) signature.append(asset_system::toString(*node.parent));
    signature.push_back('/');
    signature.push_back(id == basis.activeCamera ? 'C' : id == directional ? 'D' : node.light ? 'P' : 'T');
    for (float value : node.transform.localPosition) appendFloat(value);
    for (float value : node.transform.localScale) appendFloat(value);
    if (id != directional)
      for (float value : node.transform.localEulerAnglesRadians) appendFloat(value);
    if (id == basis.activeCamera) {
      appendFloat(node.camera->fovYRadians);
      appendFloat(node.camera->nearZ);
      appendFloat(node.camera->farZ);
    }
    signature.push_back('|');
    if (signature.size() > ac::kMaxRenderLookPlacementBytes) return std::nullopt;
  }
  return basis;
}

[[nodiscard]] bool isRenderLookAddress(const scene::AuthoringNode& node, const ac::RenderLookBasis& basis,
                                       const ac::PropertyAddress& address) {
  constexpr std::string_view camera = "asset_system::scene::Camera";
  constexpr std::string_view fog = "asset_system::scene::CameraFog";
  constexpr std::string_view bloom = "asset_system::scene::CameraBloom";
  constexpr std::string_view light = "asset_system::scene::Light";
  constexpr std::string_view transform = "asset_system::scene::Transform";
  if (address.node == basis.activeCamera) {
    constexpr std::array cameraLeaves{schema::fieldId(camera, "exposureCompensationEv"), schema::fieldId(fog, "color"),
        schema::fieldId(fog, "density"), schema::fieldId(fog, "height"), schema::fieldId(fog, "heightFalloff"),
        schema::fieldId(fog, "maxOpacity"), schema::fieldId(bloom, "strength"), schema::fieldId(bloom, "threshold")};
    return address.component == schema::typeId(camera) && std::ranges::find(cameraLeaves, address.field) != cameraLeaves.end();
  }
  if (!node.light) return false;
  if (address.component == schema::typeId(light)) {
    return address.field == schema::fieldId(light, "color") || address.field == schema::fieldId(light, "intensity") ||
           (node.light->kind == scene::LightKind::Point && address.field == schema::fieldId(light, "range"));
  }
  return node.light->kind == scene::LightKind::Directional && address.component == schema::typeId(transform) &&
         address.field == schema::fieldId(transform, "localEulerAnglesRadians");
}

// Atomic save (ADR-0129 D4): a temporary file in the target's directory,
// then a rename over it. A failure leaves the target untouched.
[[nodiscard]] bool writeTextAtomically(const fs::path& target, const std::string& text) {
  std::random_device device;
  const fs::path temporary =
      target.parent_path() / (target.filename().string() + ".tmp-" + std::to_string(device()) + std::to_string(device()));
  std::error_code ec;
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out.good()) {
      out.close();
      fs::remove(temporary, ec);
      return false;
    }
  }
  fs::rename(temporary, target, ec);
  if (ec) {
    fs::remove(temporary, ec);
    return false;
  }
  return true;
}

}  // namespace

// ----------------------------------------------------------------- locate

std::string_view toString(LocateError error) noexcept {
  switch (error) {
    case LocateError::NotASceneEntry: return "NotASceneEntry";
    case LocateError::UnsupportedRoot: return "UnsupportedRoot";
    case LocateError::ImportNotBuilt: return "ImportNotBuilt";
  }
  return "Unknown";
}

atlantis::Result<DocumentSource, LocateError> locateDocument(const asset_system::CatalogSourceEntry& entry,
                                                             const SourceRoots& roots) {
  using ResultT = atlantis::Result<DocumentSource, LocateError>;
  switch (entry.type) {
    case asset_system::CatalogAssetType::Scene:
      if (entry.root != asset_system::CatalogRoot::Assets) return ResultT::Err(LocateError::UnsupportedRoot);
      return ResultT::Ok(DocumentSource{entry.guid, HandAuthored{roots.assetsRoot / fs::path(entry.path)}});
    case asset_system::CatalogAssetType::GltfImport: {
      const auto import = std::find_if(roots.imports.begin(), roots.imports.end(),
                                       [&](const ImportedScene& s) { return s.importEntry == entry.guid; });
      if (import == roots.imports.end()) return ResultT::Err(LocateError::ImportNotBuilt);
      return ResultT::Ok(DocumentSource{import->scene, Imported{import->generatedSource}});
    }
    case asset_system::CatalogAssetType::Mesh:
    case asset_system::CatalogAssetType::Texture:
    case asset_system::CatalogAssetType::Material:
    case asset_system::CatalogAssetType::Environment:
      return ResultT::Err(LocateError::NotASceneEntry);
  }
  return ResultT::Err(LocateError::NotASceneEntry);
}

// ------------------------------------------------------------------ state

struct AuthoringSession::State {
  explicit State(DocumentSource s) : source(std::move(s)) {}

  DocumentSource source;
  std::optional<scene::AuthoringScene> document;
  SceneSink sink;
  SaveAsSink copySink;
  CreateSceneSink createSink;
  PrefabSink prefabSink;
  PrefabUpdateSink prefabUpdateSink;
  PrefabRead prefabRead;
  PrefabDigest prefabDigest;
  PrefabAssets prefabAssets;
  PrefabLookup prefabLookup;
  SceneEnvironmentLookup sceneEnvironmentLookup;
  RecoveryStore* recovery = nullptr;
  std::optional<std::string> recoveryBase;
  std::uint64_t documentRevision = 0;
  asset_system::EntityGuid documentId;
  std::uint64_t persistedRevision = 0;
  std::string persistedSource;
  mutable std::optional<bool> dirty;
  std::optional<ac::ApplyRecord> pendingApply;
  std::vector<ac::ApplyRecord> terminalApplies;
  std::optional<ac::ApplyProjection> projection;
  std::optional<ac::ApplyId> newestApply;

  void retain(ac::ApplyRecord record) {
    if (terminalApplies.size() == 64) terminalApplies.erase(terminalApplies.begin());
    terminalApplies.push_back(std::move(record));
  }
  Result<ac::ApplySnapshot, ac::ApplyLookupError> applyStatus(std::optional<ac::ApplyId> id) const {
    ac::ApplySnapshot view{document ? std::optional(documentRevision) : std::nullopt, std::nullopt, projection};
    const auto wanted = id ? id : newestApply;
    if (!wanted) return Result<ac::ApplySnapshot, ac::ApplyLookupError>::Ok(std::move(view));
    if (pendingApply && pendingApply->receipt.id == *wanted) view.request = pendingApply;
    else {
      for (const auto& record : terminalApplies) if (record.receipt.id == *wanted) { view.request = record; break; }
      if (!view.request && projection && projection->receipt.id == *wanted)
        view.request = ac::ApplyRecord{projection->receipt, ac::ApplyState::Applied, projection->generation, {}, {}};
    }
    if (!view.request) return Result<ac::ApplySnapshot, ac::ApplyLookupError>::Err(ac::ApplyLookupError::UnknownApply);
    return Result<ac::ApplySnapshot, ac::ApplyLookupError>::Ok(std::move(view));
  }
  void reportReload(ac::ApplyId id, Result<std::uint64_t, ac::ReloadError> result,
                    std::optional<ac::SceneEnvironmentApplyDetail> environmentDetail) {
    if (!pendingApply || pendingApply->receipt.id != id) return;
    auto record = std::move(*pendingApply); pendingApply.reset();
    if (result.isOk()) {
      record.state = ac::ApplyState::Applied; record.generation = result.value();
      projection = ac::ApplyProjection{record.receipt, result.value()};
    } else {
      record.state = ac::ApplyState::Failed;
      record.error = result.error();
      record.environmentDetail = environmentDetail;
    }
    retain(std::move(record));
    if (result.isOk()) publish({ac::Applied{result.value()}});
  }
  std::uint64_t nextTicket = 1;
  std::uint64_t nextSubscription = 1;
  std::vector<ConnectionState*> connections;  // the live ones

  void publish(const std::vector<ac::Event>& events) {
    for (const ac::Event& event : events) {
      for (ConnectionState* connection : connections) {
        for (Subscription& subscription : connection->subscriptions) {
          if (subscription.filter.matches(event)) subscription.queue.push_back(event);
        }
      }
    }
  }

  // One command against `candidate`, as the earlier commands left it; its
  // events appended to `events`.
  [[nodiscard]] std::optional<ac::Error> applyCommand(scene::AuthoringScene& candidate, const ac::Command& command,
                                                      std::vector<ac::Event>& events) {
    return std::visit([&](const auto& c) { return apply(candidate, c, events); }, command);
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::SetProperty& c, std::vector<ac::Event>& events) {
    const auto node = findNode(d, c.address.node);
    if (!node) return ac::Error::UnknownNode;
    const auto resolved = detail::resolveField(c.address.component, c.address.field);
    if (resolved.isErr()) return resolved.error();
    std::byte* component = detail::componentBytes(d.nodes[*node], c.address.component);
    if (component == nullptr) return ac::Error::ComponentMissing;
    if (const auto kind = detail::checkKind(resolved.value(), c.value); kind.isErr()) return kind.error();
    detail::writeField(component, c.address.component, c.address.field, resolved.value(), c.value);
    events.push_back(ac::PropertyChanged{c.address, c.value});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::CreateNode& c, std::vector<ac::Event>& events) {
    if (c.position > d.nodes.size()) return ac::Error::NodePositionOutOfRange;
    scene::AuthoringNode node;
    node.guid = c.node.has_value() ? *c.node : asset_system::mintEntityGuid();
    for (const schema::TypeId component : c.components) {
      if (!detail::isNodeComponent(component)) return ac::Error::UnknownComponentType;
      if (detail::hasComponent(node, component)) return ac::Error::ComponentAlreadyPresent;
      detail::addComponent(node, component);
    }
    const EntityGuid guid = node.guid;
    d.nodes.insert(d.nodes.begin() + static_cast<std::ptrdiff_t>(c.position), std::move(node));
    events.push_back(ac::NodeCreated{guid, c.position});
    for (const schema::TypeId component : c.components) events.push_back(ac::ComponentAdded{guid, component});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::DestroyNode& c, std::vector<ac::Event>& events) {
    const auto node = findNode(d, c.node);
    if (!node) return ac::Error::UnknownNode;
    d.nodes.erase(d.nodes.begin() + static_cast<std::ptrdiff_t>(*node));
    events.push_back(ac::NodeDestroyed{c.node});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::AddComponent& c, std::vector<ac::Event>& events) {
    const auto node = findNode(d, c.node);
    if (!node) return ac::Error::UnknownNode;
    if (!detail::isNodeComponent(c.component)) return ac::Error::UnknownComponentType;
    if (!detail::addComponent(d.nodes[*node], c.component)) return ac::Error::ComponentAlreadyPresent;
    events.push_back(ac::ComponentAdded{c.node, c.component});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::RemoveComponent& c,
                                 std::vector<ac::Event>& events) {
    const auto node = findNode(d, c.node);
    if (!node) return ac::Error::UnknownNode;
    if (!detail::isNodeComponent(c.component)) return ac::Error::UnknownComponentType;
    if (detail::isRequiredComponent(c.component)) return ac::Error::ComponentRequired;
    if (!detail::removeComponent(d.nodes[*node], c.component)) return ac::Error::ComponentMissing;
    events.push_back(ac::ComponentRemoved{c.node, c.component});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::SetParent& c, std::vector<ac::Event>& events) {
    const auto node = findNode(d, c.node);
    if (!node) return ac::Error::UnknownNode;
    d.nodes[*node].parent = c.parent;
    events.push_back(ac::ParentChanged{c.node, c.parent});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::SetSceneEnvironment& c,
                                 std::vector<ac::Event>&) {
    d.environment = c.choice.asset;
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::SetActiveCamera& c,
                                 std::vector<ac::Event>& events) {
    d.activeCamera = c.camera;
    events.push_back(ac::ActiveCameraChanged{c.camera});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::SetNodeName& c, std::vector<ac::Event>& events) {
    const auto node = findNode(d, c.node);
    if (!node) return ac::Error::UnknownNode;
    if (!scene::isValidNodeName(c.name)) return ac::Error::InvalidNodeName;
    d.nodes[*node].name = c.name;
    events.push_back(ac::NodeNameChanged{c.node, c.name});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::SwapAdjacentSiblings& c,
                                 std::vector<ac::Event>& events) {
    if (!std::holds_alternative<HandAuthored>(source.provenance))
      return ac::Error::SiblingOrderRequiresHandAuthored;
    if (c.expectedRevision != documentRevision) return ac::Error::SiblingOrderStaleRevision;
    const auto node = findNode(d, c.node);
    const auto sibling = findNode(d, c.sibling);
    if (!node || !sibling) return ac::Error::UnknownNode;
    if ((c.direction != ac::SiblingDirection::Up && c.direction != ac::SiblingDirection::Down) ||
        *node == *sibling || d.nodes[*node].parent != c.parent || d.nodes[*sibling].parent != c.parent ||
        (c.direction == ac::SiblingDirection::Up ? *sibling > *node : *sibling < *node))
      return ac::Error::InvalidSiblingSwap;
    const auto first = std::min(*node, *sibling);
    const auto last = std::max(*node, *sibling);
    for (std::size_t i = first + 1; i < last; ++i) {
      if (d.nodes[i].parent == c.parent) return ac::Error::InvalidSiblingSwap;
    }
    std::swap(d.nodes[*node], d.nodes[*sibling]);
    events.push_back(ac::NodeOrderChanged{c.node, c.sibling, *sibling, *node, documentRevision + 1});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::MoveSiblingRelative& c,
                                 std::vector<ac::Event>& events) {
    if (!std::holds_alternative<HandAuthored>(source.provenance))
      return ac::Error::SiblingOrderRequiresHandAuthored;
    if (c.expectedRevision != documentRevision) return ac::Error::SiblingOrderStaleRevision;
    const auto node = findNode(d, c.node);
    const auto anchor = findNode(d, c.anchor);
    if (!node || !anchor) return ac::Error::UnknownNode;
    if ((c.placement != ac::SiblingPlacement::Before && c.placement != ac::SiblingPlacement::After) ||
        *node == *anchor || d.nodes[*node].parent != c.parent || d.nodes[*anchor].parent != c.parent)
      return ac::Error::InvalidSiblingMove;
    std::vector<std::size_t> slots;
    for (std::size_t i = std::min(*node, *anchor); i <= std::max(*node, *anchor); ++i) {
      if (d.nodes[i].parent == c.parent) slots.push_back(i);
    }
    const bool towardEnd = *node < *anchor;
    const bool after = c.placement == ac::SiblingPlacement::After;
    const std::size_t from = towardEnd ? 0 : slots.size() - 1;
    const std::size_t to = towardEnd ? slots.size() - (after ? 1 : 2) : (after ? 1 : 0);
    if (from == to) return ac::Error::InvalidSiblingMove;
    for (std::size_t current = from; current != to;) {
      const std::size_t next = towardEnd ? current + 1 : current - 1;
      std::swap(d.nodes[slots[current]], d.nodes[slots[next]]);
      current = next;
    }
    events.push_back(ac::NodeMovedInSiblingOrder{c.node, c.parent, *node, slots[to], documentRevision + 1});
    return std::nullopt;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene&, const ac::RequireDocumentRevision&,
                                 std::vector<ac::Event>&) const {
    ATLANTIS_CHECK_MSG(false, "the transaction admits revision guards before candidate execution");
    return ac::Error::InvalidGuardedPropertyBatch;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene&, const ac::RequireTransformBatchRevision&,
                                 std::vector<ac::Event>&) const {
    ATLANTIS_CHECK_MSG(false, "the transaction admits revision guards before candidate execution");
    return ac::Error::InvalidGuardedPropertyBatch;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene&, const ac::RequireDuplicateBatchRevision&,
                                 std::vector<ac::Event>&) const {
    ATLANTIS_CHECK_MSG(false, "the transaction admits revision guards before candidate execution");
    return ac::Error::InvalidGuardedDuplicateBatch;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene&, const ac::RequireNameBatchDocument&,
                                 std::vector<ac::Event>&) const {
    ATLANTIS_CHECK_MSG(false, "the transaction admits name guards before candidate execution");
    return ac::Error::InvalidGuardedNameBatch;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene&, const ac::RequireRenderLookDocument&,
                                 std::vector<ac::Event>&) const {
    ATLANTIS_CHECK_MSG(false, "the transaction admits render-look guards before candidate execution");
    return ac::Error::InvalidGuardedRenderLook;
  }

  [[nodiscard]] std::optional<ac::Error> checkRenderLook(const std::vector<ac::Command>& commands,
                                                        const ac::RequireRenderLookDocument& guard) const {
    if (commands.size() < 2 || commands.size() > ac::kMaxRenderLookWrites + 1 ||
        !std::holds_alternative<ac::RequireRenderLookDocument>(commands.front()) ||
        !std::holds_alternative<HandAuthored>(source.provenance))
      return ac::Error::InvalidGuardedRenderLook;
    if (guard.expectedDocument != ac::DocumentToken{documentId, documentRevision})
      return ac::Error::RenderLookStaleDocument;
    const auto basis = renderLookBasis(*document);
    if (!basis || *basis != guard.basis) return ac::Error::RenderLookTargetChanged;
    std::set<ac::PropertyAddress> addresses;
    for (const auto& command : std::span(commands).subspan(1)) {
      const auto* write = std::get_if<ac::SetProperty>(&command);
      if (!write || !addresses.insert(write->address).second) return ac::Error::InvalidGuardedRenderLook;
      const auto& address = write->address;
      const auto node = findNode(*document, address.node);
      if (!node || !isRenderLookAddress(document->nodes[*node], *basis, address))
        return ac::Error::InvalidGuardedRenderLook;
      const auto field = detail::resolveField(address.component, address.field);
      if (field.isErr()) return ac::Error::InvalidGuardedRenderLook;
      const auto* component = detail::componentBytes(document->nodes[*node], address.component);
      if (!component || detail::readField(component, address.component, address.field, field.value()) == write->value)
        return ac::Error::InvalidGuardedRenderLook;
    }
    return std::nullopt;
  }

  static bool validGuardedNameBatch(const std::vector<ac::Command>& commands) {
    if (commands.size() < 2 || commands.size() > ac::kMaxGuardedNameWrites + 1) return false;
    std::set<EntityGuid> nodes;
    return std::all_of(commands.begin() + 1, commands.end(), [&](const auto& command) {
      const auto* name = std::get_if<ac::SetNodeName>(&command);
      return name && name->node != EntityGuid{} && nodes.insert(name->node).second;
    });
  }

  struct DuplicateNode {
    EntityGuid id;
    std::uint64_t position = 0;
    bool named = false;
    bool parented = false;
    bool linked = false;
    std::vector<ac::PropertyAddress> addresses{};
  };

  // Check only the write shape here. Slots, GUID availability and suffix membership depend on the guarded revision.
  static std::optional<std::vector<DuplicateNode>> duplicateShape(const std::vector<ac::Command>& commands) {
    if (commands.size() < 2 || !std::holds_alternative<ac::RequireDuplicateBatchRevision>(commands.front()))
      return std::nullopt;
    const bool removing = std::holds_alternative<ac::DestroyNode>(commands[1]);
    const auto optionalComponent = [](schema::TypeId type) {
      return type == schema::typeId("asset_system::scene::Camera") ||
             type == schema::typeId("asset_system::scene::Renderable") ||
             type == schema::typeId("asset_system::scene::Light");
    };
    constexpr auto transform = schema::typeId("asset_system::scene::Transform");
    std::vector<DuplicateNode> nodes;
    const auto find = [&](EntityGuid id) {
      return std::ranges::find(nodes, id, &DuplicateNode::id);
    };
    for (std::size_t i = 1; i < commands.size(); ++i) {
      const auto& command = commands[i];
      if (removing) {
        const auto* destroy = std::get_if<ac::DestroyNode>(&command);
        if (!destroy || destroy->node == EntityGuid{} || nodes.size() == ac::kMaxGuardedDuplicateNodes ||
            find(destroy->node) != nodes.end()) return std::nullopt;
        nodes.push_back(DuplicateNode{.id = destroy->node});
      } else if (const auto* create = std::get_if<ac::CreateNode>(&command)) {
        if (!create->node || *create->node == EntityGuid{} || nodes.size() == ac::kMaxGuardedDuplicateNodes ||
            find(*create->node) != nodes.end()) return std::nullopt;
        auto components = create->components;
        std::ranges::sort(components);
        if (std::ranges::adjacent_find(components) != components.end() ||
            std::ranges::any_of(components, [&](auto type) { return !optionalComponent(type); })) return std::nullopt;
        nodes.push_back(DuplicateNode{.id = *create->node, .position = create->position});
      } else if (const auto* write = std::get_if<ac::SetProperty>(&command)) {
        const auto node = find(write->address.node);
        const auto component = write->address.component;
        if (node == nodes.end() ||
            (component != transform && !optionalComponent(component)))
          return std::nullopt;
        const auto field = detail::resolveField(component, write->address.field);
        if (field.isErr() || !schema::hasFlags(field.value().flags, schema::FieldFlags::Serializable) ||
            schema::hasFlags(field.value().flags, schema::FieldFlags::EntityReference) ||
            field.value().primitive == schema::PrimitiveKind::EntityGuid ||
            std::ranges::find(node->addresses, write->address) != node->addresses.end()) return std::nullopt;
        node->addresses.push_back(write->address);
      } else if (const auto* name = std::get_if<ac::SetNodeName>(&command)) {
        const auto node = find(name->node);
        if (node == nodes.end() || std::exchange(node->named, true)) return std::nullopt;
      } else if (const auto* origin = std::get_if<ac::SetPrefabOrigin>(&command)) {
        const auto node = find(origin->node);
        if (node == nodes.end() || origin->prefab == AssetGuid{} || std::exchange(node->linked, true))
          return std::nullopt;
      } else if (const auto* parent = std::get_if<ac::SetParent>(&command)) {
        const auto node = find(parent->node);
        if (node == nodes.end() || std::exchange(node->parented, true)) return std::nullopt;
      } else {
        return std::nullopt;
      }
    }
    if (nodes.empty()) return std::nullopt;
    return nodes;
  }

  std::optional<ac::Error> apply(scene::AuthoringScene& candidate, const ac::SetPrefabOrigin& origin,
                                 std::vector<ac::Event>& events) const {
    const auto index = findNode(candidate, origin.node);
    if (!index || !prefabLookup || !prefabLookup(origin.prefab))
      return ac::Error::InvalidGuardedDuplicateBatch;
    for (auto& event : events) {
      auto* created = std::get_if<ac::NodeCreated>(&event);
      if (!created || created->node != origin.node) continue;
      candidate.nodes[*index].prefabOrigin = origin.prefab;
      created->prefabOrigin = origin.prefab;
      return std::nullopt;
    }
    return ac::Error::InvalidGuardedDuplicateBatch;
  }

  bool matchesDuplicateBoundary(const std::vector<DuplicateNode>& nodes, bool removing) const {
    const auto count = document->nodes.size();
    if (removing) {
      if (nodes.size() > count) return false;
      const auto suffix = std::span(document->nodes).last(nodes.size());
      return std::ranges::all_of(nodes, [&](const DuplicateNode& node) {
        return std::ranges::find(suffix, node.id, &scene::AuthoringNode::guid) != suffix.end();
      });
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (nodes[i].position != count + i || findNode(*document, nodes[i].id)) return false;
    }
    return true;
  }

  static bool validGuardedTransformBatch(const std::vector<ac::Command>& commands) {
    if (commands.size() < 2 || commands.size() > 1 + ac::kMaxGuardedTransformWrites ||
        !std::holds_alternative<ac::RequireTransformBatchRevision>(commands.front())) return false;
    constexpr std::string_view transformName = "asset_system::scene::Transform";
    const auto transform = schema::typeId(transformName);
    const auto position = schema::fieldId(transformName, "localPosition");
    const auto euler = schema::fieldId(transformName, "localEulerAnglesRadians");
    const auto scale = schema::fieldId(transformName, "localScale");
    std::vector<EntityGuid> nodes;
    std::vector<std::pair<EntityGuid, schema::FieldId>> addresses;
    for (std::size_t i = 1; i < commands.size(); ++i) {
      const auto* write = std::get_if<ac::SetProperty>(&commands[i]);
      if (!write || write->address.component != transform) return false;
      const auto field = write->address.field;
      if (field != position && field != euler && field != scale) return false;
      const std::pair address{write->address.node, field};
      if (std::ranges::find(addresses, address) != addresses.end()) return false;
      addresses.push_back(address);
      if (std::ranges::find(nodes, address.first) == nodes.end()) {
        if (nodes.size() == ac::kMaxGuardedTransformNodes) return false;
        nodes.push_back(address.first);
      }
    }
    return true;
  }

  static bool validGuardedPropertyBatch(const std::vector<ac::Command>& commands) {
    if (commands.size() < 2 || commands.size() > 1 + ac::kMaxGuardedPropertyWrites ||
        !std::holds_alternative<ac::RequireDocumentRevision>(commands.front())) return false;
    const auto* initial = std::get_if<ac::SetProperty>(&commands[1]);
    if (!initial) return false;
    std::vector<ac::EntityGuid> nodes;
    nodes.reserve(commands.size() - 1);
    for (std::size_t i = 1; i < commands.size(); ++i) {
      const auto* write = std::get_if<ac::SetProperty>(&commands[i]);
      if (!write || write->address.component != initial->address.component ||
          write->address.field != initial->address.field ||
          std::ranges::find(nodes, write->address.node) != nodes.end()) return false;
      nodes.push_back(write->address.node);
    }
    return true;
  }

  // A transaction (one command or several): applied to a copy, validated, and
  // swapped in, or refused whole.
  [[nodiscard]] std::optional<ac::CommandFailure> transact(const std::vector<ac::Command>& commands,
                                                           CommandTicket first) {
    if (!document.has_value()) return ac::CommandFailure{first, ac::Error::NoDocument};
    for (std::size_t i = 0; i < commands.size(); ++i) {
      const auto* environment = std::get_if<ac::SetSceneEnvironment>(&commands[i]);
      if (!environment) continue;
      const CommandTicket ticket{first.value + i};
      if (commands.size() != 1 || !sceneEnvironmentLookup ||
          !std::holds_alternative<HandAuthored>(source.provenance))
        return ac::CommandFailure{ticket, ac::Error::InvalidSceneEnvironment};
      if (environment->expectedDocument != ac::DocumentToken{documentId, documentRevision})
        return ac::CommandFailure{ticket, ac::Error::SceneEnvironmentStaleDocument};
      if (environment->choice.asset &&
          (*environment->choice.asset == AssetGuid{} || !sceneEnvironmentLookup(*environment->choice.asset)))
        return ac::CommandFailure{ticket, ac::Error::SceneEnvironmentUnavailable};
      if (environment->choice.asset == document->environment) return std::nullopt;
    }
    const auto lookGuard = std::ranges::find_if(commands, [](const auto& command) {
      return std::holds_alternative<ac::RequireRenderLookDocument>(command);
    });
    const auto nameGuard = std::ranges::find_if(commands, [](const auto& command) {
      return std::holds_alternative<ac::RequireNameBatchDocument>(command);
    });
    auto guard = lookGuard;
    if (guard == commands.end()) guard = nameGuard;
    if (guard == commands.end()) {
      guard = std::ranges::find_if(commands, [](const ac::Command& command) {
        return std::holds_alternative<ac::RequireDocumentRevision>(command) ||
               std::holds_alternative<ac::RequireTransformBatchRevision>(command) ||
               std::holds_alternative<ac::RequireDuplicateBatchRevision>(command);
      });
    }
    const auto* duplicate = guard == commands.end() ? nullptr : std::get_if<ac::RequireDuplicateBatchRevision>(&*guard);
    if (lookGuard == commands.end() && nameGuard == commands.end() && !duplicate && std::ranges::any_of(commands, [](const auto& command) {
          return std::holds_alternative<ac::SetPrefabOrigin>(command);
        })) return ac::CommandFailure{first, ac::Error::InvalidGuardedDuplicateBatch};
    if (lookGuard != commands.end()) {
      if (const auto refusal = checkRenderLook(commands, std::get<ac::RequireRenderLookDocument>(*lookGuard)))
        return ac::CommandFailure{CommandTicket{first.value + static_cast<std::uint64_t>(lookGuard - commands.begin())}, *refusal};
    } else if (nameGuard != commands.end()) {
      const auto ticket = CommandTicket{first.value + static_cast<std::uint64_t>(nameGuard - commands.begin())};
      if (nameGuard != commands.begin() || !std::holds_alternative<HandAuthored>(source.provenance) ||
          !validGuardedNameBatch(commands))
        return ac::CommandFailure{ticket, ac::Error::InvalidGuardedNameBatch};
      const auto& expected = std::get<ac::RequireNameBatchDocument>(*nameGuard).expectedDocument;
      if (expected != ac::DocumentToken{documentId, documentRevision})
        return ac::CommandFailure{ticket, ac::Error::NameBatchStaleDocument};
    } else if (duplicate) {
      const auto nodes = duplicateShape(commands);
      const auto ticket = CommandTicket{first.value + static_cast<std::uint64_t>(guard - commands.begin())};
      if (!nodes || !std::holds_alternative<HandAuthored>(source.provenance))
        return ac::CommandFailure{ticket, ac::Error::InvalidGuardedDuplicateBatch};
      if (duplicate->expectedRevision != documentRevision)
        return ac::CommandFailure{ticket, ac::Error::DuplicateBatchStaleRevision};
      if (!matchesDuplicateBoundary(*nodes, std::holds_alternative<ac::DestroyNode>(commands[1])))
        return ac::CommandFailure{ticket, ac::Error::InvalidGuardedDuplicateBatch};
    } else if (guard != commands.end()) {
      const auto* transform = std::get_if<ac::RequireTransformBatchRevision>(&*guard);
      if (!(transform ? validGuardedTransformBatch(commands) : validGuardedPropertyBatch(commands))) {
        return ac::CommandFailure{CommandTicket{first.value + static_cast<std::uint64_t>(guard - commands.begin())},
                                  ac::Error::InvalidGuardedPropertyBatch};
      }
      const auto expected = transform ? transform->expectedRevision : std::get<ac::RequireDocumentRevision>(*guard).expectedRevision;
      if (expected != documentRevision) return ac::CommandFailure{first, ac::Error::PropertyBatchStaleRevision};
    }
    if (commands.size() != 1) {
      for (std::size_t i = 0; i < commands.size(); ++i) {
        if (std::holds_alternative<ac::SwapAdjacentSiblings>(commands[i]))
          return ac::CommandFailure{CommandTicket{first.value + i}, ac::Error::InvalidSiblingSwap};
        if (std::holds_alternative<ac::MoveSiblingRelative>(commands[i]))
          return ac::CommandFailure{CommandTicket{first.value + i}, ac::Error::InvalidSiblingMove};
      }
    }
    scene::AuthoringScene candidate = *document;
    std::vector<ac::Event> events;
    for (std::size_t i = guard != commands.end() ? 1 : 0; i < commands.size(); ++i) {
      if (const auto refusal = applyCommand(candidate, commands[i], events)) {
        return ac::CommandFailure{CommandTicket{first.value + i}, *refusal};
      }
    }
    if (duplicate) {
      for (std::size_t i = 1; i < commands.size(); ++i) {
        const auto* origin = std::get_if<ac::SetPrefabOrigin>(&commands[i]);
        if (origin && candidate.nodes[*findNode(candidate, origin->node)].parent)
          return ac::CommandFailure{CommandTicket{first.value + i}, ac::Error::InvalidGuardedDuplicateBatch};
      }
    }
    if (const auto refusal = validateDocument(candidate, source.scene)) return ac::CommandFailure{first, *refusal};
    document = std::move(candidate);
    ++documentRevision;
    dirty.reset();
    captureRecovery();
    publish(events);
    return std::nullopt;
  }

  ac::PrefabEditResult commitPrefab(const ac::DocumentToken& expected, const EntityGuid& root,
      scene::AuthoringScene candidate, ac::PrefabSubtree before, ac::PrefabSubtree after) {
    using R = ac::PrefabEditResult;
    using E = ac::PrefabEditError;
    if (!document || documentState()->token != expected) return R::Err(E::StaleDocument);
    const auto oldCompiled = asset_system::cookSceneToMemory(*document, source.scene);
    const auto newCompiled = asset_system::cookSceneToMemory(candidate, source.scene);
    if (oldCompiled.isErr() || newCompiled.isErr() || validateDocument(candidate, source.scene))
      return R::Err(E::InvalidInstance);
    const bool affectsProjection = oldCompiled.value().artifactBytes != newCompiled.value().artifactBytes;
    std::vector<ac::Event> events;
    if (const auto existing = findNode(*document, root); existing) {
      if (const auto replacement = findNode(candidate, root); replacement) {
        const auto& oldMesh = document->nodes[*existing].renderable;
        const auto& newMesh = candidate.nodes[*replacement].renderable;
        if (oldMesh != newMesh) {
          const auto component = schema::typeId("asset_system::scene::Renderable");
          if (oldMesh) events.emplace_back(ac::ComponentRemoved{root, component});
          if (newMesh) events.emplace_back(ac::ComponentAdded{root, component});
        }
      }
    }
    for (const auto slot : before.slots) {
      const auto& old = document->nodes[static_cast<std::size_t>(slot)];
      if (!findNode(candidate, old.guid)) events.emplace_back(ac::NodeDestroyed{old.guid});
    }
    for (const auto slot : after.slots) {
      const auto& node = candidate.nodes[static_cast<std::size_t>(slot)];
      if (findNode(*document, node.guid)) continue;
      events.emplace_back(ac::NodeCreated{node.guid, slot, node.prefabOrigin});
      if (node.renderable)
        events.emplace_back(ac::ComponentAdded{node.guid, schema::typeId("asset_system::scene::Renderable")});
      if (node.parent) events.emplace_back(ac::ParentChanged{node.guid, node.parent});
      if (!node.name.empty()) events.emplace_back(ac::NodeNameChanged{node.guid, node.name});
    }
    document = std::move(candidate);
    ++documentRevision;
    dirty.reset();
    captureRecovery();
    publish(events);
    return R::Ok({documentState()->token, std::move(before), std::move(after), true, affectsProjection});
  }

  [[nodiscard]] atlantis::Result<std::monostate, ac::OpenError> open() {
    using ResultT = atlantis::Result<std::monostate, ac::OpenError>;
    std::ifstream file(sourcePathOf(source.provenance), std::ios::binary);
    if (!file.is_open()) return ResultT::Err(ac::OpenError::SourceUnreadable);
    std::ostringstream text;
    text << file.rdbuf();
    if (file.bad()) return ResultT::Err(ac::OpenError::SourceUnreadable);
    const auto raw = std::move(text).str();
    const auto parsed = asset_system::parseSceneSource(raw);
    if (parsed.isErr()) return ResultT::Err(ac::OpenError::SourceParseFailed);
    auto mapped = scene::toAuthoringScene(parsed.value());
    if (mapped.isErr()) return ResultT::Err(ac::OpenError::SourceInvalid);
    if (validateDocument(mapped.value(), source.scene).has_value()) return ResultT::Err(ac::OpenError::SourceInvalid);
    document = std::move(mapped.value());
    ++documentRevision;
    documentId = asset_system::mintEntityGuid();
    persistedRevision = documentRevision;
    persistedSource = canonicalSource();
    dirty = false;
    if (recovery) {
      recovery->opened();
      recoveryBase = recovery->fingerprint(raw);
    }
    publish({ac::DocumentReopened{source.scene}});
    return ResultT::Ok({});
  }

  // ADR-0131 D2, D3: the document cooked in memory exactly as the build cooks
  // its saved text, decoded, and handed to the host's sink.
  [[nodiscard]] Result<ac::ApplyReceipt, ac::ApplyError> applyTracked() {
    using ResultT = Result<ac::ApplyReceipt, ac::ApplyError>;
    if (!document.has_value()) return ResultT::Err(ac::ApplyError::NoDocument);
    if (!sink) return ResultT::Err(ac::ApplyError::NoSceneSink);
    const auto cooked = asset_system::cookSceneToMemory(*document, source.scene);
    if (cooked.isErr()) return ResultT::Err(ac::ApplyError::CookFailed);
    auto decoded = asset_system::decodeScene(cooked.value());
    if (decoded.isErr()) return ResultT::Err(ac::ApplyError::DecodeFailed);
    const ac::ApplyReceipt receipt{ac::ApplyId{asset_system::mintEntityGuid().bytes}, documentRevision};
    const auto accepted = sink(receipt.id, std::move(decoded.value()));
    if (accepted.isErr()) return ResultT::Err(ac::ApplyError::ReloadRefused);
    // No query or frame can interleave this acceptance-time metadata commit.
    if (pendingApply) {
      ATLANTIS_CHECK_MSG(accepted.value() == pendingApply->receipt.id, "sink must name the displaced pending apply");
      auto old = std::move(*pendingApply);
      old.state = ac::ApplyState::Superseded; old.supersededBy = receipt.id;
      retain(std::move(old));
    } else ATLANTIS_CHECK_MSG(!accepted.value(), "sink displaced an unknown apply");
    pendingApply = ac::ApplyRecord{receipt, ac::ApplyState::Pending, {}, {}, {}};
    newestApply = receipt.id;
    return ResultT::Ok(receipt);
  }

  [[nodiscard]] std::string canonicalSource() const {
    const auto parsed = scene::toParsedSceneSource(*document);
    ATLANTIS_CHECK_MSG(parsed.isOk(), "a valid document failed to map to its source form");
    return asset_system::serializeSceneSource(parsed.value());
  }

  [[nodiscard]] std::optional<ac::DocumentState> documentState(bool withEnvironment = false) const {
    if (!document) return std::nullopt;
    if (!dirty) dirty = canonicalSource() != persistedSource;
    return ac::DocumentState{{documentId, documentRevision}, source.scene,
        sourcePathOf(source.provenance).generic_string(),
        std::holds_alternative<HandAuthored>(source.provenance) ? ac::Provenance::HandAuthored : ac::Provenance::Imported,
        persistedRevision, *dirty, withEnvironment && sceneEnvironmentLookup
            ? std::optional{ac::SceneEnvironmentChoice{document->environment}} : std::nullopt};
  }

  void captureRecovery() {
    if (!recovery || !recoveryBase) return;
    const auto status = recovery->status();
    if (status.capture == ac::RecoveryCapture::Unavailable || (status.copy && status.copy->pending)) return;
    auto canonical = canonicalSource();
    dirty = canonical != persistedSource;
    if (*dirty) recovery->capture(documentRevision, *recoveryBase, std::move(canonical));
    else recovery->retire();
  }

  [[nodiscard]] std::optional<scene::AuthoringScene> decodeRecovery(std::string_view text) const {
    const auto parsed = asset_system::parseSceneSource(text);
    if (parsed.isErr()) return std::nullopt;
    auto candidate = scene::toAuthoringScene(parsed.value());
    if (candidate.isErr() || validateDocument(candidate.value(), source.scene)) return std::nullopt;
    return std::move(candidate.value());
  }

  static void recoveryLines(std::string_view text, std::vector<std::string>& lines, bool& truncated) {
    for (std::size_t row = 0; !text.empty() && row < ac::kRecoveryPreviewLines; ++row) {
      const auto newline = text.find('\n');
      const auto line = text.substr(0, newline);
      std::size_t scalars = 0, bytes = 0;
      for (; bytes < line.size(); ++bytes) {
        const auto byte = static_cast<unsigned char>(line[bytes]);
        if ((byte & 0xc0) != 0x80 && ++scalars > ac::kRecoveryPreviewScalars) break;
      }
      lines.emplace_back(line.substr(0, bytes));
      truncated |= bytes != line.size();
      text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
    }
    truncated |= !text.empty();
  }

  [[nodiscard]] Result<ac::RecoveryStatus, ac::RecoveryFailure> recoveryStatus(
      std::optional<EntityGuid> id) const {
    using R = Result<ac::RecoveryStatus, ac::RecoveryFailure>;
    using E = ac::RecoveryError;
    using S = ac::RecoverySource;
    if (!recovery) return R::Err({E::Unsupported});
    if (!document) return R::Err({E::NoDocument});
    auto status = recovery->status();
    if (!recoveryBase) status.capture = ac::RecoveryCapture::Unavailable;
    if (!id) return R::Ok(std::move(status));
    const auto copy = recovery->pending(*id);
    if (copy.isErr()) return R::Err({copy.error()});
    const auto loaded = decodeRecovery(copy.value().payload);
    if (!loaded) return R::Err({E::InvalidCopy});
    ac::RecoveryPreview preview{S::Unreadable, loaded->nodes.size(), {}, {}, {}, false};
    if (const auto raw = recovery->readSource()) {
      if (const auto official = decodeRecovery(*raw)) {
        preview.sourceNodes = official->nodes.size();
        if (*raw == copy.value().payload) preview.source = S::AlreadySaved;
        else if (recovery->fingerprint(*raw) != copy.value().baseDigest || recoveryBase != copy.value().baseDigest)
          preview.source = S::Changed;
        else preview.source = documentState()->dirty ? S::DirtyDocument : S::Matches;
        const auto canonical = asset_system::serializeSceneSource(scene::toParsedSceneSource(*official).value());
        std::string_view before = canonical, after = copy.value().payload;
        while (!before.empty() && !after.empty()) {
          const auto a = before.find('\n'), b = after.find('\n');
          if (before.substr(0, a) != after.substr(0, b) || a == std::string_view::npos || b == std::string_view::npos)
            break;
          before.remove_prefix(a + 1);
          after.remove_prefix(b + 1);
        }
        if (before != after) {
          recoveryLines(before, preview.sourceLines, preview.truncated);
          recoveryLines(after, preview.copyLines, preview.truncated);
        }
      }
    }
    if (!preview.sourceNodes) recoveryLines(copy.value().payload, preview.copyLines, preview.truncated);
    status.preview = std::move(preview);
    return R::Ok(std::move(status));
  }

  [[nodiscard]] Result<ac::RecoveryReceipt, ac::RecoveryFailure> resolveRecovery(
      const ac::DocumentToken& expected, const EntityGuid& id, bool restore) {
    using R = Result<ac::RecoveryReceipt, ac::RecoveryFailure>;
    using E = ac::RecoveryError;
    if (!recovery) return R::Err({E::Unsupported});
    const auto current = documentState();
    if (!current) return R::Err({E::NoDocument});
    if (expected != current->token) return R::Err({E::StaleDocument});
    if (!restore) {
      if (const auto error = recovery->discard(id)) return R::Err({*error});
      captureRecovery();  // If the retained live document is dirty, this is a new copy.
      return R::Ok({id, *current});
    }
    if (current->dirty) return R::Err({E::DirtyDocument});
    const auto copy = recovery->pending(id);
    if (copy.isErr()) return R::Err({copy.error()});
    auto candidate = decodeRecovery(copy.value().payload);
    if (!candidate || copy.value().payload == persistedSource) return R::Err({E::InvalidCopy});
    const auto raw = recovery->readSource();
    if (!raw) return R::Err({E::SourceUnreadable});
    if (recoveryBase != copy.value().baseDigest || recovery->fingerprint(*raw) != copy.value().baseDigest)
      return R::Err({E::SourceChanged});
    document = std::move(*candidate);
    ++documentRevision;
    documentId = asset_system::mintEntityGuid();
    dirty = true;
    recovery->adopted(documentRevision);
    publish({ac::DocumentReopened{source.scene}});
    return R::Ok({id, *documentState()});
  }

  [[nodiscard]] atlantis::Result<std::monostate, ac::SaveError> save() {
    using ResultT = atlantis::Result<std::monostate, ac::SaveError>;
    if (!document.has_value()) return ResultT::Err(ac::SaveError::NoDocument);
    const auto* handAuthored = std::get_if<HandAuthored>(&source.provenance);
    if (handAuthored == nullptr) return ResultT::Err(ac::SaveError::ImportedSceneNotSavable);
    auto canonical = canonicalSource();
    if (!writeTextAtomically(handAuthored->sourcePath, canonical)) {
      return ResultT::Err(ac::SaveError::WriteFailed);
    }
    persistedRevision = documentRevision;
    if (recovery) {
      recoveryBase = recovery->fingerprint(canonical);
      recovery->retire();
    }
    persistedSource = std::move(canonical);
    dirty = false;
    return ResultT::Ok({});
  }
};

// ------------------------------------------------------------- connection

namespace {

class InProcessAuthoringConnection final : public connection::AuthoringConnection {
 public:
  explicit InProcessAuthoringConnection(AuthoringSession::State& state) : state_(state) {
    state_.connections.push_back(&own_);
  }
  ~InProcessAuthoringConnection() override { std::erase(state_.connections, &own_); }
  InProcessAuthoringConnection(const InProcessAuthoringConnection&) = delete;
  InProcessAuthoringConnection& operator=(const InProcessAuthoringConnection&) = delete;

  std::optional<ac::DocumentInfo> document() const override {
    if (!state_.document.has_value()) return std::nullopt;
    const bool imported = std::holds_alternative<Imported>(state_.source.provenance);
    return ac::DocumentInfo{state_.source.scene,
                            imported ? ac::Provenance::Imported : ac::Provenance::HandAuthored,
                            sourcePathOf(state_.source.provenance).generic_string(),
                            static_cast<std::uint64_t>(state_.document->nodes.size()), true, true, !imported,
                            !imported && static_cast<bool>(state_.prefabLookup),
                            !imported && static_cast<bool>(state_.prefabSink),
                            !imported && static_cast<bool>(state_.prefabRead), !imported, !imported};
  }

  atlantis::Result<std::vector<ac::NodeSummary>, ac::Error> listNodes() const override {
    using ResultT = atlantis::Result<std::vector<ac::NodeSummary>, ac::Error>;
    if (!state_.document.has_value()) return ResultT::Err(ac::Error::NoDocument);
    std::vector<ac::NodeSummary> out;
    out.reserve(state_.document->nodes.size());
    for (const auto& node : state_.document->nodes) out.push_back({node.guid, node.parent, node.name, node.prefabOrigin, node.prefabBaseline});
    return ResultT::Ok(std::move(out));
  }

  atlantis::Result<std::vector<schema::TypeId>, ac::Error> listComponents(const EntityGuid& node) const override {
    using ResultT = atlantis::Result<std::vector<schema::TypeId>, ac::Error>;
    if (!state_.document.has_value()) return ResultT::Err(ac::Error::NoDocument);
    const auto index = findNode(*state_.document, node);
    if (!index) return ResultT::Err(ac::Error::UnknownNode);
    return ResultT::Ok(detail::componentsOf(state_.document->nodes[*index]));
  }

  atlantis::Result<ac::PropertyValue, ac::Error> getProperty(const ac::PropertyAddress& address) const override {
    using ResultT = atlantis::Result<ac::PropertyValue, ac::Error>;
    if (!state_.document.has_value()) return ResultT::Err(ac::Error::NoDocument);
    const auto index = findNode(*state_.document, address.node);
    if (!index) return ResultT::Err(ac::Error::UnknownNode);
    const auto resolved = detail::resolveField(address.component, address.field);
    if (resolved.isErr()) return ResultT::Err(resolved.error());
    const std::byte* component = detail::componentBytes(state_.document->nodes[*index], address.component);
    if (component == nullptr) return ResultT::Err(ac::Error::ComponentMissing);
    return ResultT::Ok(detail::readField(component, address.component, address.field, resolved.value()));
  }

  atlantis::Result<std::optional<EntityGuid>, ac::Error> activeCamera() const override {
    using ResultT = atlantis::Result<std::optional<EntityGuid>, ac::Error>;
    if (!state_.document.has_value()) return ResultT::Err(ac::Error::NoDocument);
    return ResultT::Ok(state_.document->activeCamera);
  }

  std::span<const schema::TypeDescriptor> schema() const override { return detail::authoringTypes(); }

  CommandTicket submit(ac::Command command) override {
    std::vector<ac::Command> commands;
    commands.push_back(std::move(command));
    return submitTransaction(std::move(commands)).first;
  }

  TransactionTicket submitTransaction(std::vector<ac::Command> commands) override {
    const TransactionTicket ticket{CommandTicket{state_.nextTicket}, static_cast<std::uint64_t>(commands.size())};
    state_.nextTicket += commands.size();
    if (commands.empty()) return ticket;
    if (const auto failure = state_.transact(commands, ticket.first)) own_.failures.push_back(*failure);
    return ticket;
  }

  SubscriptionId subscribe(ac::EventFilter filter) override {
    const SubscriptionId id{state_.nextSubscription++};
    own_.subscriptions.push_back({id, filter, {}});
    return id;
  }

  atlantis::Result<std::monostate, ConnectionError> unsubscribe(SubscriptionId subscription) override {
    using ResultT = atlantis::Result<std::monostate, ConnectionError>;
    const auto erased = std::erase_if(own_.subscriptions, [&](const Subscription& s) { return s.id == subscription; });
    if (erased == 0) return ResultT::Err(ConnectionError::UnknownSubscription);
    return ResultT::Ok({});
  }

  atlantis::Result<std::vector<ac::Event>, ConnectionError> drainEvents(SubscriptionId subscription) override {
    using ResultT = atlantis::Result<std::vector<ac::Event>, ConnectionError>;
    for (Subscription& s : own_.subscriptions) {
      if (s.id == subscription) return ResultT::Ok(std::exchange(s.queue, {}));
    }
    return ResultT::Err(ConnectionError::UnknownSubscription);
  }

  std::vector<ac::CommandFailure> drainFailures() override { return std::exchange(own_.failures, {}); }

  atlantis::Result<std::monostate, ac::OpenError> open() override { return state_.open(); }

  Result<std::monostate, ac::ApplyError> apply() override {
    auto result = state_.applyTracked();
    return result.isOk() ? Result<std::monostate, ac::ApplyError>::Ok({})
                         : Result<std::monostate, ac::ApplyError>::Err(result.error());
  }
  Result<ac::ApplyReceipt, ac::ApplyError> applyTracked() override { return state_.applyTracked(); }
  Result<ac::ApplySnapshot, ac::ApplyLookupError> applyStatus(std::optional<ac::ApplyId> id) const override {
    return state_.applyStatus(id);
  }

  atlantis::Result<std::monostate, ac::SaveError> save() override { return state_.save(); }
  Result<std::optional<ac::DocumentState>, ac::DocumentFailure> documentState() const override {
    return Result<std::optional<ac::DocumentState>, ac::DocumentFailure>::Ok(state_.documentState());
  }
  Result<std::optional<ac::DocumentState>, ac::DocumentFailure> documentStateWithEnvironment() const override {
    using R = Result<std::optional<ac::DocumentState>, ac::DocumentFailure>;
    if (!state_.sceneEnvironmentLookup) return R::Err({ac::DocumentError::Unsupported});
    return R::Ok(state_.documentState(true));
  }
  Result<ac::DocumentState, ac::DocumentFailure> saveIfCurrent(const ac::DocumentToken& expected) override {
    using R = Result<ac::DocumentState, ac::DocumentFailure>;
    using E = ac::DocumentError;
    if (!state_.document) return R::Err({E::NoDocument});
    if (expected != ac::DocumentToken{state_.documentId, state_.documentRevision}) return R::Err({E::StaleDocument});
    const auto result = state_.save();
    if (result.isErr()) {
      return R::Err({result.error() == ac::SaveError::ImportedSceneNotSavable ? E::ImportedSceneNotSavable : E::WriteFailed});
    }
    return R::Ok(*state_.documentState());
  }
  Result<ac::DocumentState, ac::DocumentFailure> reopenIfCurrent(std::optional<ac::DocumentToken> expected) override {
    using R = Result<ac::DocumentState, ac::DocumentFailure>;
    using E = ac::DocumentError;
    if (expected && !state_.document) return R::Err({E::NoDocument});
    if (state_.document && expected != ac::DocumentToken{state_.documentId, state_.documentRevision}) return R::Err({E::StaleDocument});
    const auto result = state_.open();
    if (result.isErr()) {
      switch (result.error()) {
        case ac::OpenError::SourceUnreadable: return R::Err({E::SourceUnreadable});
        case ac::OpenError::SourceParseFailed: return R::Err({E::SourceParseFailed});
        case ac::OpenError::SourceInvalid: return R::Err({E::SourceInvalid});
      }
    }
    return R::Ok(*state_.documentState());
  }
  Result<ac::RecoveryStatus, ac::RecoveryFailure> recoveryStatus(
      std::optional<EntityGuid> id) const override {
    return state_.recoveryStatus(id);
  }
  Result<ac::RecoveryReceipt, ac::RecoveryFailure> restoreRecovery(
      const ac::DocumentToken& token, const EntityGuid& id) override {
    return state_.resolveRecovery(token, id, true);
  }
  Result<ac::RecoveryReceipt, ac::RecoveryFailure> discardRecovery(
      const ac::DocumentToken& token, const EntityGuid& id) override {
    return state_.resolveRecovery(token, id, false);
  }
  Result<ac::SavedScene, ac::SaveAsError> saveAs(std::string_view name) override {
    using R = Result<ac::SavedScene, ac::SaveAsError>;
    if (!ac::validSaveAsName(name)) return R::Err(ac::SaveAsError::InvalidName);
    if (!state_.document) return R::Err(ac::SaveAsError::NoDocument);
    if (!std::holds_alternative<HandAuthored>(state_.source.provenance))
      return R::Err(ac::SaveAsError::ImportedSceneNotSavable);
    if (!state_.copySink) return R::Err(ac::SaveAsError::Unavailable);
    const auto parsed = scene::toParsedSceneSource(*state_.document);
    ATLANTIS_CHECK_MSG(parsed.isOk(), "saveAs(): a valid document failed to map to its source form");
    return state_.copySink(name, asset_system::serializeSceneSource(parsed.value()), state_.documentRevision);
  }

  Result<ac::CreatedScene, ac::CreateSceneError> createScene(std::string_view name) override {
    using E = ac::CreateSceneError;
    using R = Result<ac::CreatedScene, E>;
    if (!ac::validSaveAsName(name)) return R::Err(E::InvalidName);
    if (!state_.createSink) return R::Err(E::Unavailable);

    const auto sceneGuid = asset_system::mintAssetGuids(1).front();
    asset_system::ParsedSceneSource starter;
    auto& camera = starter.nodes.emplace_back();
    camera.nodeId = 1;
    camera.entityGuid = asset_system::mintEntityGuid();
    camera.name = "Camera";
    camera.transform.positionZ = 5.0f;
    camera.camera = asset_system::DecodedCamera{.fovYRadians = std::numbers::pi_v<float> / 3.0f,
                                               .nearZ = 0.1f, .farZ = 100.0f, .fog = {}, .bloom = {}};
    starter.activeCameraNodeId = camera.nodeId;
    auto source = asset_system::serializeSceneSource(starter);
    if (asset_system::cookSceneSource(source, sceneGuid).isErr()) return R::Err(E::WriteFailed);
    return state_.createSink(name, sceneGuid, std::move(source));
  }

  Result<ac::PrefabRegistration, ac::SaveAsError> createPrefab(
      std::string_view name, const EntityGuid& root, const ac::DocumentToken& expected) override {
    using E = ac::SaveAsError;
    using R = Result<ac::PrefabRegistration, E>;
    if (!state_.prefabSink) return R::Err(E::Unavailable);
    if (!ac::validSaveAsName(name)) return R::Err(E::InvalidName);
    const auto prefab = asset_system::mintAssetGuids(1).front();
    auto captured = capturePrefab(prefab, root, expected);
    if (captured.isErr()) return R::Err(captured.error());
    return state_.prefabSink(name, prefab, std::move(captured.value()), expected.documentRevision);
  }

  ac::PrefabEditResult syncPrefab(const EntityGuid& root, std::string_view revision,
      const ac::DocumentToken& expected, std::optional<AssetGuid> instantiate) override {
    using R = ac::PrefabEditResult;
    using E = ac::PrefabEditError;
    if (!state_.prefabRead) return R::Err(E::Unsupported);
    const auto current = state_.documentState();
    if (!current) return R::Err(E::NoDocument);
    if (current->token != expected) return R::Err(E::StaleDocument);
    if (current->provenance != ac::Provenance::HandAuthored || root == EntityGuid{})
      return R::Err(E::InvalidInstance);
    const auto& document = *state_.document;
    const auto index = findNode(document, root);
    if (instantiate.has_value() == index.has_value()) return R::Err(E::InvalidInstance);
    const auto origin = instantiate ? instantiate : document.nodes[*index].prefabOrigin;
    if (!origin || *origin == AssetGuid{}) return R::Err(E::InvalidInstance);
    const auto frozen = state_.prefabRead(*origin);
    if (!frozen || frozen->revision != revision) return R::Err(E::StaleSource);
    auto definition = prefab_edit::decode(frozen->source);
    if (!definition || definition->activeCamera) return R::Err(E::InvalidInstance);
    const auto templateRoot = std::ranges::find_if(definition->nodes, [](const auto& node) { return !node.parent; });
    if (templateRoot == definition->nodes.end()) return R::Err(E::InvalidInstance);

    auto before = prefab_edit::capture(document, root, state_.prefabDigest);
    if (!before) return R::Err(E::InvalidInstance);
    scene::AuthoringScene candidate = document;
    bool replace = true;
    if (index) {
      const auto old = prefab_edit::decode(before->source);
      if (!old) return R::Err(E::InvalidInstance);
      const auto& instance = document.nodes[*index];
      const auto managed = prefab_edit::digest(*old, root, *origin, state_.prefabDigest);
      const auto instanceShape = prefab_edit::normalized(*old, root, *origin, true);
      const auto templateShape = prefab_edit::normalized(*definition, templateRoot->guid, *origin, true);
      if (!managed || !instanceShape || !templateShape) return R::Err(E::InvalidInstance);
      const bool matches = *instanceShape == *templateShape;
      if (instance.prefabBaseline) {
        if (instance.prefabBaseline != managed) return R::Err(E::LocalConflict);
        if (matches) return R::Ok({expected, *before, *before, false, false});
      } else {
        if (!matches) return R::Err(E::BaselineUnavailable);
        candidate.nodes[*index].prefabBaseline = managed;
        replace = false;
      }
    }

    if (replace) {
      const auto oldRoot = index ? std::optional(document.nodes[*index]) : std::nullopt;
      const auto definitionRootGuid = templateRoot->guid;
      std::map<EntityGuid, EntityGuid> remap{{definitionRootGuid, root}};
      for (const auto& node : definition->nodes)
        if (node.guid != definitionRootGuid) remap.emplace(node.guid, asset_system::mintEntityGuid());

      for (auto& node : definition->nodes) {
        node.guid = remap.at(node.guid);
        if (node.parent) node.parent = remap.at(*node.parent);
      }
      if (oldRoot) {
        templateRoot->transform = oldRoot->transform;
        templateRoot->name = oldRoot->name;
      } else {
        templateRoot->name = frozen->name;
        const std::string base = templateRoot->name;
        std::set<std::string_view> rootNames;
        for (const auto& node : document.nodes)
          if (!node.parent) rootNames.emplace(node.name);
        for (std::size_t suffix = 2; rootNames.contains(templateRoot->name); ++suffix) {
          const auto tail = " " + std::to_string(suffix);
          auto length = std::min(base.size(), static_cast<std::size_t>(scene::kMaxNodeNameBytes) - tail.size());
          while (length < base.size() && (static_cast<unsigned char>(base[length]) & 0xc0) == 0x80) --length;
          templateRoot->name = base.substr(0, length) + tail;
        }
      }
      templateRoot->prefabOrigin = origin;
      if (oldRoot) std::rotate(definition->nodes.begin(), templateRoot, templateRoot + 1);
      auto& replacementRoot = oldRoot ? definition->nodes.front() : *templateRoot;
      const auto baseline = prefab_edit::digest(*definition, root, *origin, state_.prefabDigest);
      if (!baseline) return R::Err(E::InvalidInstance);
      replacementRoot.prefabBaseline = baseline;
      if (!oldRoot) {
        candidate.nodes.insert(candidate.nodes.end(), definition->nodes.begin(), definition->nodes.end());
      } else {
        replacementRoot.parent = oldRoot->parent;
        std::set<EntityGuid> removed;
        for (const auto slot : before->slots) {
          const auto& node = document.nodes[static_cast<std::size_t>(slot)];
          if (node.guid != root) removed.insert(node.guid);
        }
        std::erase_if(candidate.nodes, [&](const auto& node) { return removed.contains(node.guid); });
        auto insertion = candidate.nodes.begin() + *findNode(candidate, root);
        *insertion++ = replacementRoot;
        candidate.nodes.insert(insertion, definition->nodes.begin() + 1, definition->nodes.end());
      }
    }

    auto after = prefab_edit::capture(candidate, root, state_.prefabDigest);
    if (!after || !prefab_edit::assetsAvailable(after->source, state_.prefabAssets))
      return R::Err(E::InvalidInstance);
    return state_.commitPrefab(expected, root, std::move(candidate), std::move(*before), std::move(*after));
  }

  ac::PrefabEditResult replayPrefab(const EntityGuid& root, const ac::PrefabSubtree& required,
      const ac::PrefabSubtree& replacement, const ac::DocumentToken& expected) override {
    using R = ac::PrefabEditResult;
    using E = ac::PrefabEditError;
    if (!state_.prefabRead) return R::Err(E::Unsupported);
    const auto current = state_.documentState();
    if (!current) return R::Err(E::NoDocument);
    if (current->token != expected) return R::Err(E::StaleDocument);
    if (current->provenance != ac::Provenance::HandAuthored || root == EntityGuid{})
      return R::Err(E::InvalidInstance);
    const auto& document = *state_.document;
    const auto actual = prefab_edit::capture(document, root, state_.prefabDigest);
    if (!actual || *actual != required || required == replacement) return R::Err(E::ReplayConflict);
    auto candidate = prefab_edit::reinsert(document, root, required, replacement, state_.prefabDigest);
    if (!candidate || validateDocument(*candidate, state_.source.scene) ||
        !prefab_edit::assetsAvailable(replacement.source, state_.prefabAssets))
      return R::Err(E::ReplayConflict);
    const auto verified = prefab_edit::capture(*candidate, root, state_.prefabDigest);
    if (!verified || *verified != replacement) return R::Err(E::ReplayConflict);
    return state_.commitPrefab(expected, root, std::move(*candidate), required, replacement);
  }

  ac::PrefabUpdateResult updatePrefab(const AssetGuid& prefab, std::string_view revision,
      const EntityGuid& root, const ac::DocumentToken& expected) override {
    using R = ac::PrefabUpdateResult;
    if (!state_.prefabUpdateSink) return R::Err(ac::SaveAsError::Unavailable);
    if (prefab == AssetGuid{}) return R::Err(ac::SaveAsError::IdentityConflict);
    auto captured = capturePrefab(prefab, root, expected);
    if (captured.isErr()) return R::Err(captured.error());
    return state_.prefabUpdateSink(prefab, revision, std::move(captured.value()), expected.documentRevision);
  }

 private:
  Result<std::string, ac::SaveAsError> capturePrefab(const AssetGuid& prefab, const EntityGuid& root,
                                                    const ac::DocumentToken& expected) const {
    using E = ac::SaveAsError;
    using R = Result<std::string, E>;
    const auto current = state_.documentState();
    if (!current) return R::Err(E::NoDocument);
    if (current->token != expected) return R::Err(E::StaleDocument);
    if (current->provenance != ac::Provenance::HandAuthored) return R::Err(E::ImportedSceneNotSavable);
    const auto& doc = *state_.document;
    std::map<EntityGuid, const scene::AuthoringNode*> byGuid;
    std::map<EntityGuid, std::vector<EntityGuid>> children;
    for (const auto& node : doc.nodes) {
      byGuid.emplace(node.guid, &node);
      if (node.parent) children[*node.parent].push_back(node.guid);
    }
    const auto selected = byGuid.find(root);
    if (selected == byGuid.end()) return R::Err(E::UnsupportedSubtree);
    for (const auto* node = selected->second; node; node = node->parent ? byGuid.at(*node->parent) : nullptr)
      if (node->prefabOrigin) return R::Err(E::UnsupportedSubtree);
    std::vector<EntityGuid> subtree{root};
    for (std::size_t i = 0; i < subtree.size(); ++i) {
      const auto& childNodes = children[subtree[i]];
      if (childNodes.size() > ac::kMaxGuardedDuplicateNodes - subtree.size()) return R::Err(E::UnsupportedSubtree);
      subtree.insert(subtree.end(), childNodes.begin(), childNodes.end());
    }
    scene::AuthoringScene frozen;
    const std::set<EntityGuid> included(subtree.begin(), subtree.end());
    for (const auto& node : doc.nodes) {
      if (!included.contains(node.guid)) continue;
      if (node.camera || node.light || node.prefabOrigin) return R::Err(E::UnsupportedSubtree);
      frozen.nodes.push_back(node);
      if (node.guid == root) frozen.nodes.back().parent.reset();
    }
    if (!std::ranges::any_of(frozen.nodes, [](const auto& node) { return node.renderable.has_value(); }))
      return R::Err(E::UnsupportedSubtree);
    auto parsed = scene::toParsedSceneSource(frozen);
    if (parsed.isErr()) return R::Err(E::UnsupportedSubtree);
    for (auto& node : parsed.value().nodes)
      node.entityGuid = asset_system::deriveEntityGuid(prefab, "node/" + std::to_string(node.nodeId));
    auto text = asset_system::serializeSceneSource(parsed.value());
    if (text.size() > 64 * 1024 || asset_system::cookSceneToMemory(frozen, prefab).isErr())
      return R::Err(E::UnsupportedSubtree);
    return R::Ok(std::move(text));
  }
  AuthoringSession::State& state_;
  ConnectionState own_;
};

}  // namespace

// ---------------------------------------------------------------- session

AuthoringSession::AuthoringSession(DocumentSource source) : state_(std::make_unique<State>(std::move(source))) {
  ATLANTIS_CHECK_MSG(state_->source.scene != asset_system::AssetGuid{}, "AuthoringSession: the scene's AssetGuid is nil");
}

AuthoringSession::~AuthoringSession() {
  ATLANTIS_CHECK_MSG(state_->connections.empty(), "AuthoringSession destroyed with a connection still open");
}

std::unique_ptr<connection::AuthoringConnection> AuthoringSession::connect() {
  return std::make_unique<InProcessAuthoringConnection>(*state_);
}

const DocumentSource& AuthoringSession::source() const noexcept { return state_->source; }

const scene::AuthoringScene* AuthoringSession::document() const noexcept {
  return state_->document.has_value() ? &*state_->document : nullptr;
}

Result<asset_system::ValidatedSceneData, ac::Error> AuthoringSession::cookRenderLook(
    ac::DocumentToken token, ac::ApplyId apply, std::uint64_t generation,
    const ac::RenderLookBasis& basis, std::span<const ac::SetProperty> values) {
  using R = Result<asset_system::ValidatedSceneData, ac::Error>;
  if (!state_->document || !std::holds_alternative<HandAuthored>(state_->source.provenance))
    return R::Err(ac::Error::NoDocument);
  if (token != ac::DocumentToken{state_->documentId, state_->documentRevision} ||
      !state_->projection || state_->projection->generation != generation ||
      state_->projection->receipt != ac::ApplyReceipt{apply, token.documentRevision})
    return R::Err(ac::Error::RenderLookStaleDocument);
  const auto current = renderLookBasis(*state_->document);
  if (!current || *current != basis) return R::Err(ac::Error::RenderLookTargetChanged);
  if (values.size() != 8 + current->lightRoster.size() * 3)
    return R::Err(ac::Error::InvalidGuardedRenderLook);
  auto candidate = *state_->document;
  std::set<ac::PropertyAddress> addresses;
  std::vector<ac::Event> unusedEvents;
  for (const auto& write : values) {
    const auto node = findNode(candidate, write.address.node);
    if (!node || !isRenderLookAddress(candidate.nodes[*node], *current, write.address) ||
        !addresses.insert(write.address).second)
      return R::Err(ac::Error::InvalidGuardedRenderLook);
    if (const auto refusal = state_->apply(candidate, write, unusedEvents)) return R::Err(*refusal);
  }
  const auto cooked = asset_system::cookSceneToMemory(candidate, state_->source.scene);
  if (cooked.isErr()) return R::Err(ac::Error::CookRefused);
  auto decoded = asset_system::decodeScene(cooked.value());
  if (decoded.isErr()) return R::Err(ac::Error::CookRefused);
  return R::Ok(std::move(decoded.value()));
}

void AuthoringSession::setRecoveryStore(RecoveryStore& store) { state_->recovery = &store; }
void AuthoringSession::setSceneSink(SceneSink sink) { state_->sink = std::move(sink); }
void AuthoringSession::setSaveAsSink(SaveAsSink sink) { state_->copySink = std::move(sink); }
void AuthoringSession::setCreateSceneSink(CreateSceneSink sink) { state_->createSink = std::move(sink); }
void AuthoringSession::setPrefabSink(PrefabSink sink) { state_->prefabSink = std::move(sink); }
void AuthoringSession::setPrefabSync(PrefabRead read, PrefabDigest digest, PrefabAssets assets) {
  state_->prefabRead = std::move(read);
  state_->prefabDigest = std::move(digest);
  state_->prefabAssets = std::move(assets);
}
void AuthoringSession::setPrefabUpdateSink(PrefabUpdateSink sink) { state_->prefabUpdateSink = std::move(sink); }
void AuthoringSession::setPrefabLookup(PrefabLookup lookup) { state_->prefabLookup = std::move(lookup); }
void AuthoringSession::setSceneEnvironmentLookup(SceneEnvironmentLookup lookup) {
  state_->sceneEnvironmentLookup = std::move(lookup);
}

void AuthoringSession::reportApplied(std::uint64_t generation) { state_->publish({ac::Applied{generation}}); }
void AuthoringSession::reportReload(ac::ApplyId id, Result<std::uint64_t, ac::ReloadError> result,
                                    std::optional<ac::SceneEnvironmentApplyDetail> environmentDetail) {
  state_->reportReload(id, std::move(result), environmentDetail);
}

}  // namespace atlantis::authoring
