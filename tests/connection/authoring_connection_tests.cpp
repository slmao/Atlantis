#include <atlantis/connection/authoring_connection.h>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

// Plan 0065 M2 (P3; ADR-0130 D1): AuthoringConnection's value helpers --
// event kinds, filters and the closed error sets' names. The InProcess
// endpoint is Atlantis Authoring's, tested in tests/authoring.

namespace ac = atlantis::connection::authoring;
using atlantis::asset_system::EntityGuid;
using atlantis::schema::TypeId;

namespace {

[[nodiscard]] EntityGuid node(std::uint8_t n) {
  EntityGuid guid;
  guid.bytes[15] = std::byte{n};
  return guid;
}

const TypeId kLight{0x11};
const TypeId kCamera{0x22};

}  // namespace

TEST_CASE("authoring connection: kindOf follows the event alternatives", "[connection][authoring]") {
  CHECK(ac::kindOf(ac::PropertyChanged{}) == ac::EventKind::PropertyChanged);
  CHECK(ac::kindOf(ac::NodeCreated{}) == ac::EventKind::NodeCreated);
  CHECK(ac::kindOf(ac::NodeDestroyed{}) == ac::EventKind::NodeDestroyed);
  CHECK(ac::kindOf(ac::ComponentAdded{}) == ac::EventKind::ComponentAdded);
  CHECK(ac::kindOf(ac::ComponentRemoved{}) == ac::EventKind::ComponentRemoved);
  CHECK(ac::kindOf(ac::ParentChanged{}) == ac::EventKind::ParentChanged);
  CHECK(ac::kindOf(ac::ActiveCameraChanged{}) == ac::EventKind::ActiveCameraChanged);
  CHECK(ac::kindOf(ac::DocumentReopened{}) == ac::EventKind::DocumentReopened);
  CHECK(ac::kindOf(ac::Applied{3}) == ac::EventKind::Applied);
}

TEST_CASE("authoring connection: a filter matches kind, node and component", "[connection][authoring]") {
  const ac::Event property = ac::PropertyChanged{{node(1), kLight, {}}, 1.0f};
  const ac::Event added = ac::ComponentAdded{node(2), kCamera};
  const ac::Event parent = ac::ParentChanged{node(1), std::nullopt};
  const ac::Event applied = ac::Applied{4};
  const ac::Event camera = ac::ActiveCameraChanged{node(1)};

  const ac::EventFilter everything{};
  for (const auto& e : {property, added, parent, applied, camera}) CHECK(everything.matches(e));

  const ac::EventFilter byNode{ac::EventKindSet::all(), node(1), std::nullopt};
  CHECK(byNode.matches(property));
  CHECK(byNode.matches(parent));
  CHECK_FALSE(byNode.matches(added));
  CHECK_FALSE(byNode.matches(applied));  // Applied carries no node
  CHECK_FALSE(byNode.matches(camera));   // nor does ActiveCameraChanged

  const ac::EventFilter byComponent{ac::EventKindSet::all(), std::nullopt, kCamera};
  CHECK(byComponent.matches(added));
  CHECK_FALSE(byComponent.matches(property));
  CHECK_FALSE(byComponent.matches(parent));

  const ac::EventFilter byKind{ac::EventKindSet::only(ac::EventKind::Applied).with(ac::EventKind::NodeCreated),
                               std::nullopt, std::nullopt};
  CHECK(byKind.matches(applied));
  CHECK_FALSE(byKind.matches(property));
  CHECK(ac::EventKindSet{}.contains(ac::EventKind::Applied) == false);
}

TEST_CASE("authoring connection: every error has its own name", "[connection][authoring]") {
  std::set<std::string_view> names;
  for (int e = 0; e <= static_cast<int>(ac::Error::NotRepresentableInSource); ++e) {
    const std::string_view name = ac::toString(static_cast<ac::Error>(e));
    CHECK(name != "Unknown");
    CHECK(names.insert(name).second);
  }
  CHECK(ac::toString(ac::Error::MaxPointLights) == "MaxPointLights");
  CHECK(ac::toString(ac::OpenError::SourceInvalid) == "SourceInvalid");
  CHECK(ac::toString(ac::SaveError::ImportedSceneNotSavable) == "ImportedSceneNotSavable");
  CHECK(ac::toString(ac::ApplyError::NoSceneSink) == "NoSceneSink");
}
