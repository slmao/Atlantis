#pragma once

// Spec 0060 R9 / ADR-0118 D5 (Plan 0060 M8, P9): which changes require the performance gate.
//
// The paths are a committed data file, tests/performance/applicability.txt: an `include:` section
// of repository-relative globs and an `exclude:` section. A path applies when it matches an include;
// a path matched by both an include and an exclude is an include (the exclude list documents what
// is exempt on purpose). `atlantis_runtime --gate --applies` reads one changed path per line on
// stdin and says whether any of them applies. It judges nothing and needs no device.
//
// Private to the atlantis_runtime executable's startup layer (see gate_set.h); GPU-independent.

#include "gate_json.h"

#include <atlantis/result.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::runtime::gate {

struct Applicability {
  std::vector<std::string> include;
  std::vector<std::string> exclude;
};

// `#` starts a comment; blank lines are ignored; `include:` and `exclude:` open the sections.
// An error names the line. At least one include is required.
[[nodiscard]] atlantis::Result<Applicability, std::string> parseApplicability(std::string_view text);
[[nodiscard]] atlantis::Result<Applicability, std::string> loadApplicability(const std::string& path);

// A glob over a '/'-separated repository path: `*` matches any run of characters except '/', `?` one
// character except '/', `**` any run of characters including '/', and `**/` zero or more directories.
[[nodiscard]] bool matchesGlob(std::string_view pattern, std::string_view path);

// `\` becomes `/` and a leading `./` is dropped.
[[nodiscard]] std::string normalizePath(std::string_view path);

struct AppliesResult {
  bool applies = false;
  std::vector<std::string> matched;  // the changed paths that apply, normalized, in input order
};
[[nodiscard]] AppliesResult evaluateApplicability(const Applicability& applicability,
                                                  std::span<const std::string> changedPaths);

// {"applies": bool, "matched": [...]}
[[nodiscard]] Value toValue(const AppliesResult& result);

}  // namespace atlantis::runtime::gate
