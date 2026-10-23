// Plan 0065 M5 (P6; Spec 0065 Q5, S-a; ADR-0130 D3): the CLI's `scene` group
// over an AuthoringConnection -- its cli/1 envelopes, exit codes and text,
// against a real authoring session (the fixture scene of the other CLI tests).

#include "cli_tree_fixture.h"

#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/authoring/authoring_session.h>
#include <atlantis/connection/json.h>

#include <fstream>
#include <iterator>

namespace {

using atlantis::cli::Invocation;
using atlantis::cli::InvocationOptions;
using atlantis::cli::test::Cli;
using atlantis::cli::test::kSceneSource;
namespace json = atlantis::connection::json;
namespace fs = std::filesystem;

constexpr std::string_view kNode1 = "52052052-0001-4052-8052-000000000001";
constexpr std::string_view kNode3 = "52052052-0003-4052-8052-000000000003";
constexpr std::string_view kNode4 = "52052052-0004-4052-8052-000000000004";

[[nodiscard]] fs::path writtenSource() {
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() / "atlantis_cli_scene_tests";
  fs::create_directories(dir);
  const fs::path path = dir / (std::to_string(std::random_device{}()) + "_" + std::to_string(counter++) + ".scene.txt");
  std::ofstream(path, std::ios::binary | std::ios::trunc) << kSceneSource;
  return path;
}

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The CLI with the `scene` group served by an authoring session; a frame that
// applies commands reports Applied after an accepted apply, as
// atlantis_runtime's host does (a paused frame holds the reload: Spec 0065
// third Correction).
struct SceneCli {
  explicit SceneCli(bool json = false, bool serve = true)
      : source(writtenSource()),
        session(atlantis::authoring::DocumentSource{
            atlantis::asset_system::parseAssetGuid(atlantis::cli::test::kScene).value(),
            atlantis::authoring::HandAuthored{source}}),
        authoring(session.connect()),
        cli(json) {
    session.setSceneSink([this](atlantis::asset_system::ValidatedSceneData) {
      reloadRequested = true;
      return true;
    });
    InvocationOptions options;
    options.json = json;
    options.outcomeTimeout = std::chrono::milliseconds(2000);
    options.awaitFrame = [this] {
      const bool applies = cli.runtime.appliesNextFrame();
      cli.runtime.frame();
      if (reloadRequested && applies) {
        reloadRequested = false;
        session.reportApplied(++generation);
      }
    };
    invocation = std::make_unique<Invocation>(*cli.connection, &cli.runtime, nullptr, cli.out, cli.err, options);
    if (serve) invocation->setAuthoring([this] { return authoring.get(); });
  }
  ~SceneCli() {
    invocation.reset();
    authoring.reset();
    std::error_code ec;
    fs::remove(source, ec);
  }
  SceneCli(const SceneCli&) = delete;
  SceneCli& operator=(const SceneCli&) = delete;

  int run(std::string_view line) {
    cli.out.str({});
    cli.err.str({});
    std::vector<std::string> args;
    std::istringstream words{std::string(line)};
    for (std::string word; words >> word;) args.push_back(word);
    return invocation->run(args);
  }
  [[nodiscard]] json::Value envelope() const {
    const auto parsed = json::parse(cli.out.str());
    REQUIRE(parsed.isOk());
    return parsed.value();
  }

  fs::path source;
  atlantis::authoring::AuthoringSession session;
  std::unique_ptr<atlantis::connection::AuthoringConnection> authoring;
  Cli cli;
  std::unique_ptr<Invocation> invocation;
  bool reloadRequested = false;
  std::uint64_t generation = 0;
};

[[nodiscard]] std::string member(const json::Value& value, std::string_view key) {
  const json::Value* found = value.find(key);
  REQUIRE(found != nullptr);
  return json::write(*found);
}

}  // namespace

TEST_CASE("cli scene: open, nodes, get and camera read the document", "[cli][scene]") {
  SceneCli scene(/*json=*/true);
  REQUIRE(scene.run("scene open") == 0);
  json::Value open = scene.envelope();
  CHECK(member(open, "atlantis") == "\"cli/1\"");
  CHECK(member(open, "command") == "\"scene open\"");
  CHECK(member(open, "ok") == "true");
  CHECK(member(*open.find("result"), "provenance") == "\"hand-authored\"");
  CHECK(member(*open.find("result"), "nodeCount") == "5");

  REQUIRE(scene.run("scene nodes") == 0);
  const json::Value listed = scene.envelope();
  const json::Value* nodes = listed.find("result")->find("nodes");
  REQUIRE(nodes != nullptr);
  REQUIRE(nodes->asArray().size() == 5);
  CHECK(member(nodes->asArray()[2], "parent") == "\"" + std::string(kNode1) + "\"");

  REQUIRE(scene.run("scene get " + std::string(kNode3) + " Light.intensity") == 0);
  CHECK(member(*scene.envelope().find("result"), "value") == "3");
  REQUIRE(scene.run("scene camera") == 0);
  CHECK(member(*scene.envelope().find("result"), "camera") == "\"" + std::string(kNode4) + "\"");
}

TEST_CASE("cli scene: writes report their events, refusals exit 1, a transaction commits whole", "[cli][scene]") {
  SceneCli scene;
  REQUIRE(scene.run("scene open") == 0);
  CHECK(scene.cli.out.str().starts_with("opened "));

  CHECK(scene.run("scene set " + std::string(kNode3) + " Light.intensity 7") == 0);
  CHECK(scene.cli.out.str() == "ok " + std::string(kNode3) + " Light.intensity = 7\n");
  CHECK(scene.session.document()->nodes[2].light->intensity == 7.0f);

  CHECK(scene.run("scene parent " + std::string(kNode1) + " " + std::string(kNode3)) == 1);
  CHECK(scene.cli.err.str() == "refused ParentAcyclic\n");

  CHECK(scene.run("scene create --component Camera") == 0);
  const std::string created = scene.cli.out.str();
  CHECK(created.starts_with("ok created "));
  CHECK(created.find(" at 5\nok added ") != std::string::npos);
  CHECK(scene.session.document()->nodes.size() == 6);

  CHECK(scene.run("scene camera none") == 0);
  CHECK(scene.cli.out.str() == "ok camera none\n");

  // scene tx: every line parsed first, then one transaction.
  const fs::path script = scene.source.parent_path() / (scene.source.filename().string() + ".tx");
  std::ofstream(script, std::ios::binary) << "# two edits\nset " << kNode3 << " Light.intensity 2\nscene camera "
                                          << kNode4 << "\n";
  CHECK(scene.run("scene tx " + script.string()) == 0);
  CHECK(scene.cli.out.str().ends_with("committed 2\n"));
  CHECK(scene.session.document()->activeCamera.has_value());
  std::ofstream(script, std::ios::binary | std::ios::trunc) << "set " << kNode3 << " Light.intensity 5\nparent "
                                                            << kNode1 << " " << kNode3 << "\n";
  CHECK(scene.run("scene tx " + script.string()) == 1);
  CHECK(scene.cli.err.str() == "refused ParentAcyclic at 0\n");  // a validity refusal names the first command
  CHECK(scene.session.document()->nodes[2].light->intensity == 2.0f);  // nothing of the refused one
  std::error_code ec;
  fs::remove(script, ec);
}

TEST_CASE("cli scene: apply waits for Applied; save writes the source", "[cli][scene]") {
  SceneCli scene(/*json=*/true);
  REQUIRE(scene.run("scene open") == 0);
  REQUIRE(scene.run("scene set " + std::string(kNode3) + " Light.intensity 9") == 0);
  REQUIRE(scene.run("scene apply") == 0);
  CHECK(member(*scene.envelope().find("result"), "generation") == "1");
  REQUIRE(scene.run("scene save") == 0);
  const auto saved = atlantis::asset_system::scene::toParsedSceneSource(*scene.session.document());
  REQUIRE(saved.isOk());
  CHECK(readText(scene.source) == atlantis::asset_system::serializeSceneSource(saved.value()));
}

TEST_CASE("cli scene: usage, no document and no authoring session", "[cli][scene]") {
  SceneCli scene(/*json=*/true);
  CHECK(scene.run("scene") == 2);
  CHECK(scene.run("scene nodes") == 1);  // before open
  CHECK(member(*scene.envelope().find("error"), "code") == "\"NoDocument\"");
  CHECK(scene.run("scene set nonsense Light.intensity 1") == 2);

  SceneCli unserved(/*json=*/true, /*serve=*/false);
  CHECK(unserved.run("scene open") == 4);
  CHECK(member(*unserved.envelope().find("error"), "code") == "\"NoAuthoring\"");
}

// Plan 0065 M5 (Spec 0065 third Correction): while the Runtime is paused,
// `scene apply` does not wait -- it returns its pending outcome at once, as a
// write under pause does -- and its Applied{generation} is reported, once,
// after the command that lets the reload happen (the step).
TEST_CASE("cli scene: apply while paused is pending at once; Applied follows the step", "[cli][scene][pause]") {
  SceneCli scene;
  REQUIRE(scene.run("scene open") == 0);
  REQUIRE(scene.run("scene set " + std::string(kNode3) + " Light.intensity 9") == 0);
  REQUIRE(scene.run("runtime pause") == 0);
  CHECK(scene.run("scene apply") == 0);
  CHECK(scene.cli.out.str() == "pending apply (the Runtime is paused; it reloads at the next step)\n");
  CHECK(scene.reloadRequested);  // accepted by the sink, waiting
  CHECK(scene.generation == 0);

  CHECK(scene.run("scene get " + std::string(kNode3) + " Light.intensity") == 0);
  CHECK(scene.cli.out.str() == "9\n");  // other commands run; nothing applied yet

  CHECK(scene.run("runtime step") == 0);
  CHECK(scene.generation == 1);
  CHECK(scene.cli.out.str().ends_with("applied generation 1\n"));
  CHECK(scene.run("scene camera") == 0);
  CHECK(scene.cli.out.str().find("applied") == std::string::npos);  // reported once

  SceneCli machine(/*json=*/true);
  REQUIRE(machine.run("scene open") == 0);
  REQUIRE(machine.run("runtime pause") == 0);
  REQUIRE(machine.run("scene apply") == 0);
  const json::Value pending = machine.envelope();
  CHECK(member(pending, "command") == "\"scene apply\"");
  CHECK(member(*pending.find("result"), "outcome") == R"({"pending":true})");
  REQUIRE(machine.run("runtime step") == 0);
  const std::string lines = machine.cli.out.str();
  const auto second = lines.find('\n') + 1;
  REQUIRE(second < lines.size());
  const auto applied = json::parse(lines.substr(second));
  REQUIRE(applied.isOk());
  CHECK(member(applied.value(), "command") == "\"scene apply\"");
  CHECK(member(*applied.value().find("result"), "generation") == "1");
}
