#include <atlantis/connection/asset_connection.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <array>
#include <span>
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
