#include <atlantis/render_graph/execution.h>

#include <atlantis/assert.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/types.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fake_command_list.h"

// Spec 0016/D4: ResourceBinding's new sampledTexture field and execute()'s
// new transition branch -- Guard 0 widened to "exactly one of three," and
// the Undefined -> TransferDestination -> ShaderRead sequence produced by
// two writes() calls on the same pass, all with no Vulkan device anywhere
// in this test binary (V16).

namespace {

using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceBinding;
using atlantis::render_graph::ResourceHandle;
using atlantis::render_graph::execute;
using atlantis::render_graph::test::FakeCommandList;
using atlantis::render_graph::test::FakeRenderTarget;
using atlantis::render_graph::test::FakeSampledTexture;
using atlantis::rhi::ResourceState;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as attachment_execution_tests.cpp's own
// ScopedFailureHandler.
class ScopedFailureHandler {
 public:
  explicit ScopedFailureHandler(std::vector<RecordedFailure>& recorded)
      : previous_(atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
          recorded.push_back({std::string(info.expression), std::string(info.message)});
        })) {}

  ~ScopedFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureHandler(const ScopedFailureHandler&) = delete;
  ScopedFailureHandler& operator=(const ScopedFailureHandler&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

}  // namespace

TEST_CASE("CommandList sampled-texture upload regions preserve ordered subresource data in the mock",
          "[render_graph][execution][sampled_texture]") {
  atlantis::render_graph::test::FakeBuffer staging(atlantis::rhi::BufferPurpose::Staging, 512);
  FakeSampledTexture texture("cube-mips");
  FakeCommandList commandList;
  const std::array regions{
      atlantis::rhi::SampledTextureUploadRegion{.bufferOffsetBytes = 0, .mipLevel = 0, .arrayLayer = 0, .extent = {8, 8}},
      atlantis::rhi::SampledTextureUploadRegion{.bufferOffsetBytes = 256, .mipLevel = 1, .arrayLayer = 5, .extent = {4, 4}},
  };

  commandList.copyBufferToTexture(staging, texture, regions);

  REQUIRE(commandList.copiesBufferToTexture.size() == 1);
  REQUIRE(commandList.copiesBufferToTexture[0].source == &staging);
  REQUIRE(commandList.copiesBufferToTexture[0].destination == &texture);
  REQUIRE(commandList.copyBufferToTextureRegions.size() == 1);
  REQUIRE(commandList.copyBufferToTextureRegions[0] == std::vector(regions.begin(), regions.end()));
}

TEST_CASE("execute() records Undefined->TransferDestination, then the pass callback, then "
          "TransferDestination->ShaderRead",
          "[render_graph][execution][sampled_texture]") {
  // D4: the upload pass declares only a single TransferDestination usage
  // -- never a second, ShaderRead-tagged usage on the same pass, since
  // execute() records every one of a pass's declared-usage transitions
  // before invoking that pass's own executeFn (a ShaderRead usage
  // declared directly on the upload pass would land its barrier *before*
  // the copy, not after it, which real GPU hardware and Validation
  // Layers rejected during Milestone 3's own development). The trailing
  // ShaderRead transition is reached via ResourceBinding::finalState
  // instead -- this test confirms that ordering directly, not merely the
  // end-state.
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("texture-upload");
  const PassHandle upload = builder.declarePass("TextureUpload");
  builder.writes(upload, resource, ResourceState::TransferDestination);

  FakeSampledTexture fakeTexture("texture");
  FakeCommandList commandList;
  std::size_t transitionsWhenCallbackRan = static_cast<std::size_t>(-1);
  bool copyInvoked = false;
  builder.setExecute(upload, [&](atlantis::rhi::CommandList&) {
    transitionsWhenCallbackRan = commandList.sampledTextureTransitions.size();
    copyInvoked = true;
  });

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.sampledTexture = &fakeTexture;
  binding.finalState = ResourceState::ShaderRead;
  execute(compiled.value(), {binding}, commandList);

  REQUIRE(copyInvoked);
  REQUIRE(transitionsWhenCallbackRan == 1);  // only Undefined->TransferDestination had landed when the callback ran
  REQUIRE(commandList.sampledTextureTransitions.size() == 2);
  REQUIRE(commandList.sampledTextureTransitions[0].target == &fakeTexture);
  REQUIRE(commandList.sampledTextureTransitions[0].before == ResourceState::Undefined);
  REQUIRE(commandList.sampledTextureTransitions[0].after == ResourceState::TransferDestination);
  REQUIRE(commandList.sampledTextureTransitions[1].target == &fakeTexture);
  REQUIRE(commandList.sampledTextureTransitions[1].before == ResourceState::TransferDestination);
  REQUIRE(commandList.sampledTextureTransitions[1].after == ResourceState::ShaderRead);

  // A sampledTexture-carrying pass is never a draw pass -- no
  // beginRendering()/endRendering() wrapping.
  REQUIRE(commandList.beginRenderingCalls.empty());
}

TEST_CASE("execute() never recognizes a sampledTexture-only pass as a draw pass",
          "[render_graph][execution][sampled_texture]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("texture-upload");
  const PassHandle upload = builder.declarePass("TextureUpload");
  builder.writes(upload, resource, ResourceState::TransferDestination);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeSampledTexture fakeTexture("texture");
  FakeCommandList commandList;

  ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.sampledTexture = &fakeTexture;
  execute(compiled.value(), {binding}, commandList);

  REQUIRE(commandList.beginRenderingCalls.empty());
  REQUIRE(commandList.sampledTextureTransitions.size() == 1);
}

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with none of target/depthTexture/sampledTexture set",
          "[render_graph][execution][sampled_texture]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("orphan");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with both target and sampledTexture set",
          "[render_graph][execution][sampled_texture]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeRenderTarget fakeTarget("both-target");
  FakeSampledTexture fakeTexture("both-sampled");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.target = &fakeTarget;
  malformed.sampledTexture = &fakeTexture;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 0 rejects a ResourceBinding with both depthTexture and sampledTexture set",
          "[render_graph][execution][sampled_texture]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("both");
  static_cast<void>(resource);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  atlantis::render_graph::test::FakeTexture fakeDepth("both-depth");
  FakeSampledTexture fakeTexture("both-sampled");
  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  ResourceBinding malformed{};
  malformed.resource = compiled.value().resourceAt(0);
  malformed.depthTexture = &fakeDepth;
  malformed.sampledTexture = &fakeTexture;
  execute(compiled.value(), {malformed}, commandList);

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("execute() Guard 1 generalizes to a sampledTexture binding with no supplied entry",
          "[render_graph][execution][sampled_texture]") {
  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource("texture-upload");
  const PassHandle upload = builder.declarePass("TextureUpload");
  builder.writes(upload, resource, ResourceState::ShaderRead);

  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());

  FakeCommandList commandList;
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  execute(compiled.value(), {}, commandList);

  REQUIRE_FALSE(failures.empty());
}
