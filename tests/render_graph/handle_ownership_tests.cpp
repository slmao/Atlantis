#include <atlantis/assert.h>
#include <atlantis/render_graph/compile_error.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using atlantis::render_graph::CompileError;
using atlantis::render_graph::CompiledDependencyEdge;
using atlantis::render_graph::CompiledGraph;
using atlantis::render_graph::CompiledPassId;
using atlantis::render_graph::DependencyCycleError;
using atlantis::render_graph::MultipleProducersError;
using atlantis::render_graph::PassDiagnostic;
using atlantis::render_graph::PassHandle;
using atlantis::render_graph::RenderGraphBuilder;
using atlantis::render_graph::ResourceDiagnostic;
using atlantis::render_graph::ResourceHandle;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Installs a recording, non-terminating replacement failure handler for
// the lifetime of one test, and restores whatever handler was previously
// installed on destruction -- so one test's replacement handler can never
// leak into another (Core's own tests/core/assert_tests.cpp establishes
// this pattern; this is its RAII form).
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

// RenderGraphBuilder is the sole, exclusive owner of its accumulated
// declarations (ADR-0017) -- neither copyable nor movable.
static_assert(!std::is_copy_constructible_v<RenderGraphBuilder>);
static_assert(!std::is_copy_assignable_v<RenderGraphBuilder>);
static_assert(!std::is_move_constructible_v<RenderGraphBuilder>);
static_assert(!std::is_move_assignable_v<RenderGraphBuilder>);

// PassHandle/ResourceHandle/CompiledPassId are pairwise distinct types
// with no implicit conversion between any pair (Spec 0005 Error Model,
// compile-time-type-error tier; Plan 0005 Sections 2/4).
static_assert(!std::is_same_v<PassHandle, ResourceHandle>);
static_assert(!std::is_same_v<PassHandle, CompiledPassId>);
static_assert(!std::is_same_v<ResourceHandle, CompiledPassId>);

static_assert(!std::is_convertible_v<PassHandle, ResourceHandle>);
static_assert(!std::is_convertible_v<ResourceHandle, PassHandle>);
static_assert(!std::is_convertible_v<PassHandle, CompiledPassId>);
static_assert(!std::is_convertible_v<CompiledPassId, PassHandle>);
static_assert(!std::is_convertible_v<ResourceHandle, CompiledPassId>);
static_assert(!std::is_convertible_v<CompiledPassId, ResourceHandle>);

// Plain, trivially-copyable value tokens (Plan 0005 Section 2) -- safe to
// copy freely, no shared/reference-counted ownership of anything.
static_assert(std::is_copy_constructible_v<PassHandle>);
static_assert(std::is_copy_assignable_v<PassHandle>);
static_assert(std::is_copy_constructible_v<ResourceHandle>);
static_assert(std::is_copy_assignable_v<ResourceHandle>);
static_assert(std::is_copy_constructible_v<CompiledPassId>);
static_assert(std::is_copy_assignable_v<CompiledPassId>);

// CompiledGraph is move-only (Plan 0005 Section 4): move-constructible
// and move-assignable, but never copyable.
static_assert(std::is_move_constructible_v<CompiledGraph>);
static_assert(std::is_move_assignable_v<CompiledGraph>);
static_assert(!std::is_copy_constructible_v<CompiledGraph>);
static_assert(!std::is_copy_assignable_v<CompiledGraph>);

// CompileError is exactly the two-alternative closed sum type (Plan 0005
// Section 2) -- no third alternative, no `kind`/payload pair to
// desynchronize.
static_assert(std::variant_size_v<CompileError> == 2);
static_assert(std::is_same_v<std::variant_alternative_t<0, CompileError>, MultipleProducersError>);
static_assert(std::is_same_v<std::variant_alternative_t<1, CompileError>, DependencyCycleError>);

TEST_CASE("Default-constructed PassHandle values compare equal", "[render_graph][handles]") {
  const PassHandle a;
  const PassHandle b;
  REQUIRE(a == b);
}

TEST_CASE("Default-constructed ResourceHandle values compare equal", "[render_graph][handles]") {
  const ResourceHandle a;
  const ResourceHandle b;
  REQUIRE(a == b);
}

TEST_CASE("Default-constructed CompiledPassId values compare equal", "[render_graph][compiled_graph]") {
  const CompiledPassId a;
  const CompiledPassId b;
  REQUIRE(a == b);
}

TEST_CASE("A default-constructed CompiledPassId's index is the invalid sentinel", "[render_graph][compiled_graph]") {
  const CompiledPassId id;
  REQUIRE(id.index() == static_cast<std::size_t>(-1));
}

TEST_CASE("CompileError can hold a MultipleProducersError alternative", "[render_graph][compile_error]") {
  CompileError error =
      MultipleProducersError{ResourceDiagnostic{0, "resource"}, {PassDiagnostic{0, "a"}, PassDiagnostic{1, "b"}}};

  REQUIRE(std::holds_alternative<MultipleProducersError>(error));
  REQUIRE_FALSE(std::holds_alternative<DependencyCycleError>(error));
  REQUIRE(std::get<MultipleProducersError>(error).producers.size() == 2);
}

TEST_CASE("CompileError can hold a DependencyCycleError alternative", "[render_graph][compile_error]") {
  CompileError error = DependencyCycleError{{PassDiagnostic{0, "a"}, PassDiagnostic{1, "b"}}};

  REQUIRE(std::holds_alternative<DependencyCycleError>(error));
  REQUIRE_FALSE(std::holds_alternative<MultipleProducersError>(error));
  REQUIRE(std::get<DependencyCycleError>(error).passes.size() == 2);
}

TEST_CASE("Handles vended by a builder are accepted by that same builder", "[render_graph][render_graph_builder]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass("pass");
  const ResourceHandle resource = builder.declareResource("resource");

  builder.reads(pass, resource);

  REQUIRE(recorded.empty());
}

TEST_CASE("A default-constructed PassHandle passed to reads() triggers the assertion policy without crashing",
          "[render_graph][render_graph_builder]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const ResourceHandle resource = builder.declareResource();

  builder.reads(PassHandle{}, resource);

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("A default-constructed ResourceHandle passed to writes() triggers the assertion policy without crashing",
          "[render_graph][render_graph_builder]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle pass = builder.declarePass();

  builder.writes(pass, ResourceHandle{});

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("A PassHandle from a different, live builder triggers the assertion policy",
          "[render_graph][render_graph_builder]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builderA;
  RenderGraphBuilder builderB;
  const PassHandle passFromA = builderA.declarePass();
  const ResourceHandle resourceOnB = builderB.declareResource();

  builderB.reads(passFromA, resourceOnB);

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("A ResourceHandle from a different, live builder triggers the assertion policy",
          "[render_graph][render_graph_builder]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builderA;
  RenderGraphBuilder builderB;
  const ResourceHandle resourceFromA = builderA.declareResource();
  const PassHandle passOnB = builderB.declarePass();

  builderB.reads(passOnB, resourceFromA);

  REQUIRE(recorded.size() == 1);
}

TEST_CASE("Duplicate and empty pass/resource labels are legal", "[render_graph][render_graph_builder]") {
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler guard(recorded);

  RenderGraphBuilder builder;
  const PassHandle p1 = builder.declarePass("same");
  const PassHandle p2 = builder.declarePass("same");
  const PassHandle p3 = builder.declarePass();
  const ResourceHandle r1 = builder.declareResource("same");
  const ResourceHandle r2 = builder.declareResource("same");
  const ResourceHandle r3 = builder.declareResource();

  REQUIRE(recorded.empty());
  REQUIRE_FALSE(p1 == p2);
  REQUIRE_FALSE(p2 == p3);
  REQUIRE_FALSE(r1 == r2);
  REQUIRE_FALSE(r2 == r3);
}

}  // namespace
