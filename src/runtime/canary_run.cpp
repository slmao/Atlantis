#include "canary_run.h"

#include "benchmark_run.h"
#include "gate_canary.h"
#include "gate_orchestrate.h"

#include <atlantis/connection/json.h>
#include <atlantis/log.h>
#include <atlantis/profile.h>
#include <atlantis/profile_recorder.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "canary/canary_spirv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace atlantis::runtime::canary {

namespace json = atlantis::connection::json;
namespace profile = atlantis::profile;
namespace rhi = atlantis::rhi;
namespace gate = atlantis::runtime::gate;
namespace bench = atlantis::runtime::benchmark;

#if defined(ATLANTIS_PROFILING)

namespace {

// Component order is gate::kCanaryComponents: alu, bw, tex.
constexpr std::array<const char*, gate::kCanaryComponentCount> kRegionNames = {"canary_alu", "canary_bw", "canary_tex"};

struct PushConstants {
  float seed;
  float count;
};

// One fullscreen triangle: (-1,-1), (3,-1), (-1,3).
constexpr std::array<float, 6> kTriangle = {-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
constexpr std::array<std::uint16_t, 3> kIndices = {0, 1, 2};

struct RecordingGuard {
  RecordingGuard() { profile::startRecording(); }
  ~RecordingGuard() { profile::stopRecording(); }
};

[[nodiscard]] rhi::ShaderStageBytecode bytecode(const auto& words) { return {words.data(), words.size()}; }

struct Resources {
  std::unique_ptr<rhi::Device> device;
  std::unique_ptr<rhi::Pipeline> alu, bw, tex;
  std::unique_ptr<rhi::Buffer> vertices, indices;
  std::unique_ptr<rhi::HdrColorTarget> hdr;
  std::unique_ptr<rhi::OffscreenTarget> offscreen;
  std::unique_ptr<rhi::Sampler> sampler;
};

[[nodiscard]] std::optional<Resources> createResources() {
  auto deviceResult = atlantis::vulkan_backend::createDevice({.applicationName = "Atlantis GPU canary"});
  if (deviceResult.isErr()) {
    std::cerr << "atlantis_runtime: --canary: no Vulkan device could be created\n";
    return std::nullopt;
  }
  Resources r;
  r.device = std::move(deviceResult.value());
  rhi::Device& device = *r.device;

  rhi::VertexInputLayout layout;
  layout.strideBytes = sizeof(float) * 2;
  layout.attributes = {{.location = 0, .offsetBytes = 0, .format = rhi::VertexAttributeFormat::Float2}};

  const auto makePipeline = [&](const auto& fragment, std::variant<rhi::Format, rhi::HdrFormat> format,
                                std::uint32_t sampledBindings) {
    return device.createPipeline({.vertexShader = bytecode(kVertexSpirv),
                                  .fragmentShader = bytecode(fragment),
                                  .vertexInputLayout = layout,
                                  .colorFormat = format,
                                  .pushConstantSizeBytes = sizeof(PushConstants),
                                  .sampledTextureBindingCount = sampledBindings,
                                  .hasCameraUniformBinding = false,
                                  .hasDepthAttachment = false});
  };
  auto alu = makePipeline(kAluSpirv, rhi::Format::Rgba8Unorm, 0);
  auto bw = makePipeline(kBwSpirv, rhi::HdrFormat::Rgba16Float, 0);
  auto tex = makePipeline(kTexSpirv, rhi::Format::Rgba8Unorm, 1);
  if (alu.isErr() || bw.isErr() || tex.isErr()) {
    std::cerr << "atlantis_runtime: --canary: a canary pipeline could not be created\n";
    return std::nullopt;
  }
  r.alu = std::move(alu.value());
  r.bw = std::move(bw.value());
  r.tex = std::move(tex.value());

  auto vertices = device.createBuffer({.purpose = rhi::BufferPurpose::Vertex, .sizeBytes = sizeof(kTriangle)});
  auto indices = device.createBuffer({.purpose = rhi::BufferPurpose::Index, .sizeBytes = sizeof(kIndices)});
  if (vertices.isErr() || indices.isErr()) {
    std::cerr << "atlantis_runtime: --canary: a canary buffer could not be created\n";
    return std::nullopt;
  }
  r.vertices = std::move(vertices.value());
  r.indices = std::move(indices.value());
  std::memcpy(r.vertices->mappedData(), kTriangle.data(), sizeof(kTriangle));
  std::memcpy(r.indices->mappedData(), kIndices.data(), sizeof(kIndices));

  const rhi::Extent2D extent{gate::kCanaryWidth, gate::kCanaryHeight};
  auto hdr = device.createHdrColorTarget({.extent = extent, .format = rhi::HdrFormat::Rgba16Float});
  auto offscreen = device.createOffscreenTarget({.extent = extent, .format = rhi::Format::Rgba8Unorm});
  auto sampler = device.createSampler({.filter = rhi::Filter::Nearest, .addressMode = rhi::AddressMode::ClampToEdge});
  if (hdr.isErr() || offscreen.isErr() || sampler.isErr()) {
    std::cerr << "atlantis_runtime: --canary: a canary render target could not be created\n";
    return std::nullopt;
  }
  r.hdr = std::move(hdr.value());
  r.offscreen = std::move(offscreen.value());
  r.sampler = std::move(sampler.value());
  return r;
}

// One repetition: the three components in turn, each in its own named region, then submit.
[[nodiscard]] bool submitRepetition(Resources& r, unsigned int repetition) {
  auto acquired = r.offscreen->acquireTarget();
  if (acquired.isErr()) return false;
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquired.value());
  auto commandListResult = r.device->createCommandList();
  if (commandListResult.isErr()) return false;
  std::unique_ptr<rhi::CommandList> cmd = std::move(commandListResult.value());
  const float seed = 0.001F * static_cast<float>(repetition % 64);
  constexpr rhi::ClearColorValue kClear{0.0F, 0.0F, 0.0F, 1.0F};

  const auto drawTriangle = [&](float push, float count) {
    const PushConstants constants{push, count};
    cmd->pushConstant(&constants, sizeof(constants));
    cmd->drawIndexed(3);
  };

  // bw: write bandwidth. kCanaryBwOverdraw fullscreen triangles into the RGBA16F target.
  cmd->beginRegion(kRegionNames[1]);
  // From Undefined every repetition, as the renderer's frames do: the previous contents are not needed.
  cmd->transitionResource(*r.hdr, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachmentOutput);
  cmd->beginRendering(*r.hdr, nullptr, kClear, 1.0F);
  cmd->bindPipeline(*r.bw);
  cmd->bindVertexBuffer(*r.vertices);
  cmd->bindIndexBuffer(*r.indices);
  for (unsigned int i = 0; i < gate::kCanaryBwOverdraw; ++i) drawTriangle(seed + 0.01F * static_cast<float>(i), 0.0F);
  cmd->endRendering();
  cmd->transitionResource(*r.hdr, rhi::ResourceState::ColorAttachmentOutput, rhi::ResourceState::ShaderRead);
  cmd->endRegion();

  // tex: read bandwidth and latency. kCanaryTexFetches dependent pseudo-random fetches from the target just written.
  cmd->beginRegion(kRegionNames[2]);
  cmd->transitionResource(*target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachmentOutput);
  cmd->beginRendering(*target, nullptr, kClear, 1.0F);
  cmd->bindPipeline(*r.tex);
  cmd->bindVertexBuffer(*r.vertices);
  cmd->bindIndexBuffer(*r.indices);
  cmd->bindTexture(0, *r.hdr, *r.sampler);
  drawTriangle(seed, 0.0F);
  cmd->endRendering();
  cmd->endRegion();

  // alu: a long dependent mad/frac chain per fragment, no memory traffic.
  cmd->beginRegion(kRegionNames[0]);
  cmd->beginRendering(*target, nullptr, kClear, 1.0F);
  cmd->bindPipeline(*r.alu);
  cmd->bindVertexBuffer(*r.vertices);
  cmd->bindIndexBuffer(*r.indices);
  drawTriangle(seed, static_cast<float>(gate::kCanaryAluIterations));
  cmd->endRendering();
  cmd->endRegion();

  cmd->transitionResource(*target, rhi::ResourceState::ColorAttachmentOutput, rhi::ResourceState::TransferSource);
  return r.device->submit(std::move(cmd), *target).isOk();
}

// The frame's three region times in ms, in component order; false if a region is missing.
[[nodiscard]] bool regionTimes(std::uint64_t frameId, std::array<double, gate::kCanaryComponentCount>& out) {
  profile::FrameRecord record;
  if (!profile::readFrame(frameId, record) || !record.gpuDelivered) return false;
  std::size_t found = 0;
  for (std::uint32_t i = 0; i < record.regionCount; ++i) {
    const std::string_view name = profile::nameOf(record.regions[i].name);
    for (std::size_t c = 0; c < gate::kCanaryComponentCount; ++c) {
      if (name == kRegionNames[c]) {
        out[c] = static_cast<double>(record.regions[i].endNs - record.regions[i].beginNs) / 1.0e6;
        ++found;
      }
    }
  }
  return found == gate::kCanaryComponentCount;
}

[[nodiscard]] json::Value metaValue() {
  const bench::BuildInfo build = bench::currentBuildInfo();
  const profile::DeviceInfo device = profile::deviceInfo();
  const auto optionalString = [](const std::string& text) { return text.empty() ? json::Value() : json::Value::string(text); };
  json::Value buildValue = json::Value::object();
  buildValue.set("type", json::Value::string(build.type));
  buildValue.set("profiling", json::Value::boolean(build.profiling));
  buildValue.set("tracy", json::Value::boolean(build.tracy));
  buildValue.set("gpu_markers", json::Value::boolean(build.gpuMarkers));
  buildValue.set("validation", json::Value::boolean(build.validation));
  buildValue.set("compiler", json::Value::string(build.compiler));
  buildValue.set("git_revision", optionalString(build.gitRevision));
  buildValue.set("git_dirty", json::Value::boolean(build.gitDirty));
  json::Value system = json::Value::object();
  system.set("os", json::Value::string("windows"));
  system.set("cpu", optionalString(bench::currentCpuBrand()));
  system.set("gpu", optionalString(device.gpu));
  system.set("driver", optionalString(device.driver));
  system.set("vulkan_api", optionalString(device.vulkanApi));
  system.set("timestamp_period_ns", device.timestampsSupported ? gate::num(device.timestampPeriodNs) : json::Value());
  json::Value meta = json::Value::object();
  meta.set("build", std::move(buildValue));
  meta.set("system", std::move(system));
  return meta;
}

}  // namespace

int runCanary(const cli::CanaryRequest& request) {
#if defined(_WIN32)
  if (request.pinProcess) {
    std::string pinError;
    if (!gate::pinCurrentProcess(pinError)) {
      std::cerr << "atlantis_runtime: --canary: cannot pin the process: " << pinError << "\n";
      return bench::kExitEnvironment;
    }
  }
#endif
  std::optional<Resources> resources = createResources();
  if (!resources) return bench::kExitEnvironment;
  if (!profile::deviceInfo().timestampsSupported) {
    std::cerr << "atlantis_runtime: --canary: this device cannot write GPU timestamps, so the canary cannot be "
                 "measured\n";
    return bench::kExitEnvironment;
  }

  const RecordingGuard recording;
  std::array<std::vector<double>, gate::kCanaryComponentCount> measured;
  const unsigned int total = gate::kCanaryWarmup + gate::kCanaryRepetitions;
  std::optional<std::uint64_t> previousFrame;
  unsigned int previousIndex = 0;

  const auto collect = [&](std::uint64_t frameId, unsigned int index) {
    std::array<double, gate::kCanaryComponentCount> times{};
    if (!regionTimes(frameId, times)) return false;
    if (index >= gate::kCanaryWarmup) {
      for (std::size_t c = 0; c < gate::kCanaryComponentCount; ++c) measured[c].push_back(times[c]);
    }
    return true;
  };

  for (unsigned int i = 0; i < total; ++i) {
    profile::beginFrame();
    const std::uint64_t frameId = profile::currentFrameId();
    if (!submitRepetition(*resources, i)) {
      std::cerr << "atlantis_runtime: --canary: a canary submission failed\n";
      return bench::kExitRuntimeFailure;
    }
    // This submit() drained the previous one, so the previous frame's GPU timestamps have arrived.
    if (previousFrame && !collect(*previousFrame, previousIndex)) {
      std::cerr << "atlantis_runtime: --canary: no GPU timestamps arrived for a canary frame\n";
      return bench::kExitRuntimeFailure;
    }
    previousFrame = frameId;
    previousIndex = i;
  }
  if (!resources->device->waitIdle().isOk()) return bench::kExitRuntimeFailure;
  if (previousFrame && !collect(*previousFrame, previousIndex)) {
    std::cerr << "atlantis_runtime: --canary: no GPU timestamps arrived for the last canary frame\n";
    return bench::kExitRuntimeFailure;
  }

  const json::Value document = gate::buildCanaryDocument(metaValue(), measured);
  const std::string text = json::write(document) + "\n";
  if (request.outputPath.has_value()) {
    std::ofstream out(*request.outputPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "atlantis_runtime: --canary: cannot write " << *request.outputPath << "\n";
      return bench::kExitRuntimeFailure;
    }
    out << text;
  }
  if (!request.outputPath.has_value() || request.json) std::cout << text;
  return bench::kExitOk;
}

#else

int runCanary(const cli::CanaryRequest& request) {
  static_cast<void>(request);
  std::cerr << "atlantis_runtime: --canary needs a build with ATLANTIS_PROFILING=ON (this build has none); "
               "configure with -DATLANTIS_PROFILING=ON\n";
  return bench::kExitEnvironment;
}

#endif

}  // namespace atlantis::runtime::canary
