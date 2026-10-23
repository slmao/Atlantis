#include <atlantis/authoring/authoring_session.h>

#include "document_access.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/guid_mint.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/authoring/document_validation.h>

#include <algorithm>
#include <fstream>
#include <random>
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

  std::optional<ac::Error> apply(scene::AuthoringScene& d, const ac::SetActiveCamera& c,
                                 std::vector<ac::Event>& events) {
    d.activeCamera = c.camera;
    events.push_back(ac::ActiveCameraChanged{c.camera});
    return std::nullopt;
  }

  // A transaction (one command or several): applied to a copy, validated, and
  // swapped in, or refused whole.
  [[nodiscard]] std::optional<ac::CommandFailure> transact(const std::vector<ac::Command>& commands,
                                                           CommandTicket first) {
    if (!document.has_value()) return ac::CommandFailure{first, ac::Error::NoDocument};
    scene::AuthoringScene candidate = *document;
    std::vector<ac::Event> events;
    for (std::size_t i = 0; i < commands.size(); ++i) {
      if (const auto refusal = applyCommand(candidate, commands[i], events)) {
        return ac::CommandFailure{CommandTicket{first.value + i}, *refusal};
      }
    }
    if (const auto refusal = validateDocument(candidate, source.scene)) return ac::CommandFailure{first, *refusal};
    document = std::move(candidate);
    publish(events);
    return std::nullopt;
  }

  [[nodiscard]] atlantis::Result<std::monostate, ac::OpenError> open() {
    using ResultT = atlantis::Result<std::monostate, ac::OpenError>;
    std::ifstream file(sourcePathOf(source.provenance), std::ios::binary);
    if (!file.is_open()) return ResultT::Err(ac::OpenError::SourceUnreadable);
    std::ostringstream text;
    text << file.rdbuf();
    if (file.bad()) return ResultT::Err(ac::OpenError::SourceUnreadable);
    const auto parsed = asset_system::parseSceneSource(text.str());
    if (parsed.isErr()) return ResultT::Err(ac::OpenError::SourceParseFailed);
    auto mapped = scene::toAuthoringScene(parsed.value());
    if (mapped.isErr()) return ResultT::Err(ac::OpenError::SourceInvalid);
    if (validateDocument(mapped.value(), source.scene).has_value()) return ResultT::Err(ac::OpenError::SourceInvalid);
    document = std::move(mapped.value());
    publish({ac::DocumentReopened{source.scene}});
    return ResultT::Ok({});
  }

  // ADR-0131 D2, D3: the document cooked in memory exactly as the build cooks
  // its saved text, decoded, and handed to the host's sink.
  [[nodiscard]] atlantis::Result<std::monostate, ac::ApplyError> apply() {
    using ResultT = atlantis::Result<std::monostate, ac::ApplyError>;
    if (!document.has_value()) return ResultT::Err(ac::ApplyError::NoDocument);
    if (!sink) return ResultT::Err(ac::ApplyError::NoSceneSink);
    const auto cooked = asset_system::cookSceneToMemory(*document, source.scene);
    if (cooked.isErr()) return ResultT::Err(ac::ApplyError::CookFailed);
    auto decoded = asset_system::decodeScene(cooked.value());
    if (decoded.isErr()) return ResultT::Err(ac::ApplyError::DecodeFailed);
    if (!sink(std::move(decoded.value()))) return ResultT::Err(ac::ApplyError::ReloadRefused);
    return ResultT::Ok({});
  }

  [[nodiscard]] atlantis::Result<std::monostate, ac::SaveError> save() const {
    using ResultT = atlantis::Result<std::monostate, ac::SaveError>;
    if (!document.has_value()) return ResultT::Err(ac::SaveError::NoDocument);
    const auto* handAuthored = std::get_if<HandAuthored>(&source.provenance);
    if (handAuthored == nullptr) return ResultT::Err(ac::SaveError::ImportedSceneNotSavable);
    const auto parsed = scene::toParsedSceneSource(*document);
    ATLANTIS_CHECK_MSG(parsed.isOk(), "save(): a valid document failed to map to its source form");
    if (!writeTextAtomically(handAuthored->sourcePath, asset_system::serializeSceneSource(parsed.value()))) {
      return ResultT::Err(ac::SaveError::WriteFailed);
    }
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
                            static_cast<std::uint64_t>(state_.document->nodes.size())};
  }

  atlantis::Result<std::vector<ac::NodeSummary>, ac::Error> listNodes() const override {
    using ResultT = atlantis::Result<std::vector<ac::NodeSummary>, ac::Error>;
    if (!state_.document.has_value()) return ResultT::Err(ac::Error::NoDocument);
    std::vector<ac::NodeSummary> out;
    out.reserve(state_.document->nodes.size());
    for (const auto& node : state_.document->nodes) out.push_back({node.guid, node.parent});
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

  atlantis::Result<std::monostate, ac::ApplyError> apply() override { return state_.apply(); }

  atlantis::Result<std::monostate, ac::SaveError> save() override { return state_.save(); }

 private:
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

void AuthoringSession::setSceneSink(SceneSink sink) { state_->sink = std::move(sink); }

void AuthoringSession::reportApplied(std::uint64_t generation) { state_->publish({ac::Applied{generation}}); }

}  // namespace atlantis::authoring
