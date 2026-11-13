#include "cli_tree_fixture.h"
#include <atlantis/cli/script_runner.h>

namespace {
namespace ca = atlantis::connection::assets;
class Assets final : public atlantis::connection::AssetConnection {
 public:
  const atlantis::asset_system::AssetGuid guid = atlantis::asset_system::parseAssetGuid("00810081-0081-4081-8081-008100810081").value();
  const atlantis::asset_system::AssetGuid material = atlantis::asset_system::parseAssetGuid("00810081-0081-4081-8081-008100810082").value();
  bool unavailable = false;
  int previews = 0;
  int pairPreviews = 0;
  ca::MaterialStatusResult materialStatus(std::optional<ca::MaterialJobId> id) const override {
    if (!id || id->value != guid) return ca::MaterialStatusResult::Err(ca::MaterialError::UnknownJob);
    ca::MaterialJob job{*id, {ca::MaterialMode::Edit, {}, material}, ca::RebuildState::Failed, {}, {},
        ca::MaterialFailure{ca::MaterialStage::Publication, ca::MaterialFailureCode::PublishFailed,
            "source committed; publication failed", {}, ca::RegistrationState::Committed, material}};
    return ca::MaterialStatusResult::Ok({{}, job, false});
  }
  atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError> listAssets() const override {
    using R = atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError>;
    return unavailable ? R::Err(ca::AssetError::CatalogUnavailable) : R::Ok({{guid, ca::AssetType::Mesh, "assets:mesh.txt"}});
  }
  atlantis::Result<ca::AssetDetails, ca::AssetError> getAsset(const atlantis::asset_system::AssetGuid& id) const override {
    using R = atlantis::Result<ca::AssetDetails, ca::AssetError>;
    return id == guid ? R::Ok({guid, ca::AssetType::Mesh, "assets:mesh.txt", 7, std::nullopt, "test/1", {}})
                      : R::Err(ca::AssetError::UnknownAsset);
  }
  ca::MeshPreviewResult previewMesh(const atlantis::asset_system::AssetGuid& id) override {
    ++previews;
    if (id != guid) return ca::MeshPreviewResult::Err(ca::PreviewError::UnknownAsset);
    if (unavailable) return ca::MeshPreviewResult::Err(ca::PreviewError::Busy);
    return ca::MeshPreviewResult::Ok({guid, 256, 256,
        {137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',0,0,1,0,0,0,1,0,8,6,0,0,0,0,0,0,0}});
  }
  ca::MeshMaterialPreviewResult previewMeshMaterial(const atlantis::asset_system::AssetGuid& mesh,
                                                     const atlantis::asset_system::AssetGuid& chosen) override {
    using R = ca::MeshMaterialPreviewResult;
    using E = ca::MeshMaterialPreviewError;
    ++pairPreviews;
    if (mesh != guid) return R::Err(E::UnknownMesh);
    if (chosen != material) return R::Err(E::UnknownMaterial);
    if (unavailable) return R::Err(E::DependencyUnavailable);
    return R::Ok({mesh, chosen, 256, 256,
        {137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',0,0,1,0,0,0,1,0,8,6,0,0,0,0,0,0,0}});
  }
};
}

TEST_CASE("asset preview CLI: structured PNG and human/exec metadata share one operation", "[cli][assets][asset-preview]") {
  using namespace atlantis::cli::test;
  namespace json = atlantis::connection::json;
  constexpr const char* command = "asset preview 00810081-0081-4081-8081-008100810081";
  Cli cli(true);
  REQUIRE(cli.run(command) == 1);
  CHECK(cli.out.str().find("Unsupported") != std::string::npos);
  Assets assets;
  cli.invocation->setAssets([&] { return &assets; });
  CHECK(cli.run("asset preview 00000000-0000-0000-0000-000000000000") == 2);
  CHECK(cli.run("asset preview 00810081-0081-4081-8081-008100810081 extra") == 2);
  CHECK(assets.previews == 0);
  REQUIRE(cli.run(command) == 0);
  const auto parsed = json::parse(cli.out.str());
  REQUIRE(parsed.isOk());
  const auto* result = parsed.value().find("result");
  REQUIRE(result);
  const auto decoded = ca::decodeMeshPreview(*result, assets.guid);
  REQUIRE(decoded);
  CHECK(decoded->png.size() == 33);
  CHECK(result->find("mimeType")->asString() == "image/png");

  Cli human;
  human.invocation->setAssets([&] { return &assets; });
  REQUIRE(human.run(command) == 0);
  const auto summary = std::string(command) + " 256x256 image/png (33 bytes)\n";
  CHECK(human.out.str() == summary);
  human.out.str("");
  atlantis::cli::ScriptRunner script(*human.connection, {command}, human.out);
  script.setAssets([&] { return &assets; });
  script.step();
  CHECK(human.out.str() == "> " + std::string(command) + "\n" + summary);
  assets.unavailable = true;
  CHECK(cli.run(command) == 1);
  CHECK(cli.out.str().find("Busy") != std::string::npos);
  CHECK(cli.out.str().find("pngBase64") == std::string::npos);
  CHECK(assets.previews == 4);
}

TEST_CASE("asset pair preview CLI: structured bytes and actionable human/exec output", "[cli][assets][asset-preview]") {
  using namespace atlantis::cli::test;
  namespace json = atlantis::connection::json;
  const std::string mesh = "00810081-0081-4081-8081-008100810081";
  const std::string material = "00810081-0081-4081-8081-008100810082";
  const std::string command = "asset preview-material " + mesh + " " + material;
  Cli cli(true);
  REQUIRE(cli.run(command) == 1);
  CHECK(cli.out.str().find("Unsupported") != std::string::npos);
  Assets assets;
  cli.invocation->setAssets([&] { return &assets; });
  CHECK(cli.run("asset preview-material " + mesh) == 2);
  CHECK(cli.run("asset preview-material " + mesh + " 00000000-0000-0000-0000-000000000000") == 2);
  CHECK(cli.run(command + " extra") == 2);
  CHECK(assets.pairPreviews == 0);
  REQUIRE(cli.run(command) == 0);
  const auto parsed = json::parse(cli.out.str());
  REQUIRE(parsed.isOk());
  const auto* result = parsed.value().find("result");
  REQUIRE(result);
  const auto decoded = ca::decodeMeshMaterialPreview(*result, assets.guid, assets.material);
  REQUIRE(decoded);
  CHECK(decoded->png.size() == 33);
  CHECK(result->find("mimeType")->asString() == "image/png");

  Cli human;
  human.invocation->setAssets([&] { return &assets; });
  REQUIRE(human.run(command) == 0);
  const auto summary = command + " 256x256 image/png (33 bytes)\n";
  CHECK(human.out.str() == summary);
  human.out.str("");
  atlantis::cli::ScriptRunner script(*human.connection, {command}, human.out);
  script.setAssets([&] { return &assets; });
  script.step();
  CHECK(human.out.str() == "> " + command + "\n" + summary);

  assets.unavailable = true;
  CHECK(human.run(command) == 1);
  CHECK(human.err.str().find("Repair the cooked base-color texture dependency.") != std::string::npos);
  CHECK(human.out.str().find("image/png") == std::string::npos);
  CHECK(assets.pairPreviews == 4);
  CHECK(assets.previews == 0);
}

TEST_CASE("asset CLI: human JSON REPL and exec use injected shared queries", "[cli][assets]") {
  using namespace atlantis::cli::test;
  Assets assets; Cli cli(true); cli.invocation->setAssets([&] { return &assets; });
  REQUIRE(cli.run("asset list") == 0);
  auto list = atlantis::connection::json::parse(cli.out.str()); REQUIRE(list.isOk());
  REQUIRE(list.value().find("result")->isArray());
  CHECK(list.value().find("result")->asArray()[0].find("source")->asString() == "assets:mesh.txt");
  REQUIRE(cli.run("asset get 00810081-0081-4081-8081-008100810081") == 0);
  auto detail = atlantis::connection::json::parse(cli.out.str()); REQUIRE(detail.isOk());
  CHECK(detail.value().find("result")->find("sourceSchema")->isNull());
  CHECK(cli.run("asset get 00810081-0081-4081-8081-008100810082") == 1);
  CHECK(cli.run("asset get 00000000-0000-0000-0000-000000000000") == 2);
  assets.unavailable = true; CHECK(cli.run("asset list") == 1); assets.unavailable = false;
  std::istringstream repl("asset list\nquit\n"); CHECK(cli.invocation->repl(repl) == 0);
  Cli human; human.invocation->setAssets([&] { return &assets; });
  CHECK(human.run("asset list") == 0);
  CHECK(human.out.str() == "00810081-0081-4081-8081-008100810081 mesh assets:mesh.txt\n");
  atlantis::cli::ScriptRunner script(*human.connection, {"asset list"}, human.out);
  script.setAssets([&] { return &assets; }); script.step();
  CHECK(human.out.str().find("mesh assets:mesh.txt") != std::string::npos);
  atlantis::cli::InvocationOptions options;
  options.json = true;
  options.transportFailure = []() -> std::optional<atlantis::cli::CliError> {
    return atlantis::cli::CliError{"Disconnected", atlantis::cli::ErrorCategory::Connection, "lost connection", {}, "lost connection\n"};
  };
  atlantis::cli::Invocation failed(*cli.connection, nullptr, nullptr, cli.out, cli.err, options);
  failed.setAssets([&] { return &assets; });
  assets.unavailable = true;
  const std::vector<std::string> args{"asset", "list"};
  CHECK(failed.run(args) == 3);  // Transport failure overrides the catalog sentinel.
}

TEST_CASE("material CLI: terminal failure exits nonzero and retains the exact receipt", "[cli][assets][asset-material]") {
  using namespace atlantis::cli::test;
  Assets assets;
  Cli cli(true);
  cli.invocation->setAssets([&] { return &assets; });
  REQUIRE(cli.run("assets material-status " + atlantis::asset_system::toString(assets.guid)) == 1);
  auto output = atlantis::connection::json::parse(cli.out.str());
  REQUIRE(output.isOk());
  const auto* payload = output.value().find("result");
  REQUIRE(payload);
  auto status = ca::decodeMaterialStatus(*payload);
  REQUIRE(status);
  REQUIRE(status->job);
  REQUIRE(status->job->failure);
  CHECK(status->job->failure->sourceState == ca::RegistrationState::Committed);
  CHECK(status->job->failure->material == assets.material);
}
