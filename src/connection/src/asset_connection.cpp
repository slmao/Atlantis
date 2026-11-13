#include <atlantis/connection/asset_connection.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <type_traits>
#include <utility>

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
constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::size_t kEncodedPreviewLimit = 4 * ((kPreviewPngMaxBytes + 2) / 3);
constexpr std::array<std::uint8_t, 16> kPngHeader{137, 80, 78, 71, 13, 10, 26, 10,
                                                 0, 0, 0, 13, 'I', 'H', 'D', 'R'};

bool hasPreviewHeader(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 33 || bytes.size() > kPreviewPngMaxBytes ||
      !std::equal(kPngHeader.begin(), kPngHeader.end(), bytes.begin())) return false;
  const auto bigEndian = [&](std::size_t i) {
    return (std::uint32_t{bytes[i]} << 24) | (std::uint32_t{bytes[i + 1]} << 16) |
           (std::uint32_t{bytes[i + 2]} << 8) | bytes[i + 3];
  };
  return bigEndian(16) == kPreviewDimension && bigEndian(20) == kPreviewDimension;
}

std::string encodePreviewBytes(std::span<const std::uint8_t> bytes) {
  std::string out(4 * ((bytes.size() + 2) / 3), '=');
  for (std::size_t i = 0, j = 0; i < bytes.size(); i += 3, j += 4) {
    const std::uint32_t a = bytes[i];
    const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0u;
    const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0u;
    out[j] = kBase64[a >> 2];
    out[j + 1] = kBase64[((a & 3u) << 4) | (b >> 4)];
    if (i + 1 < bytes.size()) out[j + 2] = kBase64[((b & 15u) << 2) | (c >> 6)];
    if (i + 2 < bytes.size()) out[j + 3] = kBase64[c & 63u];
  }
  return out;
}

constexpr int base64Digit(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::optional<std::vector<std::uint8_t>> decodePreviewBytes(std::string_view encoded) {
  const std::size_t n = encoded.size();
  if (n < 4 || n > kEncodedPreviewLimit || n % 4 != 0) return std::nullopt;
  const std::size_t padding = encoded[n - 1] != '=' ? 0u : encoded[n - 2] != '=' ? 1u : 2u;
  const std::size_t decodedSize = n / 4 * 3 - padding;
  if (decodedSize < 33 || decodedSize > kPreviewPngMaxBytes) return std::nullopt;
  // Reject stray padding, whitespace and nonzero unused tail bits. A
  // canonical encoding represents each possible PNG with exactly one string.
  const int tail = base64Digit(encoded[n - padding - 1]);
  if (tail < 0 || (padding == 1 && (tail & 3) != 0) || (padding == 2 && (tail & 15) != 0)) return std::nullopt;

  std::vector<std::uint8_t> out;
  out.reserve(decodedSize);
  for (std::size_t i = 0; i < n; i += 4) {
    const int a = base64Digit(encoded[i]);
    const int b = base64Digit(encoded[i + 1]);
    const bool hasC = i + 2 < n - padding;
    const bool hasD = i + 3 < n - padding;
    const int c = hasC ? base64Digit(encoded[i + 2]) : 0;
    const int d = hasD ? base64Digit(encoded[i + 3]) : 0;
    if (a < 0 || b < 0 || c < 0 || d < 0) return std::nullopt;
    out.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
    if (hasC) out.push_back(static_cast<std::uint8_t>((b << 4) | (c >> 2)));
    if (hasD) out.push_back(static_cast<std::uint8_t>((c << 6) | d));
  }
  if (!hasPreviewHeader(out)) return std::nullopt;
  return out;
}

void encodePreviewImage(json::Value& payload, std::span<const std::uint8_t> png) {
  payload.set("width", json::Value::number(std::uint64_t{kPreviewDimension}));
  payload.set("height", json::Value::number(std::uint64_t{kPreviewDimension}));
  payload.set("mimeType", json::Value::string(std::string(kPreviewMimeType)));
  payload.set("pngBase64", json::Value::string(encodePreviewBytes(png)));
}

std::optional<std::vector<std::uint8_t>> decodePreviewImage(const json::Value& payload) {
  const auto* width = payload.find("width");
  const auto* height = payload.find("height");
  const auto* mime = payload.find("mimeType");
  const auto* png = payload.find("pngBase64");
  std::uint64_t w = 0, h = 0;
  if (!width || !width->toUInt64(w) || w != kPreviewDimension ||
      !height || !height->toUInt64(h) || h != kPreviewDimension ||
      !mime || !mime->isString() || mime->asString() != kPreviewMimeType || !png || !png->isString())
    return std::nullopt;
  return decodePreviewBytes(png->asString());
}

bool matchesPreviewGuid(const json::Value* value, const asset_system::AssetGuid& requested) {
  return requested != asset_system::AssetGuid{} && value && value->isString() &&
         value->asString() == asset_system::toString(requested);
}

template <class Enum, std::size_t N>
std::string_view token(Enum value, const std::array<const char*, N>& names) noexcept {
  const auto index = static_cast<std::size_t>(value);
  ATLANTIS_CHECK(index < N);
  return index < N ? names[index] : "";
}
}  // namespace

std::string_view toString(PreviewError error) noexcept {
  return token(error, std::array{"Unsupported", "Unavailable", "Busy", "CatalogUnavailable", "UnknownAsset",
                                "NotMesh", "TooLarge", "InvalidMesh", "RenderFailed"});
}

std::optional<json::Value> encodeMeshPreview(const MeshPreview& image, const asset_system::AssetGuid& requested) {
  if (requested == asset_system::AssetGuid{} || image.guid != requested ||
      image.width != kPreviewDimension || image.height != kPreviewDimension || !hasPreviewHeader(image.png))
    return std::nullopt;
  auto payload = json::Value::object();
  payload.set("guid", json::Value::string(asset_system::toString(image.guid)));
  encodePreviewImage(payload, image.png);
  return payload;
}

std::optional<MeshPreview> decodeMeshPreview(const json::Value& payload, const asset_system::AssetGuid& requested) {
  if (!matchesPreviewGuid(payload.find("guid"), requested)) return std::nullopt;
  auto bytes = decodePreviewImage(payload);
  if (!bytes) return std::nullopt;
  return MeshPreview{requested, kPreviewDimension, kPreviewDimension, std::move(*bytes)};
}

std::string_view toString(MeshMaterialPreviewError error) noexcept {
  return token(error, std::array{"Unsupported", "Unavailable", "Busy", "CatalogUnavailable", "UnknownMesh",
                                "UnknownMaterial", "NotMesh", "NotMaterial", "InvalidMesh", "InvalidMaterial",
                                "UnsupportedMaterial", "DependencyUnavailable", "IncompatiblePair", "TooLarge",
                                "RenderFailed"});
}

std::optional<json::Value> encodeMeshMaterialPreview(const MeshMaterialPreview& image,
                                                     const asset_system::AssetGuid& meshGuid,
                                                     const asset_system::AssetGuid& materialGuid) {
  if (meshGuid == asset_system::AssetGuid{} || materialGuid == asset_system::AssetGuid{} ||
      image.meshGuid != meshGuid || image.materialGuid != materialGuid ||
      image.width != kPreviewDimension || image.height != kPreviewDimension || !hasPreviewHeader(image.png))
    return std::nullopt;
  auto payload = json::Value::object();
  payload.set("meshGuid", json::Value::string(asset_system::toString(image.meshGuid)));
  payload.set("materialGuid", json::Value::string(asset_system::toString(image.materialGuid)));
  encodePreviewImage(payload, image.png);
  return payload;
}

std::optional<MeshMaterialPreview> decodeMeshMaterialPreview(const json::Value& payload,
                                                            const asset_system::AssetGuid& meshGuid,
                                                            const asset_system::AssetGuid& materialGuid) {
  if (!payload.isObject()) return std::nullopt;
  for (std::string_view key : {"meshGuid", "materialGuid", "width", "height", "mimeType", "pngBase64"}) {
    if (std::ranges::count_if(payload.asObject(), [key](const auto& member) { return member.first == key; }) != 1)
      return std::nullopt;
  }
  if (!matchesPreviewGuid(payload.find("meshGuid"), meshGuid) ||
      !matchesPreviewGuid(payload.find("materialGuid"), materialGuid))
    return std::nullopt;
  auto bytes = decodePreviewImage(payload);
  if (!bytes) return std::nullopt;
  return MeshMaterialPreview{meshGuid, materialGuid, kPreviewDimension, kPreviewDimension, std::move(*bytes)};
}

std::string_view toString(RebuildError value) noexcept {
  return token(value, std::array{"Unavailable", "Busy", "WorkspaceNeedsRepair", "CatalogUnavailable",
                                 "UnknownAsset", "UnsupportedProducer", "UnknownJob", "Unsupported"});
}

std::string_view toString(MaterialMode value) noexcept {
  return token(value, std::array{"Create", "Copy", "Edit", "Resume"});
}
std::string_view toString(MaterialError value) noexcept {
  return token(value, std::array{"Unsupported", "Unavailable", "Busy", "WorkspaceNeedsRepair", "CatalogUnavailable",
      "UnknownMaterial", "NotMaterial", "InvalidMaterial", "UnsupportedMaterial", "DependencyUnavailable",
      "InvalidRecipe", "UnknownMesh", "NotMesh", "InvalidMesh", "IncompatiblePair", "TooLarge", "RenderFailed",
      "NameConflict", "StaleEdit", "ActivationRequired", "NothingToResume", "Unchanged", "UnknownJob", "NotEditable"});
}
std::string_view toString(MaterialStage value) noexcept {
  return token(value, std::array{"Source", "Pipeline", "Validation", "SourceCommit", "Publication"});
}
std::string_view toString(MaterialFailureCode value) noexcept {
  return token(value, std::array{"InvalidSource", "InvalidRecipe", "DependencyUnavailable", "NotEditable",
      "NameConflict", "StaleEdit", "ActivationRequired", "NothingToResume", "Unchanged", "LaunchFailed",
      "BuildFailed", "InvalidOutput", "IdentityConflict", "CommitFailed", "PublishFailed", "TimedOut"});
}
bool validMaterialValues(const MaterialValues& values) noexcept {
  const auto factor = [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; };
  return values.texture != asset_system::AssetGuid{} && std::ranges::all_of(values.color, factor) &&
         factor(values.metallic) && factor(values.roughness);
}
bool validMaterialRecipe(const MaterialRecipe& recipe) noexcept {
  return validMaterialValues(recipe.values) &&
         (recipe.filter == MaterialFilter::Nearest || recipe.filter == MaterialFilter::Linear) &&
         (recipe.address == MaterialAddress::Repeat || recipe.address == MaterialAddress::ClampToEdge);
}
MaterialTarget materialTarget(const MaterialSave& request) {
  return std::visit([](const auto& value) -> MaterialTarget {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, CreateMaterial>) return {MaterialMode::Create, value.name, {}};
    if constexpr (std::is_same_v<T, CopyMaterial>) return {MaterialMode::Copy, value.name, value.material};
    if constexpr (std::is_same_v<T, EditMaterial>) return {MaterialMode::Edit, {}, value.material};
    if constexpr (std::is_same_v<T, ResumeMaterial>) return {MaterialMode::Resume, {}, value.material};
  }, request);
}
json::Value encodeMaterialValues(const MaterialValues& values) {
  auto out = json::Value::object();
  out.set("texture", json::Value::string(asset_system::toString(values.texture)));
  auto color = json::Value::array();
  for (const float value : values.color) color.push(json::Value::number(value));
  out.set("color", std::move(color));
  out.set("metallic", json::Value::number(values.metallic));
  out.set("roughness", json::Value::number(values.roughness));
  return out;
}
std::optional<MaterialValues> decodeMaterialValues(const json::Value& value) {
  if (!value.isObject() || value.asObject().size() != 4) return std::nullopt;
  const auto* texture = value.find("texture");
  const auto* color = value.find("color");
  const auto* metallic = value.find("metallic");
  const auto* roughness = value.find("roughness");
  if (!texture || !texture->isString() || !color || !color->isArray() || color->asArray().size() != 3 ||
      !metallic || !roughness) return std::nullopt;
  const auto guid = asset_system::parseAssetGuid(texture->asString());
  if (guid.isErr() || texture->asString() != asset_system::toString(guid.value())) return std::nullopt;
  MaterialValues out{guid.value()};
  for (std::size_t i = 0; i < out.color.size(); ++i)
    if (!color->asArray()[i].toFloat(out.color[i])) return std::nullopt;
  if (!metallic->toFloat(out.metallic) || !roughness->toFloat(out.roughness) ||
      !validMaterialValues(out)) return std::nullopt;
  return out;
}
json::Value encodeMaterialRecipe(const MaterialRecipe& recipe) {
  auto value = json::Value::object();
  value.set("values", encodeMaterialValues(recipe.values));
  value.set("filter", json::Value::string(recipe.filter == MaterialFilter::Linear ? "Linear" : "Nearest"));
  value.set("address", json::Value::string(recipe.address == MaterialAddress::Repeat ? "Repeat" : "ClampToEdge"));
  return value;
}
std::optional<MaterialRecipe> decodeMaterialRecipe(const json::Value& value) {
  if (!value.isObject() || value.asObject().size() != 3) return std::nullopt;
  const auto* values = value.find("values");
  const auto* filter = value.find("filter");
  const auto* address = value.find("address");
  if (!values || !filter || !filter->isString() || !address || !address->isString()) return std::nullopt;
  auto fields = decodeMaterialValues(*values);
  const auto& f = filter->asString();
  const auto& a = address->asString();
  if (!fields || (f != "Linear" && f != "Nearest") || (a != "Repeat" && a != "ClampToEdge")) return std::nullopt;
  return MaterialRecipe{*fields, f == "Linear" ? MaterialFilter::Linear : MaterialFilter::Nearest,
                        a == "Repeat" ? MaterialAddress::Repeat : MaterialAddress::ClampToEdge};
}
namespace {
using J = json::Value;
using Guid = asset_system::AssetGuid;
J text(std::string_view value) { return J::string(std::string(value)); }
J guidValue(const Guid& value) { return J::string(asset_system::toString(value)); }
J generationValue(std::optional<GenerationId> value) {
  return value ? guidValue(value->value) : J{};
}
std::optional<Guid> guidMember(const J* value) {
  if (!value || !value->isString()) return {};
  auto guid = asset_system::parseAssetGuid(value->asString());
  return guid.isOk() ? std::optional{guid.value()} : std::nullopt;
}
bool generationMember(const J* value, std::optional<GenerationId>& out) {
  if (!value) return false;
  if (value->isNull()) return true;
  const auto guid = guidMember(value);
  if (!guid) return false;
  out = GenerationId{*guid};
  return true;
}
template<class E> std::optional<E> enumMember(const J* value, E last) {
  if (!value || !value->isString()) return {};
  for (unsigned i = 0; i <= static_cast<unsigned>(last); ++i)
    if (toString(static_cast<E>(i)) == value->asString()) return static_cast<E>(i);
  return {};
}
std::optional<MaterialTarget> decodeTarget(const J& value) {
  if (!value.isObject()) return {};
  auto mode = enumMember(value.find("mode"), MaterialMode::Resume);
  if (!mode) return {};
  const bool named = *mode == MaterialMode::Create || *mode == MaterialMode::Copy;
  const bool addressed = *mode != MaterialMode::Create;
  const auto* name = value.find("name");
  const auto guid = guidMember(value.find("material"));
  if ((named && (!name || !name->isString() || !validImportName(name->asString()))) ||
      (!named && name) || (addressed && !guid) || (!addressed && value.find("material"))) return {};
  return MaterialTarget{*mode, named ? name->asString() : std::string{}, guid};
}
J encodeTarget(const MaterialTarget& target) {
  auto value = J::object();
  value.set("mode", text(toString(target.mode)));
  if (!target.name.empty()) value.set("name", text(target.name));
  if (target.material) value.set("material", guidValue(*target.material));
  return value;
}
}
json::Value encodeMaterialSave(const MaterialSave& input) {
  auto value = encodeTarget(materialTarget(input));
  std::visit([&](const auto& request) {
    using T = std::decay_t<decltype(request)>;
    if constexpr (!std::is_same_v<T, ResumeMaterial>) value.set("values", encodeMaterialValues(request.values));
    if constexpr (std::is_same_v<T, EditMaterial>) {
      value.set("activeGeneration", generationValue(request.activeGeneration));
      value.set("expected", encodeMaterialValues(request.expected));
    }
  }, input);
  return value;
}
std::optional<MaterialSave> decodeMaterialSave(const json::Value& value) {
  const auto target = decodeTarget(value);
  if (!target) return {};
  const auto count = value.asObject().size();
  if (target->mode == MaterialMode::Resume)
    return count == 2 ? std::optional<MaterialSave>{ResumeMaterial{*target->material}} : std::nullopt;
  const auto* member = value.find("values");
  auto values = member ? decodeMaterialValues(*member) : std::nullopt;
  if (!values) return {};
  if (target->mode == MaterialMode::Create && count == 3) return CreateMaterial{target->name, *values};
  if (target->mode == MaterialMode::Copy && count == 4)
    return CopyMaterial{target->name, *target->material, *values};
  if (target->mode == MaterialMode::Edit && count == 5) {
    const auto* expected = value.find("expected");
    std::optional<GenerationId> generation;
    if (expected && generationMember(value.find("activeGeneration"), generation))
      if (auto fields = decodeMaterialValues(*expected)) return EditMaterial{*target->material, generation, *fields, *values};
  }
  return {};
}
json::Value encodeMaterialRead(const MaterialRead& value) {
  auto out = J::object();
  out.set("material", guidValue(value.material));
  out.set("recipe", encodeMaterialRecipe(value.recipe));
  out.set("editable", J::boolean(value.editable));
  out.set("activeGeneration", generationValue(value.activeGeneration));
  return out;
}
std::optional<MaterialRead> decodeMaterialRead(const json::Value& value) {
  if (!value.isObject() || value.asObject().size() != 4) return {};
  const auto material = guidMember(value.find("material"));
  const auto* recipe = value.find("recipe");
  const auto* editable = value.find("editable");
  std::optional<GenerationId> generation;
  if (!material || !recipe || !editable || !editable->isBool() ||
      !generationMember(value.find("activeGeneration"), generation)) return {};
  auto decoded = decodeMaterialRecipe(*recipe);
  if (!decoded) return {};
  return MaterialRead{*material, *decoded, editable->asBool(), generation};
}
json::Value encodeMaterialJob(const MaterialJob& value) {
  auto out = encodeTarget(value.target);
  out.set("jobId", guidValue(value.id.value));
  out.set("state", text(toString(value.state)));
  if (value.material) out.set("savedMaterial", guidValue(*value.material));
  if (value.generation) out.set("generation", generationValue(value.generation));
  if (value.failure) {
    const auto& f = *value.failure;
    auto failure = J::object();
    failure.set("stage", text(toString(f.stage)));
    failure.set("code", text(toString(f.code)));
    failure.set("diagnostic", text(f.diagnostic));
    failure.set("sourceState", text(toString(f.sourceState)));
    if (f.exitCode) failure.set("exitCode", J::number(static_cast<std::uint64_t>(*f.exitCode)));
    if (f.material) failure.set("savedMaterial", guidValue(*f.material));
    out.set("failure", std::move(failure));
  }
  return out;
}
std::optional<MaterialJob> decodeMaterialJob(const json::Value& value) {
  const auto target = decodeTarget(value);
  const auto id = guidMember(value.find("jobId"));
  const auto state = enumMember(value.find("state"), RebuildState::Failed);
  if (!target || !id || !state) return {};
  MaterialJob out{{*id}, *target, *state, {}, {}, {}};
  const auto count = value.asObject().size();
  const auto base = 3u + (!target->name.empty() ? 1u : 0u) + (target->material ? 1u : 0u);
  if (*state == RebuildState::Running) return count == base ? std::optional{out} : std::nullopt;
  if (*state == RebuildState::Succeeded) {
    out.material = guidMember(value.find("savedMaterial"));
    if (count != base + 2 || !out.material || !generationMember(value.find("generation"), out.generation) ||
        !out.generation || ((target->mode == MaterialMode::Edit || target->mode == MaterialMode::Resume) &&
                            out.material != target->material)) return {};
    return out;
  }
  const auto* f = value.find("failure");
  if (count != base + 1 || !f || !f->isObject()) return {};
  const auto stage = enumMember(f->find("stage"), MaterialStage::Publication);
  const auto code = enumMember(f->find("code"), MaterialFailureCode::TimedOut);
  const auto sourceState = enumMember(f->find("sourceState"), RegistrationState::Unknown);
  const auto* diagnostic = f->find("diagnostic");
  const auto* saved = f->find("savedMaterial");
  const auto material = guidMember(saved);
  const auto* exit = f->find("exitCode");
  std::uint64_t exitCode = 0;
  if (!stage || !code || !sourceState || !diagnostic || !diagnostic->isString() ||
      diagnostic->asString().size() > 4096 || (saved && !material) ||
      (material.has_value() != (*sourceState == RegistrationState::Committed)) ||
      (material && (target->mode == MaterialMode::Edit || target->mode == MaterialMode::Resume) &&
       material != target->material) ||
      f->asObject().size() != 4u + (saved ? 1u : 0u) + (exit ? 1u : 0u) ||
      (exit && (!exit->toUInt64(exitCode) || exitCode > UINT32_MAX))) return {};
  out.failure = MaterialFailure{*stage, *code, diagnostic->asString(), exit ? std::optional{static_cast<std::uint32_t>(exitCode)} : std::nullopt,
                                *sourceState, material};
  return out;
}
json::Value encodeMaterialStatus(const MaterialStatus& value) {
  auto out = J::object();
  out.set("activeGeneration", generationValue(value.activeGeneration));
  out.set("workspaceNeedsRepair", J::boolean(value.workspaceNeedsRepair));
  if (value.job) out.set("job", encodeMaterialJob(*value.job));
  return out;
}
std::optional<MaterialStatus> decodeMaterialStatus(const json::Value& value) {
  if (!value.isObject()) return {};
  const auto* repair = value.find("workspaceNeedsRepair");
  const auto* job = value.find("job");
  MaterialStatus out;
  if (!repair || !repair->isBool() || value.asObject().size() != (job ? 3u : 2u) ||
      !generationMember(value.find("activeGeneration"), out.activeGeneration)) return {};
  out.workspaceNeedsRepair = repair->asBool();
  if (job) {
    out.job = decodeMaterialJob(*job);
    if (!out.job) return {};
  }
  return out;
}
std::optional<MaterialError> decodeMaterialError(const json::Value& value) {
  return enumMember(&value, MaterialError::NotEditable);
}
std::optional<json::Value> encodeMaterialDraftPreview(const MaterialDraftPreview& image) {
  if (image.mesh == asset_system::AssetGuid{} || !validMaterialRecipe(image.recipe) ||
      !hasPreviewHeader(image.png)) return std::nullopt;
  auto value = json::Value::object();
  value.set("mesh", json::Value::string(asset_system::toString(image.mesh)));
  value.set("recipe", encodeMaterialRecipe(image.recipe));
  encodePreviewImage(value, image.png);
  return value;
}
std::optional<MaterialDraftPreview> decodeMaterialDraftPreview(const json::Value& payload,
    const asset_system::AssetGuid& mesh, const MaterialRecipe& recipe) {
  if (!payload.isObject() || payload.asObject().size() != 6 ||
      !matchesPreviewGuid(payload.find("mesh"), mesh)) return std::nullopt;
  const auto* returned = payload.find("recipe");
  if (!returned || decodeMaterialRecipe(*returned) != recipe) return std::nullopt;
  auto png = decodePreviewImage(payload);
  if (!png) return std::nullopt;
  return MaterialDraftPreview{mesh, recipe, std::move(*png)};
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
