#include <atlantis/runtime/init_error.h>

#include <cstddef>
#include <cstring>
#include <iterator>

#include <catch2/catch_test_macros.hpp>

// Plan 0014 Step 4: this file did not previously exist -- corrected here
// rather than "extended," per this Plan's own Files/Modules Touched list
// (a mechanical drafting-time assumption, not an architectural change;
// RuntimeInitError's own toString() was previously untested by any file
// in this directory).

using atlantis::runtime::RuntimeInitError;
using atlantis::runtime::toString;

TEST_CASE("toString(RuntimeInitError) returns a distinct, non-empty string for every enumerator",
          "[runtime][init_error]") {
  constexpr RuntimeInitError kAllValues[] = {
      RuntimeInitError::PlatformInitFailed,          RuntimeInitError::ShaderLoadFailed,
      RuntimeInitError::DeviceCreateFailed,           RuntimeInitError::AssetLoadFailed,
      RuntimeInitError::MeshCreateFailed,             RuntimeInitError::CameraBufferCreateFailed,
      RuntimeInitError::AssetMetadataParseFailed,     RuntimeInitError::SceneConstructionFailed,
      // Plan 0015 Section D2.
      RuntimeInitError::SceneManifestLoadFailed,      RuntimeInitError::SceneArtifactLoadFailed,
      RuntimeInitError::SceneDependencyUnresolved,    RuntimeInitError::SceneDependencyLoadFailed,
  };

  for (std::size_t i = 0; i < std::size(kAllValues); ++i) {
    const char* text = toString(kAllValues[i]);
    REQUIRE(text != nullptr);
    REQUIRE(std::strlen(text) > 0);
    for (std::size_t j = i + 1; j < std::size(kAllValues); ++j) {
      REQUIRE(std::strcmp(text, toString(kAllValues[j])) != 0);
    }
  }
}

TEST_CASE("toString(RuntimeInitError::AssetMetadataParseFailed) and SceneConstructionFailed name themselves",
          "[runtime][init_error]") {
  REQUIRE(std::strcmp(toString(RuntimeInitError::AssetMetadataParseFailed), "AssetMetadataParseFailed") == 0);
  REQUIRE(std::strcmp(toString(RuntimeInitError::SceneConstructionFailed), "SceneConstructionFailed") == 0);
}
