#include "cli_tree_fixture.h"

#include <atlantis/connection/json.h>
#include <atlantis/schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

// Plan 0055 M6 (Spec 0055 R4-R7, R10; rulings Q3-Q5; J5, J7): every command of
// the tree against its expected human text and its expected JSON envelope
// (J-a), on the Spec 0054 CLI tests' fixture scene, through an InProcess
// connection and a fake Runtime control; the error shape on stdout and on
// stderr (--diagnostics=json) with exit codes 0-4; the --with/--where
// filters; writes and their outcomes (and a write while paused); `tx` commit
// and abort; byte determinism; the REPL.

namespace {

using atlantis::cli::test::Cli;

constexpr std::string_view kPoint = "52052052-0003-4052-8052-000000000003";
constexpr std::string_view kDirectional = "52052052-0002-4052-8052-000000000002";
constexpr std::string_view kBare = "52052052-0005-4052-8052-000000000005";

// A type's identity as the JSON shows it.
[[nodiscard]] std::string identity(std::string_view shortName) {
  const std::string qualified = "world::" + std::string(shortName);
  char id[24];
  std::snprintf(id, sizeof id, "0x%016llx", static_cast<unsigned long long>(atlantis::schema::typeId(qualified).value));
  return "{\"name\":\"" + std::string(shortName) + "\",\"qualified\":\"" + qualified + "\",\"typeId\":\"" + id + "\"}";
}

[[nodiscard]] std::string envelope(std::string_view command, std::string_view result) {
  return "{\"atlantis\":\"cli/1\",\"command\":\"" + std::string(command) + "\",\"ok\":true,\"result\":" +
         std::string(result) + ",\"diagnostics\":[]}\n";
}

[[nodiscard]] std::string failure(std::string_view command, std::string_view result, std::string_view error) {
  return "{\"atlantis\":\"cli/1\",\"command\":\"" + std::string(command) + "\",\"ok\":false,\"result\":" +
         std::string(result) + ",\"error\":" + std::string(error) + ",\"diagnostics\":[]}\n";
}

}  // namespace

TEST_CASE("atlantis: schema list and schema inspect", "[cli][tree]") {
  Cli human;
  CHECK(human.run("schema list") == 0);
  CHECK(human.out.str() ==
        "Transform struct\nCameraFog struct\nCameraBloom struct\nCamera struct\nLight struct\nLightKind enum\n"
        "Renderable struct\nWorldMatrix struct\n");
  CHECK(human.err.str().empty());
  CHECK(human.run("schema inspect Light") == 0);
  CHECK(human.out.str() ==
        "Light struct (world::Light) v1\n"
        "  kind enum LightKind serializable,editable\n"
        "  color vec3 serializable,editable\n"
        "  intensity float32 serializable,editable\n"
        "  range float32 serializable,editable\n");

  Cli json(true);
  CHECK(json.run("schema list") == 0);
  CHECK(json.out.str() ==
        envelope("schema list", "[" + identity("Transform") + "," + identity("CameraFog") + "," +
                                    identity("CameraBloom") + "," + identity("Camera") + "," + identity("Light") + "," +
                                    identity("LightKind") + "," + identity("Renderable") + "," +
                                    identity("WorldMatrix") + "]"));
  CHECK(json.run("schema inspect Light") == 0);
  const std::string light = identity("Light");
  CHECK(json.out.str() ==
        envelope("schema inspect",
                 light.substr(0, light.size() - 1) +
                     ",\"kind\":\"struct\",\"version\":1,\"fields\":["
                     "{\"name\":\"kind\",\"kind\":\"enum\",\"type\":" + identity("LightKind") +
                     ",\"flags\":[\"serializable\",\"editable\"]},"
                     "{\"name\":\"color\",\"kind\":\"vec3\",\"flags\":[\"serializable\",\"editable\"]},"
                     "{\"name\":\"intensity\",\"kind\":\"float32\",\"flags\":[\"serializable\",\"editable\"]},"
                     "{\"name\":\"range\",\"kind\":\"float32\",\"flags\":[\"serializable\",\"editable\"]}]}"));
  CHECK(json.run("schema inspect LightKind") == 0);
  const std::string kind = identity("LightKind");
  CHECK(json.out.str() ==
        envelope("schema inspect", kind.substr(0, kind.size() - 1) +
                                       ",\"kind\":\"enum\",\"version\":1,\"constants\":[{\"name\":\"Directional\","
                                       "\"value\":0},{\"name\":\"Point\",\"value\":1}]}"));
}

TEST_CASE("atlantis: world list and world inspect", "[cli][tree]") {
  Cli human;
  CHECK(human.run("world list") == 0);
  CHECK(human.out.str() == "00550055-0055-4055-8055-005500550055 5\n");
  CHECK(human.run("world inspect") == 0);
  CHECK(human.out.str() ==
        "scene 00550055-0055-4055-8055-005500550055\nentities 5\n"
        "components Transform=5,Camera=1,Light=2,Renderable=1,WorldMatrix=5\npaused false\nframe 0\n");

  Cli json(true);
  CHECK(json.run("world list") == 0);
  CHECK(json.out.str() ==
        envelope("world list", R"([{"scene":"00550055-0055-4055-8055-005500550055","entities":5}])"));
  json.runtime.frame();
  json.runtime.pause();
  CHECK(json.run("world inspect") == 0);
  CHECK(json.out.str() ==
        envelope("world inspect", R"({"scene":"00550055-0055-4055-8055-005500550055","entities":5,)"
                                  R"("components":{"Transform":5,"Camera":1,"Light":2,"Renderable":1,"WorldMatrix":5},)"
                                  R"("paused":true,"frame":1})"));
}

TEST_CASE("atlantis: entity list, its filters, and its alias world entities", "[cli][tree][filter]") {
  Cli human;
  const std::string all =
      "52052052-0001-4052-8052-000000000001 WorldMatrix,Transform,Renderable\n"
      "52052052-0002-4052-8052-000000000002 WorldMatrix,Transform,Light\n"
      "52052052-0003-4052-8052-000000000003 WorldMatrix,Transform,Light\n"
      "52052052-0004-4052-8052-000000000004 WorldMatrix,Transform,Camera\n"
      "52052052-0005-4052-8052-000000000005 WorldMatrix,Transform\n";
  CHECK(human.run("entity list") == 0);
  CHECK(human.out.str() == all);
  CHECK(human.run("world entities") == 0);  // the Spec 0054 name: byte-identical
  CHECK(human.out.str() == all);
  CHECK(human.run("entity list --with Light") == 0);
  CHECK(human.out.str() ==
        "52052052-0002-4052-8052-000000000002 WorldMatrix,Transform,Light\n"
        "52052052-0003-4052-8052-000000000003 WorldMatrix,Transform,Light\n");
  CHECK(human.run("entity list --with Light --where Light.kind=Point") == 0);
  CHECK(human.out.str() == "52052052-0003-4052-8052-000000000003 WorldMatrix,Transform,Light\n");
  CHECK(human.run("entity list --where Light.kind=Directional") == 0);  // --where implies the component
  CHECK(human.out.str() == "52052052-0002-4052-8052-000000000002 WorldMatrix,Transform,Light\n");
  CHECK(human.run("entity list --where Light.color=1,0.6,0.3") == 0);  // a vector, comma-separated
  CHECK(human.out.str() == "52052052-0003-4052-8052-000000000003 WorldMatrix,Transform,Light\n");
  CHECK(human.run("entity list --where Light.intensity=3 --where Light.range=2.5") == 0);  // ANDed
  CHECK(human.out.str() == "52052052-0003-4052-8052-000000000003 WorldMatrix,Transform,Light\n");
  // Exact equality after parse: 3.0000001 parses to the float 3 exactly; 3.001 does not.
  CHECK(human.run("entity list --where Light.intensity=3.0000001") == 0);
  CHECK(human.out.str() == "52052052-0003-4052-8052-000000000003 WorldMatrix,Transform,Light\n");
  CHECK(human.run("entity list --where Light.intensity=3.001") == 0);
  CHECK(human.out.str().empty());
  CHECK(human.run("entity list --with Camera --with Light") == 0);
  CHECK(human.out.str().empty());

  Cli json(true);
  CHECK(json.run("entity list --with Light --where Light.kind=Point") == 0);
  CHECK(json.out.str() ==
        envelope("entity list",
                 R"([{"guid":"52052052-0003-4052-8052-000000000003","components":["WorldMatrix","Transform","Light"]}])"));
  // Filter errors are text or usage errors.
  CHECK(json.run("entity list --with Nope") == 2);
  CHECK(json.run("entity list --where Light.kind=Spot") == 2);
  CHECK(json.run("entity list --where Light.kind") == 2);
  CHECK(json.run("entity list --frobnicate") == 2);
}

TEST_CASE("atlantis: entity inspect and property get, with typed JSON values", "[cli][tree]") {
  Cli human;
  CHECK(human.run("property get " + std::string(kPoint) + " Light.intensity") == 0);
  CHECK(human.out.str() == "3\n");
  CHECK(human.run("entity get " + std::string(kPoint) + " world::Light.kind") == 0);  // alias
  CHECK(human.out.str() == "Point\n");
  CHECK(human.run("entity inspect " + std::string(kDirectional)) == 0);
  CHECK(human.out.str().starts_with("52052052-0002-4052-8052-000000000002\n  WorldMatrix.column0 = "));
  CHECK(human.out.str().find("  Light.kind = Directional\n  Light.color = 0.600000024 0.699999988 1\n"
                             "  Light.intensity = 1.20000005\n") != std::string::npos);

  Cli json(true);
  const std::string point(kPoint);
  CHECK(json.run("property get " + point + " Light.intensity") == 0);
  CHECK(json.out.str() == envelope("property get", "{\"guid\":\"" + point + "\",\"path\":\"Light.intensity\",\"value\":3}"));
  CHECK(json.run("property get " + point + " Light.kind") == 0);
  CHECK(json.out.str() ==
        envelope("property get",
                 "{\"guid\":\"" + point + "\",\"path\":\"Light.kind\",\"value\":{\"name\":\"Point\",\"value\":1}}"));
  CHECK(json.run("property get " + point + " Light.color") == 0);
  CHECK(json.out.str() ==
        envelope("property get", "{\"guid\":\"" + point + "\",\"path\":\"Light.color\",\"value\":[1,0.6,0.3]}"));
  CHECK(json.run("property get 52052052-0001-4052-8052-000000000001 Renderable.meshAsset") == 0);
  CHECK(json.out.str().find(R"("value":")") != std::string::npos);  // UInt64: a decimal string
  CHECK(json.run("entity inspect " + point) == 0);
  CHECK(json.out.str().find(R"("Light":{"kind":{"name":"Point","value":1},"color":[1,0.6,0.3],"intensity":3,)"
                            R"("range":2.5})") != std::string::npos);
  CHECK(json.out.str().find(R"("Transform":{"localPosition":[0.8,0.3,0.5],)") != std::string::npos);
}

TEST_CASE("atlantis: writes report their outcome once applied; a write while paused stays pending",
          "[cli][tree][write]") {
  Cli human;
  const std::string point(kPoint);
  CHECK(human.run("property set " + point + " Light.intensity 24") == 0);
  CHECK(human.out.str() == "ok " + point + " Light.intensity = 24\n");
  CHECK(human.run("entity set " + point + " Light.intensity 12") == 0);  // alias
  CHECK(human.out.str() == "ok " + point + " Light.intensity = 12\n");
  CHECK(human.run("entity create 52052052-0000-4052-8052-0000000000c2 --component Light --component Transform") == 0);
  CHECK(human.out.str() ==
        "ok created 52052052-0000-4052-8052-0000000000c2\n"
        "ok added 52052052-0000-4052-8052-0000000000c2 Light\n"
        "ok added 52052052-0000-4052-8052-0000000000c2 Transform\n");
  CHECK(human.run("component remove 52052052-0000-4052-8052-0000000000c2 Light") == 0);
  CHECK(human.out.str() == "ok removed 52052052-0000-4052-8052-0000000000c2 Light\n");
  CHECK(human.run("entity destroy 52052052-0000-4052-8052-0000000000c2") == 0);
  CHECK(human.out.str() == "ok destroyed 52052052-0000-4052-8052-0000000000c2\n");
  // Refused: exit 1, on stderr.
  CHECK(human.run("entity destroy 52052052-0000-4052-8052-0000000000c2") == 1);
  CHECK(human.out.str().empty());
  CHECK(human.err.str() == "refused UnknownEntity\n");
  CHECK(human.run("property set " + point + " Light.kind Directional") == 1);
  CHECK(human.err.str() == "refused LightLimitExceeded\n");
  // entity create without a GUID makes one (RFC 9562 v4).
  CHECK(human.run("entity create") == 0);
  const std::string created = human.out.str();
  REQUIRE(created.size() == std::string("ok created ").size() + 36 + 1);
  CHECK(created[std::string("ok created ").size() + 14] == '4');  // the version nibble

  Cli json(true);
  CHECK(json.run("component add " + std::string(kBare) + " Camera") == 0);
  CHECK(json.out.str() ==
        envelope("component add", "{\"ticket\":1,\"outcome\":{\"events\":[{\"kind\":\"ComponentAdded\",\"guid\":\"" +
                                      std::string(kBare) + "\",\"component\":" + identity("Camera") + "}]}}"));
  CHECK(json.run("property set " + point + " Light.intensity 6") == 0);
  CHECK(json.out.str() ==
        envelope("property set", "{\"ticket\":2,\"outcome\":{\"events\":[{\"kind\":\"PropertyChanged\",\"guid\":\"" +
                                     point + "\",\"path\":\"Light.intensity\",\"value\":6}]}}"));
  CHECK(json.run("component add " + std::string(kBare) + " Camera") == 1);
  CHECK(json.out.str() ==
        failure("component add", R"({"ticket":3,"outcome":{"refused":{"code":"ComponentAlreadyPresent"}}})",
                R"({"code":"ComponentAlreadyPresent","category":"refused",)"
                R"("message":"the Runtime World refused it: ComponentAlreadyPresent","subject":{"ticket":3}})"));
  // Paused: the write is submitted and stays pending until a step applies it.
  json.runtime.pause();
  CHECK(json.run("property set " + point + " Light.intensity 7") == 0);
  CHECK(json.out.str() == envelope("property set", R"({"ticket":4,"outcome":{"pending":true}})"));
  CHECK(json.run("property get " + point + " Light.intensity") == 0);
  CHECK(json.out.str().find(R"("value":6})") != std::string::npos);
  CHECK(json.run("runtime step") == 0);
  CHECK(json.run("property get " + point + " Light.intensity") == 0);
  CHECK(json.out.str().find(R"("value":7})") != std::string::npos);
}

TEST_CASE("atlantis: runtime pause, resume and step, with the frame data and image capture",
          "[cli][tree][runtime]") {
  Cli human;
  CHECK(human.run("runtime pause") == 0);
  CHECK(human.out.str() == "paused\n");
  CHECK(human.run("runtime step --frames 2") == 0);
  CHECK(human.out.str() == "frame 2\n");
  CHECK(human.run("runtime step --capture") == 0);
  CHECK(human.out.str() ==
        "frame 3\n"
        "directional direction 0 -1 0 color 0.600000024 0.699999988 1 intensity 1.20000005\n"
        "point position 1.5 2.5 3.5 color 1 0.600000024 0.300000012 intensity 3 range 2.5\n"
        "draw items 1\n");
  CHECK(human.run("runtime resume") == 0);
  CHECK(human.out.str() == "running\n");

  Cli json(true);
  const std::filesystem::path image = std::filesystem::absolute("capture-test.png").lexically_normal();
  CHECK(json.run("runtime step --capture capture-test.png") == 0);
  std::string imagePath = image.string();
  std::string escaped;
  for (const char c : imagePath) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
  CHECK(json.out.str() ==
        envelope("runtime step",
                 R"({"frame":1,"applied":true,"capture":{"frameData":{"directionalLights":[{"direction":[0,-1,0],)"
                 R"("color":[0.6,0.7,1],"intensity":1.2}],"pointLights":[{"position":[1.5,2.5,3.5],"color":[1,0.6,0.3],)"
                 R"("intensity":3,"range":2.5}],"view":[1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],)"
                 R"("projection":[0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0],"drawItemCount":1},"image":{"path":")" +
                     escaped + R"(","width":64,"height":32}}})"));
  CHECK(json.run("runtime step --frames 0") == 2);
  CHECK(json.run("runtime pause") == 0);
  CHECK(json.out.str() == envelope("runtime pause", R"({"paused":true})"));
}

TEST_CASE("atlantis: errors -- the shape, --diagnostics=json on stderr, and exit codes", "[cli][tree][errors]") {
  Cli json(true, true);
  const std::string point(kPoint);
  CHECK(json.run("property get " + point + " Light.nope") == 2);
  const std::string unknownField =
      R"({"code":"UnknownField","category":"text","message":"UnknownField in 'Light.nope'",)"
      R"("subject":{"token":"Light.nope"}})";
  CHECK(json.out.str() == failure("property get", "null", unknownField));
  CHECK(json.err.str() == unknownField + "\n");
  CHECK(json.run("property get " + std::string(kBare) + " Light.intensity") == 1);
  CHECK(json.err.str() ==
        R"({"code":"ComponentMissing","category":"refused","message":"the Runtime World refused it: ComponentMissing",)"
        R"("subject":{"guid":"52052052-0005-4052-8052-000000000005","path":"Light.intensity"}})"
        "\n");
  CHECK(json.run("entity frobnicate") == 2);
  CHECK(json.err.str() ==
        R"({"code":"UnknownCommand","category":"usage","message":"unknown command 'entity frobnicate'","subject":{}})"
        "\n");
  CHECK(json.run("schema inspect") == 2);
  CHECK(json.err.str().starts_with(R"({"code":"Usage","category":"usage",)"));

  // Runtime diagnostics newer than the session ride in the envelope, and on
  // stderr as JSON.
  json.runtime.warn("checkConformalTransform() failed");
  CHECK(json.run("world list") == 0);
  CHECK(json.out.str().ends_with(
      R"("diagnostics":[{"sequence":1,"severity":"warning","message":"checkConformalTransform() failed"}]})"
      "\n"));
  CHECK(json.err.str() ==
        R"({"code":"RuntimeDiagnostic","category":"runtime","message":"checkConformalTransform() failed",)"
        R"("subject":{"sequence":1,"severity":"warning"}})"
        "\n");
  CHECK(json.run("world list") == 0);  // reported once
  CHECK(json.out.str().ends_with("\"diagnostics\":[]}\n"));

  // Human mode: errors on stderr, nothing on stdout.
  Cli human;
  CHECK(human.run("property get nope Light.intensity") == 2);
  CHECK(human.out.str().empty());
  CHECK(human.err.str() == "error: MalformedGuid 'nope'\n");

  // Without a Runtime control, `runtime` commands are a runtime error (exit 4).
  CHECK(atlantis::cli::exitCode(atlantis::cli::ErrorCategory::Runtime) == 4);
  CHECK(atlantis::cli::exitCode(atlantis::cli::ErrorCategory::Connection) == 3);
}

TEST_CASE("atlantis: tx submits a file of writes as one transaction -- commit, and abort at n", "[cli][tree][tx]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "atlantis_cli_tx" / std::to_string(std::random_device{}());
  fs::create_directories(dir);
  const auto write = [&](const char* name, const char* text) {
    std::ofstream(dir / name, std::ios::binary) << text;
    return (dir / name).string();
  };
  const std::string commit = write("commit.tx",
                                   "# a light, made in one transaction\n"
                                   "entity create 52052052-0000-4052-8052-0000000000d1\n"
                                   "\n"
                                   "component add 52052052-0000-4052-8052-0000000000d1 Light\n"
                                   "property set 52052052-0000-4052-8052-0000000000d1 Light.intensity 9\n");
  const std::string abort = write("abort.tx",
                                  "entity create 52052052-0000-4052-8052-0000000000d2\n"
                                  "property set 52052052-0003-4052-8052-000000000003 Light.kind Directional\n");
  const std::string malformed = write("malformed.tx", "entity create 52052052-0000-4052-8052-0000000000d3\nschema list\n");

  Cli human;
  CHECK(human.run("tx " + commit) == 0);
  CHECK(human.out.str() ==
        "ok created 52052052-0000-4052-8052-0000000000d1\n"
        "ok added 52052052-0000-4052-8052-0000000000d1 Light\n"
        "ok 52052052-0000-4052-8052-0000000000d1 Light.intensity = 9\n"
        "committed 3\n");

  Cli json(true);
  CHECK(json.run("tx " + abort) == 1);
  CHECK(json.out.str() ==
        failure("tx", R"({"ticket":{"first":1,"count":2},"committed":false,"at":1,"error":"LightLimitExceeded"})",
                R"({"code":"LightLimitExceeded","category":"refused",)"
                R"("message":"the Runtime World refused it: LightLimitExceeded","subject":{"ticket":2,"at":1}})"));
  CHECK(json.run("entity list --with Light") == 0);
  CHECK(json.out.str().find("0000000000d2") == std::string::npos);  // nothing of it applied
  CHECK(json.run("tx " + malformed) == 2);  // parsed whole first: nothing submitted
  CHECK(json.out.str().find(R"("code":"NotAWrite")") != std::string::npos);
  CHECK(json.out.str().find(R"("line":2)") != std::string::npos);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("atlantis: the same command on the same world prints the same bytes", "[cli][tree]") {
  Cli first(true);
  Cli second(true);
  for (const char* command :
       {"schema list", "world inspect", "entity list", "entity inspect 52052052-0001-4052-8052-000000000001",
        "entity list --with Light --where Light.kind=Point", "property get 52052052-0004-4052-8052-000000000004 Camera.fog.density"}) {
    INFO(command);
    REQUIRE(first.run(command) == 0);
    const std::string once = first.out.str();
    REQUIRE(first.run(command) == 0);
    CHECK(first.out.str() == once);
    REQUIRE(second.run(command) == 0);
    CHECK(second.out.str() == once);
  }
}

TEST_CASE("atlantis repl: one command per line, the prompt on stderr, errors do not end it", "[cli][tree][repl]") {
  Cli cli(true);
  std::istringstream in("world list\n\nproperty get nope Light.intensity\nruntime pause\nquit\nworld list\n");
  CHECK(cli.invocation->repl(in) == 0);
  CHECK(cli.out.str() ==
        envelope("world list", R"([{"scene":"00550055-0055-4055-8055-005500550055","entities":5}])") +
            failure("property get", "null",
                    R"({"code":"MalformedGuid","category":"text","message":"MalformedGuid in 'nope'",)"
                    R"("subject":{"token":"nope"}})") +
            envelope("runtime pause", R"({"paused":true})"));
  CHECK(cli.err.str() == "atlantis> atlantis> atlantis> atlantis> atlantis> \n");
  std::istringstream eof("schema inspect LightKind");  // ends at end of input, without a newline too
  cli.out.str({});
  CHECK(cli.invocation->repl(eof) == 0);
  CHECK(cli.out.str().starts_with(R"({"atlantis":"cli/1","command":"schema inspect","ok":true,)"));
}

TEST_CASE("atlantis: global flags anywhere on the command line", "[cli][tree]") {
  const auto parse = [](std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> list(args);
    return atlantis::cli::parseArguments(list);
  };
  const auto parsed = parse({"entity", "--json", "list", "--session", "s.json", "--with", "Light", "--diagnostics=json"});
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().json);
  CHECK(parsed.value().diagnosticsJson);
  CHECK(parsed.value().session == std::optional<std::string>{"s.json"});
  CHECK(parsed.value().command == std::vector<std::string>{"entity", "list", "--with", "Light"});
  CHECK(parse({"--session"}).isErr());
  CHECK(parse({"--diagnostics=xml"}).isErr());
  CHECK(parse({"--help"}).value().help);
}

// Spec 0064 / ADR-0128 D1-D3 (Plan 0064 M3): the Viewport camera and the
// ray-cast, the calls Atlantis Editor App makes, against the fake Runtime.
TEST_CASE("atlantis: runtime camera set and clear, and runtime raycast", "[cli][tree][runtime][camera]") {
  Cli human;
  CHECK(human.run("runtime camera set --matrix 1 0 0 0 0 1 0 0 0 0 1 0 0 2 10 1 --fov 1.5 --near 0.1 --far 100") == 0);
  CHECK(human.out.str() == "camera set\n");
  REQUIRE(human.runtime.viewportCamera.has_value());
  CHECK(human.runtime.viewportCamera->worldMatrix[13] == 2.0f);
  CHECK(human.runtime.viewportCamera->worldMatrix[14] == 10.0f);
  CHECK(human.runtime.viewportCamera->fovYRadians == 1.5f);
  CHECK(human.runtime.viewportCamera->farZ == 100.0f);
  CHECK(human.run("runtime camera clear") == 0);
  CHECK(human.out.str() == "camera cleared\n");
  CHECK_FALSE(human.runtime.viewportCamera.has_value());
  CHECK(human.run("runtime camera set --matrix 1 0 0 --fov 1.5 --near 0.1 --far 100") == 2);
  CHECK(human.run("runtime camera set --fov 1.5 --near 0.1 --far 100") == 2);
  CHECK(human.run("runtime camera spin") == 2);
  CHECK(human.runtime.viewportCameraCalls == 2);

  // The fake keeps no geometry until told: NoGeometry, a Runtime error.
  CHECK(human.run("runtime raycast --origin 0 1 5 --direction 0 0 -2") == 4);
  REQUIRE(human.runtime.lastRay.has_value());
  CHECK(human.runtime.lastRay->direction == std::array<float, 3>{0.0f, 0.0f, -1.0f});  // normalized
  human.runtime.rayAnswer = std::optional<atlantis::connection::RayHit>{};
  CHECK(human.run("runtime raycast --origin 0 1 5 --direction 0 0 -1") == 0);
  CHECK(human.out.str() == "no hit\n");
  human.runtime.rayAnswer = std::optional<atlantis::connection::RayHit>{
      atlantis::connection::RayHit{atlantis::asset_system::parseEntityGuid(kPoint).value(), 4.5f}};
  CHECK(human.run("runtime raycast --origin 0 1 5 --direction 0 0 -1") == 0);
  CHECK(human.out.str() == std::string(kPoint) + " 4.5\n");
  CHECK(human.run("runtime raycast --origin 0 1 5 --direction 0 0 0") == 2);
  CHECK(human.run("runtime raycast --origin 0 1 5") == 2);

  Cli json(true, true);
  CHECK(json.run("runtime camera set --matrix 1 0 0 0 0 1 0 0 0 0 1 0 0 2 10 1 --fov 1.5 --near 0.5 --far 100") == 0);
  CHECK(json.out.str() == envelope("runtime camera set",
                                   R"({"camera":{"worldMatrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,2,10,1],)"
                                   R"("fovYRadians":1.5,"nearZ":0.5,"farZ":100}})"));
  CHECK(json.run("runtime camera clear") == 0);
  CHECK(json.out.str() == envelope("runtime camera clear", R"({"camera":null})"));
  CHECK(json.run("runtime raycast --origin 0 1 5 --direction 0 0 -1") == 4);
  CHECK(json.err.str() ==
        R"({"code":"NoGeometry","category":"runtime","message":"the Runtime could not ray-cast: NoGeometry",)"
        R"("subject":{}})"
        "\n");
  json.runtime.rayAnswer = std::optional<atlantis::connection::RayHit>{};
  CHECK(json.run("runtime raycast --origin 0 1 5 --direction 0 0 -1") == 0);
  CHECK(json.out.str() == envelope("runtime raycast", R"({"hit":null})"));
  json.runtime.rayAnswer = std::optional<atlantis::connection::RayHit>{
      atlantis::connection::RayHit{atlantis::asset_system::parseEntityGuid(kPoint).value(), 4.5f}};
  CHECK(json.run("runtime raycast --origin 0 1 5 --direction 0 0 -1") == 0);
  CHECK(json.out.str() ==
        envelope("runtime raycast", R"({"hit":{"guid":")" + std::string(kPoint) + R"(","distance":4.5}})"));
}

// Plan 0066 P2 (Spec 0066 R2, R5, R6; ADR-0132 D2): the Viewport overlay commands and a capture's --with-overlay,
// against the fake Runtime (which refuses an overlay as the Runtime does).
TEST_CASE("atlantis: runtime overlay set, select and clear, and runtime step --with-overlay",
          "[cli][tree][runtime][overlay]") {
  const std::string point(kPoint);
  const std::filesystem::path file = std::filesystem::absolute("overlay-test.json");
  const auto write = [&](std::string_view text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
  };
  const std::string group = R"({"anchor":null,"segments":[{"a":[0,0,0],"b":[1,0,0],"color":[1,0.5,0,1],)"
                            R"("depth":"OnTop","width":2}],"triangles":[],"points":[]})";

  Cli human;
  write(R"({"selection":[")" + point + R"("],"groups":[)" + group + "]}");
  CHECK(human.run("runtime overlay set " + file.string()) == 0);
  CHECK(human.out.str() == "overlay set: 1 groups, 1 selected\n");
  REQUIRE(human.runtime.viewportOverlay.has_value());
  REQUIRE(human.runtime.viewportOverlay->groups.size() == 1);
  CHECK(human.runtime.viewportOverlay->groups[0].segments[0].widthPixels == 2.0f);
  CHECK(human.runtime.viewportOverlay->groups[0].segments[0].depth == atlantis::connection::OverlayDepth::OnTop);
  CHECK(human.run("runtime overlay select " + point + " " + point) == 0);
  CHECK(human.out.str() == "overlay selects 2\n");
  CHECK(human.runtime.viewportOverlay->groups.empty());
  CHECK(human.runtime.viewportOverlay->selection.size() == 2);
  CHECK(human.run("runtime overlay clear") == 0);
  CHECK(human.out.str() == "overlay cleared\n");
  CHECK_FALSE(human.runtime.viewportOverlay.has_value());
  CHECK(human.runtime.viewportOverlayCalls == 3);

  // Refusals: nothing reaches the Runtime but the ill-formed overlay, which it refuses (nothing changed).
  CHECK(human.run("runtime overlay") == 2);
  CHECK(human.run("runtime overlay spin") == 2);
  CHECK(human.run("runtime overlay select") == 2);
  CHECK(human.run("runtime overlay select not-a-guid") == 2);
  CHECK(human.run("runtime overlay clear now") == 2);
  CHECK(human.run("runtime overlay set") == 2);
  CHECK(human.run("runtime overlay set nowhere-overlay.json") == 2);
  write(R"({"selection":[],"groups":[{"anchor":null}]})");
  CHECK(human.run("runtime overlay set " + file.string()) == 2);
  write(R"({"selection":[],"groups":[{"anchor":null,"segments":[{"a":[0,0,0],"b":[1,0,0],"color":[1,1,1,1],)"
        R"("depth":"Tested","width":0}],"triangles":[],"points":[]}]})");
  CHECK(human.run("runtime overlay set " + file.string()) == 4);
  CHECK(human.runtime.viewportOverlayCalls == 3);
  CHECK_FALSE(human.runtime.viewportOverlay.has_value());

  // --with-overlay needs --capture; with it, the request carries the flag.
  CHECK(human.run("runtime step --with-overlay") == 2);
  CHECK(human.runtime.stepRequests.empty());
  CHECK(human.run("runtime step --capture --with-overlay") == 0);
  REQUIRE(human.runtime.stepRequests.size() == 1);
  CHECK(human.runtime.stepRequests[0].withOverlay);
  CHECK(human.run("runtime step --capture") == 0);
  CHECK_FALSE(human.runtime.stepRequests[1].withOverlay);

  Cli json(true, true);
  write(R"({"selection":[")" + point + R"("],"groups":[)" + group + "]}");
  CHECK(json.run("runtime overlay set " + file.string()) == 0);
  CHECK(json.out.str() ==
        envelope("runtime overlay set", R"({"groups":1,"selection":1,"segments":1,"triangles":0,"points":0})"));
  CHECK(json.run("runtime overlay select " + point) == 0);
  CHECK(json.out.str() == envelope("runtime overlay select", R"({"selection":[")" + point + R"("]})"));
  CHECK(json.run("runtime overlay clear") == 0);
  CHECK(json.out.str() == envelope("runtime overlay clear", R"({"overlay":null})"));
  write("{not json");
  CHECK(json.run("runtime overlay set " + file.string()) == 2);
  CHECK(json.err.str() ==
        R"({"code":"MalformedOverlay","category":"text","message":"not a Viewport overlay in its JSON form )"
        R"x((remote_protocol.md 6.6)","subject":{}})x"
        "\n");
  write(R"({"selection":["00000000-0000-0000-0000-000000000000"],"groups":[]})");
  CHECK(json.run("runtime overlay set " + file.string()) == 4);
  CHECK(json.err.str() ==
        R"({"code":"InvalidRequest","category":"runtime","message":"the Runtime refused the overlay: InvalidRequest",)"
        R"("subject":{}})"
        "\n");
  std::filesystem::remove(file);
}
