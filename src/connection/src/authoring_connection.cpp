#include <atlantis/connection/authoring_connection.h>

#include <type_traits>

namespace atlantis::connection::authoring {

namespace {

// The node an event is about, if any (EventFilter::node).
[[nodiscard]] std::optional<EntityGuid> nodeOf(const Event& event) {
  return std::visit(
      [](const auto& e) -> std::optional<EntityGuid> {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, PropertyChanged>) {
          return e.address.node;
        } else if constexpr (std::is_same_v<E, NodeCreated> || std::is_same_v<E, NodeDestroyed> ||
                             std::is_same_v<E, ComponentAdded> || std::is_same_v<E, ComponentRemoved> ||
                             std::is_same_v<E, ParentChanged> || std::is_same_v<E, NodeNameChanged>) {
          return e.node;
        } else {
          return std::nullopt;
        }
      },
      event);
}

// The component an event is about, if any (EventFilter::component).
[[nodiscard]] std::optional<schema::TypeId> componentOf(const Event& event) {
  return std::visit(
      [](const auto& e) -> std::optional<schema::TypeId> {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, PropertyChanged>) {
          return e.address.component;
        } else if constexpr (std::is_same_v<E, ComponentAdded> || std::is_same_v<E, ComponentRemoved>) {
          return e.component;
        } else {
          return std::nullopt;
        }
      },
      event);
}

}  // namespace

EventKind kindOf(const Event& event) noexcept {
  // The variant's alternatives are declared in EventKind's order.
  static_assert(std::variant_size_v<Event> == static_cast<std::size_t>(EventKind::NodeMovedInSiblingOrder) + 1);
  return static_cast<EventKind>(event.index());
}

bool EventFilter::matches(const Event& event) const {
  if (!kinds.contains(kindOf(event))) return false;
  if (const auto* order = std::get_if<NodeOrderChanged>(&event)) {
    return !component && (!node || *node == order->node || *node == order->sibling);
  }
  if (const auto* move = std::get_if<NodeMovedInSiblingOrder>(&event)) {
    return !component && (!node || *node == move->node);
  }
  if (node.has_value() && nodeOf(event) != node) return false;
  if (component.has_value() && componentOf(event) != component) return false;
  return true;
}

std::string_view toString(Error error) noexcept {
  switch (error) {
    case Error::InvalidNodeName: return "InvalidNodeName";
    case Error::SiblingOrderStaleRevision: return "SiblingOrderStaleRevision";
    case Error::SiblingOrderRequiresHandAuthored: return "SiblingOrderRequiresHandAuthored";
    case Error::InvalidSiblingSwap: return "InvalidSiblingSwap";
    case Error::InvalidSiblingMove: return "InvalidSiblingMove";
    case Error::InvalidGuardedPropertyBatch: return "InvalidGuardedPropertyBatch";
    case Error::PropertyBatchStaleRevision: return "PropertyBatchStaleRevision";
    case Error::InvalidGuardedDuplicateBatch: return "InvalidGuardedDuplicateBatch";
    case Error::DuplicateBatchStaleRevision: return "DuplicateBatchStaleRevision";
    case Error::InvalidGuardedNameBatch: return "InvalidGuardedNameBatch";
    case Error::NameBatchStaleDocument: return "NameBatchStaleDocument";
    case Error::NoDocument: return "NoDocument";
    case Error::UnknownNode: return "UnknownNode";
    case Error::UnknownComponentType: return "UnknownComponentType";
    case Error::ComponentMissing: return "ComponentMissing";
    case Error::ComponentAlreadyPresent: return "ComponentAlreadyPresent";
    case Error::ComponentRequired: return "ComponentRequired";
    case Error::UnknownField: return "UnknownField";
    case Error::KindMismatch: return "KindMismatch";
    case Error::EnumValueOutOfRange: return "EnumValueOutOfRange";
    case Error::NodePositionOutOfRange: return "NodePositionOutOfRange";
    case Error::FieldOutOfDomain: return "FieldOutOfDomain";
    case Error::NonEmptyDocument: return "NonEmptyDocument";
    case Error::NodeGuidNonNil: return "NodeGuidNonNil";
    case Error::NodeGuidUnique: return "NodeGuidUnique";
    case Error::ParentExists: return "ParentExists";
    case Error::ParentAcyclic: return "ParentAcyclic";
    case Error::ActiveCameraExists: return "ActiveCameraExists";
    case Error::ActiveCameraHasCamera: return "ActiveCameraHasCamera";
    case Error::ComponentExclusivity: return "ComponentExclusivity";
    case Error::LightRangeMatchesKind: return "LightRangeMatchesKind";
    case Error::MaxDirectionalLights: return "MaxDirectionalLights";
    case Error::MaxPointLights: return "MaxPointLights";
    case Error::CookRefused: return "CookRefused";
    case Error::NotRepresentableInSource: return "NotRepresentableInSource";
  }
  return "Unknown";
}

std::string_view toString(OpenError error) noexcept {
  switch (error) {
    case OpenError::SourceUnreadable: return "SourceUnreadable";
    case OpenError::SourceParseFailed: return "SourceParseFailed";
    case OpenError::SourceInvalid: return "SourceInvalid";
  }
  return "Unknown";
}

std::string_view toString(DocumentError error) noexcept {
  switch (error) {
    case DocumentError::NoDocument: return "NoDocument";
    case DocumentError::StaleDocument: return "StaleDocument";
    case DocumentError::ImportedSceneNotSavable: return "ImportedSceneNotSavable";
    case DocumentError::WriteFailed: return "WriteFailed";
    case DocumentError::SourceUnreadable: return "SourceUnreadable";
    case DocumentError::SourceParseFailed: return "SourceParseFailed";
    case DocumentError::SourceInvalid: return "SourceInvalid";
    case DocumentError::Unsupported: return "Unsupported";
    case DocumentError::NoAuthoring: return "NoAuthoring";
    case DocumentError::TransportFailure: return "TransportFailure";
    case DocumentError::ProtocolFailure: return "ProtocolFailure";
    case DocumentError::InvalidRequest: return "InvalidRequest";
  }
  return "Unknown";
}

std::string_view toString(SaveError error) noexcept {
  switch (error) {
    case SaveError::NoDocument: return "NoDocument";
    case SaveError::ImportedSceneNotSavable: return "ImportedSceneNotSavable";
    case SaveError::WriteFailed: return "WriteFailed";
  }
  return "Unknown";
}

bool validSaveAsName(std::string_view name) noexcept {
  if (name.empty() || name.size() > 48 || name.front() < 'a' || name.front() > 'z') return false;
  for (char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

std::string saveAsSource(std::string_view name) {
  return "scenes/saved/save_" + std::string(name) + "/scene.scene.txt";
}

std::string_view toString(SaveAsError error) noexcept {
  switch (error) {
    case SaveAsError::Unavailable: return "Unavailable";
    case SaveAsError::InvalidName: return "InvalidName";
    case SaveAsError::NoDocument: return "NoDocument";
    case SaveAsError::ImportedSceneNotSavable: return "ImportedSceneNotSavable";
    case SaveAsError::UnsafeTarget: return "UnsafeTarget";
    case SaveAsError::Busy: return "Busy";
    case SaveAsError::NameConflict: return "NameConflict";
    case SaveAsError::IdentityConflict: return "IdentityConflict";
    case SaveAsError::WriteFailed: return "WriteFailed";
    case SaveAsError::StaleSource: return "StaleSource";
    case SaveAsError::Unsupported: return "Unsupported";
    case SaveAsError::UnknownOutcome: return "UnknownOutcome";
    case SaveAsError::StaleDocument: return "StaleDocument";
    case SaveAsError::UnsupportedSubtree: return "UnsupportedSubtree";
  }
  return "Unknown";
}

bool validPrefabDigest(std::string_view value) noexcept {
  return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

std::string_view toString(PrefabEditError error) noexcept {
  using E = PrefabEditError;
  switch (error) {
    case E::Unsupported: return "Unsupported";
    case E::NoDocument: return "NoDocument";
    case E::StaleDocument: return "StaleDocument";
    case E::StaleSource: return "StaleSource";
    case E::InvalidInstance: return "InvalidInstance";
    case E::LocalConflict: return "LocalConflict";
    case E::BaselineUnavailable: return "BaselineUnavailable";
    case E::ReplayConflict: return "ReplayConflict";
    case E::UnknownOutcome: return "UnknownOutcome";
  }
  return "Unknown";
}

std::string prefabSource(std::string_view name) {
  return "prefabs/prefab_" + std::string(name) + "/source.scene.txt";
}

std::string_view toString(CreateSceneError error) noexcept {
  switch (error) {
    case CreateSceneError::Unavailable: return "Unavailable";
    case CreateSceneError::InvalidName: return "InvalidName";
    case CreateSceneError::UnsafeTarget: return "UnsafeTarget";
    case CreateSceneError::Busy: return "Busy";
    case CreateSceneError::NameConflict: return "NameConflict";
    case CreateSceneError::IdentityConflict: return "IdentityConflict";
    case CreateSceneError::WriteFailed: return "WriteFailed";
    case CreateSceneError::Unsupported: return "Unsupported";
    case CreateSceneError::UnknownOutcome: return "UnknownOutcome";
    case CreateSceneError::NoAuthoring: return "NoAuthoring";
  }
  return "Unknown";
}

std::string_view toString(ApplyError error) noexcept {
  switch (error) {
    case ApplyError::NoDocument: return "NoDocument";
    case ApplyError::NoSceneSink: return "NoSceneSink";
    case ApplyError::CookFailed: return "CookFailed";
    case ApplyError::DecodeFailed: return "DecodeFailed";
    case ApplyError::ReloadRefused: return "ReloadRefused";
  }
  return "Unknown";
}

Result<ApplyId, asset_system::GuidParseError> parseApplyId(std::string_view text) {
  const auto parsed = asset_system::parseAssetGuid(text);
  if (parsed.isErr()) return Result<ApplyId, asset_system::GuidParseError>::Err(parsed.error());
  return Result<ApplyId, asset_system::GuidParseError>::Ok(ApplyId{parsed.value().bytes});
}
std::string toString(const ApplyId& id) { return asset_system::toString(AssetGuid{id.bytes}); }
std::string_view toString(ApplyState state) noexcept {
  switch (state) {
    case ApplyState::Pending: return "Pending";
    case ApplyState::Applied: return "Applied";
    case ApplyState::Failed: return "Failed";
    case ApplyState::Superseded: return "Superseded";
  }
  return "Unknown";
}
std::string_view toString(ReloadError error) noexcept {
  switch (error) {
    case ReloadError::ScenePreparationFailed: return "ScenePreparationFailed";
    case ReloadError::RenderPreparationFailed: return "RenderPreparationFailed";
    case ReloadError::RuntimeStopped: return "RuntimeStopped";
  }
  return "Unknown";
}
std::string_view toString(ApplyLookupError) noexcept { return "UnknownApply"; }

std::string_view toString(RecoveryError error) noexcept {
  switch (error) {
    case RecoveryError::Unsupported: return "Unsupported";
    case RecoveryError::NoDocument: return "NoDocument";
    case RecoveryError::NoAuthoring: return "NoAuthoring";
    case RecoveryError::Busy: return "Busy";
    case RecoveryError::Unavailable: return "Unavailable";
    case RecoveryError::StaleDocument: return "StaleDocument";
    case RecoveryError::StaleCopy: return "StaleCopy";
    case RecoveryError::DirtyDocument: return "DirtyDocument";
    case RecoveryError::SourceChanged: return "SourceChanged";
    case RecoveryError::SourceUnreadable: return "SourceUnreadable";
    case RecoveryError::InvalidCopy: return "InvalidCopy";
    case RecoveryError::IoFailed: return "IoFailed";
    case RecoveryError::TransportFailure: return "TransportFailure";
    case RecoveryError::ProtocolFailure: return "ProtocolFailure";
    case RecoveryError::InvalidRequest: return "InvalidRequest";
  }
  return "Unknown";
}

std::string_view toString(RecoveryCapture state) noexcept {
  switch (state) {
    case RecoveryCapture::Idle: return "Idle";
    case RecoveryCapture::Writing: return "Writing";
    case RecoveryCapture::Failed: return "Failed";
    case RecoveryCapture::Unavailable: return "Unavailable";
  }
  return "Unknown";
}

std::string_view toString(RecoverySource state) noexcept {
  switch (state) {
    case RecoverySource::Matches: return "Matches";
    case RecoverySource::Changed: return "Changed";
    case RecoverySource::Unreadable: return "Unreadable";
    case RecoverySource::AlreadySaved: return "AlreadySaved";
    case RecoverySource::DirtyDocument: return "DirtyDocument";
  }
  return "Unknown";
}

}  // namespace atlantis::connection::authoring
