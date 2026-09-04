#include <atlantis/runtime/init_error.h>

#include <atlantis/assert.h>

namespace atlantis::runtime {

const char* toString(RuntimeInitError error) noexcept {
  switch (error) {  // no default -- see Plan 0013 Section D3 (2026-08-21 amendment)
    case RuntimeInitError::PlatformInitFailed:
      return "PlatformInitFailed";
    case RuntimeInitError::ShaderLoadFailed:
      return "ShaderLoadFailed";
    case RuntimeInitError::DeviceCreateFailed:
      return "DeviceCreateFailed";
    case RuntimeInitError::AssetLoadFailed:
      return "AssetLoadFailed";
    case RuntimeInitError::MeshCreateFailed:
      return "MeshCreateFailed";
    case RuntimeInitError::CameraBufferCreateFailed:
      return "CameraBufferCreateFailed";
    case RuntimeInitError::AssetMetadataParseFailed:
      return "AssetMetadataParseFailed";
    case RuntimeInitError::SceneConstructionFailed:
      return "SceneConstructionFailed";
    case RuntimeInitError::SceneManifestLoadFailed:
      return "SceneManifestLoadFailed";
    case RuntimeInitError::SceneArtifactLoadFailed:
      return "SceneArtifactLoadFailed";
    case RuntimeInitError::SceneDependencyUnresolved:
      return "SceneDependencyUnresolved";
    case RuntimeInitError::SceneDependencyLoadFailed:
      return "SceneDependencyLoadFailed";
    case RuntimeInitError::PbrBaseColorTextureNotSrgb:
      return "PbrBaseColorTextureNotSrgb";
    case RuntimeInitError::EnvironmentConfigInvalid:
      return "EnvironmentConfigInvalid";
    case RuntimeInitError::EnvironmentLoadFailed:
      return "EnvironmentLoadFailed";
    case RuntimeInitError::HdrColorTargetCreateFailed:
      return "HdrColorTargetCreateFailed";
    case RuntimeInitError::FullscreenTriangleVertexBufferCreateFailed:
      return "FullscreenTriangleVertexBufferCreateFailed";
    case RuntimeInitError::FullscreenTriangleIndexBufferCreateFailed:
      return "FullscreenTriangleIndexBufferCreateFailed";
    case RuntimeInitError::OutputTransformSamplerCreateFailed:
      return "OutputTransformSamplerCreateFailed";
    case RuntimeInitError::OutputTransformUnormPipelineCreateFailed:
      return "OutputTransformUnormPipelineCreateFailed";
    case RuntimeInitError::OutputTransformSrgbPipelineCreateFailed:
      return "OutputTransformSrgbPipelineCreateFailed";
    case RuntimeInitError::FallbackMaterialCreateFailed:
      return "FallbackMaterialCreateFailed";
    case RuntimeInitError::SkyPipelineCreateFailed:
      return "SkyPipelineCreateFailed";
  }
  // Reached only if a future RuntimeInitError value is added without a
  // corresponding case above -- see exit_reason.cpp's identical comment
  // for why this fallback is required and why it must never become a
  // `default:` case.
  ATLANTIS_CHECK_MSG(false, "toString(RuntimeInitError): unhandled enumerator");
  return "(unrecognized RuntimeInitError)";
}

}  // namespace atlantis::runtime
