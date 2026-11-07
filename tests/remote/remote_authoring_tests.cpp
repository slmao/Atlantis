// Plan 0065 M5 (P6; Spec 0065 R11; ADR-0130 D3): AuthoringConnection over
// atlantis.remote/1 -- the codec's authoring values round-trip, and the
// server and client carry a real authoring session end to end over loopback
// (remote_protocol.md §5.1, §6.8).

#include "codec.h"
#include "remote_fixture.h"

#include <atlantis/asset_system/scene_semantic_schema.h>
#include <atlantis/authoring/authoring_session.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace codec = atlantis::remote::codec;
namespace ac = atlantis::connection::authoring;
namespace access = atlantis::world::access;
using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;
using atlantis::remote::RemoteServer;
using atlantis::remote::test::fixtureSceneGuid;
using atlantis::remote::test::kSceneSource;
using atlantis::remote::test::kToken;
using atlantis::schema::TypeId;

[[nodiscard]] EntityGuid node(std::uint32_t n) {
  return atlantis::asset_system::parseEntityGuid("52052052-000" + std::to_string(n) + "-4052-8052-00000000000" +
                                                 std::to_string(n))
      .value();
}

const TypeId kLight = atlantis::schema::typeId("asset_system::scene::Light");
const TypeId kCamera = atlantis::schema::typeId("asset_system::scene::Camera");
const auto kIntensity = atlantis::schema::fieldId("asset_system::scene::Light", "intensity");

template <typename T, typename Decode>
void roundTrips(const T& value, Decode decode) {
  const auto encoded = codec::encode(value);
  const auto decoded = decode(encoded);
  REQUIRE(decoded.has_value());
  CHECK(atlantis::connection::json::write(codec::encode(*decoded)) == atlantis::connection::json::write(encoded));
}

// An authoring session over a copy of the fixture scene, served with a World.
struct ServedAuthoring {
  explicit ServedAuthoring(bool serve = true)
      : source(written()),
        session(atlantis::authoring::DocumentSource{fixtureSceneGuid(), atlantis::authoring::HandAuthored{source}}),
        world(atlantis::remote::test::cookAndDecodeScene(kSceneSource)) {
    if (serve) {
      world.server->serveAuthoring([this] {
        ++opened;
        return session.connect();
      });
    }
  }
  ~ServedAuthoring() {
    std::error_code ec;
    fs::remove(source, ec);
  }
  [[nodiscard]] static fs::path written() {
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path() / "atlantis_remote_tests";
    fs::create_directories(dir);
    const fs::path path =
        dir / ("authoring_" + atlantis::remote::test::processTag() + "_" + std::to_string(counter++) + ".scene.txt");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << kSceneSource;
    return path;
  }

  fs::path source;
  atlantis::authoring::AuthoringSession session;  // before the server: outlives its clients' connections
  atlantis::remote::test::ServedWorld world;
  int opened = 0;
};

}  // namespace

TEST_CASE("remote authoring codec: every authoring value round-trips", "[remote][authoring][codec]") {
  const ac::PropertyAddress address{node(3), kLight, kIntensity};
  roundTrips(address, &codec::decodeSceneAddress);
  const std::vector<ac::Command> commands{
      ac::SetProperty{address, 4.5f},
      ac::CreateNode{node(7), 2, {kLight, kCamera}},
      ac::CreateNode{std::nullopt, 0, {}},
      ac::DestroyNode{node(2)},
      ac::AddComponent{node(5), kLight},
      ac::RemoveComponent{node(5), kLight},
      ac::SetParent{node(3), node(1)},
      ac::SetParent{node(3), std::nullopt},
      ac::SetActiveCamera{node(4)},
      ac::SetActiveCamera{std::nullopt},
      ac::SetNodeName{node(3), "same label"},
      ac::SetNodeName{node(3), ""},
  };
  for (const auto& command : commands) roundTrips(command, &codec::decodeAuthoringCommand);
  const std::vector<ac::Event> events{
      ac::PropertyChanged{address, access::PropertyValue{access::Absent{}}},
      ac::NodeCreated{node(7), 2},
      ac::NodeDestroyed{node(2)},
      ac::ComponentAdded{node(5), kLight},
      ac::ComponentRemoved{node(5), kLight},
      ac::ParentChanged{node(3), std::nullopt},
      ac::ActiveCameraChanged{node(4)},
      ac::DocumentReopened{fixtureSceneGuid()},
      ac::Applied{12},
      ac::NodeNameChanged{node(3), "same label"},
  };
  for (const auto& event : events) {
    roundTrips(event, &codec::decodeAuthoringEvent);
    CHECK(codec::decodeAuthoringEvent(codec::encode(event)) == event);
  }
  roundTrips(ac::EventFilter{}, &codec::decodeAuthoringFilter);
  roundTrips(ac::EventFilter{ac::EventKindSet::only(ac::EventKind::Applied).with(ac::EventKind::PropertyChanged),
                             node(3), kLight},
             &codec::decodeAuthoringFilter);
  roundTrips(ac::CommandFailure{access::CommandTicket{9}, ac::Error::MaxPointLights}, &codec::decodeAuthoringFailure);
  roundTrips(ac::DocumentInfo{fixtureSceneGuid(), ac::Provenance::Imported, "D:/x/y.scene.txt", 5992},
             &codec::decodeDocumentInfo);
  roundTrips(ac::NodeSummary{node(3), node(1)}, &codec::decodeNodeSummary);
  roundTrips(ac::NodeSummary{node(3), node(1), "node label"}, &codec::decodeNodeSummary);
  for (int e = 0; e <= static_cast<int>(ac::Error::InvalidNodeName); ++e) {
    const auto error = static_cast<ac::Error>(e);
    CHECK(codec::decodeAuthoringError(codec::encodeAuthoringError(error)) == error);
  }
  CHECK(codec::decodeApplyError(codec::encodeApplyError(ac::ApplyError::ReloadRefused)) == ac::ApplyError::ReloadRefused);
  CHECK(codec::decodeSaveError(codec::encodeSaveError(ac::SaveError::ImportedSceneNotSavable)) ==
        ac::SaveError::ImportedSceneNotSavable);
  CHECK(codec::decodeOpenError(codec::encodeOpenError(ac::OpenError::SourceInvalid)) == ac::OpenError::SourceInvalid);

  // Malformed values do not decode.
  CHECK_FALSE(codec::decodeAuthoringCommand(atlantis::connection::json::parse(R"({"kind":"Teleport"})").value()));
  CHECK_FALSE(codec::decodeAuthoringFilter(
      atlantis::connection::json::parse(R"({"kinds":["Moved"],"node":null,"component":null})").value()));
  CHECK_FALSE(codec::decodeAuthoringError(atlantis::connection::json::Value::string("NotAnError")));

  // control.status carries the generation; a status without it reads 0.
  const atlantis::connection::RuntimeStatus status{true, 42, fixtureSceneGuid(), 3};
  CHECK(codec::decodeStatus(codec::encode(status)) == status);
  const auto older = atlantis::connection::json::parse(R"({"paused":true,"frame":42,"scene":")" +
                                            atlantis::asset_system::toString(fixtureSceneGuid()) + R"("})")
              .value();
  CHECK(codec::decodeStatus(older)->sceneGeneration == 0);
}

TEST_CASE("remote authoring: the document is reached, edited and observed over the wire", "[remote][authoring]") {
  ServedAuthoring served;
  auto first = served.world.attach();
  auto second = served.world.attach();
  ac::EventFilter lightOnly;
  lightOnly.node = node(3);
  atlantis::connection::AuthoringConnection& a = first->authoring();
  atlantis::connection::AuthoringConnection& b = second->authoring();

  CHECK_FALSE(a.document().has_value());
  CHECK(a.listNodes().error() == ac::Error::NoDocument);
  CHECK(served.opened == 1);  // opened at this client's first authoring.* request
  const auto everything = b.subscribe({});
  const auto light = b.subscribe(lightOnly);
  CHECK(served.opened == 2);

  REQUIRE(a.open().isOk());
  const auto document = a.document();
  REQUIRE(document.has_value());
  CHECK(document->scene == fixtureSceneGuid());
  CHECK(document->provenance == ac::Provenance::HandAuthored);
  CHECK(document->nodeCount == 5);
  CHECK(b.listNodes().value().size() == 5);
  CHECK(b.listNodes().value()[2] == ac::NodeSummary{node(3), node(1)});
  CHECK(b.listComponents(node(3)).value().size() == 2);  // Transform, Light
  CHECK(b.activeCamera().value() == node(4));
  CHECK(b.getProperty({node(3), kLight, kIntensity}).value() == access::PropertyValue{3.0f});
  std::vector<std::string_view> names;
  for (const auto& type : b.schema()) names.push_back(type.name);
  CHECK(std::find(names.begin(), names.end(), "asset_system::scene::Light") != names.end());

  // A write from one client, seen by the other's subscriptions.
  const auto ticket = a.submit(ac::SetProperty{{node(3), kLight, kIntensity}, 7.0f});
  CHECK(ticket.value > 0);
  CHECK(a.drainFailures().empty());
  CHECK(b.getProperty({node(3), kLight, kIntensity}).value() == access::PropertyValue{7.0f});
  const auto seen = b.drainEvents(everything).value();
  REQUIRE(seen.size() == 2);
  CHECK(seen[0] == ac::Event{ac::DocumentReopened{fixtureSceneGuid()}});
  CHECK(seen[1] == ac::Event{ac::PropertyChanged{{node(3), kLight, kIntensity}, 7.0f}});
  CHECK(b.drainEvents(light).value() ==
        std::vector<ac::Event>{ac::PropertyChanged{{node(3), kLight, kIntensity}, 7.0f}});

  // A refused transaction: its failure goes to its submitter only.
  const auto refused = a.submitTransaction({ac::SetProperty{{node(3), kLight, kIntensity}, 8.0f},
                                            ac::SetParent{node(1), node(3)}});  // node 3 is node 1's child
  CHECK(refused.count == 2);
  CHECK(a.drainFailures() == std::vector<ac::CommandFailure>{{refused.first, ac::Error::ParentAcyclic}});
  CHECK(b.drainFailures().empty());
  CHECK(b.drainEvents(everything).value().empty());

  // Apply needs the host's sink; the host's Applied reaches every subscriber.
  CHECK(a.apply().error() == ac::ApplyError::NoSceneSink);
  int reloads = 0;
  served.session.setSceneSink([&](ac::ApplyId, atlantis::asset_system::ValidatedSceneData) {
    ++reloads; return atlantis::Result<std::optional<ac::ApplyId>, std::monostate>::Ok({});
  });
  REQUIRE(a.apply().isOk());
  CHECK(reloads == 1);
  const auto receipt = a.applyStatus().value().request->receipt;
  served.session.reportReload(receipt.id, atlantis::Result<std::uint64_t, ac::ReloadError>::Ok(5));
  CHECK(b.drainEvents(everything).value() == std::vector<ac::Event>{ac::Applied{5}});

  // Save writes the source; unsubscribe; nothing failed on the wire.
  REQUIRE(b.save().isOk());
  CHECK(b.unsubscribe(light).isOk());
  CHECK(b.unsubscribe(light).error() == atlantis::connection::ConnectionError::UnknownSubscription);
  CHECK_FALSE(first->failure().has_value());
  CHECK_FALSE(second->failure().has_value());
}

TEST_CASE("remote authoring codec: names preserve legacy summaries and enforce received value domains",
          "[remote][authoring][codec][name]") {
  namespace json = atlantis::connection::json;
  const auto legacy = json::parse(R"({"node":"52052052-0003-4052-8052-000000000003","parent":null})").value();
  REQUIRE(codec::decodeNodeSummary(legacy).has_value());
  CHECK(codec::decodeNodeSummary(legacy)->name.empty());
  const std::vector<std::string> values{
      "", "two  spaces \"quotes\" \\ 场景", std::string(255, 'a'), std::string(256, 'a'),
      std::string("a\0b", 3), "a\nb", "\x7F", "\xC2\x80", "\xE2\x80\xA8", "\xE2\x80\xA9",
      "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE5", "\x80", "场景"};
  for (const std::string& name : values) {
    const bool valid = atlantis::asset_system::scene::isValidNodeName(name);
    const auto encoded = codec::encode(ac::NodeSummary{node(3), std::nullopt, name});
    CHECK(codec::decodeNodeSummary(encoded).has_value() == valid);
    CHECK(codec::decodeAuthoringEvent(codec::encode(ac::Event{ac::NodeNameChanged{node(3), name}})).has_value() == valid);
    // A string's domain error belongs to the transaction, not the RPC shape.
    const auto command = codec::decodeAuthoringCommand(codec::encode(ac::Command{ac::SetNodeName{node(3), name}}));
    REQUIRE(command.has_value());
    CHECK(std::get<ac::SetNodeName>(*command).name == name);
  }
  for (const json::Value& malformed : {json::Value(), json::Value::number(std::uint64_t{3}), json::Value::boolean(true)}) {
    auto summary = legacy;
    summary.set("name", malformed);
    CHECK_FALSE(codec::decodeNodeSummary(summary));
  }
  auto noName = codec::encode(ac::Command{ac::SetNodeName{node(3), ""}});
  noName.set("name", json::Value());
  CHECK_FALSE(codec::decodeAuthoringCommand(noName));
  auto named = codec::encode(ac::NodeSummary{node(3), std::nullopt, "label"});
  // The old decoder's node/parent accesses still find their original values.
  CHECK(codec::decodeEntity(*named.find("node")) == node(3));
  CHECK(named.find("parent")->isNull());
}

TEST_CASE("remote authoring: two clients observe rename and atomic refusal under the same tickets",
          "[remote][authoring][name]") {
  ServedAuthoring served;
  auto first = served.world.attach();
  auto second = served.world.attach();
  auto& a = first->authoring();
  auto& b = second->authoring();
  REQUIRE(a.open().isOk());
  const auto all = b.subscribe({});
  const auto named = b.subscribe({ac::EventKindSet::only(ac::EventKind::NodeNameChanged), node(3), std::nullopt});
  a.submit(ac::SetNodeName{node(3), "shared 场景"});
  CHECK(a.drainFailures().empty());
  CHECK(b.listNodes().value()[2].name == "shared 场景");
  const std::vector<ac::Event> expected{ac::NodeNameChanged{node(3), "shared 场景"}};
  CHECK(b.drainEvents(all).value() == expected);
  CHECK(b.drainEvents(named).value() == expected);
  const auto tickets = a.submitTransaction({ac::SetNodeName{node(3), "would change"},
                                           ac::SetNodeName{node(1), std::string(256, 'x')}});
  const auto failures = a.drainFailures();
  REQUIRE(failures.size() == 1);
  CHECK(failures[0].ticket.value == tickets.first.value + 1);
  CHECK(failures[0].error == ac::Error::InvalidNodeName);
  CHECK(b.listNodes().value()[2].name == "shared 场景");
  CHECK(b.drainEvents(all).value().empty());
}

TEST_CASE("remote authoring: a Runtime that serves no authoring session answers NoAuthoring", "[remote][authoring]") {
  ServedAuthoring served(/*serve=*/false);
  auto session = served.world.attach();
  (void)session->authoring().document();
  CHECK(session->failure() == atlantis::remote::RemoteError::ProtocolError);
}
