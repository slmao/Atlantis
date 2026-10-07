#include "cli_tree_fixture.h"

#include <atlantis/connection/json.h>
#include <atlantis/schema.h>

#include <catch2/catch_test_macros.hpp>

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
