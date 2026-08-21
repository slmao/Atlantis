#pragma once

#include <atlantis/rhi/types.h>
#include <atlantis/runtime/exit_reason.h>

namespace atlantis::runtime {

// See Plan 0013 Section D3. Every PresentationError/SubmitError
// enumerator that Presentation::acquireNextTarget()/present()/
// Device::submit() can actually return as Err(...) is, uniformly,
// Runtime-unrecoverable (Spec 0013's own Presentation-and-error-state
// table). Each function switches over every real enumerator with NO
// default case, followed by an ATLANTIS_CHECK_MSG + conservative
// fallback return -- see error_classification.cpp's own comments for
// exactly what that does and does not guarantee, and the one edit that
// would silently defeat it. Real compile-time exhaustiveness protection
// comes from atlantis_runtime_host's own target-scoped /w14062 compile
// option (src/runtime/CMakeLists.txt), not from the absence of a
// default case alone.
[[nodiscard]] RuntimeExitReason classifyPresentationError(atlantis::rhi::PresentationError error) noexcept;
[[nodiscard]] RuntimeExitReason classifySubmitError(atlantis::rhi::SubmitError error) noexcept;

}  // namespace atlantis::runtime
