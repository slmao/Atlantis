#include "authoring_fixture.h"

#include <atlantis/asset_system/scene_semantic_schema.h>
#include <atlantis/asset_system/scene_types.h>

#include <cmath>
#include <functional>
#include <limits>
#include <set>

// Plan 0065 M2 (P2; Spec 0065 R5; ADR-0130 D1, V-1): the document is valid
// after every applied transaction. One refusal per sceneSchema() constraint,
// by its own name -- the Spec 0049 conformance cases
// (tests/asset_system/scene_conformance_tests.cpp, constraintCases()) reached
// through commands -- and one per field domain; each registry is held
// complete against the schema.

using namespace authoring_test;

namespace {

struct ConstraintCase {
  scene::Constraint id;
  std::function<std::vector<ac::Command>()> commands;
  ac::Error expected;
};

[[nodiscard]] ac::Command setLight(std::uint32_t node, std::string_view leaf, ac::PropertyValue value) {
  return ac::SetProperty{ac::PropertyAddress{entity(node), kLight, field("Light", leaf)}, std::move(value)};
}

[[nodiscard]] std::vector<ConstraintCase> constraintCases() {
  using scene::Constraint;
  std::vector<ConstraintCase> out;
  out.push_back({Constraint::NonEmptyDocument,
                 [] {
                   return std::vector<ac::Command>{ac::SetActiveCamera{std::nullopt}, ac::DestroyNode{entity(2)},
                                                   ac::DestroyNode{entity(3)}, ac::DestroyNode{entity(1)}};
                 },
                 ac::Error::NonEmptyDocument});
  out.push_back({Constraint::NodeGuidNonNil,
                 [] { return std::vector<ac::Command>{ac::CreateNode{as::EntityGuid{}, 0, {}}}; },
                 ac::Error::NodeGuidNonNil});
  out.push_back({Constraint::NodeGuidUnique,
                 [] { return std::vector<ac::Command>{ac::CreateNode{entity(3), 0, {}}}; }, ac::Error::NodeGuidUnique});
  out.push_back({Constraint::ParentExists,
                 [] { return std::vector<ac::Command>{ac::SetParent{entity(2), entity(99)}}; }, ac::Error::ParentExists});
  out.push_back({Constraint::ParentAcyclic,  // node 1 (the camera) under node 2, which is under node 1
                 [] { return std::vector<ac::Command>{ac::SetParent{entity(1), entity(2)}}; }, ac::Error::ParentAcyclic});
  out.push_back({Constraint::ActiveCameraExists,
                 [] { return std::vector<ac::Command>{ac::SetActiveCamera{entity(99)}}; }, ac::Error::ActiveCameraExists});
  out.push_back({Constraint::ActiveCameraHasCamera,
                 [] { return std::vector<ac::Command>{ac::SetActiveCamera{entity(2)}}; },
                 ac::Error::ActiveCameraHasCamera});
  out.push_back({Constraint::ComponentExclusivity,  // the renderable node also a camera
                 [] { return std::vector<ac::Command>{ac::AddComponent{entity(2), kCamera}}; },
                 ac::Error::ComponentExclusivity});
  out.push_back({Constraint::LightRangeMatchesKind,  // a point light with range 0
                 [] { return std::vector<ac::Command>{setLight(3, "range", 0.0f)}; }, ac::Error::LightRangeMatchesKind});
  out.push_back({Constraint::MaxDirectionalLights,  // two directional lights (a new light's default kind)
                 [] {
                   return std::vector<ac::Command>{ac::CreateNode{entity(10), 3, {kLight}},
                                                   ac::CreateNode{entity(11), 4, {kLight}}};
                 },
                 ac::Error::MaxDirectionalLights});
  out.push_back({Constraint::MaxPointLights,  // kMaxPointLightsPerScene + 1 point lights
                 [] {
                   std::vector<ac::Command> commands;
                   for (std::uint32_t i = 0; i < as::kMaxPointLightsPerScene; ++i) {
                     const std::uint32_t node = 100 + i;
                     commands.push_back(ac::CreateNode{entity(node), 3, {kLight}});
                     commands.push_back(
                         setLight(node, "kind", wa::EnumValue{static_cast<std::int64_t>(scene::LightKind::Point)}));
                     commands.push_back(setLight(node, "range", 1.0f));
                   }
                   return commands;
                 },
                 ac::Error::MaxPointLights});
  return out;
}

// One field domain of sceneSchema(): a value outside it at an address on the
// base scene.
struct DomainCase {
  FieldId field;
  ac::PropertyAddress address;
  ac::PropertyValue outside;
  ac::Error expected = ac::Error::FieldOutOfDomain;
};

[[nodiscard]] std::vector<DomainCase> domainCases() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const auto camera = [](std::string_view owner, std::string_view leaf, ac::PropertyValue v) {
    return DomainCase{field(owner, leaf), address(kCameraNode, kCamera, field(owner, leaf)), std::move(v)};
  };
  const auto light = [](std::string_view leaf, ac::PropertyValue v) {
    return DomainCase{field("Light", leaf), address(kLightNode, kLight, field("Light", leaf)), std::move(v)};
  };
  const auto transform = [](std::string_view leaf, ac::PropertyValue v) {
    return DomainCase{field("Transform", leaf), address(kCameraNode, kTransform, field("Transform", leaf)), std::move(v)};
  };
  return {
      transform("localPosition", std::array<float, 3>{0.0f, nan, 0.0f}),
      transform("localEulerAnglesRadians", std::array<float, 3>{inf, 0.0f, 0.0f}),
      transform("localScale", std::array<float, 3>{1.0f, 1.0f, -inf}),
      camera("Camera", "fovYRadians", nan),
      camera("Camera", "nearZ", inf),
      camera("Camera", "farZ", nan),
      camera("Camera", "exposureCompensationEv", as::kExposureCompensationEvMax + 1.0f),
      camera("CameraFog", "color", std::array<float, 3>{0.0f, -1.0f, 0.0f}),
      camera("CameraFog", "density", -0.5f),
      camera("CameraFog", "height", inf),
      camera("CameraFog", "heightFalloff", -1.0f),
      camera("CameraFog", "maxOpacity", 1.5f),
      camera("CameraBloom", "strength", 2.0f),
      camera("CameraBloom", "threshold", -1.0f),
      DomainCase{field("Light", "kind"), address(kLightNode, kLight, field("Light", "kind")), wa::EnumValue{5},
                 ac::Error::EnumValueOutOfRange},
      light("color", std::array<float, 3>{1.5f, 0.0f, 0.0f}),
      light("intensity", -2.0f),
      light("range", nan),
      DomainCase{field("Renderable", "meshAsset"), address(kMeshNode, kRenderable, field("Renderable", "meshAsset")),
                 as::AssetGuid{}},
      DomainCase{field("Renderable", "materialAsset"),
                 address(kMeshNode, kRenderable, field("Renderable", "materialAsset")), as::AssetGuid{}},
  };
}

}  // namespace

TEST_CASE("authoring validity: every scene constraint has exactly one case", "[authoring][validity]") {
  std::set<scene::Constraint> registered;
  for (const auto& c : constraintCases()) CHECK(registered.insert(c.id).second);
  std::set<scene::Constraint> inSchema;
  for (const auto& rule : scene::sceneSchema().constraints) inSchema.insert(rule.id);
  CHECK(registered == inSchema);
}

TEST_CASE("authoring validity: each scene constraint refuses its transaction by name, and nothing changes",
          "[authoring][validity]") {
  for (const ConstraintCase& c : constraintCases()) {
    INFO(scene::toString(c.id));
    Fixture f;
    const SubscriptionId all = f.connection->subscribe({});
    CHECK(refusalOf(*f.connection, c.commands()) == c.expected);
    CHECK(f.document() == baseScene());
    CHECK(f.connection->drainEvents(all).value().empty());
  }
}

TEST_CASE("authoring validity: a directional light with a range is LightRangeMatchesKind (M1: was NotRepresentableInSource)",
          "[authoring][validity]") {
  Fixture f;
  CHECK(refusalOf(*f.connection, {setLight(3, "kind", wa::EnumValue{static_cast<std::int64_t>(scene::LightKind::Directional)})}) ==
        ac::Error::LightRangeMatchesKind);
}

TEST_CASE("authoring validity: the limits are inclusive", "[authoring][validity]") {
  Fixture f;
  std::vector<ac::Command> commands{ac::CreateNode{entity(10), 3, {kLight}}};  // the one directional light
  for (std::uint32_t i = 1; i < as::kMaxPointLightsPerScene; ++i) {  // the base has one point light
    const std::uint32_t node = 100 + i;
    commands.push_back(ac::CreateNode{entity(node), 3, {kLight}});
    commands.push_back(setLight(node, "kind", wa::EnumValue{static_cast<std::int64_t>(scene::LightKind::Point)}));
    commands.push_back(setLight(node, "range", 1.0f));
  }
  CHECK_FALSE(refusalOf(*f.connection, commands).has_value());
  CHECK(f.document().nodes.size() == 3 + as::kMaxPointLightsPerScene);
}

TEST_CASE("authoring validity: every field domain has a case", "[authoring][validity]") {
  std::set<std::uint64_t> registered;
  for (const auto& c : domainCases()) CHECK(registered.insert(c.field.value).second);
  std::set<std::uint64_t> inSchema;
  for (const auto& domain : scene::sceneSchema().domains) inSchema.insert(domain.field.value);
  CHECK(registered == inSchema);
}

TEST_CASE("authoring validity: a value outside its field's domain is refused, and nothing changes",
          "[authoring][validity]") {
  for (const DomainCase& c : domainCases()) {
    INFO(as::scene::toString(c.address));
    Fixture f;
    CHECK(refusalOf(*f.connection, {ac::SetProperty{c.address, c.outside}}) == c.expected);
    CHECK(f.document() == baseScene());
  }
}
