#include "cli_tree_fixture.h"
#include <atlantis/cli/script_runner.h>

namespace {
namespace ca = atlantis::connection::assets;
class Assets final : public atlantis::connection::AssetConnection {
 public:
  const atlantis::asset_system::AssetGuid guid = atlantis::asset_system::parseAssetGuid("00810081-0081-4081-8081-008100810081").value();
  bool unavailable = false;
  atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError> listAssets() const override {
    using R = atlantis::Result<std::vector<ca::AssetSummary>, ca::AssetError>;
    return unavailable ? R::Err(ca::AssetError::CatalogUnavailable) : R::Ok({{guid, ca::AssetType::Mesh, "assets:mesh.txt"}});
  }
  atlantis::Result<ca::AssetDetails, ca::AssetError> getAsset(const atlantis::asset_system::AssetGuid& id) const override {
    using R = atlantis::Result<ca::AssetDetails, ca::AssetError>;
    return id == guid ? R::Ok({guid, ca::AssetType::Mesh, "assets:mesh.txt", 7, std::nullopt, "test/1", {}})
                      : R::Err(ca::AssetError::UnknownAsset);
  }
};
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
