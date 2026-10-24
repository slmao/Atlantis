#include "authoring_fixture.h"

#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/scene_metadata.h>
#include <atlantis/authoring/document_validation.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

// Plan 0065 M2 / J9 (and the J9 note of 2026-10-22): the cost of one
// single-property transaction on Bistro's generated document, validated by
// the in-memory cook dry run (J2). Reported, not a verdict: hidden ([.j9]),
// so ctest does not run it; run it by hand on a Release build, Bistro
// imported:
//   atlantis_authoring_tests "[.j9]"
// It prints the transaction's median and maximum over 100 transactions, and
// the same edit's stages timed one by one (100 each): the document copy,
// checkSchema(), map and serialize, parse, cook (cookSceneSource() less its
// parse) and the round-trip mapping and comparison.

using namespace authoring_test;

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double ms(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Stats {
  double median = 0.0;
  double max = 0.0;
};

[[nodiscard]] Stats statsOf(std::vector<double> samples) {
  std::sort(samples.begin(), samples.end());
  return {samples[samples.size() / 2], samples.back()};
}

}  // namespace

#if defined(ATLANTIS_BISTRO_SCENE_IMPORT_DIR)
TEST_CASE("J9: one single-property transaction on Bistro's generated document", "[.j9][authoring][content]") {
  const fs::path source = fs::path{ATLANTIS_BISTRO_SCENE_IMPORT_DIR} / "bistro" / "bistro.scene.txt";
  const fs::path sidecar{std::string{ATLANTIS_BISTRO_SCENE_ARTIFACT_PATH} + ".meta.txt"};
  if (!fs::exists(source) || !fs::exists(sidecar)) SKIP("Bistro content not imported");
  const auto metadata = as::parseSceneMetadata(readText(sidecar));
  REQUIRE(metadata.isOk());
  const as::AssetGuid sceneAsset = metadata.value().assetGuid;

  AuthoringSession session(DocumentSource{sceneAsset, Imported{source}});
  auto connection = session.connect();
  REQUIRE(connection->open().isOk());
  const scene::AuthoringScene& document = *session.document();
  const auto light = std::find_if(document.nodes.begin(), document.nodes.end(), [](const scene::AuthoringNode& n) {
    return n.light.has_value() && n.light->kind == scene::LightKind::Point;
  });
  REQUIRE(light != document.nodes.end());
  const ac::PropertyAddress intensity{light->guid, kLight, field("Light", "intensity")};
  const std::size_t lightIndex = static_cast<std::size_t>(light - document.nodes.begin());

  constexpr int kRuns = 100;
  std::vector<double> transaction;
  for (int i = 0; i < kRuns; ++i) {
    const float value = 1.0f + 0.25f * static_cast<float>(i % 8);
    const auto t0 = Clock::now();
    connection->submit(ac::SetProperty{intensity, value});
    const auto t1 = Clock::now();
    REQUIRE(connection->drainFailures().empty());
    transaction.push_back(ms(t0, t1));
  }

  std::vector<double> copy, schemaCheck, serialize, parse, cook, roundTrip;
  for (int i = 0; i < kRuns; ++i) {
    const auto t0 = Clock::now();
    scene::AuthoringScene candidate = *session.document();
    candidate.nodes[lightIndex].light->intensity = 2.0f + 0.25f * static_cast<float>(i % 8);
    const auto t1 = Clock::now();
    REQUIRE(atlantis::authoring::checkSchema(candidate) == std::nullopt);
    const auto t2 = Clock::now();
    const auto mapped = scene::toParsedSceneSource(candidate);
    const std::string text = as::serializeSceneSource(mapped.value());
    const auto t3 = Clock::now();
    const auto parsed = as::parseSceneSource(text);
    const auto t4 = Clock::now();
    const auto cooked = as::cookSceneSource(text, sceneAsset);  // parses again
    const auto t5 = Clock::now();
    const auto back = scene::toAuthoringScene(parsed.value());
    const bool same = back.isOk() && back.value() == candidate;
    const auto t6 = Clock::now();
    REQUIRE(cooked.isOk());
    REQUIRE(same);
    copy.push_back(ms(t0, t1));
    schemaCheck.push_back(ms(t1, t2));
    serialize.push_back(ms(t2, t3));
    parse.push_back(ms(t3, t4));
    cook.push_back(ms(t4, t5) - ms(t3, t4));
    roundTrip.push_back(ms(t5, t6));
  }

  const Stats tx = statsOf(transaction);
  std::printf("J9 Bistro: %zu nodes; single-property transaction over %d runs: median %.2f ms, max %.2f ms "
              "(target < 10 ms median)\n",
              document.nodes.size(), kRuns, tx.median, tx.max);
  std::printf("J9 stages (median of %d): copy %.2f, checkSchema %.2f, map+serialize %.2f, parse %.2f, "
              "cook %.2f, round trip %.2f ms\n",
              kRuns, statsOf(copy).median, statsOf(schemaCheck).median, statsOf(serialize).median,
              statsOf(parse).median, statsOf(cook).median, statsOf(roundTrip).median);
}
#endif
