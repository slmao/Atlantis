#include <atlantis/asset_system/scene_semantic_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string_view>
#include <vector>

#include <atlantis/asset_system/asset_system_schema.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/asset_system/scene_types.h>

// Plan 0049 M3 / P5-P7 (Spec 0049 R3, R4, R6; rulings Q2, Q7, J2, J5): the
// scene semantic schema's structure, its v1 content row by row against the
// R4 trace, the serializers' declarations, and the version/fingerprint pin.

namespace {

namespace as = atlantis::asset_system;
namespace scene = atlantis::asset_system::scene;
using atlantis::schema::FieldId;
using atlantis::schema::fieldId;
using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeId;
using atlantis::schema::typeId;
using atlantis::schema::TypeKind;

[[nodiscard]] const TypeDescriptor* findType(TypeId id) {
  for (const TypeDescriptor& type : as::assetSystemSchema()) {
    if (type.id == id) return &type;
  }
  return nullptr;
}

// Every type the schema's components reach, and their fields by id.
struct Reached {
  std::set<TypeId> types;
  std::map<FieldId, const atlantis::schema::FieldDescriptor*> fields;
};

[[nodiscard]] Reached reach() {
  Reached out;
  std::vector<TypeId> pending;
  for (const auto& rule : scene::sceneSchema().components) pending.push_back(rule.component);
  while (!pending.empty()) {
    const TypeId id = pending.back();
    pending.pop_back();
    if (!out.types.insert(id).second) continue;
    const TypeDescriptor* type = findType(id);
    REQUIRE(type != nullptr);
    for (const auto& field : type->fields) {
      out.fields.emplace(field.id, &field);
      if (field.kind != TypeKind::Primitive) pending.push_back(field.type);
    }
  }
  return out;
}

constexpr std::string_view kTransform = "asset_system::scene::Transform";
constexpr std::string_view kCamera = "asset_system::scene::Camera";
constexpr std::string_view kCameraFog = "asset_system::scene::CameraFog";
constexpr std::string_view kCameraBloom = "asset_system::scene::CameraBloom";
constexpr std::string_view kRenderable = "asset_system::scene::Renderable";
constexpr std::string_view kLight = "asset_system::scene::Light";

}  // namespace

// Spec 0049 R5/R6: each serializer declares the semantic version it encodes.
// A semantic bump fails this build until both declare it.
static_assert(as::kSceneSourceSemanticVersion == scene::kSemanticVersion);
static_assert(as::kSceneArtifactSemanticVersion == scene::kSemanticVersion);

TEST_CASE("scene semantic schema: version and fingerprint pin", "[asset_system][scene][schema]") {
  // Ruling Q2: change only together. A semantic change alters the
  // fingerprint; bump kSemanticVersion, re-pin, and have both serializers
  // declare the new version in the same PR.
  STATIC_REQUIRE(scene::kSemanticVersion == 1);
  CHECK(scene::sceneSchema().semanticVersion == scene::kSemanticVersion);
  CHECK(scene::sceneSemanticFingerprint() == 0x1c957031a7d6f224ULL);
}

TEST_CASE("scene semantic schema: components (ruling J5)", "[asset_system][scene][schema]") {
  const auto components = scene::sceneSchema().components;
  REQUIRE(components.size() == 4);
  std::map<TypeId, scene::ComponentRule> byId;
  for (const auto& rule : components) {
    const TypeDescriptor* type = findType(rule.component);
    REQUIRE(type != nullptr);
    CHECK(type->kind == TypeKind::Struct);
    byId.emplace(rule.component, rule);
  }
  CHECK(byId.at(typeId(kTransform)).cardinality == scene::Cardinality::Required);
  CHECK(byId.at(typeId(kTransform)).exclusiveGroup == 0);
  for (const auto name : {kCamera, kRenderable, kLight}) {
    INFO(name);
    CHECK(byId.at(typeId(name)).cardinality == scene::Cardinality::Optional);
    CHECK(byId.at(typeId(name)).exclusiveGroup == 1);
  }
}

TEST_CASE("scene semantic schema: one consistent domain per non-struct field", "[asset_system][scene][schema]") {
  const Reached reached = reach();
  std::set<FieldId> leaves;
  for (const auto& [id, field] : reached.fields) {
    if (field->kind != TypeKind::Struct) leaves.insert(id);
  }
  std::set<FieldId> withDomain;
  for (const scene::FieldDomain& domain : scene::sceneSchema().domains) {
    CHECK(withDomain.insert(domain.field).second);
    REQUIRE(reached.fields.contains(domain.field));
    const auto& field = *reached.fields.at(domain.field);
    INFO(field.name << " " << scene::toString(domain.kind));
    switch (domain.kind) {
      case scene::DomainKind::Enumerated: CHECK(field.kind == TypeKind::Enum); break;
      case scene::DomainKind::NonNilGuid:
        CHECK(field.kind == TypeKind::Primitive);
        CHECK((field.primitive == PrimitiveKind::AssetGuid || field.primitive == PrimitiveKind::EntityGuid));
        break;
      case scene::DomainKind::Closed: CHECK(domain.min <= domain.max); [[fallthrough]];
      case scene::DomainKind::AtLeast:
      case scene::DomainKind::Finite:
        CHECK(field.kind == TypeKind::Primitive);
        CHECK((field.primitive == PrimitiveKind::Float32 || field.primitive == PrimitiveKind::Vec3Float32));
        break;
    }
  }
  CHECK(withDomain == leaves);
}

TEST_CASE("scene semantic schema: v1 domains are R4's trace rows 11-19 and 23", "[asset_system][scene][schema]") {
  struct Row {
    std::string_view owner;
    std::string_view field;
    scene::DomainKind kind;
    float min;
    float max;
  };
  using K = scene::DomainKind;
  const Row rows[] = {
      {kTransform, "localPosition", K::Finite, 0, 0},                 // row 11
      {kTransform, "localEulerAnglesRadians", K::Finite, 0, 0},       // row 11
      {kTransform, "localScale", K::Finite, 0, 0},                    // row 11
      {kCamera, "fovYRadians", K::Finite, 0, 0},                      // row 12
      {kCamera, "nearZ", K::Finite, 0, 0},                            // row 12
      {kCamera, "farZ", K::Finite, 0, 0},                             // row 12
      {kCamera, "exposureCompensationEv", K::Closed, as::kExposureCompensationEvMin,
       as::kExposureCompensationEvMax},                               // row 13
      {kCameraFog, "color", K::Closed, 0.0f, as::kFogColorMax},       // row 14
      {kCameraFog, "density", K::AtLeast, 0.0f, 0},                   // row 14
      {kCameraFog, "height", K::Finite, 0, 0},                        // row 14
      {kCameraFog, "heightFalloff", K::AtLeast, 0.0f, 0},             // row 14
      {kCameraFog, "maxOpacity", K::Closed, 0.0f, 1.0f},              // row 14
      {kCameraBloom, "strength", K::Closed, 0.0f, 1.0f},              // row 15
      {kCameraBloom, "threshold", K::AtLeast, 0.0f, 0},               // row 15
      {kLight, "kind", K::Enumerated, 0, 0},                          // row 16
      {kLight, "color", K::Closed, 0.0f, 1.0f},                       // row 17
      {kLight, "intensity", K::AtLeast, 0.0f, 0},                     // row 18
      {kLight, "range", K::Finite, 0, 0},                             // rows 19-20 (with the constraint)
      {kRenderable, "meshAsset", K::NonNilGuid, 0, 0},                // row 23
      {kRenderable, "materialAsset", K::NonNilGuid, 0, 0},            // row 23
  };
  REQUIRE(scene::sceneSchema().domains.size() == std::size(rows));
  for (const Row& row : rows) {
    INFO(row.owner << "." << row.field);
    const scene::FieldDomain* found = nullptr;
    for (const auto& domain : scene::sceneSchema().domains) {
      if (domain.field == fieldId(row.owner, row.field)) found = &domain;
    }
    REQUIRE(found != nullptr);
    CHECK(found->kind == row.kind);
    if (row.kind == K::Closed || row.kind == K::AtLeast) CHECK(found->min == row.min);
    if (row.kind == K::Closed) CHECK(found->max == row.max);
  }
}

TEST_CASE("scene semantic schema: v1 constraints are R4's trace rows 1-8 and 20-22",
          "[asset_system][scene][schema]") {
  using C = scene::Constraint;
  const std::map<C, std::uint32_t> expected{
      {C::NonEmptyDocument, 0},      {C::NodeGuidNonNil, 0},        {C::NodeGuidUnique, 0},
      {C::ParentExists, 0},          {C::ParentAcyclic, 0},         {C::ActiveCameraExists, 0},
      {C::ActiveCameraHasCamera, 0}, {C::ComponentExclusivity, 0},  {C::LightRangeMatchesKind, 0},
      {C::MaxDirectionalLights, 1},  {C::MaxPointLights, as::kMaxPointLightsPerScene},
  };
  std::map<C, std::uint32_t> actual;
  for (const auto& rule : scene::sceneSchema().constraints) {
    CHECK(actual.emplace(rule.id, rule.limit).second);
    CHECK(scene::toString(rule.id) != "Unknown");
  }
  CHECK(actual == expected);
}
