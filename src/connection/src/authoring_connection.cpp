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
  static_assert(std::variant_size_v<Event> == static_cast<std::size_t>(EventKind::NodeNameChanged) + 1);
  return static_cast<EventKind>(event.index());
}

bool EventFilter::matches(const Event& event) const {
  if (!kinds.contains(kindOf(event))) return false;
  if (node.has_value() && nodeOf(event) != node) return false;
  if (component.has_value() && componentOf(event) != component) return false;
  return true;
}

std::string_view toString(Error error) noexcept {
  switch (error) {
    case Error::InvalidNodeName: return "InvalidNodeName";
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
    case SaveAsError::Unsupported: return "Unsupported";
    case SaveAsError::UnknownOutcome: return "UnknownOutcome";
  }
  return "Unknown";
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

}  // namespace atlantis::connection::authoring
