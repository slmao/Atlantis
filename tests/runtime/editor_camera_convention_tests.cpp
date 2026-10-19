// Plan 0062 M1 (Spec 0062 R3; ADR-0123 D1): the editor computes its own view
// and projection for its overlay and pick rays, copying the host's camera
// convention. This holds the copy to the host's extractCameraMatrices() for a
// sweep of poses, lenses and aspects, and the editor camera's pose to what the
// host reads back from it. A change to either side fails here.

#include <atlantis/editor/model/viewport_camera.h>
#include <atlantis/runtime/scene_extraction.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace {

using atlantis::editor::CameraLens;
using atlantis::editor::CameraPose;
using atlantis::editor::ViewportCameraModel;

constexpr float kTolerance = 1e-6f;

[[nodiscard]] bool near(const std::array<float, 16>& a, const std::array<float, 16>& b) {
  for (std::size_t i = 0; i < 16; ++i) {
    if (std::fabs(a[i] - b[i]) > kTolerance) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("the editor's camera matrices equal the host's extractCameraMatrices()", "[runtime][editor][camera]") {
  constexpr std::array<float, 5> kYaws{0.0f, 0.7f, -1.9f, 3.0f, -3.1f};
  constexpr std::array<float, 5> kPitches{0.0f, 0.4f, -0.8f, 1.5533430f, -1.5533430f};  // up to +-89 degrees
  constexpr std::array<CameraLens, 3> kLenses{CameraLens{1.0472f, 0.1f, 100.0f}, CameraLens{0.5f, 0.01f, 5000.0f},
                                              CameraLens{1.6f, 1.0f, 20.0f}};
  constexpr std::array<float, 3> kAspects{1.0f, 16.0f / 9.0f, 0.4f};
  constexpr std::array<std::array<float, 3>, 3> kEyes{std::array<float, 3>{0.0f, 0.0f, 0.0f},
                                                     std::array<float, 3>{0.0f, 2.2f, 7.0f},
                                                     std::array<float, 3>{-0.75f, 0.5f, 0.25f}};
  std::size_t compared = 0;
  for (const float yaw : kYaws) {
    for (const float pitch : kPitches) {
      for (const auto& eye : kEyes) {
        // An editor camera at this direction and eye, then its pose.
        ViewportCameraModel model;
        CameraPose seed;
        const float c = std::cos(pitch);
        const std::array<float, 3> forward{-std::sin(yaw) * c, std::sin(pitch), -std::cos(yaw) * c};
        seed.worldMatrix[8] = -forward[0];
        seed.worldMatrix[9] = -forward[1];
        seed.worldMatrix[10] = -forward[2];
        seed.worldMatrix[12] = eye[0];
        seed.worldMatrix[13] = eye[1];
        seed.worldMatrix[14] = eye[2];
        for (const CameraLens& lens : kLenses) {
          seed.lens = lens;
          model.reset(seed);
          const CameraPose pose = model.pose();
          for (const float aspect : kAspects) {
            const auto host = atlantis::runtime::extractCameraMatrices(pose.worldMatrix, lens.fovYRadians, lens.nearZ,
                                                                       lens.farZ, aspect);
            const std::optional<atlantis::editor::CameraMatrices> editor = atlantis::editor::cameraMatrices(pose, aspect);
            REQUIRE(host.isOk());
            REQUIRE(editor.has_value());
            CHECK(near(editor->view, host.value().view));
            CHECK(near(editor->projection, host.value().projection));
            ++compared;
          }
        }
      }
    }
  }
  CHECK(compared == kYaws.size() * kPitches.size() * kEyes.size() * kLenses.size() * kAspects.size());
}

TEST_CASE("the editor's camera matrices fail exactly where the host's extraction does", "[runtime][editor][camera]") {
  CameraPose straightUp;
  straightUp.worldMatrix = {1, 0, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, 0, 1};  // forward = +y
  CHECK_FALSE(atlantis::editor::cameraMatrices(straightUp, 1.0f).has_value());
  CHECK(atlantis::runtime::extractCameraMatrices(straightUp.worldMatrix, 1.0f, 0.1f, 10.0f, 1.0f).isErr());

  CameraPose noForward;
  noForward.worldMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  CHECK_FALSE(atlantis::editor::cameraMatrices(noForward, 1.0f).has_value());
  CHECK(atlantis::runtime::extractCameraMatrices(noForward.worldMatrix, 1.0f, 0.1f, 10.0f, 1.0f).isErr());
}

TEST_CASE("an editor camera reset from a scene camera's world matrix reproduces its eye and forward",
          "[runtime][editor][camera]") {
  // The default scene's camera shape: an eye, a downward tilt, a column 2 that
  // is not unit length (a scaled node).
  CameraPose scene;
  const float tilt = -0.3054f;
  scene.worldMatrix = {1, 0, 0, 0,
                       0, std::cos(tilt), std::sin(tilt), 0,
                       0, -2.0f * std::sin(tilt), 2.0f * std::cos(tilt), 0,
                       0.0f, 2.2f, 7.0f, 1};
  scene.lens = CameraLens{1.0472f, 0.1f, 100.0f};
  ViewportCameraModel model;
  model.reset(scene);
  const auto host = atlantis::runtime::extractCameraMatrices(scene.worldMatrix, 1.0472f, 0.1f, 100.0f, 1.5f);
  const auto editor = model.matrices(1.5f);
  REQUIRE(host.isOk());
  REQUIRE(editor.has_value());
  CHECK(near(editor->view, host.value().view));
  CHECK(near(editor->projection, host.value().projection));
  CHECK(model.lens() == scene.lens);
}
