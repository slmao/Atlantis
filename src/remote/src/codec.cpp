#include "codec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <variant>

namespace atlantis::remote::codec {

namespace {
std::optional<connection::assets::AssetType> decodeAssetType(const Value& value) {
  if (!value.isString()) return std::nullopt;
  using connection::assets::AssetType;
  for (const auto type : {AssetType::Mesh, AssetType::Texture, AssetType::Material,
                          AssetType::Scene, AssetType::Environment}) {
    if (value.asString() == connection::assets::toString(type)) return type;
  }
  return std::nullopt;
}
}  // namespace

Value encode(const connection::assets::AssetSummary& v) {
  Value out = Value::object();
  out.set("guid", encodeAsset(v.guid));
  out.set("type", Value::string(std::string(connection::assets::toString(v.type))));
  out.set("source", Value::string(v.source));
  if (v.logicalPath) out.set("logicalPath", Value::string(*v.logicalPath));
  if (v.displayName) out.set("displayName", Value::string(*v.displayName));
  if (v.isPrefab) out.set("isPrefab", Value::boolean(true));
  return out;
}
Value encode(const connection::assets::AssetDetails& v) {
  Value out = encode(connection::assets::AssetSummary{v.guid, v.type, v.source, v.displayName, v.logicalPath, v.isPrefab});
  out.set("artifactSchema", Value::number(static_cast<std::uint64_t>(v.artifactSchema)));
  out.set("sourceSchema", v.sourceSchema ? Value::number(static_cast<std::uint64_t>(*v.sourceSchema)) : Value());
  out.set("tool", Value::string(v.tool));
  Value deps = Value::array();
  for (const auto& guid : v.dependencies) deps.push(encodeAsset(guid));
  out.set("dependencies", std::move(deps));
  return out;
}
std::optional<connection::assets::AssetSummary> decodeAssetSummary(const Value& v) {
  const auto* g = v.find("guid");
  const auto* t = v.find("type");
  const auto* s = v.find("source");
  if (!g || !t || !s || !s->isString()) return std::nullopt;
  auto guid = decodeAsset(*g);
  if (!guid || *guid == asset_system::AssetGuid{}) return std::nullopt;
  const auto type = decodeAssetType(*t);
  if (!type) return std::nullopt;
  std::optional<std::string> displayName;
  for (const auto& [key, field] : v.asObject()) {
    if (key != "displayName") continue;
    if (displayName || *type != connection::assets::AssetType::Texture || !field.isString() ||
        !connection::assets::validImportName(field.asString())) return std::nullopt;
    displayName = field.asString();
  }
  std::optional<std::string> logicalPath;
  for (const auto& [key, field] : v.asObject()) {
    if (key != "logicalPath") continue;
    const std::string_view source = s->asString();
    constexpr std::string_view prefix = "content:texture_", suffix = "/source.png";
    if (logicalPath || *type != connection::assets::AssetType::Texture || !field.isString() ||
        !source.starts_with(prefix) || !source.ends_with(suffix) ||
        source.size() <= prefix.size() + suffix.size() ||
        !connection::assets::validImportName(source.substr(prefix.size(), source.size() - prefix.size() - suffix.size())) ||
        !connection::assets::validTextureLogicalPath(field.asString())) return std::nullopt;
    logicalPath = field.asString();
  }
  const auto* prefab = v.find("isPrefab");
  if (prefab && (!prefab->isBool() || *type != connection::assets::AssetType::Scene)) return std::nullopt;
  return connection::assets::AssetSummary{*guid, *type, s->asString(), std::move(displayName), std::move(logicalPath),
                                          prefab && prefab->asBool()};
}
std::optional<connection::assets::AssetDetails> decodeAssetDetails(const Value& v) {
  auto summary = decodeAssetSummary(v);
  const auto* a = v.find("artifactSchema");
  const auto* s = v.find("sourceSchema");
  const auto* t = v.find("tool");
  const auto* d = v.find("dependencies");
  if (!summary || !a || !s || !t || !t->isString() || !d || !d->isArray()) return std::nullopt;
  auto artifact = decodeUInt64(*a);
  if (!artifact || *artifact > UINT32_MAX) return std::nullopt;
  std::optional<std::uint32_t> source;
  if (!s->isNull()) {
    auto n = decodeUInt64(*s);
    if (!n || *n > UINT32_MAX) return std::nullopt;
    source = static_cast<std::uint32_t>(*n);
  }
  std::vector<asset_system::AssetGuid> deps;
  for (const auto& item : d->asArray()) {
    auto guid = decodeAsset(item);
    if (!guid || *guid == asset_system::AssetGuid{} || (!deps.empty() && !(deps.back() < *guid))) return std::nullopt;
    deps.push_back(*guid);
  }
  return connection::assets::AssetDetails{summary->guid, summary->type, summary->source,
      static_cast<std::uint32_t>(*artifact), source, t->asString(), std::move(deps), std::move(summary->displayName),
      std::move(summary->logicalPath), summary->isPrefab};
}
std::optional<connection::assets::AssetError> decodeAssetError(const Value& v) {
  if (!v.isString()) return std::nullopt;
  for (auto error : {connection::assets::AssetError::CatalogUnavailable, connection::assets::AssetError::UnknownAsset})
    if (v.asString() == connection::assets::toString(error)) return error;
  return std::nullopt;
}

namespace {
namespace ca = connection::assets;
template<class Enum>
std::optional<Enum> rebuildToken(const Value* value, Enum last) {
  if (!value || !value->isString()) return std::nullopt;
  for (unsigned i = 0; i <= static_cast<unsigned>(last); ++i) {
    auto token = static_cast<Enum>(i);
    if (value->asString() == ca::toString(token)) return token;
  }
  return std::nullopt;
}
std::optional<asset_system::AssetGuid> nonNilAsset(const Value* value) {
  if (!value) return std::nullopt;
  const auto guid = decodeAsset(*value);
  return guid && *guid != asset_system::AssetGuid{} ? guid : std::nullopt;
}
std::optional<ca::RebuildFailure> decodeRebuildFailure(const Value& value) {
  const auto stage = rebuildToken(value.find("stage"), ca::RebuildStage::Publication);
  const auto code = rebuildToken(value.find("code"), ca::RebuildFailureCode::TimedOut);
  const auto* diagnostic = value.find("diagnostic");
  if (!stage || !code || !ca::validFailure(*stage, *code) || !diagnostic ||
      !diagnostic->isString() || diagnostic->asString().size() > 4096) return std::nullopt;
  std::optional<std::uint32_t> exit;
  if (const auto* item = value.find("exitCode")) {
    const auto number = decodeUInt64(*item);
    if (!number || *number > UINT32_MAX) return std::nullopt;
    exit = static_cast<std::uint32_t>(*number);
  }
  return ca::RebuildFailure{*stage, *code, diagnostic->asString(), exit};
}
}  // namespace

Value encode(const ca::RebuildJob& job) {
  Value out = Value::object();
  out.set("id", encodeAsset(job.id.value));
  out.set("asset", encodeAsset(job.asset));
  out.set("state", Value::string(std::string(ca::toString(job.state))));
  if (job.generation) out.set("generation", encodeAsset(job.generation->value));
  if (job.failure) {
    Value f = Value::object();
    f.set("stage", Value::string(std::string(ca::toString(job.failure->stage))));
    f.set("code", Value::string(std::string(ca::toString(job.failure->code))));
    f.set("diagnostic", Value::string(job.failure->diagnostic));
    if (job.failure->exitCode) f.set("exitCode", Value::number(static_cast<std::uint64_t>(*job.failure->exitCode)));
    out.set("failure", std::move(f));
  }
  return out;
}
Value encode(const ca::RebuildStatus& status) {
  Value out = Value::object();
  out.set("activeGeneration", status.activeGeneration ? encodeAsset(status.activeGeneration->value) : Value());
  out.set("job", status.job ? encode(*status.job) : Value());
  return out;
}
std::optional<ca::RebuildJob> decodeRebuildJob(const Value& value) {
  const auto id = nonNilAsset(value.find("id"));
  const auto asset = nonNilAsset(value.find("asset"));
  const auto state = rebuildToken(value.find("state"), ca::RebuildState::Failed);
  const auto* generation = value.find("generation");
  const auto* failure = value.find("failure");
  if (!id || !asset || !state || (generation != nullptr) != (*state == ca::RebuildState::Succeeded) ||
      (failure != nullptr) != (*state == ca::RebuildState::Failed)) return std::nullopt;
  ca::RebuildJob job{{*id}, *asset, *state, {}, {}};
  if (generation) {
    const auto guid = nonNilAsset(generation);
    if (!guid) return std::nullopt;
    job.generation = ca::GenerationId{*guid};
  }
  if (failure && !(job.failure = decodeRebuildFailure(*failure))) return std::nullopt;
  return job;
}
std::optional<ca::RebuildStatus> decodeRebuildStatus(const Value& value) {
  const auto* active = value.find("activeGeneration");
  const auto* job = value.find("job");
  if (!active || !job) return std::nullopt;
  ca::RebuildStatus out;
  if (!active->isNull()) {
    const auto guid = nonNilAsset(active);
    if (!guid) return std::nullopt;
    out.activeGeneration = ca::GenerationId{*guid};
  }
  if (!job->isNull() && !(out.job = decodeRebuildJob(*job))) return std::nullopt;
  return out;
}
std::optional<ca::MoveTextureError> decodeMoveTextureError(const Value& value, bool) {
  if (!value.isString()) return std::nullopt;
  for (unsigned i = 0; i <= static_cast<unsigned>(ca::MoveTextureError::Unchanged); ++i) {
    const auto error = static_cast<ca::MoveTextureError>(i);
    if (value.asString() == ca::toString(error)) return error;
  }
  return std::nullopt;
}
std::optional<ca::RenameTextureError> decodeRenameTextureError(const Value& value, bool) {
  if (!value.isString()) return std::nullopt;
  for (unsigned i = 0; i <= static_cast<unsigned>(ca::RenameTextureError::Unchanged); ++i) {
    const auto error = static_cast<ca::RenameTextureError>(i);
    if (value.asString() == ca::toString(error)) return error;
  }
  return std::nullopt;
}
Value encode(const ca::TextureRemovalCheck& check) {
  Value out = Value::object();
  out.set("id", encodeAsset(check.id.value));
  out.set("asset", encodeAsset(check.target.asset));
  out.set("expectedGeneration", encodeAsset(check.target.expectedGeneration.value));
  out.set("state", Value::string(std::string(ca::toString(check.state))));
  Value blockers = Value::array();
  for (const auto& blocker : check.blockers) {
    Value entry = Value::object();
    entry.set("asset", encodeAsset(blocker.asset));
    entry.set("source", Value::string(blocker.source));
    entry.set("slot", blocker.slot ? Value::string(std::string(ca::toString(*blocker.slot))) : Value());
    blockers.push(std::move(entry));
  }
  out.set("blockers", std::move(blockers));
  out.set("truncated", Value::boolean(check.truncated));
  out.set("incomplete", Value::boolean(check.incomplete));
  out.set("diagnostic", Value::string(check.diagnostic));
  return out;
}

std::optional<ca::TextureRemovalCheck> decodeTextureRemovalCheck(const Value& value) {
  using S = ca::TextureRemovalState;
  const auto id = nonNilAsset(value.find("id"));
  const auto asset = nonNilAsset(value.find("asset"));
  const auto generation = nonNilAsset(value.find("expectedGeneration"));
  const auto state = rebuildToken(value.find("state"), S::StaleCheck);
  const auto* blockers = value.find("blockers");
  const auto* truncated = value.find("truncated");
  const auto* incomplete = value.find("incomplete");
  const auto* diagnostic = value.find("diagnostic");
  if (!value.isObject() || value.asObject().size() != 8 || !id || !asset || !generation || !state ||
      !blockers || !blockers->isArray() || blockers->asArray().size() > ca::kTextureRemovalBlockerLimit ||
      !truncated || !truncated->isBool() || !incomplete || !incomplete->isBool() ||
      !diagnostic || !diagnostic->isString() || diagnostic->asString().size() > 4096) return {};
  if ((*state == S::Referenced) != !blockers->asArray().empty() ||
      (truncated->asBool() && blockers->asArray().size() != ca::kTextureRemovalBlockerLimit) ||
      ((*state == S::Checking || *state == S::Ready) && (incomplete->asBool() || !diagnostic->asString().empty())) ||
      (*state == S::CheckIncomplete && !incomplete->asBool())) return {};
  ca::TextureRemovalCheck out{{*id}, {*asset, {*generation}}, *state, {},
                             truncated->asBool(), incomplete->asBool(), diagnostic->asString()};
  for (const auto& entry : blockers->asArray()) {
    const auto dependent = nonNilAsset(entry.find("asset"));
    const auto* source = entry.find("source");
    const auto* slot = entry.find("slot");
    if (!entry.isObject() || entry.asObject().size() != 3 || !dependent || !source ||
        !source->isString() || source->asString().empty() || source->asString().size() > 4096 || !slot) return {};
    const auto parsed = slot->isNull() ? std::nullopt : rebuildToken(slot, ca::TextureReferenceSlot::Emissive);
    if (!parsed && !slot->isNull()) return {};
    ca::TextureRemovalBlocker blocker{*dependent, source->asString(), parsed};
    if (std::ranges::find(out.blockers, blocker) != out.blockers.end()) return {};
    out.blockers.push_back(std::move(blocker));
  }
  return out;
}

std::optional<ca::TextureRemovalError> decodeTextureRemovalError(const Value& value, bool) {
  return rebuildToken(&value, ca::TextureRemovalError::UnknownCheck);
}
std::optional<ca::RebuildError> decodeRebuildError(const Value& value, bool start) {
  const auto error = rebuildToken(&value, ca::RebuildError::UnknownJob);
  if (!error || *error == ca::RebuildError::Unavailable) return error;
  return (*error == ca::RebuildError::UnknownJob) != start ? error : std::nullopt;
}

std::optional<ca::PreviewError> decodePreviewError(const Value& value) {
  return rebuildToken(&value, ca::PreviewError::RenderFailed);
}

std::optional<ca::MeshMaterialPreviewError> decodeMeshMaterialPreviewError(const Value& value) {
  return rebuildToken(&value, ca::MeshMaterialPreviewError::RenderFailed);
}

Value encode(const ca::ImportJob& job) {
  Value out = Value::object();
  out.set("id", encodeAsset(job.id.value));
  out.set("name", Value::string(job.name));
  out.set("mode", Value::string(std::string(ca::toString(job.mode))));
  out.set("state", Value::string(std::string(ca::toString(job.state))));
  if (job.rootGuid) out.set("rootGuid", encodeAsset(*job.rootGuid));
  if (job.generation) out.set("generation", encodeAsset(job.generation->value));
  if (job.failure) {
    const auto& failure = *job.failure;
    Value f = Value::object();
    f.set("stage", Value::string(std::string(ca::toString(failure.stage))));
    f.set("code", Value::string(std::string(ca::toString(failure.code))));
    f.set("diagnostic", Value::string(failure.diagnostic));
    f.set("registration", Value::string(std::string(ca::toString(failure.registration))));
    if (failure.exitCode) f.set("exitCode", Value::number(static_cast<std::uint64_t>(*failure.exitCode)));
    if (failure.registeredRootGuid) f.set("registeredRootGuid", encodeAsset(*failure.registeredRootGuid));
    out.set("failure", std::move(f));
  }
  return out;
}
Value encode(const ca::ImportStatus& status) {
  Value out = Value::object();
  out.set("activeGeneration", status.activeGeneration ? encodeAsset(status.activeGeneration->value) : Value());
  out.set("job", status.job ? encode(*status.job) : Value());
  return out;
}
std::optional<ca::ImportJob> decodeImportJob(const Value& value) {
  const auto id = nonNilAsset(value.find("id"));
  const auto* name = value.find("name");
  const auto mode = rebuildToken(value.find("mode"), ca::ImportMode::Resume);
  const auto state = rebuildToken(value.find("state"), ca::RebuildState::Failed);
  if (!id || !name || !name->isString() || !ca::validImportName(name->asString()) || !mode || !state)
    return std::nullopt;
  const auto* root = value.find("rootGuid");
  const auto* generation = value.find("generation");
  const auto* failure = value.find("failure");
  const bool success = *state == ca::RebuildState::Succeeded;
  if (static_cast<bool>(root) != success || static_cast<bool>(generation) != success ||
      static_cast<bool>(failure) != (*state == ca::RebuildState::Failed)) return std::nullopt;
  ca::ImportJob job{{*id}, name->asString(), *mode, *state, {}, {}, {}};
  if (success) {
    const auto selected = nonNilAsset(generation);
    job.rootGuid = nonNilAsset(root);
    if (!selected || !job.rootGuid) return std::nullopt;
    job.generation = ca::GenerationId{*selected};
  }
  if (failure) {
    const auto stage = rebuildToken(failure->find("stage"), ca::ImportStage::Publication);
    const auto code = rebuildToken(failure->find("code"), ca::ImportFailureCode::TimedOut);
    const auto registration = rebuildToken(failure->find("registration"), ca::RegistrationState::Unknown);
    const auto* diagnostic = failure->find("diagnostic");
    const auto* committed = failure->find("registeredRootGuid");
    const auto registeredRoot = nonNilAsset(committed);
    if (!stage || !code || !ca::validFailure(*stage, *code) || !registration || !diagnostic ||
        !diagnostic->isString() || diagnostic->asString().size() > 4096 ||
        static_cast<bool>(committed) != (*registration == ca::RegistrationState::Committed) ||
        (committed && !registeredRoot)) return std::nullopt;
    std::optional<std::uint32_t> exit;
    if (const auto* raw = failure->find("exitCode")) {
      const auto number = decodeUInt64(*raw);
      if (!number || *number > UINT32_MAX) return std::nullopt;
      exit = static_cast<std::uint32_t>(*number);
    }
    job.failure = ca::ImportFailure{*stage, *code, diagnostic->asString(), exit, *registration, registeredRoot};
  }
  return job;
}
std::optional<ca::ImportStatus> decodeImportStatus(const Value& value) {
  const auto* active = value.find("activeGeneration");
  const auto* job = value.find("job");
  if (!active || !job) return std::nullopt;
  ca::ImportStatus status;
  if (!active->isNull()) {
    const auto guid = nonNilAsset(active);
    if (!guid) return std::nullopt;
    status.activeGeneration = ca::GenerationId{*guid};
  }
  if (!job->isNull() && !(status.job = decodeImportJob(*job))) return std::nullopt;
  return status;
}
Value encode(const ca::TextureImportJob& job) {
  return encode(ca::ImportJob{{job.id.value}, job.name, job.mode, job.state, job.rootGuid, job.generation, job.failure});
}
Value encode(const ca::TextureImportStatus& status) {
  Value out = Value::object();
  out.set("activeGeneration", status.activeGeneration ? encodeAsset(status.activeGeneration->value) : Value());
  out.set("job", status.job ? encode(*status.job) : Value());
  out.set("workspaceNeedsRepair", Value::boolean(status.workspaceNeedsRepair));
  return out;
}
std::optional<ca::TextureImportJob> decodeTextureImportJob(const Value& value) {
  auto job = decodeImportJob(value);
  if (!job || !value.isObject() || value.asObject().size() !=
      (job->state == ca::RebuildState::Running ? 4u : job->state == ca::RebuildState::Succeeded ? 6u : 5u))
    return std::nullopt;
  if (job->failure) {
    const auto* failure = value.find("failure");
    if (!failure->isObject() || failure->asObject().size() !=
        (4u + (job->failure->exitCode ? 1u : 0u) + (job->failure->registeredRootGuid ? 1u : 0u)))
      return std::nullopt;
  }
  return ca::TextureImportJob{{job->id.value}, std::move(job->name), job->mode, job->state,
                              job->rootGuid, job->generation, std::move(job->failure)};
}
std::optional<ca::TextureImportStatus> decodeTextureImportStatus(const Value& value) {
  if (!value.isObject() || value.asObject().size() != 3) return std::nullopt;
  const auto* repair = value.find("workspaceNeedsRepair");
  const auto* job = value.find("job");
  const auto* generation = value.find("activeGeneration");
  if (!repair || !repair->isBool() || !job || !generation) return std::nullopt;
  ca::TextureImportStatus out{{}, {}, repair->asBool()};
  if (!generation->isNull()) {
    auto id = nonNilAsset(generation);
    if (!id) return std::nullopt;
    out.activeGeneration = ca::GenerationId{*id};
  }
  if (!job->isNull() && !(out.job = decodeTextureImportJob(*job))) return std::nullopt;
  return out;
}
std::optional<ca::ImportError> decodeImportError(const Value& value, bool start) {
  const auto error = rebuildToken(&value, ca::ImportError::UnknownJob);
  if (!error || *error == ca::ImportError::Unavailable) return error;
  return (*error == ca::ImportError::UnknownJob) != start ? error : std::nullopt;
}

using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;
using connection::EventFilter;
using connection::EventKind;
using connection::EventKindSet;

namespace {

constexpr std::string_view kNilGuid = "00000000-0000-0000-0000-000000000000";

constexpr std::array kAccessErrors = {
    access::AccessError::UnknownEntity,           access::AccessError::NilGuid,
    access::AccessError::DuplicateGuid,           access::AccessError::UnknownComponentType,
    access::AccessError::ComponentMissing,        access::AccessError::ComponentAlreadyPresent,
    access::AccessError::UnknownField,            access::AccessError::FieldNotEditable,
    access::AccessError::KindMismatch,            access::AccessError::EnumValueOutOfRange,
    access::AccessError::NonFiniteValue,          access::AccessError::LightLimitExceeded,
    access::AccessError::ActiveCameraProtected,
};

constexpr std::array<std::pair<EventKind, std::string_view>, 5> kEventKinds = {{
    {EventKind::EntityCreated, "EntityCreated"},
    {EventKind::EntityDestroyed, "EntityDestroyed"},
    {EventKind::ComponentAdded, "ComponentAdded"},
    {EventKind::ComponentRemoved, "ComponentRemoved"},
    {EventKind::PropertyChanged, "PropertyChanged"},
}};

// Decimal digits only (no sign, no space), in range.
[[nodiscard]] std::optional<std::uint64_t> decimalUInt64(const std::string& text) {
  if (text.empty() || text.size() > 20) return std::nullopt;
  std::uint64_t out = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (out > (UINT64_MAX - digit) / 10) return std::nullopt;
    out = out * 10 + digit;
  }
  return out;
}

[[nodiscard]] const Value* member(const Value& object, std::string_view key) { return object.find(key); }

[[nodiscard]] std::optional<std::string_view> kindOf(const Value& value) {
  const Value* kind = member(value, "kind");
  if (kind == nullptr || !kind->isString()) return std::nullopt;
  return std::string_view(kind->asString());
}

template <std::size_t N>
[[nodiscard]] Value encodeVector(const std::array<float, N>& v) {
  Value out = Value::array();
  for (const float x : v) out.push(encodeFloat(x));
  return out;
}

template <std::size_t N>
[[nodiscard]] std::optional<std::array<float, N>> decodeVector(const Value& value) {
  if (!value.isArray() || value.asArray().size() != N) return std::nullopt;
  std::array<float, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    const auto x = decodeFloat(value.asArray()[i]);
    if (!x) return std::nullopt;
    out[i] = *x;
  }
  return out;
}

[[nodiscard]] std::string_view kindName(schema::TypeKind kind) {
  switch (kind) {
    case schema::TypeKind::Primitive: return "Primitive";
    case schema::TypeKind::Struct: return "Struct";
    case schema::TypeKind::Enum: return "Enum";
  }
  return "?";
}

[[nodiscard]] std::optional<schema::TypeKind> decodeKind(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const auto kind : {schema::TypeKind::Primitive, schema::TypeKind::Struct, schema::TypeKind::Enum}) {
    if (value.asString() == kindName(kind)) return kind;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<schema::PrimitiveKind> decodePrimitive(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const auto kind : {schema::PrimitiveKind::UInt64, schema::PrimitiveKind::Float32,
                          schema::PrimitiveKind::Vec3Float32, schema::PrimitiveKind::Vec4Float32,
                          schema::PrimitiveKind::AssetGuid, schema::PrimitiveKind::EntityGuid}) {
    if (value.asString() == schema::toString(kind)) return kind;
  }
  return std::nullopt;
}

}  // namespace

Value encodeFloat(float value) {
  if (std::isnan(value)) return Value::string("nan");
  if (std::isinf(value)) return Value::string(value > 0 ? "inf" : "-inf");
  return Value::number(value);
}

std::optional<float> decodeFloat(const Value& value) {
  if (value.isString()) {
    if (value.asString() == "nan") return std::nanf("");
    if (value.asString() == "inf") return HUGE_VALF;
    if (value.asString() == "-inf") return -HUGE_VALF;
    return std::nullopt;
  }
  float out = 0.0f;
  if (!value.toFloat(out)) return std::nullopt;
  return out;
}

Value encodeId(std::uint64_t id) {
  std::array<char, 24> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "0x%016llx", static_cast<unsigned long long>(id));
  return Value::string(buffer.data());
}

std::optional<std::uint64_t> decodeId(const Value& value) {
  if (!value.isString()) return std::nullopt;
  const std::string& text = value.asString();
  if (text.size() != 18 || text[0] != '0' || text[1] != 'x') return std::nullopt;
  std::uint64_t out = 0;
  for (std::size_t i = 2; i < text.size(); ++i) {
    const char c = text[i];
    out <<= 4;
    if (c >= '0' && c <= '9') {
      out |= static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      out |= static_cast<std::uint64_t>(c - 'a' + 10);
    } else {
      return std::nullopt;
    }
  }
  return out;
}

Value encodeTypeId(schema::TypeId id) { return encodeId(id.value); }

std::optional<schema::TypeId> decodeTypeId(const Value& value) {
  const auto id = decodeId(value);
  if (!id) return std::nullopt;
  return schema::TypeId{*id};
}

Value encodeEntity(const EntityGuid& entity) {
  return Value::string(entity == EntityGuid{} ? std::string(kNilGuid) : atlantis::asset_system::toString(entity));
}

std::optional<EntityGuid> decodeEntity(const Value& value) {
  if (!value.isString()) return std::nullopt;
  if (value.asString() == kNilGuid) return EntityGuid{};
  auto parsed = atlantis::asset_system::parseEntityGuid(value.asString());
  if (parsed.isErr()) return std::nullopt;
  return parsed.value();
}

Value encodeAsset(const AssetGuid& asset) {
  return Value::string(asset == AssetGuid{} ? std::string(kNilGuid) : atlantis::asset_system::toString(asset));
}

std::optional<AssetGuid> decodeAsset(const Value& value) {
  if (!value.isString()) return std::nullopt;
  if (value.asString() == kNilGuid) return AssetGuid{};
  auto parsed = atlantis::asset_system::parseAssetGuid(value.asString());
  if (parsed.isErr()) return std::nullopt;
  return parsed.value();
}

std::optional<std::uint64_t> decodeUInt64(const Value& value) {
  std::uint64_t out = 0;
  if (!value.toUInt64(out)) return std::nullopt;
  return out;
}

Value encodeAccessError(access::AccessError error) { return Value::string(std::string(access::toString(error))); }

std::optional<access::AccessError> decodeAccessError(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const access::AccessError error : kAccessErrors) {
    if (value.asString() == access::toString(error)) return error;
  }
  return std::nullopt;
}

Value encodeConnectionError(connection::ConnectionError error) {
  return Value::string(std::string(connection::toString(error)));
}

std::optional<connection::ConnectionError> decodeConnectionError(const Value& value) {
  if (value.isString() && value.asString() == connection::toString(connection::ConnectionError::UnknownSubscription)) {
    return connection::ConnectionError::UnknownSubscription;
  }
  return std::nullopt;
}

Value encode(const access::PropertyValue& value) {
  return std::visit(
      [](const auto& v) {
        using V = std::decay_t<decltype(v)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<V, std::uint64_t>) {
          out.set("u64", Value::string(std::to_string(v)));
        } else if constexpr (std::is_same_v<V, float>) {
          out.set("f32", encodeFloat(v));
        } else if constexpr (std::is_same_v<V, std::array<float, 3>>) {
          out.set("vec3", encodeVector(v));
        } else if constexpr (std::is_same_v<V, std::array<float, 4>>) {
          out.set("vec4", encodeVector(v));
        } else if constexpr (std::is_same_v<V, AssetGuid>) {
          out.set("assetGuid", encodeAsset(v));
        } else if constexpr (std::is_same_v<V, EntityGuid>) {
          out.set("entityGuid", encodeEntity(v));
        } else if constexpr (std::is_same_v<V, access::EnumValue>) {
          out.set("enum", Value::number(v.value));
        } else {
          static_assert(std::is_same_v<V, access::Absent>);
          out.set("absent", Value::boolean(true));
        }
        return out;
      },
      value);
}

std::optional<access::PropertyValue> decodePropertyValue(const Value& value) {
  if (!value.isObject() || value.asObject().size() != 1) return std::nullopt;
  const auto& [tag, payload] = value.asObject().front();
  if (tag == "u64") {
    const auto number = payload.isString() ? decimalUInt64(payload.asString()) : std::nullopt;
    if (!number) return std::nullopt;
    return access::PropertyValue(*number);
  }
  if (tag == "f32") {
    const auto f = decodeFloat(payload);
    if (!f) return std::nullopt;
    return access::PropertyValue(*f);
  }
  if (tag == "vec3") {
    const auto v = decodeVector<3>(payload);
    if (!v) return std::nullopt;
    return access::PropertyValue(*v);
  }
  if (tag == "vec4") {
    const auto v = decodeVector<4>(payload);
    if (!v) return std::nullopt;
    return access::PropertyValue(*v);
  }
  if (tag == "assetGuid") {
    const auto g = decodeAsset(payload);
    if (!g) return std::nullopt;
    return access::PropertyValue(*g);
  }
  if (tag == "entityGuid") {
    const auto g = decodeEntity(payload);
    if (!g) return std::nullopt;
    return access::PropertyValue(*g);
  }
  if (tag == "enum") {
    std::int64_t out = 0;
    if (!payload.toInt64(out)) return std::nullopt;
    return access::PropertyValue(access::EnumValue{out});
  }
  if (tag == "absent" && payload.isBool() && payload.asBool()) return access::PropertyValue(access::Absent{});
  return std::nullopt;
}

Value encode(const access::PropertyAddress& address) {
  Value out = Value::object();
  out.set("entity", encodeEntity(address.entity));
  out.set("component", encodeTypeId(address.component));
  out.set("field", encodeId(address.field.value));
  return out;
}

std::optional<access::PropertyAddress> decodeAddress(const Value& value) {
  const Value* entity = member(value, "entity");
  const Value* component = member(value, "component");
  const Value* field = member(value, "field");
  if (entity == nullptr || component == nullptr || field == nullptr) return std::nullopt;
  const auto e = decodeEntity(*entity);
  const auto c = decodeTypeId(*component);
  const auto f = decodeId(*field);
  if (!e || !c || !f) return std::nullopt;
  return access::PropertyAddress{*e, *c, schema::FieldId{*f}};
}

Value encode(const access::Command& command) {
  return std::visit(
      [](const auto& c) {
        using C = std::decay_t<decltype(c)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<C, access::CreateEntity>) {
          out.set("kind", Value::string("CreateEntity"));
          out.set("entity", encodeEntity(c.entity));
        } else if constexpr (std::is_same_v<C, access::DestroyEntity>) {
          out.set("kind", Value::string("DestroyEntity"));
          out.set("entity", encodeEntity(c.entity));
        } else if constexpr (std::is_same_v<C, access::AddComponent>) {
          out.set("kind", Value::string("AddComponent"));
          out.set("entity", encodeEntity(c.entity));
          out.set("component", encodeTypeId(c.component));
        } else if constexpr (std::is_same_v<C, access::RemoveComponent>) {
          out.set("kind", Value::string("RemoveComponent"));
          out.set("entity", encodeEntity(c.entity));
          out.set("component", encodeTypeId(c.component));
        } else {
          static_assert(std::is_same_v<C, access::SetProperty>);
          out.set("kind", Value::string("SetProperty"));
          out.set("address", encode(c.address));
          out.set("value", encode(c.value));
        }
        return out;
      },
      command);
}

namespace {

// The entity (and component) of the four entity/component-shaped commands and
// events.
struct EntityComponent {
  EntityGuid entity;
  schema::TypeId component;
};
[[nodiscard]] std::optional<EntityGuid> entityOf(const Value& value) {
  const Value* entity = member(value, "entity");
  if (entity == nullptr) return std::nullopt;
  return decodeEntity(*entity);
}
[[nodiscard]] std::optional<EntityComponent> entityComponentOf(const Value& value) {
  const auto entity = entityOf(value);
  const Value* component = member(value, "component");
  if (!entity || component == nullptr) return std::nullopt;
  const auto c = decodeTypeId(*component);
  if (!c) return std::nullopt;
  return EntityComponent{*entity, *c};
}
[[nodiscard]] std::optional<std::pair<access::PropertyAddress, access::PropertyValue>> addressValueOf(
    const Value& value) {
  const Value* address = member(value, "address");
  const Value* v = member(value, "value");
  if (address == nullptr || v == nullptr) return std::nullopt;
  const auto a = decodeAddress(*address);
  const auto pv = decodePropertyValue(*v);
  if (!a || !pv) return std::nullopt;
  return std::make_pair(*a, *pv);
}

}  // namespace

std::optional<access::Command> decodeCommand(const Value& value) {
  const auto kind = kindOf(value);
  if (!kind) return std::nullopt;
  if (*kind == "CreateEntity" || *kind == "DestroyEntity") {
    const auto entity = entityOf(value);
    if (!entity) return std::nullopt;
    if (*kind == "CreateEntity") return access::Command(access::CreateEntity{*entity});
    return access::Command(access::DestroyEntity{*entity});
  }
  if (*kind == "AddComponent" || *kind == "RemoveComponent") {
    const auto ec = entityComponentOf(value);
    if (!ec) return std::nullopt;
    if (*kind == "AddComponent") return access::Command(access::AddComponent{ec->entity, ec->component});
    return access::Command(access::RemoveComponent{ec->entity, ec->component});
  }
  if (*kind == "SetProperty") {
    const auto av = addressValueOf(value);
    if (!av) return std::nullopt;
    return access::Command(access::SetProperty{av->first, av->second});
  }
  return std::nullopt;
}

Value encode(const access::Event& event) {
  return std::visit(
      [](const auto& e) {
        using E = std::decay_t<decltype(e)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<E, access::EntityCreated>) {
          out.set("kind", Value::string("EntityCreated"));
          out.set("entity", encodeEntity(e.entity));
        } else if constexpr (std::is_same_v<E, access::EntityDestroyed>) {
          out.set("kind", Value::string("EntityDestroyed"));
          out.set("entity", encodeEntity(e.entity));
        } else if constexpr (std::is_same_v<E, access::ComponentAdded>) {
          out.set("kind", Value::string("ComponentAdded"));
          out.set("entity", encodeEntity(e.entity));
          out.set("component", encodeTypeId(e.component));
        } else if constexpr (std::is_same_v<E, access::ComponentRemoved>) {
          out.set("kind", Value::string("ComponentRemoved"));
          out.set("entity", encodeEntity(e.entity));
          out.set("component", encodeTypeId(e.component));
        } else {
          static_assert(std::is_same_v<E, access::PropertyChanged>);
          out.set("kind", Value::string("PropertyChanged"));
          out.set("address", encode(e.address));
          out.set("value", encode(e.value));
        }
        return out;
      },
      event);
}

std::optional<access::Event> decodeEvent(const Value& value) {
  const auto kind = kindOf(value);
  if (!kind) return std::nullopt;
  if (*kind == "EntityCreated" || *kind == "EntityDestroyed") {
    const auto entity = entityOf(value);
    if (!entity) return std::nullopt;
    if (*kind == "EntityCreated") return access::Event(access::EntityCreated{*entity});
    return access::Event(access::EntityDestroyed{*entity});
  }
  if (*kind == "ComponentAdded" || *kind == "ComponentRemoved") {
    const auto ec = entityComponentOf(value);
    if (!ec) return std::nullopt;
    if (*kind == "ComponentAdded") return access::Event(access::ComponentAdded{ec->entity, ec->component});
    return access::Event(access::ComponentRemoved{ec->entity, ec->component});
  }
  if (*kind == "PropertyChanged") {
    const auto av = addressValueOf(value);
    if (!av) return std::nullopt;
    return access::Event(access::PropertyChanged{av->first, av->second});
  }
  return std::nullopt;
}

Value encode(const access::CommandFailure& failure) {
  Value out = Value::object();
  out.set("ticket", Value::number(failure.ticket.value));
  out.set("error", encodeAccessError(failure.error));
  return out;
}

std::optional<access::CommandFailure> decodeFailure(const Value& value) {
  const Value* ticket = member(value, "ticket");
  const Value* error = member(value, "error");
  if (ticket == nullptr || error == nullptr) return std::nullopt;
  const auto t = decodeUInt64(*ticket);
  const auto e = decodeAccessError(*error);
  if (!t || !e) return std::nullopt;
  return access::CommandFailure{access::CommandTicket{*t}, *e};
}

Value encode(const access::TransactionTicket& ticket) {
  Value out = Value::object();
  out.set("first", Value::number(ticket.first.value));
  out.set("count", Value::number(ticket.count));
  return out;
}

std::optional<access::TransactionTicket> decodeTransactionTicket(const Value& value) {
  const Value* first = member(value, "first");
  const Value* count = member(value, "count");
  if (first == nullptr || count == nullptr) return std::nullopt;
  const auto f = decodeUInt64(*first);
  const auto c = decodeUInt64(*count);
  if (!f || !c) return std::nullopt;
  return access::TransactionTicket{access::CommandTicket{*f}, *c};
}

Value encode(const EventFilter& filter) {
  Value kinds = Value::array();
  for (const auto& [kind, name] : kEventKinds) {
    if (filter.kinds.contains(kind)) kinds.push(Value::string(std::string(name)));
  }
  Value out = Value::object();
  out.set("kinds", std::move(kinds));
  out.set("entity", filter.entity ? encodeEntity(*filter.entity) : Value());
  out.set("component", filter.component ? encodeTypeId(*filter.component) : Value());
  return out;
}

std::optional<EventFilter> decodeFilter(const Value& value) {
  const Value* kinds = member(value, "kinds");
  const Value* entity = member(value, "entity");
  const Value* component = member(value, "component");
  if (kinds == nullptr || !kinds->isArray() || entity == nullptr || component == nullptr) return std::nullopt;
  EventFilter filter;
  filter.kinds = EventKindSet();
  for (const Value& name : kinds->asArray()) {
    if (!name.isString()) return std::nullopt;
    bool known = false;
    for (const auto& [kind, kindText] : kEventKinds) {
      if (name.asString() == kindText) {
        filter.kinds = filter.kinds.with(kind);
        known = true;
      }
    }
    if (!known) return std::nullopt;
  }
  if (!entity->isNull()) {
    const auto e = decodeEntity(*entity);
    if (!e) return std::nullopt;
    filter.entity = *e;
  }
  if (!component->isNull()) {
    const auto c = decodeTypeId(*component);
    if (!c) return std::nullopt;
    filter.component = *c;
  }
  return filter;
}

Value ok(Value value) {
  Value out = Value::object();
  out.set("ok", std::move(value));
  return out;
}

Value err(Value name) {
  Value out = Value::object();
  out.set("err", std::move(name));
  return out;
}

Value encodeSchema(std::span<const schema::TypeDescriptor> types) {
  Value out = Value::array();
  for (const schema::TypeDescriptor& type : types) {
    Value fields = Value::array();
    for (const schema::FieldDescriptor& field : type.fields) {
      Value f = Value::object();
      f.set("id", encodeId(field.id.value));
      f.set("name", Value::string(std::string(field.name)));
      f.set("kind", Value::string(std::string(kindName(field.kind))));
      f.set("primitive", Value::string(std::string(schema::toString(field.primitive))));
      f.set("type", encodeTypeId(field.type));
      f.set("flags", Value::number(static_cast<std::uint64_t>(field.flags)));
      f.set("offset", Value::number(static_cast<std::uint64_t>(field.byteOffset)));
      if (!field.targetAssetType.empty()) f.set("targetAssetType", Value::string(std::string(field.targetAssetType)));
      fields.push(std::move(f));
    }
    Value constants = Value::array();
    for (const schema::EnumConstantDescriptor& constant : type.constants) {
      Value c = Value::object();
      c.set("name", Value::string(std::string(constant.name)));
      c.set("value", Value::number(constant.value));
      constants.push(std::move(c));
    }
    Value t = Value::object();
    t.set("id", encodeTypeId(type.id));
    t.set("name", Value::string(std::string(type.name)));
    t.set("kind", Value::string(std::string(kindName(type.kind))));
    t.set("version", Value::number(static_cast<std::uint64_t>(type.schemaVersion)));
    t.set("fields", std::move(fields));
    t.set("constants", std::move(constants));
    out.push(std::move(t));
  }
  return out;
}

bool OwnedSchema::decode(const Value& value) {
  strings_.clear();
  fields_.clear();
  constants_.clear();
  types_.clear();
  if (!value.isArray()) return false;
  const auto keep = [&](const std::string& text) -> std::string_view { return strings_.emplace_back(text); };
  for (const Value& t : value.asArray()) {
    const Value* id = member(t, "id");
    const Value* name = member(t, "name");
    const Value* kind = member(t, "kind");
    const Value* version = member(t, "version");
    const Value* fields = member(t, "fields");
    const Value* constants = member(t, "constants");
    if (!id || !name || !name->isString() || !kind || !version || !fields || !fields->isArray() || !constants ||
        !constants->isArray()) {
      return false;
    }
    schema::TypeDescriptor type;
    const auto typeId = decodeTypeId(*id);
    const auto typeKind = decodeKind(*kind);
    const auto typeVersion = decodeUInt64(*version);
    if (!typeId || !typeKind || !typeVersion) return false;
    type.id = *typeId;
    type.name = keep(name->asString());
    type.kind = *typeKind;
    type.schemaVersion = static_cast<std::uint32_t>(*typeVersion);
    std::vector<schema::FieldDescriptor>& fieldTable = fields_.emplace_back();
    for (const Value& f : fields->asArray()) {
      const Value* fid = member(f, "id");
      const Value* fname = member(f, "name");
      const Value* fkind = member(f, "kind");
      const Value* fprimitive = member(f, "primitive");
      const Value* ftype = member(f, "type");
      const Value* fflags = member(f, "flags");
      const Value* foffset = member(f, "offset");
      if (!fid || !fname || !fname->isString() || !fkind || !fprimitive || !ftype || !fflags || !foffset) return false;
      const auto fieldId = decodeId(*fid);
      const auto fieldKind = decodeKind(*fkind);
      const auto primitive = decodePrimitive(*fprimitive);
      const auto fieldType = decodeTypeId(*ftype);
      const auto flags = decodeUInt64(*fflags);
      const auto offset = decodeUInt64(*foffset);
      if (!fieldId || !fieldKind || !primitive || !fieldType || !flags || !offset) return false;
      schema::FieldDescriptor field;
      field.id = schema::FieldId{*fieldId};
      field.name = keep(fname->asString());
      field.kind = *fieldKind;
      field.primitive = *primitive;
      field.type = *fieldType;
      field.flags = static_cast<schema::FieldFlags>(*flags);
      field.byteOffset = static_cast<std::uint32_t>(*offset);
      if (const Value* target = member(f, "targetAssetType")) {
        const auto assetType = decodeAssetType(*target);
        if (!assetType || !schema::canDeclareAssetTarget(field)) return false;
        field.targetAssetType = connection::assets::toString(*assetType);
      }
      fieldTable.push_back(field);
    }
    std::vector<schema::EnumConstantDescriptor>& constantTable = constants_.emplace_back();
    for (const Value& c : constants->asArray()) {
      const Value* cname = member(c, "name");
      const Value* cvalue = member(c, "value");
      std::int64_t number = 0;
      if (!cname || !cname->isString() || !cvalue || !cvalue->toInt64(number)) return false;
      constantTable.push_back(schema::EnumConstantDescriptor{keep(cname->asString()), number});
    }
    type.fields = fieldTable;
    type.constants = constantTable;
    types_.push_back(type);
  }
  return true;
}

// Plan 0055 M3: RuntimeControl's value types (P5, P6).

namespace {

constexpr std::array kControlErrors = {
    connection::ControlError::InvalidRequest,
    connection::ControlError::NotRendering,
    connection::ControlError::CaptureFailed,
    connection::ControlError::Stopped,
    connection::ControlError::NoGeometry,
};

constexpr std::array kSeverities = {
    connection::DiagnosticSeverity::Warning,
    connection::DiagnosticSeverity::Error,
    connection::DiagnosticSeverity::Fatal,
};

template <std::size_t N>
[[nodiscard]] Value encodeFloats(const std::array<float, N>& values) {
  return encodeVector(values);
}

[[nodiscard]] std::optional<float> floatMember(const Value& object, std::string_view key) {
  const Value* value = member(object, key);
  if (value == nullptr) return std::nullopt;
  return decodeFloat(*value);
}

template <std::size_t N>
[[nodiscard]] std::optional<std::array<float, N>> floatsMember(const Value& object, std::string_view key) {
  const Value* value = member(object, key);
  if (value == nullptr) return std::nullopt;
  return decodeVector<N>(*value);
}

}  // namespace

Value encodeControlError(connection::ControlError error) {
  return Value::string(std::string(connection::toString(error)));
}

std::optional<connection::ControlError> decodeControlError(const Value& value) {
  if (!value.isString()) return std::nullopt;
  for (const connection::ControlError error : kControlErrors) {
    if (value.asString() == connection::toString(error)) return error;
  }
  return std::nullopt;
}

Value encode(const connection::RuntimeStatus& status) {
  Value out = Value::object();
  out.set("paused", Value::boolean(status.paused));
  out.set("frame", Value::number(status.frame));
  out.set("scene", encodeAsset(status.scene));
  out.set("sceneGeneration", Value::number(status.sceneGeneration));  // Plan 0065 P4 (ADR-0131 D5)
  return out;
}

std::optional<connection::RuntimeStatus> decodeStatus(const Value& value) {
  const Value* paused = member(value, "paused");
  const Value* frame = member(value, "frame");
  const Value* scene = member(value, "scene");
  if (paused == nullptr || !paused->isBool() || frame == nullptr || scene == nullptr) return std::nullopt;
  const auto f = decodeUInt64(*frame);
  const auto s = decodeAsset(*scene);
  if (!f || !s) return std::nullopt;
  // Plan 0065 P4: absent from a Runtime older than the reload; its World never changes.
  std::uint64_t generation = 0;
  if (const Value* g = member(value, "sceneGeneration")) {
    const auto decoded = decodeUInt64(*g);
    if (!decoded) return std::nullopt;
    generation = *decoded;
  }
  return connection::RuntimeStatus{paused->asBool(), *f, *s, generation};
}

Value encode(const connection::StepRequest& request) {
  Value out = Value::object();
  out.set("frames", Value::number(static_cast<std::uint64_t>(request.frames)));
  out.set("image", request.imagePath ? Value::string(*request.imagePath) : Value());
  // Spec 0066 R5 (ADR-0132 D2): captureOverlay is the server's, never carried.
  out.set("withOverlay", Value::boolean(request.withOverlay));
  return out;
}

std::optional<connection::StepRequest> decodeStepRequest(const Value& value) {
  const Value* frames = member(value, "frames");
  const Value* image = member(value, "image");
  if (frames == nullptr || image == nullptr || !(image->isNull() || image->isString())) return std::nullopt;
  const auto n = decodeUInt64(*frames);
  if (!n || *n > UINT32_MAX) return std::nullopt;
  connection::StepRequest request;
  request.frames = static_cast<std::uint32_t>(*n);
  if (image->isString()) request.imagePath = image->asString();
  // Spec 0066 R6: absent from an older client, read as false.
  if (const Value* withOverlay = member(value, "withOverlay"); withOverlay != nullptr) {
    if (!withOverlay->isBool()) return std::nullopt;
    request.withOverlay = withOverlay->asBool();
  }
  return request;
}

Value encode(const connection::ViewportOverlay& overlay) { return connection::overlayToJson(overlay); }

std::optional<connection::ViewportOverlay> decodeViewportOverlay(const Value& value) {
  return connection::overlayFromJson(value);
}

Value encode(const connection::FrameData& data) {
  Value directional = Value::array();
  for (const connection::FrameDirectionalLight& light : data.directionalLights) {
    Value l = Value::object();
    l.set("direction", encodeFloats(light.direction));
    l.set("color", encodeFloats(light.color));
    l.set("intensity", encodeFloat(light.intensity));
    directional.push(std::move(l));
  }
  Value points = Value::array();
  for (const connection::FramePointLight& light : data.pointLights) {
    Value l = Value::object();
    l.set("position", encodeFloats(light.position));
    l.set("color", encodeFloats(light.color));
    l.set("intensity", encodeFloat(light.intensity));
    l.set("range", encodeFloat(light.range));
    points.push(std::move(l));
  }
  Value out = Value::object();
  out.set("directionalLights", std::move(directional));
  out.set("pointLights", std::move(points));
  out.set("view", encodeFloats(data.view));
  out.set("projection", encodeFloats(data.projection));
  out.set("drawItemCount", Value::number(data.drawItemCount));
  if (data.exposureCompensationEv && std::isfinite(*data.exposureCompensationEv))
    out.set("exposureCompensationEv", encodeFloat(*data.exposureCompensationEv));
  return out;
}

std::optional<connection::FrameData> decodeFrameData(const Value& value) {
  const Value* directional = member(value, "directionalLights");
  const Value* points = member(value, "pointLights");
  const Value* drawItems = member(value, "drawItemCount");
  if (directional == nullptr || !directional->isArray() || points == nullptr || !points->isArray() ||
      drawItems == nullptr) {
    return std::nullopt;
  }
  connection::FrameData data;
  for (const Value& l : directional->asArray()) {
    const auto direction = floatsMember<3>(l, "direction");
    const auto color = floatsMember<3>(l, "color");
    const auto intensity = floatMember(l, "intensity");
    if (!direction || !color || !intensity) return std::nullopt;
    data.directionalLights.push_back({*direction, *color, *intensity});
  }
  for (const Value& l : points->asArray()) {
    const auto position = floatsMember<3>(l, "position");
    const auto color = floatsMember<3>(l, "color");
    const auto intensity = floatMember(l, "intensity");
    const auto range = floatMember(l, "range");
    if (!position || !color || !intensity || !range) return std::nullopt;
    data.pointLights.push_back({*position, *color, *intensity, *range});
  }
  const auto view = floatsMember<16>(value, "view");
  const auto projection = floatsMember<16>(value, "projection");
  const auto count = decodeUInt64(*drawItems);
  if (!view || !projection || !count) return std::nullopt;
  data.view = *view;
  data.projection = *projection;
  data.drawItemCount = *count;
  if (const Value* exposure = member(value, "exposureCompensationEv")) {
    data.exposureCompensationEv = decodeFloat(*exposure);
    if (!exposure->isNumber() || !data.exposureCompensationEv || !std::isfinite(*data.exposureCompensationEv))
      return std::nullopt;
  }
  return data;
}

Value encode(const connection::FrameReport& report) {
  Value out = Value::object();
  out.set("frame", Value::number(report.frame));
  out.set("applied", Value::boolean(report.applied));
  out.set("data", encode(report.data));
  if (report.image) {
    Value image = Value::object();
    image.set("path", Value::string(report.image->path));
    image.set("width", Value::number(static_cast<std::uint64_t>(report.image->width)));
    image.set("height", Value::number(static_cast<std::uint64_t>(report.image->height)));
    out.set("image", std::move(image));
  } else {
    out.set("image", Value());
  }
  return out;
}

std::optional<connection::FrameReport> decodeFrameReport(const Value& value) {
  const Value* frame = member(value, "frame");
  const Value* applied = member(value, "applied");
  const Value* data = member(value, "data");
  const Value* image = member(value, "image");
  if (frame == nullptr || applied == nullptr || !applied->isBool() || data == nullptr || image == nullptr) {
    return std::nullopt;
  }
  connection::FrameReport report;
  const auto f = decodeUInt64(*frame);
  auto d = decodeFrameData(*data);
  if (!f || !d) return std::nullopt;
  report.frame = *f;
  report.applied = applied->asBool();
  report.data = std::move(*d);
  if (!image->isNull()) {
    const Value* path = member(*image, "path");
    const Value* width = member(*image, "width");
    const Value* height = member(*image, "height");
    if (path == nullptr || !path->isString() || width == nullptr || height == nullptr) return std::nullopt;
    const auto w = decodeUInt64(*width);
    const auto h = decodeUInt64(*height);
    if (!w || !h || *w > UINT32_MAX || *h > UINT32_MAX) return std::nullopt;
    report.image = connection::CapturedImage{path->asString(), static_cast<std::uint32_t>(*w),
                                             static_cast<std::uint32_t>(*h)};
  }
  return report;
}

Value encode(const connection::DiagnosticBatch& batch) {
  Value entries = Value::array();
  for (const connection::Diagnostic& record : batch.entries) {
    Value r = Value::object();
    r.set("sequence", Value::number(record.sequence));
    r.set("severity", Value::string(std::string(connection::toString(record.severity))));
    r.set("message", Value::string(record.message));
    entries.push(std::move(r));
  }
  Value out = Value::object();
  out.set("entries", std::move(entries));
  out.set("latest", Value::number(batch.latest));
  out.set("dropped", Value::number(batch.dropped));
  return out;
}

std::optional<connection::DiagnosticBatch> decodeDiagnosticBatch(const Value& value) {
  const Value* entries = member(value, "entries");
  const Value* latest = member(value, "latest");
  const Value* dropped = member(value, "dropped");
  if (entries == nullptr || !entries->isArray() || latest == nullptr || dropped == nullptr) return std::nullopt;
  connection::DiagnosticBatch batch;
  for (const Value& r : entries->asArray()) {
    const Value* sequence = member(r, "sequence");
    const Value* severity = member(r, "severity");
    const Value* message = member(r, "message");
    if (sequence == nullptr || severity == nullptr || !severity->isString() || message == nullptr ||
        !message->isString()) {
      return std::nullopt;
    }
    const auto s = decodeUInt64(*sequence);
    std::optional<connection::DiagnosticSeverity> level;
    for (const connection::DiagnosticSeverity candidate : kSeverities) {
      if (severity->asString() == connection::toString(candidate)) level = candidate;
    }
    if (!s || !level) return std::nullopt;
    batch.entries.push_back(connection::Diagnostic{*s, *level, message->asString()});
  }
  const auto l = decodeUInt64(*latest);
  const auto d = decodeUInt64(*dropped);
  if (!l || !d) return std::nullopt;
  batch.latest = *l;
  batch.dropped = *d;
  return batch;
}

// Spec 0064 / ADR-0128 D1, D2: the control camera and the ray-cast.
Value encode(const connection::ViewportCamera& camera) {
  Value out = Value::object();
  out.set("worldMatrix", encodeFloats(camera.worldMatrix));
  out.set("fovYRadians", encodeFloat(camera.fovYRadians));
  out.set("nearZ", encodeFloat(camera.nearZ));
  out.set("farZ", encodeFloat(camera.farZ));
  return out;
}

std::optional<connection::ViewportCamera> decodeViewportCamera(const Value& value) {
  const auto matrix = floatsMember<16>(value, "worldMatrix");
  const auto fov = floatMember(value, "fovYRadians");
  const auto nearZ = floatMember(value, "nearZ");
  const auto farZ = floatMember(value, "farZ");
  if (!matrix || !fov || !nearZ || !farZ) return std::nullopt;
  return connection::ViewportCamera{*matrix, *fov, *nearZ, *farZ};
}

Value encode(const connection::ViewportNavigation& navigation) {
  Value bounds = Value::array();
  for (const auto& bound : navigation.bounds) {
    Value item = Value::object();
    item.set("entity", encodeEntity(bound.entity));
    item.set("kind", Value::string(bound.kind == connection::ViewportBoundsKind::Mesh ? "mesh" : "origin"));
    item.set("min", encodeVector(bound.min));
    item.set("max", encodeVector(bound.max));
    bounds.push(std::move(item));
  }
  Value out = Value::object();
  out.set("sceneGeneration", Value::number(navigation.sceneGeneration));
  out.set("sceneCamera", encode(navigation.sceneCamera));
  out.set("bounds", std::move(bounds));
  return out;
}

Value encode(const connection::ViewportNavigationFailure& failure) {
  Value out = Value::object();
  out.set("code", Value::string(std::string(connection::toString(failure.code))));
  if (failure.target) out.set("target", encodeEntity(*failure.target));
  return out;
}

std::optional<connection::ViewportNavigation> decodeViewportNavigation(const Value& value) {
  const auto* generationValue = value.find("sceneGeneration");
  const auto* cameraValue = value.find("sceneCamera");
  const auto* bounds = value.find("bounds");
  if (!value.isObject() || value.asObject().size() != 3 || !generationValue || !cameraValue || !bounds ||
      !bounds->isArray() || bounds->asArray().size() > connection::kMaxViewportNavigationTargets) return std::nullopt;
  const auto generation = decodeUInt64(*generationValue);
  const auto camera = decodeViewportCamera(*cameraValue);
  const auto finite = [](const auto& items) {
    for (const float value : items) if (!std::isfinite(value)) return false;
    return true;
  };
  if (!generation || !camera || camera->nearZ <= 0 || camera->farZ <= camera->nearZ ||
      camera->fovYRadians <= 0 || camera->fovYRadians >= 3.1415926f || !std::isfinite(camera->nearZ) ||
      !std::isfinite(camera->farZ) || !std::isfinite(camera->fovYRadians) || !finite(camera->worldMatrix)) return std::nullopt;
  connection::ViewportNavigation result{*generation, *camera, {}};
  for (const auto& item : bounds->asArray()) {
    const auto* entityValue = item.find("entity");
    const auto* kind = item.find("kind");
    const auto* minimum = item.find("min");
    const auto* maximum = item.find("max");
    if (!item.isObject() || item.asObject().size() != 4 || !entityValue || !kind || !kind->isString() ||
        !minimum || !maximum) return std::nullopt;
    const bool origin = kind->asString() == "origin";
    const auto entity = decodeEntity(*entityValue);
    const auto low = decodeVector<3>(*minimum);
    const auto high = decodeVector<3>(*maximum);
    if ((!origin && kind->asString() != "mesh") || !entity || !low || !high || !finite(*low) || !finite(*high) ||
        (*low)[0] > (*high)[0] || (*low)[1] > (*high)[1] || (*low)[2] > (*high)[2] ||
        (origin && *low != *high)) return std::nullopt;
    result.bounds.push_back({*entity, origin ? connection::ViewportBoundsKind::Origin
                                            : connection::ViewportBoundsKind::Mesh, *low, *high});
  }
  return result;
}

std::optional<connection::ViewportNavigationFailure> decodeViewportNavigationFailure(const Value& value) {
  using Error = connection::ViewportNavigationError;
  const auto* code = value.find("code");
  const auto* target = value.find("target");
  if (!value.isObject() || value.asObject().size() != (target ? 2u : 1u) || !code || !code->isString()) {
    return std::nullopt;
  }
  for (const auto error : {Error::InvalidRequest, Error::SceneCameraUnavailable, Error::TargetUnavailable,
                           Error::GeometryUnavailable, Error::InvalidBounds, Error::Unsupported}) {
    if (code->asString() != connection::toString(error)) continue;
    const bool targetRequired = error == Error::TargetUnavailable || error == Error::GeometryUnavailable ||
                                error == Error::InvalidBounds;
    const auto entity = target ? decodeEntity(*target) : std::nullopt;
    if (targetRequired != (target != nullptr) || (target && !entity)) return std::nullopt;
    return connection::ViewportNavigationFailure{error, entity};
  }
  return std::nullopt;
}


Value encode(const connection::AlignmentFaces& result) {
  auto faces = Value::array();
  for (const auto& face : result.faces) {
    auto item = Value::object();
    auto triangles = Value::array();
    for (auto triangle : face.triangles) triangles.push(Value::number(std::uint64_t{triangle}));
    auto corners = Value::array();
    for (const auto& corner : face.corners) corners.push(encodeFloats(corner));
    item.set("entity", encodeEntity(face.entity));
    item.set("mesh", encodeAsset(face.mesh));
    item.set("triangles", std::move(triangles));
    item.set("distance", encodeFloat(face.distance));
    item.set("corners", std::move(corners));
    item.set("normal", encodeFloats(face.normal));
    item.set("worldMatrix", encodeFloats(face.worldMatrix));
    faces.push(std::move(item));
  }
  auto out = Value::object();
  out.set("sceneGeneration", Value::number(result.sceneGeneration));
  out.set("faces", std::move(faces));
  return out;
}

Value encode(const connection::AlignmentFaceFailure& failure) {
  auto out = Value::object();
  out.set("code", Value::string(std::string(connection::toString(failure.code))));
  if (failure.rayIndex) out.set("rayIndex", Value::number(std::uint64_t{*failure.rayIndex}));
  return out;
}

std::optional<connection::AlignmentFaceFailure> decodeAlignmentFaceFailure(const Value& value,
    std::size_t rayCount) {
  using E = connection::AlignmentFaceError;
  const auto* code = value.find("code");
  const auto* index = value.find("rayIndex");
  if (!value.isObject() || value.asObject().size() != (index ? 2u : 1u) || !code || !code->isString()) return {};
  for (const auto error : {E::InvalidRequest, E::NoGeometry, E::Miss, E::UnsupportedFace, E::Ambiguous, E::TooLarge, E::Unsupported}) {
    if (code->asString() != connection::toString(error)) continue;
    const bool local = error == E::Miss || error == E::UnsupportedFace || error == E::Ambiguous || error == E::TooLarge;
    const auto ord = index ? decodeUInt64(*index) : std::nullopt;
    if (local != (index != nullptr) || (index && (!ord || *ord >= rayCount))) return {};
    return connection::AlignmentFaceFailure{error, ord ? std::optional{static_cast<std::uint32_t>(*ord)} : std::nullopt};
  }
  return {};
}

namespace {
template <std::size_t N>
std::optional<std::array<float, N>> alignmentVector(const Value* value) {
  if (!value || !value->isArray() || !std::ranges::all_of(value->asArray(), [](const Value& item) {
        return item.isNumber();
      })) return {};
  auto result = decodeVector<N>(*value);
  if (!result || !std::ranges::all_of(*result, [](float item) { return std::isfinite(item); })) return {};
  return result;
}

bool validFaceEvidence(const connection::AlignmentFace& f, const connection::Ray& ray) {
  using V = std::array<double, 3>;
  const auto sub = [](const auto& a, const auto& b) -> V {
    return {static_cast<double>(a[0])-b[0],static_cast<double>(a[1])-b[1],static_cast<double>(a[2])-b[2]};
  };
  const auto dot = [](const auto& a, const auto& b) {
    return static_cast<double>(a[0])*b[0]+static_cast<double>(a[1])*b[1]+static_cast<double>(a[2])*b[2];
  };
  const auto cross = [](const V& a, const V& b) -> V {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
  };
  const auto len = [&](const V& a) { return std::sqrt(dot(a,a)); };
  const auto& m = f.worldMatrix;
  const V x{m[0],m[1],m[2]}, y{m[4],m[5],m[6]}, z{m[8],m[9],m[10]};
  if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1 ||
      !(dot(cross(x,y), z) > 0.9999*len(x)*len(y)*len(z)) ||
      std::abs(dot(x,y)) > 1e-4*len(x)*len(y) || std::abs(dot(y,z)) > 1e-4*len(y)*len(z) ||
      std::abs(dot(z,x)) > 1e-4*len(z)*len(x) ||
      std::abs(dot(f.normal,f.normal)-1) > 1e-4 || dot(f.normal,ray.direction) >= -1e-6) return false;
  V point{};
  for (std::size_t i = 0; i < 3; ++i)
    point[i] = ray.origin[i] + static_cast<double>(ray.direction[i]) * f.distance;
  std::array<V,4> edges{};
  double longest = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    edges[i] = sub(f.corners[(i+1)%4],f.corners[i]);
    longest = std::max(longest,len(edges[i]));
  }
  const double epsilon = std::max(1e-5,1e-5*longest);
  if (!std::isfinite(longest) || !(std::abs(dot(sub(point,f.corners[0]),f.normal)) <= epsilon)) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& a = edges[i];
    const auto& b = edges[(i+1)%4];
    const auto& c = edges[(i+2)%4];
    if (len(a) < std::max(1e-3,100*epsilon) ||
        std::abs(dot(a,b)) > 0.0017453283658983088*len(a)*len(b) ||
        dot(cross(a,b),f.normal) < 0.99999*len(a)*len(b) ||
        std::abs(dot(sub(f.corners[i],f.corners[0]),f.normal)) > epsilon ||
        len({a[0]+c[0],a[1]+c[1],a[2]+c[2]}) > epsilon ||
        !(dot(cross(a,sub(point,f.corners[i])),f.normal) > epsilon*len(a))) return false;
  }
  return true;
}
}

std::optional<connection::AlignmentFaces> decodeAlignmentFaces(const Value& value,
    std::span<const connection::Ray> rays) {
  const auto* n = value.find("sceneGeneration");
  const auto generation = n ? decodeUInt64(*n) : std::nullopt;
  const auto* faces = value.find("faces");
  if (!value.isObject() || value.asObject().size() != 2 || !generation || rays.empty() ||
      rays.size() > connection::kAlignmentRayLimit || !faces || !faces->isArray() ||
      faces->asArray().size() != rays.size()) return {};
  connection::AlignmentFaces out{*generation,{}};
  for (const auto& item : faces->asArray()) {
    const auto* entity = item.find("entity");
    const auto* mesh = item.find("mesh");
    const auto* triangles = item.find("triangles");
    const auto* corners = item.find("corners");
    const auto e = entity ? decodeEntity(*entity) : std::nullopt;
    const auto a = mesh ? decodeAsset(*mesh) : std::nullopt;
    const auto* distanceValue = item.find("distance");
    const auto distance = distanceValue && distanceValue->isNumber() ? decodeFloat(*distanceValue) : std::nullopt;
    const auto normal = alignmentVector<3>(item.find("normal"));
    const auto matrix = alignmentVector<16>(item.find("worldMatrix"));
    if (!item.isObject() || item.asObject().size() != 7 || !e || !a || *e == EntityGuid{} || *a == AssetGuid{} ||
        entity->asString() != encodeEntity(*e).asString() || mesh->asString() != encodeAsset(*a).asString() ||
        !distance || !std::isfinite(*distance) || !(*distance > 0) || !normal || !matrix || !triangles || !triangles->isArray() || triangles->asArray().size() != 2 ||
        !corners || !corners->isArray() || corners->asArray().size() != 4) return {};
    auto first = decodeUInt64(triangles->asArray()[0]);
    auto second = decodeUInt64(triangles->asArray()[1]);
    if (!first || !second || *first >= *second || *second >= connection::kAlignmentTriangleLimit) return {};
    connection::AlignmentFace face{*e,*a,{static_cast<std::uint32_t>(*first),static_cast<std::uint32_t>(*second)},
        *distance,{},*normal,*matrix};
    for (std::size_t i = 0; i < 4; ++i) {
      auto corner = alignmentVector<3>(&corners->asArray()[i]);
      if (!corner) return {};
      face.corners[i] = *corner;
    }
    if (!validFaceEvidence(face, rays[out.faces.size()])) return {};
    out.faces.push_back(std::move(face));
  }
  return out;
}


Value encode(const connection::ViewportStamp& value) {
  Value out = Value::object();
  out.set("sceneGeneration", Value::number(value.sceneGeneration));
  out.set("revision", Value::number(value.revision));
  return out;
}

std::optional<connection::ViewportStamp> decodeViewportStamp(const Value& value) {
  if (!value.isObject()) return {};
  const auto* generation = value.find("sceneGeneration");
  const auto* revision = value.find("revision");
  const auto g = generation ? decodeUInt64(*generation) : std::nullopt;
  const auto r = revision ? decodeUInt64(*revision) : std::nullopt;
  if (!g || !r) return {};
  return connection::ViewportStamp{*g, *r};
}

Value encode(const connection::MaterialBindingPickRequest& value) {
  auto out = Value::object();
  out.set("ray", encode(value.ray));
  out.set("minDistance", Value::number(value.minDistance));
  out.set("maxDistance", Value::number(value.maxDistance));
  out.set("sceneGeneration", Value::number(value.sceneGeneration));
  if (value.viewport) out.set("viewport", encode(*value.viewport));
  return out;
}

std::optional<connection::MaterialBindingPickRequest> decodeMaterialBindingRequest(const Value& value) {
  const auto* ray = value.find("ray");
  const auto* nearDistance = value.find("minDistance");
  const auto* farDistance = value.find("maxDistance");
  const auto* generation = value.find("sceneGeneration");
  const auto* viewport = value.find("viewport");
  if (!value.isObject() || value.asObject().size() != (viewport ? 5 : 4) || !ray || !ray->isObject() ||
      ray->asObject().size() != 2 || !nearDistance || !nearDistance->isNumber() ||
      !farDistance || !farDistance->isNumber() || !generation || !generation->isNumber()) return {};
  const auto rayVector = [&](std::string_view key) {
    const auto* v = ray->find(key);
    return v && v->isArray() && v->asArray().size() == 3 &&
        std::ranges::all_of(v->asArray(), [](const auto& n) { return n.isNumber(); });
  };
  if (!rayVector("origin") || !rayVector("direction")) return {};
  const auto r = decodeRay(*ray);
  const auto n = decodeFloat(*nearDistance);
  const auto f = decodeFloat(*farDistance);
  const auto g = decodeUInt64(*generation);
  const auto stamp = viewport ? decodeViewportStamp(*viewport) : std::nullopt;
  if (!r || !n || !f || !g || (viewport && (!stamp || viewport->asObject().size() != 2 ||
      !viewport->find("sceneGeneration")->isNumber() || !viewport->find("revision")->isNumber()))) return {};
  const connection::MaterialBindingPickRequest request{*r, *n, *f, *g, stamp};
  if (!connection::validMaterialBindingPick(request)) return {};
  return request;
}

Value encode(const connection::MaterialBindingPick& value) {
  auto out = Value::object();
  out.set("sceneGeneration", Value::number(value.sceneGeneration));
  if (value.viewport) out.set("viewport", encode(*value.viewport));
  out.set("entity", encodeEntity(value.entity));
  out.set("mesh", encodeAsset(value.mesh));
  out.set("material", encodeAsset(value.material));
  out.set("distance", Value::number(value.distance));
  out.set("slot", Value::string(std::string(connection::kMaterialBindingSlot)));
  return out;
}

std::optional<connection::MaterialBindingPick> decodeMaterialBindingPick(
    const Value& value, const connection::MaterialBindingPickRequest& request) {
  const auto* generation = value.find("sceneGeneration");
  const auto* viewport = value.find("viewport");
  const auto* entity = value.find("entity");
  const auto* mesh = value.find("mesh");
  const auto* material = value.find("material");
  const auto* distance = value.find("distance");
  const auto* slot = value.find("slot");
  if (!value.isObject() || value.asObject().size() != (viewport ? 7 : 6) ||
      !generation || !generation->isNumber() || !entity || !mesh || !material ||
      !distance || !distance->isNumber() || !slot || !slot->isString() ||
      slot->asString() != connection::kMaterialBindingSlot) return {};
  const auto g = decodeUInt64(*generation);
  const auto stamp = viewport ? decodeViewportStamp(*viewport) : std::nullopt;
  const auto e = decodeEntity(*entity);
  const auto m = decodeAsset(*mesh);
  const auto a = decodeAsset(*material);
  const auto d = decodeFloat(*distance);
  if (!g || *g != request.sceneGeneration || stamp != request.viewport ||
      (viewport && (!stamp || encode(*stamp) != *viewport)) ||
      !e || *e == EntityGuid{} || encodeEntity(*e) != *entity ||
      !m || *m == AssetGuid{} || encodeAsset(*m) != *mesh ||
      !a || *a == AssetGuid{} || encodeAsset(*a) != *material ||
      !d || !std::isfinite(*d) || *d < request.minDistance || *d > request.maxDistance) return {};
  return connection::MaterialBindingPick{*g, stamp, *e, *m, *a, *d};
}

std::optional<connection::MaterialBindingPickError> decodeMaterialBindingPickError(const Value& value) {
  if (!value.isString()) return {};
  using E = connection::MaterialBindingPickError;
  for (unsigned i = 0; i <= static_cast<unsigned>(E::TooLarge); ++i) {
    const auto error = static_cast<E>(i);
    if (value.asString() == connection::toString(error)) return error;
  }
  return {};
}

Value encode(const connection::ViewportPolicy& value) {
  const auto list = [](const connection::ViewportMembers& members) {
    auto out = Value::array();
    for (const auto& item : members) out.push(encodeEntity(item));
    return out;
  };
  auto out = Value::object();
  out.set("hidden", list(value.hidden));
  out.set("locked", list(value.locked));
  out.set("isolated", value.isolated ? list(*value.isolated) : Value{});
  return out;
}

std::optional<connection::ViewportPolicy> decodeViewportPolicy(const Value& value,
    connection::ViewportVisibilityError& error) {
  using E = connection::ViewportVisibilityError;
  error = E::InvalidRequest;
  const auto* hidden = value.find("hidden");
  const auto* locked = value.find("locked");
  const auto* isolated = value.find("isolated");
  if (!value.isObject() || value.asObject().size() != 3 || !hidden || !hidden->isArray() ||
      !locked || !locked->isArray() || !isolated || !(isolated->isNull() || isolated->isArray())) return {};
  const std::size_t count = hidden->asArray().size() + locked->asArray().size() +
      (isolated->isArray() ? isolated->asArray().size() : 0);
  if (count > connection::kViewportVisibilityEntries) {
    error = E::TooLarge;
    return {};
  }
  const auto load = [](const Value& list, connection::ViewportMembers& out) {
    for (const auto& item : list.asArray()) {
      const auto id = decodeEntity(item);
      if (!id || encodeEntity(*id) != item) return false;
      out.push_back(*id);
    }
    return true;
  };
  connection::ViewportPolicy out{};
  if (!load(*hidden, out.hidden) || !load(*locked, out.locked) ||
      (!isolated->isNull() && !load(*isolated, out.isolated.emplace()))) return {};
  if (const auto reason = connection::validateViewportPolicy(out)) {
    error = *reason;
    return {};
  }
  return out;
}

Value encode(const connection::ViewportVisibility& value) {
  auto out = encode(value.stamp);
  out.set("presented", Value::boolean(value.presented));
  out.set("owner", Value::string(value.owner == connection::ViewportOwner::Caller ? "Caller" :
      value.owner == connection::ViewportOwner::Other ? "Other" : "None"));
  out.set("policy", value.policy ? encode(*value.policy) : Value{});
  return out;
}

std::optional<connection::ViewportVisibility> decodeViewportVisibility(const Value& value) {
  using O = connection::ViewportOwner;
  const auto stamp = decodeViewportStamp(value);
  const auto* presented = value.find("presented");
  const auto* owner = value.find("owner");
  const auto* policy = value.find("policy");
  if (!stamp || value.asObject().size() != 5 || !presented || !presented->isBool() ||
      !owner || !owner->isString() || !policy) return {};
  const auto& key = owner->asString();
  if (key == "Caller") {
    connection::ViewportVisibilityError error;
    auto decoded = decodeViewportPolicy(*policy, error);
    if (!decoded) return {};
    return connection::ViewportVisibility{*stamp, presented->asBool(), O::Caller, std::move(decoded)};
  }
  if (!policy->isNull() || (key != "None" && key != "Other")) return {};
  return connection::ViewportVisibility{*stamp, presented->asBool(), key == "None" ? O::None : O::Other, {}};
}

Value encode(const connection::ViewportVisibilityFailure& value) {
  auto out = Value::object();
  out.set("code", Value::string(std::string(connection::toString(value.code))));
  if (value.face) out.set("face", encode(*value.face));
  return out;
}

std::optional<connection::ViewportVisibilityFailure> decodeViewportVisibilityFailure(
    const Value& value, std::size_t rayCount) {
  using E = connection::ViewportVisibilityError;
  const auto* code = value.find("code");
  if (!value.isObject() || !code || !code->isString()) return {};
  for (const auto reason : {E::Unsupported, E::InvalidRequest, E::Unavailable, E::TooLarge, E::NotOwner,
       E::StaleGeneration, E::StaleRevision, E::UnknownMember, E::NotPresented, E::NoGeometry, E::FaceRefused}) {
    if (code->asString() != connection::toString(reason)) continue;
    if (reason != E::FaceRefused) {
      if (value.asObject().size() == 1) return connection::ViewportVisibilityFailure{reason};
      return {};
    }
    const auto* face = value.find("face");
    if (value.asObject().size() != 2 || !face || rayCount == 0) return {};
    const auto detail = decodeAlignmentFaceFailure(*face, rayCount);
    if (detail && detail->rayIndex) return connection::ViewportVisibilityFailure{reason, *detail};
    return {};
  }
  return {};
}

Value encode(const connection::ViewportPick& value) {
  auto out = encode(value.stamp);
  if (const auto* hit = std::get_if<std::optional<connection::RayHit>>(&value.value))
    out.set("hit", *hit ? encode(**hit) : Value{});
  else
    out.set("alignment", encode(std::get<connection::AlignmentFaces>(value.value)));
  return out;
}

std::optional<connection::ViewportPick> decodeViewportPick(const Value& value, connection::ViewportStamp expected,
    connection::ViewportPickKind kind, std::span<const connection::Ray> rays) {
  const auto stamp = decodeViewportStamp(value);
  if (!stamp || *stamp != expected || value.asObject().size() != 3) return {};
  if (kind == connection::ViewportPickKind::Alignment) {
    const auto* faces = value.find("alignment");
    auto decoded = faces ? decodeAlignmentFaces(*faces, rays) : std::nullopt;
    if (!decoded || decoded->sceneGeneration != stamp->sceneGeneration) return {};
    return connection::ViewportPick{*stamp, std::move(*decoded)};
  }
  const auto* hit = value.find("hit");
  if (!hit) return {};
  if (hit->isNull()) return connection::ViewportPick{*stamp, std::optional<connection::RayHit>{}};
  const auto decoded = decodeRayHit(*hit);
  const auto* id = hit->find("entity");
  const auto* distance = hit->find("distance");
  if (!decoded || hit->asObject().size() != 2 || decoded->entity == EntityGuid{} ||
      !id || encodeEntity(decoded->entity) != *id || !distance || !distance->isNumber() ||
      !std::isfinite(decoded->distance) || decoded->distance <= 0) return {};
  return connection::ViewportPick{*stamp, decoded};
}

std::optional<std::vector<connection::Ray>> decodeViewportRays(const Value& value, connection::ViewportPickKind kind) {
  if (!value.isArray() || value.asArray().empty() ||
      value.asArray().size() > (kind == connection::ViewportPickKind::Alignment ? 2u : 1u)) return {};
  std::vector<connection::Ray> out;
  for (const auto& item : value.asArray()) {
    if (!item.isObject() || item.asObject().size() != 2) return {};
    const auto origin = alignmentVector<3>(item.find("origin"));
    const auto direction = alignmentVector<3>(item.find("direction"));
    if (!origin || !direction) return {};
    const connection::Ray ray{*origin, *direction};
    if (!connection::validAlignmentRay(ray)) return {};
    out.push_back(ray);
  }
  return out;
}

Value encode(const connection::Ray& ray) {
  Value out = Value::object();
  out.set("origin", encodeFloats(ray.origin));
  out.set("direction", encodeFloats(ray.direction));
  return out;
}

std::optional<connection::Ray> decodeRay(const Value& value) {
  const auto origin = floatsMember<3>(value, "origin");
  const auto direction = floatsMember<3>(value, "direction");
  if (!origin || !direction) return std::nullopt;
  return connection::Ray{*origin, *direction};
}

Value encode(const connection::RayHit& hit) {
  Value out = Value::object();
  out.set("entity", encodeEntity(hit.entity));
  out.set("distance", encodeFloat(hit.distance));
  return out;
}

std::optional<connection::RayHit> decodeRayHit(const Value& value) {
  const Value* entity = member(value, "entity");
  const auto distance = floatMember(value, "distance");
  if (entity == nullptr || !distance) return std::nullopt;
  const auto e = decodeEntity(*entity);
  if (!e) return std::nullopt;
  return connection::RayHit{*e, *distance};
}

// --- Plan 0065 P6: AuthoringConnection's values ---------------------------------

namespace {

constexpr std::array kAuthoringErrors = {
    authoring::Error::NoDocument,           authoring::Error::UnknownNode,
    authoring::Error::UnknownComponentType, authoring::Error::ComponentMissing,
    authoring::Error::ComponentAlreadyPresent, authoring::Error::ComponentRequired,
    authoring::Error::UnknownField,         authoring::Error::KindMismatch,
    authoring::Error::EnumValueOutOfRange,  authoring::Error::NodePositionOutOfRange,
    authoring::Error::FieldOutOfDomain,     authoring::Error::NonEmptyDocument,
    authoring::Error::NodeGuidNonNil,       authoring::Error::NodeGuidUnique,
    authoring::Error::ParentExists,         authoring::Error::ParentAcyclic,
    authoring::Error::ActiveCameraExists,   authoring::Error::ActiveCameraHasCamera,
    authoring::Error::ComponentExclusivity, authoring::Error::LightRangeMatchesKind,
    authoring::Error::MaxDirectionalLights, authoring::Error::MaxPointLights,
    authoring::Error::CookRefused,          authoring::Error::NotRepresentableInSource,
    authoring::Error::InvalidNodeName,
    authoring::Error::SiblingOrderStaleRevision, authoring::Error::SiblingOrderRequiresHandAuthored,
    authoring::Error::InvalidSiblingSwap, authoring::Error::InvalidSiblingMove,
    authoring::Error::InvalidGuardedPropertyBatch, authoring::Error::PropertyBatchStaleRevision,
    authoring::Error::InvalidGuardedDuplicateBatch, authoring::Error::DuplicateBatchStaleRevision,
    authoring::Error::InvalidGuardedNameBatch, authoring::Error::NameBatchStaleDocument,
    authoring::Error::InvalidGuardedRenderLook, authoring::Error::RenderLookStaleDocument,
    authoring::Error::RenderLookTargetChanged,
};
constexpr std::array kOpenErrors = {authoring::OpenError::SourceUnreadable, authoring::OpenError::SourceParseFailed,
                                    authoring::OpenError::SourceInvalid};
constexpr std::array kSaveErrors = {authoring::SaveError::NoDocument, authoring::SaveError::ImportedSceneNotSavable,
                                    authoring::SaveError::WriteFailed};
constexpr std::array kApplyErrors = {authoring::ApplyError::NoDocument, authoring::ApplyError::NoSceneSink,
                                     authoring::ApplyError::CookFailed, authoring::ApplyError::DecodeFailed,
                                     authoring::ApplyError::ReloadRefused};

constexpr std::array<std::pair<authoring::EventKind, std::string_view>, 12> kAuthoringEventKinds = {{
    {authoring::EventKind::PropertyChanged, "PropertyChanged"},
    {authoring::EventKind::NodeCreated, "NodeCreated"},
    {authoring::EventKind::NodeDestroyed, "NodeDestroyed"},
    {authoring::EventKind::ComponentAdded, "ComponentAdded"},
    {authoring::EventKind::ComponentRemoved, "ComponentRemoved"},
    {authoring::EventKind::ParentChanged, "ParentChanged"},
    {authoring::EventKind::ActiveCameraChanged, "ActiveCameraChanged"},
    {authoring::EventKind::DocumentReopened, "DocumentReopened"},
    {authoring::EventKind::Applied, "Applied"},
    {authoring::EventKind::NodeNameChanged, "NodeNameChanged"},
    {authoring::EventKind::NodeOrderChanged, "NodeOrderChanged"},
    {authoring::EventKind::NodeMovedInSiblingOrder, "NodeMovedInSiblingOrder"},
}};

// Remote checks name values without depending on the scene schema module.
[[nodiscard]] bool validNodeName(std::string_view text) {
  if (text.size() > 255) return false;
  for (std::size_t at = 0; at < text.size();) {
    const auto first = static_cast<unsigned char>(text[at++]);
    std::uint32_t cp = first;
    std::uint32_t minimum = 0;
    unsigned more = 0;
    if (first >= 0xC2 && first <= 0xDF) { cp = first & 0x1F; minimum = 0x80; more = 1; }
    else if (first >= 0xE0 && first <= 0xEF) { cp = first & 0x0F; minimum = 0x800; more = 2; }
    else if (first >= 0xF0 && first <= 0xF4) { cp = first & 0x07; minimum = 0x10000; more = 3; }
    else if (first >= 0x80) return false;
    if (text.size() - at < more) return false;
    for (unsigned i = 0; i < more; ++i) {
      const auto byte = static_cast<unsigned char>(text[at++]);
      if ((byte & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (byte & 0x3F);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029) return false;
  }
  return true;
}

template <typename E, std::size_t N>
[[nodiscard]] std::optional<E> enumeratorNamed(const Value& value, const std::array<E, N>& all) {
  if (!value.isString()) return std::nullopt;
  for (const E e : all) {
    if (value.asString() == authoring::toString(e)) return e;
  }
  return std::nullopt;
}

// An EntityGuid or null.
[[nodiscard]] Value encodeOptionalEntity(const std::optional<EntityGuid>& entity) {
  return entity ? encodeEntity(*entity) : Value();
}
// Present and either null or a GUID; nullopt (outer) if malformed.
[[nodiscard]] std::optional<std::optional<EntityGuid>> optionalEntityMember(const Value& value, std::string_view key) {
  const Value* member = value.find(key);
  if (member == nullptr) return std::nullopt;
  if (member->isNull()) return std::optional<EntityGuid>{};
  const auto entity = decodeEntity(*member);
  if (!entity) return std::nullopt;
  return std::optional<EntityGuid>{*entity};
}
[[nodiscard]] std::optional<EntityGuid> nodeMember(const Value& value) {
  const Value* node = value.find("node");
  if (node == nullptr) return std::nullopt;
  return decodeEntity(*node);
}
[[nodiscard]] std::optional<schema::TypeId> componentMember(const Value& value) {
  const Value* component = value.find("component");
  if (component == nullptr) return std::nullopt;
  return decodeTypeId(*component);
}
[[nodiscard]] std::optional<std::pair<authoring::PropertyAddress, access::PropertyValue>> sceneAddressValueOf(
    const Value& value) {
  const Value* address = value.find("address");
  const Value* v = value.find("value");
  if (address == nullptr || v == nullptr) return std::nullopt;
  const auto a = decodeSceneAddress(*address);
  const auto pv = decodePropertyValue(*v);
  if (!a || !pv) return std::nullopt;
  return std::make_pair(*a, *pv);
}

}  // namespace

Value encode(const authoring::PropertyAddress& address) {
  Value out = Value::object();
  out.set("node", encodeEntity(address.node));
  out.set("component", encodeTypeId(address.component));
  out.set("field", encodeId(address.field.value));
  return out;
}

std::optional<authoring::PropertyAddress> decodeSceneAddress(const Value& value) {
  const auto node = nodeMember(value);
  const auto component = componentMember(value);
  const Value* field = value.find("field");
  if (!node || !component || field == nullptr) return std::nullopt;
  const auto f = decodeId(*field);
  if (!f) return std::nullopt;
  return authoring::PropertyAddress{*node, *component, schema::FieldId{*f}};
}

Value encode(const authoring::Command& command) {
  return std::visit(
      [](const auto& c) {
        using C = std::decay_t<decltype(c)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<C, authoring::SetProperty>) {
          out.set("kind", Value::string("SetProperty"));
          out.set("address", encode(c.address));
          out.set("value", encode(c.value));
        } else if constexpr (std::is_same_v<C, authoring::CreateNode>) {
          out.set("kind", Value::string("CreateNode"));
          out.set("node", encodeOptionalEntity(c.node));
          out.set("position", Value::number(c.position));
          Value components = Value::array();
          for (const schema::TypeId component : c.components) components.push(encodeTypeId(component));
          out.set("components", std::move(components));
        } else if constexpr (std::is_same_v<C, authoring::DestroyNode>) {
          out.set("kind", Value::string("DestroyNode"));
          out.set("node", encodeEntity(c.node));
        } else if constexpr (std::is_same_v<C, authoring::AddComponent> ||
                             std::is_same_v<C, authoring::RemoveComponent>) {
          out.set("kind", Value::string(std::is_same_v<C, authoring::AddComponent> ? "AddComponent" : "RemoveComponent"));
          out.set("node", encodeEntity(c.node));
          out.set("component", encodeTypeId(c.component));
        } else if constexpr (std::is_same_v<C, authoring::SetParent>) {
          out.set("kind", Value::string("SetParent"));
          out.set("node", encodeEntity(c.node));
          out.set("parent", encodeOptionalEntity(c.parent));
        } else if constexpr (std::is_same_v<C, authoring::SetPrefabOrigin>) {
          out.set("kind", Value::string("SetPrefabOrigin"));
          out.set("node", encodeEntity(c.node));
          out.set("prefab", encodeAsset(c.prefab));
        } else if constexpr (std::is_same_v<C, authoring::SetNodeName>) {
          out.set("kind", Value::string("SetNodeName"));
          out.set("node", encodeEntity(c.node));
          out.set("name", Value::string(c.name));
        } else if constexpr (std::is_same_v<C, authoring::SwapAdjacentSiblings>) {
          out.set("kind", Value::string("SwapAdjacentSiblings"));
          out.set("node", encodeEntity(c.node));
          out.set("sibling", encodeEntity(c.sibling));
          out.set("parent", encodeOptionalEntity(c.parent));
          out.set("direction", Value::string(c.direction == authoring::SiblingDirection::Up ? "Up" : "Down"));
          out.set("expectedRevision", Value::number(c.expectedRevision));
        } else if constexpr (std::is_same_v<C, authoring::MoveSiblingRelative>) {
          out.set("kind", Value::string("MoveSiblingRelative"));
          out.set("node", encodeEntity(c.node));
          out.set("anchor", encodeEntity(c.anchor));
          out.set("parent", encodeOptionalEntity(c.parent));
          out.set("placement", Value::string(c.placement == authoring::SiblingPlacement::Before ? "Before" : "After"));
          out.set("expectedRevision", Value::number(c.expectedRevision));
        } else if constexpr (std::is_same_v<C, authoring::RequireTransformBatchRevision>) {
          out.set("kind", Value::string("RequireTransformBatchRevision"));
          out.set("expectedRevision", Value::number(c.expectedRevision));
        } else if constexpr (std::is_same_v<C, authoring::RequireNameBatchDocument>) {
          out.set("kind", Value::string("RequireNameBatchDocument"));
          out.set("expectedDocument", encode(c.expectedDocument));
        } else if constexpr (std::is_same_v<C, authoring::RequireRenderLookDocument>) {
          out.set("kind", Value::string("RequireRenderLookDocument"));
          out.set("expectedDocument", encode(c.expectedDocument));
          out.set("activeCamera", encodeEntity(c.basis.activeCamera));
          Value roster = Value::array();
          for (const auto& light : c.basis.lightRoster) roster.push(encodeEntity(light));
          out.set("lightRoster", std::move(roster));
          out.set("placementSignature", Value::string(c.basis.placementSignature));
        } else if constexpr (std::is_same_v<C, authoring::RequireDuplicateBatchRevision>) {
          out.set("kind", Value::string("RequireDuplicateBatchRevision"));
          out.set("expectedRevision", Value::number(c.expectedRevision));
        } else if constexpr (std::is_same_v<C, authoring::RequireDocumentRevision>) {
          out.set("kind", Value::string("RequireDocumentRevision"));
          out.set("expectedRevision", Value::number(c.expectedRevision));
        } else {
          static_assert(std::is_same_v<C, authoring::SetActiveCamera>);
          out.set("kind", Value::string("SetActiveCamera"));
          out.set("camera", encodeOptionalEntity(c.camera));
        }
        return out;
      },
      command);
}

std::optional<authoring::Command> decodeAuthoringCommand(const Value& value) {
  const auto kind = kindOf(value);
  if (!kind) return std::nullopt;
  if (*kind == "SetProperty") {
    const auto av = sceneAddressValueOf(value);
    if (!av) return std::nullopt;
    return authoring::Command(authoring::SetProperty{av->first, av->second});
  }
  if (*kind == "CreateNode") {
    const auto node = optionalEntityMember(value, "node");
    const Value* position = value.find("position");
    const Value* components = value.find("components");
    if (!node || position == nullptr || components == nullptr || !components->isArray()) return std::nullopt;
    const auto p = decodeUInt64(*position);
    if (!p) return std::nullopt;
    authoring::CreateNode create{*node, *p, {}};
    for (const Value& item : components->asArray()) {
      const auto component = decodeTypeId(item);
      if (!component) return std::nullopt;
      create.components.push_back(*component);
    }
    return authoring::Command(std::move(create));
  }
  if (*kind == "DestroyNode") {
    const auto node = nodeMember(value);
    if (!node) return std::nullopt;
    return authoring::Command(authoring::DestroyNode{*node});
  }
  if (*kind == "AddComponent" || *kind == "RemoveComponent") {
    const auto node = nodeMember(value);
    const auto component = componentMember(value);
    if (!node || !component) return std::nullopt;
    if (*kind == "AddComponent") return authoring::Command(authoring::AddComponent{*node, *component});
    return authoring::Command(authoring::RemoveComponent{*node, *component});
  }
  if (*kind == "SetParent") {
    const auto node = nodeMember(value);
    const auto parent = optionalEntityMember(value, "parent");
    if (!node || !parent) return std::nullopt;
    return authoring::Command(authoring::SetParent{*node, *parent});
  }
  if (*kind == "SetPrefabOrigin") {
    const auto node = nodeMember(value);
    const auto* field = value.find("prefab");
    const auto prefab = field ? decodeAsset(*field) : std::nullopt;
    if (!node || !prefab || *prefab == asset_system::AssetGuid{}) return std::nullopt;
    return authoring::Command(authoring::SetPrefabOrigin{*node, *prefab});
  }
  if (*kind == "SetNodeName") {
    const auto node = nodeMember(value);
    const Value* name = value.find("name");
    if (!node || name == nullptr || !name->isString()) return std::nullopt;
    // Domain errors keep the authoring transaction's ticket/refusal semantics.
    return authoring::Command(authoring::SetNodeName{*node, name->asString()});
  }
  if (*kind == "SwapAdjacentSiblings") {
    const auto node = nodeMember(value);
    const auto sibling = optionalEntityMember(value, "sibling");
    const auto parent = optionalEntityMember(value, "parent");
    const Value* direction = value.find("direction");
    const Value* revision = value.find("expectedRevision");
    if (!node || !sibling || !*sibling || !parent || !direction || !direction->isString() || !revision)
      return std::nullopt;
    const auto d = direction->asString();
    const auto r = decodeUInt64(*revision);
    if (!r || (d != "Up" && d != "Down")) return std::nullopt;
    return authoring::Command(authoring::SwapAdjacentSiblings{
        *node, **sibling, *parent, d == "Up" ? authoring::SiblingDirection::Up : authoring::SiblingDirection::Down, *r});
  }
  if (*kind == "MoveSiblingRelative") {
    const auto node = nodeMember(value);
    const auto anchor = optionalEntityMember(value, "anchor");
    const auto parent = optionalEntityMember(value, "parent");
    const Value* placement = value.find("placement");
    const Value* revision = value.find("expectedRevision");
    if (!node || !anchor || !*anchor || !parent || !placement || !placement->isString() || !revision)
      return std::nullopt;
    const auto p = placement->asString();
    const auto r = decodeUInt64(*revision);
    if (!r || (p != "Before" && p != "After")) return std::nullopt;
    return authoring::Command(authoring::MoveSiblingRelative{
        *node, **anchor, *parent, p == "Before" ? authoring::SiblingPlacement::Before : authoring::SiblingPlacement::After, *r});
  }
  if (*kind == "RequireTransformBatchRevision") {
    const auto* revision = value.find("expectedRevision");
    const auto expected = revision ? decodeUInt64(*revision) : std::nullopt;
    if (!expected) return std::nullopt;
    return authoring::Command(authoring::RequireTransformBatchRevision{*expected});
  }
  if (*kind == "RequireNameBatchDocument") {
    const auto* token = value.find("expectedDocument");
    if (value.asObject().size() != 2 || !token || !token->isObject() || token->asObject().size() != 2)
      return std::nullopt;
    const auto expected = decodeDocumentToken(*token);
    return expected ? std::optional<authoring::Command>{authoring::RequireNameBatchDocument{*expected}} : std::nullopt;
  }
  if (*kind == "RequireRenderLookDocument") {
    const auto* token = value.find("expectedDocument");
    const auto* camera = value.find("activeCamera");
    const auto* roster = value.find("lightRoster");
    const auto* signature = value.find("placementSignature");
    if (value.asObject().size() != 5 || !token || !token->isObject() || token->asObject().size() != 2 ||
        !camera || !roster || !roster->isArray() || roster->asArray().size() > authoring::kMaxRenderLookLights ||
        !signature || !signature->isString() || signature->asString().empty() ||
        signature->asString().size() > authoring::kMaxRenderLookPlacementBytes) return std::nullopt;
    const auto expected = decodeDocumentToken(*token);
    const auto active = decodeEntity(*camera);
    if (!expected || !active) return std::nullopt;
    authoring::RenderLookBasis basis{*active, {}, std::string(signature->asString())};
    for (const auto& entry : roster->asArray()) {
      const auto light = decodeEntity(entry);
      if (!light || *light == *active || (!basis.lightRoster.empty() && basis.lightRoster.back() >= *light))
        return std::nullopt;
      basis.lightRoster.push_back(*light);
    }
    return authoring::Command(authoring::RequireRenderLookDocument{*expected, std::move(basis)});
  }
  if (*kind == "RequireDuplicateBatchRevision") {
    const auto* revision = value.find("expectedRevision");
    const auto expected = revision ? decodeUInt64(*revision) : std::nullopt;
    if (!expected) return std::nullopt;
    return authoring::Command(authoring::RequireDuplicateBatchRevision{*expected});
  }
  if (*kind == "RequireDocumentRevision") {
    const auto* revision = value.find("expectedRevision");
    const auto expected = revision ? decodeUInt64(*revision) : std::nullopt;
    if (!expected) return std::nullopt;
    return authoring::Command(authoring::RequireDocumentRevision{*expected});
  }
  if (*kind == "SetActiveCamera") {
    const auto camera = optionalEntityMember(value, "camera");
    if (!camera) return std::nullopt;
    return authoring::Command(authoring::SetActiveCamera{*camera});
  }
  return std::nullopt;
}

Value encode(const authoring::Event& event) {
  return std::visit(
      [](const auto& e) {
        using E = std::decay_t<decltype(e)>;
        Value out = Value::object();
        if constexpr (std::is_same_v<E, authoring::PropertyChanged>) {
          out.set("kind", Value::string("PropertyChanged"));
          out.set("address", encode(e.address));
          out.set("value", encode(e.value));
        } else if constexpr (std::is_same_v<E, authoring::NodeCreated>) {
          out.set("kind", Value::string("NodeCreated"));
          out.set("node", encodeEntity(e.node));
          out.set("position", Value::number(e.position));
          if (e.prefabOrigin) out.set("prefabOrigin", encodeAsset(*e.prefabOrigin));
        } else if constexpr (std::is_same_v<E, authoring::NodeDestroyed>) {
          out.set("kind", Value::string("NodeDestroyed"));
          out.set("node", encodeEntity(e.node));
        } else if constexpr (std::is_same_v<E, authoring::ComponentAdded> ||
                             std::is_same_v<E, authoring::ComponentRemoved>) {
          out.set("kind",
                  Value::string(std::is_same_v<E, authoring::ComponentAdded> ? "ComponentAdded" : "ComponentRemoved"));
          out.set("node", encodeEntity(e.node));
          out.set("component", encodeTypeId(e.component));
        } else if constexpr (std::is_same_v<E, authoring::ParentChanged>) {
          out.set("kind", Value::string("ParentChanged"));
          out.set("node", encodeEntity(e.node));
          out.set("parent", encodeOptionalEntity(e.parent));
        } else if constexpr (std::is_same_v<E, authoring::ActiveCameraChanged>) {
          out.set("kind", Value::string("ActiveCameraChanged"));
          out.set("camera", encodeOptionalEntity(e.camera));
        } else if constexpr (std::is_same_v<E, authoring::DocumentReopened>) {
          out.set("kind", Value::string("DocumentReopened"));
          out.set("scene", encodeAsset(e.scene));
        } else if constexpr (std::is_same_v<E, authoring::NodeNameChanged>) {
          out.set("kind", Value::string("NodeNameChanged"));
          out.set("node", encodeEntity(e.node));
          out.set("name", Value::string(e.name));
        } else if constexpr (std::is_same_v<E, authoring::NodeOrderChanged>) {
          out.set("kind", Value::string("NodeOrderChanged"));
          out.set("node", encodeEntity(e.node));
          out.set("sibling", encodeEntity(e.sibling));
          out.set("position", Value::number(e.position));
          out.set("siblingPosition", Value::number(e.siblingPosition));
          out.set("revision", Value::number(e.revision));
        } else if constexpr (std::is_same_v<E, authoring::NodeMovedInSiblingOrder>) {
          out.set("kind", Value::string("NodeMovedInSiblingOrder"));
          out.set("node", encodeEntity(e.node));
          out.set("parent", encodeOptionalEntity(e.parent));
          out.set("fromPosition", Value::number(e.fromPosition));
          out.set("position", Value::number(e.position));
          out.set("revision", Value::number(e.revision));
        } else {
          static_assert(std::is_same_v<E, authoring::Applied>);
          out.set("kind", Value::string("Applied"));
          out.set("generation", Value::number(e.generation));
        }
        return out;
      },
      event);
}

std::optional<authoring::Event> decodeAuthoringEvent(const Value& value) {
  const auto kind = kindOf(value);
  if (!kind) return std::nullopt;
  if (*kind == "PropertyChanged") {
    const auto av = sceneAddressValueOf(value);
    if (!av) return std::nullopt;
    return authoring::Event(authoring::PropertyChanged{av->first, av->second});
  }
  if (*kind == "NodeCreated") {
    const auto node = nodeMember(value);
    const Value* position = value.find("position");
    if (!node || position == nullptr) return std::nullopt;
    const auto p = decodeUInt64(*position);
    if (!p) return std::nullopt;
    const auto* field = value.find("prefabOrigin");
    const auto prefab = field ? decodeAsset(*field) : std::nullopt;
    if (field && (!prefab || *prefab == asset_system::AssetGuid{})) return std::nullopt;
    return authoring::Event(authoring::NodeCreated{*node, *p, prefab});
  }
  if (*kind == "NodeDestroyed") {
    const auto node = nodeMember(value);
    if (!node) return std::nullopt;
    return authoring::Event(authoring::NodeDestroyed{*node});
  }
  if (*kind == "ComponentAdded" || *kind == "ComponentRemoved") {
    const auto node = nodeMember(value);
    const auto component = componentMember(value);
    if (!node || !component) return std::nullopt;
    if (*kind == "ComponentAdded") return authoring::Event(authoring::ComponentAdded{*node, *component});
    return authoring::Event(authoring::ComponentRemoved{*node, *component});
  }
  if (*kind == "ParentChanged") {
    const auto node = nodeMember(value);
    const auto parent = optionalEntityMember(value, "parent");
    if (!node || !parent) return std::nullopt;
    return authoring::Event(authoring::ParentChanged{*node, *parent});
  }
  if (*kind == "ActiveCameraChanged") {
    const auto camera = optionalEntityMember(value, "camera");
    if (!camera) return std::nullopt;
    return authoring::Event(authoring::ActiveCameraChanged{*camera});
  }
  if (*kind == "DocumentReopened") {
    const Value* scene = value.find("scene");
    if (scene == nullptr) return std::nullopt;
    const auto s = decodeAsset(*scene);
    if (!s) return std::nullopt;
    return authoring::Event(authoring::DocumentReopened{*s});
  }
  if (*kind == "NodeNameChanged") {
    const auto node = nodeMember(value);
    const Value* name = value.find("name");
    if (!node || name == nullptr || !name->isString() || !validNodeName(name->asString())) return std::nullopt;
    return authoring::Event(authoring::NodeNameChanged{*node, name->asString()});
  }
  if (*kind == "NodeOrderChanged") {
    const auto node = nodeMember(value);
    const auto sibling = optionalEntityMember(value, "sibling");
    const Value* position = value.find("position");
    const Value* siblingPosition = value.find("siblingPosition");
    const Value* revision = value.find("revision");
    if (!node || !sibling || !*sibling || !position || !siblingPosition || !revision) return std::nullopt;
    const auto p = decodeUInt64(*position);
    const auto sp = decodeUInt64(*siblingPosition);
    const auto r = decodeUInt64(*revision);
    if (!p || !sp || !r || *node == **sibling || *p == *sp || *r == 0) return std::nullopt;
    return authoring::Event(authoring::NodeOrderChanged{*node, **sibling, *p, *sp, *r});
  }
  if (*kind == "NodeMovedInSiblingOrder") {
    const auto node = nodeMember(value);
    const auto parent = optionalEntityMember(value, "parent");
    const Value* from = value.find("fromPosition");
    const Value* position = value.find("position");
    const Value* revision = value.find("revision");
    if (!node || !parent || !from || !position || !revision) return std::nullopt;
    const auto f = decodeUInt64(*from);
    const auto p = decodeUInt64(*position);
    const auto r = decodeUInt64(*revision);
    if (!f || !p || !r || *f == *p || *r == 0) return std::nullopt;
    return authoring::Event(authoring::NodeMovedInSiblingOrder{*node, *parent, *f, *p, *r});
  }
  if (*kind == "Applied") {
    const Value* generation = value.find("generation");
    if (generation == nullptr) return std::nullopt;
    const auto g = decodeUInt64(*generation);
    if (!g) return std::nullopt;
    return authoring::Event(authoring::Applied{*g});
  }
  return std::nullopt;
}

Value encode(const authoring::EventFilter& filter) {
  Value kinds = Value::array();
  for (const auto& [kind, name] : kAuthoringEventKinds) {
    if (filter.kinds.contains(kind)) kinds.push(Value::string(std::string(name)));
  }
  Value out = Value::object();
  out.set("kinds", std::move(kinds));
  out.set("node", encodeOptionalEntity(filter.node));
  out.set("component", filter.component ? encodeTypeId(*filter.component) : Value());
  return out;
}

std::optional<authoring::EventFilter> decodeAuthoringFilter(const Value& value) {
  const Value* kinds = value.find("kinds");
  const auto node = optionalEntityMember(value, "node");
  const Value* component = value.find("component");
  if (kinds == nullptr || !kinds->isArray() || !node || component == nullptr) return std::nullopt;
  authoring::EventFilter filter;
  filter.kinds = authoring::EventKindSet();
  for (const Value& name : kinds->asArray()) {
    if (!name.isString()) return std::nullopt;
    bool known = false;
    for (const auto& [kind, kindText] : kAuthoringEventKinds) {
      if (name.asString() == kindText) {
        filter.kinds = filter.kinds.with(kind);
        known = true;
      }
    }
    if (!known) return std::nullopt;
  }
  filter.node = *node;
  if (!component->isNull()) {
    const auto c = decodeTypeId(*component);
    if (!c) return std::nullopt;
    filter.component = *c;
  }
  return filter;
}

Value encode(const authoring::CommandFailure& failure) {
  Value out = Value::object();
  out.set("ticket", Value::number(failure.ticket.value));
  out.set("error", encodeAuthoringError(failure.error));
  return out;
}

std::optional<authoring::CommandFailure> decodeAuthoringFailure(const Value& value) {
  const Value* ticket = value.find("ticket");
  const Value* error = value.find("error");
  if (ticket == nullptr || error == nullptr) return std::nullopt;
  const auto t = decodeUInt64(*ticket);
  const auto e = decodeAuthoringError(*error);
  if (!t || !e) return std::nullopt;
  return authoring::CommandFailure{access::CommandTicket{*t}, *e};
}

Value encode(const authoring::DocumentInfo& document) {
  Value out = Value::object();
  out.set("scene", encodeAsset(document.scene));
  out.set("provenance",
          Value::string(document.provenance == authoring::Provenance::Imported ? "Imported" : "HandAuthored"));
  out.set("source", Value::string(document.sourcePath));
  out.set("nodeCount", Value::number(document.nodeCount));
  if (document.supportsGuardedPropertyTransactions) out.set("supportsGuardedPropertyTransactions", Value::boolean(true));
  if (document.supportsGuardedTransformTransactions) out.set("supportsGuardedTransformTransactions", Value::boolean(true));
  if (document.supportsGuardedDuplicateTransactions) out.set("supportsGuardedDuplicateTransactions", Value::boolean(true));
  if (document.supportsGuardedNameTransactions) out.set("supportsGuardedNameTransactions", Value::boolean(true));
  if (document.supportsGuardedRenderLookTransactions) out.set("supportsGuardedRenderLookTransactions", Value::boolean(true));
  if (document.supportsPrefabOrigin) out.set("supportsPrefabOrigin", Value::boolean(true));
  if (document.supportsPrefabSync) out.set("supportsPrefabSync", Value::boolean(true));
  if (document.supportsPrefabRegistration) out.set("supportsPrefabRegistration", Value::boolean(true));
  return out;
}

std::optional<authoring::DocumentInfo> decodeDocumentInfo(const Value& value) {
  const Value* scene = value.find("scene");
  const Value* provenance = value.find("provenance");
  const Value* source = value.find("source");
  const Value* nodeCount = value.find("nodeCount");
  if (scene == nullptr || provenance == nullptr || !provenance->isString() || source == nullptr ||
      !source->isString() || nodeCount == nullptr) {
    return std::nullopt;
  }
  const auto s = decodeAsset(*scene);
  const auto n = decodeUInt64(*nodeCount);
  if (!s || !n) return std::nullopt;
  authoring::DocumentInfo out{*s, authoring::Provenance::HandAuthored, source->asString(), *n};
  const auto* capability = value.find("supportsGuardedPropertyTransactions");
  out.supportsGuardedPropertyTransactions = capability && capability->isBool() && capability->asBool();
  const auto* transform = value.find("supportsGuardedTransformTransactions");
  out.supportsGuardedTransformTransactions = transform && transform->isBool() && transform->asBool();
  const auto* duplicate = value.find("supportsGuardedDuplicateTransactions");
  out.supportsGuardedDuplicateTransactions = duplicate && duplicate->isBool() && duplicate->asBool();
  const auto* prefab = value.find("supportsPrefabOrigin");
  const auto* registration = value.find("supportsPrefabRegistration");
  if ((prefab && !prefab->isBool()) || (registration && !registration->isBool())) return std::nullopt;
  const auto* sync = value.find("supportsPrefabSync");
  if (sync && !sync->isBool()) return std::nullopt;
  out.supportsPrefabSync = sync && sync->asBool();
  out.supportsPrefabOrigin = prefab && prefab->asBool();
  out.supportsPrefabRegistration = registration && registration->asBool();
  if (provenance->asString() == "Imported") {
    out.provenance = authoring::Provenance::Imported;
  } else if (provenance->asString() != "HandAuthored") {
    return std::nullopt;
  }
  const auto* names = value.find("supportsGuardedNameTransactions");
  out.supportsGuardedNameTransactions = out.provenance == authoring::Provenance::HandAuthored &&
                                        names && names->isBool() && names->asBool();
  const auto* look = value.find("supportsGuardedRenderLookTransactions");
  out.supportsGuardedRenderLookTransactions = out.provenance == authoring::Provenance::HandAuthored &&
                                              look && look->isBool() && look->asBool();
  return out;
}

Value encode(const authoring::DocumentToken& token) {
  Value out = Value::object();
  out.set("documentId", encodeEntity(token.documentId));
  out.set("documentRevision", Value::number(token.documentRevision));
  return out;
}

std::optional<authoring::DocumentToken> decodeDocumentToken(const Value& value) {
  const auto* id = value.find("documentId");
  const auto* rev = value.find("documentRevision");
  if (!id || !rev || !id->isString()) return std::nullopt;
  const auto guid = decodeEntity(*id);
  const auto revision = decodeUInt64(*rev);
  if (!guid || *guid == EntityGuid{} || asset_system::toString(*guid) != id->asString() || !revision || *revision == 0)
    return std::nullopt;
  return authoring::DocumentToken{*guid, *revision};
}

Value encode(const authoring::DocumentState& state) {
  Value out = encode(state.token);
  out.set("scene", encodeAsset(state.scene));
  out.set("source", Value::string(state.source));
  out.set("provenance", Value::string(state.provenance == authoring::Provenance::Imported ? "Imported" : "HandAuthored"));
  out.set("lastPersistedRevision", Value::number(state.lastPersistedRevision));
  out.set("dirty", Value::boolean(state.dirty));
  return out;
}

std::optional<authoring::DocumentState> decodeDocumentState(const Value& value) {
  if (!value.isObject() || value.asObject().size() != 7) return std::nullopt;
  const auto token = decodeDocumentToken(value);
  const auto* scene = value.find("scene");
  const auto* source = value.find("source");
  const auto* provenance = value.find("provenance");
  const auto* persisted = value.find("lastPersistedRevision");
  const auto* dirty = value.find("dirty");
  if (!token || !scene || !scene->isString() || !source || !source->isString() || source->asString().empty() ||
      !provenance || !provenance->isString() || !persisted || !dirty || !dirty->isBool()) return std::nullopt;
  const auto guid = decodeAsset(*scene);
  const auto revision = decodeUInt64(*persisted);
  if (!guid || *guid == AssetGuid{} || asset_system::toString(*guid) != scene->asString() || !revision ||
      *revision == 0 || *revision > token->documentRevision || (*revision == token->documentRevision && dirty->asBool()))
    return std::nullopt;
  const auto type = provenance->asString();
  if (type != "Imported" && type != "HandAuthored") return std::nullopt;
  return authoring::DocumentState{*token, *guid, source->asString(),
      type == "Imported" ? authoring::Provenance::Imported : authoring::Provenance::HandAuthored, *revision, dirty->asBool()};
}

std::optional<authoring::DocumentError> decodeDocumentError(const Value& value, bool saving) {
  using E = authoring::DocumentError;
  constexpr E shared[]{E::NoDocument, E::StaleDocument};
  constexpr E saves[]{E::ImportedSceneNotSavable, E::WriteFailed};
  constexpr E opens[]{E::SourceUnreadable, E::SourceParseFailed, E::SourceInvalid};
  if (!value.isString()) return std::nullopt;
  for (E error : shared) if (value.asString() == authoring::toString(error)) return error;
  for (E error : saving ? std::span<const E>(saves) : std::span<const E>(opens))
    if (value.asString() == authoring::toString(error)) return error;
  return std::nullopt;
}

namespace {
template <typename T, std::size_t N>
std::optional<T> decodeRecoveryName(const Value* value, const T (&known)[N]) {
  if (!value || !value->isString()) return std::nullopt;
  for (const T item : known) if (value->asString() == authoring::toString(item)) return item;
  return std::nullopt;
}

const Value* required(const Value& object, std::string_view name, bool (Value::*isType)() const noexcept) {
  const auto* member = object.find(name);
  return member && (member->*isType)() ? member : nullptr;
}

std::optional<std::uint64_t> recoveryNumber(const Value& object, std::string_view key) {
  const auto* member = object.find(key);
  return member ? decodeUInt64(*member) : std::nullopt;
}

std::optional<std::vector<std::string>> recoveryLines(const Value* value) {
  if (!value || !value->isArray() || value->asArray().size() > authoring::kRecoveryPreviewLines) return std::nullopt;
  std::vector<std::string> lines;
  for (const Value& line : value->asArray()) {
    if (!line.isString()) return std::nullopt;
    const auto& text = line.asString();
    const auto scalars = std::count_if(text.begin(), text.end(),
        [](unsigned char byte) { return (byte & 0xc0) != 0x80; });
    if (static_cast<std::size_t>(scalars) > authoring::kRecoveryPreviewScalars ||
        text.size() > 4 * authoring::kRecoveryPreviewScalars || text.find_first_of("\r\n") != std::string::npos)
      return std::nullopt;
    lines.emplace_back(text);
  }
  return lines;
}
}  // namespace

Value encode(const authoring::RecoveryStatus& status) {
  Value out = Value::object();
  out.set("capture", Value::string(std::string(authoring::toString(status.capture))));
  out.set("protectedRevision", status.protectedRevision ? Value::number(*status.protectedRevision) : Value());
  Value copy;
  if (status.copy) {
    copy = Value::object();
    copy.set("id", encodeEntity(status.copy->id));
    copy.set("revision", Value::number(status.copy->revision));
    copy.set("capturedUnixSeconds", Value::number(status.copy->capturedUnixSeconds));
    copy.set("pending", Value::boolean(status.copy->pending));
  }
  out.set("copy", std::move(copy));
  Value preview;
  if (status.preview) {
    const auto& source = *status.preview;
    preview = Value::object();
    preview.set("source", Value::string(std::string(authoring::toString(source.source))));
    preview.set("copyNodes", Value::number(source.copyNodes));
    preview.set("sourceNodes", source.sourceNodes ? Value::number(*source.sourceNodes) : Value());
    Value before = Value::array(), after = Value::array();
    for (const auto& line : source.sourceLines) before.push(Value::string(line));
    for (const auto& line : source.copyLines) after.push(Value::string(line));
    preview.set("sourceLines", std::move(before));
    preview.set("copyLines", std::move(after));
    preview.set("truncated", Value::boolean(source.truncated));
  }
  out.set("preview", std::move(preview));
  return out;
}

std::optional<authoring::RecoveryStatus> decodeRecoveryStatus(const Value& value) {
  using C = authoring::RecoveryCapture;
  using S = authoring::RecoverySource;
  constexpr C captures[]{C::Idle, C::Writing, C::Failed, C::Unavailable};
  constexpr S sources[]{S::Matches, S::Changed, S::Unreadable, S::AlreadySaved, S::DirtyDocument};
  if (!value.isObject() || value.asObject().size() != 4) return std::nullopt;
  const auto capture = decodeRecoveryName(value.find("capture"), captures);
  const auto* revision = value.find("protectedRevision");
  const auto* copy = value.find("copy");
  const auto* preview = value.find("preview");
  if (!capture || !revision || !copy || !preview) return std::nullopt;
  authoring::RecoveryStatus out{*capture, {}, {}, {}};
  if (!revision->isNull()) {
    out.protectedRevision = decodeUInt64(*revision);
    if (!out.protectedRevision || *out.protectedRevision == 0) return std::nullopt;
  }
  if (!copy->isNull()) {
    if (!copy->isObject() || copy->asObject().size() != 4) return std::nullopt;
    const auto* id = copy->find("id");
    const auto guid = id ? decodeEntity(*id) : std::nullopt;
    const auto rev = recoveryNumber(*copy, "revision");
    const auto time = recoveryNumber(*copy, "capturedUnixSeconds");
    const auto* pending = required(*copy, "pending", &Value::isBool);
    if (!guid || !rev || *rev == 0 || !time || *time == 0 || *time > 253402300799ull || !pending) return std::nullopt;
    out.copy = authoring::RecoveryCopy{*guid, *rev, *time, pending->asBool()};
  }
  if (out.protectedRevision && (!out.copy || out.copy->pending)) return std::nullopt;
  if (!preview->isNull()) {
    if (!out.copy || !out.copy->pending || !preview->isObject() || preview->asObject().size() != 6) return std::nullopt;
    const auto source = decodeRecoveryName(preview->find("source"), sources);
    const auto count = recoveryNumber(*preview, "copyNodes");
    const auto* official = preview->find("sourceNodes");
    auto before = recoveryLines(preview->find("sourceLines"));
    auto after = recoveryLines(preview->find("copyLines"));
    const auto* truncated = required(*preview, "truncated", &Value::isBool);
    if (!source || !count || !official || !before || !after || !truncated) return std::nullopt;
    const auto sourceCount = official->isNull() ? std::nullopt : decodeUInt64(*official);
    if ((*source == S::Unreadable) != official->isNull() || (!official->isNull() && !sourceCount)) return std::nullopt;
    out.preview = authoring::RecoveryPreview{*source, *count, sourceCount, std::move(*before), std::move(*after),
                                              truncated->asBool()};
  }
  return out;
}

Value encode(const authoring::RecoveryReceipt& receipt) {
  Value out = Value::object();
  out.set("copyId", encodeEntity(receipt.copyId));
  out.set("document", encode(receipt.document));
  return out;
}

std::optional<authoring::RecoveryReceipt> decodeRecoveryReceipt(const Value& value) {
  if (!value.isObject() || value.asObject().size() != 2) return std::nullopt;
  const auto* id = value.find("copyId");
  const auto* document = value.find("document");
  const auto guid = id ? decodeEntity(*id) : std::nullopt;
  const auto state = document ? decodeDocumentState(*document) : std::nullopt;
  if (!guid || !state || state->provenance != authoring::Provenance::HandAuthored) return std::nullopt;
  return authoring::RecoveryReceipt{*guid, *state};
}

std::optional<authoring::RecoveryError> decodeRecoveryError(const Value& value) {
  using E = authoring::RecoveryError;
  constexpr E errors[]{E::NoDocument, E::Busy, E::Unavailable, E::StaleDocument, E::StaleCopy,
      E::DirtyDocument, E::SourceChanged, E::SourceUnreadable, E::InvalidCopy, E::IoFailed};
  return decodeRecoveryName(&value, errors);
}

Value encode(const authoring::NodeSummary& node) {
  Value out = Value::object();
  out.set("node", encodeEntity(node.node));
  out.set("parent", encodeOptionalEntity(node.parent));
  out.set("name", Value::string(node.name));
  if (node.prefabOrigin) out.set("prefabOrigin", encodeAsset(*node.prefabOrigin));
  if (node.prefabBaseline) out.set("prefabBaseline", Value::string(*node.prefabBaseline));
  return out;
}

std::optional<authoring::NodeSummary> decodeNodeSummary(const Value& value) {
  const auto node = nodeMember(value);
  const auto parent = optionalEntityMember(value, "parent");
  if (!node || !parent) return std::nullopt;
  const Value* name = value.find("name");
  if (name != nullptr && (!name->isString() || !validNodeName(name->asString()))) return std::nullopt;
  const auto* field = value.find("prefabOrigin");
  const auto prefab = field ? decodeAsset(*field) : std::nullopt;
  if (field && (!prefab || *prefab == asset_system::AssetGuid{})) return std::nullopt;
  const auto* baseline = value.find("prefabBaseline");
  if (baseline && (!prefab || !baseline->isString() || !authoring::validPrefabDigest(baseline->asString())))
    return std::nullopt;
  return authoring::NodeSummary{*node, *parent, name == nullptr ? std::string() : name->asString(), prefab,
      baseline ? std::optional<std::string>{baseline->asString()} : std::nullopt};
}

Value encodeAuthoringError(authoring::Error error) { return Value::string(std::string(authoring::toString(error))); }
std::optional<authoring::Error> decodeAuthoringError(const Value& value) {
  return enumeratorNamed(value, kAuthoringErrors);
}
Value encodeOpenError(authoring::OpenError error) { return Value::string(std::string(authoring::toString(error))); }
std::optional<authoring::OpenError> decodeOpenError(const Value& value) { return enumeratorNamed(value, kOpenErrors); }
Value encodeSaveError(authoring::SaveError error) { return Value::string(std::string(authoring::toString(error))); }
std::optional<authoring::SaveError> decodeSaveError(const Value& value) { return enumeratorNamed(value, kSaveErrors); }
Value encode(const authoring::CreatedScene& scene) {
  Value out = Value::object();
  out.set("scene", Value::string(asset_system::toString(scene.scene)));
  out.set("source", Value::string(scene.source));
  return out;
}
std::optional<authoring::CreatedScene> decodeCreatedScene(const Value& value) {
  const auto* scene = value.find("scene");
  const auto* source = value.find("source");
  if (!scene || !scene->isString() || !source || !source->isString()) return std::nullopt;
  const auto guid = asset_system::parseAssetGuid(scene->asString());
  const std::string_view path = source->asString();
  constexpr std::string_view prefix = "scenes/saved/save_", suffix = "/scene.scene.txt";
  if (guid.isErr() || guid.value() == asset_system::AssetGuid{} || asset_system::toString(guid.value()) != scene->asString() ||
      !path.starts_with(prefix) || !path.ends_with(suffix) || path.size() < prefix.size() + suffix.size() ||
      !authoring::validSaveAsName(path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()))) return std::nullopt;
  return authoring::CreatedScene{guid.value(), std::string(path)};
}
Value encodeCreateSceneError(authoring::CreateSceneError error) { return Value::string(std::string(authoring::toString(error))); }
std::optional<authoring::CreateSceneError> decodeCreateSceneError(const Value& value) {
  using E = authoring::CreateSceneError;
  constexpr std::array errors{E::Unavailable, E::InvalidName, E::UnsafeTarget, E::Busy, E::NameConflict,
                              E::IdentityConflict, E::WriteFailed};
  return enumeratorNamed(value, errors);
}
Value encode(const authoring::SavedScene& scene) {
  Value out = Value::object();
  out.set("scene", Value::string(asset_system::toString(scene.scene)));
  out.set("source", Value::string(scene.source));
  out.set("documentRevision", Value::number(scene.documentRevision));
  return out;
}
std::optional<authoring::SavedScene> decodeSavedScene(const Value& value) {
  const auto* scene = value.find("scene");
  const auto* source = value.find("source");
  const auto* revision = value.find("documentRevision");
  if (!scene || !scene->isString() || !source || !source->isString() || !revision) return std::nullopt;
  const auto guid = asset_system::parseAssetGuid(scene->asString());
  const auto number = decodeUInt64(*revision);
  constexpr std::string_view prefix = "scenes/saved/save_", suffix = "/scene.scene.txt";
  const std::string_view path = source->asString();
  if (guid.isErr() || guid.value() == asset_system::AssetGuid{} || asset_system::toString(guid.value()) != scene->asString() ||
      !number || !path.starts_with(prefix) || !path.ends_with(suffix) || path.size() < prefix.size() + suffix.size() ||
      !authoring::validSaveAsName(path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()))) return std::nullopt;
  return authoring::SavedScene{guid.value(), std::string(path), *number};
}
Value encodeSaveAsError(authoring::SaveAsError error) { return Value::string(std::string(authoring::toString(error))); }
Value encode(const authoring::PrefabRegistration& registration) {
  Value out = Value::object();
  out.set("prefab", encodeAsset(registration.prefab));
  out.set("source", Value::string(registration.source));
  out.set("documentRevision", Value::number(registration.documentRevision));
  return out;
}
std::optional<authoring::PrefabRegistration> decodePrefabRegistration(const Value& value) {
  const auto* prefab = value.find("prefab");
  const auto* source = value.find("source");
  const auto* revision = value.find("documentRevision");
  if (!value.isObject() || value.asObject().size() != 3 || !prefab || !source || !source->isString() || !revision)
    return std::nullopt;
  const auto guid = decodeAsset(*prefab);
  const auto number = decodeUInt64(*revision);
  if (!guid || *guid == asset_system::AssetGuid{} || !number) return std::nullopt;
  return authoring::PrefabRegistration{*guid, source->asString(), *number};
}
Value encode(const authoring::PrefabUpdate& update) {
  Value out = Value::object();
  out.set("registration", encode(update.registration));
  out.set("previousRevision", Value::string(update.previousRevision));
  out.set("revision", Value::string(update.revision));
  out.set("changed", Value::boolean(update.changed));
  return out;
}
std::optional<authoring::PrefabUpdate> decodePrefabUpdate(const Value& value) {
  const auto* registration = value.find("registration");
  const auto* previous = value.find("previousRevision");
  const auto* revision = value.find("revision");
  const auto* changed = value.find("changed");
  if (!value.isObject() || value.asObject().size() != 4 || !registration ||
      !previous || !previous->isString() || !authoring::validPrefabDigest(previous->asString()) ||
      !revision || !revision->isString() || !authoring::validPrefabDigest(revision->asString()) ||
      !changed || !changed->isBool() || changed->asBool() != (previous->asString() != revision->asString()))
    return std::nullopt;
  const auto parsed = decodePrefabRegistration(*registration);
  if (!parsed) return std::nullopt;
  return authoring::PrefabUpdate{*parsed, previous->asString(), revision->asString(), changed->asBool()};
}
Value encode(const authoring::PrefabSubtree& subtree) {
  Value slots = Value::array();
  for (auto slot : subtree.slots) slots.push(Value::number(slot));
  Value out = Value::object();
  out.set("source", Value::string(subtree.source));
  out.set("parent", subtree.parent ? encodeEntity(*subtree.parent) : Value());
  out.set("slots", std::move(slots));
  out.set("order", Value::string(subtree.order));
  return out;
}
std::optional<authoring::PrefabSubtree> decodePrefabSubtree(const Value& value) {
  const auto* source = value.find("source");
  const auto parent = optionalEntityMember(value, "parent");
  const auto* slots = value.find("slots");
  const auto* order = value.find("order");
  if (!value.isObject() || value.asObject().size() != 4 || !source || !source->isString() ||
      source->asString().size() > 64 * 1024 || !parent || !slots || !slots->isArray() ||
      slots->asArray().size() > authoring::kMaxGuardedDuplicateNodes || !order || !order->isString() ||
      !authoring::validPrefabDigest(order->asString()) ||
      source->asString().empty() != slots->asArray().empty()) return std::nullopt;
  authoring::PrefabSubtree result{source->asString(), *parent, {}, order->asString()};
  for (const auto& slot : slots->asArray()) {
    const auto n = decodeUInt64(slot);
    if (!n || (!result.slots.empty() && result.slots.back() >= *n)) return std::nullopt;
    result.slots.push_back(*n);
  }
  return result;
}
Value encode(const authoring::PrefabEdit& edit) {
  Value out = Value::object();
  out.set("document", encode(edit.document));
  out.set("before", encode(edit.before));
  out.set("after", encode(edit.after));
  out.set("changed", Value::boolean(edit.changed));
  out.set("affectsProjection", Value::boolean(edit.affectsProjection));
  return out;
}
std::optional<authoring::PrefabEdit> decodePrefabEdit(const Value& value) {
  const auto* document = value.find("document");
  const auto* before = value.find("before");
  const auto* after = value.find("after");
  const auto* changed = value.find("changed");
  const auto* projection = value.find("affectsProjection");
  if (!value.isObject() || value.asObject().size() != 5 || !document || !before || !after ||
      !changed || !changed->isBool() || !projection || !projection->isBool()) return std::nullopt;
  auto token = decodeDocumentToken(*document);
  auto a = decodePrefabSubtree(*before);
  auto b = decodePrefabSubtree(*after);
  if (!token || !a || !b || changed->asBool() != (*a != *b) ||
      (!changed->asBool() && projection->asBool())) return std::nullopt;
  return authoring::PrefabEdit{*token, std::move(*a), std::move(*b), changed->asBool(), projection->asBool()};
}
std::optional<authoring::PrefabEditError> decodePrefabEditError(const Value& value) {
  using E = authoring::PrefabEditError;
  constexpr std::array values{E::Unsupported, E::NoDocument, E::StaleDocument, E::StaleSource, E::InvalidInstance,
                              E::LocalConflict, E::BaselineUnavailable, E::ReplayConflict};
  return enumeratorNamed(value, values);
}
std::optional<authoring::SaveAsError> decodeSaveAsError(const Value& value) {
  using E = authoring::SaveAsError;
  constexpr std::array errors{E::Unavailable, E::InvalidName, E::NoDocument, E::ImportedSceneNotSavable, E::UnsafeTarget,
                              E::Busy, E::NameConflict, E::IdentityConflict, E::WriteFailed, E::StaleDocument,
                              E::UnsupportedSubtree, E::StaleSource, E::UnknownOutcome};
  return enumeratorNamed(value, errors);
}
Value encodeApplyError(authoring::ApplyError error) { return Value::string(std::string(authoring::toString(error))); }
std::optional<authoring::ApplyError> decodeApplyError(const Value& value) {
  return enumeratorNamed(value, kApplyErrors);
}

Value encode(const authoring::ApplyReceipt& receipt) {
  Value out = Value::object();
  out.set("id", Value::string(authoring::toString(receipt.id)));
  out.set("documentRevision", Value::number(receipt.documentRevision));
  return out;
}
std::optional<authoring::ApplyReceipt> decodeApplyReceipt(const Value& value) {
  const auto* id = value.find("id"); const auto* revision = value.find("documentRevision");
  if (!id || !id->isString() || !revision) return std::nullopt;
  auto parsed = authoring::parseApplyId(id->asString()); auto number = decodeUInt64(*revision);
  if (parsed.isErr() || !number || *number == 0) return std::nullopt;
  return authoring::ApplyReceipt{parsed.value(), *number};
}
Value encode(const authoring::ApplyRecord& record) {
  Value out = Value::object(); out.set("receipt", encode(record.receipt));
  out.set("state", Value::string(std::string(authoring::toString(record.state))));
  if (record.generation) out.set("generation", Value::number(*record.generation));
  if (record.error) out.set("error", Value::string(std::string(authoring::toString(*record.error))));
  if (record.supersededBy) out.set("supersededBy", Value::string(authoring::toString(*record.supersededBy)));
  return out;
}
std::optional<authoring::ApplyRecord> decodeApplyRecord(const Value& value) {
  const auto* receipt = value.find("receipt"); const auto* state = value.find("state");
  if (!receipt || !state) return std::nullopt;
  auto decoded = decodeApplyReceipt(*receipt);
  auto named = enumeratorNamed(*state, std::array{authoring::ApplyState::Pending, authoring::ApplyState::Applied,
      authoring::ApplyState::Failed, authoring::ApplyState::Superseded});
  if (!decoded || !named) return std::nullopt;
  authoring::ApplyRecord out{*decoded, *named, {}, {}, {}};
  const auto* gen = value.find("generation"); const auto* error = value.find("error"); const auto* by = value.find("supersededBy");
  if ((*named != authoring::ApplyState::Applied && gen) || (*named != authoring::ApplyState::Failed && error) ||
      (*named != authoring::ApplyState::Superseded && by)) return std::nullopt;
  if (*named == authoring::ApplyState::Applied) {
    out.generation = gen ? decodeUInt64(*gen) : std::nullopt;
    if (!out.generation || *out.generation == 0) return std::nullopt;
  } else if (*named == authoring::ApplyState::Failed) {
    if (!error) return std::nullopt;
    out.error = enumeratorNamed(*error, std::array{authoring::ReloadError::ScenePreparationFailed,
        authoring::ReloadError::RenderPreparationFailed, authoring::ReloadError::RuntimeStopped});
    if (!out.error) return std::nullopt;
  } else if (*named == authoring::ApplyState::Superseded) {
    if (!by || !by->isString()) return std::nullopt;
    const auto id = authoring::parseApplyId(by->asString());
    if (id.isErr() || id.value() == out.receipt.id) return std::nullopt;
    out.supersededBy = id.value();
  }
  return out;
}
Value encode(const authoring::ApplySnapshot& snapshot) {
  Value out = Value::object();
  out.set("currentDocumentRevision", snapshot.currentDocumentRevision ? Value::number(*snapshot.currentDocumentRevision) : Value());
  out.set("request", snapshot.request ? encode(*snapshot.request) : Value());
  Value projection;
  if (snapshot.projection) {
    projection = Value::object(); projection.set("receipt", encode(snapshot.projection->receipt));
    projection.set("generation", Value::number(snapshot.projection->generation));
  }
  out.set("projection", std::move(projection)); return out;
}
std::optional<authoring::ApplySnapshot> decodeApplySnapshot(const Value& value) {
  const auto* rev = value.find("currentDocumentRevision"); const auto* request = value.find("request");
  const auto* projection = value.find("projection");
  if (!rev || !request || !projection) return std::nullopt;
  authoring::ApplySnapshot out;
  if (!rev->isNull()) { out.currentDocumentRevision = decodeUInt64(*rev); if (!out.currentDocumentRevision || *out.currentDocumentRevision == 0) return std::nullopt; }
  if (!request->isNull()) { out.request = decodeApplyRecord(*request); if (!out.request) return std::nullopt; }
  if (!projection->isNull()) {
    const auto* receipt = projection->find("receipt"); const auto* gen = projection->find("generation");
    auto decoded = receipt ? decodeApplyReceipt(*receipt) : std::nullopt; auto number = gen ? decodeUInt64(*gen) : std::nullopt;
    if (!decoded || !number || *number == 0) return std::nullopt;
    out.projection = authoring::ApplyProjection{*decoded, *number};
  }
  if ((!out.currentDocumentRevision && (out.request || out.projection)) || (!out.request && out.projection)) return std::nullopt;
  if (out.request && out.request->receipt.documentRevision > *out.currentDocumentRevision) return std::nullopt;
  if (out.projection && out.projection->receipt.documentRevision > *out.currentDocumentRevision) return std::nullopt;
  if (out.request && out.projection && out.request->receipt.id == out.projection->receipt.id &&
      (out.request->state != authoring::ApplyState::Applied || out.request->receipt != out.projection->receipt ||
       out.request->generation != out.projection->generation)) return std::nullopt;
  return out;
}
Value encodeApplyLookupError(authoring::ApplyLookupError error) { return Value::string(std::string(authoring::toString(error))); }
std::optional<authoring::ApplyLookupError> decodeApplyLookupError(const Value& value) {
  return enumeratorNamed(value, std::array{authoring::ApplyLookupError::UnknownApply});
}

}  // namespace atlantis::remote::codec
