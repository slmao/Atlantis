#include <atlantis/connection/asset_connection.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <array>

namespace atlantis::connection::assets {
std::string_view toString(AssetType type) noexcept {
  switch (type) {
    case AssetType::Mesh: return "mesh";
    case AssetType::Texture: return "texture";
    case AssetType::Material: return "material";
    case AssetType::Scene: return "scene";
    case AssetType::Environment: return "environment";
  }
  ATLANTIS_CHECK(false);
  return {};
}
std::string_view toString(AssetError error) noexcept {
  switch (error) {
    case AssetError::CatalogUnavailable: return "CatalogUnavailable";
    case AssetError::UnknownAsset: return "UnknownAsset";
  }
  ATLANTIS_CHECK(false);
  return {};
}
namespace {
template <class Enum, std::size_t N>
std::string_view token(Enum value, const std::array<const char*, N>& names) noexcept {
  const auto index = static_cast<std::size_t>(value);
  ATLANTIS_CHECK(index < N);
  return index < N ? names[index] : "";
}
}  // namespace

std::string_view toString(RebuildError value) noexcept {
  return token(value, std::array{"Unavailable", "Busy", "WorkspaceNeedsRepair", "CatalogUnavailable",
                                 "UnknownAsset", "UnsupportedProducer", "UnknownJob", "Unsupported"});
}
std::string_view toString(RebuildState value) noexcept {
  return token(value, std::array{"Running", "Succeeded", "Failed"});
}
std::string_view toString(RebuildStage value) noexcept {
  return token(value, std::array{"Launch", "Pipeline", "Validation", "Publication"});
}
std::string_view toString(RebuildFailureCode value) noexcept {
  return token(value, std::array{"LaunchFailed", "BuildFailed", "InvalidOutput", "IdentityChanged", "PublishFailed",
                                 "TimedOut"});
}
bool validFailure(RebuildStage stage, RebuildFailureCode code) noexcept {
  switch (code) {
    case RebuildFailureCode::LaunchFailed: return stage == RebuildStage::Launch;
    case RebuildFailureCode::BuildFailed: return stage == RebuildStage::Pipeline;
    case RebuildFailureCode::InvalidOutput:
    case RebuildFailureCode::IdentityChanged: return stage == RebuildStage::Validation;
    case RebuildFailureCode::PublishFailed: return stage == RebuildStage::Publication;
    case RebuildFailureCode::TimedOut: return true;
  }
  return false;
}

std::string_view toString(ImportMode value) noexcept {
  return token(value, std::array{"Register", "Resume"});
}
std::string_view toString(ImportError value) noexcept {
  return token(value, std::array{"Unavailable", "Busy", "WorkspaceNeedsRepair", "CatalogUnavailable", "NameConflict",
                                 "ActivationRequired", "NothingToResume", "UnknownJob", "Unsupported"});
}
std::string_view toString(ImportStage value) noexcept {
  return token(value, std::array{"Source", "Pipeline", "Validation", "Registration", "Publication"});
}
std::string_view toString(ImportFailureCode value) noexcept {
  return token(value, std::array{"InvalidSource", "LaunchFailed", "BuildFailed", "InvalidOutput",
                                 "IdentityConflict", "RegisterFailed", "PublishFailed", "TimedOut"});
}
std::string_view toString(RegistrationState value) noexcept {
  return token(value, std::array{"NotCommitted", "Committed", "Unknown"});
}
bool validImportName(std::string_view name) noexcept {
  const auto letter = [](char c) { return c >= 'a' && c <= 'z'; };
  if (name.empty() || name.size() > 48 || !letter(name.front())) return false;
  if (!std::ranges::all_of(name, [&](char c) { return letter(c) || (c >= '0' && c <= '9') || c == '_'; }))
    return false;
  if (name == "con" || name == "prn" || name == "aux" || name == "nul") return false;
  return !(name.size() == 4 && (name.starts_with("com") || name.starts_with("lpt")) &&
           name.back() >= '0' && name.back() <= '9');
}
bool validFailure(ImportStage stage, ImportFailureCode code) noexcept {
  switch (code) {
    case ImportFailureCode::InvalidSource: return stage == ImportStage::Source;
    case ImportFailureCode::LaunchFailed:
    case ImportFailureCode::BuildFailed: return stage == ImportStage::Pipeline;
    case ImportFailureCode::InvalidOutput:
    case ImportFailureCode::IdentityConflict: return stage == ImportStage::Validation;
    case ImportFailureCode::RegisterFailed: return stage == ImportStage::Registration;
    case ImportFailureCode::PublishFailed: return stage == ImportStage::Publication;
    case ImportFailureCode::TimedOut: return true;
  }
  return false;
}
}  // namespace atlantis::connection::assets
