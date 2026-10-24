#include "authoring_fixture.h"

#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/authoring/document_validation.h>

#include <algorithm>
#include <array>
#include <limits>

// Plan 0065 M2 (P2, P3; Spec 0065 R1-R6, R9; ADR-0129 D1-D4, ADR-0130 D1-D2):
// the authoring session through its InProcess AuthoringConnection -- queries,
// each command and its events, transactions all or nothing, subscriptions and
// failures per connection, open and save. GPU-free.

using namespace authoring_test;

namespace {

[[nodiscard]] std::vector<ac::Event> drain(AuthoringConnection& connection, SubscriptionId subscription) {
  auto events = connection.drainEvents(subscription);
  REQUIRE(events.isOk());
  return events.value();
}

}  // namespace

// ================================================================== queries

TEST_CASE("authoring: before open() there is no document; open() reads it and reports DocumentReopened",
          "[authoring][session]") {
  Fixture f(baseScene(), /*open=*/false);
  AuthoringConnection& c = *f.connection;
  CHECK_FALSE(c.document().has_value());
  CHECK(c.listNodes().error() == ac::Error::NoDocument);
  CHECK(c.listComponents(entity(1)).error() == ac::Error::NoDocument);
  CHECK(c.getProperty(address(kCameraNode, kCamera, field("Camera", "nearZ"))).error() == ac::Error::NoDocument);
  CHECK(c.activeCamera().error() == ac::Error::NoDocument);
  CHECK(refusalOf(c, {ac::SetActiveCamera{}}) == ac::Error::NoDocument);
  CHECK(c.save().error() == ac::SaveError::NoDocument);
  CHECK(c.apply().error() == ac::ApplyError::NoDocument);

  const SubscriptionId all = c.subscribe({});
  REQUIRE(c.open().isOk());
  CHECK(drain(c, all) == std::vector<ac::Event>{ac::DocumentReopened{sceneGuid()}});
  const auto info = c.document();
  REQUIRE(info.has_value());
  CHECK(info->scene == sceneGuid());
  CHECK(info->provenance == ac::Provenance::HandAuthored);
  CHECK(info->sourcePath == f.source.generic_string());
  CHECK(info->nodeCount == 3);
  CHECK(f.document() == baseScene());
}

TEST_CASE("authoring: the queries read the document", "[authoring][session]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  CHECK(c.listNodes().value() == std::vector<ac::NodeSummary>{{entity(1), std::nullopt},
                                                             {entity(2), entity(1)},
                                                             {entity(3), std::nullopt}});
  std::vector<TypeId> cameraComponents{kTransform, kCamera};
  std::sort(cameraComponents.begin(), cameraComponents.end());
  CHECK(c.listComponents(entity(1)).value() == cameraComponents);
  CHECK(c.listComponents(entity(99)).error() == ac::Error::UnknownNode);
  CHECK(c.activeCamera().value() == entity(1));

  CHECK(c.getProperty(address(kCameraNode, kCamera, field("Camera", "fovYRadians"))).value() == ac::PropertyValue{1.0f});
  // A nested leaf, addressed under its node component (Spec 0049 R7).
  CHECK(c.getProperty(address(kCameraNode, kCamera, field("CameraFog", "density"))).value() ==
        ac::PropertyValue{0.125f});
  CHECK(c.getProperty(address(kCameraNode, kTransform, field("Transform", "localPosition"))).value() ==
        ac::PropertyValue{std::array<float, 3>{0.0f, 1.0f, 5.0f}});
  CHECK(c.getProperty(address(kLightNode, kLight, field("Light", "kind"))).value() ==
        ac::PropertyValue{wa::EnumValue{static_cast<std::int64_t>(scene::LightKind::Point)}});
  CHECK(c.getProperty(address(kMeshNode, kRenderable, field("Renderable", "materialAsset"))).value() ==
        ac::PropertyValue{asset(2)});
  CHECK(c.getProperty(address(kMeshNode, kCamera, field("Camera", "nearZ"))).error() == ac::Error::ComponentMissing);
  CHECK(c.getProperty(address(kCameraNode, kCameraFog, field("CameraFog", "density"))).error() ==
        ac::Error::UnknownComponentType);
  CHECK(c.getProperty(address(kCameraNode, kCamera, field("Light", "range"))).error() == ac::Error::UnknownField);

  // The schema: the scene types reachable from the node components, nothing else.
  std::vector<std::string_view> names;
  for (const auto& type : c.schema()) names.push_back(type.name);
  std::sort(names.begin(), names.end());
  CHECK(names == std::vector<std::string_view>{"asset_system::scene::Camera", "asset_system::scene::CameraBloom",
                                               "asset_system::scene::CameraFog", "asset_system::scene::Light",
                                               "asset_system::scene::LightKind", "asset_system::scene::Renderable",
                                               "asset_system::scene::Transform"});
}

// ================================================================= commands

TEST_CASE("authoring: SetProperty writes a leaf and reports PropertyChanged", "[authoring][session][commands]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});

  const auto intensity = address(kLightNode, kLight, field("Light", "intensity"));
  const auto density = address(kCameraNode, kCamera, field("CameraFog", "density"));
  const auto color = address(kLightNode, kLight, field("Light", "color"));
  const auto material = address(kMeshNode, kRenderable, field("Renderable", "materialAsset"));
  CHECK_FALSE(refusalOf(c, {ac::SetProperty{intensity, 9.0f}}).has_value());
  CHECK_FALSE(refusalOf(c, {ac::SetProperty{density, 0.0f}}).has_value());
  CHECK_FALSE(refusalOf(c, {ac::SetProperty{color, std::array<float, 3>{1.0f, 0.0f, 0.5f}}}).has_value());
  CHECK_FALSE(refusalOf(c, {ac::SetProperty{material, wa::Absent{}}}).has_value());
  CHECK(drain(c, all) == std::vector<ac::Event>{ac::PropertyChanged{intensity, 9.0f}, ac::PropertyChanged{density, 0.0f},
                                                ac::PropertyChanged{color, std::array<float, 3>{1.0f, 0.0f, 0.5f}},
                                                ac::PropertyChanged{material, wa::Absent{}}});
  CHECK(f.document().nodes[kLightNode].light->intensity == 9.0f);
  CHECK(f.document().nodes[kCameraNode].camera->fog.density == 0.0f);
  CHECK(f.document().nodes[kLightNode].light->color == std::array<float, 3>{1.0f, 0.0f, 0.5f});
  CHECK_FALSE(f.document().nodes[kMeshNode].renderable->materialAsset.has_value());
  CHECK(c.getProperty(material).value() == ac::PropertyValue{wa::Absent{}});
}

TEST_CASE("authoring: SetProperty's addressing refusals change nothing", "[authoring][session][commands]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});
  const struct {
    ac::SetProperty command;
    ac::Error expected;
  } cases[] = {
      {{ac::PropertyAddress{entity(99), kLight, field("Light", "intensity")}, 1.0f}, ac::Error::UnknownNode},
      {{address(kCameraNode, kCameraFog, field("CameraFog", "density")), 1.0f}, ac::Error::UnknownComponentType},
      {{address(kCameraNode, kCamera, field("Light", "range")), 1.0f}, ac::Error::UnknownField},
      {{address(kCameraNode, kLight, field("Light", "intensity")), 1.0f}, ac::Error::ComponentMissing},
      {{address(kLightNode, kLight, field("Light", "color")), 1.0f}, ac::Error::KindMismatch},
      {{address(kLightNode, kLight, field("Light", "intensity")), wa::Absent{}}, ac::Error::KindMismatch},
      {{address(kLightNode, kLight, field("Light", "kind")), wa::EnumValue{7}}, ac::Error::EnumValueOutOfRange},
  };
  for (const auto& entry : cases) {
    CHECK(refusalOf(c, {entry.command}) == entry.expected);
  }
  CHECK(drain(c, all).empty());
  CHECK(f.document() == baseScene());
}

TEST_CASE("authoring: CreateNode places a node in the order, with a given or minted EntityGuid",
          "[authoring][session][commands]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});

  // A given GUID, at position 1, with a (directional, range 0) light.
  CHECK_FALSE(refusalOf(c, {ac::CreateNode{entity(10), 1, {kLight}}}).has_value());
  CHECK(drain(c, all) == std::vector<ac::Event>{ac::NodeCreated{entity(10), 1}, ac::ComponentAdded{entity(10), kLight}});
  REQUIRE(f.document().nodes.size() == 4);
  CHECK(f.document().nodes[1].guid == entity(10));
  CHECK(f.document().nodes[1].light == scene::Light{});
  CHECK(f.document().nodes[1].transform == scene::Transform{});

  // A minted GUID (ADR-0097 D1's mint, Plan 0065 J8), at the end.
  CHECK_FALSE(refusalOf(c, {ac::CreateNode{std::nullopt, 4, {}}}).has_value());
  const auto events = drain(c, all);
  REQUIRE(events.size() == 1);
  const auto* created = std::get_if<ac::NodeCreated>(&events.front());
  REQUIRE(created != nullptr);
  CHECK(created->position == 4);
  CHECK(created->node != as::EntityGuid{});
  CHECK((created->node.bytes[6] & std::byte{0xF0}) == std::byte{0x40});  // version 4
  CHECK(f.document().nodes.back().guid == created->node);

  // Refusals.
  CHECK(refusalOf(c, {ac::CreateNode{entity(11), 99, {}}}) == ac::Error::NodePositionOutOfRange);
  CHECK(refusalOf(c, {ac::CreateNode{entity(11), 0, {kTransform}}}) == ac::Error::ComponentAlreadyPresent);
  CHECK(refusalOf(c, {ac::CreateNode{entity(11), 0, {kCamera, kCamera}}}) == ac::Error::ComponentAlreadyPresent);
  CHECK(refusalOf(c, {ac::CreateNode{entity(11), 0, {kCameraFog}}}) == ac::Error::UnknownComponentType);
  // A renderable at its defaults names no mesh: refused alone, accepted with its mesh in one transaction.
  CHECK(refusalOf(c, {ac::CreateNode{entity(11), 0, {kRenderable}}}) == ac::Error::FieldOutOfDomain);
  CHECK_FALSE(refusalOf(c, {ac::CreateNode{entity(11), 0, {kRenderable}},
                            ac::SetProperty{ac::PropertyAddress{entity(11), kRenderable, field("Renderable", "meshAsset")},
                                            asset(7)}})
                   .has_value());
  CHECK(f.document().nodes.front().renderable->meshAsset == asset(7));
}

TEST_CASE("authoring: DestroyNode removes a node and reports NodeDestroyed", "[authoring][session][commands]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});
  CHECK(refusalOf(c, {ac::DestroyNode{entity(99)}}) == ac::Error::UnknownNode);
  // The camera is the mesh's parent and the active camera.
  CHECK(refusalOf(c, {ac::DestroyNode{entity(1)}}) == ac::Error::ParentExists);
  CHECK_FALSE(refusalOf(c, {ac::DestroyNode{entity(3)}}).has_value());
  CHECK(drain(c, all) == std::vector<ac::Event>{ac::NodeDestroyed{entity(3)}});
  CHECK(c.listNodes().value() ==
        std::vector<ac::NodeSummary>{{entity(1), std::nullopt}, {entity(2), entity(1)}});
}

TEST_CASE("authoring: AddComponent and RemoveComponent", "[authoring][session][commands]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});
  CHECK(refusalOf(c, {ac::AddComponent{entity(99), kLight}}) == ac::Error::UnknownNode);
  CHECK(refusalOf(c, {ac::AddComponent{entity(1), kCameraFog}}) == ac::Error::UnknownComponentType);
  CHECK(refusalOf(c, {ac::AddComponent{entity(1), kCamera}}) == ac::Error::ComponentAlreadyPresent);
  CHECK(refusalOf(c, {ac::AddComponent{entity(1), kTransform}}) == ac::Error::ComponentAlreadyPresent);
  CHECK(refusalOf(c, {ac::RemoveComponent{entity(1), kTransform}}) == ac::Error::ComponentRequired);
  CHECK(refusalOf(c, {ac::RemoveComponent{entity(1), kLight}}) == ac::Error::ComponentMissing);
  CHECK(refusalOf(c, {ac::RemoveComponent{entity(1), kCamera}}) == ac::Error::ActiveCameraHasCamera);
  // A camera and a light on one node: refused by name, no longer only by the
  // cook's round trip (M1).
  CHECK(refusalOf(c, {ac::AddComponent{entity(1), kLight}}) == ac::Error::ComponentExclusivity);
  CHECK(drain(c, all).empty());

  CHECK_FALSE(refusalOf(c, {ac::RemoveComponent{entity(3), kLight}}).has_value());
  CHECK_FALSE(refusalOf(c, {ac::AddComponent{entity(3), kCamera}}).has_value());
  CHECK(drain(c, all) == std::vector<ac::Event>{ac::ComponentRemoved{entity(3), kLight}, ac::ComponentAdded{entity(3), kCamera}});
  CHECK(f.document().nodes[kLightNode].camera == scene::Camera{});
  CHECK_FALSE(f.document().nodes[kLightNode].light.has_value());
}

TEST_CASE("authoring: SetParent changes the parent only; SetActiveCamera", "[authoring][session][commands]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});
  CHECK(refusalOf(c, {ac::SetParent{entity(99), std::nullopt}}) == ac::Error::UnknownNode);
  CHECK(refusalOf(c, {ac::SetParent{entity(2), entity(99)}}) == ac::Error::ParentExists);
  CHECK(refusalOf(c, {ac::SetParent{entity(1), entity(2)}}) == ac::Error::ParentAcyclic);
  CHECK_FALSE(refusalOf(c, {ac::SetParent{entity(3), entity(2)}}).has_value());
  CHECK_FALSE(refusalOf(c, {ac::SetParent{entity(2), std::nullopt}}).has_value());
  CHECK(c.listNodes().value() == std::vector<ac::NodeSummary>{{entity(1), std::nullopt},
                                                             {entity(2), std::nullopt},
                                                             {entity(3), entity(2)}});  // the order is unchanged

  CHECK(refusalOf(c, {ac::SetActiveCamera{entity(99)}}) == ac::Error::ActiveCameraExists);
  CHECK(refusalOf(c, {ac::SetActiveCamera{entity(3)}}) == ac::Error::ActiveCameraHasCamera);
  CHECK_FALSE(refusalOf(c, {ac::SetActiveCamera{std::nullopt}}).has_value());
  CHECK(c.activeCamera().value() == std::nullopt);
  CHECK(drain(c, all) == std::vector<ac::Event>{ac::ParentChanged{entity(3), entity(2)},
                                                ac::ParentChanged{entity(2), std::nullopt},
                                                ac::ActiveCameraChanged{std::nullopt}});
}

// ============================================================= transactions

TEST_CASE("authoring: a transaction is all or nothing, and its failure names the refusing command",
          "[authoring][session][transactions]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});
  const auto intensity = address(kLightNode, kLight, field("Light", "intensity"));
  const auto density = address(kCameraNode, kCamera, field("CameraFog", "density"));

  // A validity refusal names the transaction's first command.
  const wa::TransactionTicket invalid =
      c.submitTransaction({ac::SetProperty{intensity, 5.0f}, ac::SetProperty{density, -1.0f}});
  CHECK(invalid.count == 2);
  auto failures = c.drainFailures();
  REQUIRE(failures.size() == 1);
  CHECK(failures.front() == ac::CommandFailure{invalid.first, ac::Error::FieldOutOfDomain});

  // An addressing refusal names the refused command.
  const wa::TransactionTicket misaddressed =
      c.submitTransaction({ac::SetProperty{intensity, 5.0f}, ac::DestroyNode{entity(99)}});
  failures = c.drainFailures();
  REQUIRE(failures.size() == 1);
  CHECK(failures.front() == ac::CommandFailure{wa::CommandTicket{misaddressed.first.value + 1}, ac::Error::UnknownNode});
  CHECK(misaddressed.contains(failures.front().ticket));
  CHECK(drain(c, all).empty());
  CHECK(f.document() == baseScene());

  // A transaction may pass through an invalid state (a point light with range
  // 0 on the way to a directional one).
  const auto kind = address(kLightNode, kLight, field("Light", "kind"));
  const auto range = address(kLightNode, kLight, field("Light", "range"));
  const wa::TransactionTicket ok = c.submitTransaction(
      {ac::SetProperty{range, 0.0f}, ac::SetProperty{kind, wa::EnumValue{static_cast<std::int64_t>(scene::LightKind::Directional)}}});
  CHECK(c.drainFailures().empty());
  CHECK(ok.first.value == misaddressed.first.value + 2);
  CHECK(drain(c, all) ==
        std::vector<ac::Event>{ac::PropertyChanged{range, 0.0f},
                               ac::PropertyChanged{kind, wa::EnumValue{static_cast<std::int64_t>(scene::LightKind::Directional)}}});

  // An empty transaction takes no ticket's effect.
  const wa::TransactionTicket empty = c.submitTransaction({});
  CHECK(empty.count == 0);
  CHECK(c.drainFailures().empty());
  CHECK(drain(c, all).empty());
}

// ================================================================== events

TEST_CASE("authoring: every connection sees every change, filtered per subscription; failures go to the submitter",
          "[authoring][session][events]") {
  Fixture f;
  auto second = f.session.connect();
  AuthoringConnection& a = *f.connection;
  AuthoringConnection& b = *second;

  const SubscriptionId lightOnly = b.subscribe({ac::EventKindSet::all(), entity(3), std::nullopt});
  const SubscriptionId cameraProperties =
      b.subscribe({ac::EventKindSet::only(ac::EventKind::PropertyChanged), std::nullopt, kCamera});
  CHECK(lightOnly.value != cameraProperties.value);

  const auto intensity = address(kLightNode, kLight, field("Light", "intensity"));
  const auto fov = address(kCameraNode, kCamera, field("Camera", "fovYRadians"));
  a.submit(ac::SetProperty{intensity, 3.0f});
  a.submit(ac::SetProperty{fov, 0.75f});
  a.submit(ac::SetActiveCamera{std::nullopt});
  a.submit(ac::DestroyNode{entity(99)});  // refused

  CHECK(drain(b, lightOnly) == std::vector<ac::Event>{ac::PropertyChanged{intensity, 3.0f}});
  CHECK(drain(b, cameraProperties) == std::vector<ac::Event>{ac::PropertyChanged{fov, 0.75f}});
  CHECK(a.drainFailures().size() == 1);
  CHECK(b.drainFailures().empty());

  // Unknown and closed subscriptions.
  CHECK(b.drainEvents(SubscriptionId{999}).error() == atlantis::connection::ConnectionError::UnknownSubscription);
  CHECK(b.unsubscribe(lightOnly).isOk());
  CHECK(b.unsubscribe(lightOnly).error() == atlantis::connection::ConnectionError::UnknownSubscription);
  CHECK(b.drainEvents(lightOnly).error() == atlantis::connection::ConnectionError::UnknownSubscription);

  // A subscription sees changes from the moment it is made.
  const SubscriptionId late = a.subscribe({});
  CHECK(drain(a, late).empty());
}

TEST_CASE("authoring: the host's reportApplied() reaches every subscription as Applied{generation}",
          "[authoring][session][events]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  const SubscriptionId all = c.subscribe({});
  const SubscriptionId onlyApplied = c.subscribe({ac::EventKindSet::only(ac::EventKind::Applied), std::nullopt, std::nullopt});
  const SubscriptionId byNode = c.subscribe({ac::EventKindSet::all(), entity(1), std::nullopt});
  f.session.reportApplied(7);
  CHECK(drain(c, all) == std::vector<ac::Event>{ac::Applied{7}});
  CHECK(drain(c, onlyApplied) == std::vector<ac::Event>{ac::Applied{7}});
  CHECK(drain(c, byNode).empty());  // Applied carries no node
}

// ============================================================= session calls

TEST_CASE("authoring: apply() cooks the document in memory and hands the decoded scene to the host's sink",
          "[authoring][session][apply]") {
  Fixture f;
  CHECK(f.connection->apply().error() == ac::ApplyError::NoSceneSink);

  std::vector<as::ValidatedSceneData> received;
  bool accept = true;
  f.session.setSceneSink([&](as::ValidatedSceneData scene) {
    received.push_back(std::move(scene));
    return accept;
  });
  f.connection->submit(ac::SetProperty{address(kLightNode, kLight, field("Light", "intensity")), 7.0f});
  REQUIRE(f.connection->apply().isOk());
  REQUIRE(received.size() == 1);
  // The decoded scene is the build's cook of the document's saved text (ADR-0131 D2).
  const auto cooked = as::cookSceneSource(textOf(f.document()), sceneGuid());
  REQUIRE(cooked.isOk());
  const auto expected = as::decodeScene(cooked.value());
  REQUIRE(expected.isOk());
  REQUIRE(received.front().nodeCount() == expected.value().nodeCount());
  REQUIRE(received.front().node(kLightNode).light.has_value());
  CHECK(received.front().node(kLightNode).light->intensity == 7.0f);
  for (std::size_t i = 0; i < expected.value().nodeCount(); ++i) {
    CHECK(received.front().entityGuid(i) == expected.value().entityGuid(i));
    CHECK(received.front().parentOf(i) == expected.value().parentOf(i));
  }

  accept = false;
  CHECK(f.connection->apply().error() == ac::ApplyError::ReloadRefused);
  CHECK(received.size() == 2);
}

TEST_CASE("authoring: save() writes the document atomically; an unchanged document saves the same bytes",
          "[authoring][session][save]") {
  Fixture f;
  AuthoringConnection& c = *f.connection;
  REQUIRE(c.save().isOk());
  const std::string first = readText(f.source);
  CHECK(first == textOf(baseScene()));
  REQUIRE(c.save().isOk());
  CHECK(readText(f.source) == first);

  c.submit(ac::SetProperty{address(kLightNode, kLight, field("Light", "intensity")), 0.1234567f});
  REQUIRE(c.save().isOk());
  CHECK(documentOf(readText(f.source)) == f.document());
  for (const auto& entry : std::filesystem::directory_iterator(f.dir.path)) {
    CHECK(entry.path().filename() == "scene.scene.txt");  // no temporary file left behind
  }

  // A write that cannot happen leaves the document and reports WriteFailed.
  std::filesystem::remove_all(f.dir.path);
  CHECK(c.save().error() == ac::SaveError::WriteFailed);
  CHECK(c.document().has_value());
}

TEST_CASE("authoring: an imported document is editable but never saved (ADR-0129 D4)", "[authoring][session][save]") {
  TempDir dir;
  const fs::path generated = dir.path / "imported.scene.txt";
  writeText(generated, textOf(baseScene()));
  AuthoringSession session(DocumentSource{sceneGuid(), Imported{generated}});
  auto c = session.connect();
  REQUIRE(c->open().isOk());
  CHECK(c->document()->provenance == ac::Provenance::Imported);
  c->submit(ac::SetProperty{address(kLightNode, kLight, field("Light", "intensity")), 7.0f});
  CHECK(c->drainFailures().empty());
  CHECK(c->save().error() == ac::SaveError::ImportedSceneNotSavable);
  CHECK(readText(generated) == textOf(baseScene()));
}

TEST_CASE("authoring: open() refuses an unreadable, unparsable or invalid source, and reopening discards edits",
          "[authoring][session]") {
  TempDir dir;
  const auto openOf = [&](const std::string& text) {
    const fs::path path = dir.path / "s.scene.txt";
    std::filesystem::remove(path);
    if (!text.empty()) writeText(path, text);
    AuthoringSession session(DocumentSource{sceneGuid(), HandAuthored{path}});
    auto c = session.connect();
    return c->open();
  };
  CHECK(openOf("").error() == ac::OpenError::SourceUnreadable);
  CHECK(openOf("not a scene").error() == ac::OpenError::SourceParseFailed);
  scene::AuthoringScene nilGuid = baseScene();
  std::string text = textOf(nilGuid);
  const std::string guid = as::toString(entity(3));
  text.replace(text.find(guid), guid.size(), "00000000-0000-0000-0000-000000000000");
  CHECK(openOf(text).error() == ac::OpenError::SourceInvalid);

  Fixture f;
  const SubscriptionId all = f.connection->subscribe({});
  f.connection->submit(ac::DestroyNode{entity(3)});
  REQUIRE(f.document().nodes.size() == 2);
  REQUIRE(f.connection->open().isOk());
  CHECK(f.document() == baseScene());
  const auto events = drain(*f.connection, all);
  REQUIRE(events.size() == 2);
  CHECK(events.back() == ac::Event{ac::DocumentReopened{sceneGuid()}});
}

TEST_CASE("authoring: locateDocument() finds a catalog entry's source", "[authoring][session]") {
  using atlantis::authoring::ImportedScene;
  using atlantis::authoring::locateDocument;
  using atlantis::authoring::LocateError;
  using atlantis::authoring::SourceRoots;
  const SourceRoots roots{fs::path("assets_root"), {ImportedScene{asset(50), asset(51), fs::path("gen/x.scene.txt")}}};

  const auto scene = locateDocument({asset(40), as::CatalogAssetType::Scene, as::CatalogRoot::Assets, "scenes/a.scene.txt"}, roots);
  REQUIRE(scene.isOk());
  CHECK(scene.value().scene == asset(40));
  CHECK(std::get<HandAuthored>(scene.value().provenance).sourcePath == fs::path("assets_root") / "scenes/a.scene.txt");

  const auto imported = locateDocument({asset(50), as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, "x/x.gltf"}, roots);
  REQUIRE(imported.isOk());
  CHECK(imported.value().scene == asset(51));
  CHECK(std::get<Imported>(imported.value().provenance).generatedSourcePath == fs::path("gen/x.scene.txt"));

  CHECK(locateDocument({asset(52), as::CatalogAssetType::GltfImport, as::CatalogRoot::Content, "y/y.gltf"}, roots).error() ==
        LocateError::ImportNotBuilt);
  CHECK(locateDocument({asset(53), as::CatalogAssetType::Scene, as::CatalogRoot::Content, "s.scene.txt"}, roots).error() ==
        LocateError::UnsupportedRoot);
  CHECK(locateDocument({asset(54), as::CatalogAssetType::Mesh, as::CatalogRoot::Assets, "m.mesh.txt"}, roots).error() ==
        LocateError::NotASceneEntry);
}

TEST_CASE("authoring: every committed scene opens from its catalog entry and validates", "[authoring][session]") {
  const auto catalog = as::parseAssetCatalogSource(readText(fs::path{ATLANTIS_ASSETS_DIR} / "asset_catalog.txt"));
  REQUIRE(catalog.isOk());
  std::size_t scenes = 0;
  for (const auto& entry : catalog.value().entries()) {
    if (entry.type != as::CatalogAssetType::Scene) continue;
    ++scenes;
    INFO(entry.path);
    const auto located = atlantis::authoring::locateDocument(entry, {fs::path{ATLANTIS_ASSETS_DIR}, {}});
    REQUIRE(located.isOk());
    AuthoringSession session(located.value());
    auto c = session.connect();
    REQUIRE(c->open().isOk());
    CHECK(atlantis::authoring::validateDocument(*session.document(), entry.guid) == std::nullopt);
  }
  CHECK(scenes == 27);  // 26 scenes and the CMake-declaration fixture
}
