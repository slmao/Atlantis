#pragma once

#include <cstddef>

namespace atlantis::render_graph {

class RenderGraphBuilder;  // fwd decl; only it may construct/interpret handles

// A graph-local, builder-scoped reference to a declared pass. Distinct,
// strongly-typed from ResourceHandle -- the two never implicitly convert
// to one another; using one where the other is expected is a compile
// error (Spec 0005 Error Model, compile-time-type-error tier).
//
// Ownership: a plain, trivially-copyable value token. It owns nothing --
// copying does not transfer or share ownership of anything, it only
// produces another reference to the same builder-scoped identity
// (ADR-0017). Default-constructed to an always-invalid state (index_ ==
// the sentinel below, never a valid position). Only valid for use with
// the RenderGraphBuilder instance that vended it, for that builder's
// lifetime -- using it after that builder is destroyed is a lifetime
// precondition violation, not a guaranteed-detectable error (Spec 0005
// Error Model; see Plan Section 5 for why).
//
// Thread-safety: the value itself has no internal synchronization and
// needs none -- it may be freely copied and passed between threads as
// ordinary data. What is *not* thread-safe is using it: every call that
// takes a PassHandle (RenderGraphBuilder::reads()/writes(), etc.) must
// happen on the single Phase 1 logical frame thread that owns the
// RenderGraphBuilder it names (ADR-0004) -- the same restriction
// RenderGraphBuilder itself states, not an independent one.
class PassHandle {
 public:
  PassHandle() noexcept = default;
  [[nodiscard]] bool operator==(const PassHandle&) const noexcept = default;

 private:
  friend class RenderGraphBuilder;
  PassHandle(const void* owner, std::size_t index) noexcept : owner_(owner), index_(index) {}

  const void* owner_ = nullptr;
  std::size_t index_ = static_cast<std::size_t>(-1);  // invalid sentinel, never a real position
};

// A graph-local, builder-scoped reference to a declared logical resource.
// Same ownership/thread-safety contract as PassHandle above, a distinct
// type from it.
class ResourceHandle {
 public:
  ResourceHandle() noexcept = default;
  [[nodiscard]] bool operator==(const ResourceHandle&) const noexcept = default;

 private:
  friend class RenderGraphBuilder;
  ResourceHandle(const void* owner, std::size_t index) noexcept : owner_(owner), index_(index) {}

  const void* owner_ = nullptr;
  std::size_t index_ = static_cast<std::size_t>(-1);
};

}  // namespace atlantis::render_graph
