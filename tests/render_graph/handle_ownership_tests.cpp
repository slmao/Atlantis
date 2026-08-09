#include <atlantis/render_graph/compile_error.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/render_graph/render_graph_builder.h>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace {

using atlantis::render_graph::CompileError;
using atlantis::render_graph::CompiledDependencyEdge;
using atlantis::render_graph::CompiledGraph;
using atlantis::render_graph::CompiledPassId;
using atlantis::render_graph::DependencyCycleError;
using atlantis::render_graph::MultipleProducersError;
using atlantis::render_graph::PassDiagnostic;
using atlantis::render_graph::PassHandle;
using atlantis::render_graph::ResourceDiagnostic;
using atlantis::render_graph::ResourceHandle;

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

}  // namespace
