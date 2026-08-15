#pragma once

#include <filesystem>
#include <string>

namespace atlantis::tools::shader_compiler {

// One (source .slang file, vertex entry, fragment entry) shader PAIR --
// matching RHI's unchanged two-separate-ShaderStageBytecode-blobs
// contract (ADR-0025). `stampPath`'s own filename stem (e.g.
// "minimal_mesh" from "minimal_mesh.stamp") names the four final
// artifact files inside outputDir: <stem>.vert.spv, <stem>.vert.refl.json,
// <stem>.frag.spv, <stem>.frag.refl.json (Plan 0008 Section 7).
struct CompileAndValidateRequest {
  std::filesystem::path slangcPath;
  std::filesystem::path spirvValPath;
  std::filesystem::path sourcePath;
  std::string vertexEntry;
  std::string fragmentEntry;
  std::filesystem::path outputDir;
  std::string expectedContract;  // this round's only value: "minimal-renderer"
  std::filesystem::path stampPath;
};

// Runs Plan 0008 Section 5's full compile/reflect/validate algorithm
// (steps 1-13) for both stages of this shader pair, then Section 7's
// publish transaction (14a-14f) -- publishing all four final artifacts
// and the stamp only once every validation step has succeeded for both
// stages, with the stamp written strictly last. Returns 0 (and leaves
// the stamp written) only on full success; any failure returns nonzero,
// leaves no stamp, and leaves no partially-published final artifact
// behind. Diagnostics (slangc/spirv-val output, or this function's own
// failure description) are written to stderr for build-log visibility.
[[nodiscard]] int compileAndValidate(const CompileAndValidateRequest& request);

}  // namespace atlantis::tools::shader_compiler
