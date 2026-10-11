#pragma once

// Spec 0059 / ADR-0117 D3 (Plan 0059 M9, P10): the benchmark's diagnostic
// variants, applied through the Runtime's own RuntimeConnection -- as World
// commands, in one transaction, before warmup -- exactly as any client would
// edit the scene. Private to the atlantis_runtime executable's startup layer.

#include "cli.h"

#include <atlantis/connection/runtime_connection.h>
#include <atlantis/result.h>

#include <cstddef>
#include <string>

namespace atlantis::runtime::benchmark {

struct VariantEdit {
  std::size_t commands = 0;  // commands in the transaction (0 for baseline)
};

// Submits the variant's edits as one transaction on `connection`; they take effect
// at the start of the next frame. The error text says why a variant cannot apply
// to this scene (it has no camera, or no point light).
[[nodiscard]] atlantis::Result<VariantEdit, std::string> submitVariant(
    atlantis::connection::RuntimeConnection& connection, cli::BenchmarkVariant variant);

// After the frame that applied them: an empty string if the World shows the
// variant and no command was refused, else what is wrong.
[[nodiscard]] std::string verifyVariant(atlantis::connection::RuntimeConnection& connection,
                                        cli::BenchmarkVariant variant);

}  // namespace atlantis::runtime::benchmark
