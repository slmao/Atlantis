#include "authoring_fixture.h"

#include <atlantis/authoring/document_validation.h>

#include <array>
#include <random>

// Plan 0065 M2 (Spec 0065 R5, R9): after any sequence of commands -- accepted
// or refused -- the document is valid, a refused transaction left it as it
// was, and the document equals the re-parse of its saved text. A seeded
// random walk over every command kind, values in and out of their domains.

using namespace authoring_test;

namespace {

class Walk {
 public:
  explicit Walk(std::uint32_t seed) : random_(seed) {}

  [[nodiscard]] std::vector<ac::Command> next(const scene::AuthoringScene& document) {
    std::vector<ac::Command> commands;
    const int count = pick(1, 3);
    for (int i = 0; i < count; ++i) commands.push_back(command(document));
    return commands;
  }

 private:
  [[nodiscard]] int pick(int low, int high) { return std::uniform_int_distribution<int>(low, high)(random_); }
  [[nodiscard]] float value() {
    static constexpr std::array<float, 10> kValues{0.0f, -0.0f, 0.5f, 1.0f, 2.0f, -1.0f, 0.1234567f, 1e-7f, 3.5f, 64.0f};
    return kValues[static_cast<std::size_t>(pick(0, static_cast<int>(kValues.size()) - 1))];
  }
  [[nodiscard]] as::EntityGuid someNode(const scene::AuthoringScene& document) {
    if (document.nodes.empty() || pick(0, 9) == 0) return entity(static_cast<std::uint32_t>(pick(1, 40)));
    return document.nodes[static_cast<std::size_t>(pick(0, static_cast<int>(document.nodes.size()) - 1))].guid;
  }
  [[nodiscard]] TypeId someComponent() {
    static const std::array<TypeId, 4> kTypes{kTransform, kCamera, kRenderable, kLight};
    return kTypes[static_cast<std::size_t>(pick(0, 3))];
  }

  [[nodiscard]] ac::Command command(const scene::AuthoringScene& document) {
    switch (pick(0, 9)) {
      case 0:
        return ac::CreateNode{entity(static_cast<std::uint32_t>(pick(20, 40))),
                              static_cast<std::uint64_t>(pick(0, static_cast<int>(document.nodes.size()))),
                              pick(0, 1) == 0 ? std::vector<TypeId>{} : std::vector<TypeId>{someComponent()}};
      case 1:
        return ac::DestroyNode{someNode(document)};
      case 2:
        return ac::AddComponent{someNode(document), someComponent()};
      case 3:
        return ac::RemoveComponent{someNode(document), someComponent()};
      case 4:
        return ac::SetParent{someNode(document), pick(0, 2) == 0 ? std::nullopt : std::optional(someNode(document))};
      case 5:
        return ac::SetActiveCamera{pick(0, 3) == 0 ? std::nullopt : std::optional(someNode(document))};
      case 6:
        return ac::SetProperty{ac::PropertyAddress{someNode(document), kTransform, field("Transform", "localPosition")},
                               std::array<float, 3>{value(), value(), value()}};
      case 7:
        return ac::SetProperty{ac::PropertyAddress{someNode(document), kCamera, field("CameraFog", "density")}, value()};
      case 8:
        return ac::SetProperty{ac::PropertyAddress{someNode(document), kLight, field("Light", "range")}, value()};
      default:
        return ac::SetProperty{ac::PropertyAddress{someNode(document), kLight, field("Light", "kind")},
                               wa::EnumValue{pick(0, 1)}};
    }
  }

  std::mt19937 random_;
};

}  // namespace

TEST_CASE("authoring round trip: after any command sequence the document is valid and equals its saved text",
          "[authoring][round_trip]") {
  for (const std::uint32_t seed : {1u, 2u, 3u, 65u}) {
    INFO("seed " << seed);
    Fixture f;
    Walk walk(seed);
    std::size_t accepted = 0;
    for (int step = 0; step < 150; ++step) {
      const scene::AuthoringScene before = f.document();
      const auto refusal = refusalOf(*f.connection, walk.next(before));
      if (refusal.has_value()) {
        CHECK(f.document() == before);
        continue;
      }
      ++accepted;
      CHECK(atlantis::authoring::validateDocument(f.document(), sceneGuid()) == std::nullopt);
      REQUIRE(f.connection->save().isOk());
      CHECK(documentOf(readText(f.source)) == f.document());
    }
    CHECK(accepted > 10);  // the walk is not vacuous

    // Reopening reads back exactly the saved document.
    const scene::AuthoringScene saved = f.document();
    REQUIRE(f.connection->open().isOk());
    CHECK(f.document() == saved);
  }
}
