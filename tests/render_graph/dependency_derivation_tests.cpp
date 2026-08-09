#include <atlantis/assert.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// This file covers only the same-pass usage-conflict and duplicate-usage
// legality cases reachable through RenderGraphBuilder::reads()/writes()
// (Plan 0005 Section 7 items 3-4). The producer/reader edge-derivation,
// fan-out, and multiple-producer cases this file's name also covers
// (Plan 0005 Section 9) are added once RenderGraphBuilder::compile() is
// implemented -- calling compile() is out of scope for this
// implementation step.

namespace {

using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceHandle;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// See tests/render_graph/handle_ownership_tests.cpp for the same pattern
// with an identical rationale; duplicated locally rather than shared
// through a new header, since this implementation step's authorized file
// list does not include one.
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

TEST_CASE("A pass reading then writing the same resource triggers the assertion policy",
          "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.reads(pass, resource);
  builder.writes(pass, resource);

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("A pass writing then reading the same resource triggers the assertion policy",
          "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.writes(pass, resource);
  builder.reads(pass, resource);

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("Declaring the same read usage twice on the same pass is legal", "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.reads(pass, resource);
  builder.reads(pass, resource);

  REQUIRE(recorded.empty());
}

TEST_CASE("Declaring the same write usage twice on the same pass is legal", "[render_graph][dependency_derivation]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();
  const ResourceHandle resource = builder.declareResource();

  builder.writes(pass, resource);
  builder.writes(pass, resource);

  REQUIRE(recorded.empty());
}

}  // namespace
