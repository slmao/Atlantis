#include <atlantis/asset_system/scene_semantic_schema.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <atlantis/asset_system/asset_system_schema.h>
#include <atlantis/asset_system/scene_types.h>

namespace atlantis::asset_system::scene {

namespace {

using schema::fieldId;
using schema::typeId;

constexpr std::string_view kTransform = "asset_system::scene::Transform";
constexpr std::string_view kCamera = "asset_system::scene::Camera";
constexpr std::string_view kCameraFog = "asset_system::scene::CameraFog";
constexpr std::string_view kCameraBloom = "asset_system::scene::CameraBloom";
constexpr std::string_view kRenderable = "asset_system::scene::Renderable";
constexpr std::string_view kLight = "asset_system::scene::Light";

constexpr std::array kComponents{
    ComponentRule{typeId(kTransform), Cardinality::Required, 0},
    ComponentRule{typeId(kCamera), Cardinality::Optional, 1},
    ComponentRule{typeId(kRenderable), Cardinality::Optional, 1},
    ComponentRule{typeId(kLight), Cardinality::Optional, 1},
};

static_assert(std::count_if(kComponents.begin(), kComponents.end(), [](const ComponentRule& rule) {
                return rule.cardinality == Cardinality::Required;
              }) == 1,
              "exactly one component (Transform) is Required");

constexpr FieldDomain finite(std::string_view owner, std::string_view field) {
  return {fieldId(owner, field), DomainKind::Finite, 0.0f, 0.0f};
}
constexpr FieldDomain closed(std::string_view owner, std::string_view field, float min, float max) {
  return {fieldId(owner, field), DomainKind::Closed, min, max};
}
constexpr FieldDomain atLeast(std::string_view owner, std::string_view field, float min) {
  return {fieldId(owner, field), DomainKind::AtLeast, min, 0.0f};
}
constexpr FieldDomain of(std::string_view owner, std::string_view field, DomainKind kind) {
  return {fieldId(owner, field), kind, 0.0f, 0.0f};
}

// Plan 0049's R4 trace, rows 11-19 and 23; limits reuse the codecs' own
// constants so the schema and the codecs cannot disagree on a number.
constexpr std::array kDomains{
    finite(kTransform, "localPosition"),
    finite(kTransform, "localEulerAnglesRadians"),
    finite(kTransform, "localScale"),
    finite(kCamera, "fovYRadians"),
    finite(kCamera, "nearZ"),
    finite(kCamera, "farZ"),
    closed(kCamera, "exposureCompensationEv", kExposureCompensationEvMin, kExposureCompensationEvMax),
    closed(kCameraFog, "color", 0.0f, kFogColorMax),
    atLeast(kCameraFog, "density", 0.0f),
    finite(kCameraFog, "height"),
    atLeast(kCameraFog, "heightFalloff", 0.0f),
    closed(kCameraFog, "maxOpacity", 0.0f, 1.0f),
    closed(kCameraBloom, "strength", 0.0f, 1.0f),
    atLeast(kCameraBloom, "threshold", 0.0f),
    of(kLight, "kind", DomainKind::Enumerated),
    closed(kLight, "color", 0.0f, 1.0f),
    atLeast(kLight, "intensity", 0.0f),
    finite(kLight, "range"),  // and LightRangeMatchesKind
    of(kRenderable, "meshAsset", DomainKind::NonNilGuid),
    of(kRenderable, "materialAsset", DomainKind::NonNilGuid),
};

// R4 trace rows 1-8, 20-22. The artifact's node-count bound is a format
// capacity, not semantics (Plan 0049 ruling J2), so it is not listed.
constexpr std::array kConstraints{
    ConstraintRule{Constraint::NonEmptyDocument, 0},
    ConstraintRule{Constraint::NodeGuidNonNil, 0},
    ConstraintRule{Constraint::NodeGuidUnique, 0},
    ConstraintRule{Constraint::ParentExists, 0},
    ConstraintRule{Constraint::ParentAcyclic, 0},
    ConstraintRule{Constraint::ActiveCameraExists, 0},
    ConstraintRule{Constraint::ActiveCameraHasCamera, 0},
    ConstraintRule{Constraint::ComponentExclusivity, 0},
    ConstraintRule{Constraint::LightRangeMatchesKind, 0},
    ConstraintRule{Constraint::MaxDirectionalLights, 1},
    ConstraintRule{Constraint::MaxPointLights, kMaxPointLightsPerScene},
};

constexpr SceneSchema kSceneSchema{kSemanticVersion, kComponents, kDomains, kConstraints};

// Canonical little-endian byte stream for the fingerprint (Plan 0049 P7).
class Canonical {
 public:
  void u8(std::uint8_t v) { bytes_.push_back(static_cast<char>(v)); }
  void u16(std::uint16_t v) { little(v, 2); }
  void u32(std::uint32_t v) { little(v, 4); }
  void u64(std::uint64_t v) { little(v, 8); }
  void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
  void text(std::string_view s) {
    u32(static_cast<std::uint32_t>(s.size()));
    bytes_.append(s);
  }
  [[nodiscard]] const std::string& bytes() const noexcept { return bytes_; }

 private:
  void little(std::uint64_t v, int count) {
    for (int i = 0; i < count; ++i) bytes_.push_back(static_cast<char>((v >> (8 * i)) & 0xffu));
  }
  std::string bytes_;
};

[[nodiscard]] const schema::TypeDescriptor* findType(schema::TypeId id) {
  for (const schema::TypeDescriptor& type : assetSystemSchema()) {
    if (type.id == id) return &type;
  }
  return nullptr;
}

}  // namespace

const SceneSchema& sceneSchema() noexcept { return kSceneSchema; }

std::uint64_t sceneSemanticFingerprint() {
  // Every descriptor the components reach, sorted (and de-duplicated) by id.
  std::map<std::uint64_t, const schema::TypeDescriptor*> reached;
  std::vector<schema::TypeId> pending;
  for (const ComponentRule& rule : kSceneSchema.components) pending.push_back(rule.component);
  while (!pending.empty()) {
    const schema::TypeId id = pending.back();
    pending.pop_back();
    if (reached.contains(id.value)) continue;
    const schema::TypeDescriptor* type = findType(id);
    if (type == nullptr) continue;  // a dangling id: the schema tests report it
    reached.emplace(id.value, type);
    for (const schema::FieldDescriptor& field : type->fields) {
      if (field.kind != schema::TypeKind::Primitive) pending.push_back(field.type);
    }
  }

  Canonical out;
  for (const auto& [id, type] : reached) {
    out.u64(id);
    out.u8(static_cast<std::uint8_t>(type->kind));
    out.u32(type->schemaVersion);
    std::vector<const schema::FieldDescriptor*> fields;
    for (const schema::FieldDescriptor& field : type->fields) fields.push_back(&field);
    std::sort(fields.begin(), fields.end(), [](auto* a, auto* b) { return a->id < b->id; });
    out.u32(static_cast<std::uint32_t>(fields.size()));
    for (const schema::FieldDescriptor* field : fields) {
      out.u64(field->id.value);
      out.u8(static_cast<std::uint8_t>(field->kind));
      out.u8(field->kind == schema::TypeKind::Primitive ? static_cast<std::uint8_t>(field->primitive) : 0xffu);
      out.u64(field->type.value);
      out.u16(static_cast<std::uint16_t>(field->flags));
    }
    std::vector<schema::EnumConstantDescriptor> constants(type->constants.begin(), type->constants.end());
    std::sort(constants.begin(), constants.end(), [](const auto& a, const auto& b) { return a.value < b.value; });
    out.u32(static_cast<std::uint32_t>(constants.size()));
    for (const auto& constant : constants) {
      out.u64(static_cast<std::uint64_t>(constant.value));
      out.text(constant.name);
    }
  }

  std::vector<ComponentRule> components(kSceneSchema.components.begin(), kSceneSchema.components.end());
  std::sort(components.begin(), components.end(), [](const auto& a, const auto& b) { return a.component < b.component; });
  out.u32(static_cast<std::uint32_t>(components.size()));
  for (const ComponentRule& rule : components) {
    out.u64(rule.component.value);
    out.u8(static_cast<std::uint8_t>(rule.cardinality));
    out.u8(rule.exclusiveGroup);
  }

  std::vector<FieldDomain> domains(kSceneSchema.domains.begin(), kSceneSchema.domains.end());
  std::sort(domains.begin(), domains.end(), [](const auto& a, const auto& b) { return a.field < b.field; });
  out.u32(static_cast<std::uint32_t>(domains.size()));
  for (const FieldDomain& domain : domains) {
    out.u64(domain.field.value);
    out.u8(static_cast<std::uint8_t>(domain.kind));
    out.f32(domain.min);
    out.f32(domain.max);
  }

  std::vector<ConstraintRule> constraints(kSceneSchema.constraints.begin(), kSceneSchema.constraints.end());
  std::sort(constraints.begin(), constraints.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  out.u32(static_cast<std::uint32_t>(constraints.size()));
  for (const ConstraintRule& rule : constraints) {
    out.u8(static_cast<std::uint8_t>(rule.id));
    out.u32(rule.limit);
  }

  return schema::fnv1a64(out.bytes());
}

std::string_view toString(Constraint constraint) noexcept {
  switch (constraint) {
    case Constraint::NonEmptyDocument: return "NonEmptyDocument";
    case Constraint::NodeGuidNonNil: return "NodeGuidNonNil";
    case Constraint::NodeGuidUnique: return "NodeGuidUnique";
    case Constraint::ParentExists: return "ParentExists";
    case Constraint::ParentAcyclic: return "ParentAcyclic";
    case Constraint::ActiveCameraExists: return "ActiveCameraExists";
    case Constraint::ActiveCameraHasCamera: return "ActiveCameraHasCamera";
    case Constraint::ComponentExclusivity: return "ComponentExclusivity";
    case Constraint::LightRangeMatchesKind: return "LightRangeMatchesKind";
    case Constraint::MaxDirectionalLights: return "MaxDirectionalLights";
    case Constraint::MaxPointLights: return "MaxPointLights";
  }
  return "Unknown";
}

std::string_view toString(DomainKind kind) noexcept {
  switch (kind) {
    case DomainKind::Finite: return "Finite";
    case DomainKind::Closed: return "Closed";
    case DomainKind::AtLeast: return "AtLeast";
    case DomainKind::NonNilGuid: return "NonNilGuid";
    case DomainKind::Enumerated: return "Enumerated";
  }
  return "Unknown";
}

}  // namespace atlantis::asset_system::scene
