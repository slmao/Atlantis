#include <atlantis/asset_system/validated_scene_data.h>

#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using atlantis::asset_system::ValidatedSceneData;

// V11: ValidatedSceneData has no public default constructor of any
// kind (ADR-0053's own Human Review Correction, 2026-08-23) -- a
// type-level guarantee, not a documented convention. This is the
// compile-time half of V11; the runtime half (no external code can
// name the private non-default constructor, mutate a field, or obtain
// a mutable reference) is demonstrated below as documented,
// intentionally-uncompilable examples -- matching EntityId's own V27
// precedent -- rather than built, since a real negative-compilation
// test harness is out of this repository's own established scope.
static_assert(!std::is_default_constructible_v<ValidatedSceneData>);
static_assert(std::is_copy_constructible_v<ValidatedSceneData>);
static_assert(std::is_move_constructible_v<ValidatedSceneData>);
static_assert(std::is_copy_assignable_v<ValidatedSceneData>);
static_assert(std::is_move_assignable_v<ValidatedSceneData>);

// V11: every accessor returns a plain value or a const reference --
// never a mutable one -- confirmed at compile time, matching
// runtime_ownership_tests.cpp's own established static_assert pattern.
static_assert(std::is_same_v<decltype(std::declval<const ValidatedSceneData&>().nodeCount()), std::size_t>);
static_assert(!std::is_reference_v<decltype(std::declval<const ValidatedSceneData&>().nodeCount())>);
static_assert(std::is_const_v<
               std::remove_reference_t<decltype(std::declval<const ValidatedSceneData&>().node(0))>>);

// Uncompilable-if-uncommented documented examples, matching EntityId's
// own V27 precedent exactly -- this file does not, and must not,
// contain a build target that actually compiles any of these:
//
//   ValidatedSceneData empty;                       // no default constructor
//   ValidatedSceneData bogus({}, {}, std::nullopt);  // the three-argument
//                                                     // constructor is private
//   someValidatedSceneData.node(0) = {};             // node() returns const&
//
TEST_CASE("ValidatedSceneData's own default-constructibility is rejected at compile time", "[asset_system][scene]") {
  // The static_asserts above already prove this at compile time; this
  // TEST_CASE exists so ctest reports at least one runtime case for
  // this file rather than a build-only, invisible-to-ctest check.
  SUCCEED("static_assert(!std::is_default_constructible_v<ValidatedSceneData>) above already verified this");
}

namespace {

std::atomic<int> gScratchCounter{0};

// Deferred from Step 1 (this file's own prior commit): copy/move
// preserving every accessor's own observed value, verified against a
// real decodeScene() result -- there is no other way to obtain a
// ValidatedSceneData instance at all, by this same design, so no such
// test could exist before decodeScene() (Step 4) was written.
[[nodiscard]] atlantis::asset_system::ValidatedSceneData makeDecodedOneNodeScene() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "atlantis_validated_scene_data_tests" /
                        std::to_string(gScratchCounter.fetch_add(1));
  fs::create_directories(dir);
  const fs::path sourcePath = dir / "scene.scene.txt";
  const fs::path artifactPath = dir / "scene.ascene";
  const fs::path metadataPath = dir / "scene.ascene.meta.txt";
  {
    std::ofstream out(sourcePath, std::ios::binary | std::ios::trunc);
    out << "atlantis_scene_source_version: 3\n"
           "node_count: 1\n"
           "active_camera: none\n"
           "node: node_id=1 parent=none position=1.0 2.0 3.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";
  }
  auto cookResult = atlantis::asset_system::cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string());
  REQUIRE(cookResult.isOk());
  auto decodeResult = atlantis::asset_system::decodeScene(artifactPath.string(), metadataPath.string());
  REQUIRE(decodeResult.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decodeResult.value();
}

}  // namespace

TEST_CASE("ValidatedSceneData's copy constructor preserves every accessor's own observed value",
          "[asset_system][scene]") {
  const ValidatedSceneData original = makeDecodedOneNodeScene();
  const ValidatedSceneData copy(original);  // NOLINT(performance-unnecessary-copy-initialization)
  CHECK(copy.nodeCount() == original.nodeCount());
  CHECK(copy.node(0).transform.positionX == original.node(0).transform.positionX);
  CHECK(copy.parentOf(0) == original.parentOf(0));
  CHECK(copy.activeCameraIndex() == original.activeCameraIndex());
}

TEST_CASE("ValidatedSceneData's move constructor preserves every accessor's own observed value",
          "[asset_system][scene]") {
  ValidatedSceneData original = makeDecodedOneNodeScene();
  const std::size_t expectedNodeCount = original.nodeCount();
  const float expectedPositionX = original.node(0).transform.positionX;
  const std::optional<std::size_t> expectedParent = original.parentOf(0);
  const std::optional<std::size_t> expectedActiveCamera = original.activeCameraIndex();

  const ValidatedSceneData moved(std::move(original));
  CHECK(moved.nodeCount() == expectedNodeCount);
  CHECK(moved.node(0).transform.positionX == expectedPositionX);
  CHECK(moved.parentOf(0) == expectedParent);
  CHECK(moved.activeCameraIndex() == expectedActiveCamera);
}

TEST_CASE("ValidatedSceneData's copy assignment preserves every accessor's own observed value",
          "[asset_system][scene]") {
  const ValidatedSceneData original = makeDecodedOneNodeScene();
  ValidatedSceneData other = makeDecodedOneNodeScene();
  other = original;
  CHECK(other.node(0).transform.positionX == original.node(0).transform.positionX);
}

TEST_CASE("ValidatedSceneData's move assignment preserves every accessor's own observed value",
          "[asset_system][scene]") {
  ValidatedSceneData original = makeDecodedOneNodeScene();
  const float expectedPositionX = original.node(0).transform.positionX;
  ValidatedSceneData other = makeDecodedOneNodeScene();
  other = std::move(original);
  CHECK(other.node(0).transform.positionX == expectedPositionX);
}
