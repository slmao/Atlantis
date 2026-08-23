#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/world/world.h>

#include <unordered_map>

namespace atlantis::runtime {

struct SceneLoadOutcome {
  atlantis::world::World world;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
};

// Plan 0015 Section D10, steps (a)-(g) -- factored out of
// RuntimeApplication::initializeSteps() into its own free function,
// matching scene_extraction.h's own already-established precedent
// ("Factored out of runtime_application.cpp's own anonymous namespace
// so this pure ... logic is unit-testable"). Here specifically so
// V20's own manifest-load, scene-decode, and dependency-unresolved
// failure paths are directly testable without a real Platform session
// or GPU Device: each of those three conditions is detected at steps
// (a)/(b)/(d), strictly before device is ever dereferenced (step (e),
// mesh creation, is the only step that touches it, and only when
// distinctIds is non-empty) -- a test exercising only those three
// conditions, or a scene with no Renderable references at all, may
// safely pass nullptr. RuntimeApplication::initializeSteps() itself
// always passes a real, already-constructed Device.
[[nodiscard]] atlantis::Result<SceneLoadOutcome, RuntimeInitError> loadAndInstantiateScene(
    const BootstrapConfig& config, atlantis::rhi::Device* device,
    const atlantis::rhi::VertexInputLayout& vertexInputLayout);

}  // namespace atlantis::runtime
