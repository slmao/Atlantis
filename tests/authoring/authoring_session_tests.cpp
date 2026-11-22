#include "authoring_fixture.h"

#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/authoring/document_validation.h>

#include <algorithm>
#include <array>
#include <limits>
#include <span>

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

TEST_CASE("authoring: guarded source persistence follows content independently of Apply", "[authoring][session][persistence]") {
  Fixture unopened(baseScene(), false);
  auto& a = *unopened.connection;
  auto b = unopened.session.connect();
  REQUIRE_FALSE(a.documentState().value());
  const auto first = a.reopenIfCurrent(std::nullopt).value();
  REQUIRE(first.token.documentRevision == 1);
  CHECK(a.reopenIfCurrent(std::nullopt).error().error == ac::DocumentError::StaleDocument);
  const auto onDisk = readText(unopened.source);
  const auto node = entity(1);
  const auto before = a.listNodes().value()[0].name;
  CHECK_FALSE(refusalOf(*b, {ac::SetNodeName{node, "unsaved"}}));
  const auto dirty = a.documentState().value().value();
  REQUIRE(dirty.dirty);
  CHECK(dirty.token.documentId == first.token.documentId);
  CHECK(dirty.lastPersistedRevision == first.token.documentRevision);
  CHECK(a.saveIfCurrent(first.token).error().error == ac::DocumentError::StaleDocument);
  CHECK(a.reopenIfCurrent(first.token).error().error == ac::DocumentError::StaleDocument);
  CHECK(readText(unopened.source) == onDisk);
  CHECK_FALSE(refusalOf(*b, {ac::SetNodeName{node, before}}));
  const auto undone = a.documentState().value().value();
  CHECK_FALSE(undone.dirty);
  CHECK(undone.lastPersistedRevision != undone.token.documentRevision);
  CHECK_FALSE(refusalOf(*b, {ac::SetNodeName{node, "saved"}}));
  const auto reviewed = a.documentState().value().value();
  REQUIRE(b->save().isOk());
  const auto saved = a.documentState().value().value();
  CHECK(saved.token == reviewed.token);
  CHECK(saved.lastPersistedRevision == saved.token.documentRevision);
  CHECK_FALSE(saved.dirty);
  CHECK(a.saveIfCurrent(saved.token).value() == saved);
  unopened.session.setSceneSink([](ac::ApplyId, atlantis::asset_system::ValidatedSceneData) {
    return atlantis::Result<std::optional<ac::ApplyId>, std::monostate>::Ok({});
  });
  const auto receipt = a.applyTracked().value();
  writeText(unopened.source, "invalid source");
  CHECK(a.reopenIfCurrent(saved.token).error().error == ac::DocumentError::SourceParseFailed);
  CHECK(a.documentState().value().value() == saved);
  REQUIRE(b->saveIfCurrent(saved.token).isOk());
  const auto opened = a.reopenIfCurrent(saved.token).value();
  CHECK(opened.token.documentId != saved.token.documentId);
  CHECK(opened.token.documentRevision == saved.token.documentRevision + 1);
  CHECK(a.reopenIfCurrent(saved.token).error().error == ac::DocumentError::StaleDocument);
  CHECK(b->listNodes().value()[0].name == "saved");
  CHECK(b->applyStatus(receipt.id).value().request->state == ac::ApplyState::Pending);
  unopened.session.reportReload(receipt.id, atlantis::Result<std::uint64_t, ac::ReloadError>::Err(
      ac::ReloadError::ScenePreparationFailed));
  CHECK(b->applyStatus(receipt.id).value().request->state == ac::ApplyState::Failed);
  CHECK(a.documentState().value() == opened);
  const auto recovery = b->applyTracked().value();
  CHECK(recovery.documentRevision == opened.token.documentRevision);
  unopened.session.reportReload(recovery.id, atlantis::Result<std::uint64_t, ac::ReloadError>::Ok(1));
  CHECK(a.applyStatus(recovery.id).value().request->state == ac::ApplyState::Applied);
  CHECK(a.documentState().value() == opened);
  Fixture restarted;
  CHECK(restarted.connection->documentState().value()->token.documentRevision == first.token.documentRevision);
  CHECK(restarted.connection->saveIfCurrent(first.token).error().error == ac::DocumentError::StaleDocument);
}

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
  CHECK(c.saveAs("copy").error() == ac::SaveAsError::NoDocument);
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
  CHECK(info->supportsGuardedPropertyTransactions);
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

TEST_CASE("authoring: a guarded sibling exchange changes exactly two slots and survives save",
          "[authoring][session][sibling_order]") {
  auto base = baseScene();
  auto nested = base.nodes[1];
  nested.guid = entity(4);
  nested.parent = entity(1);
  auto descendant = nested;
  descendant.guid = entity(5);
  descendant.parent = entity(2);
  base.nodes.insert(base.nodes.begin() + 2, descendant);
  base.nodes.push_back(nested);
  Fixture f(base);
  auto& c = *f.connection;
  REQUIRE(c.applyStatus().isOk());
  auto revision = [&] { return c.applyStatus().value().currentDocumentRevision.value(); };
  auto up = ac::SiblingDirection::Up;
  auto down = ac::SiblingDirection::Down;
  using Swap = ac::SwapAdjacentSiblings;
  using Change = ac::NodeOrderChanged;
  const auto order = ac::EventKindSet::only(ac::EventKind::NodeOrderChanged);
  const auto byFirst = c.subscribe({order, entity(1), {}});
  const auto byOther = c.subscribe({order, entity(3), {}});
  const auto unrelated = c.subscribe({order, entity(2), {}});
  const auto byComponent = c.subscribe({order, entity(1), kCamera});
  const auto legacy = c.subscribe({});
  const auto original = readText(f.source);
  const auto start = revision();
  const Swap root{entity(1), entity(3), {}, down, start};
  CHECK(refusalOf(c, {Swap{entity(1), entity(3), {}, up, start}}) == ac::Error::InvalidSiblingSwap);
  CHECK(refusalOf(c, {Swap{entity(1), entity(2), {}, down, start}}) == ac::Error::InvalidSiblingSwap);
  CHECK(refusalOf(c, {Swap{entity(1), entity(1), {}, down, start}}) == ac::Error::InvalidSiblingSwap);
  CHECK(refusalOf(c, {Swap{entity(9), entity(1), {}, down, start}}) == ac::Error::UnknownNode);
  CHECK(refusalOf(c, {root, ac::SetNodeName{entity(1), "refused"}}) == ac::Error::InvalidSiblingSwap);
  CHECK(refusalOf(c, {Swap{entity(1), entity(3), {}, down, start - 1}}) == ac::Error::SiblingOrderStaleRevision);
  CHECK(revision() == start);
  CHECK(f.document() == base);
  CHECK(drain(c, byFirst).empty());
  CHECK(readText(f.source) == original);

  const auto ticket = c.submitTransaction({root});
  REQUIRE(ticket.count == 1);
  CHECK(ticket.first.value != 0);
  CHECK(c.drainFailures().empty());
  CHECK(revision() == start + 1);
  const std::vector<ac::Event> event{Change{entity(1), entity(3), 3, 0, start + 1}};
  CHECK(drain(c, byFirst) == event);
  CHECK(drain(c, byOther) == event);
  CHECK(drain(c, unrelated).empty());
  CHECK(drain(c, byComponent).empty());
  CHECK(drain(c, legacy).empty());
  std::swap(base.nodes[0], base.nodes[3]);
  CHECK(f.document() == base);
  CHECK_FALSE(refusalOf(c, {Swap{entity(4), entity(2), entity(1), up, revision()}}));
  std::swap(base.nodes[1], base.nodes[4]);
  CHECK(f.document() == base);
  CHECK(readText(f.source) == original);
  REQUIRE(c.save().isOk());
  const auto beforeReopen = revision();
  REQUIRE(c.open().isOk());
  CHECK(f.document() == base);
  CHECK(refusalOf(c, {Swap{entity(2), entity(4), entity(1), up, beforeReopen}}) == ac::Error::SiblingOrderStaleRevision);
  CHECK(c.activeCamera().value() == entity(1));

  AuthoringSession imported(DocumentSource{sceneGuid(), Imported{f.source}});
  auto importedClient = imported.connect();
  REQUIRE(importedClient->open().isOk());
  CHECK(refusalOf(*importedClient, {Swap{entity(3), entity(1), {}, down,
             importedClient->applyStatus().value().currentDocumentRevision.value()}}) == ac::Error::SiblingOrderRequiresHandAuthored);
}

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

TEST_CASE("authoring: guarded property batches have one revision, ordinary events and atomic refusal",
          "[authoring][session]") {
  auto fixture = baseScene();
  for (int node = 4; node <= 67; ++node) {
    scene::AuthoringNode extra;
    extra.guid = entity(node);
    fixture.nodes.push_back(extra);
  }
  Fixture f(fixture);
  AuthoringConnection& c = *f.connection;
  auto other = f.session.connect();
  auto revision = [&] { return c.applyStatus().value().currentDocumentRevision.value(); };
  const auto original = revision();
  const auto eventStream = c.subscribe({});
  auto write = [](int node, float value) {
    return ac::SetProperty{ac::PropertyAddress{entity(static_cast<std::uint32_t>(node)), kTransform, field("Transform", "localScale")},
                          std::array<float, 3>{value, 1.0f, 1.0f}};
  };
  using Guard = ac::RequireDocumentRevision;
  const auto first = write(1, 2);
  const auto second = write(2, 3);
  const auto unknown = write(99, 4);
  CHECK(refusalOf(c, {Guard{original}}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {first, Guard{original}, second}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {Guard{original}, first, Guard{original}}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {Guard{original}, first, first}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {Guard{original}, first, ac::SetNodeName{entity(2), "name"}}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {Guard{original}, first, ac::SetProperty{
      ac::PropertyAddress{entity(2), kTransform, field("Transform", "localPosition")}, first.value}}) == ac::Error::InvalidGuardedPropertyBatch);
  const auto rejected = c.submitTransaction({Guard{original}, first, unknown});
  const auto failures = c.drainFailures();
  REQUIRE(failures.size() == 1);
  CHECK(failures.front().ticket.value == rejected.first.value + 2);
  CHECK(failures.front().error == ac::Error::UnknownNode);
  CHECK(revision() == original);
  CHECK(f.document() == fixture);
  CHECK(drain(c, eventStream).empty());

  std::vector<ac::Command> max{Guard{original}};
  for (int node = 4; node <= 67; ++node) max.push_back(write(node, 2));
  auto oversized = max;
  oversized.push_back(first);
  CHECK(refusalOf(c, oversized) == ac::Error::InvalidGuardedPropertyBatch);
  const auto ticket = c.submitTransaction(max);
  CHECK(ticket.count == 65);
  CHECK(c.drainFailures().empty());
  CHECK(revision() == original + 1);
  CHECK(drain(c, eventStream).size() == 64);

  const auto beforeRace = revision();
  REQUIRE_FALSE(refusalOf(*other, {ac::SetNodeName{entity(3), "outside"}}));
  CHECK(refusalOf(c, {Guard{beforeRace}, first, second}) == ac::Error::PropertyBatchStaleRevision);
  const auto unrelated = drain(c, eventStream);
  REQUIRE(unrelated.size() == 1);
  CHECK(std::holds_alternative<ac::NodeNameChanged>(unrelated.front()));
  const auto single = c.submitTransaction({Guard{revision()}, first});
  CHECK(single.count == 2);
  CHECK(c.drainFailures().empty());
  CHECK(drain(c, eventStream) == std::vector<ac::Event>{ac::PropertyChanged{first.address, first.value}});
  const auto beforeMulti = revision();
  const auto multi = c.submitTransaction({Guard{beforeMulti}, write(1, 5), second});
  CHECK(multi.count == 3);
  CHECK(c.drainFailures().empty());
  CHECK(revision() == beforeMulti + 1);
  CHECK(drain(c, eventStream) == std::vector<ac::Event>{
      ac::PropertyChanged{first.address, write(1, 5).value}, ac::PropertyChanged{second.address, second.value}});
}

TEST_CASE("authoring: guarded Transform batches admit mixed leaves once", "[authoring][session]") {
  auto initial = baseScene();
  for (std::uint32_t node = 4; node <= 68; ++node) {
    scene::AuthoringNode extra;
    extra.guid = entity(node);
    initial.nodes.push_back(extra);
  }
  Fixture f(initial);
  auto& c = *f.connection;
  REQUIRE(c.document()->supportsGuardedTransformTransactions);
  const auto stream = c.subscribe({});
  const auto revision = c.applyStatus().value().currentDocumentRevision.value();
  using Guard = ac::RequireTransformBatchRevision;
  auto write = [](std::uint32_t node, std::string_view leaf) {
    return ac::SetProperty{{entity(node), kTransform, field("Transform", leaf)}, std::array{2.0f, 1.0f, 1.0f}};
  };
  const auto first = write(4, "localPosition");
  const auto second = write(4, "localScale");
  CHECK(refusalOf(c, {Guard{revision}}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {Guard{revision}, first, first}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {Guard{revision}, first, ac::RequireDocumentRevision{revision}}) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(refusalOf(c, {Guard{revision}, first, ac::SetNodeName{entity(4), "other"}}) == ac::Error::InvalidGuardedPropertyBatch);
  const auto misplaced = c.submitTransaction({first, Guard{revision}, second});
  auto failures = c.drainFailures();
  REQUIRE(failures.size() == 1);
  CHECK(failures.front().ticket.value == misplaced.first.value + 1);
  CHECK(failures.front().error == ac::Error::InvalidGuardedPropertyBatch);
  const auto failed = c.submitTransaction({Guard{revision}, first, write(99, "localScale")});
  failures = c.drainFailures();
  REQUIRE(failures.size() == 1);
  CHECK(failures.front().ticket.value == failed.first.value + 2);
  CHECK(failures.front().error == ac::Error::UnknownNode);

  std::vector<ac::Command> tooManyNodes{Guard{revision}};
  for (std::uint32_t node = 4; node <= 68; ++node) tooManyNodes.push_back(write(node, "localPosition"));
  CHECK(refusalOf(c, tooManyNodes) == ac::Error::InvalidGuardedPropertyBatch);
  CHECK(revision == c.applyStatus().value().currentDocumentRevision.value());
  CHECK(f.document() == initial);
  CHECK(drain(c, stream).empty());

  std::vector<ac::Command> full{Guard{revision}};
  std::vector<ac::Event> expected;
  for (std::uint32_t node = 4; node <= 67; ++node) {
    for (const auto leaf : {"localPosition", "localEulerAnglesRadians", "localScale"}) {
      const auto command = write(node, leaf);
      full.push_back(command);
      expected.emplace_back(ac::PropertyChanged{command.address, command.value});
    }
  }
  auto oversized = full;
  oversized.push_back(write(68, "localPosition"));
  CHECK(refusalOf(c, oversized) == ac::Error::InvalidGuardedPropertyBatch);
  const auto ticket = c.submitTransaction(full);
  CHECK(ticket.count == 193);
  CHECK(c.drainFailures().empty());
  CHECK(c.applyStatus().value().currentDocumentRevision == revision + 1);
  CHECK(drain(c, stream) == expected);
  CHECK(refusalOf(c, {Guard{revision}, first, second}) == ac::Error::PropertyBatchStaleRevision);
  CHECK(drain(c, stream).empty());
}

TEST_CASE("authoring: guarded duplicate admits atomic append and suffix removal", "[authoring][session]") {
  const auto initial = baseScene();
  Fixture f(initial);
  auto& c = *f.connection;
  REQUIRE(c.document()->supportsGuardedDuplicateTransactions);
  const auto stream = c.subscribe({});
  const auto revision = c.applyStatus().value().currentDocumentRevision.value();
  const auto size = c.listNodes().value().size();
  using Guard = ac::RequireDuplicateBatchRevision;
  using Error = ac::Error;
  const ac::CreateNode child{entity(40), size, {}};
  const ac::CreateNode parent{entity(41), size + 1, {kCamera}};
  const ac::SetProperty scale{{entity(40), kTransform, field("Transform", "localScale")}, std::array{-2.0f, 1.0f, 1.0f}};
  const auto invalid = Error::InvalidGuardedDuplicateBatch;
  CHECK(refusalOf(c, {Guard{revision}}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, ac::CreateNode{ac::EntityGuid{}, size, {}}}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, child, child}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, child, scale, scale}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, child, ac::SetNodeName{entity(1), "original"}}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, ac::CreateNode{entity(1), size, {}}}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, parent}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, ac::DestroyNode{entity(1)}}) == invalid);
  CHECK(refusalOf(c, {Guard{revision}, child, ac::RequireTransformBatchRevision{revision}}) == invalid);
  CHECK(refusalOf(c, {ac::RequireDocumentRevision{revision}, Guard{revision}, child}) == Error::InvalidGuardedPropertyBatch);
  const auto misplaced = c.submitTransaction({child, Guard{revision}});
  CHECK(c.drainFailures() == std::vector<ac::CommandFailure>{{{misplaced.first.value + 1}, invalid}});
  const auto wrongValue = c.submitTransaction({Guard{revision}, child, ac::SetProperty{scale.address, false}});
  const auto failures = c.drainFailures();
  REQUIRE(failures.size() == 1);
  CHECK(failures.front().ticket.value == wrongValue.first.value + 2);
  CHECK(failures.front().error == Error::KindMismatch);
  CHECK(refusalOf(c, {Guard{revision}, child, ac::SetParent{entity(40), entity(99)}}) == Error::ParentExists);
  std::vector<ac::Command> oversized{Guard{revision}};
  for (std::uint32_t i = 0; i < 65; ++i) oversized.push_back(ac::CreateNode{entity(100 + i), size + i, {}});
  CHECK(refusalOf(c, oversized) == invalid);
  CHECK(f.document() == initial);
  CHECK(c.applyStatus().value().currentDocumentRevision == revision);
  CHECK(drain(c, stream).empty());

  const auto append = c.submitTransaction({Guard{revision}, child, scale, ac::SetParent{entity(40), entity(41)}, parent});
  CHECK(append.count == 5);
  REQUIRE(c.drainFailures().empty());
  CHECK(c.applyStatus().value().currentDocumentRevision == revision + 1);
  CHECK(drain(c, stream) == std::vector<ac::Event>{ac::NodeCreated{entity(40), size},
      ac::PropertyChanged{scale.address, scale.value}, ac::ParentChanged{entity(40), entity(41)},
      ac::NodeCreated{entity(41), size + 1}, ac::ComponentAdded{entity(41), kCamera}});
  CHECK(refusalOf(c, {Guard{revision}, child}) == Error::DuplicateBatchStaleRevision);
  const auto remove = c.submitTransaction({Guard{revision + 1}, ac::DestroyNode{entity(41)}, ac::DestroyNode{entity(40)}});
  CHECK(remove.count == 3);
  CHECK(c.drainFailures().empty());
  CHECK(drain(c, stream) == std::vector<ac::Event>{ac::NodeDestroyed{entity(41)}, ac::NodeDestroyed{entity(40)}});
  CHECK(f.document() == initial);
  REQUIRE(c.open().isOk());
  (void)drain(c, stream);
  CHECK(refusalOf(c, {Guard{revision + 2}, child}) == Error::DuplicateBatchStaleRevision);
  CHECK(drain(c, stream).empty());
}

TEST_CASE("authoring: prefab capture leaves committed values and the original document intact", "[authoring][session][prefab]") {
  auto original = baseScene();
  original.nodes[kMeshNode].name = "Table";
  scene::AuthoringNode chair;
  chair.guid = entity(4);
  chair.name = "Chair";
  chair.parent = entity(2);
  chair.transform.localPosition = {1.5f, 0.125f, -3.0f};
  chair.renderable = scene::Renderable{asset(1), {}};
  original.nodes.insert(original.nodes.begin(), chair);
  Fixture f(original);
  auto& c = *f.connection;
  using E = ac::SaveAsError;
  using R = atlantis::Result<ac::PrefabRegistration, E>;
  const auto before = c.documentState().value().value();
  CHECK(c.createPrefab("seating", entity(2), before.token).error() == E::Unavailable);
  std::string captured;
  as::AssetGuid capturedGuid{};
  std::size_t writes = 0;
  f.session.setPrefabSink([&](std::string_view name, as::AssetGuid guid, std::string text, std::uint64_t revision) {
    ++writes;
    captured = std::move(text);
    capturedGuid = guid;
    return R::Ok({guid, ac::prefabSource(name), revision});
  });
  CHECK(c.document()->supportsPrefabRegistration);
  REQUIRE_FALSE(refusalOf(c, {ac::SetNodeName{entity(2), "Edited Table"}}));
  CHECK(c.createPrefab("seating", entity(2), before.token).error() == E::StaleDocument);
  const auto current = c.documentState().value().value();
  CHECK(c.createPrefab("seating", entity(1), current.token).error() == E::UnsupportedSubtree);
  REQUIRE(c.createPrefab("seating", entity(2), current.token).isOk());
  CHECK(writes == 1);
  const auto saved = documentOf(captured);
  REQUIRE(saved.nodes.size() == 2);
  CHECK(saved.nodes[0].name == chair.name);
  CHECK(saved.nodes[0].transform == chair.transform);
  CHECK(saved.nodes[0].renderable == chair.renderable);
  CHECK(saved.nodes[0].parent == saved.nodes[1].guid);
  CHECK(saved.nodes[1].name == "Edited Table");
  CHECK_FALSE(saved.nodes[1].parent.has_value());
  CHECK(saved.nodes[1].guid != entity(2));
  CHECK(capturedGuid != as::AssetGuid{});
  CHECK(c.documentState().value() == current);
  CHECK(readText(f.source) == textOf(original));
  CHECK_FALSE(c.applyStatus().value().request.has_value());
}

TEST_CASE("authoring: prefab origin is guarded, preserved by old edits, and omitted from cooked bytes", "[authoring][session][prefab]") {
  Fixture f;
  auto& c = *f.connection;
  const auto source = asset(88);
  f.session.setPrefabLookup([source](const auto& id) { return id == source; });
  REQUIRE(c.document()->supportsPrefabOrigin);
  const auto token = c.documentState().value()->token;
  const auto count = f.document().nodes.size();
  const ac::CreateNode create{entity(55), count, {}};
  const ac::SetPrefabOrigin associate{entity(55), source};
  const ac::RequireDuplicateBatchRevision guard{token.documentRevision};
  CHECK(refusalOf(c, {create, associate}) == ac::Error::InvalidGuardedDuplicateBatch);
  CHECK(refusalOf(c, {guard, create, ac::SetPrefabOrigin{entity(55), asset(89)}}) ==
        ac::Error::InvalidGuardedDuplicateBatch);
  CHECK(refusalOf(c, {guard, create, associate, ac::SetParent{entity(55), entity(1)}}) ==
        ac::Error::InvalidGuardedDuplicateBatch);
  CHECK(c.documentState().value()->token == token);
  CHECK(f.document().nodes.size() == count);
  const auto stream = c.subscribe({});
  REQUIRE_FALSE(refusalOf(c, {guard, create, associate}));
  CHECK(drain(c, stream) == std::vector<ac::Event>{ac::NodeCreated{entity(55), count, source}});
  REQUIRE_FALSE(refusalOf(c, {ac::SetNodeName{entity(55), "Placed Prefab"}}));
  const auto linked = f.document();
  auto plain = linked;
  plain.nodes.back().prefabOrigin.reset();
  const auto cooked = as::cookSceneToMemory(linked, f.session.source().scene);
  const auto old = as::cookSceneToMemory(plain, f.session.source().scene);
  REQUIRE(cooked.isOk());
  REQUIRE(old.isOk());
  CHECK(cooked.value().artifactBytes == old.value().artifactBytes);
  REQUIRE(c.save().isOk());
  CHECK(readText(f.source).starts_with("atlantis_scene_source_version: 9\n"));
  REQUIRE(c.open().isOk());
  CHECK(f.document() == linked);
  CHECK(c.listNodes().value().back().prefabOrigin == source);
}

TEST_CASE("authoring: relative sibling moves rotate only occupied slots and settle once", "[authoring][session]") {
  auto initial = baseScene();
  scene::AuthoringNode middle;
  middle.guid = entity(4);
  scene::AuthoringNode child;
  child.guid = entity(5);
  child.parent = middle.guid;
  initial.nodes.insert(initial.nodes.begin() + 2, {middle, child});
  Fixture f(initial);
  auto& c = *f.connection;
  using Move = ac::MoveSiblingRelative;
  using Event = ac::NodeMovedInSiblingOrder;
  const auto before = ac::SiblingPlacement::Before, after = ac::SiblingPlacement::After;
  const auto kind = ac::EventKindSet::only(ac::EventKind::NodeMovedInSiblingOrder);
  const auto modern = c.subscribe({kind, entity(1), {}});
  const auto anchorOnly = c.subscribe({kind, entity(3), {}});
  const auto component = c.subscribe({kind, entity(1), kTransform});
  const auto adjacent = c.subscribe({ac::EventKindSet::all(), {}, {}});
  const auto revision = c.applyStatus().value().currentDocumentRevision.value();
  const auto source = readText(f.source);
  CHECK(refusalOf(c, {Move{entity(1), entity(4), {}, before, revision}}) == ac::Error::InvalidSiblingMove);
  CHECK(refusalOf(c, {Move{entity(1), entity(5), {}, after, revision}}) == ac::Error::InvalidSiblingMove);
  CHECK(refusalOf(c, {Move{entity(1), entity(1), {}, after, revision}}) == ac::Error::InvalidSiblingMove);
  CHECK(refusalOf(c, {Move{entity(1), entity(99), {}, after, revision}}) == ac::Error::UnknownNode);
  CHECK(refusalOf(c, {ac::SetNodeName{entity(1), "refused"}, Move{entity(1), entity(3), {}, after, revision}})
        == ac::Error::InvalidSiblingMove);
  CHECK(c.applyStatus().value().currentDocumentRevision == revision);
  CHECK(drain(c, modern).empty());
  auto ticket = c.submitTransaction({Move{entity(1), entity(3), {}, after, revision}});
  REQUIRE(ticket.first.value != 0);
  CHECK(ticket.count == 1);
  CHECK(c.drainFailures().empty());
  auto expected = initial;
  expected.nodes[0] = initial.nodes[2];
  expected.nodes[2] = initial.nodes[4];
  expected.nodes[4] = initial.nodes[0];
  CHECK(f.document() == expected);
  CHECK(drain(c, modern) == std::vector<ac::Event>{Event{entity(1), {}, 0, 4, revision + 1}});
  CHECK(drain(c, adjacent).empty());
  CHECK(drain(c, anchorOnly).empty());
  CHECK(drain(c, component).empty());
  CHECK(refusalOf(c, {Move{entity(1), entity(4), {}, before, revision}}) == ac::Error::SiblingOrderStaleRevision);
  CHECK(readText(f.source) == source);
  REQUIRE(c.save().isOk());
  REQUIRE(c.open().isOk());
  CHECK(f.document() == expected);
  CHECK(c.activeCamera().value() == entity(1));
  CHECK_FALSE(refusalOf(c, {Move{entity(1), entity(4), {}, before,
      c.applyStatus().value().currentDocumentRevision.value()}}));
  CHECK(f.document() == initial);

  AuthoringSession imported(DocumentSource{sceneGuid(), Imported{f.source}});
  auto peer = imported.connect();
  REQUIRE(peer->open().isOk());
  CHECK(refusalOf(*peer, {Move{entity(4), entity(1), {}, after,
      peer->applyStatus().value().currentDocumentRevision.value()}}) == ac::Error::SiblingOrderRequiresHandAuthored);
}

TEST_CASE("authoring: relative sibling moves preserve nested slots in both directions", "[authoring][session]") {
  auto initial = baseScene();
  for (int index = 4; index != 7; ++index) {
    scene::AuthoringNode node;
    node.guid = entity(index);
    node.parent = entity(1);
    initial.nodes.insert(initial.nodes.begin() + (index == 4 ? 1 : initial.nodes.size()), node);
  }
  Fixture f(initial);
  auto& c = *f.connection;
  const auto revision = c.applyStatus().value().currentDocumentRevision.value();
  using Move = ac::MoveSiblingRelative;
  using Placement = ac::SiblingPlacement;
  auto expected = initial;
  expected.nodes[1] = initial.nodes[2];
  expected.nodes[2] = initial.nodes[4];
  expected.nodes[4] = initial.nodes[5];
  expected.nodes[5] = initial.nodes[1];
  REQUIRE_FALSE(refusalOf(c, {Move{entity(4), entity(6), entity(1), Placement::After, revision}}));
  CHECK(f.document() == expected);
  REQUIRE_FALSE(refusalOf(c, {Move{entity(4), entity(2), entity(1), Placement::Before, revision + 1}}));
  CHECK(f.document() == initial);
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
  f.session.setSceneSink([&](ac::ApplyId, as::ValidatedSceneData scene) {
    received.push_back(std::move(scene));
    using Ack = atlantis::Result<std::optional<ac::ApplyId>, std::monostate>;
    return accept ? Ack::Ok({}) : Ack::Err({});
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

TEST_CASE("authoring: creates a fresh camera-only source without opening or retargeting the document",
          "[authoring][session][create_scene]") {
  Fixture f(baseScene(), false);
  auto& connection = *f.connection;
  using R = atlantis::Result<ac::CreatedScene, ac::CreateSceneError>;
  CHECK(connection.createScene("new_scene").error() == ac::CreateSceneError::Unavailable);
  CHECK(connection.createScene("../escape").error() == ac::CreateSceneError::InvalidName);
  std::vector<as::EntityGuid> cameras;
  f.session.setCreateSceneSink([&](std::string_view name, as::AssetGuid guid, std::string text) {
    const auto parsed = as::parseSceneSource(text);
    REQUIRE(parsed.isOk());
    REQUIRE(parsed.value().nodes.size() == 1);
    const auto& node = parsed.value().nodes.front();
    REQUIRE(node.camera);
    CHECK(node.name == "Camera");
    CHECK(parsed.value().activeCameraNodeId == node.nodeId);
    CHECK_FALSE(node.parentNodeId);
    CHECK_FALSE(node.meshAsset);
    CHECK_FALSE(node.materialAsset);
    CHECK_FALSE(node.light);
    CHECK(node.transform.positionZ == 5.0f);
    CHECK(node.camera->fovYRadians > 1.04f);
    CHECK(node.camera->fovYRadians < 1.05f);
    CHECK(node.camera->nearZ == 0.1f);
    CHECK(node.camera->farZ == 100.0f);
    CHECK(node.camera->exposureCompensationEv == 0.0f);
    CHECK(node.camera->fog.density == 0.0f);
    CHECK(node.camera->bloom.strength == 0.0f);
    REQUIRE(as::cookSceneSource(text, guid).isOk());
    cameras.push_back(node.entityGuid);
    return R::Ok({guid, ac::saveAsSource(name)});
  });
  const auto first = connection.createScene("first");
  REQUIRE(first.isOk());
  CHECK_FALSE(connection.document());
  REQUIRE(connection.open().isOk());
  REQUIRE_FALSE(refusalOf(connection, {ac::SetNodeName{entity(2), "Unsaved"}}));
  const auto before = f.document();
  const auto original = readText(f.source);
  const auto status = connection.applyStatus().value();
  const auto subscription = connection.subscribe({});
  const auto second = connection.createScene("second");
  REQUIRE(second.isOk());
  CHECK(first.value().scene != second.value().scene);
  REQUIRE(cameras.size() == 2);
  CHECK(cameras[0] != cameras[1]);
  CHECK(f.document() == before);
  CHECK(readText(f.source) == original);
  CHECK(connection.applyStatus().value().currentDocumentRevision == status.currentDocumentRevision);
  CHECK(drain(connection, subscription).empty());
}

TEST_CASE("authoring: Save As snapshots unsaved edits without changing the active scene", "[authoring][session][save]") {
  Fixture f;
  auto& connection = *f.connection;
  CHECK(connection.saveAs("copy").error() == ac::SaveAsError::Unavailable);
  CHECK(connection.saveAs("../copy").error() == ac::SaveAsError::InvalidName);
  const std::string original = readText(f.source);
  REQUIRE_FALSE(refusalOf(connection, {ac::SetNodeName{entity(3), "Copy mesh"}}).has_value());
  auto subscription = connection.subscribe({});
  auto state = connection.applyStatus().value();
  f.session.setSaveAsSink([&](std::string_view name, std::string text, std::uint64_t revision) {
    CHECK(name == "copy");
    CHECK(documentOf(text) == f.document());
    CHECK(revision == state.currentDocumentRevision);
    return atlantis::Result<ac::SavedScene, ac::SaveAsError>::Ok({asset(71), ac::saveAsSource(name), revision});
  });
  const auto saved = connection.saveAs("copy");
  REQUIRE(saved.isOk());
  CHECK(saved.value().scene != connection.document()->scene);
  CHECK(connection.document()->sourcePath == f.source.generic_string());
  CHECK(connection.applyStatus().value() == state);
  CHECK(drain(connection, subscription).empty());
  CHECK(readText(f.source) == original);
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
  CHECK(c->saveAs("copy").error() == ac::SaveAsError::ImportedSceneNotSavable);
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

TEST_CASE("authoring: rename is atomic, ordered and shared without changing identity", "[authoring][session][name]") {
  Fixture f;
  auto& a = *f.connection;
  auto b = f.session.connect();
  const auto all = b->subscribe({});
  const auto byNode = b->subscribe({ac::EventKindSet::all(), entity(2), std::nullopt});
  const auto byComponent = b->subscribe({ac::EventKindSet::all(), entity(2), kRenderable});
  const auto original = f.document();
  const std::string label = "two  spaces \"quoted\" \\ 鍦烘櫙";
  CHECK(refusalOf(a, {ac::SetNodeName{entity(2), label}, ac::SetNodeName{entity(3), label}}) == std::nullopt);
  CHECK(a.listNodes().value()[1] == ac::NodeSummary{entity(2), entity(1), label});
  CHECK(b->listNodes().value()[2].name == label);
  CHECK(f.document().nodes[1].guid == original.nodes[1].guid);
  CHECK(f.document().nodes[1].transform == original.nodes[1].transform);
  CHECK(drain(*b, all) == std::vector<ac::Event>{ac::NodeNameChanged{entity(2), label}, ac::NodeNameChanged{entity(3), label}});
  CHECK(drain(*b, byNode) == std::vector<ac::Event>{ac::NodeNameChanged{entity(2), label}});
  CHECK(drain(*b, byComponent).empty());
  CHECK(refusalOf(a, {ac::SetNodeName{entity(2), label}}) == std::nullopt);
  CHECK(drain(*b, all) == std::vector<ac::Event>{ac::NodeNameChanged{entity(2), label}});
  const auto before = f.document();
  CHECK(refusalOf(a, {ac::SetNodeName{entity(2), "would change"}, ac::DestroyNode{entity(99)}}) == ac::Error::UnknownNode);
  CHECK(f.document() == before);
  CHECK(drain(*b, all).empty());
  CHECK(refusalOf(a, {ac::SetNodeName{entity(2), std::string(256, 'x')}}) == ac::Error::InvalidNodeName);
  CHECK(refusalOf(a, {ac::SetNodeName{entity(2), "bad\nname"}}) == ac::Error::InvalidNodeName);
  CHECK(refusalOf(a, {ac::SetNodeName{entity(2), std::string("bad\0name", 8)}}) == ac::Error::InvalidNodeName);
  CHECK(f.document() == before);
  CHECK(drain(*b, all).empty());
  CHECK(refusalOf(a, {ac::SetNodeName{entity(99), ""}}) == ac::Error::UnknownNode);
  CHECK(refusalOf(a, {ac::CreateNode{entity(20), 1, {}}, ac::SetNodeName{entity(20), "created"}}) == std::nullopt);
  const auto events = drain(*b, all);
  REQUIRE(events.size() == 2);
  CHECK(events[0] == ac::Event{ac::NodeCreated{entity(20), 1}});
  CHECK(events[1] == ac::Event{ac::NodeNameChanged{entity(20), "created"}});
  CHECK(refusalOf(a, {ac::SetNodeName{entity(20), ""}}) == std::nullopt);
  CHECK(a.listNodes().value()[1].name.empty());
}

TEST_CASE("authoring: names survive save reopen and apply; imported save stays refused", "[authoring][session][name]") {
  Fixture f;
  const std::string label = " camera  鍦烘櫙";
  REQUIRE(refusalOf(*f.connection, {ac::SetNodeName{entity(1), label}}) == std::nullopt);
  REQUIRE(f.connection->save().isOk());
  CHECK(documentOf(readText(f.source)) == f.document());
  REQUIRE(refusalOf(*f.connection, {ac::SetNodeName{entity(1), "temporary"}}) == std::nullopt);
  REQUIRE(f.connection->open().isOk());
  CHECK(f.document().nodes[0].name == label);
  bool received = false;
  f.session.setSceneSink([&](ac::ApplyId, as::ValidatedSceneData) {
    received = true; return atlantis::Result<std::optional<ac::ApplyId>, std::monostate>::Ok({});
  });
  REQUIRE(f.connection->apply().isOk());
  CHECK(received);
  CHECK(f.document().nodes[0].name == label);
  AuthoringSession imported(DocumentSource{sceneGuid(), Imported{f.source}});
  auto c = imported.connect();
  REQUIRE(c->open().isOk());
  REQUIRE(refusalOf(*c, {ac::SetNodeName{entity(1), "import edit"}}) == std::nullopt);
  CHECK(c->save().error() == ac::SaveError::ImportedSceneNotSavable);
  REQUIRE(c->open().isOk());
  CHECK(c->listNodes().value()[0].name == label);
}

namespace {
using ApplyAck = atlantis::Result<std::optional<ac::ApplyId>, std::monostate>;
using ReloadOutcome = atlantis::Result<std::uint64_t, ac::ReloadError>;
struct ApplySink {
  std::optional<ac::ApplyId> pending;
  std::optional<as::ValidatedSceneData> snapshot;
  bool accepts = true;
  void install(AuthoringSession& session) {
    session.setSceneSink([this](ac::ApplyId id, as::ValidatedSceneData scene) {
      if (!accepts) return ApplyAck::Err({});
      const auto old = pending; pending = id; snapshot.emplace(std::move(scene));
      return ApplyAck::Ok(old);
    });
  }
  void complete(AuthoringSession& session, ReloadOutcome outcome) {
    REQUIRE(pending);
    const auto id = *pending; pending.reset(); snapshot.reset();
    session.reportReload(id, std::move(outcome));
  }
};
}

TEST_CASE("authoring outcomes: receipt captures immutable scene and observational revision", "[authoring][apply_outcomes]") {
  Fixture f(baseScene(), false); auto& c = *f.connection;
  CHECK_FALSE(c.applyStatus().value().currentDocumentRevision);
  CHECK_FALSE(c.applyStatus().value().request);
  CHECK(c.applyTracked().error() == ac::ApplyError::NoDocument);
  REQUIRE(c.open().isOk());
  CHECK(c.applyStatus().value().currentDocumentRevision == 1);
  CHECK(c.applyTracked().error() == ac::ApplyError::NoSceneSink);
  ApplySink sink; sink.install(f.session);
  const auto first = c.applyTracked().value();
  CHECK(first.documentRevision == 1);
  CHECK(ac::parseApplyId(ac::toString(first.id)).value() == first.id);
  auto second = f.session.connect();
  CHECK(second->applyStatus(first.id).value() == c.applyStatus(first.id).value());
  const float original = sink.snapshot->node(kLightNode).light->intensity;
  c.submit(ac::SetProperty{address(kLightNode, kLight, field("Light", "intensity")), 7.0f});
  REQUIRE(c.drainFailures().empty());
  CHECK(c.applyStatus().value().currentDocumentRevision == 2);
  CHECK(sink.snapshot->node(kLightNode).light->intensity == original);
  CHECK(c.applyStatus(first.id).value().request->receipt == first);
  REQUIRE(c.save().isOk()); CHECK(c.applyStatus().value().currentDocumentRevision == 2);
  REQUIRE(c.open().isOk()); CHECK(c.applyStatus().value().currentDocumentRevision == 3);
  CHECK(c.applyStatus(first.id).value().request->state == ac::ApplyState::Pending);
  second.reset();
  sink.complete(f.session, ReloadOutcome::Err(ac::ReloadError::ScenePreparationFailed));
  auto reconnected = f.session.connect();
  CHECK(reconnected->applyStatus(first.id).value().request->state == ac::ApplyState::Failed);
  CHECK(reconnected->getProperty(address(kLightNode, kLight, field("Light", "intensity"))).value() == ac::PropertyValue{7.0f});
  CHECK_FALSE(reconnected->applyStatus().value().projection);
}

TEST_CASE("authoring outcomes: same-batch latest wins, refused replacement and stale reports", "[authoring][apply_outcomes]") {
  Fixture f; auto& c = *f.connection; ApplySink sink; sink.install(f.session);
  const auto sub = c.subscribe({});
  const auto a = c.applyTracked().value(); const auto b = c.applyTracked().value(); const auto d = c.applyTracked().value();
  const auto ar = *c.applyStatus(a.id).value().request; const auto br = *c.applyStatus(b.id).value().request;
  CHECK(ar.state == ac::ApplyState::Superseded); CHECK(ar.supersededBy == b.id);
  CHECK(br.state == ac::ApplyState::Superseded); CHECK(br.supersededBy == d.id);
  CHECK(a.documentRevision == d.documentRevision); CHECK(a.id != d.id);
  sink.accepts = false;
  CHECK(c.applyTracked().error() == ac::ApplyError::ReloadRefused);
  CHECK(c.applyStatus().value().request->receipt == d); CHECK(sink.pending == d.id);
  f.session.reportReload(a.id, ReloadOutcome::Ok(77));
  CHECK(c.applyStatus().value().request->state == ac::ApplyState::Pending);
  sink.complete(f.session, ReloadOutcome::Err(ac::ReloadError::RenderPreparationFailed));
  f.session.reportReload(d.id, ReloadOutcome::Ok(88));
  CHECK(c.applyStatus(d.id).value().request->error == ac::ReloadError::RenderPreparationFailed);
  CHECK(c.drainEvents(sub).value().empty());
  sink.accepts = true; const auto recovery = c.applyTracked().value();
  sink.complete(f.session, ReloadOutcome::Ok(9));
  CHECK(c.applyStatus(recovery.id).value().projection == ac::ApplyProjection{recovery, 9});
  CHECK(c.drainEvents(sub).value() == std::vector<ac::Event>{ac::Applied{9}});
  CHECK(c.applyStatus(recovery.id).value().request->generation == 9);
  CHECK(c.applyStatus(recovery.id).value() == c.applyStatus(recovery.id).value());
}

TEST_CASE("authoring outcomes: bounded history, independent projection and unknown identity", "[authoring][apply_outcomes]") {
  Fixture f; auto& c = *f.connection; ApplySink sink; sink.install(f.session);
  const auto projected = c.applyTracked().value(); sink.complete(f.session, ReloadOutcome::Ok(1));
  std::vector<ac::ApplyReceipt> failures;
  for (int i = 0; i < 66; ++i) {
    failures.push_back(c.applyTracked().value());
    sink.complete(f.session, ReloadOutcome::Err(ac::ReloadError::ScenePreparationFailed));
  }
  CHECK(c.applyStatus(projected.id).value().request->state == ac::ApplyState::Applied);
  CHECK(c.applyStatus(projected.id).value().request->generation == 1);
  CHECK(c.applyStatus(failures[0].id).error() == ac::ApplyLookupError::UnknownApply);
  CHECK(c.applyStatus(failures[1].id).error() == ac::ApplyLookupError::UnknownApply);
  CHECK(c.applyStatus(failures[2].id).value().request->state == ac::ApplyState::Failed);
  const auto held = c.applyTracked().value();
  CHECK(c.applyStatus(failures[2].id).isOk()); CHECK(c.applyStatus(held.id).value().request->state == ac::ApplyState::Pending);
  const auto replacement = c.applyTracked().value();
  CHECK(c.applyStatus(failures[2].id).error() == ac::ApplyLookupError::UnknownApply);
  CHECK(c.applyStatus(held.id).value().request->supersededBy == replacement.id);
  // A second session cannot reinterpret a previous endpoint's receipt.
  AuthoringSession restarted(DocumentSource{sceneGuid(), HandAuthored{f.source}});
  auto fresh = restarted.connect();
  CHECK(fresh->applyStatus(projected.id).error() == ac::ApplyLookupError::UnknownApply);
  CHECK_FALSE(fresh->applyStatus().value().request);
}

TEST_CASE("authoring outcomes: refused and empty mutations preserve revision, equal writes advance it", "[authoring][apply_outcomes]") {
  Fixture f; auto& c = *f.connection;
  CHECK(c.applyStatus().value().currentDocumentRevision == 1);
  c.submitTransaction({}); CHECK(c.applyStatus().value().currentDocumentRevision == 1);
  c.submit(ac::DestroyNode{entity(99)}); REQUIRE_FALSE(c.drainFailures().empty());
  CHECK(c.applyStatus().value().currentDocumentRevision == 1);
  const auto addr = address(kLightNode, kLight, field("Light", "intensity"));
  c.submit(ac::SetProperty{addr, c.getProperty(addr).value()}); REQUIRE(c.drainFailures().empty());
  CHECK(c.applyStatus().value().currentDocumentRevision == 2);
  writeText(f.source, "invalid source"); CHECK(c.open().isErr());
  CHECK(c.applyStatus().value().currentDocumentRevision == 2);
  CHECK(c.getProperty(addr).isOk());
}

TEST_CASE("authoring outcomes: legacy acceptance shares records and RuntimeStopped is terminal", "[authoring][apply_outcomes]") {
  Fixture f; auto& c = *f.connection; ApplySink sink; sink.install(f.session);
  REQUIRE(c.apply().isOk()); const auto receipt = c.applyStatus().value().request->receipt;
  sink.complete(f.session, ReloadOutcome::Err(ac::ReloadError::RuntimeStopped));
  CHECK(c.applyStatus(receipt.id).value().request->error == ac::ReloadError::RuntimeStopped);
  CHECK_FALSE(c.applyStatus().value().projection);
}

namespace {
class RecoveryFixture final : public atlantis::authoring::RecoveryStore {
 public:
  explicit RecoveryFixture(fs::path path) : path_(std::move(path)) {}
  void opened() override {
    if (entry) entry->copy.pending = true;
    protectedRevision.reset();
  }
  std::optional<std::string> fingerprint(std::string_view bytes) const override { return std::string(bytes); }
  std::optional<std::string> readSource() const override { return readText(path_); }
  ac::RecoveryStatus status() const override {
    return {ac::RecoveryCapture::Idle, protectedRevision, entry ? std::optional(entry->copy) : std::nullopt, {}};
  }
  void capture(std::uint64_t revision, std::string base, std::string payload) override {
    entry = atlantis::authoring::RecoverySnapshot{{entity(static_cast<std::uint32_t>(revision + 50)), revision, 1, false},
                                                  std::move(base), std::move(payload)};
    protectedRevision = revision;
  }
  void retire() override {
    if (entry && !entry->copy.pending) entry.reset();
    protectedRevision.reset();
  }
  atlantis::Result<atlantis::authoring::RecoverySnapshot, ac::RecoveryError>
  pending(const as::EntityGuid& id) const override {
    using R = atlantis::Result<atlantis::authoring::RecoverySnapshot, ac::RecoveryError>;
    if (!entry || !entry->copy.pending || entry->copy.id != id) return R::Err(ac::RecoveryError::StaleCopy);
    return R::Ok(*entry);
  }
  void adopted(std::uint64_t revision) override { entry->copy.pending = false; protectedRevision = revision; }
  std::optional<ac::RecoveryError> discard(const as::EntityGuid& id) override {
    if (pending(id).isErr()) return ac::RecoveryError::StaleCopy;
    entry.reset();
    return std::nullopt;
  }
  std::optional<atlantis::authoring::RecoverySnapshot> entry;
 private:
  fs::path path_;
  std::optional<std::uint64_t> protectedRevision;
};
}

TEST_CASE("authoring: recovery keeps the formal anchor and reload settlement separate", "[authoring][recovery]") {
  Fixture fixture(baseScene(), false);
  RecoveryFixture copy(fixture.source);
  fixture.session.setRecoveryStore(copy);
  auto& a = *fixture.connection;
  auto b = fixture.session.connect();
  const auto saved = readText(fixture.source);
  REQUIRE(a.open().isOk());
  const auto baseline = a.documentState().value().value();
  CHECK_FALSE(refusalOf(*b, {ac::SetNodeName{entity(2), "recover this"}}));
  const auto dirty = a.documentState().value().value();
  REQUIRE(dirty.dirty);
  REQUIRE(a.recoveryStatus().value().protectedRevision == dirty.token.documentRevision);
  CHECK(a.applyTracked().error() == ac::ApplyError::NoSceneSink);
  CHECK(readText(fixture.source) == saved);
  const auto id = a.recoveryStatus().value().copy->id;
  REQUIRE(a.reopenIfCurrent(dirty.token).isOk());
  const auto reopened = a.documentState().value().value();
  CHECK(reopened.token.documentId != baseline.token.documentId);
  CHECK(a.restoreRecovery(dirty.token, id).error().error == ac::RecoveryError::StaleDocument);
  REQUIRE(a.recoveryStatus(id).value().preview->source == ac::RecoverySource::Matches);
  REQUIRE(a.restoreRecovery(reopened.token, id).isOk());
  const auto restored = a.documentState().value().value();
  CHECK(restored.token.documentId != reopened.token.documentId);
  CHECK(restored.token.documentRevision == reopened.token.documentRevision + 1);
  CHECK(restored.lastPersistedRevision == reopened.lastPersistedRevision);
  CHECK(restored.dirty);
  CHECK(a.listNodes().value()[1].name == "recover this");
  CHECK(readText(fixture.source) == saved);
  CHECK_FALSE(a.applyStatus().value().request);
  CHECK(a.recoveryStatus().value().protectedRevision == restored.token.documentRevision);
  REQUIRE(a.saveIfCurrent(restored.token).isOk());
  CHECK_FALSE(a.recoveryStatus().value().copy);
  CHECK(readText(fixture.source) != saved);
}

TEST_CASE("authoring: a source edit during a recovery decision can only be discarded", "[authoring][recovery]") {
  Fixture fixture(baseScene(), false);
  RecoveryFixture copy(fixture.source);
  fixture.session.setRecoveryStore(copy);
  auto& a = *fixture.connection;
  REQUIRE(a.open().isOk());
  CHECK_FALSE(refusalOf(a, {ac::SetNodeName{entity(1), "unsaved"}}));
  const auto entry = a.recoveryStatus().value().copy.value();
  REQUIRE(a.open().isOk());
  const auto reviewed = a.documentState().value().value();
  REQUIRE(a.recoveryStatus(entry.id).value().preview->source == ac::RecoverySource::Matches);
  auto source = baseScene();
  source.nodes[0].name = "external save";
  const auto external = textOf(source);
  writeText(fixture.source, external);
  CHECK(a.recoveryStatus(entry.id).value().preview->source == ac::RecoverySource::Changed);
  CHECK(a.restoreRecovery(reviewed.token, entry.id).error().error == ac::RecoveryError::SourceChanged);
  CHECK(a.documentState().value().value() == reviewed);
  REQUIRE(a.discardRecovery(reviewed.token, entry.id).isOk());
  CHECK(a.documentState().value().value() == reviewed);
  CHECK(readText(fixture.source) == external);
  CHECK_FALSE(a.recoveryStatus().value().copy);
}

TEST_CASE("authoring: Prefab sync preserves the root and settles whole-subtree replay", "[authoring][session][prefab]") {
  Fixture f;
  auto& c = *f.connection;
  using E = ac::PrefabEditError;
  const auto origin = asset(80);
  const auto root = entity(70);
  const auto other = entity(71);
  const auto digest = [](std::string_view contents) -> std::optional<std::string> {
    const auto bytes = std::as_bytes(std::span(contents.data(), contents.size()));
    return as::toString(as::AssetGuid{as::fnv1a128(bytes)});
  };
  const auto baselineDigest = [digest](std::string_view contents) -> std::optional<std::string> {
    auto value = *digest(contents);
    std::erase(value, '-');
    return value + value;
  };
  scene::AuthoringScene definition;
  definition.nodes = {
      scene::AuthoringNode{entity(11), entity(10), {}, {}, scene::Renderable{asset(1), asset(2)}, {}, "Original chair"},
      scene::AuthoringNode{entity(10), {}, {}, {}, {}, {}, "Template"}};
  const auto source = [&] { return as::serializeSceneSource(scene::toParsedSceneSource(definition).value()); };
  auto frozen = source();
  auto revision = *baselineDigest(frozen);
  f.session.setPrefabSync([&](const as::AssetGuid& guid) -> std::optional<atlantis::authoring::PrefabDefinition> {
    if (guid != origin) return std::nullopt;
    return atlantis::authoring::PrefabDefinition{"seating", frozen, revision};
  }, baselineDigest, [](const auto& renderable) { return renderable.meshAsset == asset(1); });
  const auto token = [&] { return c.documentState().value()->token; };
  REQUIRE(c.document()->supportsPrefabSync);
  const auto first = c.syncPrefab(root, revision, token(), origin);
  REQUIRE(first.isOk());
  CHECK(first.value().affectsProjection);
  const auto initialBlock = as::parseSceneSource(first.value().after.source);
  REQUIRE(initialBlock.isOk());
  REQUIRE(initialBlock.value().nodes.size() == 2);
  CHECK(initialBlock.value().nodes.front().name == "Original chair");
  CHECK(initialBlock.value().nodes.back().entityGuid == root);
  REQUIRE(c.syncPrefab(other, revision, token(), origin).isOk());
  REQUIRE_FALSE(refusalOf(c, {ac::SetNodeName{root, "Placed instance"}, ac::SetParent{root, entity(2)}}));
  REQUIRE_FALSE(refusalOf(c, {ac::SetProperty{address(69, kTransform, field("Transform", "localPosition")),
      std::array<float, 3>{2, 1, -3}}}));
  const auto original = c.listNodes().value();
  REQUIRE(std::ranges::find(original, root, &ac::NodeSummary::node)->prefabBaseline.has_value());
  const auto savedBase = token();
  CHECK_FALSE(c.syncPrefab(root, revision, savedBase, {}).value().changed);
  definition.nodes.front().name = "Replacement chair";
  definition.nodes.emplace_back(scene::AuthoringNode{
      entity(12), entity(11), {}, {}, scene::Renderable{asset(1), {}}, {}, "Nested chair leg"});
  const auto staleRevision = revision;
  frozen = source();
  revision = *baselineDigest(frozen);
  CHECK(c.syncPrefab(root, staleRevision, token(), {}).error() == E::StaleSource);
  CHECK(token() == savedBase);
  const auto changed = c.syncPrefab(root, revision, token(), {});
  REQUIRE(changed.isOk());
  REQUIRE(changed.value().affectsProjection);
  const auto settled = c.listNodes().value();
  const auto anchor = std::ranges::find(settled, root, &ac::NodeSummary::node);
  REQUIRE(anchor != settled.end());
  CHECK(anchor->name == "Placed instance");
  CHECK(anchor->parent == entity(2));
  CHECK(anchor->prefabOrigin == origin);
  CHECK(anchor->prefabBaseline != std::ranges::find(original, root, &ac::NodeSummary::node)->prefabBaseline);
  CHECK(c.getProperty(address(69, kTransform, field("Transform", "localPosition"))).value() ==
        ac::PropertyValue{std::array<float, 3>{2, 1, -3}});
  const auto otherChild = std::ranges::find(original, std::optional(other), &ac::NodeSummary::parent);
  REQUIRE(otherChild != original.end());
  CHECK(std::ranges::find(settled, otherChild->node, &ac::NodeSummary::node)->name == "Original chair");

  const auto undo = c.replayPrefab(root, changed.value().after, changed.value().before, token());
  REQUIRE(undo.isOk());
  CHECK(c.listNodes().value() == original);
  REQUIRE(c.replayPrefab(root, changed.value().before, changed.value().after, token()).isOk());
  CHECK(c.listNodes().value() == settled);
  REQUIRE(c.save().isOk());
  REQUIRE(readText(f.source).starts_with("atlantis_scene_source_version: 10\n"));
  REQUIRE(c.open().isOk());
  CHECK(c.listNodes().value() == settled);

  const auto child = std::ranges::find(settled, std::optional(root), &ac::NodeSummary::parent);
  REQUIRE(child != settled.end());
  REQUIRE_FALSE(refusalOf(c, {ac::SetNodeName{child->node, "Locally edited chair"}}));
  const auto beforeConflict = token();
  CHECK(c.syncPrefab(root, revision, beforeConflict, {}).error() == E::LocalConflict);
  CHECK(c.replayPrefab(root, changed.value().after, changed.value().before, beforeConflict).error() == E::ReplayConflict);
  CHECK(token() == beforeConflict);
}

TEST_CASE("authoring: a guarded name batch matches its document token and writes every name or none",
          "[authoring][session][name]") {
  Fixture f;
  auto& writer = *f.connection;
  auto other = f.session.connect();
  REQUIRE(writer.document()->supportsGuardedNameTransactions);
  const auto subscription = other->subscribe({});
  using G = ac::RequireNameBatchDocument;
  using E = ac::Error;
  const auto start = writer.documentState().value()->token;
  const auto initial = f.document();
  const ac::SetNodeName one{entity(2), "Bistro Lamp 01"};
  const ac::SetNodeName two{entity(3), "Bistro Lamp 02"};
  CHECK(refusalOf(writer, {G{start}, one, one}) == E::InvalidGuardedNameBatch);
  CHECK(refusalOf(writer, {one, G{start}, two}) == E::InvalidGuardedNameBatch);
  CHECK(refusalOf(writer, {G{start}, one, G{start}}) == E::InvalidGuardedNameBatch);
  CHECK(refusalOf(writer, {G{start}, ac::RequireDocumentRevision{start.documentRevision}, one}) ==
        E::InvalidGuardedNameBatch);
  CHECK(refusalOf(writer, {G{start}, ac::SetNodeName{ac::EntityGuid{}, "bad"}}) == E::InvalidGuardedNameBatch);
  CHECK(refusalOf(writer, {G{{entity(99), start.documentRevision}}, one}) == E::NameBatchStaleDocument);
  const auto invalid = writer.submitTransaction({G{start}, one, ac::SetNodeName{entity(3), std::string(256, 'x')}});
  CHECK(writer.drainFailures() == std::vector<ac::CommandFailure>{
      {wa::CommandTicket{invalid.first.value + 2}, E::InvalidNodeName}});
  CHECK(f.document() == initial);
  CHECK(writer.documentState().value()->token == start);
  CHECK(drain(*other, subscription).empty());

  const auto issued = writer.submitTransaction({G{start}, two, one});
  CHECK(issued.count == 3);
  REQUIRE(writer.drainFailures().empty());
  auto renamed = initial;
  renamed.nodes[1].name = one.name;
  renamed.nodes[2].name = two.name;
  CHECK(f.document() == renamed);
  CHECK(writer.documentState().value()->token.documentRevision == start.documentRevision + 1);
  CHECK(drain(*other, subscription) == std::vector<ac::Event>{
      ac::NodeNameChanged{entity(3), two.name}, ac::NodeNameChanged{entity(2), one.name}});

  auto opened = writer.documentState().value()->token;
  REQUIRE_FALSE(refusalOf(*other, {ac::DestroyNode{entity(3)}, ac::CreateNode{entity(3), 2, {}},
                                  ac::SetNodeName{entity(3), two.name}}));
  (void)drain(*other, subscription);
  const auto replaced = f.document();
  CHECK(refusalOf(writer, {G{opened}, one, two}) == E::NameBatchStaleDocument);
  CHECK(f.document() == replaced);
  CHECK(drain(*other, subscription).empty());

  AuthoringSession imported(DocumentSource{sceneGuid(), Imported{f.source}});
  auto incoming = imported.connect();
  REQUIRE(incoming->open().isOk());
  CHECK_FALSE(incoming->document()->supportsGuardedNameTransactions);
  CHECK(refusalOf(*incoming, {G{incoming->documentState().value()->token}, one}) == E::InvalidGuardedNameBatch);
  CHECK_FALSE(refusalOf(*incoming, {one}));
}

TEST_CASE("authoring: guarded name batches admit 64 distinct writes and reject 65",
          "[authoring][session][name]") {
  Fixture f;
  auto& writer = *f.connection;
  std::vector<ac::Command> creation;
  for (unsigned n = 0; n < 65; ++n) creation.push_back(ac::CreateNode{entity(100 + n), 3 + n, {}});
  REQUIRE_FALSE(refusalOf(writer, creation));
  const auto before = writer.documentState().value()->token;
  std::vector<ac::Command> names{ac::RequireNameBatchDocument{before}};
  for (unsigned n = 0; n < 65; ++n) names.push_back(ac::SetNodeName{entity(100 + n), "Lamp"});
  CHECK(refusalOf(writer, names) == ac::Error::InvalidGuardedNameBatch);
  CHECK(writer.documentState().value()->token == before);
  names.pop_back();
  CHECK_FALSE(refusalOf(writer, names));
  CHECK(writer.documentState().value()->token.documentRevision == before.documentRevision + 1);
}
