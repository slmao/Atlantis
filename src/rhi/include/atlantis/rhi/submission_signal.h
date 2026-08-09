#pragma once

namespace atlantis::rhi {

// Opaque token returned by Device::submit(), consumed by
// Presentation::present() as the signal to wait on before presenting
// (ADR-0019, ADR-0020). No public method beyond the destructor --
// mirrors Device's own "intentionally declares no method" precedent.
// A caller never constructs, inspects, or stores one beyond passing it
// from submit() straight to present(). Precondition, not enforced here,
// confirmed by Human Review as an accepted design constraint (see
// plans/0006-rhi-render-graph-frame-execution-foundation.md's Human
// Review Approval note): a caller must call present() for one submit()
// call before calling submit() again -- submit() followed directly by
// application exit remains legal (Device::waitIdle() drains it).
class SubmissionSignal {
 public:
  virtual ~SubmissionSignal() = default;
};

}  // namespace atlantis::rhi
