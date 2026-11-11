#include "mesh_preview.h"

#include "frame_capture.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/log.h>
#include <atlantis/profile.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <span>
#include <utility>

namespace atlantis::runtime::detail {
namespace {
namespace as = asset_system;
namespace ac = connection::assets;
using shader_system::rhi_integration::MeshVertexAttributeSchema;
using Position = std::array<float, 3>;

Position readPosition(std::span<const std::byte> bytes, std::uint32_t index) {
  Position pos;
  std::memcpy(pos.data(), bytes.data() + static_cast<std::size_t>(index) * as::kMeshArtifactVertexStrideBytes +
                             as::kMeshArtifactPositionOffsetBytes, sizeof(pos));
  return pos;
}

template <class Index>
std::optional<FramedMeshPreview> frameMesh(std::span<const std::byte> bytes, std::span<const Index> indices) {
  constexpr auto stride = as::kMeshArtifactVertexStrideBytes;
  if (bytes.empty() || bytes.size() % stride != 0 || indices.empty() || indices.size() % 3 != 0) return std::nullopt;
  constexpr double infinity = std::numeric_limits<double>::infinity();
  std::array<double, 3> low{infinity, infinity, infinity};
  std::array<double, 3> high{-infinity, -infinity, -infinity};
  std::vector<bool> referenced(bytes.size() / stride, false);
  for (const std::uint32_t index : indices) {
    if (index >= referenced.size()) return std::nullopt;
    if (referenced[index]) continue;
    referenced[index] = true;
    const auto pos = readPosition(bytes, index);
    for (std::size_t i = 0; i < pos.size(); ++i) {
      if (!std::isfinite(pos[i])) return std::nullopt;
      low[i] = std::min(low[i], static_cast<double>(pos[i]));
      high[i] = std::max(high[i], static_cast<double>(pos[i]));
    }
  }
  const double radius = std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]) * 0.5;
  if (!std::isfinite(radius) || radius <= 0.0) return std::nullopt;
  const std::array center{(low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5, (low[2] + high[2]) * 0.5};
  FramedMeshPreview framed{{bytes.begin(), bytes.end()}, {}};
  for (std::size_t index = 0; index < referenced.size(); ++index) {
    if (!referenced[index]) continue;
    Position pos = readPosition(bytes, static_cast<std::uint32_t>(index));
    for (std::size_t i = 0; i < pos.size(); ++i) {
      pos[i] = static_cast<float>((static_cast<double>(pos[i]) - center[i]) / radius);
    }
    std::memcpy(framed.vertexBytes.data() + index * stride + as::kMeshArtifactPositionOffsetBytes,
                pos.data(), sizeof(pos));
  }

  bool nondegenerate = false;
  for (std::size_t i = 0; i < indices.size(); i += 3) {
    const auto a = readPosition(framed.vertexBytes, indices[i]);
    const auto b = readPosition(framed.vertexBytes, indices[i + 1]);
    const auto c = readPosition(framed.vertexBytes, indices[i + 2]);
    const std::array u{double(b[0]) - a[0], double(b[1]) - a[1], double(b[2]) - a[2]};
    const std::array v{double(c[0]) - a[0], double(c[1]) - a[1], double(c[2]) - a[2]};
    if (u[1] * v[2] != u[2] * v[1] || u[2] * v[0] != u[0] * v[2] || u[0] * v[1] != u[1] * v[0]) {
      nondegenerate = true;
      break;
    }
  }
  if (!nondegenerate) return std::nullopt;

  constexpr double fov = 35.0 * std::numbers::pi / 180.0;
  constexpr double paddedRadius = 1.15;
  const double distance = paddedRadius / std::sin(fov * 0.5);
  const double directionScale = distance / std::sqrt(29.0);  // length of (3, 2, 4)
  const auto nearZ = static_cast<float>(distance - paddedRadius);
  const auto farZ = static_cast<float>(distance + paddedRadius);
  if (!(nearZ > 0.0f && nearZ < farZ)) return std::nullopt;
  framed.camera = {lookAtMatrix(static_cast<float>(3.0 * directionScale), static_cast<float>(2.0 * directionScale),
                                static_cast<float>(4.0 * directionScale), 0.0f, 0.0f, 0.0f),
                   perspectiveMatrix(static_cast<float>(fov), 1.0f, nearZ, farZ)};
  const auto finite = [](float value) { return std::isfinite(value); };
  if (!std::all_of(framed.camera.view.begin(), framed.camera.view.end(), finite) ||
      !std::all_of(framed.camera.projection.begin(), framed.camera.projection.end(), finite)) return std::nullopt;
  return framed;
}

std::optional<std::vector<std::uint32_t>> readSpirv(const std::string& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return std::nullopt;
  const auto size = static_cast<std::streamoff>(in.tellg());
  if (size <= 0 || size % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) return std::nullopt;
  in.seekg(0);
  std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
  if (!in.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size))) return std::nullopt;
  return words;
}

struct PreviewShaderPair {
  std::vector<std::uint32_t> vertex;
  std::vector<std::uint32_t> fragment;
  rhi::VertexInputLayout layout;

  [[nodiscard]] rhi::PipelineCreateParams parameters() const {
    return {.vertexShader = {vertex.data(), vertex.size()},
            .fragmentShader = {fragment.data(), fragment.size()},
            .vertexInputLayout = layout};
  }
};

std::optional<PreviewShaderPair> readPair(const std::string& vertexPath, const std::string& fragmentPath,
                                          const std::string& reflectionPath,
                                          const std::vector<MeshVertexAttributeSchema>& schema, std::uint32_t stride) {
  auto vertex = readSpirv(vertexPath);
  auto fragment = readSpirv(fragmentPath);
  auto reflection = shader_system::loadReflectionMetadata(reflectionPath);
  if (!vertex || !fragment || reflection.isErr()) return std::nullopt;
  auto layout = shader_system::rhi_integration::toVertexInputLayout(reflection.value(), schema, stride);
  if (layout.isErr()) return std::nullopt;
  return PreviewShaderPair{std::move(*vertex), std::move(*fragment), std::move(layout.value())};
}

}  // namespace

std::optional<FramedMeshPreview> frameMeshPreview(const asset_system::StaticMeshAssetData& mesh) {
  if (mesh.vertexStrideBytes() != as::kMeshArtifactVertexStrideBytes) return std::nullopt;
  return mesh.indexType() == as::MeshIndexType::Uint16
             ? frameMesh(mesh.vertexBytes(), std::span<const std::uint16_t>{mesh.indices()})
             : frameMesh(mesh.vertexBytes(), std::span<const std::uint32_t>{mesh.indices32()});
}

struct MeshPreviewResources {
  std::optional<renderer::Mesh> mesh;
  std::optional<renderer::Material> material;
  std::unique_ptr<rhi::Pipeline> shadowPipeline;
  std::unique_ptr<rhi::Pipeline> outputPipeline;
  std::unique_ptr<rhi::Sampler> shadowSampler;
  std::unique_ptr<rhi::Sampler> outputSampler;
  std::unique_ptr<rhi::Texture> depth;
  std::unique_ptr<rhi::ShadowMap> shadow;
  std::unique_ptr<rhi::HdrColorTarget> hdr;
  std::unique_ptr<rhi::Buffer> camera;
  std::unique_ptr<rhi::Buffer> light;
  std::unique_ptr<rhi::Buffer> fullscreenVertices;
  std::unique_ptr<rhi::Buffer> fullscreenIndices;
  std::unique_ptr<rhi::Buffer> readback;
  std::unique_ptr<rhi::OffscreenTarget> offscreen;
  std::unique_ptr<rhi::RenderTarget> target;  // end the borrow before destroying offscreen
};

MeshPreviewExecution renderMeshPreview(rhi::Device& device, const BootstrapConfig& config,
                                       const as::StaticMeshAssetData& mesh, const as::AssetGuid& guid,
                                       std::shared_ptr<MeshPreviewResources>& incompleteSubmission,
                                       PreviewCompletion complete, const MeshPreviewReadOnly* readOnly) {
  using E = ac::PreviewError;
  using R = ac::MeshPreviewResult;
  using rhi::BufferPurpose;
  ATLANTIS_CHECK(!incompleteSubmission);
  const auto failed = [](const char* stage, bool fatal = false) {
    ATLANTIS_LOG_ERROR("mesh preview: {} failed", stage);
    return MeshPreviewExecution{R::Err(E::RenderFailed), fatal};
  };
  const auto deviceCreationFailed = [&](const char* stage) {
    // RHI allocation/creation errors do not expose device loss separately.
    // Only probe after a failed GPU call; the normal render path waits once.
    return failed(stage, device.waitIdle().isErr());
  };
  const auto framed = frameMeshPreview(mesh);
  if (!framed) return {R::Err(E::InvalidMesh), false};

  constexpr auto stride = as::kMeshArtifactVertexStrideBytes;
  const auto geometry = readPair(config.vertexShaderSpirvPath, config.fragmentShaderSpirvPath,
                                 config.vertexShaderReflectionPath,
                                 {{0, as::kMeshArtifactPositionOffsetBytes}, {1, as::kMeshArtifactColorOffsetBytes}}, stride);
  const auto shadowShader = readPair(config.shadowCastVertexShaderSpirvPath, config.shadowCastFragmentShaderSpirvPath,
                                     config.shadowCastVertexShaderReflectionPath,
                                     {{0, as::kMeshArtifactPositionOffsetBytes}}, stride);
  const auto outputShader = readPair(config.outputTransformUnormVertexShaderSpirvPath,
                                     config.outputTransformUnormFragmentShaderSpirvPath,
                                     config.outputTransformUnormVertexShaderReflectionPath, {{0, 0}}, sizeof(float) * 2);
  if (!geometry || !shadowShader || !outputShader) return failed("shader inputs");

  auto resources = std::make_shared<MeshPreviewResources>();
  auto& r = *resources;
  const auto createMesh = [&]<class Index>(const std::vector<Index>& indices) {
    return renderer::createMesh(device, geometry->layout, framed->vertexBytes.data(), framed->vertexBytes.size(),
                                indices.data(), static_cast<std::uint32_t>(indices.size()), ATL_DEBUG_NAME("preview_mesh"));
  };
  auto gpuMesh = mesh.indexType() == as::MeshIndexType::Uint16 ? createMesh(mesh.indices()) : createMesh(mesh.indices32());
  if (gpuMesh.isErr()) return deviceCreationFailed("mesh upload");
  r.mesh.emplace(std::move(gpuMesh.value()));

  // These pipeline objects carry descriptor sets updated during recording.
  // The live scene may still have its previous submission in flight.
  auto geometryParams = geometry->parameters();
  geometryParams.colorFormat = rhi::HdrFormat::Rgba16Float;
  geometryParams.pushConstantSizeBytes = sizeof(Mat4);
  auto material = renderer::createMaterial(device, geometryParams);
  if (material.isErr()) return deviceCreationFailed("material creation");
  r.material.emplace(std::move(material.value()));
  auto shadowParams = shadowShader->parameters();
  shadowParams.pushConstantSizeBytes = sizeof(Mat4);
  shadowParams.hasColorAttachment = false;
  auto shadowPipeline = device.createPipeline(shadowParams);
  if (shadowPipeline.isErr()) return deviceCreationFailed("shadow pipeline creation");
  r.shadowPipeline = std::move(shadowPipeline.value());
  auto outputParams = outputShader->parameters();
  outputParams.colorFormat = rhi::Format::Rgba8Unorm;
  outputParams.pushConstantSizeBytes = sizeof(float);
  outputParams.sampledTextureBindingCount = 1;
  outputParams.hasCameraUniformBinding = false;
  outputParams.hasDepthAttachment = false;
  auto outputPipeline = device.createPipeline(outputParams);
  if (outputPipeline.isErr()) return deviceCreationFailed("output pipeline creation");
  r.outputPipeline = std::move(outputPipeline.value());

  constexpr rhi::Extent2D extent{ac::kPreviewDimension, ac::kPreviewDimension};
  constexpr std::size_t pixelBytes = std::size_t{extent.width} * extent.height * 4;
  const auto assign = [](auto& destination, auto result) {
    if (result.isErr()) return false;
    destination = std::move(result.value());
    return true;
  };
  if (!assign(r.depth, device.createTexture({.extent = extent, .debugName = ATL_DEBUG_NAME("preview_depth")})) ||
      !assign(r.hdr, device.createHdrColorTarget({.extent = extent, .debugName = ATL_DEBUG_NAME("preview_hdr")})) ||
      !assign(r.shadow, device.createShadowMap({.extent = {1, 1}, .debugName = ATL_DEBUG_NAME("preview_shadow")})) ||
      !assign(r.offscreen, device.createOffscreenTarget({.extent = extent, .format = rhi::Format::Rgba8Unorm,
                                                         .debugName = ATL_DEBUG_NAME("preview_color")}))) {
    return deviceCreationFailed("attachment creation");
  }

  if (!assign(r.camera, device.createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(CameraMatrices)})) ||
      !assign(r.light, device.createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(CameraMatrices)})) ||
      !assign(r.readback, device.createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = pixelBytes}))) {
    return deviceCreationFailed("buffer creation");
  }
  const CameraMatrices noLights{identityMatrix(), identityMatrix()};
  std::memcpy(r.camera->mappedData(), &framed->camera, sizeof(CameraMatrices));
  std::memcpy(r.light->mappedData(), &noLights, sizeof(CameraMatrices));
  std::optional<MeshPreviewReadOnly> headlessInputs;
  if (!readOnly) {
    constexpr std::array<float, 6> vertices{-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    constexpr std::array<std::uint16_t, 3> indices{0, 1, 2};
    if (!assign(r.fullscreenVertices, device.createBuffer({.purpose = BufferPurpose::Vertex,
                                                           .sizeBytes = sizeof(vertices)})) ||
        !assign(r.fullscreenIndices, device.createBuffer({.purpose = BufferPurpose::Index,
                                                          .sizeBytes = sizeof(indices)})) ||
        !assign(r.shadowSampler, device.createSampler({.filter = rhi::Filter::Nearest,
                                                      .addressMode = rhi::AddressMode::ClampToEdge})) ||
        !assign(r.outputSampler, device.createSampler({.filter = rhi::Filter::Linear,
                                                      .addressMode = rhi::AddressMode::ClampToEdge}))) {
      return deviceCreationFailed("headless input creation");
    }
    std::memcpy(r.fullscreenVertices->mappedData(), vertices.data(), sizeof(vertices));
    std::memcpy(r.fullscreenIndices->mappedData(), indices.data(), sizeof(indices));
    headlessInputs.emplace(MeshPreviewReadOnly{*r.fullscreenVertices, *r.fullscreenIndices,
                                                *r.outputSampler, *r.shadowSampler});
    readOnly = &*headlessInputs;
  }

  auto target = r.offscreen->acquireTarget();
  if (target.isErr()) return failed("offscreen acquire", target.error() == rhi::OffscreenAcquireError::DeviceLost);
  r.target = std::move(target.value());
  auto list = device.createCommandList();
  if (list.isErr()) return deviceCreationFailed("command list creation");
  using rhi::ResourceState;
  const renderer::DrawItem draw{.mesh = &*r.mesh, .material = &*r.material, .objectToWorld = identityMatrix()};
  renderer::Renderer renderer;
  renderer.drawFrame(*list.value(), *r.target, *r.depth, *r.camera, std::span{&draw, 1}, ResourceState::TransferSource,
                     *r.hdr, readOnly->fullscreenVertices, readOnly->fullscreenIndices, *r.outputPipeline,
                     readOnly->outputSampler, 0.0f, nullptr, nullptr, *r.shadow, readOnly->shadowSampler,
                     *r.shadowPipeline, *r.light, {});

  render_graph::RenderGraphBuilder copy;
  const auto color = copy.declareResource("preview-color");
  const auto read = copy.declarePass("preview-copy-to-buffer");
  copy.writes(read, color, ResourceState::TransferSource);
  copy.setExecute(read, [&r](rhi::CommandList& cmd) { cmd.copyRenderTargetToBuffer(*r.target, *r.readback); });
  const auto compiled = copy.compile();
  if (compiled.isErr()) return failed("readback graph");
  const std::vector<render_graph::ResourceBinding> bindings{{.resource = compiled.value().resourceAt(0),
                                                            .target = r.target.get(),
                                                            .incomingState = ResourceState::TransferSource,
                                                            .finalState = std::nullopt}};
  render_graph::execute(compiled.value(), bindings, *list.value());
  if (device.submit(std::move(list.value()), *r.target).isErr()) {
    incompleteSubmission = std::move(resources);  // conservative on device loss
    return failed("submit", true);
  }
  if ((complete ? complete(device) : device.waitIdle()).isErr()) {
    incompleteSubmission = std::move(resources);
    return failed("completion", true);
  }
  r.target.reset();
  const auto* pixels = static_cast<const std::uint8_t*>(r.readback->mappedData());
  auto png = encodeCapturePng(extent.width, extent.height, {pixels, pixelBytes}, ac::kPreviewPngMaxBytes);
  if (!png) return failed("PNG encoding");
  return {R::Ok(ac::MeshPreview{guid, extent.width, extent.height, std::move(*png)}), false};
}

}  // namespace atlantis::runtime::detail

namespace atlantis::runtime {

bool RuntimeApplication::meshPreviewAvailable() const noexcept {
  return device_ && fullscreenTriangleVertexBuffer_ && fullscreenTriangleIndexBuffer_ && outputTransformSampler_ &&
         shadowMapSampler_ && lifecycle_.state() == RuntimeLifecycleState::Running && shouldContinue();
}

connection::assets::MeshPreviewResult RuntimeApplication::previewMesh(
    const BootstrapConfig& config, const asset_system::AssetGuid& guid, asset_system::StaticMeshAssetData mesh) {
  return previewMeshWithCompletion(config, guid, mesh, nullptr);
}

connection::assets::MeshPreviewResult RuntimeApplication::previewMeshWithCompletion(
    const BootstrapConfig& config, const asset_system::AssetGuid& guid, const asset_system::StaticMeshAssetData& mesh,
    detail::PreviewCompletion complete) {
  using namespace connection::assets;
  if (!meshPreviewAvailable()) return MeshPreviewResult::Err(PreviewError::Unavailable);
  const detail::MeshPreviewReadOnly readOnly{*fullscreenTriangleVertexBuffer_, *fullscreenTriangleIndexBuffer_,
                                            *outputTransformSampler_, *shadowMapSampler_};
  auto execution = detail::renderMeshPreview(*device_, config, mesh, guid, incompleteMeshPreview_, complete, &readOnly);
  if (execution.fatal) {
    lifecycle_.markFailed();
    return MeshPreviewResult::Err(PreviewError::Unavailable);
  }
  return std::move(execution.result);
}

}  // namespace atlantis::runtime
