#pragma once

#include <memory>

#include <atlantis/result.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/submission_signal.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// Represents a logical GPU device and its queues. Has no window/surface
// knowledge. Owned by whoever constructs it (a verification demo today;
// Runtime, once that module exists). Not internally thread-safe;
// construction, use, and destruction all happen on the single Phase 1
// frame thread (ADR-0004).
class Device {
 public:
  virtual ~Device() = default;

  // Vends a CommandList already in the recording state (its underlying
  // command buffer has begun). Not pooled beyond ordinary RAII -- a
  // caller creates one, records into it, and submits it exactly once
  // (ADR-0020).
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<CommandList>, CommandListCreateError>
  createCommandList() = 0;

  // Takes ownership of commandList (moved in, per ADR-0020). target is
  // read only for its acquire-complete wait signal -- Device does not
  // take ownership of target. Internally waits on, then releases, any
  // previously-retained submission before accepting this one -- a caller
  // never manages a fence directly (single-frame-in-flight baseline).
  // On success, the returned SubmissionSignal is what present() must be
  // given. Precondition, not enforced by the type system, confirmed by
  // Human Review as an accepted design constraint: a caller must call
  // present() for a successful submit() before calling submit() again --
  // submit() followed directly by application exit remains legal
  // (waitIdle() drains it). See
  // plans/0006-rhi-render-graph-frame-execution-foundation.md.
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<SubmissionSignal>, SubmitError> submit(
      std::unique_ptr<CommandList> commandList, const RenderTarget& target) = 0;

  // Blocks until every submission this Device has made has finished
  // executing on the GPU. Required before destroying a Presentation/
  // Device with an outstanding acquired RenderTarget or unwaited
  // submission (ADR-0019) -- including a mid-frame exit (acquired, never
  // submitted) or a submit-then-exit (submitted, never presented).
  [[nodiscard]] virtual atlantis::Result<std::monostate, SubmitError> waitIdle() = 0;
};

}  // namespace atlantis::rhi
