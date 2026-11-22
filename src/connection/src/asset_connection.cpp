#include <atlantis/connection/asset_connection.h>
#include <atlantis/connection/authoring_connection.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <set>
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
constexpr std::array<std::string_view, 3> kContentErrors{"Unsupported", "CatalogUnavailable", "InvalidRequest"};
constexpr std::array<std::string_view, 5> kContentRowErrors{"UnknownAsset", "UnsupportedAssetType",
    "UnsupportedArtifactSchema", "UnreadableArtifact", "InvalidArtifact"};

std::optional<std::uint32_t> contentU32(const json::Value& object, std::string_view key) {
  const auto* value = object.find(key);
  std::uint64_t result = 0;
  if (!value || !value->toUInt64(result) || result == 0 || result > std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;
  return static_cast<std::uint32_t>(result);
}

std::optional<asset_system::AssetGuid> canonicalContentGuid(const json::Value* value) {
  if (!value || !value->isString()) return std::nullopt;
  const auto guid = asset_system::parseAssetGuid(value->asString());
  if (guid.isErr() || guid.value() == asset_system::AssetGuid{} || asset_system::toString(guid.value()) != value->asString())
    return std::nullopt;
  return guid.value();
}

bool validContentTexture(const TextureContentMetrics& texture) {
  if (texture.artifactSchema != 3 || !texture.width || texture.width > 8192 || !texture.height || texture.height > 8192 ||
      !texture.mipCount || texture.mipCount > 14 || !texture.pixelPayloadBytes ||
      texture.pixelPayloadBytes > 357913940) return false;
  const bool bc7 = texture.layout == ContentTextureLayout::Bc7;
  if (bc7 ? (texture.width % 4 != 0 || texture.height % 4 != 0) :
            texture.layout != ContentTextureLayout::Rgba8) return false;
  if (texture.mipCount > static_cast<std::uint32_t>(std::bit_width(std::max(texture.width, texture.height)))) return false;
  std::uint64_t bytes = 0;
  for (std::uint32_t level = 0; level < texture.mipCount; ++level) {
    const auto width = std::max(1u, texture.width >> level);
    const auto height = std::max(1u, texture.height >> level);
    bytes += bc7 ? std::uint64_t{(width + 3) / 4} * ((height + 3) / 4) * 16
                 : std::uint64_t{width} * height * 4;
  }
  return bytes == texture.pixelPayloadBytes;
}
}

std::string_view toString(ContentMetricsError error) noexcept {
  return kContentErrors[static_cast<std::size_t>(error)];
}
std::optional<ContentMetricsError> contentMetricsError(std::string_view error) noexcept {
  const auto it = std::ranges::find(kContentErrors, error);
  if (it == kContentErrors.end()) return std::nullopt;
  return static_cast<ContentMetricsError>(it - kContentErrors.begin());
}

bool validContentMetricsRequest(std::span<const asset_system::AssetGuid> guids) {
  if (guids.empty() || guids.size() > kContentMetricsLimit) return false;
  std::set<asset_system::AssetGuid> unique;
  for (const auto& id : guids)
    if (id == asset_system::AssetGuid{} || !unique.insert(id).second) return false;
  return true;
}

std::optional<std::vector<asset_system::AssetGuid>> decodeContentMetricsGuids(const json::Value& value) {
  if (!value.isArray() || value.asArray().size() > kContentMetricsLimit) return std::nullopt;
  std::vector<asset_system::AssetGuid> guids;
  for (const auto& entry : value.asArray()) {
    const auto guid = canonicalContentGuid(&entry);
    if (!guid) return std::nullopt;
    guids.push_back(*guid);
  }
  return validContentMetricsRequest(guids) ? std::optional{std::move(guids)} : std::nullopt;
}

std::optional<std::vector<ContentMetricRow>> decodeContentMetrics(const json::Value& value,
    std::span<const asset_system::AssetGuid> guids) {
  const auto* rows = value.find("rows");
  if (!value.isObject() || value.asObject().size() != 1 || !rows || !rows->isArray() ||
      rows->asArray().size() != guids.size() || !validContentMetricsRequest(guids) ||
      json::write(value).size() > kContentMetricsReplyBytes) return std::nullopt;
  std::vector<ContentMetricRow> result;
  for (const auto& row : rows->asArray()) {
    const auto guid = canonicalContentGuid(row.find("guid"));
    if (!row.isObject() || row.asObject().size() != 2 || !guid || *guid != guids[result.size()]) return std::nullopt;
    if (const auto* mesh = row.find("mesh"); mesh && mesh->isObject() && mesh->asObject().size() == 2) {
      const auto schema = contentU32(*mesh, "artifactSchema");
      const auto triangles = contentU32(*mesh, "indexedTriangles");
      if (!schema || (*schema != 4 && *schema != 5) || !triangles || *triangles > UINT32_MAX / 3) return std::nullopt;
      result.push_back({*guid, MeshContentMetrics{*schema, *triangles}});
    } else if (const auto* texture = row.find("texture"); texture && texture->isObject() && texture->asObject().size() == 6) {
      const auto schema = contentU32(*texture, "artifactSchema"), width = contentU32(*texture, "width"),
          height = contentU32(*texture, "height"), mips = contentU32(*texture, "mipCount"),
          bytes = contentU32(*texture, "pixelPayloadBytes");
      const auto* layout = texture->find("layout");
      if (!schema || !width || !height || !mips || !bytes || !layout || !layout->isString() ||
          (layout->asString() != "Rgba8" && layout->asString() != "Bc7")) return std::nullopt;
      const TextureContentMetrics decoded{*schema, *width, *height,
          layout->asString() == "Rgba8" ? ContentTextureLayout::Rgba8 : ContentTextureLayout::Bc7, *mips, *bytes};
      if (!validContentTexture(decoded)) return std::nullopt;
      result.push_back({*guid, decoded});
    } else if (const auto* error = row.find("err"); error && error->isString()) {
      const auto it = std::ranges::find(kContentRowErrors, error->asString());
      if (it == kContentRowErrors.end()) return std::nullopt;
      result.push_back({*guid, static_cast<ContentMetricError>(it - kContentRowErrors.begin())});
    } else return std::nullopt;
  }
  return result;
}

std::optional<json::Value> encodeContentMetrics(std::span<const ContentMetricRow> rows,
    std::span<const asset_system::AssetGuid> guids) {
  if (rows.size() != guids.size() || !validContentMetricsRequest(guids)) return std::nullopt;
  auto entries = json::Value::array();
  const auto number = [](auto value) { return json::Value::number(static_cast<std::uint64_t>(value)); };
  for (const auto& row : rows) {
    auto entry = json::Value::object();
    entry.set("guid", json::Value::string(asset_system::toString(row.guid)));
    if (const auto* mesh = std::get_if<MeshContentMetrics>(&row.result)) {
      auto result = json::Value::object();
      result.set("artifactSchema", number(mesh->artifactSchema));
      result.set("indexedTriangles", number(mesh->indexedTriangles));
      entry.set("mesh", std::move(result));
    } else if (const auto* texture = std::get_if<TextureContentMetrics>(&row.result)) {
      if (!validContentTexture(*texture)) return std::nullopt;
      auto result = json::Value::object();
      result.set("artifactSchema", number(texture->artifactSchema));
      result.set("width", number(texture->width));
      result.set("height", number(texture->height));
      result.set("layout", json::Value::string(texture->layout == ContentTextureLayout::Rgba8 ? "Rgba8" : "Bc7"));
      result.set("mipCount", number(texture->mipCount));
      result.set("pixelPayloadBytes", number(texture->pixelPayloadBytes));
      entry.set("texture", std::move(result));
    } else {
      const auto index = static_cast<std::size_t>(std::get<ContentMetricError>(row.result));
      if (index >= kContentRowErrors.size()) return std::nullopt;
      entry.set("err", json::Value::string(std::string(kContentRowErrors[index])));
    }
    entries.push(std::move(entry));
  }
  auto result = json::Value::object();
  result.set("rows", std::move(entries));
  return decodeContentMetrics(result, guids) ? std::optional{std::move(result)} : std::nullopt;
}

std::string_view toString(StaticGroupError error) noexcept {
  switch (error) {
    case StaticGroupError::Unsupported: return "Unsupported";
    case StaticGroupError::CatalogUnavailable: return "CatalogUnavailable";
    case StaticGroupError::UnknownAsset: return "UnknownAsset";
    case StaticGroupError::UnsupportedGroup: return "UnsupportedGroup";
    case StaticGroupError::InvalidGroup: return "InvalidGroup";
  }
  ATLANTIS_CHECK(false);
  return {};
}

std::optional<StaticGroupError> staticGroupError(std::string_view name) noexcept {
  using E = StaticGroupError;
  for (const auto error : {E::Unsupported, E::CatalogUnavailable, E::UnknownAsset, E::UnsupportedGroup, E::InvalidGroup})
    if (toString(error) == name) return error;
  return std::nullopt;
}

namespace {
using GroupGuid = asset_system::AssetGuid;
bool validGroup(const StaticGroup& group, const GroupGuid& requested, bool prefab) {
  const std::size_t minimum = prefab ? 1 : 2;
  const std::size_t limit = prefab ? 64 : kStaticGroupNodeLimit;
  if (group.scene == GroupGuid{} || group.scene != requested ||
      !(prefab ? authoring::validSaveAsName(group.name) : validImportName(group.name)) ||
      group.nodes.size() < minimum || group.nodes.size() > limit) return false;
  std::size_t renderables = 0;
  std::size_t roots = 0;
  for (const auto& node : group.nodes) {
    if (!node.parent) ++roots;
    if (node.name.size() > 255 || (!prefab && !node.name.empty())) return false;
    for (const auto& vector : {node.position, node.eulerRadians, node.scale})
      if (!std::ranges::all_of(vector, [](float n) { return std::isfinite(n); })) return false;
    if (!prefab && std::ranges::find(node.scale, 0.0f) != node.scale.end()) return false;
    std::size_t depth = 0;
    for (auto parent = node.parent; parent; parent = group.nodes[*parent].parent)
      if (*parent >= group.nodes.size() || ++depth >= group.nodes.size()) return false;
    if (!node.renderable) continue;
    if (node.renderable->mesh == GroupGuid{} || node.renderable->material == GroupGuid{}) return false;
    ++renderables;
  }
  return renderables >= minimum && (!prefab || roots == 1);
}

std::optional<GroupGuid> groupGuid(const json::Value* value) {
  if (!value || !value->isString()) return std::nullopt;
  const auto guid = asset_system::parseAssetGuid(value->asString());
  if (guid.isErr() || guid.value() == GroupGuid{} || asset_system::toString(guid.value()) != value->asString())
    return std::nullopt;
  return guid.value();
}
}  // namespace

std::optional<json::Value> encodeStaticGroup(const StaticGroup& group, const GroupGuid& requested, bool prefab) {
  if (!validGroup(group, requested, prefab)) return std::nullopt;
  using J = json::Value;
  const auto vec = [](const std::array<float, 3>& v) {
    return J::array({J::number(v[0]), J::number(v[1]), J::number(v[2])});
  };
  auto nodes = J::array();
  for (const auto& node : group.nodes) {
    auto renderable = J();
    if (node.renderable) {
      const auto& mesh = *node.renderable;
      renderable = J::object({{"mesh", J::string(asset_system::toString(mesh.mesh))},
                              {"material", mesh.material ? J::string(asset_system::toString(*mesh.material)) : J()}});
    }
    auto value = J::object({{"parent", node.parent ? J::number(static_cast<std::uint64_t>(*node.parent)) : J()},
                          {"position", vec(node.position)}, {"eulerRadians", vec(node.eulerRadians)},
                          {"scale", vec(node.scale)}, {"renderable", std::move(renderable)}});
    if (prefab) value.set("name", J::string(node.name));
    nodes.push(std::move(value));
  }
  auto payload = J::object({{"scene", J::string(asset_system::toString(group.scene))},
                            {"name", J::string(group.name)}, {"nodes", std::move(nodes)}});
  if (json::write(payload).size() > kStaticGroupByteLimit) return std::nullopt;
  return payload;
}

std::optional<StaticGroup> decodeStaticGroup(const json::Value& payload, const GroupGuid& requested, bool prefab) {
  const auto* name = payload.find("name");
  const auto* nodes = payload.find("nodes");
  if (!payload.isObject() || payload.asObject().size() != 3 ||
      !name || !name->isString() || !nodes || !nodes->isArray() || nodes->asArray().size() > (prefab ? 64 : kStaticGroupNodeLimit) ||
      groupGuid(payload.find("scene")) != requested || json::write(payload).size() > kStaticGroupByteLimit)
    return std::nullopt;
  StaticGroup group{requested, name->asString(), {}};
  const auto vec = [](const json::Value* value, std::array<float, 3>& out) {
    return value && value->isArray() && value->asArray().size() == 3 &&
           value->asArray()[0].toFloat(out[0]) && value->asArray()[1].toFloat(out[1]) &&
           value->asArray()[2].toFloat(out[2]);
  };
  for (const auto& value : nodes->asArray()) {
    const auto* parent = value.find("parent");
    const auto* renderable = value.find("renderable");
    StaticGroupNode node{};
    if (!value.isObject() || value.asObject().size() != (prefab ? 6u : 5u) || !parent || !renderable ||
        !vec(value.find("position"), node.position) || !vec(value.find("eulerRadians"), node.eulerRadians) ||
        !vec(value.find("scale"), node.scale)) return std::nullopt;
    if (prefab) {
      const auto* label = value.find("name");
      if (!label || !label->isString()) return std::nullopt;
      node.name = label->asString();
    }
    if (!parent->isNull()) {
      std::uint64_t index;
      if (!parent->toUInt64(index) || index >= nodes->asArray().size()) return std::nullopt;
      node.parent = static_cast<std::size_t>(index);
    }
    if (!renderable->isNull()) {
      const auto mesh = groupGuid(renderable->find("mesh"));
      const auto* material = renderable->find("material");
      if (!renderable->isObject() || renderable->asObject().size() != 2 || !mesh || !material)
        return std::nullopt;
      const auto materialGuid = groupGuid(material);
      if (!material->isNull() && !materialGuid) return std::nullopt;
      node.renderable = StaticGroupRenderable{*mesh, materialGuid};
    }
    group.nodes.push_back(node);
  }
  if (!validGroup(group, requested, prefab)) return std::nullopt;
  return group;
}

namespace {
constexpr std::array<std::string_view, 5> kReviewStates{"Pending", "Ready", "Blocked", "Failed", "Expired"};
constexpr std::array<std::string_view, 4> kChangeKinds{"Matched", "Added", "Removed", "Conflict"};
constexpr std::size_t kReviewRows = 4 * kStaticGroupNodeLimit;

template<class E, std::size_t N>
std::optional<E> reviewEnum(const json::Value* value, const std::array<std::string_view, N>& names) {
  if (!value || !value->isString()) return {};
  const auto found = std::ranges::find(names, value->asString());
  return found != names.end() ? std::optional{static_cast<E>(found - names.begin())} : std::nullopt;
}

const std::string* reviewText(const json::Value* value, std::size_t limit) {
  return value && value->isString() && value->asString().size() <= limit ? &value->asString() : nullptr;
}

json::Value reviewGuid(const GroupGuid& value) { return json::Value::string(asset_system::toString(value)); }

json::Value encodeRevision(const std::optional<StaticGroupRevisionRow>& revision) {
  if (!revision) return {};
  auto row = json::Value::object();
  row.set("label", json::Value::string(revision->label));
  row.set("index", json::Value::number(std::uint64_t{revision->index}));
  row.set("slot", revision->slot ? json::Value::number(std::uint64_t{*revision->slot}) : json::Value{});
  row.set("material", revision->material ? reviewGuid(*revision->material) : json::Value{});
  return row;
}

bool decodeRevision(const json::Value* data, std::optional<StaticGroupRevisionRow>& result) {
  if (!data) return false;
  if (data->isNull()) return true;
  const auto* label = reviewText(data->find("label"), 128);
  const auto* index = data->find("index");
  const auto* slot = data->find("slot");
  const auto* material = data->find("material");
  std::uint64_t i = 0, s = 0;
  if (!data->isObject() || data->asObject().size() != 4 || !label || !index || !slot || !material ||
      !index->toUInt64(i) || i >= kStaticGroupNodeLimit ||
      (!slot->isNull() && (!slot->toUInt64(s) || s >= kStaticGroupNodeLimit))) return false;
  auto guid = groupGuid(material);
  if (!material->isNull() && (!guid || slot->isNull())) return false;
  result.emplace(StaticGroupRevisionRow{*label, static_cast<std::uint32_t>(i),
      slot->isNull() ? std::nullopt : std::optional{static_cast<std::uint32_t>(s)}, guid});
  return true;
}
}

std::optional<StaticGroupReview> decodeStaticGroupReview(const json::Value& data) {
  if (!data.isObject() || data.asObject().size() != 7 || json::write(data).size() > kStaticGroupByteLimit) return {};
  const auto id = groupGuid(data.find("id")), scene = groupGuid(data.find("scene")), generation = groupGuid(data.find("generation"));
  const auto* name = reviewText(data.find("name"), 48);
  const auto* diagnostic = reviewText(data.find("diagnostic"), 1024);
  const auto state = reviewEnum<StaticGroupReviewState>(data.find("state"), kReviewStates);
  const auto* rows = data.find("rows");
  if (!id || !scene || !generation || !name || !validImportName(*name) || !diagnostic || !state || !rows ||
      !rows->isArray() || rows->asArray().size() > kReviewRows) return {};
  const bool ready = *state == StaticGroupReviewState::Ready;
  if ((ready && rows->asArray().empty()) ||
      (!ready && *state != StaticGroupReviewState::Blocked && !rows->asArray().empty())) return {};
  StaticGroupReview review{*id, *scene, GenerationId{*generation}, *name, *state, *diagnostic, {}};
  std::set<std::string> keys;
  for (const auto& row : rows->asArray()) {
    const auto* key = reviewText(row.find("key"), 75);
    const auto guid = groupGuid(row.find("guid"));
    const auto change = reviewEnum<StaticGroupChange>(row.find("change"), kChangeKinds);
    const auto* note = reviewText(row.find("diagnostic"), 160);
    std::optional<StaticGroupRevisionRow> before, after;
    if (!row.isObject() || row.asObject().size() != 6 || !key || !keys.insert(*key).second || !guid || !change || !note ||
        !decodeRevision(row.find("before"), before) || !decodeRevision(row.find("after"), after)) return {};
    const bool isMesh = key->starts_with("mesh-key/");
    if ((!isMesh && !key->starts_with("material-key/")) ||
        (before && before->slot.has_value() != isMesh) || (after && after->slot.has_value() != isMesh) ||
        (!before && !after) || (*change == StaticGroupChange::Added && (before || !after)) ||
        (*change == StaticGroupChange::Removed && (!before || after)) ||
        (*change == StaticGroupChange::Matched && (!before || !after || before->material != after->material)) ||
        (ready && (*change == StaticGroupChange::Conflict || *change == StaticGroupChange::Removed))) return {};
    review.rows.push_back({*key, *guid, *change, std::move(before), std::move(after), *note});
  }
  return review;
}

std::optional<json::Value> encodeStaticGroupReview(const StaticGroupReview& review) {
  const auto state = static_cast<std::size_t>(review.state);
  if (state >= kReviewStates.size() || review.rows.size() > kReviewRows) return {};
  auto data = json::Value::object();
  data.set("id", reviewGuid(review.id));
  data.set("scene", reviewGuid(review.scene));
  data.set("generation", reviewGuid(review.generation.value));
  data.set("name", json::Value::string(review.name));
  data.set("state", json::Value::string(std::string(kReviewStates[state])));
  data.set("diagnostic", json::Value::string(review.diagnostic));
  auto rows = json::Value::array();
  for (const auto& row : review.rows) {
    const auto change = static_cast<std::size_t>(row.change);
    if (change >= kChangeKinds.size()) return {};
    auto dataRow = json::Value::object();
    dataRow.set("key", json::Value::string(row.key));
    dataRow.set("guid", reviewGuid(row.guid));
    dataRow.set("change", json::Value::string(std::string(kChangeKinds[change])));
    dataRow.set("before", encodeRevision(row.before));
    dataRow.set("after", encodeRevision(row.after));
    dataRow.set("diagnostic", json::Value::string(row.diagnostic));
    rows.push(std::move(dataRow));
  }
  data.set("rows", std::move(rows));
  return decodeStaticGroupReview(data) ? std::optional{std::move(data)} : std::nullopt;
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

std::string_view toString(TextureRemovalError value) noexcept {
  using E = TextureRemovalError;
  switch (value) {
    case E::Unsupported: return "Unsupported";
    case E::Unavailable: return "Unavailable";
    case E::Busy: return "Busy";
    case E::WorkspaceNeedsRepair: return "WorkspaceNeedsRepair";
    case E::CatalogUnavailable: return "CatalogUnavailable";
    case E::UnknownAsset: return "UnknownAsset";
    case E::UnsupportedProducer: return "UnsupportedProducer";
    case E::StaleCheck: return "StaleCheck";
    case E::UnknownCheck: return "UnknownCheck";
  }
  return "Unavailable";
}

std::string_view toString(TextureRemovalState value) noexcept {
  using S = TextureRemovalState;
  switch (value) {
    case S::Checking: return "Checking";
    case S::Ready: return "Ready";
    case S::Referenced: return "Referenced";
    case S::CheckIncomplete: return "CheckIncomplete";
    case S::StaleCheck: return "StaleCheck";
  }
  return "CheckIncomplete";
}

std::string_view toString(TextureReferenceSlot value) noexcept {
  using S = TextureReferenceSlot;
  switch (value) {
    case S::BaseColor: return "BaseColor";
    case S::Normal: return "Normal";
    case S::MetallicRoughness: return "MetallicRoughness";
    case S::Occlusion: return "Occlusion";
    case S::Emissive: return "Emissive";
  }
  return {};
}

std::string_view toString(RenameTextureError value) noexcept {
  return token(value, std::array{"Unsupported", "Unavailable", "Busy", "WorkspaceNeedsRepair", "CatalogUnavailable",
      "UnknownAsset", "UnsupportedProducer", "InvalidName", "NameConflict", "StaleSnapshot", "Unchanged"});
}
std::string_view toString(MoveTextureError value) noexcept {
  return token(value, std::array{"Unsupported", "Unavailable", "Busy", "WorkspaceNeedsRepair", "CatalogUnavailable",
      "UnknownAsset", "UnsupportedProducer", "InvalidPath", "PathConflict", "StaleSnapshot", "Unchanged"});
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
bool basicMaterialValues(const MaterialValues& values) noexcept {
  return !values.normalMap && !values.emissiveTexture && !values.metallicRoughnessMap && !values.occlusionMap &&
         std::ranges::all_of(values.emissiveFactor, [](float value) { return value == 0.0f; });
}
bool representsMaterialValues(const MaterialValues& values, MaterialRecipeVersion version) noexcept {
  if (version == MaterialRecipeVersion::Basic) return basicMaterialValues(values);
  if (version == MaterialRecipeVersion::WithCombinedMaps) return true;
  if (version == MaterialRecipeVersion::WithOcclusion)
    return !values.occlusionMap || (!values.normalMap && !values.metallicRoughnessMap);
  return !values.occlusionMap &&
         (version == MaterialRecipeVersion::WithMetallicRoughness || !values.metallicRoughnessMap);
}
bool validMaterialValues(const MaterialValues& values) noexcept {
  const auto factor = [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; };
  return values.texture != asset_system::AssetGuid{} && std::ranges::all_of(values.color, factor) &&
         factor(values.metallic) && factor(values.roughness) &&
         (!values.normalMap || *values.normalMap != asset_system::AssetGuid{}) &&
         (!values.emissiveTexture || *values.emissiveTexture != asset_system::AssetGuid{}) &&
         (!values.metallicRoughnessMap || *values.metallicRoughnessMap != asset_system::AssetGuid{}) &&
         (!values.occlusionMap || *values.occlusionMap != asset_system::AssetGuid{}) &&
         std::ranges::all_of(values.emissiveFactor, [](float value) {
           return std::isfinite(value) && value >= 0.0f && value <= 65504.0f;
         });
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
json::Value encodeMaterialValues(const MaterialValues& values, MaterialRecipeVersion version) {
  auto out = json::Value::object();
  out.set("texture", json::Value::string(asset_system::toString(values.texture)));
  auto color = json::Value::array();
  for (const float value : values.color) color.push(json::Value::number(value));
  out.set("color", std::move(color));
  out.set("metallic", json::Value::number(values.metallic));
  out.set("roughness", json::Value::number(values.roughness));
  if (version != MaterialRecipeVersion::Basic) {
    const auto slot = [](const auto& guid) {
      return guid ? json::Value::string(asset_system::toString(*guid)) : json::Value{};
    };
    out.set("normalMap", slot(values.normalMap));
    out.set("emissiveTexture", slot(values.emissiveTexture));
    if (version >= MaterialRecipeVersion::WithMetallicRoughness)
      out.set("metallicRoughnessMap", slot(values.metallicRoughnessMap));
    if (version >= MaterialRecipeVersion::WithOcclusion)
      out.set("occlusionMap", slot(values.occlusionMap));
    auto factor = json::Value::array();
    for (float component : values.emissiveFactor) factor.push(json::Value::number(component));
    out.set("emissiveFactor", std::move(factor));
  }
  return out;
}
std::optional<MaterialValues> decodeMaterialValues(const json::Value& value, MaterialRecipeVersion version) {
  const bool withMaps = version != MaterialRecipeVersion::Basic;
  const bool packed = version >= MaterialRecipeVersion::WithMetallicRoughness;
  const bool occlusion = version >= MaterialRecipeVersion::WithOcclusion;
  if (!value.isObject() || value.asObject().size() != (occlusion ? 9u : packed ? 8u : withMaps ? 7u : 4u)) return std::nullopt;
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
  if (!metallic->toFloat(out.metallic) || !roughness->toFloat(out.roughness)) return std::nullopt;
  if (withMaps) {
    const auto slot = [&](std::string_view name, std::optional<asset_system::AssetGuid>& dst) {
      const auto* field = value.find(name);
      if (!field) return false;
      if (field->isNull()) return true;
      if (!field->isString()) return false;
      auto parsed = asset_system::parseAssetGuid(field->asString());
      if (parsed.isErr() || field->asString() != asset_system::toString(parsed.value())) return false;
      dst = parsed.value();
      return true;
    };
    if (packed && !slot("metallicRoughnessMap", out.metallicRoughnessMap)) return std::nullopt;
    if (occlusion && !slot("occlusionMap", out.occlusionMap)) return std::nullopt;
    const auto* factor = value.find("emissiveFactor");
    if (!slot("normalMap", out.normalMap) || !slot("emissiveTexture", out.emissiveTexture) ||
        !factor || !factor->isArray() || factor->asArray().size() != out.emissiveFactor.size()) return std::nullopt;
    for (std::size_t i = 0; i < out.emissiveFactor.size(); ++i)
      if (!factor->asArray()[i].toFloat(out.emissiveFactor[i])) return std::nullopt;
  }
  if (!validMaterialValues(out) || !representsMaterialValues(out, version)) return std::nullopt;
  return out;
}
json::Value encodeMaterialRecipe(const MaterialRecipe& recipe, MaterialRecipeVersion version) {
  auto value = json::Value::object();
  value.set("values", encodeMaterialValues(recipe.values, version));
  value.set("filter", json::Value::string(recipe.filter == MaterialFilter::Linear ? "Linear" : "Nearest"));
  value.set("address", json::Value::string(recipe.address == MaterialAddress::Repeat ? "Repeat" : "ClampToEdge"));
  return value;
}
std::optional<MaterialRecipe> decodeMaterialRecipe(const json::Value& value, MaterialRecipeVersion version) {
  if (!value.isObject() || value.asObject().size() != 3) return std::nullopt;
  const auto* values = value.find("values");
  const auto* filter = value.find("filter");
  const auto* address = value.find("address");
  if (!values || !filter || !filter->isString() || !address || !address->isString()) return std::nullopt;
  auto fields = decodeMaterialValues(*values, version);
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
json::Value encodeMaterialSave(const MaterialSave& input, MaterialRecipeVersion version) {
  auto value = encodeTarget(materialTarget(input));
  std::visit([&](const auto& request) {
    using T = std::decay_t<decltype(request)>;
    if constexpr (!std::is_same_v<T, ResumeMaterial>) value.set("values", encodeMaterialValues(request.values, version));
    if constexpr (std::is_same_v<T, EditMaterial>) {
      value.set("activeGeneration", generationValue(request.activeGeneration));
      value.set("expected", encodeMaterialValues(request.expected, version));
    }
  }, input);
  return value;
}
std::optional<MaterialSave> decodeMaterialSave(const json::Value& value, MaterialRecipeVersion version) {
  const auto target = decodeTarget(value);
  if (!target) return {};
  const auto count = value.asObject().size();
  if (target->mode == MaterialMode::Resume)
    return count == 2 ? std::optional<MaterialSave>{ResumeMaterial{*target->material}} : std::nullopt;
  const auto* member = value.find("values");
  auto values = member ? decodeMaterialValues(*member, version) : std::nullopt;
  if (!values) return {};
  if (target->mode == MaterialMode::Create && count == 3) return CreateMaterial{target->name, *values};
  if (target->mode == MaterialMode::Copy && count == 4)
    return CopyMaterial{target->name, *target->material, *values};
  if (target->mode == MaterialMode::Edit && count == 5) {
    const auto* expected = value.find("expected");
    std::optional<GenerationId> generation;
    if (expected && generationMember(value.find("activeGeneration"), generation))
      if (auto fields = decodeMaterialValues(*expected, version)) return EditMaterial{*target->material, generation, *fields, *values};
  }
  return {};
}
json::Value encodeMaterialRead(const MaterialRead& value, MaterialRecipeVersion version) {
  auto out = J::object();
  out.set("material", guidValue(value.material));
  out.set("recipe", encodeMaterialRecipe(value.recipe, version));
  out.set("editable", J::boolean(value.editable));
  out.set("activeGeneration", generationValue(value.activeGeneration));
  return out;
}
std::optional<MaterialRead> decodeMaterialRead(const json::Value& value, MaterialRecipeVersion version) {
  if (!value.isObject() || value.asObject().size() != 4) return {};
  const auto material = guidMember(value.find("material"));
  const auto* recipe = value.find("recipe");
  const auto* editable = value.find("editable");
  std::optional<GenerationId> generation;
  if (!material || !recipe || !editable || !editable->isBool() ||
      !generationMember(value.find("activeGeneration"), generation)) return {};
  auto decoded = decodeMaterialRecipe(*recipe, version);
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
std::optional<json::Value> encodeMaterialDraftPreview(const MaterialDraftPreview& image, MaterialRecipeVersion version) {
  if (image.mesh == asset_system::AssetGuid{} || !validMaterialRecipe(image.recipe) ||
      !hasPreviewHeader(image.png)) return std::nullopt;
  auto value = json::Value::object();
  value.set("mesh", json::Value::string(asset_system::toString(image.mesh)));
  value.set("recipe", encodeMaterialRecipe(image.recipe, version));
  encodePreviewImage(value, image.png);
  return value;
}
std::optional<MaterialDraftPreview> decodeMaterialDraftPreview(const json::Value& payload,
    const asset_system::AssetGuid& mesh, const MaterialRecipe& recipe, MaterialRecipeVersion version) {
  if (!payload.isObject() || payload.asObject().size() != 6 ||
      !matchesPreviewGuid(payload.find("mesh"), mesh)) return std::nullopt;
  const auto* returned = payload.find("recipe");
  if (!returned || decodeMaterialRecipe(*returned, version) != recipe) return std::nullopt;
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
bool validTextureLogicalPath(std::string_view path) noexcept {
  if (!path.starts_with("textures/") || !path.ends_with(".png")) return false;
  path.remove_prefix(9);
  path.remove_suffix(4);
  for (int folders = 0;; ++folders) {
    const auto slash = path.find('/');
    if (!validImportName(path.substr(0, slash))) return false;
    if (slash == std::string_view::npos) return true;
    if (folders == 4) return false;
    path.remove_prefix(slash + 1);
  }
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

namespace {
using json::Value;
using namespace std::string_view_literals;
constexpr std::array projectErrors{"Unsupported"sv, "Unavailable"sv, "Busy"sv, "UnknownScan"sv};
constexpr std::array projectRoots{"Inbox"sv, "Prefabs"sv};
constexpr std::array projectCoverage{"Complete"sv, "Incomplete"sv, "Unavailable"sv};
constexpr std::array projectKinds{"Directory"sv, "Gltf"sv, "Png"sv, "Prefab"sv, "Other"sv};
constexpr std::array projectRegistrations{"Unknown"sv, "SourceOnly"sv, "Registered"sv};

template<class Enum, std::size_t N>
std::optional<Enum> fromProjectName(const std::array<std::string_view, N>& names, const Value* member) {
  if (!member || !member->isString()) return {};
  const auto found = std::ranges::find(names, member->asString());
  return found == names.end() ? std::nullopt : std::optional{static_cast<Enum>(found - names.begin())};
}

std::optional<asset_system::AssetGuid> projectGuid(const Value* member) {
  if (!member || !member->isString()) return {};
  auto parsed = asset_system::parseAssetGuid(member->asString());
  if (parsed.isErr() || parsed.value() == asset_system::AssetGuid{} ||
      asset_system::toString(parsed.value()) != member->asString()) return {};
  return parsed.value();
}

Value stringValue(std::string_view text) { return Value::string(std::string(text)); }
}

std::string_view toString(ProjectScanError value) noexcept { return projectErrors[static_cast<std::size_t>(value)]; }
std::string_view toString(ProjectRootKind value) noexcept { return projectRoots[static_cast<std::size_t>(value)]; }
std::string_view toString(ProjectCoverage value) noexcept { return projectCoverage[static_cast<std::size_t>(value)]; }
std::string_view toString(ProjectSourceKind value) noexcept { return projectKinds[static_cast<std::size_t>(value)]; }
std::optional<ProjectScanError> projectScanError(std::string_view name) noexcept {
  const auto found = std::ranges::find(projectErrors, name);
  return found == projectErrors.end() ? std::nullopt : std::optional{static_cast<ProjectScanError>(found - projectErrors.begin())};
}

bool validProjectPath(std::string_view path) noexcept {
  if (path.empty() || path.size() > 512 || path.ends_with('/')) return false;
  std::size_t levels = 0;
  while (!path.empty()) {
    const auto slash = path.find('/');
    const auto part = path.substr(0, slash);
    if (++levels > 5 || part.empty() || part.starts_with('.') || part.ends_with('.') || part.ends_with(' ') ||
        part.find_first_of("\\:*?\"<>|") != std::string_view::npos ||
        std::ranges::any_of(part, [](unsigned char c) { return c < 32 || c == 127; })) return false;
    if (slash == std::string_view::npos) return true;
    path.remove_prefix(slash + 1);
  }
  return false;
}

json::Value encodeProjectScan(const ProjectScan& scan) {
  auto roots = Value::array();
  for (const auto& root : scan.roots) {
    auto sources = Value::array();
    for (const auto& row : root.sources) {
      auto outputs = Value::array();
      for (const auto& guid : row.outputs) outputs.push(stringValue(asset_system::toString(guid)));
      auto value = Value::object();
      value.set("path", stringValue(row.path));
      value.set("kind", stringValue(toString(row.kind)));
      value.set("importName", row.importName ? stringValue(*row.importName) : Value());
      value.set("evidence", stringValue(row.evidence));
      value.set("registration", stringValue(projectRegistrations[static_cast<std::size_t>(row.registration)]));
      value.set("producer", row.producer ? stringValue(asset_system::toString(*row.producer)) : Value());
      value.set("outputs", std::move(outputs));
      value.set("newerPrefabSource", Value::boolean(row.newerPrefabSource));
      sources.push(std::move(value));
    }
    auto rootValue = Value::object();
    rootValue.set("kind", stringValue(toString(root.kind)));
    rootValue.set("coverage", stringValue(toString(root.coverage)));
    rootValue.set("diagnostic", stringValue(root.diagnostic));
    rootValue.set("sources", std::move(sources));
    roots.push(std::move(rootValue));
  }
  auto result = Value::object();
  result.set("id", stringValue(asset_system::toString(scan.id)));
  result.set("pending", Value::boolean(scan.pending));
  result.set("catalogReady", Value::boolean(scan.catalogReady));
  result.set("roots", std::move(roots));
  return result;
}

std::optional<ProjectScan> decodeProjectScan(const json::Value& value) {
  if (!value.isObject() || value.asObject().size() != 4 || json::write(value).size() > kProjectResultBytes) return {};
  const auto id = projectGuid(value.find("id"));
  const auto* pending = value.find("pending");
  const auto* ready = value.find("catalogReady");
  const auto* roots = value.find("roots");
  if (!id || !pending || !pending->isBool() || !ready || !ready->isBool() || !roots || !roots->isArray() ||
      (pending->asBool() ? ready->asBool() || !roots->asArray().empty() : roots->asArray().size() != 2)) return {};
  ProjectScan scan{*id, pending->asBool(), ready->asBool(), {}};
  std::set<ProjectRootKind> usedRoots;
  std::set<asset_system::AssetGuid> usedLinks;
  std::size_t entries = 0;
  for (const auto& node : roots->asArray()) {
    if (!node.isObject() || node.asObject().size() != 4) return {};
    const auto kind = fromProjectName<ProjectRootKind>(projectRoots, node.find("kind"));
    const auto coverage = fromProjectName<ProjectCoverage>(projectCoverage, node.find("coverage"));
    const auto* diagnostic = node.find("diagnostic");
    const auto* sources = node.find("sources");
    if (!kind || !usedRoots.insert(*kind).second || !coverage || !diagnostic || !diagnostic->isString() ||
        diagnostic->asString().size() > 256 || !sources || !sources->isArray() ||
        (*coverage == ProjectCoverage::Unavailable && !sources->asArray().empty())) return {};
    ProjectRoot root{*kind, *coverage, diagnostic->asString(), {}};
    std::set<std::string> paths;
    for (const auto& row : sources->asArray()) {
      if (++entries > kProjectEntries || !row.isObject() || row.asObject().size() != 8) return {};
      const auto* path = row.find("path");
      const auto sourceKind = fromProjectName<ProjectSourceKind>(projectKinds, row.find("kind"));
      const auto registration = fromProjectName<ProjectRegistration>(projectRegistrations, row.find("registration"));
      const auto* name = row.find("importName");
      const auto* evidence = row.find("evidence");
      const auto* producer = row.find("producer");
      const auto* outputs = row.find("outputs");
      const auto* newer = row.find("newerPrefabSource");
      if (!path || !path->isString() || !validProjectPath(path->asString()) || !paths.insert(path->asString()).second ||
          !sourceKind || !registration || !name || !evidence || !evidence->isString() ||
          !authoring::validPrefabDigest(evidence->asString()) || !producer || !outputs || !outputs->isArray() ||
          !newer || !newer->isBool()) return {};
      const auto rootGuid = projectGuid(producer);
      if ((!producer->isNull() && !rootGuid) || (*registration == ProjectRegistration::Registered) != rootGuid.has_value() ||
          (*coverage != ProjectCoverage::Complete && *registration != ProjectRegistration::Unknown) ||
          ((!rootGuid || !scan.catalogReady) && !outputs->asArray().empty()) ||
          (newer->asBool() && (*sourceKind != ProjectSourceKind::Prefab || outputs->asArray().empty()))) return {};
      ProjectSource item{path->asString(), *sourceKind, {}, evidence->asString(),
                         *registration, rootGuid, {}, newer->asBool()};
      if (!name->isNull()) {
        if (!name->isString() || !validImportName(name->asString()) || *kind != ProjectRootKind::Inbox) return {};
        const auto& slug = name->asString();
        const bool gltf = *sourceKind == ProjectSourceKind::Gltf && item.path == slug + ".gltf";
        const bool png = *sourceKind == ProjectSourceKind::Png && item.path == slug + "/source.png";
        if (!gltf && !png) return {};
        item.importName = slug;
      }
      for (const auto& output : outputs->asArray()) {
        auto linked = projectGuid(&output);
        if (!linked || !usedLinks.insert(*linked).second || usedLinks.size() > kProjectLinks) return {};
        item.outputs.push_back(*linked);
      }
      if ((*kind == ProjectRootKind::Prefabs && *sourceKind != ProjectSourceKind::Directory &&
           *sourceKind != ProjectSourceKind::Prefab) ||
          (*kind == ProjectRootKind::Inbox && *sourceKind == ProjectSourceKind::Prefab) ||
          (*sourceKind == ProjectSourceKind::Directory && std::ranges::count(item.path, '/') >= 4) ||
          (*registration != ProjectRegistration::Unknown && !item.importName && *sourceKind != ProjectSourceKind::Prefab) ||
          ((*sourceKind == ProjectSourceKind::Png || *sourceKind == ProjectSourceKind::Prefab) &&
           (item.outputs.size() > 1 || (!item.outputs.empty() && item.outputs.front() != rootGuid)))) return {};
      root.sources.push_back(std::move(item));
    }
    scan.roots.push_back(std::move(root));
  }
  return scan;
}

}  // namespace atlantis::connection::assets
